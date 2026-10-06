// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/EHBGeneratedMeshComponent.h"

#include "Materials/Material.h"
#include "Misc/AutomationTest.h"

namespace
{
	void BuildSingleTriangleMesh(
		float HeightOffset,
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
			FVector(0.0, 100.0, HeightOffset)
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBGeneratedMeshActualMatchTest,"EHB.GeneratedMesh.ActualOutputMatch",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBGeneratedMeshActualMatchTest::RunTest(const FString& Parameters)
{
 auto* C=NewObject<UEHBGeneratedMeshComponent>(GetTransientPackage());
 TArray<FVector> V,N;TArray<int32> T;TArray<FVector2D> UV;TArray<FLinearColor> Colors;TArray<FProcMeshTangent> Tangents;BuildSingleTriangleMesh(20,V,T,N,UV,Colors,Tangents);
 auto Submit=[&](){C->CreateMeshSection_LinearColor(0,V,T,N,UV,Colors,Tangents,true);};
 auto Matches=[&](){return C->MatchesMeshSection(0,V,T,N,UV,{},{},{},{},{},true);};Submit();TestTrue(TEXT("Actual output matches submitted mesh"),Matches());
 for(int32 Case=0;Case<11;++Case)
 {
  auto* Section=C->GetProcMeshSection(0);const uint32 Hash=Section->ContentHash;
  switch(Case){case 0:Section->ProcVertexBuffer[0].Position.X+=5;break;case 1:Section->ProcVertexBuffer[0].Normal=FVector::ForwardVector;break;
   case 2:Section->ProcVertexBuffer[0].UV0.X+=1;break;case 3:Section->ProcVertexBuffer[0].UV3.X=1;break;case 4:Section->ProcVertexBuffer[0].Color=FColor::Red;break;
   case 5:Section->ProcVertexBuffer[0].Tangent.bFlipTangentY=true;break;case 6:Swap(Section->ProcIndexBuffer[0],Section->ProcIndexBuffer[1]);break;
   case 7:Section->SectionLocalBox.Max.Z+=5;break;case 8:Section->bEnableCollision=false;break;case 9:Section->bSectionVisible=false;break;case 10:Section->ProcVertexBuffer.RemoveAt(0);break;}
  TestEqual(TEXT("Corruption deliberately retains old hash"),Section->ContentHash,Hash);TestFalse(TEXT("Old hash cannot disguise damaged actual output"),Matches());
  const auto Before=C->GetSubmittedSectionUpdateCount();Submit();TestTrue(TEXT("Identical source repairs corrupted output"),Matches());TestEqual(TEXT("Repair submits a replacement"),C->GetSubmittedSectionUpdateCount(),Before+1);
 }
 T={-4,1,99,0,0,1,2};Submit();TestTrue(TEXT("Matcher respects clamping, dropped degenerates and trailing index"),Matches());
 C->ClearAllMeshSections();TestFalse(TEXT("Missing geometry never matches"),Matches());return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEHBGeneratedMeshComponentSkipsIdenticalSubmissionsTest,
	"EHB.GeneratedMesh.SkipsIdenticalSectionSubmissions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEHBGeneratedMeshComponentSkipsIdenticalSubmissionsTest::RunTest(const FString& Parameters)
{
	UEHBGeneratedMeshComponent* Component = NewObject<UEHBGeneratedMeshComponent>(GetTransientPackage());
	TestNotNull(TEXT("Generated mesh component should be created."), Component);
	if (!Component)
	{
		return false;
	}

	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FLinearColor> VertexColors;
	TArray<FProcMeshTangent> Tangents;
	BuildSingleTriangleMesh(0.0f, Vertices, Triangles, Normals, UVs, VertexColors, Tangents);

	Component->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs, VertexColors, Tangents, false);
	TestEqual(TEXT("First submission should create one section."), Component->GetNumSections(), 1);
	TestTrue(TEXT("First submission should be counted."), Component->GetSubmittedSectionUpdateCount() == 1);
	const int32 RevisionAfterFirstSubmission = Component->GetMeshRevision();

	Component->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs, VertexColors, Tangents, false);
	TestTrue(TEXT("Identical submission should be skipped."), Component->GetSkippedIdenticalSectionUpdateCount() == 1);
	TestTrue(TEXT("Skipped submission should not be counted as submitted."), Component->GetSubmittedSectionUpdateCount() == 1);
	TestEqual(TEXT("Skipped submission should not advance mesh revision."), Component->GetMeshRevision(), RevisionAfterFirstSubmission);

	BuildSingleTriangleMesh(25.0f, Vertices, Triangles, Normals, UVs, VertexColors, Tangents);
	Component->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs, VertexColors, Tangents, false);
	TestTrue(TEXT("Changed submission should be counted."), Component->GetSubmittedSectionUpdateCount() == 2);
	TestTrue(TEXT("Changed submission should advance mesh revision."), Component->GetMeshRevision() > RevisionAfterFirstSubmission);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEHBGeneratedMeshComponentClearsTrailingSectionsTest,
	"EHB.GeneratedMesh.ClearsTrailingSections",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEHBGeneratedMeshComponentClearsTrailingSectionsTest::RunTest(const FString& Parameters)
{
	UEHBGeneratedMeshComponent* Component = NewObject<UEHBGeneratedMeshComponent>(GetTransientPackage());
	TestNotNull(TEXT("Generated mesh component should be created."), Component);
	if (!Component)
	{
		return false;
	}

	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FLinearColor> VertexColors;
	TArray<FProcMeshTangent> Tangents;
	BuildSingleTriangleMesh(0.0f, Vertices, Triangles, Normals, UVs, VertexColors, Tangents);
	Component->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs, VertexColors, Tangents, false);

