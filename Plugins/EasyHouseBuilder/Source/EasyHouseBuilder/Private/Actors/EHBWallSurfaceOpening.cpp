#include "Actors/EHB_Wall.h"
#include "Core/EHBSurfaceOpening.h"

bool AEHB_Wall::ResolveSurfaceBoundOpening(const FEHBCutOperation& Operation,TArray<FVector2d>& Polygon,FName& Status) const
{
 Polygon.Reset();TArray<FEHBLogicalSurfaceDefinition> Definitions;
 if(!BuildLogicalBaseSurfacesFromSource(Definitions,Status))return false;
 const FEHBLogicalSurfaceDefinition* Host=nullptr;int32 Matches=0;
 for(const auto& S:Definitions)if(S.SurfaceGuid==Operation.SurfaceHost.SurfaceGuid){Host=&S;++Matches;}
 if(Matches!=1){Status=Matches?TEXT("SurfaceOpeningHostAmbiguous"):TEXT("SurfaceOpeningHostUnavailable");return false;}
 TArray<FVector> Points;
 if(!FEHBSurfaceOpening::Resolve(Operation,*Host,GetElementLocalTransform(),Points,Status))return false;
 const auto Normal=GetElementLocalTransform().InverseTransformVectorNoScale(Host->PlaneToBuilding.GetUnitAxis(EAxis::Z));
 if(FMath::Abs(Normal.Y)<0.999999){Status=TEXT("UnsupportedWallOpeningPlane");return false;}
 for(const auto& P:Points)Polygon.Add(FVector2d(P.X,P.Z));
 Status=TEXT("Ready");return true;
}

bool AEHB_Wall::ValidateSurfaceOpeningBindings(const TArray<FEHBCutOperation>& Operations,FName& Status) const
{
 for(const auto& Operation:Operations)
 {
  if(!Operation.bEnabled||Operation.SurfaceHost.Version==0)continue;
  int32 Matches=0;for(const auto& Other:Operations)Matches+=Other.OperationGuid==Operation.OperationGuid?1:0;
  if(Matches!=1){Status=TEXT("DuplicateSurfaceOpeningIdentity");return false;}
  TArray<FVector2d> Polygon;if(!ResolveSurfaceBoundOpening(Operation,Polygon,Status))return false;
 }
 Status=TEXT("Ready");return true;
}
