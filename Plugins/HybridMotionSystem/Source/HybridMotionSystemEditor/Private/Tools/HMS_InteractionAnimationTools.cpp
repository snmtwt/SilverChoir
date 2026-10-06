#include "Tools/HMS_InteractionAnimationTools.h"
#include "Tools/HMS_InteractionAssetTools.h"
#include "PoseSearch/PoseSearchDatabase.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "AnimationBlueprintLibrary.h"
#include "AnimNotifyState_MotionWarping.h"
#include "PoseSearch/PoseSearchAnimNotifies.h"
#include "RootMotionModifier_SkewWarp.h"
#include "ScopedTransaction.h"
#include "IDetailsView.h"
#include "PropertyEditorModule.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "UObject/StrongObjectPtr.h"

#define LOCTEXT_NAMESPACE "HMSInteractionAnimationTools"

namespace
{
	const FName TrackName(TEXT("HMS_Interaction"));
	const FName WarpName(TEXT("HMS.EntryWarp"));
	const FName BeforeName(TEXT("HMS.BlockBeforeEntry"));
	const FName AfterName(TEXT("HMS.BlockAfterEntry"));

	bool IsOwned(const UAnimSequence& Sequence, const FAnimNotifyEvent& Event)
	{
		return Sequence.AnimNotifyTracks.IsValidIndex(Event.TrackIndex)
			&& Sequence.AnimNotifyTracks[Event.TrackIndex].TrackName == TrackName
			&& ((Event.NotifyName == WarpName && Cast<UAnimNotifyState_MotionWarping>(Event.NotifyStateClass))
				|| ((Event.NotifyName == BeforeName || Event.NotifyName == AfterName)
					&& Cast<UAnimNotifyState_PoseSearchBlockTransition>(Event.NotifyStateClass)));
	}

	struct FRootSamples
	{
		TArray<float> Times;
		TArray<FTransform> Transforms;
		float MotionEnd = 0.0f;
		float Distance = 0.0f;
		float Turn = 0.0f;
	};

	FRootSamples SampleRoot(const UAnimSequence& Sequence, float ExtraTime = -1.0f)
	{
		FRootSamples Samples;
		const int32 Frames = FMath::Max(1, Sequence.GetNumberOfSampledKeys() - 1);
		const float Length = Sequence.GetPlayLength();
		for (int32 Frame = 0; Frame <= Frames; ++Frame)
		{
			Samples.Times.Add(Length * Frame / Frames);
		}
		if (ExtraTime >= 0.0f && ExtraTime <= Length)
		{
			Samples.Times.AddUnique(ExtraTime);
			Samples.Times.Sort();
		}
		for (const float Time : Samples.Times)
		{
			const FAnimExtractContext ExtractionContext(Time, false, {}, false);
			Samples.Transforms.Add(Sequence.ExtractRootTrackTransform(ExtractionContext, nullptr));
			if (Samples.Transforms.Num() > 1)
			{
				const FTransform& Previous = Samples.Transforms[Samples.Transforms.Num() - 2];
				const FTransform& Current = Samples.Transforms.Last();
				const float Distance = FVector::Distance(Previous.GetLocation(), Current.GetLocation());
				const float Turn = FMath::Abs(FMath::FindDeltaAngleDegrees(Previous.Rotator().Yaw, Current.Rotator().Yaw));
				Samples.Distance += Distance;
				Samples.Turn += Turn;
				if (Distance > 0.05f || Turn > 0.05f)
				{
					Samples.MotionEnd = Time;
				}
			}
		}
		return Samples;
	}

