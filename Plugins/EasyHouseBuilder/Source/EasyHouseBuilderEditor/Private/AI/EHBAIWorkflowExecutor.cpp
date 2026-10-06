// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/EHBAIWorkflowExecutor.h"

#include "Actors/EHB_DoorWindow.h"
#include "Actors/EHB_Floor.h"
#include "Actors/EHB_FloorSlab.h"
#include "Actors/EHB_Pillar.h"
#include "Actors/EHBGableRoof.h"
#include "Actors/EHBHipRoof.h"
#include "Actors/EHB_Wall.h"
#include "Core/EHBBuildingActorBase.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/World.h"
#include "ScopedTransaction.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Settings/EHBBuildingToolsetSettings.h"

#define LOCTEXT_NAMESPACE "EHBAIWorkflowExecutor"

namespace
{
	struct FEHBAIFloorSpec
	{
		int32 FloorIndex = 1;
		float BaseZ = 0.0f;
		float WallHeight = 300.0f;
		float WallThickness = 20.0f;
		float CeilingSlabThickness = 20.0f;
		bool bHasExplicitBaseZ = false;
	};

	struct FEHBAIPillarSpec
	{
		FString Id;
		FVector LocalLocation = FVector::ZeroVector;
		float Height = 300.0f;
		int32 FloorIndex = 1;
	};

	struct FEHBAIRoomSpec
	{
		FString Id;
		int32 FloorIndex = 1;
		float Height = 300.0f;
		float Thickness = 20.0f;
		TArray<FString> PillarLoop;
	};

	struct FEHBAIWallSpec
	{
		FString StartId;
		FString EndId;
		int32 FloorIndex = 1;
		float Height = 300.0f;
		float Thickness = 20.0f;
	};

	struct FEHBAIWallCurveSpec
	{
		FString StartId;
		FString EndId;
		float ControlOffset = 0.0f;
		float SegmentLength = 50.0f;
	};

	struct FEHBAIDoorWindowSpec
	{
		FString Id;
		FString StartId;
		FString EndId;
		EEHBDoorWindowElementKind Kind = EEHBDoorWindowElementKind::Window;
		float DistanceFromStart = 0.0f;
		float Ratio = 0.5f;
		bool bUseRatio = true;
		float Width = 120.0f;
		float Height = 150.0f;
		float SillHeight = 90.0f;
		float Thickness = 24.0f;
	};

	struct FEHBAISlabSpec
	{
		FString Id;
		bool bIsFoundation = false;
		bool bKeepBottomOnGround = false;
		float Thickness = 20.0f;
		float TopZ = 0.0f;
		bool bHasExplicitTopZ = false;
		int32 FloorIndex = 1;
		TArray<FVector> LocalTopPolygon;
		TArray<FVector> AIDesignTopPolygon;
		FString SourceJson;
		float FoundationExpansion = 0.0f;
		float VisualExpansion = 0.0f;
		bool bHasAIFoundationSource = false;
	};

	struct FEHBAIRoofSpec
	{
		FString Id;
		bool bUseHipRoof = false;
		int32 FloorIndex = 1;
		FVector LocalCenter = FVector::ZeroVector;
		bool bHasLocalCenter = false;
		bool bHasLength = false;
		bool bHasWidth = false;
		bool bAutoFitToWallFootprint = false;
		float AutoFitPadding = 0.0f;
		float YawDegrees = 0.0f;
		float Length = 600.0f;
		float Width = 400.0f;
		float PitchDegrees = 25.0f;
		float Thickness = 20.0f;
		float EaveOffset = 30.0f;
		float RidgeOffsetRatio = 0.0f;
		EEHBRoofAxisMode AxisMode = EEHBRoofAxisMode::RidgeAlongX;
		bool bGenerateRidge = true;
		bool bGenerateEaves = true;
		bool bGenerateGableRakes = true;
		bool bGenerateGableEndWalls = true;
		float RidgeWidth = 18.0f;
		float RidgeHeight = 10.0f;
		float EaveWidth = 18.0f;
		float EaveHeight = 18.0f;
		float GableRakeWidth = 16.0f;
		float GableRakeHeight = 10.0f;
		float GableEndWallBoundaryInset = 0.0f;
		bool bCutCollidingElements = false;
		bool bRemoveDisconnectedCutPieces = true;
		bool bKeepCutAwayDisconnectedPieces = false;
		bool bUseExactSourceMeshCutters = true;
		bool bUseControlledEnvelopeCutters = true;
		FEHBMeshEnvelopeBuildOptions EnvelopeCutOptions;
		bool bUseWallFootprintCutters = true;
		int32 MinWallFootprintGroupWallCount = 2;
		float WallFootprintGroupEndpointTolerance = 80.0f;
		float WallFootprintPadding = -2.0f;
		float WallFootprintMaxDimension = 0.0f;
		float WallFootprintMinArea = 100.0f;
		bool bShowCutDebugVisualization = false;
		bool bDebugDrawRawRoofBounds = true;
		bool bDebugDrawSourceBounds = true;
		bool bDebugDrawCutterBounds = true;
		bool bDebugDrawResultBounds = true;
		float CutDebugDrawDuration = 12.0f;
		float CutDebugDrawThickness = 2.0f;
	};

	struct FEHBAIRoomSurfaceCandidate
	{
		FEHBBuildingClosedLoop Loop;
		TArray<FVector> BuildingLocalPolygon;
		FVector Centroid = FVector::ZeroVector;
		float AbsArea = 0.0f;
	};

	static FString MakeWallEdgeKey(const FString& FirstId, const FString& SecondId)
	{
		return FirstId < SecondId
			? FString::Printf(TEXT("%s|%s"), *FirstId, *SecondId)
			: FString::Printf(TEXT("%s|%s"), *SecondId, *FirstId);
	}

	static float ReadNumber(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName, float DefaultValue)
	{
		double NumberValue = DefaultValue;
		return Object.IsValid() && Object->TryGetNumberField(FieldName, NumberValue)
			? static_cast<float>(NumberValue)
			: DefaultValue;
	}

	static int32 ReadInt(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName, int32 DefaultValue)
	{
		double NumberValue = static_cast<double>(DefaultValue);
		return Object.IsValid() && Object->TryGetNumberField(FieldName, NumberValue)
			? FMath::RoundToInt(NumberValue)
			: DefaultValue;
	}

	static bool ReadBool(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName, bool DefaultValue)
	{
		bool BoolValue = DefaultValue;
		return Object.IsValid() && Object->TryGetBoolField(FieldName, BoolValue)
			? BoolValue
			: DefaultValue;
	}

	static EEHBRoofAxisMode ReadRoofAxisMode(const TSharedPtr<FJsonObject>& Object, EEHBRoofAxisMode DefaultValue)
	{
		FString AxisString;
		if (!Object.IsValid() || !Object->TryGetStringField(TEXT("axisMode"), AxisString))
		{
			return DefaultValue;
		}

		AxisString = AxisString.TrimStartAndEnd().ToLower();
		if (AxisString == TEXT("y") || AxisString == TEXT("ridgealongy") || AxisString == TEXT("alongy"))
		{
			return EEHBRoofAxisMode::RidgeAlongY;
		}
		return EEHBRoofAxisMode::RidgeAlongX;
	}

