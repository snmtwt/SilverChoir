// Copyright Epic Games, Inc. All Rights Reserved.

#include "Actors/EHB_Floor.h"
#include "Core/EHBActorImportScope.h"
#include "Geometry/EHBFloorContactGeometry.h"
#include "Core/EHBOutlineSignature.h"

#include "Arrangement2d.h"
#include "ConstrainedDelaunay2.h"
#include "Engine/World.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "MaterialDomain.h"
#include "Components/EHBGeneratedMeshComponent.h"
#include "Settings/EHBBuildingToolsetSettings.h"
#include "ThirdParty/clipper/clipper.h"

namespace
{
	constexpr double EHBFloorPointTolerance = 0.01;
	constexpr float EHBFloorUVWorldSize = 100.0f;
	constexpr double EHBFloorClipperScale = 1000.0;
	constexpr double EHBFloorMinArea = 1.0;

	UMaterialInterface* ResolveConfiguredDefaultWhiteBoxMaterial()
	{
		if (const UEHBBuildingToolsetSettings* Settings = GetDefault<UEHBBuildingToolsetSettings>())
		{
			if (UMaterialInterface* Material = Settings->DefaultWhiteBoxMaterial.LoadSynchronous())
			{
				return Material;
			}
		}
		return UMaterial::GetDefaultMaterial(MD_Surface);
	}

	bool ArePointsNearlyEqual2D(const FVector2d& A, const FVector2d& B)
	{
		const double DX = A.X - B.X;
		const double DY = A.Y - B.Y;
		return DX * DX + DY * DY <= EHBFloorPointTolerance * EHBFloorPointTolerance;
	}

	TArray<FVector2d> To2DLoop(const TArray<FVector>& Loop)
	{
		TArray<FVector2d> Result;
		Result.Reserve(Loop.Num());
		for (const FVector& Point : Loop)
		{
			const FVector2d Point2D(Point.X, Point.Y);
			if (Result.IsEmpty() || !ArePointsNearlyEqual2D(Result.Last(), Point2D))
			{
				Result.Add(Point2D);
			}
		}
		if (Result.Num() >= 2 && ArePointsNearlyEqual2D(Result[0], Result.Last()))
		{
			Result.Pop(EAllowShrinking::No);
		}
		return Result;
	}

	double CalculateTwiceArea(const TArray<FVector2d>& Loop)
	{
		double Area = 0.0;
		for (int32 Index = 0; Index < Loop.Num(); ++Index)
		{
			const FVector2d& A = Loop[Index];
			const FVector2d& B = Loop[(Index + 1) % Loop.Num()];
			Area += A.X * B.Y - B.X * A.Y;
		}
		return Area;
	}

	bool IsPointInsidePolygon(const FVector2d& Point, const TArray<FVector2d>& Polygon)
	{
		bool bInside = false;
		for (int32 CurrentIndex = 0, PreviousIndex = Polygon.Num() - 1;
			CurrentIndex < Polygon.Num();
			PreviousIndex = CurrentIndex++)
		{
			const FVector2d& Current = Polygon[CurrentIndex];
			const FVector2d& Previous = Polygon[PreviousIndex];
			const bool bCrosses = (Current.Y > Point.Y) != (Previous.Y > Point.Y);
			if (!bCrosses)
			{
				continue;
			}

			const double IntersectionX =
				(Previous.X - Current.X) * (Point.Y - Current.Y) / (Previous.Y - Current.Y) + Current.X;
			if (Point.X < IntersectionX)
			{
				bInside = !bInside;
			}
		}
		return bInside;
	}

	void InsertClosedLoop(UE::Geometry::FArrangement2d& Arrangement, const TArray<FVector2d>& Loop)
	{
		for (int32 Index = 0; Index < Loop.Num(); ++Index)
		{
			const FVector2d& A = Loop[Index];
			const FVector2d& B = Loop[(Index + 1) % Loop.Num()];
			if (!ArePointsNearlyEqual2D(A, B))
			{
				Arrangement.Insert(A, B);
			}
		}
	}

	ClipperLib::Path ToFloorClipperPath(const TArray<FVector>& Polygon)
	{
		ClipperLib::Path Result;
		Result.reserve(Polygon.Num());
		for (const FVector& Point : Polygon)
		{
			Result.push_back(ClipperLib::IntPoint(
				static_cast<ClipperLib::cInt>(FMath::RoundToDouble(Point.X * EHBFloorClipperScale)),
				static_cast<ClipperLib::cInt>(FMath::RoundToDouble(Point.Y * EHBFloorClipperScale))));
		}
		return Result;
	}

	TArray<FVector> FromFloorClipperPath(const ClipperLib::Path& Path, float Z)
	{
		TArray<FVector> Result;
		Result.Reserve(static_cast<int32>(Path.size()));
		for (const ClipperLib::IntPoint& Point : Path)
		{
			Result.Add(FVector(
				static_cast<double>(Point.X) / EHBFloorClipperScale,
				static_cast<double>(Point.Y) / EHBFloorClipperScale,
				Z));
		}
		return Result;
	}

	double GetAbsClipperAreaCm2(const ClipperLib::Path& Path)
	{
		return FMath::Abs(static_cast<double>(ClipperLib::Area(Path)))
			/ (EHBFloorClipperScale * EHBFloorClipperScale);
	}

	bool IsUsableClipperPath(const ClipperLib::Path& Path)
	{
		return Path.size() >= 3 && GetAbsClipperAreaCm2(Path) > EHBFloorMinArea;
	}

