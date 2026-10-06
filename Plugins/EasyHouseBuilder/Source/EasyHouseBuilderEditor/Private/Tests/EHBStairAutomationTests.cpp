// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Actors/EHB_Stair.h"
#include "Components/EHBGeneratedMeshComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"

#include "Editor.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"

namespace
{
	AEHB_Stair* SpawnTransientStairTestActor(UWorld* World, const FVector& WorldLocation)
	{
		if (!World)
		{
			return nullptr;
		}

		FActorSpawnParameters SpawnParameters;
		SpawnParameters.ObjectFlags |= RF_Transient;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<AEHB_Stair>(
			AEHB_Stair::StaticClass(),
			WorldLocation,
			FRotator::ZeroRotator,
			SpawnParameters);
	}

	void DestroyStairTestActor(AActor* Actor)
	{
		if (Actor && !Actor->IsActorBeingDestroyed())
		{
			Actor->Destroy();
		}
	}

	bool FindHighestVertexZAtXY(
		const FEHBMeshSection& Section,
		float X,
		float Y,
		float& OutZ,
		float Tolerance = 0.2f)
	{
		bool bFound = false;
		OutZ = -FLT_MAX;
		for (const FEHBMeshVertex& Vertex : Section.ProcVertexBuffer)
		{
			if (FMath::Abs(Vertex.Position.X - X) <= Tolerance
				&& FMath::Abs(Vertex.Position.Y - Y) <= Tolerance)
			{
				OutZ = FMath::Max(OutZ, static_cast<float>(Vertex.Position.Z));
				bFound = true;
			}
		}
		return bFound;
	}

