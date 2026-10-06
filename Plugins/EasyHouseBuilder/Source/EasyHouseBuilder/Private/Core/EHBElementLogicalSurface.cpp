#include "Actors/EHBElementActorBase.h"
#include "Actors/EHB_Floor.h"
#include "Actors/EHB_Wall.h"
#include "Math/RotationMatrix.h"
#include "Actors/EHB_FloorSlab.h"
#include "Components/EHBArchitecturalSurfaceComponent.h"
#include "Core/EHBActorImportScope.h"
#include "Core/EHBBuildingActorBase.h"
#include "Cutting/EHBPolygonClipper.h"
#include "EHBWallSurfaceHosts.inl"

namespace
{
 FGuid NewPortIdentity(FGuid Element,FName Name,int32 Index)
 {return FGuid::NewDeterministicGuid(FString::Printf(TEXT("EHB.LogicalSurface.v1|%s|%s|%d"),*Element.ToString(EGuidFormats::Digits),*Name.ToString().ToLower(),Index));}
}

FGuid AEHBElementActorBase::FindLogicalSurfaceIdentity(FName Name,int32 SubIndex) const
{
 if(LogicalSurfaceIdentityOwner!=ElementGuid)return {};
 const auto* I=LogicalSurfaceIdentities.FindByPredicate([&](const auto& V){return V.Name==Name&&V.SubIndex==SubIndex;});return I?I->SurfaceGuid:FGuid();
}

FGuid AEHBElementActorBase::ResolveLogicalSurfaceIdentity(FName Name,int32 SubIndex,FGuid LegacyGuid)
{
 if(IsTemplate()||FEHBActorImportScope::IsActive()||!ElementGuid.IsValid()||Name.IsNone())return {};
 if(LogicalSurfaceIdentityOwner.IsValid()&&LogicalSurfaceIdentityOwner!=ElementGuid)
  for(auto& I:LogicalSurfaceIdentities)I.SurfaceGuid=NewPortIdentity(ElementGuid,I.Name,I.SubIndex);
 LogicalSurfaceIdentityOwner=ElementGuid;
 if(auto* I=LogicalSurfaceIdentities.FindByPredicate([&](const auto& V){return V.Name==Name&&V.SubIndex==SubIndex;}))return I->SurfaceGuid;
 auto& I=LogicalSurfaceIdentities.AddDefaulted_GetRef();I.Name=Name;I.SubIndex=SubIndex;I.SurfaceGuid=LegacyGuid.IsValid()?LegacyGuid:NewPortIdentity(ElementGuid,Name,SubIndex);return I.SurfaceGuid;
}

void AEHBElementActorBase::RefreshLogicalSurfaceIdentities()
{
 if(IsTemplate()||FEHBActorImportScope::IsActive()||!ElementGuid.IsValid())return;
 if(LogicalSurfaceIdentityOwner.IsValid()&&LogicalSurfaceIdentityOwner!=ElementGuid)
  for(auto& I:LogicalSurfaceIdentities)I.SurfaceGuid=NewPortIdentity(ElementGuid,I.Name,I.SubIndex);
 LogicalSurfaceIdentityOwner=ElementGuid;
 // Floor finishes currently use plain generated components, so their single
 // semantic top (possibly disconnected) has no legacy component GUID to adopt.
 if(IsA<AEHB_Floor>())ResolveLogicalSurfaceIdentity(TEXT("Floor.Top"),INDEX_NONE);
 TInlineComponentArray<UEHBArchitecturalSurfaceComponent*> Components(this);
 for(auto* C:Components)if(IsValid(C)&&!C->SurfaceName.IsNone())
 {const auto Id=ResolveLogicalSurfaceIdentity(C->SurfaceName,C->SurfaceSubIndex,C->SurfaceGuid);if(Id.IsValid()){C->SurfaceGuid=Id;C->OwnerElementGuid=ElementGuid;}}
}

