// Copyright Epic Games, Inc. All Rights Reserved.

#include "Actors/EHB_Railing.h"

#include "Actors/EHB_RailingGate.h"
#include "Actors/EHB_Stair.h"
#include "Components/EHBGeneratedMeshComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/SplineMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/EHBBuildingActorBase.h"
#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "MaterialDomain.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Sampling/EHBRailingMeshData.h"
#include "Settings/EHBBuildingToolsetSettings.h"
#include "UObject/ConstructorHelpers.h"

/*
 * 扶手生成的核心思路：
 * 1. 先把不同来源的路径统一成 Distance -> FEHBRailingPathSample。
 * 2. 再在这条一维路径上生成稳定的柱数据，门洞只是在距离区间上裁剪普通构件。
 * 3. 最后由柱数据驱动具体表现组件：HISM 柱、SplineMesh 横杆、EHB generated mesh 围栏板。
 *
 * 这样做的好处是：线性扶手、楼梯扶手、未来样条扶手都可以复用同一套柱/门洞/关系逻辑。
 */
namespace
{
	constexpr float EHBRailingDefaultPostWidth = 8.0f;
	constexpr float EHBRailingDefaultPostHeight = 100.0f;
	constexpr float EHBRailingDefaultPostSpacing = 120.0f;
	constexpr float EHBRailingDefaultRailHeight = 100.0f;
	constexpr float EHBRailingDefaultRailThickness = 8.0f;
	constexpr float EHBRailingDefaultMaxRailSegmentLength = 120.0f;
	constexpr float EHBRailingMinRailJointOverlap = 0.5f;
	constexpr float EHBRailingMaxRailJointOverlap = 20.0f;
	constexpr float EHBRailingEndpointConnectTolerance = 8.0f;
	constexpr float EHBRailingMinMiterAngleDegrees = 8.0f;
	constexpr float EHBRailingMaxMiterAngleDegrees = 172.0f;
	bool bEHBRailingRebuildConnectedGuard = false;

	TSoftObjectPtr<UStaticMesh> GetDefaultRailingStaticMesh()
	{
		return TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Engine/BasicShapes/Cube.Cube")));
	}

	float GetRailJointOverlap(float InRailThickness, float SegmentLength)
	{
		if (SegmentLength <= UE_SMALL_NUMBER)
		{
			return 0.0f;
		}

		const float DesiredOverlap = FMath::Clamp(
			InRailThickness * 0.5f,
			EHBRailingMinRailJointOverlap,
			EHBRailingMaxRailJointOverlap);
		return FMath::Min(DesiredOverlap, SegmentLength * 0.25f);
	}

	FBox GetSafeMeshBounds(UStaticMesh* StaticMesh)
	{
		if (StaticMesh)
		{
			const FBox Bounds = StaticMesh->GetBounds().GetBox();
			if (Bounds.IsValid)
			{
				return Bounds;
			}
		}
		return FBox(
			FVector(-50.0f),
			FVector(50.0f));
	}

	float GetSafeSourceSize(float SourceSize)
	{
		return FMath::Max(0.1f, FMath::Abs(SourceSize));
	}

	float GetHorizontalPanelWidth(const FVector& SourceSize)
	{
		return FMath::Max(FMath::Abs(SourceSize.X), FMath::Abs(SourceSize.Y));
	}

	bool IsPanelWidthAxisY(const FVector& SourceSize)
	{
		return FMath::Abs(SourceSize.Y) > FMath::Abs(SourceSize.X);
	}

	FRotator MakePanelSampleRotation(const FVector& Forward, bool bWidthAxisY)
	{
		const FVector SafeForward = FVector(Forward.X, Forward.Y, 0.0f)
			.GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
		return bWidthAxisY
			? FRotationMatrix::MakeFromYZ(SafeForward, FVector::UpVector).Rotator()
			: FRotationMatrix::MakeFromXZ(SafeForward, FVector::UpVector).Rotator();
	}

	const FEHBRailingMeshData* FindSampledRailingRow(const FDataTableRowHandle& RowHandle)
	{
		if (!RowHandle.DataTable
			|| RowHandle.RowName.IsNone()
			|| RowHandle.DataTable->GetRowStruct() != FEHBRailingMeshData::StaticStruct())
		{
			return nullptr;
		}

		return RowHandle.DataTable->FindRow<FEHBRailingMeshData>(
			RowHandle.RowName,
			TEXT("FindSampledRailingRow"),
			false);
	}

	// 默认使用 UE 基础 Cube，因此缩放时按 100cm 源尺寸换算。
	constexpr float EHBRailingMeshSourceSize = 100.0f;
	// 距离很近的自动柱和门洞边柱会合并，避免端点/台阶点重复生成两根柱。
	constexpr float EHBRailingDuplicateDistanceTolerance = 0.5f;
	// 门洞边缘留出很小容差：边柱要保留，开口内部才裁剪。
	constexpr float EHBRailingGateEdgeTolerance = 1.0f;

	UMaterialInterface* ResolveRailingMaterial(const TSoftObjectPtr<UMaterialInterface>& Material)
	{
		if (UMaterialInterface* LoadedMaterial = Material.LoadSynchronous())
		{
			return LoadedMaterial;
		}
		if (const UEHBBuildingToolsetSettings* Settings = GetDefault<UEHBBuildingToolsetSettings>())
		{
			if (UMaterialInterface* DefaultMaterial = Settings->DefaultWhiteBoxMaterial.LoadSynchronous())
			{
				return DefaultMaterial;
			}
		}
		return UMaterial::GetDefaultMaterial(MD_Surface);
	}

	UStaticMesh* ResolveRailingMesh(const TSoftObjectPtr<UStaticMesh>& Mesh)
	{
		return Mesh.LoadSynchronous();
	}

	FVector GetSafeLinearForward(const FVector& Start, const FVector& End)
	{
		return (End - Start).GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
	}

	FRotator MakePathRotation(const FVector& Forward)
	{
		return FRotationMatrix::MakeFromXZ(
			Forward.GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector),
			FVector::UpVector).Rotator();
	}

	FRotator MakeUprightPathRotation(const FVector& Forward)
	{
		const FVector HorizontalForward(Forward.X, Forward.Y, 0.0f);
		return FRotationMatrix::MakeFromXZ(
			HorizontalForward.GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector),
			FVector::UpVector).Rotator();
	}

void AppendPanelTriangle(
		TArray<FVector>& Vertices,
		TArray<int32>& Triangles,
		TArray<FVector>& Normals,
		TArray<FVector2D>& UVs,
		const FVector& InA,
		const FVector& InB,
		const FVector& InC,
		const FVector& DesiredNormal,
		const FVector2D& InUVA,
		const FVector2D& InUVB,
		const FVector2D& InUVC)
	{
		FVector A = InA;
		FVector B = InB;
		FVector C = InC;
		FVector2D UVA = InUVA;
		FVector2D UVB = InUVB;
		FVector2D UVC = InUVC;
		const FVector SafeNormal = DesiredNormal.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
		const FVector TriangleNormal = FVector::CrossProduct(B - A, C - A).GetSafeNormal();
		// Generated mesh visible winding is opposite the raw geometric cross product.
		if (!TriangleNormal.IsNearlyZero() && FVector::DotProduct(TriangleNormal, SafeNormal) > 0.0f)
		{
			Swap(B, C);
			Swap(UVB, UVC);
		}

		const int32 BaseIndex = Vertices.Num();
		Vertices.Append({ A, B, C });
		Triangles.Append({ BaseIndex, BaseIndex + 1, BaseIndex + 2 });
		Normals.Append({ SafeNormal, SafeNormal, SafeNormal });
		UVs.Append({ UVA, UVB, UVC });
	}

	void AppendPanelQuad(
		TArray<FVector>& Vertices,
		TArray<int32>& Triangles,
		TArray<FVector>& Normals,
		TArray<FVector2D>& UVs,
		const FVector& A,
		const FVector& B,
		const FVector& C,
		const FVector& D,
		const FVector& DesiredNormal,
		float U0,
		float U1,
		float V0,
		float V1)
	{
		const FVector2D UVA(U0, V0);
		const FVector2D UVB(U1, V0);
		const FVector2D UVC(U1, V1);
		const FVector2D UVD(U0, V1);
		AppendPanelTriangle(Vertices, Triangles, Normals, UVs, A, B, C, DesiredNormal, UVA, UVB, UVC);
		AppendPanelTriangle(Vertices, Triangles, Normals, UVs, A, C, D, DesiredNormal, UVA, UVC, UVD);
		AppendPanelTriangle(Vertices, Triangles, Normals, UVs, A, C, B, -DesiredNormal, UVA, UVC, UVB);
		AppendPanelTriangle(Vertices, Triangles, Normals, UVs, A, D, C, -DesiredNormal, UVA, UVD, UVC);
	}

	void AppendRailTriangle(
		TArray<FVector>& Vertices,
		TArray<int32>& Triangles,
		TArray<FVector>& Normals,
		TArray<FVector2D>& UVs,
		const FVector& InA,
		const FVector& InB,
		const FVector& InC,
		const FVector& DesiredNormal,
		float U0,
		float U1)
	{
		FVector A = InA;
		FVector B = InB;
		FVector C = InC;
		const FVector SafeNormal = DesiredNormal.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
		const FVector TriangleNormal = FVector::CrossProduct(B - A, C - A).GetSafeNormal();
		if (!TriangleNormal.IsNearlyZero() && FVector::DotProduct(TriangleNormal, SafeNormal) > 0.0f)
		{
			Swap(B, C);
		}

		const int32 BaseIndex = Vertices.Num();
		Vertices.Append({ A, B, C });
		Triangles.Append({ BaseIndex, BaseIndex + 1, BaseIndex + 2 });
		Normals.Append({ SafeNormal, SafeNormal, SafeNormal });
		UVs.Append({
			FVector2D(U0, 0.0f),
			FVector2D(U1, 0.0f),
			FVector2D(U1, 1.0f)
		});
	}

	void AppendRailQuad(
		TArray<FVector>& Vertices,
		TArray<int32>& Triangles,
		TArray<FVector>& Normals,
		TArray<FVector2D>& UVs,
		const FVector& A,
		const FVector& B,
		const FVector& C,
		const FVector& D,
		const FVector& DesiredNormal,
		float U0,
		float U1)
	{
		AppendRailTriangle(Vertices, Triangles, Normals, UVs, A, B, C, DesiredNormal, U0, U1);
		AppendRailTriangle(Vertices, Triangles, Normals, UVs, A, C, D, DesiredNormal, U0, U1);
	}

	void BuildRailCrossSection(
		const FVector& Center,
		const FVector& Forward,
		const FVector& Right,
		float HalfWidth,
		float HalfHeight,
		FVector OutCorners[4])
	{
		const FVector SafeForward = Forward.GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
		FVector SafeUp = FVector::UpVector;
		if (FMath::Abs(FVector::DotProduct(SafeUp, SafeForward)) > 0.98f)
		{
			SafeUp = FVector::CrossProduct(SafeForward, FVector::RightVector)
				.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
		}
		FVector SafeRight = (Right - SafeUp * FVector::DotProduct(Right, SafeUp)).GetSafeNormal();
		if (SafeRight.IsNearlyZero())
		{
			SafeRight = FVector::CrossProduct(SafeUp, SafeForward)
				.GetSafeNormal(UE_SMALL_NUMBER, FVector::RightVector);
		}

		OutCorners[0] = Center - SafeRight * HalfWidth - SafeUp * HalfHeight;
		OutCorners[1] = Center + SafeRight * HalfWidth - SafeUp * HalfHeight;
		OutCorners[2] = Center + SafeRight * HalfWidth + SafeUp * HalfHeight;
		OutCorners[3] = Center - SafeRight * HalfWidth + SafeUp * HalfHeight;
	}

	void AppendRailSweepSegment(
		TArray<FVector>& Vertices,
		TArray<int32>& Triangles,
		TArray<FVector>& Normals,
		TArray<FVector2D>& UVs,
		const FVector& Start,
		const FVector& StartForward,
		const FVector& StartRight,
		const FVector& End,
		const FVector& EndForward,
		const FVector& EndRight,
		float HalfWidth,
		float HalfHeight,
		float U0,
		float U1,
		const FPlane* StartCut = nullptr)
	{
		FVector StartCorners[4];
		FVector EndCorners[4];
		BuildRailCrossSection(Start, StartForward, StartRight, HalfWidth, HalfHeight, StartCorners);
		BuildRailCrossSection(End, EndForward, EndRight, HalfWidth, HalfHeight, EndCorners);

		bool bClipped = false;
		if (StartCut)
		{
			for (int32 I = 0; I < 4; ++I)
			{
				const double A = StartCut->PlaneDot(StartCorners[I]);
				const double B = StartCut->PlaneDot(EndCorners[I]);
				if (B <= 0) return; // Never emit a rail segment behind its persisted cut.
				if (A < 0)
				{
					StartCorners[I] = FMath::Lerp(StartCorners[I], EndCorners[I], A / (A-B));
					bClipped = true;
				}
			}
		}
		if (bClipped)
			AppendRailQuad(Vertices, Triangles, Normals, UVs, StartCorners[0], StartCorners[1], StartCorners[2], StartCorners[3], -StartCut->GetNormal(), U0, U0);

		for (int32 Index = 0; Index < 4; ++Index)
		{
			const int32 NextIndex = (Index + 1) % 4;
			const FVector StartFaceCenter = (StartCorners[Index] + StartCorners[NextIndex]) * 0.5f;
			const FVector EndFaceCenter = (EndCorners[Index] + EndCorners[NextIndex]) * 0.5f;
			FVector FaceNormal = ((StartFaceCenter - Start) + (EndFaceCenter - End)).GetSafeNormal(
				UE_SMALL_NUMBER,
				FVector::UpVector);
			if (bClipped)
			{
				FVector GeometricNormal = FVector::CrossProduct(EndCorners[Index]-StartCorners[Index], StartCorners[NextIndex]-StartCorners[Index]).GetSafeNormal();
				if (FVector::DotProduct(GeometricNormal, FaceNormal) < 0) GeometricNormal *= -1;
				FaceNormal = GeometricNormal;
			}
			AppendRailQuad(
				Vertices,
				Triangles,
				Normals,
				UVs,
				StartCorners[Index],
				EndCorners[Index],
				EndCorners[NextIndex],
				StartCorners[NextIndex],
				FaceNormal,
				U0,
				U1);
		}
	}
}

