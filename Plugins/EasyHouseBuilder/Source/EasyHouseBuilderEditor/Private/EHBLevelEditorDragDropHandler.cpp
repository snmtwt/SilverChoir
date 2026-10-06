// Copyright Epic Games, Inc. All Rights Reserved.

#include "EHBLevelEditorDragDropHandler.h"

#include "Actors/EHBRoofBase.h"
#include "Actors/EHB_Pillar.h"
#include "Actors/EHB_Wall.h"
#include "Components/EHBArchitecturalSurfaceComponent.h"
#include "Components/EHBGeneratedMeshComponent.h"
#include "Core/EHBBuildingActorBase.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "Materials/MaterialInterface.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "EHBLevelEditorDragDropHandler"

namespace
{
	bool IsWallHitLeftSide(const AEHB_Wall* Wall, const UPrimitiveComponent* HitComponent, bool& bOutLeftSide)
	{
		if (!Wall || !HitComponent)
		{
			return false;
		}

		if (HitComponent == Wall->LeftWallMeshComponent || HitComponent->ComponentTags.Contains(TEXT("EHB_LeftWall")))
		{
			bOutLeftSide = true;
			return true;
		}

		if (HitComponent == Wall->RightWallMeshComponent || HitComponent->ComponentTags.Contains(TEXT("EHB_RightWall")))
		{
			bOutLeftSide = false;
			return true;
		}

		return false;
	}

	bool IsLoopInteriorOnWallLeftSide(const FEHBBuildingClosedLoop& Loop, const AEHB_Wall* Wall, bool& bOutLeftSide)
	{
		if (!Wall || Loop.PillarGuids.Num() < 3 || Loop.WallGuids.Num() != Loop.PillarGuids.Num())
		{
			return false;
		}

		for (int32 Index = 0; Index < Loop.WallGuids.Num(); ++Index)
		{
			if (Loop.WallGuids[Index] != Wall->ElementGuid)
			{
				continue;
			}

			const FGuid FromPillarGuid = Loop.PillarGuids[Index];
			const FGuid ToPillarGuid = Loop.PillarGuids[(Index + 1) % Loop.PillarGuids.Num()];
			if (Wall->StartPillarGuid == FromPillarGuid && Wall->EndPillarGuid == ToPillarGuid)
			{
				bOutLeftSide = true;
				return true;
			}

			if (Wall->StartPillarGuid == ToPillarGuid && Wall->EndPillarGuid == FromPillarGuid)
			{
				bOutLeftSide = false;
				return true;
			}
		}

		return false;
	}

	bool ApplyMaterialToPillar(AEHB_Pillar* Pillar, UMaterialInterface* Material)
	{
		if (!Pillar || !Material)
		{
			return false;
		}

		Pillar->Modify();
		Pillar->OverrideMaterial = Material;
		Pillar->RebuildPillarMesh();
		Pillar->MarkPackageDirty();
		return true;
	}

	FEHBWallSurfaceStyle MakeSimpleMaterialSurfaceStyle(UMaterialInterface* Material)
	{
		FEHBWallSurfaceStyle SurfaceStyle;
		SurfaceStyle.SourceType = EEHBWallSurfaceSourceType::Simple;
		SurfaceStyle.OverrideMaterial = TSoftObjectPtr<UMaterialInterface>(Material);
		return SurfaceStyle;
	}

	AEHB_Pillar* FindPillarByGuid(AEHBBuildingActorBase* Building, const FGuid& PillarGuid)
	{
		if (!Building || !PillarGuid.IsValid())
		{
			return nullptr;
		}

		return Cast<AEHB_Pillar>(Building->FindElementActorByGuid(PillarGuid));
	}

	bool ApplyMaterialToWallConnectedPillars(
		AEHBBuildingActorBase* Building,
		const AEHB_Wall* Wall,
		bool bWallLeftSide,
		UMaterialInterface* Material)
	{
		if (!Building || !Wall || !Material)
		{
			return false;
		}

		bool bChanged = false;
		TSet<FGuid> AppliedPillarGuids;
		const FEHBWallSurfaceStyle SurfaceStyle = MakeSimpleMaterialSurfaceStyle(Material);
		auto ApplyToPillar = [&](const FGuid& PillarGuid)
		{
			if (!PillarGuid.IsValid() || AppliedPillarGuids.Contains(PillarGuid))
			{
				return;
			}

			AppliedPillarGuids.Add(PillarGuid);
			AEHB_Pillar* Pillar = FindPillarByGuid(Building, PillarGuid);
			if (!Pillar)
			{
				return;
			}

			bChanged |= Pillar->SetConnectedWallSurfaceOverrideForSide(
				Wall->ElementGuid,
				SurfaceStyle,
				bWallLeftSide,
				false,
				true);
		};

		ApplyToPillar(Wall->StartPillarGuid);
		ApplyToPillar(Wall->EndPillarGuid);
		return bChanged;
	}

