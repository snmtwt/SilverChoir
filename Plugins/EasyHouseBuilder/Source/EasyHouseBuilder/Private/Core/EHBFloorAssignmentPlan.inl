bool FEHBFloorAssignmentPlan::Build(const TArray<FEHBFloorAssignmentState>& Elements,
 const TArray<FEHBElementRelation>& Relations,TArray<FEHBFloorAssignmentState>& Out,FName& Status)
{
 auto Working=Elements;Out.Reset();
 Working.Sort([](const auto& A,const auto& B){return A.ElementGuid<B.ElementGuid;});
 TMap<FGuid,int32> Index;
 TMap<FGuid,EEHBBuildingFloorElementRole> DesiredRoles;
 for(int32 I=0;I<Working.Num();++I)
 {
  if(!Working[I].ElementGuid.IsValid()||Index.Contains(Working[I].ElementGuid)){Status=TEXT("InvalidFloorPlanIdentity");return false;}
  if(Working[I].FloorIndex<0||Working[I].FloorIndex>=MAX_int32){Status=TEXT("InvalidFloorPlanIndex");return false;}
  Index.Add(Working[I].ElementGuid,I);
  DesiredRoles.Add(Working[I].ElementGuid,Working[I].Role);
  // Derived values are outputs, never independent anchors. Starting from the
  // previous fixed point would let an unrooted cycle support itself forever.
  auto& E=Working[I];
  if(E.Policy==EEHBFloorAssignmentPolicy::Automatic&&E.Source==EEHBFloorAssignmentSource::DerivedFromSupport&&E.Role!=EEHBBuildingFloorElementRole::Foundation)
  {E.FloorIndex=0;E.Role=EEHBBuildingFloorElementRole::None;E.Source=EEHBFloorAssignmentSource::Unassigned;E.Candidates.Reset();E.Conflict=false;}
 }
 auto DeriveRole=[](EEHBBuildingElementType Type)
 {
  switch(Type)
  {
   case EEHBBuildingElementType::FoundationAndFloor:return EEHBBuildingFloorElementRole::FloorCeiling;
   case EEHBBuildingElementType::Roof:return EEHBBuildingFloorElementRole::Roof;
   case EEHBBuildingElementType::DoorWindow:return EEHBBuildingFloorElementRole::HostedElement;
   case EEHBBuildingElementType::Floor:return EEHBBuildingFloorElementRole::FloorFinish;
   case EEHBBuildingElementType::Railing:return EEHBBuildingFloorElementRole::Railing;
   case EEHBBuildingElementType::Stair:return EEHBBuildingFloorElementRole::VerticalConnector;
   default:return EEHBBuildingFloorElementRole::FloorBody;
  }
 };
 TMap<FGuid,TArray<const FEHBElementRelation*>> Incoming;
 for(const auto& R:Relations)
 {
  if(R.bEnabled&&R.bAffectsFloorAssignment&&R.Target.Kind==EEHBRelationEndpointKind::BuildingElement&&Index.Contains(R.Target.ElementGuid)
   &&(R.Type==EEHBElementRelationType::StructuralSupport||R.Type==EEHBElementRelationType::HostedElement||R.Type==EEHBElementRelationType::BoundaryAttachment))Incoming.FindOrAdd(R.Target.ElementGuid).Add(&R);
 }
 const TArray<const FEHBElementRelation*> Empty;
 for(int32 Pass=0;Pass<=Working.Num();++Pass)
 {
  bool Changed=false;
  for(auto& E:Working)
  {
   if(E.Policy!=EEHBFloorAssignmentPolicy::Automatic||E.Role==EEHBBuildingFloorElementRole::Foundation)continue;
   TSet<int32> CandidateSet;
   const auto* Edges=Incoming.Find(E.ElementGuid);
   for(const auto* Edge:Edges?*Edges:Empty)
   {
    const auto& R=*Edge;
    int32 Candidate=INDEX_NONE;
    if(R.Source.Kind==EEHBRelationEndpointKind::WorldGround||R.Source.Kind==EEHBRelationEndpointKind::ExternalActor)Candidate=1;
    else if(R.Source.Kind==EEHBRelationEndpointKind::BuildingElement)
    {
     const int32* Found=Index.Find(R.Source.ElementGuid);if(!Found)continue;const auto& S=Working[*Found];
     if(S.FloorIndex>=MAX_int32-1){Status=TEXT("FloorPlanIndexOverflow");return false;}
     if(S.Role!=EEHBBuildingFloorElementRole::Foundation&&S.Source==EEHBFloorAssignmentSource::Unassigned)continue;
     if(R.Type==EEHBElementRelationType::HostedElement)Candidate=S.FloorIndex>0?S.FloorIndex:INDEX_NONE;
     else if(R.Type==EEHBElementRelationType::BoundaryAttachment)Candidate=S.Role==EEHBBuildingFloorElementRole::FloorCeiling?S.FloorIndex+1:S.FloorIndex;
     else if(S.Role==EEHBBuildingFloorElementRole::Foundation)Candidate=1;
     else if(S.Role!=EEHBBuildingFloorElementRole::None)
     {
      const auto Desired=DesiredRoles.FindChecked(E.ElementGuid);
      const auto Role=Desired==EEHBBuildingFloorElementRole::None?DeriveRole(E.Type):Desired;
      Candidate=(Role==EEHBBuildingFloorElementRole::FloorCeiling||Role==EEHBBuildingFloorElementRole::Roof)?S.FloorIndex:S.FloorIndex+1;
     }
    }
    if(Candidate!=INDEX_NONE)CandidateSet.Add(Candidate);
   }
   auto Next=E;
   if(CandidateSet.IsEmpty())
   {
    if(E.Source!=EEHBFloorAssignmentSource::DerivedFromSupport)continue;
    Next.FloorIndex=0;Next.Role=EEHBBuildingFloorElementRole::None;Next.Source=EEHBFloorAssignmentSource::Unassigned;Next.Candidates.Reset();Next.Conflict=false;
   }
   else
   {
    Next.Candidates=CandidateSet.Array();Next.Candidates.Sort();Next.FloorIndex=FMath::Max(0,Next.Candidates[0]);
    if(Next.Role==EEHBBuildingFloorElementRole::None){const auto Desired=DesiredRoles.FindChecked(E.ElementGuid);Next.Role=Desired==EEHBBuildingFloorElementRole::None?DeriveRole(Next.Type):Desired;}
    Next.Source=EEHBFloorAssignmentSource::DerivedFromSupport;Next.Conflict=Next.Candidates.Num()>1;
   }
   if(Next.FloorIndex!=E.FloorIndex||Next.Role!=E.Role||Next.Source!=E.Source||Next.Candidates!=E.Candidates||Next.Conflict!=E.Conflict){E=MoveTemp(Next);Changed=true;}
  }
  if(!Changed){Out=MoveTemp(Working);Status=TEXT("Ready");return true;}
 }
 Status=TEXT("FloorAssignmentDidNotConverge");return false;
}
