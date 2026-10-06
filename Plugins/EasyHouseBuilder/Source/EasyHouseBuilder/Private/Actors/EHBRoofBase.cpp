#include "Actors/EHBRoofBase.h"

#include "Actors/EHB_Wall.h"
#include "Components/EHBGeneratedMeshComponent.h"
#include "Core/EHBBuildingActorBase.h"
#include "Cutting/EHBCutSourceBuilder.h"
#include "Cutting/EHBGeneratedMeshCollector.h"
#include "Cutting/EHBManifoldBoolean.h"
#include "DrawDebugHelpers.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/Material.h"
#include "MaterialDomain.h"
#include "Selections/MeshConnectedComponents.h"
#include "Settings/EHBBuildingToolsetSettings.h"

using UE::Geometry::FDynamicMesh3;
using UE::Geometry::FIndex3i;

DEFINE_LOG_CATEGORY_STATIC(LogEHBRoof, Log, All);

namespace
{
	constexpr double EHBRoofBooleanMergeTolerance = 0.01;
	constexpr int32 EHBRoofBooleanSlopeGroupID = 1;
	constexpr int32 EHBRoofBooleanSideGroupID = 2;

	UMaterialInterface* LoadConfiguredMaterial(const TSoftObjectPtr<UMaterialInterface>& Material)
	{
		return Material.IsNull() ? nullptr : Material.LoadSynchronous();
	}

	int32 AppendGeneratedTriangleAsDynamic(
		const FVector& A,
		const FVector& B,
		const FVector& C,
		FDynamicMesh3& OutMesh)
	{
		const int32 DynamicA = OutMesh.AppendVertex(FVector3d(A));
		const int32 DynamicB = OutMesh.AppendVertex(FVector3d(B));
		const int32 DynamicC = OutMesh.AppendVertex(FVector3d(C));
		return OutMesh.AppendTriangle(DynamicA, DynamicC, DynamicB);
	}

	bool ConvertAggregateDataToDynamicMesh(const FEHBMeshAggregateData& AggregateData, FDynamicMesh3& OutMesh)
	{
		OutMesh.Clear();
		if (AggregateData.Vertices.IsEmpty() || AggregateData.Triangles.IsEmpty())
		{
			return false;
		}

		OutMesh.EnableTriangleGroups(0);
		for (const FEHBMeshTriangleRef& TriangleRef : AggregateData.Triangles)
		{
			if (!AggregateData.Vertices.IsValidIndex(TriangleRef.VertexA)
				|| !AggregateData.Vertices.IsValidIndex(TriangleRef.VertexB)
				|| !AggregateData.Vertices.IsValidIndex(TriangleRef.VertexC))
			{
				continue;
			}

			const int32 TriangleID = AppendGeneratedTriangleAsDynamic(
				AggregateData.Vertices[TriangleRef.VertexA],
				AggregateData.Vertices[TriangleRef.VertexB],
				AggregateData.Vertices[TriangleRef.VertexC],
				OutMesh);
			if (TriangleID >= 0)
			{
				OutMesh.SetTriangleGroup(TriangleID, TriangleRef.SourceSectionIndex);
			}
		}

		return OutMesh.TriangleCount() > 0;
	}

	FString FormatBox(const FBox& Box)
	{
		if (!Box.IsValid)
		{
			return TEXT("Invalid");
		}

		return FString::Printf(
			TEXT("Min=%s Max=%s Size=%s"),
			*Box.Min.ToString(),
			*Box.Max.ToString(),
			*Box.GetSize().ToString());
	}

	FBox CalculateUnifiedMeshBounds(const FEHBRoofUnifiedMeshData& MeshData)
	{
		FBox Bounds(ForceInit);
		for (const FVector& Vertex : MeshData.Vertices)
		{
			Bounds += Vertex;
		}
		return Bounds;
	}

	FBox CalculateDynamicMeshBounds(const FDynamicMesh3& Mesh)
	{
		FBox Bounds(ForceInit);
		for (const int32 VertexID : Mesh.VertexIndicesItr())
		{
			Bounds += FVector(Mesh.GetVertex(VertexID));
		}
		return Bounds;
	}

}

AEHBRoofBase::AEHBRoofBase()
{
	ElementType = EEHBBuildingElementType::Roof;
	FloorRole = EEHBBuildingFloorElementRole::Roof;
	FloorAssignmentPolicy = EEHBFloorAssignmentPolicy::Automatic;
	ElementName = TEXT("Roof");
	SemanticTags.AddUnique(TEXT("Envelope.Roof"));

	RoofBodyMeshComponent = CreateDefaultSubobject<UEHBGeneratedMeshComponent>(TEXT("RoofBody"));
	RoofBodyMeshComponent->SetupAttachment(SceneRoot);
	RoofBodyMeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	RoofBodyMeshComponent->SetCollisionObjectType(ECC_WorldStatic);
	RoofBodyMeshComponent->SetCollisionResponseToAllChannels(ECR_Block);
	RoofBodyMeshComponent->ComponentTags.AddUnique(TEXT("EHB_RoofV2_Body"));
}

FName AEHBRoofBase::AutoCollisionCutTag()
{
	return TEXT("RoofAutoCollisionV2");
}

void AEHBRoofBase::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RebuildRoofMesh();
}

#if WITH_EDITOR
void AEHBRoofBase::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		RebuildRoofMesh();
	}
}
#endif

void AEHBRoofBase::GetGeneratedMeshComponents(TArray<UEHBGeneratedMeshComponent*>& OutComponents) const
{
	OutComponents.Reset();
	if (RoofBodyMeshComponent)
	{
		OutComponents.Add(RoofBodyMeshComponent);
	}
}

FEHBResolvedDefaultRoofMaterials AEHBRoofBase::ResolveConfiguredDefaultRoofMaterials() const
{
	FEHBResolvedDefaultRoofMaterials Materials;

	UMaterialInterface* WhiteBoxFallback = nullptr;
	if (const UEHBBuildingToolsetSettings* Settings = GetDefault<UEHBBuildingToolsetSettings>())
	{
		WhiteBoxFallback = LoadConfiguredMaterial(Settings->DefaultWhiteBoxMaterial);
		Materials.Slope = LoadConfiguredMaterial(Settings->DefaultRoofSlopeMaterial);
		Materials.SideWall = LoadConfiguredMaterial(Settings->DefaultRoofSideWallMaterial);
		Materials.Ridge = LoadConfiguredMaterial(Settings->DefaultRoofRidgeMaterial);
		Materials.Eave = LoadConfiguredMaterial(Settings->DefaultRoofEaveMaterial);
		Materials.DiagonalRidge = LoadConfiguredMaterial(Settings->DefaultRoofDiagonalRidgeMaterial);
	}

	UMaterialInterface* SurfaceFallback = WhiteBoxFallback ? WhiteBoxFallback : UMaterial::GetDefaultMaterial(MD_Surface);
	Materials.Slope = Materials.Slope ? Materials.Slope : SurfaceFallback;
	Materials.SideWall = Materials.SideWall ? Materials.SideWall : Materials.Slope;
	Materials.Ridge = Materials.Ridge ? Materials.Ridge : Materials.Slope;
	Materials.Eave = Materials.Eave ? Materials.Eave : Materials.Slope;
	Materials.DiagonalRidge = Materials.DiagonalRidge ? Materials.DiagonalRidge : Materials.Ridge;
	return Materials;
}

