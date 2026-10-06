#include "SmartObject/HMS_EntryRootMotionModifier.h"
#include "MotionWarpingComponent.h"

FTransform UHMS_EntryRootMotionModifier::ProcessRootMotion(const FTransform& InRootMotion, float DeltaSeconds)
{
 if (!Animation.IsValid() || EndTime<=StartTime) { return InRootMotion; }
 auto RootAt=[&](float T) { return UMotionWarpingUtilities::ExtractRootMotionFromAnimation(Animation.Get(),StartTime,FMath::Clamp(T,StartTime,EndTime)); };
 const FTransform Total=RootAt(EndTime);
 auto Alpha=[&](float T) { return FMath::Clamp((T-StartTime)/(EndTime-StartTime),0.f,1.f); };
 auto Smooth=[&](float T) { const float A=Alpha(T); return A*A*(3-2*A); };
 const FVector Error=WorldDisplacement-InitialMeshRotation.RotateVector(Total.GetTranslation());
 auto WorldAt=[&](float T)
 {
  // Retain lateral/curved authored travel; distribute only the endpoint residual.
  const float A=Alpha(T);
  return InitialMeshRotation.RotateVector(RootAt(T).GetTranslation())+Error*Smooth(T)
   +InitialVelocityCorrection*(EndTime-StartTime)*A*FMath::Square(1-A);
 };
 const FQuat PreviousRotation=FQuat(FVector::UpVector,FMath::DegreesToRadians(RotationCorrectionDegrees*Smooth(PreviousPosition)))*InitialMeshRotation*RootAt(PreviousPosition).GetRotation();
 FVector Translation=PreviousRotation.UnrotateVector(WorldAt(CurrentPosition)-WorldAt(PreviousPosition));
 Translation.Z=InRootMotion.GetTranslation().Z;
 const FQuat NextRotation=FQuat(FVector::UpVector,FMath::DegreesToRadians(RotationCorrectionDegrees*Smooth(CurrentPosition)))*InitialMeshRotation*RootAt(CurrentPosition).GetRotation();
 FTransform Result=InRootMotion;
 Result.SetRotation(PreviousRotation.Inverse()*NextRotation);
 if(CurrentPosition>EndTime)
 {
  const FTransform Tail=UMotionWarpingUtilities::ExtractRootMotionFromAnimation(Animation.Get(),EndTime,CurrentPosition);
  Translation+=Result.GetRotation().RotateVector(Tail.GetTranslation());
  Result.SetRotation(Result.GetRotation()*Tail.GetRotation());
 }
 Result.SetTranslation(Translation);
 return Result;
}