AEHB_Railing::AEHB_Railing()
{
	PrimaryActorTick.bCanEverTick = false;
	ElementType = EEHBBuildingElementType::Railing;
	FloorRole = EEHBBuildingFloorElementRole::Railing;
	// 扶手需要依附到其他结构上，但自己不提供 CanSupport，也不参与房间边界。
	ElementCapabilities =
		static_cast<int32>(EEHBElementCapability::BoundaryAccessory)
		| static_cast<int32>(EEHBElementCapability::RequiresSupport);
	SemanticTags.AddUnique(TEXT("Railing.Assembly"));

	PostMeshComponent = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("RailingPosts"));
	PostMeshComponent->SetupAttachment(SceneRoot);
	PostMeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	PostMeshComponent->SetCollisionObjectType(ECC_WorldStatic);
	PostMeshComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
	PostMeshComponent->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

	PanelMeshComponent = CreateDefaultSubobject<UEHBGeneratedMeshComponent>(TEXT("RailingPanel"));
	PanelMeshComponent->SetupAttachment(SceneRoot);
	PanelMeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	PanelMeshComponent->SetCollisionObjectType(ECC_WorldStatic);
	PanelMeshComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
	PanelMeshComponent->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

	PanelSampleMeshComponent = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("RailingSamplePanels"));
	PanelSampleMeshComponent->SetupAttachment(SceneRoot);
	PanelSampleMeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	PanelSampleMeshComponent->SetCollisionObjectType(ECC_WorldStatic);
	PanelSampleMeshComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
	PanelSampleMeshComponent->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

	RailGeneratedMeshComponent = CreateDefaultSubobject<UEHBGeneratedMeshComponent>(TEXT("RailingRailMesh"));
	RailGeneratedMeshComponent->SetupAttachment(SceneRoot);
	RailGeneratedMeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	RailGeneratedMeshComponent->SetCollisionObjectType(ECC_WorldStatic);
	RailGeneratedMeshComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
	RailGeneratedMeshComponent->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		PostMesh = CubeMesh.Object;
		RailMesh = CubeMesh.Object;
		PostMeshComponent->SetStaticMesh(CubeMesh.Object);
	}
}

void AEHB_Railing::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RebuildRailing();
}

void AEHB_Railing::Destroyed()
{
	ClearRailingRelations();
	Super::Destroyed();
}

#if WITH_EDITOR
void AEHB_Railing::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	RebuildRailing();
	NotifyElementGeometryChanged(true);
}
#endif

bool AEHB_Railing::ConfigureLinear(
	AEHBBuildingActorBase* InBuilding,
	const FTransform& LocalTransform,
	const FVector& Start,
	const FVector& End,
	int32 InFloorIndex)
{
	if (!InBuilding)
	{
		return false;
	}

	PathMode = EEHBRailingPathMode::Linear;
	HostedStair = nullptr;
	HostedStairGuid.Invalidate();
	LinearStart = Start;
	LinearEnd = End;
	AttachToBuilding(InBuilding, LocalTransform);
	SetFloorAssignment(FMath::Max(1, InFloorIndex), EEHBBuildingFloorElementRole::Railing);
	return RebuildRailing();
}

bool AEHB_Railing::ConfigureOnStair(
	AEHBBuildingActorBase* InBuilding,
	AEHB_Stair* Stair,
	EEHBRailingSide Side,
	int32 InFloorIndex)
{
	if (!InBuilding || !Stair)
	{
		return false;
	}

	Stair->EnsureElementGuid();
	PathMode = EEHBRailingPathMode::StairHosted;
	HostedStair = Stair;
	HostedStairGuid = Stair->ElementGuid;
	RailingSide = Side;
	// 楼梯扶手默认按踏步对齐柱子，视觉上比固定距离更稳定；仍可在属性面板改回距离模式。
	PostSpacingMode = EEHBRailingPostSpacingMode::StepAligned;
	AttachToBuilding(InBuilding, Stair->GetElementLocalTransform());
	SetFloorAssignment(FMath::Max(1, InFloorIndex), EEHBBuildingFloorElementRole::Railing);
	return RebuildRailing();
}

bool AEHB_Railing::RebuildRailing()
{
	// 重建入口统一夹紧参数，防止编辑器输入 0 或负数导致组件缩放/采样失败。
	PostSpacing = FMath::Max(1.0f, PostSpacing);
	StepsPerPost = FMath::Max(1, StepsPerPost);
	PostWidth = FMath::Max(0.1f, PostWidth);
	PostHeight = FMath::Max(1.0f, PostHeight);
	RailHeight = FMath::Max(1.0f, RailHeight);
	RailThickness = FMath::Max(0.1f, RailThickness);
	MaxRailSegmentLength = FMath::Max(1.0f, MaxRailSegmentLength);
	PanelTopOffset = FMath::Max(PanelBottomOffset + 1.0f, PanelTopOffset);

	// 门洞先规范化，再生成柱。这样门洞边柱和普通柱可以在同一轮里去重。
	NormalizeGateConnections();
	if (!BuildPostData(GeneratedPosts))
	{
		GeneratedPosts.Reset();
	}

	RebuildPostInstances();
	RebuildPostOverrideMeshes();
	RebuildRailMeshes();
	RebuildPanelMesh();
	ApplyMaterials();
	// 表现组件重建后，同步刷新非承重关系：楼梯托管、端点吸附、围栏门托管。
	RefreshRailingRelations();
	if (!bEHBRailingRebuildConnectedGuard)
	{
		TGuardValue<bool> ConnectedRebuildGuard(bEHBRailingRebuildConnectedGuard, true);
		RebuildConnectedRailings(false);
	}
	MarkPackageDirty();
	return !GeneratedPosts.IsEmpty();
}

int32 AEHB_Railing::RebuildConnectedRailings(bool bModifyConnectedRailings)
{
	if (!OwningBuilding)
	{
		return 0;
	}

	const bool bSetConnectedGuard = !bEHBRailingRebuildConnectedGuard;
	if (bSetConnectedGuard)
	{
		bEHBRailingRebuildConnectedGuard = true;
	}

	TSet<AEHB_Railing*> ConnectedRailings;
	const FVector StartEndpointWorld = GetRailingEndpointWorldLocation(true);
	const FVector EndEndpointWorld = GetRailingEndpointWorldLocation(false);

	FEHBElementQuery Query;
	Query.ElementTypes = { EEHBBuildingElementType::Railing };
	for (AEHBElementActorBase* Element : OwningBuilding->QueryElements(Query))
	{
		AEHB_Railing* OtherRailing = Cast<AEHB_Railing>(Element);
		if (!OtherRailing || OtherRailing == this || OtherRailing->IsActorBeingDestroyed())
		{
			continue;
		}

		if (IsEndpointNearRailingEndpoint(OtherRailing, StartEndpointWorld)
			|| IsEndpointNearRailingEndpoint(OtherRailing, EndEndpointWorld))
		{
			ConnectedRailings.Add(OtherRailing);
		}
	}

	int32 RebuiltCount = 0;
	for (AEHB_Railing* ConnectedRailing : ConnectedRailings)
	{
		if (!ConnectedRailing)
		{
			continue;
		}

		if (bModifyConnectedRailings)
		{
			ConnectedRailing->Modify();
		}
		if (ConnectedRailing->RebuildRailing())
		{
			++RebuiltCount;
		}
	}

	if (bSetConnectedGuard)
	{
		bEHBRailingRebuildConnectedGuard = false;
	}
	return RebuiltCount;
}

float AEHB_Railing::GetRailMeshTopOffset() const
{
	UStaticMesh* LoadedRailMesh = RailMesh.Get();
	if (!LoadedRailMesh)
	{
		for (const TObjectPtr<USplineMeshComponent>& RailMeshComponent : RailMeshComponents)
		{
			if (RailMeshComponent && RailMeshComponent->GetStaticMesh())
			{
				LoadedRailMesh = RailMeshComponent->GetStaticMesh();
				break;
			}
		}
	}
	if (!LoadedRailMesh)
	{
		LoadedRailMesh = ResolveRailingMesh(RailMesh);
	}
	const FBox RailSourceBounds = GetSafeMeshBounds(LoadedRailMesh);
	const FVector RailSourceSize = RailSourceBounds.GetSize();
	const float RailCrossScale = RailThickness / GetSafeSourceSize(FMath::Max(RailSourceSize.Y, RailSourceSize.Z));
	return FMath::Max(0.0f, (RailSourceBounds.Max.Z - RailSourceBounds.GetCenter().Z) * RailCrossScale);
}

float AEHB_Railing::GetRailTopHeight() const
{
	return FMath::Max(1.0f, RailHeight) + GetRailMeshTopOffset();
}

void AEHB_Railing::SetRailTopHeight(float InTopHeight)
{
	RailHeight = FMath::Max(1.0f, InTopHeight - GetRailMeshTopOffset());
}

bool AEHB_Railing::ConfigureFromSampledRailingRow(UDataTable* InTable, FName InRowName, bool bFinished)
{
	if (!InTable || InRowName.IsNone() || InTable->GetRowStruct() != FEHBRailingMeshData::StaticStruct())
	{
		return false;
	}

	const FEHBRailingMeshData* Row = InTable->FindRow<FEHBRailingMeshData>(
		InRowName,
		TEXT("AEHB_Railing::ConfigureFromSampledRailingRow"),
		false);
	if (!Row)
	{
		return false;
	}
	const bool bHasSampledPost = !Row->PostMesh.SourceStaticMesh.IsNull();
	const bool bHasSampledRail = !Row->RailMesh.SourceStaticMesh.IsNull();
	if (!bHasSampledPost && !bHasSampledRail)
	{
		return false;
	}

	Modify();
	PostMeshOverrides.Reset();
	TrimPostOverrideMeshComponents(0);
	SampledRailingRow.DataTable = InTable;
	SampledRailingRow.RowName = InRowName;
	PostMesh = bHasSampledPost ? Row->PostMesh.SourceStaticMesh : GetDefaultRailingStaticMesh();
	RailMesh = bHasSampledRail ? Row->RailMesh.SourceStaticMesh : GetDefaultRailingStaticMesh();
	FillMode = Row->FillMode;
	PostWidth = bHasSampledPost ? FMath::Max(0.1f, Row->RecommendedPostWidth) : EHBRailingDefaultPostWidth;
	PostHeight = bHasSampledPost ? FMath::Max(1.0f, Row->RecommendedPostHeight) : EHBRailingDefaultPostHeight;
	PostSpacing = FMath::Max(1.0f, Row->RecommendedPostSpacing);
	RailHeight = bHasSampledRail ? FMath::Max(1.0f, Row->RecommendedRailHeight) : EHBRailingDefaultRailHeight;
	RailThickness = bHasSampledRail ? FMath::Max(0.1f, Row->RecommendedRailThickness) : EHBRailingDefaultRailThickness;
	MaxRailSegmentLength = bHasSampledRail ? FMath::Max(1.0f, Row->RecommendedMaxRailSegmentLength) : EHBRailingDefaultMaxRailSegmentLength;
	if (PathMode == EEHBRailingPathMode::StairHosted)
	{
		const AEHB_Stair* Stair = HostedStair;
		if (!Stair && OwningBuilding && HostedStairGuid.IsValid())
		{
			Stair = Cast<AEHB_Stair>(OwningBuilding->FindElementActorByGuid(HostedStairGuid));
		}
		if (Stair)
		{
			const float TreadDepth = Stair->StairData.bUseActualDimensions
				? Stair->StairData.TreadDepth
				: Stair->StairData.DefaultTreadDepth;
			MaxRailSegmentLength = FMath::Min(MaxRailSegmentLength, FMath::Max(4.0f, TreadDepth * 0.5f));
		}
	}
	PanelBottomOffset = FMath::Max(0.0f, Row->RecommendedPanelBottomOffset);
	PanelTopOffset = FMath::Max(PanelBottomOffset + 1.0f, Row->RecommendedPanelTopOffset);
	PostMaterial = bHasSampledPost ? Row->PostMaterial : TSoftObjectPtr<UMaterialInterface>();
	RailMaterial = bHasSampledRail ? Row->RailMaterial : TSoftObjectPtr<UMaterialInterface>();
	PanelMaterial = Row->PanelMaterial;

	const bool bRebuilt = RebuildRailing();
	if (bFinished)
	{
		NotifyElementGeometryChanged(true);
	}
	return bRebuilt;
}