	void AppendSupportSurfacePaths(const FEHBFloorSupportSurface& Surface, ClipperLib::Paths& OutPaths)
	{
		if (Surface.OuterPolygon.Num() < 3)
		{
			return;
		}

		ClipperLib::Path OuterPath = ToFloorClipperPath(Surface.OuterPolygon);
		if (!IsUsableClipperPath(OuterPath))
		{
			return;
		}

		ClipperLib::Paths HolePaths;
		for (const FEHBFloorFinishHole& Hole : Surface.Holes)
		{
			if (Hole.LocalPolygon.Num() < 3)
			{
				continue;
			}

			ClipperLib::Path HolePath = ToFloorClipperPath(Hole.LocalPolygon);
			if (IsUsableClipperPath(HolePath))
			{
				HolePaths.push_back(MoveTemp(HolePath));
			}
		}

		if (HolePaths.empty())
		{
			if (!ClipperLib::Orientation(OuterPath))
			{
				ClipperLib::ReversePath(OuterPath);
			}
			OutPaths.push_back(MoveTemp(OuterPath));
			return;
		}

		ClipperLib::Clipper DifferenceClipper;
		DifferenceClipper.AddPath(OuterPath, ClipperLib::ptSubject, true);
		DifferenceClipper.AddPaths(HolePaths, ClipperLib::ptClip, true);

		ClipperLib::PolyTree SurfaceTree;
		if (!DifferenceClipper.Execute(ClipperLib::ctDifference, SurfaceTree, ClipperLib::pftNonZero, ClipperLib::pftNonZero))
		{
			return;
		}

		ClipperLib::Paths SurfacePaths;
		ClipperLib::ClosedPathsFromPolyTree(SurfaceTree, SurfacePaths);
		for (ClipperLib::Path& Path : SurfacePaths)
		{
			if (IsUsableClipperPath(Path))
			{
				OutPaths.push_back(MoveTemp(Path));
			}
		}
	}

	void AddRegionsFromPolyNode(const ClipperLib::PolyNode* Node, float Z, TArray<FEHBFloorFinishRegion>& OutRegions)
	{
		if (!Node)
		{
			return;
		}

		if (Node->Contour.size() >= 3 && !Node->IsHole() && GetAbsClipperAreaCm2(Node->Contour) > EHBFloorMinArea)
		{
			FEHBFloorFinishRegion& Region = OutRegions.AddDefaulted_GetRef();
			Region.OuterPolygon = FromFloorClipperPath(Node->Contour, Z);

			for (const ClipperLib::PolyNode* HoleNode : Node->Childs)
			{
				if (!HoleNode)
				{
					continue;
				}

				if (HoleNode->IsHole() && HoleNode->Contour.size() >= 3 && GetAbsClipperAreaCm2(HoleNode->Contour) > EHBFloorMinArea)
				{
					FEHBFloorFinishHole& Hole = Region.Holes.AddDefaulted_GetRef();
					Hole.LocalPolygon = FromFloorClipperPath(HoleNode->Contour, Z);
				}

				for (const ClipperLib::PolyNode* IslandNode : HoleNode->Childs)
				{
					AddRegionsFromPolyNode(IslandNode, Z, OutRegions);
				}
			}
			return;
		}

		for (const ClipperLib::PolyNode* ChildNode : Node->Childs)
		{
			AddRegionsFromPolyNode(ChildNode, Z, OutRegions);
		}
	}

	float GetRegionLogicalZ(const FEHBFloorFinishRegion& Region)
	{
		return Region.OuterPolygon.IsEmpty() ? 0.0f : Region.OuterPolygon[0].Z;
	}
}

AEHB_Floor::AEHB_Floor()
{
	ElementType = EEHBBuildingElementType::Floor;
	FloorRole = EEHBBuildingFloorElementRole::FloorFinish;
	ElementCapabilities = static_cast<int32>(EEHBElementCapability::SurfaceFinish);
	SemanticTags.AddUnique(TEXT("Finish.Floor"));

	MeshComponent = CreateDefaultSubobject<UEHBGeneratedMeshComponent>(TEXT("FloorMesh"));
	MeshComponent->SetupAttachment(SceneRoot);
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MeshComponent->SetCollisionObjectType(ECC_WorldStatic);
	MeshComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
	MeshComponent->ComponentTags.AddUnique(TEXT("EHB_Floor"));
	MeshComponent->ComponentTags.AddUnique(TEXT("EHB_FloorRegion"));
	RegionMeshComponents.Add(MeshComponent);
}

void AEHB_Floor::OnConstruction(const FTransform& Transform)
{
	if (FEHBActorImportScope::IsActive()) { Super::OnConstruction(Transform); return; }
	Super::OnConstruction(Transform);
	RebuildFloorMesh();
	UpdateCollisionSettings();
}

void AEHB_Floor::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();
	UpdateCollisionSettings();
}

void AEHB_Floor::Destroyed()
{
	ClearSurfaceFinishRelations();
	Super::Destroyed();
}

#if WITH_EDITOR
void AEHB_Floor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (FEHBActorImportScope::IsActive()) { Super::PostEditChangeProperty(PropertyChangedEvent); return; }
	Super::PostEditChangeProperty(PropertyChangedEvent);
	VisualOffset = FMath::Max(0.0f, VisualOffset);
	RebuildFloorMesh();
	UpdateCollisionSettings();
	NotifyElementGeometryChanged(true);
}
#endif

bool AEHB_Floor::ConfigureFromRoomLoop(
	AEHBBuildingActorBase* InBuilding,
	const FEHBBuildingClosedLoop& RoomLoop,
	float LocalSurfaceZ,
	bool bCreateSurfaceRelations)
{
	if (!InBuilding || RoomLoop.PillarGuids.Num() < 3)
	{
		return false;
	}

	FEHBNodeRoomBoundary Boundary;
	if(!InBuilding->TryGetRoomBoundary(RoomLoop.LoopGuid,RoomLoop.FloorIndex,Boundary))return false;
	TArray<FVector> NewPolygon=MoveTemp(Boundary.Polygon);
	for(auto& Point:NewPolygon)Point.Z=LocalSurfaceZ;

	FEHBFloorFinishRegion Region;
	Region.OuterPolygon = NewPolygon;

	Modify();
	AttachToBuilding(InBuilding, FTransform::Identity);
	RoomLoopGuid = RoomLoop.LoopGuid;
	RoomFloorIndex = RoomLoop.FloorIndex;
	LocalFloorPolygon = MoveTemp(NewPolygon);
	FloorRegions = { MoveTemp(Region) };
	SetFloorAssignment(RoomLoop.FloorIndex, EEHBBuildingFloorElementRole::FloorFinish);

	const bool bRebuilt = RebuildFloorMesh();
	UpdateCollisionSettings();
	if (bCreateSurfaceRelations)
	{
		RefreshSurfaceFinishRelationsFromRoomLoop(RoomLoop);
	}
	if (bRebuilt) RecordOutlineSource(EEHBOutlineSource::RoomBoundary);
	MarkPackageDirty();
	return bRebuilt;
}