bool AEHBRoofBase::ApplyMaterialToRoofComponent(
	UPrimitiveComponent* HitComponent,
	UMaterialInterface* Material,
	bool bApplyAllRoofParts)
{
	if (!Material || !RoofBodyMeshComponent)
	{
		return false;
	}

	if (!bApplyAllRoofParts && HitComponent && HitComponent != RoofBodyMeshComponent)
	{
		return false;
	}

	Modify();
	RoofBodyMeshComponent->Modify();
	RoofBodyMaterial = Material;
	RoofBodyMeshComponent->SetMaterialIfChanged(0, Material);
	RoofBodyMeshComponent->MarkPackageDirty();
	MarkPackageDirty();
	return true;
}

void AEHBRoofBase::OnElementActorMoved_Implementation(const FTransform& OldLocalTransform, const FTransform& NewLocalTransform, bool bFinished)
{
	Super::OnElementActorMoved_Implementation(OldLocalTransform, NewLocalTransform, bFinished);

	UE_LOG(
		LogEHBRoof,
		Verbose,
		TEXT("[EHB RoofV2] OnElementActorMoved roof=%s finished=%d cutColliding=%d old=%s new=%s"),
		*GetName(),
		bFinished ? 1 : 0,
		bCutCollidingElements ? 1 : 0,
		*OldLocalTransform.GetLocation().ToString(),
		*NewLocalTransform.GetLocation().ToString());

	if (bFinished)
	{
		RefreshAutoCollisionCutOperations();
	}
}

bool AEHBRoofBase::RebuildRoofMesh()
{
	FEHBRoofUnifiedMeshData MeshData;
	if (!BuildRawRoofMesh(MeshData) || !MeshData.IsValid())
	{
		UE_LOG(LogEHBRoof, Warning, TEXT("[EHB RoofV2 CutTrace] RebuildRoofMesh raw build failed roof=%s"), *GetName());
		if (RoofBodyMeshComponent)
		{
			RoofBodyMeshComponent->ClearAllMeshSections();
		}
		return false;
	}

	const int32 RawTriangleCount = MeshData.Triangles.Num() / 3;
	const FBox RawBounds = CalculateUnifiedMeshBounds(MeshData);
	if (bDebugDrawRawRoofBounds)
	{
		DrawRoofCutDebugBounds(RawBounds, FColor::Cyan, TEXT("raw roof"));
	}
	UE_LOG(
		LogEHBRoof,
		Verbose,
		TEXT("[EHB RoofV2 CutTrace] RebuildRoofMesh begin roof=%s rawTriangles=%d cutOps=%d rawBounds=%s"),
		*GetName(),
		RawTriangleCount,
		CutOperations.Num(),
		*FormatBox(RawBounds));

	if (!ApplySourceMeshCuts(MeshData))
	{
		UE_LOG(LogEHBRoof, Warning, TEXT("[EHB RoofV2] RebuildRoofMesh failed while applying cuts roof=%s"), *GetName());
		return false;
	}

	const int32 CutTriangleCount = MeshData.Triangles.Num() / 3;
	const FBox CutBounds = CalculateUnifiedMeshBounds(MeshData);
	if (bDebugDrawResultBounds)
	{
		DrawRoofCutDebugBounds(CutBounds, FColor::Green, TEXT("cut result"));
	}
	UE_LOG(
		LogEHBRoof,
		Verbose,
		TEXT("[EHB RoofV2 CutTrace] RebuildRoofMesh after cuts roof=%s rawTriangles=%d cutTriangles=%d cutBounds=%s"),
		*GetName(),
		RawTriangleCount,
		CutTriangleCount,
		*FormatBox(CutBounds));

	if (!SubmitRoofBodyMesh(MeshData))
	{
		UE_LOG(LogEHBRoof, Warning, TEXT("[EHB RoofV2 CutTrace] RebuildRoofMesh submit failed roof=%s"), *GetName());
		return false;
	}

	RebuildRoofAccessories(MeshData);
	MarkPackageDirty();
	return true;
}

bool AEHBRoofBase::RefreshAutoCollisionCutOperations()
{
	TArray<AEHBElementActorBase*> Candidates;
	CollectAutoCutCandidates(Candidates);

	const FBox SelfBounds = GetComponentsBoundingBox(true);
	UE_LOG(
		LogEHBRoof,
		Display,
		TEXT("[EHB WallFootprintCut] RefreshAutoCollision begin roof=%s cutColliding=%d useWallFootprint=%d minWallCount=%d endpointTol=%.2f padding=%.2f maxDim=%.2f existingCutOps=%d candidates=%d selfBounds=%s"),
		*GetName(),
		bCutCollidingElements ? 1 : 0,
		bUseWallFootprintCutters ? 1 : 0,
		MinWallFootprintGroupWallCount,
		WallFootprintGroupEndpointTolerance,
		WallFootprintPadding,
		WallFootprintMaxDimension,
		CutOperations.Num(),
		Candidates.Num(),
		*FormatBox(SelfBounds));

	bool bSelfChanged = false;
	bool bOtherChanged = false;
	int32 OverlapCount = 0;

	if (!bCutCollidingElements)
	{
		const bool bRemoved = RemoveAutoCollisionCutOperations(this, FGuid());
		bSelfChanged |= bRemoved;
		UE_LOG(
			LogEHBRoof,
			Verbose,
			TEXT("[EHB RoofV2 CutTrace] RefreshAutoCollision self cutting disabled roof=%s removedSelfAutoOps=%d"),
			*GetName(),
			bRemoved ? 1 : 0);
	}

	for (AEHBElementActorBase* Candidate : Candidates)
	{
		if (!IsValid(Candidate) || Candidate == this)
		{
			continue;
		}

		const bool bOverlaps = DoesOverlapElementForAutoCut(Candidate);
		const FBox CandidateBounds = Candidate->GetComponentsBoundingBox(true);
		if (bOverlaps)
		{
			++OverlapCount;
		}

		const AEHBRoofBase* CandidateRoof = Cast<AEHBRoofBase>(Candidate);
		const bool bCandidateIsWall = Candidate->IsA<AEHB_Wall>();
		if (bCandidateIsWall || bOverlaps)
		{
			UE_LOG(
				LogEHBRoof,
				Display,
				TEXT("[EHB WallFootprintCut] AutoCut candidate target=%s candidate=%s class=%s isWall=%d overlaps=%d selfCut=%d candidateCut=%d candidateBounds=%s"),
				*GetName(),
				*Candidate->GetName(),
				*Candidate->GetClass()->GetName(),
				bCandidateIsWall ? 1 : 0,
				bOverlaps ? 1 : 0,
				bCutCollidingElements ? 1 : 0,
				(CandidateRoof && CandidateRoof->bCutCollidingElements) ? 1 : 0,
				*FormatBox(CandidateBounds));
		}

		if (bCutCollidingElements && bOverlaps)
		{
			bSelfChanged |= UpsertAutoCollisionCutOperation(this, Candidate);
		}
		else
		{
			bSelfChanged |= RemoveAutoCollisionCutOperations(this, Candidate->ElementGuid);
		}

		if (CandidateRoof && CandidateRoof->bCutCollidingElements && bOverlaps)
		{
			if (UpsertAutoCollisionCutOperation(Candidate, this))
			{
				bOtherChanged = true;
				RebuildCuttableRoofTarget(Candidate);
			}
		}
		else if (RemoveAutoCollisionCutOperations(Candidate, ElementGuid))
		{
			bOtherChanged = true;
			RebuildCuttableRoofTarget(Candidate);
		}
	}

	if (bSelfChanged)
	{
		RebuildRoofMesh();
	}

	UE_LOG(
		LogEHBRoof,
		Display,
		TEXT("[EHB WallFootprintCut] RefreshAutoCollision end roof=%s candidates=%d overlaps=%d selfChanged=%d otherChanged=%d cutOps=%d"),
		*GetName(),
		Candidates.Num(),
		OverlapCount,
		bSelfChanged ? 1 : 0,
		bOtherChanged ? 1 : 0,
		CutOperations.Num());

	return bSelfChanged || bOtherChanged;
}

