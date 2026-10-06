// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Core/EHBWallNodeDefinitions.h"
#include "EHBWallNodeRooms.generated.h"

/** Directed traversal input. Legacy adapters supply resolved values, never Actor references. */
struct EASYHOUSEBUILDER_API FEHBRoomGraphEdge
{
	FGuid WallGuid,StartNodeGuid,EndNodeGuid;
	int32 FloorIndex=0;
};

/** Centerline boundary; not the inset finish polygon, a support host, or an occupied volume. */
USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBNodeRoomBoundary
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category="EasyHouse|Room")
	FGuid RoomGuid;
	UPROPERTY(BlueprintReadOnly, Category="EasyHouse|Room")
	int32 FloorIndex=0;
	UPROPERTY(BlueprintReadOnly, Category="EasyHouse|Room")
	TArray<FGuid> NodeGuids;
	UPROPERTY(BlueprintReadOnly, Category="EasyHouse|Room")
	TArray<FGuid> WallGuids;
	UPROPERTY(BlueprintReadOnly, Category="EasyHouse|Room")
	TArray<FVector> Polygon;
	UPROPERTY(BlueprintReadOnly, Category="EasyHouse|Room")
	double Area=0;
};

/** Pure face extraction shared by legacy buildings and optional-binding node models. */
struct EASYHOUSEBUILDER_API FEHBWallNodeRooms
{
	// Compatibility traversal: keeps incoming order and the legacy skip-invalid behavior.
	// Callers must validate authoring graphs before using it for a new model commit.
	static void ExtractFaces(FGuid BuildingGuid,const TMap<FGuid,FVector>& NodeLocations,
		const TArray<FEHBRoomGraphEdge>& Connections,TArray<FEHBNodeRoomBoundary>& Rooms);
	// Validated, deterministic model query. Failure clears Rooms; no scene/cache mutation.
	// Crossing, overlapping, non-simple or nested boundaries require an explicit layout/hole plan.
	static bool Build(FGuid BuildingGuid,const FEHBWallNodeModel& Model,
		TArray<FEHBNodeRoomBoundary>& Rooms,FName& Reason);
	// Explicit floor and building-local XY; boundary points can belong to both adjacent rooms.
	static TArray<FGuid> FindAtPoint(const TArray<FEHBNodeRoomBoundary>& Rooms,int32 FloorIndex,FVector LocalPoint);
};