bool AEHB_Floor::ConfigureDefaultFloor(
	AEHBBuildingActorBase* InBuilding,
	const FTransform& LocalTransform,
	float InSize,
	int32 InFloorIndex)
{
	if (!InBuilding)
	{
		return false;
	}

	const float HalfSize = FMath::Max(10.0f, InSize) * 0.5f;
	LocalFloorPolygon = {
		FVector(-HalfSize, -HalfSize, 0.0f),
		FVector(-HalfSize, HalfSize, 0.0f),
		FVector(HalfSize, HalfSize, 0.0f),
		FVector(HalfSize, -HalfSize, 0.0f)
	};

	FEHBFloorFinishRegion Region;
	Region.OuterPolygon = LocalFloorPolygon;
	FloorRegions = { MoveTemp(Region) };
	RecordOutlineSource(EEHBOutlineSource::ManualOrUnclassified);
	RoomLoopGuid.Invalidate();
	RoomFloorIndex = INDEX_NONE;
	ClearSurfaceFinishRelations();

	AttachToBuilding(InBuilding, LocalTransform);
	SetFloorAssignment(FMath::Max(1, InFloorIndex), EEHBBuildingFloorElementRole::FloorFinish);
	const bool bRebuilt = RebuildFloorMesh();
	UpdateCollisionSettings();
	return bRebuilt;
}

bool AEHB_Floor::SetFloorRegions(const TArray<FEHBFloorFinishRegion>& InRegions, bool bRefreshRelations)
{
	if (!ApplyFloorRegions(InRegions, true))
	{
		return false;
	}
	RecordOutlineSource(EEHBOutlineSource::ManualOrUnclassified);
	FloorRegions = InRegions;
	LocalFloorPolygon.Reset();
	if (!FloorRegions.IsEmpty())
	{
		LocalFloorPolygon = FloorRegions[0].OuterPolygon;
	}

	UpdateCollisionSettings();
	if (bRefreshRelations)
	{
		FEHBBuildingClosedLoop RoomLoop;
		if (TryGetRoomLoop(RoomLoop))
		{
			RefreshSurfaceFinishRelationsFromRoomLoop(RoomLoop);
		}
	}
	MarkPackageDirty();
	return true;
}

bool AEHB_Floor::SetFloorRegionsIfNeeded(const TArray<FEHBFloorFinishRegion>& InRegions,bool& bOutChanged)
{
 bOutChanged=false;bool GeometryChanged=false;
 if(!ApplyFloorRegions(InRegions,true,&GeometryChanged))return false;
 bool Same=FloorRegions.Num()==InRegions.Num();
 for(int32 I=0;Same&&I<InRegions.Num();++I)
 {
  Same=FloorRegions[I].OuterPolygon==InRegions[I].OuterPolygon&&FloorRegions[I].Holes.Num()==InRegions[I].Holes.Num();
  for(int32 H=0;Same&&H<InRegions[I].Holes.Num();++H)Same=FloorRegions[I].Holes[H].LocalPolygon==InRegions[I].Holes[H].LocalPolygon;
 }
 Same&=!InRegions.IsEmpty()&&LocalFloorPolygon==InRegions[0].OuterPolygon;
 if(!Same||GeometryChanged)
 {
  SetFlags(RF_Transactional);Modify();FloorRegions=InRegions;LocalFloorPolygon=InRegions[0].OuterPolygon;
  RecordOutlineSource(EEHBOutlineSource::ManualOrUnclassified);MarkPackageDirty();bOutChanged=true;
 }
 return true;
}

bool AEHB_Floor::ValidateFloorRegions(const TArray<FEHBFloorFinishRegion>& InRegions) const
{
	if (!MeshComponent || InRegions.IsEmpty() || !FMath::IsFinite(VisualOffset)) return false;
	for (const auto& Region : InRegions)
	{
		TArray<FVector> Vertices, Normals;
		TArray<int32> Triangles;
		TArray<FVector2D> UVs;
		if (!BuildRegionMesh(Region, Vertices, Triangles, Normals, UVs)) return false;
	}
	return true;
}

bool AEHB_Floor::RebuildFloorMesh()
{
	if (!MeshComponent)
	{
		return false;
	}

	TArray<FEHBFloorFinishRegion> RegionsToBuild = FloorRegions;
	if (RegionsToBuild.IsEmpty() && LocalFloorPolygon.Num() >= 3)
	{
		FEHBFloorFinishRegion& Region = RegionsToBuild.AddDefaulted_GetRef();
		Region.OuterPolygon = LocalFloorPolygon;
	}

	return ApplyFloorRegions(RegionsToBuild, false);
}