bool AEHBRoofBase::RedrawRoofCutDebug()
{
	if (!IsRoofCutDebugEnabled())
	{
		return false;
	}

	FEHBRoofUnifiedMeshData MeshData;
	if (!BuildRawRoofMesh(MeshData) || !MeshData.IsValid())
	{
		return false;
	}

	if (bDebugDrawRawRoofBounds)
	{
		DrawRoofCutDebugMeshBounds(MeshData, FColor::Cyan, TEXT("raw roof"));
	}

	if (!CutOperations.IsEmpty())
	{
		FEHBRoofUnifiedMeshData DebugCutMesh = MeshData;
		if (ApplySourceMeshCuts(DebugCutMesh, false) && DebugCutMesh.IsValid() && bDebugDrawResultBounds)
		{
			DrawRoofCutDebugMeshBounds(DebugCutMesh, FColor::Green, TEXT("cut result"));
		}
	}
	else if (bDebugDrawResultBounds)
	{
		DrawRoofCutDebugMeshBounds(MeshData, FColor::Green, TEXT("render result"));
	}

	return true;
}

bool AEHBRoofBase::GetRoofUnifiedMesh(FEHBRoofUnifiedMeshData& OutMesh) const
{
	return BuildRawRoofMesh(OutMesh);
}

bool AEHBRoofBase::GetRoofProjectionBounds(FEHBRoofProjectionBounds& OutBounds) const
{
	OutBounds.Reset();

	FEHBRoofUnifiedMeshData MeshData;
	if (!BuildRawRoofMesh(MeshData) || !MeshData.IsValid())
	{
		return false;
	}

	FBox Bounds(ForceInit);
	for (const FVector& Vertex : MeshData.Vertices)
	{
		Bounds += Vertex;
	}

	if (!Bounds.IsValid)
	{
		return false;
	}

	OutBounds.bIsValid = true;
	OutBounds.Min = FVector2D(Bounds.Min.X, Bounds.Min.Y);
	OutBounds.Max = FVector2D(Bounds.Max.X, Bounds.Max.Y);
	OutBounds.LocalCenter = Bounds.GetCenter();
	OutBounds.LocalSize = Bounds.GetSize();
	return true;
}

void AEHBRoofBase::RebuildRoofAccessories(const FEHBRoofUnifiedMeshData& CutBodyMesh)
{
}

bool AEHBRoofBase::SubmitRoofBodyMesh(const FEHBRoofUnifiedMeshData& MeshData)
{
	if (!RoofBodyMeshComponent || !MeshData.IsValid())
	{
		return false;
	}

	FEHBScopedGeneratedMeshUpdate ScopedMeshUpdate(RoofBodyMeshComponent);
	RoofBodyMeshComponent->ClearAllMeshSections();

	TArray<FVector> Normals = MeshData.Normals;
	if (Normals.Num() != MeshData.Vertices.Num())
	{
		Normals.Init(FVector::UpVector, MeshData.Vertices.Num());
	}

	TArray<FVector2D> UV0 = MeshData.UV0;
	if (UV0.Num() != MeshData.Vertices.Num())
	{
		UV0.Init(FVector2D::ZeroVector, MeshData.Vertices.Num());
	}

	TArray<FLinearColor> VertexColors;
	VertexColors.Init(FLinearColor::White, MeshData.Vertices.Num());

	TArray<FEHBMeshTangent> Tangents;
	Tangents.Init(FEHBMeshTangent(), MeshData.Vertices.Num());

	RoofBodyMeshComponent->CreateMeshSection_LinearColor(
		0,
		MeshData.Vertices,
		MeshData.Triangles,
		Normals,
		UV0,
		VertexColors,
		Tangents,
		true);
	RoofBodyMeshComponent->SetMeshSectionName(0, TEXT("RoofBody"));
	const FEHBResolvedDefaultRoofMaterials DefaultRoofMaterials = ResolveConfiguredDefaultRoofMaterials();
	RoofBodyMeshComponent->SetMaterialIfChanged(
		0,
		RoofBodyMaterial ? RoofBodyMaterial.Get() : DefaultRoofMaterials.Slope);
	return true;
}