	bool FindLowestVertexZAtXY(
		const FEHBMeshSection& Section,
		float X,
		float Y,
		float& OutZ,
		float Tolerance = 0.2f)
	{
		bool bFound = false;
		OutZ = FLT_MAX;
		for (const FEHBMeshVertex& Vertex : Section.ProcVertexBuffer)
		{
			if (FMath::Abs(Vertex.Position.X - X) <= Tolerance
				&& FMath::Abs(Vertex.Position.Y - Y) <= Tolerance)
			{
				OutZ = FMath::Min(OutZ, static_cast<float>(Vertex.Position.Z));
				bFound = true;
			}
		}
		return bFound;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEHBStairSideBoardSlopeTest,
	"EHB.Stair.SideBoardSlope",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEHBStairSideBoardSlopeTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("Editor world should be available"), World))
	{
		return false;
	}

	AEHB_Stair* Stair = SpawnTransientStairTestActor(World, FVector(0.0f, 16000.0f, 140000.0f));
	if (!TestNotNull(TEXT("Stair should spawn"), Stair))
	{
		return false;
	}

	Stair->StairData.bUseActualDimensions = true;
	Stair->StairData.TreadDepth = 30.0f;
	Stair->StairData.StairWidth = 140.0f;
	Stair->StairData.StairHeight = 280.0f;
	Stair->StairData.NosingLength = 2.0f;
	Stair->StairData.SideProtruding = 1.5f;
	Stair->StairData.SideBoardTopOffset = 6.0f;
	Stair->StairData.bBottomStepControlInitialized = false;
	Stair->StairData.bGenerateTreads = false;
	Stair->StairData.bFillRisers = false;
	Stair->StairData.bGenerateSides = true;
	Stair->StairData.bGenerateSideGuards = false;

	TestTrue(TEXT("Stair should rebuild side mesh"), Stair->RebuildStairMesh());
	if (!TestNotNull(TEXT("Side mesh component should exist"), Stair->SideMeshComponent.Get()))
	{
		DestroyStairTestActor(Stair);
		return false;
	}

	const FEHBMeshSection* SideSection = Stair->SideMeshComponent->GetProcMeshSection(0);
	if (!TestNotNull(TEXT("Side mesh section should exist"), SideSection))
	{
		DestroyStairTestActor(Stair);
		return false;
	}

	int32 StepCount = 0;
	float StepHeight = 0.0f;
	if (!TestTrue(TEXT("Stair should calculate steps"), Stair->CalculateStairs(Stair->StairData.StairHeight, StepCount, StepHeight)))
	{
		DestroyStairTestActor(Stair);
		return false;
	}

	const float StepRun = Stair->StairData.TreadDepth - Stair->StairData.NosingLength;
	const float StairLength = Stair->StairData.TreadDepth + StepRun * static_cast<float>(FMath::Max(0, StepCount - 1));
	const float HalfWidth = Stair->StairData.StairWidth * 0.5f;
	const float MaxSideBoardTopZ = Stair->StairData.StairHeight + StepHeight;
	const float ExpectedRawSlope = -StepHeight / StepRun;
	bool bFoundLandingHeightIntersection = false;
	bool bFoundGroundIntersection = false;

	for (const FEHBMeshVertex& Vertex : SideSection->ProcVertexBuffer)
	{
		TestTrue(
			TEXT("Side board should not generate geometry above the attached landing height"),
			Vertex.Position.Z <= MaxSideBoardTopZ + 0.1f);
	}

	for (int32 StepIndex = 0; StepIndex < StepCount; ++StepIndex)
	{
		const float BackDistance = StepRun * static_cast<float>(StepIndex);
		const float FrontDistance = FMath::Min(
			StairLength,
			BackDistance + StepRun + Stair->StairData.SideProtruding);
		const float StepTopZ = Stair->StairData.StairHeight - StepHeight * static_cast<float>(StepIndex);
		const float FrontAlpha = (FrontDistance - BackDistance) / StepRun;
		const float UnclampedExpectedFrontZ = StepTopZ + Stair->StairData.SideBoardTopOffset;
		const float UnclampedExpectedBackZ = UnclampedExpectedFrontZ + StepHeight * FrontAlpha;
		const bool bBackAboveLanding = UnclampedExpectedBackZ > MaxSideBoardTopZ + UE_KINDA_SMALL_NUMBER;
		const bool bFrontAboveLanding = UnclampedExpectedFrontZ > MaxSideBoardTopZ + UE_KINDA_SMALL_NUMBER;

		for (const float DirectionSign : { -1.0f, 1.0f })
		{
			const float InnerY = DirectionSign * HalfWidth;
			const FString Prefix = FString::Printf(TEXT("Step %d side %.0f"), StepIndex, DirectionSign);

			if (bBackAboveLanding && bFrontAboveLanding)
			{
				continue;
			}

			if (bBackAboveLanding)
			{
				if (StepIndex == 0)
				{
					float ExtensionZ = 0.0f;
					const FVector ExtensionPoint = Stair->TransformStraightStairLocalPointToPath(
						FVector(-Stair->StairData.TreadDepth, InnerY, MaxSideBoardTopZ));
					TestTrue(TEXT("Landing extension remains present after splitting cap at platform edge"),
						FindHighestVertexZAtXY(*SideSection, ExtensionPoint.X, ExtensionPoint.Y, ExtensionZ));
					TestTrue(TEXT("Landing extension remains level with platform"),
						FMath::IsNearlyEqual(ExtensionZ, MaxSideBoardTopZ, 0.1f));
				}
				const float CrossingAlpha = FMath::Clamp(
					(MaxSideBoardTopZ - UnclampedExpectedBackZ) / (UnclampedExpectedFrontZ - UnclampedExpectedBackZ),
					0.0f,
					1.0f);
				const float CrossingDistance = FMath::Lerp(BackDistance, FrontDistance, CrossingAlpha);
				float CapStartZ = 0.0f;
				float CrossingZ = 0.0f;
				float FrontZ = 0.0f;
				const FVector ExpectedCapStartPoint = Stair->TransformStraightStairLocalPointToPath(
					FVector(0.0f, InnerY, MaxSideBoardTopZ));
				const FVector ExpectedCrossingPoint = Stair->TransformStraightStairLocalPointToPath(
					FVector(CrossingDistance, InnerY, MaxSideBoardTopZ));
				const FVector ExpectedFrontPoint = Stair->TransformStraightStairLocalPointToPath(
					FVector(FrontDistance, InnerY, UnclampedExpectedFrontZ));
				TestTrue(
					*FString::Printf(TEXT("%s should start the horizontal cap at the upper landing edge"), *Prefix),
					FindHighestVertexZAtXY(*SideSection, ExpectedCapStartPoint.X, ExpectedCapStartPoint.Y, CapStartZ));
				TestTrue(
					*FString::Printf(TEXT("%s cap start should match slab height"), *Prefix),
					FMath::IsNearlyEqual(CapStartZ, MaxSideBoardTopZ, 0.1f));
				TestTrue(
					*FString::Printf(TEXT("%s should insert a landing-height side board vertex"), *Prefix),
					FindHighestVertexZAtXY(*SideSection, ExpectedCrossingPoint.X, ExpectedCrossingPoint.Y, CrossingZ));
				TestTrue(
					*FString::Printf(TEXT("%s landing-height vertex should match slab height"), *Prefix),
					FMath::IsNearlyEqual(CrossingZ, MaxSideBoardTopZ, 0.1f));

				if (FrontDistance - CrossingDistance > 0.1f)
				{
					TestTrue(
						*FString::Printf(TEXT("%s should continue the side board below the landing-height intersection"), *Prefix),
						FindHighestVertexZAtXY(*SideSection, ExpectedFrontPoint.X, ExpectedFrontPoint.Y, FrontZ));
					TestTrue(
						*FString::Printf(TEXT("%s lower side board front should keep the shifted profile"), *Prefix),
						FMath::IsNearlyEqual(FrontZ, UnclampedExpectedFrontZ, 0.1f));

					const float ActualLowerSlope = (FrontZ - CrossingZ) / FMath::Max(UE_SMALL_NUMBER, FrontDistance - CrossingDistance);
					TestTrue(
						*FString::Printf(TEXT("%s lower side board edge should keep the original slope"), *Prefix),
						FMath::IsNearlyEqual(ActualLowerSlope, ExpectedRawSlope, 0.01f));
				}
				bFoundLandingHeightIntersection = true;
			}
			else
			{
				float BackZ = 0.0f;
				float FrontZ = 0.0f;
				const FVector ExpectedBackPoint = Stair->TransformStraightStairLocalPointToPath(
					FVector(BackDistance, InnerY, UnclampedExpectedBackZ));
				const FVector ExpectedFrontPoint = Stair->TransformStraightStairLocalPointToPath(
					FVector(FrontDistance, InnerY, UnclampedExpectedFrontZ));
				TestTrue(
					*FString::Printf(TEXT("%s should have a back top vertex"), *Prefix),
					FindHighestVertexZAtXY(*SideSection, ExpectedBackPoint.X, ExpectedBackPoint.Y, BackZ));
				TestTrue(
					*FString::Printf(TEXT("%s should have a front top vertex"), *Prefix),
					FindHighestVertexZAtXY(*SideSection, ExpectedFrontPoint.X, ExpectedFrontPoint.Y, FrontZ));
				TestTrue(
					*FString::Printf(TEXT("%s back offset should match tread"), *Prefix),
					FMath::IsNearlyEqual(BackZ, UnclampedExpectedBackZ, 0.1f));
				TestTrue(
					*FString::Printf(TEXT("%s front offset should keep the shifted side board profile"), *Prefix),
					FMath::IsNearlyEqual(FrontZ, UnclampedExpectedFrontZ, 0.1f));

				const float ActualSlope = (FrontZ - BackZ) / FMath::Max(UE_SMALL_NUMBER, FrontDistance - BackDistance);
				TestTrue(
					*FString::Printf(TEXT("%s top edge should follow the expected side board profile"), *Prefix),
					FMath::IsNearlyEqual(ActualSlope, ExpectedRawSlope, 0.01f));
			}

			const float GeneratedBackTopZ = bBackAboveLanding
				? MaxSideBoardTopZ
				: UnclampedExpectedBackZ;
			const float GeneratedFrontTopZ = bFrontAboveLanding
				? MaxSideBoardTopZ
				: UnclampedExpectedFrontZ;
			const float BackRawBottomZ = GeneratedBackTopZ - Stair->StairData.SideBoardHeight;
			const float FrontRawBottomZ = GeneratedFrontTopZ - Stair->StairData.SideBoardHeight;
			if (BackRawBottomZ > UE_KINDA_SMALL_NUMBER && FrontRawBottomZ < -UE_KINDA_SMALL_NUMBER)
			{
				const float GroundCrossingAlpha = FMath::Clamp(
					(0.0f - BackRawBottomZ) / (FrontRawBottomZ - BackRawBottomZ),
					0.0f,
					1.0f);
				const float GroundCrossingDistance = FMath::Lerp(BackDistance, FrontDistance, GroundCrossingAlpha);
				float GroundCrossingZ = 0.0f;
				float GroundFrontZ = 0.0f;
				const FVector ExpectedGroundCrossingPoint = Stair->TransformStraightStairLocalPointToPath(
					FVector(GroundCrossingDistance, InnerY, 0.0f));
				const FVector ExpectedGroundFrontPoint = Stair->TransformStraightStairLocalPointToPath(
					FVector(FrontDistance, InnerY, 0.0f));
				TestTrue(
					*FString::Printf(TEXT("%s lower side board should extend to the ground crossing"), *Prefix),
					FindLowestVertexZAtXY(*SideSection, ExpectedGroundCrossingPoint.X, ExpectedGroundCrossingPoint.Y, GroundCrossingZ));
				TestTrue(
					*FString::Printf(TEXT("%s ground crossing should sit on the ground"), *Prefix),
					FMath::IsNearlyEqual(GroundCrossingZ, 0.0f, 0.1f));
				TestTrue(
					*FString::Printf(TEXT("%s lower side board should continue along the ground after crossing"), *Prefix),
					FindLowestVertexZAtXY(*SideSection, ExpectedGroundFrontPoint.X, ExpectedGroundFrontPoint.Y, GroundFrontZ));
				TestTrue(
					*FString::Printf(TEXT("%s ground continuation should stay on the ground"), *Prefix),
					FMath::IsNearlyEqual(GroundFrontZ, 0.0f, 0.1f));
				bFoundGroundIntersection = true;
			}
		}
	}

	TestTrue(TEXT("Side board should split at the landing height before turning horizontal"), bFoundLandingHeightIntersection);
	TestTrue(TEXT("Side board lower edge should split at the ground before continuing flat"), bFoundGroundIntersection);

	DestroyStairTestActor(Stair);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEHBStairEmbeddedRailingTest,
	"EHB.Stair.EmbeddedRailing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEHBStairEmbeddedRailingTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("Editor world should be available"), World))
	{
		return false;
	}

	AEHB_Stair* Stair = SpawnTransientStairTestActor(World, FVector(0.0f, 17000.0f, 140000.0f));
	if (!TestNotNull(TEXT("Stair should spawn"), Stair))
	{
		return false;
	}

	Stair->StairData.bUseActualDimensions = true;
	Stair->StairData.TreadDepth = 30.0f;
	Stair->StairData.StairWidth = 140.0f;
	Stair->StairData.StairHeight = 280.0f;
	Stair->StairData.NosingLength = 2.0f;
	Stair->StairData.bGenerateRailing = true;
	Stair->StairData.bGenerateLeftRailing = true;
	Stair->StairData.bGenerateRightRailing = false;
	Stair->StairData.RailingStepsPerPost = 2;
	Stair->StairData.RailingEdgeInset = 10.0f;
	Stair->StairData.RailingPostForwardOffset = 0.0f;
	Stair->StairData.RailingMaxRailSegmentLength = 10.0f;

	TestTrue(TEXT("Stair should rebuild embedded railing meshes"), Stair->RebuildStairMesh());
	TestNotNull(TEXT("Left embedded railing post component should exist"), Stair->LeftRailingPostMeshComponent.Get());
	TestNotNull(TEXT("Right embedded railing post component should exist"), Stair->RightRailingPostMeshComponent.Get());
	TestNotNull(TEXT("Left embedded railing rail component should exist"), Stair->LeftRailingRailMeshComponent.Get());
	TestNotNull(TEXT("Right embedded railing rail component should exist"), Stair->RightRailingRailMeshComponent.Get());
	TestTrue(
		TEXT("Left embedded railing should generate post instances"),
		Stair->LeftRailingPostMeshComponent && Stair->LeftRailingPostMeshComponent->GetInstanceCount() > 0);
	TestEqual(
		TEXT("Right embedded railing should stay empty when disabled"),
		Stair->RightRailingPostMeshComponent ? Stair->RightRailingPostMeshComponent->GetInstanceCount() : 0,
		0);

	const FEHBMeshSection* LeftRailSection = Stair->LeftRailingRailMeshComponent
		? Stair->LeftRailingRailMeshComponent->GetProcMeshSection(0)
		: nullptr;
	TestTrue(
		TEXT("Left embedded railing should generate one rail mesh section"),
		LeftRailSection && LeftRailSection->ProcVertexBuffer.Num() > 0 && LeftRailSection->ProcIndexBuffer.Num() > 0);

	TArray<FEHBStairRailingPostSample> Samples;
	TestTrue(
		TEXT("Stair railing post samples should be available"),
		Stair->BuildRailingPostSamples(
			EEHBRailingSide::Left,
			EEHBRailingPostSpacingMode::StepAligned,
			Stair->StairData.TreadDepth * static_cast<float>(Stair->StairData.RailingStepsPerPost),
			Stair->StairData.RailingStepsPerPost,
			Stair->StairData.RailingEdgeInset,
			0.0f,
			Samples));
	if (!Samples.IsEmpty())
	{
		TestTrue(
			TEXT("First stair railing post should sit near the center of the upper landing tread"),
			FMath::IsNearlyEqual(Samples[0].Distance, -Stair->StairData.TreadDepth * 0.5f, 1.0f));
		if (Samples.Num() > 1)
		{
			const float StepRun = FMath::Max(1.0f, Stair->StairData.TreadDepth - Stair->StairData.NosingLength);
			const int32 ExpectedFirstStepIndex = FMath::Min(
				Stair->GeneratedStepCount - 1,
				FMath::Max(1, Stair->StairData.RailingStepsPerPost) - 1);
			const float ExpectedFirstStepDistance =
				StepRun * static_cast<float>(ExpectedFirstStepIndex) + Stair->StairData.TreadDepth * 0.5f;
			TestTrue(
				TEXT("Second stair railing post should count the upper landing tread in the configured interval"),
				FMath::IsNearlyEqual(Samples[1].Distance, ExpectedFirstStepDistance, 1.0f));
		}
		for (int32 SampleIndex = 0; SampleIndex < Samples.Num(); ++SampleIndex)
		{
			const FVector SampleUp = Samples[SampleIndex].LocalRotation.Quaternion().GetUpVector();
			TestTrue(
				*FString::Printf(TEXT("Stair railing post sample %d should stay upright"), SampleIndex),
				FVector::DotProduct(SampleUp, FVector::UpVector) > 0.999f);

			FEHBStairPathSample PathSample;
			if (TestTrue(
				*FString::Printf(TEXT("Stair railing path sample %d should be available"), SampleIndex),
				Stair->SamplePathForRailing(
					EEHBRailingSide::Left,
					Samples[SampleIndex].Distance,
					Stair->StairData.RailingEdgeInset,
					0.0f,
					PathSample)))
			{
				const FVector SampleFaceOut = Samples[SampleIndex].LocalRotation.RotateVector(FVector::RightVector);
				TestTrue(
					*FString::Printf(TEXT("Stair railing post sample %d face should point outward"), SampleIndex),
					FVector::DotProduct(SampleFaceOut.GetSafeNormal(), PathSample.LocalRight.GetSafeNormal()) > 0.999f);
			}
		}
	}

	TArray<FEHBStairRailingPostSample> OffsetSamples;
	Stair->StairData.RailingPostForwardOffset = 6.0f;
	TestTrue(
		TEXT("Stair railing post samples should honor forward offset"),
		Stair->BuildRailingPostSamples(
			EEHBRailingSide::Left,
			EEHBRailingPostSpacingMode::StepAligned,
			Stair->StairData.TreadDepth * static_cast<float>(Stair->StairData.RailingStepsPerPost),
			Stair->StairData.RailingStepsPerPost,
			Stair->StairData.RailingEdgeInset,
			0.0f,
			OffsetSamples));
	if (!Samples.IsEmpty() && !OffsetSamples.IsEmpty())
	{
		TestTrue(
			TEXT("First stair railing post should move forward by the configured offset"),
			FMath::IsNearlyEqual(OffsetSamples[0].Distance, Samples[0].Distance + 6.0f, 0.5f));
	}
	Stair->StairData.RailingPostForwardOffset = 0.0f;

	int32 StepCount = 0;
	float StepHeight = 0.0f;
	if (TestTrue(TEXT("Stair should calculate steps for railing rail continuity"), Stair->CalculateStairs(Stair->StairData.StairHeight, StepCount, StepHeight)))
	{
		const float StepRun = FMath::Max(1.0f, Stair->StairData.TreadDepth - Stair->StairData.NosingLength);
		FEHBStairPathSample RailBeforeStep;
		FEHBStairPathSample RailAfterStep;
		if (TestTrue(
			TEXT("Smooth stair railing rail samples should be available around a step boundary"),
			Stair->SampleRailPathForRailing(
				EEHBRailingSide::Left,
				StepRun - 0.25f,
				Stair->StairData.RailingEdgeInset,
				0.0f,
				RailBeforeStep)
				&& Stair->SampleRailPathForRailing(
					EEHBRailingSide::Left,
					StepRun + 0.25f,
					Stair->StairData.RailingEdgeInset,
					0.0f,
					RailAfterStep)))
		{
			TestTrue(
				TEXT("Smooth stair railing rail should not jump by a full step at tread boundaries"),
				FMath::Abs(RailBeforeStep.LocalLocation.Z - RailAfterStep.LocalLocation.Z) < StepHeight * 0.1f);
		}
	}

	if (LeftRailSection && !Samples.IsEmpty())
	{
		FEHBStairPathSample RailFaceSample;
		if (TestTrue(
			TEXT("Embedded railing rail face sample should be available"),
			Stair->SampleRailPathForRailing(
				EEHBRailingSide::Left,
				Samples[0].Distance,
				Stair->StairData.RailingEdgeInset,
				0.0f,
				RailFaceSample)))
		{
			int32 OutwardNormalCount = 0;
			int32 OutwardFrontWindingCount = 0;
			for (int32 TriangleIndex = 0; TriangleIndex + 2 < LeftRailSection->ProcIndexBuffer.Num(); TriangleIndex += 3)
			{
				const int32 IA = static_cast<int32>(LeftRailSection->ProcIndexBuffer[TriangleIndex]);
				const int32 IB = static_cast<int32>(LeftRailSection->ProcIndexBuffer[TriangleIndex + 1]);
				const int32 IC = static_cast<int32>(LeftRailSection->ProcIndexBuffer[TriangleIndex + 2]);
				if (!LeftRailSection->ProcVertexBuffer.IsValidIndex(IA)
					|| !LeftRailSection->ProcVertexBuffer.IsValidIndex(IB)
					|| !LeftRailSection->ProcVertexBuffer.IsValidIndex(IC))
				{
					continue;
				}

				const FEHBMeshVertex& A = LeftRailSection->ProcVertexBuffer[IA];
				const FEHBMeshVertex& B = LeftRailSection->ProcVertexBuffer[IB];
				const FEHBMeshVertex& C = LeftRailSection->ProcVertexBuffer[IC];
				const FVector AverageNormal = (FVector(A.Normal) + FVector(B.Normal) + FVector(C.Normal)).GetSafeNormal();
				if (FVector::DotProduct(AverageNormal, RailFaceSample.LocalRight) <= 0.8f)
				{
					continue;
				}

				++OutwardNormalCount;
				const FVector GeometricNormal = FVector::CrossProduct(
					FVector(B.Position) - FVector(A.Position),
					FVector(C.Position) - FVector(A.Position)).GetSafeNormal();
				if (FVector::DotProduct(GeometricNormal, RailFaceSample.LocalRight) < -0.8f)
				{
					++OutwardFrontWindingCount;
				}
			}

			TestTrue(TEXT("Embedded railing rail should generate outward-facing side normals"), OutwardNormalCount > 0);
			TestEqual(
				TEXT("Embedded railing rail outward faces should use visible generated-mesh winding"),
				OutwardFrontWindingCount,
				OutwardNormalCount);
		}
	}

	FTransform FirstPostInstanceTransform;
	if (Stair->LeftRailingPostMeshComponent
		&& TestTrue(
			TEXT("First embedded railing post instance transform should be available"),
			Stair->LeftRailingPostMeshComponent->GetInstanceTransform(0, FirstPostInstanceTransform, false)))
	{
		TestTrue(
			TEXT("First embedded railing post instance should stay upright"),
			FVector::DotProduct(FirstPostInstanceTransform.GetRotation().GetUpVector(), FVector::UpVector) > 0.999f);
		FEHBStairPathSample FirstPostPathSample;
		if (TestTrue(
			TEXT("First embedded railing post path sample should be available"),
			Stair->SamplePathForRailing(
				EEHBRailingSide::Left,
				Samples.IsEmpty() ? 0.0f : Samples[0].Distance,
				Stair->StairData.RailingEdgeInset,
				0.0f,
				FirstPostPathSample)))
		{
			const FVector FirstPostFaceOut = FirstPostInstanceTransform.GetRotation().RotateVector(FVector::RightVector);
			TestTrue(
				TEXT("First embedded railing post instance face should point outward"),
				FVector::DotProduct(FirstPostFaceOut.GetSafeNormal(), FirstPostPathSample.LocalRight.GetSafeNormal()) > 0.999f);
		}
	}

	DestroyStairTestActor(Stair);
	return true;
}

#endif
