#include "CSOCoverTypes.h"

bool FCSOAgentProfile::IsValid() const
{
    return FMath::IsFinite(Radius) && FMath::IsFinite(CrouchHalfHeight) && FMath::IsFinite(StandHalfHeight)
        && FMath::IsFinite(CrouchEyeHeight) && FMath::IsFinite(StandEyeHeight) && FMath::IsFinite(LeanDistance)
        && FMath::IsFinite(Clearance) && FMath::IsFinite(EyeSweepRadius)
        && Radius > 0 && CrouchHalfHeight >= Radius && StandHalfHeight >= CrouchHalfHeight
        && CrouchEyeHeight > 0 && CrouchEyeHeight <= CrouchHalfHeight * 2
        && StandEyeHeight > CrouchEyeHeight && StandEyeHeight <= StandHalfHeight * 2
        && LeanDistance >= 0 && Clearance >= 0 && EyeSweepRadius >= 0
        && Radius <= 10000.f && CrouchHalfHeight <= 10000.f && StandHalfHeight <= 10000.f
        && LeanDistance <= 10000.f && Clearance <= 10000.f && EyeSweepRadius <= 10000.f;
}

FVector FCSOBakedCover::GetEye(const FCSOAgentProfile& Profile) const
{
    return Position + FVector(0, 0, bCrouched ? Profile.CrouchEyeHeight : Profile.StandEyeHeight);
}

FVector FCSOBakedCover::GetPeekEye(const FCSOAgentProfile& Profile, ECSOPeek Peek) const
{
    if (Peek == ECSOPeek::Stand) return Position + FVector(0, 0, Profile.StandEyeHeight);
    const float BakedDistance = Peek == ECSOPeek::Left ? LeftPeekDistance : RightPeekDistance;
    const float Distance = BakedDistance > 0.f ? BakedDistance : Profile.LeanDistance;
    return GetEye(Profile) + GetRight() * Distance * (Peek == ECSOPeek::Left ? -1.f : 1.f);
}