bool AEHB_Floor::ApplyFloorRegions(const TArray<FEHBFloorFinishRegion>& InRegions, bool bRecordTransaction, bool* bOutChanged)
{
	if (!MeshComponent || InRegions.IsEmpty() || !FMath::IsFinite(VisualOffset))
	{
		return false;
	}
	if(bOutChanged)*bOutChanged=false;
	struct FPreparedRegion
	{
		TArray<FVector> Vertices, Normals;
		TArray<int32> Triangles;
		TArray<FVector2D> UVs;
	};
	TArray<FPreparedRegion> Prepared;
	Prepared.SetNum(InRegions.Num());
	// Validate every region before changing actor data, components or relationships.
	for (int32 I=0; I<InRegions.Num(); ++I)
	{
		auto& Mesh=Prepared[I];
		if (!BuildRegionMesh(InRegions[I], Mesh.Vertices, Mesh.Triangles, Mesh.Normals, Mesh.UVs))
			return false;
	}
 if(bOutChanged)
 {
  UMaterialInterface* Material=FloorMaterial.LoadSynchronous();if(!Material)Material=ResolveConfiguredDefaultWhiteBoxMaterial();
  const bool Collision=GetWorld()&&GetWorld()->IsGameWorld()?bEnableRuntimeCollision:bEnableEditorCollision;
  FCollisionResponseContainer Responses(ECR_Ignore);if(Collision){Responses.SetResponse(ECC_Pawn,ECR_Block);Responses.SetResponse(ECC_Visibility,ECR_Block);}
  bool Matches=RegionMeshComponents.Num()==Prepared.Num()&&!RegionMeshComponents.IsEmpty()&&RegionMeshComponents[0]==MeshComponent;
  for(int32 I=0;Matches&&I<Prepared.Num();++I)
  {
   const auto* C=RegionMeshComponents[I].Get();const auto& Mesh=Prepared[I];
   Matches=IsValid(C)&&C->GetNumSections()==1&&C->GetProcMeshSection(0)->SectionName==TEXT("FloorSurface")
    &&C->GetMaterial(0)==Material&&C->GetCollisionEnabled()==(Collision?ECollisionEnabled::QueryOnly:ECollisionEnabled::NoCollision)
    &&C->GetCollisionObjectType()==ECC_WorldStatic&&C->GetCollisionResponseToChannels()==Responses
    &&C->MatchesMeshSection(0,Mesh.Vertices,Mesh.Triangles,Mesh.Normals,Mesh.UVs,{},{},{},{},{},true);
  }
  if(Matches)return true;
 }
	if (bRecordTransaction)
	{
		SetFlags(RF_Transactional);
		Modify();
		for (auto* Component : GetComponents())
		{
			Component->SetFlags(RF_Transactional);
			Component->Modify();
		}
	}
	TArray<UEHBGeneratedMeshComponent*> Components;
	for (int32 I=0; I<Prepared.Num(); ++I)
	{
		auto* Component=GetOrCreateRegionMeshComponent(I);
		if (!Component) return false;
		Components.Add(Component);
	}
	for (int32 I=0; I<Prepared.Num(); ++I)
	{
		const auto& Mesh=Prepared[I];
		ApplyMeshToComponent(Components[I], Mesh.Vertices, Mesh.Triangles, Mesh.Normals, Mesh.UVs);
	}
	TrimRegionMeshComponents(Prepared.Num());
	if(bOutChanged)*bOutChanged=true;
	return true;
}

bool AEHB_Floor::BuildCurrentSurfaceFinishRelationPlan(const TArray<FEHBFloorFinishRegion>& InRegions,
 TArray<FEHBElementRelation>& OutRelations,FName& Status,
 const TMap<FGuid,TArray<FEHBFloorSupportSurface>>* CandidateHostSurfaces,
 const FEHBFloorHostTopologyDraft* HostTopology) const
{
 FEHBBuildingClosedLoop Context;
 if(OutlineSource==EEHBOutlineSource::RetainedRegion)Context.FloorIndex=RoomFloorIndex;
 else if(!TryGetRoomLoop(Context)){OutRelations.Reset();Status=TEXT("InvalidFinishRoom");return false;}
 return BuildSurfaceFinishRelationPlan(Context,InRegions,OutRelations,Status,CandidateHostSurfaces,HostTopology);
}