float AEHB_Railing::GetRailingLength() const
{
	if (PathMode == EEHBRailingPathMode::StairHosted)
	{
		const AEHB_Stair* Stair = HostedStair;
		if (!Stair && OwningBuilding && HostedStairGuid.IsValid())
		{
			Stair = Cast<AEHB_Stair>(OwningBuilding->FindElementActorByGuid(HostedStairGuid));
		}
		return Stair ? Stair->GetStairLength() : 0.0f;
	}

	return FVector::Dist(LinearStart, LinearEnd);
}

bool AEHB_Railing::EvaluatePathAtDistance(float Distance, FEHBRailingPathSample& OutSample) const
{
	OutSample = FEHBRailingPathSample();
	const float Length = GetRailingLength();
	if (Length <= UE_SMALL_NUMBER)
	{
		return false;
	}

	const float ClampedDistance = FMath::Clamp(Distance, 0.0f, Length);
	if (PathMode == EEHBRailingPathMode::StairHosted)
	{
		const AEHB_Stair* Stair = HostedStair;
		if (!Stair && OwningBuilding && HostedStairGuid.IsValid())
		{
			Stair = Cast<AEHB_Stair>(OwningBuilding->FindElementActorByGuid(HostedStairGuid));
		}
		if (!Stair)
		{
			return false;
		}

		FEHBStairPathSample StairSample;
		if (!Stair->SamplePathForRailing(
			RailingSide,
			ClampedDistance,
			StairSideOffset,
			StairPostBaseHeightOffset,
			StairSample))
		{
			return false;
		}

		const FTransform StairToWorld = Stair->GetActorTransform();
		const FTransform WorldToRailing = GetActorTransform().Inverse();
		// 楼梯返回的是楼梯局部空间，扶手生成组件使用扶手局部空间，因此中间经过世界空间转换。
		OutSample.Distance = StairSample.Distance;
		OutSample.StepIndex = StairSample.StepIndex;
		OutSample.LocalLocation = WorldToRailing.TransformPosition(StairToWorld.TransformPosition(StairSample.LocalLocation));
		OutSample.LocalForward = WorldToRailing.TransformVectorNoScale(StairToWorld.TransformVectorNoScale(StairSample.LocalForward))
			.GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
		OutSample.LocalRight = WorldToRailing.TransformVectorNoScale(StairToWorld.TransformVectorNoScale(StairSample.LocalRight))
			.GetSafeNormal(UE_SMALL_NUMBER, FVector::RightVector);
		return true;
	}

	const FVector Forward = GetSafeLinearForward(LinearStart, LinearEnd);
	OutSample.Distance = ClampedDistance;
	OutSample.LocalLocation = LinearStart + Forward * ClampedDistance;
	OutSample.LocalForward = Forward;
	OutSample.LocalRight = FVector::CrossProduct(FVector::UpVector, Forward).GetSafeNormal(UE_SMALL_NUMBER, FVector::RightVector);
	return true;
}

bool AEHB_Railing::EvaluateRailPathAtDistance(float Distance, FEHBRailingPathSample& OutSample) const
{
	OutSample = FEHBRailingPathSample();
	if (PathMode != EEHBRailingPathMode::StairHosted)
	{
		return EvaluatePathAtDistance(Distance, OutSample);
	}

	const float Length = GetRailingLength();
	if (Length <= UE_SMALL_NUMBER)
	{
		return false;
	}

	const AEHB_Stair* Stair = HostedStair;
	if (!Stair && OwningBuilding && HostedStairGuid.IsValid())
	{
		Stair = Cast<AEHB_Stair>(OwningBuilding->FindElementActorByGuid(HostedStairGuid));
	}
	if (!Stair)
	{
		return false;
	}

	const float ClampedDistance = FMath::Clamp(Distance, 0.0f, Length);
	FEHBStairPathSample StairSample;
	if (!Stair->SampleRailPathForRailing(
		RailingSide,
		ClampedDistance,
		StairSideOffset,
		StairPostBaseHeightOffset,
		StairSample))
	{
		return false;
	}

	const FTransform StairToWorld = Stair->GetActorTransform();
	const FTransform WorldToRailing = GetActorTransform().Inverse();
	OutSample.Distance = StairSample.Distance;
	OutSample.StepIndex = StairSample.StepIndex;
	OutSample.LocalLocation = WorldToRailing.TransformPosition(StairToWorld.TransformPosition(StairSample.LocalLocation));
	OutSample.LocalForward = WorldToRailing.TransformVectorNoScale(StairToWorld.TransformVectorNoScale(StairSample.LocalForward))
		.GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
	OutSample.LocalRight = WorldToRailing.TransformVectorNoScale(StairToWorld.TransformVectorNoScale(StairSample.LocalRight))
		.GetSafeNormal(UE_SMALL_NUMBER, FVector::RightVector);
	return true;
}

bool AEHB_Railing::FindPostByGuid(FGuid PostGuid, FEHBRailingPost& OutPost) const
{
	OutPost = FEHBRailingPost();
	if (!PostGuid.IsValid())
	{
		return false;
	}

	for (const FEHBRailingPost& Post : GeneratedPosts)
	{
		if (Post.PostGuid == PostGuid)
		{
			OutPost = Post;
			return true;
		}
	}
	return false;
}

bool AEHB_Railing::GetPostGuidForInstanceIndex(int32 InstanceIndex, FGuid& OutPostGuid) const
{
	OutPostGuid.Invalidate();
	if (!PostInstanceGuids.IsValidIndex(InstanceIndex))
	{
		return false;
	}

	OutPostGuid = PostInstanceGuids[InstanceIndex];
	return OutPostGuid.IsValid();
}

bool AEHB_Railing::GetPostGuidForOverrideComponent(const UActorComponent* Component, FGuid& OutPostGuid) const
{
	OutPostGuid.Invalidate();
	if (!Component)
	{
		return false;
	}

	for (int32 Index = 0; Index < PostOverrideMeshComponents.Num(); ++Index)
	{
		if (PostOverrideMeshComponents[Index] == Component && PostOverrideComponentGuids.IsValidIndex(Index))
		{
			OutPostGuid = PostOverrideComponentGuids[Index];
			return OutPostGuid.IsValid();
		}
	}

	return false;
}

bool AEHB_Railing::ApplyPostMeshSampleToPost(FGuid PostGuid, UDataTable* InTable, FName InRowName, bool bFinished)
{
	if (!PostGuid.IsValid()
		|| !InTable
		|| InRowName.IsNone()
		|| InTable->GetRowStruct() != FEHBRailingMeshData::StaticStruct())
	{
		return false;
	}

	FEHBRailingPost TargetPost;
	if (!FindPostByGuid(PostGuid, TargetPost) || TargetPost.bSuppressInstance)
	{
		return false;
	}

	const FEHBRailingMeshData* Row = InTable->FindRow<FEHBRailingMeshData>(
		InRowName,
		TEXT("AEHB_Railing::ApplyPostMeshSampleToPost"),
		false);
	if (!Row || Row->PostMesh.SourceStaticMesh.IsNull())
	{
		return false;
	}

	Modify();
	PostMeshOverrides.RemoveAll(
		[PostGuid](const FEHBRailingPostMeshOverride& Override)
		{
			return Override.PostGuid == PostGuid;
		});

	FEHBRailingPostMeshOverride& Override = PostMeshOverrides.AddDefaulted_GetRef();
	Override.PostGuid = PostGuid;
	Override.SampledRailingRow.DataTable = InTable;
	Override.SampledRailingRow.RowName = InRowName;
	Override.PostMesh = Row->PostMesh.SourceStaticMesh;
	Override.PostMaterial = Row->PostMaterial;
	Override.PostWidth = FMath::Max(0.1f, Row->RecommendedPostWidth);
	Override.PostHeight = FMath::Max(1.0f, Row->RecommendedPostHeight);

	const bool bRebuilt = RebuildRailing();
	if (bFinished)
	{
		NotifyElementGeometryChanged(true);
	}
	return bRebuilt;
}

bool AEHB_Railing::ApplyPostMeshSampleToAllPosts(UDataTable* InTable, FName InRowName, bool bFinished)
{
	if (!InTable
		|| InRowName.IsNone()
		|| InTable->GetRowStruct() != FEHBRailingMeshData::StaticStruct())
	{
		return false;
	}

	const FEHBRailingMeshData* Row = InTable->FindRow<FEHBRailingMeshData>(
		InRowName,
		TEXT("AEHB_Railing::ApplyPostMeshSampleToAllPosts"),
		false);
	if (!Row || Row->PostMesh.SourceStaticMesh.IsNull())
	{
		return false;
	}

	Modify();
	PostMeshOverrides.Reset();
	TrimPostOverrideMeshComponents(0);
	PostMesh = Row->PostMesh.SourceStaticMesh;
	PostMaterial = Row->PostMaterial;
	PostWidth = FMath::Max(0.1f, Row->RecommendedPostWidth);
	PostHeight = FMath::Max(1.0f, Row->RecommendedPostHeight);

	const bool bRebuilt = RebuildRailing();
	if (bFinished)
	{
		NotifyElementGeometryChanged(true);
	}
	return bRebuilt;
}

bool AEHB_Railing::AddOrUpdateGateConnection(
	AEHB_RailingGate* Gate,
	float DistanceFromStart,
	float Width,
	float Height,
	EEHBRailingGateHingeSide HingeSide)
{
	if (!Gate)
	{
		return false;
	}

	Gate->EnsureElementGuid();
	const float Length = GetRailingLength();
	if (Length <= UE_SMALL_NUMBER)
	{
		return false;
	}

	const float SafeWidth = FMath::Clamp(FMath::Max(1.0f, Width), 1.0f, Length);
	const float SafeDistance = FMath::Clamp(DistanceFromStart, SafeWidth * 0.5f, Length - SafeWidth * 0.5f);
	FEHBRailingPathSample GateSample;
	if (!EvaluatePathAtDistance(SafeDistance, GateSample))
	{
		return false;
	}

	FEHBRailingGateConnection* ExistingConnection = GateConnections.FindByPredicate(
		[Gate](const FEHBRailingGateConnection& Connection)
		{
			return Connection.GateGuid == Gate->ElementGuid;
		});

	FEHBRailingGateConnection& Connection = ExistingConnection
		? *ExistingConnection
		: GateConnections.AddDefaulted_GetRef();
	Connection.GateGuid = Gate->ElementGuid;
	Connection.DistanceFromStart = SafeDistance;
	Connection.Width = SafeWidth;
	Connection.Height = FMath::Max(1.0f, Height);
	Connection.HingeSide = HingeSide;
	// 门洞边柱 Guid 由扶手保存，而不是跟随门 Actor 重算，保证门移动/重建后端点引用稳定。
	if (!Connection.LeftPostGuid.IsValid())
	{
		Connection.LeftPostGuid = FGuid::NewGuid();
	}
	if (!Connection.RightPostGuid.IsValid())
	{
		Connection.RightPostGuid = FGuid::NewGuid();
	}
	Connection.GateLocalToRailing = FTransform(
		MakePathRotation(GateSample.LocalForward),
		GateSample.LocalLocation,
		FVector::OneVector);

	RebuildRailing();
	return true;
}

bool AEHB_Railing::RemoveGateConnection(FGuid GateGuid)
{
	if (!GateGuid.IsValid())
	{
		return false;
	}

	const int32 RemovedCount = GateConnections.RemoveAll(
		[GateGuid](const FEHBRailingGateConnection& Connection)
		{
			return Connection.GateGuid == GateGuid;
		});
	if (RemovedCount <= 0)
	{
		return false;
	}

	RebuildRailing();
	return true;
}