bool AEHBElementActorBase::QueryLogicalBaseSurfaces(TArray<FEHBLogicalSurfaceDefinition>& Surfaces,FName& Status) const
{
 if(OwningBuilding&&OwningBuilding->HasUnpublishedEdit()){Surfaces.Reset();Status=TEXT("LogicalSurfaceEditInFlight");return false;}
 return BuildLogicalBaseSurfacesFromSource(Surfaces,Status);
}

bool AEHBElementActorBase::BuildLogicalBaseSurfacesFromSource(TArray<FEHBLogicalSurfaceDefinition>& Surfaces,FName& Status) const
{
 Surfaces.Reset();auto Fail=[&](FName Why){Status=Why;return false;};
 if(IsTemplate()||IsActorBeingDestroyed()||!ElementGuid.IsValid())return Fail(TEXT("InvalidLogicalSurfaceElement"));
 if(GetAttachParentActor()!=OwningBuilding.Get())return Fail(TEXT("InvalidLogicalSurfaceParent"));
 FEHBLogicalSurfaceDefinition Result;Result.ElementGuid=ElementGuid;Result.FloorIndex=FloorIndex;
 if(OwningBuilding)Result.BuildingGuid=OwningBuilding->BuildingGuid;
 if(GetClass()==AEHB_Wall::StaticClass())
 {
  const auto* Wall=CastChecked<AEHB_Wall>(this);const auto Transform=GetElementLocalTransform();
  if(!FMath::IsFinite(Wall->Height)||Wall->Height<1||!FMath::IsFinite(Wall->Thickness)||Wall->Thickness<1||!FMath::IsFinite(Wall->CurveControlOffset))return Fail(TEXT("InvalidLogicalWallDimensions"));
  if(Transform.ContainsNaN()||!Transform.GetScale3D().Equals(FVector::OneVector,1.e-6))return Fail(TEXT("InvalidLogicalWallTransform"));
  FEHBWallSurfaceHostSource Source;Source.BuildingGuid=Result.BuildingGuid;Source.FloorIndex=FloorIndex;
  Source.LeftSurfaceGuid=FindLogicalSurfaceIdentity(TEXT("Wall.Left"));Source.RightSurfaceGuid=FindLogicalSurfaceIdentity(TEXT("Wall.Right"));
  Source.Height=Wall->Height;Source.Thickness=Wall->Thickness;Source.bStructural=HasAllCapabilities(static_cast<int32>(EEHBElementCapability::Structural));
  Source.Sides.WallGuid=ElementGuid;Source.Sides.LocalTransform=Transform;
  for(bool Left:{true,false})
  {
   TArray<FVector> Top;if(!Wall->BuildSideTopPolylineInBuildingSpace(Left,Top))return Fail(TEXT("InvalidLogicalWallSource"));
   if(Top.Num()!=2)return Fail(TEXT("CurvedLogicalSurfaceRequiresDefinition"));
   if(Left){Source.Sides.StartLeft=Top[0];Source.Sides.EndLeft=Top[1];}
   else {Source.Sides.StartRight=Top[0];Source.Sides.EndRight=Top[1];}
  }
  return FEHBWallSurfaceHosts::Build(Source,Surfaces,Status);
 }
 TArray<FEHBFloorFinishRegion> Source;
 if(GetClass()==AEHB_Floor::StaticClass())
 {
  const auto* Floor=CastChecked<AEHB_Floor>(this);Result.SourceName=TEXT("Floor.Top");Source=Floor->FloorRegions;
  if(Source.IsEmpty())Source.AddDefaulted_GetRef().OuterPolygon=Floor->LocalFloorPolygon;
 }
 else if(GetClass()==AEHB_FloorSlab::StaticClass())
 {
  const auto* Slab=CastChecked<AEHB_FloorSlab>(this);
  if(!FMath::IsFinite(Slab->Thickness)||Slab->Thickness<1)return Fail(TEXT("InvalidLogicalSurfaceThickness"));
  if(Slab->bIsFoundation&&Slab->bKeepFoundationBottomOnGround)return Fail(TEXT("VariableThicknessSurfaceRequiresDefinition"));
  Result.SourceName=TEXT("Slab.Surface");Result.Thickness=Slab->Thickness;Result.bStructural=HasAllCapabilities(static_cast<int32>(EEHBElementCapability::Structural));Result.bCanSupport=HasAllCapabilities(static_cast<int32>(EEHBElementCapability::CanSupport));
  auto& R=Source.AddDefaulted_GetRef();R.OuterPolygon=Slab->LocalTopPolygon;for(const auto& H:Slab->LocalHoles)R.Holes.AddDefaulted_GetRef().LocalPolygon=H.LocalPolygon;
 }
 else return Fail(TEXT("LogicalSurfaceAdapterRequired"));
 Result.SurfaceGuid=FindLogicalSurfaceIdentity(Result.SourceName,Result.SourceSubIndex);if(!Result.SurfaceGuid.IsValid())return Fail(TEXT("LogicalSurfaceIdentityNotMigrated"));
 if(Source.IsEmpty()||Source[0].OuterPolygon.IsEmpty())return Fail(TEXT("EmptyLogicalSurface"));
 const double Z=Source[0].OuterPolygon[0].Z;
 Result.PlaneToBuilding=FTransform(FQuat::Identity,FVector(0,0,Z))*GetElementLocalTransform();
 auto Loop=[&](const TArray<FVector>& In,TArray<FVector2D>& Out){for(const auto& P:In){if(P.ContainsNaN()||FMath::Abs(P.Z-Z)>0.001)return false;Out.Add(FVector2D(P.X,P.Y));}return true;};
 for(const auto& R:Source)
 {
  auto& Region=Result.Regions.AddDefaulted_GetRef();if(!Loop(R.OuterPolygon,Region.Boundary))return Fail(TEXT("NonPlanarLogicalSurface"));
  for(const auto& H:R.Holes)if(!Loop(H.LocalPolygon,Region.Holes.AddDefaulted_GetRef().Vertices))return Fail(TEXT("NonPlanarLogicalSurface"));
 }
 if(GetClass()==AEHB_FloorSlab::StaticClass())
 {
  // Authored holes are subtractive source loops, not necessarily enclosed holes.
  // Validate each source before clipping; a boolean must not repair invalid input.
  const auto Authored=Result.Regions[0];
  auto ValidateLoop=[&](const TArray<FVector2D>& Vertices)
  {
   auto Check=Result;Check.Regions.Reset();Check.Regions.AddDefaulted_GetRef().Boundary=Vertices;
   return Check.Validate(Status);
  };
  if(!ValidateLoop(Authored.Boundary))return false;
  TArray<FVector> Outer;for(const auto& P:Authored.Boundary)Outer.Add(FVector(P.X,P.Y,0));
  TArray<TArray<FVector>> Cutters;
  for(const auto& H:Authored.Holes)
  {
   if(!ValidateLoop(H.Vertices))return false;
   auto& Cutter=Cutters.AddDefaulted_GetRef();for(const auto& P:H.Vertices)Cutter.Add(FVector(P.X,P.Y,0));
  }
  if(!Cutters.IsEmpty())
  {
   FEHBPolygonClipResult Clipped;
   if(!FEHBPolygonClipper::DifferenceXY(Outer,Cutters,Clipped))return Fail(TEXT("LogicalSurfaceSourceClipFailed"));
   Result.Regions.Reset();
   for(const auto& Part:Clipped.Regions)
   {
    auto& Region=Result.Regions.AddDefaulted_GetRef();
    for(const auto& P:Part.OuterLoop)Region.Boundary.Add(FVector2D(P.X,P.Y));
    for(const auto& H:Part.HoleLoops){auto& Hole=Region.Holes.AddDefaulted_GetRef();for(const auto& P:H.ToLocalPositions())Hole.Vertices.Add(FVector2D(P.X,P.Y));}
   }
  }
 }
 if(!Result.Validate(Status))return false;Surfaces.Add(MoveTemp(Result));Status=TEXT("Ready");return true;
}