bool AEHBRoofBase::ApplySourceMeshCuts(FEHBRoofUnifiedMeshData& InOutMesh, bool bFilterDisconnectedPieces) const
{
	TArray<const FEHBCutOperation*> SourceMeshOperations;
	for (const FEHBCutOperation& Operation : CutOperations)
	{
		if (!Operation.bEnabled
			|| Operation.Stage != EEHBCutStage::SourceOverlap
			|| Operation.ProjectionMode != EEHBCutProjectionMode::SourceMesh
			|| Operation.OperationType != EEHBCutOperationType::Subtract
			|| Operation.Source.SourceType != EEHBCutSourceType::Element)
		{
			continue;
		}

		SourceMeshOperations.Add(&Operation);
	}

	UE_LOG(
		LogEHBRoof,
		Display,
		TEXT("[EHB WallFootprintCut] ApplySourceMeshCuts scan roof=%s totalCutOps=%d sourceMeshOps=%d inputTriangles=%d filterDisconnected=%d inputBounds=%s"),
		*GetName(),
		CutOperations.Num(),
		SourceMeshOperations.Num(),
		InOutMesh.Triangles.Num() / 3,
		bFilterDisconnectedPieces ? 1 : 0,
		*FormatBox(CalculateUnifiedMeshBounds(InOutMesh)));

	if (SourceMeshOperations.IsEmpty())
	{
		UE_LOG(LogEHBRoof, Display, TEXT("[EHB WallFootprintCut] ApplySourceMeshCuts skipped roof=%s reason=no source mesh operations"), *GetName());
		return true;
	}

	SourceMeshOperations.Sort(
		[](const FEHBCutOperation& A, const FEHBCutOperation& B)
		{
			return A.Priority < B.Priority;
		});

	FDynamicMesh3 TargetMesh;
	if (!ConvertUnifiedMeshToDynamicMesh(InOutMesh, TargetMesh))
	{
		UE_LOG(LogEHBRoof, Warning, TEXT("[EHB RoofV2] Target mesh conversion failed roof=%s"), *GetName());
		return false;
	}

	const FBox InitialTargetBounds = CalculateDynamicMeshBounds(TargetMesh);
	UE_LOG(
		LogEHBRoof,
		Verbose,
		TEXT("[EHB RoofV2 CutTrace] Target dynamic mesh roof=%s triangles=%d vertices=%d bounds=%s"),
		*GetName(),
		TargetMesh.TriangleCount(),
		TargetMesh.VertexCount(),
		*FormatBox(InitialTargetBounds));

	TArray<AEHBElementActorBase*> SourceElements;
	SourceElements.Reserve(SourceMeshOperations.Num());
	for (const FEHBCutOperation* Operation : SourceMeshOperations)
	{
		if (!Operation)
		{
			continue;
		}

		AEHBElementActorBase* SourceElement = nullptr;
		if (!ResolveCutSourceElement(*Operation, SourceElement) || !IsValid(SourceElement) || SourceElement == this)
		{
			UE_LOG(
				LogEHBRoof,
				Warning,
				TEXT("[EHB RoofV2 CutTrace] Source operation unresolved target=%s opGuid=%s sourceGuid=%s sourcePtr=%s"),
				*GetName(),
				*Operation->OperationGuid.ToString(),
				*Operation->Source.SourceElementGuid.ToString(),
				Operation->Source.SourceElement ? *Operation->Source.SourceElement->GetName() : TEXT("None"));
			continue;
		}

		TArray<UEHBGeneratedMeshComponent*> SourceComponents;
		SourceElement->GetGeneratedMeshComponents(SourceComponents);
		UE_LOG(
			LogEHBRoof,
			Display,
			TEXT("[EHB WallFootprintCut] Resolve source target=%s source=%s class=%s isWall=%d opGuid=%s componentCount=%d sourceWorldBounds=%s"),
			*GetName(),
			*SourceElement->GetName(),
			*SourceElement->GetClass()->GetName(),
			SourceElement->IsA<AEHB_Wall>() ? 1 : 0,
			*Operation->OperationGuid.ToString(),
			SourceComponents.Num(),
			*FormatBox(SourceElement->GetComponentsBoundingBox(true)));

		SourceElements.AddUnique(SourceElement);
	}

	FEHBCutSourceBuildContext CutSourceContext;
	CutSourceContext.TargetElement = this;
	CutSourceContext.TargetLocalToWorld = GetActorTransform();
	CutSourceContext.Intent = EEHBCutSourceIntent::RoofIntersection;
	CutSourceContext.bUseWallFootprintCutters = bUseWallFootprintCutters;
	CutSourceContext.MinWallFootprintGroupWallCount = MinWallFootprintGroupWallCount;
	CutSourceContext.WallFootprintGroupEndpointTolerance = WallFootprintGroupEndpointTolerance;
	CutSourceContext.WallFootprintPadding = WallFootprintPadding;
	CutSourceContext.WallFootprintMaxDimension = WallFootprintMaxDimension;
	CutSourceContext.WallFootprintMinArea = WallFootprintMinArea;
	CutSourceContext.WallFootprintZPadding = EnvelopeCutOptions.ZPadding;
	CutSourceContext.WallFootprintMinExtrudeHeight = EnvelopeCutOptions.MinExtrudeHeight;

	TArray<FEHBResolvedCutSourceData> ResolvedCutSources;
	FString CutSourceFailureReason;
	if (!FEHBCutSourceBuilder::BuildSources(SourceElements, CutSourceContext, ResolvedCutSources, &CutSourceFailureReason))
	{
		UE_LOG(
			LogEHBRoof,
			Display,
			TEXT("[EHB WallFootprintCut] ApplySourceMeshCuts skipped roof=%s reason=no resolved cut sources details=%s"),
			*GetName(),
			*CutSourceFailureReason);
		return true;
	}
	UE_LOG(
		LogEHBRoof,
		Display,
		TEXT("[EHB WallFootprintCut] Resolved cut sources roof=%s inputSourceElements=%d resolvedSources=%d"),
		*GetName(),
		SourceElements.Num(),
		ResolvedCutSources.Num());

	TArray<FEHBMeshAggregateData> SourceAggregates;
	SourceAggregates.Reserve(ResolvedCutSources.Num());
	for (const FEHBResolvedCutSourceData& ResolvedSource : ResolvedCutSources)
	{
		if (bDebugDrawSourceBounds)
		{
			DrawRoofCutDebugBounds(
				ResolvedSource.TargetLocalBounds,
				FColor::Orange,
				FString::Printf(TEXT("source: %s"), *ResolvedSource.DebugName.ToString()));
		}

		if (ResolvedSource.GeometryKind == EEHBCutSourceGeometryKind::MeshAggregate3D)
		{
			UE_LOG(
				LogEHBRoof,
				Display,
				TEXT("[EHB WallFootprintCut] Resolved mesh aggregate source target=%s source=%s vertices=%d triangles=%d targetLocalBounds=%s"),
				*GetName(),
				*ResolvedSource.DebugName.ToString(),
				ResolvedSource.MeshAggregate.Vertices.Num(),
				ResolvedSource.MeshAggregate.Triangles.Num(),
				*FormatBox(ResolvedSource.MeshAggregate.LocalBounds));
			SourceAggregates.Add(ResolvedSource.MeshAggregate);
		}
		else if (ResolvedSource.GeometryKind == EEHBCutSourceGeometryKind::OrientedQuadFootprint3D)
		{
			UE_LOG(
				LogEHBRoof,
				Display,
				TEXT("[EHB WallFootprintCut] Resolved wall footprint source target=%s source=%s sourceGuids=%d footprintPoints=%d z=(%.2f, %.2f) targetLocalBounds=%s"),
				*GetName(),
				*ResolvedSource.DebugName.ToString(),
				ResolvedSource.SourceElementGuids.Num(),
				ResolvedSource.TargetLocalFootprint.Num(),
				ResolvedSource.MinZ,
				ResolvedSource.MaxZ,
				*FormatBox(ResolvedSource.TargetLocalBounds));
		}
	}

	bool bAppliedDirectCutters = false;
	for (int32 SourceIndex = 0; SourceIndex < ResolvedCutSources.Num(); ++SourceIndex)
	{
		const FEHBResolvedCutSourceData& ResolvedSource = ResolvedCutSources[SourceIndex];
		if (ResolvedSource.GeometryKind != EEHBCutSourceGeometryKind::OrientedQuadFootprint3D)
		{
			continue;
		}

		FDynamicMesh3 DirectSourceMesh;
		FString DirectSourceFailureReason;
		if (!FEHBCutSourceBuilder::BuildDynamicMeshForSource(ResolvedSource, DirectSourceMesh, &DirectSourceFailureReason))
		{
			UE_LOG(
				LogEHBRoof,
				Warning,
				TEXT("[EHB RoofV2] Direct cut source build failed target=%s source=%s reason=%s"),
				*GetName(),
				*ResolvedSource.DebugName.ToString(),
				*DirectSourceFailureReason);
			return false;
		}

		const FBox SourceBounds = CalculateDynamicMeshBounds(DirectSourceMesh);
		const FBox TargetBoundsBefore = CalculateDynamicMeshBounds(TargetMesh);
		const bool bBoundsIntersect = SourceBounds.IsValid
			&& TargetBoundsBefore.IsValid
			&& SourceBounds.Intersect(TargetBoundsBefore);
		UE_LOG(
			LogEHBRoof,
			Display,
			TEXT("[EHB WallFootprintCut] Direct source cutter target=%s source=%s index=%d triangles=%d vertices=%d bounds=%s targetBeforeBounds=%s boundsIntersect=%d"),
			*GetName(),
			*ResolvedSource.DebugName.ToString(),
			SourceIndex,
			DirectSourceMesh.TriangleCount(),
			DirectSourceMesh.VertexCount(),
			*FormatBox(SourceBounds),
			*FormatBox(TargetBoundsBefore),
			bBoundsIntersect ? 1 : 0);

		if (bDebugDrawCutterBounds)
		{
			DrawRoofCutDebugBounds(
				SourceBounds,
				FColor::Magenta,
				FString::Printf(TEXT("direct cutter %d"), SourceIndex));
		}

		FDynamicMesh3 DifferenceMesh;
		FString FailureReason;
		const bool bApplied = FEHBManifoldBoolean::ApplyDifference(
			TargetMesh,
			DirectSourceMesh,
			DifferenceMesh,
			EHBRoofBooleanMergeTolerance,
			EHBRoofBooleanSlopeGroupID,
			EHBRoofBooleanSideGroupID,
			&FailureReason);
		if (!bApplied)
		{
			UE_LOG(
				LogEHBRoof,
				Warning,
				TEXT("[EHB RoofV2] Manifold direct difference failed target=%s source=%s reason=%s"),
				*GetName(),
				*ResolvedSource.DebugName.ToString(),
				*FailureReason);
			return false;
		}

		UE_LOG(
			LogEHBRoof,
			Display,
			TEXT("[EHB WallFootprintCut] Manifold direct difference applied target=%s source=%s before=%d sourceTri=%d after=%d afterBounds=%s"),
			*GetName(),
			*ResolvedSource.DebugName.ToString(),
			TargetMesh.TriangleCount(),
			DirectSourceMesh.TriangleCount(),
			DifferenceMesh.TriangleCount(),
			*FormatBox(CalculateDynamicMeshBounds(DifferenceMesh)));
		TargetMesh = MoveTemp(DifferenceMesh);
		bAppliedDirectCutters = true;
	}

	if (bUseExactSourceMeshCutters)
	{
		for (int32 AggregateIndex = 0; AggregateIndex < SourceAggregates.Num(); ++AggregateIndex)
		{
			FDynamicMesh3 ExactSourceMesh;
			if (!ConvertAggregateDataToDynamicMesh(SourceAggregates[AggregateIndex], ExactSourceMesh))
			{
				UE_LOG(
					LogEHBRoof,
					Warning,
					TEXT("[EHB RoofV2] Exact source mesh conversion failed target=%s aggregate=%d vertices=%d triangles=%d"),
					*GetName(),
					AggregateIndex,
					SourceAggregates[AggregateIndex].Vertices.Num(),
					SourceAggregates[AggregateIndex].Triangles.Num());
				return false;
			}

			const FBox SourceBounds = CalculateDynamicMeshBounds(ExactSourceMesh);
			const FBox TargetBoundsBefore = CalculateDynamicMeshBounds(TargetMesh);
			const bool bBoundsIntersect = SourceBounds.IsValid
				&& TargetBoundsBefore.IsValid
				&& SourceBounds.Intersect(TargetBoundsBefore);
			UE_LOG(
				LogEHBRoof,
				Display,
				TEXT("[EHB RoofV2 CutTrace] Exact source cutter target=%s aggregate=%d sourceTriangles=%d sourceVertices=%d sourceBounds=%s targetBeforeBounds=%s boundsIntersect=%d"),
				*GetName(),
				AggregateIndex,
				ExactSourceMesh.TriangleCount(),
				ExactSourceMesh.VertexCount(),
				*FormatBox(SourceBounds),
				*FormatBox(TargetBoundsBefore),
				bBoundsIntersect ? 1 : 0);

			if (bDebugDrawCutterBounds)
			{
				DrawRoofCutDebugBounds(
					SourceBounds,
					FColor::Red,
					FString::Printf(TEXT("exact cutter %d"), AggregateIndex));
			}

			FDynamicMesh3 DifferenceMesh;
			FString FailureReason;
			const bool bApplied = FEHBManifoldBoolean::ApplyDifference(
				TargetMesh,
				ExactSourceMesh,
				DifferenceMesh,
				EHBRoofBooleanMergeTolerance,
				EHBRoofBooleanSlopeGroupID,
				EHBRoofBooleanSideGroupID,
				&FailureReason);
			if (!bApplied)
			{
				UE_LOG(
					LogEHBRoof,
					Warning,
					TEXT("[EHB RoofV2] Manifold exact difference failed target=%s aggregate=%d reason=%s"),
					*GetName(),
					AggregateIndex,
					*FailureReason);
				return false;
			}

			UE_LOG(
				LogEHBRoof,
				Display,
				TEXT("[EHB RoofV2 CutTrace] Manifold exact difference applied target=%s aggregate=%d before=%d sourceTri=%d after=%d afterBounds=%s"),
				*GetName(),
				AggregateIndex,
				TargetMesh.TriangleCount(),
				ExactSourceMesh.TriangleCount(),
				DifferenceMesh.TriangleCount(),
				*FormatBox(CalculateDynamicMeshBounds(DifferenceMesh)));
			TargetMesh = MoveTemp(DifferenceMesh);
		}

		if (bFilterDisconnectedPieces && !FilterDisconnectedCutPieces(TargetMesh))
		{
			return false;
		}

		UE_LOG(
			LogEHBRoof,
			Display,
			TEXT("[EHB RoofV2 CutTrace] ApplySourceMeshCuts exact final roof=%s outputTriangles=%d outputBounds=%s"),
			*GetName(),
			TargetMesh.TriangleCount(),
			*FormatBox(CalculateDynamicMeshBounds(TargetMesh)));

		if (bDebugDrawResultBounds)
		{
			DrawRoofCutDebugDynamicBounds(TargetMesh, FColor::Green, TEXT("exact cut result"));
		}

		return ConvertDynamicMeshToUnifiedMesh(TargetMesh, InOutMesh);
	}

	if (SourceAggregates.IsEmpty())
	{
		if (bFilterDisconnectedPieces && !FilterDisconnectedCutPieces(TargetMesh))
		{
			return false;
		}

		UE_LOG(
			LogEHBRoof,
			Verbose,
			TEXT("[EHB RoofV2 CutTrace] ApplySourceMeshCuts final roof=%s directOnly=%d outputTriangles=%d outputBounds=%s"),
			*GetName(),
			bAppliedDirectCutters ? 1 : 0,
			TargetMesh.TriangleCount(),
			*FormatBox(CalculateDynamicMeshBounds(TargetMesh)));

		if (bDebugDrawResultBounds)
		{
			DrawRoofCutDebugDynamicBounds(TargetMesh, FColor::Green, TEXT("direct cut result"));
		}

		return ConvertDynamicMeshToUnifiedMesh(TargetMesh, InOutMesh);
	}

	FEHBMeshEnvelopeBuildOptions EffectiveEnvelopeOptions = EnvelopeCutOptions;
	if (!bUseControlledEnvelopeCutters)
	{
		EffectiveEnvelopeOptions.ConcavityBridgeDistance = 0.0f;
		EffectiveEnvelopeOptions.ProjectionPadding = 0.0f;
	}

	TArray<FDynamicMesh3> SourceEnvelopeMeshes;
	FString EnvelopeFailureReason;
	if (!FEHBMeshEnvelopeBuilder::BuildEnvelopeMeshes(
			SourceAggregates,
			EffectiveEnvelopeOptions,
			SourceEnvelopeMeshes,
			&EnvelopeFailureReason))
	{
		UE_LOG(
			LogEHBRoof,
			Warning,
			TEXT("[EHB RoofV2] Source envelope failed target=%s sources=%d reason=%s"),
			*GetName(),
			SourceAggregates.Num(),
			*EnvelopeFailureReason);
		return false;
	}

	UE_LOG(
		LogEHBRoof,
		Display,
		TEXT("[EHB RoofV2 CutTrace] Source envelopes built target=%s sources=%d envelopes=%d controlled=%d bridge=%.2f padding=%.2f zPadding=%.2f"),
		*GetName(),
		SourceAggregates.Num(),
		SourceEnvelopeMeshes.Num(),
		bUseControlledEnvelopeCutters ? 1 : 0,
		EffectiveEnvelopeOptions.ConcavityBridgeDistance,
		EffectiveEnvelopeOptions.ProjectionPadding,
		EffectiveEnvelopeOptions.ZPadding);

	for (int32 EnvelopeIndex = 0; EnvelopeIndex < SourceEnvelopeMeshes.Num(); ++EnvelopeIndex)
	{
		const FDynamicMesh3& SourceMesh = SourceEnvelopeMeshes[EnvelopeIndex];
		const FBox SourceBounds = CalculateDynamicMeshBounds(SourceMesh);
		const FBox TargetBoundsBefore = CalculateDynamicMeshBounds(TargetMesh);
		const bool bBoundsIntersect = SourceBounds.IsValid
			&& TargetBoundsBefore.IsValid
			&& SourceBounds.Intersect(TargetBoundsBefore);
		UE_LOG(
			LogEHBRoof,
			Display,
			TEXT("[EHB RoofV2 CutTrace] Envelope cutter target=%s envelope=%d triangles=%d vertices=%d bounds=%s targetBeforeBounds=%s boundsIntersect=%d"),
			*GetName(),
			EnvelopeIndex,
			SourceMesh.TriangleCount(),
			SourceMesh.VertexCount(),
			*FormatBox(SourceBounds),
			*FormatBox(TargetBoundsBefore),
			bBoundsIntersect ? 1 : 0);

		if (bDebugDrawCutterBounds)
		{
			DrawRoofCutDebugBounds(
				SourceBounds,
				FColor::Purple,
				FString::Printf(TEXT("envelope cutter %d"), EnvelopeIndex));
		}

		FDynamicMesh3 DifferenceMesh;
		FString FailureReason;
		const bool bApplied = FEHBManifoldBoolean::ApplyDifference(
			TargetMesh,
			SourceMesh,
			DifferenceMesh,
			EHBRoofBooleanMergeTolerance,
			EHBRoofBooleanSlopeGroupID,
			EHBRoofBooleanSideGroupID,
			&FailureReason);
		if (!bApplied)
		{
			UE_LOG(
				LogEHBRoof,
				Warning,
				TEXT("[EHB RoofV2] Manifold envelope difference failed target=%s envelope=%d reason=%s"),
				*GetName(),
				EnvelopeIndex,
				*FailureReason);
			return false;
		}

		UE_LOG(
			LogEHBRoof,
			Display,
			TEXT("[EHB RoofV2 CutTrace] Manifold envelope difference applied target=%s envelope=%d before=%d sourceTri=%d after=%d afterBounds=%s"),
			*GetName(),
			EnvelopeIndex,
			TargetMesh.TriangleCount(),
			SourceMesh.TriangleCount(),
			DifferenceMesh.TriangleCount(),
			*FormatBox(CalculateDynamicMeshBounds(DifferenceMesh)));
		TargetMesh = MoveTemp(DifferenceMesh);
	}

	if (bFilterDisconnectedPieces && !FilterDisconnectedCutPieces(TargetMesh))
	{
		return false;
	}

	UE_LOG(
		LogEHBRoof,
		Display,
		TEXT("[EHB RoofV2 CutTrace] ApplySourceMeshCuts final roof=%s outputTriangles=%d outputBounds=%s"),
		*GetName(),
		TargetMesh.TriangleCount(),
		*FormatBox(CalculateDynamicMeshBounds(TargetMesh)));

	if (bDebugDrawResultBounds)
	{
		DrawRoofCutDebugDynamicBounds(TargetMesh, FColor::Green, TEXT("envelope cut result"));
	}

	return ConvertDynamicMeshToUnifiedMesh(TargetMesh, InOutMesh);
}

