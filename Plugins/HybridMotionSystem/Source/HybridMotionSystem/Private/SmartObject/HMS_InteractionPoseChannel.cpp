#include "SmartObject/HMS_InteractionPoseChannel.h"
#include "SmartObject/HMS_SmartObjectInteractionComponent.h"
#include "PoseSearch/PoseSearchContext.h"
#include "PoseSearch/PoseSearchSchema.h"
#include "PoseSearch/PoseSearchAssetIndexer.h"
#include "IObjectChooser.h"
#include "GameFramework/Pawn.h"
#include "MoverComponent.h"
#include "Animation/AnimInstance.h"

bool UHMS_InteractionPoseChannel::Finalize(UPoseSearchSchema* Schema)
{
 ChannelDataOffset=Schema->SchemaCardinality; ChannelCardinality=4; Schema->SchemaCardinality+=4;
 FBoneReference Root; Root.BoneName=TEXT("root");
 RootIndex=Schema->AddBoneReference(Root,UE::PoseSearch::DefaultRole,true);
 return RootIndex!=UE::PoseSearch::InvalidSchemaBoneIdx;
}
void UHMS_InteractionPoseChannel::BuildQuery(UE::PoseSearch::FSearchContext& Context) const
{
 auto Values=Context.EditFeatureVector();
 Values[ChannelDataOffset]=Values[ChannelDataOffset+1]=1.e6f;
 Values[ChannelDataOffset+2]=1; Values[ChannelDataOffset+3]=0;
 const auto* Chooser=Context.GetContext(UE::PoseSearch::DefaultRole);
 const UAnimInstance* Anim=Chooser ? Cast<UAnimInstance>(Chooser->GetFirstObjectParam()) : nullptr;
 const APawn* Pawn=Anim ? Cast<APawn>(Anim->GetOwningActor()) : nullptr;
 const auto* Interaction=Pawn ? Pawn->FindComponentByClass<UHMS_SmartObjectInteractionComponent>() : nullptr;
 const auto* Mover=Pawn ? Pawn->FindComponentByClass<UMoverComponent>() : nullptr;
 if (!Interaction || !Mover || !Interaction->IsBusy()) { return; }
 const FTransform Target=Interaction->GetProfileTargetTransform();
 const FQuat MeshRotation=(Mover->GetBaseVisualComponentTransform()*Pawn->GetActorTransform()).GetRotation();
 const FVector Remaining=MeshRotation.UnrotateVector(Target.GetLocation()-Pawn->GetNavAgentLocation());
 const float Yaw=FMath::DegreesToRadians(FMath::FindDeltaAngleDegrees(Pawn->GetActorRotation().Yaw,Target.Rotator().Yaw));
 Values[ChannelDataOffset]=Remaining.X; Values[ChannelDataOffset+1]=Remaining.Y;
 Values[ChannelDataOffset+2]=FMath::Cos(Yaw); Values[ChannelDataOffset+3]=FMath::Sin(Yaw);
}
bool UHMS_InteractionPoseChannel::IsFilterValid(TConstArrayView<float> Pose,TConstArrayView<float> Query,int32,const UE::PoseSearch::FPoseMetadata&) const
{
 const int32 I=ChannelDataOffset;
 const FVector2D P(Pose[I],Pose[I+1]),Q(Query[I],Query[I+1]);
 const float Tolerance=FMath::Max(PositionTolerance,0.25f*P.Size());
 return (P-Q).Size()<=Tolerance && Pose[I+2]*Query[I+2]+Pose[I+3]*Query[I+3]>=FMath::Cos(FMath::DegreesToRadians(FacingTolerance));
}
#if WITH_EDITOR
void UHMS_InteractionPoseChannel::FillWeights(TArrayView<float> Weights) const
{
 for(int32 I=0;I<4;++I) { Weights[ChannelDataOffset+I]=0.2f; }
}
bool UHMS_InteractionPoseChannel::IndexAsset(UE::PoseSearch::FAssetIndexer& Indexer) const
{
 float Contact=Indexer.GetPlayLength()-ContactEndOffset;
 Indexer.ProcessAllAnimNotifyEvents([&](TConstArrayView<FAnimNotifyEvent> Events)
 {
  for(const auto& Event:Events)
   { if(Event.NotifyName==TEXT("HMS.EntryContact")) { Contact=Event.GetTime(); return true; } }
  return false;
 });
 if(Contact<=0) { return false; }
 for(int32 I=Indexer.GetBeginSampleIdx();I<Indexer.GetEndSampleIdx();++I)
 {
  FVector P; FQuat Q; const float Offset=Contact-Indexer.CalculateSampleTime(I);
  if(!Indexer.GetSamplePosition(P,Offset,0,I,RootIndex,RootIndex,UE::PoseSearch::DefaultRole,UE::PoseSearch::DefaultRole)
   || !Indexer.GetSampleRotation(Q,Offset,0,I,RootIndex,RootIndex,UE::PoseSearch::DefaultRole,UE::PoseSearch::DefaultRole)) { return false; }
  auto V=Indexer.GetPoseVector(I); const float Yaw=FMath::DegreesToRadians(Q.Rotator().Yaw);
  V[ChannelDataOffset]=P.X; V[ChannelDataOffset+1]=P.Y;
  V[ChannelDataOffset+2]=FMath::Cos(Yaw); V[ChannelDataOffset+3]=FMath::Sin(Yaw);
 }
 return true;
}
#endif