void AEHB_Railing::RefreshRailingRelations()
{
	// Rebuilding (including Undo reconstruction) must not change the identity of
	// an unchanged anchor/host/gate relation. Removed endpoints still disappear.
	TArray<FEHBElementRelation> PreviousRelations;
	if (OwningBuilding)
	{
		for (const auto& Relation : OwningBuilding->ElementRelations)
			if (RailingRelationGuids.Contains(Relation.RelationGuid)) PreviousRelations.Add(Relation);
	}
	ClearRailingRelations();
	if (!OwningBuilding || !ElementGuid.IsValid())
	{
		return;
	}

	auto AddRelation = [this, &PreviousRelations](const FEHBElementRelation& Relation)
	{
		FEHBElementRelation Updated = Relation;
		if (const auto* Previous = PreviousRelations.FindByPredicate([&](const auto& R) { return R.IsEquivalentTo(Relation); }))
			Updated.RelationGuid = Previous->RelationGuid;
		const FGuid RelationGuid = OwningBuilding->AddOrUpdateElementRelation(Updated, true);
		if (RelationGuid.IsValid())
		{
			RailingRelationGuids.AddUnique(RelationGuid);
		}
	};

	AEHB_Stair* Stair = HostedStair;
	if (!Stair && HostedStairGuid.IsValid())
	{
		Stair = Cast<AEHB_Stair>(OwningBuilding->FindElementActorByGuid(HostedStairGuid));
	}
	if (PathMode == EEHBRailingPathMode::StairHosted && Stair && Stair->ElementGuid.IsValid())
	{
		// 楼梯托管关系是几何依赖关系：楼梯路径变化后，扶手需要重建。
		FEHBElementRelation Relation;
		Relation.Type = EEHBElementRelationType::HostedElement;
		Relation.Source = FEHBElementRelationEndpoint::MakeElement(Stair->ElementGuid, EEHBElementSurfaceKind::Perimeter, TEXT("Stair.RailingHost"));
		Relation.Target = FEHBElementRelationEndpoint::MakeElement(ElementGuid, EEHBElementSurfaceKind::WholeElement, TEXT("Railing.Assembly"));
		Relation.Origin = EEHBRelationOrigin::SystemGenerated;
		Relation.bGeometryDependent = true;
		Relation.bAffectsFloorAssignment = false;
		Relation.TargetRelativeToSource = GetActorTransform().GetRelativeTransform(Stair->GetActorTransform());
		AddRelation(Relation);
	}

	if (StartAnchor.ElementGuid.IsValid())
	{
		// 端点吸附只描述边界连接，不表示承重；扶手不会支撑其他主体结构。
		FEHBElementRelation Relation;
		Relation.Type = EEHBElementRelationType::BoundaryAttachment;
		Relation.Source = FEHBElementRelationEndpoint::MakeElement(StartAnchor.ElementGuid, EEHBElementSurfaceKind::Custom, TEXT("Railing.Anchor"));
		Relation.Target = FEHBElementRelationEndpoint::MakeElement(ElementGuid, EEHBElementSurfaceKind::Start, TEXT("Railing.Start"));
		Relation.Origin = EEHBRelationOrigin::UserAuthored;
		Relation.bGeometryDependent = true;
		Relation.bAffectsFloorAssignment = false;
		AddRelation(Relation);
	}

	if (EndAnchor.ElementGuid.IsValid())
	{
		FEHBElementRelation Relation;
		Relation.Type = EEHBElementRelationType::BoundaryAttachment;
		Relation.Source = FEHBElementRelationEndpoint::MakeElement(EndAnchor.ElementGuid, EEHBElementSurfaceKind::Custom, TEXT("Railing.Anchor"));
		Relation.Target = FEHBElementRelationEndpoint::MakeElement(ElementGuid, EEHBElementSurfaceKind::End, TEXT("Railing.End"));
		Relation.Origin = EEHBRelationOrigin::UserAuthored;
		Relation.bGeometryDependent = true;
		Relation.bAffectsFloorAssignment = false;
		AddRelation(Relation);
	}

	for (const FEHBRailingGateConnection& Connection : GateConnections)
	{
		if (!Connection.GateGuid.IsValid())
		{
			continue;
		}

		// 围栏门挂在扶手的 Opening 上，门洞的裁剪数据仍由 GateConnections 保存。
		FEHBElementRelation Relation;
		Relation.Type = EEHBElementRelationType::HostedElement;
		Relation.Source = FEHBElementRelationEndpoint::MakeElement(ElementGuid, EEHBElementSurfaceKind::Opening, TEXT("Railing.GateOpening"));
		Relation.Target = FEHBElementRelationEndpoint::MakeElement(Connection.GateGuid, EEHBElementSurfaceKind::WholeElement, TEXT("RailingGate"));
		Relation.Origin = EEHBRelationOrigin::SystemGenerated;
		Relation.bGeometryDependent = true;
		Relation.bAffectsFloorAssignment = false;
		Relation.TargetRelativeToSource = Connection.GateLocalToRailing;
		Relation.NumericMetadata.Add(TEXT("DistanceFromStart"), Connection.DistanceFromStart);
		Relation.NumericMetadata.Add(TEXT("Width"), Connection.Width);
		AddRelation(Relation);
	}
}

void AEHB_Railing::ClearRailingRelations()
{
	if (!OwningBuilding)
	{
		RailingRelationGuids.Reset();
		return;
	}

	for (const FGuid& RelationGuid : RailingRelationGuids)
	{
		OwningBuilding->RemoveElementRelation(RelationGuid);
	}
	RailingRelationGuids.Reset();
}

bool AEHB_Railing::BuildPostData(TArray<FEHBRailingPost>& OutPosts)
{
	const TArray<FEHBRailingPost> PreviousPosts = GeneratedPosts;
	OutPosts.Reset();

	// Migrate endpoint roles before changing geometry. The old end distance may
	// differ from the new length; nearest-distance matching would lose its ID.
	if (PathMode != EEHBRailingPathMode::StairHosted)
	{
		const FEHBRailingPost* PreviousStart = nullptr;
		const FEHBRailingPost* PreviousEnd = nullptr;
		for (const FEHBRailingPost& Post : PreviousPosts)
		{
			if (!Post.bExplicit || Post.bGatePost || !Post.PostGuid.IsValid()) continue;
			if (!PreviousStart || Post.Distance < PreviousStart->Distance) PreviousStart = &Post;
			if (!PreviousEnd || Post.Distance > PreviousEnd->Distance) PreviousEnd = &Post;
		}
		if (!LinearStartPostGuid.IsValid())
			LinearStartPostGuid = PreviousStart && FMath::IsNearlyZero(PreviousStart->Distance)
				? PreviousStart->PostGuid : FGuid::NewGuid();
		if (!LinearEndPostGuid.IsValid())
			LinearEndPostGuid = PreviousEnd && PreviousEnd->Distance > UE_SMALL_NUMBER
				? PreviousEnd->PostGuid : FGuid::NewGuid();
	}
	TSet<FGuid> UsedPostGuids;

	// 自动重建时尽量复用原有 Guid，避免外部引用、实例点击选择和门洞边柱绑定频繁失效。
	auto FindPreviousGuid = [this, &PreviousPosts, &UsedPostGuids](float Distance, bool bGatePost, FGuid PreferredGuid)
	{
		if (PreferredGuid.IsValid())
		{
			UsedPostGuids.Add(PreferredGuid);
			return PreferredGuid;
		}

		for (const FEHBRailingPost& PreviousPost : PreviousPosts)
		{
			if (PreviousPost.bGatePost == bGatePost
				&& FMath::Abs(PreviousPost.Distance - Distance) <= 1.0f
				&& PreviousPost.PostGuid.IsValid()
				&& !UsedPostGuids.Contains(PreviousPost.PostGuid)
				&& (PathMode == EEHBRailingPathMode::StairHosted
					|| (PreviousPost.PostGuid != LinearStartPostGuid && PreviousPost.PostGuid != LinearEndPostGuid)))
			{
				UsedPostGuids.Add(PreviousPost.PostGuid);
				return PreviousPost.PostGuid;
			}
		}
		const FGuid NewGuid = FGuid::NewGuid();
		UsedPostGuids.Add(NewGuid);
		return NewGuid;
	};

	// 同一位置附近只保留一根柱。显式柱（端点/门洞边柱）优先级更高。
	auto AddOrMergePost = [&OutPosts](const FEHBRailingPost& NewPost)
	{
		for (FEHBRailingPost& ExistingPost : OutPosts)
		{
			if (FMath::Abs(ExistingPost.Distance - NewPost.Distance) <= EHBRailingDuplicateDistanceTolerance)
			{
				if (NewPost.bExplicit || !ExistingPost.bExplicit)
				{
					ExistingPost = NewPost;
				}
				return;
			}
		}
		OutPosts.Add(NewPost);
	};

	const float Length = GetRailingLength();
	if (Length <= UE_SMALL_NUMBER)
	{
		return false;
	}

	if (PathMode == EEHBRailingPathMode::StairHosted)
	{
		AEHB_Stair* Stair = HostedStair;
		if (!Stair && OwningBuilding && HostedStairGuid.IsValid())
		{
			Stair = Cast<AEHB_Stair>(OwningBuilding->FindElementActorByGuid(HostedStairGuid));
		}

		TArray<FEHBStairRailingPostSample> StairSamples;
		if (Stair && Stair->BuildRailingPostSamples(
			RailingSide,
			PostSpacingMode,
			PostSpacing,
			StepsPerPost,
			StairSideOffset,
			StairPostBaseHeightOffset,
			StairSamples))
		{
			const FTransform StairToWorld = Stair->GetActorTransform();
			const FTransform WorldToRailing = GetActorTransform().Inverse();
			// 楼梯采样结果来自楼梯局部空间；扶手组件都放在扶手 Actor 下，所以统一转成扶手局部。
			for (const FEHBStairRailingPostSample& StairSample : StairSamples)
			{
				if (IsDistanceInsideGateOpening(StairSample.Distance))
				{
					continue;
				}

				FEHBRailingPost Post;
				Post.PostGuid = FindPreviousGuid(StairSample.Distance, false, FGuid());
				Post.Distance = StairSample.Distance;
				Post.StepIndex = StairSample.StepIndex;
				Post.LocalBaseLocation = WorldToRailing.TransformPosition(StairToWorld.TransformPosition(StairSample.LocalBaseLocation));
				const FVector LocalForward = WorldToRailing.TransformVectorNoScale(
					StairToWorld.TransformVectorNoScale(StairSample.LocalRotation.Vector()));
				Post.LocalRotation = MakeUprightPathRotation(LocalForward);
				Post.bExplicit = StairSample.Distance < 0.0f
					|| FMath::IsNearlyZero(StairSample.Distance)
					|| FMath::IsNearlyEqual(StairSample.Distance, Length, 0.5f);
				Post.bSuppressInstance =
					(bOmitStartPost
						&& (StairSample.Distance < 0.0f
							|| FMath::IsNearlyZero(StairSample.Distance, EHBRailingDuplicateDistanceTolerance)))
					|| (bOmitEndPost && FMath::IsNearlyEqual(StairSample.Distance, Length, EHBRailingDuplicateDistanceTolerance));
				AddOrMergePost(Post);
			}
		}
	}
	else
	{
		const FVector Forward = GetSafeLinearForward(LinearStart, LinearEnd);
		const int32 SegmentCount = FMath::Max(1, FMath::CeilToInt(Length / FMath::Max(1.0f, PostSpacing)));
		for (int32 Index = 0; Index <= SegmentCount; ++Index)
		{
			const float Distance = Length * static_cast<float>(Index) / static_cast<float>(SegmentCount);
			if (IsDistanceInsideGateOpening(Distance))
			{
				continue;
			}

			FEHBRailingPost Post;
			Post.PostGuid = FindPreviousGuid(Distance, false,
				Index == 0 ? LinearStartPostGuid : (Index == SegmentCount ? LinearEndPostGuid : FGuid()));
			Post.Distance = Distance;
			Post.LocalBaseLocation = LinearStart + Forward * Distance;
			Post.LocalRotation = MakeUprightPathRotation(Forward);
			Post.bExplicit = Index == 0 || Index == SegmentCount;
			Post.bSuppressInstance = (Index == 0 && bOmitStartPost) || (Index == SegmentCount && bOmitEndPost);
			AddOrMergePost(Post);
		}
	}

	for (FEHBRailingGateConnection& Connection : GateConnections)
	{
		// 门洞两侧总是补边柱；普通柱、横杆和围栏板通过 IsDistanceInsideGateOpening 被裁掉。
		const float HalfWidth = FMath::Max(0.5f, Connection.Width * 0.5f);
		const float LeftDistance = FMath::Clamp(Connection.DistanceFromStart - HalfWidth, 0.0f, Length);
		const float RightDistance = FMath::Clamp(Connection.DistanceFromStart + HalfWidth, 0.0f, Length);
		const FGuid GatePostGuids[2] = {
			Connection.LeftPostGuid,
			Connection.RightPostGuid
		};
		const float GatePostDistances[2] = {
			LeftDistance,
			RightDistance
		};

		for (int32 Index = 0; Index < 2; ++Index)
		{
			FEHBRailingPathSample PathSample;
			if (!EvaluatePathAtDistance(GatePostDistances[Index], PathSample))
			{
				continue;
			}

			FEHBRailingPost Post;
			Post.PostGuid = FindPreviousGuid(GatePostDistances[Index], true, GatePostGuids[Index]);
			Post.Distance = GatePostDistances[Index];
			Post.StepIndex = PathSample.StepIndex;
			Post.LocalBaseLocation = PathSample.LocalLocation;
			Post.LocalRotation = MakeUprightPathRotation(PathSample.LocalForward);
			Post.bExplicit = true;
			Post.bGatePost = true;
			AddOrMergePost(Post);
		}
	}

	OutPosts.Sort(
		[](const FEHBRailingPost& A, const FEHBRailingPost& B)
		{
			return A.Distance < B.Distance;
		});
	return !OutPosts.IsEmpty();
}