	bool ApplyMaterialToRoomInterior(
		AEHBBuildingActorBase* Building,
		const FEHBBuildingClosedLoop& RoomLoop,
		UMaterialInterface* Material)
	{
		if (!Building || !Material)
		{
			return false;
		}

		bool bChanged = false;
		const FEHBWallPillarSet RoomSet = Building->GetRoomWallsAndPillars(RoomLoop);
		for (AEHB_Wall* Wall : RoomSet.Walls)
		{
			bool bInteriorOnLeftSide = false;
			if (IsLoopInteriorOnWallLeftSide(RoomLoop, Wall, bInteriorOnLeftSide))
			{
				bChanged |= Wall->ApplyMaterialToSide(bInteriorOnLeftSide, Material);
				bChanged |= ApplyMaterialToWallConnectedPillars(Building, Wall, bInteriorOnLeftSide, Material);
			}
		}

		return bChanged;
	}

	bool ApplyMaterialToExteriorShell(AEHBBuildingActorBase* Building, const AEHB_Wall* ReferenceWall, UMaterialInterface* Material)
	{
		if (!Building || !ReferenceWall || !Material)
		{
			return false;
		}

		bool bChanged = false;
		const FEHBWallPillarSet ExteriorSet = Building->GetExteriorWallsAndPillarsFromWall(ReferenceWall);
		for (AEHB_Wall* Wall : ExteriorSet.Walls)
		{
			const TArray<FEHBBuildingClosedLoop> WallLoops = Building->GetClosedLoopsByWallGuid(Wall->ElementGuid);
			if (WallLoops.Num() != 1)
			{
				continue;
			}

			bool bInteriorOnLeftSide = false;
			if (IsLoopInteriorOnWallLeftSide(WallLoops[0], Wall, bInteriorOnLeftSide))
			{
				bChanged |= Wall->ApplyMaterialToSide(!bInteriorOnLeftSide, Material);
				bChanged |= ApplyMaterialToWallConnectedPillars(Building, Wall, !bInteriorOnLeftSide, Material);
			}
		}

		return bChanged;
	}

	bool ApplyShiftRoomMaterialFromWall(AEHB_Wall* HitWall, const UPrimitiveComponent* HitComponent, UMaterialInterface* Material)
	{
		if (!HitWall || !Material || !HitWall->OwningBuilding)
		{
			return false;
		}

		bool bHitLeftSide = false;
		if (!IsWallHitLeftSide(HitWall, HitComponent, bHitLeftSide))
		{
			return false;
		}

		AEHBBuildingActorBase* Building = HitWall->OwningBuilding.Get();
		Building->Modify();
		Building->RebuildClosedLoops();

		const TArray<FEHBBuildingClosedLoop> WallLoops = Building->GetClosedLoopsByWallGuid(HitWall->ElementGuid);
		for (const FEHBBuildingClosedLoop& Loop : WallLoops)
		{
			bool bInteriorOnLeftSide = false;
			if (IsLoopInteriorOnWallLeftSide(Loop, HitWall, bInteriorOnLeftSide) && bInteriorOnLeftSide == bHitLeftSide)
			{
				const bool bChanged = ApplyMaterialToRoomInterior(Building, Loop, Material);
				if (bChanged)
				{
					Building->MarkPackageDirty();
				}
				return bChanged;
			}
		}

		if (WallLoops.Num() == 1)
		{
			const bool bChanged = ApplyMaterialToExteriorShell(Building, HitWall, Material);
			if (bChanged)
			{
				Building->MarkPackageDirty();
			}
			return bChanged;
		}

		return false;
	}

	bool ApplyShiftRoomMaterialFromPillar(AEHB_Pillar* HitPillar, UMaterialInterface* Material)
	{
		if (!HitPillar || !Material || !HitPillar->OwningBuilding)
		{
			return false;
		}

		AEHBBuildingActorBase* Building = HitPillar->OwningBuilding.Get();
		Building->Modify();
		Building->RebuildClosedLoops();

		for (const FGuid& WallGuid : HitPillar->ConnectedWallGuids)
		{
			AEHB_Wall* Wall = Cast<AEHB_Wall>(Building->FindElementActorByGuid(WallGuid));
			if (Building->IsExteriorWall(Wall))
			{
				const bool bChanged = ApplyMaterialToExteriorShell(Building, Wall, Material);
				if (bChanged)
				{
					Building->MarkPackageDirty();
				}
				return bChanged;
			}
		}

		const TArray<FEHBBuildingClosedLoop> PillarLoops = Building->GetClosedLoopsByPillarGuid(HitPillar->ElementGuid);
		if (!PillarLoops.IsEmpty())
		{
			const bool bChanged = ApplyMaterialToRoomInterior(Building, PillarLoops[0], Material);
			if (bChanged)
			{
				Building->MarkPackageDirty();
			}
			return bChanged;
		}

		return false;
	}
}