bool AEHB_Floor::BuildSurfaceFinishRelationPlan(const FEHBBuildingClosedLoop& RoomLoop,
 const TArray<FEHBFloorFinishRegion>& InRegions, TArray<FEHBElementRelation>& OutRelations,
 FName& Status, const TMap<FGuid,TArray<FEHBFloorSupportSurface>>* CandidateHostSurfaces,
 const FEHBFloorHostTopologyDraft* HostTopology) const
{
 OutRelations.Reset();
 auto Fail=[&](FName Reason){Status=Reason;return false;};
 const bool Independent=OutlineSource==EEHBOutlineSource::RetainedRegion;
 if(!OwningBuilding || !ElementGuid.IsValid() || RoomLoop.LoopGuid!=RoomLoopGuid
  || RoomLoop.FloorIndex!=RoomFloorIndex || FloorIndex!=RoomFloorIndex
  || (Independent?RoomLoopGuid.IsValid():!RoomLoopGuid.IsValid())) return Fail(TEXT("InvalidFinishRoom"));
 if(!ValidateFloorRegions(InRegions) || !GetElementLocalTransform().Equals(FTransform::Identity,0.0001))
  return Fail(TEXT("UnsupportedFinishCoverage"));
 TMap<FGuid,const FEHBElementRelation*> PreviousByHost;
 TSet<FGuid> CachedIds;
 for(FGuid Id:SurfaceFinishRelationGuids)
  if(!Id.IsValid() || CachedIds.Contains(Id))return Fail(TEXT("InvalidFinishRelationCache"));
  else CachedIds.Add(Id);
 for(const auto& Relation:OwningBuilding->ElementRelations)
 {
  const bool bOwned=Relation.Type==EEHBElementRelationType::SurfaceFinish
   && Relation.Target.Kind==EEHBRelationEndpointKind::BuildingElement && Relation.Target.ElementGuid==ElementGuid;
  if(CachedIds.Contains(Relation.RelationGuid) && !bOwned)return Fail(TEXT("ForeignFinishRelationCache"));
  if(!bOwned)continue;
  const auto* Host=OwningBuilding->FindElementActorByGuid(Relation.Source.ElementGuid);
  if(!CachedIds.Contains(Relation.RelationGuid) || PreviousByHost.Contains(Relation.Source.ElementGuid)
   || !Relation.Source.IsValid() || !Relation.Target.IsValid() || Relation.Source.Kind!=EEHBRelationEndpointKind::BuildingElement || !Host
   || !Host->HasAllCapabilities(static_cast<int32>(EEHBElementCapability::CanSupport))
   || Relation.Source.SurfaceKind!=EEHBElementSurfaceKind::Top || Relation.Target.SurfaceKind!=EEHBElementSurfaceKind::Bottom
   || Relation.Source.SurfaceName!=TEXT("FloorFinish.SourceTop") || Relation.Target.SurfaceName!=TEXT("FloorFinish.Bottom")
   || Relation.Source.SubIndex!=INDEX_NONE || Relation.Target.SubIndex!=INDEX_NONE
   || Relation.Origin!=EEHBRelationOrigin::SystemGenerated || !Relation.bEnabled || !Relation.bGeometryDependent
   || Relation.bAffectsFloorAssignment) return Fail(TEXT("UnsupportedFinishRelation"));
  PreviousByHost.Add(Relation.Source.ElementGuid,&Relation);
 }
 if(PreviousByHost.Num()!=CachedIds.Num())return Fail(TEXT("MissingFinishRelationCache"));
 FEHBElementQuery Query;Query.RequiredCapabilities=static_cast<int32>(EEHBElementCapability::CanSupport);
 auto Sources=OwningBuilding->QueryElements(Query);
 Sources.Sort([](const auto& A,const auto& B){return A.ElementGuid<B.ElementGuid;});
 TSet<FGuid> CurrentHosts;for(const auto* Host:Sources)if(Host!=this&&!Host->HasAllCapabilities(static_cast<int32>(EEHBElementCapability::SurfaceFinish)))CurrentHosts.Add(Host->ElementGuid);
 if(CandidateHostSurfaces)for(const auto& Pair:*CandidateHostSurfaces)
  if(!CurrentHosts.Contains(Pair.Key)||(HostTopology&&HostTopology->RemovedHostGuids.Contains(Pair.Key)))return Fail(TEXT("InvalidCandidateHostSurfaces"));
 if(HostTopology)
 {
  for(FGuid Id:HostTopology->RemovedHostGuids)if(!Id.IsValid()||!CurrentHosts.Contains(Id))return Fail(TEXT("InvalidRemovedFinishHost"));
  for(const auto& Pair:HostTopology->NewHostSurfaces)
   if(!Pair.Key.IsValid()||Pair.Key==OwningBuilding->BuildingGuid||OwningBuilding->FindElementActorByGuid(Pair.Key)
    ||OwningBuilding->FindPhysicalPillarForNode(Pair.Key).IsValid()||HostTopology->RemovedHostGuids.Contains(Pair.Key)
    ||Pair.Value.IsEmpty()||(CandidateHostSurfaces&&CandidateHostSurfaces->Contains(Pair.Key)))return Fail(TEXT("InvalidNewFinishHost"));
 }
 if(!ContactCache)ContactCache=MakeShared<FEHBFloorContactCache>();
 // Keep live cache entries only. Preview-only physical identities must never enter
 // or evict the persistent contact cache, and removed hosts are still live pre-edit.
 ContactCache->RetainHosts(CurrentHosts);
 TArray<FGuid> HostIds=CurrentHosts.Array();
 if(HostTopology){HostIds.RemoveAll([&](FGuid Id){return HostTopology->RemovedHostGuids.Contains(Id);});for(const auto& Pair:HostTopology->NewHostSurfaces)HostIds.Add(Pair.Key);}
 HostIds.Sort();TArray<FEHBElementRelation> Planned;
 for(FGuid HostId:HostIds)
 {
  const auto* NewSurfaces=HostTopology?HostTopology->NewHostSurfaces.Find(HostId):nullptr;
  const auto* Candidate=CandidateHostSurfaces?CandidateHostSurfaces->Find(HostId):nullptr;
  TArray<FEHBFloorSupportSurface> Current;
  if(!NewSurfaces&&!Candidate&&!FEHBFloorContactGeometry::CaptureHorizontalTops(OwningBuilding->FindElementActorByGuid(HostId),Current,Status))return false;
  FEHBFloorContact Contact;
  if(NewSurfaces)
  {if(!FEHBFloorContactGeometry::Build(InRegions,*NewSurfaces,Contact,Status))return false;}
  else if(!ContactCache->Query(HostId,InRegions,Candidate?*Candidate:Current,Contact,Status))return false;
  if(Contact.Area<=0)continue;
  const auto* Previous=PreviousByHost.FindRef(HostId);
  FEHBElementRelation Relation=Previous?*Previous:FEHBElementRelation();
  if(!Previous)
  {
   Relation.RelationGuid=FGuid::NewGuid();Relation.Type=EEHBElementRelationType::SurfaceFinish;
   Relation.Source=FEHBElementRelationEndpoint::MakeElement(HostId,EEHBElementSurfaceKind::Top,TEXT("FloorFinish.SourceTop"));
   Relation.Target=FEHBElementRelationEndpoint::MakeElement(ElementGuid,EEHBElementSurfaceKind::Bottom,TEXT("FloorFinish.Bottom"));
   Relation.Origin=EEHBRelationOrigin::SystemGenerated;Relation.bGeometryDependent=true;Relation.bAffectsFloorAssignment=false;
  }
  Relation.ContactPoint=Contact.Point;
  Relation.ContactArea=Contact.Area;
  Relation.ContactNormal=FVector::UpVector;
  Relation.StringMetadata.Reset();
  if(Independent)Relation.StringMetadata.Add(TEXT("OutlineSource"),TEXT("RetainedRegion"));
  else Relation.StringMetadata.Add(TEXT("RoomLoopGuid"),RoomLoop.LoopGuid.ToString(EGuidFormats::DigitsWithHyphens));
  Relation.NumericMetadata.Add(TEXT("RoomFloorIndex"),RoomLoop.FloorIndex);
  Planned.Add(MoveTemp(Relation));
 }
 OutRelations=MoveTemp(Planned);Status=TEXT("Ready");return true;
}

