// Copyright Epic Games, Inc. All Rights Reserved.

#include "Actors/EHB_RailingGate.h"

#include "Actors/EHB_Railing.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/EHBBuildingActorBase.h"
#include "Engine/StaticMesh.h"
#include "MaterialDomain.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Settings/EHBBuildingToolsetSettings.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	constexpr float EHBRailingGateMeshSourceSize = 100.0f;

	UMaterialInterface* ResolveGateMaterial(const TSoftObjectPtr<UMaterialInterface>& Material)
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

	FRotator MakeGatePathRotation(const FVector& Forward)
	{
		return FRotationMatrix::MakeFromXZ(
			Forward.GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector),
			FVector::UpVector).Rotator();
	}
}

AEHB_RailingGate::AEHB_RailingGate()
{
	PrimaryActorTick.bCanEverTick = false;
	ElementType = EEHBBuildingElementType::Railing;
	FloorRole = EEHBBuildingFloorElementRole::Railing;
	// 围栏门是扶手的附属构件，不参与承重，也不作为房间边界。
	ElementCapabilities =
		static_cast<int32>(EEHBElementCapability::BoundaryAccessory)
		| static_cast<int32>(EEHBElementCapability::HostedElement);
	SemanticTags.AddUnique(TEXT("Railing.Gate"));

	GatePivotComponent = CreateDefaultSubobject<USceneComponent>(TEXT("GatePivot"));
	GatePivotComponent->SetupAttachment(SceneRoot);

	GateLeafComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("GateLeaf"));
	GateLeafComponent->SetupAttachment(GatePivotComponent);
	GateLeafComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	GateLeafComponent->SetCollisionObjectType(ECC_WorldStatic);
	GateLeafComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
	GateLeafComponent->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	GateLeafComponent->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		GateMesh = CubeMesh.Object;
		GateLeafComponent->SetStaticMesh(CubeMesh.Object);
	}
}

void AEHB_RailingGate::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RebuildGateMesh();
}

void AEHB_RailingGate::Destroyed()
{
	// 门洞数据保存在扶手上，所以门删除时必须清理扶手里的 GateConnection。
	if (AEHB_Railing* Railing = FindOwningRailing())
	{
		Railing->RemoveGateConnection(ElementGuid);
	}
	Super::Destroyed();
}

#if WITH_EDITOR
void AEHB_RailingGate::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	Width = FMath::Max(1.0f, Width);
	Height = FMath::Max(1.0f, Height);
	Thickness = FMath::Max(0.1f, Thickness);
	RebuildGateMesh();

	if (AEHB_Railing* Railing = FindOwningRailing())
	{
		Railing->AddOrUpdateGateConnection(this, DistanceFromRailingStart, Width, Height, HingeSide);
	}
}
#endif

bool AEHB_RailingGate::ConfigureOnRailing(
	AEHB_Railing* Railing,
	float InDistanceFromStart,
	float InWidth,
	float InHeight,
	EEHBRailingGateHingeSide InHingeSide)
{
	if (!Railing || !Railing->OwningBuilding)
	{
		return false;
	}

	Railing->EnsureElementGuid();
	EnsureElementGuid();
	OwningRailingGuid = Railing->ElementGuid;
	DistanceFromRailingStart = InDistanceFromStart;
	Width = FMath::Max(1.0f, InWidth);
	Height = FMath::Max(1.0f, InHeight);
	HingeSide = InHingeSide;

	FEHBRailingPathSample PathSample;
	if (!Railing->EvaluatePathAtDistance(DistanceFromRailingStart, PathSample))
	{
		return false;
	}

	// Actor 原点放在门洞中心，朝向跟随扶手路径切线；门扇再由局部铰链组件偏移。
	const FTransform GateWorldTransform(
		Railing->GetActorTransform().TransformRotation(MakeGatePathRotation(PathSample.LocalForward).Quaternion()),
		Railing->GetActorTransform().TransformPosition(PathSample.LocalLocation),
		FVector::OneVector);
	const FTransform GateLocalToBuilding = GateWorldTransform.GetRelativeTransform(Railing->OwningBuilding->GetActorTransform());
	AttachToBuilding(Railing->OwningBuilding, GateLocalToBuilding);
	SetFloorAssignment(FMath::Max(1, Railing->FloorIndex), EEHBBuildingFloorElementRole::Railing);

	// 回写扶手连接会触发扶手重建，门洞范围内的普通柱/横杆/围栏板随之被裁掉。
	const bool bConnectionUpdated = Railing->AddOrUpdateGateConnection(this, DistanceFromRailingStart, Width, Height, HingeSide);
	RebuildGateMesh();
	return bConnectionUpdated;
}

bool AEHB_RailingGate::RebuildGateMesh()
{
	if (!GatePivotComponent || !GateLeafComponent)
	{
		return false;
	}

	Width = FMath::Max(1.0f, Width);
	Height = FMath::Max(1.0f, Height);
	Thickness = FMath::Max(0.1f, Thickness);

	if (UStaticMesh* LoadedMesh = GateMesh.LoadSynchronous())
	{
		GateLeafComponent->SetStaticMesh(LoadedMesh);
	}

	const bool bLeftHinge = HingeSide == EEHBRailingGateHingeSide::Left;
	const float HingeX = bLeftHinge ? -Width * 0.5f : Width * 0.5f;
	const float LeafOffsetX = bLeftHinge ? Width * 0.5f : -Width * 0.5f;
	const float OpenSign = bLeftHinge ? 1.0f : -1.0f;

	// Pivot 位于门洞一侧边缘，门扇相对 Pivot 偏移半个门宽；开合只旋转 Pivot。
	GatePivotComponent->SetRelativeLocation(FVector(HingeX, 0.0f, 0.0f));
	GatePivotComponent->SetRelativeRotation(FRotator(0.0f, CurrentOpenAngle * OpenSign, 0.0f));
	GateLeafComponent->SetRelativeLocation(FVector(LeafOffsetX, 0.0f, Height * 0.5f));
	GateLeafComponent->SetRelativeRotation(FRotator::ZeroRotator);
	GateLeafComponent->SetRelativeScale3D(FVector(
		Width / EHBRailingGateMeshSourceSize,
		Thickness / EHBRailingGateMeshSourceSize,
		Height / EHBRailingGateMeshSourceSize));
	GateLeafComponent->SetMaterial(0, ResolveGateMaterial(GateMaterial));
	GateLeafComponent->MarkRenderStateDirty();
	return true;
}

AEHB_Railing* AEHB_RailingGate::FindOwningRailing() const
{
	if (!OwningBuilding || !OwningRailingGuid.IsValid())
	{
		return nullptr;
	}
	return Cast<AEHB_Railing>(OwningBuilding->FindElementActorByGuid(OwningRailingGuid));
}
