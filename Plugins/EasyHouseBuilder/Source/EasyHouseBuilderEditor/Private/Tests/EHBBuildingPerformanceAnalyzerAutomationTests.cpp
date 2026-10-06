// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Diagnostics/EHBBuildingPerformanceAnalyzer.h"

#include "Components/EHBGeneratedMeshComponent.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"

namespace
{
	void BuildPerformanceTestTriangle(
		TArray<FVector>& OutVertices,
		TArray<int32>& OutTriangles,
		TArray<FVector>& OutNormals,
		TArray<FVector2D>& OutUVs,
		TArray<FLinearColor>& OutVertexColors,
		TArray<FProcMeshTangent>& OutTangents)
	{
		OutVertices = {
			FVector(0.0, 0.0, 0.0),
			FVector(100.0, 0.0, 0.0),
			FVector(0.0, 100.0, 0.0)
		};
		OutTriangles = { 0, 1, 2 };
		OutNormals.Init(FVector::UpVector, OutVertices.Num());
		OutUVs = {
			FVector2D(0.0, 0.0),
			FVector2D(1.0, 0.0),
			FVector2D(0.0, 1.0)
		};
		OutVertexColors.Init(FLinearColor::White, OutVertices.Num());
		OutTangents.Init(FProcMeshTangent(), OutVertices.Num());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEHBBuildingPerformanceAnalyzerReportsActorMeshStatsTest,
	"EHB.PerformanceAnalyzer.ReportsActorMeshStats",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEHBBuildingPerformanceAnalyzerReportsActorMeshStatsTest::RunTest(const FString& Parameters)
{
	AActor* Actor = NewObject<AActor>(GetTransientPackage());
	TestNotNull(TEXT("Transient actor should be created."), Actor);
	if (!Actor)
	{
		return false;
	}

	UEHBGeneratedMeshComponent* Component = NewObject<UEHBGeneratedMeshComponent>(Actor);
	TestNotNull(TEXT("Generated mesh component should be created."), Component);
	if (!Component)
	{
		return false;
	}

	Actor->AddInstanceComponent(Component);

	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FLinearColor> VertexColors;
	TArray<FProcMeshTangent> Tangents;
	BuildPerformanceTestTriangle(Vertices, Triangles, Normals, UVs, VertexColors, Tangents);
	Component->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs, VertexColors, Tangents, true);

	const FEHBElementPerformanceEntry Entry = UEHBBuildingPerformanceAnalyzer::AnalyzeActor(Actor);
	TestEqual(TEXT("Analyzer should count generated mesh components."), Entry.GeneratedMeshComponentCount, 1);
	TestEqual(TEXT("Analyzer should count generated mesh vertices."), Entry.GeneratedMeshStats.VertexCount, 3);
	TestEqual(TEXT("Analyzer should count generated mesh triangles."), Entry.GeneratedMeshStats.TriangleCount, 1);
	TestEqual(TEXT("Analyzer should count generated collision triangles."), Entry.GeneratedMeshStats.CollisionTriangleCount, 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEHBBuildingPerformanceAnalyzerReportsNullBuildingTest,
	"EHB.PerformanceAnalyzer.ReportsNullBuilding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEHBBuildingPerformanceAnalyzerReportsNullBuildingTest::RunTest(const FString& Parameters)
{
	const FEHBBuildingPerformanceReport Report = UEHBBuildingPerformanceAnalyzer::AnalyzeBuilding(
		nullptr,
		FEHBBuildingPerformanceBudget());

	TestFalse(TEXT("Null building report should be invalid."), Report.bValid);
	TestFalse(TEXT("Null building report should not be within budget."), Report.bWithinBudget);
	TestTrue(TEXT("Null building report should include an issue."), Report.Issues.Num() > 0);
	TestTrue(TEXT("Null building report should have a blocking issue."), UEHBBuildingPerformanceAnalyzer::HasBlockingPerformanceIssues(Report));

	return true;
}

#endif
