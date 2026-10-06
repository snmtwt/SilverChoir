// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Actors/EHBGableRoof.h"

#include "Components/EHBGeneratedMeshComponent.h"
#include "Cutting/EHBCutSourceBuilder.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Materials/Material.h"
#include "Misc/AutomationTest.h"

namespace
{
	AEHBGableRoof* SpawnTransientGableRoofForTest(UWorld* World)
	{
		if (!World)
		{
			return nullptr;
		}

		FActorSpawnParameters SpawnParameters;
		SpawnParameters.ObjectFlags |= RF_Transient;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<AEHBGableRoof>(
			AEHBGableRoof::StaticClass(),
			FVector::ZeroVector,
			FRotator::ZeroRotator,
			SpawnParameters);
	}

	void DestroyIfValid(AActor* Actor)
	{
		if (Actor && !Actor->IsActorBeingDestroyed())
		{
			Actor->Destroy();
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEHBGableRoofMaterialSlotsApplySinglePartTest,
	"EHB.Roof.GableMaterialSlots.ApplySinglePart",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEHBGableRoofMaterialSlotsApplySinglePartTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	TestNotNull(TEXT("Editor world should exist."), World);
	if (!World)
	{
		return false;
	}

	AEHBGableRoof* Roof = SpawnTransientGableRoofForTest(World);
	TestNotNull(TEXT("Gable roof should spawn."), Roof);
	if (!Roof)
	{
		return false;
	}

	UMaterialInterface* BodyMaterial = NewObject<UMaterial>(GetTransientPackage());
	UMaterialInterface* RidgeMaterial = NewObject<UMaterial>(GetTransientPackage());
	UMaterialInterface* EaveMaterial = NewObject<UMaterial>(GetTransientPackage());

	Roof->ApplyMaterialToRoofComponent(Roof->RoofBodyMeshComponent.Get(), BodyMaterial, false);
	Roof->ApplyMaterialToRoofComponent(Roof->RidgeMeshComponent.Get(), RidgeMaterial, false);

	TestEqual(TEXT("Body material property should be unchanged by ridge material application."), Roof->RoofBodyMaterial.Get(), BodyMaterial);
	TestEqual(TEXT("Ridge material property should store the applied material."), Roof->RidgeMaterial.Get(), RidgeMaterial);
	TestNull(TEXT("Eave material should remain unset after applying ridge only."), Roof->EaveMaterial.Get());

	TestEqual(TEXT("Body component material should be unchanged."), Roof->RoofBodyMeshComponent->GetMaterial(0), BodyMaterial);
	TestEqual(TEXT("Ridge component material should be applied."), Roof->RidgeMeshComponent->GetMaterial(0), RidgeMaterial);

	Roof->ApplyMaterialToRoofComponent(Roof->EaveMeshComponent.Get(), EaveMaterial, true);
	TestEqual(TEXT("Apply-all should update body material."), Roof->RoofBodyMaterial.Get(), EaveMaterial);
	TestEqual(TEXT("Apply-all should update ridge material."), Roof->RidgeMaterial.Get(), EaveMaterial);
	TestEqual(TEXT("Apply-all should update eave material."), Roof->EaveMaterial.Get(), EaveMaterial);
	TestEqual(TEXT("Apply-all should update gable rake material."), Roof->GableRakeMaterial.Get(), EaveMaterial);
	TestEqual(TEXT("Apply-all should update gable end wall material."), Roof->GableEndWallMaterial.Get(), EaveMaterial);

	DestroyIfValid(Roof);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEHBRoofWallFootprintCutSourceBuildsClosedPrismTest,
	"EHB.Roof.Cutting.WallFootprintBuildsClosedPrism",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEHBRoofWallFootprintCutSourceBuildsClosedPrismTest::RunTest(const FString& Parameters)
{
	FEHBResolvedCutSourceData Source;
	Source.GeometryKind = EEHBCutSourceGeometryKind::OrientedQuadFootprint3D;
	Source.TargetLocalFootprint = {
		FVector(0.0f, 0.0f, 0.0f),
		FVector(100.0f, 0.0f, 0.0f),
		FVector(100.0f, 80.0f, 0.0f),
		FVector(0.0f, 80.0f, 0.0f),
	};
	Source.MinZ = -20.0f;
	Source.MaxZ = 120.0f;

	UE::Geometry::FDynamicMesh3 Mesh;
	FString FailureReason;
	TestTrue(
		TEXT("Wall footprint cut source should build a dynamic mesh."),
		FEHBCutSourceBuilder::BuildDynamicMeshForSource(Source, Mesh, &FailureReason));
	TestEqual(TEXT("A quad prism should use eight shared vertices."), Mesh.VertexCount(), 8);
	TestEqual(TEXT("A quad prism should triangulate to twelve triangles."), Mesh.TriangleCount(), 12);

	int32 BoundaryEdgeCount = 0;
	for (const int32 EdgeID : Mesh.EdgeIndicesItr())
	{
		if (Mesh.IsBoundaryEdge(EdgeID))
		{
			++BoundaryEdgeCount;
		}
	}
	TestEqual(TEXT("The wall footprint prism must be closed."), BoundaryEdgeCount, 0);

	return true;
}

#endif