void AEHB_Railing::RebuildPostInstances()
{
	if (!PostMeshComponent)
	{
		return;
	}

	UStaticMesh* LoadedPostMesh = ResolveRailingMesh(PostMesh);
	if (LoadedPostMesh)
	{
		PostMeshComponent->SetStaticMesh(LoadedPostMesh);
	}
	PostMeshComponent->ClearInstances();
	PostInstanceGuids.Reset();

	const FBox SourceBounds = GetSafeMeshBounds(LoadedPostMesh);
	const FVector SourceSize = SourceBounds.GetSize();
	const float PostHorizontalScale = PostWidth / GetSafeSourceSize(FMath::Max(SourceSize.X, SourceSize.Y));
	const float PostVerticalScale = PostHeight / GetSafeSourceSize(SourceSize.Z);
	const FVector PostInstanceScale(PostHorizontalScale, PostHorizontalScale, PostVerticalScale);
	const FVector SourceAnchorLocal(
		SourceBounds.GetCenter().X * PostHorizontalScale,
		SourceBounds.GetCenter().Y * PostHorizontalScale,
		SourceBounds.Min.Z * PostVerticalScale);

	// 柱子是大量重复构件，使用 HISM 保持 Actor 数量低，并支持实例索引反查 PostGuid。
	for (const FEHBRailingPost& Post : GeneratedPosts)
	{
		if (Post.bSuppressInstance || HasPostMeshOverride(Post.PostGuid))
		{
			continue;
		}

		const FVector InstanceLocation = Post.LocalBaseLocation - Post.LocalRotation.RotateVector(SourceAnchorLocal);
		PostMeshComponent->AddInstance(FTransform(Post.LocalRotation, InstanceLocation, PostInstanceScale), false);
		PostInstanceGuids.Add(Post.PostGuid);
	}
	PostMeshComponent->MarkRenderStateDirty();
}

void AEHB_Railing::RebuildPostOverrideMeshes()
{
	int32 ComponentIndex = 0;
	PostOverrideComponentGuids.Reset();
	for (const FEHBRailingPostMeshOverride& Override : PostMeshOverrides)
	{
		if (!Override.PostGuid.IsValid())
		{
			continue;
		}

		FEHBRailingPost Post;
		if (!FindPostByGuid(Override.PostGuid, Post) || Post.bSuppressInstance)
		{
			continue;
		}

		UStaticMesh* LoadedPostMesh = ResolveRailingMesh(Override.PostMesh);
		if (!LoadedPostMesh)
		{
			continue;
		}

		UStaticMeshComponent* Component = GetOrCreatePostOverrideMeshComponent(ComponentIndex++);
		if (!Component)
		{
			continue;
		}

		const FBox SourceBounds = GetSafeMeshBounds(LoadedPostMesh);
		const FVector SourceSize = SourceBounds.GetSize();
		const float PostHorizontalScale = Override.PostWidth / GetSafeSourceSize(FMath::Max(SourceSize.X, SourceSize.Y));
		const float PostVerticalScale = Override.PostHeight / GetSafeSourceSize(SourceSize.Z);
		const FVector PostInstanceScale(PostHorizontalScale, PostHorizontalScale, PostVerticalScale);
		const FVector SourceAnchorLocal(
			SourceBounds.GetCenter().X * PostHorizontalScale,
			SourceBounds.GetCenter().Y * PostHorizontalScale,
			SourceBounds.Min.Z * PostVerticalScale);
		const FVector InstanceLocation = Post.LocalBaseLocation - Post.LocalRotation.RotateVector(SourceAnchorLocal);

		Component->SetStaticMesh(LoadedPostMesh);
		Component->SetRelativeTransform(FTransform(Post.LocalRotation, InstanceLocation, PostInstanceScale));
		Component->SetMaterial(0, ResolveRailingMaterial(Override.PostMaterial));
		Component->SetVisibility(true);
		PostOverrideComponentGuids.Add(Override.PostGuid);
	}

	TrimPostOverrideMeshComponents(ComponentIndex);
}

void AEHB_Railing::RebuildRailMeshes()
{
	TrimRailMeshComponents(0);
	if (!RailGeneratedMeshComponent)
	{
		return;
	}

	UStaticMesh* GeneratedRailMesh = ResolveRailingMesh(RailMesh);
	if (!GeneratedRailMesh || GeneratedPosts.Num() < 2)
	{
		RailGeneratedMeshComponent->ClearAllMeshSections();
		RailGeneratedMeshComponent->SetVisibility(false);
		return;
	}

	const bool bUseWallCut = bHasStartWallCut && PathMode == EEHBRailingPathMode::Linear
		&& !StartWallCutPoint.ContainsNaN() && !StartWallCutNormal.ContainsNaN() && !StartWallCutNormal.IsNearlyZero();
	const FPlane WallCut(StartWallCutPoint, StartWallCutNormal.GetSafeNormal());
	const FBox GeneratedRailBounds = GetSafeMeshBounds(GeneratedRailMesh);
	const FVector GeneratedRailSize = GeneratedRailBounds.GetSize();
	const float GeneratedRailScale = RailThickness / GetSafeSourceSize(FMath::Max(GeneratedRailSize.Y, GeneratedRailSize.Z));
	const float HalfRailWidth = FMath::Max(0.1f, FMath::Abs(GeneratedRailSize.Y) * GeneratedRailScale * 0.5f);
	const float HalfRailHeight = FMath::Max(0.1f, FMath::Abs(GeneratedRailSize.Z) * GeneratedRailScale * 0.5f);
	const float RailingLengthForMesh = GetRailingLength();
	FVector ConnectedStartDirectionForMesh = FVector::ZeroVector;
	FVector ConnectedEndDirectionForMesh = FVector::ZeroVector;
	const bool bRailingStartConnectedForMesh = FindCompatibleRailEndpointConnection(true, ConnectedStartDirectionForMesh);
	const bool bRailingEndConnectedForMesh = FindCompatibleRailEndpointConnection(false, ConnectedEndDirectionForMesh);

	TArray<FVector> RailVertices;
	TArray<int32> RailTriangles;
	TArray<FVector> RailNormals;
	TArray<FVector2D> RailUVs;

	for (int32 PostIndex = 0; PostIndex + 1 < GeneratedPosts.Num(); ++PostIndex)
	{
		const float StartDistance = GeneratedPosts[PostIndex].Distance;
		const float EndDistance = GeneratedPosts[PostIndex + 1].Distance;
		if (EndDistance <= StartDistance + UE_SMALL_NUMBER)
		{
			continue;
		}

		const int32 SubSegmentCount = FMath::Max(1, FMath::CeilToInt((EndDistance - StartDistance) / MaxRailSegmentLength));
		for (int32 SubSegmentIndex = 0; SubSegmentIndex < SubSegmentCount; ++SubSegmentIndex)
		{
			const float Alpha0 = static_cast<float>(SubSegmentIndex) / static_cast<float>(SubSegmentCount);
			const float Alpha1 = static_cast<float>(SubSegmentIndex + 1) / static_cast<float>(SubSegmentCount);
			const float D0 = FMath::Lerp(StartDistance, EndDistance, Alpha0);
			const float D1 = FMath::Lerp(StartDistance, EndDistance, Alpha1);
			const float MidDistance = (D0 + D1) * 0.5f;
			if (IsDistanceInsideGateOpening(MidDistance))
			{
				continue;
			}

			FEHBRailingPathSample StartSample;
			FEHBRailingPathSample EndSample;
			if (!EvaluateRailPathAtDistance(D0, StartSample) || !EvaluateRailPathAtDistance(D1, EndSample))
			{
				continue;
			}

			const FVector Start = StartSample.LocalLocation + FVector::UpVector * RailHeight;
			const FVector End = EndSample.LocalLocation + FVector::UpVector * RailHeight;
			const float RawSegmentLength = FVector::Distance(Start, End);
			if (RawSegmentLength <= UE_SMALL_NUMBER)
			{
				continue;
			}

			FGuid GatePostGuid;
			bool bLeftGatePost = false;
			const bool bStartAtGatePost = IsDistanceNearGatePost(D0, GatePostGuid, bLeftGatePost);
			const bool bEndAtGatePost = IsDistanceNearGatePost(D1, GatePostGuid, bLeftGatePost);
			const bool bAtRailingStart = FMath::IsNearlyZero(D0, EHBRailingDuplicateDistanceTolerance);
			const bool bAtRailingEnd = FMath::IsNearlyEqual(D1, RailingLengthForMesh, EHBRailingDuplicateDistanceTolerance);
			const bool bCanExtendStart =
				(D0 > EHBRailingDuplicateDistanceTolerance || (bAtRailingStart && bRailingStartConnectedForMesh))
				&& !bStartAtGatePost;
			const bool bCanExtendEnd =
				(D1 < RailingLengthForMesh - EHBRailingDuplicateDistanceTolerance || (bAtRailingEnd && bRailingEndConnectedForMesh))
				&& !bEndAtGatePost;
			const FVector SegmentDirection = (End - Start).GetSafeNormal(UE_SMALL_NUMBER, StartSample.LocalForward);
			FVector StartMiterTangent = FVector::ZeroVector;
			FVector EndMiterTangent = FVector::ZeroVector;
			const bool bUseStartMiter = bAtRailingStart
				&& bRailingStartConnectedForMesh
				&& ComputeRailEndpointMiterTangent(SegmentDirection, ConnectedStartDirectionForMesh, true, StartMiterTangent);
			const bool bUseEndMiter = bAtRailingEnd
				&& bRailingEndConnectedForMesh
				&& ComputeRailEndpointMiterTangent(SegmentDirection, ConnectedEndDirectionForMesh, false, EndMiterTangent);
			const float JointOverlap = GetRailJointOverlap(RailThickness, RawSegmentLength);
			const float StartJointOverlap = (bAtRailingStart && bRailingStartConnectedForMesh && !bUseStartMiter)
				? ComputeRailEndpointMiterExtension(SegmentDirection, ConnectedStartDirectionForMesh, RawSegmentLength)
				: JointOverlap;
			const float EndJointOverlap = (bAtRailingEnd && bRailingEndConnectedForMesh && !bUseEndMiter)
				? ComputeRailEndpointMiterExtension(-SegmentDirection, ConnectedEndDirectionForMesh, RawSegmentLength)
				: JointOverlap;
			const FVector ExtendedStart = (bCanExtendStart && !bUseStartMiter) ? Start - SegmentDirection * StartJointOverlap : Start;
			const FVector ExtendedEnd = (bCanExtendEnd && !bUseEndMiter) ? End + SegmentDirection * EndJointOverlap : End;
			const FVector StartTangentDirection = bUseStartMiter
				? StartMiterTangent
				: StartSample.LocalForward.GetSafeNormal(UE_SMALL_NUMBER, SegmentDirection);
			const FVector EndTangentDirection = bUseEndMiter
				? EndMiterTangent
				: EndSample.LocalForward.GetSafeNormal(UE_SMALL_NUMBER, SegmentDirection);
			AppendRailSweepSegment(
				RailVertices,
				RailTriangles,
				RailNormals,
				RailUVs,
				ExtendedStart,
				StartTangentDirection,
				StartSample.LocalRight,
				ExtendedEnd,
				EndTangentDirection,
				EndSample.LocalRight,
				HalfRailWidth,
				HalfRailHeight,
				D0 / EHBRailingMeshSourceSize,
				D1 / EHBRailingMeshSourceSize,
				bUseWallCut ? &WallCut : nullptr);
		}
	}

	if (RailVertices.IsEmpty() || RailTriangles.IsEmpty())
	{
		RailGeneratedMeshComponent->ClearAllMeshSections();
		RailGeneratedMeshComponent->SetVisibility(false);
		return;
	}

	TArray<FLinearColor> VertexColors;
	VertexColors.Init(FLinearColor::White, RailVertices.Num());
	TArray<FProcMeshTangent> Tangents;
	Tangents.Init(FProcMeshTangent(), RailVertices.Num());
	RailGeneratedMeshComponent->CreateMeshSection_LinearColor(
		0,
		RailVertices,
		RailTriangles,
		RailNormals,
		RailUVs,
		VertexColors,
		Tangents,
		true);
	RailGeneratedMeshComponent->SetMeshSectionName(0, FName(TEXT("RailingRails")));
	RailGeneratedMeshComponent->ClearMeshSectionsFrom(1);
	RailGeneratedMeshComponent->SetVisibility(true);
	return;

#if 0
	int32 SegmentComponentIndex = 0;
	UStaticMesh* LoadedRailMesh = ResolveRailingMesh(RailMesh);
	if (!LoadedRailMesh || GeneratedPosts.Num() < 2)
	{
		TrimRailMeshComponents(0);
		return;
	}

	const FBox RailSourceBounds = GetSafeMeshBounds(LoadedRailMesh);
	const FVector RailSourceSize = RailSourceBounds.GetSize();
	const float RailCrossScale = RailThickness / GetSafeSourceSize(FMath::Max(RailSourceSize.Y, RailSourceSize.Z));
	const FVector2D RailSourceOffset(
		-RailSourceBounds.GetCenter().Y * RailCrossScale,
		-RailSourceBounds.GetCenter().Z * RailCrossScale);
	const float RailingLength = GetRailingLength();
	FVector ConnectedStartDirection = FVector::ZeroVector;
	FVector ConnectedEndDirection = FVector::ZeroVector;
	const bool bRailingStartConnected = FindCompatibleRailEndpointConnection(true, ConnectedStartDirection);
	const bool bRailingEndConnected = FindCompatibleRailEndpointConnection(false, ConnectedEndDirection);
	for (int32 PostIndex = 0; PostIndex + 1 < GeneratedPosts.Num(); ++PostIndex)
	{
		const float StartDistance = GeneratedPosts[PostIndex].Distance;
		const float EndDistance = GeneratedPosts[PostIndex + 1].Distance;
		if (EndDistance <= StartDistance + UE_SMALL_NUMBER)
		{
			continue;
		}

		const int32 SubSegmentCount = FMath::Max(1, FMath::CeilToInt((EndDistance - StartDistance) / MaxRailSegmentLength));
		// 横杆按最大长度切成多个 SplineMesh，弯曲楼梯上不会被一根长网格拉得过度变形。
		for (int32 SubSegmentIndex = 0; SubSegmentIndex < SubSegmentCount; ++SubSegmentIndex)
		{
			const float Alpha0 = static_cast<float>(SubSegmentIndex) / static_cast<float>(SubSegmentCount);
			const float Alpha1 = static_cast<float>(SubSegmentIndex + 1) / static_cast<float>(SubSegmentCount);
			const float D0 = FMath::Lerp(StartDistance, EndDistance, Alpha0);
			const float D1 = FMath::Lerp(StartDistance, EndDistance, Alpha1);
			const float MidDistance = (D0 + D1) * 0.5f;
			if (IsDistanceInsideGateOpening(MidDistance))
			{
				continue;
			}

			FEHBRailingPathSample StartSample;
			FEHBRailingPathSample EndSample;
			if (!EvaluatePathAtDistance(D0, StartSample) || !EvaluatePathAtDistance(D1, EndSample))
			{
				continue;
			}

			USplineMeshComponent* SegmentComponent = GetOrCreateRailMeshComponent(SegmentComponentIndex++);
			if (!SegmentComponent)
			{
				continue;
			}

			const FVector Start = StartSample.LocalLocation + FVector::UpVector * RailHeight;
			const FVector End = EndSample.LocalLocation + FVector::UpVector * RailHeight;
			const float RawSegmentLength = FVector::Distance(Start, End);
			if (RawSegmentLength <= UE_SMALL_NUMBER)
			{
				continue;
			}

			FGuid GatePostGuid;
			bool bLeftGatePost = false;
			const bool bStartAtGatePost = IsDistanceNearGatePost(D0, GatePostGuid, bLeftGatePost);
			const bool bEndAtGatePost = IsDistanceNearGatePost(D1, GatePostGuid, bLeftGatePost);
			const bool bAtRailingStart = FMath::IsNearlyZero(D0, EHBRailingDuplicateDistanceTolerance);
			const bool bAtRailingEnd = FMath::IsNearlyEqual(D1, RailingLength, EHBRailingDuplicateDistanceTolerance);
			const bool bCanExtendStart =
				(D0 > EHBRailingDuplicateDistanceTolerance || (bAtRailingStart && bRailingStartConnected))
				&& !bStartAtGatePost;
			const bool bCanExtendEnd =
				(D1 < RailingLength - EHBRailingDuplicateDistanceTolerance || (bAtRailingEnd && bRailingEndConnected))
				&& !bEndAtGatePost;
			const FVector SegmentDirection = (End - Start).GetSafeNormal(UE_SMALL_NUMBER, StartSample.LocalForward);
			const float JointOverlap = GetRailJointOverlap(RailThickness, RawSegmentLength);
			FVector StartMiterTangent = FVector::ZeroVector;
			FVector EndMiterTangent = FVector::ZeroVector;
			const bool bUseStartMiter = bAtRailingStart
				&& bRailingStartConnected
				&& ComputeRailEndpointMiterTangent(SegmentDirection, ConnectedStartDirection, true, StartMiterTangent);
			const bool bUseEndMiter = bAtRailingEnd
				&& bRailingEndConnected
				&& ComputeRailEndpointMiterTangent(SegmentDirection, ConnectedEndDirection, false, EndMiterTangent);
			const float StartJointOverlap = (bAtRailingStart && bRailingStartConnected && !bUseStartMiter)
				? ComputeRailEndpointMiterExtension(SegmentDirection, ConnectedStartDirection, RawSegmentLength)
				: JointOverlap;
			const float EndJointOverlap = (bAtRailingEnd && bRailingEndConnected && !bUseEndMiter)
				? ComputeRailEndpointMiterExtension(-SegmentDirection, ConnectedEndDirection, RawSegmentLength)
				: JointOverlap;
			const FVector ExtendedStart = (bCanExtendStart && !bUseStartMiter) ? Start - SegmentDirection * StartJointOverlap : Start;
			const FVector ExtendedEnd = (bCanExtendEnd && !bUseEndMiter) ? End + SegmentDirection * EndJointOverlap : End;
			const float SegmentLength = FVector::Distance(ExtendedStart, ExtendedEnd);
			const float DefaultTangentLength = PathMode == EEHBRailingPathMode::StairHosted
				? SegmentLength
				: SegmentLength * 0.5f;
			const float MiterTangentLength = FMath::Min(DefaultTangentLength, FMath::Max(RailThickness * 1.25f, 4.0f));
			const FVector StartTangentDirection = bUseStartMiter
				? StartMiterTangent
				: StartSample.LocalForward.GetSafeNormal(UE_SMALL_NUMBER, SegmentDirection);
			const FVector EndTangentDirection = bUseEndMiter
				? EndMiterTangent
				: EndSample.LocalForward.GetSafeNormal(UE_SMALL_NUMBER, SegmentDirection);
			SegmentComponent->SetStaticMesh(LoadedRailMesh);
			SegmentComponent->SetForwardAxis(ESplineMeshAxis::X, false);
			SegmentComponent->SetStartAndEnd(
				ExtendedStart,
				StartTangentDirection * (bUseStartMiter ? MiterTangentLength : DefaultTangentLength),
				ExtendedEnd,
				EndTangentDirection * (bUseEndMiter ? MiterTangentLength : DefaultTangentLength),
				false);
			SegmentComponent->SetStartScale(FVector2D(RailCrossScale, RailCrossScale), false);
			SegmentComponent->SetEndScale(FVector2D(RailCrossScale, RailCrossScale), false);
			SegmentComponent->SetStartOffset(RailSourceOffset, false);
			SegmentComponent->SetEndOffset(RailSourceOffset, false);
			SegmentComponent->SetVisibility(true);
			SegmentComponent->UpdateMesh();
		}
	}

	TrimRailMeshComponents(SegmentComponentIndex);
#endif
}