bool AEHBRoofBase::ConvertUnifiedMeshToDynamicMesh(const FEHBRoofUnifiedMeshData& MeshData, FDynamicMesh3& OutMesh) const
{
	OutMesh.Clear();
	if (!MeshData.IsValid())
	{
		return false;
	}

	OutMesh.EnableTriangleGroups(0);
	int32 SourceTriangleIndex = 0;
	for (int32 Index = 0; Index + 2 < MeshData.Triangles.Num(); Index += 3)
	{
		const int32 AIndex = MeshData.Triangles[Index];
		const int32 BIndex = MeshData.Triangles[Index + 1];
		const int32 CIndex = MeshData.Triangles[Index + 2];
		if (!MeshData.Vertices.IsValidIndex(AIndex)
			|| !MeshData.Vertices.IsValidIndex(BIndex)
			|| !MeshData.Vertices.IsValidIndex(CIndex))
		{
			continue;
		}

		const int32 TriangleID = AppendGeneratedTriangleAsDynamic(
			MeshData.Vertices[AIndex],
			MeshData.Vertices[BIndex],
			MeshData.Vertices[CIndex],
			OutMesh);
		if (TriangleID >= 0)
		{
			const int32 GroupID = MeshData.TriangleGroups.IsValidIndex(SourceTriangleIndex)
				? MeshData.TriangleGroups[SourceTriangleIndex]
				: 0;
			OutMesh.SetTriangleGroup(TriangleID, GroupID);
		}
		++SourceTriangleIndex;
	}

	return OutMesh.TriangleCount() > 0;
}

