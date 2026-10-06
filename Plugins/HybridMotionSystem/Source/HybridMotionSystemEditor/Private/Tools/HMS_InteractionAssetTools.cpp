#include "Tools/HMS_InteractionAssetTools.h"
#include "SmartObject/HMS_InteractionPoseChannel.h"
#include "Animation/AnimMontage.h"
#include "Animation/Skeleton.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Chooser.h"
#include "ObjectChooser_Asset.h"
#include "Misc/PackageName.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchDerivedData.h"
#include "AnimNotifyState_MotionWarping.h"
#include "PoseSearch/PoseSearchAnimNotifies.h"
#include "ScopedTransaction.h"
#include "PoseSearch/PoseSearchSchema.h"
#include "PoseSearch/PoseSearchIndex.h"
#include "PoseSearch/PoseSearchFeatureChannel_Position.h"
#include "PoseSearch/PoseSearchFeatureChannel_Velocity.h"
#include "GameplayInteractionStateTreeSchema.h"
#include "StateTree.h"
#include "StateTreeEditorData.h"
#include "StateTreeState.h"
#include "StateTreeCompiler.h"
#include "StateTreeCompilerLog.h"
#include "StateTree/SmartObjectTasks/HMS_ApproachAndEnterTask.h"
#include "SmartObject/HMS_SmartObjectInteractionProfile.h"
#include "Engine/UserDefinedEnum.h"
#include "Kismet2/EnumEditorUtils.h"

namespace
{
	bool CanCreate(const FString& Path)
	{
		return FPackageName::IsValidLongPackageName(Path) && !FPackageName::DoesPackageExist(Path)
			&& !FindPackage(nullptr, *Path);
	}
	template<typename T> T* NewAsset(const FString& Path)
	{
		return NewObject<T>(CreatePackage(*Path), *FPackageName::GetLongPackageAssetName(Path), RF_Public | RF_Standalone | RF_Transactional);
	}
	void Publish(UObject* Asset) { Asset->MarkPackageDirty(); FAssetRegistryModule::AssetCreated(Asset); }
}

UUserDefinedEnum* UHMS_InteractionAssetTools::CreateInteractionStateEnum(
	const FString& PackagePath, const TArray<FText>& StateNames)
{
	if (!CanCreate(PackagePath) || StateNames.IsEmpty() || StateNames.Num() > 255)
	{
		return nullptr;
	}
	TSet<FString> Names;
	for (const FText& Name : StateNames)
	{
		const FString DisplayName = Name.ToString().TrimStartAndEnd();
		if (DisplayName.IsEmpty() || Names.Contains(DisplayName))
		{
			return nullptr;
		}
		Names.Add(DisplayName);
	}
	UUserDefinedEnum* Enum = Cast<UUserDefinedEnum>(FEnumEditorUtils::CreateUserDefinedEnum(
		CreatePackage(*PackagePath), *FPackageName::GetLongPackageAssetName(PackagePath),
		RF_Public | RF_Standalone | RF_Transactional));
	if (!Enum) { return nullptr; }
	for (int32 Index = 0; Index < StateNames.Num(); ++Index)
	{
		FEnumEditorUtils::AddNewEnumeratorForUserDefinedEnum(Enum);
		FEnumEditorUtils::SetEnumeratorDisplayName(Enum, Index, StateNames[Index]);
	}
	Publish(Enum);
	return Enum;
}

FString UHMS_InteractionAssetTools::DescribeEntrySamples(UPoseSearchDatabase* Database)
{
 if (!Database || !Database->Schema) { return TEXT("Invalid database"); }
 FString Text;
 const auto& Index=Database->GetSearchIndex();
 for(int32 I=0;I<Index.PoseMetadata.Num();++I)
 {
  const auto V=Index.GetPoseValues(I);
  if(V.Num()>=4) { Text+=FString::Printf(TEXT("%d,%.6f,%.6f,%.6f,%.6f\n"),I,V[0],V[1],V[2],V[3]); }
 }
 return Text;
}

bool UHMS_InteractionAssetTools::AddEntryMontage(UPoseSearchDatabase* Database, UAnimMontage* Montage)
{
 if (!Database || !Montage || !Database->Schema) { return false; }
 for(int32 I=0;const auto* Entry=Database->GetDatabaseAnimationAsset(I);++I)
  { if(Entry->AnimAsset==Montage) { return RefreshEntryDatabase(Database); } }
 Database->Modify(); FPoseSearchDatabaseAnimationAsset Entry; Entry.AnimAsset=Montage;
 Database->AddAnimationAsset(Entry);
 return RefreshEntryDatabase(Database);
}