	void BakeCurves(UAnimSequence& Sequence, float ContactTime)
	{
		const FRootSamples Samples = SampleRoot(Sequence, ContactTime);
		TArray<float> Remaining;
		Remaining.SetNumZeroed(Samples.Times.Num());
		for (int32 Index = Samples.Times.Num() - 2; Index >= 0; --Index)
		{
			Remaining[Index] = Remaining[Index + 1];
			if (Samples.Times[Index + 1] <= ContactTime)
			{
				Remaining[Index] += FVector::Distance(Samples.Transforms[Index].GetLocation(), Samples.Transforms[Index + 1].GetLocation());
			}
		}
		const FName Names[] = {TEXT("HMS_Interaction_RootX"), TEXT("HMS_Interaction_RootY"),
			TEXT("HMS_Interaction_RootZ"), TEXT("HMS_Interaction_RootYaw"), TEXT("HMS_Interaction_DistanceRemaining")};
		TArray<FRichCurveKey> Keys[5];
		float Yaw = 0.0f;
		for (int32 Index = 0; Index < Samples.Times.Num(); ++Index)
		{
			const FVector Position = Samples.Transforms[0].InverseTransformPosition(Samples.Transforms[Index].GetLocation());
			if (Index > 0)
			{
				Yaw += FMath::FindDeltaAngleDegrees(Samples.Transforms[Index - 1].Rotator().Yaw, Samples.Transforms[Index].Rotator().Yaw);
			}
			const float Values[] = {float(Position.X), float(Position.Y), float(Position.Z), Yaw, Remaining[Index]};
			for (int32 Curve = 0; Curve < 5; ++Curve)
			{
				FRichCurveKey& Key = Keys[Curve].Emplace_GetRef(Samples.Times[Index], Values[Curve]);
				Key.InterpMode = RCIM_Linear;
			}
		}
		IAnimationDataController& Controller = Sequence.GetController();
		Controller.OpenBracket(LOCTEXT("Bake", "生成智能对象轨迹参考曲线"));
		for (int32 Curve = 0; Curve < 5; ++Curve)
		{
			const FAnimationCurveIdentifier Id(Names[Curve], ERawCurveTrackTypes::RCT_Float);
			if (!Sequence.GetDataModel()->FindFloatCurve(Id))
			{
				Controller.AddCurve(Id);
			}
			Controller.SetCurveKeys(Id, Keys[Curve]);
		}
		Controller.CloseBracket();
	}

	class SHMSInteractionAnimationWindow : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SHMSInteractionAnimationWindow) {}
			SLATE_ARGUMENT(UAnimSequence*, Animation)
		SLATE_END_ARGS()
		void Construct(const FArguments& Args)
		{
			Settings.Reset(UHMS_InteractionAnimationTools::SuggestSettings(Args._Animation));
			FDetailsViewArgs DetailsArgs;
			DetailsArgs.bHideSelectionTip = true;
			DetailsArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
			Details = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor")).CreateDetailView(DetailsArgs);
			Details->SetObject(Settings.Get());
			ChildSlot
			[
				SNew(SBorder).Padding(12)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)
					[SNew(STextBlock).AutoWrapText(true).Text(UHMS_InteractionAnimationTools::Analyze(Args._Animation))]
					+ SVerticalBox::Slot().FillHeight(1)[Details.ToSharedRef()]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 8)
					[SAssignNew(Status, STextBlock).AutoWrapText(true).Text(LOCTEXT("Review", "初始范围只按根运动估算。请按转身前姿态与贴靠帧调整。生成支持撤销，需要手动保存动画；不会自动修改状态树或查询表。"))]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
					[SNew(SButton).Text(LOCTEXT("Generate", "一键生成 / 更新通知与轨迹"))
						.OnClicked_Lambda([this]() { FText Result; UHMS_InteractionAnimationTools::Generate(Settings.Get(), Result); Status->SetText(Result); return FReply::Handled(); })]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 6).HAlign(HAlign_Right)
					[SNew(SButton).Text(LOCTEXT("RefreshQuery", "更新进入查询范围与索引"))
						.IsEnabled_Lambda([this]() { return Settings->EntryDatabase != nullptr; })
						.OnClicked_Lambda([this]()
						{
							Status->SetText(UHMS_InteractionAssetTools::RefreshEntryDatabase(Settings->EntryDatabase)
								? LOCTEXT("RefreshOK", "已按数据库各 Montage 的源动画通知更新查询范围并重建索引。请保存数据库。")
								: LOCTEXT("RefreshFailed", "更新失败：需要单段、原速、非循环 Montage，以及有效的 HMS 进入窗口与接入结束通知。请检查数据库和源动画。"));
							return FReply::Handled();
						})]
				]
			];
		}
	private:
		TStrongObjectPtr<UHMS_InteractionAnimationSettings> Settings;
		TSharedPtr<IDetailsView> Details;
		TSharedPtr<STextBlock> Status;
	};
}