bool AEHB_Floor::TryRefreshSurfaceFinishRelationsFromRoomLoop(const FEHBBuildingClosedLoop& RoomLoop, FName& Status)
{
 TArray<FEHBFloorFinishRegion> Coverage=FloorRegions;
 if(Coverage.IsEmpty() && LocalFloorPolygon.Num()>=3)Coverage.AddDefaulted_GetRef().OuterPolygon=LocalFloorPolygon;
 TArray<FEHBElementRelation> Planned;
 if(!BuildSurfaceFinishRelationPlan(RoomLoop,Coverage,Planned,Status))return false;
 // Plan validates the complete old ownership/cache before any graph mutation.
 // Existing equivalent endpoints are updated in place, preserving relation IDs.
 const auto PreviousGraph=OwningBuilding->ElementRelations;
 const auto PreviousCache=SurfaceFinishRelationGuids;
 Modify();TArray<FGuid> NextCache;
 for(const auto& Relation:Planned)
 {
  const FGuid Id=OwningBuilding->AddOrUpdateElementRelation(Relation,true);
  if(!Id.IsValid() || Id!=Relation.RelationGuid)
  {
   OwningBuilding->ElementRelations=PreviousGraph;OwningBuilding->RebuildElementAndRelationshipIndexes();
   Status=TEXT("FinishPublishFailedRestored");return false;
  }
  NextCache.Add(Id);
 }
 for(FGuid Id:PreviousCache)if(!NextCache.Contains(Id))OwningBuilding->RemoveElementRelation(Id);
 SurfaceFinishRelationGuids=MoveTemp(NextCache);Status=TEXT("Refreshed");return true;
}

void AEHB_Floor::RefreshSurfaceFinishRelationsFromRoomLoop(const FEHBBuildingClosedLoop& RoomLoop)
{
 FName Status;
 if(!TryRefreshSurfaceFinishRelationsFromRoomLoop(RoomLoop,Status))
  UE_LOG(LogTemp,Warning,TEXT("Floor finish relation refresh refused: %s"),*Status.ToString());
}

void AEHB_Floor::ClearSurfaceFinishRelations()
{
 Modify();
 if(OwningBuilding)
 {
  const TSet<FGuid> CachedIds(SurfaceFinishRelationGuids);
  TArray<FGuid> OwnedIds;
  for(const auto& Relation:OwningBuilding->ElementRelations)
   if(CachedIds.Contains(Relation.RelationGuid) && Relation.Type==EEHBElementRelationType::SurfaceFinish
    && Relation.Target.Kind==EEHBRelationEndpointKind::BuildingElement && Relation.Target.ElementGuid==ElementGuid)
    OwnedIds.Add(Relation.RelationGuid);
  for(FGuid Id:OwnedIds)OwningBuilding->RemoveElementRelation(Id);
 }
 SurfaceFinishRelationGuids.Reset();
}

bool AEHB_Floor::TryGetRoomLoop(FEHBBuildingClosedLoop& OutRoomLoop) const
{
	OutRoomLoop = FEHBBuildingClosedLoop();
	if (!OwningBuilding || !RoomLoopGuid.IsValid())
	{
		return false;
	}

	for (const FEHBBuildingClosedLoop& Loop : OwningBuilding->GetClosedLoopsByFloor(RoomFloorIndex))
	{
		if (Loop.LoopGuid == RoomLoopGuid)
		{
			OutRoomLoop = Loop;
			return true;
		}
	}
	return false;
}

