// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Actors/EHB_Railing.h"
#include "Actors/EHB_RailingGate.h"
#include "Actors/EHB_Stair.h"
#include "EHB_Building.h"

#include "Components/EHBGeneratedMeshComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Sampling/EHBRailingMeshData.h"
#include "StaticMeshDescription.h"
#include "Serialization/ObjectReader.h"
#include "Serialization/ObjectWriter.h"
#include "EHBRailingCreationSnap.h"

namespace
{
	// A real narrow mesh with an offset pivot, independent of optional project assets.
	// A 100 cm engine cube would hide the source-bounds regression this fixture tests.
	UStaticMesh* BuildTransientNarrowPostMesh()
	{
		UStaticMeshDescription* Description = NewObject<UStaticMeshDescription>(GetTransientPackage(), NAME_None, RF_Transient);
		Description->RegisterAttributes();
		const FPolygonGroupID Group = Description->GetMeshDescription().CreatePolygonGroup();
		Description->SetPolygonGroupMaterialSlotName(Group, TEXT("Post"));
		FPolygonID Faces[6];
		Description->CreateCube(FVector::ZeroVector, FVector(4, 6, 90), Group,
			Faces[0], Faces[1], Faces[2], Faces[3], Faces[4], Faces[5]);
		// Translate vertices explicitly: UE 5.8 CreateCube applies Center before
		// the vertex sign masks, which changes extents for a nonzero center.
		auto Positions = Description->GetVertexPositions();
		for (FVertexID Vertex : Description->GetMeshDescription().Vertices().GetElementIDs())
			Positions[Vertex] += FVector3f(17, -9, 127);
		UStaticMesh* Mesh = NewObject<UStaticMesh>(GetTransientPackage(), NAME_None, RF_Transient);
		Mesh->GetStaticMaterials().Add(FStaticMaterial(nullptr, TEXT("Post")));
		UStaticMesh::FBuildMeshDescriptionsParams Params;
		Params.bFastBuild = true;
		Params.bMarkPackageDirty = false;
		Params.bBuildSimpleCollision = false;
		Params.bAllowCpuAccess = true;
		return Mesh->BuildFromMeshDescriptions({ &Description->GetMeshDescription() }, Params) ? Mesh : nullptr;
	}

	template <typename TActor>
	TActor* SpawnTransientRailingTestActor(UWorld* World, const FVector& WorldLocation)
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

	void DestroyRailingTestActor(AActor* Actor)
	{
		if (Actor && !Actor->IsActorBeingDestroyed())
		{
			Actor->Destroy();
		}
	}

	const FEHBMeshSection* GetRailMeshSection(const AEHB_Railing* Railing)
	{
		return Railing && Railing->RailGeneratedMeshComponent
			? Railing->RailGeneratedMeshComponent->GetProcMeshSection(0)
			: nullptr;
	}

	bool HasGeneratedRailMesh(const AEHB_Railing* Railing)
	{
		const FEHBMeshSection* Section = GetRailMeshSection(Railing);
		return Section && Section->ProcVertexBuffer.Num() > 0 && Section->ProcIndexBuffer.Num() > 0;
	}

	bool GetGeneratedRailBounds(const AEHB_Railing* Railing, FBox& OutBounds)
	{
		const FEHBMeshSection* Section = GetRailMeshSection(Railing);
		if (!Section || Section->ProcVertexBuffer.IsEmpty())
		{
			return false;
		}

		OutBounds = FBox(ForceInit);
		for (const FEHBMeshVertex& Vertex : Section->ProcVertexBuffer)
		{
			OutBounds += FVector(Vertex.Position);
		}
		return OutBounds.IsValid != 0;
	}

