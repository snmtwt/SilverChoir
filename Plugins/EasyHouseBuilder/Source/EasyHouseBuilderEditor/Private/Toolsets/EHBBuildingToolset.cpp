// Copyright Epic Games, Inc. All Rights Reserved.

#include "Toolsets/EHBBuildingToolset.h"
#include "Cutting/EHBPolygonClipper.h"
#include "Geometry/EHBSurfaceGeometryTypes.h"
#include "EHBNodeAuthorityEditing.h"
#include "EHBNodeEditingActivation.h"
#include "EHBWallCreationCommand.h"
#include "EHBWallRemovalCommand.h"
#include "EHBFinishRegionCommand.h"
#include "Core/EHBWallPathPlanning.h"
#include "EHBRoomFinishMove.h"
#include "EHBRoomSubdivision.h"
#include "Core/EHBChangeNotificationBatch.h"
#include "Geometry/EHBFloorContactGeometry.h"
#include "EHBBuildingCopy.h"
#include "Tests/EHBNodeOwnershipTestHooks.h"
#include "EHBWallRailingJunction.h"
#include "EHBPreservedRailing.h"

#include "Actors/EHBElementActorBase.h"
#include "Actors/EHB_DoorWindow.h"
#include "Actors/EHB_FloorSlab.h"
#include "Actors/EHB_Floor.h"
#include "Actors/EHB_Pillar.h"
#include "Actors/EHB_Railing.h"
#include "Actors/EHB_RailingGate.h"
#include "Actors/EHBGableRoof.h"
#include "Actors/EHBHipRoof.h"
#include "Actors/EHB_Stair.h"
#include "Actors/EHB_Wall.h"
#include "EHB_Building.h"
#include "Core/EHBBuildingActorBase.h"
#include "Core/EHBWallTopology.h"
#include "Core/EHBWallNodeCopy.h"
#include "Definitions/EHBBuildingTypes.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "EditorModeManager.h"
#include "Engine/World.h"
#include "Engine/Selection.h"
#include "EngineUtils.h"
#include "Engine/Level.h"
#include "Tests/EHBWallSplitTestHooks.h"
#include "Tests/EHBNodeMoveTestHooks.h"
#include "Core/EHBRoomIdentity.h"

#if WITH_DEV_AUTOMATION_TESTS
EHBWallSplitTestHooks::EFailurePhase EHBWallSplitTestHooks::FailurePhase = EHBWallSplitTestHooks::EFailurePhase::None;
EHBNodeMoveTestHooks::EFailurePhase EHBNodeMoveTestHooks::FailurePhase = EHBNodeMoveTestHooks::EFailurePhase::None;
int32 EHBNodeMoveTestHooks::CandidateJunctionSolveCount = 0;
int32 EHBNodeMoveTestHooks::DefinitionBatchSolveCount = 0;
int32 EHBNodeMoveTestHooks::PreparedDefinitionReuseCount = 0;
#endif
#include "JsonObjectConverter.h"
#include "EasyHouseEditorMode.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Settings/EHBBuildingToolsetSettings.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "EHBBuildingToolset"

namespace
{
	constexpr float EHBToolsetPolygonTolerance = 0.01f;

	UWorld* GetEditorWorld()
	{
		if (!GEditor)
		{
			return nullptr;
		}

		return GEditor->GetEditorWorldContext().World();
	}

	template <typename TActor>
	TActor* SpawnEHBActor(UWorld& World, UClass* ActorClass, const FString& DesiredName, const FTransform& WorldTransform)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.Name = MakeUniqueObjectName(World.GetCurrentLevel(), ActorClass, FName(*DesiredName));
		SpawnParams.OverrideLevel = World.GetCurrentLevel();
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnParams.bNoFail = false;
		SpawnParams.ObjectFlags |= RF_Transactional;

		TActor* Actor = World.SpawnActor<TActor>(ActorClass, WorldTransform, SpawnParams);
		if (Actor)
		{
			Actor->SetActorLabel(DesiredName.IsEmpty() ? Actor->GetName() : DesiredName);
			Actor->SetFlags(RF_Transactional);
		}
		return Actor;
	}

	TSubclassOf<AEHB_Railing> ResolveRailingActorClass()
	{
		TSubclassOf<AEHB_Railing> ActorClass = AEHB_Railing::StaticClass();
		const UEHBBuildingToolsetSettings* Settings = GetDefault<UEHBBuildingToolsetSettings>();
		if (Settings && !Settings->RailingActorClass.IsNull())
		{
			if (UClass* LoadedClass = Settings->RailingActorClass.LoadSynchronous())
			{
				if (LoadedClass->IsChildOf(AEHB_Railing::StaticClass()))
				{
					ActorClass = LoadedClass;
				}
			}
		}
		return ActorClass;
	}

	TSubclassOf<AEHB_RailingGate> ResolveRailingGateActorClass()
	{
		TSubclassOf<AEHB_RailingGate> ActorClass = AEHB_RailingGate::StaticClass();
		const UEHBBuildingToolsetSettings* Settings = GetDefault<UEHBBuildingToolsetSettings>();
		if (Settings && !Settings->RailingGateActorClass.IsNull())
		{
			if (UClass* LoadedClass = Settings->RailingGateActorClass.LoadSynchronous())
			{
				if (LoadedClass->IsChildOf(AEHB_RailingGate::StaticClass()))
				{
					ActorClass = LoadedClass;
				}
			}
		}
		return ActorClass;
	}

	TSubclassOf<AEHBGableRoof> ResolveGableRoofActorClass()
	{
		TSubclassOf<AEHBGableRoof> ActorClass = AEHBGableRoof::StaticClass();
		const UEHBBuildingToolsetSettings* Settings = GetDefault<UEHBBuildingToolsetSettings>();
		if (Settings && !Settings->GableRoofActorClass.IsNull())
		{
			if (UClass* LoadedClass = Settings->GableRoofActorClass.LoadSynchronous())
			{
				if (LoadedClass->IsChildOf(AEHBGableRoof::StaticClass()))
				{
					ActorClass = LoadedClass;
				}
			}
		}
		return ActorClass;
	}

	TSubclassOf<AEHBHipRoof> ResolveHipRoofActorClass()
	{
		TSubclassOf<AEHBHipRoof> ActorClass = AEHBHipRoof::StaticClass();
		const UEHBBuildingToolsetSettings* Settings = GetDefault<UEHBBuildingToolsetSettings>();
		if (Settings && !Settings->HipRoofActorClass.IsNull())
		{
			if (UClass* LoadedClass = Settings->HipRoofActorClass.LoadSynchronous())
			{
				if (LoadedClass->IsChildOf(AEHBHipRoof::StaticClass()))
				{
					ActorClass = LoadedClass;
				}
			}
		}
		return ActorClass;
	}

	bool IsBuildingModeActive()
	{
		return GEditor
			&& GLevelEditorModeTools().GetActiveMode(FEasyHouseEditorMode::EM_EasyHouseEditorModeId) != nullptr;
	}

	FEasyHouseEditorMode* GetActiveBuildingMode()
	{
		return GEditor
			? GLevelEditorModeTools().GetActiveModeTyped<FEasyHouseEditorMode>(FEasyHouseEditorMode::EM_EasyHouseEditorModeId)
			: nullptr;
	}

	AEHBBuildingActorBase* GetBuildingModeActiveBuilding()
	{
		if (FEasyHouseEditorMode* BuildingMode = GetActiveBuildingMode())
		{
			return BuildingMode->GetActiveBuilding();
		}

		return nullptr;
	}

	AEHBBuildingActorBase* GetEditorSelectedBuilding()
	{
		if (!GEditor)
		{
			return nullptr;
		}

		USelection* SelectedActors = GEditor->GetSelectedActors();
		if (!SelectedActors)
		{
			return nullptr;
		}

		for (FSelectionIterator It(*SelectedActors); It; ++It)
		{
			if (AEHBBuildingActorBase* Building = Cast<AEHBBuildingActorBase>(*It))
			{
				return Building;
			}
		}

		return nullptr;
	}

	void SyncSelectedBuildingToBuildingMode(AEHBBuildingActorBase* SelectedBuilding)
	{
		if (SelectedBuilding)
		{
			if (FEasyHouseEditorMode* BuildingMode = GetActiveBuildingMode())
			{
				if (BuildingMode->GetActiveBuilding() != SelectedBuilding)
				{
					BuildingMode->SetActiveBuilding(SelectedBuilding);
				}
			}
		}
	}

	AEHBBuildingActorBase* FindSingleExistingBuilding(UWorld* World)
	{
		if (!World)
		{
			return nullptr;
		}

		AEHBBuildingActorBase* SingleBuilding = nullptr;
		for (TActorIterator<AEHBBuildingActorBase> It(World); It; ++It)
		{
			AEHBBuildingActorBase* Building = *It;
			if (!IsValid(Building))
			{
				continue;
			}

			if (SingleBuilding)
			{
				return nullptr;
			}

			SingleBuilding = Building;
		}

		return SingleBuilding;
	}

	void SelectAndSyncExistingBuilding(AEHBBuildingActorBase* Building)
	{
		if (!Building)
		{
			return;
		}

		if (GEditor)
		{
			GEditor->SelectNone(false, true);
			GEditor->SelectActor(Building, true, true);
		}

		SyncSelectedBuildingToBuildingMode(Building);
	}

	AEHBBuildingActorBase* ResolveExistingBuildingForAI(UWorld* World)
	{
		if (AEHBBuildingActorBase* SelectedBuilding = GetEditorSelectedBuilding())
		{
			SelectAndSyncExistingBuilding(SelectedBuilding);
			return SelectedBuilding;
		}

		if (AEHBBuildingActorBase* ModeBuilding = GetBuildingModeActiveBuilding())
		{
			SelectAndSyncExistingBuilding(ModeBuilding);
			return ModeBuilding;
		}

		if (AEHBBuildingActorBase* SingleBuilding = FindSingleExistingBuilding(World))
		{
			SelectAndSyncExistingBuilding(SingleBuilding);
			return SingleBuilding;
		}

		return nullptr;
	}

	AEHBBuildingActorBase* ResolveEditableBuilding(AEHBBuildingActorBase* RequestedBuilding)
	{
		AEHBBuildingActorBase* SelectedBuilding = GetEditorSelectedBuilding();
		if (!SelectedBuilding)
		{
			return nullptr;
		}

		if (RequestedBuilding && RequestedBuilding != SelectedBuilding)
		{
			return nullptr;
		}

		SyncSelectedBuildingToBuildingMode(SelectedBuilding);
		return SelectedBuilding;
	}

	AEHBBuildingActorBase* ResolveEditableElement(AEHBElementActorBase* Element)
	{
		return Element ? ResolveEditableBuilding(Element->OwningBuilding) : nullptr;
	}

	TSharedRef<FJsonObject> MakeVectorJson(const FVector& Vector)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetNumberField(TEXT("x"), Vector.X);
		Object->SetNumberField(TEXT("y"), Vector.Y);
		Object->SetNumberField(TEXT("z"), Vector.Z);
		return Object;
	}

	TSharedRef<FJsonObject> MakeRotatorJson(const FRotator& Rotator)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetNumberField(TEXT("pitch"), Rotator.Pitch);
		Object->SetNumberField(TEXT("yaw"), Rotator.Yaw);
		Object->SetNumberField(TEXT("roll"), Rotator.Roll);
		return Object;
	}

	TSharedPtr<FJsonValue> MakeVectorValue(const FVector& Vector)
	{
		return MakeShared<FJsonValueObject>(MakeVectorJson(Vector));
	}

	TArray<TSharedPtr<FJsonValue>> MakeVectorArray(const TArray<FVector>& Vectors)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		Values.Reserve(Vectors.Num());
		for (const FVector& Vector : Vectors)
		{
			Values.Add(MakeVectorValue(Vector));
		}
		return Values;
	}

	TArray<TSharedPtr<FJsonValue>> MakeStringArray(std::initializer_list<const TCHAR*> Items)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		Values.Reserve(static_cast<int32>(Items.size()));
		for (const TCHAR* Item : Items)
		{
			Values.Add(MakeShared<FJsonValueString>(FString(Item)));
		}
		return Values;
	}

	TSharedRef<FJsonObject> MakeElementJson(const AEHBElementActorBase& Element)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("name"), Element.ElementName.IsNone() ? Element.GetActorLabel() : Element.ElementName.ToString());
		Object->SetStringField(TEXT("actor"), Element.GetPathName());
		Object->SetStringField(TEXT("guid"), Element.ElementGuid.ToString(EGuidFormats::DigitsWithHyphensLower));
		Object->SetStringField(TEXT("type"), StaticEnum<EEHBBuildingElementType>()->GetNameStringByValue(static_cast<int64>(Element.ElementType)));
		Object->SetNumberField(TEXT("floor"), Element.FloorIndex);
		Object->SetStringField(TEXT("floorRole"), StaticEnum<EEHBBuildingFloorElementRole>()->GetNameStringByValue(static_cast<int64>(Element.FloorRole)));
		Object->SetStringField(TEXT("location"), Element.GetActorLocation().ToString());
		const FTransform LocalTransform = Element.OwningBuilding
			? Element.GetActorTransform().GetRelativeTransform(Element.OwningBuilding->GetActorTransform())
			: Element.GetActorTransform();
		Object->SetObjectField(TEXT("localLocation"), MakeVectorJson(LocalTransform.GetLocation()));
		Object->SetObjectField(TEXT("localRotation"), MakeRotatorJson(LocalTransform.Rotator()));
		Object->SetObjectField(TEXT("localScale"), MakeVectorJson(LocalTransform.GetScale3D()));

		if (const AEHB_Pillar* Pillar = Cast<AEHB_Pillar>(&Element))
		{
			Object->SetStringField(TEXT("shapeType"), StaticEnum<EEHBPillarShapeType>()->GetNameStringByValue(static_cast<int64>(Pillar->ShapeType)));
			Object->SetNumberField(TEXT("height"), Pillar->Height);
			Object->SetNumberField(TEXT("width"), Pillar->Width);
			Object->SetNumberField(TEXT("depth"), Pillar->Depth);
			Object->SetNumberField(TEXT("radius"), Pillar->Radius);
			Object->SetNumberField(TEXT("connectedWallCount"), Pillar->ConnectedWallGuids.Num());
			Object->SetNumberField(TEXT("connectedPillarCount"), Pillar->ConnectedPillarGuids.Num());
			Object->SetArrayField(TEXT("footprintLocal"), MakeVectorArray(Pillar->PolygonPillarFootprint));
		}
		else if (const AEHB_Wall* Wall = Cast<AEHB_Wall>(&Element))
		{
			Object->SetObjectField(TEXT("localStart"), MakeVectorJson(Wall->LocalStart));
			Object->SetObjectField(TEXT("localEnd"), MakeVectorJson(Wall->LocalEnd));
			Object->SetNumberField(TEXT("length"), FVector::Dist(Wall->LocalStart, Wall->LocalEnd));
			Object->SetNumberField(TEXT("height"), Wall->Height);
			Object->SetNumberField(TEXT("thickness"), Wall->Thickness);
			Object->SetNumberField(TEXT("curveControlOffset"), Wall->CurveControlOffset);
			Object->SetNumberField(TEXT("curveSegmentLength"), Wall->CurveSegmentLength);
			Object->SetStringField(TEXT("startPillarGuid"), Wall->StartPillarGuid.ToString(EGuidFormats::DigitsWithHyphensLower));
			Object->SetStringField(TEXT("endPillarGuid"), Wall->EndPillarGuid.ToString(EGuidFormats::DigitsWithHyphensLower));
			Object->SetNumberField(TEXT("doorWindowConnectionCount"), Wall->DoorWindowConnections.Num());

			TArray<TSharedPtr<FJsonValue>> Connections;
			Connections.Reserve(Wall->DoorWindowConnections.Num());
			for (const FEHBWallDoorWindowConnection& Connection : Wall->DoorWindowConnections)
			{
				TSharedRef<FJsonObject> ConnectionObject = MakeShared<FJsonObject>();
				ConnectionObject->SetStringField(TEXT("doorWindowGuid"), Connection.DoorWindowGuid.ToString(EGuidFormats::DigitsWithHyphensLower));
				ConnectionObject->SetStringField(TEXT("kind"), StaticEnum<EEHBDoorWindowElementKind>()->GetNameStringByValue(static_cast<int64>(Connection.Kind)));
				ConnectionObject->SetNumberField(TEXT("distanceFromStart"), Connection.DistanceFromStart);
				ConnectionObject->SetNumberField(TEXT("bottomHeight"), Connection.BottomHeight);
				ConnectionObject->SetNumberField(TEXT("openingWidth"), Connection.OpeningWidth);
				ConnectionObject->SetNumberField(TEXT("openingHeight"), Connection.OpeningHeight);
				ConnectionObject->SetNumberField(TEXT("openingThickness"), Connection.OpeningThickness);
				ConnectionObject->SetArrayField(TEXT("localOutlinePoints"), MakeVectorArray(Connection.LocalOutlinePoints));
				Connections.Add(MakeShared<FJsonValueObject>(ConnectionObject));
			}
			Object->SetArrayField(TEXT("doorWindowConnections"), Connections);
		}
		else if (const AEHB_Floor* Floor = Cast<AEHB_Floor>(&Element))
		{
			Object->SetStringField(TEXT("roomLoopGuid"),Floor->RoomLoopGuid.ToString());
			Object->SetNumberField(TEXT("roomFloorIndex"),Floor->RoomFloorIndex);
			Object->SetStringField(TEXT("outlineSource"),StaticEnum<EEHBOutlineSource>()->GetNameStringByValue(static_cast<int64>(Floor->OutlineSource)));
			Object->SetBoolField(TEXT("recordedOutlineUnchanged"),Floor->IsRecordedOutlineUnchanged());
			Object->SetNumberField(TEXT("regionCount"),Floor->FloorRegions.Num());
		}
		else if (const AEHB_FloorSlab* FloorSlab = Cast<AEHB_FloorSlab>(&Element))
		{
			Object->SetNumberField(TEXT("holeCount"), FloorSlab->LocalHoles.Num());
			Object->SetBoolField(TEXT("isFoundation"), FloorSlab->bIsFoundation);
			Object->SetNumberField(TEXT("thickness"), FloorSlab->Thickness);
			Object->SetNumberField(TEXT("topZ"), FloorSlab->GetTopZ());
			Object->SetNumberField(TEXT("bottomZ"), FloorSlab->GetBottomZ());
			Object->SetNumberField(TEXT("visualExpansion"), FloorSlab->VisualExpansion);
			Object->SetNumberField(TEXT("topPolygonPointCount"), FloorSlab->LocalTopPolygon.Num());
			Object->SetArrayField(TEXT("localTopPolygon"), MakeVectorArray(FloorSlab->LocalTopPolygon));
			Object->SetBoolField(TEXT("hasRoomFillAnchor"), FloorSlab->bHasRoomFillAnchor);
			Object->SetStringField(TEXT("roomFillLoopGuid"),FloorSlab->RoomFillLoopGuid.ToString());
			Object->SetNumberField(TEXT("roomFillFloorIndex"),FloorSlab->RoomFillFloorIndex);
			Object->SetStringField(TEXT("outlineSource"),StaticEnum<EEHBOutlineSource>()->GetNameStringByValue(static_cast<int64>(FloorSlab->OutlineSource)));
			Object->SetBoolField(TEXT("recordedOutlineUnchanged"),FloorSlab->IsRecordedOutlineUnchanged());
			Object->SetStringField(TEXT("roomFillAnchorWallGuid"), FloorSlab->RoomFillAnchorWallGuid.ToString(EGuidFormats::DigitsWithHyphensLower));
			Object->SetStringField(TEXT("roomFillAnchorWallSide"), StaticEnum<EEHBFloorSlabWallSide>()->GetNameStringByValue(static_cast<int64>(FloorSlab->RoomFillAnchorWallSide)));

			TArray<TSharedPtr<FJsonValue>> Holes;
			Holes.Reserve(FloorSlab->LocalHoles.Num());
			for (const FEHBFloorSlabHole& Hole : FloorSlab->LocalHoles)
			{
				TSharedRef<FJsonObject> HoleObject = MakeShared<FJsonObject>();
				HoleObject->SetNumberField(TEXT("pointCount"), Hole.LocalPolygon.Num());
				HoleObject->SetArrayField(TEXT("localPolygon"), MakeVectorArray(Hole.LocalPolygon));
				Holes.Add(MakeShared<FJsonValueObject>(HoleObject));
			}
			Object->SetArrayField(TEXT("holes"), Holes);
		}
		else if (const AEHB_Stair* Stair = Cast<AEHB_Stair>(&Element))
		{
			Object->SetNumberField(TEXT("generatedStepCount"), Stair->GeneratedStepCount);
			Object->SetNumberField(TEXT("generatedStepHeight"), Stair->GeneratedStepHeight);
			Object->SetNumberField(TEXT("stairLength"), Stair->GetStairLength());
			Object->SetNumberField(TEXT("stairHeight"), Stair->StairData.StairHeight);
			Object->SetNumberField(TEXT("stairWidth"), Stair->StairData.StairWidth);
			Object->SetNumberField(TEXT("treadDepth"), Stair->StairData.TreadDepth);
			Object->SetNumberField(TEXT("minStepHeight"), Stair->StairData.MinStepHeight);
			Object->SetNumberField(TEXT("maxStepHeight"), Stair->StairData.MaxStepHeight);
			Object->SetNumberField(TEXT("nosingLength"), Stair->StairData.NosingLength);
			Object->SetBoolField(TEXT("generateTreads"), Stair->StairData.bGenerateTreads);
			Object->SetBoolField(TEXT("fillRisers"), Stair->StairData.bFillRisers);
			Object->SetBoolField(TEXT("generateSides"), Stair->StairData.bGenerateSides);
			Object->SetBoolField(TEXT("fillBottomPart"), Stair->StairData.bFillBottomPart);
			Object->SetBoolField(TEXT("generateSideGuards"), Stair->StairData.bGenerateSideGuards);
			Object->SetBoolField(TEXT("useIntermediateControls"), Stair->StairData.bUseIntermediateControls);
			Object->SetStringField(TEXT("pathControlPolicy"), TEXT("AI-created stairs use endpoint controls only: top landing transform plus bottom step location/yaw."));
			Object->SetObjectField(TEXT("bottomControlWorldLocation"), MakeVectorJson(Stair->GetBottomControlWorldLocation()));
			Object->SetObjectField(TEXT("bottomControlWorldRotation"), MakeRotatorJson(Stair->GetBottomControlWorldRotation()));
		}
		else if (const AEHB_Railing* Railing = Cast<AEHB_Railing>(&Element))
		{
			Object->SetStringField(TEXT("pathMode"), StaticEnum<EEHBRailingPathMode>()->GetNameStringByValue(static_cast<int64>(Railing->PathMode)));
			Object->SetStringField(TEXT("side"), StaticEnum<EEHBRailingSide>()->GetNameStringByValue(static_cast<int64>(Railing->RailingSide)));
			Object->SetStringField(TEXT("fillMode"), StaticEnum<EEHBRailingFillMode>()->GetNameStringByValue(static_cast<int64>(Railing->FillMode)));
			Object->SetObjectField(TEXT("linearStart"), MakeVectorJson(Railing->LinearStart));
			Object->SetObjectField(TEXT("linearEnd"), MakeVectorJson(Railing->LinearEnd));
			Object->SetNumberField(TEXT("length"), Railing->GetRailingLength());
			Object->SetNumberField(TEXT("postSpacing"), Railing->PostSpacing);
			Object->SetNumberField(TEXT("stepsPerPost"), Railing->StepsPerPost);
			Object->SetNumberField(TEXT("postWidth"), Railing->PostWidth);
			Object->SetNumberField(TEXT("postHeight"), Railing->PostHeight);
			Object->SetNumberField(TEXT("railHeight"), Railing->RailHeight);
			Object->SetNumberField(TEXT("railThickness"), Railing->RailThickness);
			Object->SetNumberField(TEXT("maxRailSegmentLength"), Railing->MaxRailSegmentLength);
			Object->SetNumberField(TEXT("generatedPostCount"), Railing->GeneratedPosts.Num());
			Object->SetNumberField(TEXT("gateConnectionCount"), Railing->GateConnections.Num());
			Object->SetNumberField(TEXT("relationCount"), Railing->RailingRelationGuids.Num());
			Object->SetBoolField(TEXT("omitStartPost"), Railing->bOmitStartPost);
			Object->SetBoolField(TEXT("omitEndPost"), Railing->bOmitEndPost);
			Object->SetStringField(TEXT("startAnchorElementGuid"), Railing->StartAnchor.ElementGuid.ToString(EGuidFormats::DigitsWithHyphensLower));
			Object->SetStringField(TEXT("endAnchorElementGuid"), Railing->EndAnchor.ElementGuid.ToString(EGuidFormats::DigitsWithHyphensLower));
			Object->SetStringField(TEXT("startAnchorPostGuid"), Railing->StartAnchor.PostGuid.ToString(EGuidFormats::DigitsWithHyphensLower));
			Object->SetStringField(TEXT("endAnchorPostGuid"), Railing->EndAnchor.PostGuid.ToString(EGuidFormats::DigitsWithHyphensLower));
			Object->SetObjectField(TEXT("startAnchorLocalPoint"), MakeVectorJson(Railing->StartAnchor.LocalPoint));
			Object->SetObjectField(TEXT("endAnchorLocalPoint"), MakeVectorJson(Railing->EndAnchor.LocalPoint));
			Object->SetStringField(TEXT("hostedStairGuid"), Railing->HostedStairGuid.ToString(EGuidFormats::DigitsWithHyphensLower));
		}
		else if (const AEHB_RailingGate* Gate = Cast<AEHB_RailingGate>(&Element))
		{
			Object->SetStringField(TEXT("owningRailingGuid"), Gate->OwningRailingGuid.ToString(EGuidFormats::DigitsWithHyphensLower));
			Object->SetNumberField(TEXT("distanceFromRailingStart"), Gate->DistanceFromRailingStart);
			Object->SetNumberField(TEXT("width"), Gate->Width);
			Object->SetNumberField(TEXT("height"), Gate->Height);
			Object->SetNumberField(TEXT("thickness"), Gate->Thickness);
			Object->SetStringField(TEXT("hingeSide"), StaticEnum<EEHBRailingGateHingeSide>()->GetNameStringByValue(static_cast<int64>(Gate->HingeSide)));
			Object->SetNumberField(TEXT("currentOpenAngle"), Gate->CurrentOpenAngle);
		}
		else if (const AEHBGableRoof* Roof = Cast<AEHBGableRoof>(&Element))
		{
			Object->SetStringField(TEXT("roofType"), TEXT("Gable"));
			Object->SetStringField(TEXT("axisMode"), StaticEnum<EEHBRoofAxisMode>()->GetNameStringByValue(static_cast<int64>(Roof->AxisMode)));
			Object->SetNumberField(TEXT("length"), Roof->Length);
			Object->SetNumberField(TEXT("width"), Roof->Width);
			Object->SetNumberField(TEXT("pitchDegrees"), Roof->PitchDegrees);
			Object->SetNumberField(TEXT("thickness"), Roof->Thickness);
			Object->SetNumberField(TEXT("eaveOffset"), Roof->EaveOffset);
			Object->SetNumberField(TEXT("ridgeOffsetRatio"), Roof->RidgeOffsetRatio);
			Object->SetBoolField(TEXT("cutCollidingElements"), Roof->bCutCollidingElements);
			Object->SetBoolField(TEXT("removeDisconnectedCutPieces"), Roof->bRemoveDisconnectedCutPieces);
			Object->SetBoolField(TEXT("keepCutAwayDisconnectedPieces"), Roof->bKeepCutAwayDisconnectedPieces);
		}
		else if (const AEHB_DoorWindow* DoorWindow = Cast<AEHB_DoorWindow>(&Element))
		{
			Object->SetStringField(TEXT("kind"), StaticEnum<EEHBDoorWindowElementKind>()->GetNameStringByValue(static_cast<int64>(DoorWindow->Kind)));
			Object->SetNumberField(TEXT("distanceFromWallStart"), DoorWindow->DistanceFromWallStart);
			Object->SetNumberField(TEXT("openingWidth"), DoorWindow->OpeningWidth);
			Object->SetNumberField(TEXT("openingHeight"), DoorWindow->OpeningHeight);
			Object->SetNumberField(TEXT("openingThickness"), DoorWindow->OpeningThickness);
			Object->SetNumberField(TEXT("bottomHeight"), DoorWindow->GetOpeningBottomHeight());
			Object->SetStringField(TEXT("owningWallGuid"), DoorWindow->OwningWallGuid.ToString(EGuidFormats::DigitsWithHyphensLower));
			Object->SetArrayField(TEXT("localOpeningOutline"), MakeVectorArray(DoorWindow->GetOpeningOutlineLocalPoints()));
		}
		return Object;
	}

	TSharedRef<FJsonObject> MakeClosedLoopJson(const AEHBBuildingActorBase& Building, const FEHBBuildingClosedLoop& Loop)
	{
		auto MakeGuidStringArray = [](const TArray<FGuid>& Guids)
		{
			TArray<TSharedPtr<FJsonValue>> Values;
			Values.Reserve(Guids.Num());
			for (const FGuid& Guid : Guids)
			{
				Values.Add(MakeShared<FJsonValueString>(Guid.ToString(EGuidFormats::DigitsWithHyphensLower)));
			}
			return Values;
		};

		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("guid"), Loop.LoopGuid.ToString(EGuidFormats::DigitsWithHyphensLower));
		Object->SetNumberField(TEXT("floorIndex"), Loop.FloorIndex);
		Object->SetNumberField(TEXT("area"), Loop.Area);
		Object->SetBoolField(TEXT("clockwise"), Loop.bClockwise);
		Object->SetNumberField(TEXT("wallCount"), Loop.WallGuids.Num());
		Object->SetNumberField(TEXT("pillarCount"), Loop.PillarGuids.Num());
		Object->SetArrayField(TEXT("wallGuids"), MakeGuidStringArray(Loop.WallGuids));
		Object->SetArrayField(TEXT("pillarGuids"), MakeGuidStringArray(Loop.PillarGuids));

		TArray<FVector> LocalPolygon;
		LocalPolygon.Reserve(Loop.PillarGuids.Num());
		FVector Centroid = FVector::ZeroVector;
		for (const FGuid& PillarGuid : Loop.PillarGuids)
		{
			const AEHBElementActorBase* PillarActor = Building.FindElementActorByGuid(PillarGuid);
			if (!PillarActor)
			{
				continue;
			}

			FVector LocalPoint = PillarActor->GetElementLocalTransform().GetLocation();
			LocalPoint.Z = 0.0f;
			LocalPolygon.Add(LocalPoint);
			Centroid += LocalPoint;
		}

		if (!LocalPolygon.IsEmpty())
		{
			Centroid /= static_cast<float>(LocalPolygon.Num());
			Centroid.Z = LocalPolygon[0].Z;
		}

		Object->SetArrayField(TEXT("localPolygon"), MakeVectorArray(LocalPolygon));
		Object->SetObjectField(TEXT("interiorPointHint"), MakeVectorJson(Centroid));
		Object->SetStringField(TEXT("usage"), TEXT("Use interiorPointHint for CreateRoomFilledFloorSlabAtPoint, slab holes, and stair endpoints. Keep stairs away from localPolygon edges."));
		return Object;
	}

	void MarkBuildingChanged(AEHBBuildingActorBase* Building)
	{
		if (!Building)
		{
			return;
		}

		Building->RebuildElementAndRelationshipIndexes();
		Building->RebuildClosedLoops();
		Building->Modify();
		Building->MarkPackageDirty();
	}

	double CalculateSignedArea2D(const TArray<FVector>& Polygon)
	{
		if (Polygon.Num() < 3)
		{
			return 0.0;
		}

		double TwiceArea = 0.0;
		for (int32 Index = 0; Index < Polygon.Num(); ++Index)
		{
			const FVector& A = Polygon[Index];
			const FVector& B = Polygon[(Index + 1) % Polygon.Num()];
			TwiceArea += static_cast<double>(A.X) * static_cast<double>(B.Y)
				- static_cast<double>(B.X) * static_cast<double>(A.Y);
		}
		return TwiceArea * 0.5;
	}

	float Cross2D(const FVector& A, const FVector& B, const FVector& C)
	{
		return (B.X - A.X) * (C.Y - A.Y) - (B.Y - A.Y) * (C.X - A.X);
	}

	bool ArePointsNearlyEqual2D(const FVector& A, const FVector& B)
	{
		return FVector::DistSquared2D(A, B) <= EHBToolsetPolygonTolerance * EHBToolsetPolygonTolerance;
	}

	bool IsPointOnSegment2D(const FVector& Point, const FVector& A, const FVector& B)
	{
		if (FMath::Abs(Cross2D(A, B, Point)) > EHBToolsetPolygonTolerance)
		{
			return false;
		}
		return Point.X >= FMath::Min(A.X, B.X) - EHBToolsetPolygonTolerance
			&& Point.X <= FMath::Max(A.X, B.X) + EHBToolsetPolygonTolerance
			&& Point.Y >= FMath::Min(A.Y, B.Y) - EHBToolsetPolygonTolerance
			&& Point.Y <= FMath::Max(A.Y, B.Y) + EHBToolsetPolygonTolerance;
	}

	bool IsPointInsidePolygon2D(const FVector& Point, const TArray<FVector>& Polygon)
	{
		if (Polygon.Num() < 3)
		{
			return false;
		}

		bool bInside = false;
		for (int32 CurrentIndex = 0, PreviousIndex = Polygon.Num() - 1;
			CurrentIndex < Polygon.Num();
			PreviousIndex = CurrentIndex++)
		{
			const FVector& Current = Polygon[CurrentIndex];
			const FVector& Previous = Polygon[PreviousIndex];
			if (IsPointOnSegment2D(Point, Previous, Current))
			{
				return true;
			}

			const bool bCrosses = (Current.Y > Point.Y) != (Previous.Y > Point.Y);
			if (!bCrosses)
			{
				continue;
			}

			const double IntersectionX =
				(Previous.X - Current.X) * (Point.Y - Current.Y) / (Previous.Y - Current.Y) + Current.X;
			if (Point.X < IntersectionX)
			{
				bInside = !bInside;
			}
		}
		return bInside;
	}

	bool SanitizeHolePolygon(TArray<FVector>& InOutPolygon, float Z)
	{
		TArray<FVector> Sanitized;
		Sanitized.Reserve(InOutPolygon.Num());
		for (FVector Point : InOutPolygon)
		{
			Point.Z = Z;
			if (Sanitized.IsEmpty() || !ArePointsNearlyEqual2D(Sanitized.Last(), Point))
			{
				Sanitized.Add(Point);
			}
		}
		if (Sanitized.Num() >= 2 && ArePointsNearlyEqual2D(Sanitized[0], Sanitized.Last()))
		{
			Sanitized.Pop(EAllowShrinking::No);
		}
		if (Sanitized.Num() < 3)
		{
			return false;
		}

		InOutPolygon = MoveTemp(Sanitized);
		return true;
	}

	bool SanitizeOuterPolygon(TArray<FVector>& InOutPolygon, float Z)
	{
		if (!SanitizeHolePolygon(InOutPolygon, Z))
		{
			return false;
		}
		return FMath::Abs(CalculateSignedArea2D(InOutPolygon)) > 1.0;
	}

 double ClipRegionArea(const FEHBPolygonClipResult& Result)
 {
  double Area=0;for(const auto& R:Result.Regions){Area+=FMath::Abs(CalculateSignedArea2D(R.OuterLoop));for(const auto& H:R.HoleLoops)Area-=FMath::Abs(CalculateSignedArea2D(H.ToLocalPositions()));}return Area;
 }
 bool IsHoleInsidePolygon(const TArray<FVector>& OuterPolygon,const FEHBFloorSlabHole& Hole)
 {
  const auto& Polygon=Hole.LocalPolygon;
  auto Invalid=[](const FVector& P){return P.ContainsNaN()||FMath::Abs(P.X)>1.e8||FMath::Abs(P.Y)>1.e8;};
  if(Polygon.Num()<3||OuterPolygon.Num()<3||Polygon.ContainsByPredicate(Invalid)||OuterPolygon.ContainsByPredicate(Invalid))return false;
  // Compare on the clipper's grid on both sides. Unquantized rotated areas
  // differ slightly even when the candidate is entirely inside the surface.
  FEHBPolygonClipResult Normalized;if(!FEHBPolygonClipper::UnionXY({Polygon},Normalized))return false;
  const double Area=ClipRegionArea(Normalized);if(!FMath::IsFinite(Area)||Area<=0.01)return false;
  FEHBPolygonClipResult Intersection;return FEHBPolygonClipper::IntersectionXY(Polygon,OuterPolygon,Intersection)&&FMath::Abs(ClipRegionArea(Intersection)-Area)<=0.01;
 }
 bool IsHoleInsideSlab(const AEHB_FloorSlab& FloorSlab,const TArray<FVector>& HolePolygon)
 {
  TArray<FEHBPlanarSurfaceRegion> Regions;if(!FloorSlab.BuildEffectiveDisplayRegions(Regions,false,false))return false;
  FEHBFloorSlabHole Candidate;Candidate.LocalPolygon=HolePolygon;
  for(const auto& R:Regions)
  {
   if(!IsHoleInsidePolygon(R.BoundaryLoop,Candidate))continue;
   bool Overlaps=false;for(const auto& H:R.HoleLoops){FEHBPolygonClipResult Intersection;if(FEHBPolygonClipper::IntersectionXY(HolePolygon,H.LocalLoop,Intersection)&&ClipRegionArea(Intersection)>0.01){Overlaps=true;break;}}
   if(!Overlaps)return true;
  }
  return false;
 }

	FEHBToolsetOperationResult CommitFloorSlabHole(AEHB_FloorSlab* FloorSlab, TArray<FVector> HolePolygon, const FText& TransactionName)
	{
		FEHBToolsetOperationResult Result;
		AEHBBuildingActorBase* Building = FloorSlab ? ResolveEditableBuilding(FloorSlab->OwningBuilding) : nullptr;
		if (!FloorSlab || !Building)
		{
			Result.Message = TEXT("FloorSlab is null or does not belong to the currently selected EHB_Building.");
			return Result;
		}

		const float TopZ = FloorSlab->GetTopZ();
		if (!SanitizeHolePolygon(HolePolygon, TopZ))
		{
			Result.Message = TEXT("Hole polygon needs at least three unique points.");
			return Result;
		}

		if (!IsHoleInsideSlab(*FloorSlab, HolePolygon))
		{
			Result.Message = TEXT("Hole polygon must stay inside the floor slab outer polygon.");
			return Result;
		}

  auto Holes=FloorSlab->LocalHoles;Holes.AddDefaulted_GetRef().LocalPolygon=MoveTemp(HolePolygon);
  if(Building->WallNodeAuthority.Version==2&&(FloorSlab->OutlineSource==EEHBOutlineSource::RoomBoundary||FloorSlab->OutlineSource==EEHBOutlineSource::RetainedRegion||FloorSlab->DisplayPartition.IsActive()))return UEHBBuildingToolset::SetFloorSlabOpenings(FloorSlab,Holes,FloorSlab->CutOperations,Building->RelationshipGraphRevision,Building->GetElementGeometryRevision(FloorSlab->ElementGuid),false);
  if(!FloorSlab->ValidateSlabOutline(FloorSlab->LocalTopPolygon,Holes)){Result.Message=TEXT("Slab hole candidate geometry is invalid; existing result retained.");return Result;}
  FScopedTransaction Transaction(TransactionName);Building->Modify();
  if(!FloorSlab->SetSlabOutline(FloorSlab->LocalTopPolygon,Holes)){Transaction.Cancel();Result.Message=TEXT("Failed to apply slab hole candidate.");return Result;}
		FloorSlab->NotifyElementGeometryChanged(true);
		FloorSlab->MarkPackageDirty();
		MarkBuildingChanged(Building);
		Result.bSucceeded = true;
		Result.Message = FString::Printf(TEXT("Floor slab hole added. HoleCount=%d"), FloorSlab->LocalHoles.Num());
		return Result;
	}

	FEHBToolsetOperationResult CommitFloorSlabTopPolygon(
		AEHB_FloorSlab* FloorSlab,
		TArray<FVector> NewLocalTopPolygon,
		bool bPreserveExistingHoles,
		const FText& TransactionName)
	{
		FEHBToolsetOperationResult Result;
		AEHBBuildingActorBase* Building = FloorSlab ? ResolveEditableBuilding(FloorSlab->OwningBuilding) : nullptr;
		if (!FloorSlab || !Building)
		{
			Result.Message = TEXT("FloorSlab is null or does not belong to the currently selected EHB_Building.");
			return Result;
		}

		const float TopZ = FloorSlab->GetTopZ();
		if (!SanitizeOuterPolygon(NewLocalTopPolygon, TopZ))
		{
			Result.Message = TEXT("Floor slab polygon needs at least three unique non-collinear points.");
			return Result;
		}

		if (bPreserveExistingHoles)
		{
			for (const FEHBFloorSlabHole& Hole : FloorSlab->LocalHoles)
			{
				if (!IsHoleInsidePolygon(NewLocalTopPolygon, Hole))
				{
					Result.Message = TEXT("Existing holes are not all inside the new slab polygon. Retry with bPreserveExistingHoles=false or move holes first.");
					return Result;
				}
			}
		}

  const TArray<FEHBFloorSlabHole> Holes=bPreserveExistingHoles?FloorSlab->LocalHoles:TArray<FEHBFloorSlabHole>();
  if(!FloorSlab->ValidateSlabOutline(NewLocalTopPolygon,Holes)){Result.Message=TEXT("Slab outline candidate geometry is invalid; existing result retained.");return Result;}
  FScopedTransaction Transaction(TransactionName);Building->Modify();
  if(!FloorSlab->SetSlabOutline(NewLocalTopPolygon,Holes)){Transaction.Cancel();Result.Message=TEXT("Failed to apply slab outline candidate.");return Result;}
		FloorSlab->NotifyElementGeometryChanged(true);
		FloorSlab->MarkPackageDirty();
		MarkBuildingChanged(Building);
		Result.bSucceeded = true;
		Result.Message = FString::Printf(
			TEXT("Floor slab polygon updated. PointCount=%d HoleCount=%d"),
			FloorSlab->LocalTopPolygon.Num(),
			FloorSlab->LocalHoles.Num());
		return Result;
	}

	FString MakePlanPointKey(const FVector& Point, int32 FloorIndex)
	{
		const int32 X = FMath::RoundToInt(Point.X * 10.0f);
		const int32 Y = FMath::RoundToInt(Point.Y * 10.0f);
		const int32 Z = FMath::RoundToInt(Point.Z * 10.0f);
		return FString::Printf(TEXT("%d:%d:%d:%d"), FloorIndex, X, Y, Z);
	}

	FString MakePlanEdgeKey(const FVector& A, const FVector& B, int32 FloorIndex)
	{
		const FString AKey = MakePlanPointKey(A, FloorIndex);
		const FString BKey = MakePlanPointKey(B, FloorIndex);
		return AKey < BKey
			? FString::Printf(TEXT("%s|%s"), *AKey, *BKey)
			: FString::Printf(TEXT("%s|%s"), *BKey, *AKey);
	}

	int32 QuantizePlanValue(double Value)
	{
		return FMath::RoundToInt(Value * 10.0);
	}

	FString MakePlanLineKey(bool bHorizontal, int32 FloorIndex, double Z, double Constant)
	{
		return FString::Printf(
			TEXT("%d:%s:%d:%d"),
			FloorIndex,
			bHorizontal ? TEXT("H") : TEXT("V"),
			QuantizePlanValue(Z),
			QuantizePlanValue(Constant));
	}

	struct FEHBAIWallInterval
	{
		double Min = 0.0;
		double Max = 0.0;
	};

	struct FEHBAIWallLine
	{
		bool bHorizontal = true;
		int32 FloorIndex = 1;
		double Z = 0.0;
		double Constant = 0.0;
		TArray<FEHBAIWallInterval> Intervals;
		TArray<double> Breaks;
	};

	struct FEHBAIWallSegment
	{
		int32 FloorIndex = 1;
		FVector Start = FVector::ZeroVector;
		FVector End = FVector::ZeroVector;
	};

	bool IsValueInsideInterval(double Value, const FEHBAIWallInterval& Interval, double Tolerance)
	{
		return Value >= Interval.Min - Tolerance && Value <= Interval.Max + Tolerance;
	}

	bool IsSpanCoveredByAnyInterval(double Start, double End, const TArray<FEHBAIWallInterval>& Intervals, double Tolerance)
	{
		const double Mid = (Start + End) * 0.5;
		for (const FEHBAIWallInterval& Interval : Intervals)
		{
			if (Start >= Interval.Min - Tolerance && End <= Interval.Max + Tolerance && IsValueInsideInterval(Mid, Interval, Tolerance))
			{
				return true;
			}
		}
		return false;
	}

	void SortUniqueWallBreaks(TArray<double>& Breaks, double Tolerance)
	{
		Breaks.Sort();
		TArray<double> UniqueBreaks;
		for (double BreakValue : Breaks)
		{
			if (UniqueBreaks.IsEmpty() || !FMath::IsNearlyEqual(UniqueBreaks.Last(), BreakValue, Tolerance))
			{
				UniqueBreaks.Add(BreakValue);
			}
		}
		Breaks = MoveTemp(UniqueBreaks);
	}

	TSharedPtr<FJsonValue> MakeVectorJsonValue(const FVector& Value)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetNumberField(TEXT("x"), Value.X);
		Object->SetNumberField(TEXT("y"), Value.Y);
		Object->SetNumberField(TEXT("z"), Value.Z);
		return MakeShared<FJsonValueObject>(Object);
	}

	bool CalculateToolsetStairLayout(
		float StairHeight,
		float TreadDepth,
		float MinStepHeight,
		float MaxStepHeight,
		float NosingLength,
		int32& OutStepCount,
		float& OutStepHeight,
		float& OutStairLength)
	{
		OutStepCount = 0;
		OutStepHeight = 0.0f;
		OutStairLength = FMath::Max(1.0f, TreadDepth);

		const float SafeStairHeight = FMath::Max(1.0f, StairHeight);
		float SafeMinStepHeight = FMath::Max(1.0f, MinStepHeight);
		float SafeMaxStepHeight = FMath::Max(1.0f, MaxStepHeight);
		if (SafeMinStepHeight > SafeMaxStepHeight)
		{
			Swap(SafeMinStepHeight, SafeMaxStepHeight);
		}

		OutStepCount = FMath::Max(1, FMath::CeilToInt(SafeStairHeight / SafeMaxStepHeight));
		OutStepHeight = SafeStairHeight / static_cast<float>(OutStepCount);
		while (OutStepCount > 1 && OutStepHeight < SafeMinStepHeight)
		{
			--OutStepCount;
			OutStepHeight = SafeStairHeight / static_cast<float>(OutStepCount);
		}

		const float SafeTreadDepth = FMath::Max(1.0f, TreadDepth);
		const float SafeNosingLength = FMath::Clamp(NosingLength, 0.0f, FMath::Max(0.0f, SafeTreadDepth - 1.0f));
		const float StepRun = FMath::Max(1.0f, SafeTreadDepth - SafeNosingLength);
		OutStairLength = SafeTreadDepth + StepRun * static_cast<float>(FMath::Max(0, OutStepCount - 1));
		return true;
	}

	bool SampleToolsetSideSegment(
		const TArray<FVector>& InPoints,
		bool bClosedLoop,
		int32 SideSegmentIndex,
		float DistanceAlongSide,
		bool bUseLeftSide,
		float SideOffset,
		FVector& OutLocalStart,
		float& OutUpDirectionYawDegrees)
	{
		TArray<FVector> Points = InPoints;
		if (bClosedLoop && Points.Num() > 1 && Points[0].Equals(Points.Last(), 0.1f))
		{
			Points.Pop();
		}

		const int32 SegmentCount = bClosedLoop ? Points.Num() : Points.Num() - 1;
		if (SegmentCount <= 0)
		{
			return false;
		}

		const int32 WrappedSegmentIndex = (SideSegmentIndex % SegmentCount + SegmentCount) % SegmentCount;
		const int32 NextPointIndex = bClosedLoop
			? (WrappedSegmentIndex + 1) % Points.Num()
			: WrappedSegmentIndex + 1;
		if (!Points.IsValidIndex(WrappedSegmentIndex) || !Points.IsValidIndex(NextPointIndex))
		{
			return false;
		}

		const FVector A = Points[WrappedSegmentIndex];
		const FVector B = Points[NextPointIndex];
		FVector Segment = B - A;
		Segment.Z = 0.0f;
		const float SegmentLength = Segment.Size();
		if (SegmentLength <= UE_KINDA_SMALL_NUMBER)
		{
			return false;
		}

		const FVector SegmentDirection = Segment / SegmentLength;
		const float ClampedDistance = FMath::Clamp(DistanceAlongSide, 0.0f, SegmentLength);
		const FVector BasePoint = A + SegmentDirection * ClampedDistance;
		const FVector LeftNormal(-SegmentDirection.Y, SegmentDirection.X, 0.0f);
		const FVector SideNormal = bUseLeftSide ? LeftNormal : -LeftNormal;

		OutLocalStart = BasePoint + SideNormal * FMath::Max(0.0f, SideOffset);
		OutUpDirectionYawDegrees = SideNormal.Rotation().Yaw;
		return true;
	}
}

FString UEHBBuildingToolset::GetModelingGuide()
{
	return FString(TEXT(R"GUIDE(# Parametric Building Toolset AI Guide

## Startup
1. Call GetToolsetCapabilities, GetModelingGuide and GetStatus before creating or editing.
2. AI creation requires bHasSelectedBuilding=true. The target is the currently selected EHB_Building actor in the editor.
3. If no EHB_Building is selected, stop and ask the user to select a building object. Do not auto-create a building and do not guess a target.
4. If Building Mode is active, the selected building is synchronized into Building Mode as the active building.
5. All coordinates passed to creation tools are Building-local centimeters unless explicitly documented otherwise.

## Creation Rules
- Use EHB functions only. Do not spawn EHB element actors with generic editor tools.
- Create walls only through pillar/wall relationships: CreatePillar then ConnectPillars, or preferably a higher-level recipe.
- For AI-created houses, pillar width and depth must equal the connected wall thickness. Use PillarSize=WallThickness in high-level recipes.
- For ordinary AI-generated houses and multi-room layouts, prefer CreateAIHouseFromRoomPlan.
- CreateAIHouseFromRoomPlan converts rectangular rooms into a wall-line graph, splits shared endpoints, partial overlaps and T-junctions, reuses pillars, creates unique wall segments, and can create foundation and room slabs.
- Use CreateRectangularFloorPlan only for simple aligned rectangular rooms where shared walls are full matching edges.
- Use CreateRoomFilledFloorSlabAtPoint for normal room floors/ceilings. Use CreateFloorSlab with bFoundation=true for ground-touching platforms, porches and foundations.
- Add doors/windows only with AddDoorWindow on an existing wall. Doors use SillHeight=0; windows may use a positive sill height.
- Add roofs only after the top-floor enclosed mass is stable. Prefer one clean gable or hip roof over a coherent enclosed volume.
- For indoor stairs, cut the upper slab opening first, then create the stair from the slab/hole side.

## Validation
- After creation, call GetBuildingSnapshot and GetFloorSummary for the selected building.
- If a tool returns ok=false, stop creating new elements and report the message to the user.
- If a requested element type is not exposed by this toolset, explain that it is not supported yet instead of faking it with unrelated EHB elements.
)GUIDE"));
}
FString UEHBBuildingToolset::GetToolsetCapabilities()
{
	auto MakeFunctionObject = [](
		const FString& Name,
		const FString& UseWhen,
		std::initializer_list<const TCHAR*> KeyParameters,
		std::initializer_list<const TCHAR*> Returns,
		std::initializer_list<const TCHAR*> CommonMistakes)
	{
		TSharedRef<FJsonObject> Function = MakeShared<FJsonObject>();
		Function->SetStringField(TEXT("name"), Name);
		Function->SetStringField(TEXT("useWhen"), UseWhen);
		Function->SetArrayField(TEXT("keyParameters"), MakeStringArray(KeyParameters));
		Function->SetArrayField(TEXT("returns"), MakeStringArray(Returns));
		Function->SetArrayField(TEXT("commonMistakes"), MakeStringArray(CommonMistakes));
		return MakeShared<FJsonValueObject>(Function);
	};

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("ok"), true);
	Root->SetStringField(TEXT("toolset"), TEXT("EasyHouseBuilder"));
	Root->SetStringField(TEXT("purpose"), TEXT("AI-facing relation-safe building creation for the EHB plugin."));

	TSharedRef<FJsonObject> Startup = MakeShared<FJsonObject>();
	Startup->SetArrayField(TEXT("requiredFirstCalls"), MakeStringArray({
		TEXT("GetToolsetCapabilities"),
		TEXT("GetModelingGuide"),
		TEXT("GetStatus")
	}));
	Startup->SetArrayField(TEXT("readinessRequired"), MakeStringArray({
		TEXT("bHasEditorWorld=true"),
		TEXT("bHasSelectedBuilding=true")
	}));
	Startup->SetStringField(TEXT("activeBuildingRule"), TEXT("ActiveBuilding/SelectedBuilding is the existing EHB_Building actor selected for editing. Creation tools must reuse that existing building and must not create a replacement building object."));
	Startup->SetStringField(TEXT("notReadyRule"), TEXT("If no existing EHB_Building can be resolved, stop EHB creation and ask the user to select or manually create a building object. Do not call CreateBuilding as a fallback."));
	Root->SetObjectField(TEXT("startupProtocol"), Startup);

	TSharedRef<FJsonObject> Coordinates = MakeShared<FJsonObject>();
	Coordinates->SetStringField(TEXT("unit"), TEXT("centimeters"));
	Coordinates->SetStringField(TEXT("buildingLocalOrigin"), TEXT("Building actor location is the default house center. Local (0,0,0) is the Building actor location."));
	Coordinates->SetStringField(TEXT("floorIndex"), TEXT("0 is foundation, 1 is first normal floor, 2 is second normal floor, etc."));
	Coordinates->SetStringField(TEXT("floorHeightRule"), TEXT("FloorHeight is the vertical distance between adjacent floor structural bases, usually equal to WallHeight. Floor slabs embed downward into the lower floor volume; do not add slab thickness between first-floor wall top and second-floor wall bottom."));
	Coordinates->SetStringField(TEXT("groundPlatformRule"), TEXT("Do not place normal floor/ceiling slabs on the ground. Ground-touching or low platforms, porches and step landings must be foundations; use bFoundation=true and adjust Thickness/FoundationThickness or TopZ to set their height."));
	Coordinates->SetStringField(TEXT("wallDirection"), TEXT("Wall direction runs from StartPillar to EndPillar. Door/window DistanceFromStart is measured along this centerline."));
	Coordinates->SetStringField(TEXT("stairPreferredConvention"), TEXT("Use CreateStairFromSide when a wall side, floor slab side, or upper slab hole side is known. For indoor stairs, cut the upper slab first, then call CreateStairFromSide with SourceFloorSlab and SlabHoleIndex so the stair starts from the hole side. Use CreateStairFromBottom only when no side anchor is available, and CreateStairBetweenLandings for curved/turned stairs with explicit endpoints. The top landing must touch or slightly overlap the upper floor slab/opening edge; do not leave a visible gap between stair and slab."));
	Root->SetObjectField(TEXT("coordinateSystem"), Coordinates);

	TSharedRef<FJsonObject> Defaults = MakeShared<FJsonObject>();
	Defaults->SetNumberField(TEXT("wallHeight"), 300.0);
	Defaults->SetNumberField(TEXT("wallThickness"), 20.0);
	Defaults->SetNumberField(TEXT("pillarSize"), 20.0);
	Defaults->SetStringField(TEXT("pillarSizeRule"), TEXT("For AI-created houses, pillar width and pillar depth must equal WallThickness. Do not make larger decorative corner pillars unless the user explicitly asks for them."));
	Defaults->SetNumberField(TEXT("floorSlabThickness"), 20.0);
	Defaults->SetNumberField(TEXT("foundationThickness"), 20.0);
	Defaults->SetNumberField(TEXT("doorWidth"), 90.0);
	Defaults->SetNumberField(TEXT("doorHeight"), 210.0);
	Defaults->SetNumberField(TEXT("windowWidth"), 120.0);
	Defaults->SetNumberField(TEXT("windowHeight"), 120.0);
	Defaults->SetNumberField(TEXT("windowSillHeight"), 90.0);
	Defaults->SetNumberField(TEXT("stairHeight"), 300.0);
	Defaults->SetNumberField(TEXT("stairWidth"), 150.0);
	Defaults->SetNumberField(TEXT("stairTreadDepth"), 30.0);
	Defaults->SetNumberField(TEXT("stairMinStepHeight"), 10.0);
	Defaults->SetNumberField(TEXT("stairMaxStepHeight"), 20.0);
	Defaults->SetNumberField(TEXT("railingHeight"), 100.0);
	Defaults->SetNumberField(TEXT("railingPostSpacing"), 120.0);
	Defaults->SetNumberField(TEXT("railingRailThickness"), 8.0);
	Defaults->SetNumberField(TEXT("railingGateWidth"), 90.0);
	Defaults->SetNumberField(TEXT("railingGateHeight"), 95.0);
	Defaults->SetNumberField(TEXT("railingGateThickness"), 6.0);
	Defaults->SetNumberField(TEXT("roofPitchDegrees"), 25.0);
	Defaults->SetNumberField(TEXT("roofThickness"), 20.0);
	Defaults->SetNumberField(TEXT("roofEaveOffset"), 35.0);
	Defaults->SetStringField(TEXT("roofAxisMode"), TEXT("RidgeAlongLongSide"));
	Defaults->SetStringField(TEXT("roofSizingRule"), TEXT("Length/Width should cover the enclosed wall footprint after any planned wall curves are applied and before eave overhang; do not size from balconies, terraces or low platforms."));
	Root->SetObjectField(TEXT("recommendedDefaultsCm"), Defaults);

	TSharedRef<FJsonObject> Policies = MakeShared<FJsonObject>();
	Policies->SetArrayField(TEXT("hardRules"), MakeStringArray({
		TEXT("Always reuse the existing selected/current EHB_Building actor. Do not create a new EHB_Building for ordinary AI generation or modification."),
		TEXT("Do not spawn EHB_Wall, EHB_DoorWindow, EHB_Stair or other EHB element actors through generic editor tools."),
		TEXT("Create walls only by CreatePillar followed by ConnectPillars."),
		TEXT("For AI-created houses, every structural wall pillar must use Width=Depth=WallThickness. In high-level recipes, treat PillarSize as WallThickness."),
		TEXT("Create doors/windows only by AddDoorWindow on an existing wall."),
		TEXT("Create room floor/ceiling slabs with CreateRoomFilledFloorSlabAtPoint, not manually supplied polygons."),
		TEXT("When curved walls are planned, apply SetWallCurve before room floor/ceiling fills; if using CreateAIHouseFromRoomPlan for curved layouts, set bCreateRoomSlabs=false, curve the walls, then fill rooms."),
		TEXT("For room fill, place the slab origin/RoomInteriorPoint inside the target room. Do not place it on a shared wall or rely on AnchorSide unless using the advanced compatibility function."),
		TEXT("Use CreateFloorSlab or slab polygon editing only for foundations or exceptional irregular slabs."),
		TEXT("Do not place normal floor/ceiling slabs on the ground or use them as low platforms."),
		TEXT("For ground-touching or low platforms, porches and step landings, create a foundation with bFoundation=true and bKeepFoundationBottomOnGround=true, then adjust Thickness/FoundationThickness or TopZ for the desired height."),
		TEXT("Use floor slab hole tools before creating stairs through a slab."),
		TEXT("When a wall side, floor slab side or upper slab hole side is available, create stairs with CreateStairFromSide instead of freehand coordinates."),
		TEXT("For indoor stairs through a room, cut the upper floor slab first, then call CreateStairFromSide with SourceFloorSlab and SlabHoleIndex so the stair is created from the hole side."),
		TEXT("Do not edit stair intermediate controls for AI-created stairs; use CreateStairFromSide, CreateStairBetweenLandings or the bottom step control instead."),
		TEXT("Stair endpoints must remain inside a room or stairwell and keep at least max(60cm, StairWidth/2) clearance from walls."),
		TEXT("Stair top landings must touch or slightly overlap the upper floor slab/opening edge; do not leave a visible gap between stair and slab."),
		TEXT("Stair opening clearance belongs around the stair in the slab hole, not between the stair top landing and the slab."),
		TEXT("When stairs meet a foundation, call GetStairFoundationPlan first and align FoundationThickness with the returned stepHeight when possible."),
		TEXT("Create standalone railings only with CreateRailing, stair-hosted railings only with CreateRailingOnStair, and railing gates only with CreateRailingGate."),
		TEXT("If a railing endpoint should be at a pillar, pass that pillar directly as StartPillar or EndPillar on CreateRailing."),
		TEXT("Do not create nearby short railings, overlapping railings, helper walls or extra wall segments to simulate a railing connection to a pillar."),
		TEXT("Do not fake railing gate openings with separate railing pieces or wall segments; use CreateRailingGate on the owning railing."),
		TEXT("Use CreateGableRoof for default gable roofs and CreateHipRoof for default hip/four-slope/four-sided roofs; unsupported sampled roof families should be skipped or explained."),
		TEXT("Do not create one roof per room, balcony, terrace, stair, porch, railing, slab or facade detail. Roofs cover coherent enclosed building masses."),
		TEXT("After substantial edits call GetBuildingSnapshot and GetFloorSummary.")
	}));
	Policies->SetArrayField(TEXT("largeHouseStrategy"), MakeStringArray({
		TEXT("Prefer CreateAIHouseFromRoomPlan for ordinary AI-generated houses and multi-room layouts. It builds a wall-line graph, reuses pillars, splits shared/overlapping/T-junction wall spans, and creates unique short wall segments."),
		TEXT("Use CreateRectangularFloorPlan only for simple aligned rectangular plans where every shared wall is a full matching edge."),
		TEXT("Use low-level CreatePillar/ConnectPillars only for custom topology or after planning all shared endpoints and wall split points."),
		TEXT("Group elements by FloorIndex and verify with GetFloorSummary.")
	}));
	Policies->SetArrayField(TEXT("recommendedWorkflow"), MakeStringArray({
		TEXT("Before designing or creating anything, call GetStatus and GetActiveBuilding, then keep using that returned existing Building object for every subsequent tool call."),
		TEXT("First design the house logic: purpose, entrance, circulation, room functions, adjacency, stair zone and floor heights."),
		TEXT("Design foundation shape and height before upper construction; use foundations for low platforms/porches/step landings, and use GetStairFoundationPlan when foundation steps or stairs are involved."),
		TEXT("For each floor, prefer CreateAIHouseFromRoomPlan from room rectangles to form reasonable closed rooms before adding stairs, doors or windows."),
		TEXT("Apply planned wall curves before room slabs; then add room slabs with CreateRoomFilledFloorSlabAtPoint and cut stair openings large enough for people to pass."),
		TEXT("Add indoor stairs only after the upper slab opening is cut; then create the stair from the hole side with CreateStairFromSide(SourceFloorSlab, SlabHoleIndex)."),
		TEXT("Add doors/windows after room boundaries and stairs are stable."),
		TEXT("Add roofs after the top-floor enclosed mass and planned wall curves are stable; derive roof center and size from enclosed wall footprint, not from decorative platforms or balconies."),
		TEXT("Review whether the current floor is usable for humans before starting the next floor."),
		TEXT("Only start the next floor after snapshot/floor summary checks pass.")
	}));
	Policies->SetArrayField(TEXT("railingCreationRules"), MakeStringArray({
		TEXT("Use CreateRailing for standalone linear railings, CreateRailingOnStair for railings hosted by stairs, and CreateRailingGate for gates hosted by existing railings."),
		TEXT("StartPillar and EndPillar are real railing endpoints. When supplied, their pillar locations replace the matching coordinate endpoints and the railing records anchor relations to those pillars."),
		TEXT("Never add helper walls, duplicate wall segments, nearby filler railings or overlapping railing fragments just to make a railing appear connected to a pillar."),
		TEXT("For both sides of a stair, call CreateRailingOnStair once per side; do not create a free linear railing beside a hosted stair railing."),
		TEXT("For a gate, create the parent railing first, then call CreateRailingGate with DistanceFromStart along that railing.")
	}));
	Policies->SetArrayField(TEXT("roofCreationRules"), MakeStringArray({
		TEXT("Use CreateGableRoof for default gable roofs and CreateHipRoof for default hip/four-slope/four-sided roofs; sampled roof families are not exposed yet."),
		TEXT("When the user says hip roof, hipped roof, four-slope roof, four-sided roof, 鍥涘潯灞嬮《 or 鍥涜竟灞嬮《, call CreateHipRoof."),
		TEXT("Create roofs after the top-floor wall/body mass is stable. Do not add roofs while walls, rooms, slabs or stairs are still being guessed."),
		TEXT("Default to one main gable or hip roof over one simple rectangular enclosed top mass. For ordinary houses, one clean roof is preferred over many small overlapping roofs."),
		TEXT("Use multiple roofs only for clear separate enclosed masses: main wing, perpendicular wing, garage, dormer-like raised volume, or a distinct lower wing."),
		TEXT("Do not create separate roofs for balconies, terraces, railings, low foundations, porches, thin awnings, stair landings, every room, or small facade offsets."),
		TEXT("LocalCenter is the center of the enclosed volume being covered, in Building-local coordinates. For a normal floor, set Z to floor base plus wall height."),
		TEXT("Length and Width are roof-local cover dimensions before eave overhang. Derive them from enclosed wall footprint, not from total foundation/platform extents."),
		TEXT("For AI workflow roof JSON, set autoFitToWallFootprint=true or omit localCenter/length/width to let the executor size the roof from the current wall footprint after curves are applied."),
		TEXT("Use EaveOffset for overhang, normally 25-50cm. Do not combine a huge Length/Width with a huge EaveOffset."),
		TEXT("AxisMode controls ridge direction. RidgeAlongX means ridge follows roof-local X and Length projects along X; RidgeAlongY swaps projection. Usually align the ridge with the longer side of the covered mass."),
		TEXT("Use YawDegrees to align the roof rectangle to the covered mass. Do not leave yaw at 0 when the house wing is rotated."),
		TEXT("Use PitchDegrees around 18-35 for normal residential roofs unless the user requested a very steep or shallow roof."),
		TEXT("Keep bGenerateRidge, bGenerateEaves, bGenerateGableRakes and bGenerateGableEndWalls true for normal roofs; do not spawn separate trim actors for these parts. Set GableEndWallBoundaryInset only when the gable side wall face should be inset from the roof end boundary."),
		TEXT("For hip roofs, use bGenerateHipRidges, HipRidgeWidth and HipRidgeHeight instead of gable end wall parameters; hip roofs do not generate triangular gable end walls."),
		TEXT("For intersecting roofs, create the dominant/main roof first and the secondary/intersecting roof second. Enable bCutCollidingElements only on roofs that should trim themselves."),
		TEXT("The other roof is not trimmed unless its own bCutCollidingElements is true. If only the secondary roof should be cut back, enable the flag only on the secondary roof."),
		TEXT("Use controlled envelope cutter options only when overlaps should bridge obvious concave gaps; keep exact source mesh cutters enabled for precise source data."),
		TEXT("For dormer-like openings built from ordinary walls, keep bUseWallFootprintCutters=true. Connected wall groups are converted into conservative footprint cutter volumes before cutting the roof."),
		TEXT("For wallFootprintPadding / WallFootprintPadding, use a small negative value such as -2 or -4 when the opening must be slightly smaller so the dormer wall covers the cut edge. Positive values enlarge the opening and can expose visible gaps."),
		TEXT("WallFootprintMaxDimension is an optional safety limit. Leave it at 0 for unrestricted wall footprint openings; set it only when a generated roof must reject oversized accidental cutters."),
		TEXT("When diagnosing roof intersections, set bShowCutDebugVisualization=true and keep raw/source/cutter/result bounds enabled. Turn it off after inspection."),
		TEXT("Roof materials are semantic slots: RoofBodyMaterial, RidgeMaterial, EaveMaterial, GableRakeMaterial and GableEndWallMaterial. Do not assume one dropped material should recolor every roof part."),
		TEXT("After roof creation, call GetBuildingSnapshot and inspect roof count, local center, local rotation, length, width and floor assignment.")
	}));
	Policies->SetArrayField(TEXT("roofLayoutChecklist"), MakeStringArray({
		TEXT("Roof count is small: usually 1 for a simple house, 2-3 only for clearly separate enclosed volumes."),
		TEXT("No roof was created for a balcony, terrace, railing, stair, porch platform, or decorative slab."),
		TEXT("Each roof center lies over the enclosed wall footprint it covers."),
		TEXT("Each roof Z is near the top of the owning floor walls, not at foundation or ground platform height."),
		TEXT("Each roof Length/Width covers the enclosed mass with modest eaves, without spanning unrelated outdoor platforms."),
		TEXT("Ridge direction follows the longer side of the covered mass unless the user explicitly requested otherwise."),
		TEXT("Intersecting roofs have intentional bCutCollidingElements settings, not all roofs blindly cutting each other.")
	}));
	Policies->SetArrayField(TEXT("stairCreationRules"), MakeStringArray({
		TEXT("Use CreateStairFromSide whenever the stair should start from a wall side, floor slab side or upper slab hole side."),
		TEXT("SourceFloorSlab and SourceWall are mutually exclusive on CreateStairFromSide; pass exactly one boundary source."),
		TEXT("For indoor stairs, first cut the upper floor slab with AddFloorSlabRectangularHole or AddFloorSlabCircularHole, then pass that slab and the committed SlabHoleIndex to CreateStairFromSide."),
		TEXT("A valid SlabHoleIndex is treated as the upper landing edge, so keep SideOffset at 0 unless intentionally nudging along the selected side normal."),
		TEXT("Do not create indoor stairs in the room center and then move them near the slab; bind creation to the wall, slab edge or slab hole side."),
		TEXT("If the generated stair points to the wrong side of the selected edge, retry CreateStairFromSide with the opposite bUseLeftSide value instead of editing intermediate stair controls.")
	}));
	Policies->SetArrayField(TEXT("floorUsabilityChecklist"), MakeStringArray({
		TEXT("Every major room has a door or connection to a corridor/stair hall."),
		TEXT("Rooms are closed loops and slabs fill the intended room, not an adjacent room."),
		TEXT("Stair bottom and top have landing space and do not terminate at a wall."),
		TEXT("Stair openings align with the stair and are wider/longer than the stair clearance."),
		TEXT("Stair top landing touches or slightly overlaps the upper floor slab/opening edge, with no visible stair-to-slab gap."),
		TEXT("No normal floor/ceiling slab is placed on the ground or used as a low platform; those elements are foundations."),
		TEXT("Next-floor walls start at the previous wall top, without adding slab thickness as a vertical gap."),
		TEXT("Doors/windows avoid pillars, corners, stair openings and invalid wall distances."),
		TEXT("FloorIndex and FloorRole are correct for pillars, walls, slabs, stairs and openings.")
	}));
	Root->SetObjectField(TEXT("policies"), Policies);

	TArray<TSharedPtr<FJsonValue>> Functions;
	Functions.Add(MakeFunctionObject(TEXT("GetStatus"), TEXT("Readiness check before every create/edit batch. AI creation requires an existing selected/current EHB_Building actor."), { TEXT("none") }, { TEXT("bHasEditorWorld"), TEXT("bHasSelectedBuilding"), TEXT("SelectedBuilding"), TEXT("ReadinessMessage") }, { TEXT("Continuing EHB creation when no existing EHB_Building actor is selected or resolvable.") }));
	Functions.Add(MakeFunctionObject(TEXT("CreateBuilding"), TEXT("Guarded compatibility function. It reuses an existing selected/current EHB_Building if one can be resolved and will not auto-spawn a new building for ordinary AI generation."), { TEXT("Name"), TEXT("WorldLocation") }, { TEXT("Existing AEHBBuildingActorBase or null") }, { TEXT("Calling CreateBuilding as the first step of ordinary generation."), TEXT("Expecting this function to create a replacement building object when an existing building should be used.") }));
	Functions.Add(MakeFunctionObject(TEXT("CreatePillar"), TEXT("Create structural wall endpoints in Building local coordinates. For house wall endpoints, set Width and Depth equal to the wall thickness that will be used by ConnectPillars."), { TEXT("Building"), TEXT("Name"), TEXT("LocalLocation"), TEXT("Height"), TEXT("Width"), TEXT("Depth"), TEXT("FloorIndex") }, { TEXT("AEHB_Pillar") }, { TEXT("Using generic actors for wall endpoints."), TEXT("Using pillar Width/Depth different from the connected wall thickness."), TEXT("Forgetting FloorIndex on multi-floor buildings.") }));
	Functions.Add(MakeFunctionObject(TEXT("ConnectPillars"), TEXT("Create relation-safe walls between existing pillars."), { TEXT("Building"), TEXT("StartPillar"), TEXT("EndPillar"), TEXT("WallHeight"), TEXT("WallThickness") }, { TEXT("AEHB_Wall") }, { TEXT("Directly spawning EHB_Wall."), TEXT("Connecting pillars from a non-active building.") }));
	Functions.Add(MakeFunctionObject(TEXT("AddDoorWindow"), TEXT("Attach a door/window opening to an existing wall and rebuild the wall cutout."), { TEXT("Wall"), TEXT("bDoor"), TEXT("DistanceFromStart"), TEXT("Width"), TEXT("Height"), TEXT("SillHeight"), TEXT("OpeningThickness") }, { TEXT("AEHB_DoorWindow") }, { TEXT("Creating EHB_DoorWindow directly."), TEXT("Scaling a default door/window after creation instead of passing dimensions."), TEXT("Placing the opening outside the wall length.") }));
	Functions.Add(MakeFunctionObject(TEXT("ReplaceDoorWindow"), TEXT("Replace an existing wall-hosted door/window with another door/window actor, preserving the replacement actor's class, spline shape and current wall position while removing the old actor."), { TEXT("ReplacementDoorWindow"), TEXT("ExistingDoorWindow") }, { TEXT("bool") }, { TEXT("Destroying the old actor manually without clearing wall relations."), TEXT("Keeping the existing actor when the replacement actor should be preferred.") }));
	Functions.Add(MakeFunctionObject(TEXT("CreateFloorSlab"), TEXT("Create foundations or exceptional manual slabs from Building-local polygons. Use bFoundation=true for any ground-touching or low platform; normal floor slabs should not sit on the ground."), { TEXT("Building"), TEXT("Name"), TEXT("LocalTopPolygon"), TEXT("Thickness"), TEXT("TopZ"), TEXT("bFoundation"), TEXT("bKeepFoundationBottomOnGround"), TEXT("VisualExpansion"), TEXT("FloorIndex") }, { TEXT("AEHB_FloorSlab") }, { TEXT("Using this for ordinary room slabs instead of CreateRoomFilledFloorSlabAtPoint."), TEXT("Creating a ground-level or low platform with bFoundation=false."), TEXT("Supplying self-intersecting or fewer-than-3-point polygons.") }));
	Functions.Add(MakeFunctionObject(TEXT("CreateGableRoof"), TEXT("Create a default gable roof over a coherent enclosed building mass. Use after walls/top mass are stable; do not use it for every room, porch, balcony, terrace or decorative platform."), { TEXT("Building"), TEXT("Name"), TEXT("LocalCenter"), TEXT("YawDegrees"), TEXT("Length"), TEXT("Width"), TEXT("PitchDegrees"), TEXT("Thickness"), TEXT("EaveOffset"), TEXT("RidgeOffsetRatio"), TEXT("AxisMode"), TEXT("bGenerateRidge"), TEXT("bGenerateEaves"), TEXT("bGenerateGableRakes"), TEXT("bGenerateGableEndWalls"), TEXT("RidgeWidth"), TEXT("RidgeHeight"), TEXT("EaveWidth"), TEXT("EaveHeight"), TEXT("GableRakeWidth"), TEXT("GableRakeHeight"), TEXT("GableEndWallBoundaryInset"), TEXT("bCutCollidingElements"), TEXT("bRemoveDisconnectedCutPieces"), TEXT("bKeepCutAwayDisconnectedPieces"), TEXT("bUseExactSourceMeshCutters"), TEXT("bUseControlledEnvelopeCutters"), TEXT("EnvelopeConcavityBridgeDistance"), TEXT("EnvelopeProjectionPadding"), TEXT("EnvelopeZPadding"), TEXT("EnvelopeMinExtrudeHeight"), TEXT("EnvelopeMinProjectedArea"), TEXT("EnvelopeThinProjectionFallbackWidth"), TEXT("EnvelopePathCleanTolerance"), TEXT("FloorIndex"), TEXT("bUseWallFootprintCutters"), TEXT("MinWallFootprintGroupWallCount"), TEXT("WallFootprintGroupEndpointTolerance"), TEXT("WallFootprintPadding"), TEXT("WallFootprintMaxDimension"), TEXT("WallFootprintMinArea"), TEXT("bShowCutDebugVisualization"), TEXT("bDebugDrawRawRoofBounds"), TEXT("bDebugDrawSourceBounds"), TEXT("bDebugDrawCutterBounds"), TEXT("bDebugDrawResultBounds"), TEXT("CutDebugDrawDuration"), TEXT("CutDebugDrawThickness") }, { TEXT("AEHBGableRoof") }, { TEXT("Placing LocalCenter in world space instead of Building-local space."), TEXT("Creating one roof per room, balcony, porch, stair landing, terrace, railing, slab or facade offset."), TEXT("Sizing Length/Width from total foundation/platform extents instead of enclosed wall footprint."), TEXT("Leaving YawDegrees at 0 for a rotated wing."), TEXT("Expecting the other roof to be cut when only this roof has bCutCollidingElements=true."), TEXT("Setting WallFootprintMaxDimension to a small value when large wall footprint roof openings are intended; use 0 for unrestricted openings."), TEXT("Leaving roof debug visualization enabled after diagnosis."), TEXT("Using unsupported sampled roof types instead of default gable roofs.") }));
	Functions.Add(MakeFunctionObject(TEXT("CreateHipRoof"), TEXT("Create a default hip/four-slope/four-sided roof with four sloped planes over a coherent enclosed building mass. Use it when the user says hip roof, hipped roof, four-slope roof, four-sided roof, 鍥涘潯灞嬮《 or 鍥涜竟灞嬮《, or when cleaner residential roof massing without gable end walls is desired."), { TEXT("Building"), TEXT("Name"), TEXT("LocalCenter"), TEXT("YawDegrees"), TEXT("Length"), TEXT("Width"), TEXT("PitchDegrees"), TEXT("Thickness"), TEXT("EaveOffset"), TEXT("RidgeOffsetRatio"), TEXT("AxisMode"), TEXT("bGenerateRidge"), TEXT("bGenerateEaves"), TEXT("bGenerateHipRidges"), TEXT("RidgeWidth"), TEXT("RidgeHeight"), TEXT("EaveWidth"), TEXT("EaveHeight"), TEXT("HipRidgeWidth"), TEXT("HipRidgeHeight"), TEXT("bCutCollidingElements"), TEXT("bRemoveDisconnectedCutPieces"), TEXT("bKeepCutAwayDisconnectedPieces"), TEXT("bUseExactSourceMeshCutters"), TEXT("bUseControlledEnvelopeCutters"), TEXT("EnvelopeConcavityBridgeDistance"), TEXT("EnvelopeProjectionPadding"), TEXT("EnvelopeZPadding"), TEXT("EnvelopeMinExtrudeHeight"), TEXT("EnvelopeMinProjectedArea"), TEXT("EnvelopeThinProjectionFallbackWidth"), TEXT("EnvelopePathCleanTolerance"), TEXT("FloorIndex"), TEXT("bUseWallFootprintCutters"), TEXT("MinWallFootprintGroupWallCount"), TEXT("WallFootprintGroupEndpointTolerance"), TEXT("WallFootprintPadding"), TEXT("WallFootprintMaxDimension"), TEXT("WallFootprintMinArea"), TEXT("bShowCutDebugVisualization"), TEXT("bDebugDrawRawRoofBounds"), TEXT("bDebugDrawSourceBounds"), TEXT("bDebugDrawCutterBounds"), TEXT("bDebugDrawResultBounds"), TEXT("CutDebugDrawDuration"), TEXT("CutDebugDrawThickness") }, { TEXT("AEHBHipRoof") }, { TEXT("Using CreateGableRoof when the request says hip, hipped, four-slope, four-sided, 鍥涘潯灞嬮《 or 鍥涜竟灞嬮《."), TEXT("Using gable end wall parameters on a hip roof."), TEXT("Creating many overlapping hip roofs for small facade details."), TEXT("Forgetting to align AxisMode and YawDegrees with the covered mass."), TEXT("Expecting another roof to be cut unless that other roof also enables bCutCollidingElements.") }));
	Functions.Add(MakeFunctionObject(TEXT("CreateRoomFilledFloorSlabAtPoint"), TEXT("AI-preferred room fill: create a normal floor/ceiling slab by placing its origin at a point inside the target closed room. Do not use this for ground platforms."), { TEXT("Building"), TEXT("Name"), TEXT("RoomInteriorPoint"), TEXT("TopZ"), TEXT("Thickness"), TEXT("FloorIndex") }, { TEXT("AEHB_FloorSlab") }, { TEXT("Passing a point on a shared wall instead of inside the room."), TEXT("Calling before a closed wall loop exists."), TEXT("Calling before planned wall curves are applied."), TEXT("Using room fill to create a ground-touching or low platform instead of a foundation.") }));
	Functions.Add(MakeFunctionObject(TEXT("CreateRoomFilledFloorSlab"), TEXT("Advanced compatibility room fill from an anchor wall and side when the side is known. Do not use this for ground platforms."), { TEXT("Building"), TEXT("Name"), TEXT("AnchorWall"), TEXT("AnchorSide"), TEXT("TopZ"), TEXT("Thickness"), TEXT("FloorIndex") }, { TEXT("AEHB_FloorSlab") }, { TEXT("Choosing the wrong AnchorSide; prefer CreateRoomFilledFloorSlabAtPoint for AI."), TEXT("Using room fill to create a ground-touching or low platform instead of a foundation.") }));
	Functions.Add(MakeFunctionObject(TEXT("AddFloorSlabRectangularHole"), TEXT("Commit a rectangular hole to a slab, usually for stairs."), { TEXT("FloorSlab"), TEXT("LocalCenter"), TEXT("Width"), TEXT("Depth"), TEXT("YawDegrees") }, { TEXT("FEHBToolsetOperationResult") }, { TEXT("Putting hole points outside the slab polygon."), TEXT("Using temporary meshes to fake holes.") }));
	Functions.Add(MakeFunctionObject(TEXT("GetStairFoundationPlan"), TEXT("Plan stair step count/height and foundation thickness before creating stairs attached to a foundation."), { TEXT("StairHeight"), TEXT("TreadDepth"), TEXT("DesiredStepHeight"), TEXT("FoundationThickness"), TEXT("MinStepHeight"), TEXT("MaxStepHeight"), TEXT("NosingLength") }, { TEXT("JSON with stepHeight, stepCount, stairLength, recommendedFoundationThickness") }, { TEXT("Creating a foundation with a height unrelated to the stair riser height.") }));
	Functions.Add(MakeFunctionObject(TEXT("CreateStairFromSide"), TEXT("Create a stair from a wall side, floor slab outer side, or committed upper floor slab hole side. For indoor stairs, cut the upper slab hole first, then pass SourceFloorSlab and SlabHoleIndex so the stair is created from the hole side."), { TEXT("Building"), TEXT("Name"), TEXT("SourceFloorSlab"), TEXT("SourceWall"), TEXT("SideSegmentIndex"), TEXT("DistanceAlongSide"), TEXT("bUseLeftSide"), TEXT("SideOffset"), TEXT("SlabHoleIndex"), TEXT("bSideIsUpperLanding"), TEXT("StairHeight"), TEXT("StairWidth"), TEXT("TreadDepth"), TEXT("FloorIndex") }, { TEXT("AEHB_Stair") }, { TEXT("Passing both SourceFloorSlab and SourceWall."), TEXT("Creating indoor stairs before cutting the upper slab hole."), TEXT("Leaving SlabHoleIndex=-1 when anchoring to a slab hole side."), TEXT("Using SideOffset to create a visible gap at the upper slab hole edge."), TEXT("Editing intermediate stair controls instead of retrying with the opposite bUseLeftSide.") }));
	Functions.Add(MakeFunctionObject(TEXT("CreateStairBetweenLandings"), TEXT("Endpoint-only stair creation for curved/turned stairs from lower foot and upper landing; place LocalTopLocation on the upper slab/opening edge."), { TEXT("Building"), TEXT("Name"), TEXT("LocalBottomLocation"), TEXT("BottomUpDirectionYawDegrees"), TEXT("LocalTopLocation"), TEXT("TopDownDirectionYawDegrees"), TEXT("StairWidth"), TEXT("TreadDepth"), TEXT("MinStepHeight"), TEXT("MaxStepHeight"), TEXT("NosingLength"), TEXT("FloorIndex") }, { TEXT("AEHB_Stair") }, { TEXT("Trying to adjust intermediate stair controls."), TEXT("Using TopDownDirectionYawDegrees as the upward direction."), TEXT("Leaving a visible gap between the top landing and the slab/opening edge.") }));
	Functions.Add(MakeFunctionObject(TEXT("CreateStairFromBottom"), TEXT("Grounded stair creation from a lower-floor foot point and upward direction when no wall/slab/hole side anchor is available; choose the bottom point/direction so the computed top landing reaches the slab/opening edge."), { TEXT("Building"), TEXT("Name"), TEXT("LocalBottomLocation"), TEXT("UpDirectionYawDegrees"), TEXT("StairHeight"), TEXT("StairWidth"), TEXT("TreadDepth"), TEXT("MinStepHeight"), TEXT("MaxStepHeight"), TEXT("NosingLength"), TEXT("FloorIndex") }, { TEXT("AEHB_Stair") }, { TEXT("Using CreateStair top-anchor semantics when only a bottom point is known."), TEXT("Passing the downward direction instead of UpDirectionYawDegrees."), TEXT("Leaving a visible gap between the top landing and the slab/opening edge."), TEXT("Using this instead of CreateStairFromSide when a wall/slab/hole side anchor is available.") }));
	Functions.Add(MakeFunctionObject(TEXT("CreateStair"), TEXT("Advanced top-anchor stair creation when the upper landing point and downward direction are known; LocalTopLocation must be on the upper slab/opening edge."), { TEXT("Building"), TEXT("Name"), TEXT("LocalTopLocation"), TEXT("YawDegrees"), TEXT("StairHeight"), TEXT("StairWidth"), TEXT("TreadDepth"), TEXT("MinStepHeight"), TEXT("MaxStepHeight"), TEXT("NosingLength"), TEXT("FloorIndex") }, { TEXT("AEHB_Stair") }, { TEXT("Passing a lower-floor point as LocalTopLocation."), TEXT("Using the up direction as YawDegrees; this function expects top-to-bottom direction."), TEXT("Leaving a visible gap between the top landing and the slab/opening edge.") }));
	Functions.Add(MakeFunctionObject(TEXT("CreateRailing"), TEXT("Create a standalone procedural railing along a Building-local line for balconies, terraces, porches or platform edges. StartPillar/EndPillar can replace coordinate endpoints and anchor the railing to existing pillars."), { TEXT("Building"), TEXT("Name"), TEXT("LocalStart"), TEXT("LocalEnd"), TEXT("RailingHeight"), TEXT("PostSpacing"), TEXT("RailThickness"), TEXT("FillMode"), TEXT("FloorIndex"), TEXT("StartPillar"), TEXT("EndPillar") }, { TEXT("AEHB_Railing") }, { TEXT("Spawning EHB_Railing directly."), TEXT("Passing world-space endpoints instead of Building-local endpoints."), TEXT("Using this for stair railings instead of CreateRailingOnStair."), TEXT("Leaving duplicate railing posts on top of pillar endpoints instead of passing StartPillar/EndPillar."), TEXT("Creating nearby short railings, overlapping railings or helper walls to simulate a pillar connection.") }));
	Functions.Add(MakeFunctionObject(TEXT("CreateRailingOnStair"), TEXT("Create a railing hosted by an existing EHB stair, following that stair's path and relationship graph."), { TEXT("Building"), TEXT("Name"), TEXT("Stair"), TEXT("Side"), TEXT("RailingHeight"), TEXT("StepsPerPost"), TEXT("RailThickness"), TEXT("StairSideOffset"), TEXT("FloorIndex=0 to inherit") }, { TEXT("AEHB_Railing") }, { TEXT("Creating a free linear railing beside a stair when it should stay hosted."), TEXT("Passing a stair from another building."), TEXT("Forgetting to create left and right sides as separate calls when both are needed.") }));
	Functions.Add(MakeFunctionObject(TEXT("CreateRailingGate"), TEXT("Create a gate hosted by an existing railing. The owning railing stores GateConnections and cuts its posts/rails/panels around the gate opening."), { TEXT("Building"), TEXT("Name"), TEXT("Railing"), TEXT("DistanceFromStart"), TEXT("GateWidth"), TEXT("GateHeight"), TEXT("GateThickness"), TEXT("HingeSide"), TEXT("OpenAngleDegrees"), TEXT("FloorIndex=0 to inherit") }, { TEXT("AEHB_RailingGate") }, { TEXT("Creating separate railing fragments to leave a gap for a gate."), TEXT("Creating wall segments as fake gate posts."), TEXT("Passing a railing from another building."), TEXT("Placing the gate before creating the parent railing.") }));
	Functions.Add(MakeFunctionObject(TEXT("CreateRectangularRoom"), TEXT("Create one relation-safe rectangular room centered on the Building actor by default. PillarSize is forced to WallThickness for AI-created house consistency."), { TEXT("Building"), TEXT("NamePrefix"), TEXT("LocalCenter"), TEXT("Width"), TEXT("Depth"), TEXT("WallHeight"), TEXT("WallThickness"), TEXT("PillarSize=WallThickness"), TEXT("bCreateFoundation"), TEXT("bCreateCeilingSlab"), TEXT("FloorIndex"), TEXT("FoundationThickness") }, { TEXT("JSON string with created pillars/walls/slabs") }, { TEXT("Using it repeatedly for adjacent rooms instead of CreateRectangularFloorPlan."), TEXT("Expecting PillarSize to differ from WallThickness in AI house generation.") }));
	Functions.Add(MakeFunctionObject(TEXT("CreateAIHouseFromRoomPlan"), TEXT("AI-preferred multi-room house shell generator. Input rectangular room specs; it builds a wall-line graph, splits shared endpoints, overlapping spans and T-junctions, reuses pillars, creates unique short wall segments, and optionally creates foundation and room-filled slabs. PillarSize is forced to WallThickness."), { TEXT("Building"), TEXT("NamePrefix"), TEXT("Rooms"), TEXT("WallHeight"), TEXT("WallThickness"), TEXT("PillarSize=WallThickness"), TEXT("FloorHeight"), TEXT("bCreateFoundation"), TEXT("bCreateRoomSlabs"), TEXT("FoundationPadding"), TEXT("FoundationThickness") }, { TEXT("JSON string with created pillars/walls/slabs and wall graph counts") }, { TEXT("Calling low-level CreatePillar/ConnectPillars for ordinary room layouts."), TEXT("Using CreateRectangularFloorPlan for T-junction or partial shared wall layouts."), TEXT("Providing overlapping room rectangles unless the overlap is intentional and should become merged wall spans."), TEXT("Passing a PillarSize different from WallThickness.") }));
	Functions.Add(MakeFunctionObject(TEXT("CreateRectangularFloorPlan"), TEXT("Create large multi-room rectangular plans while reusing shared pillars/walls. PillarSize is forced to WallThickness for AI-created house consistency."), { TEXT("Building"), TEXT("NamePrefix"), TEXT("Rooms"), TEXT("WallHeight"), TEXT("WallThickness"), TEXT("PillarSize=WallThickness"), TEXT("FloorHeight"), TEXT("bCreateFoundation"), TEXT("bCreateCeilingSlabs"), TEXT("FoundationPadding"), TEXT("FoundationThickness") }, { TEXT("JSON string with created pillars/walls/slabs") }, { TEXT("Calling CreateRectangularRoom repeatedly for adjacent rooms, causing duplicate shared walls."), TEXT("Using inconsistent room centers/sizes so shared edges do not align."), TEXT("Expecting PillarSize to differ from WallThickness in AI house generation.") }));
	Functions.Add(MakeFunctionObject(TEXT("GetBuildingSnapshot"), TEXT("Inspect created elements, local transforms, wall connections, slab holes, stair layout and relation counts."), { TEXT("Building or null for active building") }, { TEXT("JSON snapshot") }, { TEXT("Continuing after failed creation without inspecting snapshot.") }));
	Functions.Add(MakeFunctionObject(TEXT("GetFloorSummary"), TEXT("Verify multi-floor element grouping and closed-loop room counts."), { TEXT("Building") }, { TEXT("JSON floor summary") }, { TEXT("Leaving generated elements on wrong FloorIndex/FloorRole.") }));
	Root->SetArrayField(TEXT("functions"), Functions);

	TSharedRef<FJsonObject> Recovery = MakeShared<FJsonObject>();
	Recovery->SetArrayField(TEXT("onFailure"), MakeStringArray({
		TEXT("Stop creating new elements."),
		TEXT("Call GetStatus."),
		TEXT("Call GetBuildingSnapshot if an active building exists."),
		TEXT("Prefer object references from the latest successful tool return or snapshot."),
		TEXT("If a requested element type is unsupported, tell the user it is not exposed by the plugin yet.")
	}));
	Root->SetObjectField(TEXT("recoveryProtocol"), Recovery);

	FString Output;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
	FJsonSerializer::Serialize(Root, Writer);
	return Output;
}

FEHBToolsetStatus UEHBBuildingToolset::GetStatus()
{
	FEHBToolsetStatus Status;
	UWorld* World = GetEditorWorld();
	Status.bHasEditorWorld = World != nullptr;
	Status.LevelName = World ? World->GetMapName() : FString();
	Status.bIsBuildingModeActive = IsBuildingModeActive();
	Status.SelectedBuilding = ResolveExistingBuildingForAI(World);
	Status.bHasSelectedBuilding = Status.SelectedBuilding != nullptr;
	Status.ActiveBuilding = Status.SelectedBuilding;
	Status.bHasActiveBuilding = Status.ActiveBuilding != nullptr;
	if (!Status.bHasEditorWorld)
	{
		Status.ReadinessMessage = TEXT("No editor world is available.");
	}
	else if (!Status.bHasSelectedBuilding)
	{
		Status.ReadinessMessage = TEXT("No existing EHB_Building actor is selected or uniquely resolvable. Select one building object in the editor before AI creates or edits building elements.");
	}
	else if (!Status.bIsBuildingModeActive)
	{
		Status.ReadinessMessage = TEXT("Ready with an existing EHB_Building. Building Mode is not active, but AI creation will still use this building object.");
	}
	else
	{
		Status.ReadinessMessage = TEXT("Ready: an existing EHB_Building actor is selected. AI creation will use only this building object.");
	}
	return Status;
}

AEHBBuildingActorBase* UEHBBuildingToolset::GetActiveBuilding()
{
	return ResolveExistingBuildingForAI(GetEditorWorld());
}

AEHBBuildingActorBase* UEHBBuildingToolset::CreateBuilding(const FString& Name, FVector WorldLocation)
{
	UWorld* World = GetEditorWorld();
	if (!World)
	{
		return nullptr;
	}

	if (AEHBBuildingActorBase* ExistingBuilding = ResolveExistingBuildingForAI(World))
	{
		UE_LOG(LogTemp, Display, TEXT("CreateBuilding was requested by AI, but an existing EHB_Building is available. Reusing '%s' instead of spawning a new building."), *ExistingBuilding->GetActorLabel());
		return ExistingBuilding;
	}

	UE_LOG(LogTemp, Warning, TEXT("CreateBuilding was blocked for AI request '%s' at %s. Select or manually create an EHB_Building before AI generation."), *Name, *WorldLocation.ToString());
	return nullptr;
}

FEHBToolsetOperationResult UEHBBuildingToolset::ClearBuilding(AEHBBuildingActorBase* Building, bool bConfirm)
{
	FEHBToolsetOperationResult Result;
	Building = ResolveEditableBuilding(Building);
	if (!Building)
	{
		Result.Message = TEXT("Building is null or is not the currently selected EHB_Building.");
		return Result;
	}
	if (!bConfirm)
	{
		Result.Message = TEXT("ClearBuilding requires bConfirm=true.");
		return Result;
	}

	const FScopedTransaction Transaction(LOCTEXT("ClearBuilding", "Clear EHB Building"));
	Building->Modify();

	TArray<AActor*> AttachedActors;
	Building->GetAttachedActors(AttachedActors);

	int32 DestroyedCount = 0;
	for (AActor* Actor : AttachedActors)
	{
		if (AEHBElementActorBase* Element = Cast<AEHBElementActorBase>(Actor))
		{
			Element->Modify();
			Element->Destroy();
			++DestroyedCount;
		}
	}

	MarkBuildingChanged(Building);
	Result.bSucceeded = true;
	Result.Message = FString::Printf(TEXT("Destroyed %d building elements."), DestroyedCount);
	return Result;
}

AEHB_Pillar* UEHBBuildingToolset::CreatePillar(
	AEHBBuildingActorBase* Building,
	const FString& Name,
	FVector LocalLocation,
	float Height,
	float Width,
	float Depth,
	int32 FloorIndex)
{
	UWorld* World = GetEditorWorld();
	Building = ResolveEditableBuilding(Building);
	if (!World || !Building)
	{
		return nullptr;
	}

	const FScopedTransaction Transaction(LOCTEXT("CreatePillar", "Create EHB Pillar"));
	Building->Modify();

	const FString ActorName = Name.IsEmpty() ? TEXT("EHB_Pillar") : Name;
	const FTransform LocalTransform(FRotator::ZeroRotator, LocalLocation);
	const FTransform WorldTransform = LocalTransform * Building->GetActorTransform();
	AEHB_Pillar* Pillar = SpawnEHBActor<AEHB_Pillar>(*World, AEHB_Pillar::StaticClass(), ActorName, WorldTransform);
	if (!Pillar)
	{
		return nullptr;
	}

	Pillar->Modify();
	Pillar->ElementName = FName(*ActorName);
	Pillar->AttachToBuilding(Building, LocalTransform);
	Pillar->ConfigureAsPolygonPillar(FMath::Max(1.0f, Height), FMath::Max(1.0f, Width), FMath::Max(1.0f, Depth), LocalTransform, true);
	Pillar->SetFloorAssignment(FMath::Max(0, FloorIndex), EEHBBuildingFloorElementRole::FloorBody);
	Pillar->MarkPackageDirty();

	MarkBuildingChanged(Building);
	return Pillar;
}

AEHB_Wall* UEHBBuildingToolset::ConnectPillars(
	AEHBBuildingActorBase* Building,
	AEHB_Pillar* StartPillar,
	AEHB_Pillar* EndPillar,
	float WallHeight,
	float WallThickness)
{
	Building = ResolveEditableBuilding(Building);
	if (!Building || !StartPillar || !EndPillar)
	{
		return nullptr;
	}

	const FScopedTransaction Transaction(LOCTEXT("ConnectPillars", "Connect EHB Pillars"));
	Building->Modify();
	StartPillar->Modify();
	EndPillar->Modify();

	AEHB_Wall* Wall = Building->ConnectPillars(StartPillar, EndPillar, FMath::Max(1.0f, WallHeight), FMath::Max(1.0f, WallThickness));
	if (Wall)
	{
		Wall->SetFloorAssignment(StartPillar->FloorIndex, EEHBBuildingFloorElementRole::FloorBody);
		Wall->MarkPackageDirty();
	}

	MarkBuildingChanged(Building);
	return Wall;
}

FEHBToolsetOperationResult UEHBBuildingToolset::SetWallCurve(AEHB_Wall* Wall, float ControlOffset, float SegmentLength)
{
	FEHBToolsetOperationResult Result;
	if (!Wall || !ResolveEditableBuilding(Wall->OwningBuilding))
	{
		Result.Message = TEXT("Wall is null or does not belong to the currently selected EHB_Building.");
		return Result;
	}

	const FScopedTransaction Transaction(LOCTEXT("SetWallCurve", "Set EHB Wall Curve"));
	Wall->Modify();
	Wall->ApplyCurveSettings(ControlOffset, SegmentLength, true);

	MarkBuildingChanged(Wall->OwningBuilding);
	Result.bSucceeded = true;
	Result.Message = TEXT("Wall curve updated and connected pillars rebuilt.");
	return Result;
}

AEHB_FloorSlab* UEHBBuildingToolset::CreateFloorSlab(
	AEHBBuildingActorBase* Building,
	const FString& Name,
	const TArray<FVector>& LocalTopPolygon,
	float Thickness,
	float TopZ,
	bool bFoundation,
	bool bKeepFoundationBottomOnGround,
	float VisualExpansion,
	int32 FloorIndex)
{
	UWorld* World = GetEditorWorld();
	Building = ResolveEditableBuilding(Building);
	if (!World || !Building || LocalTopPolygon.Num() < 3)
	{
		return nullptr;
	}

	const FScopedTransaction Transaction(LOCTEXT("CreateFloorSlab", "Create EHB Floor Slab"));
	Building->Modify();

	const FString ActorName = Name.IsEmpty() ? TEXT("EHB_FloorSlab") : Name;
	const float SafeThickness = FMath::Max(1.0f, Thickness);
	// Ground-touching manual slabs are foundations, not floor/ceiling slabs.
	const bool bGroundTouchingPlatform = !bFoundation && FMath::IsNearlyZero(TopZ, 1.0f);
	const bool bEffectiveFoundation = bFoundation || bGroundTouchingPlatform;
	const bool bEffectiveKeepFoundationBottomOnGround =
		bEffectiveFoundation && (bKeepFoundationBottomOnGround || FMath::IsNearlyZero(TopZ, 1.0f));
	const float LocalTopZ = bEffectiveFoundation && bEffectiveKeepFoundationBottomOnGround && FMath::IsNearlyZero(TopZ, 1.0f) ? SafeThickness : TopZ;
	const FTransform LocalTransform(FRotator::ZeroRotator, FVector(0.0f, 0.0f, LocalTopZ));
	const FTransform WorldTransform = LocalTransform * Building->GetActorTransform();
	AEHB_FloorSlab* Slab = SpawnEHBActor<AEHB_FloorSlab>(*World, AEHB_FloorSlab::StaticClass(), ActorName, WorldTransform);
	if (!Slab)
	{
		return nullptr;
	}

	Slab->Modify();
	Slab->ElementName = FName(*ActorName);
	Slab->AttachToBuilding(Building, LocalTransform);
	Slab->ConfigureDefaultSlab(Building, LocalTransform, 100.0f, SafeThickness, bEffectiveFoundation);
	Slab->LocalTopPolygon = LocalTopPolygon;
	Slab->Thickness = SafeThickness;
	Slab->bIsFoundation = bEffectiveFoundation;
	Slab->bKeepFoundationBottomOnGround = bEffectiveKeepFoundationBottomOnGround;
	Slab->VisualExpansion = FMath::Max(0.0f, VisualExpansion);
	Slab->SetFloorAssignment(
		bEffectiveFoundation ? 0 : FMath::Max(0, FloorIndex),
		bEffectiveFoundation ? EEHBBuildingFloorElementRole::Foundation : EEHBBuildingFloorElementRole::FloorCeiling);
	Slab->RebuildSlabMesh();
	if (bEffectiveFoundation && bEffectiveKeepFoundationBottomOnGround)
	{
		const FVector InitialSlabWorldLocation = Slab->GetActorLocation();
		if (Slab->SnapFoundationBottomToGround())
		{
			const FVector BuildingGroundingDelta = Slab->GetActorLocation() - InitialSlabWorldLocation;
			Building->Modify();
			Building->AddActorWorldOffset(BuildingGroundingDelta, false, nullptr, ETeleportType::TeleportPhysics);
			Slab->AttachToBuilding(Building, LocalTransform);
			Slab->RebuildSlabMesh();
		}
	}
	Slab->MarkPackageDirty();

	MarkBuildingChanged(Building);
	return Slab;
}

AEHBGableRoof* UEHBBuildingToolset::CreateGableRoof(
	AEHBBuildingActorBase* Building,
	const FString& Name,
	FVector LocalCenter,
	float YawDegrees,
	float Length,
	float Width,
	float PitchDegrees,
	float Thickness,
	float EaveOffset,
	float RidgeOffsetRatio,
	EEHBRoofAxisMode AxisMode,
	bool bGenerateRidge,
	bool bGenerateEaves,
	bool bGenerateGableRakes,
	bool bGenerateGableEndWalls,
	float RidgeWidth,
	float RidgeHeight,
	float EaveWidth,
	float EaveHeight,
	float GableRakeWidth,
	float GableRakeHeight,
	bool bCutCollidingElements,
	bool bRemoveDisconnectedCutPieces,
	bool bKeepCutAwayDisconnectedPieces,
	bool bUseExactSourceMeshCutters,
	bool bUseControlledEnvelopeCutters,
	float EnvelopeConcavityBridgeDistance,
	float EnvelopeProjectionPadding,
	float EnvelopeZPadding,
	float EnvelopeMinExtrudeHeight,
	float EnvelopeMinProjectedArea,
	float EnvelopeThinProjectionFallbackWidth,
	float EnvelopePathCleanTolerance,
	int32 FloorIndex,
	bool bUseWallFootprintCutters,
	int32 MinWallFootprintGroupWallCount,
	float WallFootprintGroupEndpointTolerance,
	float WallFootprintPadding,
	float WallFootprintMaxDimension,
	float WallFootprintMinArea,
	bool bShowCutDebugVisualization,
	bool bDebugDrawRawRoofBounds,
	bool bDebugDrawSourceBounds,
	bool bDebugDrawCutterBounds,
	bool bDebugDrawResultBounds,
	float CutDebugDrawDuration,
	float CutDebugDrawThickness,
	float GableEndWallBoundaryInset)
{
	UWorld* World = GetEditorWorld();
	Building = ResolveEditableBuilding(Building);
	if (!World || !Building)
	{
		return nullptr;
	}

	const FScopedTransaction Transaction(LOCTEXT("CreateGableRoof", "Create EHB Gable Roof"));
	Building->Modify();

	const FString ActorName = Name.IsEmpty() ? TEXT("EHB_GableRoof") : Name;
	const FTransform LocalTransform(FRotator(0.0f, YawDegrees, 0.0f), LocalCenter);
	const FTransform WorldTransform = LocalTransform * Building->GetActorTransform();
	AEHBGableRoof* Roof = SpawnEHBActor<AEHBGableRoof>(*World, ResolveGableRoofActorClass(), ActorName, WorldTransform);
	if (!Roof)
	{
		return nullptr;
	}

	Roof->Modify();
	Roof->ElementName = FName(*ActorName);
	Roof->AttachToBuilding(Building, LocalTransform);
	Roof->Length = FMath::Max(1.0f, Length);
	Roof->Width = FMath::Max(1.0f, Width);
	Roof->PitchDegrees = FMath::Clamp(PitchDegrees, 1.0f, 89.0f);
	Roof->Thickness = FMath::Max(0.1f, Thickness);
	Roof->EaveOffset = FMath::Max(0.0f, EaveOffset);
	Roof->RidgeOffsetRatio = FMath::Clamp(RidgeOffsetRatio, -0.45f, 0.45f);
	Roof->AxisMode = AxisMode;
	Roof->bGenerateRidge = bGenerateRidge;
	Roof->bGenerateEaves = bGenerateEaves;
	Roof->bGenerateGableRakes = bGenerateGableRakes;
	Roof->bGenerateGableEndWalls = bGenerateGableEndWalls;
	Roof->RidgeWidth = FMath::Max(0.1f, RidgeWidth);
	Roof->RidgeHeight = FMath::Max(0.1f, RidgeHeight);
	Roof->EaveWidth = FMath::Max(0.1f, EaveWidth);
	Roof->EaveHeight = FMath::Max(0.1f, EaveHeight);
	Roof->GableRakeWidth = FMath::Max(0.1f, GableRakeWidth);
	Roof->GableRakeHeight = FMath::Max(0.1f, GableRakeHeight);
	Roof->GableEndWallBoundaryInset = FMath::Max(0.0f, GableEndWallBoundaryInset);
	Roof->bCutCollidingElements = bCutCollidingElements;
	Roof->bRemoveDisconnectedCutPieces = bRemoveDisconnectedCutPieces;
	Roof->bKeepCutAwayDisconnectedPieces = bKeepCutAwayDisconnectedPieces;
	Roof->bUseExactSourceMeshCutters = bUseExactSourceMeshCutters;
	Roof->bUseControlledEnvelopeCutters = bUseControlledEnvelopeCutters;
	Roof->EnvelopeCutOptions.ConcavityBridgeDistance = FMath::Max(0.0f, EnvelopeConcavityBridgeDistance);
	Roof->EnvelopeCutOptions.ProjectionPadding = FMath::Max(0.0f, EnvelopeProjectionPadding);
	Roof->EnvelopeCutOptions.ZPadding = FMath::Max(0.0f, EnvelopeZPadding);
	Roof->EnvelopeCutOptions.MinExtrudeHeight = FMath::Max(1.0f, EnvelopeMinExtrudeHeight);
	Roof->EnvelopeCutOptions.MinProjectedArea = FMath::Max(0.0f, EnvelopeMinProjectedArea);
	Roof->EnvelopeCutOptions.ThinProjectionFallbackWidth = FMath::Max(0.0f, EnvelopeThinProjectionFallbackWidth);
	Roof->EnvelopeCutOptions.PathCleanTolerance = FMath::Max(0.0f, EnvelopePathCleanTolerance);
	Roof->bUseWallFootprintCutters = bUseWallFootprintCutters;
	Roof->MinWallFootprintGroupWallCount = FMath::Max(1, MinWallFootprintGroupWallCount);
	Roof->WallFootprintGroupEndpointTolerance = FMath::Max(0.0f, WallFootprintGroupEndpointTolerance);
	Roof->WallFootprintPadding = FMath::Clamp(WallFootprintPadding, -100.0f, 100.0f);
	Roof->WallFootprintMaxDimension = FMath::Max(0.0f, WallFootprintMaxDimension);
	Roof->WallFootprintMinArea = FMath::Max(0.0f, WallFootprintMinArea);
	Roof->bShowCutDebugVisualization = bShowCutDebugVisualization;
	Roof->bDebugDrawRawRoofBounds = bDebugDrawRawRoofBounds;
	Roof->bDebugDrawSourceBounds = bDebugDrawSourceBounds;
	Roof->bDebugDrawCutterBounds = bDebugDrawCutterBounds;
	Roof->bDebugDrawResultBounds = bDebugDrawResultBounds;
	Roof->CutDebugDrawDuration = FMath::Max(0.1f, CutDebugDrawDuration);
	Roof->CutDebugDrawThickness = FMath::Max(0.1f, CutDebugDrawThickness);
	Roof->SetFloorAssignment(FMath::Max(1, FloorIndex), EEHBBuildingFloorElementRole::Roof);

	Roof->RebuildRoofMesh();
	if (Roof->bCutCollidingElements)
	{
		Roof->RefreshAutoCollisionCutOperations();
		Roof->RebuildRoofMesh();
	}
	Roof->MarkPackageDirty();

	MarkBuildingChanged(Building);
	return Roof;
}

AEHBHipRoof* UEHBBuildingToolset::CreateHipRoof(
	AEHBBuildingActorBase* Building,
	const FString& Name,
	FVector LocalCenter,
	float YawDegrees,
	float Length,
	float Width,
	float PitchDegrees,
	float Thickness,
	float EaveOffset,
	float RidgeOffsetRatio,
	EEHBRoofAxisMode AxisMode,
	bool bGenerateRidge,
	bool bGenerateEaves,
	bool bGenerateHipRidges,
	float RidgeWidth,
	float RidgeHeight,
	float EaveWidth,
	float EaveHeight,
	float HipRidgeWidth,
	float HipRidgeHeight,
	bool bCutCollidingElements,
	bool bRemoveDisconnectedCutPieces,
	bool bKeepCutAwayDisconnectedPieces,
	bool bUseExactSourceMeshCutters,
	bool bUseControlledEnvelopeCutters,
	float EnvelopeConcavityBridgeDistance,
	float EnvelopeProjectionPadding,
	float EnvelopeZPadding,
	float EnvelopeMinExtrudeHeight,
	float EnvelopeMinProjectedArea,
	float EnvelopeThinProjectionFallbackWidth,
	float EnvelopePathCleanTolerance,
	int32 FloorIndex,
	bool bUseWallFootprintCutters,
	int32 MinWallFootprintGroupWallCount,
	float WallFootprintGroupEndpointTolerance,
	float WallFootprintPadding,
	float WallFootprintMaxDimension,
	float WallFootprintMinArea,
	bool bShowCutDebugVisualization,
	bool bDebugDrawRawRoofBounds,
	bool bDebugDrawSourceBounds,
	bool bDebugDrawCutterBounds,
	bool bDebugDrawResultBounds,
	float CutDebugDrawDuration,
	float CutDebugDrawThickness)
{
	UWorld* World = GetEditorWorld();
	Building = ResolveEditableBuilding(Building);
	if (!World || !Building)
	{
		return nullptr;
	}

	const FScopedTransaction Transaction(LOCTEXT("CreateHipRoof", "Create EHB Hip Roof"));
	Building->Modify();

	const FString ActorName = Name.IsEmpty() ? TEXT("EHB_HipRoof") : Name;
	const FTransform LocalTransform(FRotator(0.0f, YawDegrees, 0.0f), LocalCenter);
	const FTransform WorldTransform = LocalTransform * Building->GetActorTransform();
	AEHBHipRoof* Roof = SpawnEHBActor<AEHBHipRoof>(*World, ResolveHipRoofActorClass(), ActorName, WorldTransform);
	if (!Roof)
	{
		return nullptr;
	}

	Roof->Modify();
	Roof->ElementName = FName(*ActorName);
	Roof->AttachToBuilding(Building, LocalTransform);
	Roof->Length = FMath::Max(1.0f, Length);
	Roof->Width = FMath::Max(1.0f, Width);
	Roof->PitchDegrees = FMath::Clamp(PitchDegrees, 1.0f, 89.0f);
	Roof->Thickness = FMath::Max(0.1f, Thickness);
	Roof->EaveOffset = FMath::Max(0.0f, EaveOffset);
	Roof->RidgeOffsetRatio = FMath::Clamp(RidgeOffsetRatio, -0.45f, 0.45f);
	Roof->AxisMode = AxisMode;
	Roof->bGenerateRidge = bGenerateRidge;
	Roof->bGenerateEaves = bGenerateEaves;
	Roof->bGenerateGableRakes = bGenerateHipRidges;
	Roof->bGenerateGableEndWalls = false;
	Roof->RidgeWidth = FMath::Max(0.1f, RidgeWidth);
	Roof->RidgeHeight = FMath::Max(0.1f, RidgeHeight);
	Roof->EaveWidth = FMath::Max(0.1f, EaveWidth);
	Roof->EaveHeight = FMath::Max(0.1f, EaveHeight);
	Roof->GableRakeWidth = FMath::Max(0.1f, HipRidgeWidth);
	Roof->GableRakeHeight = FMath::Max(0.1f, HipRidgeHeight);
	Roof->GableEndWallBoundaryInset = 0.0f;
	Roof->bCutCollidingElements = bCutCollidingElements;
	Roof->bRemoveDisconnectedCutPieces = bRemoveDisconnectedCutPieces;
	Roof->bKeepCutAwayDisconnectedPieces = bKeepCutAwayDisconnectedPieces;
	Roof->bUseExactSourceMeshCutters = bUseExactSourceMeshCutters;
	Roof->bUseControlledEnvelopeCutters = bUseControlledEnvelopeCutters;
	Roof->EnvelopeCutOptions.ConcavityBridgeDistance = FMath::Max(0.0f, EnvelopeConcavityBridgeDistance);
	Roof->EnvelopeCutOptions.ProjectionPadding = FMath::Max(0.0f, EnvelopeProjectionPadding);
	Roof->EnvelopeCutOptions.ZPadding = FMath::Max(0.0f, EnvelopeZPadding);
	Roof->EnvelopeCutOptions.MinExtrudeHeight = FMath::Max(1.0f, EnvelopeMinExtrudeHeight);
	Roof->EnvelopeCutOptions.MinProjectedArea = FMath::Max(0.0f, EnvelopeMinProjectedArea);
	Roof->EnvelopeCutOptions.ThinProjectionFallbackWidth = FMath::Max(0.0f, EnvelopeThinProjectionFallbackWidth);
	Roof->EnvelopeCutOptions.PathCleanTolerance = FMath::Max(0.0f, EnvelopePathCleanTolerance);
	Roof->bUseWallFootprintCutters = bUseWallFootprintCutters;
	Roof->MinWallFootprintGroupWallCount = FMath::Max(1, MinWallFootprintGroupWallCount);
	Roof->WallFootprintGroupEndpointTolerance = FMath::Max(0.0f, WallFootprintGroupEndpointTolerance);
	Roof->WallFootprintPadding = FMath::Clamp(WallFootprintPadding, -100.0f, 100.0f);
	Roof->WallFootprintMaxDimension = FMath::Max(0.0f, WallFootprintMaxDimension);
	Roof->WallFootprintMinArea = FMath::Max(0.0f, WallFootprintMinArea);
	Roof->bShowCutDebugVisualization = bShowCutDebugVisualization;
	Roof->bDebugDrawRawRoofBounds = bDebugDrawRawRoofBounds;
	Roof->bDebugDrawSourceBounds = bDebugDrawSourceBounds;
	Roof->bDebugDrawCutterBounds = bDebugDrawCutterBounds;
	Roof->bDebugDrawResultBounds = bDebugDrawResultBounds;
	Roof->CutDebugDrawDuration = FMath::Max(0.1f, CutDebugDrawDuration);
	Roof->CutDebugDrawThickness = FMath::Max(0.1f, CutDebugDrawThickness);
	Roof->SetFloorAssignment(FMath::Max(1, FloorIndex), EEHBBuildingFloorElementRole::Roof);

	Roof->RebuildRoofMesh();
	if (Roof->bCutCollidingElements)
	{
		Roof->RefreshAutoCollisionCutOperations();
		Roof->RebuildRoofMesh();
	}
	Roof->MarkPackageDirty();

	MarkBuildingChanged(Building);
	return Roof;
}

FEHBToolsetOperationResult UEHBBuildingToolset::SetFloorSlabTopPolygon(
	AEHB_FloorSlab* FloorSlab,
	const TArray<FVector>& LocalTopPolygon,
	bool bPreserveExistingHoles)
{
	return CommitFloorSlabTopPolygon(
		FloorSlab,
		LocalTopPolygon,
		bPreserveExistingHoles,
		LOCTEXT("SetFloorSlabTopPolygon", "Set EHB Floor Slab Top Polygon"));
}

FEHBToolsetOperationResult UEHBBuildingToolset::InsertFloorSlabCorner(
	AEHB_FloorSlab* FloorSlab,
	int32 AfterPointIndex,
	FVector NewLocalLocation)
{
	FEHBToolsetOperationResult Result;
	if (!FloorSlab)
	{
		Result.Message = TEXT("FloorSlab is null.");
		return Result;
	}
	if (!FloorSlab->LocalTopPolygon.IsValidIndex(AfterPointIndex))
	{
		Result.Message = TEXT("AfterPointIndex is out of range.");
		return Result;
	}

	TArray<FVector> NewPolygon = FloorSlab->LocalTopPolygon;
	NewLocalLocation.Z = FloorSlab->GetTopZ();
	NewPolygon.Insert(NewLocalLocation, AfterPointIndex + 1);
	return CommitFloorSlabTopPolygon(
		FloorSlab,
		MoveTemp(NewPolygon),
		true,
		LOCTEXT("InsertFloorSlabCorner", "Insert EHB Floor Slab Corner"));
}

FEHBToolsetOperationResult UEHBBuildingToolset::MoveFloorSlabCorner(
	AEHB_FloorSlab* FloorSlab,
	int32 PointIndex,
	FVector NewLocalLocation)
{
	FEHBToolsetOperationResult Result;
	if (!FloorSlab)
	{
		Result.Message = TEXT("FloorSlab is null.");
		return Result;
	}
	if (!FloorSlab->LocalTopPolygon.IsValidIndex(PointIndex))
	{
		Result.Message = TEXT("PointIndex is out of range.");
		return Result;
	}

	TArray<FVector> NewPolygon = FloorSlab->LocalTopPolygon;
	NewLocalLocation.Z = FloorSlab->GetTopZ();
	NewPolygon[PointIndex] = NewLocalLocation;
	return CommitFloorSlabTopPolygon(
		FloorSlab,
		MoveTemp(NewPolygon),
		true,
		LOCTEXT("MoveFloorSlabCorner", "Move EHB Floor Slab Corner"));
}

FEHBToolsetOperationResult UEHBBuildingToolset::RemoveFloorSlabCorner(
	AEHB_FloorSlab* FloorSlab,
	int32 PointIndex)
{
	FEHBToolsetOperationResult Result;
	if (!FloorSlab)
	{
		Result.Message = TEXT("FloorSlab is null.");
		return Result;
	}
	if (!FloorSlab->LocalTopPolygon.IsValidIndex(PointIndex))
	{
		Result.Message = TEXT("PointIndex is out of range.");
		return Result;
	}
	if (FloorSlab->LocalTopPolygon.Num() <= 3)
	{
		Result.Message = TEXT("Cannot remove a corner from a 3-point polygon.");
		return Result;
	}

	TArray<FVector> NewPolygon = FloorSlab->LocalTopPolygon;
	NewPolygon.RemoveAt(PointIndex);
	return CommitFloorSlabTopPolygon(
		FloorSlab,
		MoveTemp(NewPolygon),
		true,
		LOCTEXT("RemoveFloorSlabCorner", "Remove EHB Floor Slab Corner"));
}

AEHB_FloorSlab* UEHBBuildingToolset::CreateRoomFilledFloorSlab(
	AEHBBuildingActorBase* Building,
	const FString& Name,
	AEHB_Wall* AnchorWall,
	EEHBFloorSlabWallSide AnchorSide,
	float TopZ,
	float Thickness,
	int32 FloorIndex)
{
	UWorld* World = GetEditorWorld();
	Building = ResolveEditableBuilding(Building);
	if (!World || !Building || !AnchorWall || AnchorWall->OwningBuilding != Building)
	{
		return nullptr;
	}

	const FScopedTransaction Transaction(LOCTEXT("CreateRoomFilledFloorSlab", "Create Room-Filled EHB Floor Slab"));
	Building->Modify();
	AnchorWall->Modify();

	const FString ActorName = Name.IsEmpty() ? TEXT("EHB_RoomFilledSlab") : Name;
	const float SafeThickness = FMath::Max(1.0f, Thickness);
	FVector LocalCenter = (AnchorWall->LocalStart + AnchorWall->LocalEnd) * 0.5f;
	LocalCenter.Z = TopZ;
	const FTransform LocalTransform(FRotator::ZeroRotator, LocalCenter);
	const FTransform WorldTransform = LocalTransform * Building->GetActorTransform();
	AEHB_FloorSlab* Slab = SpawnEHBActor<AEHB_FloorSlab>(*World, AEHB_FloorSlab::StaticClass(), ActorName, WorldTransform);
	if (!Slab)
	{
		return nullptr;
	}

	Slab->Modify();
	Slab->ElementName = FName(*ActorName);
	Slab->AttachToBuilding(Building, LocalTransform);
	Slab->ConfigureDefaultSlab(Building, LocalTransform, 100.0f, SafeThickness, false);
	Slab->Thickness = SafeThickness;
	Slab->bIsFoundation = false;
	Slab->bKeepFoundationBottomOnGround = false;
	Slab->bHasRoomFillAnchor = true;
	Slab->RoomFillAnchorWallGuid = AnchorWall->ElementGuid;
	Slab->RoomFillAnchorWallSide = AnchorSide == EEHBFloorSlabWallSide::None ? EEHBFloorSlabWallSide::Left : AnchorSide;
	Slab->SetFloorAssignment(FMath::Max(1, FloorIndex), EEHBBuildingFloorElementRole::FloorCeiling);

	if (!FEasyHouseEditorMode::FillFloorSlabRoomForToolset(Slab))
	{
		Slab->Destroy();
		MarkBuildingChanged(Building);
		return nullptr;
	}

	Slab->MarkPackageDirty();
	MarkBuildingChanged(Building);
	return Slab;
}

AEHB_FloorSlab* UEHBBuildingToolset::CreateRoomFilledFloorSlabAtPoint(
	AEHBBuildingActorBase* Building,
	const FString& Name,
	FVector RoomInteriorPoint,
	float TopZ,
	float Thickness,
	int32 FloorIndex)
{
	UWorld* World = GetEditorWorld();
	Building = ResolveEditableBuilding(Building);
	if (!World || !Building)
	{
		return nullptr;
	}

	const FScopedTransaction Transaction(LOCTEXT("CreateRoomFilledFloorSlabAtPoint", "Create Point Room-Filled EHB Floor Slab"));
	Building->Modify();

	const FString ActorName = Name.IsEmpty() ? TEXT("EHB_RoomFilledSlab") : Name;
	const float SafeThickness = FMath::Max(1.0f, Thickness);
	FVector LocalCenter = RoomInteriorPoint;
	LocalCenter.Z = TopZ;
	const FTransform LocalTransform(FRotator::ZeroRotator, LocalCenter);
	const FTransform WorldTransform = LocalTransform * Building->GetActorTransform();
	AEHB_FloorSlab* Slab = SpawnEHBActor<AEHB_FloorSlab>(*World, AEHB_FloorSlab::StaticClass(), ActorName, WorldTransform);
	if (!Slab)
	{
		return nullptr;
	}

	Slab->Modify();
	Slab->ElementName = FName(*ActorName);
	Slab->AttachToBuilding(Building, LocalTransform);
	Slab->ConfigureDefaultSlab(Building, LocalTransform, 100.0f, SafeThickness, false);
	Slab->Thickness = SafeThickness;
	Slab->bIsFoundation = false;
	Slab->bKeepFoundationBottomOnGround = false;
	Slab->bHasRoomFillAnchor = false;
	Slab->RoomFillAnchorWallGuid = FGuid();
	Slab->RoomFillAnchorWallSide = EEHBFloorSlabWallSide::None;
	Slab->SetFloorAssignment(FMath::Max(1, FloorIndex), EEHBBuildingFloorElementRole::FloorCeiling);

	if (!FEasyHouseEditorMode::FillFloorSlabRoomForToolset(Slab))
	{
		Slab->Destroy();
		MarkBuildingChanged(Building);
		return nullptr;
	}

	Slab->MarkPackageDirty();
	MarkBuildingChanged(Building);
	return Slab;
}

FEHBToolsetOperationResult UEHBBuildingToolset::AddFloorSlabRectangularHole(
	AEHB_FloorSlab* FloorSlab,
	FVector LocalCenter,
	float Width,
	float Depth,
	float YawDegrees)
{
	const float SafeWidth = FMath::Max(1.0f, Width);
	const float SafeDepth = FMath::Max(1.0f, Depth);
	const float HalfWidth = SafeWidth * 0.5f;
	const float HalfDepth = SafeDepth * 0.5f;
	const float YawRadians = FMath::DegreesToRadians(YawDegrees);
	const float CosYaw = FMath::Cos(YawRadians);
	const float SinYaw = FMath::Sin(YawRadians);
	auto RotateOffset = [CosYaw, SinYaw](float X, float Y)
	{
		return FVector(
			X * CosYaw - Y * SinYaw,
			X * SinYaw + Y * CosYaw,
			0.0f);
	};

	TArray<FVector> HolePolygon;
	HolePolygon.Reserve(4);
	HolePolygon.Add(LocalCenter + RotateOffset(-HalfWidth, -HalfDepth));
	HolePolygon.Add(LocalCenter + RotateOffset(HalfWidth, -HalfDepth));
	HolePolygon.Add(LocalCenter + RotateOffset(HalfWidth, HalfDepth));
	HolePolygon.Add(LocalCenter + RotateOffset(-HalfWidth, HalfDepth));
	return CommitFloorSlabHole(
		FloorSlab,
		MoveTemp(HolePolygon),
		LOCTEXT("AddFloorSlabRectangularHole", "Add EHB Floor Slab Rectangular Hole"));
}

FEHBToolsetOperationResult UEHBBuildingToolset::AddFloorSlabCircularHole(
	AEHB_FloorSlab* FloorSlab,
	FVector LocalCenter,
	float Diameter,
	int32 SideCount)
{
	const float Radius = FMath::Max(1.0f, Diameter) * 0.5f;
	const int32 SafeSideCount = FMath::Clamp(SideCount, 8, 96);
	TArray<FVector> HolePolygon;
	HolePolygon.Reserve(SafeSideCount);
	for (int32 Index = 0; Index < SafeSideCount; ++Index)
	{
		const float Angle = 2.0f * PI * static_cast<float>(Index) / static_cast<float>(SafeSideCount);
		HolePolygon.Add(LocalCenter + FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0.0f));
	}

	return CommitFloorSlabHole(
		FloorSlab,
		MoveTemp(HolePolygon),
		LOCTEXT("AddFloorSlabCircularHole", "Add EHB Floor Slab Circular Hole"));
}

AEHB_Stair* UEHBBuildingToolset::CreateStair(
	AEHBBuildingActorBase* Building,
	const FString& Name,
	FVector LocalTopLocation,
	float YawDegrees,
	float StairHeight,
	float StairWidth,
	float TreadDepth,
	bool bGenerateTreads,
	bool bFillRisers,
	bool bGenerateSides,
	bool bFillBottomPart,
	bool bGenerateSideGuards,
	int32 FloorIndex,
	float MinStepHeight,
	float MaxStepHeight,
	float NosingLength)
{
	UWorld* World = GetEditorWorld();
	Building = ResolveEditableBuilding(Building);
	if (!World || !Building)
	{
		return nullptr;
	}

	const FScopedTransaction Transaction(LOCTEXT("CreateStair", "Create EHB Stair"));
	Building->Modify();

	const FString ActorName = Name.IsEmpty() ? TEXT("EHB_Stair") : Name;
	const float SafeStairHeight = FMath::Max(1.0f, StairHeight);
	const float SafeStairWidth = FMath::Max(1.0f, StairWidth);
	const float SafeTreadDepth = FMath::Max(1.0f, TreadDepth);
	float SafeMinStepHeight = FMath::Max(1.0f, MinStepHeight);
	float SafeMaxStepHeight = FMath::Max(1.0f, MaxStepHeight);
	if (SafeMinStepHeight > SafeMaxStepHeight)
	{
		Swap(SafeMinStepHeight, SafeMaxStepHeight);
	}
	const float SafeNosingLength = FMath::Clamp(NosingLength, 0.0f, FMath::Max(0.0f, SafeTreadDepth - 1.0f));
	const FVector InternalLocalOrigin = LocalTopLocation - FVector(0.0f, 0.0f, SafeStairHeight);
	const FTransform LocalTransform(FRotator(0.0f, YawDegrees, 0.0f), InternalLocalOrigin);
	const FTransform WorldTransform = LocalTransform * Building->GetActorTransform();
	AEHB_Stair* Stair = SpawnEHBActor<AEHB_Stair>(*World, AEHB_Stair::StaticClass(), ActorName, WorldTransform);
	if (!Stair)
	{
		return nullptr;
	}

	Stair->Modify();
	Stair->ElementName = FName(*ActorName);
	Stair->ConfigureDefaultStair(
		Building,
		LocalTransform,
		SafeStairHeight,
		SafeStairWidth,
		SafeTreadDepth);
	Stair->StairData.bGenerateTreads = bGenerateTreads;
	Stair->StairData.bFillRisers = bFillRisers;
	Stair->StairData.bGenerateSides = bGenerateSides;
	Stair->StairData.bFillBottomPart = bFillBottomPart;
	Stair->StairData.bGenerateSideGuards = bGenerateSideGuards;
	Stair->StairData.MinStepHeight = SafeMinStepHeight;
	Stair->StairData.MaxStepHeight = SafeMaxStepHeight;
	Stair->StairData.NosingLength = SafeNosingLength;
	Stair->StairData.bUseIntermediateControls = false;
	Stair->ResetIntermediateControlOffsets();
	Stair->SetFloorAssignment(FMath::Max(1, FloorIndex), EEHBBuildingFloorElementRole::VerticalConnector);
	Stair->RebuildStairMesh();
	Stair->MarkPackageDirty();

	MarkBuildingChanged(Building);
	return Stair;
}

AEHB_Stair* UEHBBuildingToolset::CreateStairFromBottom(
	AEHBBuildingActorBase* Building,
	const FString& Name,
	FVector LocalBottomLocation,
	float UpDirectionYawDegrees,
	float StairHeight,
	float StairWidth,
	float TreadDepth,
	bool bGenerateTreads,
	bool bFillRisers,
	bool bGenerateSides,
	bool bFillBottomPart,
	bool bGenerateSideGuards,
	int32 FloorIndex,
	float MinStepHeight,
	float MaxStepHeight,
	float NosingLength)
{
	int32 StepCount = 0;
	float StepHeight = 0.0f;
	float StairLength = 0.0f;
	CalculateToolsetStairLayout(
		StairHeight,
		TreadDepth,
		MinStepHeight,
		MaxStepHeight,
		NosingLength,
		StepCount,
		StepHeight,
		StairLength);

	const float UpYawRadians = FMath::DegreesToRadians(UpDirectionYawDegrees);
	const FVector UpDirection(FMath::Cos(UpYawRadians), FMath::Sin(UpYawRadians), 0.0f);
	const FVector LocalTopLocation =
		LocalBottomLocation
		+ UpDirection * StairLength
		+ FVector(0.0f, 0.0f, FMath::Max(1.0f, StairHeight));
	const float DownDirectionYawDegrees = FRotator::NormalizeAxis(UpDirectionYawDegrees + 180.0f);

	return CreateStairBetweenLandings(
		Building,
		Name,
		LocalBottomLocation,
		UpDirectionYawDegrees,
		LocalTopLocation,
		DownDirectionYawDegrees,
		StairWidth,
		TreadDepth,
		bGenerateTreads,
		bFillRisers,
		bGenerateSides,
		bFillBottomPart,
		bGenerateSideGuards,
		FloorIndex,
		MinStepHeight,
		MaxStepHeight,
		NosingLength);
}

AEHB_Stair* UEHBBuildingToolset::CreateStairFromSide(
	AEHBBuildingActorBase* Building,
	const FString& Name,
	AEHB_FloorSlab* SourceFloorSlab,
	AEHB_Wall* SourceWall,
	int32 SideSegmentIndex,
	float DistanceAlongSide,
	bool bUseLeftSide,
	float SideOffset,
	int32 SlabHoleIndex,
	bool bSideIsUpperLanding,
	float StairHeight,
	float StairWidth,
	float TreadDepth,
	bool bGenerateTreads,
	bool bFillRisers,
	bool bGenerateSides,
	bool bFillBottomPart,
	bool bGenerateSideGuards,
	int32 FloorIndex,
	float MinStepHeight,
	float MaxStepHeight,
	float NosingLength)
{
	Building = ResolveEditableBuilding(Building);
	const bool bHasFloorSlab = SourceFloorSlab != nullptr;
	const bool bHasWall = SourceWall != nullptr;
	if (!Building || bHasFloorSlab == bHasWall)
	{
		return nullptr;
	}

	TArray<FVector> SidePoints;
	bool bClosedLoop = true;
	bool bUseSampleAsUpperLanding = bSideIsUpperLanding;
	if (SourceFloorSlab)
	{
		if (SourceFloorSlab->OwningBuilding != Building)
		{
			return nullptr;
		}

		if (SlabHoleIndex >= 0)
		{
			if (!SourceFloorSlab->LocalHoles.IsValidIndex(SlabHoleIndex)
				|| SourceFloorSlab->LocalHoles[SlabHoleIndex].LocalPolygon.Num() < 3)
			{
				return nullptr;
			}
			SidePoints = SourceFloorSlab->LocalHoles[SlabHoleIndex].LocalPolygon;
			bUseSampleAsUpperLanding = true;
		}
		else
  {
   TArray<FEHBPlanarSurfaceRegion> Regions;if(!SourceFloorSlab->BuildEffectiveDisplayRegions(Regions,true,true))return nullptr;
   int32 Segments=0;for(const auto& R:Regions)Segments+=R.BoundaryLoop.Num();if(Segments==0)return nullptr;
   SideSegmentIndex=(SideSegmentIndex%Segments+Segments)%Segments;
   for(const auto& R:Regions){if(SideSegmentIndex<R.BoundaryLoop.Num()){SidePoints=R.BoundaryLoop;break;}SideSegmentIndex-=R.BoundaryLoop.Num();}
  }

		const FTransform SlabLocalTransform = SourceFloorSlab->GetElementLocalTransform();
		for (FVector& Point : SidePoints)
		{
			Point.Z = SourceFloorSlab->GetTopZ();
			Point = SlabLocalTransform.TransformPosition(Point);
		}
	}
	else
	{
		if (SourceWall->OwningBuilding != Building
			|| !SourceWall->BuildSideTopPolylineInBuildingSpace(bUseLeftSide, SidePoints, FMath::Max(10.0f, SourceWall->CurveSegmentLength)))
		{
			return nullptr;
		}

		const float WallHeight = FMath::Max(1.0f, SourceWall->Height);
		for (FVector& Point : SidePoints)
		{
			Point.Z -= WallHeight;
		}
		bClosedLoop = false;
	}

	FVector LocalSideAnchor = FVector::ZeroVector;
	float SideNormalYawDegrees = 0.0f;
	if (!SampleToolsetSideSegment(
		SidePoints,
		bClosedLoop,
		SideSegmentIndex,
		DistanceAlongSide,
		bUseLeftSide,
		SideOffset,
		LocalSideAnchor,
		SideNormalYawDegrees))
	{
		return nullptr;
	}

	if (bUseSampleAsUpperLanding)
	{
		return CreateStair(
			Building,
			Name,
			LocalSideAnchor,
			SideNormalYawDegrees,
			StairHeight,
			StairWidth,
			TreadDepth,
			bGenerateTreads,
			bFillRisers,
			bGenerateSides,
			bFillBottomPart,
			bGenerateSideGuards,
			FloorIndex,
			MinStepHeight,
			MaxStepHeight,
			NosingLength);
	}

	return CreateStairFromBottom(
		Building,
		Name,
		LocalSideAnchor,
		SideNormalYawDegrees,
		StairHeight,
		StairWidth,
		TreadDepth,
		bGenerateTreads,
		bFillRisers,
		bGenerateSides,
		bFillBottomPart,
		bGenerateSideGuards,
		FloorIndex,
		MinStepHeight,
		MaxStepHeight,
		NosingLength);
}

AEHB_Stair* UEHBBuildingToolset::CreateStairBetweenLandings(
	AEHBBuildingActorBase* Building,
	const FString& Name,
	FVector LocalBottomLocation,
	float BottomUpDirectionYawDegrees,
	FVector LocalTopLocation,
	float TopDownDirectionYawDegrees,
	float StairWidth,
	float TreadDepth,
	bool bGenerateTreads,
	bool bFillRisers,
	bool bGenerateSides,
	bool bFillBottomPart,
	bool bGenerateSideGuards,
	int32 FloorIndex,
	float MinStepHeight,
	float MaxStepHeight,
	float NosingLength)
{
	Building = ResolveEditableBuilding(Building);
	if (!Building)
	{
		return nullptr;
	}

	const float StairHeight = LocalTopLocation.Z - LocalBottomLocation.Z;
	if (StairHeight <= 1.0f)
	{
		return nullptr;
	}

	AEHB_Stair* Stair = CreateStair(
		Building,
		Name,
		LocalTopLocation,
		TopDownDirectionYawDegrees,
		StairHeight,
		StairWidth,
		TreadDepth,
		bGenerateTreads,
		bFillRisers,
		bGenerateSides,
		bFillBottomPart,
		bGenerateSideGuards,
		FloorIndex,
		MinStepHeight,
		MaxStepHeight,
		NosingLength);
	if (!Stair)
	{
		return nullptr;
	}

	Stair->Modify();
	const FVector BottomWorldLocation = Building->GetActorTransform().TransformPosition(LocalBottomLocation);
	const FVector BottomStairLocalLocation = Stair->GetActorTransform().InverseTransformPosition(BottomWorldLocation);
	FVector2D BottomStepLocation(BottomStairLocalLocation.X, BottomStairLocalLocation.Y);
	if (BottomStepLocation.SizeSquared() < 100.0f)
	{
		BottomStepLocation = BottomStepLocation.IsNearlyZero()
			? FVector2D(10.0f, 0.0f)
			: BottomStepLocation.GetSafeNormal() * 10.0f;
	}

	const float BottomBaseYawLocal = FMath::RadiansToDegrees(FMath::Atan2(BottomStepLocation.Y, BottomStepLocation.X));
	const float DesiredBottomDownYawLocal = FRotator::NormalizeAxis(BottomUpDirectionYawDegrees + 180.0f - TopDownDirectionYawDegrees);
	Stair->StairData.BottomStepLocation = BottomStepLocation;
	Stair->StairData.BottomStepYawOffset = FRotator::NormalizeAxis(DesiredBottomDownYawLocal - BottomBaseYawLocal);
	Stair->StairData.bBottomStepControlInitialized = true;
	Stair->StairData.bUseIntermediateControls = false;
	Stair->ResetIntermediateControlOffsets();
	Stair->RebuildStairMesh();
	Stair->MarkPackageDirty();
	MarkBuildingChanged(Building);
	return Stair;
}

AEHB_Railing* UEHBBuildingToolset::CreateRailing(
	AEHBBuildingActorBase* Building,
	const FString& Name,
	FVector LocalStart,
	FVector LocalEnd,
	float RailingHeight,
	float PostSpacing,
	float RailThickness,
	EEHBRailingFillMode FillMode,
	int32 FloorIndex,
	float PostWidth,
	float MaxRailSegmentLength,
	AEHB_Pillar* StartPillar,
	AEHB_Pillar* EndPillar)
{
	UWorld* World = GetEditorWorld();
	Building = ResolveEditableBuilding(Building);
	if (!World || !Building)
	{
		return nullptr;
	}

	if ((StartPillar && StartPillar->OwningBuilding != Building) || (EndPillar && EndPillar->OwningBuilding != Building))
	{
		return nullptr;
	}

	if (StartPillar)
	{
		StartPillar->EnsureElementGuid();
		LocalStart = StartPillar->GetElementLocalTransform().GetLocation();
	}
	if (EndPillar)
	{
		EndPillar->EnsureElementGuid();
		LocalEnd = EndPillar->GetElementLocalTransform().GetLocation();
	}

	const FVector LocalDirection2D = (LocalEnd - LocalStart).GetSafeNormal2D();
	if (LocalDirection2D.IsNearlyZero())
	{
		return nullptr;
	}

	const FScopedTransaction Transaction(LOCTEXT("CreateRailing", "Create EHB Railing"));
	Building->Modify();

	const FString ActorName = Name.IsEmpty() ? TEXT("EHB_Railing") : Name;
	const FVector LocalCenter = FMath::Lerp(LocalStart, LocalEnd, 0.5f);
	const FRotator LocalRotation(0.0f, LocalDirection2D.Rotation().Yaw, 0.0f);
	const FTransform LocalTransform(LocalRotation, LocalCenter);
	const FTransform WorldTransform = LocalTransform * Building->GetActorTransform();

	AEHB_Railing* Railing = SpawnEHBActor<AEHB_Railing>(*World, ResolveRailingActorClass(), ActorName, WorldTransform);
	if (!Railing)
	{
		return nullptr;
	}

	Railing->Modify();
	Railing->ElementName = FName(*ActorName);
	Railing->PostHeight = FMath::Max(1.0f, RailingHeight);
	Railing->RailHeight = FMath::Max(1.0f, RailingHeight);
	Railing->PostSpacing = FMath::Max(1.0f, PostSpacing);
	Railing->RailThickness = FMath::Max(0.1f, RailThickness);
	Railing->PostWidth = FMath::Max(0.1f, PostWidth);
	Railing->MaxRailSegmentLength = FMath::Max(1.0f, MaxRailSegmentLength);
	Railing->FillMode = FillMode;
	Railing->bOmitStartPost = StartPillar != nullptr;
	Railing->bOmitEndPost = EndPillar != nullptr;

	const FVector RailingLocalStart = LocalTransform.InverseTransformPosition(LocalStart);
	const FVector RailingLocalEnd = LocalTransform.InverseTransformPosition(LocalEnd);
	if (StartPillar)
	{
		Railing->StartAnchor.ElementGuid = StartPillar->ElementGuid;
		Railing->StartAnchor.LocalPoint = RailingLocalStart;
	}
	if (EndPillar)
	{
		Railing->EndAnchor.ElementGuid = EndPillar->ElementGuid;
		Railing->EndAnchor.LocalPoint = RailingLocalEnd;
	}
	if (!Railing->ConfigureLinear(Building, LocalTransform, RailingLocalStart, RailingLocalEnd, FMath::Max(1, FloorIndex)))
	{
		Railing->Destroy();
		return nullptr;
	}

	Railing->MarkPackageDirty();
	MarkBuildingChanged(Building);
	return Railing;
}

AEHB_Railing* UEHBBuildingToolset::CreateRailingOnStair(
	AEHBBuildingActorBase* Building,
	const FString& Name,
	AEHB_Stair* Stair,
	EEHBRailingSide Side,
	float RailingHeight,
	int32 StepsPerPost,
	float RailThickness,
	float StairSideOffset,
	int32 FloorIndex)
{
	UWorld* World = GetEditorWorld();
	Building = ResolveEditableBuilding(Building);
	if (!World || !Building || !Stair || Stair->OwningBuilding != Building)
	{
		return nullptr;
	}

	const FScopedTransaction Transaction(LOCTEXT("CreateRailingOnStair", "Create EHB Stair Railing"));
	Building->Modify();
	Stair->Modify();

	const FString ActorName = Name.IsEmpty() ? TEXT("EHB_StairRailing") : Name;
	AEHB_Railing* Railing = SpawnEHBActor<AEHB_Railing>(*World, ResolveRailingActorClass(), ActorName, Stair->GetActorTransform());
	if (!Railing)
	{
		return nullptr;
	}

	Railing->Modify();
	Railing->ElementName = FName(*ActorName);
	Railing->PostHeight = FMath::Max(1.0f, RailingHeight);
	Railing->RailHeight = FMath::Max(1.0f, RailingHeight);
	Railing->RailThickness = FMath::Max(0.1f, RailThickness);
	Railing->PostWidth = FMath::Max(0.1f, RailThickness);
	Railing->StepsPerPost = FMath::Max(1, StepsPerPost);
	Railing->StairSideOffset = FMath::Max(0.0f, StairSideOffset);
	Railing->FillMode = EEHBRailingFillMode::PostsAndRails;

	const int32 EffectiveFloorIndex = FloorIndex > 0 ? FloorIndex : FMath::Max(1, Stair->FloorIndex);
	if (!Railing->ConfigureOnStair(Building, Stair, Side, EffectiveFloorIndex))
	{
		Railing->Destroy();
		return nullptr;
	}

	Railing->MarkPackageDirty();
	MarkBuildingChanged(Building);
	return Railing;
}

AEHB_RailingGate* UEHBBuildingToolset::CreateRailingGate(
	AEHBBuildingActorBase* Building,
	const FString& Name,
	AEHB_Railing* Railing,
	float DistanceFromStart,
	float GateWidth,
	float GateHeight,
	float GateThickness,
	EEHBRailingGateHingeSide HingeSide,
	float OpenAngleDegrees,
	int32 FloorIndex)
{
	UWorld* World = GetEditorWorld();
	Building = ResolveEditableBuilding(Building);
	if (!World || !Building || !Railing || Railing->OwningBuilding != Building)
	{
		return nullptr;
	}

	if (Railing->GetRailingLength() <= UE_SMALL_NUMBER)
	{
		return nullptr;
	}

	const FScopedTransaction Transaction(LOCTEXT("CreateRailingGate", "Create EHB Railing Gate"));
	Building->Modify();
	Railing->Modify();

	const FString ActorName = Name.IsEmpty() ? TEXT("EHB_RailingGate") : Name;
	AEHB_RailingGate* Gate = SpawnEHBActor<AEHB_RailingGate>(*World, ResolveRailingGateActorClass(), ActorName, Railing->GetActorTransform());
	if (!Gate)
	{
		return nullptr;
	}

	Gate->Modify();
	Gate->ElementName = FName(*ActorName);
	Gate->Thickness = FMath::Max(0.1f, GateThickness);
	Gate->CurrentOpenAngle = FMath::Clamp(OpenAngleDegrees, -180.0f, 180.0f);

	if (!Gate->ConfigureOnRailing(
		Railing,
		DistanceFromStart,
		GateWidth,
		GateHeight,
		HingeSide))
	{
		Gate->Destroy();
		return nullptr;
	}

	if (FloorIndex > 0)
	{
		Gate->SetFloorAssignment(FloorIndex, EEHBBuildingFloorElementRole::Railing);
	}
	Gate->Thickness = FMath::Max(0.1f, GateThickness);
	Gate->CurrentOpenAngle = FMath::Clamp(OpenAngleDegrees, -180.0f, 180.0f);
	Gate->RebuildGateMesh();
	Gate->MarkPackageDirty();
	Railing->MarkPackageDirty();
	MarkBuildingChanged(Building);
	return Gate;
}

FString UEHBBuildingToolset::GetStairFoundationPlan(
	float StairHeight,
	float TreadDepth,
	float DesiredStepHeight,
	float FoundationThickness,
	float MinStepHeight,
	float MaxStepHeight,
	float NosingLength)
{
	const float SafeStairHeight = FMath::Max(1.0f, StairHeight);
	const float SafeTreadDepth = FMath::Max(1.0f, TreadDepth);
	float SafeMinStepHeight = FMath::Max(1.0f, MinStepHeight);
	float SafeMaxStepHeight = FMath::Max(1.0f, MaxStepHeight);
	if (SafeMinStepHeight > SafeMaxStepHeight)
	{
		Swap(SafeMinStepHeight, SafeMaxStepHeight);
	}

	const float SafeNosingLength = FMath::Clamp(NosingLength, 0.0f, FMath::Max(0.0f, SafeTreadDepth - 1.0f));
	const float RequestedTargetStepHeight = FoundationThickness > 0.0f
		? FoundationThickness
		: DesiredStepHeight;
	const FString InputMode = FoundationThickness > 0.0f
		? TEXT("foundationThickness")
		: (DesiredStepHeight > 0.0f ? TEXT("desiredStepHeight") : TEXT("default"));

	int32 StepCount = 0;
	float StepHeight = 0.0f;
	float StairLength = 0.0f;
	bool bUsedFallbackLayout = false;
	if (RequestedTargetStepHeight > 0.0f)
	{
		const float TargetStepHeight = FMath::Clamp(RequestedTargetStepHeight, SafeMinStepHeight, SafeMaxStepHeight);
		StepCount = FMath::Max(1, FMath::RoundToInt(SafeStairHeight / TargetStepHeight));
		StepHeight = SafeStairHeight / static_cast<float>(StepCount);
		if (StepHeight < SafeMinStepHeight || StepHeight > SafeMaxStepHeight)
		{
			bUsedFallbackLayout = true;
			CalculateToolsetStairLayout(
				SafeStairHeight,
				SafeTreadDepth,
				SafeMinStepHeight,
				SafeMaxStepHeight,
				SafeNosingLength,
				StepCount,
				StepHeight,
				StairLength);
		}
		else
		{
			const float StepRun = FMath::Max(1.0f, SafeTreadDepth - SafeNosingLength);
			StairLength = SafeTreadDepth + StepRun * static_cast<float>(FMath::Max(0, StepCount - 1));
		}
	}
	else
	{
		CalculateToolsetStairLayout(
			SafeStairHeight,
			SafeTreadDepth,
			SafeMinStepHeight,
			SafeMaxStepHeight,
			SafeNosingLength,
			StepCount,
			StepHeight,
			StairLength);
	}

	const float FoundationDelta = FoundationThickness > 0.0f ? FoundationThickness - StepHeight : 0.0f;
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("ok"), true);
	Root->SetStringField(TEXT("inputMode"), InputMode);
	Root->SetBoolField(TEXT("usedFallbackLayout"), bUsedFallbackLayout);
	Root->SetNumberField(TEXT("stairHeight"), SafeStairHeight);
	Root->SetNumberField(TEXT("treadDepth"), SafeTreadDepth);
	Root->SetNumberField(TEXT("nosingLength"), SafeNosingLength);
	Root->SetNumberField(TEXT("stepCount"), StepCount);
	Root->SetNumberField(TEXT("stepHeight"), StepHeight);
	Root->SetNumberField(TEXT("stairLength"), StairLength);
	Root->SetNumberField(TEXT("recommendedFoundationThickness"), StepHeight);
	Root->SetNumberField(TEXT("recommendedStairMinStepHeight"), StepHeight);
	Root->SetNumberField(TEXT("recommendedStairMaxStepHeight"), StepHeight);
	Root->SetNumberField(TEXT("requestedFoundationThickness"), FoundationThickness);
	Root->SetNumberField(TEXT("foundationHeightDelta"), FoundationDelta);
	Root->SetBoolField(TEXT("foundationShouldUseRecommendedThickness"), FoundationThickness <= 0.0f || !FMath::IsNearlyEqual(FoundationThickness, StepHeight, 0.1f));
	Root->SetStringField(TEXT("usage"), TEXT("Use recommendedFoundationThickness for foundation slabs. Pass recommendedStairMinStepHeight and recommendedStairMaxStepHeight to stair creation to keep risers aligned."));

	FString Output;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
	FJsonSerializer::Serialize(Root, Writer);
	return Output;
}

AEHB_DoorWindow* UEHBBuildingToolset::AddDoorWindow(
	AEHB_Wall* Wall,
	bool bDoor,
	float DistanceFromStart,
	float Width,
	float Height,
	float SillHeight,
	float OpeningThickness)
{
	UWorld* World = GetEditorWorld();
	AEHBBuildingActorBase* Building = Wall ? ResolveEditableBuilding(Wall->OwningBuilding) : nullptr;
	if (!World || !Wall || !Building)
	{
		return nullptr;
	}

	const FScopedTransaction Transaction(LOCTEXT("AddDoorWindow", "Add EHB Door Window"));
	Wall->Modify();
	Building->Modify();

	const float SafeDistanceFromStart = FMath::Max(0.0f, DistanceFromStart);
	const float SafeWidth = FMath::Max(1.0f, Width);
	const float SafeHeight = FMath::Max(1.0f, Height);
	const float SafeBottomHeight = FMath::Max(0.0f, SillHeight);
	const float SafeOpeningThickness = OpeningThickness > 0.0f
		? FMath::Max(1.0f, OpeningThickness)
		: FMath::Max(1.0f, Wall->Thickness);
	const FString ActorName = bDoor ? TEXT("EHB_Door") : TEXT("EHB_Window");
	const FTransform DoorWindowTransform(Wall->GetWorldLocationOnCenterAxisAtDistance(
		SafeDistanceFromStart,
		SafeBottomHeight));

	AEHB_DoorWindow* DoorWindow = SpawnEHBActor<AEHB_DoorWindow>(*World, AEHB_DoorWindow::StaticClass(), ActorName, DoorWindowTransform);
	if (!DoorWindow)
	{
		return nullptr;
	}

	DoorWindow->Modify();
	DoorWindow->ElementName = FName(*ActorName);
	DoorWindow->AttachToBuilding(Building, DoorWindowTransform.GetRelativeTransform(Building->GetActorTransform()));
	DoorWindow->Kind = bDoor ? EEHBDoorWindowElementKind::Door : EEHBDoorWindowElementKind::Window;
	if (bDoor)
	{
		DoorWindow->InitializeDefaultDoorOpening();
	}
	else
	{
		DoorWindow->InitializeDefaultWindowOpening();
	}
	DoorWindow->SetRectangularOpeningDimensions(SafeWidth, SafeHeight, SafeBottomHeight, SafeOpeningThickness);
	DoorWindow->BindToWall(Wall, SafeDistanceFromStart);
	DoorWindow->SetFloorAssignment(Wall->FloorIndex, EEHBBuildingFloorElementRole::HostedElement);
	DoorWindow->RebuildDoorWindow();
	DoorWindow->MarkPackageDirty();

	Wall->RebuildWallMesh();
	MarkBuildingChanged(Building);
	return DoorWindow;
}

bool UEHBBuildingToolset::ReplaceDoorWindow(
	AEHB_DoorWindow* ReplacementDoorWindow,
	AEHB_DoorWindow* ExistingDoorWindow)
{
	if (!ReplacementDoorWindow || !ExistingDoorWindow || ReplacementDoorWindow == ExistingDoorWindow)
	{
		return false;
	}

	AEHBBuildingActorBase* BuildingToMark = ExistingDoorWindow->OwningBuilding
		? ExistingDoorWindow->OwningBuilding
		: ReplacementDoorWindow->OwningBuilding;
	const FScopedTransaction Transaction(LOCTEXT("ReplaceDoorWindow", "Replace EHB Door Window"));
	const bool bReplaced = AEHB_DoorWindow::ReplaceDoorWindow(ReplacementDoorWindow, ExistingDoorWindow);
	if (bReplaced)
	{
		MarkBuildingChanged(BuildingToMark ? BuildingToMark : ReplacementDoorWindow->OwningBuilding.Get());
	}
	return bReplaced;
}

FString UEHBBuildingToolset::CreateRectangularRoom(
	AEHBBuildingActorBase* Building,
	const FString& NamePrefix,
	FVector LocalCenter,
	float Width,
	float Depth,
	float WallHeight,
	float WallThickness,
	float PillarSize,
	bool bCreateFoundation,
	bool bCreateCeilingSlab,
	int32 FloorIndex,
	float FoundationThickness)
{
	Building = ResolveEditableBuilding(Building);
	if (!Building)
	{
		return TEXT("{\"ok\":false,\"message\":\"Building is null or is not the currently selected EHB_Building.\"}");
	}

	const float SafeWidth = FMath::Max(1.0f, Width);
	const float SafeDepth = FMath::Max(1.0f, Depth);
	const float SafeWallHeight = FMath::Max(1.0f, WallHeight);
	const float SafeWallThickness = FMath::Max(1.0f, WallThickness);
	const float SafePillarSize = SafeWallThickness;
	const float SafeFoundationThickness = FMath::Max(1.0f, FoundationThickness);
	const int32 SafeFloorIndex = FMath::Max(1, FloorIndex);
	const FString Prefix = NamePrefix.IsEmpty() ? TEXT("Room") : NamePrefix;

	const FVector HalfExtent(SafeWidth * 0.5f, SafeDepth * 0.5f, 0.0f);
	const FVector P0 = LocalCenter + FVector(-HalfExtent.X, -HalfExtent.Y, 0.0f);
	const FVector P1 = LocalCenter + FVector(HalfExtent.X, -HalfExtent.Y, 0.0f);
	const FVector P2 = LocalCenter + FVector(HalfExtent.X, HalfExtent.Y, 0.0f);
	const FVector P3 = LocalCenter + FVector(-HalfExtent.X, HalfExtent.Y, 0.0f);

	TArray<FVector> SlabPolygon;
	SlabPolygon.Add(P0);
	SlabPolygon.Add(P1);
	SlabPolygon.Add(P2);
	SlabPolygon.Add(P3);

	AEHB_FloorSlab* Foundation = nullptr;
	if (bCreateFoundation)
	{
		Foundation = CreateFloorSlab(Building, Prefix + TEXT("_Foundation"), SlabPolygon, SafeFoundationThickness, 0.0f, true, true, 5.0f, 0);
		if (!Foundation)
		{
			return TEXT("{\"ok\":false,\"message\":\"Failed to create foundation before upper building elements.\"}");
		}
	}

	AEHB_Pillar* Pillar0 = CreatePillar(Building, Prefix + TEXT("_P0"), P0, SafeWallHeight, SafePillarSize, SafePillarSize, SafeFloorIndex);
	AEHB_Pillar* Pillar1 = CreatePillar(Building, Prefix + TEXT("_P1"), P1, SafeWallHeight, SafePillarSize, SafePillarSize, SafeFloorIndex);
	AEHB_Pillar* Pillar2 = CreatePillar(Building, Prefix + TEXT("_P2"), P2, SafeWallHeight, SafePillarSize, SafePillarSize, SafeFloorIndex);
	AEHB_Pillar* Pillar3 = CreatePillar(Building, Prefix + TEXT("_P3"), P3, SafeWallHeight, SafePillarSize, SafePillarSize, SafeFloorIndex);

	if (!Pillar0 || !Pillar1 || !Pillar2 || !Pillar3)
	{
		return TEXT("{\"ok\":false,\"message\":\"Failed to create one or more pillars.\"}");
	}

	AEHB_Wall* Wall0 = ConnectPillars(Building, Pillar0, Pillar1, SafeWallHeight, SafeWallThickness);
	AEHB_Wall* Wall1 = ConnectPillars(Building, Pillar1, Pillar2, SafeWallHeight, SafeWallThickness);
	AEHB_Wall* Wall2 = ConnectPillars(Building, Pillar2, Pillar3, SafeWallHeight, SafeWallThickness);
	AEHB_Wall* Wall3 = ConnectPillars(Building, Pillar3, Pillar0, SafeWallHeight, SafeWallThickness);

	if (!Wall0 || !Wall1 || !Wall2 || !Wall3)
	{
		return TEXT("{\"ok\":false,\"message\":\"Failed to connect one or more walls.\"}");
	}

	AEHB_FloorSlab* Ceiling = nullptr;
	if (bCreateCeilingSlab)
	{
		Ceiling = CreateRoomFilledFloorSlabAtPoint(
			Building,
			Prefix + TEXT("_Ceiling"),
			LocalCenter,
			LocalCenter.Z + SafeWallHeight,
			20.0f,
			SafeFloorIndex);
	}

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("ok"), true);
	Root->SetStringField(TEXT("message"), TEXT("Rectangular room created under the selected Building actor, with foundation first, then pillars/walls, then optional room-filled ceiling slab."));
	Root->SetStringField(TEXT("building"), Building->GetPathName());
	Root->SetNumberField(TEXT("foundationThickness"), bCreateFoundation ? SafeFoundationThickness : 0.0f);

	TArray<TSharedPtr<FJsonValue>> Pillars;
	for (AEHB_Pillar* Pillar : { Pillar0, Pillar1, Pillar2, Pillar3 })
	{
		Pillars.Add(MakeShared<FJsonValueString>(Pillar->GetPathName()));
	}
	Root->SetArrayField(TEXT("pillars"), Pillars);

	TArray<TSharedPtr<FJsonValue>> Walls;
	for (AEHB_Wall* Wall : { Wall0, Wall1, Wall2, Wall3 })
	{
		Walls.Add(MakeShared<FJsonValueString>(Wall->GetPathName()));
	}
	Root->SetArrayField(TEXT("walls"), Walls);

	TArray<TSharedPtr<FJsonValue>> Slabs;
	if (Foundation)
	{
		Slabs.Add(MakeShared<FJsonValueString>(Foundation->GetPathName()));
	}
	if (Ceiling)
	{
		Slabs.Add(MakeShared<FJsonValueString>(Ceiling->GetPathName()));
	}
	Root->SetArrayField(TEXT("slabs"), Slabs);

	FString Output;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
	FJsonSerializer::Serialize(Root, Writer);
	return Output;
}

FString UEHBBuildingToolset::CreateRectangularFloorPlan(
	AEHBBuildingActorBase* Building,
	const FString& NamePrefix,
	const TArray<FEHBToolsetRectRoomSpec>& Rooms,
	float WallHeight,
	float WallThickness,
	float PillarSize,
	float FloorHeight,
	bool bCreateFoundation,
	bool bCreateCeilingSlabs,
	float FoundationPadding,
	float FoundationThickness)
{
	Building = ResolveEditableBuilding(Building);
	if (!Building)
	{
		return TEXT("{\"ok\":false,\"message\":\"Building is null or is not the currently selected EHB_Building.\"}");
	}
	if (Rooms.IsEmpty())
	{
		return TEXT("{\"ok\":false,\"message\":\"Rooms is empty.\"}");
	}

	const FString Prefix = NamePrefix.IsEmpty() ? TEXT("Plan") : NamePrefix;
	const float SafeWallHeight = FMath::Max(1.0f, WallHeight);
	const float SafeWallThickness = FMath::Max(1.0f, WallThickness);
	const float SafePillarSize = SafeWallThickness;
	const float SafeFloorHeight = FMath::Max(1.0f, FloorHeight);
	const float SafeFoundationPadding = FMath::Max(0.0f, FoundationPadding);
	const float SafeFoundationThickness = FMath::Max(1.0f, FoundationThickness);

	TMap<FString, AEHB_Pillar*> PillarsByPoint;
	TMap<FString, AEHB_Wall*> WallsByEdge;
	TArray<AEHB_Pillar*> CreatedPillars;
	TArray<AEHB_Wall*> CreatedWalls;
	TArray<AEHB_FloorSlab*> CreatedSlabs;
	TArray<TSharedPtr<FJsonValue>> RoomResults;
	bool bHasFoundationBounds = false;
	double MinX = 0.0;
	double MinY = 0.0;
	double MaxX = 0.0;
	double MaxY = 0.0;

	for (const FEHBToolsetRectRoomSpec& Room : Rooms)
	{
		const int32 SafeFloorIndex = FMath::Max(1, Room.FloorIndex);
		const float SafeWidth = FMath::Max(1.0f, Room.Width);
		const float SafeDepth = FMath::Max(1.0f, Room.Depth);
		FVector LocalCenter = Room.LocalCenter;
		if (FMath::IsNearlyZero(LocalCenter.Z) && SafeFloorIndex > 1)
		{
			LocalCenter.Z = static_cast<float>(SafeFloorIndex - 1) * SafeFloorHeight;
		}

		const FVector HalfExtent(SafeWidth * 0.5f, SafeDepth * 0.5f, 0.0f);
		const FVector Corners[] = {
			LocalCenter + FVector(-HalfExtent.X, -HalfExtent.Y, 0.0f),
			LocalCenter + FVector(HalfExtent.X, -HalfExtent.Y, 0.0f),
			LocalCenter + FVector(HalfExtent.X, HalfExtent.Y, 0.0f),
			LocalCenter + FVector(-HalfExtent.X, HalfExtent.Y, 0.0f)
		};

		for (const FVector& Corner : Corners)
		{
			if (!bHasFoundationBounds)
			{
				MinX = MaxX = Corner.X;
				MinY = MaxY = Corner.Y;
				bHasFoundationBounds = true;
			}
			else
			{
				MinX = FMath::Min(MinX, static_cast<double>(Corner.X));
				MinY = FMath::Min(MinY, static_cast<double>(Corner.Y));
				MaxX = FMath::Max(MaxX, static_cast<double>(Corner.X));
				MaxY = FMath::Max(MaxY, static_cast<double>(Corner.Y));
			}
		}
	}

	AEHB_FloorSlab* Foundation = nullptr;
	if (bCreateFoundation && bHasFoundationBounds)
	{
		TArray<FVector> FoundationPolygon;
		FoundationPolygon.Add(FVector(MinX - SafeFoundationPadding, MinY - SafeFoundationPadding, 0.0));
		FoundationPolygon.Add(FVector(MaxX + SafeFoundationPadding, MinY - SafeFoundationPadding, 0.0));
		FoundationPolygon.Add(FVector(MaxX + SafeFoundationPadding, MaxY + SafeFoundationPadding, 0.0));
		FoundationPolygon.Add(FVector(MinX - SafeFoundationPadding, MaxY + SafeFoundationPadding, 0.0));
		Foundation = CreateFloorSlab(Building, Prefix + TEXT("_Foundation"), FoundationPolygon, SafeFoundationThickness, 0.0f, true, true, 5.0f, 0);
		if (Foundation)
		{
			CreatedSlabs.Add(Foundation);
		}
	}

	for (int32 RoomIndex = 0; RoomIndex < Rooms.Num(); ++RoomIndex)
	{
		const FEHBToolsetRectRoomSpec& Room = Rooms[RoomIndex];
		const int32 SafeFloorIndex = FMath::Max(1, Room.FloorIndex);
		const float SafeWidth = FMath::Max(1.0f, Room.Width);
		const float SafeDepth = FMath::Max(1.0f, Room.Depth);
		FVector LocalCenter = Room.LocalCenter;
		if (FMath::IsNearlyZero(LocalCenter.Z) && SafeFloorIndex > 1)
		{
			LocalCenter.Z = static_cast<float>(SafeFloorIndex - 1) * SafeFloorHeight;
		}

		const FVector HalfExtent(SafeWidth * 0.5f, SafeDepth * 0.5f, 0.0f);
		const FVector Corners[] = {
			LocalCenter + FVector(-HalfExtent.X, -HalfExtent.Y, 0.0f),
			LocalCenter + FVector(HalfExtent.X, -HalfExtent.Y, 0.0f),
			LocalCenter + FVector(HalfExtent.X, HalfExtent.Y, 0.0f),
			LocalCenter + FVector(-HalfExtent.X, HalfExtent.Y, 0.0f)
		};

		for (const FVector& Corner : Corners)
		{
			if (!bHasFoundationBounds)
			{
				MinX = MaxX = Corner.X;
				MinY = MaxY = Corner.Y;
				bHasFoundationBounds = true;
			}
			else
			{
				MinX = FMath::Min(MinX, static_cast<double>(Corner.X));
				MinY = FMath::Min(MinY, static_cast<double>(Corner.Y));
				MaxX = FMath::Max(MaxX, static_cast<double>(Corner.X));
				MaxY = FMath::Max(MaxY, static_cast<double>(Corner.Y));
			}
		}

		const FString RoomName = Room.Name.IsEmpty()
			? FString::Printf(TEXT("%s_Room_%02d"), *Prefix, RoomIndex + 1)
			: Room.Name;
		AEHB_Pillar* RoomPillars[4] = { nullptr, nullptr, nullptr, nullptr };
		for (int32 CornerIndex = 0; CornerIndex < 4; ++CornerIndex)
		{
			const FString PointKey = MakePlanPointKey(Corners[CornerIndex], SafeFloorIndex);
			if (AEHB_Pillar** ExistingPillar = PillarsByPoint.Find(PointKey))
			{
				RoomPillars[CornerIndex] = *ExistingPillar;
				continue;
			}

			AEHB_Pillar* Pillar = CreatePillar(
				Building,
				FString::Printf(TEXT("%s_P_%02d_%d"), *RoomName, CornerIndex, SafeFloorIndex),
				Corners[CornerIndex],
				SafeWallHeight,
				SafePillarSize,
				SafePillarSize,
				SafeFloorIndex);
			if (!Pillar)
			{
				return FString::Printf(TEXT("{\"ok\":false,\"message\":\"Failed to create pillar for room %s.\"}"), *RoomName);
			}

			PillarsByPoint.Add(PointKey, Pillar);
			RoomPillars[CornerIndex] = Pillar;
			CreatedPillars.Add(Pillar);
		}

		AEHB_Wall* RoomWalls[4] = { nullptr, nullptr, nullptr, nullptr };
		for (int32 EdgeIndex = 0; EdgeIndex < 4; ++EdgeIndex)
		{
			const int32 StartIndex = EdgeIndex;
			const int32 EndIndex = (EdgeIndex + 1) % 4;
			const FString EdgeKey = MakePlanEdgeKey(Corners[StartIndex], Corners[EndIndex], SafeFloorIndex);
			if (AEHB_Wall** ExistingWall = WallsByEdge.Find(EdgeKey))
			{
				RoomWalls[EdgeIndex] = *ExistingWall;
				continue;
			}

			AEHB_Wall* Wall = ConnectPillars(
				Building,
				RoomPillars[StartIndex],
				RoomPillars[EndIndex],
				SafeWallHeight,
				SafeWallThickness);
			if (!Wall)
			{
				return FString::Printf(TEXT("{\"ok\":false,\"message\":\"Failed to create wall for room %s.\"}"), *RoomName);
			}

			WallsByEdge.Add(EdgeKey, Wall);
			RoomWalls[EdgeIndex] = Wall;
			CreatedWalls.Add(Wall);
		}

		AEHB_FloorSlab* Ceiling = nullptr;
		if (bCreateCeilingSlabs)
		{
			Ceiling = CreateRoomFilledFloorSlabAtPoint(
				Building,
				RoomName + TEXT("_Ceiling"),
				LocalCenter,
				LocalCenter.Z + SafeWallHeight,
				20.0f,
				SafeFloorIndex);
			if (Ceiling)
			{
				CreatedSlabs.Add(Ceiling);
			}
		}

		TSharedRef<FJsonObject> RoomObject = MakeShared<FJsonObject>();
		RoomObject->SetStringField(TEXT("name"), RoomName);
		RoomObject->SetNumberField(TEXT("floorIndex"), SafeFloorIndex);
		RoomObject->SetStringField(TEXT("center"), LocalCenter.ToString());
		RoomObject->SetBoolField(TEXT("ceilingCreated"), Ceiling != nullptr);
		RoomResults.Add(MakeShared<FJsonValueObject>(RoomObject));
	}

	MarkBuildingChanged(Building);
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("ok"), true);
	Root->SetStringField(TEXT("message"), TEXT("Rectangular floor plan created with foundation first, shared pillars and shared walls."));
	Root->SetStringField(TEXT("building"), Building->GetPathName());
	Root->SetNumberField(TEXT("inputRoomCount"), Rooms.Num());
	Root->SetNumberField(TEXT("createdPillarCount"), CreatedPillars.Num());
	Root->SetNumberField(TEXT("createdWallCount"), CreatedWalls.Num());
	Root->SetNumberField(TEXT("createdSlabCount"), CreatedSlabs.Num());
	Root->SetNumberField(TEXT("uniquePillarCount"), PillarsByPoint.Num());
	Root->SetNumberField(TEXT("uniqueWallCount"), WallsByEdge.Num());
	Root->SetBoolField(TEXT("foundationCreated"), Foundation != nullptr);
	Root->SetNumberField(TEXT("foundationThickness"), Foundation ? SafeFoundationThickness : 0.0f);
	Root->SetNumberField(TEXT("floorHeight"), SafeFloorHeight);
	Root->SetStringField(TEXT("floorHeightRule"), TEXT("FloorHeight is wall-base to next wall-base distance; slabs embed downward and must not add vertical gaps between floors."));
	Root->SetArrayField(TEXT("rooms"), RoomResults);

	FString Output;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
	FJsonSerializer::Serialize(Root, Writer);
	return Output;
}

FString UEHBBuildingToolset::CreateAIHouseFromRoomPlan(
	AEHBBuildingActorBase* Building,
	const FString& NamePrefix,
	const TArray<FEHBToolsetRectRoomSpec>& Rooms,
	float WallHeight,
	float WallThickness,
	float PillarSize,
	float FloorHeight,
	bool bCreateFoundation,
	bool bCreateRoomSlabs,
	float FoundationPadding,
	float FoundationThickness)
{
	Building = ResolveEditableBuilding(Building);
	if (!Building)
	{
		return TEXT("{\"ok\":false,\"message\":\"Building is null or is not the currently selected EHB_Building.\"}");
	}
	if (Rooms.IsEmpty())
	{
		return TEXT("{\"ok\":false,\"message\":\"Rooms is empty.\"}");
	}

	const FString Prefix = NamePrefix.IsEmpty() ? TEXT("AIHouse") : NamePrefix;
	const float SafeWallHeight = FMath::Max(1.0f, WallHeight);
	const float SafeWallThickness = FMath::Max(1.0f, WallThickness);
	const float SafePillarSize = SafeWallThickness;
	const float SafeFloorHeight = FMath::Max(1.0f, FloorHeight);
	const float SafeFoundationPadding = FMath::Max(0.0f, FoundationPadding);
	const float SafeFoundationThickness = FMath::Max(1.0f, FoundationThickness);
	constexpr double AxisTolerance = 0.1;
	constexpr double MinSegmentLength = 1.0;

	TMap<FString, FEHBAIWallLine> LinesByKey;
	TArray<TSharedPtr<FJsonValue>> RoomResults;
	bool bHasFoundationBounds = false;
	double MinX = 0.0;
	double MinY = 0.0;
	double MaxX = 0.0;
	double MaxY = 0.0;

	auto AddBoundsPoint = [&bHasFoundationBounds, &MinX, &MinY, &MaxX, &MaxY](const FVector& Point)
	{
		if (!bHasFoundationBounds)
		{
			MinX = MaxX = Point.X;
			MinY = MaxY = Point.Y;
			bHasFoundationBounds = true;
			return;
		}

		MinX = FMath::Min(MinX, static_cast<double>(Point.X));
		MinY = FMath::Min(MinY, static_cast<double>(Point.Y));
		MaxX = FMath::Max(MaxX, static_cast<double>(Point.X));
		MaxY = FMath::Max(MaxY, static_cast<double>(Point.Y));
	};

	auto AddWallLine = [&LinesByKey](const FVector& A, const FVector& B, int32 FloorIndex, FString& OutError) -> bool
	{
		const double DeltaX = FMath::Abs(static_cast<double>(A.X) - static_cast<double>(B.X));
		const double DeltaY = FMath::Abs(static_cast<double>(A.Y) - static_cast<double>(B.Y));
		const bool bHorizontal = DeltaY <= 0.1 && DeltaX > 0.1;
		const bool bVertical = DeltaX <= 0.1 && DeltaY > 0.1;
		if (!bHorizontal && !bVertical)
		{
			OutError = TEXT("CreateAIHouseFromRoomPlan only accepts axis-aligned rectangular rooms.");
			return false;
		}

		const double Z = (static_cast<double>(A.Z) + static_cast<double>(B.Z)) * 0.5;
		const double Constant = bHorizontal
			? (static_cast<double>(A.Y) + static_cast<double>(B.Y)) * 0.5
			: (static_cast<double>(A.X) + static_cast<double>(B.X)) * 0.5;
		const double AxisA = bHorizontal ? static_cast<double>(A.X) : static_cast<double>(A.Y);
		const double AxisB = bHorizontal ? static_cast<double>(B.X) : static_cast<double>(B.Y);
		FEHBAIWallInterval Interval;
		Interval.Min = FMath::Min(AxisA, AxisB);
		Interval.Max = FMath::Max(AxisA, AxisB);

		const FString Key = MakePlanLineKey(bHorizontal, FloorIndex, Z, Constant);
		FEHBAIWallLine& Line = LinesByKey.FindOrAdd(Key);
		Line.bHorizontal = bHorizontal;
		Line.FloorIndex = FloorIndex;
		Line.Z = Z;
		Line.Constant = Constant;
		Line.Intervals.Add(Interval);
		Line.Breaks.Add(Interval.Min);
		Line.Breaks.Add(Interval.Max);
		return true;
	};

	for (int32 RoomIndex = 0; RoomIndex < Rooms.Num(); ++RoomIndex)
	{
		const FEHBToolsetRectRoomSpec& Room = Rooms[RoomIndex];
		const int32 SafeFloorIndex = FMath::Max(1, Room.FloorIndex);
		const float SafeWidth = FMath::Max(1.0f, Room.Width);
		const float SafeDepth = FMath::Max(1.0f, Room.Depth);
		FVector LocalCenter = Room.LocalCenter;
		if (FMath::IsNearlyZero(LocalCenter.Z) && SafeFloorIndex > 1)
		{
			LocalCenter.Z = static_cast<float>(SafeFloorIndex - 1) * SafeFloorHeight;
		}

		const FVector HalfExtent(SafeWidth * 0.5f, SafeDepth * 0.5f, 0.0f);
		const FVector Corners[] = {
			LocalCenter + FVector(-HalfExtent.X, -HalfExtent.Y, 0.0f),
			LocalCenter + FVector(HalfExtent.X, -HalfExtent.Y, 0.0f),
			LocalCenter + FVector(HalfExtent.X, HalfExtent.Y, 0.0f),
			LocalCenter + FVector(-HalfExtent.X, HalfExtent.Y, 0.0f)
		};

		for (const FVector& Corner : Corners)
		{
			AddBoundsPoint(Corner);
		}

		FString Error;
		for (int32 EdgeIndex = 0; EdgeIndex < 4; ++EdgeIndex)
		{
			if (!AddWallLine(Corners[EdgeIndex], Corners[(EdgeIndex + 1) % 4], SafeFloorIndex, Error))
			{
				return FString::Printf(TEXT("{\"ok\":false,\"message\":\"%s\"}"), *Error);
			}
		}

		const FString RoomName = Room.Name.IsEmpty()
			? FString::Printf(TEXT("%s_Room_%02d"), *Prefix, RoomIndex + 1)
			: Room.Name;
		TSharedRef<FJsonObject> RoomObject = MakeShared<FJsonObject>();
		RoomObject->SetStringField(TEXT("name"), RoomName);
		RoomObject->SetNumberField(TEXT("floorIndex"), SafeFloorIndex);
		RoomObject->SetStringField(TEXT("center"), LocalCenter.ToString());
		RoomObject->SetBoolField(TEXT("slabRequested"), bCreateRoomSlabs);
		RoomResults.Add(MakeShared<FJsonValueObject>(RoomObject));
	}

	TArray<FEHBAIWallLine*> Lines;
	for (TPair<FString, FEHBAIWallLine>& LinePair : LinesByKey)
	{
		Lines.Add(&LinePair.Value);
	}
	for (FEHBAIWallLine* Horizontal : Lines)
	{
		if (!Horizontal || !Horizontal->bHorizontal)
		{
			continue;
		}

		for (FEHBAIWallLine* Vertical : Lines)
		{
			if (!Vertical || Vertical->bHorizontal || Horizontal->FloorIndex != Vertical->FloorIndex || !FMath::IsNearlyEqual(Horizontal->Z, Vertical->Z, AxisTolerance))
			{
				continue;
			}

			for (const FEHBAIWallInterval& HInterval : Horizontal->Intervals)
			{
				if (!IsValueInsideInterval(Vertical->Constant, HInterval, AxisTolerance))
				{
					continue;
				}

				for (const FEHBAIWallInterval& VInterval : Vertical->Intervals)
				{
					if (IsValueInsideInterval(Horizontal->Constant, VInterval, AxisTolerance))
					{
						Horizontal->Breaks.Add(Vertical->Constant);
						Vertical->Breaks.Add(Horizontal->Constant);
					}
				}
			}
		}
	}

	TArray<FEHBAIWallSegment> Segments;
	TSet<FString> SegmentKeys;
	for (FEHBAIWallLine* Line : Lines)
	{
		if (!Line)
		{
			continue;
		}

		SortUniqueWallBreaks(Line->Breaks, AxisTolerance);
		for (int32 BreakIndex = 0; BreakIndex + 1 < Line->Breaks.Num(); ++BreakIndex)
		{
			const double A = Line->Breaks[BreakIndex];
			const double B = Line->Breaks[BreakIndex + 1];
			if (B - A < MinSegmentLength || !IsSpanCoveredByAnyInterval(A, B, Line->Intervals, AxisTolerance))
			{
				continue;
			}

			FEHBAIWallSegment Segment;
			Segment.FloorIndex = Line->FloorIndex;
			Segment.Start = Line->bHorizontal
				? FVector(A, Line->Constant, Line->Z)
				: FVector(Line->Constant, A, Line->Z);
			Segment.End = Line->bHorizontal
				? FVector(B, Line->Constant, Line->Z)
				: FVector(Line->Constant, B, Line->Z);

			const FString SegmentKey = MakePlanEdgeKey(Segment.Start, Segment.End, Segment.FloorIndex);
			if (!SegmentKeys.Contains(SegmentKey))
			{
				SegmentKeys.Add(SegmentKey);
				Segments.Add(Segment);
			}
		}
	}

	if (Segments.IsEmpty())
	{
		return TEXT("{\"ok\":false,\"message\":\"No valid wall segments could be built from the room plan.\"}");
	}

	TArray<AEHB_FloorSlab*> CreatedSlabs;
	AEHB_FloorSlab* Foundation = nullptr;
	if (bCreateFoundation && bHasFoundationBounds)
	{
		TArray<FVector> FoundationPolygon;
		FoundationPolygon.Add(FVector(MinX - SafeFoundationPadding, MinY - SafeFoundationPadding, 0.0));
		FoundationPolygon.Add(FVector(MaxX + SafeFoundationPadding, MinY - SafeFoundationPadding, 0.0));
		FoundationPolygon.Add(FVector(MaxX + SafeFoundationPadding, MaxY + SafeFoundationPadding, 0.0));
		FoundationPolygon.Add(FVector(MinX - SafeFoundationPadding, MaxY + SafeFoundationPadding, 0.0));
		Foundation = CreateFloorSlab(Building, Prefix + TEXT("_Foundation"), FoundationPolygon, SafeFoundationThickness, 0.0f, true, true, 5.0f, 0);
		if (Foundation)
		{
			CreatedSlabs.Add(Foundation);
		}
	}

	TMap<FString, AEHB_Pillar*> PillarsByPoint;
	TArray<AEHB_Pillar*> CreatedPillars;
	TArray<AEHB_Wall*> CreatedWalls;
	TArray<TSharedPtr<FJsonValue>> WallResults;
	auto GetOrCreatePillar = [&PillarsByPoint, &CreatedPillars, Building, &Prefix, SafeWallHeight, SafePillarSize](const FVector& Point, int32 FloorIndex) -> AEHB_Pillar*
	{
		const FString PointKey = MakePlanPointKey(Point, FloorIndex);
		if (AEHB_Pillar** Existing = PillarsByPoint.Find(PointKey))
		{
			return *Existing;
		}

		AEHB_Pillar* Pillar = UEHBBuildingToolset::CreatePillar(
			Building,
			FString::Printf(TEXT("%s_AI_P_%03d"), *Prefix, PillarsByPoint.Num() + 1),
			Point,
			SafeWallHeight,
			SafePillarSize,
			SafePillarSize,
			FloorIndex);
		if (Pillar)
		{
			PillarsByPoint.Add(PointKey, Pillar);
			CreatedPillars.Add(Pillar);
		}
		return Pillar;
	};

	for (int32 SegmentIndex = 0; SegmentIndex < Segments.Num(); ++SegmentIndex)
	{
		const FEHBAIWallSegment& Segment = Segments[SegmentIndex];
		AEHB_Pillar* StartPillar = GetOrCreatePillar(Segment.Start, Segment.FloorIndex);
		AEHB_Pillar* EndPillar = GetOrCreatePillar(Segment.End, Segment.FloorIndex);
		if (!StartPillar || !EndPillar)
		{
			return FString::Printf(TEXT("{\"ok\":false,\"message\":\"Failed to create pillar for AI wall segment %d.\"}"), SegmentIndex);
		}

		AEHB_Wall* Wall = ConnectPillars(Building, StartPillar, EndPillar, SafeWallHeight, SafeWallThickness);
		if (!Wall)
		{
			return FString::Printf(TEXT("{\"ok\":false,\"message\":\"Failed to create AI wall segment %d.\"}"), SegmentIndex);
		}

		CreatedWalls.Add(Wall);
		TSharedRef<FJsonObject> WallObject = MakeShared<FJsonObject>();
		WallObject->SetStringField(TEXT("wall"), Wall->GetPathName());
		WallObject->SetNumberField(TEXT("floorIndex"), Segment.FloorIndex);
		WallObject->SetField(TEXT("localStart"), MakeVectorJsonValue(Segment.Start));
		WallObject->SetField(TEXT("localEnd"), MakeVectorJsonValue(Segment.End));
		WallResults.Add(MakeShared<FJsonValueObject>(WallObject));
	}

	for (int32 RoomIndex = 0; RoomIndex < Rooms.Num(); ++RoomIndex)
	{
		const FEHBToolsetRectRoomSpec& Room = Rooms[RoomIndex];
		const int32 SafeFloorIndex = FMath::Max(1, Room.FloorIndex);
		FVector LocalCenter = Room.LocalCenter;
		if (FMath::IsNearlyZero(LocalCenter.Z) && SafeFloorIndex > 1)
		{
			LocalCenter.Z = static_cast<float>(SafeFloorIndex - 1) * SafeFloorHeight;
		}

		if (bCreateRoomSlabs)
		{
			const FString RoomName = Room.Name.IsEmpty()
				? FString::Printf(TEXT("%s_Room_%02d"), *Prefix, RoomIndex + 1)
				: Room.Name;
			AEHB_FloorSlab* Slab = CreateRoomFilledFloorSlabAtPoint(
				Building,
				RoomName + TEXT("_Ceiling"),
				LocalCenter,
				LocalCenter.Z + SafeWallHeight,
				20.0f,
				SafeFloorIndex);
			if (Slab)
			{
				CreatedSlabs.Add(Slab);
			}

			if (RoomResults.IsValidIndex(RoomIndex) && RoomResults[RoomIndex].IsValid() && RoomResults[RoomIndex]->AsObject().IsValid())
			{
				RoomResults[RoomIndex]->AsObject()->SetBoolField(TEXT("slabCreated"), Slab != nullptr);
			}
		}
	}

	MarkBuildingChanged(Building);
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("ok"), true);
	Root->SetStringField(TEXT("message"), TEXT("AI house plan created from a wall-line graph: room edges were merged, intersections/T-junctions were split, pillars were reused, and unique wall segments were connected."));
	Root->SetStringField(TEXT("building"), Building->GetPathName());
	Root->SetStringField(TEXT("wallGraphRule"), TEXT("Input rectangles are converted to horizontal/vertical wall lines. Every shared endpoint, overlap endpoint and T-junction becomes a break point. Only adjacent covered spans become walls."));
	Root->SetNumberField(TEXT("inputRoomCount"), Rooms.Num());
	Root->SetNumberField(TEXT("wallLineCount"), LinesByKey.Num());
	Root->SetNumberField(TEXT("uniqueWallSegmentCount"), Segments.Num());
	Root->SetNumberField(TEXT("uniquePillarCount"), PillarsByPoint.Num());
	Root->SetNumberField(TEXT("createdPillarCount"), CreatedPillars.Num());
	Root->SetNumberField(TEXT("createdWallCount"), CreatedWalls.Num());
	Root->SetNumberField(TEXT("createdSlabCount"), CreatedSlabs.Num());
	Root->SetBoolField(TEXT("foundationCreated"), Foundation != nullptr);
	Root->SetNumberField(TEXT("foundationThickness"), Foundation ? SafeFoundationThickness : 0.0f);
	Root->SetNumberField(TEXT("floorHeight"), SafeFloorHeight);
	Root->SetStringField(TEXT("floorHeightRule"), TEXT("FloorHeight is wall-base to next wall-base distance; slabs embed downward and must not add vertical gaps between floors."));
	Root->SetArrayField(TEXT("rooms"), RoomResults);
	Root->SetArrayField(TEXT("walls"), WallResults);

	FString Output;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
	FJsonSerializer::Serialize(Root, Writer);
	return Output;
}

FEHBToolsetOperationResult UEHBBuildingToolset::SetElementFloorAssignment(
	AEHBElementActorBase* Element,
	int32 FloorIndex,
	EEHBBuildingFloorElementRole FloorRole)
{
	FEHBToolsetOperationResult Result;
	AEHBBuildingActorBase* Building = ResolveEditableElement(Element);
	if (!Element || !Building)
	{
		Result.Message = TEXT("Element is null or does not belong to the currently selected EHB_Building.");
		return Result;
	}

	const bool bValidFoundation = FloorRole == EEHBBuildingFloorElementRole::Foundation && FloorIndex >= 0;
	const bool bValidRegular = FloorRole != EEHBBuildingFloorElementRole::None
		&& FloorRole != EEHBBuildingFloorElementRole::Foundation
		&& FloorIndex > 0;
	if (!bValidFoundation && !bValidRegular)
	{
		Result.Message = TEXT("Invalid floor assignment. Foundation uses FloorIndex>=0; regular building elements use FloorIndex>0 and a non-None, non-Foundation role.");
		return Result;
	}

	const FScopedTransaction Transaction(LOCTEXT("SetElementFloorAssignment", "Set EHB Element Floor Assignment"));
	Building->Modify();
	Element->Modify();
	Element->SetFloorAssignment(FloorIndex, FloorRole);
	Element->MarkPackageDirty();
	MarkBuildingChanged(Building);

	Result.bSucceeded = true;
	Result.Message = FString::Printf(
		TEXT("Element floor assignment updated. FloorIndex=%d FloorRole=%s"),
		Element->FloorIndex,
		*StaticEnum<EEHBBuildingFloorElementRole>()->GetNameStringByValue(static_cast<int64>(Element->FloorRole)));
	return Result;
}

FString UEHBBuildingToolset::GetFloorSummary(AEHBBuildingActorBase* Building)
{
	AEHBBuildingActorBase* SelectedBuilding = GetEditorSelectedBuilding();
	if (!SelectedBuilding)
	{
		return TEXT("{\"ok\":false,\"message\":\"No EHB_Building actor is selected. Select a building object before using this tool.\"}");
	}

	if (!Building)
	{
		Building = SelectedBuilding;
	}
	else if (Building != SelectedBuilding)
	{
		return TEXT("{\"ok\":false,\"message\":\"Requested building is not the currently selected EHB_Building.\"}");
	}

	Building->RebuildElementAndRelationshipIndexes();
	Building->RebuildClosedLoops();

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("ok"), true);
	Root->SetStringField(TEXT("building"), Building->GetPathName());
	Root->SetStringField(TEXT("guid"), Building->BuildingGuid.ToString(EGuidFormats::DigitsWithHyphensLower));

	TArray<int32> FloorIndices;
	Building->FloorElementsByIndex.GetKeys(FloorIndices);
	for (const FEHBBuildingClosedLoop& Loop : Building->ClosedLoops)
	{
		if (Loop.FloorIndex >= 0)
		{
			FloorIndices.AddUnique(Loop.FloorIndex);
		}
	}
	FloorIndices.Sort();

	TArray<TSharedPtr<FJsonValue>> Floors;
	for (const int32 FloorIndex : FloorIndices)
	{
		TSharedRef<FJsonObject> FloorObject = MakeShared<FJsonObject>();
		FloorObject->SetNumberField(TEXT("floorIndex"), FloorIndex);

		TArray<TSharedPtr<FJsonValue>> Elements;
		for (const FEHBBuildingFloorElementEntry& Entry : Building->GetFloorElementEntries(FloorIndex))
		{
			TSharedRef<FJsonObject> EntryObject = MakeShared<FJsonObject>();
			EntryObject->SetStringField(TEXT("guid"), Entry.ElementGuid.ToString(EGuidFormats::DigitsWithHyphensLower));
			EntryObject->SetStringField(TEXT("type"), StaticEnum<EEHBBuildingElementType>()->GetNameStringByValue(static_cast<int64>(Entry.ElementType)));
			EntryObject->SetStringField(TEXT("role"), StaticEnum<EEHBBuildingFloorElementRole>()->GetNameStringByValue(static_cast<int64>(Entry.FloorRole)));
			if (const AEHBElementActorBase* ElementActor = Building->FindElementActorByGuid(Entry.ElementGuid))
			{
				EntryObject->SetStringField(TEXT("actor"), ElementActor->GetPathName());
				EntryObject->SetStringField(TEXT("label"), ElementActor->GetActorLabel());
			}
			Elements.Add(MakeShared<FJsonValueObject>(EntryObject));
		}
		FloorObject->SetArrayField(TEXT("elements"), Elements);
		FloorObject->SetNumberField(TEXT("elementCount"), Elements.Num());

		TArray<TSharedPtr<FJsonValue>> Loops;
		for (const FEHBBuildingClosedLoop& Loop : Building->ClosedLoops)
		{
			if (Loop.FloorIndex != FloorIndex)
			{
				continue;
			}

			TSharedRef<FJsonObject> LoopObject = MakeShared<FJsonObject>();
			LoopObject->SetStringField(TEXT("guid"), Loop.LoopGuid.ToString(EGuidFormats::DigitsWithHyphensLower));
			LoopObject->SetNumberField(TEXT("area"), Loop.Area);
			LoopObject->SetNumberField(TEXT("wallCount"), Loop.WallGuids.Num());
			Loops.Add(MakeShared<FJsonValueObject>(LoopObject));
		}
		FloorObject->SetArrayField(TEXT("closedLoops"), Loops);
		FloorObject->SetNumberField(TEXT("closedLoopCount"), Loops.Num());
		Floors.Add(MakeShared<FJsonValueObject>(FloorObject));
	}
	Root->SetArrayField(TEXT("floors"), Floors);
	Root->SetNumberField(TEXT("floorCount"), Floors.Num());

	FString Output;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
	FJsonSerializer::Serialize(Root, Writer);
	return Output;
}

FString UEHBBuildingToolset::GetWallTopologySnapshot(AEHBBuildingActorBase* Building)
{
	AEHBBuildingActorBase* SelectedBuilding = GetEditorSelectedBuilding();
	if (!SelectedBuilding || (Building && Building != SelectedBuilding))
	{
		return TEXT("{\"ok\":false,\"message\":\"Select the target EHB_Building before reading topology.\"}");
	}
	const FEHBWallTopologySnapshot Snapshot = UEHBWallTopologyLibrary::CaptureWallTopology(SelectedBuilding);
	FString Json;
	if (!FJsonObjectConverter::UStructToJsonObjectString(Snapshot, Json))
	{
		return TEXT("{\"ok\":false,\"message\":\"Could not serialize topology snapshot.\"}");
	}
	return Json;
}

FString UEHBBuildingToolset::GetBuildingSnapshot(AEHBBuildingActorBase* Building)
{
	AEHBBuildingActorBase* SelectedBuilding = GetEditorSelectedBuilding();
	if (!SelectedBuilding)
	{
		return TEXT("{\"ok\":false,\"message\":\"No EHB_Building actor is selected. Select a building object before using this tool.\"}");
	}

	if (!Building)
	{
		Building = SelectedBuilding;
	}
	else if (Building != SelectedBuilding)
	{
		return TEXT("{\"ok\":false,\"message\":\"Requested building is not the currently selected EHB_Building.\"}");
	}

	Building->RebuildElementAndRelationshipIndexes();
	Building->RebuildClosedLoops();

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("ok"), true);
	Root->SetStringField(TEXT("building"), Building->GetPathName());
	Root->SetStringField(TEXT("label"), Building->GetActorLabel());
	Root->SetStringField(TEXT("guid"), Building->BuildingGuid.ToString(EGuidFormats::DigitsWithHyphensLower));
	Root->SetObjectField(TEXT("lastCommittedEdit"),FJsonObjectConverter::UStructToJsonObject(Building->LastCommittedEdit));
	Root->SetStringField(TEXT("location"), Building->GetActorLocation().ToString());
	Root->SetStringField(TEXT("coordinateSystem"), TEXT("All localLocation/localStart/localEnd/localTopPolygon values are in active Building local centimeters unless a field explicitly says World."));
	Root->SetNumberField(TEXT("closedLoopCount"), Building->ClosedLoops.Num());
	Root->SetNumberField(TEXT("relationCount"), Building->ElementRelations.Num());

	TArray<TSharedPtr<FJsonValue>> ClosedLoops;
	ClosedLoops.Reserve(Building->ClosedLoops.Num());
	for (const FEHBBuildingClosedLoop& Loop : Building->ClosedLoops)
	{
		ClosedLoops.Add(MakeShared<FJsonValueObject>(MakeClosedLoopJson(*Building, Loop)));
	}
	Root->SetArrayField(TEXT("closedLoops"), ClosedLoops);

	TArray<TSharedPtr<FJsonValue>> Elements;
	TMap<FString, int32> ElementTypeCounts;
	TArray<AActor*> AttachedActors;
	Building->GetAttachedActors(AttachedActors);
	for (AActor* Actor : AttachedActors)
	{
		if (const AEHBElementActorBase* Element = Cast<AEHBElementActorBase>(Actor))
		{
			const FString ElementType = StaticEnum<EEHBBuildingElementType>()->GetNameStringByValue(static_cast<int64>(Element->ElementType));
			++ElementTypeCounts.FindOrAdd(ElementType);
			Elements.Add(MakeShared<FJsonValueObject>(MakeElementJson(*Element)));
		}
	}
	Root->SetArrayField(TEXT("elements"), Elements);

	TSharedRef<FJsonObject> TypeCounts = MakeShared<FJsonObject>();
	for (const TPair<FString, int32>& Pair : ElementTypeCounts)
	{
		TypeCounts->SetNumberField(Pair.Key, Pair.Value);
	}
	Root->SetObjectField(TEXT("elementTypeCounts"), TypeCounts);

	FString Output;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
	FJsonSerializer::Serialize(Root, Writer);
	return Output;
}



FString UEHBBuildingToolset::PrepareTopologyMigration(AEHBBuildingActorBase* Building, bool bApply)
{
	AEHBBuildingActorBase* Selected = GetEditorSelectedBuilding();
	if (!Selected || (Building && Building != Selected))
		return TEXT("{\"bSucceeded\":false,\"status\":\"TargetNotSelected\"}");
	FEHBTopologyMigrationResult Result = UEHBWallTopologyLibrary::PrepareTopologyMigration(Selected, false);
	if (bApply && (!GEditor || GEditor->PlayWorld || GEditor->IsTransactionActive()))
	{
		Result.bSucceeded = false;
		Result.Status = TEXT("RequiresIndependentEditorTransaction");
	}
	else if (bApply && Result.bSucceeded && Result.Status == TEXT("Ready"))
	{
		const FScopedTransaction Transaction(LOCTEXT("InitializeTopologyBaseline", "Prepare Wall Railing Connections"));
		Selected->SetFlags(RF_Transactional);
		Result = UEHBWallTopologyLibrary::PrepareTopologyMigration(Selected, true);
	}
	FString Json;
	FJsonObjectConverter::UStructToJsonObjectString(Result, Json);
	return Json;
}

#undef LOCTEXT_NAMESPACE

FString UEHBBuildingToolset::PreviewWallSplitForRailing(AEHBBuildingActorBase* Building, FGuid WallGuid, float DistanceFromStart)
{
	AEHBBuildingActorBase* Selected = GetEditorSelectedBuilding();
	if (!Selected || (Building && Building != Selected))
		return TEXT("{\"bSucceeded\":false,\"bCommitAvailable\":false,\"status\":\"TargetNotSelected\"}");
	const FEHBWallSplitPreview Result = UEHBWallTopologyLibrary::PreviewWallSplitForRailing(Selected, WallGuid, DistanceFromStart);
	FString Json;
	FJsonObjectConverter::UStructToJsonObjectString(Result, Json);
	return Json;
}

namespace
{
// Both public commands own exactly one transaction. The continuation is internal
// and runs only after opening migration has succeeded, before baseline capture.
struct FEHBWallSplitRequest
{
 FGuid WallGuid;
 float DistanceFromStart;
 int32 ExpectedGraphRevision;
 FVector ExpectedStart, ExpectedEnd;
 float ExpectedHeight, ExpectedThickness;
};
struct FEHBPreparedWallSplit
{
 FEHBWallSplitRequest Request;
 AEHB_Wall* Source = nullptr;
 FEHBWallSplitPreview Preview;
 int32 RequestIndex=INDEX_NONE;
 FVector EffectiveSourceEnd=FVector::ZeroVector;
};
// All source plans are checked before any mutation. One transaction owns every split and continuation.
// Called only inside the enclosing split transaction after room/host preflight.
// The split returns a logical identity independently of its optional physical actor.
bool ApplyOptionalNodeWallSplit(AEHBBuildingActorBase* Building,AEHB_Wall* Source,float Distance,const FEHBWallSplitPreview& Preview,bool Physical,FGuid& OutNode,AEHB_Pillar*& OutPillar,TArray<AEHB_Wall*>& OutWalls)
{
 OutNode.Invalidate();OutPillar=nullptr;OutWalls.Reset();FEHBWallNodeModel Model;
 if(!UEHBWallTopologyLibrary::CaptureWallNodeModelSource(Building,Model).bSucceeded)return false;
 const auto* Found=Model.Walls.FindByPredicate([&](const auto& W){return W.WallGuid==Source->ElementGuid;});if(!Found)return false;
 const auto Original=*Found;Model.Walls.RemoveAll([&](const auto& W){return W.WallGuid==Original.WallGuid;});
 FEHBWallNodeDefinition Node;Node.NodeGuid=FGuid::NewGuid();Node.FloorIndex=Source->FloorIndex;
 Node.LocalTransform=FTransform((Source->LocalEnd-Source->LocalStart).Rotation(),Source->GetBuildingLocalLocationOnCenterAxisAtDistance(Distance,0));
 Node.JunctionDimensions=FVector(Preview.PillarWidth,Preview.PillarWidth,Preview.PillarHeight);
 const FGuid PlannedNode=Node.NodeGuid,PlannedPhysical=Physical?FGuid::NewGuid():FGuid();Model.Nodes.Add(Node);if(Physical)Model.PillarBindings.Add({PlannedNode,PlannedPhysical});
 auto First=Original,Second=Original;First.WallGuid=FGuid::NewGuid();Second.WallGuid=FGuid::NewGuid();First.EndNodeGuid=PlannedNode;Second.StartNodeGuid=PlannedNode;Model.Walls.Append({First,Second});
 TArray<FEHBWallJunctionWallSides> Sides;FName Status;if(!UEHBWallTopologyLibrary::BuildWallNodeModelSides(Model,Sides,Status))return false;
 AEHB_Pillar* Pillar=nullptr;FGuid ActualNode=PlannedNode;
 if(Physical)
 {
 Pillar=Building->CreatePillarAtLocalLocation(Node.LocalTransform.GetLocation(),Node.LocalTransform.Rotator(),Preview.PillarHeight,Preview.PillarWidth,Preview.PillarWidth,Node.FloorIndex,TEXT("EHB_SplitPillar"),false);
 if(!Pillar||!Pillar->GetRootComponent()||!Pillar->GetElementLocalTransform().Equals(Node.LocalTransform,1.e-8))return false;
 // Preserve the same authored local pose used by the pure split/subdivision plan.
 Pillar->GetRootComponent()->SetRelativeLocation_Direct(Node.LocalTransform.GetLocation());
 Pillar->GetRootComponent()->SetRelativeRotation_Direct(Node.LocalTransform.Rotator());Pillar->GetRootComponent()->UpdateComponentToWorld();
 Building->RegisterAuthoredWallNode(Pillar);Building->RecordAuthoredWallNode(Pillar);Pillar->SynchronizePlannedEditorMove();
 ActualNode=Building->FindNodeForPhysicalPillar(Pillar->ElementGuid);if(!ActualNode.IsValid())return false;
 }
 else
 {
  if(Building->WallNodeAuthority.Nodes.ContainsByPredicate([&](const auto& N){return N.NodeGuid==PlannedNode;}))return false;
  Building->Modify();Building->WallNodeAuthority.Nodes.Add(Node);
 }
 for(auto& N:Model.Nodes)if(N.NodeGuid==PlannedNode)N.NodeGuid=ActualNode;
 for(auto& P:Model.PillarBindings)if(P.NodeGuid==PlannedNode){P.NodeGuid=ActualNode;P.PhysicalPillarGuid=Pillar->ElementGuid;}
 for(auto& W:Model.Walls){if(W.StartNodeGuid==PlannedNode)W.StartNodeGuid=ActualNode;if(W.EndNodeGuid==PlannedNode)W.EndNodeGuid=ActualNode;}
 if(!UEHBWallTopologyLibrary::BuildWallNodeModelSides(Model,Sides,Status))return false;
 for(const FGuid Id:{First.WallGuid,Second.WallGuid})
 {
  const auto* Definition=Model.Walls.FindByPredicate([&](const auto& W){return W.WallGuid==Id;});const auto* Geometry=Sides.FindByPredicate([&](const auto& S){return S.WallGuid==Id;});
  if(!Definition||!Geometry)return false;auto* Wall=Building->CreateWallFromNodePlan(*Definition,*Geometry);if(!Wall)return false;OutWalls.Add(Wall);
 }
 Source->Modify();if(!Source->Destroy())return false;
 Building->RebuildElementAndRelationshipIndexes();if(!Building->RebuildWallNodeAuthorityGeometry())return false;
 OutNode=ActualNode;OutPillar=Pillar;return true;
}

FEHBToolsetOperationResult CommitWallSplitsWithContinuation(AEHBBuildingActorBase* Building,
 const TArray<FEHBWallSplitRequest>& Requests, bool bMigrateRectangularOpenings,
 const FText& TransactionLabel, FName Command, TFunctionRef<bool(const TArray<AEHB_Pillar*>&,FEHBRoomSubdivision&,const TMap<FGuid,FGuid>&)> ContinueAfterSplits,
 bool bPreviewOnly, bool bAllowExistingRailings, bool bInitializeMissingBaseline,
 bool bRequireSelectedBuilding, bool bConservativeScope, bool bAllowRoomSubdivision=false, const FEHBRoomSubdivision::FPath* RoomPath=nullptr,FEHBWallPathPreview* GeometryPreview=nullptr,bool bCreatePhysicalSplitNodes=true)
{
	FEHBToolsetOperationResult Result;
	auto Reject = [&](const TCHAR* Message) { Result.Message = Message; return Result; };
	if (!GEditor || GEditor->PlayWorld || GEditor->IsTransactionActive()) return Reject(TEXT("RequiresIndependentEditorTransaction"));
	if (bRequireSelectedBuilding)
 {
  AEHBBuildingActorBase* Selected = GetEditorSelectedBuilding();
  if (!Selected || (Building && Selected != Building)) return Reject(TEXT("TargetNotSelected"));
  Building = Selected;
 }
 if (!IsValid(Building) || Building->IsActorBeingDestroyed() || !Building->GetWorld()) return Reject(TEXT("InvalidBuilding"));
	if(Building->IsChangeNotificationBusy())return Reject(TEXT("BuildingChangePublicationBusy"));
	if (Building->GetClass() != AEHB_Building::StaticClass()) return Reject(TEXT("UnsupportedBuildingClass"));

 const bool Physical=RoomPath?RoomPath->Options.bCreatePhysicalColumns:bCreatePhysicalSplitNodes;
 if(!Physical&&Building->WallNodeAuthority.Version!=2)return Reject(TEXT("RequiresOptionalNodeAuthority"));
 if(Requests.IsEmpty())return Reject(TEXT("MissingWallSplitRequests"));
 TArray<FEHBPreparedWallSplit> Plans;
 TSet<FGuid> SourceGuids;
 TMap<FGuid, AEHB_DoorWindow*> TransferActors;
 TMap<FGuid, FEHBElementRelation> TransferRelations;
 TMap<FGuid, FEHBCutOperation> TransferCuts;
 TMap<FGuid, FGuid> TransferSourceGuids;
 for(const auto& Request:Requests)
 {
	AEHB_Wall* Source = Cast<AEHB_Wall>(Building->FindElementActorByGuid(Request.WallGuid));
	if (!Source || Source->IsActorBeingDestroyed()) return Reject(TEXT("MissingWall"));
	if (Request.ExpectedStart.ContainsNaN() || Request.ExpectedEnd.ContainsNaN() || !FMath::IsFinite(Request.ExpectedHeight) || !FMath::IsFinite(Request.ExpectedThickness)
		|| Building->RelationshipGraphRevision != Request.ExpectedGraphRevision
		|| !Source->LocalStart.Equals(Request.ExpectedStart, 0.001) || !Source->LocalEnd.Equals(Request.ExpectedEnd, 0.001)
		|| !FMath::IsNearlyEqual(Source->Height, Request.ExpectedHeight, 0.001f) || !FMath::IsNearlyEqual(Source->Thickness, Request.ExpectedThickness, 0.001f))
		return Reject(TEXT("StaleSource"));
	const auto Preview = UEHBWallTopologyLibrary::PreviewWallSplitForRailing(Building, Request.WallGuid, Request.DistanceFromStart);
	if (!Preview.bSucceeded) { Result.Message = Preview.Status.ToString(); return Result; }
	if (bMigrateRectangularOpenings && !SourceGuids.Contains(Request.WallGuid))
	{
		if (!Preview.bOpeningTransfersComplete) return Reject(TEXT("IncompleteOpeningTransfers"));
		for (const auto& Transfer : Preview.OpeningTransfers)
		{
			auto* Door = Cast<AEHB_DoorWindow>(Building->FindElementActorByGuid(Transfer.OpeningGuid));
			if (!Door || Door->FloorIndex != Source->FloorIndex) return Reject(TEXT("UnsupportedOpeningFloor"));
			const auto* Connection = Source->DoorWindowConnections.FindByPredicate([&](const auto& C) { return C.DoorWindowGuid == Transfer.OpeningGuid; });
			if (!Connection || Connection->Kind != Door->Kind || !FMath::IsFinite(Connection->OpeningThickness)
				|| !FMath::IsNearlyEqual(Connection->OpeningThickness, Door->OpeningThickness)) return Reject(TEXT("StaleOpeningRecord"));
			TransferActors.Add(Transfer.OpeningGuid, Door);
   TransferSourceGuids.Add(Transfer.OpeningGuid, Request.WallGuid);
		}
		for (const auto& Cut : Source->CutOperations)
		{
			const FGuid Id = Cut.Source.SourceElementGuid;
			if (!TransferActors.Contains(Id) || TransferCuts.Contains(Id) || !Cut.OperationGuid.IsValid()
				|| !Cut.bEnabled || Cut.OperationType != EEHBCutOperationType::Subtract
				|| Cut.Stage != EEHBCutStage::SurfaceOpening || Cut.ProjectionMode != EEHBCutProjectionMode::VerticalXZ
				|| Cut.TransformPolicy != EEHBCutTransformPolicy::SourceActorDriven || Cut.Priority != 100
				|| !Cut.OperationTag.IsNone() || Cut.Source.SourceType != EEHBCutSourceType::Element
				|| Cut.Source.SourceElement != TransferActors.FindRef(Id)) return Reject(TEXT("UnsupportedOpeningCut"));
			TransferCuts.Add(Id, Cut);
		}

	}

  SourceGuids.Add(Request.WallGuid);
  auto& Plan=Plans.AddDefaulted_GetRef();Plan.Request=Request;Plan.Source=Source;Plan.Preview=Preview;Plan.RequestIndex=Plans.Num()-1;
 }
 if(TransferCuts.Num()!=TransferActors.Num())return Reject(TEXT("MissingOpeningCut"));
 TArray<FEHBWallSplitIntervalRequest> IntervalRequests;TArray<FEHBWallSplitOpeningInterval> IntervalOpenings;
 for(const auto& Plan:Plans)IntervalRequests.Add({Plan.Request.WallGuid,Plan.Request.ExpectedStart,Plan.Request.ExpectedEnd,Plan.Request.DistanceFromStart,Plan.Preview.PillarWidth});
 for(const auto& Entry:TransferActors)IntervalOpenings.Add({TransferSourceGuids.FindChecked(Entry.Key),Entry.Key,Entry.Value->DistanceFromWallStart,Entry.Value->OpeningWidth});
 const auto Sequence=FEHBWallSplitIntervalPlanning::Build(IntervalRequests,IntervalOpenings);
 if(!Sequence.bSucceeded){Result.Message=Sequence.Status.ToString();return Result;}
 TArray<FEHBPreparedWallSplit> Ordered;
 for(const auto& Step:Sequence.Steps)
 {
  auto Plan=Plans[Step.RequestIndex];const auto OriginalTransfers=Plan.Preview.OpeningTransfers;Plan.Preview.OpeningTransfers.Reset();
  Plan.EffectiveSourceEnd=Step.EffectiveSourceEnd;
  for(const auto& Route:Step.Openings)
  {
   const auto* Original=OriginalTransfers.FindByPredicate([&](const auto& T){return T.OpeningGuid==Route.OpeningGuid;});
   if(!Original)return Reject(TEXT("MissingPlannedOpening"));
   auto& Transfer=Plan.Preview.OpeningTransfers.Add_GetRef(*Original);Transfer.bAfterPillar=Route.bAfter;
   Transfer.NewDistanceFromStart=Route.NewDistance;Transfer.ProposedWallLocalStart=Route.TargetStart;Transfer.ProposedWallLocalEnd=Route.TargetEnd;
  }
  Ordered.Add(MoveTemp(Plan));
 }
 Plans=MoveTemp(Ordered);
	const auto Baseline = UEHBWallTopologyLibrary::PrepareTopologyMigration(Building, false);
	if (Baseline.Status != FName(TEXT("AlreadyInitialized")) && !(bInitializeMissingBaseline && Baseline.Status == FName(TEXT("Ready"))))
	{
		Result.Message = Baseline.Status == FName(TEXT("Ready")) ? TEXT("RequiresCoherentBaseline") : Baseline.Status.ToString();
		return Result;
	}

	const auto* Settings = GetDefault<UEHBBuildingToolsetSettings>();
	if ((!Settings->PillarActorClass.IsNull() && Settings->PillarActorClass.LoadSynchronous() != AEHB_Pillar::StaticClass())
		|| (!Settings->WallActorClass.IsNull() && Settings->WallActorClass.LoadSynchronous() != AEHB_Wall::StaticClass()))
		return Reject(TEXT("UnsupportedSpawnClass"));
	// Inspect the whole conservative refresh scope, including actors absent from
	// cached indexes. No hosted objects, cuts or custom generation enter this path.
	TArray<AEHBElementActorBase*> Elements;
	TMap<FGuid, AEHB_Railing*> ExistingRailings;
	TArray<FEHBPreservedRailing> PreservedRailings;
 FEHBRoomSubdivision Subdivision;bool bHasRoomSubdivision=false;
 TArray<AEHBElementActorBase*> AllElements;for(TActorIterator<AEHBElementActorBase> It(Building->GetWorld());It;++It)if(It->OwningBuilding==Building&&!It->IsActorBeingDestroyed())AllElements.Add(*It);
 if((Building->WallNodeAuthority.Version==2&&(RoomPath||!Physical))||AllElements.ContainsByPredicate([](const auto* E){return E->IsA<AEHB_Floor>()||E->IsA<AEHB_FloorSlab>();}))
 {
  if(!bAllowRoomSubdivision)return Reject(TEXT("RequiresRoomSubdivisionPlan"));
  TArray<FEHBRoomSubdivision::FSplit> DependencySplits;for(const auto& P:Plans)DependencySplits.Add({P.Source,P.Preview,P.RequestIndex,Physical});
  FName Status;if(!Subdivision.PrepareMultiple(Building,DependencySplits,AllElements,Status,RoomPath))return Reject(*Status.ToString());
  bHasRoomSubdivision=true;
 }

	for (TActorIterator<AEHBElementActorBase> It(Building->GetWorld()); It; ++It)
	{
		auto* Element = *It;
		if (Element->OwningBuilding != Building || Element->IsActorBeingDestroyed()) continue;
		bool bSupported = (Element->CutOperations.IsEmpty() || (bMigrateRectangularOpenings && SourceGuids.Contains(Element->ElementGuid))) && Element->GetActorScale3D().Equals(FVector::OneVector)
			&& FMath::IsNearlyZero(Element->GetActorRotation().Pitch) && FMath::IsNearlyZero(Element->GetActorRotation().Roll);
		if (const auto* Pillar = Cast<AEHB_Pillar>(Element))
			bSupported &= Pillar->GetClass() == AEHB_Pillar::StaticClass() && Pillar->ShapeType == EEHBPillarShapeType::Polygon && Pillar->ConnectedSurfaceOverrides.IsEmpty();
		else if (const auto* Wall = Cast<AEHB_Wall>(Element))
			bSupported &= Wall->GetClass() == AEHB_Wall::StaticClass() && Wall->CurveControlOffset == 0 && (Wall->DoorWindowConnections.IsEmpty() || (bMigrateRectangularOpenings && SourceGuids.Contains(Wall->ElementGuid)))
				&& Wall->LeftSurfaceStyle.SourceType == EEHBWallSurfaceSourceType::Simple && Wall->RightSurfaceStyle.SourceType == EEHBWallSurfaceSourceType::Simple;
		else if (const auto* Door = Cast<AEHB_DoorWindow>(Element))
			bSupported &= bMigrateRectangularOpenings && TransferActors.FindRef(Door->ElementGuid) == Door;
		else if (auto* Railing = Cast<AEHB_Railing>(Element))
		{
			bSupported &= bAllowExistingRailings && FEHBPreservedRailing::Supports(Railing);
			if (bSupported) ExistingRailings.Add(Railing->ElementGuid, Railing);
		}
		else if(bHasRoomSubdivision && Subdivision.Contains(Element)){}
		else bSupported = false;
		if (!bSupported) return Reject(TEXT("RequiresDependencyMigration"));
		Elements.Add(Element);
	}

 for(int32 PlanIndex=0;PlanIndex<Plans.Num();++PlanIndex)
 {
  const auto& Preview=Plans[PlanIndex].Preview;const auto* Source=Plans[PlanIndex].Source;
	// Conservative plan-view clearance for the new square column. This can reject
	// close but valid arrangements; it must not place a column through a neighbour.
	const float NewRadius = Preview.PillarWidth * 0.707107f;
	for (auto* Element : Elements)
	{
		if (Element->FloorIndex != Source->FloorIndex) continue;
		if (const auto* Pillar = Cast<AEHB_Pillar>(Element))
		{
			const float Radius = FMath::Sqrt(FMath::Square(Pillar->Width) + FMath::Square(Pillar->Depth)) * 0.5f;
			if (FVector::Dist2D(Preview.LocalPillarPosition, Pillar->GetElementLocalTransform().GetLocation()) <= NewRadius + Radius)
				return Reject(TEXT("InsufficientColumnClearance"));
		}
		else if (const auto* Wall = Cast<AEHB_Wall>(Element); Wall && Wall != Source)
		{
			FVector Position = Preview.LocalPillarPosition;
			FVector Start = Wall->LocalStart;
			FVector End = Wall->LocalEnd;
			Position.Z = Start.Z = End.Z = 0;
			if (FMath::PointDistToSegment(Position, Start, End) <= NewRadius + Wall->Thickness * 0.5f)
				return Reject(TEXT("InsufficientWallClearance"));
		}
		else if (const auto* Railing = Cast<AEHB_Railing>(Element))
		{
			FVector A = Building->GetActorTransform().InverseTransformPosition(Railing->GetActorTransform().TransformPosition(Railing->LinearStart));
			FVector B = Building->GetActorTransform().InverseTransformPosition(Railing->GetActorTransform().TransformPosition(Railing->LinearEnd));
			FVector P = Preview.LocalPillarPosition; A.Z = B.Z = P.Z = 0;
			if (FMath::PointDistToSegment(P, A, B) <= NewRadius + FMath::Max(Railing->PostWidth, Railing->RailThickness) * 0.707107f)
				return Reject(TEXT("InsufficientRailingClearance"));
		}
	}

  for(int32 Other=0;Other<PlanIndex;++Other)
   if(Source->FloorIndex==Plans[Other].Source->FloorIndex && FVector::Dist2D(Preview.LocalPillarPosition,Plans[Other].Preview.LocalPillarPosition)<=NewRadius+Plans[Other].Preview.PillarWidth*0.707107f)
    return Reject(TEXT("InsufficientPlannedColumnClearance"));
 }
	for (const auto& Relation : Building->ElementRelations)
	{
		if (Relation.Type == EEHBElementRelationType::TopologyConnection && Relation.bEnabled) continue;
  if(bHasRoomSubdivision && Subdivision.FinishRelations.Contains(Relation.RelationGuid))continue;
		const FGuid Id = Relation.Target.ElementGuid;
		if (auto* Railing = ExistingRailings.FindRef(Id))
		{
			if (!FEHBPreservedRailing::MatchesRelation(Railing, Relation)
				|| PreservedRailings.ContainsByPredicate([&](const auto& P) { return P.Actor == Railing; })) return Reject(TEXT("UnplannedRailingRelation"));
			PreservedRailings.AddDefaulted_GetRef().Capture(Railing, Relation);
			continue;
		}
		if (!bMigrateRectangularOpenings || !TransferActors.Contains(Id) || TransferRelations.Contains(Id)
			|| Relation.Type != EEHBElementRelationType::HostedElement || !Relation.bEnabled
			|| Relation.Origin != EEHBRelationOrigin::SystemGenerated || !Relation.Source.RefersToElement(TransferSourceGuids.FindRef(Id))
			|| !Relation.Target.RefersToElement(Id) || !Relation.RelationGuid.IsValid()) return Reject(TEXT("UnplannedRelation"));
		TransferRelations.Add(Id, Relation);
	}
	if (PreservedRailings.Num() != ExistingRailings.Num()) return Reject(TEXT("MissingRailingRelation"));
	if (TransferRelations.Num() != TransferActors.Num()) return Reject(TEXT("MissingOpeningRelation"));
	if(Building->LastCommittedEdit.Sequence==MAX_int64)return Reject(TEXT("EditSequenceExhausted"));
	if (bPreviewOnly) { if(GeometryPreview&&bHasRoomSubdivision)Subdivision.ExportPreview(*GeometryPreview);Result.bSucceeded = true; Result.Message = TEXT("Ready"); return Result; }

 TArray<FGuid> AffectedNodes,AffectedRooms;
 if(bHasRoomSubdivision)for(FGuid Id:Subdivision.Rooms)AffectedRooms.AddUnique(Id);
 const auto BeforeGraph=UEHBWallTopologyLibrary::CaptureWallTopology(Building);
 for(const auto& Node:BeforeGraph.Nodes)
 {
  if(bConservativeScope || BeforeGraph.Walls.ContainsByPredicate([&](const auto& W){return SourceGuids.Contains(W.WallGuid)&&(W.StartNodeGuid==Node.NodeGuid||W.EndNodeGuid==Node.NodeGuid);}))AffectedNodes.AddUnique(Node.NodeGuid);
  if(bConservativeScope)for(const auto& Room:Building->GetClosedLoopsByPillarGuid(Node.SourcePillarGuid))AffectedRooms.AddUnique(Room.LoopGuid);
 }
 for(const auto& Plan:Plans)for(const auto& Room:Building->GetClosedLoopsByWallGuid(Plan.Request.WallGuid))AffectedRooms.AddUnique(Room.LoopGuid);
 FEHBChangeNotificationBatch Notifications(*Building);
 if(!Notifications.IsActive())return Reject(TEXT("BuildingChangePublicationBusy"));
 bool bApplied=true;FGuid NewPillarGuid;TArray<AEHB_Pillar*> InsertedPillars;InsertedPillars.SetNumZeroed(Plans.Num());TArray<FGuid> InsertedNodes;InsertedNodes.SetNum(Plans.Num());int32 CompletedSplits=0;
 TMap<FGuid,FGuid> SubdivisionIds;
 TMap<FGuid,AEHB_Wall*> CurrentSources;for(const auto& Plan:Plans)CurrentSources.Add(Plan.Request.WallGuid,Plan.Source);
 TMap<FGuid,FGuid> FinalOpeningHosts;TMap<FGuid,FTransform> OriginalOpeningPoses;
 for(const auto& Entry:TransferActors)OriginalOpeningPoses.Add(Entry.Key,Entry.Value->GetActorTransform());
 {
  FScopedTransaction Transaction(TransactionLabel);
  Building->SetFlags(RF_Transactional);Building->Modify();Building->GetLevel()->Modify();
  if(bInitializeMissingBaseline && Baseline.Status==FName(TEXT("Ready")))
  {Building->TopologyMigrationBaseline.Version=1;Building->TopologyMigrationBaseline.Nodes=BeforeGraph.Nodes;Building->TopologyMigrationBaseline.Walls=BeforeGraph.Walls;}
  for(auto* Element:Elements)
  {
   Element->SetFlags(RF_Transactional);Element->Modify();
   TInlineComponentArray<UActorComponent*> Components(Element);
   for(auto* Component:Components){Component->SetFlags(RF_Transactional);Component->Modify();}
  }
  if(bHasRoomSubdivision)bApplied=Subdivision.DetachRemovedHost(Building);
  for(auto& Plan:Plans)
  {
   if(!bApplied)break;
   auto* Source=CurrentSources.FindRef(Plan.Request.WallGuid);const auto& Preview=Plan.Preview;
   // Later cuts consume a new left remainder, never the deleted original source actor.
   if(!IsValid(Source)||Source->IsActorBeingDestroyed()||!Source->LocalStart.Equals(Plan.Request.ExpectedStart,0.001)
    ||!Source->LocalEnd.Equals(Plan.EffectiveSourceEnd,0.001)){bApplied=false;break;}
   const float DistanceFromStart=Plan.Request.DistanceFromStart;
   const auto LeftStyle=Source->LeftSurfaceStyle,RightStyle=Source->RightSurfaceStyle;
   const auto CapMaterial=Source->CapOverrideMaterial;const bool bCaps=Source->bGenerateLinkedPillarEndCaps;
   const int32 SourceFloor=Source->FloorIndex;const auto SourceRole=Source->FloorRole;
		Building->RebuildElementAndRelationshipIndexes();
		TArray<AEHB_Wall*> NewWalls;
		// Detach before deletion, which otherwise cascades to the source's windows.
		for (const auto& Transfer : Preview.OpeningTransfers) if(bMigrateRectangularOpenings) TransferActors.FindChecked(Transfer.OpeningGuid)->ClearWallBinding();
		AEHB_Pillar* Pillar=nullptr;FGuid NewNode;bool SplitApplied=false;
  if(Building->WallNodeAuthority.Version==2)SplitApplied=ApplyOptionalNodeWallSplit(Building,Source,DistanceFromStart,Preview,Physical,NewNode,Pillar,NewWalls);
  else {Pillar=Building->InsertPillarOnWall(Source,DistanceFromStart,Preview.PillarHeight,Preview.PillarWidth,NewWalls);if(Pillar)NewNode=Building->WallNodeOwnership.Version==1?Building->FindNodeForPhysicalPillar(Pillar->ElementGuid):Pillar->ElementGuid;SplitApplied=Pillar!=nullptr;}
		if (SplitApplied && NewNode.IsValid() && NewWalls.Num() == 2)
		{
			NewPillarGuid = Pillar?Pillar->ElementGuid:FGuid();
   InsertedPillars[Plan.RequestIndex]=Pillar;InsertedNodes[Plan.RequestIndex]=NewNode;
   ++CompletedSplits;CurrentSources.Add(Plan.Request.WallGuid,NewWalls[0]);if(bHasRoomSubdivision)
   {
    const auto& Slots=Subdivision.SplitSlots[CompletedSplits-1];
    SubdivisionIds.Add(Slots.Node,NewNode);if(Slots.Pillar.IsValid())SubdivisionIds.Add(Slots.Pillar,NewPillarGuid);
    SubdivisionIds.Add(Slots.Left,NewWalls[0]->ElementGuid);SubdivisionIds.Add(Slots.Right,NewWalls[1]->ElementGuid);
   }
			for (auto* Wall : NewWalls)
			{
				Wall->Modify();
				Wall->LeftSurfaceStyle = LeftStyle;
				Wall->RightSurfaceStyle = RightStyle;
				Wall->CapOverrideMaterial = CapMaterial;
				Wall->bGenerateLinkedPillarEndCaps = bCaps;
				Wall->SetFloorAssignment(SourceFloor, SourceRole);
				Wall->RebuildWallMesh();
			}
			bool bTransfersApplied = true;
			for (const auto& Transfer : Preview.OpeningTransfers)
			{
				if (!bMigrateRectangularOpenings) break;
				auto* Door = TransferActors.FindRef(Transfer.OpeningGuid);
				auto* Target = NewWalls[Transfer.bAfterPillar ? 1 : 0];
				if (!Door || Door->IsActorBeingDestroyed()
					|| !Target->LocalStart.Equals(Transfer.ProposedWallLocalStart, 0.001)
					|| !Target->LocalEnd.Equals(Transfer.ProposedWallLocalEnd, 0.001)) { bTransfersApplied = false; break; }
				Door->OwningWallGuid = Target->ElementGuid;
    FinalOpeningHosts.Add(Transfer.OpeningGuid,Target->ElementGuid);
				Door->DistanceFromWallStart = Transfer.NewDistanceFromStart;
				Target->AddOrUpdateDoorWindowConnection(Door, Transfer.NewDistanceFromStart);
				auto* NewCut = Target->CutOperations.FindByPredicate([&](const auto& Cut) { return Cut.Source.SourceElementGuid == Transfer.OpeningGuid; });
				if (!NewCut) { bTransfersApplied = false; break; }
				const auto& PreviousCut = TransferCuts.FindChecked(Transfer.OpeningGuid);
				NewCut->OperationGuid = PreviousCut.OperationGuid;
				for (int32 Index = 0; Index < NewCut->Source.ExplicitPolygon.Points.Num() && Index < PreviousCut.Source.ExplicitPolygon.Points.Num(); ++Index)
					NewCut->Source.ExplicitPolygon.Points[Index].PointGuid = PreviousCut.Source.ExplicitPolygon.Points[Index].PointGuid;
				auto Relation = TransferRelations.FindChecked(Transfer.OpeningGuid);
				Relation.Source.ElementGuid = Target->ElementGuid;
				Relation.TargetRelativeToSource = Transfer.PreservedWorldTransform.GetRelativeTransform(Target->GetActorTransform());
				Relation.NumericMetadata.Add(TEXT("DistanceFromWallStart"), Transfer.NewDistanceFromStart);
				bTransfersApplied &= Building->AddOrUpdateElementRelation(Relation, false) == Relation.RelationGuid;
				bTransfersApplied &= Door->GetActorTransform().Equals(Transfer.PreservedWorldTransform, 0.001);
				Door->MarkPackageDirty();
			}
			for (auto* Wall : NewWalls) Wall->RebuildWallMesh();
			for (const auto& Transfer : Preview.OpeningTransfers)
			{
				if (!bMigrateRectangularOpenings) break;
				const auto* Door = TransferActors.FindRef(Transfer.OpeningGuid);
				bTransfersApplied &= Door && !Door->IsActorBeingDestroyed()
					&& Door->OwningWallGuid == NewWalls[Transfer.bAfterPillar ? 1 : 0]->ElementGuid
					&& Door->GetActorTransform().Equals(Transfer.PreservedWorldTransform, 0.001);
			}

   bApplied=bTransfersApplied;
  }
  else bApplied=false;
  if(!bApplied)break;
#if WITH_DEV_AUTOMATION_TESTS
  if(EHBWallCreationCommand::FailAfterSplit==CompletedSplits)
  {EHBWallCreationCommand::FailAfterSplit=0;bApplied=false;break;}
#endif
  }
  if(bApplied)bApplied=ContinueAfterSplits(InsertedPillars,Subdivision,SubdivisionIds);
  // Final hosts follow the last applicable interval step, not an intermediate wall removed later.
  bApplied &= FinalOpeningHosts.Num()==TransferActors.Num();
  for(const auto& Entry:TransferActors)
  {
   const auto* Door=Entry.Value;
   bApplied &= IsValid(Door)&&!Door->IsActorBeingDestroyed()&&Door->OwningWallGuid==FinalOpeningHosts.FindRef(Entry.Key)
    && Door->GetActorTransform().Equals(OriginalOpeningPoses.FindChecked(Entry.Key),0.001);
  }
  for(const auto& Preserved:PreservedRailings)bApplied &= Preserved.IsPreserved(true);
  Building->RebuildClosedLoops();const auto Updated=UEHBWallTopologyLibrary::CaptureWallTopology(Building);
  bApplied &= Updated.Issues.IsEmpty() && CompletedSplits==Plans.Num();
  if(bApplied && bHasRoomSubdivision){FName Status;bApplied=Subdivision.Apply(Building,SubdivisionIds,Status,RoomPath);if(!bApplied)UE_LOG(LogTemp,Warning,TEXT("Room subdivision apply failed: %s"),*Status.ToString());}
  for(int32 I=0;bApplied && I<Plans.Num();++I)
   bApplied &= !Updated.Walls.ContainsByPredicate([&](const auto& W){return W.WallGuid==Plans[I].Request.WallGuid;})
    && Updated.Nodes.ContainsByPredicate([&](const auto& N){return N.NodeGuid==InsertedNodes[Plans[I].RequestIndex]&&N.LocalPosition.Equals(Plans[I].Preview.LocalPillarPosition,0.001);});
#if WITH_DEV_AUTOMATION_TESTS
  if(bApplied && EHBWallSplitTestHooks::ConsumeFailure(EHBWallSplitTestHooks::EFailurePhase::AfterGeometry))bApplied=false;
#endif
  if(bApplied)
  {
   Building->TopologyMigrationBaseline.Nodes=Updated.Nodes;Building->TopologyMigrationBaseline.Walls=Updated.Walls;Building->MarkPackageDirty();
#if WITH_DEV_AUTOMATION_TESTS
   if(EHBWallSplitTestHooks::ConsumeFailure(EHBWallSplitTestHooks::EFailurePhase::AfterBaseline))bApplied=false;
#endif
  }
  if(bApplied)
  {
   for(const auto& Node:Updated.Nodes)
    if(bConservativeScope || InsertedNodes.Contains(Node.NodeGuid))AffectedNodes.AddUnique(Node.NodeGuid);
   for(const auto& N:Updated.Nodes)if(InsertedNodes.Contains(N.NodeGuid))for(const auto& Room:Building->GetClosedLoopsByFloor(N.FloorIndex))AffectedRooms.AddUnique(Room.LoopGuid);
   bApplied=Notifications.RecordCommittedEdit(Command,AffectedNodes,AffectedRooms,Subdivision.Displays.AuthoredElements);
#if WITH_DEV_AUTOMATION_TESTS
   if(bApplied && EHBWallSplitTestHooks::ConsumeFailure(EHBWallSplitTestHooks::EFailurePhase::AfterEditRecord))bApplied=false;
#endif
  }
 }
 if(!bApplied)
 {
  const bool bRolledBack=GEditor->UndoTransaction(false);
  if(bRolledBack)Notifications.Rollback();else Notifications.Publish();
  return Reject(bRolledBack?TEXT("SplitFailedRolledBack"):TEXT("SplitFailedRollbackFailed"));
 }
 Result.CommittedEdit=Building->LastCommittedEdit;Notifications.Publish();
 Result.bSucceeded=true;Result.Message=Physical?FString::Printf(TEXT("Committed; PillarGuid=%s"),*NewPillarGuid.ToString()):FString::Printf(TEXT("Committed; NodeGuid=%s"),*InsertedNodes.Last().ToString());
 GEditor->RedrawLevelEditingViewports();return Result;
}

FEHBToolsetOperationResult CommitWallSplitWithContinuation(AEHBBuildingActorBase* Building, FGuid WallGuid,
 float DistanceFromStart, int32 ExpectedGraphRevision, FVector ExpectedStart, FVector ExpectedEnd,
 float ExpectedHeight, float ExpectedThickness, bool bMigrateRectangularOpenings,
 const FText& TransactionLabel, FName Command, TFunctionRef<bool(AEHB_Pillar*)> ContinueAfterSplit,
 bool bPreviewOnly=false, bool bAllowExistingRailings=false, bool bInitializeMissingBaseline=false,
 bool bRequireSelectedBuilding=true, bool bConservativeScope=false)
{
 return CommitWallSplitsWithContinuation(Building,{{WallGuid,DistanceFromStart,ExpectedGraphRevision,ExpectedStart,ExpectedEnd,ExpectedHeight,ExpectedThickness}},
  bMigrateRectangularOpenings,TransactionLabel,Command,[&](const auto& Pillars,FEHBRoomSubdivision&,const TMap<FGuid,FGuid>&){return ContinueAfterSplit(Pillars[0]);},
  bPreviewOnly,bAllowExistingRailings,bInitializeMissingBaseline,bRequireSelectedBuilding,bConservativeScope,true);
}

}

FEHBWallPathResult EHBWallCreationCommand::CommitFromWall(AEHBBuildingActorBase* Building,
 const FEHBWallCreationEndpoint& Start, const FEHBWallCreationEndpoint& End,
 const FEHBWallCreationOptions& Options, bool bPreviewOnly)
{
 if((Start.Wall!=nullptr)==(End.Wall!=nullptr))
 {FEHBWallPathResult Result;Result.Status=TEXT("RequiresSingleWallEndpoint");return Result;}
 return CommitAnchoredPath(Building,{Start,End},false,Options,bPreviewOnly);
}

FEHBWallPathResult EHBWallCreationCommand::CommitAnchoredPath(AEHBBuildingActorBase* Building,
 const TArray<FEHBWallCreationEndpoint>& Endpoints, bool bClosed, const FEHBWallCreationOptions& Options, bool bPreviewOnly,FEHBWallPathPreview* GeometryPreview)
{
 if(GeometryPreview)*GeometryPreview={};
 FEHBWallPathResult Result;
 auto Reject=[&](FName Reason){Result={};Result.Status=Reason;return Result;};
 if(!IsValid(Building) || Building->IsActorBeingDestroyed())return Reject(TEXT("InvalidBuilding"));
 if(Building->IsChangeNotificationBusy())return Reject(TEXT("BuildingChangePublicationBusy"));
 if(!Options.bCreatePhysicalColumns&&Building->WallNodeAuthority.Version!=2)return Reject(TEXT("RequiresOptionalNodeAuthority"));
 if(Endpoints.Num()<(bClosed?3:2))return Reject(TEXT("InvalidPath"));
 for(float Value:{Options.WallHeight,Options.WallThickness,Options.PillarHeight,Options.PillarWidth,Options.PillarDepth})
  if(!FMath::IsFinite(Value)||Value<1)return Reject(TEXT("InvalidDimensions"));
 if(Options.FreePillarLocalRotation.ContainsNaN())return Reject(TEXT("InvalidRotation"));
 const auto Transform=Building->GetActorTransform();auto Working=Endpoints;
 TArray<FEHBWallSplitRequest> Requests;TArray<int32> SourceSlots;TSet<FGuid> SourceGuids;
 for(int32 I=0;I<Working.Num();++I)
 {
  auto& Endpoint=Working[I];auto* Source=Endpoint.Wall;
  if(Source)
  {
   if(!IsValid(Source)||Source->IsActorBeingDestroyed()||Source->OwningBuilding!=Building||Endpoint.Pillar||Endpoint.NodeGuid.IsValid())return Reject(TEXT("StaleSource"));
   const auto Split=UEHBWallTopologyLibrary::PreviewWallSplitForRailing(Building,Source->ElementGuid,Endpoint.WallDistance);
   if(!Split.bSucceeded)return Reject(Split.Status);
   if(Endpoint.LocalLocation.ContainsNaN()||Endpoint.WorldLocation.ContainsNaN()
    ||!Endpoint.LocalLocation.Equals(Split.LocalPillarPosition,0.01)
    ||!Transform.InverseTransformPosition(Endpoint.WorldLocation).Equals(Split.LocalPillarPosition,0.01))return Reject(TEXT("StaleSourcePosition"));
   Requests.Add({Source->ElementGuid,Endpoint.WallDistance,Building->RelationshipGraphRevision,Source->LocalStart,Source->LocalEnd,Source->Height,Source->Thickness});
   SourceSlots.Add(I);SourceGuids.Add(Source->ElementGuid);
   Endpoint.Wall=nullptr;Endpoint.WallDistance=0;Endpoint.LocalLocation=Split.LocalPillarPosition;
   Endpoint.WorldLocation=Transform.TransformPosition(Endpoint.LocalLocation);Endpoint.FloorIndex=Source->FloorIndex;
  }
  else if(!Endpoint.Pillar)
  {
   if(Endpoint.LocalLocation.ContainsNaN()||Endpoint.WorldLocation.ContainsNaN()
    ||!Transform.InverseTransformPosition(Endpoint.WorldLocation).Equals(Endpoint.LocalLocation,0.001))return Reject(TEXT("EndpointFrameMismatch"));
   if(!Endpoint.NodeGuid.IsValid()&&Options.bSnapToIntegerBuildingCoordinates)Endpoint.LocalLocation=Building->RoundBuildingLocalCoordinates(Endpoint.LocalLocation);
   Endpoint.WorldLocation=Transform.TransformPosition(Endpoint.LocalLocation);
  }
 }
 if(Requests.IsEmpty())return Reject(TEXT("MissingWallSplitRequests"));
 auto NormalizedOptions=Options;NormalizedOptions.bSnapToIntegerBuildingCoordinates=false;
 const auto Route=FEHBWallPathPlanning::BuildForBuilding(Building,Working,bClosed,NormalizedOptions);
 if(!Route.bSucceeded)return Reject(Route.Status);
 const FName Command=bClosed?FName(TEXT("CreateAnchoredClosedWallPath"))
  :(Requests.Num()==1&&Endpoints.Num()==2?FName(TEXT("CreateWallFromWall")):FName(TEXT("CreateAnchoredWallPath")));
 FEHBRoomSubdivision::FPath RoomPath;RoomPath.Route=Route;RoomPath.Endpoints=Working;RoomPath.SourceEndpoints=SourceSlots;RoomPath.Options=NormalizedOptions;
 // Existing pillar endpoints use their authoritative local pose, as creation does.
 for(auto& E:RoomPath.Endpoints)if(E.Pillar)E.LocalLocation=E.Pillar->GetElementLocalTransform().GetLocation();
 RoomPath.ActualPoints.SetNum(Route.Points.Num());RoomPath.ActualWalls.SetNum(Route.Segments.Num());
 const auto Applied=CommitWallSplitsWithContinuation(Building,Requests,true,
  NSLOCTEXT("EHB","CreateAnchoredWallPath","Create Wall Path On Existing Walls"),Command,
  [&](const TArray<AEHB_Pillar*>& Inserted,FEHBRoomSubdivision& DependencyPlan,const TMap<FGuid,FGuid>& SplitIds)
  {
   if(Inserted.Num()!=SourceSlots.Num())return false;
   for(int32 I=0;I<Inserted.Num();++I)Working[SourceSlots[I]].Pillar=Inserted[I];
   if(Building->WallNodeAuthority.Version==2)
   {
    if(!DependencyPlan.MaterializePath(Building,RoomPath,Working,Result.Walls,Result.FailureReason,SplitIds))return false;
    Result.PrimaryWall=Result.Walls.IsEmpty()?nullptr:Result.Walls[0];Result.Endpoints=Working;return Result.PrimaryWall!=nullptr;
   }
   const int32 Edges=bClosed?Working.Num():Working.Num()-1;
   for(int32 I=0;I<Edges;++I)
   {
    FEHBWallCreationResult Created;
    if(!Building->CreateOrReuseWallSegment(Working[I],Working[(I+1)%Working.Num()],NormalizedOptions,Created))return false;
    auto ResolveGuid=[&](int32 Index)
    {
     const auto& Point=Route.Points[Index];if(Point.ExistingPillarGuid.IsValid())return Point.ExistingPillarGuid;
     return Working.IsValidIndex(Point.EndpointIndex)&&Working[Point.EndpointIndex].Pillar?Working[Point.EndpointIndex].Pillar->ElementGuid:FGuid();
    };
    int32 Expected=0;
    for(int32 J=0;J<Route.Segments.Num();++J)
    {
     if(Route.PathEdges[J]!=I)continue;++Expected;
     const auto Pair=Route.Segments[J];const auto X=ResolveGuid(Pair.X),Y=ResolveGuid(Pair.Y);
     if(!X.IsValid()||!Y.IsValid()||!Created.Walls.ContainsByPredicate([&](const auto* Wall)
      {return (Wall->StartPillarGuid==X&&Wall->EndPillarGuid==Y)||(Wall->StartPillarGuid==Y&&Wall->EndPillarGuid==X);}))return false;
    }
    for(int32 J=0;J<Route.Segments.Num();++J)if(Route.PathEdges[J]==I)
    {
     const auto Pair=Route.Segments[J];const auto X=ResolveGuid(Pair.X),Y=ResolveGuid(Pair.Y);
     const auto* Match=Created.Walls.FindByPredicate([&](const auto* W){return (W->StartPillarGuid==X&&W->EndPillarGuid==Y)||(W->StartPillarGuid==Y&&W->EndPillarGuid==X);});
     if(!Match)return false;RoomPath.ActualWalls[J]=(*Match)->ElementGuid;
    }
    if(Expected!=Created.Walls.Num())return false;
    for(auto* Wall:Created.Walls)Result.Walls.AddUnique(Wall);
    if(!Result.PrimaryWall)Result.PrimaryWall=Created.PrimaryWall;
#if WITH_DEV_AUTOMATION_TESTS
    if(FailAfterEdge==I+1){FailAfterEdge=0;return false;}
#endif
   }
   for(int32 I=0;I<Route.Points.Num();++I)
   {
    const auto& P=Route.Points[I];RoomPath.ActualPoints[I]=P.ExistingPillarGuid;
    if(!P.ExistingPillarGuid.IsValid()&&Working.IsValidIndex(P.EndpointIndex)&&Working[P.EndpointIndex].Pillar)RoomPath.ActualPoints[I]=Working[P.EndpointIndex].Pillar->ElementGuid;
   }
   Result.Endpoints=Working;return Result.PrimaryWall!=nullptr;
  },bPreviewOnly,true,true,false,true,true,&RoomPath,GeometryPreview);
 if(!Applied.bSucceeded)
 {
  const FName FailureReason=Result.FailureReason;
  auto Failed=Reject(FName(*Applied.Message));Failed.FailureReason=FailureReason;return Failed;
 }
 Result.bSucceeded=true;Result.Status=bPreviewOnly?TEXT("Ready"):TEXT("Committed");Result.CommittedEdit=Applied.CommittedEdit;return Result;
}

FEHBToolsetOperationResult UEHBBuildingToolset::CommitPlainWallSplit(AEHBBuildingActorBase* Building, FGuid WallGuid,
	float DistanceFromStart, int32 ExpectedGraphRevision, FVector ExpectedStart, FVector ExpectedEnd,
	float ExpectedHeight, float ExpectedThickness, bool bMigrateRectangularOpenings)
{
	return CommitWallSplitWithContinuation(Building, WallGuid, DistanceFromStart, ExpectedGraphRevision,
		ExpectedStart, ExpectedEnd, ExpectedHeight, ExpectedThickness, bMigrateRectangularOpenings,
		NSLOCTEXT("EHBBuildingToolset", "CommitPlainWallSplit", "Insert Building Column In Plain Wall"), TEXT("SplitWall"),
		[](AEHB_Pillar*) { return true; });
}

FEHBToolsetOperationResult UEHBBuildingToolset::CommitWallSplitAndRailing(AEHBBuildingActorBase* Building, FGuid WallGuid,
	float DistanceFromStart, int32 ExpectedGraphRevision, FVector ExpectedStart, FVector ExpectedEnd,
	float ExpectedHeight, float ExpectedThickness, FVector RailingWorldEnd,
	float RailingHeight, float RailingThickness, float PostSpacing, bool bPreviewOnly)
{
	FEHBToolsetOperationResult Result;
	auto Reject = [&](const TCHAR* Message) { Result.Message = Message; return Result; };
	auto* Selected = GetEditorSelectedBuilding();
	if (!Selected || (Building && Building != Selected)) return Reject(TEXT("TargetNotSelected"));
	Building = Selected;
	const auto Preview = UEHBWallTopologyLibrary::PreviewWallSplitForRailing(Building, WallGuid, DistanceFromStart);
	if (!Preview.bSucceeded) { Result.Message = Preview.Status.ToString(); return Result; }
	const FVector Start = Building->GetActorTransform().TransformPosition(Preview.LocalPillarPosition);
	const double Length = FVector::Dist2D(Start, RailingWorldEnd);
	if (RailingWorldEnd.ContainsNaN() || !FMath::IsFinite(RailingHeight) || !FMath::IsFinite(RailingThickness)
		|| !FMath::IsFinite(PostSpacing) || !FMath::IsFinite(Length)
		|| RailingHeight < 1 || RailingHeight > Preview.PillarHeight
		|| RailingThickness < 1 || RailingThickness > Preview.PillarWidth || PostSpacing < RailingThickness
		|| Length <= FMath::Max(10.0f, Preview.PillarWidth) || Length / PostSpacing > 2048
		|| !FMath::IsNearlyEqual(Start.Z, RailingWorldEnd.Z, 0.01)) return Reject(TEXT("InvalidHorizontalRailingDimensions"));
	const auto* Settings = GetDefault<UEHBBuildingToolsetSettings>();
	if (!Settings->RailingActorClass.IsNull() && Settings->RailingActorClass.LoadSynchronous() != AEHB_Railing::StaticClass())
		return Reject(TEXT("UnsupportedRailingClass"));
	const FVector LocalEnd = Building->GetActorTransform().InverseTransformPosition(RailingWorldEnd);
	const auto* SourceWall = Cast<AEHB_Wall>(Building->FindElementActorByGuid(WallGuid));
	if (!SourceWall) return Reject(TEXT("MissingWall"));
	const FVector Branch = (LocalEnd - Preview.LocalPillarPosition).GetSafeNormal2D();
	const FVector WallDirection = (ExpectedEnd - ExpectedStart).GetSafeNormal2D();
	const bool bNeedsWallCut = !EHBWallRailingJunction::ClearsRetainedWall(Branch, WallDirection,
		Preview.PillarWidth, SourceWall->Thickness, RailingThickness);
	const double FirstPostDistance = Length / FMath::Max(1, FMath::CeilToInt(Length / PostSpacing));
	if (bNeedsWallCut && !EHBWallRailingJunction::CanTrimToWallFace(Branch, WallDirection,
		Preview.PillarWidth, SourceWall->Thickness, RailingThickness, FirstPostDistance))
		return Reject(TEXT("RailingIntersectsSourceJunction"));
	FVector CutNormal = FVector::CrossProduct(FVector::UpVector, WallDirection);
	if (FVector::DotProduct(CutNormal, Branch) < 0) CutNormal *= -1;
	const FVector CutWorldNormal = Building->GetActorTransform().TransformVectorNoScale(CutNormal);
	const FVector CutWorldPoint = Start + CutWorldNormal * (SourceWall->Thickness * 0.5);
	for (TActorIterator<AEHBElementActorBase> It(Building->GetWorld()); It; ++It)
	{
		const auto* Element = *It;
		if (Element->OwningBuilding != Building || Element->IsActorBeingDestroyed() || Element->FloorIndex != SourceWall->FloorIndex) continue;
		if (const auto* Wall = Cast<AEHB_Wall>(Element); Wall && Wall->ElementGuid != WallGuid)
		{
			FVector A = Preview.LocalPillarPosition, B = LocalEnd, C = Wall->LocalStart, D = Wall->LocalEnd, P, Q;
			A.Z = B.Z = C.Z = D.Z = 0;
			FMath::SegmentDistToSegmentSafe(A, B, C, D, P, Q);
			if (FVector::Dist(P, Q) <= (Wall->Thickness + RailingThickness) * 0.5f) return Reject(TEXT("RailingIntersectsWall"));
		}
		else if (const auto* Pillar = Cast<AEHB_Pillar>(Element))
		{
			FVector A = Preview.LocalPillarPosition, B = LocalEnd, P = Pillar->GetElementLocalTransform().GetLocation();
			A.Z = B.Z = P.Z = 0;
			const float Radius = FMath::Sqrt(FMath::Square(Pillar->Width) + FMath::Square(Pillar->Depth)) * 0.5f;
			if (FMath::PointDistToSegment(P, A, B) <= Radius + RailingThickness * 0.5f) return Reject(TEXT("RailingIntersectsColumn"));
		}
		else if (const auto* Railing = Cast<AEHB_Railing>(Element))
		{
			FVector A = Preview.LocalPillarPosition, B = LocalEnd;
			FVector C = Building->GetActorTransform().InverseTransformPosition(Railing->GetActorTransform().TransformPosition(Railing->LinearStart));
			FVector D = Building->GetActorTransform().InverseTransformPosition(Railing->GetActorTransform().TransformPosition(Railing->LinearEnd));
			FVector P, Q; A.Z = B.Z = C.Z = D.Z = 0;
			FMath::SegmentDistToSegmentSafe(A, B, C, D, P, Q);
			// Keep separate branches outside the legacy 8 cm endpoint auto-join radius.
			const float Radius = FMath::Max(10.0f, (RailingThickness + FMath::Max(Railing->PostWidth, Railing->RailThickness)) * 0.707107f);
			if (FVector::Dist(P, Q) <= Radius) return Reject(TEXT("RailingIntersectsRailing"));
		}
	}
	FGuid RailingGuid;
	Result = CommitWallSplitWithContinuation(Building, WallGuid, DistanceFromStart, ExpectedGraphRevision,
		ExpectedStart, ExpectedEnd, ExpectedHeight, ExpectedThickness, true,
		NSLOCTEXT("EHBBuildingToolset", "SplitWallAndCreateRailing", "Create Railing From Wall With Building Column"), TEXT("SplitWallAndRailing"),
		[&](AEHB_Pillar* Pillar)
		{
			const FTransform WorldTransform((RailingWorldEnd - Start).Rotation(), (Start + RailingWorldEnd) * 0.5);
			FActorSpawnParameters Params;
			Params.Owner = Building;
			Params.OverrideLevel = Building->GetLevel();
			Params.ObjectFlags = RF_Transactional;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			auto* Railing = Building->GetWorld()->SpawnActor<AEHB_Railing>(AEHB_Railing::StaticClass(), WorldTransform, Params);
			if (!Railing) return false;
			Railing->Modify();
#if WITH_DEV_AUTOMATION_TESTS
			if (EHBWallSplitTestHooks::ConsumeFailure(EHBWallSplitTestHooks::EFailurePhase::AfterRailingSpawn)) return false;
#endif
			Railing->PostHeight = Railing->RailHeight = RailingHeight;
			Railing->RailThickness = Railing->PostWidth = RailingThickness;
			Railing->PostSpacing = Railing->MaxRailSegmentLength = PostSpacing;
			Railing->FillMode = EEHBRailingFillMode::PostsAndRails;
			Railing->bOmitStartPost = true;
			Railing->bOmitEndPost = false;
			const FVector RailStart = WorldTransform.InverseTransformPosition(Start);
			if (!Railing->ConfigureLinear(Building, WorldTransform.GetRelativeTransform(Building->GetActorTransform()),
				RailStart, WorldTransform.InverseTransformPosition(RailingWorldEnd), Pillar->FloorIndex)) return false;
			Railing->StartAnchor.ElementGuid = Pillar->ElementGuid;
			Railing->StartAnchor.LocalPoint = RailStart;
			Railing->bHasStartWallCut = bNeedsWallCut;
			Railing->StartWallCutPoint = WorldTransform.InverseTransformPosition(CutWorldPoint);
			Railing->StartWallCutNormal = WorldTransform.InverseTransformVectorNoScale(CutWorldNormal);
			Railing->RebuildRailing();
			RailingGuid = Railing->ElementGuid;
			Railing->MarkPackageDirty();
			return !Railing->IsActorBeingDestroyed() && !Railing->GeneratedPosts.IsEmpty()
				&& Building->ElementRelations.ContainsByPredicate([&](const auto& Relation)
				{
					return Relation.bEnabled && Relation.Type == EEHBElementRelationType::BoundaryAttachment
						&& Relation.Source.RefersToElement(Pillar->ElementGuid) && Relation.Target.RefersToElement(RailingGuid);
				});
		}, bPreviewOnly, true);
	if (Result.bSucceeded && !bPreviewOnly) Result.Message += FString::Printf(TEXT("; RailingGuid=%s"), *RailingGuid.ToString());
	return Result;
}

FString UEHBBuildingToolset::PreviewNodeMove(AEHBBuildingActorBase* Building, FGuid NodeGuid, FVector ExpectedPosition, FVector TargetPosition)
{
	AEHBBuildingActorBase* Selected = GetEditorSelectedBuilding();
	if (!Selected || (Building && Building != Selected))
		return TEXT("{\"bSucceeded\":false,\"status\":\"TargetNotSelected\"}");
	const FEHBNodeMovePreview Result = UEHBWallTopologyLibrary::PreviewNodeMove(Selected, NodeGuid, ExpectedPosition, TargetPosition);
	FString Json;
	FJsonObjectConverter::UStructToJsonObjectString(Result, Json);
	return Json;
}

namespace
{
const FName FixedSlabCommandKey(TEXT("EHBFixedSlabNodeMove"));
bool IsFixedSlabMoveRelation(const FEHBElementRelation& Relation, FGuid PillarGuid)
{
	return Relation.Type == EEHBElementRelationType::BoundaryAttachment
		&& Relation.Origin == EEHBRelationOrigin::SystemGenerated
		&& Relation.Target.Kind == EEHBRelationEndpointKind::BuildingElement
		&& Relation.Target.ElementGuid == PillarGuid
		&& Relation.StringMetadata.FindRef(FixedSlabCommandKey) == TEXT("1");
}

// Return the cyclic offset that retains every point and traversal direction.
int32 FindSameDirectionPolygonStart(const TArray<FVector>& Values,const TArray<FVector>& Reference)
{
 if(Values.Num()<3||Values.Num()!=Reference.Num())return INDEX_NONE;
 for(int32 Start=0;Start<Values.Num();++Start)
 {
  bool bSame=true;for(int32 I=0;bSame&&I<Values.Num();++I)bSame=Values[(I+Start)%Values.Num()].Equals(Reference[I],0.001);
  if(bSame)return Start;
 }
 return INDEX_NONE;
}
bool MatchesRoomCenterline(AEHBBuildingActorBase* Building, const FEHBBuildingClosedLoop& Room, TArray<FVector> Polygon)
{
	TArray<FVector> Boundary;
	for(FGuid Id:Room.PillarGuids)
	{
		const auto* Pillar=Building->FindElementActorByGuid(Id);if(!Pillar)return false;
		Boundary.Add(Pillar->GetElementLocalTransform().GetLocation());
	}
	auto Simplify=[](TArray<FVector>& Points)
	{
		for(auto& P:Points){if(P.ContainsNaN())return false;P.Z=0;}
		if(Points.Num()>1&&Points[0].Equals(Points.Last(),0.001))Points.Pop();
		bool Changed=true;
		while(Changed&&Points.Num()>3)
		{
			Changed=false;
			for(int32 I=0;I<Points.Num();++I)
			{
				const FVector A=Points[I]-Points[(I+Points.Num()-1)%Points.Num()],B=Points[(I+1)%Points.Num()]-Points[I];
				if(FVector::DotProduct(A,B)>0&&FMath::Abs(A.X*B.Y-A.Y*B.X)<=0.001*FMath::Max(A.Size2D()+B.Size2D(),1.0))
				{Points.RemoveAt(I);Changed=true;break;}
			}
		}
		return Points.Num()>=3;
	};
	if(!Simplify(Polygon)||!Simplify(Boundary)||Polygon.Num()!=Boundary.Num())return false;
	for(int32 First=0;First<Polygon.Num();++First)for(int32 Direction:{-1,1})
	{
		bool Matches=true;
		for(int32 I=0;I<Boundary.Num();++I)Matches&=Boundary[I].Equals(Polygon[(First+Direction*I+Polygon.Num())%Polygon.Num()],0.001);
		if(Matches)return true;
	}
	return false;
}

FEHBToolsetOperationResult CommitPlannedNodeMove(AEHBBuildingActorBase* Building, FGuid NodeGuid, FVector ExpectedPosition, FVector TargetPosition, AEHB_FloorSlab* FixedSlab = nullptr, FRotator WorldRotation = FRotator::ZeroRotator, bool bFollowRoomFloors = false, bool bPreviewOnly = false, const TArray<FEHBNodeMoveRequest>* BatchRequests = nullptr, AEHB_Wall* SelectedWallContext = nullptr)
{
	FEHBToolsetOperationResult Result;
	if (!GEditor || GEditor->PlayWorld || GEditor->IsTransactionActive())
	{
		Result.Message = TEXT("RequiresIndependentEditorTransaction");
		return Result;
	}
	AEHBBuildingActorBase* Selected = GetEditorSelectedBuilding();
	if (!Selected && SelectedWallContext && !SelectedWallContext->IsActorBeingDestroyed()
		&& GEditor->GetSelectedActors()->Num() == 1 && GEditor->GetSelectedActors()->IsSelected(SelectedWallContext))
		Selected = SelectedWallContext->OwningBuilding;
	if (!Selected || (Building && Building != Selected))
	{
		Result.Message = TEXT("TargetNotSelected");
		return Result;
	}
	Building = Selected;
	const FEHBNodeMovePreview Preview = BatchRequests ? UEHBWallTopologyLibrary::PreviewNodeMoves(Building, *BatchRequests)
		: UEHBWallTopologyLibrary::PreviewNodeMove(Building, NodeGuid, ExpectedPosition, TargetPosition);
	if (!Preview.bSucceeded || (!FixedSlab && !bFollowRoomFloors && !Preview.UnplannedElementGuids.IsEmpty()))
	{
		Result.Message = Preview.Status.ToString();
		return Result;
	}
	const auto Elements = Building->QueryElements(FEHBElementQuery());
	const auto* Node = Preview.ProposedTopology.Nodes.FindByPredicate([&](const auto& N) { return N.NodeGuid == NodeGuid; });
	const FGuid PillarGuid = Node ? Node->SourcePillarGuid : FGuid();
	AEHB_Pillar* MovedPillar = nullptr;
	TMap<FGuid, FVector> PlannedPillarPositions;
	for (const auto& Request : Preview.NodeMoves)
	{
		const auto* PlannedNode = Preview.ProposedTopology.Nodes.FindByPredicate([&](const auto& N) { return N.NodeGuid == Request.NodeGuid; });
		if (!PlannedNode || !PlannedNode->SourcePillarGuid.IsValid()) { Result.Message = TEXT("MissingLegacyPillar"); return Result; }
		PlannedPillarPositions.Add(PlannedNode->SourcePillarGuid, PlannedNode->LocalPosition);
	}
	TArray<AEHB_Pillar*> MovedPillars;
	struct FRoomFloorPlan
	{
		AEHB_Floor* Floor = nullptr;
		EEHBOutlineSource Source = EEHBOutlineSource::ManualOrUnclassified;
		TArray<FEHBFloorFinishRegion> Regions;
		TArray<FEHBElementRelation> Relations;
	};
	TArray<FRoomFloorPlan> FloorPlans;
	struct FRoomSlabPlan { AEHB_FloorSlab* Slab = nullptr; TArray<FVector> Polygon; };
	TArray<FRoomSlabPlan> SlabPlans;
	TArray<FEHBWallJunctionWallSides> CandidateRoomWallSides;
	bool bCandidateRoomSidesReady=false;
	TSet<FGuid> RequestedRoomWalls;
	if(bFollowRoomFloors)for(const auto& Change:Preview.RoomBoundaryChanges)if(!Change.BoundRoomSlabGuids.IsEmpty())
	{
		const auto Rooms=Building->GetClosedLoopsByFloor(Change.FloorIndex);
		const auto* Room=Rooms.FindByPredicate([&](const auto& R){return R.LoopGuid==Change.RoomGuid;});
		if(!Room){Result.Message=TEXT("MissingRoomBoundary");return Result;}
		for(FGuid Id:Room->WallGuids)RequestedRoomWalls.Add(Id);
	}
	TArray<FGuid> OriginalRoomIds;
	if (bFollowRoomFloors)
	{
		if (!Building->GetActorScale3D().Equals(FVector::OneVector, 0.0001)
			|| !FMath::IsNearlyZero(Building->GetActorRotation().Pitch) || !FMath::IsNearlyZero(Building->GetActorRotation().Roll))
		{ Result.Message = TEXT("UnsupportedBuildingTransform"); return Result; }
		for (const auto& Room : Building->GetClosedLoopsByFloor(1)) OriginalRoomIds.Add(Room.LoopGuid);
		TMap<FGuid,FGuid> RoomNodesByPillar;
		if (Building->WallNodeOwnership.Version == 1)
			for (const auto& Binding : Building->WallNodeOwnership.Bindings) RoomNodesByPillar.Add(Binding.PhysicalPillarGuid,Binding.NodeGuid);
		for (const auto& Change : Preview.RoomBoundaryChanges)
		{
			TArray<FGuid> BoundaryNodes;
			for (FGuid PillarId : Change.BoundaryPillarGuids)
				BoundaryNodes.Add(Building->WallNodeOwnership.Version == 1 ? RoomNodesByPillar.FindRef(PillarId) : PillarId);
			if (FEHBRoomIdentity::Make(Building->BuildingGuid, Change.FloorIndex, BoundaryNodes, Change.ProposedPolygon) != Change.RoomGuid)
			{ Result.Message = TEXT("RoomIdentityChangeUnsupported"); return Result; }
		}
	}
	for (auto* Element : Elements)
	{
		bool bSupported = Element && Element->CutOperations.IsEmpty();
		if (auto* Pillar = Cast<AEHB_Pillar>(Element))
		{
			bSupported &= Pillar->GetClass() == AEHB_Pillar::StaticClass() && Pillar->ShapeType == EEHBPillarShapeType::Polygon && Pillar->ConnectedSurfaceOverrides.IsEmpty();
			if (Pillar->ElementGuid == PillarGuid) MovedPillar = Pillar;
			if (PlannedPillarPositions.Contains(Pillar->ElementGuid)) MovedPillars.Add(Pillar);
		}
		else if (auto* Wall = Cast<AEHB_Wall>(Element))
		{
			bSupported &= Wall->GetClass() == AEHB_Wall::StaticClass() && Wall->CurveControlOffset == 0 && Wall->DoorWindowConnections.IsEmpty()
				&& Wall->LeftSurfaceStyle.SourceType == EEHBWallSurfaceSourceType::Simple && Wall->RightSurfaceStyle.SourceType == EEHBWallSurfaceSourceType::Simple;
		}
		else if (auto* Floor = Cast<AEHB_Floor>(Element))
		{
			FEHBBuildingClosedLoop Room;
			bSupported &= bFollowRoomFloors && Floor->GetClass() == AEHB_Floor::StaticClass()
				&& Floor->FloorIndex == 1 && Floor->RoomFloorIndex == 1 && Floor->TryGetRoomLoop(Room)
				&& Floor->IsRecordedOutlineUnchanged()
				&& Floor->GetElementLocalTransform().Equals(FTransform::Identity, 0.0001)
				&& Floor->FloorRegions.Num() == 1 && Floor->FloorRegions[0].Holes.IsEmpty();
			if (bSupported)
			{
				const auto& Polygon = Floor->FloorRegions[0].OuterPolygon;
				bSupported = !Polygon.IsEmpty() && !Polygon.ContainsByPredicate([&](const FVector& P){return FMath::Abs(P.Z-Polygon[0].Z)>0.001;})
					&& MatchesRoomCenterline(Building,Room,Polygon);
			}
			if (!bSupported) { Result.Message = TEXT("RoomFloorFollowUnsupported: unchanged planar full-room outline required"); return Result; }
   const auto* Change=Preview.RoomBoundaryChanges.FindByPredicate([&](const auto& C){return C.RoomGuid==Floor->RoomLoopGuid;});
   FRoomFloorPlan Plan;Plan.Floor=Floor;Plan.Source=Floor->OutlineSource;Plan.Regions=Floor->FloorRegions;
   if(Change)
   {
    if(Change->bHasStaleFloorBinding || !Change->BoundFloorFinishGuids.Contains(Floor->ElementGuid)){Result.Message=TEXT("StaleFloorBinding");return Result;}
    const double Z=Plan.Regions[0].OuterPolygon[0].Z;
    Plan.Regions[0].OuterPolygon=Change->ProposedPolygon;for(auto& P:Plan.Regions[0].OuterPolygon)P.Z=Z;
    if(!Floor->ValidateFloorRegions(Plan.Regions)){Result.Message=TEXT("InvalidProposedFloor");return Result;}
   }
   // An unchanged room may still gain/lose contact with a neighboring changed host.
   FloorPlans.Add(MoveTemp(Plan));
		}
		else if (auto* Slab = Cast<AEHB_FloorSlab>(Element))
		{
			if (bFollowRoomFloors)
			{
				bSupported &= Slab->GetClass()==AEHB_FloorSlab::StaticClass() && Slab->LocalHoles.IsEmpty() && Slab->PreviewCutters.IsEmpty()
					&& !Slab->bIsFoundation && !Slab->bHasAIFoundationSource && Slab->FloorIndex==1 && Slab->RoomFillFloorIndex==1
					&& Slab->FloorRole==EEHBBuildingFloorElementRole::FloorCeiling && Slab->OutlineSource==EEHBOutlineSource::RoomBoundary
					&& Slab->IsRecordedOutlineUnchanged();
				const auto Rooms=Building->GetClosedLoopsByFloor(1);
				const auto* Room=Rooms.FindByPredicate([&](const auto& R){return R.LoopGuid==Slab->RoomFillLoopGuid;});
				const FTransform LocalTransform=Slab->GetElementLocalTransform();
				TArray<FVector> CurrentPolygon;
				bSupported &= Room && FEasyHouseEditorMode::BuildRoomSlabFollowOutline(Slab,*Room,{},CurrentPolygon);
				bSupported &= CurrentPolygon.Num()==Slab->LocalTopPolygon.Num();
    const int32 SlabOutlineStart=bSupported?FindSameDirectionPolygonStart(CurrentPolygon,Slab->LocalTopPolygon):INDEX_NONE;
    bSupported &= SlabOutlineStart!=INDEX_NONE;
				if(!bSupported){Result.Message=TEXT("RoomSlabFollowUnsupported: unchanged full room ceiling outline required");return Result;}
				const auto* Change=Preview.RoomBoundaryChanges.FindByPredicate([&](const auto& C){return C.RoomGuid==Slab->RoomFillLoopGuid;});
				if(Change)
				{
					if(Change->bHasStaleSlabBinding || !Change->BoundRoomSlabGuids.Contains(Slab->ElementGuid))
					{Result.Message=TEXT("StaleSlabBinding");return Result;}
					FRoomSlabPlan Plan;Plan.Slab=Slab;
					if(!bCandidateRoomSidesReady)
					{
						if(!FEasyHouseEditorMode::BuildCandidateRoomWallSides(Building,PlannedPillarPositions,CandidateRoomWallSides,&RequestedRoomWalls))
						{Result.Message=TEXT("RoomSlabFollowJunctionSolveFailed");return Result;}
						bCandidateRoomSidesReady=true;
					}
					if(!FEasyHouseEditorMode::BuildRoomSlabOutlineFromWallSides(Slab,*Room,CandidateRoomWallSides,Plan.Polygon))
					{Result.Message=TEXT("RoomSlabFollowJunctionSolveFailed");return Result;}
     // Preserve the slab's stored control-point start while following the same room cycle.
     if(SlabOutlineStart>0)
     {
      const auto Polygon=Plan.Polygon;
      for(int32 I=0;I<Polygon.Num();++I)Plan.Polygon[I]=Polygon[(I+SlabOutlineStart)%Polygon.Num()];
     }
					if(!Slab->ValidateSlabOutline(Plan.Polygon,{})){Result.Message=TEXT("InvalidProposedSlab");return Result;}
					SlabPlans.Add(MoveTemp(Plan));
				}
			}
			else bSupported &= FixedSlab && Slab->GetClass() == AEHB_FloorSlab::StaticClass()
				&& Slab->LocalHoles.IsEmpty() && Slab->PreviewCutters.IsEmpty() && !Slab->bHasRoomFillAnchor
				&& !Slab->bIsFoundation && !Slab->bHasAIFoundationSource;
		}
		else bSupported = false;
		// Legacy snap inset assumes world centimetres for pillar dimensions.
		if (FixedSlab || bFollowRoomFloors) bSupported &= Element->GetActorScale3D().Equals(FVector::OneVector, 0.0001)
			&& FMath::IsNearlyZero(Element->GetActorRotation().Pitch) && FMath::IsNearlyZero(Element->GetActorRotation().Roll);
		if (!bSupported)
		{
			Result.Message = TEXT("UnsupportedGeometry: native simple pillars/walls and explicitly fixed plain slabs at unit scale are required.");
			return Result;
		}
	}
	for (const auto& Relation : Building->ElementRelations)
		if (Relation.bEnabled && Relation.Type != EEHBElementRelationType::TopologyConnection
   && !(bFollowRoomFloors && Relation.Type==EEHBElementRelationType::SurfaceFinish
    && FloorPlans.ContainsByPredicate([&](const auto& P){return P.Floor->SurfaceFinishRelationGuids.Contains(Relation.RelationGuid);} ))
			&& !(FixedSlab && IsFixedSlabMoveRelation(Relation, Relation.Target.ElementGuid)
				&& Relation.Source.Kind == EEHBRelationEndpointKind::BuildingElement
				&& Cast<AEHB_FloorSlab>(Building->FindElementActorByGuid(Relation.Source.ElementGuid))
				&& Cast<AEHB_Pillar>(Building->FindElementActorByGuid(Relation.Target.ElementGuid))
				&& !Relation.bAffectsFloorAssignment && !Relation.bGeometryDependent))
		{
			Result.Message = TEXT("UnplannedRelation");
			return Result;
		}
	if (!MovedPillar || MovedPillars.Num() != PlannedPillarPositions.Num())
	{
		Result.Message = TEXT("MissingLegacyPillar");
		return Result;
	}
	if (FixedSlab && !Preview.bWouldChange && MovedPillar->GetActorRotation().Equals(WorldRotation, 0.001))
	{
		TArray<const FEHBElementRelation*> Associations;
		for (const auto& Relation : Building->ElementRelations)
			if (IsFixedSlabMoveRelation(Relation, PillarGuid)) Associations.Add(&Relation);
		if (Associations.Num() == 1 && Associations[0]->bEnabled
			&& Associations[0]->Source.ElementGuid == FixedSlab->ElementGuid
			&& Associations[0]->TargetRelativeToSource.Equals(MovedPillar->GetActorTransform().GetRelativeTransform(FixedSlab->GetActorTransform()), 0.001))
		{
			Result.bSucceeded = true;
			Result.Message = TEXT("NoChange");
			return Result;
		}
	}
	if (!FixedSlab && !Preview.bWouldChange)
	{
		Result.bSucceeded = true;
		Result.Message = TEXT("NoChange");
		return Result;
	}
	// Definition drafts support planar poses at unit scale. Preserve the basic
	// command's legacy transform domain; fixed-slab yaw joins the same draft.
	const auto SupportsDefinitionTransform=[](const FTransform& Transform)
	{
		const FRotator Rotation=Transform.Rotator();
		return Transform.GetScale3D().Equals(FVector::OneVector,0.0001)
			&& FMath::IsNearlyZero(Rotation.Pitch) && FMath::IsNearlyZero(Rotation.Roll);
	};
	bool bUseDefinitionBatch=SupportsDefinitionTransform(Building->GetActorTransform());
	if(bUseDefinitionBatch)for(const auto* Element:Elements)
		if((Element->IsA<AEHB_Pillar>() || Element->IsA<AEHB_Wall>())
			&& !SupportsDefinitionTransform(Element->GetElementLocalTransform())) bUseDefinitionBatch=false;
	bUseDefinitionBatch |= bFollowRoomFloors || FixedSlab!=nullptr;
	FEHBWallNodeMoveDraft DefinitionDraft;
	if(bUseDefinitionBatch)
	{
		FEHBPreparedWallNodeDefinitions Source;const auto Capture=UEHBWallTopologyLibrary::CaptureNodeDefinitionSource(Building,Source);
		if(!Capture.bSucceeded){Result.Message=Capture.Status.ToString();return Result;}
		TArray<FEHBNodeRotationRequest> Rotations;
		if(FixedSlab)
		{
			const auto* SourceNode=Source.Nodes.FindByPredicate([&](const auto& N){return N.NodeGuid==NodeGuid;});
			if(!SourceNode){Result.Message=TEXT("MissingLegacyPillar");return Result;}
			auto& Rotation=Rotations.AddDefaulted_GetRef();Rotation.NodeGuid=NodeGuid;
			Rotation.ExpectedLocalRotation=SourceNode->LocalTransform.GetRotation();
			Rotation.TargetLocalRotation=Building->GetActorQuat().Inverse()*WorldRotation.Quaternion();
		}
		DefinitionDraft=UEHBWallTopologyLibrary::BuildNodeDefinitionMoveDraft(Source,Preview.NodeMoves,Rotations);
		if(!DefinitionDraft.bSucceeded){Result.Message=DefinitionDraft.Status.ToString();return Result;}
	}
 TMap<FGuid,TArray<FEHBFloorSupportSurface>> CandidateFinishTops;
 if(bFollowRoomFloors && !FloorPlans.IsEmpty())
 {
  FName Status;
  if(!EHBRoomFinishMove::BuildTopologyTops(DefinitionDraft.Definitions,CandidateFinishTops,Status)){Result.Message=Status.ToString();return Result;}
  for(const auto& Plan:SlabPlans)
  {
   FEHBFloorSupportSurface Top;
   for(auto P:Plan.Polygon){P.Z=Plan.Slab->GetTopZ();Top.OuterPolygon.Add(Plan.Slab->GetElementLocalTransform().TransformPosition(P));}
   CandidateFinishTops.Add(Plan.Slab->ElementGuid,{MoveTemp(Top)});
  }
  for(auto& Plan:FloorPlans)
  {
   FEHBBuildingClosedLoop Room;
   if(!Plan.Floor->TryGetRoomLoop(Room)||!Plan.Floor->BuildSurfaceFinishRelationPlan(Room,Plan.Regions,Plan.Relations,Status,&CandidateFinishTops))
   {Result.Message=Status.IsNone()?TEXT("MissingFinishRoom"):Status.ToString();return Result;}
  }
 }
	if(Building->LastCommittedEdit.Sequence==MAX_int64){Result.Message=TEXT("EditSequenceExhausted");return Result;}
	if (bPreviewOnly)
	{
		Result.bSucceeded = true;
		Result.Message = FString::Printf(TEXT("Ready; FollowingFloors=%d; FollowingSlabs=%d"), FloorPlans.Num(), SlabPlans.Num());
		return Result;
	}
	FEHBChangeNotificationBatch Notifications(*Building);
	if(!Notifications.IsActive()){Result.Message=TEXT("BuildingChangePublicationBusy");return Result;}
	bool bApplied = false;
	{
	// Snapshot the full conservative legacy refresh scope, including component
	// transforms and generated mesh arrays. Do not rely on actor Modify alone.
	FScopedTransaction Transaction(NSLOCTEXT("EHBBuildingToolset", "CommitBasicNodeMove", "Move EHB Topology Node"));
	Building->SetFlags(RF_Transactional);
	Building->Modify();
	for (auto* Element : Elements)
	{
		Element->SetFlags(RF_Transactional);
		Element->Modify();
		TInlineComponentArray<UActorComponent*> Components(Element);
		for (auto* Component : Components)
		{
			Component->SetFlags(RF_Transactional);
			Component->Modify();
		}
	}
	Building->RebuildElementAndRelationshipIndexes();
	// Register before moving, so failed endpoint validation cannot leave moved geometry.
	// This records an editing association, not physical support or floor assignment.
	if (FixedSlab)
	{
		FEHBElementRelation Association;
		Association.Type = EEHBElementRelationType::BoundaryAttachment;
		Association.Origin = EEHBRelationOrigin::SystemGenerated;
		Association.Source = FEHBElementRelationEndpoint::MakeElement(FixedSlab->ElementGuid, EEHBElementSurfaceKind::Perimeter);
		Association.Target = FEHBElementRelationEndpoint::MakeElement(PillarGuid, EEHBElementSurfaceKind::Bottom);
		Association.bAffectsFloorAssignment = false;
		Association.bGeometryDependent = false;
		Association.ContactPoint = TargetPosition;
		Association.StringMetadata.Add(FixedSlabCommandKey, TEXT("1"));
		FTransform PlannedWorld = MovedPillar->GetActorTransform();
		PlannedWorld.SetLocation(Building->GetActorTransform().TransformPosition(TargetPosition));
		PlannedWorld.SetRotation(WorldRotation.Quaternion());
		Association.TargetRelativeToSource = PlannedWorld.GetRelativeTransform(FixedSlab->GetActorTransform());
		TArray<FGuid> PreviousAssociations;
		for (const auto& Relation : Building->ElementRelations)
			if (IsFixedSlabMoveRelation(Relation, PillarGuid)) PreviousAssociations.Add(Relation.RelationGuid);
		const FGuid Added = Building->AddOrUpdateElementRelation(Association, false);
		if (!Added.IsValid())
		{
			Transaction.Cancel();
			Notifications.Rollback();
			Result.Message = TEXT("AssociationRejected");
			return Result;
		}
		for (FGuid Id : PreviousAssociations) Building->RemoveElementRelation(Id);
	}
	// Bypass a second automatic snap during application.
	// Apply every center before any legacy wall refresh, so refresh observes the final graph.
	if(bUseDefinitionBatch&&Building->HasWallNodeAuthority())
		Building->WallNodeAuthority.Nodes=DefinitionDraft.Definitions.Nodes;
	for (auto* Pillar : MovedPillars)
	{
		FTransform Transform = Pillar->GetElementLocalTransform();
		Transform.SetLocation(PlannedPillarPositions.FindChecked(Pillar->ElementGuid));
		if (FixedSlab) Transform.SetRotation(Building->GetActorQuat().Inverse() * WorldRotation.Quaternion());
		Pillar->SetActorRelativeTransform(Transform);
		if(!bUseDefinitionBatch)Building->RecordAuthoredWallNode(Pillar);
		if(!bUseDefinitionBatch)Pillar->RebuildPillarMesh();
	}
	TArray<FGuid> RefreshRoots;for(auto* Pillar:MovedPillars)RefreshRoots.Add(Pillar->ElementGuid);
	const auto RefreshStats=bUseDefinitionBatch
		?Building->RefreshNodeDefinitionDraft(DefinitionDraft,true)
		:Building->RefreshWallsConnectedToPillars(RefreshRoots,true);
#if WITH_DEV_AUTOMATION_TESTS
	EHBNodeMoveTestHooks::DefinitionBatchSolveCount+=RefreshStats.DefinitionSolveCalls;
#endif
	Building->RebuildClosedLoops();
	bApplied = RefreshStats.bSucceeded;
	if (bFollowRoomFloors)
	{
		const auto Rooms = Building->GetClosedLoopsByFloor(1);
		bApplied &= Rooms.Num() == OriginalRoomIds.Num();
		for (const auto& Room : Rooms) bApplied &= OriginalRoomIds.Contains(Room.LoopGuid);
		for (const auto& Plan : FloorPlans)
		{
			FEHBBuildingClosedLoop Room;
			if (!bApplied || !Plan.Floor->TryGetRoomLoop(Room) || !Plan.Floor->SetFloorRegions(Plan.Regions, false))
			{ bApplied = false; break; }
			Plan.Floor->RecordOutlineSource(Plan.Source);
			bApplied &= Plan.Floor->IsRecordedOutlineUnchanged();
		}
	}
#if WITH_DEV_AUTOMATION_TESTS
	if (bApplied && EHBNodeMoveTestHooks::ConsumeFailure(EHBNodeMoveTestHooks::EFailurePhase::AfterFloors)) bApplied = false;
#endif
	if(bApplied)for(const auto& Plan:SlabPlans)
	{
		const auto Rooms=Building->GetClosedLoopsByFloor(Plan.Slab->RoomFillFloorIndex);
		const auto* Room=Rooms.FindByPredicate([&](const auto& R){return R.LoopGuid==Plan.Slab->RoomFillLoopGuid;});
		TArray<FVector> Actual;
		bApplied=Room && FEasyHouseEditorMode::BuildRoomSlabFollowOutline(Plan.Slab,*Room,{},Actual) && Actual.Num()==Plan.Polygon.Num();
  if(bApplied)
  {
   const int32 Start=FindSameDirectionPolygonStart(Actual,Plan.Polygon);bApplied=Start!=INDEX_NONE;
   if(bApplied&&Start>0){const auto Polygon=Actual;for(int32 I=0;I<Actual.Num();++I)Actual[I]=Polygon[(I+Start)%Polygon.Num()];}
  }
		if(!bApplied||!Plan.Slab->SetSlabOutline(Actual,{})){bApplied=false;break;}
		Plan.Slab->RecordOutlineSource(EEHBOutlineSource::RoomBoundary);
		bApplied &= Plan.Slab->IsRecordedOutlineUnchanged();
	}
#if WITH_DEV_AUTOMATION_TESTS
	if(bApplied && EHBNodeMoveTestHooks::ConsumeFailure(EHBNodeMoveTestHooks::EFailurePhase::AfterSlabs))bApplied=false;
#endif
	for (auto* Pillar : MovedPillars) Pillar->NotifyElementGeometryChanged(true);
 if(bApplied && !CandidateFinishTops.IsEmpty())
 {
  FName Status;
  for(const auto& Pair:CandidateFinishTops)
  {
   TArray<FEHBFloorSupportSurface> Actual;
   if(!FEHBFloorContactGeometry::CaptureHorizontalTops(Building->FindElementActorByGuid(Pair.Key),Actual,Status)
    || !EHBRoomFinishMove::SameTopCoverage(Pair.Value,Actual,Status)){UE_LOG(LogTemp,Warning,TEXT("EHB finish host verification failed: %s (%s)"),*Pair.Key.ToString(),*Status.ToString());
 bApplied=false;break;}
  }
  if(bApplied)for(const auto& Plan:FloorPlans)
  {
   FEHBBuildingClosedLoop Room;TArray<FEHBElementRelation> Actual;
   if(!Plan.Floor->TryGetRoomLoop(Room)||!Plan.Floor->BuildSurfaceFinishRelationPlan(Room,Plan.Regions,Actual,Status)
    || Actual.Num()!=Plan.Relations.Num()){UE_LOG(LogTemp,Warning,TEXT("EHB finish relation count verification failed: %s"),*Status.ToString());bApplied=false;break;}
   for(const auto& Expected:Plan.Relations)
   {
    const auto* Found=Actual.FindByPredicate([&](const auto& R){return R.Source.IsEquivalentTo(Expected.Source)&&R.Target.IsEquivalentTo(Expected.Target);});
    if(!Found||FMath::Abs(Found->ContactArea-Expected.ContactArea)>0.01){UE_LOG(LogTemp,Warning,TEXT("EHB finish contact area verification failed"));bApplied=false;break;}
   }
   if(!bApplied||!Plan.Floor->TryRefreshSurfaceFinishRelationsFromRoomLoop(Room,Status)){bApplied=false;break;}
  }
 }
#if WITH_DEV_AUTOMATION_TESTS
 if(bApplied && EHBNodeMoveTestHooks::ConsumeFailure(EHBNodeMoveTestHooks::EFailurePhase::AfterContacts))bApplied=false;
#endif

	if (bApplied)
	{
		// Relative transforms in a yawed building can round-trip by a few ulps.
		// Validate against the plan, then persist the actual final coordinates;
		// baseline/source comparisons remain exact for detecting later edits.
		const auto FinalGraph = UEHBWallTopologyLibrary::CaptureWallTopology(Building);
		bApplied = FinalGraph.Issues.IsEmpty() && FinalGraph.Nodes.Num() == Preview.ProposedTopology.Nodes.Num()
			&& FinalGraph.Walls.Num() == Preview.ProposedTopology.Walls.Num();
		for (const auto& FinalNode : FinalGraph.Nodes)
		{
			const auto* Planned = Preview.ProposedTopology.Nodes.FindByPredicate([&](const auto& N) { return N.NodeGuid == FinalNode.NodeGuid; });
			bApplied &= Planned && Planned->SourcePillarGuid == FinalNode.SourcePillarGuid && Planned->FloorIndex == FinalNode.FloorIndex
				&& Planned->LocalPosition.Equals(FinalNode.LocalPosition, 0.001);
		}
		for (const auto& FinalWall : FinalGraph.Walls)
		{
			const auto* Planned = Preview.ProposedTopology.Walls.FindByPredicate([&](const auto& W) { return W.WallGuid == FinalWall.WallGuid; });
			bApplied &= Planned && Planned->StartNodeGuid == FinalWall.StartNodeGuid && Planned->EndNodeGuid == FinalWall.EndNodeGuid;
		}
		if (bApplied) Building->TopologyMigrationBaseline.Nodes = FinalGraph.Nodes;
	}
#if WITH_DEV_AUTOMATION_TESTS
	if (bApplied && EHBNodeMoveTestHooks::ConsumeFailure(EHBNodeMoveTestHooks::EFailurePhase::AfterBaseline)) bApplied = false;
#endif
	if(bApplied)
	{
		TArray<FGuid> Rooms;
		for(const auto& Change:Preview.RoomBoundaryChanges)Rooms.AddUnique(Change.RoomGuid);
		for(const auto& Plan:FloorPlans)Rooms.AddUnique(Plan.Floor->RoomLoopGuid);
		for(const auto& Plan:SlabPlans)Rooms.AddUnique(Plan.Slab->RoomFillLoopGuid);
		bApplied=Notifications.RecordCommittedEdit(SelectedWallContext?TEXT("MoveWall"):FixedSlab?TEXT("MoveNodeOnSlab"):bFollowRoomFloors?TEXT("MoveNodeWithRoomFloors"):TEXT("MoveNode"),Preview.UpdatePlan.JunctionNodeGuids,Rooms);
	}
#if WITH_DEV_AUTOMATION_TESTS
	if(bApplied&&EHBNodeMoveTestHooks::ConsumeFailure(EHBNodeMoveTestHooks::EFailurePhase::AfterEditRecord))bApplied=false;
#endif
	for (auto* Element : Elements) Element->MarkPackageDirty();
	Building->MarkPackageDirty();
	} // Finish recording before production recovery; failed edits must not be redoable.
	if (!bApplied)
	{
		const bool bRolledBack=GEditor->UndoTransaction(false);
		if(bRolledBack)Notifications.Rollback();else Notifications.Publish();
		Result.Message = bRolledBack ? TEXT("NodeMoveFailedRolledBack") : TEXT("NodeMoveFailedRollbackFailed");
		return Result;
	}
	Result.CommittedEdit=Building->LastCommittedEdit;
	Notifications.Publish();
	GEditor->RedrawLevelEditingViewports();
	Result.bSucceeded = true;
	Result.Message = TEXT("Committed");
	return Result;
}
} // namespace

FString UEHBBuildingToolset::PrepareWallNodeDefinitions(AEHBBuildingActorBase* Building,bool bApply)
{
	auto* Selected=GetEditorSelectedBuilding();
	if(!Selected||(Building&&Building!=Selected))return TEXT("{\"bSucceeded\":false,\"status\":\"TargetNotSelected\"}");
	auto Result=UEHBWallTopologyLibrary::PrepareWallNodeDefinitions(Selected,false);
	if(bApply&&(!GEditor||GEditor->PlayWorld||GEditor->IsTransactionActive()))
	{Result.bSucceeded=false;Result.Status=TEXT("RequiresIndependentEditorTransaction");}
	else if(bApply&&Result.bSucceeded&&Result.Status==TEXT("Ready"))
	{
		const FScopedTransaction Transaction(NSLOCTEXT("EHBNodes","PrepareDefinitions","Prepare Wall Node Definitions"));
		Selected->SetFlags(RF_Transactional);Result=UEHBWallTopologyLibrary::PrepareWallNodeDefinitions(Selected,true);
	}
	FString Json;FJsonObjectConverter::UStructToJsonObjectString(Result,Json);return Json;
}

FString UEHBBuildingToolset::PreviewPillarSeparation(AEHBBuildingActorBase* Building,FGuid PillarGuid)
{
	auto* Selected=GetEditorSelectedBuilding();
	if(!Selected||(Building&&Building!=Selected))return TEXT("{\"bSucceeded\":false,\"status\":\"TargetNotSelected\"}");
	const auto Result=UEHBWallTopologyLibrary::PreviewPillarSeparation(Selected,PillarGuid);
	FString Json;FJsonObjectConverter::UStructToJsonObjectString(Result,Json);return Json;
}

FString UEHBBuildingToolset::PreviewNodeMoves(AEHBBuildingActorBase* Building, const TArray<FEHBNodeMoveRequest>& Requests)
{
	auto* Selected = GetEditorSelectedBuilding();
	if (!Selected || (Building && Building != Selected)) return TEXT("{\"bSucceeded\":false,\"status\":\"TargetNotSelected\"}");
	const auto Result = UEHBWallTopologyLibrary::PreviewNodeMoves(Selected, Requests);
	FString Json; FJsonObjectConverter::UStructToJsonObjectString(Result, Json); return Json;
}

FEHBToolsetOperationResult UEHBBuildingToolset::CommitBasicNodeMove(AEHBBuildingActorBase* Building, FGuid NodeGuid, FVector ExpectedPosition, FVector TargetPosition)
{
	if(Building&&Building->WallNodeAuthority.Version==2)return CommitNodeMoveWithRoomFloors(Building,NodeGuid,ExpectedPosition,TargetPosition,false);
	return CommitPlannedNodeMove(Building, NodeGuid, ExpectedPosition, TargetPosition);
}

FEHBWallPathResult EHBWallCreationCommand::InsertColumnOnWall(AEHBBuildingActorBase* Building,AEHB_Wall* Wall,float Distance,float ColumnHeight,float ColumnWidth,bool bPreviewOnly)
{return InsertNodeOnWall(Building,Wall,Distance,ColumnHeight,ColumnWidth,true,bPreviewOnly);}

FEHBWallPathResult EHBWallCreationCommand::InsertNodeOnWall(AEHBBuildingActorBase* Building,AEHB_Wall* Wall,float Distance,float ColumnHeight,float ColumnWidth,bool bCreatePhysicalColumn,bool bPreviewOnly)
{
 FEHBWallPathResult Result;
 if(!Building||!IsValid(Wall)||Wall->IsActorBeingDestroyed()||Wall->OwningBuilding!=Building){Result.Status=TEXT("InvalidSourceWall");return Result;}
 // Wider/taller columns change room support domains and need an explicit dimension plan.
 if(!FMath::IsNearlyEqual(ColumnHeight,Wall->Height)||!FMath::IsNearlyEqual(ColumnWidth,Wall->Thickness)){Result.Status=TEXT("WallColumnDimensionsRequirePlan");return Result;}
 const auto Applied=CommitWallSplitsWithContinuation(Building,{{Wall->ElementGuid,Distance,Building->RelationshipGraphRevision,Wall->LocalStart,Wall->LocalEnd,Wall->Height,Wall->Thickness}},false,
  NSLOCTEXT("EHBBuildingToolset","ClickInsertNode","Insert Wall Control Node"),TEXT("SplitWallByClick"),[&](const TArray<AEHB_Pillar*>& Pillars,FEHBRoomSubdivision& Plan,const TMap<FGuid,FGuid>& Ids)
  {
   if(Pillars.Num()!=1)return false;auto* Pillar=Pillars[0];auto& Endpoint=Result.Endpoints.AddDefaulted_GetRef();Endpoint.Pillar=Pillar;
   if(Pillar){Endpoint.NodeGuid=Building->FindNodeForPhysicalPillar(Pillar->ElementGuid);Endpoint.LocalLocation=Pillar->GetElementLocalTransform().GetLocation();Endpoint.FloorIndex=Pillar->FloorIndex;}
   else {if(Plan.SplitSlots.Num()!=1)return false;Endpoint.NodeGuid=Ids.FindRef(Plan.SplitSlots[0].Node);}
   if(Building->WallNodeAuthority.Version==2){const auto* N=Building->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==Endpoint.NodeGuid;});if(!N)return false;Endpoint.LocalLocation=N->LocalTransform.GetLocation();Endpoint.ExpectedNodeRevision=N->GeometryRevision;Endpoint.FloorIndex=N->FloorIndex;}
   Endpoint.WorldLocation=Building->GetActorTransform().TransformPosition(Endpoint.LocalLocation);return true;
  },bPreviewOnly,true,true,false,true,true,nullptr,nullptr,bCreatePhysicalColumn);
 if(!Applied.bSucceeded){Result.Endpoints.Reset();Result.Status=FName(*Applied.Message);return Result;}
 Result.bSucceeded=true;Result.Status=bPreviewOnly?TEXT("Ready"):TEXT("Committed");Result.CommittedEdit=Applied.CommittedEdit;return Result;
}

FEHBToolsetOperationResult UEHBBuildingToolset::CommitWallNodeMoves(AEHBBuildingActorBase* Building,const TArray<FEHBVersionedNodeMoveRequest>& Requests,bool bPreviewOnly)
{
	TArray<FEHBNodeMoveRequest> Moves;TMap<FGuid,int32> Revisions;
	for(const auto& R:Requests){auto& M=Moves.AddDefaulted_GetRef();M.NodeGuid=R.NodeGuid;M.ExpectedPosition=R.ExpectedPosition;M.TargetPosition=R.TargetPosition;Revisions.Add(R.NodeGuid,R.ExpectedRevision);}
	return CommitWallNodeMovesNative(Building,Moves,Revisions,bPreviewOnly);
}

FEHBToolsetOperationResult UEHBBuildingToolset::CommitWallNodeMovesNative(AEHBBuildingActorBase* Building,const TArray<FEHBNodeMoveRequest>& Requests,const TMap<FGuid,int32>& ExpectedNodeRevisions,bool bPreviewOnly)
{
	return EHBNodeAuthorityEditing::ExecuteMoves(Building,Requests,ExpectedNodeRevisions,bPreviewOnly);
}

FEHBToolsetOperationResult UEHBBuildingToolset::CommitNodeMoveWithRoomFloors(AEHBBuildingActorBase* Building, FGuid NodeGuid, FVector ExpectedPosition, FVector TargetPosition, bool bPreviewOnly)
{
	if(Building&&Building->WallNodeAuthority.Version==2)
	{
		FEHBNodeMoveRequest R;R.NodeGuid=NodeGuid;R.ExpectedPosition=ExpectedPosition;R.TargetPosition=TargetPosition;TMap<FGuid,int32> Revisions;
		if(const auto* N=Building->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==NodeGuid;}))Revisions.Add(NodeGuid,N->GeometryRevision);
		return EHBNodeAuthorityEditing::ExecuteMoves(Building,{R},Revisions,bPreviewOnly);
	}
	return CommitPlannedNodeMove(Building, NodeGuid, ExpectedPosition, TargetPosition, nullptr, FRotator::ZeroRotator, true, bPreviewOnly);
}

FEHBToolsetOperationResult UEHBBuildingToolset::CommitWallMoveWithRoomFloors(AEHBBuildingActorBase* Building, FGuid WallGuid,
	FVector ExpectedStartPillarPosition, FVector ExpectedEndPillarPosition, FVector LocalDelta, bool bPreviewOnly)
{
	FEHBToolsetOperationResult Result;
	auto* Selected = GetEditorSelectedBuilding();
	AEHB_Wall* SelectedWall = nullptr;
	if (!Selected && GEditor && GEditor->GetSelectedActors()->Num() == 1)
		for (FSelectionIterator It(*GEditor->GetSelectedActors()); It; ++It)
			if (auto* Candidate = Cast<AEHB_Wall>(*It); Candidate && Candidate->ElementGuid == WallGuid && !Candidate->IsActorBeingDestroyed())
			{ SelectedWall = Candidate; Selected = Candidate->OwningBuilding.Get(); }
	if (!Selected || (Building && Building != Selected)) { Result.Message = TEXT("TargetNotSelected"); return Result; }
	Building = Selected;
	if (LocalDelta.ContainsNaN() || !FMath::IsNearlyZero(LocalDelta.Z)) { Result.Message = TEXT("InvalidHorizontalDelta"); return Result; }
	LocalDelta.Z = 0;
	const auto Graph = UEHBWallTopologyLibrary::CaptureWallTopology(Building);
	const auto* Wall = Graph.Walls.FindByPredicate([&](const auto& W) { return W.WallGuid == WallGuid; });
	if (!Wall) { Result.Message = TEXT("UnknownWall"); return Result; }
	TArray<FEHBNodeMoveRequest> Requests; Requests.SetNum(2);
	Requests[0].NodeGuid = Wall->StartNodeGuid; Requests[0].ExpectedPosition = ExpectedStartPillarPosition;
	Requests[0].TargetPosition = ExpectedStartPillarPosition + LocalDelta;
	Requests[1].NodeGuid = Wall->EndNodeGuid; Requests[1].ExpectedPosition = ExpectedEndPillarPosition;
	Requests[1].TargetPosition = ExpectedEndPillarPosition + LocalDelta;
	if(Building->WallNodeAuthority.Version==2)
	{
		TMap<FGuid,int32> Revisions;for(const auto& R:Requests)if(const auto* N=Building->WallNodeAuthority.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==R.NodeGuid;}))Revisions.Add(R.NodeGuid,N->GeometryRevision);
		return EHBNodeAuthorityEditing::ExecuteMoves(Building,Requests,Revisions,bPreviewOnly,TEXT("MoveWallWithRoomFloors"));
	}
	return CommitPlannedNodeMove(Building, Wall->StartNodeGuid, ExpectedStartPillarPosition, Requests[0].TargetPosition,
		nullptr, FRotator::ZeroRotator, true, bPreviewOnly, &Requests, SelectedWall);
}

FEHBToolsetOperationResult UEHBBuildingToolset::CommitNodeMoveOnFixedSlab(AEHBBuildingActorBase* Building, FGuid NodeGuid, FVector ExpectedPosition, FRotator ExpectedWorldRotation, FVector RequestedPosition, FGuid ExpectedSlabGuid, FVector ExpectedSnapWorldPosition, FRotator ExpectedSnapWorldRotation, float MaxSnapDistance)
{
	FEHBToolsetOperationResult Result;
	auto Reject = [&](const TCHAR* Message) { Result.Message = Message; return Result; };
	auto* Selected = GetEditorSelectedBuilding();
	if (!Selected || (Building && Building != Selected)) return Reject(TEXT("TargetNotSelected"));
	Building = Selected;
	if (RequestedPosition.ContainsNaN() || ExpectedSnapWorldPosition.ContainsNaN() || ExpectedWorldRotation.ContainsNaN()
		|| ExpectedSnapWorldRotation.ContainsNaN() || !ExpectedSlabGuid.IsValid() || !FMath::IsFinite(MaxSnapDistance) || MaxSnapDistance < 0)
		return Reject(TEXT("InvalidInput"));
	if (!Building->GetActorScale3D().Equals(FVector::OneVector, 0.0001)
		|| !FMath::IsNearlyZero(Building->GetActorRotation().Pitch) || !FMath::IsNearlyZero(Building->GetActorRotation().Roll))
		return Reject(TEXT("UnsupportedBuildingTransform"));
	const auto Initial = UEHBWallTopologyLibrary::PreviewNodeMove(Building, NodeGuid, ExpectedPosition, ExpectedPosition);
	if (!Initial.bSucceeded) { Result.Message = Initial.Status.ToString(); return Result; }
	const auto* Node = Initial.ProposedTopology.Nodes.FindByPredicate([&](const auto& N) { return N.NodeGuid == NodeGuid; });
	auto* Pillar = Node ? Cast<AEHB_Pillar>(Building->FindElementActorByGuid(Node->SourcePillarGuid)) : nullptr;
	if (!Pillar) return Reject(TEXT("MissingLegacyPillar"));
	if (!Pillar->GetActorRotation().Equals(ExpectedWorldRotation, 0.001)) return Reject(TEXT("StaleRotation"));
	const auto Snap = Pillar->PreviewFloorSlabBoundarySnap(Building->GetActorTransform().TransformPosition(RequestedPosition), ExpectedWorldRotation, MaxSnapDistance);
	if (!Snap.bFound) return Reject(TEXT("NoSnapCandidate"));
	if (Snap.SlabGuid != ExpectedSlabGuid || !Snap.WorldLocation.Equals(ExpectedSnapWorldPosition, 0.001)
		|| !Snap.WorldRotation.Equals(ExpectedSnapWorldRotation, 0.001)) return Reject(TEXT("StaleSnap"));
	auto* Slab = Cast<AEHB_FloorSlab>(Building->FindElementActorByGuid(Snap.SlabGuid));
	if (!Slab) return Reject(TEXT("MissingSlab"));
	return CommitPlannedNodeMove(Building, NodeGuid, ExpectedPosition,
		Building->GetActorTransform().InverseTransformPosition(Snap.WorldLocation), Slab, Snap.WorldRotation);
}

FString UEHBBuildingToolset::PreviewNodeMoveWithSlabSnap(AEHBBuildingActorBase* Building, FGuid NodeGuid, FVector ExpectedPosition, FVector TargetPosition, float MaxSnapDistance)
{
	AEHBBuildingActorBase* Selected = GetEditorSelectedBuilding();
	if (!Selected || (Building && Building != Selected))
		return TEXT("{\"bSucceeded\":false,\"status\":\"TargetNotSelected\"}");
	Building = Selected;
	if (TargetPosition.ContainsNaN() || !FMath::IsFinite(MaxSnapDistance) || MaxSnapDistance < 0)
		return TEXT("{\"bSucceeded\":false,\"status\":\"InvalidInput\"}");
	const FRotator BuildingRotation = Building->GetActorRotation();
	if (!FMath::IsNearlyZero(BuildingRotation.Pitch) || !FMath::IsNearlyZero(BuildingRotation.Roll))
		return TEXT("{\"bSucceeded\":false,\"status\":\"TiltedBuildingUnsupported\"}");
	const auto Initial = UEHBWallTopologyLibrary::PreviewNodeMove(Building, NodeGuid, ExpectedPosition, ExpectedPosition);
	if (!Initial.bSucceeded)
	{
		FString Json;
		FJsonObjectConverter::UStructToJsonObjectString(Initial, Json);
		return Json;
	}
	const auto* Node = Initial.ProposedTopology.Nodes.FindByPredicate([&](const auto& N) { return N.NodeGuid == NodeGuid; });
	const auto* Pillar = Node ? Cast<AEHB_Pillar>(Building->FindElementActorByGuid(Node->SourcePillarGuid)) : nullptr;
	if (!Pillar) return TEXT("{\"bSucceeded\":false,\"status\":\"MissingLegacyPillar\"}");
	const FVector DesiredWorld = Building->GetActorTransform().TransformPosition(TargetPosition);
	const auto Snap = Pillar->PreviewFloorSlabBoundarySnap(DesiredWorld, Pillar->GetActorRotation(), MaxSnapDistance);
	const FVector ResolvedLocal = Snap.bFound ? Building->GetActorTransform().InverseTransformPosition(Snap.WorldLocation) : TargetPosition;
	const auto Move = UEHBWallTopologyLibrary::PreviewNodeMove(Building, NodeGuid, ExpectedPosition, ResolvedLocal);
	FString SnapJson, MoveJson;
	FJsonObjectConverter::UStructToJsonObjectString(Snap, SnapJson);
	FJsonObjectConverter::UStructToJsonObjectString(Move, MoveJson);
	return FString::Printf(TEXT("{\"snap\":%s,\"move\":%s,\"commitAvailable\":false,\"explicitCommitTool\":\"CommitNodeMoveOnFixedSlab\",\"explicitCommitPolicy\":\"FixedPlainSlabsUnitScale\"}"), *SnapJson, *MoveJson);
}

FString UEHBBuildingToolset::MigrateWallNodeOwnership(AEHBBuildingActorBase* Building,bool bApply)
{
	auto* Selected=GetEditorSelectedBuilding();
	if(!Selected||(Building&&Building!=Selected))return TEXT("{\"bSucceeded\":false,\"status\":\"TargetNotSelected\"}");
	auto Result=Selected->MigrateWallNodeOwnership(false);
	if(bApply&&(!GEditor||GEditor->PlayWorld||GEditor->IsTransactionActive())){Result.bSucceeded=false;Result.Status=TEXT("RequiresIndependentEditorTransaction");}
	else if(bApply&&Result.bSucceeded&&Result.Status==TEXT("Ready"))
	{
		bool bChanged=false;
		{
			const FScopedTransaction Transaction(NSLOCTEXT("EasyHouseBuilder","MigrateWallNodeOwnership","Migrate Wall Connection Nodes"));
			Selected->SetFlags(RF_Transactional);Selected->Modify();
			for(auto* Element:Selected->QueryElements(FEHBElementQuery()))
			{
				Element->SetFlags(RF_Transactional);Element->Modify();TInlineComponentArray<UActorComponent*> Components(Element);
				for(auto* Component:Components){Component->SetFlags(RF_Transactional);Component->Modify();}
			}
			Result=Selected->MigrateWallNodeOwnership(true);bChanged=Result.bChanged;
			if(Result.bSucceeded&&!UEHBWallTopologyLibrary::CaptureWallTopology(Selected).Issues.IsEmpty()){Result.bSucceeded=false;Result.Status=TEXT("MigrationFinalTopologyMismatch");}
#if WITH_DEV_AUTOMATION_TESTS
			if(Result.bSucceeded&&EHBNodeOwnershipTestHooks::FailAfterApply){EHBNodeOwnershipTestHooks::FailAfterApply=false;Result.bSucceeded=false;Result.Status=TEXT("InjectedFailure");}
#endif
		}
		if(!Result.bSucceeded&&bChanged)
		{
			const bool bRestored=GEditor->UndoTransaction(false);Result.bChanged=!bRestored;
			Result.Status=bRestored?TEXT("NodeOwnershipFailedRolledBack"):TEXT("NodeOwnershipRollbackFailed");
		}
	}
	FString Json;FJsonObjectConverter::UStructToJsonObjectString(Result,Json);return Json;
}

FString UEHBBuildingToolset::MigrateWallNodeAuthority(AEHBBuildingActorBase* Building,bool bApply)
{
	auto* Selected=GetEditorSelectedBuilding();
	if(!Selected||(Building&&Building!=Selected))return TEXT("{\"bSucceeded\":false,\"status\":\"TargetNotSelected\"}");
	auto Result=Selected->MigrateWallNodeAuthority(false);
	if(bApply&&(!GEditor||GEditor->PlayWorld||GEditor->IsTransactionActive())){Result.bSucceeded=false;Result.Status=TEXT("RequiresIndependentEditorTransaction");}
	else if(bApply&&Result.bSucceeded&&Result.Status==TEXT("Ready"))
	{
		bool bChanged=false;
		{
			const FScopedTransaction Transaction(NSLOCTEXT("EasyHouseBuilder","MigrateWallNodeAuthority","Activate Wall Node Pose Authority"));
			Selected->SetFlags(RF_Transactional);Selected->Modify();
			for(auto* Element:Selected->QueryElements(FEHBElementQuery()))
			{
				Element->SetFlags(RF_Transactional);Element->Modify();TInlineComponentArray<UActorComponent*> Components(Element);
				for(auto* Component:Components){Component->SetFlags(RF_Transactional);Component->Modify();}
			}
			Result=Selected->MigrateWallNodeAuthority(true);bChanged=Result.bChanged;
			if(Result.bSucceeded&&!UEHBWallTopologyLibrary::CaptureWallTopology(Selected).Issues.IsEmpty()){Result.bSucceeded=false;Result.Status=TEXT("MigrationFinalTopologyMismatch");}
#if WITH_DEV_AUTOMATION_TESTS
			if(Result.bSucceeded&&EHBNodeOwnershipTestHooks::FailAfterApply){EHBNodeOwnershipTestHooks::FailAfterApply=false;Result.bSucceeded=false;Result.Status=TEXT("InjectedFailure");}
#endif
		}
		if(!Result.bSucceeded&&bChanged)
		{
			const bool bRestored=GEditor->UndoTransaction(false);Result.bChanged=!bRestored;
			Result.Status=bRestored?TEXT("NodeAuthorityFailedRolledBack"):TEXT("NodeAuthorityRollbackFailed");
		}
	}
	FString Json;FJsonObjectConverter::UStructToJsonObjectString(Result,Json);return Json;
}

FEHBToolsetOperationResult UEHBBuildingToolset::EnableWallNodeEditing(AEHBBuildingActorBase* Building,bool bPreviewOnly)
{return EHBNodeEditingActivation::Execute(Building,bPreviewOnly);}

FString UEHBBuildingToolset::PreviewWallNodeCopy(AEHBBuildingActorBase* Building)
{
 auto* Selected=GetEditorSelectedBuilding();
 if(!Selected||(Building&&Building!=Selected))return TEXT("{\"bSucceeded\":false,\"status\":\"TargetNotSelected\",\"bCanCommit\":false}");
 auto Json=MakeShared<FJsonObject>();Json->SetBoolField(TEXT("bCanCommit"),false);
 FEHBPreparedWallNodeDefinitions Source;
 const auto Capture=UEHBWallTopologyLibrary::CaptureNodeDefinitionSource(Selected,Source);
 FEHBWallNodeCopyDraft Draft;
 if(Selected->WallNodeOwnership.Version!=1)Draft.Status=TEXT("RequiresTypedNodeOwnership");
 else if(!Capture.bSucceeded)Draft.Status=Capture.Status;
 else
 {
  TArray<FGuid> Elements;for(const auto* Element:Selected->QueryElements(FEHBElementQuery()))Elements.Add(Element->ElementGuid);
  Draft=FEHBWallNodeCopy::BuildDraft(Selected->BuildingGuid,Source,Elements,Selected->ElementRelations);
 }
 Json->SetBoolField(TEXT("bSucceeded"),Draft.bSucceeded);Json->SetStringField(TEXT("status"),Draft.Status.ToString());
 Json->SetStringField(TEXT("scope"),TEXT("Value-only identity draft; Actor properties, persisted snapshots, outline bindings and the copy transaction are not applied."));
 if(Draft.bSucceeded)
 {
  Json->SetStringField(TEXT("sourceBuildingGuid"),Selected->BuildingGuid.ToString());Json->SetStringField(TEXT("buildingGuid"),Draft.BuildingGuid.ToString());
  auto AddMap=[&](const TCHAR* Name,const TMap<FGuid,FGuid>& Map)
  {
   TArray<FGuid> Keys;Map.GetKeys(Keys);Keys.Sort([](const FGuid& A,const FGuid& B){return A<B;});TArray<TSharedPtr<FJsonValue>> Values;
   for(FGuid Key:Keys){auto Pair=MakeShared<FJsonObject>();Pair->SetStringField(TEXT("source"),Key.ToString());Pair->SetStringField(TEXT("copy"),Map.FindChecked(Key).ToString());Values.Add(MakeShared<FJsonValueObject>(Pair));}
   Json->SetArrayField(Name,Values);
  };
  AddMap(TEXT("elementGuids"),Draft.ElementGuids);AddMap(TEXT("nodeGuids"),Draft.NodeGuids);AddMap(TEXT("relationGuids"),Draft.RelationGuids);AddMap(TEXT("roomGuids"),Draft.RoomGuids);
  Json->SetObjectField(TEXT("model"),FJsonObjectConverter::UStructToJsonObject(Draft.Model));
  TArray<TSharedPtr<FJsonValue>> Relations;for(const auto& Relation:Draft.Relations)Relations.Add(MakeShared<FJsonValueObject>(FJsonObjectConverter::UStructToJsonObject(Relation)));Json->SetArrayField(TEXT("relations"),Relations);
 }
 FString Result;FJsonSerializer::Serialize(Json,TJsonWriterFactory<>::Create(&Result));return Result;
}

FString UEHBBuildingToolset::CopyWallNodeBuilding(AEHBBuildingActorBase* Building, FVector WorldOffset)
{
 auto* Selected=GetEditorSelectedBuilding();
 if(!Selected||(Building&&Building!=Selected))return TEXT("{\"bSucceeded\":false,\"status\":\"TargetNotSelected\"}");
 const auto Result=EHBBuildingCopy::Execute(Selected,WorldOffset);
 auto Json=MakeShared<FJsonObject>();Json->SetBoolField(TEXT("bSucceeded"),Result.bSucceeded);Json->SetStringField(TEXT("status"),Result.Status.ToString());
 if(!Result.FailureReason.IsNone())Json->SetStringField(TEXT("failureReason"),Result.FailureReason.ToString());
 Json->SetNumberField(TEXT("actorCount"),Result.Actors.Num());
 if(Result.Building){Json->SetStringField(TEXT("buildingGuid"),Result.Building->BuildingGuid.ToString());Json->SetStringField(TEXT("actorPath"),Result.Building->GetPathName());}
 if(Result.bSucceeded)
 {
  Json->SetStringField(TEXT("sourceBuildingGuid"),Selected->BuildingGuid.ToString());
  auto AddMap=[&](const TCHAR* Key,const TMap<FGuid,FGuid>& Map){TArray<FGuid> Keys;Map.GetKeys(Keys);Keys.Sort([](const auto& A,const auto& B){return A<B;});TArray<TSharedPtr<FJsonValue>> Items;for(FGuid Id:Keys){auto Item=MakeShared<FJsonObject>();Item->SetStringField(TEXT("source"),Id.ToString());Item->SetStringField(TEXT("copy"),Map.FindChecked(Id).ToString());Items.Add(MakeShared<FJsonValueObject>(Item));}Json->SetArrayField(Key,Items);};
  AddMap(TEXT("elementIds"),Result.IdentityDraft.ElementGuids);AddMap(TEXT("nodeIds"),Result.IdentityDraft.NodeGuids);AddMap(TEXT("relationIds"),Result.IdentityDraft.RelationGuids);AddMap(TEXT("roomIds"),Result.IdentityDraft.RoomGuids);
 }
 FString Text;FJsonSerializer::Serialize(Json,TJsonWriterFactory<>::Create(&Text));return Text;
}

FEHBToolsetOperationResult UEHBBuildingToolset::RemovePhysicalColumn(AEHBBuildingActorBase* Building,FGuid NodeGuid,FGuid ExpectedPillarGuid,int32 ExpectedNodeRevision,bool bPreviewOnly)
{return EHBNodeAuthorityEditing::Execute(Building,NodeGuid,ExpectedPillarGuid,ExpectedNodeRevision,FVector::ZeroVector,FVector::ZeroVector,true,bPreviewOnly);}
FEHBToolsetOperationResult UEHBBuildingToolset::RemoveWalls(AEHBBuildingActorBase* Building,const TArray<FGuid>& WallGuids,int32 ExpectedGraphRevision,bool bPreviewOnly)
{return EHBWallRemovalCommand::Execute(Building,WallGuids,ExpectedGraphRevision,bPreviewOnly);}
FEHBToolsetOperationResult UEHBBuildingToolset::EditFinishRegion(AEHBBuildingActorBase* B,FGuid Id,int32 Graph,int32 Geometry,const TArray<FVector>& Polygon,FGuid Room,bool Preview)
{return EHBFinishRegionCommand::Execute(B,Id,Graph,Geometry,Polygon,Room,Preview);}
FEHBToolsetOperationResult UEHBBuildingToolset::MoveUnboundWallNode(AEHBBuildingActorBase* Building,FGuid NodeGuid,int32 ExpectedNodeRevision,FVector ExpectedPosition,FVector TargetPosition,bool bPreviewOnly)
{return EHBNodeAuthorityEditing::Execute(Building,NodeGuid,FGuid(),ExpectedNodeRevision,ExpectedPosition,TargetPosition,false,bPreviewOnly);}

FEHBToolsetOperationResult UEHBBuildingToolset::SetFloorSlabOpenings(AEHB_FloorSlab* S,const TArray<FEHBFloorSlabHole>& Holes,const TArray<FEHBCutOperation>& Cuts,int32 Graph,int32 Geometry,bool Preview)
{
 // The compound command validates exactly one selected owner or target slab,
 // matching viewport handle selection as well as building-selected AI calls.
 return EHBFinishRegionCommand::EditSlabSources(S,Graph,Geometry,Holes,Cuts,Preview);
}
#include "Core/EHBPreparedWallOpening.h"
FEHBToolsetOperationResult UEHBBuildingToolset::PreviewWallOpenings(AEHB_Wall* W,const TArray<FEHBCutOperation>& Cuts,int32 Graph,int32 Geometry,FEHBPreparedWallOpening& Out)
{
 Out={};FEHBToolsetOperationResult Result;auto Fail=[&](FName Why){Result.Message=Why.ToString();return Result;};
 if(!GEditor||GEditor->PlayWorld||GEditor->IsTransactionActive()||GIsTransacting)return Fail(TEXT("RequiresIndependentEditorTransaction"));
 if(!IsValid(W)||W->IsActorBeingDestroyed()||W->GetWorld()!=GEditor->GetEditorWorldContext().World())return Fail(TEXT("InvalidOpeningWall"));
 auto* B=W->OwningBuilding.Get();
 if(!IsValid(B)||B->IsActorBeingDestroyed()||B->FindElementActorByGuid(W->ElementGuid)!=W||W->GetAttachParentActor()!=B)return Fail(TEXT("InvalidOpeningOwner"));
 if(B->IsChangeNotificationBusy())return Fail(TEXT("ChangeNotificationBusy"));
 if(Graph!=B->RelationshipGraphRevision||Geometry!=B->GetElementGeometryRevision(W->ElementGuid))return Fail(TEXT("StaleOpeningRevision"));
 if(FLevelUtils::IsLevelLocked(W->GetLevel())||!FLevelUtils::IsLevelVisible(W->GetLevel()))return Fail(TEXT("RequiresVisibleUnlockedLevel"));
 if(GEditor->GetSelectedActors()->Num()!=1||(!GEditor->GetSelectedActors()->IsSelected(B)&&!GEditor->GetSelectedActors()->IsSelected(W)))return Fail(TEXT("TargetNotSelected"));
 FName Status;Result.bSucceeded=W->PrepareSurfaceOpening(Cuts,Out,Status);Result.Message=Status.ToString();return Result;
}
#include "EHBWallOpeningCommand.h"
FEHBToolsetOperationResult UEHBBuildingToolset::SetWallOpenings(AEHB_Wall* W,const TArray<FEHBCutOperation>& Cuts,int32 Graph,int32 Geometry,bool Preview)
{return FEHBWallOpeningCommand::Execute(W,Cuts,Graph,Geometry,Preview);}