bool UEHBLevelEditorDragDropHandler::PreDropObjectsAtCoordinates(
	int32 MouseX,
	int32 MouseY,
	UWorld* World,
	FViewport* Viewport,
	const TArray<UObject*>& DroppedObjects,
	TArray<AActor*>& OutNewActors)
{
	if (!Super::PreDropObjectsAtCoordinates(MouseX, MouseY, World, Viewport, DroppedObjects, OutNewActors))
	{
		return false;
	}

	if (!Viewport || DroppedObjects.Num() != 1)
	{
		return true;
	}

	UMaterialInterface* Material = Cast<UMaterialInterface>(DroppedObjects[0]);
	if (!Material)
	{
		return true;
	}

	const HHitProxy* HitProxy = Viewport->GetHitProxy(MouseX, MouseY);
	if (!HitProxy || !HitProxy->IsA(HActor::StaticGetType()))
	{
		return true;
	}

	const HActor* ActorHitProxy = static_cast<const HActor*>(HitProxy);
	UPrimitiveComponent* HitComponent = const_cast<UPrimitiveComponent*>(ActorHitProxy->PrimComponent.Get());
	AEHB_Wall* Wall = Cast<AEHB_Wall>(ActorHitProxy->Actor);
	if (!Wall && HitComponent)
	{
		Wall = Cast<AEHB_Wall>(HitComponent->GetOwner());
	}

	AEHB_Pillar* Pillar = Cast<AEHB_Pillar>(ActorHitProxy->Actor);
	if (!Pillar && HitComponent)
	{
		Pillar = Cast<AEHB_Pillar>(HitComponent->GetOwner());
	}

	AEHBRoofBase* Roof = Cast<AEHBRoofBase>(ActorHitProxy->Actor);
	if (!Roof && HitComponent)
	{
		Roof = Cast<AEHBRoofBase>(HitComponent->GetOwner());
	}

	UEHBGeneratedMeshComponent* GeneratedMeshComponent = Cast<UEHBGeneratedMeshComponent>(HitComponent);
	IEHBSurfaceMaterialTarget* SurfaceMaterialTarget = HitComponent
		? Cast<IEHBSurfaceMaterialTarget>(HitComponent)
		: nullptr;
	if (!Wall && !Pillar && !Roof && !SurfaceMaterialTarget && !GeneratedMeshComponent)
	{
		return true;
	}

	const bool bApplyAllSurfaces =
		FSlateApplication::IsInitialized()
		&& FSlateApplication::Get().GetPlatformApplication().IsValid()
		&& FSlateApplication::Get().GetPlatformApplication()->GetModifierKeys().IsShiftDown();

	const FScopedTransaction Transaction(LOCTEXT("ApplyEHBMaterialTransaction", "Apply EHB Material"));
	if (bApplyAllSurfaces && Wall && ApplyShiftRoomMaterialFromWall(Wall, HitComponent, Material))
	{
	}
	else if (bApplyAllSurfaces && Pillar && ApplyShiftRoomMaterialFromPillar(Pillar, Material))
	{
	}
	else if (Wall)
	{
		if (!Wall->ApplyMaterialToHitSurface(HitComponent, Material, bApplyAllSurfaces))
		{
			return true;
		}
	}
	else if (Pillar)
	{
		if (!ApplyMaterialToPillar(Pillar, Material))
		{
			return true;
		}
	}
	else if (Roof)
	{
		if (!Roof->ApplyMaterialToRoofComponent(HitComponent, Material, bApplyAllSurfaces))
		{
			return true;
		}
	}
	else
	{
		if (SurfaceMaterialTarget)
		{
			HitComponent->Modify();
			AActor* Owner = HitComponent->GetOwner();
			if (Owner)
			{
				Owner->Modify();
			}

			FEHBSurfaceMaterialApplyOptions Options;
			Options.bApplyAllSections = true;
			Options.bPersistAsSurfaceOverride = true;
			if (!SurfaceMaterialTarget->ApplyMaterialToSurface(Material, Options))
			{
				return true;
			}
		}
		else
		{
			GeneratedMeshComponent->Modify();
			AActor* Owner = GeneratedMeshComponent->GetOwner();
			if (Owner)
			{
				Owner->Modify();
			}

			const int32 MaterialSlotCount = FMath::Max(GeneratedMeshComponent->GetNumMaterials(), 1);
			bool bChanged = false;
			for (int32 MaterialIndex = 0; MaterialIndex < MaterialSlotCount; ++MaterialIndex)
			{
				bChanged |= GeneratedMeshComponent->SetMaterialIfChanged(MaterialIndex, Material);
			}

			if (!bChanged)
			{
				return true;
			}

			GeneratedMeshComponent->MarkPackageDirty();
			if (Owner)
			{
				Owner->MarkPackageDirty();
			}
		}
	}

	if (GEditor)
	{
		GEditor->OnSceneMaterialsModified();
		GEditor->RedrawLevelEditingViewports();
	}

	WorldSurrogateReferencingObject.Reset();
	return false;
}

#undef LOCTEXT_NAMESPACE