	static bool TryReadVector(const TSharedPtr<FJsonValue>& Value, FVector& OutVector, float DefaultZ = 0.0f)
	{
		if (!Value.IsValid())
		{
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>* ArrayValue = nullptr;
		if (Value->TryGetArray(ArrayValue) && ArrayValue && ArrayValue->Num() >= 2)
		{
			double X = 0.0;
			double Y = 0.0;
			double Z = DefaultZ;
			if (!(*ArrayValue)[0]->TryGetNumber(X) || !(*ArrayValue)[1]->TryGetNumber(Y))
			{
				return false;
			}
			if (ArrayValue->Num() >= 3)
			{
				(*ArrayValue)[2]->TryGetNumber(Z);
			}
			OutVector = FVector(X, Y, Z);
			return true;
		}

		const TSharedPtr<FJsonObject>* ObjectValue = nullptr;
		if (Value->TryGetObject(ObjectValue) && ObjectValue && ObjectValue->IsValid())
		{
			double X = 0.0;
			double Y = 0.0;
			double Z = DefaultZ;
			if (!(*ObjectValue)->TryGetNumberField(TEXT("x"), X) || !(*ObjectValue)->TryGetNumberField(TEXT("y"), Y))
			{
				return false;
			}
			(*ObjectValue)->TryGetNumberField(TEXT("z"), Z);
			OutVector = FVector(X, Y, Z);
			return true;
		}

		return false;
	}

	static bool TryReadVectorField(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName, FVector& OutVector, float DefaultZ = 0.0f)
	{
		if (!Object.IsValid())
		{
			return false;
		}

		const TSharedPtr<FJsonValue>* FieldValue = Object->Values.Find(FieldName);
		return FieldValue && TryReadVector(*FieldValue, OutVector, DefaultZ);
	}

	static bool DoesVectorFieldContainZ(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName)
	{
		if (!Object.IsValid())
		{
			return false;
		}

		const TSharedPtr<FJsonValue>* FieldValue = Object->Values.Find(FieldName);
		if (!FieldValue || !FieldValue->IsValid())
		{
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>* ArrayValue = nullptr;
		if ((*FieldValue)->TryGetArray(ArrayValue) && ArrayValue)
		{
			return ArrayValue->Num() >= 3;
		}

		const TSharedPtr<FJsonObject>* ObjectValue = nullptr;
		if ((*FieldValue)->TryGetObject(ObjectValue) && ObjectValue && ObjectValue->IsValid())
		{
			return (*ObjectValue)->HasField(TEXT("z"));
		}

		return false;
	}

	static bool TryReadPolygonField(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName, float DefaultZ, TArray<FVector>& OutPolygon)
	{
		OutPolygon.Reset();
		const TArray<TSharedPtr<FJsonValue>>* PolygonValues = nullptr;
		if (!Object.IsValid() || !Object->TryGetArrayField(FieldName, PolygonValues) || !PolygonValues)
		{
			return false;
		}

		OutPolygon.Reserve(PolygonValues->Num());
		for (const TSharedPtr<FJsonValue>& PointValue : *PolygonValues)
		{
			FVector Point;
			if (!TryReadVector(PointValue, Point, DefaultZ))
			{
				return false;
			}
			OutPolygon.Add(Point);
		}

		return OutPolygon.Num() >= 3;
	}

	static bool TryReadStringLoop(
		const TSharedPtr<FJsonObject>& Object,
		const TCHAR* FieldName,
		const TFunction<FString(const FString&)>& ResolveId,
		TArray<FString>& OutLoop)
	{
		OutLoop.Reset();
		const TArray<TSharedPtr<FJsonValue>>* LoopValues = nullptr;
		if (!Object.IsValid() || !Object->TryGetArrayField(FieldName, LoopValues) || !LoopValues)
		{
			return false;
		}

		OutLoop.Reserve(LoopValues->Num());
		for (const TSharedPtr<FJsonValue>& LoopValue : *LoopValues)
		{
			FString Id;
			if (!LoopValue.IsValid() || !LoopValue->TryGetString(Id))
			{
				return false;
			}

			Id = ResolveId(Id.TrimStartAndEnd());
			if (Id.IsEmpty())
			{
				return false;
			}
			OutLoop.Add(Id);
		}

		return OutLoop.Num() >= 3;
	}

	static bool TryBuildPolygonFromPillarLoop(
		const TArray<FString>& PillarLoop,
		const TMap<FString, FVector>& PillarLocationsById,
		TArray<FVector>& OutPolygon)
	{
		OutPolygon.Reset();
		if (PillarLoop.Num() < 3)
		{
			return false;
		}

		OutPolygon.Reserve(PillarLoop.Num());
		for (const FString& PillarId : PillarLoop)
		{
			const FVector* PillarLocation = PillarLocationsById.Find(PillarId);
			if (!PillarLocation)
			{
				return false;
			}
			OutPolygon.Add(FVector(PillarLocation->X, PillarLocation->Y, 0.0f));
		}
		return true;
	}

	static double Cross2D(const FVector& Origin, const FVector& A, const FVector& B)
	{
		return (A.X - Origin.X) * (B.Y - Origin.Y) - (A.Y - Origin.Y) * (B.X - Origin.X);
	}

	static float GetClosestAlphaOnSegmentXY(const FVector& SegmentStart, const FVector& SegmentEnd, const FVector& Point)
	{
		const FVector Segment(SegmentEnd.X - SegmentStart.X, SegmentEnd.Y - SegmentStart.Y, 0.0f);
		const float SegmentLengthSquared = Segment.SizeSquared2D();
		if (SegmentLengthSquared <= UE_SMALL_NUMBER)
		{
			return 0.0f;
		}

		const FVector ToPoint(Point.X - SegmentStart.X, Point.Y - SegmentStart.Y, 0.0f);
		return FMath::Clamp(FVector::DotProduct(ToPoint, Segment) / SegmentLengthSquared, 0.0f, 1.0f);
	}

	static float GetDistanceSquaredToSegmentXY(const FVector& Point, const FVector& SegmentStart, const FVector& SegmentEnd)
	{
		const float Alpha = GetClosestAlphaOnSegmentXY(SegmentStart, SegmentEnd, Point);
		const FVector ClosestPoint = FMath::Lerp(SegmentStart, SegmentEnd, Alpha);
		return FVector::DistSquared2D(Point, ClosestPoint);
	}

	static double CalculateSignedArea2D(const TArray<FVector>& Polygon)
	{
		double Area = 0.0;
		for (int32 Index = 0; Index < Polygon.Num(); ++Index)
		{
			const FVector& Current = Polygon[Index];
			const FVector& Next = Polygon[(Index + 1) % Polygon.Num()];
			Area += Current.X * Next.Y - Next.X * Current.Y;
		}
		return Area * 0.5;
	}

	static void ReversePoints(TArray<FVector>& Points)
	{
		for (int32 FirstIndex = 0, LastIndex = Points.Num() - 1; FirstIndex < LastIndex; ++FirstIndex, --LastIndex)
		{
			Swap(Points[FirstIndex], Points[LastIndex]);
		}
	}

	static void AddUniquePointXY(TArray<FVector>& Points, const FVector& Point, float Tolerance = 0.25f)
	{
		if (!Points.IsEmpty() && FVector::DistSquared2D(Points.Last(), Point) <= Tolerance * Tolerance)
		{
			return;
		}
		Points.Add(Point);
	}

	static bool CleanAutoFillPolygonXY(TArray<FVector>& Points, float MergeTolerance = 2.0f)
	{
		if (Points.Num() < 2)
		{
			return false;
		}

		const float MergeToleranceSquared = MergeTolerance * MergeTolerance;
		for (int32 GuardIndex = 0; GuardIndex < 256 && Points.Num() > 1; ++GuardIndex)
		{
			bool bMergedAnyPoint = false;
			for (int32 Index = Points.Num() - 1; Index >= 0 && Points.Num() > 1; --Index)
			{
				const int32 PreviousIndex = (Index - 1 + Points.Num()) % Points.Num();
				if (PreviousIndex == Index)
				{
					continue;
				}

				if (FVector::DistSquared2D(Points[PreviousIndex], Points[Index]) <= MergeToleranceSquared)
				{
					Points[PreviousIndex] = (Points[PreviousIndex] + Points[Index]) * 0.5f;
					Points.RemoveAt(Index, 1, EAllowShrinking::No);
					bMergedAnyPoint = true;
				}
			}

			if (!bMergedAnyPoint)
			{
				break;
			}
		}

		constexpr float CollinearTolerance = 0.05f;
		const float CollinearToleranceSquared = CollinearTolerance * CollinearTolerance;
		for (int32 GuardIndex = 0; GuardIndex < 256 && Points.Num() > 3; ++GuardIndex)
		{
			bool bRemovedAnyPoint = false;
			for (int32 Index = Points.Num() - 1; Index >= 0 && Points.Num() > 3; --Index)
			{
				const int32 PreviousIndex = (Index - 1 + Points.Num()) % Points.Num();
				const int32 NextIndex = (Index + 1) % Points.Num();
				if (PreviousIndex == NextIndex
					|| FVector::DistSquared2D(Points[PreviousIndex], Points[NextIndex]) <= MergeToleranceSquared)
				{
					continue;
				}

				if (GetDistanceSquaredToSegmentXY(Points[Index], Points[PreviousIndex], Points[NextIndex]) <= CollinearToleranceSquared)
				{
					Points.RemoveAt(Index, 1, EAllowShrinking::No);
					bRemovedAnyPoint = true;
				}
			}

			if (!bRemovedAnyPoint)
			{
				break;
			}
		}

		return Points.Num() >= 3 && FMath::Abs(CalculateSignedArea2D(Points)) > 1.0;
	}

	static bool IsPointInsidePolygonXY(const FVector& Point, const TArray<FVector>& Polygon)
	{
		bool bInside = false;
		for (int32 Index = 0, PreviousIndex = Polygon.Num() - 1; Index < Polygon.Num(); PreviousIndex = Index++)
		{
			const FVector& A = Polygon[Index];
			const FVector& B = Polygon[PreviousIndex];
			const bool bIntersects =
				((A.Y > Point.Y) != (B.Y > Point.Y))
				&& (Point.X < (B.X - A.X) * (Point.Y - A.Y) / (B.Y - A.Y) + A.X);
			if (bIntersects)
			{
				bInside = !bInside;
			}
		}
		return bInside;
	}

	static bool IsPointNearPolygonBoundaryXY(const FVector& Point, const TArray<FVector>& Polygon, float BoundaryTolerance = 2.0f)
	{
		if (Polygon.Num() < 2)
		{
			return false;
		}

		const float BoundaryToleranceSquared = BoundaryTolerance * BoundaryTolerance;
		for (int32 Index = 0; Index < Polygon.Num(); ++Index)
		{
			const FVector& A = Polygon[Index];
			const FVector& B = Polygon[(Index + 1) % Polygon.Num()];
			if (GetDistanceSquaredToSegmentXY(Point, A, B) <= BoundaryToleranceSquared)
			{
				return true;
			}
		}
		return false;
	}

	static bool LoopContainsInteriorWall(
		AEHBBuildingActorBase* Building,
		const FEHBBuildingClosedLoop& Loop,
		const TArray<FVector>& BuildingLocalPolygon)
	{
		if (!Building || BuildingLocalPolygon.Num() < 3)
		{
			return false;
		}

		TSet<FGuid> LoopWallGuids;
		for (const FGuid& WallGuid : Loop.WallGuids)
		{
			LoopWallGuids.Add(WallGuid);
		}

		TArray<AActor*> AttachedActors;
		Building->GetAttachedActors(AttachedActors);
		for (AActor* Actor : AttachedActors)
		{
			const AEHB_Wall* Wall = Cast<AEHB_Wall>(Actor);
			if (!Wall
				|| Wall->OwningBuilding != Building
				|| Wall->FloorIndex != Loop.FloorIndex
				|| LoopWallGuids.Contains(Wall->ElementGuid))
			{
				continue;
			}

			FVector WallMidpoint = (Wall->LocalStart + Wall->LocalEnd) * 0.5f;
			WallMidpoint.Z = 0.0f;
			if (IsPointInsidePolygonXY(WallMidpoint, BuildingLocalPolygon)
				&& !IsPointNearPolygonBoundaryXY(WallMidpoint, BuildingLocalPolygon, FMath::Max(2.0f, Wall->Thickness * 0.25f)))
			{
				return true;
			}
		}

		return false;
	}

	static EEHBFloorSlabWallSide ResolveLoopSideForWall(const FEHBBuildingClosedLoop& Loop, const AEHB_Wall* Wall)
	{
		if (!Wall
			|| !Wall->StartPillarGuid.IsValid()
			|| !Wall->EndPillarGuid.IsValid()
			|| Loop.PillarGuids.Num() < 3
			|| Loop.WallGuids.Num() != Loop.PillarGuids.Num())
		{
			return EEHBFloorSlabWallSide::None;
		}

		for (int32 Index = 0; Index < Loop.WallGuids.Num(); ++Index)
		{
			if (Loop.WallGuids[Index] != Wall->ElementGuid)
			{
				continue;
			}

			const FGuid FromPillarGuid = Loop.PillarGuids[Index];
			const FGuid ToPillarGuid = Loop.PillarGuids[(Index + 1) % Loop.PillarGuids.Num()];
			if (FromPillarGuid == Wall->StartPillarGuid && ToPillarGuid == Wall->EndPillarGuid)
			{
				return Loop.bClockwise ? EEHBFloorSlabWallSide::Right : EEHBFloorSlabWallSide::Left;
			}

			if (FromPillarGuid == Wall->EndPillarGuid && ToPillarGuid == Wall->StartPillarGuid)
			{
				return Loop.bClockwise ? EEHBFloorSlabWallSide::Left : EEHBFloorSlabWallSide::Right;
			}
		}

		return EEHBFloorSlabWallSide::None;
	}

	static bool BuildClosedLoopFillPolygonInBuildingSpace(
		AEHBBuildingActorBase* Building,
		const FEHBBuildingClosedLoop& Loop,
		TArray<FVector>& OutBuildingLocalPolygon)
	{
		OutBuildingLocalPolygon.Reset();
		if (!Building
			|| Loop.PillarGuids.Num() < 3
			|| Loop.WallGuids.Num() != Loop.PillarGuids.Num())
		{
			return false;
		}

		OutBuildingLocalPolygon.Reserve(Loop.PillarGuids.Num() * 2);
		for (int32 Index = 0; Index < Loop.WallGuids.Num(); ++Index)
		{
			bool bSameDirection = true;
			AEHB_Wall* Wall = Building->FindWallBetweenPillars(
				Loop.PillarGuids[Index],
				Loop.PillarGuids[(Index + 1) % Loop.PillarGuids.Num()],
				bSameDirection);
			if (!Wall || Wall->ElementGuid != Loop.WallGuids[Index])
			{
				OutBuildingLocalPolygon.Reset();
				return false;
			}

			const EEHBFloorSlabWallSide RoomSide = ResolveLoopSideForWall(Loop, Wall);
			if (RoomSide == EEHBFloorSlabWallSide::None)
			{
				OutBuildingLocalPolygon.Reset();
				return false;
			}

			TArray<FVector> SidePolyline;
			if (!Wall->BuildSideTopPolylineInBuildingSpace(RoomSide == EEHBFloorSlabWallSide::Left, SidePolyline, 25.0f)
				|| SidePolyline.Num() < 2)
			{
				OutBuildingLocalPolygon.Reset();
				return false;
			}

			if (!bSameDirection)
			{
				ReversePoints(SidePolyline);
			}

			for (FVector Point : SidePolyline)
			{
				Point.Z = 0.0f;
				AddUniquePointXY(OutBuildingLocalPolygon, Point, 0.5f);
			}
		}

		return CleanAutoFillPolygonXY(OutBuildingLocalPolygon);
	}

	static bool TryResolveRoofAutoFitFromWallFootprint(
		AEHBBuildingActorBase* Building,
		int32 FloorIndex,
		float YawDegrees,
		EEHBRoofAxisMode AxisMode,
		float RoofZ,
		float Padding,
		FVector& InOutLocalCenter,
		float& InOutLength,
		float& InOutWidth)
	{
		if (!Building)
		{
			return false;
		}

		const FQuat YawRotation = FRotator(0.0f, YawDegrees, 0.0f).Quaternion();
		const FQuat InverseYawRotation = YawRotation.Inverse();
		bool bHasBounds = false;
		FVector2D MinPoint = FVector2D::ZeroVector;
		FVector2D MaxPoint = FVector2D::ZeroVector;

		auto IncludePoint = [&](FVector Point)
		{
			Point.Z = 0.0f;
			const FVector RoofLocalPoint = InverseYawRotation.RotateVector(Point);
			const FVector2D XY(RoofLocalPoint.X, RoofLocalPoint.Y);
			if (!bHasBounds)
			{
				MinPoint = XY;
				MaxPoint = XY;
				bHasBounds = true;
				return;
			}

			MinPoint.X = FMath::Min(MinPoint.X, XY.X);
			MinPoint.Y = FMath::Min(MinPoint.Y, XY.Y);
			MaxPoint.X = FMath::Max(MaxPoint.X, XY.X);
			MaxPoint.Y = FMath::Max(MaxPoint.Y, XY.Y);
		};

		Building->RebuildClosedLoops();
		for (const FEHBBuildingClosedLoop& Loop : Building->ClosedLoops)
		{
			if (Loop.FloorIndex != FloorIndex)
			{
				continue;
			}

			TArray<FVector> LoopPolygon;
			if (!BuildClosedLoopFillPolygonInBuildingSpace(Building, Loop, LoopPolygon))
			{
				continue;
			}

			for (const FVector& Point : LoopPolygon)
			{
				IncludePoint(Point);
			}
		}

		if (!bHasBounds)
		{
			TArray<AActor*> AttachedActors;
			Building->GetAttachedActors(AttachedActors);
			for (AActor* AttachedActor : AttachedActors)
			{
				const AEHB_Wall* Wall = Cast<AEHB_Wall>(AttachedActor);
				if (!Wall || Wall->FloorIndex != FloorIndex)
				{
					continue;
				}

				IncludePoint(Wall->LocalStart);
				IncludePoint(Wall->LocalEnd);

				TArray<FVector> SidePolyline;
				if (Wall->BuildSideTopPolylineInBuildingSpace(true, SidePolyline, FMath::Max(10.0f, Wall->CurveSegmentLength)))
				{
					for (const FVector& Point : SidePolyline)
					{
						IncludePoint(Point);
					}
				}
				if (Wall->BuildSideTopPolylineInBuildingSpace(false, SidePolyline, FMath::Max(10.0f, Wall->CurveSegmentLength)))
				{
					for (const FVector& Point : SidePolyline)
					{
						IncludePoint(Point);
					}
				}
			}
		}

		if (!bHasBounds)
		{
			return false;
		}

		const float SafePadding = FMath::Max(0.0f, Padding);
		const FVector2D Center2D = (MinPoint + MaxPoint) * 0.5f;
		const FVector Center = YawRotation.RotateVector(FVector(Center2D.X, Center2D.Y, 0.0f));
		InOutLocalCenter = FVector(Center.X, Center.Y, RoofZ);

		const float SizeX = FMath::Max(1.0f, MaxPoint.X - MinPoint.X + SafePadding * 2.0f);
		const float SizeY = FMath::Max(1.0f, MaxPoint.Y - MinPoint.Y + SafePadding * 2.0f);
		if (AxisMode == EEHBRoofAxisMode::RidgeAlongX)
		{
			InOutLength = SizeX;
			InOutWidth = SizeY;
		}
		else
		{
			InOutLength = SizeY;
			InOutWidth = SizeX;
		}
		return true;
	}

	static bool TryIntersectOffsetLines2D(
		const FVector& FirstPoint,
		const FVector& FirstDirection,
		const FVector& SecondPoint,
		const FVector& SecondDirection,
		FVector& OutIntersection)
	{
		const double Denominator = FirstDirection.X * SecondDirection.Y - FirstDirection.Y * SecondDirection.X;
		if (FMath::Abs(Denominator) <= UE_SMALL_NUMBER)
		{
			return false;
		}

		const FVector Delta = SecondPoint - FirstPoint;
		const double T = (Delta.X * SecondDirection.Y - Delta.Y * SecondDirection.X) / Denominator;
		OutIntersection = FirstPoint + FirstDirection * T;
		OutIntersection.Z = 0.0f;
		return true;
	}

	static bool OffsetPolygon2D(const TArray<FVector>& Polygon, float OffsetDistance, TArray<FVector>& OutPolygon)
	{
		OutPolygon.Reset();
		if (Polygon.Num() < 3 || OffsetDistance <= 0.0f)
		{
			return false;
		}

		const bool bCounterClockwise = CalculateSignedArea2D(Polygon) > 0.0;
		OutPolygon.Reserve(Polygon.Num());
		for (int32 Index = 0; Index < Polygon.Num(); ++Index)
		{
			const FVector& Previous = Polygon[(Index - 1 + Polygon.Num()) % Polygon.Num()];
			const FVector& Current = Polygon[Index];
			const FVector& Next = Polygon[(Index + 1) % Polygon.Num()];

			const FVector PreviousEdge = (Current - Previous).GetSafeNormal2D();
			const FVector NextEdge = (Next - Current).GetSafeNormal2D();
			if (PreviousEdge.IsNearlyZero() || NextEdge.IsNearlyZero())
			{
				OutPolygon.Add(Current);
				continue;
			}

			const FVector PreviousOutward = bCounterClockwise
				? FVector(PreviousEdge.Y, -PreviousEdge.X, 0.0f)
				: FVector(-PreviousEdge.Y, PreviousEdge.X, 0.0f);
			const FVector NextOutward = bCounterClockwise
				? FVector(NextEdge.Y, -NextEdge.X, 0.0f)
				: FVector(-NextEdge.Y, NextEdge.X, 0.0f);

			FVector ExpandedPoint;
			if (TryIntersectOffsetLines2D(
				Current + PreviousOutward * OffsetDistance,
				PreviousEdge,
				Current + NextOutward * OffsetDistance,
				NextEdge,
				ExpandedPoint))
			{
				OutPolygon.Add(ExpandedPoint);
				continue;
			}

			FVector FallbackDirection = PreviousOutward + NextOutward;
			if (FallbackDirection.IsNearlyZero())
			{
				FallbackDirection = NextOutward;
			}
			OutPolygon.Add(Current + FallbackDirection.GetSafeNormal2D() * OffsetDistance);
		}

		return OutPolygon.Num() >= 3;
	}

	static FString WriteJsonObjectToString(const TSharedPtr<FJsonObject>& Object)
	{
		if (!Object.IsValid())
		{
			return FString();
		}

		FString JsonText;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonText);
		FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
		return JsonText;
	}

	static bool BuildConvexHull2D(TArray<FVector> Points, TArray<FVector>& OutHull)
	{
		OutHull.Reset();
		if (Points.Num() < 3)
		{
			return false;
		}

		Points.Sort([](const FVector& A, const FVector& B)
		{
			if (!FMath::IsNearlyEqual(A.X, B.X))
			{
				return A.X < B.X;
			}
			return A.Y < B.Y;
		});

		TArray<FVector> UniquePoints;
		UniquePoints.Reserve(Points.Num());
		for (const FVector& Point : Points)
		{
			if (UniquePoints.IsEmpty() || FVector::Dist2D(UniquePoints.Last(), Point) > 0.1f)
			{
				UniquePoints.Add(FVector(Point.X, Point.Y, 0.0f));
			}
		}

		if (UniquePoints.Num() < 3)
		{
			return false;
		}

		TArray<FVector> LowerHull;
		for (const FVector& Point : UniquePoints)
		{
			while (LowerHull.Num() >= 2 && Cross2D(LowerHull[LowerHull.Num() - 2], LowerHull.Last(), Point) <= 0.0)
			{
				LowerHull.Pop(EAllowShrinking::No);
			}
			LowerHull.Add(Point);
		}

		TArray<FVector> UpperHull;
		for (int32 Index = UniquePoints.Num() - 1; Index >= 0; --Index)
		{
			const FVector& Point = UniquePoints[Index];
			while (UpperHull.Num() >= 2 && Cross2D(UpperHull[UpperHull.Num() - 2], UpperHull.Last(), Point) <= 0.0)
			{
				UpperHull.Pop(EAllowShrinking::No);
			}
			UpperHull.Add(Point);
		}

		LowerHull.Pop(EAllowShrinking::No);
		UpperHull.Pop(EAllowShrinking::No);
		OutHull = MoveTemp(LowerHull);
		OutHull.Append(UpperHull);
		return OutHull.Num() >= 3;
	}

	static void AddOrMergeWallSpec(
		const FEHBAIWallSpec& WallSpec,
		TArray<FEHBAIWallSpec>& WallSpecs,
		TMap<FString, int32>& WallEdgeToIndex)
	{
		if (WallSpec.StartId == WallSpec.EndId)
		{
			return;
		}

		const FString EdgeKey = MakeWallEdgeKey(WallSpec.StartId, WallSpec.EndId);
		if (int32* ExistingIndex = WallEdgeToIndex.Find(EdgeKey))
		{
			FEHBAIWallSpec& ExistingSpec = WallSpecs[*ExistingIndex];
			ExistingSpec.Height = FMath::Max(ExistingSpec.Height, WallSpec.Height);
			ExistingSpec.Thickness = FMath::Max(ExistingSpec.Thickness, WallSpec.Thickness);
			return;
		}

		WallEdgeToIndex.Add(EdgeKey, WallSpecs.Num());
		WallSpecs.Add(WallSpec);
	}

	static bool IsWallSpecCoveredByCollinearSegments(
		const FEHBAIWallSpec& Candidate,
		int32 CandidateIndex,
		const TArray<FEHBAIWallSpec>& WallSpecs,
		const TMap<FString, FVector>& PillarLocationsById)
	{
		const FVector* CandidateStart = PillarLocationsById.Find(Candidate.StartId);
		const FVector* CandidateEnd = PillarLocationsById.Find(Candidate.EndId);
		if (!CandidateStart || !CandidateEnd)
		{
			return false;
		}

		const FVector CandidateVector = *CandidateEnd - *CandidateStart;
		const float CandidateLengthSquared = CandidateVector.SizeSquared2D();
		if (CandidateLengthSquared <= UE_SMALL_NUMBER)
		{
			return false;
		}

		TArray<TPair<float, float>> CoveredIntervals;
		constexpr float LineTolerance = 2.0f;
		constexpr float AlphaTolerance = 0.002f;
		const float LineToleranceSquared = LineTolerance * LineTolerance;

		for (int32 OtherIndex = 0; OtherIndex < WallSpecs.Num(); ++OtherIndex)
		{
			if (OtherIndex == CandidateIndex)
			{
				continue;
			}

			const FEHBAIWallSpec& Other = WallSpecs[OtherIndex];
			if (Other.FloorIndex != Candidate.FloorIndex)
			{
				continue;
			}

			const FVector* OtherStart = PillarLocationsById.Find(Other.StartId);
			const FVector* OtherEnd = PillarLocationsById.Find(Other.EndId);
			if (!OtherStart || !OtherEnd)
			{
				continue;
			}

			if (GetDistanceSquaredToSegmentXY(*OtherStart, *CandidateStart, *CandidateEnd) > LineToleranceSquared
				|| GetDistanceSquaredToSegmentXY(*OtherEnd, *CandidateStart, *CandidateEnd) > LineToleranceSquared)
			{
				continue;
			}

			float StartAlpha = GetClosestAlphaOnSegmentXY(*CandidateStart, *CandidateEnd, *OtherStart);
			float EndAlpha = GetClosestAlphaOnSegmentXY(*CandidateStart, *CandidateEnd, *OtherEnd);
			if (StartAlpha > EndAlpha)
			{
				Swap(StartAlpha, EndAlpha);
			}

			if (EndAlpha - StartAlpha <= AlphaTolerance)
			{
				continue;
			}

			if (StartAlpha > 1.0f + AlphaTolerance || EndAlpha < -AlphaTolerance)
			{
				continue;
			}

			CoveredIntervals.Add(TPair<float, float>(
				FMath::Clamp(StartAlpha, 0.0f, 1.0f),
				FMath::Clamp(EndAlpha, 0.0f, 1.0f)));
		}

		TArray<TPair<float, FString>> CollinearPillarsOnCandidate;
		CollinearPillarsOnCandidate.Add(TPair<float, FString>(0.0f, Candidate.StartId));
		CollinearPillarsOnCandidate.Add(TPair<float, FString>(1.0f, Candidate.EndId));
		for (const TPair<FString, FVector>& PillarEntry : PillarLocationsById)
		{
			if (PillarEntry.Key == Candidate.StartId || PillarEntry.Key == Candidate.EndId)
			{
				continue;
			}

			if (GetDistanceSquaredToSegmentXY(PillarEntry.Value, *CandidateStart, *CandidateEnd) > LineToleranceSquared)
			{
				continue;
			}

			const float Alpha = GetClosestAlphaOnSegmentXY(*CandidateStart, *CandidateEnd, PillarEntry.Value);
			if (Alpha > AlphaTolerance && Alpha < 1.0f - AlphaTolerance)
			{
				CollinearPillarsOnCandidate.Add(TPair<float, FString>(Alpha, PillarEntry.Key));
			}
		}

		if (CollinearPillarsOnCandidate.Num() >= 3)
		{
			CollinearPillarsOnCandidate.Sort([](const TPair<float, FString>& A, const TPair<float, FString>& B)
			{
				return A.Key < B.Key;
			});

			auto HasWallSpecBetween = [&WallSpecs, CandidateIndex](const FString& FirstId, const FString& SecondId)
			{
				const FString EdgeKey = MakeWallEdgeKey(FirstId, SecondId);
				for (int32 WallIndex = 0; WallIndex < WallSpecs.Num(); ++WallIndex)
				{
					if (WallIndex != CandidateIndex
						&& MakeWallEdgeKey(WallSpecs[WallIndex].StartId, WallSpecs[WallIndex].EndId) == EdgeKey)
					{
						return true;
					}
				}
				return false;
			};

			bool bHasCompleteSplitPath = true;
			for (int32 PillarIndex = 0; PillarIndex + 1 < CollinearPillarsOnCandidate.Num(); ++PillarIndex)
			{
				if (!HasWallSpecBetween(CollinearPillarsOnCandidate[PillarIndex].Value, CollinearPillarsOnCandidate[PillarIndex + 1].Value))
				{
					bHasCompleteSplitPath = false;
					break;
				}
			}

			if (bHasCompleteSplitPath)
			{
				return true;
			}
		}

		if (CoveredIntervals.Num() < 2)
		{
			return false;
		}

		CoveredIntervals.Sort([](const TPair<float, float>& A, const TPair<float, float>& B)
		{
			return A.Key < B.Key;
		});

		float CoveredEnd = 0.0f;
		if (CoveredIntervals[0].Key > AlphaTolerance)
		{
			return false;
		}

		for (const TPair<float, float>& Interval : CoveredIntervals)
		{
			if (Interval.Key > CoveredEnd + AlphaTolerance)
			{
				return false;
			}
			CoveredEnd = FMath::Max(CoveredEnd, Interval.Value);
			if (CoveredEnd >= 1.0f - AlphaTolerance)
			{
				return true;
			}
		}

		return false;
	}

	static void RemoveRedundantOverlappingWallSpecs(
		TArray<FEHBAIWallSpec>& WallSpecs,
		TMap<FString, int32>& WallEdgeToIndex,
		const TMap<FString, FVector>& PillarLocationsById)
	{
		TArray<FEHBAIWallSpec> FilteredWallSpecs;
		FilteredWallSpecs.Reserve(WallSpecs.Num());
		for (int32 WallIndex = 0; WallIndex < WallSpecs.Num(); ++WallIndex)
		{
			if (!IsWallSpecCoveredByCollinearSegments(WallSpecs[WallIndex], WallIndex, WallSpecs, PillarLocationsById))
			{
				FilteredWallSpecs.Add(WallSpecs[WallIndex]);
			}
		}

		WallSpecs = MoveTemp(FilteredWallSpecs);
		WallEdgeToIndex.Reset();
		for (int32 WallIndex = 0; WallIndex < WallSpecs.Num(); ++WallIndex)
		{
			WallEdgeToIndex.Add(MakeWallEdgeKey(WallSpecs[WallIndex].StartId, WallSpecs[WallIndex].EndId), WallIndex);
		}
	}

	static void NormalizeStackedFloorBaseZs(TMap<int32, FEHBAIFloorSpec>& FloorsByIndex)
	{
		TArray<int32> FloorIndices;
		FloorsByIndex.GetKeys(FloorIndices);
		FloorIndices.Sort();

		for (int32 Index = 1; Index < FloorIndices.Num(); ++Index)
		{
			const int32 LowerFloorIndex = FloorIndices[Index - 1];
			const int32 UpperFloorIndex = FloorIndices[Index];
			if (UpperFloorIndex != LowerFloorIndex + 1)
			{
				continue;
			}

			const FEHBAIFloorSpec* LowerFloorSpec = FloorsByIndex.Find(LowerFloorIndex);
			FEHBAIFloorSpec* UpperFloorSpec = FloorsByIndex.Find(UpperFloorIndex);
			if (!LowerFloorSpec || !UpperFloorSpec)
			{
				continue;
			}

			const float ExpectedUpperBaseZ = LowerFloorSpec->BaseZ + LowerFloorSpec->WallHeight;
			if (!UpperFloorSpec->bHasExplicitBaseZ)
			{
				UpperFloorSpec->BaseZ = ExpectedUpperBaseZ;
				continue;
			}

			const float ExtraGap = UpperFloorSpec->BaseZ - ExpectedUpperBaseZ;
			const float SlabThicknessTolerance = FMath::Max(2.0f, LowerFloorSpec->CeilingSlabThickness * 0.2f);
			const bool bLooksLikeCeilingThicknessWasAdded =
				ExtraGap > 0.5f
				&& FMath::Abs(ExtraGap - LowerFloorSpec->CeilingSlabThickness) <= SlabThicknessTolerance;

			if (bLooksLikeCeilingThicknessWasAdded)
			{
				UpperFloorSpec->BaseZ = ExpectedUpperBaseZ;
			}
		}
	}

	static FEHBAIWorkflowExecutionResult Fail(const FText& StatusText)
	{
		FEHBAIWorkflowExecutionResult Result;
		Result.StatusText = StatusText;
		return Result;
	}
}

FEHBAIWorkflowExecutionResult FEHBAIWorkflowExecutor::ExecuteJson(
	AEHBBuildingActorBase* Building,
	const FString& JsonText,
	const FEHBAIWorkflowDefaults& Defaults)
{
	if (!Building || !Building->GetWorld())
	{
		return Fail(LOCTEXT("InvalidBuilding", "当前建筑对象无效，无法执行 AI 工作流。"));
	}

	const FString TrimmedJsonText = JsonText.TrimStartAndEnd();
	if (TrimmedJsonText.IsEmpty())
	{
		return Fail(LOCTEXT("EmptyInput", "JSON 为空。"));
	}

	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(TrimmedJsonText);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		return Fail(LOCTEXT("ParseFailed", "JSON 解析失败，请检查逗号、引号和括号。"));
	}