	BuildSingleTriangleMesh(50.0f, Vertices, Triangles, Normals, UVs, VertexColors, Tangents);
	Component->CreateMeshSection_LinearColor(1, Vertices, Triangles, Normals, UVs, VertexColors, Tangents, false);
	TestEqual(TEXT("Two sections should exist before trimming."), Component->GetNumSections(), 2);

	Component->ClearMeshSectionsFrom(1);
	TestEqual(TEXT("Trailing section should be removed."), Component->GetNumSections(), 1);
	TestNotNull(TEXT("Primary section should remain available."), Component->GetProcMeshSection(0));

	BuildSingleTriangleMesh(0.0f, Vertices, Triangles, Normals, UVs, VertexColors, Tangents);
	const uint64 SkipsBeforeRepeat = Component->GetSkippedIdenticalSectionUpdateCount();
	Component->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs, VertexColors, Tangents, false);
	TestTrue(TEXT("Primary section should keep its content hash after trimming."),
		Component->GetSkippedIdenticalSectionUpdateCount() == SkipsBeforeRepeat + 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEHBGeneratedMeshComponentSkipsUnchangedMaterialsTest,
	"EHB.GeneratedMesh.SkipsUnchangedMaterials",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEHBGeneratedMeshComponentSkipsUnchangedMaterialsTest::RunTest(const FString& Parameters)
{
	UEHBGeneratedMeshComponent* Component = NewObject<UEHBGeneratedMeshComponent>(GetTransientPackage());
	TestNotNull(TEXT("Generated mesh component should be created."), Component);
	if (!Component)
	{
		return false;
	}

	UMaterial* MaterialA = NewObject<UMaterial>(GetTransientPackage());
	UMaterial* MaterialB = NewObject<UMaterial>(GetTransientPackage());
	TestNotNull(TEXT("First test material should be created."), MaterialA);
	TestNotNull(TEXT("Second test material should be created."), MaterialB);
	if (!MaterialA || !MaterialB)
	{
		return false;
	}

	TestTrue(TEXT("First material assignment should report a change."), Component->SetMaterialIfChanged(0, MaterialA));
	TestTrue(TEXT("Material slot should store the first material."), Component->GetMaterial(0) == MaterialA);
	TestFalse(TEXT("Repeating the same material assignment should be skipped."), Component->SetMaterialIfChanged(0, MaterialA));
	TestTrue(TEXT("Changing to a different material should report a change."), Component->SetMaterialIfChanged(0, MaterialB));
	TestTrue(TEXT("Material slot should store the second material."), Component->GetMaterial(0) == MaterialB);
	TestFalse(TEXT("Negative material slots should be ignored."), Component->SetMaterialIfChanged(-1, MaterialA));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEHBGeneratedMeshComponentReportsStatsTest,
	"EHB.GeneratedMesh.ReportsStats",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEHBGeneratedMeshComponentReportsStatsTest::RunTest(const FString& Parameters)
{
	UEHBGeneratedMeshComponent* Component = NewObject<UEHBGeneratedMeshComponent>(GetTransientPackage());
	TestNotNull(TEXT("Generated mesh component should be created."), Component);
	if (!Component)
	{
		return false;
	}

	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FLinearColor> VertexColors;
	TArray<FProcMeshTangent> Tangents;
	BuildSingleTriangleMesh(0.0f, Vertices, Triangles, Normals, UVs, VertexColors, Tangents);
	Component->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs, VertexColors, Tangents, false);

	BuildSingleTriangleMesh(50.0f, Vertices, Triangles, Normals, UVs, VertexColors, Tangents);
	Component->CreateMeshSection_LinearColor(1, Vertices, Triangles, Normals, UVs, VertexColors, Tangents, true);

	const FEHBGeneratedMeshStats Stats = Component->GetMeshStats();
	TestEqual(TEXT("Stats should report both sections."), Stats.SectionCount, 2);
	TestEqual(TEXT("Stats should report both visible sections."), Stats.VisibleSectionCount, 2);
	TestEqual(TEXT("Stats should report all vertices."), Stats.VertexCount, 6);
	TestEqual(TEXT("Stats should report all triangles."), Stats.TriangleCount, 2);
	TestEqual(TEXT("Stats should report only collision-enabled sections."), Stats.CollisionSectionCount, 1);
	TestEqual(TEXT("Stats should report collision vertices."), Stats.CollisionVertexCount, 3);
	TestEqual(TEXT("Stats should report collision triangles."), Stats.CollisionTriangleCount, 1);
	TestEqual(TEXT("Stats should estimate vertex buffer memory."), Stats.EstimatedVertexBufferBytes, static_cast<int64>(6 * sizeof(FProcMeshVertex)));
	TestEqual(TEXT("Stats should estimate index buffer memory."), Stats.EstimatedIndexBufferBytes, static_cast<int64>(6 * sizeof(uint32)));
	TestEqual(TEXT("Stats should include submitted section count."), Stats.SubmittedSectionUpdateCount, static_cast<int64>(2));
	TestEqual(TEXT("Stats should include skipped section count."), Stats.SkippedIdenticalSectionUpdateCount, static_cast<int64>(0));

	Component->ResetMeshUpdateStats();
	const FEHBGeneratedMeshStats ResetStats = Component->GetMeshStats();
	TestEqual(TEXT("Reset should not remove sections."), ResetStats.SectionCount, 2);
	TestEqual(TEXT("Reset should clear submitted section count."), ResetStats.SubmittedSectionUpdateCount, static_cast<int64>(0));
	TestEqual(TEXT("Reset should clear skipped section count."), ResetStats.SkippedIdenticalSectionUpdateCount, static_cast<int64>(0));

	return true;
}

#endif