bool AEHBRoofBase::ConvertDynamicMeshToUnifiedMesh(const FDynamicMesh3& DynamicMesh, FEHBRoofUnifiedMeshData& OutMesh) const
{
	OutMesh.Reset();
	if (DynamicMesh.TriangleCount() <= 0)
	{
		return false;
	}

	for (const int32 TriangleID : DynamicMesh.TriangleIndicesItr())
	{
		FVector3d A3;
		FVector3d B3;
		FVector3d C3;
		DynamicMesh.GetTriVertices(TriangleID, A3, B3, C3);

		const FVector A(A3);
		const FVector B(B3);
		const FVector C(C3);
		const FVector GeneratedNormal = FVector(FVector3d::CrossProduct(B3 - A3, C3 - A3).GetSafeNormal());
		if (GeneratedNormal.IsNearlyZero())
		{
			continue;
		}

		const int32 BaseIndex = OutMesh.Vertices.Num();
		OutMesh.Vertices.Add(A);
		OutMesh.Vertices.Add(B);
		OutMesh.Vertices.Add(C);
		OutMesh.Normals.Add(GeneratedNormal);
		OutMesh.Normals.Add(GeneratedNormal);
		OutMesh.Normals.Add(GeneratedNormal);
		OutMesh.UV0.Add(FVector2D(A.X, A.Y) / 100.0f);
		OutMesh.UV0.Add(FVector2D(B.X, B.Y) / 100.0f);
		OutMesh.UV0.Add(FVector2D(C.X, C.Y) / 100.0f);
		OutMesh.Triangles.Add(BaseIndex);
		OutMesh.Triangles.Add(BaseIndex + 2);
		OutMesh.Triangles.Add(BaseIndex + 1);
		OutMesh.TriangleGroups.Add(DynamicMesh.HasTriangleGroups() ? DynamicMesh.GetTriangleGroup(TriangleID) : 0);
	}

	OutMesh.RebuildBounds();
	return OutMesh.IsValid();
}