UHMS_InteractionAnimationSettings* UHMS_InteractionAnimationTools::SuggestSettings(UAnimSequence* Animation)
{
	UHMS_InteractionAnimationSettings* Settings = NewObject<UHMS_InteractionAnimationSettings>();
	Settings->Animation = Animation;
	if (Animation && Animation->GetPlayLength() > 0.0f)
	{
		const FRootSamples Samples = SampleRoot(*Animation);
		Settings->ContactTime = Samples.MotionEnd > 0.0f ? Samples.MotionEnd : Animation->GetPlayLength();
		Settings->SearchStart = FMath::Max(0.0f, Settings->ContactTime - 0.6f);
		Settings->SearchEnd = FMath::Max(Settings->SearchStart, Settings->ContactTime - 0.2f);
		for (const FAnimNotifyEvent& Event : Animation->Notifies)
		{
			if (IsOwned(*Animation, Event))
			{
				if (Event.NotifyName == WarpName)
				{
					Settings->SearchStart = Event.GetTime();
					Settings->ContactTime = Event.GetTime() + Event.GetDuration();
					const auto* Notify = CastChecked<UAnimNotifyState_MotionWarping>(Event.NotifyStateClass);
					if (const auto* Modifier = Cast<URootMotionModifier_SkewWarp>(Notify->RootMotionModifier))
					{
						Settings->WarpTargetName = Modifier->WarpTargetName;
					}
				}
				else if (Event.NotifyName == AfterName) { Settings->SearchEnd = Event.GetTime(); }
			}
		}
	}
	return Settings;
}

FText UHMS_InteractionAnimationTools::Analyze(UAnimSequence* Animation)
{
	if (!Animation) { return LOCTEXT("NoAnimation", "请打开动画序列后使用此工具。"); }
	const FRootSamples Samples = SampleRoot(*Animation);
	return FText::FromString(FString::Printf(TEXT("%s：%.3f 秒，根骨累计位移 %.1f cm，累计 Yaw %.1f°，最后明显根运动约 %.3f 秒。\n轨迹从现有根骨提取；根骨不转不代表身体没有转身。接触时刻需要查看姿态确认。"),
		*Animation->GetName(), Animation->GetPlayLength(), Samples.Distance, Samples.Turn, Samples.MotionEnd));
}