void AEHB_Floor::UpdateCollisionSettings()
{
	TArray<UEHBGeneratedMeshComponent*> Components;
	if (MeshComponent)
	{
		Components.Add(MeshComponent);
	}
	for (UEHBGeneratedMeshComponent* RegionComponent : RegionMeshComponents)
	{
		if (RegionComponent)
		{
			Components.AddUnique(RegionComponent);
		}
	}

	bool bEnableCollision = false;
	const UWorld* World = GetWorld();
	if (World && World->IsGameWorld())
	{
		bEnableCollision = bEnableRuntimeCollision;
	}
	else
	{
		bEnableCollision = bEnableEditorCollision;
	}

	for (UEHBGeneratedMeshComponent* Component : Components)
	{
		Component->SetCollisionEnabled(bEnableCollision ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
		Component->SetCollisionObjectType(ECC_WorldStatic);
		Component->SetCollisionResponseToAllChannels(ECR_Ignore);
		if (bEnableCollision)
		{
			Component->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
			Component->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		}
	}
}

bool AEHB_Floor::BuildFloorFinishRegionsFromSupportSurfaces(
	const TArray<FVector>& RoomPolygon,
	const TArray<FEHBFloorSupportSurface>& SupportSurfaces,
	float LocalSurfaceZ,
	TArray<FEHBFloorFinishRegion>& OutRegions)
{
	OutRegions.Reset();
	if (RoomPolygon.Num() < 3 || SupportSurfaces.IsEmpty())
	{
		return false;
	}

	ClipperLib::Path RoomPath = ToFloorClipperPath(RoomPolygon);
	if (!IsUsableClipperPath(RoomPath))
	{
		return false;
	}

	ClipperLib::Paths SupportPaths;
	for (const FEHBFloorSupportSurface& Surface : SupportSurfaces)
	{
		AppendSupportSurfacePaths(Surface, SupportPaths);
	}
	if (SupportPaths.empty())
	{
		return false;
	}

	ClipperLib::Clipper IntersectionClipper;
	IntersectionClipper.AddPaths(SupportPaths, ClipperLib::ptSubject, true);
	IntersectionClipper.AddPath(RoomPath, ClipperLib::ptClip, true);

	ClipperLib::PolyTree SolutionTree;
	if (!IntersectionClipper.Execute(ClipperLib::ctIntersection, SolutionTree, ClipperLib::pftNonZero, ClipperLib::pftNonZero))
	{
		return false;
	}

	for (const ClipperLib::PolyNode* ChildNode : SolutionTree.Childs)
	{
		AddRegionsFromPolyNode(ChildNode, LocalSurfaceZ, OutRegions);
	}

	OutRegions.RemoveAll(
		[](const FEHBFloorFinishRegion& Region)
		{
			return Region.OuterPolygon.Num() < 3
				|| FMath::Abs(CalculateTwiceArea(To2DLoop(Region.OuterPolygon))) <= EHBFloorMinArea;
		});

	OutRegions.Sort(
		[](const FEHBFloorFinishRegion& A, const FEHBFloorFinishRegion& B)
		{
			return FMath::Abs(CalculateTwiceArea(To2DLoop(A.OuterPolygon)))
				> FMath::Abs(CalculateTwiceArea(To2DLoop(B.OuterPolygon)));
		});

	return !OutRegions.IsEmpty();
}

float AEHB_Floor::GetRenderSurfaceZ() const
{
	if (!FloorRegions.IsEmpty() && !FloorRegions[0].OuterPolygon.IsEmpty())
	{
		return FloorRegions[0].OuterPolygon[0].Z + FMath::Max(0.0f, VisualOffset);
	}

	if (LocalFloorPolygon.IsEmpty())
	{
		return FMath::Max(0.0f, VisualOffset);
	}
	return LocalFloorPolygon[0].Z + FMath::Max(0.0f, VisualOffset);
}

UEHBGeneratedMeshComponent* AEHB_Floor::GetOrCreateRegionMeshComponent(int32 RegionIndex)
{
	if (!MeshComponent || RegionIndex < 0)
	{
		return nullptr;
	}

	RegionMeshComponents.RemoveAll(
		[](const TObjectPtr<UEHBGeneratedMeshComponent>& Component)
		{
			return !Component;
		});

	RegionMeshComponents.Remove(MeshComponent);
	RegionMeshComponents.Insert(MeshComponent, 0);

	while (RegionMeshComponents.Num() <= RegionIndex)
	{
		const FName ComponentName(*FString::Printf(TEXT("FloorRegionMesh_%d"), RegionMeshComponents.Num()));
		UEHBGeneratedMeshComponent* NewComponent = NewObject<UEHBGeneratedMeshComponent>(this, ComponentName, RF_Transactional);
		if (!NewComponent)
		{
			return nullptr;
		}

		NewComponent->CreationMethod = EComponentCreationMethod::Instance;
		NewComponent->SetupAttachment(SceneRoot);
		NewComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		NewComponent->SetCollisionObjectType(ECC_WorldStatic);
		NewComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
		NewComponent->ComponentTags.AddUnique(TEXT("EHB_Floor"));
		NewComponent->ComponentTags.AddUnique(TEXT("EHB_FloorRegion"));
		AddInstanceComponent(NewComponent);
		NewComponent->RegisterComponent();
		RegionMeshComponents.Add(NewComponent);
	}

	return RegionMeshComponents[RegionIndex].Get();
}

void AEHB_Floor::TrimRegionMeshComponents(int32 DesiredCount)
{
	const int32 SafeDesiredCount = FMath::Max(1, DesiredCount);
	if (MeshComponent)
	{
		RegionMeshComponents.Remove(MeshComponent);
		RegionMeshComponents.Insert(MeshComponent, 0);
	}

	for (int32 Index = RegionMeshComponents.Num() - 1; Index >= SafeDesiredCount; --Index)
	{
		UEHBGeneratedMeshComponent* Component = RegionMeshComponents[Index].Get();
		RegionMeshComponents.RemoveAt(Index);
		if (Component && Component != MeshComponent)
		{
			Component->ClearAllMeshSections();
			RemoveInstanceComponent(Component);
			Component->DestroyComponent();
		}
	}
}

bool AEHB_Floor::BuildRegionMesh(
	const FEHBFloorFinishRegion& Region,
	TArray<FVector>& Vertices,
	TArray<int32>& Triangles,
	TArray<FVector>& Normals,
	TArray<FVector2D>& UVs) const
{
	if (Region.OuterPolygon.Num() < 3)
	{
		return false;
	}

	const double SurfaceZ = Region.OuterPolygon[0].Z;
	auto IsPlanarFiniteLoop = [SurfaceZ](const TArray<FVector>& Loop)
	{
		return Loop.Num() >= 3 && !Loop.ContainsByPredicate([SurfaceZ](const FVector& P)
		{
			return P.ContainsNaN() || FMath::Abs(P.Z-SurfaceZ) > 0.001;
		});
	};
	if (!IsPlanarFiniteLoop(Region.OuterPolygon)) return false;
	for (const auto& Hole : Region.Holes)
		if (!IsPlanarFiniteLoop(Hole.LocalPolygon)) return false;

	TArray<FVector2d> OuterLoop = To2DLoop(Region.OuterPolygon);
	if (OuterLoop.Num() < 3 || FMath::Abs(CalculateTwiceArea(OuterLoop)) <= UE_DOUBLE_SMALL_NUMBER)
	{
		return false;
	}

	TArray<TArray<FVector2d>> HoleLoops;
	HoleLoops.Reserve(Region.Holes.Num());
	for (const FEHBFloorFinishHole& Hole : Region.Holes)
	{
		TArray<FVector2d> HoleLoop = To2DLoop(Hole.LocalPolygon);
		if (HoleLoop.Num() < 3 || FMath::Abs(CalculateTwiceArea(HoleLoop)) <= UE_DOUBLE_SMALL_NUMBER) return false;
		HoleLoops.Add(MoveTemp(HoleLoop));
	}

	FBox2d Bounds(ForceInit);
	for (const FVector2d& Point : OuterLoop)
	{
		Bounds += Point;
	}

	const double ArrangementTolerance = FMath::Max(Bounds.GetSize().X, Bounds.GetSize().Y) / 128.0;
	UE::Geometry::FArrangement2d Arrangement(FMath::Max(0.01, ArrangementTolerance));
	InsertClosedLoop(Arrangement, OuterLoop);
	for (const TArray<FVector2d>& HoleLoop : HoleLoops)
	{
		InsertClosedLoop(Arrangement, HoleLoop);
	}

	UE::Geometry::FConstrainedDelaunay2d Triangulator;
	Triangulator.FillRule = UE::Geometry::FConstrainedDelaunay2d::EFillRule::Odd;
	Triangulator.bOrientedEdges = false;
	Triangulator.bSplitBowties = true;
	Triangulator.Add(Arrangement.Graph);

	const bool bSucceeded = Triangulator.Triangulate(
		[&OuterLoop, &HoleLoops](const TArray<FVector2d>& CandidateVertices, const UE::Geometry::FIndex3i& Triangle)
		{
			const FVector2d Centroid =
				(CandidateVertices[Triangle.A] + CandidateVertices[Triangle.B] + CandidateVertices[Triangle.C]) / 3.0;
			if (!IsPointInsidePolygon(Centroid, OuterLoop))
			{
				return false;
			}

			for (const TArray<FVector2d>& HoleLoop : HoleLoops)
			{
				if (IsPointInsidePolygon(Centroid, HoleLoop))
				{
					return false;
				}
			}
			return true;
		});

	if (!bSucceeded)
	{
		return false;
	}

	const int32 VertexCount = Triangulator.Vertices.Num();
	Vertices.Reset(VertexCount);
	Triangles.Reset(Triangulator.Triangles.Num() * 3);
	Normals.Reset(VertexCount);
	UVs.Reset(VertexCount);

	const float RenderZ = GetRegionLogicalZ(Region) + FMath::Max(0.0f, VisualOffset);
	for (const FVector2d& Vertex2D : Triangulator.Vertices)
	{
		Vertices.Add(FVector(Vertex2D.X, Vertex2D.Y, RenderZ));
		Normals.Add(FVector::UpVector);
		UVs.Add(FVector2D(Vertex2D.X / EHBFloorUVWorldSize, Vertex2D.Y / EHBFloorUVWorldSize));
	}

	for (const UE::Geometry::FIndex3i& Triangle : Triangulator.Triangles)
	{
		int32 A = Triangle.A;
		int32 B = Triangle.B;
		int32 C = Triangle.C;
		const FVector TriangleNormal = FVector::CrossProduct(Vertices[B] - Vertices[A], Vertices[C] - Vertices[A]).GetSafeNormal();
		// UE treats the clockwise side as the visible front face. For a horizontal floor
		// viewed from above, that winding has a mathematical cross product pointing down.
		if (FVector::DotProduct(TriangleNormal, FVector::UpVector) >= 0.0f)
		{
			Swap(B, C);
		}
		Triangles.Append({ A, B, C });
	}

	return Vertices.Num() >= 3 && Triangles.Num() >= 3;
}

void AEHB_Floor::ApplyMeshToComponent(
	UEHBGeneratedMeshComponent* TargetComponent,
	const TArray<FVector>& Vertices,
	const TArray<int32>& Triangles,
	const TArray<FVector>& Normals,
	const TArray<FVector2D>& UVs)
{
	if (!TargetComponent)
	{
		return;
	}

	TArray<FLinearColor> VertexColors;
	TArray<FProcMeshTangent> Tangents;
	VertexColors.Init(FLinearColor::White, Vertices.Num());
	Tangents.Init(FProcMeshTangent(), Vertices.Num());

	FEHBScopedGeneratedMeshUpdate ScopedMeshUpdate(TargetComponent);
	TargetComponent->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs, VertexColors, Tangents, true);
	TargetComponent->SetMeshSectionName(0, FName(TEXT("FloorSurface")));
	TargetComponent->ClearMeshSectionsFrom(1);
	UMaterialInterface* Material = FloorMaterial.LoadSynchronous();
	if (!Material)
	{
		Material = ResolveConfiguredDefaultWhiteBoxMaterial();
	}
	TargetComponent->SetMaterialIfChanged(0, Material);
	UpdateCollisionSettings();
}