bool AEHBRoofBase::FilterDisconnectedCutPieces(FDynamicMesh3& Mesh) const
{
	if (!bRemoveDisconnectedCutPieces || Mesh.TriangleCount() <= 0)
	{
		return true;
	}

	UE::Geometry::FMeshConnectedComponents Components(&Mesh);
	Components.FindConnectedTriangles();
	if (Components.Num() <= 1)
	{
		return true;
	}

	int32 SelectedComponentIndex = INDEX_NONE;
	double SelectedScore = bKeepCutAwayDisconnectedPieces ? -TNumericLimits<double>::Max() : TNumericLimits<double>::Max();
	for (int32 ComponentIndex = 0; ComponentIndex < Components.Num(); ++ComponentIndex)
	{
		const UE::Geometry::FMeshConnectedComponents::FComponent& Component = Components[ComponentIndex];
		if (Component.Indices.IsEmpty())
		{
			continue;
		}

		FVector3d Centroid = FVector3d::Zero();
		int32 ValidTriangleCount = 0;
		for (const int32 TriangleID : Component.Indices)
		{
			if (!Mesh.IsTriangle(TriangleID))
			{
				continue;
			}

			FVector3d A;
			FVector3d B;
			FVector3d C;
			Mesh.GetTriVertices(TriangleID, A, B, C);
			Centroid += (A + B + C) / 3.0;
			++ValidTriangleCount;
		}

		if (ValidTriangleCount <= 0)
		{
			continue;
		}

		Centroid /= static_cast<double>(ValidTriangleCount);
		const double Score = Centroid.SquaredLength();
		const bool bBetterComponent = bKeepCutAwayDisconnectedPieces
			? Score > SelectedScore
			: Score < SelectedScore;
		if (bBetterComponent)
		{
			SelectedScore = Score;
			SelectedComponentIndex = ComponentIndex;
		}
	}

	if (SelectedComponentIndex == INDEX_NONE)
	{
		return false;
	}

	TSet<int32> KeptTriangles;
	for (const int32 TriangleID : Components[SelectedComponentIndex].Indices)
	{
		KeptTriangles.Add(TriangleID);
	}

	TArray<int32> TrianglesToRemove;
	for (const int32 TriangleID : Mesh.TriangleIndicesItr())
	{
		if (!KeptTriangles.Contains(TriangleID))
		{
			TrianglesToRemove.Add(TriangleID);
		}
	}

	for (const int32 TriangleID : TrianglesToRemove)
	{
		Mesh.RemoveTriangle(TriangleID, true, false);
	}

	UE_LOG(
		LogEHBRoof,
		Verbose,
		TEXT("[EHB RoofV2] FilterDisconnectedCutPieces roof=%s components=%d kept=%d removedTris=%d keepCutAway=%d"),
		*GetName(),
		Components.Num(),
		SelectedComponentIndex,
		TrianglesToRemove.Num(),
		bKeepCutAwayDisconnectedPieces ? 1 : 0);

	return Mesh.TriangleCount() > 0;
}

bool AEHBRoofBase::IsRoofCutDebugEnabled() const
{
	return bShowCutDebugVisualization
		&& !HasAnyFlags(RF_ClassDefaultObject)
		&& GetWorld() != nullptr;
}

void AEHBRoofBase::DrawRoofCutDebugBounds(const FBox& LocalBounds, const FColor& Color, const FString& Label) const
{
	if (!IsRoofCutDebugEnabled() || !LocalBounds.IsValid)
	{
		return;
	}

	UWorld* World = GetWorld();
	const FTransform ActorTransform = GetActorTransform();
	const FVector WorldCenter = ActorTransform.TransformPosition(LocalBounds.GetCenter());
	const FVector WorldExtent = LocalBounds.GetExtent() * ActorTransform.GetScale3D().GetAbs();
	const float Duration = FMath::Max(0.1f, CutDebugDrawDuration);
	const float Thickness = FMath::Max(0.1f, CutDebugDrawThickness);

	DrawDebugBox(
		World,
		WorldCenter,
		WorldExtent,
		ActorTransform.GetRotation(),
		Color,
		false,
		Duration,
		0,
		Thickness);

	if (bDebugDrawLabels)
	{
		const FVector LabelOffset(0.0f, 0.0f, WorldExtent.Z + 20.0f);
		DrawDebugString(
			World,
			WorldCenter + LabelOffset,
			Label,
			nullptr,
			Color,
			Duration,
			true);
	}
}

