// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Actors/EHB_DoorWindow.h"
#include "Actors/EHB_Floor.h"
#include "Actors/EHB_FloorSlab.h"
#include "Actors/EHB_Pillar.h"
#include "Actors/EHB_Wall.h"
#include "Components/EHBDoorWindowOpeningSplineComponent.h"
#include "EHB_Building.h"

#include "Editor.h"
#include "EasyHouseEditorMode.h"
#include "JsonObjectConverter.h"
#include "ScopedTransaction.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Materials/Material.h"
#include "Misc/AutomationTest.h"
#include "Components/EHBGeneratedMeshComponent.h"

namespace
{
	template <typename TActor>
	TActor* SpawnTransientActor(UWorld* World, const FVector& WorldLocation)
	{
		if (!World)
		{
			return nullptr;
		}

		FActorSpawnParameters SpawnParameters;
		SpawnParameters.ObjectFlags |= RF_Transient;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<TActor>(
			TActor::StaticClass(),
			WorldLocation,
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

	bool IsPointOnPolyline2D(const FVector2D& Point, const TArray<FVector>& Polyline, float Tolerance)
	{
		if (Polyline.Num() < 2)
		{
			return false;
		}

		const float ToleranceSquared = Tolerance * Tolerance;
		for (int32 Index = 0; Index < Polyline.Num(); ++Index)
		{
			const FVector2D A(Polyline[Index].X, Polyline[Index].Y);
			const FVector2D B(Polyline[(Index + 1) % Polyline.Num()].X, Polyline[(Index + 1) % Polyline.Num()].Y);
			const FVector2D Segment = B - A;
			const float SegmentLengthSquared = Segment.SizeSquared();
			const float Alpha = SegmentLengthSquared > UE_SMALL_NUMBER
				? FMath::Clamp(FVector2D::DotProduct(Point - A, Segment) / SegmentLengthSquared, 0.0f, 1.0f)
				: 0.0f;
			const FVector2D ClosestPoint = A + Segment * Alpha;
			if (FVector2D::DistSquared(Point, ClosestPoint) <= ToleranceSquared)
			{
				return true;
			}
		}

		return false;
	}

	bool IsBuildingLocalPointOnPillarFootprint(const AEHB_Pillar* Pillar, const FVector& BuildingLocalPoint, float Tolerance)
	{
		if (!Pillar)
		{
			return false;
		}

		TArray<FVector> Footprint;
		Pillar->GetPillarFootprintLocalPoints(Footprint);
		if (Footprint.Num() < 3)
		{
			return false;
		}

		const FVector PillarLocalPoint = Pillar->GetElementLocalTransform().InverseTransformPosition(BuildingLocalPoint);
		return IsPointOnPolyline2D(FVector2D(PillarLocalPoint.X, PillarLocalPoint.Y), Footprint, Tolerance);
	}

	bool HasTopVisibleFloorWinding(UEHBGeneratedMeshComponent* Component)
	{
		if (!Component)
		{
			return false;
		}

		const FProcMeshSection* Section = Component->GetProcMeshSection(0);
		if (!Section || Section->ProcIndexBuffer.Num() < 3)
		{
			return false;
		}

		for (int32 Index = 0; Index + 2 < Section->ProcIndexBuffer.Num(); Index += 3)
		{
			const int32 AIndex = Section->ProcIndexBuffer[Index];
			const int32 BIndex = Section->ProcIndexBuffer[Index + 1];
			const int32 CIndex = Section->ProcIndexBuffer[Index + 2];
			if (!Section->ProcVertexBuffer.IsValidIndex(AIndex)
				|| !Section->ProcVertexBuffer.IsValidIndex(BIndex)
				|| !Section->ProcVertexBuffer.IsValidIndex(CIndex))
			{
				return false;
			}

			const FVector& A = Section->ProcVertexBuffer[AIndex].Position;
			const FVector& B = Section->ProcVertexBuffer[BIndex].Position;
			const FVector& C = Section->ProcVertexBuffer[CIndex].Position;
			const FVector WindingNormal = FVector::CrossProduct(B - A, C - A).GetSafeNormal();
			if (FVector::DotProduct(WindingNormal, FVector::UpVector) >= 0.0f)
			{
				return false;
			}
		}

		return true;
	}

	bool IsPointInsideTriangle2D(const FVector2D& Point, const FVector2D& A, const FVector2D& B, const FVector2D& C)
	{
		auto SignedArea = [](const FVector2D& P0, const FVector2D& P1, const FVector2D& P2)
		{
			return (P0.X - P2.X) * (P1.Y - P2.Y) - (P1.X - P2.X) * (P0.Y - P2.Y);
		};

		const double Area0 = SignedArea(Point, A, B);
		const double Area1 = SignedArea(Point, B, C);
		const double Area2 = SignedArea(Point, C, A);
		constexpr double Tolerance = 0.01;
		const bool bHasNegative = Area0 < -Tolerance || Area1 < -Tolerance || Area2 < -Tolerance;
		const bool bHasPositive = Area0 > Tolerance || Area1 > Tolerance || Area2 > Tolerance;
		return !(bHasNegative && bHasPositive);
	}

	bool DoesWallSurfaceCoverWallPoint(UEHBGeneratedMeshComponent* Component, const FVector2D& WallXZPoint)
	{
		if (!Component)
		{
			return false;
		}

		const FProcMeshSection* Section = Component->GetProcMeshSection(0);
		if (!Section || Section->ProcIndexBuffer.Num() < 3)
		{
			return false;
		}

		for (int32 Index = 0; Index + 2 < Section->ProcIndexBuffer.Num(); Index += 3)
		{
			const int32 AIndex = Section->ProcIndexBuffer[Index];
			const int32 BIndex = Section->ProcIndexBuffer[Index + 1];
			const int32 CIndex = Section->ProcIndexBuffer[Index + 2];
			if (!Section->ProcVertexBuffer.IsValidIndex(AIndex)
				|| !Section->ProcVertexBuffer.IsValidIndex(BIndex)
				|| !Section->ProcVertexBuffer.IsValidIndex(CIndex))
			{
				continue;
			}

			const FVector& A3D = Section->ProcVertexBuffer[AIndex].Position;
			const FVector& B3D = Section->ProcVertexBuffer[BIndex].Position;
			const FVector& C3D = Section->ProcVertexBuffer[CIndex].Position;
			if (IsPointInsideTriangle2D(
				WallXZPoint,
				FVector2D(A3D.X, A3D.Z),
				FVector2D(B3D.X, B3D.Z),
				FVector2D(C3D.X, C3D.Z)))
			{
				return true;
			}
		}

		return false;
	}

	bool DoesGeneratedMeshAvoidNeedleTriangles(UEHBGeneratedMeshComponent* Component)
	{
		if (!Component)
		{
			return false;
		}

		constexpr float MinEdgeLength = 0.01f;
		constexpr float MinTriangleAltitude = 0.05f;
		bool bFoundTriangle = false;
		for (int32 SectionIndex = 0; SectionIndex < Component->GetNumSections(); ++SectionIndex)
		{
			const FProcMeshSection* Section = Component->GetProcMeshSection(SectionIndex);
			if (!Section)
			{
				continue;
			}

			for (int32 Index = 0; Index + 2 < Section->ProcIndexBuffer.Num(); Index += 3)
			{
				const int32 AIndex = Section->ProcIndexBuffer[Index];
				const int32 BIndex = Section->ProcIndexBuffer[Index + 1];
				const int32 CIndex = Section->ProcIndexBuffer[Index + 2];
				if (!Section->ProcVertexBuffer.IsValidIndex(AIndex)
					|| !Section->ProcVertexBuffer.IsValidIndex(BIndex)
					|| !Section->ProcVertexBuffer.IsValidIndex(CIndex))
				{
					return false;
				}

				const FVector& A = Section->ProcVertexBuffer[AIndex].Position;
				const FVector& B = Section->ProcVertexBuffer[BIndex].Position;
				const FVector& C = Section->ProcVertexBuffer[CIndex].Position;
				if (A.ContainsNaN() || B.ContainsNaN() || C.ContainsNaN())
				{
					return false;
				}

				const float ABSquared = (B - A).SizeSquared();
				const float BCSquared = (C - B).SizeSquared();
				const float CASquared = (A - C).SizeSquared();
				if (ABSquared <= FMath::Square(MinEdgeLength)
					|| BCSquared <= FMath::Square(MinEdgeLength)
					|| CASquared <= FMath::Square(MinEdgeLength))
				{
					return false;
				}

				const float MaxEdgeLength = FMath::Sqrt(FMath::Max3(ABSquared, BCSquared, CASquared));
				const FVector Cross = FVector::CrossProduct(B - A, C - A);
				if (Cross.SizeSquared() <= FMath::Square(MaxEdgeLength * MinTriangleAltitude))
				{
					return false;
				}

				bFoundTriangle = true;
			}
		}

		return bFoundTriangle;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEHBRelationshipCoreGraphTest,
	"EHB.Relationship.CoreGraph",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEHBRelationshipCoreGraphTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("Editor world should be available"), World))
	{
		return false;
	}

	const FVector TestOrigin(0.0f, 0.0f, 140000.0f);
	AEHB_Building* Building = SpawnTransientActor<AEHB_Building>(World, TestOrigin);
	AEHB_Pillar* FirstFloorPillar = SpawnTransientActor<AEHB_Pillar>(World, TestOrigin);
	AEHB_FloorSlab* FirstFloorCeiling = SpawnTransientActor<AEHB_FloorSlab>(World, TestOrigin + FVector(0.0f, 0.0f, 300.0f));
	AEHB_Pillar* SecondFloorPillar = SpawnTransientActor<AEHB_Pillar>(World, TestOrigin + FVector(0.0f, 0.0f, 320.0f));
	if (!TestNotNull(TEXT("Building should spawn"), Building)
		|| !TestNotNull(TEXT("First-floor pillar should spawn"), FirstFloorPillar)
		|| !TestNotNull(TEXT("Floor slab should spawn"), FirstFloorCeiling)
		|| !TestNotNull(TEXT("Second-floor pillar should spawn"), SecondFloorPillar))
	{
		return false;
	}

	FirstFloorPillar->AttachToBuilding(Building, FTransform::Identity);
	FirstFloorPillar->SetFloorAssignment(1, EEHBBuildingFloorElementRole::FloorBody);
	FirstFloorCeiling->AttachToBuilding(
		Building,
		FTransform(FVector(0.0f, 0.0f, 300.0f)));
	FirstFloorCeiling->SetAutomaticFloorAssignment();
	SecondFloorPillar->AttachToBuilding(
		Building,
		FTransform(FVector(0.0f, 0.0f, 320.0f)));
	SecondFloorPillar->SetAutomaticFloorAssignment();

	const FGuid PillarSupportsSlab = Building->SetStructuralSupportRelation(
		FirstFloorPillar,
		FirstFloorCeiling,
		EEHBElementSurfaceKind::Top,
		EEHBElementSurfaceKind::Bottom,
		FVector(0.0f, 0.0f, 300.0f),
		FVector::UpVector,
		1600.0f,
		EEHBRelationOrigin::AutoDetected);
	TestTrue(TEXT("Pillar-to-slab support relation should be created"), PillarSupportsSlab.IsValid());
	TestEqual(TEXT("Ceiling slab should belong to the supporting storey"), FirstFloorCeiling->FloorIndex, 1);
	TestEqual(
		TEXT("Automatic slab role should resolve to floor ceiling"),
		FirstFloorCeiling->FloorRole,
		EEHBBuildingFloorElementRole::FloorCeiling);

	const FGuid SlabSupportsPillar = Building->SetStructuralSupportRelation(
		FirstFloorCeiling,
		SecondFloorPillar,
		EEHBElementSurfaceKind::Top,
		EEHBElementSurfaceKind::Bottom,
		FVector(0.0f, 0.0f, 320.0f),
		FVector::UpVector,
		1600.0f,
		EEHBRelationOrigin::AutoDetected);
	TestTrue(TEXT("Slab-to-pillar support relation should be created"), SlabSupportsPillar.IsValid());
	TestEqual(TEXT("Pillar above first-floor ceiling should resolve to floor two"), SecondFloorPillar->FloorIndex, 2);

	const TArray<AEHBElementActorBase*> Supporters = SecondFloorPillar->GetSupporters();
	TestEqual(TEXT("Second-floor pillar should have one supporter"), Supporters.Num(), 1);
	TestTrue(
		TEXT("Second-floor pillar should be supported by the slab"),
		Supporters.Num() == 1 && Supporters[0] == FirstFloorCeiling);

	const TArray<AEHBElementActorBase*> Affected = FirstFloorPillar->GetAffectedElements(true, 8);
	TestEqual(TEXT("Recursive affected query should reach slab and upper pillar"), Affected.Num(), 2);

	const TArray<FGuid> SupportPath = Building->FindElementRelationPath(
		FirstFloorPillar->ElementGuid,
		SecondFloorPillar->ElementGuid,
		EEHBRelationQueryDirection::Outgoing,
		{ EEHBElementRelationType::StructuralSupport },
		8);
	TestEqual(TEXT("Support path should include three elements"), SupportPath.Num(), 3);

	FEHBElementQuery FloorTwoPillarQuery;
	FloorTwoPillarQuery.FloorIndex = 2;
	FloorTwoPillarQuery.ElementTypes = { EEHBBuildingElementType::Pillar };
	FloorTwoPillarQuery.RequiredCapabilities = static_cast<int32>(EEHBElementCapability::Structural);
	const TArray<AEHBElementActorBase*> FloorTwoPillars = Building->QueryElements(FloorTwoPillarQuery);
	TestEqual(TEXT("Floor query should find the upper pillar"), FloorTwoPillars.Num(), 1);

	const FGuid RejectedCycle = Building->SetStructuralSupportRelation(
		SecondFloorPillar,
		FirstFloorPillar,
		EEHBElementSurfaceKind::Top,
		EEHBElementSurfaceKind::Bottom,
		FVector::ZeroVector,
		FVector::UpVector,
		1.0f,
		EEHBRelationOrigin::UserAuthored);
	TestFalse(TEXT("Structural support cycles should be rejected"), RejectedCycle.IsValid());

	FirstFloorPillar->NotifyElementGeometryChanged(true);
	TestTrue(
		TEXT("Geometry-dependent support relation should become stale"),
		Building->IsElementRelationStale(PillarSupportsSlab));
	const TArray<FEHBRelationValidationIssue> Issues =
		Building->ValidateElementRelationshipGraph(false, false);
	TestTrue(TEXT("Validation should report the stale auto relation"), Issues.Num() > 0);

	const FGuid RemovedSlabGuid = FirstFloorCeiling->ElementGuid;
	DestroyIfValid(FirstFloorCeiling);
	FEHBRelationQuery RemovedSlabQuery;
	RemovedSlabQuery.ElementGuid = RemovedSlabGuid;
	TestEqual(
		TEXT("Deleting an element should remove all of its relations"),
		Building->QueryElementRelations(RemovedSlabQuery).Num(),
		0);

	DestroyIfValid(SecondFloorPillar);
	DestroyIfValid(FirstFloorPillar);
	DestroyIfValid(Building);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEHBRelationshipCompatibilityTest,
	"EHB.Relationship.ExistingElementCompatibility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEHBRelationshipCompatibilityTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("Editor world should be available"), World))
	{
		return false;
	}

	const FVector TestOrigin(4000.0f, 0.0f, 140000.0f);
	AEHB_Building* Building = SpawnTransientActor<AEHB_Building>(World, TestOrigin);
	AEHB_Pillar* StartPillar = SpawnTransientActor<AEHB_Pillar>(World, TestOrigin);
	AEHB_Pillar* EndPillar = SpawnTransientActor<AEHB_Pillar>(World, TestOrigin + FVector(400.0f, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("Building should spawn"), Building)
		|| !TestNotNull(TEXT("Start pillar should spawn"), StartPillar)
		|| !TestNotNull(TEXT("End pillar should spawn"), EndPillar))
	{
		return false;
	}

	StartPillar->AttachToBuilding(Building, FTransform(FVector::ZeroVector));
	EndPillar->AttachToBuilding(Building, FTransform(FVector(400.0f, 0.0f, 0.0f)));
	StartPillar->SetFloorAssignment(1, EEHBBuildingFloorElementRole::FloorBody);
	EndPillar->SetFloorAssignment(1, EEHBBuildingFloorElementRole::FloorBody);

	AEHB_Wall* Wall = Building->ConnectPillars(StartPillar, EndPillar, 300.0f, 20.0f);
	if (!TestNotNull(TEXT("Wall should be created"), Wall))
	{
		return false;
	}

	FEHBRelationQuery WallTopologyQuery;
	WallTopologyQuery.ElementGuid = Wall->ElementGuid;
	WallTopologyQuery.Direction = EEHBRelationQueryDirection::Outgoing;
	WallTopologyQuery.Types = { EEHBElementRelationType::TopologyConnection };
	TestEqual(
		TEXT("Existing wall creation should emit two endpoint topology relations"),
		Building->QueryElementRelations(WallTopologyQuery).Num(),
		2);

	UMaterial* OriginalLeftMaterial = NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient);
	UMaterial* OriginalRightMaterial = NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient);
	UMaterial* OriginalCapMaterial = NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient);
	UMaterial* SingleSurfaceMaterial = NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient);
	UMaterial* AllSurfaceMaterial = NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient);
	if (!TestNotNull(TEXT("Original left material should be available"), OriginalLeftMaterial)
		|| !TestNotNull(TEXT("Original right material should be available"), OriginalRightMaterial)
		|| !TestNotNull(TEXT("Original cap material should be available"), OriginalCapMaterial)
		|| !TestNotNull(TEXT("Single-surface material should be available"), SingleSurfaceMaterial)
		|| !TestNotNull(TEXT("All-surface material should be available"), AllSurfaceMaterial))
	{
		return false;
	}

	Wall->LeftSurfaceStyle.OverrideMaterial = OriginalLeftMaterial;
	Wall->RightSurfaceStyle.OverrideMaterial = OriginalRightMaterial;
	Wall->CapOverrideMaterial = OriginalCapMaterial;
	Wall->RebuildWallMesh();

	TestTrue(
		TEXT("Applying a material to the hit left wall component should succeed"),
		Wall->ApplyMaterialToHitSurface(Wall->LeftWallMeshComponent, SingleSurfaceMaterial, false));
	TestEqual(
		TEXT("Hit left wall material should change"),
		Wall->LeftSurfaceStyle.OverrideMaterial.Get(),
		Cast<UMaterialInterface>(SingleSurfaceMaterial));
	TestEqual(
		TEXT("Right wall material should remain unchanged when left wall is hit"),
		Wall->RightSurfaceStyle.OverrideMaterial.Get(),
		Cast<UMaterialInterface>(OriginalRightMaterial));
	TestEqual(
		TEXT("Wall cap material should remain unchanged when a side wall is hit"),
		Wall->CapOverrideMaterial.Get(),
		Cast<UMaterialInterface>(OriginalCapMaterial));

	TestTrue(
		TEXT("Applying a material with all-surfaces mode should succeed"),
		Wall->ApplyMaterialToHitSurface(Wall->RightWallMeshComponent, AllSurfaceMaterial, true));
	TestEqual(TEXT("All-surfaces mode should update the left wall"), Wall->LeftSurfaceStyle.OverrideMaterial.Get(), Cast<UMaterialInterface>(AllSurfaceMaterial));
	TestEqual(TEXT("All-surfaces mode should update the right wall"), Wall->RightSurfaceStyle.OverrideMaterial.Get(), Cast<UMaterialInterface>(AllSurfaceMaterial));
	TestEqual(TEXT("All-surfaces mode should update the wall cap"), Wall->CapOverrideMaterial.Get(), Cast<UMaterialInterface>(AllSurfaceMaterial));

	const TArray<FGuid> PillarPath = Building->FindElementRelationPath(
		StartPillar->ElementGuid,
		EndPillar->ElementGuid,
		EEHBRelationQueryDirection::Both,
		{ EEHBElementRelationType::TopologyConnection },
		4);
	TestEqual(TEXT("Topology graph should connect both pillars through the wall"), PillarPath.Num(), 3);

	AEHB_DoorWindow* DoorWindow = SpawnTransientActor<AEHB_DoorWindow>(World, TestOrigin + FVector(200.0f, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("Door/window should spawn"), DoorWindow))
	{
		return false;
	}
	DoorWindow->AttachToBuilding(Building, FTransform(FVector(200.0f, 0.0f, 0.0f)));
	DoorWindow->BindToWall(Wall, 200.0f);

	FEHBRelationQuery HostQuery;
	HostQuery.ElementGuid = DoorWindow->ElementGuid;
	HostQuery.Direction = EEHBRelationQueryDirection::Incoming;
	HostQuery.Types = { EEHBElementRelationType::HostedElement };
	TestEqual(TEXT("Door/window binding should emit one host relation"), Building->QueryElementRelations(HostQuery).Num(), 1);

	DoorWindow->ClearWallBinding();
	TestEqual(TEXT("Clearing wall binding should remove host relation"), Building->QueryElementRelations(HostQuery).Num(), 0);

	DestroyIfValid(DoorWindow);
	DestroyIfValid(Wall);
	DestroyIfValid(StartPillar);
	DestroyIfValid(EndPillar);
	DestroyIfValid(Building);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEHBWallCreationRectangleRoomTest,
	"EHB.Relationship.WallCreationRectangleRoom",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEHBWallCreationRectangleRoomTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("Editor world should be available"), World))
	{
		return false;
	}

	const FVector TestOrigin(4300.0f, 900.0f, 140000.0f);
	AEHB_Building* Building = SpawnTransientActor<AEHB_Building>(World, TestOrigin);
	if (!TestNotNull(TEXT("Building should spawn"), Building))
	{
		return false;
	}

	auto Cleanup = [&Building]()
	{
		if (Building)
		{
			TArray<AActor*> AttachedActors;
			Building->GetAttachedActors(AttachedActors);
			for (AActor* AttachedActor : AttachedActors)
			{
				DestroyIfValid(AttachedActor);
			}
		}
		DestroyIfValid(Building);
	};

	FEHBWallCreationOptions Options;
	Options.WallHeight = 300.0f;
	Options.WallThickness = 20.0f;
	Options.PillarHeight = 300.0f;
	Options.PillarWidth = 20.0f;
	Options.PillarDepth = 20.0f;
	Options.FloorIndex = 1;
	Options.NewPillarNamePrefix = TEXT("EHB_RectangleTestPillar");

	AEHB_Pillar* MiddleBottomPillar = Building->CreatePillarAtLocalLocation(
		FVector(200.0f, 0.0f, 0.0f),
		FRotator::ZeroRotator,
		Options.PillarHeight,
		Options.PillarWidth,
		Options.PillarDepth,
		Options.FloorIndex,
		TEXT("EHB_RectangleTestMiddlePillar"),
		true);
	if (!TestNotNull(TEXT("Existing middle pillar should be created"), MiddleBottomPillar))
	{
		Cleanup();
		return false;
	}

	auto MakeEndpoint = [Building, &Options](const FVector& LocalLocation)
	{
		FEHBWallCreationEndpoint Endpoint;
		Endpoint.LocalLocation = LocalLocation;
		Endpoint.WorldLocation = Building->GetActorTransform().TransformPosition(LocalLocation);
		Endpoint.FloorIndex = Options.FloorIndex;
		return Endpoint;
	};

	TArray<FEHBWallCreationEndpoint> Corners;
	Corners.Add(MakeEndpoint(FVector(0.0f, 0.0f, 0.0f)));
	Corners.Add(MakeEndpoint(FVector(400.0f, 0.0f, 0.0f)));
	Corners.Add(MakeEndpoint(FVector(400.0f, 200.0f, 0.0f)));
	Corners.Add(MakeEndpoint(FVector(0.0f, 200.0f, 0.0f)));

	TArray<AEHB_Wall*> AllWalls;
	for (int32 EdgeIndex = 0; EdgeIndex < Corners.Num(); ++EdgeIndex)
	{
		FEHBWallCreationResult Result;
		const bool bCreated = Building->CreateOrReuseWallSegment(
			Corners[EdgeIndex],
			Corners[(EdgeIndex + 1) % Corners.Num()],
			Options,
			Result);
		const FString EdgeMessage = FString::Printf(TEXT("Rectangle edge %d should create or reuse walls"), EdgeIndex);
		if (!TestTrue(*EdgeMessage, bCreated))
		{
			Cleanup();
			return false;
		}

		if (EdgeIndex == 0)
		{
			TestEqual(TEXT("Bottom edge should be split by the existing middle pillar"), Result.Walls.Num(), 2);
		}

		for (AEHB_Wall* Wall : Result.Walls)
		{
			AllWalls.AddUnique(Wall);
		}
	}

	Building->RebuildClosedLoops();

	AEHB_Pillar* BottomStartPillar = Corners[0].Pillar;
	AEHB_Pillar* BottomEndPillar = Corners[1].Pillar;
	if (!TestNotNull(TEXT("Bottom start pillar should exist"), BottomStartPillar)
		|| !TestNotNull(TEXT("Bottom end pillar should exist"), BottomEndPillar))
	{
		Cleanup();
		return false;
	}

	bool bSameDirection = true;
	TestNull(
		TEXT("Bottom edge should not create one long wall over the middle pillar"),
		Building->FindWallBetweenPillars(BottomStartPillar->ElementGuid, BottomEndPillar->ElementGuid, bSameDirection));
	TestNotNull(
		TEXT("Bottom start should connect to the middle pillar"),
		Building->FindWallBetweenPillars(BottomStartPillar->ElementGuid, MiddleBottomPillar->ElementGuid, bSameDirection));
	TestNotNull(
		TEXT("Middle pillar should connect to the bottom end"),
		Building->FindWallBetweenPillars(MiddleBottomPillar->ElementGuid, BottomEndPillar->ElementGuid, bSameDirection));
	TestEqual(TEXT("Rectangle room with one middle edge pillar should contain five wall segments"), AllWalls.Num(), 5);

	const TArray<FEHBBuildingClosedLoop> Loops = Building->GetClosedLoopsByFloor(1);
	TestEqual(TEXT("Rectangle wall creation should produce one closed room loop"), Loops.Num(), 1);
	if (Loops.Num() == 1)
	{
		TestEqual(TEXT("Closed room loop should include the split bottom edge"), Loops[0].WallGuids.Num(), 5);
		TestEqual(TEXT("Closed room loop should include all five boundary pillars"), Loops[0].PillarGuids.Num(), 5);
		TestTrue(TEXT("Closed room loop should preserve rectangle area"), FMath::IsNearlyEqual(Loops[0].Area, 80000.0f, 1.0f));
	}

	Cleanup();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEHBWallSampledSurfaceUsesSourceMaterialsTest,
	"EHB.Relationship.WallSampledSurfaceUsesSourceMaterials",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEHBWallSampledSurfaceUsesSourceMaterialsTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("Editor world should be available"), World))
	{
		return false;
	}

	const FVector TestOrigin(4600.0f, 0.0f, 140000.0f);
	AEHB_Building* Building = SpawnTransientActor<AEHB_Building>(World, TestOrigin);
	AEHB_Pillar* StartPillar = SpawnTransientActor<AEHB_Pillar>(World, TestOrigin);
	AEHB_Pillar* EndPillar = SpawnTransientActor<AEHB_Pillar>(World, TestOrigin + FVector(400.0f, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("Building should spawn"), Building)
		|| !TestNotNull(TEXT("Start pillar should spawn"), StartPillar)
		|| !TestNotNull(TEXT("End pillar should spawn"), EndPillar))
	{
		return false;
	}

	StartPillar->AttachToBuilding(Building, FTransform(FVector::ZeroVector));
	EndPillar->AttachToBuilding(Building, FTransform(FVector(400.0f, 0.0f, 0.0f)));
	StartPillar->SetFloorAssignment(1, EEHBBuildingFloorElementRole::FloorBody);
	EndPillar->SetFloorAssignment(1, EEHBBuildingFloorElementRole::FloorBody);

	AEHB_Wall* Wall = Building->ConnectPillars(StartPillar, EndPillar, 300.0f, 20.0f);
	if (!TestNotNull(TEXT("Wall should be created"), Wall))
	{
		return false;
	}

	UMaterial* SourceMaterialA = NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient);
	UMaterial* SourceMaterialB = NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient);
	if (!TestNotNull(TEXT("First sampled source material should be available"), SourceMaterialA)
		|| !TestNotNull(TEXT("Second sampled source material should be available"), SourceMaterialB))
	{
		return false;
	}

	FEHBWallMeshData SampleRow;
	SampleRow.WallWidth = 400.0f;
	SampleRow.WallHeight = 300.0f;
	SampleRow.WallThickness = 20.0f;
	SampleRow.WallLocalBoundsMin = FVector(0.0f, -10.0f, 0.0f);
	SampleRow.WallLocalBoundsMax = FVector(400.0f, 10.0f, 300.0f);
	SampleRow.Materials.Add(SourceMaterialA);
	SampleRow.Materials.Add(SourceMaterialB);

	auto AddSampleSurface = [](FEHBWallMeshSampleSurface& Surface, EEHBWallMeshSampleSide Side, float Y)
	{
		Surface.SampleSide = Side;
		Surface.Vertices.Reset();
		Surface.Triangles.Reset();
		Surface.TriangleMaterialIndices.Reset();

		auto AddVertex = [&Surface, Y](float X, float Z)
		{
			FEHBWallMeshSampleVertex Vertex;
			Vertex.Position = FVector(X, Y, Z);
			Vertex.Normal = FVector(0.0f, Y >= 0.0f ? 1.0f : -1.0f, 0.0f);
			Vertex.Tangent = FVector::ForwardVector;
			Vertex.UV0 = FVector2D(X / 400.0f, Z / 300.0f);
			return Surface.Vertices.Add(Vertex);
		};

		const int32 A = AddVertex(0.0f, 0.0f);
		const int32 B = AddVertex(0.0f, 300.0f);
		const int32 C = AddVertex(400.0f, 300.0f);
		const int32 D = AddVertex(0.0f, 0.0f);
		const int32 E = AddVertex(400.0f, 300.0f);
		const int32 F = AddVertex(400.0f, 0.0f);
		auto AddHorizontalVertex = [&Surface](float X, float InY, float Z)
		{
			FEHBWallMeshSampleVertex Vertex;
			Vertex.Position = FVector(X, InY, Z);
			Vertex.Normal = FVector::UpVector;
			Vertex.Tangent = FVector::ForwardVector;
			Vertex.UV0 = FVector2D(X / 400.0f, Z / 300.0f);
			return Surface.Vertices.Add(Vertex);
		};
		const int32 G = AddHorizontalVertex(0.0f, Y, 150.0f);
		const int32 H = AddHorizontalVertex(0.0f, Y + (Y >= 0.0f ? 20.0f : -20.0f), 150.0f);
		const int32 I = AddHorizontalVertex(400.0f, Y, 150.0f);

		Surface.Triangles = { A, B, C, D, E, F, G, H, I };
		Surface.TriangleMaterialIndices = { 0, 1, 0 };
		Surface.SampleTriangleCount = 3;
	};

	AddSampleSurface(SampleRow.FrontSurface, EEHBWallMeshSampleSide::Front, 10.0f);
	AddSampleSurface(SampleRow.BackSurface, EEHBWallMeshSampleSide::Back, -10.0f);

	UDataTable* SampleTable = NewObject<UDataTable>(GetTransientPackage(), NAME_None, RF_Transient);
	SampleTable->RowStruct = FEHBWallMeshData::StaticStruct();
	SampleTable->AddRow(TEXT("TwoMaterialWall"), SampleRow);

	Wall->RightSurfaceStyle.SourceType = EEHBWallSurfaceSourceType::SampledMesh;
	Wall->RightSurfaceStyle.SampledWallRow.DataTable = SampleTable;
	Wall->RightSurfaceStyle.SampledWallRow.RowName = TEXT("TwoMaterialWall");
	Wall->RightSurfaceStyle.SampleSide = EEHBWallMeshSampleSide::Front;
	Wall->RightSurfaceStyle.OverrideMaterial.Reset();
	Wall->RebuildRightWallMesh();

	TestEqual(TEXT("Sampled wall should create one section per source material"), Wall->RightWallMeshComponent->GetNumMaterials(), 2);
	TestEqual(TEXT("First sampled wall section should use the first source material"), Wall->RightWallMeshComponent->GetMaterial(0), Cast<UMaterialInterface>(SourceMaterialA));
	TestEqual(TEXT("Second sampled wall section should use the second source material"), Wall->RightWallMeshComponent->GetMaterial(1), Cast<UMaterialInterface>(SourceMaterialB));
	auto IsMeshWithinXBounds = [](UEHBGeneratedMeshComponent* Component, float MinX, float MaxX)
	{
		if (!Component)
		{
			return false;
		}

		for (int32 SectionIndex = 0; SectionIndex < Component->GetNumSections(); ++SectionIndex)
		{
			const FProcMeshSection* Section = Component->GetProcMeshSection(SectionIndex);
			if (!Section)
			{
				continue;
			}

			for (const auto& Vertex : Section->ProcVertexBuffer)
			{
				if (Vertex.Position.X < MinX || Vertex.Position.X > MaxX)
				{
					return false;
				}
			}
		}

		return true;
	};
	const float HalfReferenceLength = FVector::Dist2D(Wall->LocalStart, Wall->LocalEnd) * 0.5f;
	TestTrue(
		TEXT("Sampled wall should clip horizontal sampled faces to the wall X bounds"),
		IsMeshWithinXBounds(Wall->RightWallMeshComponent, -HalfReferenceLength - 0.05f, HalfReferenceLength + 0.05f));

	DestroyIfValid(Wall);
	DestroyIfValid(StartPillar);
	DestroyIfValid(EndPillar);
	DestroyIfValid(Building);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEHBWallSampleStartOffsetUsesChainOriginTest,
	"EHB.Relationship.WallSampleStartOffsetUsesChainOrigin",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEHBWallSampleStartOffsetUsesChainOriginTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("Editor world should be available"), World))
	{
		return false;
	}

	const FVector TestOrigin(4700.0f, 500.0f, 140000.0f);
	AEHB_Building* Building = SpawnTransientActor<AEHB_Building>(World, TestOrigin);
	AEHB_Pillar* PillarA = SpawnTransientActor<AEHB_Pillar>(World, TestOrigin);
	AEHB_Pillar* PillarB = SpawnTransientActor<AEHB_Pillar>(World, TestOrigin + FVector(400.0f, 0.0f, 0.0f));
	AEHB_Pillar* PillarC = SpawnTransientActor<AEHB_Pillar>(World, TestOrigin + FVector(800.0f, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("Building should spawn"), Building)
		|| !TestNotNull(TEXT("First pillar should spawn"), PillarA)
		|| !TestNotNull(TEXT("Second pillar should spawn"), PillarB)
		|| !TestNotNull(TEXT("Third pillar should spawn"), PillarC))
	{
		DestroyIfValid(PillarC);
		DestroyIfValid(PillarB);
		DestroyIfValid(PillarA);
		DestroyIfValid(Building);
		return false;
	}

	PillarA->AttachToBuilding(Building, FTransform(FVector::ZeroVector));
	PillarB->AttachToBuilding(Building, FTransform(FVector(400.0f, 0.0f, 0.0f)));
	PillarC->AttachToBuilding(Building, FTransform(FVector(800.0f, 0.0f, 0.0f)));
	PillarA->SetFloorAssignment(1, EEHBBuildingFloorElementRole::FloorBody);
	PillarB->SetFloorAssignment(1, EEHBBuildingFloorElementRole::FloorBody);
	PillarC->SetFloorAssignment(1, EEHBBuildingFloorElementRole::FloorBody);

	AEHB_Wall* WallAB = Building->ConnectPillars(PillarA, PillarB, 300.0f, 20.0f);
	AEHB_Wall* WallBC = Building->ConnectPillars(PillarB, PillarC, 300.0f, 20.0f);
	if (!TestNotNull(TEXT("First wall should be created"), WallAB)
		|| !TestNotNull(TEXT("Second wall should be created"), WallBC))
	{
		DestroyIfValid(WallBC);
		DestroyIfValid(WallAB);
		DestroyIfValid(PillarC);
		DestroyIfValid(PillarB);
		DestroyIfValid(PillarA);
		DestroyIfValid(Building);
		return false;
	}

	FEHBWallMeshData SampleRow;
	SampleRow.WallWidth = 200.0f;
	SampleRow.WallHeight = 300.0f;
	SampleRow.WallThickness = 20.0f;
	SampleRow.WallLocalBoundsMin = FVector(0.0f, -10.0f, 0.0f);
	SampleRow.WallLocalBoundsMax = FVector(200.0f, 10.0f, 300.0f);

	UDataTable* SampleTable = NewObject<UDataTable>(GetTransientPackage(), NAME_None, RF_Transient);
	SampleTable->RowStruct = FEHBWallMeshData::StaticStruct();
	SampleTable->AddRow(TEXT("OffsetWall"), SampleRow);

	auto ConfigureSampledRightSurface = [SampleTable](AEHB_Wall* Wall, float Offset)
	{
		if (!Wall)
		{
			return;
		}

		Wall->RightSurfaceStyle.SourceType = EEHBWallSurfaceSourceType::SampledMesh;
		Wall->RightSurfaceStyle.SampledWallRow.DataTable = SampleTable;
		Wall->RightSurfaceStyle.SampledWallRow.RowName = TEXT("OffsetWall");
		Wall->RightSurfaceStyle.SampleSide = EEHBWallMeshSampleSide::Front;
		Wall->RightSurfaceStyle.SampleStartOffset = Offset;
		Wall->RightSurfaceStyle.OverrideMaterial.Reset();
	};

	ConfigureSampledRightSurface(WallAB, 0.0f);
	ConfigureSampledRightSurface(WallBC, 0.0f);

	float WallABEndPhaseWithoutOffset = 0.0f;
	float WallBCStartPhaseWithoutOffset = 0.0f;
	TestTrue(
		TEXT("First sampled wall endpoint phase should be available"),
		WallAB->GetSurfaceSamplePhaseAtEndpoint(false, false, WallABEndPhaseWithoutOffset));
	TestTrue(
		TEXT("Second sampled wall start phase should be available"),
		WallBC->GetSurfaceSamplePhaseAtEndpoint(false, true, WallBCStartPhaseWithoutOffset));

	ConfigureSampledRightSurface(WallAB, 0.25f);
	ConfigureSampledRightSurface(WallBC, 0.25f);

	float WallABEndPhaseWithOffset = 0.0f;
	float WallBCStartPhaseWithOffset = 0.0f;
	TestTrue(
		TEXT("First sampled wall endpoint phase with offset should be available"),
		WallAB->GetSurfaceSamplePhaseAtEndpoint(false, false, WallABEndPhaseWithOffset));
	TestTrue(
		TEXT("Second sampled wall start phase with offset should be available"),
		WallBC->GetSurfaceSamplePhaseAtEndpoint(false, true, WallBCStartPhaseWithOffset));

	constexpr float ExpectedManualPhaseOffset = 50.0f;
	TestTrue(
		TEXT("First sampled wall should apply the manual offset once"),
		FMath::IsNearlyEqual(
			WallABEndPhaseWithOffset - WallABEndPhaseWithoutOffset,
			ExpectedManualPhaseOffset,
			0.01f));
	TestTrue(
		TEXT("Continuous sampled wall should inherit the chain-origin offset without adding it again"),
		FMath::IsNearlyEqual(
			WallBCStartPhaseWithOffset - WallBCStartPhaseWithoutOffset,
			ExpectedManualPhaseOffset,
			0.01f));

	DestroyIfValid(WallBC);
	DestroyIfValid(WallAB);
	DestroyIfValid(PillarC);
	DestroyIfValid(PillarB);
	DestroyIfValid(PillarA);
	DestroyIfValid(Building);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEHBCurvedWallEndpointMatchesPillarFootprintTest,
	"EHB.Relationship.CurvedWallEndpointMatchesPillarFootprint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEHBCurvedWallEndpointMatchesPillarFootprintTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("Editor world should be available"), World))
	{
		return false;
	}

	const FVector TestOrigin(5400.0f, 1200.0f, 140000.0f);
	AEHB_Building* Building = SpawnTransientActor<AEHB_Building>(World, TestOrigin);
	AEHB_Pillar* StartPillar = SpawnTransientActor<AEHB_Pillar>(World, TestOrigin);
	AEHB_Pillar* EndPillar = SpawnTransientActor<AEHB_Pillar>(World, TestOrigin + FVector(400.0f, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("Building should spawn"), Building)
		|| !TestNotNull(TEXT("Start pillar should spawn"), StartPillar)
		|| !TestNotNull(TEXT("End pillar should spawn"), EndPillar))
	{
		DestroyIfValid(EndPillar);
		DestroyIfValid(StartPillar);
		DestroyIfValid(Building);
		return false;
	}

	StartPillar->AttachToBuilding(Building, FTransform(FVector::ZeroVector));
	EndPillar->AttachToBuilding(Building, FTransform(FVector(400.0f, 0.0f, 0.0f)));
	StartPillar->ConfigureAsPolygonPillar(300.0f, 40.0f, 40.0f, FTransform(FVector::ZeroVector), true);
	EndPillar->ConfigureAsPolygonPillar(300.0f, 40.0f, 40.0f, FTransform(FVector(400.0f, 0.0f, 0.0f)), true);
	StartPillar->SetFloorAssignment(1, EEHBBuildingFloorElementRole::FloorBody);
	EndPillar->SetFloorAssignment(1, EEHBBuildingFloorElementRole::FloorBody);

	AEHB_Wall* Wall = Building->ConnectPillars(StartPillar, EndPillar, 300.0f, 20.0f);
	if (!TestNotNull(TEXT("Wall should be created"), Wall))
	{
		DestroyIfValid(EndPillar);
		DestroyIfValid(StartPillar);
		DestroyIfValid(Building);
		return false;
	}

	Wall->ApplyCurveSettings(80.0f, 20.0f, true);

	TArray<FVector> LeftPolyline;
	TArray<FVector> RightPolyline;
	const bool bHasLeftPolyline = Wall->BuildSideTopPolylineInBuildingSpace(true, LeftPolyline, 20.0f);
	const bool bHasRightPolyline = Wall->BuildSideTopPolylineInBuildingSpace(false, RightPolyline, 20.0f);
	TestTrue(TEXT("Curved wall left side should produce a polyline"), bHasLeftPolyline && LeftPolyline.Num() >= 2);
	TestTrue(TEXT("Curved wall right side should produce a polyline"), bHasRightPolyline && RightPolyline.Num() >= 2);

	if (bHasLeftPolyline && bHasRightPolyline && LeftPolyline.Num() >= 2 && RightPolyline.Num() >= 2)
	{
		constexpr float BoundaryTolerance = 0.75f;
		TestTrue(
			TEXT("Curved wall left start should trim to the start pillar footprint"),
			IsBuildingLocalPointOnPillarFootprint(StartPillar, LeftPolyline[0], BoundaryTolerance));
		TestTrue(
			TEXT("Curved wall right start should trim to the start pillar footprint"),
			IsBuildingLocalPointOnPillarFootprint(StartPillar, RightPolyline[0], BoundaryTolerance));
		TestTrue(
			TEXT("Curved wall left end should trim to the end pillar footprint"),
			IsBuildingLocalPointOnPillarFootprint(EndPillar, LeftPolyline.Last(), BoundaryTolerance));
		TestTrue(
			TEXT("Curved wall right end should trim to the end pillar footprint"),
			IsBuildingLocalPointOnPillarFootprint(EndPillar, RightPolyline.Last(), BoundaryTolerance));
	}

	DestroyIfValid(Wall);
	DestroyIfValid(EndPillar);
	DestroyIfValid(StartPillar);
	DestroyIfValid(Building);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEHBPillarInheritsConnectedWallSurfaceSampleTest,
	"EHB.Relationship.PillarInheritsConnectedWallSurfaceSample",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEHBPillarInheritsConnectedWallSurfaceSampleTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("Editor world should be available"), World))
	{
		return false;
	}

	const FVector TestOrigin(5000.0f, 600.0f, 140000.0f);
	AEHB_Building* Building = SpawnTransientActor<AEHB_Building>(World, TestOrigin);
	AEHB_Pillar* StartPillar = SpawnTransientActor<AEHB_Pillar>(World, TestOrigin);
	AEHB_Pillar* EndPillar = SpawnTransientActor<AEHB_Pillar>(World, TestOrigin + FVector(400.0f, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("Building should spawn"), Building)
		|| !TestNotNull(TEXT("Start pillar should spawn"), StartPillar)
		|| !TestNotNull(TEXT("End pillar should spawn"), EndPillar))
	{
		DestroyIfValid(EndPillar);
		DestroyIfValid(StartPillar);
		DestroyIfValid(Building);
		return false;
	}

	StartPillar->AttachToBuilding(Building, FTransform(FVector::ZeroVector));
	EndPillar->AttachToBuilding(Building, FTransform(FVector(400.0f, 0.0f, 0.0f)));
	StartPillar->SetFloorAssignment(1, EEHBBuildingFloorElementRole::FloorBody);
	EndPillar->SetFloorAssignment(1, EEHBBuildingFloorElementRole::FloorBody);

	AEHB_Wall* Wall = Building->ConnectPillars(StartPillar, EndPillar, 300.0f, 20.0f);
	if (!TestNotNull(TEXT("Wall should be created"), Wall))
	{
		DestroyIfValid(EndPillar);
		DestroyIfValid(StartPillar);
		DestroyIfValid(Building);
		return false;
	}

	UMaterial* SourceMaterialA = NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient);
	UMaterial* SourceMaterialB = NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient);
	UMaterial* SimpleConnectedMaterial = NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient);
	UMaterial* OppositeConnectedMaterial = NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient);
	if (!TestNotNull(TEXT("First inherited source material should be available"), SourceMaterialA)
		|| !TestNotNull(TEXT("Second inherited source material should be available"), SourceMaterialB)
		|| !TestNotNull(TEXT("Simple connected material should be available"), SimpleConnectedMaterial)
		|| !TestNotNull(TEXT("Opposite connected material should be available"), OppositeConnectedMaterial))
	{
		DestroyIfValid(Wall);
		DestroyIfValid(EndPillar);
		DestroyIfValid(StartPillar);
		DestroyIfValid(Building);
		return false;
	}

	FEHBWallMeshData SampleRow;
	SampleRow.WallWidth = 40.0f;
	SampleRow.WallHeight = 300.0f;
	SampleRow.WallThickness = 20.0f;
	SampleRow.WallLocalBoundsMin = FVector(0.0f, -10.0f, 0.0f);
	SampleRow.WallLocalBoundsMax = FVector(40.0f, 10.0f, 300.0f);
	SampleRow.Materials.Add(SourceMaterialA);
	SampleRow.Materials.Add(SourceMaterialB);

	auto AddSampleSurface = [](FEHBWallMeshSampleSurface& Surface, EEHBWallMeshSampleSide Side, float Y)
	{
		Surface.SampleSide = Side;
		Surface.Vertices.Reset();
		Surface.Triangles.Reset();
		Surface.TriangleMaterialIndices.Reset();

		auto AddVertex = [&Surface, Y](float X, float Z)
		{
			FEHBWallMeshSampleVertex Vertex;
			Vertex.Position = FVector(X, Y, Z);
			Vertex.Normal = FVector(0.0f, Y >= 0.0f ? 1.0f : -1.0f, 0.0f);
			Vertex.Tangent = FVector::ForwardVector;
			Vertex.UV0 = FVector2D(X / 40.0f, Z / 300.0f);
			return Surface.Vertices.Add(Vertex);
		};

		const int32 A = AddVertex(0.0f, 0.0f);
		const int32 B = AddVertex(0.0f, 300.0f);
		const int32 C = AddVertex(40.0f, 300.0f);
		const int32 D = AddVertex(0.0f, 0.0f);
		const int32 E = AddVertex(40.0f, 300.0f);
		const int32 F = AddVertex(40.0f, 0.0f);
		const int32 G = AddVertex(39.98f, 0.0f);
		const int32 H = AddVertex(40.0f, 300.0f);
		const int32 I = AddVertex(40.0f, 0.0f);

		Surface.Triangles = { A, B, C, D, E, F, G, H, I };
		Surface.TriangleMaterialIndices = { 0, 1, 0 };
		Surface.SampleTriangleCount = 3;
	};

	AddSampleSurface(SampleRow.FrontSurface, EEHBWallMeshSampleSide::Front, 10.0f);
	AddSampleSurface(SampleRow.BackSurface, EEHBWallMeshSampleSide::Back, -10.0f);

	UDataTable* SampleTable = NewObject<UDataTable>(GetTransientPackage(), NAME_None, RF_Transient);
	SampleTable->RowStruct = FEHBWallMeshData::StaticStruct();
	SampleTable->AddRow(TEXT("InheritedWall"), SampleRow);

	Wall->RightSurfaceStyle.SourceType = EEHBWallSurfaceSourceType::SampledMesh;
	Wall->RightSurfaceStyle.SampledWallRow.DataTable = SampleTable;
	Wall->RightSurfaceStyle.SampledWallRow.RowName = TEXT("InheritedWall");
	Wall->RightSurfaceStyle.SampleSide = EEHBWallMeshSampleSide::Front;
	Wall->RightSurfaceStyle.OverrideMaterial.Reset();
	Wall->RebuildWallMesh();
	Wall->RebuildConnectedPillarMeshes();

	auto ContainsMaterial = [](UEHBGeneratedMeshComponent* Component, UMaterialInterface* Material)
	{
		if (!Component || !Material)
		{
			return false;
		}

		for (int32 SectionIndex = 0; SectionIndex < Component->GetNumSections(); ++SectionIndex)
		{
			if (Component->GetMaterial(SectionIndex) == Material)
			{
				return true;
			}
		}

		return false;
	};

	TestTrue(
		TEXT("Sampled wall should add inherited surface sections to the connected pillar"),
		StartPillar->PillarMeshComponent && StartPillar->PillarMeshComponent->GetNumSections() > 1);
	TestTrue(
		TEXT("Inherited pillar surface should use the first wall source material"),
		ContainsMaterial(StartPillar->PillarMeshComponent, SourceMaterialA));
	TestTrue(
		TEXT("Inherited pillar surface should not contain clipped needle triangles"),
		DoesGeneratedMeshAvoidNeedleTriangles(StartPillar->PillarMeshComponent));

	StartPillar->bInheritConnectedWallSurfaceSamples = false;
	StartPillar->RebuildPillarMesh();
	TestEqual(
		TEXT("Disabling inheritance should return the polygon pillar to one generated material section"),
		StartPillar->PillarMeshComponent ? StartPillar->PillarMeshComponent->GetNumSections() : 0,
		1);

	TestTrue(
		TEXT("Explicit connected surface override should work even when automatic inheritance is disabled"),
		StartPillar->SetConnectedWallSurfaceOverride(Wall->ElementGuid, Wall->RightSurfaceStyle, true));
	TestTrue(
		TEXT("Explicit pillar surface override should restore sampled surface sections"),
		StartPillar->PillarMeshComponent && StartPillar->PillarMeshComponent->GetNumSections() > 1);
	TestTrue(
		TEXT("Explicit pillar surface override should use the wall source material"),
		ContainsMaterial(StartPillar->PillarMeshComponent, SourceMaterialA));
	TestTrue(
		TEXT("Explicit pillar surface override should not contain clipped needle triangles"),
		DoesGeneratedMeshAvoidNeedleTriangles(StartPillar->PillarMeshComponent));

	TestTrue(
		TEXT("Clearing explicit pillar surface override should succeed"),
		StartPillar->ClearConnectedWallSurfaceOverride(Wall->ElementGuid, true));
	StartPillar->RebuildPillarMesh();
	TestEqual(
		TEXT("Clearing override while inheritance is disabled should return to one section"),
		StartPillar->PillarMeshComponent ? StartPillar->PillarMeshComponent->GetNumSections() : 0,
		1);

	FEHBWallSurfaceStyle SimpleSurfaceStyle;
	SimpleSurfaceStyle.SourceType = EEHBWallSurfaceSourceType::Simple;
	SimpleSurfaceStyle.OverrideMaterial = TSoftObjectPtr<UMaterialInterface>(SimpleConnectedMaterial);
	TestTrue(
		TEXT("Simple material connected side override should work"),
		StartPillar->SetConnectedWallSurfaceOverrideForSide(Wall->ElementGuid, SimpleSurfaceStyle, false, false, true));
	TestTrue(
		TEXT("Simple material connected side override should create a separate pillar material section"),
		StartPillar->PillarMeshComponent && StartPillar->PillarMeshComponent->GetNumSections() > 1);
	TestTrue(
		TEXT("Simple material connected side override should use the override material"),
		ContainsMaterial(StartPillar->PillarMeshComponent, SimpleConnectedMaterial));
	const FEHBPillarConnectedSurfaceOverride* SimpleOverride = StartPillar->ConnectedSurfaceOverrides.FindByPredicate(
		[Wall](const FEHBPillarConnectedSurfaceOverride& Override)
		{
			return Override.WallGuid == Wall->ElementGuid;
		});
	TestTrue(TEXT("Simple side override should be stored"), SimpleOverride != nullptr);
	if (SimpleOverride)
	{
		TestFalse(TEXT("Simple side override should not target the opposite wall side"), SimpleOverride->bApplyLeftWallSide);
		TestTrue(TEXT("Simple side override should target the requested wall side"), SimpleOverride->bApplyRightWallSide);
		TestFalse(TEXT("Simple side override should leave the wall end cap unchanged"), SimpleOverride->bApplyWallEndCap);
	}

	int32 SimpleMaterialIndex = INDEX_NONE;
	for (int32 MaterialIndex = 0; MaterialIndex < StartPillar->PolygonPillarSourceMaterials.Num(); ++MaterialIndex)
	{
		if (StartPillar->PolygonPillarSourceMaterials[MaterialIndex].Get() == SimpleConnectedMaterial)
		{
			SimpleMaterialIndex = MaterialIndex;
			break;
		}
	}
	TestTrue(TEXT("Simple side override material should be listed as a pillar source material"), SimpleMaterialIndex > 0);
	if (SimpleMaterialIndex > 0)
	{
		int32 SimpleMaterialTriangleCount = 0;
		for (const int32 TriangleMaterialIndex : StartPillar->PolygonPillarTriangleMaterialIndices)
		{
			if (TriangleMaterialIndex == SimpleMaterialIndex)
			{
				++SimpleMaterialTriangleCount;
			}
		}
		TestEqual(TEXT("Simple side override should affect only one pillar side face"), SimpleMaterialTriangleCount, 2);
	}

	FEHBWallSurfaceStyle OppositeSurfaceStyle;
	OppositeSurfaceStyle.SourceType = EEHBWallSurfaceSourceType::Simple;
	OppositeSurfaceStyle.OverrideMaterial = TSoftObjectPtr<UMaterialInterface>(OppositeConnectedMaterial);
	TestTrue(
		TEXT("Opposite simple material connected side override should work"),
		StartPillar->SetConnectedWallSurfaceOverrideForSide(Wall->ElementGuid, OppositeSurfaceStyle, true, false, true));
	TestTrue(
		TEXT("Applying an opposite side material should keep the original side material"),
		ContainsMaterial(StartPillar->PillarMeshComponent, SimpleConnectedMaterial));
	TestTrue(
		TEXT("Applying an opposite side material should use the new opposite side material"),
		ContainsMaterial(StartPillar->PillarMeshComponent, OppositeConnectedMaterial));
	const FEHBPillarConnectedSurfaceOverride* TwoSidedOverride = StartPillar->ConnectedSurfaceOverrides.FindByPredicate(
		[Wall](const FEHBPillarConnectedSurfaceOverride& Override)
		{
			return Override.WallGuid == Wall->ElementGuid;
		});
	TestTrue(TEXT("Two-sided simple override should be stored"), TwoSidedOverride != nullptr);
	if (TwoSidedOverride)
	{
		TestTrue(TEXT("Two-sided simple override should use per-side styles"), TwoSidedOverride->bUsePerSideSurfaceStyles);
		TestTrue(TEXT("Two-sided simple override should keep the first wall side enabled"), TwoSidedOverride->bApplyRightWallSide);
		TestTrue(TEXT("Two-sided simple override should enable the opposite wall side"), TwoSidedOverride->bApplyLeftWallSide);
		TestEqual(TEXT("Two-sided simple override should preserve the first wall side material"), TwoSidedOverride->RightWallSideSurfaceStyle.OverrideMaterial.Get(), Cast<UMaterialInterface>(SimpleConnectedMaterial));
		TestEqual(TEXT("Two-sided simple override should store the opposite wall side material"), TwoSidedOverride->LeftWallSideSurfaceStyle.OverrideMaterial.Get(), Cast<UMaterialInterface>(OppositeConnectedMaterial));
	}

	auto FindPillarMaterialIndex = [StartPillar](UMaterialInterface* Material) -> int32
	{
		for (int32 MaterialIndex = 0; MaterialIndex < StartPillar->PolygonPillarSourceMaterials.Num(); ++MaterialIndex)
		{
			if (StartPillar->PolygonPillarSourceMaterials[MaterialIndex].Get() == Material)
			{
				return MaterialIndex;
			}
		}
		return INDEX_NONE;
	};
	auto CountPillarMaterialTriangles = [StartPillar](int32 MaterialIndex)
	{
		int32 Count = 0;
		for (const int32 TriangleMaterialIndex : StartPillar->PolygonPillarTriangleMaterialIndices)
		{
			if (TriangleMaterialIndex == MaterialIndex)
			{
				++Count;
			}
		}
		return Count;
	};
	const int32 PreservedMaterialIndex = FindPillarMaterialIndex(SimpleConnectedMaterial);
	const int32 OppositeMaterialIndex = FindPillarMaterialIndex(OppositeConnectedMaterial);
	TestTrue(TEXT("Preserved side material should remain listed"), PreservedMaterialIndex > 0);
	TestTrue(TEXT("Opposite side material should be listed"), OppositeMaterialIndex > 0);
	if (PreservedMaterialIndex > 0 && OppositeMaterialIndex > 0)
	{
		TestEqual(TEXT("Preserved side material should still affect only one pillar side face"), CountPillarMaterialTriangles(PreservedMaterialIndex), 2);
		TestEqual(TEXT("Opposite side material should affect only one pillar side face"), CountPillarMaterialTriangles(OppositeMaterialIndex), 2);
	}
	if (StartPillar->PolygonPillarTriangleMaterialIndices.Num() >= 2)
	{
		TestEqual(TEXT("Pillar top first triangle should keep the base material"), StartPillar->PolygonPillarTriangleMaterialIndices[0], 0);
		TestEqual(TEXT("Pillar top second triangle should keep the base material"), StartPillar->PolygonPillarTriangleMaterialIndices[1], 0);
	}

	DestroyIfValid(Wall);
	DestroyIfValid(EndPillar);
	DestroyIfValid(StartPillar);
	DestroyIfValid(Building);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEHBRelationshipMultipleDoorWindowsOnWallTest,
	"EHB.Relationship.MultipleDoorWindowsOnWall",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEHBRelationshipMultipleDoorWindowsOnWallTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("Editor world should be available"), World))
	{
		return false;
	}

	const FVector TestOrigin(18000.0f, 0.0f, 140000.0f);
	AEHB_Building* Building = SpawnTransientActor<AEHB_Building>(World, TestOrigin);
	AEHB_Wall* Wall = SpawnTransientActor<AEHB_Wall>(World, TestOrigin);
	AEHB_DoorWindow* Door = SpawnTransientActor<AEHB_DoorWindow>(World, TestOrigin);
	AEHB_DoorWindow* Window = SpawnTransientActor<AEHB_DoorWindow>(World, TestOrigin);
	AEHB_DoorWindow* ReplacementWindow = SpawnTransientActor<AEHB_DoorWindow>(World, TestOrigin);
	if (!TestNotNull(TEXT("Building should spawn"), Building)
		|| !TestNotNull(TEXT("Wall should spawn"), Wall)
		|| !TestNotNull(TEXT("Door should spawn"), Door)
		|| !TestNotNull(TEXT("Window should spawn"), Window)
		|| !TestNotNull(TEXT("Replacement window should spawn"), ReplacementWindow))
	{
		DestroyIfValid(ReplacementWindow);
		DestroyIfValid(Window);
		DestroyIfValid(Door);
		DestroyIfValid(Wall);
		DestroyIfValid(Building);
		return false;
	}

	Wall->ConfigureAsSimpleWall(
		Building,
		nullptr,
		nullptr,
		FVector::ZeroVector,
		FVector(600.0f, 0.0f, 0.0f),
		300.0f,
		20.0f,
		true);

	auto PlaceDoorWindow = [Building, Wall](AEHB_DoorWindow* DoorWindow, float DistanceFromStart)
	{
		if (!DoorWindow || !Wall)
		{
			return false;
		}

		DoorWindow->AttachToBuilding(Building, FTransform::Identity);

		const float WallLength = FVector::Dist2D(Wall->LocalStart, Wall->LocalEnd);
		if (WallLength <= UE_SMALL_NUMBER)
		{
			return false;
		}

		const FVector WallLocalLocation(
			-WallLength * 0.5f + FMath::Clamp(DistanceFromStart, 0.0f, WallLength),
			0.0f,
			DoorWindow->GetOpeningBottomHeight());
		const FTransform DoorWindowWorldTransform(
			Wall->GetActorQuat(),
			Wall->GetActorTransform().TransformPosition(WallLocalLocation),
			FVector::OneVector);
		DoorWindow->SetActorTransform(DoorWindowWorldTransform, false, nullptr, ETeleportType::TeleportPhysics);
		DoorWindow->BindToWall(Wall, DistanceFromStart);
		return true;
	};

	Door->InitializeDefaultDoorOpening();
	Window->InitializeDefaultWindowOpening();
	ReplacementWindow->InitializeDefaultWindowOpening();
	Door->SillHeight = 45.0f;
	if (TestNotNull(TEXT("Door opening spline should exist"), Door->OpeningSplineComponent.Get()))
	{
		Door->OpeningSplineComponent->ClearSplinePoints(false);
		Door->OpeningSplineComponent->AddSplinePoint(FVector(-45.0f, 0.0f, 40.0f), ESplineCoordinateSpace::Local, false);
		Door->OpeningSplineComponent->AddSplinePoint(FVector(-45.0f, 0.0f, 220.0f), ESplineCoordinateSpace::Local, false);
		Door->OpeningSplineComponent->AddSplinePoint(FVector(45.0f, 0.0f, 220.0f), ESplineCoordinateSpace::Local, false);
		Door->OpeningSplineComponent->AddSplinePoint(FVector(45.0f, 0.0f, 40.0f), ESplineCoordinateSpace::Local, false);
		Door->OpeningSplineComponent->SetClosedLoop(true, false);
		Door->OpeningSplineComponent->UpdateSpline();
		Door->HandleOpeningSplineEdited();
	}
	TestEqual(TEXT("Default door opening should be a door"), Door->Kind, EEHBDoorWindowElementKind::Door);
	TestTrue(TEXT("Default door opening should sit on the ground"), FMath::IsNearlyZero(Door->GetOpeningBottomHeight()));
	TestTrue(TEXT("Default door opening should be rectangular"), Door->OpeningHeight > Door->OpeningWidth);
	TestEqual(TEXT("Default window opening should be a window"), Window->Kind, EEHBDoorWindowElementKind::Window);
	TestTrue(TEXT("Default window opening should be square"), FMath::IsNearlyEqual(Window->OpeningWidth, Window->OpeningHeight));
	TestTrue(TEXT("Default window opening should be 90cm above the ground"), FMath::IsNearlyEqual(Window->GetOpeningBottomHeight(), 90.0f));

	TestTrue(TEXT("Door should bind to the wall"), PlaceDoorWindow(Door, 160.0f));
	TestTrue(TEXT("Window should bind to the same wall"), PlaceDoorWindow(Window, 430.0f));
	Wall->RebuildWallMesh();

	TestEqual(TEXT("Wall should keep both door/window connections"), Wall->DoorWindowConnections.Num(), 2);
	if (Wall->DoorWindowConnections.Num() > 0)
	{
		TestTrue(TEXT("Door connection should be stored at ground level"), FMath::IsNearlyZero(Wall->DoorWindowConnections[0].BottomHeight));
	}

	const FVector2D DoorCenterInWallXZ(-140.0f, 75.0f);
	const FVector2D DoorLowerOpeningInWallXZ(-140.0f, 10.0f);
	const FVector2D WindowCenterInWallXZ(130.0f, 165.0f);
	TestFalse(
		TEXT("Left wall surface should keep the first door opening cut"),
		DoesWallSurfaceCoverWallPoint(Wall->LeftWallMeshComponent, DoorCenterInWallXZ));
	TestFalse(
		TEXT("Right wall surface should keep the first door opening cut"),
		DoesWallSurfaceCoverWallPoint(Wall->RightWallMeshComponent, DoorCenterInWallXZ));
	TestFalse(
		TEXT("Door opening should cut down to the floor on the left wall surface"),
		DoesWallSurfaceCoverWallPoint(Wall->LeftWallMeshComponent, DoorLowerOpeningInWallXZ));
	TestFalse(
		TEXT("Door opening should cut down to the floor on the right wall surface"),
		DoesWallSurfaceCoverWallPoint(Wall->RightWallMeshComponent, DoorLowerOpeningInWallXZ));
	TestFalse(
		TEXT("Left wall surface should keep the second window opening cut"),
		DoesWallSurfaceCoverWallPoint(Wall->LeftWallMeshComponent, WindowCenterInWallXZ));
	TestFalse(
		TEXT("Right wall surface should keep the second window opening cut"),
		DoesWallSurfaceCoverWallPoint(Wall->RightWallMeshComponent, WindowCenterInWallXZ));

	FEHBRelationQuery DoorHostQuery;
	DoorHostQuery.ElementGuid = Door->ElementGuid;
	DoorHostQuery.Direction = EEHBRelationQueryDirection::Incoming;
	DoorHostQuery.Types = { EEHBElementRelationType::HostedElement };
	TestEqual(TEXT("Door should have one host relation"), Building->QueryElementRelations(DoorHostQuery).Num(), 1);

	FEHBRelationQuery WindowHostQuery = DoorHostQuery;
	WindowHostQuery.ElementGuid = Window->ElementGuid;
	TestEqual(TEXT("Window should have one host relation"), Building->QueryElementRelations(WindowHostQuery).Num(), 1);

	TestTrue(TEXT("Replacement window should initially bind near the existing window"), PlaceDoorWindow(ReplacementWindow, 425.0f));
	TestEqual(TEXT("Wall should temporarily keep the replacement candidate connection"), Wall->DoorWindowConnections.Num(), 3);
	TestEqual(
		TEXT("Wall should detect the touched window"),
		Wall->FindOverlappingDoorWindow(ReplacementWindow, ReplacementWindow->DistanceFromWallStart),
		Window);
	TestTrue(TEXT("Replacement function should replace the touched window"), AEHB_DoorWindow::ReplaceDoorWindow(ReplacementWindow, Window));
	Window = nullptr;
	TestEqual(TEXT("Wall should keep the door and replacement window after replacement"), Wall->DoorWindowConnections.Num(), 2);
	TestTrue(
		TEXT("Replacement should keep the dragged window distance instead of snapping back to the touched window slot"),
		FMath::IsNearlyEqual(ReplacementWindow->DistanceFromWallStart, 425.0f, 0.1f));
	TestTrue(
		TEXT("Wall should contain the replacement window connection"),
		Wall->DoorWindowConnections.ContainsByPredicate(
			[ReplacementWindow](const FEHBWallDoorWindowConnection& Connection)
			{
				return ReplacementWindow && Connection.DoorWindowGuid == ReplacementWindow->ElementGuid;
			}));

	FEHBRelationQuery ReplacementWindowHostQuery = DoorHostQuery;
	ReplacementWindowHostQuery.ElementGuid = ReplacementWindow->ElementGuid;
	TestEqual(TEXT("Replacement window should have one host relation"), Building->QueryElementRelations(ReplacementWindowHostQuery).Num(), 1);

	FEHBWallDoorWindowConnection UnresolvedSerializedConnection = Wall->DoorWindowConnections[0];
	UnresolvedSerializedConnection.DoorWindowGuid = FGuid::NewGuid();
	UnresolvedSerializedConnection.DistanceFromStart = 520.0f;
	const FGuid UnresolvedSerializedGuid = UnresolvedSerializedConnection.DoorWindowGuid;
	Wall->DoorWindowConnections.Add(UnresolvedSerializedConnection);
	Wall->RebuildWallMesh();
	TestTrue(
		TEXT("Wall rebuild should preserve serialized openings whose actor is temporarily unresolved"),
		Wall->DoorWindowConnections.ContainsByPredicate(
			[UnresolvedSerializedGuid](const FEHBWallDoorWindowConnection& Connection)
			{
				return Connection.DoorWindowGuid == UnresolvedSerializedGuid;
			}));

	ReplacementWindow->ClearWallBinding();
	TestFalse(
		TEXT("Explicit clear should still remove the selected replacement window connection"),
		Wall->DoorWindowConnections.ContainsByPredicate(
			[ReplacementWindow](const FEHBWallDoorWindowConnection& Connection)
			{
				return ReplacementWindow && Connection.DoorWindowGuid == ReplacementWindow->ElementGuid;
			}));

	Door->ClearWallBinding();
	DestroyIfValid(ReplacementWindow);
	DestroyIfValid(Window);
	DestroyIfValid(Door);
	DestroyIfValid(Wall);
	DestroyIfValid(Building);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEHBRoomLocationQueryTest,
	"EHB.Relationship.RoomLocationQuery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEHBRoomLocationQueryTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("Editor world should be available"), World))
	{
		return false;
	}

	const FVector TestOrigin(9000.0f, 0.0f, 140000.0f);
	AEHB_Building* Building = SpawnTransientActor<AEHB_Building>(World, TestOrigin);
	if (!TestNotNull(TEXT("Building should spawn"), Building))
	{
		return false;
	}

	TArray<AEHB_Pillar*> Pillars;
	const TArray<FVector> PillarLocations =
	{
		FVector(0.0f, 0.0f, 0.0f),
		FVector(400.0f, 0.0f, 0.0f),
		FVector(400.0f, 400.0f, 0.0f),
		FVector(0.0f, 400.0f, 0.0f),
		FVector(800.0f, 0.0f, 0.0f),
		FVector(800.0f, 400.0f, 0.0f)
	};
	for (const FVector& LocalLocation : PillarLocations)
	{
		AEHB_Pillar* Pillar = SpawnTransientActor<AEHB_Pillar>(World, TestOrigin + LocalLocation);
		if (!TestNotNull(TEXT("Pillar should spawn"), Pillar))
		{
			continue;
		}

		Pillar->AttachToBuilding(Building, FTransform(LocalLocation));
		Pillar->SetFloorAssignment(1, EEHBBuildingFloorElementRole::FloorBody);
		Pillars.Add(Pillar);
	}

	if (Pillars.Num() != PillarLocations.Num())
	{
		DestroyIfValid(Building);
		return false;
	}

	TArray<AEHB_Wall*> Walls;
	auto ConnectAndTrackWall = [Building, &Walls](AEHB_Pillar* A, AEHB_Pillar* B)
	{
		if (AEHB_Wall* Wall = Building->ConnectPillars(A, B, 300.0f, 20.0f))
		{
			Walls.AddUnique(Wall);
		}
	};

	ConnectAndTrackWall(Pillars[0], Pillars[1]);
	ConnectAndTrackWall(Pillars[1], Pillars[2]);
	ConnectAndTrackWall(Pillars[2], Pillars[3]);
	ConnectAndTrackWall(Pillars[3], Pillars[0]);
	ConnectAndTrackWall(Pillars[1], Pillars[4]);
	ConnectAndTrackWall(Pillars[4], Pillars[5]);
	ConnectAndTrackWall(Pillars[5], Pillars[2]);

	Building->RebuildClosedLoops();
	TestEqual(TEXT("Adjacent rooms should produce two closed loops"), Building->GetClosedLoopsByFloor(1).Num(), 2);

	const FVector LeftRoomPoint = TestOrigin + FVector(200.0f, 200.0f, 150.0f);
	const FVector RightRoomPoint = TestOrigin + FVector(600.0f, 200.0f, 150.0f);
	TestEqual(TEXT("World location Z should resolve to floor one"), Building->ResolveFloorIndexFromWorldLocation(LeftRoomPoint), 1);
	TestEqual(TEXT("World Z should resolve to floor one"), Building->ResolveFloorIndexFromWorldZ(LeftRoomPoint.Z), 1);
	TestEqual(
		TEXT("Left room point should find one room"),
		Building->FindClosedLoopsContainingWorldLocation(LeftRoomPoint).Num(),
		1);
	TestEqual(
		TEXT("Right room point should find one room"),
		Building->FindClosedLoopsContainingWorldLocation(RightRoomPoint).Num(),
		1);

	const FVector SharedWallPoint = TestOrigin + FVector(400.0f, 200.0f, 150.0f);
	TestEqual(
		TEXT("A boundary point without normal should be ambiguous"),
		Building->FindClosedLoopsContainingWorldLocation(SharedWallPoint).Num(),
		2);

	FEHBBuildingClosedLoop AmbiguousLoop;
	TestFalse(
		TEXT("Unique hit query should fail when a zero normal produces multiple rooms"),
		Building->FindClosedLoopByWorldHit(SharedWallPoint, FVector::ZeroVector, AmbiguousLoop));

	FEHBBuildingClosedLoop LeftLoop;
	FEHBBuildingClosedLoop RightLoop;
	TestTrue(
		TEXT("Negative X normal should resolve the left room"),
		Building->FindClosedLoopByWorldHit(SharedWallPoint, FVector(-1.0f, 0.0f, 0.0f), LeftLoop));
	TestTrue(
		TEXT("Positive X normal should resolve the right room"),
		Building->FindClosedLoopByWorldHit(SharedWallPoint, FVector(1.0f, 0.0f, 0.0f), RightLoop));
	TestTrue(
		TEXT("Opposite normals should resolve different rooms"),
		LeftLoop.LoopGuid.IsValid() && RightLoop.LoopGuid.IsValid() && LeftLoop.LoopGuid != RightLoop.LoopGuid);

	for (AEHB_Wall* Wall : Walls)
	{
		DestroyIfValid(Wall);
	}
	for (AEHB_Pillar* Pillar : Pillars)
	{
		DestroyIfValid(Pillar);
	}
	DestroyIfValid(Building);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEHBFloorFinishRelationshipTest,
	"EHB.Relationship.FloorFinishSemantics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEHBFloorFinishRelationshipTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("Editor world should be available"), World))
	{
		return false;
	}

	const FVector TestOrigin(13000.0f, 0.0f, 140000.0f);
	AEHB_Building* Building = SpawnTransientActor<AEHB_Building>(World, TestOrigin);
	if (!TestNotNull(TEXT("Building should spawn"), Building))
	{
		return false;
	}

	TArray<AEHB_Pillar*> Pillars;
	const TArray<FVector> PillarLocations =
	{
		FVector(0.0f, 0.0f, 0.0f),
		FVector(400.0f, 0.0f, 0.0f),
		FVector(400.0f, 400.0f, 0.0f),
		FVector(0.0f, 400.0f, 0.0f)
	};
	for (const FVector& LocalLocation : PillarLocations)
	{
		AEHB_Pillar* Pillar = SpawnTransientActor<AEHB_Pillar>(World, TestOrigin + LocalLocation);
		if (!TestNotNull(TEXT("Pillar should spawn"), Pillar))
		{
			continue;
		}

		Pillar->AttachToBuilding(Building, FTransform(LocalLocation));
		Pillar->SetFloorAssignment(1, EEHBBuildingFloorElementRole::FloorBody);
		Pillars.Add(Pillar);
	}

	if (Pillars.Num() != PillarLocations.Num())
	{
		DestroyIfValid(Building);
		return false;
	}

	TArray<AEHB_Wall*> Walls;
	auto ConnectAndTrackWall = [Building, &Walls](AEHB_Pillar* A, AEHB_Pillar* B)
	{
		if (AEHB_Wall* Wall = Building->ConnectPillars(A, B, 300.0f, 20.0f))
		{
			Walls.AddUnique(Wall);
		}
	};

	ConnectAndTrackWall(Pillars[0], Pillars[1]);
	ConnectAndTrackWall(Pillars[1], Pillars[2]);
	ConnectAndTrackWall(Pillars[2], Pillars[3]);
	ConnectAndTrackWall(Pillars[3], Pillars[0]);

	AEHB_FloorSlab* SupportSlab = SpawnTransientActor<AEHB_FloorSlab>(World, TestOrigin + FVector(200.0f, 200.0f, 0.0f));
	if (!TestNotNull(TEXT("Support slab should spawn"), SupportSlab))
	{
		for (AEHB_Wall* Wall : Walls)
		{
			DestroyIfValid(Wall);
		}
		for (AEHB_Pillar* Pillar : Pillars)
		{
			DestroyIfValid(Pillar);
		}
		DestroyIfValid(Building);
		return false;
	}
	SupportSlab->ConfigureDefaultSlab(Building, FTransform(FVector(200.0f, 200.0f, 0.0f)), 600.0f, 20.0f, true);
	SupportSlab->SetFloorAssignment(0, EEHBBuildingFloorElementRole::Foundation);

	Building->RebuildClosedLoops();
	const TArray<FEHBBuildingClosedLoop> Loops = Building->GetClosedLoopsByFloor(1);
	if (!TestEqual(TEXT("Square room should produce one closed loop"), Loops.Num(), 1))
	{
		DestroyIfValid(SupportSlab);
		for (AEHB_Wall* Wall : Walls)
		{
			DestroyIfValid(Wall);
		}
		for (AEHB_Pillar* Pillar : Pillars)
		{
			DestroyIfValid(Pillar);
		}
		DestroyIfValid(Building);
		return false;
	}

	AEHB_Floor* Floor = SpawnTransientActor<AEHB_Floor>(World, TestOrigin);
	if (!TestNotNull(TEXT("Floor finish should spawn"), Floor))
	{
		DestroyIfValid(SupportSlab);
		for (AEHB_Wall* Wall : Walls)
		{
			DestroyIfValid(Wall);
		}
		for (AEHB_Pillar* Pillar : Pillars)
		{
			DestroyIfValid(Pillar);
		}
		DestroyIfValid(Building);
		return false;
	}

	TestTrue(TEXT("Floor should configure from room loop"), Floor->ConfigureFromRoomLoop(Building, Loops[0], 0.0f, true));
	TestEqual(TEXT("Floor element type should be Floor"), Floor->ElementType, EEHBBuildingElementType::Floor);
	TestEqual(TEXT("Floor role should be FloorFinish"), Floor->FloorRole, EEHBBuildingFloorElementRole::FloorFinish);
	TestTrue(
		TEXT("Floor finish should advertise only surface-finish semantics"),
		Floor->HasAllCapabilities(static_cast<int32>(EEHBElementCapability::SurfaceFinish))
			&& !Floor->HasAllCapabilities(static_cast<int32>(EEHBElementCapability::Structural))
			&& !Floor->HasAllCapabilities(static_cast<int32>(EEHBElementCapability::CanSupport)));
	TestTrue(TEXT("Floor render surface should use the visual offset"), FMath::IsNearlyEqual(Floor->GetRenderSurfaceZ(), 0.1f));
	TestEqual(TEXT("Floor should keep the source room loop id"), Floor->RoomLoopGuid, Loops[0].LoopGuid);

	FEHBBuildingClosedLoop ResolvedLoop;
	TestTrue(TEXT("Floor should resolve its room loop"), Floor->TryGetRoomLoop(ResolvedLoop));
	TestEqual(TEXT("Resolved room loop should match the floor room"), ResolvedLoop.LoopGuid, Loops[0].LoopGuid);

	FEHBElementQuery FloorQuery;
	FloorQuery.FloorIndex = 1;
	FloorQuery.ElementTypes = { EEHBBuildingElementType::Floor };
	FloorQuery.FloorRoles = { EEHBBuildingFloorElementRole::FloorFinish };
	FloorQuery.RequiredCapabilities = static_cast<int32>(EEHBElementCapability::SurfaceFinish);
	TestEqual(TEXT("Floor query should find the room finish"), Building->QueryElements(FloorQuery).Num(), 1);

	FEHBRelationQuery FinishQuery;
	FinishQuery.ElementGuid = Floor->ElementGuid;
	FinishQuery.Direction = EEHBRelationQueryDirection::Incoming;
	FinishQuery.Types = { EEHBElementRelationType::SurfaceFinish };
	TestEqual(TEXT("Floor should create a finish relation to the same-height support slab"), Building->QueryElementRelations(FinishQuery).Num(), 1);

	const TArray<AEHBElementActorBase*> AffectedBySlab = SupportSlab->GetAffectedElements(false, 1);
	TestTrue(TEXT("Support slab affected query should include the floor finish"), AffectedBySlab.Contains(Floor));
	const TArray<AEHBElementActorBase*> AffectedByPillar = Pillars[0]->GetAffectedElements(false, 1);
	TestFalse(TEXT("Different-height boundary pillar should not be treated as a floor finish source"), AffectedByPillar.Contains(Floor));

	const FGuid RejectedFloorSupportsWall = Building->SetStructuralSupportRelation(
		Floor,
		Walls[0],
		EEHBElementSurfaceKind::Top,
		EEHBElementSurfaceKind::Bottom,
		FVector::ZeroVector,
		FVector::UpVector,
		1.0f,
		EEHBRelationOrigin::UserAuthored);
	TestFalse(TEXT("Floor finish must not support structural elements"), RejectedFloorSupportsWall.IsValid());

	const FGuid RejectedSlabSupportsFloor = Building->SetStructuralSupportRelation(
		SupportSlab,
		Floor,
		EEHBElementSurfaceKind::Top,
		EEHBElementSurfaceKind::Bottom,
		FVector::ZeroVector,
		FVector::UpVector,
		1.0f,
		EEHBRelationOrigin::UserAuthored);
	TestFalse(TEXT("Floor finish must not be a structural support target"), RejectedSlabSupportsFloor.IsValid());

	FEHBElementRelation InvalidSupport;
	InvalidSupport.Type = EEHBElementRelationType::StructuralSupport;
	InvalidSupport.Source = FEHBElementRelationEndpoint::MakeElement(Pillars[0]->ElementGuid, EEHBElementSurfaceKind::Top);
	InvalidSupport.Target = FEHBElementRelationEndpoint::MakeElement(Floor->ElementGuid, EEHBElementSurfaceKind::Bottom);
	InvalidSupport.Origin = EEHBRelationOrigin::UserAuthored;
	TestFalse(
		TEXT("Generic relation entry should reject floor finish as structural target"),
		Building->AddOrUpdateElementRelation(InvalidSupport, true).IsValid());

	TestEqual(TEXT("Floor editor collision should be disabled by default"), Floor->MeshComponent->GetCollisionEnabled(), ECollisionEnabled::NoCollision);

	FEHBFloorSupportSurface SplitSupportSurface;
	SplitSupportSurface.OuterPolygon = {
		FVector(0.0f, 0.0f, 0.0f),
		FVector(400.0f, 0.0f, 0.0f),
		FVector(400.0f, 400.0f, 0.0f),
		FVector(0.0f, 400.0f, 0.0f)
	};
	FEHBFloorFinishHole& SplitHole = SplitSupportSurface.Holes.AddDefaulted_GetRef();
	SplitHole.LocalPolygon = {
		FVector(180.0f, -10.0f, 0.0f),
		FVector(220.0f, -10.0f, 0.0f),
		FVector(220.0f, 410.0f, 0.0f),
		FVector(180.0f, 410.0f, 0.0f)
	};

	TArray<FEHBFloorFinishRegion> SplitRegions;
	TestTrue(
		TEXT("A through support hole should still produce floor finish regions"),
		AEHB_Floor::BuildFloorFinishRegionsFromSupportSurfaces(
			SplitSupportSurface.OuterPolygon,
			{ SplitSupportSurface },
			0.0f,
			SplitRegions));
	TestEqual(TEXT("A through support hole should split the floor into two regions"), SplitRegions.Num(), 2);
	TestTrue(TEXT("Split floor regions should rebuild as separate mesh components"), Floor->SetFloorRegions(SplitRegions, false));
	TestTrue(TEXT("Split floor should own at least two region mesh components"), Floor->RegionMeshComponents.Num() >= 2);
	bool bAllSplitRegionMeshesUseTopVisibleWinding = true;
	int32 TestedSplitRegionMeshCount = 0;
	for (TObjectPtr<UEHBGeneratedMeshComponent>& RegionMeshComponent : Floor->RegionMeshComponents)
	{
		if (UEHBGeneratedMeshComponent* Component = RegionMeshComponent.Get())
		{
			++TestedSplitRegionMeshCount;
			bAllSplitRegionMeshesUseTopVisibleWinding = bAllSplitRegionMeshesUseTopVisibleWinding
				&& HasTopVisibleFloorWinding(Component);
		}
	}
	TestTrue(
		TEXT("Split floor finish mesh components should keep their top side visible"),
		TestedSplitRegionMeshCount >= 2 && bAllSplitRegionMeshesUseTopVisibleWinding);


	auto CaptureFinish = [&](bool bIgnoreTransientRevisions=false)
	{
		FString State=Floor->RoomLoopGuid.ToString()+FString::FromInt(Floor->RoomFloorIndex)+FString::FromInt(Floor->FloorIndex);
		for(const auto& P:Floor->LocalFloorPolygon)State+=P.ToString();
		for(const auto& Region:Floor->FloorRegions)
		{
			State+=TEXT("Region");for(const auto& P:Region.OuterPolygon)State+=P.ToString();
			for(const auto& Hole:Region.Holes){State+=TEXT("Hole");for(const auto& P:Hole.LocalPolygon)State+=P.ToString();}
		}
		for(const auto& C:Floor->RegionMeshComponents) if(C)
		{
			State+=C->GetPathName()+FString::FromInt(static_cast<int32>(C->GetCollisionEnabled()));
			if(const auto* Mesh=C->GetProcMeshSection(0))
			{
				for(const auto& V:Mesh->ProcVertexBuffer)State+=V.Position.ToString()+V.Normal.ToString();
				for(uint32 I:Mesh->ProcIndexBuffer)State+=TEXT("|")+FString::FromInt(I);
			}
		}
		for(const auto& Id:Floor->SurfaceFinishRelationGuids)State+=Id.ToString();
		for(const auto& Relation:Building->ElementRelations)
		{
			auto SnapshotRelation=Relation;
			if(bIgnoreTransientRevisions){SnapshotRelation.SourceGeometryRevision=0;SnapshotRelation.TargetGeometryRevision=0;}
			FString Json;FJsonObjectConverter::UStructToJsonObjectString(SnapshotRelation,Json);State+=Json;
		}
		return State;
	};
	const FString ValidFinish=CaptureFinish();
	const FString ValidSemanticFinish=CaptureFinish(true);
	TestFalse(TEXT("Empty fill is a rejection"),Floor->SetFloorRegions({},true));
	TestEqual(TEXT("Empty fill retains regions, meshes, room and relations"),CaptureFinish(),ValidFinish);
	TArray<FEHBFloorFinishRegion> Invalid=SplitRegions;
	Invalid[0].OuterPolygon[0].X+=20;
	Invalid[1].OuterPolygon={FVector::ZeroVector,FVector(1,0,0)};
	TestFalse(TEXT("One invalid region rejects the complete result"),Floor->SetFloorRegions(Invalid,true));
	TestEqual(TEXT("Partial success cannot replace an earlier valid region"),CaptureFinish(),ValidFinish);
	Invalid=SplitRegions;Invalid[0].OuterPolygon[0].Z+=2;
	TestFalse(TEXT("Non-planar finish rejected"),Floor->SetFloorRegions(Invalid,true));
	TestEqual(TEXT("Non-planar failure preserves finish"),CaptureFinish(),ValidFinish);
	Invalid=SplitRegions;Invalid[0].Holes.AddDefaulted_GetRef().LocalPolygon={FVector::ZeroVector};
	TestFalse(TEXT("Malformed hole is rejected rather than silently filled"),Floor->SetFloorRegions(Invalid,true));
	TestEqual(TEXT("Malformed hole preserves finish"),CaptureFinish(),ValidFinish);
	// A successful same-component edit must restore both design and generated mesh.
	TArray<FEHBFloorFinishRegion> Changed=SplitRegions;
	for(auto& Region:Changed)for(auto& P:Region.OuterPolygon)P.X+=15;
	{
		FScopedTransaction Transaction(NSLOCTEXT("EHBTests","FloorRegions","Edit floor regions"));
		TestTrue(TEXT("Valid replacement succeeds"),Floor->SetFloorRegions(Changed,false));
	}
	TestNotEqual(TEXT("Valid replacement changes geometry"),CaptureFinish(),ValidFinish);
	const FString ChangedFinish=CaptureFinish(true);
	TestTrue(TEXT("Undo floor region edit"),GEditor->UndoTransaction());
	TestEqual(TEXT("Undo restores design, geometry and relationships"),CaptureFinish(true),ValidSemanticFinish);
	for(const auto& Relation:Building->ElementRelations)
	{
		if(Relation.Source.Kind==EEHBRelationEndpointKind::BuildingElement)
			TestEqual(TEXT("Source revision follows rebuilt cache"),Relation.SourceGeometryRevision,Building->GetElementGeometryRevision(Relation.Source.ElementGuid));
		if(Relation.Target.Kind==EEHBRelationEndpointKind::BuildingElement)
			TestEqual(TEXT("Target revision follows rebuilt cache"),Relation.TargetGeometryRevision,Building->GetElementGeometryRevision(Relation.Target.ElementGuid));
	}
	GEditor->RedoTransaction();
	TestEqual(TEXT("Redo reapplies complete region geometry"),CaptureFinish(true),ChangedFinish);
	GEditor->UndoTransaction();

	{
		FScopedTransaction Transaction(NSLOCTEXT("EHBTests","FloorRegionCount","Change floor region count"));
		TestTrue(TEXT("Reduce valid floor region count"),Floor->SetFloorRegions({SplitRegions[0]},false));
	}
	TestEqual(TEXT("Removed region releases its mesh component"),Floor->RegionMeshComponents.Num(),1);
	TestTrue(TEXT("Undo region component removal"),GEditor->UndoTransaction());
	TestEqual(TEXT("Undo restores removed component and its geometry"),CaptureFinish(true),ValidSemanticFinish);
	GEditor->RedoTransaction();
	TestEqual(TEXT("Redo removes surplus component"),Floor->RegionMeshComponents.Num(),1);
	GEditor->UndoTransaction();

	Floor->RoomLoopGuid.Invalidate();Floor->RoomFloorIndex=INDEX_NONE;
	Floor->SetFloorAssignment(2,EEHBBuildingFloorElementRole::FloorFinish);
	{
		const FString BeforeMissingRoom=CaptureFinish();
		FEasyHouseEditorMode Mode;
		AddExpectedError(TEXT("no unique room was found for the selected floor"),EAutomationExpectedErrorFlags::Contains,1);
		TestFalse(TEXT("Explicit upper floor cannot fill a downstairs room"),Mode.FillFloorRoom(Floor));
		TestEqual(TEXT("Missing upper room preserves floor assignment and geometry"),CaptureFinish(),BeforeMissingRoom);
	}
	for(auto* P:Pillars)P->SetFloorAssignment(2,EEHBBuildingFloorElementRole::FloorBody);
	for(auto* W:Walls)W->SetFloorAssignment(2,EEHBBuildingFloorElementRole::FloorBody);
	Floor->SetFloorAssignment(2,EEHBBuildingFloorElementRole::FloorFinish);
	Building->RebuildClosedLoops();
	const auto UpperRooms=Building->GetClosedLoopsByFloor(2);
	if(TestEqual(TEXT("Upper room fixture"),UpperRooms.Num(),1))
	{
		Floor->RoomLoopGuid=UpperRooms[0].LoopGuid;Floor->RoomFloorIndex=2;
		SupportSlab->SetActorRelativeLocation(FVector(200,200,-10000));
		const FString BeforeFill=CaptureFinish();
		FEasyHouseEditorMode Mode;
		AddExpectedError(TEXT("no structural top surface exists at this room height"),EAutomationExpectedErrorFlags::Contains,1);
		TestFalse(TEXT("Actual editor fill rejects unsupported upper room"),Mode.FillFloorRoom(Floor));
		TestEqual(TEXT("Editor failure preserves prior floor and associations"),CaptureFinish(),BeforeFill);
	}

	DestroyIfValid(Floor);
	DestroyIfValid(SupportSlab);
	for (AEHB_Wall* Wall : Walls)
	{
		DestroyIfValid(Wall);
	}
	for (AEHB_Pillar* Pillar : Pillars)
	{
		DestroyIfValid(Pillar);
	}
	DestroyIfValid(Building);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEHBClearAllElementsTest,
	"EHB.Relationship.ClearAllElements",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEHBClearAllElementsTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("Editor world should be available"), World))
	{
		return false;
	}

	const FVector TestOrigin(19000.0f, 900.0f, 140000.0f);
	AEHB_Building* Building = SpawnTransientActor<AEHB_Building>(World, TestOrigin);
	AEHB_Pillar* StartPillar = SpawnTransientActor<AEHB_Pillar>(World, TestOrigin);
	AEHB_Pillar* EndPillar = SpawnTransientActor<AEHB_Pillar>(World, TestOrigin + FVector(400.0f, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("Building should spawn"), Building)
		|| !TestNotNull(TEXT("Start pillar should spawn"), StartPillar)
		|| !TestNotNull(TEXT("End pillar should spawn"), EndPillar))
	{
		DestroyIfValid(EndPillar);
		DestroyIfValid(StartPillar);
		DestroyIfValid(Building);
		return false;
	}

	Building->EnsureBuildingGuid();
	StartPillar->AttachToBuilding(Building, FTransform(FVector::ZeroVector));
	EndPillar->AttachToBuilding(Building, FTransform(FVector(400.0f, 0.0f, 0.0f)));
	StartPillar->SetFloorAssignment(1, EEHBBuildingFloorElementRole::FloorBody);
	EndPillar->SetFloorAssignment(1, EEHBBuildingFloorElementRole::FloorBody);

	AEHB_Wall* Wall = Building->ConnectPillars(StartPillar, EndPillar, 300.0f, 20.0f);
	if (!TestNotNull(TEXT("Wall should be created before clearing"), Wall))
	{
		DestroyIfValid(EndPillar);
		DestroyIfValid(StartPillar);
		DestroyIfValid(Building);
		return false;
	}

	FEHBElementQuery Query;
	TestTrue(TEXT("Building should contain elements before clearing"), Building->QueryElements(Query).Num() >= 3);
	TestTrue(TEXT("Building should contain relations before clearing"), Building->ElementRelations.Num() > 0);

	const int32 RemovedCount = Building->ClearAllElements();
	TestTrue(TEXT("ClearAllElements should destroy the wall and both pillars"), RemovedCount >= 3);
	TestEqual(TEXT("ClearAllElements should leave no queryable elements"), Building->QueryElements(Query).Num(), 0);
	TestEqual(TEXT("ClearAllElements should remove all element relations"), Building->ElementRelations.Num(), 0);

	TArray<AActor*> AttachedActors;
	Building->GetAttachedActors(AttachedActors, true, true);
	int32 RemainingElementCount = 0;
	for (AActor* AttachedActor : AttachedActors)
	{
		if (Cast<AEHBElementActorBase>(AttachedActor) && !AttachedActor->IsActorBeingDestroyed())
		{
			++RemainingElementCount;
		}
	}
	TestEqual(TEXT("ClearAllElements should not leave attached live element actors"), RemainingElementCount, 0);

	DestroyIfValid(Building);
	return true;
}

#endif
