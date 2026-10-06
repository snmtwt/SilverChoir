#pragma once

#include "CoreMinimal.h"

/** Shared deterministic identity for boundary caches and read-only edit planning. */
struct FEHBRoomIdentity
{
	static FGuid Make(const FGuid& BuildingGuid, int32 FloorIndex,
		const TArray<FGuid>& PillarGuids, const TArray<FVector>& Points)
	{
		if (!BuildingGuid.IsValid() || PillarGuids.Num() < 3 || PillarGuids.Num() != Points.Num()) return FGuid();
		// A subdivision of a straight edge is not a new room. Keep true corners,
		// then canonicalize cyclic order and traversal direction. Coordinates do
		// not enter the identity, so moving existing corners preserves the ID.
		TArray<FString> Corners;
		for (int32 I=0; I<PillarGuids.Num(); ++I)
		{
			const FVector A=Points[I]-Points[(I+Points.Num()-1)%Points.Num()];
			const FVector B=Points[(I+1)%Points.Num()]-Points[I];
			const double Cross=FMath::Abs(A.X*B.Y-A.Y*B.X);
			const bool bStraight=FVector::DotProduct(A,B)>0 && Cross<=0.001*FMath::Max(A.Size2D()+B.Size2D(),1.0);
			if(!bStraight) Corners.Add(PillarGuids[I].ToString(EGuidFormats::Digits));
		}
		if(Corners.Num()<3)
		{
			Corners.Reset();
			for(const FGuid& Guid:PillarGuids) Corners.Add(Guid.ToString(EGuidFormats::Digits));
		}
		int32 First=0;
		for(int32 I=1;I<Corners.Num();++I) if(Corners[I]<Corners[First]) First=I;
		FString Forward,Reverse;
		for(int32 I=0;I<Corners.Num();++I)
		{
			Forward+=TEXT("|")+Corners[(First+I)%Corners.Num()];
			Reverse+=TEXT("|")+Corners[(First-I+Corners.Num())%Corners.Num()];
		}
		const FString Key=TEXT("EHB.RoomBoundary.v1|")+BuildingGuid.ToString(EGuidFormats::Digits)
			+TEXT("|")+FString::FromInt(FloorIndex)+(Forward<Reverse?Forward:Reverse);
		return FGuid::NewDeterministicGuid(Key);
	}

};
