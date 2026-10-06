#include "Core/EHBWallSurfaceHosts.h"
#include "Math/RotationMatrix.h"

bool FEHBWallSurfaceHosts::Build(const FEHBWallSurfaceHostSource& Source,
 TArray<FEHBLogicalSurfaceDefinition>& Out,FName& Status)
{
 Out.Reset();auto Fail=[&](FName Why){Status=Why;return false;};
 if(!Source.Sides.WallGuid.IsValid())return Fail(TEXT("InvalidLogicalSurfaceElement"));
 if(!Source.LeftSurfaceGuid.IsValid()||!Source.RightSurfaceGuid.IsValid())return Fail(TEXT("LogicalSurfaceIdentityNotMigrated"));
 if(Source.LeftSurfaceGuid==Source.RightSurfaceGuid)return Fail(TEXT("SurfaceOpeningHostAmbiguous"));
 if(!FMath::IsFinite(Source.Height)||Source.Height<1||!FMath::IsFinite(Source.Thickness)||Source.Thickness<1)return Fail(TEXT("InvalidLogicalWallDimensions"));
 const auto& Transform=Source.Sides.LocalTransform;
 if(Transform.ContainsNaN()||!Transform.GetRotation().IsNormalized()||!Transform.GetScale3D().Equals(FVector::OneVector,1.e-6))return Fail(TEXT("InvalidLogicalWallTransform"));
 const auto Up=Transform.GetUnitAxis(EAxis::Z);TArray<FEHBLogicalSurfaceDefinition> Candidates;
 for(bool Left:{true,false})
 {
  const FVector TopStart=Left?Source.Sides.StartLeft:Source.Sides.StartRight;
  const FVector TopEnd=Left?Source.Sides.EndLeft:Source.Sides.EndRight;
  if(TopStart.ContainsNaN()||TopEnd.ContainsNaN())return Fail(TEXT("InvalidLogicalWallSource"));
  // Reverse the left chart so U cross V is outward on both wall sides.
  const auto Start=(Left?TopEnd:TopStart)-Up*Source.Height;
  const auto End=(Left?TopStart:TopEnd)-Up*Source.Height;
  const auto Along=End-Start;const double Length=Along.Size();
  if(!FMath::IsFinite(Length)||Length<=0.001||FMath::Abs(FVector::DotProduct(Along,Up))>0.001)return Fail(TEXT("InvalidLogicalWallSource"));
  FEHBLogicalSurfaceDefinition Side;Side.BuildingGuid=Source.BuildingGuid;Side.ElementGuid=Source.Sides.WallGuid;Side.FloorIndex=Source.FloorIndex;
  Side.SourceName=Left?TEXT("Wall.Left"):TEXT("Wall.Right");Side.SurfaceGuid=Left?Source.LeftSurfaceGuid:Source.RightSurfaceGuid;
  Side.Thickness=Source.Thickness;Side.bStructural=Source.bStructural;Side.bCanSupport=false;
  Side.PlaneToBuilding=FTransform(FRotationMatrix::MakeFromXY(Along/Length,Up).ToQuat(),Start);
  Side.Regions.AddDefaulted_GetRef().Boundary={{0,0},{Length,0},{Length,Source.Height},{0,Source.Height}};
  if(!Side.Validate(Status))return false;Candidates.Add(MoveTemp(Side));
 }
 Out=MoveTemp(Candidates);Status=TEXT("Ready");return true;
}