void AEHB_Railing::RebuildPanelMesh()
{
	if (!PanelMeshComponent && !PanelSampleMeshComponent)
	{
		return;
	}

	const bool bBuildPanel = FillMode == EEHBRailingFillMode::SolidPanel
		|| FillMode == EEHBRailingFillMode::PostsRailsAndPanel;
	if (!bBuildPanel || GeneratedPosts.Num() < 2)
	{
		if (PanelMeshComponent)
		{
			PanelMeshComponent->ClearAllMeshSections();
			PanelMeshComponent->SetVisibility(false);
		}
		if (PanelSampleMeshComponent)
		{
			PanelSampleMeshComponent->ClearInstances();
			PanelSampleMeshComponent->SetVisibility(false);
		}
		return;
	}

	const FEHBRailingMeshData* SampledRow = FindSampledRailingRow(SampledRailingRow);
	UStaticMesh* SampledPanelMesh = SampledRow
		? ResolveRailingMesh(SampledRow->PanelMaterialSourceMesh.SourceStaticMesh)
		: nullptr;
	const FBox SampledPanelBounds = GetSafeMeshBounds(SampledPanelMesh);
	const FVector SampledPanelSize = SampledPanelMesh ? SampledPanelBounds.GetSize() : FVector::ZeroVector;
	const float PanelSampleWidth = GetSafeSourceSize(
		SampledPanelMesh ? GetHorizontalPanelWidth(SampledPanelSize) : EHBRailingMeshSourceSize);
	const float PanelSampleHeight = GetSafeSourceSize(
		SampledPanelMesh ? FMath::Abs(SampledPanelSize.Z) : EHBRailingMeshSourceSize);
	const float PanelHeight = FMath::Max(1.0f, PanelTopOffset - PanelBottomOffset);

	if (SampledPanelMesh && PanelSampleMeshComponent)
	{
		if (PanelMeshComponent)
		{
			PanelMeshComponent->ClearAllMeshSections();
			PanelMeshComponent->SetVisibility(false);
		}

		PanelSampleMeshComponent->SetStaticMesh(SampledPanelMesh);
		PanelSampleMeshComponent->ClearInstances();

		const bool bWidthAxisY = IsPanelWidthAxisY(SampledPanelSize);
		const float WidthScale = 1.0f / PanelSampleWidth;
		const float HeightScale = PanelHeight / PanelSampleHeight;
		const float ThicknessScale = 1.0f;
		int32 InstanceCount = 0;

		for (int32 PostIndex = 0; PostIndex + 1 < GeneratedPosts.Num(); ++PostIndex)
		{
			const float D0 = GeneratedPosts[PostIndex].Distance;
			const float D1 = GeneratedPosts[PostIndex + 1].Distance;
			const float MidDistance = (D0 + D1) * 0.5f;
			if (IsDistanceInsideGateOpening(MidDistance))
			{
				continue;
			}

			FEHBRailingPathSample StartSample;
			FEHBRailingPathSample EndSample;
			if (!EvaluatePathAtDistance(D0, StartSample) || !EvaluatePathAtDistance(D1, EndSample))
			{
				continue;
			}

			const FVector SegmentDelta = EndSample.LocalLocation - StartSample.LocalLocation;
			const float SegmentLength = SegmentDelta.Size();
			if (SegmentLength <= UE_SMALL_NUMBER)
			{
				continue;
			}

			const FVector SegmentBaseCenter = (StartSample.LocalLocation + EndSample.LocalLocation) * 0.5f
				+ FVector::UpVector * PanelBottomOffset;
			const float SegmentWidthScale = SegmentLength * WidthScale;
			const FVector PanelScale = bWidthAxisY
				? FVector(ThicknessScale, SegmentWidthScale, HeightScale)
				: FVector(SegmentWidthScale, ThicknessScale, HeightScale);
			const FRotator PanelRotation = MakePanelSampleRotation(SegmentDelta, bWidthAxisY);
			const FVector SourceAnchorLocal(
				SampledPanelBounds.GetCenter().X * PanelScale.X,
				SampledPanelBounds.GetCenter().Y * PanelScale.Y,
				SampledPanelBounds.Min.Z * PanelScale.Z);
			const FVector InstanceLocation = SegmentBaseCenter - PanelRotation.RotateVector(SourceAnchorLocal);

			PanelSampleMeshComponent->AddInstance(FTransform(PanelRotation, InstanceLocation, PanelScale), false);
			++InstanceCount;
		}

		PanelSampleMeshComponent->SetVisibility(InstanceCount > 0);
		PanelSampleMeshComponent->MarkRenderStateDirty();
		return;
	}

	if (PanelSampleMeshComponent)
	{
		PanelSampleMeshComponent->ClearInstances();
		PanelSampleMeshComponent->SetVisibility(false);
	}
	if (!PanelMeshComponent)
	{
		return;
	}

	FEHBScopedGeneratedMeshUpdate ScopedMeshUpdate(PanelMeshComponent);
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;

	for (int32 PostIndex = 0; PostIndex + 1 < GeneratedPosts.Num(); ++PostIndex)
	{
		const float D0 = GeneratedPosts[PostIndex].Distance;
		const float D1 = GeneratedPosts[PostIndex + 1].Distance;
		const float MidDistance = (D0 + D1) * 0.5f;
		if (IsDistanceInsideGateOpening(MidDistance))
		{
			continue;
		}

		FEHBRailingPathSample StartSample;
		FEHBRailingPathSample EndSample;
		if (!EvaluatePathAtDistance(D0, StartSample) || !EvaluatePathAtDistance(D1, EndSample))
		{
			continue;
		}

		// 实体围栏板按相邻柱之间的路径片段生成，门洞内部的片段会被跳过。
		const FVector A = StartSample.LocalLocation + FVector::UpVector * PanelBottomOffset;
		const FVector B = EndSample.LocalLocation + FVector::UpVector * PanelBottomOffset;
		const FVector C = EndSample.LocalLocation + FVector::UpVector * PanelTopOffset;
		const FVector D = StartSample.LocalLocation + FVector::UpVector * PanelTopOffset;
		const FVector PanelNormal = ((StartSample.LocalRight + EndSample.LocalRight) * 0.5f)
			.GetSafeNormal(UE_SMALL_NUMBER, FVector::RightVector);
		AppendPanelQuad(
			Vertices,
			Triangles,
			Normals,
			UVs,
			A,
			B,
			C,
			D,
			PanelNormal,
			D0 / PanelSampleWidth,
			D1 / PanelSampleWidth,
			0.0f,
			PanelHeight / PanelSampleHeight);
	}

	if (Vertices.IsEmpty() || Triangles.IsEmpty())
	{
		PanelMeshComponent->ClearAllMeshSections();
		PanelMeshComponent->SetVisibility(false);
		return;
	}

	TArray<FLinearColor> VertexColors;
	VertexColors.Init(FLinearColor::White, Vertices.Num());
	TArray<FProcMeshTangent> Tangents;
	Tangents.Init(FProcMeshTangent(), Vertices.Num());
	PanelMeshComponent->CreateMeshSection_LinearColor(
		0,
		Vertices,
		Triangles,
		Normals,
		UVs,
		VertexColors,
		Tangents,
		true);
	PanelMeshComponent->SetMeshSectionName(0, FName(TEXT("RailingPanel")));
	PanelMeshComponent->ClearMeshSectionsFrom(1);
	PanelMeshComponent->SetVisibility(true);
}

