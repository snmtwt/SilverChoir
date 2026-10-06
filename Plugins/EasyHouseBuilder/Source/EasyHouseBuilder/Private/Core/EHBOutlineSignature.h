#pragma once
#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Hash/Blake3.h"
#include "Serialization/BufferArchive.h"

/** Hash stored design values, excluding materials, display offset and derived meshes. */
struct FEHBOutlineSignature
{
	static FVector Canonical(FVector V)
	{
		if(V.X==0)V.X=0; if(V.Y==0)V.Y=0; if(V.Z==0)V.Z=0;
		return V;
	}
	FBufferArchive Data;
	bool bValid = true;
	FEHBOutlineSignature(FString Kind, const USceneComponent* Root, FGuid BuildingGuid, int32 FloorIndex)
	{
		Data << Kind << BuildingGuid << FloorIndex;
		if (!Root) { bValid=false; return; }
		// Use the component's stored relative properties rather than a round-trip
		// through world-space matrices, so saving/loading preserves the signature.
		FVector Location=Canonical(Root->GetRelativeLocation()), Scale=Canonical(Root->GetRelativeScale3D());
		FRotator Rotation=Root->GetRelativeRotation();
		if(Rotation.Pitch==0)Rotation.Pitch=0; if(Rotation.Yaw==0)Rotation.Yaw=0; if(Rotation.Roll==0)Rotation.Roll=0;
		bValid &= !Location.ContainsNaN() && !Scale.ContainsNaN() && !Rotation.ContainsNaN();
		Data << Location << Rotation << Scale;
	}
	void Polygon(const TArray<FVector>& Points)
	{
		int32 Count=Points.Num(); Data << Count;
		for (FVector Point:Points) { bValid &= !Point.ContainsNaN(); Point=Canonical(Point); Data << Point; }
	}
	FGuid Finish() const
	{
		return bValid ? FGuid::NewGuidFromHash(FBlake3::HashBuffer(Data.GetData(),Data.Num())) : FGuid();
	}
};
