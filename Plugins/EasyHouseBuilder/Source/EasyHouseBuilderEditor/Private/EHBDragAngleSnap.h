#pragma once

#include "CoreMinimal.h"

namespace EHBDragAngleSnap
{
	// World XY octants, independent of the building transform. Keep the endpoint
	// elevation, and leave arbitrary angles outside the small capture cone free.
	inline FVector Snap(const FVector& Start, const FVector& End)
	{
		const FVector Delta(End.X - Start.X, End.Y - Start.Y, 0);
		if (Start.ContainsNaN() || End.ContainsNaN() || Delta.IsNearlyZero()) return End;
		const double Angle = FMath::RadiansToDegrees(FMath::Atan2(Delta.Y, Delta.X));
		const double Target = FMath::RoundToDouble(Angle / 45.0) * 45.0;
		if (FMath::Abs(FMath::FindDeltaAngleDegrees(Angle, Target)) > 3.0) return End;
		const double Radians = FMath::DegreesToRadians(Target);
		const FVector Direction(FMath::Cos(Radians), FMath::Sin(Radians), 0);
		FVector Result = Start + Direction * FVector::DotProduct(Delta, Direction);
		Result.Z = End.Z;
		return Result;
	}

 struct FOptions
 {
  bool bAngleSnap=true;
  bool bFixedLength=false;
  bool bFixedDirection=false;
  double LengthCm=300;
  double DirectionDegrees=0;
  bool bFixedRectangle=false;
  double RectangleWidthCm=400,RectangleDepthCm=300;
 };
 // Constraints apply to free XY endpoints. Authored hosts retain priority.
 inline FVector Resolve(const FVector& Start,const FVector& End,const FOptions& Options,bool bBypass=false)
 {
  if(bBypass||Start.ContainsNaN()||End.ContainsNaN())return End;
  if((Options.bFixedLength&&(!FMath::IsFinite(Options.LengthCm)||Options.LengthCm<=0))
   ||(Options.bFixedDirection&&!FMath::IsFinite(Options.DirectionDegrees)))return End;
  FVector Result=Options.bAngleSnap&&!Options.bFixedDirection?Snap(Start,End):End;
  FVector Direction=(Result-Start).GetSafeNormal2D();
  if(Options.bFixedDirection)
  {
   const double Radians=FMath::DegreesToRadians(FMath::UnwindDegrees(Options.DirectionDegrees));
   Direction=FVector(FMath::Cos(Radians),FMath::Sin(Radians),0);
  }
  if(Direction.IsNearlyZero())return End;
  const double Length=Options.bFixedLength?Options.LengthCm:FVector::Dist2D(Start,Result);
  Result=Start+Direction*Length;Result.Z=End.Z;return Result;
 }

 inline FVector ResolveRectangle(const FVector& Start,const FVector& End,const FTransform& Frame,const FOptions& Options,bool bBypass=false)
 {
  if(bBypass||!Options.bFixedRectangle||Start.ContainsNaN()||End.ContainsNaN()||Frame.ContainsNaN()
   ||!FMath::IsFinite(Options.RectangleWidthCm)||!FMath::IsFinite(Options.RectangleDepthCm)
   ||Options.RectangleWidthCm<=0||Options.RectangleDepthCm<=0)return End;
  const double ScaleX=Frame.TransformVector(FVector::ForwardVector).Size(),ScaleY=Frame.TransformVector(FVector::RightVector).Size();
  if(ScaleX<=UE_SMALL_NUMBER||ScaleY<=UE_SMALL_NUMBER)return End;
  const FVector A=Frame.InverseTransformPosition(Start),Z=Frame.InverseTransformPosition(End);
  return Frame.TransformPosition(A+FVector((Z.X<A.X?-1:1)*Options.RectangleWidthCm/ScaleX,(Z.Y<A.Y?-1:1)*Options.RectangleDepthCm/ScaleY,0));
 }
}