	const TSharedPtr<FJsonObject>* BuildingObjectPtr = nullptr;
	const TSharedPtr<FJsonObject> BuildingObject = Root->TryGetObjectField(TEXT("building"), BuildingObjectPtr) && BuildingObjectPtr
		? *BuildingObjectPtr
		: nullptr;

	const float DefaultWallHeight = FMath::Max(1.0f, ReadNumber(BuildingObject, TEXT("wallHeight"), Defaults.WallHeight));
	const float DefaultWallThickness = FMath::Max(1.0f, ReadNumber(BuildingObject, TEXT("wallThickness"), Defaults.WallThickness));
	const int32 DefaultFloorIndex = FMath::Max(1, ReadInt(BuildingObject, TEXT("floorIndex"), 1));
	const float PillarMergeTolerance = FMath::Max(0.0f, ReadNumber(BuildingObject, TEXT("pillarMergeTolerance"), 5.0f));
	const float DefaultFoundationExpansion = FMath::Max(
		0.0f,
		ReadNumber(
			BuildingObject,
			TEXT("foundationExpansion"),
			ReadNumber(Root, TEXT("foundationExpansion"), DefaultWallThickness)));

	FString Schema;
	Root->TryGetStringField(TEXT("schema"), Schema);
	FString Stage;
	Root->TryGetStringField(TEXT("stage"), Stage);
	const bool bFoundationOnlyWorkflow =
		Schema.Equals(TEXT("EHB_AI_Foundation_v1"), ESearchCase::IgnoreCase)
		|| Stage.Equals(TEXT("foundation_design"), ESearchCase::IgnoreCase)
		|| Stage.Equals(TEXT("foundation"), ESearchCase::IgnoreCase);

	TMap<int32, FEHBAIFloorSpec> FloorsByIndex;
	const TArray<TSharedPtr<FJsonValue>>* FloorValues = nullptr;
	if (Root->TryGetArrayField(TEXT("floors"), FloorValues) && FloorValues)
	{
		for (const TSharedPtr<FJsonValue>& FloorValue : *FloorValues)
		{
			const TSharedPtr<FJsonObject> FloorObject = FloorValue.IsValid() ? FloorValue->AsObject() : nullptr;
			if (!FloorObject.IsValid())
			{
				continue;
			}

			FEHBAIFloorSpec FloorSpec;
			FloorSpec.FloorIndex = FMath::Max(1, ReadInt(FloorObject, TEXT("floor"), DefaultFloorIndex));
			FloorSpec.bHasExplicitBaseZ = FloorObject->HasField(TEXT("baseZ"));
			FloorSpec.BaseZ = ReadNumber(FloorObject, TEXT("baseZ"), 0.0f);
			FloorSpec.WallHeight = FMath::Max(1.0f, ReadNumber(FloorObject, TEXT("wallHeight"), DefaultWallHeight));
			FloorSpec.WallThickness = FMath::Max(1.0f, ReadNumber(FloorObject, TEXT("wallThickness"), DefaultWallThickness));
			FloorSpec.CeilingSlabThickness = FMath::Max(1.0f, ReadNumber(FloorObject, TEXT("ceilingSlabThickness"), Defaults.FloorSlabThickness));
			FloorsByIndex.Add(FloorSpec.FloorIndex, FloorSpec);
		}
	}
	NormalizeStackedFloorBaseZs(FloorsByIndex);

	auto GetFloorSpec = [&FloorsByIndex, DefaultWallHeight, DefaultWallThickness, DefaultFloorIndex, Defaults](int32 FloorIndex)
	{
		const int32 SafeFloorIndex = FMath::Max(1, FloorIndex);
		if (const FEHBAIFloorSpec* FloorSpec = FloorsByIndex.Find(SafeFloorIndex))
		{
			return *FloorSpec;
		}

		FEHBAIFloorSpec Fallback;
		Fallback.FloorIndex = SafeFloorIndex;
		Fallback.BaseZ = SafeFloorIndex == DefaultFloorIndex ? 0.0f : (SafeFloorIndex - 1) * DefaultWallHeight;
		Fallback.WallHeight = DefaultWallHeight;
		Fallback.WallThickness = DefaultWallThickness;
		Fallback.CeilingSlabThickness = Defaults.FloorSlabThickness;
		return Fallback;
	};

	TArray<FEHBAIPillarSpec> PillarSpecs;
	TMap<FString, FVector> PillarLocationsById;
	TMap<FString, FString> PillarAliases;
	TSet<FString> SeenPillarIds;

	auto ResolvePillarId = [&PillarAliases](const FString& InId)
	{
		FString ResolvedId = InId;
		const FString* AliasTarget = PillarAliases.Find(ResolvedId);
		while (AliasTarget)
		{
			ResolvedId = *AliasTarget;
			AliasTarget = PillarAliases.Find(ResolvedId);
		}
		return ResolvedId;
	};