	bool GetGeneratedRailXRange(const AEHB_Railing* Railing, float& OutMinX, float& OutMaxX)
	{
		FBox Bounds(ForceInit);
		if (!GetGeneratedRailBounds(Railing, Bounds))
		{
			return false;
		}

		OutMinX = Bounds.Min.X;
		OutMaxX = Bounds.Max.X;
		return OutMinX <= OutMaxX;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEHBRailingLinearGateTest,
	"EHB.Railing.LinearGate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEHBRailingLinearGateTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("Editor world should be available"), World))
	{
		return false;
	}

	const FVector TestOrigin(0.0f, 12000.0f, 140000.0f);
	AEHB_Building* Building = SpawnTransientRailingTestActor<AEHB_Building>(World, TestOrigin);
	AEHB_Railing* Railing = SpawnTransientRailingTestActor<AEHB_Railing>(World, TestOrigin);
	AEHB_RailingGate* Gate = SpawnTransientRailingTestActor<AEHB_RailingGate>(World, TestOrigin);
	if (!TestNotNull(TEXT("Building should spawn"), Building)
		|| !TestNotNull(TEXT("Railing should spawn"), Railing)
		|| !TestNotNull(TEXT("Railing gate should spawn"), Gate))
	{
		DestroyRailingTestActor(Gate);
		DestroyRailingTestActor(Railing);
		DestroyRailingTestActor(Building);
		return false;
	}

	TestTrue(
		TEXT("Linear railing should configure and generate"),
		Railing->ConfigureLinear(
			Building,
			FTransform::Identity,
			FVector::ZeroVector,
			FVector(360.0f, 0.0f, 0.0f),
			2));
	Railing->PostSpacing = 90.0f;
	TestTrue(TEXT("Linear railing should rebuild after spacing changes"), Railing->RebuildRailing());
	TestEqual(TEXT("Railing should be registered on the requested floor"), Railing->FloorIndex, 2);
	TestEqual(TEXT("Railing should use the railing floor role"), Railing->FloorRole, EEHBBuildingFloorElementRole::Railing);
	TestTrue(TEXT("Linear railing should generate posts"), Railing->GeneratedPosts.Num() >= 4);
	TestTrue(
		TEXT("Post HISM should contain one instance per generated post"),
		Railing->PostMeshComponent
			&& Railing->PostMeshComponent->GetInstanceCount() == Railing->GeneratedPosts.Num());
	TestTrue(TEXT("Linear railing should generate rail mesh"), HasGeneratedRailMesh(Railing));

	TestTrue(
		TEXT("Gate should configure on the railing"),
		Gate->ConfigureOnRailing(Railing, 180.0f, 90.0f, 95.0f, EEHBRailingGateHingeSide::Left));
	TestEqual(TEXT("Gate should be registered on the railing floor"), Gate->FloorIndex, 2);
	TestEqual(TEXT("Railing should store one gate connection"), Railing->GateConnections.Num(), 1);

	int32 GatePostCount = 0;
	int32 NonGatePostsInsideOpening = 0;
	for (const FEHBRailingPost& Post : Railing->GeneratedPosts)
	{
		if (Post.bGatePost)
		{
			++GatePostCount;
		}
		else if (Post.Distance > 135.0f && Post.Distance < 225.0f)
		{
			++NonGatePostsInsideOpening;
		}
	}
	TestEqual(TEXT("Gate should force two edge posts"), GatePostCount, 2);
	TestEqual(TEXT("Normal posts should be removed from the gate opening"), NonGatePostsInsideOpening, 0);

	FEHBRelationQuery GateHostQuery;
	GateHostQuery.ElementGuid = Gate->ElementGuid;
	GateHostQuery.Direction = EEHBRelationQueryDirection::Incoming;
	GateHostQuery.Types = { EEHBElementRelationType::HostedElement };
	TestEqual(
		TEXT("Railing gate should receive one hosted-element relation"),
		Building->QueryElementRelations(GateHostQuery).Num(),
		1);

	DestroyRailingTestActor(Gate);
	DestroyRailingTestActor(Railing);
	DestroyRailingTestActor(Building);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEHBRailingStairHostedTest,
	"EHB.Railing.StairHosted",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEHBRailingStairHostedTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("Editor world should be available"), World))
	{
		return false;
	}

	const FVector TestOrigin(2000.0f, 12000.0f, 140000.0f);
	AEHB_Building* Building = SpawnTransientRailingTestActor<AEHB_Building>(World, TestOrigin);
	AEHB_Stair* Stair = SpawnTransientRailingTestActor<AEHB_Stair>(World, TestOrigin);
	AEHB_Railing* Railing = SpawnTransientRailingTestActor<AEHB_Railing>(World, TestOrigin);
	if (!TestNotNull(TEXT("Building should spawn"), Building)
		|| !TestNotNull(TEXT("Stair should spawn"), Stair)
		|| !TestNotNull(TEXT("Railing should spawn"), Railing))
	{
		DestroyRailingTestActor(Railing);
		DestroyRailingTestActor(Stair);
		DestroyRailingTestActor(Building);
		return false;
	}

	Stair->ConfigureDefaultStair(Building, FTransform::Identity, 280.0f, 140.0f, 30.0f);
	Stair->SetFloorAssignment(1, EEHBBuildingFloorElementRole::VerticalConnector);
	Stair->SetBottomControlWorldLocation(TestOrigin + FVector(360.0f, 120.0f, 0.0f));
	Stair->StairData.bUseIntermediateControls = true;
	Stair->StairData.IntermediateControlOffsets = {
		FVector2D(0.0f, 0.0f),
		FVector2D(0.0f, 120.0f),
		FVector2D(0.0f, -30.0f)
	};
	TestTrue(TEXT("Curved stair should rebuild before railing sampling"), Stair->RebuildStairMesh());

	TestTrue(
		TEXT("Stair-hosted railing should configure and generate"),
		Railing->ConfigureOnStair(Building, Stair, EEHBRailingSide::Right, 1));
	Railing->StepsPerPost = 3;
	Railing->StairSideOffset = 10.0f;
	Railing->MaxRailSegmentLength = 30.0f;
	TestTrue(TEXT("Stair-hosted railing should rebuild with step-aligned spacing"), Railing->RebuildRailing());
	TestEqual(TEXT("Railing should use stair-hosted mode"), Railing->PathMode, EEHBRailingPathMode::StairHosted);
	TestEqual(TEXT("Railing should store the stair host guid"), Railing->HostedStairGuid, Stair->ElementGuid);
	TestTrue(TEXT("Stair-hosted railing should generate posts"), Railing->GeneratedPosts.Num() >= 2);
	TArray<FEHBStairRailingPostSample> ExpectedStairPostSamples;
	TestTrue(
		TEXT("Stair-hosted railing should reuse stair post samples"),
		Stair->BuildRailingPostSamples(
			EEHBRailingSide::Right,
			EEHBRailingPostSpacingMode::StepAligned,
			Railing->PostSpacing,
			Railing->StepsPerPost,
			Railing->StairSideOffset,
			Railing->StairPostBaseHeightOffset,
			ExpectedStairPostSamples));
	TestEqual(
		TEXT("Stair-hosted railing should place posts on the upper landing, configured steps, and lower endpoint"),
		Railing->GeneratedPosts.Num(),
		ExpectedStairPostSamples.Num());
	if (Railing->GeneratedPosts.Num() > 1)
	{
		TestTrue(
			TEXT("First generated post should land on the upper landing tread center"),
			FMath::IsNearlyEqual(Railing->GeneratedPosts[0].Distance, -Stair->StairData.TreadDepth * 0.5f, 0.5f));
		const int32 ExpectedSecondPostStepIndex = FMath::Min(Stair->GeneratedStepCount - 1, Railing->StepsPerPost - 1);
		TestEqual(
			TEXT("Second generated post should count the upper landing tread in the configured interval"),
			Railing->GeneratedPosts[1].StepIndex,
			ExpectedSecondPostStepIndex);
	}
	if (Railing->GeneratedPosts.Num() > 2)
	{
		const int32 ExpectedThirdPostStepIndex = FMath::Min(Stair->GeneratedStepCount - 1, Railing->StepsPerPost * 2 - 1);
		TestEqual(TEXT("Third generated post should follow the configured step interval"), Railing->GeneratedPosts[2].StepIndex, ExpectedThirdPostStepIndex);
		TestEqual(TEXT("Last generated post should land on the bottom step endpoint"), Railing->GeneratedPosts.Last().StepIndex, Stair->GeneratedStepCount - 1);
	}

	FEHBRailingPathSample StartSample;
	FEHBRailingPathSample EndSample;
	const float RailingLength = Railing->GetRailingLength();
	TestTrue(TEXT("Railing should evaluate a start path sample"), Railing->EvaluatePathAtDistance(0.0f, StartSample));
	TestTrue(TEXT("Railing should evaluate an end path sample"), Railing->EvaluatePathAtDistance(RailingLength, EndSample));
	TestTrue(
		TEXT("Stair-hosted railing should sit inside the stair tread edge"),
		FMath::Abs(StartSample.LocalLocation.Y) < Stair->StairData.StairWidth * 0.5f);
	TestTrue(
		TEXT("Stair-hosted railing should honor edge inset"),
		FMath::IsNearlyEqual(
			FMath::Abs(StartSample.LocalLocation.Y),
			Stair->StairData.StairWidth * 0.5f - Railing->StairSideOffset,
			1.0f));
	TestTrue(
		TEXT("Stair-hosted railing should follow the stair lateral curve"),
		FMath::Abs(EndSample.LocalLocation.Y - StartSample.LocalLocation.Y) > 20.0f);
	TestTrue(
		TEXT("Stair-hosted railing base should follow stair top height"),
		EndSample.LocalLocation.Z < StartSample.LocalLocation.Z);
	TestTrue(
		TEXT("Generated railing posts should follow the curved stair path laterally"),
		FMath::Abs(Railing->GeneratedPosts.Last().LocalBaseLocation.Y - Railing->GeneratedPosts[0].LocalBaseLocation.Y) > 20.0f);
	TestTrue(
		TEXT("Generated railing post rotations should follow the curved stair tangent"),
		FVector::DotProduct(
			Railing->GeneratedPosts[0].LocalRotation.Vector().GetSafeNormal(),
			Railing->GeneratedPosts.Last().LocalRotation.Vector().GetSafeNormal()) < 0.98f);

	FEHBRelationQuery RailingHostQuery;
	RailingHostQuery.ElementGuid = Railing->ElementGuid;
	RailingHostQuery.Direction = EEHBRelationQueryDirection::Incoming;
	RailingHostQuery.Types = { EEHBElementRelationType::HostedElement };
	TestEqual(
		TEXT("Stair should host the railing through one relation"),
		Building->QueryElementRelations(RailingHostQuery).Num(),
		1);

	DestroyRailingTestActor(Railing);
	DestroyRailingTestActor(Stair);
	DestroyRailingTestActor(Building);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEHBRailingContinuousRailJointsTest,
	"EHB.Railing.ContinuousRailJoints",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEHBRailingContinuousRailJointsTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("Editor world should be available"), World))
	{
		return false;
	}

	const FVector TestOrigin(3000.0f, 12000.0f, 140000.0f);
	AEHB_Building* Building = SpawnTransientRailingTestActor<AEHB_Building>(World, TestOrigin);
	AEHB_Railing* Railing = SpawnTransientRailingTestActor<AEHB_Railing>(World, TestOrigin);
	if (!TestNotNull(TEXT("Building should spawn"), Building)
		|| !TestNotNull(TEXT("Railing should spawn"), Railing))
	{
		DestroyRailingTestActor(Railing);
		DestroyRailingTestActor(Building);
		return false;
	}

	TestTrue(
		TEXT("Linear railing should configure"),
		Railing->ConfigureLinear(
			Building,
			FTransform::Identity,
			FVector::ZeroVector,
			FVector(300.0f, 0.0f, 0.0f),
			1));
	Railing->PostSpacing = 150.0f;
	Railing->MaxRailSegmentLength = 1000.0f;
	Railing->RailThickness = 8.0f;
	TestTrue(TEXT("Linear railing should rebuild with two rail spans"), Railing->RebuildRailing());
	TestEqual(TEXT("Railing should have three generated posts"), Railing->GeneratedPosts.Num(), 3);
	float RailMinX = 0.0f;
	float RailMaxX = 0.0f;
	if (!TestTrue(TEXT("Railing should have generated rail mesh bounds"), GetGeneratedRailXRange(Railing, RailMinX, RailMaxX)))
	{
		DestroyRailingTestActor(Railing);
		DestroyRailingTestActor(Building);
		return false;
	}

	TestTrue(
		TEXT("Railing end points should not extend past the full run"),
		RailMinX >= -Railing->RailThickness
			&& RailMaxX <= 300.0f + Railing->RailThickness);

	DestroyRailingTestActor(Railing);
	DestroyRailingTestActor(Building);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEHBRailingRailTopHeightDoesNotChangePostHeightTest,
	"EHB.Railing.RailTopHeightDoesNotChangePostHeight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEHBRailingRailTopHeightDoesNotChangePostHeightTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("Editor world should be available"), World))
	{
		return false;
	}

	const FVector TestOrigin(3300.0f, 12000.0f, 140000.0f);
	AEHB_Building* Building = SpawnTransientRailingTestActor<AEHB_Building>(World, TestOrigin);
	AEHB_Railing* Railing = SpawnTransientRailingTestActor<AEHB_Railing>(World, TestOrigin);
	if (!TestNotNull(TEXT("Building should spawn"), Building)
		|| !TestNotNull(TEXT("Railing should spawn"), Railing))
	{
		DestroyRailingTestActor(Railing);
		DestroyRailingTestActor(Building);
		return false;
	}

	TestTrue(
		TEXT("Linear railing should configure"),
		Railing->ConfigureLinear(
			Building,
			FTransform::Identity,
			FVector::ZeroVector,
			FVector(240.0f, 0.0f, 0.0f),
			1));

	Railing->PostHeight = 180.0f;
	Railing->RailThickness = 8.0f;
	Railing->SetRailTopHeight(120.0f);
	TestTrue(TEXT("Railing should rebuild after top height changes"), Railing->RebuildRailing());
	TestTrue(
		TEXT("Rail top height should match the requested top height"),
		FMath::IsNearlyEqual(Railing->GetRailTopHeight(), 120.0f, 0.01f));
	TestTrue(
		TEXT("Default cube rail center should be below the requested top height by half the rail thickness"),
		FMath::IsNearlyEqual(Railing->RailHeight, 116.0f, 0.01f));
	TestTrue(
		TEXT("Changing rail top height should not alter post height"),
		FMath::IsNearlyEqual(Railing->PostHeight, 180.0f, 0.01f));

	DestroyRailingTestActor(Railing);
	DestroyRailingTestActor(Building);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEHBRailingSampledPostMeshUsesSourceBoundsTest,
	"EHB.Railing.SampledPostMeshUsesSourceBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEHBRailingSampledPostMeshUsesSourceBoundsTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("Editor world should be available"), World))
	{
		return false;
	}

	UStaticMesh* SamplePostMesh = BuildTransientNarrowPostMesh();
	if (!TestNotNull(TEXT("Sample railing post mesh should be available"), SamplePostMesh))
	{
		return false;
	}

	const FBox SourceBounds = SamplePostMesh->GetBounds().GetBox();
	const FVector SourceSize = SourceBounds.GetSize();
	const float SourceWidth = FMath::Max(SourceSize.X, SourceSize.Y);
	TestTrue(TEXT("Fixture has real 8 x 12 x 180 cm bounds"), SourceSize.Equals(FVector(8, 12, 180), 0.001));
	TestTrue(TEXT("Sample post source width should be smaller than a default cube"), SourceWidth < 20.0f);

	const FVector TestOrigin(3500.0f, 12000.0f, 140000.0f);
	AEHB_Building* Building = SpawnTransientRailingTestActor<AEHB_Building>(World, TestOrigin);
	AEHB_Railing* Railing = SpawnTransientRailingTestActor<AEHB_Railing>(World, TestOrigin);
	if (!TestNotNull(TEXT("Building should spawn"), Building)
		|| !TestNotNull(TEXT("Railing should spawn"), Railing))
	{
		DestroyRailingTestActor(Railing);
		DestroyRailingTestActor(Building);
		return false;
	}

	TestTrue(
		TEXT("Linear railing should configure"),
		Railing->ConfigureLinear(
			Building,
			FTransform::Identity,
			FVector::ZeroVector,
			FVector(150.0f, 0.0f, 0.0f),
			1));
	Railing->PostMesh = SamplePostMesh;
	Railing->PostWidth = SourceWidth;
	Railing->PostHeight = SourceSize.Z;
	TestTrue(TEXT("Railing should rebuild with sampled post mesh"), Railing->RebuildRailing());

	FTransform FirstPostTransform;
	TestTrue(
		TEXT("Post instance transform should be readable"),
		Railing->PostMeshComponent && Railing->PostMeshComponent->GetInstanceTransform(0, FirstPostTransform, false));
	TestTrue(
		TEXT("Sampled post mesh should keep source width instead of being scaled by 100 cm fallback"),
		FirstPostTransform.GetScale3D().Equals(FVector::OneVector, 0.001));
	const FVector SourceBase(SourceBounds.GetCenter().X, SourceBounds.GetCenter().Y, SourceBounds.Min.Z);
	TestTrue(TEXT("Offset source pivot is anchored to first post base"),
		FirstPostTransform.TransformPosition(SourceBase).Equals(FVector::ZeroVector, 0.001));
	Railing->PostWidth = SourceWidth * 2;
	Railing->PostHeight = SourceSize.Z * 0.5f;
	TestTrue(TEXT("Sampled post dimensions can change"), Railing->RebuildRailing());
	TestTrue(TEXT("Resized instance readable"), Railing->PostMeshComponent->GetInstanceTransform(0, FirstPostTransform, false));
	TestTrue(TEXT("Horizontal and vertical scaling use their respective source bounds"),
		FirstPostTransform.GetScale3D().Equals(FVector(2, 2, 0.5), 0.001));
	TestTrue(TEXT("Resizing preserves the base anchor despite offset source pivot"),
		FirstPostTransform.TransformPosition(SourceBase).Equals(FVector::ZeroVector, 0.001));

	DestroyRailingTestActor(Railing);
	DestroyRailingTestActor(Building);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEHBRailingConnectedRailEndpointsOverlapTest,
	"EHB.Railing.ConnectedRailEndpointsOverlap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEHBRailingConnectedRailEndpointsOverlapTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("Editor world should be available"), World))
	{
		return false;
	}

	const FVector TestOrigin(3800.0f, 12000.0f, 140000.0f);
	AEHB_Building* Building = SpawnTransientRailingTestActor<AEHB_Building>(World, TestOrigin);
	AEHB_Railing* FirstRailing = SpawnTransientRailingTestActor<AEHB_Railing>(World, TestOrigin);
	AEHB_Railing* SecondRailing = SpawnTransientRailingTestActor<AEHB_Railing>(World, TestOrigin);
	if (!TestNotNull(TEXT("Building should spawn"), Building)
		|| !TestNotNull(TEXT("First railing should spawn"), FirstRailing)
		|| !TestNotNull(TEXT("Second railing should spawn"), SecondRailing))
	{
		DestroyRailingTestActor(SecondRailing);
		DestroyRailingTestActor(FirstRailing);
		DestroyRailingTestActor(Building);
		return false;
	}

	TestTrue(
		TEXT("First railing should configure"),
		FirstRailing->ConfigureLinear(
			Building,
			FTransform::Identity,
			FVector::ZeroVector,
			FVector(150.0f, 0.0f, 0.0f),
			1));
	TestTrue(
		TEXT("Second railing should configure"),
		SecondRailing->ConfigureLinear(
			Building,
			FTransform::Identity,
			FVector(150.0f, 0.0f, 0.0f),
			FVector(300.0f, 0.0f, 0.0f),
			1));

	FirstRailing->PostSpacing = 150.0f;
	FirstRailing->MaxRailSegmentLength = 1000.0f;
	SecondRailing->PostSpacing = 150.0f;
	SecondRailing->MaxRailSegmentLength = 1000.0f;
	SecondRailing->bOmitStartPost = true;
	SecondRailing->StartAnchor.ElementGuid = FirstRailing->ElementGuid;
	SecondRailing->StartAnchor.PostGuid = FirstRailing->GeneratedPosts.Last().PostGuid;

	TestTrue(TEXT("First connected railing should rebuild"), FirstRailing->RebuildRailing());
	TestTrue(TEXT("Second connected railing should rebuild"), SecondRailing->RebuildRailing());
	float FirstMinX = 0.0f;
	float FirstMaxX = 0.0f;
	float SecondMinX = 0.0f;
	float SecondMaxX = 0.0f;
	if (!TestTrue(TEXT("First railing should have generated rail mesh bounds"), GetGeneratedRailXRange(FirstRailing, FirstMinX, FirstMaxX))
		|| !TestTrue(TEXT("Second railing should have generated rail mesh bounds"), GetGeneratedRailXRange(SecondRailing, SecondMinX, SecondMaxX)))
	{
		DestroyRailingTestActor(SecondRailing);
		DestroyRailingTestActor(FirstRailing);
		DestroyRailingTestActor(Building);
		return false;
	}

	TestTrue(
		TEXT("First railing should extend past the shared endpoint"),
		FirstMaxX > 150.0f + 1.0f);
	TestTrue(
		TEXT("Second railing should extend before the shared endpoint"),
		SecondMinX < 150.0f - 1.0f);

	DestroyRailingTestActor(SecondRailing);
	DestroyRailingTestActor(FirstRailing);
	DestroyRailingTestActor(Building);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEHBRailingConnectedRailEndpointsMiterAngleTest,
	"EHB.Railing.ConnectedRailEndpointsMiterAngle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEHBRailingConnectedRailEndpointsMiterAngleTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("Editor world should be available"), World))
	{
		return false;
	}

	const FVector TestOrigin(4100.0f, 12000.0f, 140000.0f);
	AEHB_Building* Building = SpawnTransientRailingTestActor<AEHB_Building>(World, TestOrigin);
	AEHB_Railing* FirstRailing = SpawnTransientRailingTestActor<AEHB_Railing>(World, TestOrigin);
	AEHB_Railing* SecondRailing = SpawnTransientRailingTestActor<AEHB_Railing>(World, TestOrigin);
	if (!TestNotNull(TEXT("Building should spawn"), Building)
		|| !TestNotNull(TEXT("First railing should spawn"), FirstRailing)
		|| !TestNotNull(TEXT("Second railing should spawn"), SecondRailing))
	{
		DestroyRailingTestActor(SecondRailing);
		DestroyRailingTestActor(FirstRailing);
		DestroyRailingTestActor(Building);
		return false;
	}

	const FVector Joint(150.0f, 0.0f, 0.0f);
	TestTrue(
		TEXT("First angled railing should configure"),
		FirstRailing->ConfigureLinear(
			Building,
			FTransform::Identity,
			FVector::ZeroVector,
			Joint,
			1));
	FirstRailing->PostSpacing = 150.0f;
	FirstRailing->MaxRailSegmentLength = 1000.0f;
	TestTrue(TEXT("First angled railing should rebuild before a connected railing exists"), FirstRailing->RebuildRailing());

	TestTrue(
		TEXT("Second angled railing should configure"),
		SecondRailing->ConfigureLinear(
			Building,
			FTransform::Identity,
			Joint,
			FVector(150.0f, 150.0f, 0.0f),
			1));

	SecondRailing->PostSpacing = 150.0f;
	SecondRailing->MaxRailSegmentLength = 1000.0f;
	SecondRailing->StartAnchor.ElementGuid = FirstRailing->ElementGuid;
	SecondRailing->StartAnchor.PostGuid = FirstRailing->GeneratedPosts.Last().PostGuid;

	TestTrue(TEXT("Second angled railing should rebuild and refresh the connected first railing"), SecondRailing->RebuildRailing());

	FBox FirstRailBounds(ForceInit);
	FBox SecondRailBounds(ForceInit);
	if (!TestTrue(TEXT("First angled rail mesh should exist"), GetGeneratedRailBounds(FirstRailing, FirstRailBounds))
		|| !TestTrue(TEXT("Second angled rail mesh should exist"), GetGeneratedRailBounds(SecondRailing, SecondRailBounds)))
	{
		DestroyRailingTestActor(SecondRailing);
		DestroyRailingTestActor(FirstRailing);
		DestroyRailingTestActor(Building);
		return false;
	}

	const float JointTolerance = FirstRailing->RailThickness * 1.5f;
	TestTrue(
		TEXT("First angled rail should not extend deeply past the shared endpoint"),
		FirstRailBounds.Max.X <= Joint.X + JointTolerance
			&& FirstRailBounds.Max.Y <= Joint.Y + JointTolerance);
	TestTrue(
		TEXT("Second angled rail should not extend deeply before the shared endpoint"),
		SecondRailBounds.Min.X >= Joint.X - JointTolerance
			&& SecondRailBounds.Min.Y >= Joint.Y - JointTolerance);
	TestTrue(
		TEXT("First angled rail should still reach the miter joint"),
		FirstRailBounds.Max.X >= Joint.X - JointTolerance
			&& FirstRailBounds.Min.Y <= Joint.Y + JointTolerance);
	TestTrue(
		TEXT("Second angled rail should still reach the miter joint"),
		SecondRailBounds.Min.X <= Joint.X + JointTolerance
			&& SecondRailBounds.Min.Y <= Joint.Y + JointTolerance);

	DestroyRailingTestActor(SecondRailing);
	DestroyRailingTestActor(FirstRailing);
	DestroyRailingTestActor(Building);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEHBRailingConnectedRailEndpointsRequireMatchingSampleTest,
	"EHB.Railing.ConnectedRailEndpointsRequireMatchingSample",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEHBRailingConnectedRailEndpointsRequireMatchingSampleTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("Editor world should be available"), World))
	{
		return false;
	}

	UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("Default cube mesh should be available"), CubeMesh))
	{
		return false;
	}

	UDataTable* SampleTable = NewObject<UDataTable>(GetTransientPackage(), NAME_None, RF_Transient);
	SampleTable->RowStruct = FEHBRailingMeshData::StaticStruct();

	FEHBRailingMeshData FirstSampleRow;
	FirstSampleRow.TemplateMetadata.TemplateGuid = FGuid::NewGuid();
	FirstSampleRow.RailMesh.SourceStaticMesh = CubeMesh;
	FirstSampleRow.RailMesh.SourceMeshName = CubeMesh->GetFName();
	FirstSampleRow.RecommendedRailHeight = 100.0f;
	FirstSampleRow.RecommendedRailThickness = 8.0f;
	FirstSampleRow.RecommendedMaxRailSegmentLength = 1000.0f;
	SampleTable->AddRow(FName(TEXT("SampleA")), FirstSampleRow);

	FEHBRailingMeshData SecondSampleRow = FirstSampleRow;
	SecondSampleRow.TemplateMetadata.TemplateGuid = FGuid::NewGuid();
	SampleTable->AddRow(FName(TEXT("SampleB")), SecondSampleRow);

	const FVector TestOrigin(4300.0f, 12000.0f, 140000.0f);
	AEHB_Building* Building = SpawnTransientRailingTestActor<AEHB_Building>(World, TestOrigin);
	AEHB_Railing* FirstSameSampleRailing = SpawnTransientRailingTestActor<AEHB_Railing>(World, TestOrigin);
	AEHB_Railing* SecondSameSampleRailing = SpawnTransientRailingTestActor<AEHB_Railing>(World, TestOrigin);
	AEHB_Railing* FirstDifferentSampleRailing = SpawnTransientRailingTestActor<AEHB_Railing>(World, TestOrigin);
	AEHB_Railing* SecondDifferentSampleRailing = SpawnTransientRailingTestActor<AEHB_Railing>(World, TestOrigin);
	if (!TestNotNull(TEXT("Building should spawn"), Building)
		|| !TestNotNull(TEXT("First same-sample railing should spawn"), FirstSameSampleRailing)
		|| !TestNotNull(TEXT("Second same-sample railing should spawn"), SecondSameSampleRailing)
		|| !TestNotNull(TEXT("First different-sample railing should spawn"), FirstDifferentSampleRailing)
		|| !TestNotNull(TEXT("Second different-sample railing should spawn"), SecondDifferentSampleRailing))
	{
		DestroyRailingTestActor(SecondDifferentSampleRailing);
		DestroyRailingTestActor(FirstDifferentSampleRailing);
		DestroyRailingTestActor(SecondSameSampleRailing);
		DestroyRailingTestActor(FirstSameSampleRailing);
		DestroyRailingTestActor(Building);
		return false;
	}

	auto ConfigurePair = [this, Building, SampleTable](
		AEHB_Railing* FirstRailing,
		AEHB_Railing* SecondRailing,
		FName FirstRowName,
		FName SecondRowName,
		float YOffset)
	{
		if (!FirstRailing || !SecondRailing)
		{
			return false;
		}

		const FVector Start(0.0f, YOffset, 0.0f);
		const FVector Joint(150.0f, YOffset, 0.0f);
		const FVector End(300.0f, YOffset, 0.0f);
		if (!FirstRailing->ConfigureLinear(Building, FTransform::Identity, Start, Joint, 1)
			|| !SecondRailing->ConfigureLinear(Building, FTransform::Identity, Joint, End, 1))
		{
			return false;
		}
		if (!FirstRailing->ConfigureFromSampledRailingRow(SampleTable, FirstRowName, false)
			|| !SecondRailing->ConfigureFromSampledRailingRow(SampleTable, SecondRowName, false))
		{
			return false;
		}

		FirstRailing->PostSpacing = 150.0f;
		FirstRailing->MaxRailSegmentLength = 1000.0f;
		SecondRailing->PostSpacing = 150.0f;
		SecondRailing->MaxRailSegmentLength = 1000.0f;
		SecondRailing->bOmitStartPost = true;
		SecondRailing->StartAnchor.ElementGuid = FirstRailing->ElementGuid;
		SecondRailing->StartAnchor.PostGuid = FirstRailing->GeneratedPosts.IsEmpty()
			? FGuid()
			: FirstRailing->GeneratedPosts.Last().PostGuid;
		return FirstRailing->RebuildRailing() && SecondRailing->RebuildRailing();
	};

	TestTrue(
		TEXT("Same sampled railings should configure"),
		ConfigurePair(
			FirstSameSampleRailing,
			SecondSameSampleRailing,
			FName(TEXT("SampleA")),
			FName(TEXT("SampleA")),
			0.0f));
	TestTrue(
		TEXT("Different sampled railings should configure"),
		ConfigurePair(
			FirstDifferentSampleRailing,
			SecondDifferentSampleRailing,
			FName(TEXT("SampleA")),
			FName(TEXT("SampleB")),
			300.0f));

	float SameSampleFirstMinX = 0.0f;
	float SameSampleFirstMaxX = 0.0f;
	float SameSampleSecondMinX = 0.0f;
	float SameSampleSecondMaxX = 0.0f;
	float DifferentSampleFirstMinX = 0.0f;
	float DifferentSampleFirstMaxX = 0.0f;
	float DifferentSampleSecondMinX = 0.0f;
	float DifferentSampleSecondMaxX = 0.0f;
	if (!TestTrue(TEXT("Same-sample first rail should exist"), GetGeneratedRailXRange(FirstSameSampleRailing, SameSampleFirstMinX, SameSampleFirstMaxX))
		|| !TestTrue(TEXT("Same-sample second rail should exist"), GetGeneratedRailXRange(SecondSameSampleRailing, SameSampleSecondMinX, SameSampleSecondMaxX))
		|| !TestTrue(TEXT("Different-sample first rail should exist"), GetGeneratedRailXRange(FirstDifferentSampleRailing, DifferentSampleFirstMinX, DifferentSampleFirstMaxX))
		|| !TestTrue(TEXT("Different-sample second rail should exist"), GetGeneratedRailXRange(SecondDifferentSampleRailing, DifferentSampleSecondMinX, DifferentSampleSecondMaxX)))
	{
		DestroyRailingTestActor(SecondDifferentSampleRailing);
		DestroyRailingTestActor(FirstDifferentSampleRailing);
		DestroyRailingTestActor(SecondSameSampleRailing);
		DestroyRailingTestActor(FirstSameSampleRailing);
		DestroyRailingTestActor(Building);
		return false;
	}

	TestTrue(
		TEXT("Same sampled railing should extend past the shared endpoint"),
		SameSampleFirstMaxX > 150.0f + 1.0f);
	TestTrue(
		TEXT("Same sampled railing should extend before the shared endpoint"),
		SameSampleSecondMinX < 150.0f - 1.0f);
	TestTrue(
		TEXT("Different sampled railing should stop at the shared endpoint"),
		FMath::IsNearlyEqual(DifferentSampleFirstMaxX, 150.0f, 0.1f));
	TestTrue(
		TEXT("Different sampled railing should start at the shared endpoint"),
		FMath::IsNearlyEqual(DifferentSampleSecondMinX, 150.0f, 0.1f));

	DestroyRailingTestActor(SecondDifferentSampleRailing);
	DestroyRailingTestActor(FirstDifferentSampleRailing);
	DestroyRailingTestActor(SecondSameSampleRailing);
	DestroyRailingTestActor(FirstSameSampleRailing);
	DestroyRailingTestActor(Building);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEHBRailingSinglePostSampleOverrideTest,
	"EHB.Railing.SinglePostSampleOverride",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEHBRailingSinglePostSampleOverrideTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("Editor world should be available"), World))
	{
		return false;
	}

	UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("Default cube mesh should be available"), CubeMesh))
	{
		return false;
	}

	const FVector TestOrigin(4400.0f, 12000.0f, 140000.0f);
	AEHB_Building* Building = SpawnTransientRailingTestActor<AEHB_Building>(World, TestOrigin);
	AEHB_Railing* Railing = SpawnTransientRailingTestActor<AEHB_Railing>(World, TestOrigin);
	if (!TestNotNull(TEXT("Building should spawn"), Building)
		|| !TestNotNull(TEXT("Railing should spawn"), Railing))
	{
		DestroyRailingTestActor(Railing);
		DestroyRailingTestActor(Building);
		return false;
	}

	TestTrue(
		TEXT("Linear railing should configure"),
		Railing->ConfigureLinear(
			Building,
			FTransform::Identity,
			FVector::ZeroVector,
			FVector(300.0f, 0.0f, 0.0f),
			1));
	Railing->PostSpacing = 150.0f;
	Railing->MaxRailSegmentLength = 1000.0f;
	TestTrue(TEXT("Linear railing should rebuild with three posts"), Railing->RebuildRailing());
	TestEqual(TEXT("Railing should have three generated posts"), Railing->GeneratedPosts.Num(), 3);

	UDataTable* SampleTable = NewObject<UDataTable>(GetTransientPackage(), NAME_None, RF_Transient);
	SampleTable->RowStruct = FEHBRailingMeshData::StaticStruct();

	FEHBRailingMeshData WidePostRow;
	WidePostRow.PostMesh.SourceStaticMesh = CubeMesh;
	WidePostRow.PostMesh.SourceMeshName = CubeMesh->GetFName();
	WidePostRow.RecommendedPostWidth = 24.0f;
	WidePostRow.RecommendedPostHeight = 140.0f;
	WidePostRow.RecommendedPostSpacing = 150.0f;
	SampleTable->AddRow(FName(TEXT("WidePost")), WidePostRow);

	const FGuid MiddlePostGuid = Railing->GeneratedPosts[1].PostGuid;
	TestTrue(
		TEXT("Single post sample should apply"),
		Railing->ApplyPostMeshSampleToPost(MiddlePostGuid, SampleTable, FName(TEXT("WidePost")), false));
	TestEqual(TEXT("Single post override should be stored"), Railing->PostMeshOverrides.Num(), 1);
	TestEqual(TEXT("Default post HISM should skip the overridden post"), Railing->PostMeshComponent->GetInstanceCount(), 2);
	TestEqual(TEXT("One override component should be created"), Railing->PostOverrideMeshComponents.Num(), 1);

	FGuid OverrideComponentGuid;
	TestTrue(
		TEXT("Override component should map back to the post guid"),
		Railing->GetPostGuidForOverrideComponent(Railing->PostOverrideMeshComponents[0], OverrideComponentGuid));
	TestEqual(TEXT("Override component guid should match the target post"), OverrideComponentGuid, MiddlePostGuid);

	TestTrue(
		TEXT("All posts sample should apply"),
		Railing->ApplyPostMeshSampleToAllPosts(SampleTable, FName(TEXT("WidePost")), false));
	TestEqual(TEXT("All posts sample should clear single post overrides"), Railing->PostMeshOverrides.Num(), 0);
	TestEqual(TEXT("All posts sample should remove override components"), Railing->PostOverrideMeshComponents.Num(), 0);
	TestEqual(
		TEXT("All posts sample should restore one HISM instance per generated post"),
		Railing->PostMeshComponent->GetInstanceCount(),
		Railing->GeneratedPosts.Num());

	DestroyRailingTestActor(Railing);
	DestroyRailingTestActor(Building);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEHBRailingPartialSampleUsesDefaultsTest,
	"EHB.Railing.PartialSampleUsesDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEHBRailingPartialSampleUsesDefaultsTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("Editor world should be available"), World))
	{
		return false;
	}

	UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("Default cube mesh should be available"), CubeMesh))
	{
		return false;
	}

	const FVector TestOrigin(4000.0f, 12000.0f, 140000.0f);
	AEHB_Building* Building = SpawnTransientRailingTestActor<AEHB_Building>(World, TestOrigin);
	AEHB_Railing* Railing = SpawnTransientRailingTestActor<AEHB_Railing>(World, TestOrigin);
	if (!TestNotNull(TEXT("Building should spawn"), Building)
		|| !TestNotNull(TEXT("Railing should spawn"), Railing))
	{
		DestroyRailingTestActor(Railing);
		DestroyRailingTestActor(Building);
		return false;
	}

	TestTrue(
		TEXT("Linear railing should configure before sample replacement"),
		Railing->ConfigureLinear(
			Building,
			FTransform::Identity,
			FVector::ZeroVector,
			FVector(300.0f, 0.0f, 0.0f),
			1));

	UDataTable* SampleTable = NewObject<UDataTable>(GetTransientPackage(), NAME_None, RF_Transient);
	SampleTable->RowStruct = FEHBRailingMeshData::StaticStruct();

	FEHBRailingMeshData PostOnlyRow;
	PostOnlyRow.PostMesh.SourceStaticMesh = CubeMesh;
	PostOnlyRow.PostMesh.SourceMeshName = CubeMesh->GetFName();
	PostOnlyRow.RecommendedPostWidth = 16.0f;
	PostOnlyRow.RecommendedPostHeight = 140.0f;
	PostOnlyRow.RecommendedPostSpacing = 95.0f;
	SampleTable->AddRow(FName(TEXT("PostOnly")), PostOnlyRow);

	Railing->RailThickness = 33.0f;
	TestTrue(
		TEXT("Post-only railing sample should apply"),
		Railing->ConfigureFromSampledRailingRow(SampleTable, FName(TEXT("PostOnly")), false));
	TestEqual(TEXT("Post-only sample should apply sampled post width"), Railing->PostWidth, 16.0f);
	TestEqual(TEXT("Post-only sample should reset rail thickness to default"), Railing->RailThickness, 8.0f);
	TestEqual(TEXT("Post-only sample should reset max rail segment length to default"), Railing->MaxRailSegmentLength, 120.0f);

	FEHBRailingMeshData RailOnlyRow;
	RailOnlyRow.RailMesh.SourceStaticMesh = CubeMesh;
	RailOnlyRow.RailMesh.SourceMeshName = CubeMesh->GetFName();
	RailOnlyRow.RecommendedRailHeight = 108.0f;
	RailOnlyRow.RecommendedRailThickness = 18.0f;
	RailOnlyRow.RecommendedMaxRailSegmentLength = 180.0f;
	SampleTable->AddRow(FName(TEXT("RailOnly")), RailOnlyRow);

	Railing->PostWidth = 42.0f;
	TestTrue(
		TEXT("Rail-only railing sample should apply"),
		Railing->ConfigureFromSampledRailingRow(SampleTable, FName(TEXT("RailOnly")), false));
	TestEqual(TEXT("Rail-only sample should reset post width to default"), Railing->PostWidth, 8.0f);
	TestEqual(TEXT("Rail-only sample should apply sampled rail thickness"), Railing->RailThickness, 18.0f);
	TestEqual(TEXT("Rail-only sample should apply sampled max rail segment length"), Railing->MaxRailSegmentLength, 180.0f);

	DestroyRailingTestActor(Railing);
	DestroyRailingTestActor(Building);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEHBRailingPersistentEndpointsTest,
	"EHB.Railing.PersistentLinearEndpoints",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEHBRailingPersistentEndpointsTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	AEHB_Railing* Railing = SpawnTransientRailingTestActor<AEHB_Railing>(World, FVector(0, 12000, 160000));
	if (!TestNotNull(TEXT("Railing spawned"), Railing)) return false;
	Railing->LinearStart = FVector::ZeroVector;
	Railing->LinearEnd = FVector(360, 0, 0);
	Railing->PostSpacing = 90;
	TestTrue(TEXT("Initial rebuild"), Railing->RebuildRailing());
	const FGuid Start = Railing->GeneratedPosts[0].PostGuid;
	const FGuid End = Railing->GeneratedPosts.Last().PostGuid;
	// Simulate a legacy asset: generated posts exist, new persistent fields do not.
	Railing->LinearStartPostGuid.Invalidate();
	Railing->LinearEndPostGuid.Invalidate();
	Railing->LinearEnd = FVector(720, 0, 0);
	TestTrue(TEXT("Legacy endpoint migration while extending"), Railing->RebuildRailing());
	TestEqual(TEXT("Start retains legacy identity"), Railing->GeneratedPosts[0].PostGuid, Start);
	TestEqual(TEXT("End follows endpoint role, not old distance"), Railing->GeneratedPosts.Last().PostGuid, End);
	TSet<FGuid> Seen;
	for (const FEHBRailingPost& Post : Railing->GeneratedPosts)
	{
		TestFalse(TEXT("Each generated post has a unique identity"), Seen.Contains(Post.PostGuid));
		Seen.Add(Post.PostGuid);
		if (FMath::IsNearlyEqual(Post.Distance, 360.0f))
			TestTrue(TEXT("Interior post cannot steal old endpoint identity"), Post.PostGuid != End);
	}
	TArray<uint8> Serialized;
	FObjectWriter Writer(Railing, Serialized);
	Railing->LinearStartPostGuid.Invalidate();
	Railing->LinearEndPostGuid.Invalidate();
	FObjectReader Reader(Railing, Serialized);
	TestEqual(TEXT("Serialized start identity restored"), Railing->LinearStartPostGuid, Start);
	TestEqual(TEXT("Serialized end identity restored"), Railing->LinearEndPostGuid, End);
	Railing->GeneratedPosts.Reset();
	Railing->LinearEnd = FVector(500, 100, 0);
	Railing->PostSpacing = 37;
	Railing->bOmitEndPost = true;
	TestTrue(TEXT("Rebuild without cache after direction and spacing edits"), Railing->RebuildRailing());
	TestEqual(TEXT("Start survives missing generated cache"), Railing->GeneratedPosts[0].PostGuid, Start);
	TestEqual(TEXT("Omitted end still has persistent identity"), Railing->GeneratedPosts.Last().PostGuid, End);
	TestTrue(TEXT("End instance remains suppressed"), Railing->GeneratedPosts.Last().bSuppressInstance);
	// A gate can consume the legacy start post. The remaining end must not
	// be migrated into both endpoint roles.
	Railing->GeneratedPosts.RemoveAll([](const FEHBRailingPost& Post)
	{
		return FMath::IsNearlyZero(Post.Distance);
	});
	Railing->LinearStartPostGuid.Invalidate();
	Railing->LinearEndPostGuid.Invalidate();
	TestTrue(TEXT("Migrate legacy cache missing start"), Railing->RebuildRailing());
	TestEqual(TEXT("Remaining end preserves its role"), Railing->GeneratedPosts.Last().PostGuid, End);
	TestTrue(TEXT("Missing start cannot borrow end identity"), Railing->GeneratedPosts[0].PostGuid != End);
	DestroyRailingTestActor(Railing);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBRailingWallEndpointSnapTest,
	"EHB.Railing.WallEndpointSnap", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEHBRailingWallEndpointSnapTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	const FVector Origin(0, 12000, 170000);
	AEHB_Building* Building = SpawnTransientRailingTestActor<AEHB_Building>(World, Origin);
	AEHB_Pillar* Start = SpawnTransientRailingTestActor<AEHB_Pillar>(World, Origin);
	AEHB_Pillar* End = SpawnTransientRailingTestActor<AEHB_Pillar>(World, Origin);
	if (!Building || !Start || !End)
	{
		DestroyRailingTestActor(End); DestroyRailingTestActor(Start); DestroyRailingTestActor(Building);
		return false;
	}
	Start->AttachToBuilding(Building, FTransform(FVector::ZeroVector));
	End->AttachToBuilding(Building, FTransform(FVector(50, 0, 0)));
	AEHB_Wall* Wall = Building->ConnectPillars(Start, End, 300, 5);
	if (TestNotNull(TEXT("Wall created"), Wall))
	{
		Wall->SetFlags(RF_Transient);
		const FGuid StartId = Start->ElementGuid;
		const FGuid EndId = End->ElementGuid;
		const float Length = FVector::Dist2D(Wall->LocalStart, Wall->LocalEnd);
		auto At = [Wall, Length](float Ratio) { return Wall->GetWorldLocationOnCenterAxisAtDistance(Length * Ratio, 0); };
		TestTrue(TEXT("Overlapping snap ranges choose nearest end"),
			EHBRailingCreationSnap::FindWallEndpointPillar(Wall, Building, At(0.6f), 40) == End);
		TestTrue(TEXT("Start still resolves"),
			EHBRailingCreationSnap::FindWallEndpointPillar(Wall, Building, At(0.1f), 10) == Start);
		TestNull(TEXT("Middle outside range creates no endpoint attachment"),
			EHBRailingCreationSnap::FindWallEndpointPillar(Wall, Building, At(0.5f), 1));
		TestNull(TEXT("Missing building rejected"),
			EHBRailingCreationSnap::FindWallEndpointPillar(Wall, nullptr, At(0), 40));
		Building->SetActorScale3D(FVector(5, 2, 1));
		TestNull(TEXT("Snap tolerance remains world centimeters under scale"),
			EHBRailingCreationSnap::FindWallEndpointPillar(Wall, Building, At(0.5f), Length * 0.6f));
		TestEqual(TEXT("Start column identity unchanged"), Start->ElementGuid, StartId);
		TestEqual(TEXT("End column identity unchanged"), End->ElementGuid, EndId);
		TestEqual(TEXT("Wall retains start column"), Wall->StartPillarGuid, StartId);
		TestEqual(TEXT("Wall retains end column"), Wall->EndPillarGuid, EndId);
	}
	DestroyRailingTestActor(Wall); DestroyRailingTestActor(End);
	DestroyRailingTestActor(Start); DestroyRailingTestActor(Building);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
