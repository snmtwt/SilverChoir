// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Geometry/EHBSurfaceGeometryTypes.h"

#include "Misc/AutomationTest.h"

namespace
{
	TArray<FVector> MakeSquareLoop(float Size)
	{
		return {
			FVector(0.0f, 0.0f, 0.0f),
			FVector(Size, 0.0f, 0.0f),
			FVector(Size, Size, 0.0f),
			FVector(0.0f, Size, 0.0f)
		};
	}

	FVector GetTriangleNormal(const FEHBSurfaceMeshBuildResult& Result, int32 TriangleOffset)
	{
		const FVector& A = Result.Vertices[Result.Triangles[TriangleOffset]];
		const FVector& B = Result.Vertices[Result.Triangles[TriangleOffset + 1]];
		const FVector& C = Result.Vertices[Result.Triangles[TriangleOffset + 2]];
		return FVector::CrossProduct(B - A, C - A).GetSafeNormal();
	}

	bool IsTriangleOnPlaneZ(const FEHBSurfaceMeshBuildResult& Result, int32 TriangleOffset, float Z)
	{
		constexpr float PlaneTolerance = 0.01f;
		return FMath::IsNearlyEqual(Result.Vertices[Result.Triangles[TriangleOffset]].Z, Z, PlaneTolerance)
			&& FMath::IsNearlyEqual(Result.Vertices[Result.Triangles[TriangleOffset + 1]].Z, Z, PlaneTolerance)
			&& FMath::IsNearlyEqual(Result.Vertices[Result.Triangles[TriangleOffset + 2]].Z, Z, PlaneTolerance);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEHBSurfaceGeometryBuildsSimpleSlabTest,
	"EHB.SurfaceGeometry.BuildsSimpleSlab",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEHBSurfaceGeometryBuildsSimpleSlabTest::RunTest(const FString& Parameters)
{
	FEHBSlabSurfaceMeshBuildInput Input;
	Input.TopBoundaryLoop = MakeSquareLoop(200.0f);
	Input.DefaultBottomZ = -20.0f;
	Input.bBuildTop = true;
	Input.bBuildBottom = true;
	Input.bBuildSides = true;

	FEHBSurfaceMeshBuildResult Result;
	TestTrue(TEXT("Simple slab should build."), FEHBPlanarSurfaceGeometryBuilder::BuildSlabSurface(Input, Result));
	TestTrue(TEXT("Simple slab should contain vertices."), Result.Vertices.Num() > 0);
	TestTrue(TEXT("Simple slab should contain triangles."), Result.Triangles.Num() >= 3);
	TestEqual(TEXT("Normals should match vertices."), Result.Normals.Num(), Result.Vertices.Num());
	TestEqual(TEXT("UVs should match vertices."), Result.UV0.Num(), Result.Vertices.Num());

	int32 TopTriangleCount = 0;
	int32 BottomTriangleCount = 0;
	for (int32 TriangleOffset = 0; TriangleOffset + 2 < Result.Triangles.Num(); TriangleOffset += 3)
	{
		const FVector TriangleNormal = GetTriangleNormal(Result, TriangleOffset);
		if (IsTriangleOnPlaneZ(Result, TriangleOffset, 0.0f))
		{
			++TopTriangleCount;
			TestTrue(
				TEXT("Top slab triangles should use generated-mesh front winding."),
				FVector::DotProduct(TriangleNormal, FVector::UpVector) < -0.5f);
		}
		else if (IsTriangleOnPlaneZ(Result, TriangleOffset, Input.DefaultBottomZ))
		{
			++BottomTriangleCount;
			TestTrue(
				TEXT("Bottom slab triangles should use generated-mesh front winding."),
				FVector::DotProduct(TriangleNormal, FVector::UpVector) > 0.5f);
		}
	}

	TestTrue(TEXT("Simple slab should include top cap triangles."), TopTriangleCount > 0);
	TestTrue(TEXT("Simple slab should include bottom cap triangles."), BottomTriangleCount > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEHBSurfaceGeometryBuildsSlabWithHoleTest,
	"EHB.SurfaceGeometry.BuildsSlabWithHole",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEHBSurfaceGeometryBuildsSlabWithHoleTest::RunTest(const FString& Parameters)
{
	FEHBSlabSurfaceMeshBuildInput Input;
	Input.TopBoundaryLoop = MakeSquareLoop(400.0f);
	Input.TopHoleLoops.Add({
		FVector(100.0f, 100.0f, 0.0f),
		FVector(300.0f, 100.0f, 0.0f),
		FVector(300.0f, 300.0f, 0.0f),
		FVector(100.0f, 300.0f, 0.0f)
	});
	Input.DefaultBottomZ = -30.0f;
	Input.bBuildBottom = true;
	Input.bBuildSides = true;

	FEHBSurfaceMeshBuildResult Result;
	TestTrue(TEXT("Slab with hole should build."), FEHBPlanarSurfaceGeometryBuilder::BuildSlabSurface(Input, Result));
	TestTrue(TEXT("Slab with hole should contain side and cap triangles."), Result.Triangles.Num() > 12);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEHBSurfaceGeometryBuildsVerticalSurfaceTest,
	"EHB.SurfaceGeometry.BuildsVerticalSurface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEHBSurfaceGeometryBuildsVerticalSurfaceTest::RunTest(const FString& Parameters)
{
	FEHBVerticalSurfaceMeshBuildInput Input;
	Input.LocalBasePolyline = {
		FVector(0.0f, 0.0f, 0.0f),
		FVector(300.0f, 0.0f, 0.0f)
	};
	Input.SurfaceHeight = 240.0f;

	FEHBSurfaceMeshBuildResult Result;
	TestTrue(TEXT("Vertical surface should build."), FEHBVerticalSurfaceGeometryBuilder::BuildVerticalSurface(Input, Result));
	TestEqual(TEXT("Single strip should have four vertices."), Result.Vertices.Num(), 4);
	TestEqual(TEXT("Single strip should have two triangles."), Result.Triangles.Num(), 6);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEHBSurfaceGeometryFindsBoundaryPointTest,
	"EHB.SurfaceGeometry.FindsBoundaryPoint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEHBSurfaceGeometryFindsBoundaryPointTest::RunTest(const FString& Parameters)
{
	FEHBSurfaceBoundaryHit Hit;
	const bool bFound = FEHBSurfaceGeometryUtil::FindClosestBoundaryPointXY(
		MakeSquareLoop(100.0f),
		FTransform::Identity,
		FVector(50.0f, -20.0f, 30.0f),
		0.0f,
		true,
		Hit);

	TestTrue(TEXT("Boundary hit should resolve."), bFound);
	TestEqual(TEXT("Closest point should lie on lower edge X."), Hit.WorldPoint.X, 50.0);
	TestEqual(TEXT("Closest point should lie on lower edge Y."), Hit.WorldPoint.Y, 0.0);
	TestEqual(TEXT("Closest segment should be the first edge."), Hit.SegmentIndex, 0);
	return true;
}

#endif