UChooserTable* UHMS_InteractionAssetTools::CreateEntryQueryAssets(UAnimMontage* Montage, const FString& Folder)
{
	const FString SchemaPath=Folder/TEXT("PSS_Entry"), DatabasePath=Folder/TEXT("PSD_Entry"), ChooserPath=Folder/TEXT("CHT_Entry");
	if (!Montage || !Montage->GetSkeleton() || !CanCreate(SchemaPath) || !CanCreate(DatabasePath) || !CanCreate(ChooserPath)) { return nullptr; }
	USkeleton* Skeleton=Montage->GetSkeleton();
	for (FName Bone : {FName("foot_l"),FName("foot_r"),FName("pelvis")})
	{
		if (Skeleton->GetReferenceSkeleton().FindBoneIndex(Bone)==INDEX_NONE) { return nullptr; }
	}
	auto* Schema=NewAsset<UPoseSearchSchema>(SchemaPath);
	Schema->AddSkeleton(Skeleton);
	Schema->SampleRate=30;
 Schema->AddChannel(NewObject<UHMS_InteractionPoseChannel>(Schema));
	for (FName Bone : {FName("foot_l"),FName("foot_r"),FName("pelvis")})
	{
		auto* Position=NewObject<UPoseSearchFeatureChannel_Position>(Schema);
		Position->Bone.BoneName=Bone;
		Position->InputQueryPose=EInputQueryPose::UseCharacterPose;
		Schema->AddChannel(Position);
		auto* Velocity=NewObject<UPoseSearchFeatureChannel_Velocity>(Schema);
		Velocity->Bone.BoneName=Bone;
		Velocity->InputQueryPose=EInputQueryPose::UseCharacterPose;
		Schema->AddChannel(Velocity);
	}
	Schema->PostEditChange();
	auto* Database=NewAsset<UPoseSearchDatabase>(DatabasePath);
	Database->Schema=Schema;
	Database->PoseSearchMode=EPoseSearchMode::BruteForce;
	FPoseSearchDatabaseAnimationAsset Entry;
	Entry.AnimAsset=Montage;
	Database->AddAnimationAsset(Entry);
	Database->PostEditChange();
	if (!RefreshEntryDatabase(Database)) { return nullptr; }
	auto* Chooser=NewAsset<UChooserTable>(ChooserPath);
	Chooser->OutputObjectType=UPoseSearchDatabase::StaticClass();
	FInstancedStruct Result=FInstancedStruct::Make<FAssetChooser>();
	Result.GetMutable<FAssetChooser>().Asset=Database;
	Chooser->ResultsStructs.Add(Result);
	Chooser->Compile(true);
	Publish(Schema); Publish(Database); Publish(Chooser);
	return Chooser;
}

UStateTree* UHMS_InteractionAssetTools::CreateEntryStateTree(UHMS_SmartObjectInteractionProfile* Profile, const FString& PackagePath)
{
	if (!Profile || !Profile->EntryDatabaseChooser || !CanCreate(PackagePath)) { return nullptr; }
	auto* Tree=NewAsset<UStateTree>(PackagePath);
	auto* Data=NewObject<UStateTreeEditorData>(Tree, NAME_None, RF_Transactional);
	Tree->EditorData=Data;
	Data->Schema=NewObject<UGameplayInteractionStateTreeSchema>(Data);
	UStateTreeState& Root=Data->AddRootState();
	Root.Name=TEXT("Approach and enter");
	Root.AddTask<FHMS_ApproachAndEnterTask>().GetInstanceData().Profile=Profile;
	Root.AddTransition(EStateTreeTransitionTrigger::OnStateSucceeded,EStateTreeTransitionType::Succeeded);
	Root.AddTransition(EStateTreeTransitionTrigger::OnStateFailed,EStateTreeTransitionType::Failed);
	FStateTreeCompilerLog Log;
	FStateTreeCompiler Compiler(Log);
	if (!Compiler.Compile(*Tree)) { return nullptr; }
	Publish(Tree);
	return Tree;
}

bool UHMS_InteractionAssetTools::SetEntryQueryRange(UPoseSearchDatabase* Database, UAnimMontage* Montage, float SearchStart, float SearchEnd)
{
	if (!Database || !Database->Schema || !Montage || !FMath::IsFinite(SearchStart) || !FMath::IsFinite(SearchEnd)
		|| Montage->SlotAnimTracks.Num()!=1 || Montage->SlotAnimTracks[0].AnimTrack.AnimSegments.Num()!=1) { return false; }
	const auto& Segment=Montage->SlotAnimTracks[0].AnimTrack.AnimSegments[0];
	const UAnimSequenceBase* Source=Segment.GetAnimReference();
	if (!Source || !FMath::IsNearlyZero(Segment.StartPos) || !FMath::IsNearlyZero(Segment.AnimStartTime)
		|| !FMath::IsNearlyEqual(Segment.AnimPlayRate,1.f) || Segment.LoopingCount!=1) { return false; }
	const float Margin=0.5f/FMath::Max(Database->Schema->SampleRate,1);
	if (SearchStart<0 || SearchEnd<=SearchStart+2*Margin || SearchEnd>Montage->GetPlayLength()) { return false; }
	bool bInsideWarp=false;
	for (const FAnimNotifyEvent& Event : Source->Notifies)
	{
		if (Event.NotifyName==TEXT("HMS.EntryWarp") && Cast<UAnimNotifyState_MotionWarping>(Event.NotifyStateClass)
			&& SearchStart>=Event.GetTime() && SearchEnd<Event.GetTime()+Event.GetDuration()) { bInsideWarp=true; }
	}
	if (!bInsideWarp) { return false; }
	TArray<int32> MatchingIndices;
	for (int32 Index=0; const auto* Entry=Database->GetDatabaseAnimationAsset(Index); ++Index)
	{
		if (Entry->AnimAsset==Montage) { MatchingIndices.Add(Index); }
	}
	if (MatchingIndices.IsEmpty()) { return false; }
	const FScopedTransaction Transaction(NSLOCTEXT("HMSInteractionAssets","QueryRange","HMS 设置进入查询时段"));
	Database->Modify();
	for (int32 Index : MatchingIndices)
	{
		FPoseSearchDatabaseAnimationAsset Entry=*Database->GetDatabaseAnimationAsset(Index);
		Entry.SamplingRange=FFloatInterval(SearchStart+Margin,SearchEnd-Margin);
		Database->SetAnimationAssetAt(Entry,Index);
	}
	Database->PostEditChange();
	Database->MarkPackageDirty();
	using namespace UE::PoseSearch;
	return FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(Database,
		ERequestAsyncBuildFlag::NewRequest | ERequestAsyncBuildFlag::WaitForCompletion)==EAsyncBuildIndexResult::Success;
}