void AEHB_Railing::TrimRailMeshComponents(int32 DesiredCount)
{
	for (int32 Index = RailMeshComponents.Num() - 1; Index >= DesiredCount; --Index)
	{
		if (USplineMeshComponent* Component = RailMeshComponents[Index])
		{
			Component->DestroyComponent();
		}
		RailMeshComponents.RemoveAt(Index);
	}
}

USplineMeshComponent* AEHB_Railing::GetOrCreateRailMeshComponent(int32 SegmentIndex)
{
	if (SegmentIndex < 0)
	{
		return nullptr;
	}

	while (RailMeshComponents.Num() <= SegmentIndex)
	{
		const FName ComponentName(*FString::Printf(TEXT("RailingRail_%d"), RailMeshComponents.Num()));
		USplineMeshComponent* NewComponent = NewObject<USplineMeshComponent>(this, ComponentName, RF_Transactional);
		if (!NewComponent)
		{
			return nullptr;
		}

		NewComponent->CreationMethod = EComponentCreationMethod::Instance;
		NewComponent->SetupAttachment(SceneRoot);
		NewComponent->SetMobility(EComponentMobility::Movable);
		NewComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		NewComponent->SetCollisionObjectType(ECC_WorldStatic);
		NewComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
		NewComponent->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		AddInstanceComponent(NewComponent);
		NewComponent->RegisterComponent();
		RailMeshComponents.Add(NewComponent);
	}

	return RailMeshComponents[SegmentIndex];
}

void AEHB_Railing::TrimPostOverrideMeshComponents(int32 DesiredCount)
{
	for (int32 Index = PostOverrideMeshComponents.Num() - 1; Index >= DesiredCount; --Index)
	{
		if (UStaticMeshComponent* Component = PostOverrideMeshComponents[Index])
		{
			Component->DestroyComponent();
		}
		PostOverrideMeshComponents.RemoveAt(Index);
	}

	if (PostOverrideComponentGuids.Num() > DesiredCount)
	{
		PostOverrideComponentGuids.SetNum(DesiredCount);
	}
}

UStaticMeshComponent* AEHB_Railing::GetOrCreatePostOverrideMeshComponent(int32 ComponentIndex)
{
	if (ComponentIndex < 0)
	{
		return nullptr;
	}

	while (PostOverrideMeshComponents.Num() <= ComponentIndex)
	{
		const FName ComponentName(*FString::Printf(TEXT("RailingPostOverride_%d"), PostOverrideMeshComponents.Num()));
		UStaticMeshComponent* NewComponent = NewObject<UStaticMeshComponent>(this, ComponentName, RF_Transactional);
		if (!NewComponent)
		{
			return nullptr;
		}

		NewComponent->CreationMethod = EComponentCreationMethod::Instance;
		NewComponent->SetupAttachment(SceneRoot);
		NewComponent->SetMobility(EComponentMobility::Movable);
		NewComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		NewComponent->SetCollisionObjectType(ECC_WorldStatic);
		NewComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
		NewComponent->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		AddInstanceComponent(NewComponent);
		NewComponent->RegisterComponent();
		PostOverrideMeshComponents.Add(NewComponent);
	}

	return PostOverrideMeshComponents[ComponentIndex];
}

const FEHBRailingPostMeshOverride* AEHB_Railing::FindPostMeshOverride(FGuid PostGuid) const
{
	if (!PostGuid.IsValid())
	{
		return nullptr;
	}

	return PostMeshOverrides.FindByPredicate(
		[PostGuid](const FEHBRailingPostMeshOverride& Override)
		{
			return Override.PostGuid == PostGuid && !Override.PostMesh.IsNull();
		});
}

bool AEHB_Railing::HasPostMeshOverride(FGuid PostGuid) const
{
	return FindPostMeshOverride(PostGuid) != nullptr;
}

FVector AEHB_Railing::GetRailingEndpointWorldLocation(bool bStartEndpoint) const
{
	FEHBRailingPathSample Sample;
	if (EvaluatePathAtDistance(bStartEndpoint ? 0.0f : GetRailingLength(), Sample))
	{
		return GetActorTransform().TransformPosition(Sample.LocalLocation);
	}
	return GetActorLocation();
}

float AEHB_Railing::ComputeRailEndpointMiterExtension(
	const FVector& RailDirectionAwayFromJoint,
	const FVector& OtherRailDirectionAwayFromJoint,
	float SegmentLength) const
{
	const float BaseOverlap = GetRailJointOverlap(RailThickness, SegmentLength);
	if (SegmentLength <= UE_SMALL_NUMBER)
	{
		return BaseOverlap;
	}

	const FVector RailDirection2D(RailDirectionAwayFromJoint.X, RailDirectionAwayFromJoint.Y, 0.0f);
	const FVector OtherDirection2D(OtherRailDirectionAwayFromJoint.X, OtherRailDirectionAwayFromJoint.Y, 0.0f);
	const FVector SafeRailDirection = RailDirection2D.GetSafeNormal();
	const FVector SafeOtherDirection = OtherDirection2D.GetSafeNormal();
	if (SafeRailDirection.IsNearlyZero() || SafeOtherDirection.IsNearlyZero())
	{
		return BaseOverlap;
	}

	const float Dot = FMath::Clamp(FVector::DotProduct(SafeRailDirection, SafeOtherDirection), -1.0f, 1.0f);
	const float AngleRadians = FMath::Acos(Dot);
	const float AngleDegrees = FMath::RadiansToDegrees(AngleRadians);
	if (AngleDegrees <= EHBRailingMinMiterAngleDegrees || AngleDegrees >= EHBRailingMaxMiterAngleDegrees)
	{
		return BaseOverlap;
	}

	const float HalfAngleTan = FMath::Tan(AngleRadians * 0.5f);
	if (FMath::IsNearlyZero(HalfAngleTan))
	{
		return BaseOverlap;
	}

	// SplineMesh cannot cut an angled end cap, so use the miter angle as extra overlap
	// to bury the visible butt end when two sampled rail meshes meet at a corner.
	const float DesiredMiterOverlap = RailThickness / HalfAngleTan;
	const float SafeMaxOverlap = FMath::Min(EHBRailingMaxRailJointOverlap, SegmentLength * 0.35f);
	return FMath::Max(BaseOverlap, FMath::Clamp(DesiredMiterOverlap, EHBRailingMinRailJointOverlap, SafeMaxOverlap));
}

bool AEHB_Railing::ComputeRailEndpointMiterTangent(
	const FVector& SegmentDirection,
	const FVector& OtherRailDirectionAwayFromJoint,
	bool bStartEndpoint,
	FVector& OutMiterTangent) const
{
	OutMiterTangent = FVector::ZeroVector;

	const FVector SegmentDirection2D(SegmentDirection.X, SegmentDirection.Y, 0.0f);
	const FVector OtherAway2D(OtherRailDirectionAwayFromJoint.X, OtherRailDirectionAwayFromJoint.Y, 0.0f);
	const FVector SafeSegmentDirection = SegmentDirection2D.GetSafeNormal();
	const FVector SafeOtherAwayDirection = OtherAway2D.GetSafeNormal();
	if (SafeSegmentDirection.IsNearlyZero() || SafeOtherAwayDirection.IsNearlyZero())
	{
		return false;
	}

	const FVector OtherContinuationDirection = bStartEndpoint ? -SafeOtherAwayDirection : SafeOtherAwayDirection;
	const float Dot = FMath::Clamp(FVector::DotProduct(SafeSegmentDirection, OtherContinuationDirection), -1.0f, 1.0f);
	const float AngleDegrees = FMath::RadiansToDegrees(FMath::Acos(Dot));
	if (AngleDegrees <= EHBRailingMinMiterAngleDegrees || AngleDegrees >= EHBRailingMaxMiterAngleDegrees)
	{
		return false;
	}

	const FVector MiterTangent2D = (SafeSegmentDirection + OtherContinuationDirection).GetSafeNormal();
	if (MiterTangent2D.IsNearlyZero())
	{
		return false;
	}

	OutMiterTangent = FVector(MiterTangent2D.X, MiterTangent2D.Y, SegmentDirection.Z)
		.GetSafeNormal(UE_SMALL_NUMBER, SegmentDirection);
	return !OutMiterTangent.IsNearlyZero();
}

bool AEHB_Railing::FindCompatibleEndpointOnRailing(
	const AEHB_Railing* OtherRailing,
	const FVector& EndpointWorld,
	FVector& OutOtherRailDirectionLocal) const
{
	OutOtherRailDirectionLocal = FVector::ZeroVector;
	if (!IsRailStyleCompatibleWith(OtherRailing))
	{
		return false;
	}

	const float ToleranceSquared = FMath::Square(EHBRailingEndpointConnectTolerance);
	const FTransform& OtherTransform = OtherRailing->GetActorTransform();
	const FTransform& ThisTransform = GetActorTransform();
	const auto TryEndpoint = [OtherRailing, &OtherTransform, &ThisTransform, EndpointWorld, ToleranceSquared, &OutOtherRailDirectionLocal](bool bOtherStartEndpoint)
	{
		FEHBRailingPathSample OtherSample;
		if (!OtherRailing->EvaluatePathAtDistance(bOtherStartEndpoint ? 0.0f : OtherRailing->GetRailingLength(), OtherSample))
		{
			return false;
		}

		const FVector OtherEndpointWorld = OtherTransform.TransformPosition(OtherSample.LocalLocation);
		if (FVector::DistSquared(EndpointWorld, OtherEndpointWorld) > ToleranceSquared)
		{
			return false;
		}

		const FVector OtherDirectionAwayFromJoint = bOtherStartEndpoint
			? OtherSample.LocalForward
			: -OtherSample.LocalForward;
		const FVector OtherDirectionWorld = OtherTransform.TransformVectorNoScale(OtherDirectionAwayFromJoint)
			.GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
		OutOtherRailDirectionLocal = ThisTransform.InverseTransformVectorNoScale(OtherDirectionWorld)
			.GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
		return true;
	};

	return TryEndpoint(true) || TryEndpoint(false);
}

bool AEHB_Railing::FindCompatibleEndpointOnStairEmbeddedRailing(
	const AEHB_Stair* Stair,
	const FVector& EndpointWorld,
	FVector& OutOtherRailDirectionLocal) const
{
	OutOtherRailDirectionLocal = FVector::ZeroVector;
	if (!IsRailStyleCompatibleWithStair(Stair))
	{
		return false;
	}

	const float ToleranceSquared = FMath::Square(EHBRailingEndpointConnectTolerance);
	const FTransform& StairTransform = Stair->GetActorTransform();
	const FTransform& ThisTransform = GetActorTransform();
	for (EEHBRailingSide Side : { EEHBRailingSide::Left, EEHBRailingSide::Right })
	{
		if (!Stair->IsEmbeddedRailingSideGenerated(Side))
		{
			continue;
		}

		const TArray<FEHBRailingPost>& Posts = Stair->GetEmbeddedRailingPosts(Side);
		for (int32 PostIndex = 0; PostIndex < Posts.Num(); ++PostIndex)
		{
			const FEHBRailingPost& Post = Posts[PostIndex];
			if (Post.bSuppressInstance || !Post.PostGuid.IsValid())
			{
				continue;
			}

			const FVector PostWorldLocation = StairTransform.TransformPosition(Post.LocalBaseLocation);
			if (FVector::DistSquared(EndpointWorld, PostWorldLocation) > ToleranceSquared)
			{
				continue;
			}

			const bool bLastPost = PostIndex == Posts.Num() - 1;
			const FVector OtherDirectionAwayFromJoint = bLastPost
				? -Post.LocalRotation.Vector()
				: Post.LocalRotation.Vector();
			const FVector OtherDirectionWorld = StairTransform.TransformVectorNoScale(OtherDirectionAwayFromJoint)
				.GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
			OutOtherRailDirectionLocal = ThisTransform.InverseTransformVectorNoScale(OtherDirectionWorld)
				.GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
			return true;
		}
	}

	return false;
}

