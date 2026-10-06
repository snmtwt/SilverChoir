// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Actors/EHB_FloorSlab.h"
#include "Actors/EHB_Pillar.h"
#include "Components/EHBArchitecturalSurfaceComponent.h"

#include "Editor.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEHBFloorSlabHoleLoopEditingTest,
	"EHB.FloorSlab.HoleLoopEditing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	AEHB_FloorSlab* SpawnTransientFloorSlabForTest(UWorld* World, const FVector& Location)
	{
		if (!World)
		{
			return nullptr;
		}

		FActorSpawnParameters SpawnParameters;
		SpawnParameters.ObjectFlags |= RF_Transient;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<AEHB_FloorSlab>(
			AEHB_FloorSlab::StaticClass(),
			Location,
			FRotator::ZeroRotator,
			SpawnParameters);
	}
}

bool FEHBFloorSlabHoleLoopEditingTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("Editor world should be available."), World))
	{
		return false;
	}

	AEHB_FloorSlab* Slab = SpawnTransientFloorSlabForTest(World, FVector(0.0f, 0.0f, 180000.0f));
	if (!TestNotNull(TEXT("Floor slab should spawn."), Slab))
	{
		return false;
	}

	Slab->Modify();
	Slab->Thickness = 20.0f;
	Slab->LocalTopPolygon = {
		FVector(0.0f, 0.0f, 0.0f),
		FVector(500.0f, 0.0f, 0.0f),
		FVector(500.0f, 500.0f, 0.0f),
		FVector(0.0f, 500.0f, 0.0f)
	};
	FEHBFloorSlabHole& Hole = Slab->LocalHoles.AddDefaulted_GetRef();
	Hole.LocalPolygon = {
		FVector(150.0f, 150.0f, 0.0f),
		FVector(350.0f, 150.0f, 0.0f),
		FVector(350.0f, 350.0f, 0.0f),
		FVector(150.0f, 350.0f, 0.0f)
	};
	TestTrue(TEXT("Initial slab with a hole should rebuild."), Slab->RebuildSlabMesh());

	TestTrue(TEXT("Hole edge should accept a midpoint corner."), Slab->InsertCornerOnEdge(0, 0, 1));
	TestEqual(TEXT("Inserted hole corner should add one hole point."), Slab->LocalHoles[0].LocalPolygon.Num(), 5);

	const FVector NewCornerWorld = Slab->GetActorTransform().TransformPosition(FVector(260.0f, 130.0f, 0.0f));
	TestTrue(TEXT("Hole corner should move in world space."), Slab->UpdateCornerWorldLocation(0, 1, NewCornerWorld));
	TestTrue(
		TEXT("Moved hole corner should update local XY."),
		Slab->LocalHoles[0].LocalPolygon[1].Equals(FVector(260.0f, 130.0f, 0.0f), 0.01f));

	const FVector EdgePointBeforeOffset = Slab->LocalHoles[0].LocalPolygon[0];
	TestTrue(TEXT("Hole edge should offset in world space."), Slab->OffsetEdgeWorldLocation(0, 0, 1, FVector(10.0f, 20.0f, 0.0f)));
	TestTrue(
		TEXT("Offset hole edge should move its first endpoint."),
		Slab->LocalHoles[0].LocalPolygon[0].Equals(EdgePointBeforeOffset + FVector(10.0f, 20.0f, 0.0f), 0.01f));

	TestTrue(TEXT("Removing hole corner above triangle count should keep the hole."), Slab->RemoveCorner(0, 1));
	TestEqual(TEXT("Hole should still exist after removing from five to four points."), Slab->LocalHoles.Num(), 1);
	TestEqual(TEXT("Hole should now have four points."), Slab->LocalHoles[0].LocalPolygon.Num(), 4);

	TestTrue(TEXT("Removing hole corner from four points should keep a triangular hole."), Slab->RemoveCorner(0, 0));
	TestEqual(TEXT("Hole should still exist after becoming triangular."), Slab->LocalHoles.Num(), 1);
	TestEqual(TEXT("Hole should now have three points."), Slab->LocalHoles[0].LocalPolygon.Num(), 3);

	TestTrue(TEXT("Removing a corner from a triangular hole should remove the hole."), Slab->RemoveCorner(0, 0));
	TestEqual(TEXT("The triangular hole should be removed."), Slab->LocalHoles.Num(), 0);

	World->EditorDestroyActor(Slab, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEHBFloorSlabCutOperationLoopEditingTest,
	"EHB.FloorSlab.CutOperationLoopEditing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEHBFloorSlabCutOperationLoopEditingTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("Editor world should be available."), World))
	{
		return false;
	}

	AEHB_FloorSlab* Slab = SpawnTransientFloorSlabForTest(World, FVector(0.0f, 0.0f, 180500.0f));
	if (!TestNotNull(TEXT("Floor slab should spawn."), Slab))
	{
		return false;
	}

	Slab->Thickness = 20.0f;
	Slab->LocalTopPolygon = {
		FVector(0.0f, 0.0f, 0.0f),
		FVector(500.0f, 0.0f, 0.0f),
		FVector(500.0f, 500.0f, 0.0f),
		FVector(0.0f, 500.0f, 0.0f)
	};

	FEHBCutOperation Operation;
	Operation.OperationType = EEHBCutOperationType::Subtract;
	Operation.Stage = EEHBCutStage::Profile;
	Operation.ProjectionMode = EEHBCutProjectionMode::HorizontalXY;
	Operation.TransformPolicy = EEHBCutTransformPolicy::TargetLocal;
	Operation.Source.SourceType = EEHBCutSourceType::ExplicitPrism;
	Operation.Source.PrimitiveShape = EEHBCutPrimitiveShape::Square;
	Operation.Source.LocalTransform = FTransform(FVector(250.0f, 250.0f, 0.0f));
	Operation.Source.Size = 160.0f;
	Operation.Source.Height = 80.0f;
	TestTrue(TEXT("Cut operation should rebuild the slab."), Slab->AddCutOperation(Operation));
	TestEqual(TEXT("The slab should store one cut operation."), Slab->CutOperations.Num(), 1);

	const int32 CutLoopIndex = AEHB_FloorSlab::MakeCutOperationLoopIndex(0);
	TArray<FVector> CutLoop;
	TestTrue(TEXT("Committed cut operation should expose an editable loop."), Slab->GetEditableLoopCopy(CutLoopIndex, CutLoop));
	TestEqual(TEXT("Square cut operation should expose four corners."), CutLoop.Num(), 4);

	TestTrue(TEXT("Cut operation edge should accept a midpoint corner."), Slab->InsertCornerOnEdge(CutLoopIndex, 0, 1));
	TestEqual(TEXT("Editing should bake the cutter into an explicit polygon."), Slab->CutOperations[0].Source.SourceType, EEHBCutSourceType::ExplicitPolygon);
	TestEqual(TEXT("Inserted cut operation corner should add one polygon point."), Slab->CutOperations[0].Source.ExplicitPolygon.Points.Num(), 5);

	const FVector NewCornerWorld = Slab->GetActorTransform().TransformPosition(FVector(185.0f, 245.0f, 0.0f));
	TestTrue(TEXT("Cut operation corner should move in world space."), Slab->UpdateCornerWorldLocation(CutLoopIndex, 1, NewCornerWorld));
	TestTrue(
		TEXT("Moved cut operation corner should update local XY."),
		Slab->CutOperations[0].Source.ExplicitPolygon.Points[1].LocalPosition.Equals(FVector(185.0f, 245.0f, 0.0f), 0.01f));

	const FVector EdgePointBeforeOffset = Slab->CutOperations[0].Source.ExplicitPolygon.Points[0].LocalPosition;
	TestTrue(TEXT("Cut operation edge should offset in world space."), Slab->OffsetEdgeWorldLocation(CutLoopIndex, 0, 1, FVector(10.0f, 20.0f, 0.0f)));
	TestTrue(
		TEXT("Offset cut operation edge should move its first endpoint."),
		Slab->CutOperations[0].Source.ExplicitPolygon.Points[0].LocalPosition.Equals(EdgePointBeforeOffset + FVector(10.0f, 20.0f, 0.0f), 0.01f));

	TestTrue(TEXT("Removing cut operation corner above triangle count should keep the operation."), Slab->RemoveCorner(CutLoopIndex, 1));
	TestEqual(TEXT("Cut operation should still exist after removing from five to four points."), Slab->CutOperations.Num(), 1);
	TestEqual(TEXT("Cut operation polygon should now have four points."), Slab->CutOperations[0].Source.ExplicitPolygon.Points.Num(), 4);

	TestTrue(TEXT("Removing cut operation corner from four points should keep a triangular cut."), Slab->RemoveCorner(CutLoopIndex, 0));
	TestEqual(TEXT("Cut operation should still exist after becoming triangular."), Slab->CutOperations.Num(), 1);
	TestEqual(TEXT("Cut operation polygon should now have three points."), Slab->CutOperations[0].Source.ExplicitPolygon.Points.Num(), 3);

	TestTrue(TEXT("Removing a corner from a triangular cut should remove the cut operation."), Slab->RemoveCorner(CutLoopIndex, 0));
	TestEqual(TEXT("The triangular cut operation should be removed."), Slab->CutOperations.Num(), 0);

	World->EditorDestroyActor(Slab, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEHBFloorSlabEffectiveOuterPolygonTest,
	"EHB.FloorSlab.EffectiveOuterPolygon",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEHBFloorSlabEffectiveOuterPolygonTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("Editor world should be available."), World))
	{
		return false;
	}

	AEHB_FloorSlab* Slab = SpawnTransientFloorSlabForTest(World, FVector(0.0f, 0.0f, 181000.0f));
	if (!TestNotNull(TEXT("Floor slab should spawn."), Slab))
	{
		return false;
	}

	Slab->Thickness = 20.0f;
	Slab->VisualExpansion = 20.0f;
	Slab->LocalTopPolygon = {
		FVector(0.0f, 0.0f, 0.0f),
		FVector(200.0f, 0.0f, 0.0f),
		FVector(200.0f, 200.0f, 0.0f),
		FVector(0.0f, 200.0f, 0.0f)
	};

	TArray<FVector> EffectivePolygon;
	TestTrue(TEXT("Effective polygon should be generated."), Slab->BuildEffectiveOuterPolygon(EffectivePolygon, true, false));
	float MinX = TNumericLimits<float>::Max();
	float MaxX = -TNumericLimits<float>::Max();
	for (const FVector& Point : EffectivePolygon)
	{
		MinX = FMath::Min(MinX, Point.X);
		MaxX = FMath::Max(MaxX, Point.X);
	}

	TestTrue(TEXT("Effective polygon should include the negative expansion."), FMath::IsNearlyEqual(MinX, -20.0f, 0.25f));
	TestTrue(TEXT("Effective polygon should include the positive expansion."), FMath::IsNearlyEqual(MaxX, 220.0f, 0.25f));

	World->EditorDestroyActor(Slab, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEHBFloorSlabSideEdgeSnapTest,
	"EHB.FloorSlab.SideEdgeSnap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEHBFloorSlabSideEdgeSnapTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("Editor world should be available."), World))
	{
		return false;
	}

	AEHB_FloorSlab* TargetSlab = SpawnTransientFloorSlabForTest(World, FVector(0.0f, 0.0f, 182000.0f));
	AEHB_FloorSlab* MovingSlab = SpawnTransientFloorSlabForTest(World, FVector(210.0f, 0.0f, 182000.0f));
	if (!TestNotNull(TEXT("Target slab should spawn."), TargetSlab)
		|| !TestNotNull(TEXT("Moving slab should spawn."), MovingSlab))
	{
		if (TargetSlab)
		{
			World->EditorDestroyActor(TargetSlab, true);
		}
		if (MovingSlab)
		{
			World->EditorDestroyActor(MovingSlab, true);
		}
		return false;
	}

	const TArray<FVector> SquarePolygon = {
		FVector(0.0f, 0.0f, 0.0f),
		FVector(200.0f, 0.0f, 0.0f),
		FVector(200.0f, 200.0f, 0.0f),
		FVector(0.0f, 200.0f, 0.0f)
	};

	TargetSlab->Thickness = 20.0f;
	TargetSlab->VisualExpansion = 20.0f;
	TargetSlab->LocalTopPolygon = SquarePolygon;
	MovingSlab->Thickness = 20.0f;
	MovingSlab->LocalTopPolygon = SquarePolygon;
	TestTrue(TEXT("Target slab should rebuild."), TargetSlab->RebuildSlabMesh());
	TestTrue(TEXT("Moving slab should rebuild."), MovingSlab->RebuildSlabMesh());

	TestTrue(TEXT("Moving slab should snap to the target slab's expanded side edge."), MovingSlab->SnapSideToAdjacentFloorSlab(15.0f));
	TestTrue(TEXT("Moving slab left edge should align to target expanded right edge."), FMath::IsNearlyEqual(MovingSlab->GetActorLocation().X, 220.0f, 0.25f));

	World->EditorDestroyActor(MovingSlab, true);
	World->EditorDestroyActor(TargetSlab, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEHBFloorSlabCornerAlignedSnapTest,
	"EHB.FloorSlab.CornerAlignedSnap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEHBFloorSlabCornerAlignedSnapTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("Editor world should be available."), World))
	{
		return false;
	}

	AEHB_FloorSlab* TargetSlab = SpawnTransientFloorSlabForTest(World, FVector(0.0f, 0.0f, 183000.0f));
	AEHB_FloorSlab* MovingSlab = SpawnTransientFloorSlabForTest(World, FVector(208.0f, 5.0f, 183000.0f));
	if (!TestNotNull(TEXT("Target slab should spawn."), TargetSlab)
		|| !TestNotNull(TEXT("Moving slab should spawn."), MovingSlab))
	{
		if (TargetSlab)
		{
			World->EditorDestroyActor(TargetSlab, true);
		}
		if (MovingSlab)
		{
			World->EditorDestroyActor(MovingSlab, true);
		}
		return false;
	}

	const TArray<FVector> SquarePolygon = {
		FVector(0.0f, 0.0f, 0.0f),
		FVector(200.0f, 0.0f, 0.0f),
		FVector(200.0f, 200.0f, 0.0f),
		FVector(0.0f, 200.0f, 0.0f)
	};

	TargetSlab->Thickness = 20.0f;
	TargetSlab->LocalTopPolygon = SquarePolygon;
	MovingSlab->Thickness = 20.0f;
	MovingSlab->LocalTopPolygon = SquarePolygon;
	MovingSlab->SetActorRotation(FRotator(0.0f, 5.0f, 0.0f));
	TestTrue(TEXT("Target slab should rebuild."), TargetSlab->RebuildSlabMesh());
	TestTrue(TEXT("Moving slab should rebuild."), MovingSlab->RebuildSlabMesh());

	TestTrue(TEXT("Moving slab should rotate and snap when a side corner is close."), MovingSlab->SnapSideToAdjacentFloorSlab(15.0f));
	TestTrue(TEXT("Moving slab corner should align to target corner X."), FMath::IsNearlyEqual(MovingSlab->GetActorLocation().X, 200.0f, 0.5f));
	TestTrue(TEXT("Moving slab corner should align to target corner Y."), FMath::IsNearlyEqual(MovingSlab->GetActorLocation().Y, 0.0f, 0.5f));
	TestTrue(
		TEXT("Moving slab yaw should align to the target side."),
		FMath::Abs(FRotator::NormalizeAxis(MovingSlab->GetActorRotation().Yaw)) <= 0.25f);

	World->EditorDestroyActor(MovingSlab, true);
	World->EditorDestroyActor(TargetSlab, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEHBFloorSlabAdjacentSnapToggleTest,
	"EHB.FloorSlab.AdjacentSnapToggle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEHBFloorSlabAdjacentSnapToggleTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("Editor world should be available."), World))
	{
		return false;
	}

	AEHB_FloorSlab* TargetSlab = SpawnTransientFloorSlabForTest(World, FVector(0.0f, 0.0f, 184000.0f));
	AEHB_FloorSlab* MovingSlab = SpawnTransientFloorSlabForTest(World, FVector(210.0f, 0.0f, 184000.0f));
	if (!TestNotNull(TEXT("Target slab should spawn."), TargetSlab)
		|| !TestNotNull(TEXT("Moving slab should spawn."), MovingSlab))
	{
		if (TargetSlab)
		{
			World->EditorDestroyActor(TargetSlab, true);
		}
		if (MovingSlab)
		{
			World->EditorDestroyActor(MovingSlab, true);
		}
		return false;
	}

	const TArray<FVector> SquarePolygon = {
		FVector(0.0f, 0.0f, 0.0f),
		FVector(200.0f, 0.0f, 0.0f),
		FVector(200.0f, 200.0f, 0.0f),
		FVector(0.0f, 200.0f, 0.0f)
	};

	TargetSlab->Thickness = 20.0f;
	TargetSlab->LocalTopPolygon = SquarePolygon;
	MovingSlab->Thickness = 20.0f;
	MovingSlab->LocalTopPolygon = SquarePolygon;
	MovingSlab->bEnableAdjacentSlabSnap = false;
	TestTrue(TEXT("Target slab should rebuild."), TargetSlab->RebuildSlabMesh());
	TestTrue(TEXT("Moving slab should rebuild."), MovingSlab->RebuildSlabMesh());

	TestFalse(TEXT("Moving slab should not snap when adjacent snap is disabled."), MovingSlab->SnapSideToAdjacentFloorSlab(15.0f));
	TestTrue(TEXT("Moving slab location should remain unchanged when snap is disabled."), FMath::IsNearlyEqual(MovingSlab->GetActorLocation().X, 210.0f, 0.25f));

	World->EditorDestroyActor(MovingSlab, true);
	World->EditorDestroyActor(TargetSlab, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEHBFloorSlabCornerHandleSnapTest,
	"EHB.FloorSlab.CornerHandleSnap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEHBFloorSlabCornerHandleSnapTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("Editor world should be available."), World))
	{
		return false;
	}

	AEHB_FloorSlab* TargetSlab = SpawnTransientFloorSlabForTest(World, FVector(0.0f, 0.0f, 185000.0f));
	AEHB_FloorSlab* EditingSlab = SpawnTransientFloorSlabForTest(World, FVector(350.0f, 0.0f, 185000.0f));
	if (!TestNotNull(TEXT("Target slab should spawn."), TargetSlab)
		|| !TestNotNull(TEXT("Editing slab should spawn."), EditingSlab))
	{
		if (TargetSlab)
		{
			World->EditorDestroyActor(TargetSlab, true);
		}
		if (EditingSlab)
		{
			World->EditorDestroyActor(EditingSlab, true);
		}
		return false;
	}

	const TArray<FVector> SquarePolygon = {
		FVector(0.0f, 0.0f, 0.0f),
		FVector(200.0f, 0.0f, 0.0f),
		FVector(200.0f, 200.0f, 0.0f),
		FVector(0.0f, 200.0f, 0.0f)
	};

	TargetSlab->Thickness = 20.0f;
	TargetSlab->LocalTopPolygon = SquarePolygon;
	EditingSlab->Thickness = 20.0f;
	EditingSlab->LocalTopPolygon = SquarePolygon;
	TestTrue(TEXT("Target slab should rebuild."), TargetSlab->RebuildSlabMesh());
	TestTrue(TEXT("Editing slab should rebuild."), EditingSlab->RebuildSlabMesh());

	FVector CandidateWorldLocation(203.0f, 4.0f, EditingSlab->GetActorLocation().Z + 8.0f);
	TestTrue(
		TEXT("Corner handle candidate should snap to an adjacent slab corner."),
		EditingSlab->SnapOuterCornerHandleWorldLocation(0, CandidateWorldLocation));
	TestTrue(TEXT("Corner handle candidate X should align."), FMath::IsNearlyEqual(CandidateWorldLocation.X, 200.0f, 0.25f));
	TestTrue(TEXT("Corner handle candidate Y should align."), FMath::IsNearlyEqual(CandidateWorldLocation.Y, 0.0f, 0.25f));

	World->EditorDestroyActor(EditingSlab, true);
	World->EditorDestroyActor(TargetSlab, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEHBFloorSlabCornerAdjacentAxisSnapTest,
	"EHB.FloorSlab.CornerAdjacentAxisSnap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEHBFloorSlabCornerAdjacentAxisSnapTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("Editor world should be available."), World))
	{
		return false;
	}

	const FVector SlabLocation(500.0f, 700.0f, 185500.0f);
	AEHB_FloorSlab* Slab = SpawnTransientFloorSlabForTest(World, SlabLocation);
	if (!TestNotNull(TEXT("Floor slab should spawn."), Slab))
	{
		return false;
	}

	Slab->LocalTopPolygon = {
		FVector(-100.0f, -100.0f, 0.0f),
		FVector(-100.0f, 100.0f, 0.0f),
		FVector(100.0f, 100.0f, 0.0f),
		FVector(100.0f, -100.0f, 0.0f)
	};

	FVector CandidateWorld = SlabLocation + FVector(-86.0f, -82.0f, 8.0f);
	TestTrue(
		TEXT("Corner should snap independently to the neighboring points' X and Y axes."),
		Slab->SnapCornerToAdjacentAxesWorldLocation(INDEX_NONE, 0, CandidateWorld, 20.0f));
	TestTrue(TEXT("Corner X should align with the next point."), FMath::IsNearlyEqual(CandidateWorld.X, 400.0f, 0.01f));
	TestTrue(TEXT("Corner Y should align with the previous point."), FMath::IsNearlyEqual(CandidateWorld.Y, 600.0f, 0.01f));
	TestTrue(TEXT("Corner snapping should preserve world Z."), FMath::IsNearlyEqual(CandidateWorld.Z, SlabLocation.Z + 8.0f, 0.01f));

	TestTrue(
		TEXT("Corner distance to the previous point should be editable."),
		Slab->UpdateCornerDistanceToAdjacentPoint(INDEX_NONE, 0, true, 250.0f));
	TestTrue(
		TEXT("Edited corner should have the requested previous-point distance."),
		FMath::IsNearlyEqual(
			FVector::Dist2D(Slab->LocalTopPolygon[0], Slab->LocalTopPolygon[3]),
			250.0f,
			0.01f));

	World->EditorDestroyActor(Slab, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEHBFloorSlabEdgeHandleSnapTest,
	"EHB.FloorSlab.EdgeHandleSnap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEHBFloorSlabEdgeHandleSnapTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("Editor world should be available."), World))
	{
		return false;
	}

	AEHB_FloorSlab* TargetSlab = SpawnTransientFloorSlabForTest(World, FVector(0.0f, 0.0f, 186000.0f));
	AEHB_FloorSlab* EditingSlab = SpawnTransientFloorSlabForTest(World, FVector(230.0f, 0.0f, 186000.0f));
	if (!TestNotNull(TEXT("Target slab should spawn."), TargetSlab)
		|| !TestNotNull(TEXT("Editing slab should spawn."), EditingSlab))
	{
		if (TargetSlab)
		{
			World->EditorDestroyActor(TargetSlab, true);
		}
		if (EditingSlab)
		{
			World->EditorDestroyActor(EditingSlab, true);
		}
		return false;
	}

	const TArray<FVector> SquarePolygon = {
		FVector(0.0f, 0.0f, 0.0f),
		FVector(200.0f, 0.0f, 0.0f),
		FVector(200.0f, 200.0f, 0.0f),
		FVector(0.0f, 200.0f, 0.0f)
	};

	TargetSlab->Thickness = 20.0f;
	TargetSlab->LocalTopPolygon = SquarePolygon;
	EditingSlab->Thickness = 20.0f;
	EditingSlab->LocalTopPolygon = SquarePolygon;
	TestTrue(TEXT("Target slab should rebuild."), TargetSlab->RebuildSlabMesh());
	TestTrue(TEXT("Editing slab should rebuild."), EditingSlab->RebuildSlabMesh());

	FVector CandidateWorldDelta(-25.0f, 0.0f, 0.0f);
	TestTrue(
		TEXT("Edge handle delta should snap to an adjacent slab edge."),
		EditingSlab->SnapOuterEdgeHandleWorldDelta(3, 0, CandidateWorldDelta));
	TestTrue(TEXT("Edge handle delta should align X to the target side."), FMath::IsNearlyEqual(CandidateWorldDelta.X, -30.0f, 0.25f));
	TestTrue(TEXT("Edge handle delta should keep Y unchanged."), FMath::IsNearlyEqual(CandidateWorldDelta.Y, 0.0f, 0.25f));

	World->EditorDestroyActor(EditingSlab, true);
	World->EditorDestroyActor(TargetSlab, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEHBFloorSlabEdgeHandleCornerSnapTest,
	"EHB.FloorSlab.EdgeHandleCornerSnap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEHBFloorSlabEdgeHandleCornerSnapTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("Editor world should be available."), World))
	{
		return false;
	}

	AEHB_FloorSlab* TargetSlab = SpawnTransientFloorSlabForTest(World, FVector(0.0f, 0.0f, 186500.0f));
	AEHB_FloorSlab* EditingSlab = SpawnTransientFloorSlabForTest(World, FVector(230.0f, 8.0f, 186500.0f));
	if (!TestNotNull(TEXT("Target slab should spawn."), TargetSlab)
		|| !TestNotNull(TEXT("Editing slab should spawn."), EditingSlab))
	{
		if (TargetSlab)
		{
			World->EditorDestroyActor(TargetSlab, true);
		}
		if (EditingSlab)
		{
			World->EditorDestroyActor(EditingSlab, true);
		}
		return false;
	}

	const TArray<FVector> SquarePolygon = {
		FVector(0.0f, 0.0f, 0.0f),
		FVector(200.0f, 0.0f, 0.0f),
		FVector(200.0f, 200.0f, 0.0f),
		FVector(0.0f, 200.0f, 0.0f)
	};

	TargetSlab->Thickness = 20.0f;
	TargetSlab->LocalTopPolygon = SquarePolygon;
	EditingSlab->Thickness = 20.0f;
	EditingSlab->LocalTopPolygon = SquarePolygon;
	TestTrue(TEXT("Target slab should rebuild."), TargetSlab->RebuildSlabMesh());
	TestTrue(TEXT("Editing slab should rebuild."), EditingSlab->RebuildSlabMesh());

	FVector CandidateWorldDelta(-25.0f, 0.0f, 0.0f);
	TestTrue(
		TEXT("Edge handle delta should snap both edge and endpoint to an adjacent slab corner."),
		EditingSlab->SnapOuterEdgeHandleWorldDelta(3, 0, CandidateWorldDelta));
	TestTrue(TEXT("Edge handle delta should align X to the target side."), FMath::IsNearlyEqual(CandidateWorldDelta.X, -30.0f, 0.25f));
	TestTrue(TEXT("Edge handle delta should align Y to the target corners."), FMath::IsNearlyEqual(CandidateWorldDelta.Y, -8.0f, 0.25f));

	World->EditorDestroyActor(EditingSlab, true);
	World->EditorDestroyActor(TargetSlab, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEHBFloorSlabEdgeHandlePillarSideSnapTest,
	"EHB.FloorSlab.EdgeHandlePillarSideSnap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEHBFloorSlabEdgeHandlePillarSideSnapTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("Editor world should be available."), World))
	{
		return false;
	}

	AEHB_Pillar* Pillar = World
		? World->SpawnActor<AEHB_Pillar>(AEHB_Pillar::StaticClass(), FVector(0.0f, 0.0f, 186800.0f), FRotator::ZeroRotator)
		: nullptr;
	AEHB_FloorSlab* EditingSlab = SpawnTransientFloorSlabForTest(World, FVector(48.0f, -50.0f, 186800.0f));
	if (!TestNotNull(TEXT("Pillar should spawn."), Pillar)
		|| !TestNotNull(TEXT("Editing slab should spawn."), EditingSlab))
	{
		if (Pillar)
		{
			World->EditorDestroyActor(Pillar, true);
		}
		if (EditingSlab)
		{
			World->EditorDestroyActor(EditingSlab, true);
		}
		return false;
	}

	Pillar->ConfigureAsPolygonPillar(
		300.0f,
		40.0f,
		40.0f,
		FTransform(FVector(0.0f, 0.0f, 186800.0f)),
		true);
	EditingSlab->Thickness = 20.0f;
	EditingSlab->LocalTopPolygon = {
		FVector(0.0f, 0.0f, 0.0f),
		FVector(100.0f, 0.0f, 0.0f),
		FVector(100.0f, 100.0f, 0.0f),
		FVector(0.0f, 100.0f, 0.0f)
	};
	TestTrue(TEXT("Pillar should rebuild."), Pillar->PillarMeshComponent != nullptr);
	TestTrue(TEXT("Editing slab should rebuild."), EditingSlab->RebuildSlabMesh());

	FVector CandidateWorldDelta(-23.0f, 0.0f, 0.0f);
	TestTrue(
		TEXT("Edge handle delta should snap to a pillar side exported by its side surface component."),
		EditingSlab->SnapOuterEdgeHandleWorldDelta(3, 0, CandidateWorldDelta));
	TestTrue(TEXT("Edge handle delta should align to the pillar side X."), FMath::IsNearlyEqual(CandidateWorldDelta.X, -28.0f, 0.25f));
	TestTrue(TEXT("Edge handle delta should also align a close endpoint on the pillar side."), FMath::IsNearlyEqual(CandidateWorldDelta.Y, -30.0f, 0.25f));

	World->EditorDestroyActor(EditingSlab, true);
	World->EditorDestroyActor(Pillar, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEHBFloorSlabPillarCornerInsetSnapTest,
	"EHB.FloorSlab.PillarCornerInsetSnap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEHBFloorSlabPillarCornerInsetSnapTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("Editor world should be available."), World))
	{
		return false;
	}

	AEHB_FloorSlab* Slab = SpawnTransientFloorSlabForTest(World, FVector(0.0f, 0.0f, 187000.0f));
	AEHB_Pillar* Pillar = World
		? World->SpawnActor<AEHB_Pillar>(AEHB_Pillar::StaticClass(), FVector(-5.0f, -5.0f, 187000.0f), FRotator::ZeroRotator)
		: nullptr;
	if (!TestNotNull(TEXT("Slab should spawn."), Slab) || !TestNotNull(TEXT("Pillar should spawn."), Pillar))
	{
		if (Pillar)
		{
			World->EditorDestroyActor(Pillar, true);
		}
		if (Slab)
		{
			World->EditorDestroyActor(Slab, true);
		}
		return false;
	}

	Slab->Thickness = 20.0f;
	Slab->VisualExpansion = 20.0f;
	Slab->LocalTopPolygon = {
		FVector(0.0f, 0.0f, 0.0f),
		FVector(200.0f, 0.0f, 0.0f),
		FVector(200.0f, 200.0f, 0.0f),
		FVector(0.0f, 200.0f, 0.0f)
	};
	TestTrue(TEXT("Slab should rebuild."), Slab->RebuildSlabMesh());

	Pillar->ConfigureAsPolygonPillar(300.0f, 40.0f, 40.0f, FTransform(FVector(-5.0f, -5.0f, 187000.0f)), true);
	const FVector SnappedPillarLocation = Pillar->GetActorLocation();
	const float SlabTopWorldZ = Slab->GetActorTransform().TransformPosition(FVector(0.0f, 0.0f, Slab->GetTopZ())).Z;
	TestTrue(
		*FString::Printf(TEXT("Pillar should snap inward from the expanded slab corner. Actual=%s SlabTopZ=%.2f"), *SnappedPillarLocation.ToString(), SlabTopWorldZ),
		FVector::Dist2D(SnappedPillarLocation, FVector(0.0f, 0.0f, 187000.0f)) <= 0.25f);
	TestTrue(
		*FString::Printf(TEXT("Pillar side should align with the expanded slab side X. Actual=%s"), *SnappedPillarLocation.ToString()),
		FMath::IsNearlyEqual(SnappedPillarLocation.X - 20.0f, -20.0f, 0.25f));
	TestTrue(
		*FString::Printf(TEXT("Pillar side should align with the expanded slab side Y. Actual=%s"), *SnappedPillarLocation.ToString()),
		FMath::IsNearlyEqual(SnappedPillarLocation.Y - 20.0f, -20.0f, 0.25f));

	World->EditorDestroyActor(Pillar, true);
	World->EditorDestroyActor(Slab, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEHBFloorSlabPillarEdgeInsetSnapTest,
	"EHB.FloorSlab.PillarEdgeInsetSnap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEHBFloorSlabPillarEdgeInsetSnapTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("Editor world should be available."), World))
	{
		return false;
	}

	AEHB_FloorSlab* Slab = SpawnTransientFloorSlabForTest(World, FVector(0.0f, 0.0f, 187500.0f));
	AEHB_Pillar* Pillar = World
		? World->SpawnActor<AEHB_Pillar>(AEHB_Pillar::StaticClass(), FVector(100.0f, 120.0f, 187500.0f), FRotator::ZeroRotator)
		: nullptr;
	if (!TestNotNull(TEXT("Slab should spawn."), Slab) || !TestNotNull(TEXT("Pillar should spawn."), Pillar))
	{
		if (Pillar)
		{
			World->EditorDestroyActor(Pillar, true);
		}
		if (Slab)
		{
			World->EditorDestroyActor(Slab, true);
		}
		return false;
	}

	Slab->Thickness = 20.0f;
	Slab->VisualExpansion = 20.0f;
	Slab->LocalTopPolygon = {
		FVector(0.0f, 0.0f, 0.0f),
		FVector(200.0f, 0.0f, 0.0f),
		FVector(200.0f, 200.0f, 0.0f),
		FVector(0.0f, 200.0f, 0.0f)
	};
	TestTrue(TEXT("Slab should rebuild."), Slab->RebuildSlabMesh());

	Pillar->ConfigureAsPolygonPillar(300.0f, 40.0f, 40.0f, FTransform(FVector(100.0f, 120.0f, 187500.0f)), true);
	Pillar->SetElementLocalTransform(FTransform(FVector(100.0f, 5.0f, 187500.0f)), false);

	TestTrue(TEXT("Pillar should live snap to the expanded slab edge."), FMath::IsNearlyEqual(Pillar->GetActorLocation().Y, 0.0f, 0.25f));
	TestTrue(TEXT("Pillar edge should align with the expanded slab edge."), FMath::IsNearlyEqual(Pillar->GetActorLocation().Y - 20.0f, -20.0f, 0.25f));
	TestTrue(TEXT("Pillar should preserve its edge projection."), FMath::IsNearlyEqual(Pillar->GetActorLocation().X, 100.0f, 0.25f));

	World->EditorDestroyActor(Pillar, true);
	World->EditorDestroyActor(Slab, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEHBFloorSlabPillarBoundarySnapIgnoresDifferentHeightTest,
	"EHB.FloorSlab.PillarBoundarySnapIgnoresDifferentHeight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEHBFloorSlabPillarBoundarySnapIgnoresDifferentHeightTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("Editor world should be available."), World))
	{
		return false;
	}

	const FVector LowerOrigin(91000.0f, 76000.0f, 188200.0f);
	AEHB_FloorSlab* UpperSlab = SpawnTransientFloorSlabForTest(World, LowerOrigin + FVector(0.0f, 0.0f, 300.0f));
	AEHB_Pillar* LowerPillar = World
		? World->SpawnActor<AEHB_Pillar>(AEHB_Pillar::StaticClass(), LowerOrigin + FVector(5.0f, 5.0f, 0.0f), FRotator::ZeroRotator)
		: nullptr;
	if (!TestNotNull(TEXT("Upper slab should spawn."), UpperSlab) || !TestNotNull(TEXT("Lower pillar should spawn."), LowerPillar))
	{
		if (LowerPillar)
		{
			World->EditorDestroyActor(LowerPillar, true);
		}
		if (UpperSlab)
		{
			World->EditorDestroyActor(UpperSlab, true);
		}
		return false;
	}

	UpperSlab->Thickness = 20.0f;
	UpperSlab->VisualExpansion = 20.0f;
	UpperSlab->LocalTopPolygon = {
		FVector(0.0f, 0.0f, 0.0f),
		FVector(200.0f, 0.0f, 0.0f),
		FVector(200.0f, 200.0f, 0.0f),
		FVector(0.0f, 200.0f, 0.0f)
	};
	TestTrue(TEXT("Upper slab should rebuild."), UpperSlab->RebuildSlabMesh());

	const FVector DesiredPillarLocation = LowerOrigin + FVector(5.0f, 5.0f, 0.0f);
	LowerPillar->ConfigureAsPolygonPillar(300.0f, 40.0f, 40.0f, FTransform(DesiredPillarLocation), true);

	TestTrue(
		TEXT("Lower pillar should ignore the upper slab boundary snap."),
		FVector::Dist2D(LowerPillar->GetActorLocation(), DesiredPillarLocation) <= 0.25f);
	TestTrue(
		TEXT("Lower pillar should preserve its floor height while ignoring upper slab snap."),
		FMath::IsNearlyEqual(LowerPillar->GetActorLocation().Z, DesiredPillarLocation.Z, 0.25f));

	World->EditorDestroyActor(LowerPillar, true);
	World->EditorDestroyActor(UpperSlab, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEHBFloorSlabHoleSideSnapEdgesTest,
	"EHB.FloorSlab.HoleSideSnapEdges",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEHBFloorSlabHoleSideSnapEdgesTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("Editor world should be available."), World))
	{
		return false;
	}

	AEHB_FloorSlab* Slab = SpawnTransientFloorSlabForTest(World, FVector(0.0f, 0.0f, 188000.0f));
	if (!TestNotNull(TEXT("Slab should spawn."), Slab))
	{
		return false;
	}

	Slab->Thickness = 20.0f;
	Slab->LocalTopPolygon = {
		FVector(0.0f, 0.0f, 0.0f),
		FVector(400.0f, 0.0f, 0.0f),
		FVector(400.0f, 400.0f, 0.0f),
		FVector(0.0f, 400.0f, 0.0f)
	};
	FEHBFloorSlabHole& Hole = Slab->LocalHoles.AddDefaulted_GetRef();
	Hole.LocalPolygon = {
		FVector(100.0f, 100.0f, 0.0f),
		FVector(200.0f, 100.0f, 0.0f),
		FVector(200.0f, 200.0f, 0.0f),
		FVector(100.0f, 200.0f, 0.0f)
	};
	TestTrue(TEXT("Slab should rebuild."), Slab->RebuildSlabMesh());

	TArray<UEHBArchitecturalSurfaceComponent*> SurfaceComponents;
	Slab->GetComponents(SurfaceComponents);
	TArray<FEHBSurfaceSideSnapEdge> SideEdges;
	for (UEHBArchitecturalSurfaceComponent* SurfaceComponent : SurfaceComponents)
	{
		if (SurfaceComponent)
		{
			SurfaceComponent->BuildSideSnapEdges(SideEdges);
		}
	}

	TestTrue(TEXT("Slab side snap edges should include outer and hole loops."), SideEdges.Num() >= 8);

	bool bFoundHoleBottomEdge = false;
	for (const FEHBSurfaceSideSnapEdge& Edge : SideEdges)
	{
		const bool bMatchesHoleBottom =
			FMath::IsNearlyEqual(Edge.WorldStart.Y, 100.0f, 0.25f)
			&& FMath::IsNearlyEqual(Edge.WorldEnd.Y, 100.0f, 0.25f)
			&& FMath::IsNearlyEqual(FMath::Min(Edge.WorldStart.X, Edge.WorldEnd.X), 100.0f, 0.25f)
			&& FMath::IsNearlyEqual(FMath::Max(Edge.WorldStart.X, Edge.WorldEnd.X), 200.0f, 0.25f);
		if (bMatchesHoleBottom)
		{
			bFoundHoleBottomEdge = true;
			TestTrue(TEXT("Hole side snap normal should face into the hole."), FVector::DotProduct(Edge.WorldOutwardNormal, FVector::YAxisVector) > 0.9f);
			TestEqual(TEXT("Hole side snap edge should use the opening surface kind."), Edge.Endpoint.SurfaceKind, EEHBElementSurfaceKind::Opening);
			TestEqual(TEXT("Hole side snap edge should expose a hole side surface name."), Edge.Endpoint.SurfaceName, FName(TEXT("Slab.HoleSide")));
			break;
		}
	}

	TestTrue(TEXT("Slab side snap edges should expose the hole side edge."), bFoundHoleBottomEdge);

	World->EditorDestroyActor(Slab, true);
	return true;
}

#include "EHBCutSlabRegionsCases.inl"
#include "EHBCutSlabAtomicCases.inl"

#endif