bool UHMS_InteractionAssetTools::RefreshEntryDatabase(UPoseSearchDatabase* Database)
{
	if (!Database || !Database->Schema) { return false; }
	TArray<FPoseSearchDatabaseAnimationAsset> Entries;
	for (int32 Index=0; const auto* Original=Database->GetDatabaseAnimationAsset(Index); ++Index)
	{
		const auto* Montage=Cast<UAnimMontage>(Original->AnimAsset);
		if (!Montage || Montage->SlotAnimTracks.Num()!=1 || Montage->SlotAnimTracks[0].AnimTrack.AnimSegments.Num()!=1) { return false; }
		const auto& Segment=Montage->SlotAnimTracks[0].AnimTrack.AnimSegments[0];
		const UAnimSequenceBase* Source=Segment.GetAnimReference();
		if (!Source || !FMath::IsNearlyZero(Segment.StartPos) || !FMath::IsNearlyZero(Segment.AnimStartTime)
			|| !FMath::IsNearlyEqual(Segment.AnimPlayRate,1.f) || Segment.LoopingCount!=1) { return false; }
		float Start=-1, End=-1, Contact=-1;
		for (const FAnimNotifyEvent& Event : Source->Notifies)
		{
			if (Event.NotifyName==TEXT("HMS.EntryWarp") && Cast<UAnimNotifyState_MotionWarping>(Event.NotifyStateClass))
			{ Start=Event.GetTime(); Contact=Start+Event.GetDuration(); }
			if (Event.NotifyName==TEXT("HMS.BlockAfterEntry") && Cast<UAnimNotifyState_PoseSearchBlockTransition>(Event.NotifyStateClass)) { End=Event.GetTime(); }
		}
		const float Margin=0.5f/Database->Schema->SampleRate;
		if (Start<0 || End<=Start+2*Margin || Contact<=End || Contact>Montage->GetPlayLength()) { return false; }
		FPoseSearchDatabaseAnimationAsset Entry=*Original;
		// Montage indexing does not inherit source sequence notifies. Restrict the searchable
		// range explicitly from the authoring markers; playback still continues to montage end.
		Entry.SamplingRange=FFloatInterval(Start+Margin,End-Margin);
		Entries.Add(Entry);
	}
	if (Entries.IsEmpty()) { return false; }
	const FScopedTransaction Transaction(NSLOCTEXT("HMSInteractionAssets","Refresh","HMS 更新进入动画查询范围"));
	Database->Modify();
	for (int32 Index=0; Index<Entries.Num(); ++Index)
 {
  Database->SetAnimationAssetAt(Entries[Index],Index);
  auto* Montage=CastChecked<UAnimMontage>(Entries[Index].AnimAsset);
  const UAnimSequenceBase* Source=Montage->SlotAnimTracks[0].AnimTrack.AnimSegments[0].GetAnimReference();
  for(const FAnimNotifyEvent& Event:Source->Notifies)
  {
   if(Event.NotifyName==TEXT("HMS.EntryWarp"))
   {
    Montage->Modify();
    Montage->Notifies.RemoveAll([](const FAnimNotifyEvent& E) { return E.NotifyName==TEXT("HMS.EntryContact"); });
    FAnimNotifyEvent& Marker=Montage->Notifies.AddDefaulted_GetRef();
    Marker.NotifyName=TEXT("HMS.EntryContact"); Marker.Link(Montage,Event.GetTime()+Event.GetDuration());
    Montage->PostEditChange(); Montage->MarkPackageDirty();
    break;
   }
  }
 }
	Database->PostEditChange();
	Database->MarkPackageDirty();
	using namespace UE::PoseSearch;
	return FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(Database,
		ERequestAsyncBuildFlag::NewRequest | ERequestAsyncBuildFlag::WaitForCompletion)==EAsyncBuildIndexResult::Success;
}