bool UHMS_InteractionAnimationTools::Generate(UHMS_InteractionAnimationSettings* Settings, FText& Result)
{
	UAnimSequence* Sequence = Settings ? Settings->Animation.Get() : nullptr;
	if (!Sequence || !Sequence->GetSkeleton() || !Sequence->GetDataModel())
	{
		Result = LOCTEXT("InvalidAsset", "需要带有效骨架和动画数据的动画序列。"); return false;
	}
	const float Length = Sequence->GetPlayLength();
	if (!FMath::IsFinite(Settings->SearchStart) || !FMath::IsFinite(Settings->SearchEnd) || !FMath::IsFinite(Settings->ContactTime)
		|| Settings->SearchStart < 0 || Settings->SearchStart >= Settings->SearchEnd
		|| Settings->SearchEnd >= Settings->ContactTime || Settings->ContactTime > Length || Settings->WarpTargetName.IsNone())
	{
		Result = LOCTEXT("InvalidTimes", "时间必须满足：0 ≤ 接入开始 < 接入结束 < 对齐结束 ≤ 动画长度，且目标名称不能为空。"); return false;
	}
	if (SampleRoot(*Sequence).Distance < 0.1f)
	{
		Result = LOCTEXT("NoTranslation", "动画缺少有效根位移。请先在 DCC / 重定向器中修复根轨迹；本工具不会凭空生成进入位移。"); return false;
	}
	for (const FAnimNotifyEvent& Event : Sequence->Notifies)
	{
		if (!IsOwned(*Sequence, Event) && Cast<UAnimNotifyState_MotionWarping>(Event.NotifyStateClass)
			&& Event.GetTime() < Settings->ContactTime && Event.GetTime() + Event.GetDuration() > Settings->SearchStart)
		{
			Result = LOCTEXT("Conflict", "此范围已有其他 Motion Warping 窗口，请先检查或移动现有窗口，避免双重变形。"); return false;
		}
	}
	const FScopedTransaction Transaction(LOCTEXT("Transaction", "HMS 生成智能对象进入通知与轨迹"));
	Sequence->Modify();
	Sequence->Notifies.RemoveAll([Sequence](const FAnimNotifyEvent& Event) { return IsOwned(*Sequence, Event); });
	TArray<FName> Tracks;
	UAnimationBlueprintLibrary::GetAnimationNotifyTrackNames(Sequence, Tracks);
	if (!Tracks.Contains(TrackName)) { UAnimationBlueprintLibrary::AddAnimationNotifyTrack(Sequence, TrackName, FLinearColor(0.15f, 0.65f, 0.8f)); }
	auto AddState = [Sequence](FName Name, float Start, float End, TSubclassOf<UAnimNotifyState> Class)
	{
		UAnimNotifyState* Notify = UAnimationBlueprintLibrary::AddAnimationNotifyStateEvent(Sequence, TrackName, Start, End - Start, Class);
		// The library refreshes and sorts the event array before returning.
		for (FAnimNotifyEvent& Event : Sequence->Notifies)
		{
			if (Event.NotifyStateClass == Notify) { Event.NotifyName = Name; break; }
		}
		return Notify;
	};
	if (Settings->SearchStart > 0)
	{
		AddState(BeforeName, 0, Settings->SearchStart, UAnimNotifyState_PoseSearchBlockTransition::StaticClass());
	}
	AddState(AfterName, Settings->SearchEnd, Length, UAnimNotifyState_PoseSearchBlockTransition::StaticClass());
	auto* Notify = CastChecked<UAnimNotifyState_MotionWarping>(AddState(WarpName, Settings->SearchStart, Settings->ContactTime, UAnimNotifyState_MotionWarping::StaticClass()));
	auto* Modifier = NewObject<URootMotionModifier_SkewWarp>(Notify, NAME_None, RF_Transactional);
	Modifier->WarpTargetName = Settings->WarpTargetName;
	Modifier->bWarpTranslation = true;
	Modifier->bIgnoreZAxis = true;
	Modifier->bWarpRotation = true;
	Notify->RootMotionModifier = Modifier;
	if (Settings->bEnableRootMotion)
	{
		Sequence->bEnableRootMotion = true;
		Sequence->RootMotionRootLock = ERootMotionRootLock::AnimFirstFrame;
	}
	if (Settings->bBakeTrajectoryCurves) { BakeCurves(*Sequence, Settings->ContactTime); }
	Sequence->RefreshCacheData();
	Sequence->PostEditChange();
	Sequence->MarkPackageDirty();
	Result = LOCTEXT("Done", "已更新 HMS_Interaction 通知轨道及所选参考曲线。支持 Ctrl+Z；请保存动画，并在对应 Pose Search 数据库中重建索引。运行时仍需状态树提交同名对齐目标并使用查询返回的起播时间。");
	return true;
}

void UHMS_InteractionAnimationTools::OpenWindow(UAnimSequence* Animation)
{
	FSlateApplication::Get().AddWindow(SNew(SWindow).Title(LOCTEXT("Title", "HMS 智能对象动画工具"))
		.ClientSize(FVector2D(680, 620)).SupportsMaximize(false).SupportsMinimize(false)
		[SNew(SHMSInteractionAnimationWindow).Animation(Animation)]);
}

#undef LOCTEXT_NAMESPACE