void AEHBRoofBase::DrawRoofCutDebugMeshBounds(const FEHBRoofUnifiedMeshData& MeshData, const FColor& Color, const FString& Label) const
{
	FBox Bounds = MeshData.LocalBounds;
	if (!Bounds.IsValid)
	{
		Bounds = CalculateUnifiedMeshBounds(MeshData);
	}
	DrawRoofCutDebugBounds(Bounds, Color, Label);
}

void AEHBRoofBase::DrawRoofCutDebugDynamicBounds(const FDynamicMesh3& Mesh, const FColor& Color, const FString& Label) const
{
	DrawRoofCutDebugBounds(CalculateDynamicMeshBounds(Mesh), Color, Label);
}

bool AEHBRoofBase::ResolveCutSourceElement(const FEHBCutOperation& Operation, AEHBElementActorBase*& OutElement) const
{
	OutElement = Operation.Source.SourceElement;
	if (IsValid(OutElement))
	{
		return true;
	}

	if (OwningBuilding && Operation.Source.SourceElementGuid.IsValid())
	{
		OutElement = OwningBuilding->FindElementActorByGuid(Operation.Source.SourceElementGuid);
		return IsValid(OutElement);
	}

	return false;
}

void AEHBRoofBase::CollectAutoCutCandidates(TArray<AEHBElementActorBase*>& OutCandidates) const
{
	OutCandidates.Reset();

	if (OwningBuilding)
	{
		for (AEHBElementActorBase* Element : OwningBuilding->GetElementActorsByFloor(FloorIndex))
		{
			if (IsValid(Element) && Element != this)
			{
				OutCandidates.AddUnique(Element);
			}
		}
	}

	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AEHBElementActorBase> It(World); It; ++It)
		{
			AEHBElementActorBase* Element = *It;
			if (IsValid(Element) && Element != this)
			{
				OutCandidates.AddUnique(Element);
			}
		}
	}
}

bool AEHBRoofBase::DoesOverlapElementForAutoCut(const AEHBElementActorBase* OtherElement) const
{
	if (!IsValid(OtherElement))
	{
		return false;
	}

	const FBox SelfBounds = GetComponentsBoundingBox(true);
	const FBox OtherBounds = OtherElement->GetComponentsBoundingBox(true);
	return SelfBounds.IsValid
		&& OtherBounds.IsValid
		&& SelfBounds.Intersect(OtherBounds);
}

bool AEHBRoofBase::UpsertAutoCollisionCutOperation(AEHBElementActorBase* TargetElement, AEHBElementActorBase* SourceElement) const
{
	if (!IsValid(TargetElement) || !IsValid(SourceElement) || TargetElement == SourceElement)
	{
		return false;
	}

	TArray<FEHBCutOperation>& TargetOperations = TargetElement->GetMutableCutOperations();
	for (FEHBCutOperation& Operation : TargetOperations)
	{
		if (IsAutoCollisionCutOperation(Operation, &SourceElement->ElementGuid))
		{
			Operation.Source.SourceElement = SourceElement;
			Operation.Source.SourceElementGuid = SourceElement->ElementGuid;
			Operation.bEnabled = true;
			UE_LOG(
				LogEHBRoof,
				Verbose,
				TEXT("[EHB RoofV2 CutTrace] UpsertAutoCollision existing target=%s source=%s opGuid=%s cutOps=%d"),
				*TargetElement->GetName(),
				*SourceElement->GetName(),
				*Operation.OperationGuid.ToString(),
				TargetOperations.Num());
			return false;
		}
	}

	TargetElement->Modify();

	FEHBCutOperation NewOperation;
	NewOperation.OperationGuid = FGuid::NewGuid();
	NewOperation.bEnabled = true;
	NewOperation.OperationType = EEHBCutOperationType::Subtract;
	NewOperation.Stage = EEHBCutStage::SourceOverlap;
	NewOperation.ProjectionMode = EEHBCutProjectionMode::SourceMesh;
	NewOperation.TransformPolicy = EEHBCutTransformPolicy::SourceActorDriven;
	NewOperation.Priority = 1000;
	NewOperation.OperationTag = AutoCollisionCutTag();
	NewOperation.Source.SourceType = EEHBCutSourceType::Element;
	NewOperation.Source.SourceElement = SourceElement;
	NewOperation.Source.SourceElementGuid = SourceElement->ElementGuid;
	NewOperation.EnsureGuids();
	TargetOperations.Add(NewOperation);
	TargetElement->MarkPackageDirty();
	UE_LOG(
		LogEHBRoof,
		Verbose,
		TEXT("[EHB RoofV2 CutTrace] UpsertAutoCollision added target=%s source=%s opGuid=%s cutOps=%d"),
		*TargetElement->GetName(),
		*SourceElement->GetName(),
		*NewOperation.OperationGuid.ToString(),
		TargetOperations.Num());
	return true;
}

bool AEHBRoofBase::RemoveAutoCollisionCutOperations(AEHBElementActorBase* TargetElement, const FGuid& SourceGuid) const
{
	if (!IsValid(TargetElement))
	{
		return false;
	}

	TArray<FEHBCutOperation>& TargetOperations = TargetElement->GetMutableCutOperations();
	const int32 PreviousCount = TargetOperations.Num();
	TargetOperations.RemoveAll(
		[this, &SourceGuid](const FEHBCutOperation& Operation)
		{
			const FGuid* SourceGuidPtr = SourceGuid.IsValid() ? &SourceGuid : nullptr;
			return IsAutoCollisionCutOperation(Operation, SourceGuidPtr);
		});

	if (TargetOperations.Num() == PreviousCount)
	{
		return false;
	}

	TargetElement->Modify();
	TargetElement->MarkPackageDirty();
	UE_LOG(
		LogEHBRoof,
		Verbose,
		TEXT("[EHB RoofV2 CutTrace] RemoveAutoCollision target=%s sourceGuid=%s removed=%d remaining=%d"),
		*TargetElement->GetName(),
		SourceGuid.IsValid() ? *SourceGuid.ToString() : TEXT("Any"),
		PreviousCount - TargetOperations.Num(),
		TargetOperations.Num());
	return true;
}

bool AEHBRoofBase::IsAutoCollisionCutOperation(const FEHBCutOperation& Operation, const FGuid* OptionalSourceGuid) const
{
	return Operation.OperationTag == AutoCollisionCutTag()
		&& (!OptionalSourceGuid || !OptionalSourceGuid->IsValid() || Operation.Source.SourceElementGuid == *OptionalSourceGuid);
}

bool AEHBRoofBase::RebuildCuttableRoofTarget(AEHBElementActorBase* TargetElement) const
{
	AEHBRoofBase* RoofTarget = Cast<AEHBRoofBase>(TargetElement);
	return IsValid(RoofTarget) ? RoofTarget->RebuildRoofMesh() : false;
}
