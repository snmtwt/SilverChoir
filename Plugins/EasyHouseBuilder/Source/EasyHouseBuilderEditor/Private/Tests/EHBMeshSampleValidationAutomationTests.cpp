// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Sampling/EHBMeshSampleValidation.h"

#include "Engine/StaticMesh.h"
#include "Misc/AutomationTest.h"
#include "Sampling/EHBDoorWindowMeshData.h"
#include "Sampling/EHBPillarMeshData.h"
#include "Sampling/EHBRailingMeshData.h"
#include "Sampling/EHBWallMeshData.h"

namespace
{
	void AddSingleTriangleWallSurface(FEHBWallMeshSampleSurface& Surface, EEHBWallMeshSampleSide Side)
	{
		Surface.SampleSide = Side;
		Surface.SampleTriangleCount = 1;
		Surface.Vertices.SetNum(3);
		Surface.Vertices[0].Position = FVector(0.0, 0.0, 0.0);
		Surface.Vertices[1].Position = FVector(100.0, 0.0, 0.0);
		Surface.Vertices[2].Position = FVector(0.0, 0.0, 100.0);
		Surface.Triangles = { 0, 1, 2 };
		Surface.TriangleMaterialIndices = { 0 };
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEHBMeshSampleValidationAcceptsCleanWallSampleTest,
	"EHB.MeshSampleValidation.AcceptsCleanWallSample",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEHBMeshSampleValidationAcceptsCleanWallSampleTest::RunTest(const FString& Parameters)
{
	FEHBWallMeshData Row;
	Row.WallWidth = 100.0f;
	Row.WallHeight = 300.0f;
	Row.WallThickness = 20.0f;
	Row.SourceTriangleCount = 1;
	AddSingleTriangleWallSurface(Row.FrontSurface, EEHBWallMeshSampleSide::Front);
	AddSingleTriangleWallSurface(Row.BackSurface, EEHBWallMeshSampleSide::Back);

	const FEHBMeshSampleValidationResult Result = FEHBMeshSampleValidation::ValidateWallSurfaceForTarget(Row, nullptr, false);
	TestFalse(TEXT("Clean wall sample should not report errors."), Result.HasErrors());
	TestEqual(
		TEXT("Clean wall sample should be classified as light."),
		static_cast<uint8>(Result.PerformanceClass),
		static_cast<uint8>(EEHBMeshSamplePerformanceClass::Light));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEHBMeshSampleValidationRejectsInvalidPillarSampleTest,
	"EHB.MeshSampleValidation.RejectsInvalidPillarSample",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEHBMeshSampleValidationRejectsInvalidPillarSampleTest::RunTest(const FString& Parameters)
{
	const FEHBPillarMeshData Row;
	const FEHBMeshSampleValidationResult Result = FEHBMeshSampleValidation::ValidatePillarForTarget(Row, nullptr);
	TestTrue(TEXT("Empty pillar sample should report errors."), Result.HasErrors());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEHBMeshSampleValidationRejectsDoorWindowWithoutClassTest,
	"EHB.MeshSampleValidation.RejectsDoorWindowWithoutClass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEHBMeshSampleValidationRejectsDoorWindowWithoutClassTest::RunTest(const FString& Parameters)
{
	FEHBDoorWindowMeshData Row;
	Row.OpeningWidth = 120.0f;
	Row.OpeningHeight = 180.0f;
	Row.OpeningThickness = 20.0f;

	const FEHBMeshSampleValidationResult Result = FEHBMeshSampleValidation::ValidateDoorWindowForTarget(Row, nullptr);
	TestTrue(TEXT("Door/window sample without a generated class should report errors."), Result.HasErrors());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEHBMeshSampleValidationAllowsPartialRailingSampleTest,
	"EHB.MeshSampleValidation.AllowsPartialRailingSample",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEHBMeshSampleValidationAllowsPartialRailingSampleTest::RunTest(const FString& Parameters)
{
	const FSoftObjectPath CubeMeshPath(TEXT("/Engine/BasicShapes/Cube.Cube"));

	FEHBRailingMeshData PostOnlyRow;
	PostOnlyRow.PostMesh.SourceStaticMesh = TSoftObjectPtr<UStaticMesh>(CubeMeshPath);
	const FEHBMeshSampleValidationResult PostOnlyResult =
		FEHBMeshSampleValidation::ValidateRailingForTarget(PostOnlyRow, nullptr);
	TestFalse(TEXT("Railing sample with only a post mesh should be valid."), PostOnlyResult.HasErrors());

	FEHBRailingMeshData RailOnlyRow;
	RailOnlyRow.RailMesh.SourceStaticMesh = TSoftObjectPtr<UStaticMesh>(CubeMeshPath);
	const FEHBMeshSampleValidationResult RailOnlyResult =
		FEHBMeshSampleValidation::ValidateRailingForTarget(RailOnlyRow, nullptr);
	TestFalse(TEXT("Railing sample with only a rail mesh should be valid."), RailOnlyResult.HasErrors());

	const FEHBRailingMeshData EmptyRow;
	const FEHBMeshSampleValidationResult EmptyResult =
		FEHBMeshSampleValidation::ValidateRailingForTarget(EmptyRow, nullptr);
	TestTrue(TEXT("Railing sample without post or rail mesh should be invalid."), EmptyResult.HasErrors());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEHBMeshSampleValidationInitializesMetadataTest,
	"EHB.MeshSampleValidation.InitializesMetadata",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEHBMeshSampleValidationInitializesMetadataTest::RunTest(const FString& Parameters)
{
	FEHBMeshSampleTemplateMetadata Metadata;
	FEHBMeshSampleValidation::InitializeMetadata(
		Metadata,
		EEHBMeshSampleTemplateKind::WallSurface,
		TEXT("HeavyWall"),
		nullptr,
		13000,
		1);

	TestTrue(TEXT("Metadata should receive a stable template Guid."), Metadata.TemplateGuid.IsValid());
	TestEqual(
		TEXT("Metadata kind should be written."),
		static_cast<uint8>(Metadata.TemplateKind),
		static_cast<uint8>(EEHBMeshSampleTemplateKind::WallSurface));
	TestEqual(TEXT("Metadata name should be written."), Metadata.TemplateName, FName(TEXT("HeavyWall")));
	TestEqual(
		TEXT("Metadata should classify estimated cost."),
		static_cast<uint8>(Metadata.PerformanceClass),
		static_cast<uint8>(EEHBMeshSamplePerformanceClass::Heavy));

	return true;
}

#endif