bool AEHB_Railing::FindCompatibleRailEndpointConnection(
	bool bStartEndpoint,
	FVector& OutOtherRailDirectionLocal) const
{
	OutOtherRailDirectionLocal = FVector::ZeroVector;
	if (!OwningBuilding)
	{
		return false;
	}

	const FVector EndpointWorld = GetRailingEndpointWorldLocation(bStartEndpoint);
	const FEHBRailingAnchor& Anchor = bStartEndpoint ? StartAnchor : EndAnchor;
	if (Anchor.ElementGuid.IsValid())
	{
		const AEHB_Railing* AnchorRailing = Cast<AEHB_Railing>(OwningBuilding->FindElementActorByGuid(Anchor.ElementGuid));
		if (FindCompatibleEndpointOnRailing(AnchorRailing, EndpointWorld, OutOtherRailDirectionLocal))
		{
			return true;
		}

		const AEHB_Stair* AnchorStair = Cast<AEHB_Stair>(OwningBuilding->FindElementActorByGuid(Anchor.ElementGuid));
		if (FindCompatibleEndpointOnStairEmbeddedRailing(AnchorStair, EndpointWorld, OutOtherRailDirectionLocal))
		{
			return true;
		}
	}

	FEHBElementQuery Query;
	Query.ElementTypes = { EEHBBuildingElementType::Railing };
	for (AEHBElementActorBase* Element : OwningBuilding->QueryElements(Query))
	{
		const AEHB_Railing* OtherRailing = Cast<AEHB_Railing>(Element);
		if (FindCompatibleEndpointOnRailing(OtherRailing, EndpointWorld, OutOtherRailDirectionLocal))
		{
			return true;
		}
	}

	return false;
}

bool AEHB_Railing::IsEndpointNearRailingEndpoint(const AEHB_Railing* OtherRailing, const FVector& EndpointWorld) const
{
	if (!OtherRailing || OtherRailing == this || OtherRailing->IsActorBeingDestroyed())
	{
		return false;
	}

	const FTransform& OtherTransform = OtherRailing->GetActorTransform();
	const float ToleranceSquared = FMath::Square(EHBRailingEndpointConnectTolerance);
	const auto IsNearEndpoint = [OtherRailing, &OtherTransform, EndpointWorld, ToleranceSquared](bool bOtherStartEndpoint)
	{
		FEHBRailingPathSample OtherSample;
		if (!OtherRailing->EvaluatePathAtDistance(bOtherStartEndpoint ? 0.0f : OtherRailing->GetRailingLength(), OtherSample))
		{
			return false;
		}

		const FVector OtherEndpointWorld = OtherTransform.TransformPosition(OtherSample.LocalLocation);
		return FVector::DistSquared(EndpointWorld, OtherEndpointWorld) <= ToleranceSquared;
	};

	return IsNearEndpoint(true) || IsNearEndpoint(false);
}

bool AEHB_Railing::IsRailStyleCompatibleWith(const AEHB_Railing* OtherRailing) const
{
	if (!OtherRailing || OtherRailing == this || OtherRailing->IsActorBeingDestroyed())
	{
		return false;
	}
	if (!IsRailingSampleCompatibleWith(OtherRailing))
	{
		return false;
	}
	if (RailMesh.ToSoftObjectPath() != OtherRailing->RailMesh.ToSoftObjectPath())
	{
		return false;
	}
	if (RailMaterial.ToSoftObjectPath() != OtherRailing->RailMaterial.ToSoftObjectPath())
	{
		return false;
	}
	return FMath::IsNearlyEqual(RailThickness, OtherRailing->RailThickness, 0.1f)
		&& FMath::IsNearlyEqual(RailHeight, OtherRailing->RailHeight, 0.5f);
}

bool AEHB_Railing::IsRailStyleCompatibleWithStair(const AEHB_Stair* Stair) const
{
	if (!Stair || Stair->IsActorBeingDestroyed())
	{
		return false;
	}
	if (!IsRailingSampleCompatibleWithStair(Stair))
	{
		return false;
	}

	const TSoftObjectPtr<UStaticMesh> ThisRailMesh = RailMesh.IsNull()
		? GetDefaultRailingStaticMesh()
		: RailMesh;
	const TSoftObjectPtr<UStaticMesh> StairRailMesh = Stair->StairData.RailingRailMesh.IsNull()
		? GetDefaultRailingStaticMesh()
		: Stair->StairData.RailingRailMesh;
	if (ThisRailMesh.ToSoftObjectPath() != StairRailMesh.ToSoftObjectPath())
	{
		return false;
	}
	if (RailMaterial.ToSoftObjectPath() != Stair->StairData.RailingRailMaterial.ToSoftObjectPath())
	{
		return false;
	}

	return FMath::IsNearlyEqual(RailThickness, Stair->StairData.RailingRailThickness, 0.1f)
		&& FMath::IsNearlyEqual(RailHeight, Stair->StairData.RailingRailHeight, 0.5f);
}

bool AEHB_Railing::IsRailingSampleCompatibleWith(const AEHB_Railing* OtherRailing) const
{
	if (!OtherRailing)
	{
		return false;
	}

	const bool bThisHasSample = HasRailingSampleIdentity();
	const bool bOtherHasSample = OtherRailing->HasRailingSampleIdentity();
	if (!bThisHasSample && !bOtherHasSample)
	{
		return true;
	}
	if (bThisHasSample != bOtherHasSample)
	{
		return false;
	}

	FGuid ThisTemplateGuid;
	FGuid OtherTemplateGuid;
	if (TryGetRailingSampleTemplateGuid(ThisTemplateGuid)
		&& OtherRailing->TryGetRailingSampleTemplateGuid(OtherTemplateGuid))
	{
		return ThisTemplateGuid == OtherTemplateGuid;
	}

	return SampledRailingRow.DataTable == OtherRailing->SampledRailingRow.DataTable
		&& SampledRailingRow.RowName == OtherRailing->SampledRailingRow.RowName;
}

bool AEHB_Railing::IsRailingSampleCompatibleWithStair(const AEHB_Stair* Stair) const
{
	if (!Stair)
	{
		return false;
	}

	const bool bThisHasSample = HasRailingSampleIdentity();
	const bool bStairHasSample =
		Stair->StairData.SampledRailingRow.DataTable != nullptr
		&& !Stair->StairData.SampledRailingRow.RowName.IsNone();
	if (!bThisHasSample && !bStairHasSample)
	{
		return true;
	}
	if (bThisHasSample != bStairHasSample)
	{
		return false;
	}

	FGuid ThisTemplateGuid;
	if (TryGetRailingSampleTemplateGuid(ThisTemplateGuid)
		&& Stair->StairData.SampledRailingRow.DataTable
		&& Stair->StairData.SampledRailingRow.DataTable->GetRowStruct() == FEHBRailingMeshData::StaticStruct())
	{
		const FEHBRailingMeshData* StairRow = Stair->StairData.SampledRailingRow.DataTable->FindRow<FEHBRailingMeshData>(
			Stair->StairData.SampledRailingRow.RowName,
			TEXT("AEHB_Railing::IsRailingSampleCompatibleWithStair"),
			false);
		if (StairRow && StairRow->TemplateMetadata.TemplateGuid.IsValid())
		{
			return ThisTemplateGuid == StairRow->TemplateMetadata.TemplateGuid;
		}
	}

	return SampledRailingRow.DataTable == Stair->StairData.SampledRailingRow.DataTable
		&& SampledRailingRow.RowName == Stair->StairData.SampledRailingRow.RowName;
}

bool AEHB_Railing::HasRailingSampleIdentity() const
{
	return SampledRailingRow.DataTable != nullptr && !SampledRailingRow.RowName.IsNone();
}

bool AEHB_Railing::TryGetRailingSampleTemplateGuid(FGuid& OutTemplateGuid) const
{
	OutTemplateGuid.Invalidate();
	if (!SampledRailingRow.DataTable
		|| SampledRailingRow.RowName.IsNone()
		|| SampledRailingRow.DataTable->GetRowStruct() != FEHBRailingMeshData::StaticStruct())
	{
		return false;
	}

	const FEHBRailingMeshData* Row = SampledRailingRow.DataTable->FindRow<FEHBRailingMeshData>(
		SampledRailingRow.RowName,
		TEXT("AEHB_Railing::TryGetRailingSampleTemplateGuid"),
		false);
	if (!Row || !Row->TemplateMetadata.TemplateGuid.IsValid())
	{
		return false;
	}

	OutTemplateGuid = Row->TemplateMetadata.TemplateGuid;
	return true;
}

bool AEHB_Railing::IsRailEndpointConnectedToCompatibleRailing(bool bStartEndpoint) const
{
	FVector OtherRailDirectionLocal;
	return FindCompatibleRailEndpointConnection(bStartEndpoint, OtherRailDirectionLocal);
}

bool AEHB_Railing::IsDistanceInsideGateOpening(float Distance) const
{
	FGuid GatePostGuid;
	bool bLeftPost = false;
	// 边柱位置不能被当成洞内裁掉。
	if (IsDistanceNearGatePost(Distance, GatePostGuid, bLeftPost))
	{
		return false;
	}

	for (const FEHBRailingGateConnection& Connection : GateConnections)
	{
		const float HalfWidth = Connection.Width * 0.5f;
		if (Distance > Connection.DistanceFromStart - HalfWidth + EHBRailingGateEdgeTolerance
			&& Distance < Connection.DistanceFromStart + HalfWidth - EHBRailingGateEdgeTolerance)
		{
			return true;
		}
	}
	return false;
}

bool AEHB_Railing::IsDistanceNearGatePost(float Distance, FGuid& OutGatePostGuid, bool& bOutLeftPost) const
{
	OutGatePostGuid.Invalidate();
	bOutLeftPost = false;
	for (const FEHBRailingGateConnection& Connection : GateConnections)
	{
		const float HalfWidth = Connection.Width * 0.5f;
		const float LeftDistance = Connection.DistanceFromStart - HalfWidth;
		const float RightDistance = Connection.DistanceFromStart + HalfWidth;
		if (FMath::Abs(Distance - LeftDistance) <= EHBRailingGateEdgeTolerance)
		{
			OutGatePostGuid = Connection.LeftPostGuid;
			bOutLeftPost = true;
			return OutGatePostGuid.IsValid();
		}
		if (FMath::Abs(Distance - RightDistance) <= EHBRailingGateEdgeTolerance)
		{
			OutGatePostGuid = Connection.RightPostGuid;
			bOutLeftPost = false;
			return OutGatePostGuid.IsValid();
		}
	}
	return false;
}

void AEHB_Railing::NormalizeGateConnections()
{
	const float Length = GetRailingLength();
	GateConnections.RemoveAll(
		[Length](const FEHBRailingGateConnection& Connection)
		{
			return !Connection.GateGuid.IsValid() || Length <= UE_SMALL_NUMBER;
		});

	for (FEHBRailingGateConnection& Connection : GateConnections)
	{
		// 任何门洞都必须完整落在当前扶手长度内；路径变短时自动夹紧。
		Connection.Width = FMath::Clamp(FMath::Max(1.0f, Connection.Width), 1.0f, Length);
		Connection.Height = FMath::Max(1.0f, Connection.Height);
		Connection.DistanceFromStart = FMath::Clamp(
			Connection.DistanceFromStart,
			Connection.Width * 0.5f,
			FMath::Max(Connection.Width * 0.5f, Length - Connection.Width * 0.5f));
		if (!Connection.LeftPostGuid.IsValid())
		{
			Connection.LeftPostGuid = FGuid::NewGuid();
		}
		if (!Connection.RightPostGuid.IsValid())
		{
			Connection.RightPostGuid = FGuid::NewGuid();
		}

		FEHBRailingPathSample Sample;
		if (EvaluatePathAtDistance(Connection.DistanceFromStart, Sample))
		{
			Connection.GateLocalToRailing = FTransform(
				MakePathRotation(Sample.LocalForward),
				Sample.LocalLocation,
				FVector::OneVector);
		}
	}
}

void AEHB_Railing::ApplyMaterials()
{
	if (PostMeshComponent)
	{
		PostMeshComponent->SetMaterial(0, ResolveRailingMaterial(PostMaterial));
	}

	UMaterialInterface* LoadedRailMaterial = ResolveRailingMaterial(RailMaterial);
	for (USplineMeshComponent* RailComponent : RailMeshComponents)
	{
		if (RailComponent)
		{
			RailComponent->SetMaterial(0, LoadedRailMaterial);
		}
	}
	if (RailGeneratedMeshComponent)
	{
		RailGeneratedMeshComponent->SetMaterialIfChanged(0, LoadedRailMaterial);
	}

	if (PanelMeshComponent)
	{
		PanelMeshComponent->SetMaterialIfChanged(0, ResolveRailingMaterial(PanelMaterial));
	}
	if (PanelSampleMeshComponent)
	{
		PanelSampleMeshComponent->SetMaterial(0, ResolveRailingMaterial(PanelMaterial));
	}
}