FGuid AEHB_Floor::BuildOutlineSignature() const
{
	const bool Independent=OutlineSource==EEHBOutlineSource::RetainedRegion;
	if (!OwningBuilding || (Independent?RoomLoopGuid.IsValid():!RoomLoopGuid.IsValid()) || RoomFloorIndex!=FloorIndex || CutOperations.Num()>0) return FGuid();
	FEHBOutlineSignature Signature(Independent?TEXT("EHB.RetainedFloor.v1"):TEXT("EHB.FloorOutline.v1"),GetRootComponent(),OwningBuilding ? OwningBuilding->BuildingGuid : FGuid(),FloorIndex);
	FGuid Room=RoomLoopGuid;int32 RoomFloor=RoomFloorIndex;
	Signature.Data << Room << RoomFloor;
	Signature.Polygon(LocalFloorPolygon);
	int32 RegionCount=FloorRegions.Num();Signature.Data << RegionCount;
	for(const auto& Region:FloorRegions)
	{
		Signature.Polygon(Region.OuterPolygon);
		int32 HoleCount=Region.Holes.Num();Signature.Data << HoleCount;
		for(const auto& Hole:Region.Holes)Signature.Polygon(Hole.LocalPolygon);
	}
	return Signature.Finish();
}

void AEHB_Floor::RecordOutlineSource(EEHBOutlineSource Source)
{
	Modify();
	OutlineSource=Source;
	RecordedOutlineSignature=Source==EEHBOutlineSource::ManualOrUnclassified ? FGuid() : BuildOutlineSignature();
}

bool AEHB_Floor::IsRecordedOutlineUnchanged() const
{
	return OutlineSource!=EEHBOutlineSource::ManualOrUnclassified && RecordedOutlineSignature.IsValid()
		&& RecordedOutlineSignature==BuildOutlineSignature();
}

FEHBFloorContactCacheStats AEHB_Floor::GetContactCacheStats() const
{
 return ContactCache?ContactCache->GetStats():FEHBFloorContactCacheStats();
}
