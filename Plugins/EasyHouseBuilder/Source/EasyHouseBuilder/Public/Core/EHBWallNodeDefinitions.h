// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "EHBWallNodeDefinitions.generated.h"

/** Persistent node values. No Actor/component references. */
USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBWallNodeDefinition
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Node") FGuid NodeGuid;
	UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Node") FTransform LocalTransform;
	UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Node") int32 FloorIndex=INDEX_NONE;
	UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Node") FVector JunctionDimensions=FVector::ZeroVector;
	UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Node") int32 GeometryRevision=1;
};

USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBWallNodePillarBinding
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Node") FGuid NodeGuid;
	UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Node") FGuid PhysicalPillarGuid;
};

USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBNodeConnectedWallDefinition
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Node") FGuid WallGuid;
	UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Node") FGuid StartNodeGuid;
	UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Node") FGuid EndNodeGuid;
	UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Node") float Thickness=20;
	UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Node") float Height=300;
};

/** Value-only node/wall model. Version 0 is empty/uninitialized; version 1 is supported.
 * Physical bindings are optional; their absence creates no pillar/support/host identity.
 * Owning a value does not activate building authority or authorize an Actor deletion. */
USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBWallNodeModel
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Node") int32 Version=0;
	UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Node") TArray<FEHBWallNodeDefinition> Nodes;
	UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Node") TArray<FEHBWallNodePillarBinding> PillarBindings;
	UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Node") TArray<FEHBNodeConnectedWallDefinition> Walls;
};

/** Immutable inactive preparation. Version 1 must retain EVERY source physical binding.
 * Inherited property names/types preserve the existing serialized preparation payload.
 * The stricter preparation validator is deliberately distinct from model validation. */
USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBPreparedWallNodeDefinitions : public FEHBWallNodeModel
{
	GENERATED_BODY()
};

/** Building-owned connection identity registry. V0 keeps legacy element endpoints.
 * V1 activates typed node endpoints. Actors supply pose until the separate
 * WallNodeAuthority is explicitly enabled; connection ownership alone never does so.
 * This registry is distinct from inactive preparation and from the optional-binding value model.
 * V1 bindings are one-to-one and required until pose authority supports an unbound node. */
USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBWallNodeOwnership
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Node") int32 Version=0;
	UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Node") TArray<FEHBWallNodePillarBinding> Bindings;
};

/** Opt-in pose authority, independent of connection ownership and inactive preparation.
 * V0 preserves old assets; V1 keeps every physical binding; V2 explicitly permits
 * an unbound logical node with building-owned derived junction geometry. */
USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBWallNodeAuthority
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Node") int32 Version=0;
	UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Node") TArray<FEHBWallNodeDefinition> Nodes;
};