	if (!Root->HasTypedField<EJson::Array>(TEXT("pillars")) && FloorValues)
	{
		TArray<TSharedPtr<FJsonValue>> FlattenedPillarValues;
		for (const TSharedPtr<FJsonValue>& FloorValue : *FloorValues)
		{
			const TSharedPtr<FJsonObject> FloorObject = FloorValue.IsValid() ? FloorValue->AsObject() : nullptr;
			if (!FloorObject.IsValid())
			{
				continue;
			}

			const int32 NestedFloorIndex = FMath::Max(1, ReadInt(FloorObject, TEXT("floor"), DefaultFloorIndex));
			const TArray<TSharedPtr<FJsonValue>>* NestedPillarValues = nullptr;
			if (!FloorObject->TryGetArrayField(TEXT("pillars"), NestedPillarValues) || !NestedPillarValues)
			{
				continue;
			}

			for (const TSharedPtr<FJsonValue>& NestedPillarValue : *NestedPillarValues)
			{
				const TSharedPtr<FJsonObject> NestedPillarObject = NestedPillarValue.IsValid() ? NestedPillarValue->AsObject() : nullptr;
				if (NestedPillarObject.IsValid() && !NestedPillarObject->HasField(TEXT("floor")))
				{
					NestedPillarObject->SetNumberField(TEXT("floor"), NestedFloorIndex);
				}
				FlattenedPillarValues.Add(NestedPillarValue);
			}
		}

		if (!FlattenedPillarValues.IsEmpty())
		{
			Root->SetArrayField(TEXT("pillars"), MoveTemp(FlattenedPillarValues));
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* PillarValues = nullptr;
	if ((!Root->TryGetArrayField(TEXT("pillars"), PillarValues) || !PillarValues || PillarValues->IsEmpty()) && !bFoundationOnlyWorkflow)
	{
		return Fail(LOCTEXT("NoPillars", "JSON 中没有 pillars 数组，至少需要 2 个柱点。"));
	}

	if (PillarValues)
	{
	for (const TSharedPtr<FJsonValue>& PillarValue : *PillarValues)
	{
		const TSharedPtr<FJsonObject> PillarObject = PillarValue.IsValid() ? PillarValue->AsObject() : nullptr;
		if (!PillarObject.IsValid())
		{
			return Fail(LOCTEXT("InvalidPillarObject", "pillars 中存在无效对象。"));
		}

		FString PillarId;
		if (!PillarObject->TryGetStringField(TEXT("id"), PillarId) || PillarId.TrimStartAndEnd().IsEmpty())
		{
			return Fail(LOCTEXT("MissingPillarId", "每个 pillar 都需要非空 id。"));
		}

		PillarId = PillarId.TrimStartAndEnd();
		if (SeenPillarIds.Contains(PillarId))
		{
			return Fail(FText::Format(LOCTEXT("DuplicatePillarId", "重复的 pillar id：{0}"), FText::FromString(PillarId)));
		}
		SeenPillarIds.Add(PillarId);

		const int32 PillarFloorIndex = FMath::Max(1, ReadInt(PillarObject, TEXT("floor"), DefaultFloorIndex));
		const FEHBAIFloorSpec FloorSpec = GetFloorSpec(PillarFloorIndex);
		FVector LocalLocation;
		const bool bLocationHasExplicitZ = DoesVectorFieldContainZ(PillarObject, TEXT("location"));
		if (!TryReadVectorField(PillarObject, TEXT("location"), LocalLocation, FloorSpec.BaseZ))
		{
			return Fail(FText::Format(LOCTEXT("InvalidPillarLocation", "pillar {0} 缺少有效 location。"), FText::FromString(PillarId)));
		}

		if (FloorsByIndex.Contains(PillarFloorIndex))
		{
			const float ZSnapTolerance = FMath::Max(2.0f, FloorSpec.CeilingSlabThickness + 2.0f);
			if (!bLocationHasExplicitZ || FMath::Abs(LocalLocation.Z - FloorSpec.BaseZ) <= ZSnapTolerance)
			{
				LocalLocation.Z = FloorSpec.BaseZ;
			}
		}

		FString MergeTargetId;
		if (PillarMergeTolerance > 0.0f)
		{
			for (const FEHBAIPillarSpec& ExistingPillar : PillarSpecs)
			{
				const bool bSameFloor = ExistingPillar.FloorIndex == PillarFloorIndex;
				const bool bNear = FVector::Dist(ExistingPillar.LocalLocation, LocalLocation) <= PillarMergeTolerance;
				if (bSameFloor && bNear)
				{
					MergeTargetId = ExistingPillar.Id;
					break;
				}
			}
		}

		if (!MergeTargetId.IsEmpty())
		{
			PillarAliases.Add(PillarId, MergeTargetId);
			continue;
		}

		FEHBAIPillarSpec PillarSpec;
		PillarSpec.Id = PillarId;
		PillarSpec.LocalLocation = LocalLocation;
		PillarSpec.FloorIndex = PillarFloorIndex;
		PillarSpec.Height = FMath::Max(1.0f, ReadNumber(PillarObject, TEXT("height"), FloorSpec.WallHeight));
		PillarSpecs.Add(PillarSpec);
		PillarLocationsById.Add(PillarId, LocalLocation);
	}

	}

	if (PillarSpecs.Num() < 2 && !bFoundationOnlyWorkflow)
	{
		return Fail(LOCTEXT("TooFewPillars", "合并近距离柱子后，可用柱点少于 2 个。"));
	}

	TArray<FEHBAIWallSpec> WallSpecs;
	TMap<FString, int32> WallEdgeToIndex;
	if (!Root->HasTypedField<EJson::Array>(TEXT("walls")))
	{
		const TArray<TSharedPtr<FJsonValue>>* RootRelationValues = nullptr;
		if (Root->TryGetArrayField(TEXT("pillarRelations"), RootRelationValues) && RootRelationValues)
		{
			Root->SetArrayField(TEXT("walls"), *RootRelationValues);
		}
		else if (FloorValues)
		{
			TArray<TSharedPtr<FJsonValue>> FlattenedRelationValues;
			for (const TSharedPtr<FJsonValue>& FloorValue : *FloorValues)
			{
				const TSharedPtr<FJsonObject> FloorObject = FloorValue.IsValid() ? FloorValue->AsObject() : nullptr;
				if (!FloorObject.IsValid())
				{
					continue;
				}

				const int32 NestedFloorIndex = FMath::Max(1, ReadInt(FloorObject, TEXT("floor"), DefaultFloorIndex));
				const TArray<TSharedPtr<FJsonValue>>* NestedRelationValues = nullptr;
				if ((!FloorObject->TryGetArrayField(TEXT("pillarRelations"), NestedRelationValues) || !NestedRelationValues)
					&& (!FloorObject->TryGetArrayField(TEXT("walls"), NestedRelationValues) || !NestedRelationValues))
				{
					continue;
				}

				for (const TSharedPtr<FJsonValue>& NestedRelationValue : *NestedRelationValues)
				{
					const TSharedPtr<FJsonObject> NestedRelationObject = NestedRelationValue.IsValid() ? NestedRelationValue->AsObject() : nullptr;
					if (NestedRelationObject.IsValid() && !NestedRelationObject->HasField(TEXT("floor")))
					{
						NestedRelationObject->SetNumberField(TEXT("floor"), NestedFloorIndex);
					}
					FlattenedRelationValues.Add(NestedRelationValue);
				}
			}

			if (!FlattenedRelationValues.IsEmpty())
			{
				Root->SetArrayField(TEXT("walls"), MoveTemp(FlattenedRelationValues));
			}
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* WallValues = nullptr;
	if (Root->TryGetArrayField(TEXT("walls"), WallValues) && WallValues)
	{
		for (const TSharedPtr<FJsonValue>& WallValue : *WallValues)
		{
			const TSharedPtr<FJsonObject> WallObject = WallValue.IsValid() ? WallValue->AsObject() : nullptr;
			if (!WallObject.IsValid())
			{
				return Fail(LOCTEXT("InvalidWallObject", "walls 中存在无效对象。"));
			}

			FEHBAIWallSpec WallSpec;
			if (!WallObject->TryGetStringField(TEXT("start"), WallSpec.StartId) || !WallObject->TryGetStringField(TEXT("end"), WallSpec.EndId))
			{
				return Fail(LOCTEXT("WallMissingEndpoint", "每个 wall 都需要 start 和 end。"));
			}

			WallSpec.StartId = ResolvePillarId(WallSpec.StartId.TrimStartAndEnd());
			WallSpec.EndId = ResolvePillarId(WallSpec.EndId.TrimStartAndEnd());
			if (!PillarLocationsById.Contains(WallSpec.StartId) || !PillarLocationsById.Contains(WallSpec.EndId))
			{
				return Fail(FText::Format(
					LOCTEXT("WallUnknownEndpoint", "wall 引用了不存在的柱点：{0} -> {1}"),
					FText::FromString(WallSpec.StartId),
					FText::FromString(WallSpec.EndId)));
			}

			const int32 WallFloorIndex = FMath::Max(1, ReadInt(WallObject, TEXT("floor"), DefaultFloorIndex));
			const FEHBAIFloorSpec FloorSpec = GetFloorSpec(WallFloorIndex);
			WallSpec.FloorIndex = WallFloorIndex;
			WallSpec.Height = FMath::Max(1.0f, ReadNumber(WallObject, TEXT("height"), FloorSpec.WallHeight));
			WallSpec.Thickness = FMath::Max(1.0f, ReadNumber(WallObject, TEXT("thickness"), FloorSpec.WallThickness));
			AddOrMergeWallSpec(WallSpec, WallSpecs, WallEdgeToIndex);
		}
	}

	TArray<FEHBAIRoomSpec> RoomSpecs;
	const TArray<TSharedPtr<FJsonValue>>* RoomValues = nullptr;
	if (Root->TryGetArrayField(TEXT("rooms"), RoomValues) && RoomValues)
	{
		for (const TSharedPtr<FJsonValue>& RoomValue : *RoomValues)
		{
			const TSharedPtr<FJsonObject> RoomObject = RoomValue.IsValid() ? RoomValue->AsObject() : nullptr;
			if (!RoomObject.IsValid())
			{
				return Fail(LOCTEXT("InvalidRoomObject", "rooms 中存在无效对象。"));
			}

			FEHBAIRoomSpec RoomSpec;
			RoomObject->TryGetStringField(TEXT("id"), RoomSpec.Id);
			RoomSpec.FloorIndex = FMath::Max(1, ReadInt(RoomObject, TEXT("floor"), DefaultFloorIndex));
			const FEHBAIFloorSpec FloorSpec = GetFloorSpec(RoomSpec.FloorIndex);
			RoomSpec.Height = FMath::Max(1.0f, ReadNumber(RoomObject, TEXT("height"), FloorSpec.WallHeight));
			RoomSpec.Thickness = FMath::Max(1.0f, ReadNumber(RoomObject, TEXT("thickness"), FloorSpec.WallThickness));

			if (!TryReadStringLoop(RoomObject, TEXT("pillarLoop"), ResolvePillarId, RoomSpec.PillarLoop))
			{
				return Fail(LOCTEXT("InvalidRoomLoop", "每个 room 都需要至少 3 个有效 pillarLoop 柱点。"));
			}

			for (const FString& PillarId : RoomSpec.PillarLoop)
			{
				if (!PillarLocationsById.Contains(PillarId))
				{
					return Fail(FText::Format(LOCTEXT("RoomUnknownPillar", "room 引用了不存在的柱点：{0}"), FText::FromString(PillarId)));
				}
			}

			for (int32 Index = 0; Index < RoomSpec.PillarLoop.Num(); ++Index)
			{
				FEHBAIWallSpec WallSpec;
				WallSpec.StartId = RoomSpec.PillarLoop[Index];
				WallSpec.EndId = RoomSpec.PillarLoop[(Index + 1) % RoomSpec.PillarLoop.Num()];
				WallSpec.FloorIndex = RoomSpec.FloorIndex;
				WallSpec.Height = RoomSpec.Height;
				WallSpec.Thickness = RoomSpec.Thickness;
				AddOrMergeWallSpec(WallSpec, WallSpecs, WallEdgeToIndex);
			}

			RoomSpecs.Add(MoveTemp(RoomSpec));
		}
	}

	if (WallSpecs.IsEmpty() && !bFoundationOnlyWorkflow)
	{
		return Fail(LOCTEXT("NoWallsOrRooms", "JSON 中没有可生成的墙体连接。请提供 rooms[].pillarLoop 或 walls 数组。"));
	}

	if (!WallSpecs.IsEmpty())
	{
		RemoveRedundantOverlappingWallSpecs(WallSpecs, WallEdgeToIndex, PillarLocationsById);
	}

	TArray<FEHBAIWallCurveSpec> WallCurveSpecs;
	const TArray<TSharedPtr<FJsonValue>>* WallCurveValues = nullptr;
	if (Root->TryGetArrayField(TEXT("wallCurves"), WallCurveValues) && WallCurveValues)
	{
		for (const TSharedPtr<FJsonValue>& CurveValue : *WallCurveValues)
		{
			const TSharedPtr<FJsonObject> CurveObject = CurveValue.IsValid() ? CurveValue->AsObject() : nullptr;
			if (!CurveObject.IsValid())
			{
				return Fail(LOCTEXT("InvalidCurveObject", "wallCurves 中存在无效对象。"));
			}

			FEHBAIWallCurveSpec CurveSpec;
			if (!CurveObject->TryGetStringField(TEXT("start"), CurveSpec.StartId) || !CurveObject->TryGetStringField(TEXT("end"), CurveSpec.EndId))
			{
				return Fail(LOCTEXT("CurveMissingEndpoint", "每个 wallCurve 都需要 start 和 end。"));
			}

			CurveSpec.StartId = ResolvePillarId(CurveSpec.StartId.TrimStartAndEnd());
			CurveSpec.EndId = ResolvePillarId(CurveSpec.EndId.TrimStartAndEnd());
			if (!WallEdgeToIndex.Contains(MakeWallEdgeKey(CurveSpec.StartId, CurveSpec.EndId)))
			{
				return Fail(FText::Format(
					LOCTEXT("CurveUnknownWall", "wallCurve 没有找到对应墙体连接：{0} -> {1}"),
					FText::FromString(CurveSpec.StartId),
					FText::FromString(CurveSpec.EndId)));
			}

			CurveSpec.ControlOffset = ReadNumber(CurveObject, TEXT("controlOffset"), 0.0f);
			CurveSpec.SegmentLength = FMath::Max(10.0f, ReadNumber(CurveObject, TEXT("segmentLength"), 50.0f));
			WallCurveSpecs.Add(CurveSpec);
		}
	}

	TArray<FEHBAIDoorWindowSpec> DoorWindowSpecs;
	auto AppendDoorWindowValuesFromField = [&Root, FloorValues, DefaultFloorIndex](const TCHAR* FieldName)
	{
		TArray<TSharedPtr<FJsonValue>> CombinedValues;

		const TArray<TSharedPtr<FJsonValue>>* RootValues = nullptr;
		if (Root->TryGetArrayField(FieldName, RootValues) && RootValues)
		{
			CombinedValues.Append(*RootValues);
		}

		if (FloorValues)
		{
			for (const TSharedPtr<FJsonValue>& FloorValue : *FloorValues)
			{
				const TSharedPtr<FJsonObject> FloorObject = FloorValue.IsValid() ? FloorValue->AsObject() : nullptr;
				if (!FloorObject.IsValid())
				{
					continue;
				}

				const TArray<TSharedPtr<FJsonValue>>* NestedValues = nullptr;
				if (!FloorObject->TryGetArrayField(FieldName, NestedValues) || !NestedValues)
				{
					continue;
				}

				for (const TSharedPtr<FJsonValue>& NestedValue : *NestedValues)
				{
					const TSharedPtr<FJsonObject> NestedObject = NestedValue.IsValid() ? NestedValue->AsObject() : nullptr;
					if (NestedObject.IsValid() && !NestedObject->HasField(TEXT("floor")))
					{
						NestedObject->SetNumberField(TEXT("floor"), ReadInt(FloorObject, TEXT("floor"), DefaultFloorIndex));
					}
					CombinedValues.Add(NestedValue);
				}
			}
		}

		return CombinedValues;
	};

	auto ParseDoorWindowValues = [&](const TArray<TSharedPtr<FJsonValue>>& Values) -> FText
	{
		for (const TSharedPtr<FJsonValue>& DoorWindowValue : Values)
		{
			const TSharedPtr<FJsonObject> DoorWindowObject = DoorWindowValue.IsValid() ? DoorWindowValue->AsObject() : nullptr;
			if (!DoorWindowObject.IsValid())
			{
				return LOCTEXT("InvalidDoorWindowObject", "openings/doorWindows 中存在无效对象。");
			}

			FEHBAIDoorWindowSpec DoorWindowSpec;
			DoorWindowObject->TryGetStringField(TEXT("id"), DoorWindowSpec.Id);
			if (!DoorWindowObject->TryGetStringField(TEXT("start"), DoorWindowSpec.StartId))
			{
				DoorWindowObject->TryGetStringField(TEXT("wallStart"), DoorWindowSpec.StartId);
			}
			if (!DoorWindowObject->TryGetStringField(TEXT("end"), DoorWindowSpec.EndId))
			{
				DoorWindowObject->TryGetStringField(TEXT("wallEnd"), DoorWindowSpec.EndId);
			}
			if (DoorWindowSpec.StartId.TrimStartAndEnd().IsEmpty() || DoorWindowSpec.EndId.TrimStartAndEnd().IsEmpty())
			{
				return LOCTEXT("DoorWindowMissingWall", "每个 opening/doorWindow 都需要 start/end 或 wallStart/wallEnd。");
			}

			DoorWindowSpec.StartId = ResolvePillarId(DoorWindowSpec.StartId.TrimStartAndEnd());
			DoorWindowSpec.EndId = ResolvePillarId(DoorWindowSpec.EndId.TrimStartAndEnd());
			if (!WallEdgeToIndex.Contains(MakeWallEdgeKey(DoorWindowSpec.StartId, DoorWindowSpec.EndId)))
			{
				return FText::Format(
					LOCTEXT("DoorWindowUnknownWall", "opening/doorWindow 没有找到对应墙体连接：{0} -> {1}"),
					FText::FromString(DoorWindowSpec.StartId),
					FText::FromString(DoorWindowSpec.EndId));
			}

			FString KindString;
			if (!DoorWindowObject->TryGetStringField(TEXT("kind"), KindString))
			{
				DoorWindowObject->TryGetStringField(TEXT("type"), KindString);
			}
			KindString = KindString.TrimStartAndEnd().ToLower();
			DoorWindowSpec.Kind = KindString == TEXT("door")
				? EEHBDoorWindowElementKind::Door
				: EEHBDoorWindowElementKind::Window;

			double DistanceValue = 0.0;
			if (DoorWindowObject->TryGetNumberField(TEXT("distanceFromStart"), DistanceValue)
				|| DoorWindowObject->TryGetNumberField(TEXT("distance"), DistanceValue))
			{
				DoorWindowSpec.DistanceFromStart = FMath::Max(0.0f, static_cast<float>(DistanceValue));
				DoorWindowSpec.bUseRatio = false;
			}
			else
			{
				DoorWindowSpec.Ratio = FMath::Clamp(ReadNumber(DoorWindowObject, TEXT("ratio"), 0.5f), 0.0f, 1.0f);
				DoorWindowSpec.bUseRatio = true;
			}

			DoorWindowSpec.Width = FMath::Max(10.0f, ReadNumber(DoorWindowObject, TEXT("width"), ReadNumber(DoorWindowObject, TEXT("openingWidth"), DoorWindowSpec.Kind == EEHBDoorWindowElementKind::Door ? 110.0f : 120.0f)));
			DoorWindowSpec.Height = FMath::Max(10.0f, ReadNumber(DoorWindowObject, TEXT("height"), ReadNumber(DoorWindowObject, TEXT("openingHeight"), DoorWindowSpec.Kind == EEHBDoorWindowElementKind::Door ? 220.0f : 150.0f)));
			DoorWindowSpec.SillHeight = DoorWindowSpec.Kind == EEHBDoorWindowElementKind::Door
				? 0.0f
				: FMath::Max(0.0f, ReadNumber(DoorWindowObject, TEXT("sillHeight"), ReadNumber(DoorWindowObject, TEXT("bottomHeight"), 90.0f)));
			DoorWindowSpec.Thickness = FMath::Max(1.0f, ReadNumber(DoorWindowObject, TEXT("thickness"), ReadNumber(DoorWindowObject, TEXT("openingThickness"), DefaultWallThickness + 8.0f)));

			const TArray<TSharedPtr<FJsonValue>>* RatioValues = nullptr;
			if (DoorWindowObject->TryGetArrayField(TEXT("ratios"), RatioValues) && RatioValues && RatioValues->Num() > 0)
			{
				for (int32 RatioIndex = 0; RatioIndex < RatioValues->Num(); ++RatioIndex)
				{
					double RatioValue = 0.5;
					if (!(*RatioValues)[RatioIndex].IsValid() || !(*RatioValues)[RatioIndex]->TryGetNumber(RatioValue))
					{
						continue;
					}

					FEHBAIDoorWindowSpec MultiSpec = DoorWindowSpec;
					if (!MultiSpec.Id.IsEmpty())
					{
						MultiSpec.Id = FString::Printf(TEXT("%s_%d"), *MultiSpec.Id, RatioIndex + 1);
					}
					MultiSpec.Ratio = FMath::Clamp(static_cast<float>(RatioValue), 0.0f, 1.0f);
					MultiSpec.bUseRatio = true;
					DoorWindowSpecs.Add(MultiSpec);
				}
				continue;
			}

			const TArray<TSharedPtr<FJsonValue>>* DistanceValues = nullptr;
			if (DoorWindowObject->TryGetArrayField(TEXT("distances"), DistanceValues) && DistanceValues && DistanceValues->Num() > 0)
			{
				for (int32 DistanceIndex = 0; DistanceIndex < DistanceValues->Num(); ++DistanceIndex)
				{
					double MultiDistanceValue = 0.0;
					if (!(*DistanceValues)[DistanceIndex].IsValid() || !(*DistanceValues)[DistanceIndex]->TryGetNumber(MultiDistanceValue))
					{
						continue;
					}

					FEHBAIDoorWindowSpec MultiSpec = DoorWindowSpec;
					if (!MultiSpec.Id.IsEmpty())
					{
						MultiSpec.Id = FString::Printf(TEXT("%s_%d"), *MultiSpec.Id, DistanceIndex + 1);
					}
					MultiSpec.DistanceFromStart = FMath::Max(0.0f, static_cast<float>(MultiDistanceValue));
					MultiSpec.bUseRatio = false;
					DoorWindowSpecs.Add(MultiSpec);
				}
				continue;
			}

			DoorWindowSpecs.Add(DoorWindowSpec);
		}

		return FText::GetEmpty();
	};

	const FText OpeningParseError = ParseDoorWindowValues(AppendDoorWindowValuesFromField(TEXT("openings")));
	if (!OpeningParseError.IsEmpty())
	{
		return Fail(OpeningParseError);
	}
	const FText DoorWindowParseError = ParseDoorWindowValues(AppendDoorWindowValuesFromField(TEXT("doorWindows")));
	if (!DoorWindowParseError.IsEmpty())
	{
		return Fail(DoorWindowParseError);
	}

	TArray<FEHBAISlabSpec> SlabSpecs;
	auto AddSlabFromObject = [&](const TSharedPtr<FJsonObject>& SlabObject, bool bDefaultFoundation, const FString& DefaultId) -> FText
	{
		if (!SlabObject.IsValid())
		{
			return LOCTEXT("InvalidSlabObject", "slab 对象无效。");
		}

		FEHBAISlabSpec SlabSpec;
		SlabSpec.Id = DefaultId;
		SlabObject->TryGetStringField(TEXT("id"), SlabSpec.Id);
		FString Type;
		SlabObject->TryGetStringField(TEXT("type"), Type);
		Type = Type.TrimStartAndEnd().ToLower();
		SlabSpec.bIsFoundation = bDefaultFoundation || Type == TEXT("foundation");
		SlabSpec.Thickness = FMath::Max(1.0f, ReadNumber(SlabObject, TEXT("thickness"), Defaults.FloorSlabThickness));
		SlabSpec.FloorIndex = SlabSpec.bIsFoundation ? 0 : FMath::Max(1, ReadInt(SlabObject, TEXT("floorIndex"), DefaultFloorIndex));
		SlabSpec.bHasExplicitTopZ = SlabObject->HasField(TEXT("z")) || SlabObject->HasField(TEXT("topZ"));
		SlabSpec.TopZ = ReadNumber(SlabObject, TEXT("z"), ReadNumber(SlabObject, TEXT("topZ"), SlabSpec.bIsFoundation ? 0.0f : GetFloorSpec(SlabSpec.FloorIndex).BaseZ));

		if (!TryReadPolygonField(SlabObject, TEXT("polygon"), 0.0f, SlabSpec.LocalTopPolygon)
			&& !TryReadPolygonField(SlabObject, TEXT("outline"), 0.0f, SlabSpec.LocalTopPolygon))
		{
			TArray<FString> SlabLoop;
			if (!TryReadStringLoop(SlabObject, TEXT("pillarLoop"), ResolvePillarId, SlabLoop)
				&& !TryReadStringLoop(SlabObject, TEXT("outline"), ResolvePillarId, SlabLoop))
			{
				return FText::Format(
					LOCTEXT("InvalidSlabPolygon", "{0} 缺少有效 polygon、pillarLoop 或 outline。"),
					FText::FromString(SlabSpec.Id.IsEmpty() ? TEXT("slab") : SlabSpec.Id));
			}

			if (!TryBuildPolygonFromPillarLoop(SlabLoop, PillarLocationsById, SlabSpec.LocalTopPolygon))
			{
				return FText::Format(
					LOCTEXT("InvalidSlabLoop", "{0} 引用了不存在的柱点。"),
					FText::FromString(SlabSpec.Id.IsEmpty() ? TEXT("slab") : SlabSpec.Id));
			}
		}

		if (SlabSpec.bIsFoundation)
		{
			SlabSpec.bKeepBottomOnGround = ReadBool(SlabObject, TEXT("keepFoundationBottomOnGround"), true);
			if (SlabSpec.bKeepBottomOnGround && !SlabSpec.bHasExplicitTopZ && SlabSpec.TopZ <= UE_KINDA_SMALL_NUMBER)
			{
				SlabSpec.TopZ = SlabSpec.Thickness;
			}
			SlabSpec.bHasAIFoundationSource = true;
			SlabSpec.SourceJson = WriteJsonObjectToString(SlabObject);
			SlabSpec.AIDesignTopPolygon = SlabSpec.LocalTopPolygon;
			SlabSpec.VisualExpansion = FMath::Max(
				0.0f,
				ReadNumber(SlabObject, TEXT("visualExpansion"), ReadNumber(SlabObject, TEXT("expandMargin"), DefaultFoundationExpansion)));
			SlabSpec.FoundationExpansion = SlabSpec.VisualExpansion;
		}

		SlabSpecs.Add(MoveTemp(SlabSpec));
		return FText::GetEmpty();
	};

	const TSharedPtr<FJsonObject>* FoundationObjectPtr = nullptr;
	if (Root->TryGetObjectField(TEXT("foundation"), FoundationObjectPtr) && FoundationObjectPtr && FoundationObjectPtr->IsValid())
	{
		const FText ErrorText = AddSlabFromObject(*FoundationObjectPtr, true, TEXT("AI_Foundation"));
		if (!ErrorText.IsEmpty())
		{
			return Fail(ErrorText);
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* FoundationValues = nullptr;
	if (Root->TryGetArrayField(TEXT("foundations"), FoundationValues) && FoundationValues)
	{
		for (const TSharedPtr<FJsonValue>& FoundationValue : *FoundationValues)
		{
			const TSharedPtr<FJsonObject> FoundationObject = FoundationValue.IsValid() ? FoundationValue->AsObject() : nullptr;
			const FText ErrorText = AddSlabFromObject(FoundationObject, true, TEXT("AI_Foundation"));
			if (!ErrorText.IsEmpty())
			{
				return Fail(ErrorText);
			}
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* SlabValues = nullptr;
	if (Root->TryGetArrayField(TEXT("slabs"), SlabValues) && SlabValues)
	{
		for (const TSharedPtr<FJsonValue>& SlabValue : *SlabValues)
		{
			const TSharedPtr<FJsonObject> SlabObject = SlabValue.IsValid() ? SlabValue->AsObject() : nullptr;
			const FText ErrorText = AddSlabFromObject(SlabObject, false, TEXT(""));
			if (!ErrorText.IsEmpty())
			{
				return Fail(ErrorText);
			}
		}
	}

	const bool bHasFoundation = SlabSpecs.ContainsByPredicate([](const FEHBAISlabSpec& SlabSpec)
	{
		return SlabSpec.bIsFoundation;
	});
	if (bFoundationOnlyWorkflow && !bHasFoundation)
	{
		return Fail(LOCTEXT("FoundationOnlyMissingFoundation", "地基阶段 JSON 需要包含 foundation 对象。"));
	}

	const TSharedPtr<FJsonObject>* AutoFoundationObjectPtr = nullptr;
	const bool bAutoFoundationEnabled = Root->TryGetObjectField(TEXT("autoFoundation"), AutoFoundationObjectPtr) && AutoFoundationObjectPtr && AutoFoundationObjectPtr->IsValid()
		? ReadBool(*AutoFoundationObjectPtr, TEXT("enabled"), true)
		: ReadBool(Root, TEXT("autoFoundation"), true);
	if (!bHasFoundation && bAutoFoundationEnabled && !bFoundationOnlyWorkflow)
	{
		int32 LowestFloorIndex = TNumericLimits<int32>::Max();
		for (const FEHBAIPillarSpec& PillarSpec : PillarSpecs)
		{
			LowestFloorIndex = FMath::Min(LowestFloorIndex, PillarSpec.FloorIndex);
		}

		TArray<FVector> FoundationPoints;
		for (const FEHBAIPillarSpec& PillarSpec : PillarSpecs)
		{
			if (PillarSpec.FloorIndex == LowestFloorIndex)
			{
				FoundationPoints.Add(PillarSpec.LocalLocation);
			}
		}

		TArray<FVector> FoundationHull;
		if (BuildConvexHull2D(FoundationPoints, FoundationHull))
		{
			const FEHBAIFloorSpec LowestFloorSpec = GetFloorSpec(LowestFloorIndex == TNumericLimits<int32>::Max() ? DefaultFloorIndex : LowestFloorIndex);
			const TSharedPtr<FJsonObject> AutoFoundationObject = (AutoFoundationObjectPtr && AutoFoundationObjectPtr->IsValid())
				? *AutoFoundationObjectPtr
				: nullptr;

			FEHBAISlabSpec AutoFoundationSpec;
			AutoFoundationSpec.Id = TEXT("AI_AutoFoundation");
			AutoFoundationSpec.bIsFoundation = true;
			AutoFoundationSpec.bKeepBottomOnGround = ReadBool(AutoFoundationObject, TEXT("keepFoundationBottomOnGround"), true);
			AutoFoundationSpec.Thickness = FMath::Max(1.0f, ReadNumber(AutoFoundationObject, TEXT("thickness"), 60.0f));
			AutoFoundationSpec.bHasExplicitTopZ = AutoFoundationObject.IsValid()
				&& (AutoFoundationObject->HasField(TEXT("z")) || AutoFoundationObject->HasField(TEXT("topZ")));
			AutoFoundationSpec.TopZ = ReadNumber(AutoFoundationObject, TEXT("z"), ReadNumber(AutoFoundationObject, TEXT("topZ"), LowestFloorSpec.BaseZ));
			if (AutoFoundationSpec.bKeepBottomOnGround
				&& !AutoFoundationSpec.bHasExplicitTopZ
				&& AutoFoundationSpec.TopZ <= UE_KINDA_SMALL_NUMBER)
			{
				AutoFoundationSpec.TopZ = AutoFoundationSpec.Thickness;
			}
			AutoFoundationSpec.FloorIndex = 0;
			AutoFoundationSpec.LocalTopPolygon = FoundationHull;
			AutoFoundationSpec.AIDesignTopPolygon = FoundationHull;
			AutoFoundationSpec.VisualExpansion = FMath::Max(
				0.0f,
				ReadNumber(AutoFoundationObject, TEXT("visualExpansion"), ReadNumber(AutoFoundationObject, TEXT("expandMargin"), DefaultFoundationExpansion)));
			AutoFoundationSpec.FoundationExpansion = AutoFoundationSpec.VisualExpansion;
			AutoFoundationSpec.bHasAIFoundationSource = true;
			AutoFoundationSpec.SourceJson = WriteJsonObjectToString(AutoFoundationObject);
			SlabSpecs.Insert(MoveTemp(AutoFoundationSpec), 0);
		}
	}

	const TSharedPtr<FJsonObject>* RoomCeilingObjectPtr = nullptr;
	if (Root->TryGetObjectField(TEXT("roomCeilingSlabs"), RoomCeilingObjectPtr) && RoomCeilingObjectPtr && RoomCeilingObjectPtr->IsValid())
	{
		const TSharedPtr<FJsonObject> RoomCeilingObject = *RoomCeilingObjectPtr;
		if (ReadBool(RoomCeilingObject, TEXT("enabled"), true))
		{
			const float DefaultCeilingThickness = FMath::Max(1.0f, ReadNumber(RoomCeilingObject, TEXT("defaultThickness"), Defaults.FloorSlabThickness));
			for (const FEHBAIRoomSpec& RoomSpec : RoomSpecs)
			{
				TArray<FVector> RoomPolygon;
				if (!TryBuildPolygonFromPillarLoop(RoomSpec.PillarLoop, PillarLocationsById, RoomPolygon))
				{
					return Fail(FText::Format(LOCTEXT("InvalidRoomCeilingLoop", "房间 {0} 的顶板轮廓无效。"), FText::FromString(RoomSpec.Id)));
				}

				const FEHBAIFloorSpec FloorSpec = GetFloorSpec(RoomSpec.FloorIndex);
				FEHBAISlabSpec SlabSpec;
				SlabSpec.Id = RoomSpec.Id.IsEmpty()
					? FString::Printf(TEXT("RoomCeiling_Floor%d"), RoomSpec.FloorIndex)
					: FString::Printf(TEXT("RoomCeiling_%s"), *RoomSpec.Id);
				SlabSpec.bIsFoundation = false;
				SlabSpec.Thickness = FMath::Max(1.0f, ReadNumber(RoomCeilingObject, TEXT("thickness"), FloorSpec.CeilingSlabThickness > 0.0f ? FloorSpec.CeilingSlabThickness : DefaultCeilingThickness));
				SlabSpec.TopZ = FloorSpec.BaseZ + RoomSpec.Height;
				SlabSpec.FloorIndex = RoomSpec.FloorIndex;
				SlabSpec.LocalTopPolygon = MoveTemp(RoomPolygon);
				SlabSpecs.Add(MoveTemp(SlabSpec));
			}
		}
	}

	TArray<FEHBAIRoofSpec> RoofSpecs;
	auto AppendRoofValuesFromField = [&Root, FloorValues, DefaultFloorIndex](const TCHAR* FieldName)
	{
		TArray<TSharedPtr<FJsonValue>> CombinedValues;

		const TSharedPtr<FJsonObject>* RootObject = nullptr;
		if (Root->TryGetObjectField(FieldName, RootObject) && RootObject && RootObject->IsValid())
		{
			CombinedValues.Add(MakeShared<FJsonValueObject>(*RootObject));
		}

		const TArray<TSharedPtr<FJsonValue>>* RootValues = nullptr;
		if (Root->TryGetArrayField(FieldName, RootValues) && RootValues)
		{
			CombinedValues.Append(*RootValues);
		}

		if (FloorValues)
		{
			for (const TSharedPtr<FJsonValue>& FloorValue : *FloorValues)
			{
				const TSharedPtr<FJsonObject> FloorObject = FloorValue.IsValid() ? FloorValue->AsObject() : nullptr;
				if (!FloorObject.IsValid())
				{
					continue;
				}

				const TArray<TSharedPtr<FJsonValue>>* NestedValues = nullptr;
				if (!FloorObject->TryGetArrayField(FieldName, NestedValues) || !NestedValues)
				{
					continue;
				}

				for (const TSharedPtr<FJsonValue>& NestedValue : *NestedValues)
				{
					const TSharedPtr<FJsonObject> NestedObject = NestedValue.IsValid() ? NestedValue->AsObject() : nullptr;
					if (NestedObject.IsValid() && !NestedObject->HasField(TEXT("floor")) && !NestedObject->HasField(TEXT("floorIndex")))
					{
						NestedObject->SetNumberField(TEXT("floor"), ReadInt(FloorObject, TEXT("floor"), DefaultFloorIndex));
					}
					CombinedValues.Add(NestedValue);
				}
			}
		}

		return CombinedValues;
	};

	auto ParseRoofValues = [&](const TArray<TSharedPtr<FJsonValue>>& Values) -> FText
	{
		for (const TSharedPtr<FJsonValue>& RoofValue : Values)
		{
			const TSharedPtr<FJsonObject> RoofObject = RoofValue.IsValid() ? RoofValue->AsObject() : nullptr;
			if (!RoofObject.IsValid())
			{
				return LOCTEXT("InvalidRoofObject", "roof object is invalid.");
			}

			FString TypeString;
			RoofObject->TryGetStringField(TEXT("type"), TypeString);
			TypeString = TypeString.TrimStartAndEnd().ToLower();
			const bool bIsGableRoofType = TypeString.IsEmpty()
				|| TypeString == TEXT("gable")
				|| TypeString == TEXT("gable_roof")
				|| TypeString == TEXT("default_gable");
			const bool bIsHipRoofType = TypeString == TEXT("hip")
				|| TypeString == TEXT("hip_roof")
				|| TypeString == TEXT("default_hip")
				|| TypeString == TEXT("four_slope")
				|| TypeString == TEXT("four_slope_roof")
				|| TypeString == TEXT("four_hip")
				|| TypeString == TEXT("four_hip_roof")
				|| TypeString == TEXT("four_side")
				|| TypeString == TEXT("four_side_roof")
				|| TypeString == TEXT("four_sided")
				|| TypeString == TEXT("four_sided_roof")
				|| TypeString == TEXT("hipped")
				|| TypeString == TEXT("hipped_roof")
				|| TypeString == TEXT("四坡")
				|| TypeString == TEXT("四坡屋顶")
				|| TypeString == TEXT("四边")
				|| TypeString == TEXT("四边屋顶")
				|| TypeString == TEXT("四面坡")
				|| TypeString == TEXT("四面坡屋顶");
			if (!bIsGableRoofType && !bIsHipRoofType)
			{
				return FText::Format(
					LOCTEXT("UnsupportedRoofType", "Unsupported roof type for AI workflow: {0}. Supported default roof types are gable and hip/four-slope."),
					FText::FromString(TypeString));
			}

			FEHBAIRoofSpec RoofSpec;
			RoofSpec.bUseHipRoof = bIsHipRoofType;
			RoofObject->TryGetStringField(TEXT("id"), RoofSpec.Id);
			if (RoofSpec.Id.IsEmpty())
			{
				RoofObject->TryGetStringField(TEXT("name"), RoofSpec.Id);
			}
			RoofSpec.FloorIndex = FMath::Max(1, ReadInt(RoofObject, TEXT("floor"), ReadInt(RoofObject, TEXT("floorIndex"), DefaultFloorIndex)));
			const FEHBAIFloorSpec FloorSpec = GetFloorSpec(RoofSpec.FloorIndex);
			const float DefaultRoofZ = ReadNumber(
				RoofObject,
				TEXT("roofZ"),
				ReadNumber(RoofObject, TEXT("z"), ReadNumber(RoofObject, TEXT("baseZ"), FloorSpec.BaseZ + FloorSpec.WallHeight)));

			bool bHasCenter = TryReadVectorField(RoofObject, TEXT("localCenter"), RoofSpec.LocalCenter, DefaultRoofZ);
			if (!bHasCenter)
			{
				bHasCenter = TryReadVectorField(RoofObject, TEXT("center"), RoofSpec.LocalCenter, DefaultRoofZ);
			}
			if (!bHasCenter)
			{
				bHasCenter = TryReadVectorField(RoofObject, TEXT("location"), RoofSpec.LocalCenter, DefaultRoofZ);
			}
			RoofSpec.bHasLocalCenter = bHasCenter;
			const bool bCenterHasExplicitZ =
				DoesVectorFieldContainZ(RoofObject, TEXT("localCenter"))
				|| DoesVectorFieldContainZ(RoofObject, TEXT("center"))
				|| DoesVectorFieldContainZ(RoofObject, TEXT("location"));
			if (!bCenterHasExplicitZ)
			{
				RoofSpec.LocalCenter.Z = DefaultRoofZ;
			}

			RoofSpec.YawDegrees = ReadNumber(RoofObject, TEXT("yawDegrees"), ReadNumber(RoofObject, TEXT("yaw"), 0.0f));
			double ExplicitRoofDimension = 0.0;
			RoofSpec.bHasLength = RoofObject->TryGetNumberField(TEXT("length"), ExplicitRoofDimension);
			RoofSpec.bHasWidth = RoofObject->TryGetNumberField(TEXT("width"), ExplicitRoofDimension);
			const bool bHasAutoFitField =
				RoofObject->HasField(TEXT("autoFitToWallFootprint"))
				|| RoofObject->HasField(TEXT("fitToWallFootprint"))
				|| RoofObject->HasField(TEXT("autoBounds"));
			const bool bAutoFitRequested =
				ReadBool(
					RoofObject,
					TEXT("autoFitToWallFootprint"),
					ReadBool(RoofObject, TEXT("fitToWallFootprint"), ReadBool(RoofObject, TEXT("autoBounds"), false)));
			RoofSpec.bAutoFitToWallFootprint =
				bAutoFitRequested || (!bHasAutoFitField && (!RoofSpec.bHasLocalCenter || !RoofSpec.bHasLength || !RoofSpec.bHasWidth));
			if (!RoofSpec.bHasLocalCenter)
			{
				if (!RoofSpec.bAutoFitToWallFootprint)
				{
					return FText::Format(
						LOCTEXT("RoofMissingCenter", "Roof {0} needs localCenter, center or location unless autoFitToWallFootprint is enabled."),
						FText::FromString(RoofSpec.Id.IsEmpty() ? TEXT("gable") : RoofSpec.Id));
				}
				RoofSpec.LocalCenter = FVector::ZeroVector;
				RoofSpec.LocalCenter.Z = DefaultRoofZ;
			}
			RoofSpec.Length = FMath::Max(1.0f, ReadNumber(RoofObject, TEXT("length"), 600.0f));
			RoofSpec.Width = FMath::Max(1.0f, ReadNumber(RoofObject, TEXT("width"), 400.0f));
			RoofSpec.AutoFitPadding = FMath::Max(
				0.0f,
				ReadNumber(RoofObject, TEXT("autoFitPadding"), ReadNumber(RoofObject, TEXT("fitPadding"), 0.0f)));
			RoofSpec.PitchDegrees = FMath::Clamp(ReadNumber(RoofObject, TEXT("pitchDegrees"), ReadNumber(RoofObject, TEXT("pitch"), 25.0f)), 1.0f, 89.0f);
			RoofSpec.Thickness = FMath::Max(0.1f, ReadNumber(RoofObject, TEXT("thickness"), 20.0f));
			RoofSpec.EaveOffset = FMath::Max(0.0f, ReadNumber(RoofObject, TEXT("eaveOffset"), 30.0f));
			RoofSpec.RidgeOffsetRatio = FMath::Clamp(ReadNumber(RoofObject, TEXT("ridgeOffsetRatio"), 0.0f), -0.45f, 0.45f);
			RoofSpec.AxisMode = ReadRoofAxisMode(RoofObject, RoofSpec.AxisMode);

			RoofSpec.bGenerateRidge = ReadBool(RoofObject, TEXT("bGenerateRidge"), ReadBool(RoofObject, TEXT("generateRidge"), true));
			RoofSpec.bGenerateEaves = ReadBool(RoofObject, TEXT("bGenerateEaves"), ReadBool(RoofObject, TEXT("generateEaves"), true));
			RoofSpec.bGenerateGableRakes = RoofSpec.bUseHipRoof
				? ReadBool(RoofObject, TEXT("bGenerateHipRidges"), ReadBool(RoofObject, TEXT("generateHipRidges"), true))
				: ReadBool(RoofObject, TEXT("bGenerateGableRakes"), ReadBool(RoofObject, TEXT("generateGableRakes"), true));
			RoofSpec.bGenerateGableEndWalls = RoofSpec.bUseHipRoof
				? false
				: ReadBool(RoofObject, TEXT("bGenerateGableEndWalls"), ReadBool(RoofObject, TEXT("generateGableEndWalls"), true));
			RoofSpec.RidgeWidth = FMath::Max(0.1f, ReadNumber(RoofObject, TEXT("ridgeWidth"), 18.0f));
			RoofSpec.RidgeHeight = FMath::Max(0.1f, ReadNumber(RoofObject, TEXT("ridgeHeight"), 10.0f));
			RoofSpec.EaveWidth = FMath::Max(0.1f, ReadNumber(RoofObject, TEXT("eaveWidth"), 18.0f));
			RoofSpec.EaveHeight = FMath::Max(0.1f, ReadNumber(RoofObject, TEXT("eaveHeight"), 18.0f));
			RoofSpec.GableRakeWidth = FMath::Max(
				0.1f,
				RoofSpec.bUseHipRoof
					? ReadNumber(RoofObject, TEXT("hipRidgeWidth"), ReadNumber(RoofObject, TEXT("gableRakeWidth"), 16.0f))
					: ReadNumber(RoofObject, TEXT("gableRakeWidth"), 16.0f));
			RoofSpec.GableRakeHeight = FMath::Max(
				0.1f,
				RoofSpec.bUseHipRoof
					? ReadNumber(RoofObject, TEXT("hipRidgeHeight"), ReadNumber(RoofObject, TEXT("gableRakeHeight"), 10.0f))
					: ReadNumber(RoofObject, TEXT("gableRakeHeight"), 10.0f));
			RoofSpec.GableEndWallBoundaryInset = FMath::Max(
				0.0f,
				ReadNumber(
					RoofObject,
					TEXT("gableEndWallBoundaryInset"),
					ReadNumber(RoofObject, TEXT("sideWallBoundaryInset"), 0.0f)));

			const TSharedPtr<FJsonObject>* CuttingObjectPtr = nullptr;
			const TSharedPtr<FJsonObject> CuttingObject = RoofObject->TryGetObjectField(TEXT("cutting"), CuttingObjectPtr) && CuttingObjectPtr
				? *CuttingObjectPtr
				: RoofObject;
			RoofSpec.bCutCollidingElements = ReadBool(CuttingObject, TEXT("bCutCollidingElements"), ReadBool(CuttingObject, TEXT("cutCollidingElements"), false));
			RoofSpec.bRemoveDisconnectedCutPieces = ReadBool(CuttingObject, TEXT("bRemoveDisconnectedCutPieces"), ReadBool(CuttingObject, TEXT("removeDisconnectedCutPieces"), true));
			RoofSpec.bKeepCutAwayDisconnectedPieces = ReadBool(CuttingObject, TEXT("bKeepCutAwayDisconnectedPieces"), ReadBool(CuttingObject, TEXT("keepCutAwayDisconnectedPieces"), false));
			RoofSpec.bUseExactSourceMeshCutters = ReadBool(CuttingObject, TEXT("bUseExactSourceMeshCutters"), ReadBool(CuttingObject, TEXT("useExactSourceMeshCutters"), true));
			RoofSpec.bUseControlledEnvelopeCutters = ReadBool(CuttingObject, TEXT("bUseControlledEnvelopeCutters"), ReadBool(CuttingObject, TEXT("useControlledEnvelopeCutters"), true));

			const TSharedPtr<FJsonObject>* EnvelopeObjectPtr = nullptr;
			const TSharedPtr<FJsonObject> EnvelopeObject = RoofObject->TryGetObjectField(TEXT("envelopeCutOptions"), EnvelopeObjectPtr) && EnvelopeObjectPtr
				? *EnvelopeObjectPtr
				: (RoofObject->TryGetObjectField(TEXT("envelope"), EnvelopeObjectPtr) && EnvelopeObjectPtr ? *EnvelopeObjectPtr : RoofObject);
			RoofSpec.EnvelopeCutOptions.ConcavityBridgeDistance = FMath::Max(0.0f, ReadNumber(EnvelopeObject, TEXT("concavityBridgeDistance"), RoofSpec.EnvelopeCutOptions.ConcavityBridgeDistance));
			RoofSpec.EnvelopeCutOptions.ProjectionPadding = FMath::Max(0.0f, ReadNumber(EnvelopeObject, TEXT("projectionPadding"), RoofSpec.EnvelopeCutOptions.ProjectionPadding));
			RoofSpec.EnvelopeCutOptions.ZPadding = FMath::Max(0.0f, ReadNumber(EnvelopeObject, TEXT("zPadding"), RoofSpec.EnvelopeCutOptions.ZPadding));
			RoofSpec.EnvelopeCutOptions.MinExtrudeHeight = FMath::Max(1.0f, ReadNumber(EnvelopeObject, TEXT("minExtrudeHeight"), RoofSpec.EnvelopeCutOptions.MinExtrudeHeight));
			RoofSpec.EnvelopeCutOptions.MinProjectedArea = FMath::Max(0.0f, ReadNumber(EnvelopeObject, TEXT("minProjectedArea"), RoofSpec.EnvelopeCutOptions.MinProjectedArea));
			RoofSpec.EnvelopeCutOptions.ThinProjectionFallbackWidth = FMath::Max(0.0f, ReadNumber(EnvelopeObject, TEXT("thinProjectionFallbackWidth"), RoofSpec.EnvelopeCutOptions.ThinProjectionFallbackWidth));
			RoofSpec.EnvelopeCutOptions.PathCleanTolerance = FMath::Max(0.0f, ReadNumber(EnvelopeObject, TEXT("pathCleanTolerance"), RoofSpec.EnvelopeCutOptions.PathCleanTolerance));

			const TSharedPtr<FJsonObject>* WallFootprintObjectPtr = nullptr;
			const TSharedPtr<FJsonObject> WallFootprintObject = RoofObject->TryGetObjectField(TEXT("wallFootprint"), WallFootprintObjectPtr) && WallFootprintObjectPtr
				? *WallFootprintObjectPtr
				: RoofObject;
			RoofSpec.bUseWallFootprintCutters = ReadBool(WallFootprintObject, TEXT("bUseWallFootprintCutters"), ReadBool(WallFootprintObject, TEXT("useWallFootprintCutters"), true));
			RoofSpec.MinWallFootprintGroupWallCount = FMath::Max(1, ReadInt(WallFootprintObject, TEXT("minWallFootprintGroupWallCount"), RoofSpec.MinWallFootprintGroupWallCount));
			RoofSpec.WallFootprintGroupEndpointTolerance = FMath::Max(0.0f, ReadNumber(WallFootprintObject, TEXT("wallFootprintGroupEndpointTolerance"), RoofSpec.WallFootprintGroupEndpointTolerance));
			RoofSpec.WallFootprintPadding = FMath::Clamp(ReadNumber(WallFootprintObject, TEXT("wallFootprintPadding"), RoofSpec.WallFootprintPadding), -100.0f, 100.0f);
			RoofSpec.WallFootprintMaxDimension = FMath::Max(0.0f, ReadNumber(WallFootprintObject, TEXT("wallFootprintMaxDimension"), RoofSpec.WallFootprintMaxDimension));
			RoofSpec.WallFootprintMinArea = FMath::Max(0.0f, ReadNumber(WallFootprintObject, TEXT("wallFootprintMinArea"), RoofSpec.WallFootprintMinArea));

			const TSharedPtr<FJsonObject>* DebugObjectPtr = nullptr;
			const TSharedPtr<FJsonObject> DebugObject = RoofObject->TryGetObjectField(TEXT("debug"), DebugObjectPtr) && DebugObjectPtr
				? *DebugObjectPtr
				: RoofObject;
			RoofSpec.bShowCutDebugVisualization = ReadBool(DebugObject, TEXT("bShowCutDebugVisualization"), ReadBool(DebugObject, TEXT("showCutDebugVisualization"), false));
			RoofSpec.bDebugDrawRawRoofBounds = ReadBool(DebugObject, TEXT("bDebugDrawRawRoofBounds"), ReadBool(DebugObject, TEXT("debugDrawRawRoofBounds"), true));
			RoofSpec.bDebugDrawSourceBounds = ReadBool(DebugObject, TEXT("bDebugDrawSourceBounds"), ReadBool(DebugObject, TEXT("debugDrawSourceBounds"), true));
			RoofSpec.bDebugDrawCutterBounds = ReadBool(DebugObject, TEXT("bDebugDrawCutterBounds"), ReadBool(DebugObject, TEXT("debugDrawCutterBounds"), true));
			RoofSpec.bDebugDrawResultBounds = ReadBool(DebugObject, TEXT("bDebugDrawResultBounds"), ReadBool(DebugObject, TEXT("debugDrawResultBounds"), true));
			RoofSpec.CutDebugDrawDuration = FMath::Max(0.1f, ReadNumber(DebugObject, TEXT("cutDebugDrawDuration"), RoofSpec.CutDebugDrawDuration));
			RoofSpec.CutDebugDrawThickness = FMath::Max(0.1f, ReadNumber(DebugObject, TEXT("cutDebugDrawThickness"), RoofSpec.CutDebugDrawThickness));

			RoofSpecs.Add(MoveTemp(RoofSpec));
		}

		return FText::GetEmpty();
	};

	const FText RoofParseError = ParseRoofValues(AppendRoofValuesFromField(TEXT("roofs")));
	if (!RoofParseError.IsEmpty())
	{
		return Fail(RoofParseError);
	}
	const FText SingularRoofParseError = ParseRoofValues(AppendRoofValuesFromField(TEXT("roof")));
	if (!SingularRoofParseError.IsEmpty())
	{
		return Fail(SingularRoofParseError);
	}

	const FScopedTransaction Transaction(LOCTEXT("ExecuteAIWorkflowTransaction", "Execute AI Building Workflow"));
	Building->Modify();

	UWorld* World = Building->GetWorld();
	const UEHBBuildingToolsetSettings* ToolsetSettings = GetDefault<UEHBBuildingToolsetSettings>();
	TSubclassOf<AEHB_Pillar> PillarClass = AEHB_Pillar::StaticClass();
	if (ToolsetSettings && !ToolsetSettings->PillarActorClass.IsNull())
	{
		if (UClass* LoadedPillarClass = ToolsetSettings->PillarActorClass.LoadSynchronous())
		{
			PillarClass = LoadedPillarClass;
		}
	}
	TSubclassOf<AEHBGableRoof> GableRoofClass = AEHBGableRoof::StaticClass();
	if (ToolsetSettings && !ToolsetSettings->GableRoofActorClass.IsNull())
	{
		if (UClass* LoadedGableRoofClass = ToolsetSettings->GableRoofActorClass.LoadSynchronous())
		{
			if (LoadedGableRoofClass->IsChildOf(AEHBGableRoof::StaticClass()))
			{
				GableRoofClass = LoadedGableRoofClass;
			}
		}
	}
	TSubclassOf<AEHBGableRoof> HipRoofClass = AEHBHipRoof::StaticClass();
	if (ToolsetSettings && !ToolsetSettings->HipRoofActorClass.IsNull())
	{
		if (UClass* LoadedHipRoofClass = ToolsetSettings->HipRoofActorClass.LoadSynchronous())
		{
			if (LoadedHipRoofClass->IsChildOf(AEHBHipRoof::StaticClass()))
			{
				HipRoofClass = LoadedHipRoofClass;
			}
		}
	}

	TMap<FString, AEHB_Pillar*> SpawnedPillars;
	TMap<FString, FEHBWallCreationEndpoint> PillarCreationEndpointsById;
	TSet<FGuid> SpawnedPillarGuids;
	for (const FEHBAIPillarSpec& PillarSpec : PillarSpecs)
	{
		const bool bUseUnifiedWallCreation = Building != nullptr;
		if (bUseUnifiedWallCreation)
		{
			FEHBWallCreationEndpoint Endpoint;
			const float PillarWallThickness = GetFloorSpec(PillarSpec.FloorIndex).WallThickness;
			const FVector DesiredWorldLocation = Building->GetActorTransform().TransformPosition(PillarSpec.LocalLocation);
			const float EndpointSnapDistance = FMath::Max(30.0f, PillarWallThickness * 2.0f);
			if (!Building->ResolveWallCreationEndpoint(
				DesiredWorldLocation,
				EndpointSnapDistance,
				PillarWallThickness,
				PillarSpec.FloorIndex,
				nullptr,
				Endpoint))
			{
				return Fail(FText::Format(LOCTEXT("ResolvePillarEndpointFailed", "瑙ｆ瀽鏌卞瓙浣嶇疆澶辫触锛歿0}"), FText::FromString(PillarSpec.Id)));
			}

			if (!Endpoint.Pillar && !Endpoint.Wall)
			{
				const FString ActorNamePrefix = FString::Printf(TEXT("EHB_AI_%s"), *PillarSpec.Id);
				Endpoint.Pillar = Building->CreatePillarAtLocalLocation(
					Endpoint.LocalLocation,
					FRotator::ZeroRotator,
					PillarSpec.Height,
					PillarWallThickness,
					PillarWallThickness,
					PillarSpec.FloorIndex,
					ActorNamePrefix,
					true);
				if (!Endpoint.Pillar)
				{
					return Fail(FText::Format(LOCTEXT("SpawnPillarFailed", "鐢熸垚鏌卞瓙澶辫触锛歿0}"), FText::FromString(PillarSpec.Id)));
				}
				Endpoint.WorldLocation = Endpoint.Pillar->GetActorLocation();
				Endpoint.LocalLocation = Endpoint.Pillar->GetElementLocalTransform().GetLocation();
				Endpoint.FloorIndex = FMath::Max(1, Endpoint.Pillar->FloorIndex);
			}

			PillarCreationEndpointsById.Add(PillarSpec.Id, Endpoint);
			if (Endpoint.Pillar)
			{
				SpawnedPillars.Add(PillarSpec.Id, Endpoint.Pillar);
				SpawnedPillarGuids.Add(Endpoint.Pillar->ElementGuid);
			}
			continue;
		}

		const FString ActorNamePrefix = FString::Printf(TEXT("EHB_AI_%s"), *PillarSpec.Id);
		const FName ActorName = MakeUniqueObjectName(Building->GetLevel(), PillarClass, FName(*ActorNamePrefix));

		FActorSpawnParameters SpawnParams;
		SpawnParams.Name = ActorName;
		SpawnParams.Owner = Building;
		SpawnParams.OverrideLevel = Building->GetLevel();
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnParams.ObjectFlags |= RF_Transactional;

		const FTransform LocalTransform(FRotator::ZeroRotator, PillarSpec.LocalLocation);
		const FTransform WorldTransform = LocalTransform * Building->GetActorTransform();
		AEHB_Pillar* Pillar = World->SpawnActor<AEHB_Pillar>(PillarClass, WorldTransform, SpawnParams);
		if (!Pillar)
		{
			return Fail(FText::Format(LOCTEXT("SpawnPillarFailed", "生成柱子失败：{0}"), FText::FromString(PillarSpec.Id)));
		}

		Pillar->SetFlags(RF_Transactional);
		Pillar->Modify();
		Pillar->ElementName = ActorName;
		Pillar->AttachToBuilding(Building, LocalTransform);
		const float PillarWallThickness = GetFloorSpec(PillarSpec.FloorIndex).WallThickness;
		Pillar->ConfigureAsPolygonPillar(PillarSpec.Height, PillarWallThickness, PillarWallThickness, LocalTransform, true);
		Pillar->SetFloorAssignment(PillarSpec.FloorIndex, EEHBBuildingFloorElementRole::FloorBody);
#if WITH_EDITOR
		Pillar->SetActorLabel(ActorName.ToString());
#endif
		SpawnedPillars.Add(PillarSpec.Id, Pillar);
		SpawnedPillarGuids.Add(Pillar->ElementGuid);
	}

	int32 CreatedWallCount = 0;
	TMap<FString, AEHB_Wall*> SpawnedWallsByEdge;
	for (const FEHBAIWallSpec& WallSpec : WallSpecs)
	{
		if (FEHBWallCreationEndpoint* StartEndpoint = PillarCreationEndpointsById.Find(WallSpec.StartId))
		{
			if (FEHBWallCreationEndpoint* EndEndpoint = PillarCreationEndpointsById.Find(WallSpec.EndId))
			{
				FEHBWallCreationOptions WallCreationOptions;
				WallCreationOptions.WallHeight = WallSpec.Height;
				WallCreationOptions.WallThickness = WallSpec.Thickness;
				WallCreationOptions.PillarHeight = WallSpec.Height;
				WallCreationOptions.PillarWidth = WallSpec.Thickness;
				WallCreationOptions.PillarDepth = WallSpec.Thickness;
				WallCreationOptions.EndpointSnapDistance = FMath::Max(30.0f, WallSpec.Thickness * 2.0f);
				WallCreationOptions.FloorIndex = WallSpec.FloorIndex;
				WallCreationOptions.NewPillarNamePrefix = TEXT("EHB_AI_Pillar");

				FEHBWallCreationResult WallCreationResult;
				if (Building->CreateOrReuseWallSegment(*StartEndpoint, *EndEndpoint, WallCreationOptions, WallCreationResult)
					&& WallCreationResult.PrimaryWall)
				{
					++CreatedWallCount;
					SpawnedWallsByEdge.Add(MakeWallEdgeKey(WallSpec.StartId, WallSpec.EndId), WallCreationResult.PrimaryWall);
					if (StartEndpoint->Pillar)
					{
						SpawnedPillars.Add(WallSpec.StartId, StartEndpoint->Pillar);
						SpawnedPillarGuids.Add(StartEndpoint->Pillar->ElementGuid);
					}
					if (EndEndpoint->Pillar)
					{
						SpawnedPillars.Add(WallSpec.EndId, EndEndpoint->Pillar);
						SpawnedPillarGuids.Add(EndEndpoint->Pillar->ElementGuid);
					}
					continue;
				}
			}
		}

		AEHB_Pillar* const* StartPillar = SpawnedPillars.Find(WallSpec.StartId);
		AEHB_Pillar* const* EndPillar = SpawnedPillars.Find(WallSpec.EndId);
		AEHB_Wall* Wall = (StartPillar && EndPillar)
			? Building->ConnectPillars(*StartPillar, *EndPillar, WallSpec.Height, WallSpec.Thickness)
			: nullptr;
		if (Wall)
		{
			++CreatedWallCount;
			SpawnedWallsByEdge.Add(MakeWallEdgeKey(WallSpec.StartId, WallSpec.EndId), Wall);
		}
	}

	int32 CurvedWallCount = 0;
	for (const FEHBAIWallCurveSpec& CurveSpec : WallCurveSpecs)
	{
		AEHB_Wall* const* Wall = SpawnedWallsByEdge.Find(MakeWallEdgeKey(CurveSpec.StartId, CurveSpec.EndId));
		if (!Wall || !(*Wall))
		{
			continue;
		}

		(*Wall)->Modify();
		(*Wall)->ApplyCurveSettings(CurveSpec.ControlOffset, CurveSpec.SegmentLength, true);
		++CurvedWallCount;
	}

	int32 CreatedDoorWindowCount = 0;
	for (const FEHBAIDoorWindowSpec& DoorWindowSpec : DoorWindowSpecs)
	{
		AEHB_Wall* const* WallPtr = SpawnedWallsByEdge.Find(MakeWallEdgeKey(DoorWindowSpec.StartId, DoorWindowSpec.EndId));
		AEHB_Wall* Wall = WallPtr ? *WallPtr : nullptr;
		if (!Wall)
		{
			continue;
		}

		const float WallLength = FVector::Dist2D(Wall->LocalStart, Wall->LocalEnd);
		if (WallLength <= UE_SMALL_NUMBER)
		{
			continue;
		}

		const float DistanceFromStart = DoorWindowSpec.bUseRatio
			? WallLength * FMath::Clamp(DoorWindowSpec.Ratio, 0.0f, 1.0f)
			: DoorWindowSpec.DistanceFromStart;
		const float ClampedDistance = FMath::Clamp(DistanceFromStart, 5.0f, FMath::Max(5.0f, WallLength - 5.0f));
		const float BottomHeight = DoorWindowSpec.Kind == EEHBDoorWindowElementKind::Door ? 0.0f : DoorWindowSpec.SillHeight;
		const FVector WallDirection = (Wall->LocalEnd - Wall->LocalStart).GetSafeNormal2D();
		if (WallDirection.IsNearlyZero())
		{
			continue;
		}

		const float Alpha = ClampedDistance / WallLength;
		FVector LocalLocation = Wall->LocalStart + WallDirection * ClampedDistance;
		LocalLocation.Z = FMath::Lerp(Wall->LocalStart.Z, Wall->LocalEnd.Z, Alpha) + BottomHeight;
		const FRotator LocalRotation(0.0f, FMath::RadiansToDegrees(FMath::Atan2(WallDirection.Y, WallDirection.X)), 0.0f);
		const FTransform LocalTransform(LocalRotation, LocalLocation);
		const FTransform WorldTransform = LocalTransform * Building->GetActorTransform();

		const FString DoorWindowName = DoorWindowSpec.Id.IsEmpty()
			? FString::Printf(TEXT("EHB_AI_%s"), DoorWindowSpec.Kind == EEHBDoorWindowElementKind::Door ? TEXT("Door") : TEXT("Window"))
			: FString::Printf(TEXT("EHB_AI_%s"), *DoorWindowSpec.Id);
		const FName ActorName = MakeUniqueObjectName(Building->GetLevel(), AEHB_DoorWindow::StaticClass(), FName(*DoorWindowName));

		FActorSpawnParameters SpawnParams;
		SpawnParams.Name = ActorName;
		SpawnParams.Owner = Building;
		SpawnParams.OverrideLevel = Building->GetLevel();
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnParams.ObjectFlags |= RF_Transactional;

		AEHB_DoorWindow* DoorWindow = World->SpawnActor<AEHB_DoorWindow>(AEHB_DoorWindow::StaticClass(), WorldTransform, SpawnParams);
		if (!DoorWindow)
		{
			continue;
		}

		DoorWindow->SetFlags(RF_Transactional);
		DoorWindow->Modify();
		DoorWindow->ElementName = ActorName;
		DoorWindow->Kind = DoorWindowSpec.Kind;
		DoorWindow->SillHeight = DoorWindowSpec.SillHeight;
		DoorWindow->OpeningWidth = DoorWindowSpec.Width;
		DoorWindow->OpeningHeight = DoorWindowSpec.Height;
		DoorWindow->OpeningThickness = DoorWindowSpec.Thickness;
		DoorWindow->AttachToBuilding(Building, LocalTransform);
		DoorWindow->SetFloorAssignment(Wall->FloorIndex, EEHBBuildingFloorElementRole::HostedElement);
		if (DoorWindowSpec.Kind == EEHBDoorWindowElementKind::Door)
		{
			DoorWindow->InitializeDefaultDoorOpening();
		}
		else
		{
			DoorWindow->InitializeDefaultWindowOpening();
		}
		DoorWindow->BindToWall(Wall, ClampedDistance);
		Wall->RebuildWallMesh();
		DoorWindow->MarkPackageDirty();
#if WITH_EDITOR
		DoorWindow->SetActorLabel(ActorName.ToString());
#endif
		++CreatedDoorWindowCount;
	}

	if (!SpawnedPillars.IsEmpty() && CreatedWallCount > 0)
	{
		TSet<FGuid> RefreshedPillarGuids;
		for (const TPair<FString, AEHB_Pillar*>& SpawnedPillarPair : SpawnedPillars)
		{
			AEHB_Pillar* Pillar = SpawnedPillarPair.Value;
			if (!Pillar || !Pillar->ElementGuid.IsValid() || RefreshedPillarGuids.Contains(Pillar->ElementGuid))
			{
				continue;
			}

			Building->RefreshWallsConnectedToPillar(Pillar->ElementGuid, true);
			RefreshedPillarGuids.Add(Pillar->ElementGuid);
		}
	}

	int32 CreatedFloorCount = 0;
	const bool bAutoRoomSurfacesEnabled = ReadBool(Root, TEXT("autoRoomSurfaces"), true);
	const bool bGenerateAutoRoomCeilingSlabs = bAutoRoomSurfacesEnabled && RoomSpecs.IsEmpty();
	if (bAutoRoomSurfacesEnabled && CreatedWallCount > 0)
	{
		Building->RebuildClosedLoops();
		TArray<FEHBAIRoomSurfaceCandidate> RoomSurfaceCandidates;
		for (const FEHBBuildingClosedLoop& Loop : Building->ClosedLoops)
		{
			if (Loop.FloorIndex <= 0 || Loop.PillarGuids.Num() < 3)
			{
				continue;
			}

			bool bTouchesNewPillar = false;
			for (const FGuid& PillarGuid : Loop.PillarGuids)
			{
				bTouchesNewPillar = bTouchesNewPillar || SpawnedPillarGuids.Contains(PillarGuid);
			}
			if (!bTouchesNewPillar)
			{
				continue;
			}

			TArray<FVector> RoomPolygon;
			if (!BuildClosedLoopFillPolygonInBuildingSpace(Building, Loop, RoomPolygon))
			{
				continue;
			}
			if (LoopContainsInteriorWall(Building, Loop, RoomPolygon))
			{
				continue;
			}

			FEHBAIRoomSurfaceCandidate& Candidate = RoomSurfaceCandidates.AddDefaulted_GetRef();
			Candidate.Loop = Loop;
			Candidate.BuildingLocalPolygon = MoveTemp(RoomPolygon);
			Candidate.AbsArea = FMath::Abs(static_cast<float>(CalculateSignedArea2D(Candidate.BuildingLocalPolygon)));
			for (const FVector& Point : Candidate.BuildingLocalPolygon)
			{
				Candidate.Centroid += Point;
			}
			Candidate.Centroid = Candidate.BuildingLocalPolygon.IsEmpty()
				? FVector::ZeroVector
				: Candidate.Centroid / static_cast<float>(Candidate.BuildingLocalPolygon.Num());
		}

		TSet<int32> CompositeCandidateIndices;
		for (int32 CandidateIndex = 0; CandidateIndex < RoomSurfaceCandidates.Num(); ++CandidateIndex)
		{
			const FEHBAIRoomSurfaceCandidate& Candidate = RoomSurfaceCandidates[CandidateIndex];
			for (int32 OtherIndex = 0; OtherIndex < RoomSurfaceCandidates.Num(); ++OtherIndex)
			{
				if (OtherIndex == CandidateIndex)
				{
					continue;
				}

				const FEHBAIRoomSurfaceCandidate& Other = RoomSurfaceCandidates[OtherIndex];
				if (Other.Loop.FloorIndex == Candidate.Loop.FloorIndex
					&& Other.AbsArea < Candidate.AbsArea - 1.0f
					&& IsPointInsidePolygonXY(Other.Centroid, Candidate.BuildingLocalPolygon)
					&& !IsPointNearPolygonBoundaryXY(Other.Centroid, Candidate.BuildingLocalPolygon, 2.0f))
				{
					CompositeCandidateIndices.Add(CandidateIndex);
					break;
				}
			}
		}

		for (int32 CandidateIndex = 0; CandidateIndex < RoomSurfaceCandidates.Num(); ++CandidateIndex)
		{
			if (CompositeCandidateIndices.Contains(CandidateIndex))
			{
				continue;
			}

			const FEHBAIRoomSurfaceCandidate& Candidate = RoomSurfaceCandidates[CandidateIndex];
			const FEHBBuildingClosedLoop& Loop = Candidate.Loop;
			TArray<FVector> RoomPolygon = Candidate.BuildingLocalPolygon;

			const FEHBAIFloorSpec FloorSpec = GetFloorSpec(Loop.FloorIndex);
			for (FVector& Point : RoomPolygon)
			{
				Point.Z = FloorSpec.BaseZ;
			}

			const FString FloorNamePrefix = FString::Printf(TEXT("EHB_AI_RoomFloor_F%d"), Loop.FloorIndex);
			const FName FloorActorName = MakeUniqueObjectName(Building->GetLevel(), AEHB_Floor::StaticClass(), FName(*FloorNamePrefix));

			FActorSpawnParameters FloorSpawnParams;
			FloorSpawnParams.Name = FloorActorName;
			FloorSpawnParams.Owner = Building;
			FloorSpawnParams.OverrideLevel = Building->GetLevel();
			FloorSpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			FloorSpawnParams.ObjectFlags |= RF_Transactional;

			AEHB_Floor* Floor = World->SpawnActor<AEHB_Floor>(AEHB_Floor::StaticClass(), Building->GetActorTransform(), FloorSpawnParams);
			if (Floor)
			{
				Floor->SetFlags(RF_Transactional);
				Floor->Modify();
				Floor->ElementName = FloorActorName;
				Floor->AttachToBuilding(Building, FTransform::Identity);
				Floor->RoomLoopGuid = Loop.LoopGuid;
				Floor->RoomFloorIndex = Loop.FloorIndex;
				Floor->SetFloorAssignment(Loop.FloorIndex, EEHBBuildingFloorElementRole::FloorFinish);

				FEHBFloorFinishRegion FloorRegion;
				FloorRegion.OuterPolygon = RoomPolygon;
				if (Floor->SetFloorRegions({ MoveTemp(FloorRegion) }, true))
				{
					++CreatedFloorCount;
				}
				else
				{
					Floor->Destroy();
				}
#if WITH_EDITOR
				if (!Floor->IsActorBeingDestroyed())
				{
					Floor->SetActorLabel(FloorActorName.ToString());
				}
#endif
			}

			if (bGenerateAutoRoomCeilingSlabs)
			{
				FEHBAISlabSpec CeilingSlabSpec;
				CeilingSlabSpec.Id = FString::Printf(TEXT("RoomCeiling_F%d_%s"), Loop.FloorIndex, *Loop.LoopGuid.ToString(EGuidFormats::Short));
				CeilingSlabSpec.bIsFoundation = false;
				CeilingSlabSpec.Thickness = FMath::Max(1.0f, FloorSpec.CeilingSlabThickness);
				CeilingSlabSpec.TopZ = FloorSpec.BaseZ + FloorSpec.WallHeight;
				CeilingSlabSpec.FloorIndex = Loop.FloorIndex;
				for (FVector& Point : RoomPolygon)
				{
					Point.Z = 0.0f;
				}
				CeilingSlabSpec.LocalTopPolygon = RoomPolygon;
				SlabSpecs.Add(MoveTemp(CeilingSlabSpec));
			}
		}
	}

	int32 CreatedSlabCount = 0;
	for (const FEHBAISlabSpec& SlabSpec : SlabSpecs)
	{
		const FString SlabName = SlabSpec.Id.IsEmpty()
			? (SlabSpec.bIsFoundation ? FString(TEXT("EHB_AI_Foundation")) : FString(TEXT("EHB_AI_FloorSlab")))
			: FString::Printf(TEXT("EHB_AI_%s"), *SlabSpec.Id);
		const FName ActorName = MakeUniqueObjectName(Building->GetLevel(), AEHB_FloorSlab::StaticClass(), FName(*SlabName));

		FActorSpawnParameters SpawnParams;
		SpawnParams.Name = ActorName;
		SpawnParams.Owner = Building;
		SpawnParams.OverrideLevel = Building->GetLevel();
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnParams.ObjectFlags |= RF_Transactional;

		const FTransform LocalTransform(FRotator::ZeroRotator, FVector(0.0f, 0.0f, SlabSpec.TopZ));
		const FTransform WorldTransform = LocalTransform * Building->GetActorTransform();
		AEHB_FloorSlab* Slab = World->SpawnActor<AEHB_FloorSlab>(AEHB_FloorSlab::StaticClass(), WorldTransform, SpawnParams);
		if (!Slab)
		{
			continue;
		}

		Slab->SetFlags(RF_Transactional);
		Slab->Modify();
		Slab->ElementName = ActorName;
		Slab->ConfigureDefaultSlab(Building, LocalTransform, 100.0f, SlabSpec.Thickness, SlabSpec.bIsFoundation);
		Slab->Modify();
		Slab->Thickness = SlabSpec.Thickness;
		Slab->bIsFoundation = SlabSpec.bIsFoundation;
		Slab->bKeepFoundationBottomOnGround = SlabSpec.bIsFoundation && SlabSpec.bKeepBottomOnGround;
		Slab->LocalTopPolygon = SlabSpec.LocalTopPolygon;
		Slab->VisualExpansion = SlabSpec.VisualExpansion;
		if (SlabSpec.bIsFoundation)
		{
			Slab->bHasAIFoundationSource = SlabSpec.bHasAIFoundationSource;
			Slab->AIFoundationSourceJson = SlabSpec.SourceJson;
			Slab->AIDesignTopPolygon = SlabSpec.AIDesignTopPolygon;
			Slab->AIFoundationExpansion = SlabSpec.FoundationExpansion;
		}
		Slab->LocalHoles.Reset();
		Slab->PreviewCutters.Reset();
		Slab->SetFloorAssignment(
			SlabSpec.bIsFoundation ? 0 : SlabSpec.FloorIndex,
			SlabSpec.bIsFoundation ? EEHBBuildingFloorElementRole::Foundation : EEHBBuildingFloorElementRole::FloorCeiling);
		bool bRebuiltSlab = Slab->RebuildSlabMesh();
		if (Slab->bIsFoundation && Slab->bKeepFoundationBottomOnGround)
		{
			if (Slab->SnapFoundationBottomToGround())
			{
				bRebuiltSlab = Slab->RebuildSlabMesh();
			}
		}
		if (bRebuiltSlab)
		{
			++CreatedSlabCount;
		}
#if WITH_EDITOR
		Slab->SetActorLabel(ActorName.ToString());
#endif
	}

	int32 CreatedRoofCount = 0;
	TArray<AEHBGableRoof*> SpawnedRoofs;
	for (const FEHBAIRoofSpec& InitialRoofSpec : RoofSpecs)
	{
		FEHBAIRoofSpec RoofSpec = InitialRoofSpec;
		if (RoofSpec.bAutoFitToWallFootprint)
		{
			const bool bResolvedRoofBounds = TryResolveRoofAutoFitFromWallFootprint(
				Building,
				RoofSpec.FloorIndex,
				RoofSpec.YawDegrees,
				RoofSpec.AxisMode,
				RoofSpec.LocalCenter.Z,
				RoofSpec.AutoFitPadding,
				RoofSpec.LocalCenter,
				RoofSpec.Length,
				RoofSpec.Width);
			if (!bResolvedRoofBounds && (!RoofSpec.bHasLocalCenter || !RoofSpec.bHasLength || !RoofSpec.bHasWidth))
			{
				continue;
			}
		}

		const FString RoofName = RoofSpec.Id.IsEmpty()
			? FString(RoofSpec.bUseHipRoof ? TEXT("EHB_AI_HipRoof") : TEXT("EHB_AI_GableRoof"))
			: FString::Printf(TEXT("EHB_AI_%s"), *RoofSpec.Id);
		TSubclassOf<AEHBGableRoof> RoofClass = RoofSpec.bUseHipRoof ? HipRoofClass : GableRoofClass;
		const FName ActorName = MakeUniqueObjectName(Building->GetLevel(), RoofClass, FName(*RoofName));

		FActorSpawnParameters SpawnParams;
		SpawnParams.Name = ActorName;
		SpawnParams.Owner = Building;
		SpawnParams.OverrideLevel = Building->GetLevel();
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnParams.ObjectFlags |= RF_Transactional;

		const FTransform LocalTransform(FRotator(0.0f, RoofSpec.YawDegrees, 0.0f), RoofSpec.LocalCenter);
		const FTransform WorldTransform = LocalTransform * Building->GetActorTransform();
		AEHBGableRoof* Roof = World->SpawnActor<AEHBGableRoof>(RoofClass, WorldTransform, SpawnParams);
		if (!Roof)
		{
			continue;
		}

		Roof->SetFlags(RF_Transactional);
		Roof->Modify();
		Roof->ElementName = ActorName;
		Roof->AttachToBuilding(Building, LocalTransform);
		Roof->Length = RoofSpec.Length;
		Roof->Width = RoofSpec.Width;
		Roof->PitchDegrees = RoofSpec.PitchDegrees;
		Roof->Thickness = RoofSpec.Thickness;
		Roof->EaveOffset = RoofSpec.EaveOffset;
		Roof->RidgeOffsetRatio = RoofSpec.RidgeOffsetRatio;
		Roof->AxisMode = RoofSpec.AxisMode;
		Roof->bGenerateRidge = RoofSpec.bGenerateRidge;
		Roof->bGenerateEaves = RoofSpec.bGenerateEaves;
		Roof->bGenerateGableRakes = RoofSpec.bGenerateGableRakes;
		Roof->bGenerateGableEndWalls = RoofSpec.bUseHipRoof ? false : RoofSpec.bGenerateGableEndWalls;
		Roof->RidgeWidth = RoofSpec.RidgeWidth;
		Roof->RidgeHeight = RoofSpec.RidgeHeight;
		Roof->EaveWidth = RoofSpec.EaveWidth;
		Roof->EaveHeight = RoofSpec.EaveHeight;
		Roof->GableRakeWidth = RoofSpec.GableRakeWidth;
		Roof->GableRakeHeight = RoofSpec.GableRakeHeight;
		Roof->GableEndWallBoundaryInset = RoofSpec.GableEndWallBoundaryInset;
		Roof->bCutCollidingElements = RoofSpec.bCutCollidingElements;
		Roof->bRemoveDisconnectedCutPieces = RoofSpec.bRemoveDisconnectedCutPieces;
		Roof->bKeepCutAwayDisconnectedPieces = RoofSpec.bKeepCutAwayDisconnectedPieces;
		Roof->bUseExactSourceMeshCutters = RoofSpec.bUseExactSourceMeshCutters;
		Roof->bUseControlledEnvelopeCutters = RoofSpec.bUseControlledEnvelopeCutters;
		Roof->EnvelopeCutOptions = RoofSpec.EnvelopeCutOptions;
		Roof->bUseWallFootprintCutters = RoofSpec.bUseWallFootprintCutters;
		Roof->MinWallFootprintGroupWallCount = RoofSpec.MinWallFootprintGroupWallCount;
		Roof->WallFootprintGroupEndpointTolerance = RoofSpec.WallFootprintGroupEndpointTolerance;
		Roof->WallFootprintPadding = RoofSpec.WallFootprintPadding;
		Roof->WallFootprintMaxDimension = RoofSpec.WallFootprintMaxDimension;
		Roof->WallFootprintMinArea = RoofSpec.WallFootprintMinArea;
		Roof->bShowCutDebugVisualization = RoofSpec.bShowCutDebugVisualization;
		Roof->bDebugDrawRawRoofBounds = RoofSpec.bDebugDrawRawRoofBounds;
		Roof->bDebugDrawSourceBounds = RoofSpec.bDebugDrawSourceBounds;
		Roof->bDebugDrawCutterBounds = RoofSpec.bDebugDrawCutterBounds;
		Roof->bDebugDrawResultBounds = RoofSpec.bDebugDrawResultBounds;
		Roof->CutDebugDrawDuration = RoofSpec.CutDebugDrawDuration;
		Roof->CutDebugDrawThickness = RoofSpec.CutDebugDrawThickness;
		Roof->SetFloorAssignment(RoofSpec.FloorIndex, EEHBBuildingFloorElementRole::Roof);
		Roof->RebuildRoofMesh();
		Roof->MarkPackageDirty();
#if WITH_EDITOR
		Roof->SetActorLabel(ActorName.ToString());
#endif
		SpawnedRoofs.Add(Roof);
		++CreatedRoofCount;
	}

	for (AEHBGableRoof* Roof : SpawnedRoofs)
	{
		if (!Roof || !Roof->bCutCollidingElements)
		{
			continue;
		}
		Roof->RefreshAutoCollisionCutOperations();
		Roof->RebuildRoofMesh();
		Roof->MarkPackageDirty();
	}

	Building->MarkPackageDirty();
	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports();
	}

	FEHBAIWorkflowExecutionResult Result;
	Result.bSucceeded = true;
	Result.PillarCount = SpawnedPillars.Num();
	Result.WallCount = CreatedWallCount;
	Result.CurvedWallCount = CurvedWallCount;
	Result.DoorWindowCount = CreatedDoorWindowCount;
	Result.FloorCount = CreatedFloorCount;
	Result.SlabCount = CreatedSlabCount;
	Result.RoofCount = CreatedRoofCount;
	Result.StatusText = FText::Format(
		LOCTEXT("ExecuteSuccess", "AI workflow complete: {0} pillars, {1} walls, {2} curved walls, {3} slabs/foundations, {4} roofs."),
		FText::AsNumber(Result.PillarCount),
		FText::AsNumber(Result.WallCount),
		FText::AsNumber(Result.CurvedWallCount),
		FText::AsNumber(Result.SlabCount),
		FText::AsNumber(Result.RoofCount));
	return Result;
}

#undef LOCTEXT_NAMESPACE
