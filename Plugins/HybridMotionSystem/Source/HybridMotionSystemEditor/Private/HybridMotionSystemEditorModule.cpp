#include "HybridMotionSystemEditorModule.h"

#include "Tools/HMS_BatchRetargetSettings.h"
#include "Tools/HMS_InteractionAnimationTools.h"

#include "Animation/AnimationAsset.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimCurveTypes.h"
#include "Animation/Skeleton.h"
#include "Chooser.h"
#include "ObjectChooser_Asset.h"
#include "PoseSearch/Chooser/PoseSearchChooserColumn.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchSchema.h"
#include "Curves/RichCurve.h"

#include "IAnimationEditorModule.h"
#include "IAnimationEditor.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "IDetailsView.h"
#include "RetargetEditor/IKRetargetBatchOperation.h"
#include "PropertyEditorModule.h"
#include "Retargeter/IKRetargeter.h"
#include "Misc/MessageDialog.h"
#include "Misc/PackageName.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "FileHelpers.h"
#include "HAL/IConsoleManager.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphNode_Comment.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"

#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/SWidget.h"

#define LOCTEXT_NAMESPACE "FHybridMotionSystemEditorModule"

namespace
{
	UPoseSearchSchema* FindUE58SchemaForDatabase(const FString& DatabaseName)
	{
		const TCHAR* SchemaPath = nullptr;
		if (DatabaseName.Contains(TEXT("Spin"), ESearchCase::IgnoreCase))
		{
			SchemaPath = TEXT("/Game/StreamerGirl/Anim/ExperimentalStateMachineData/PSS_HMS58_Spins.PSS_HMS58_Spins");
		}
		else if (DatabaseName.Contains(TEXT("Stop"), ESearchCase::IgnoreCase))
		{
			SchemaPath = TEXT("/Game/StreamerGirl/Anim/ExperimentalStateMachineData/PSS_HMS58_Stops.PSS_HMS58_Stops");
		}
		else if (DatabaseName.Contains(TEXT("Traversal"), ESearchCase::IgnoreCase))
		{
			SchemaPath = TEXT("/Game/StreamerGirl/Anim/ExperimentalStateMachineData/PSS_HMS58_TraversalTransitions.PSS_HMS58_TraversalTransitions");
		}
		else if (DatabaseName.Contains(TEXT("Loop"), ESearchCase::IgnoreCase))
		{
			SchemaPath = TEXT("/Game/StreamerGirl/Anim/ExperimentalStateMachineData/PSS_HMS58_Loops.PSS_HMS58_Loops");
		}
		else if (DatabaseName.Contains(TEXT("Idle"), ESearchCase::IgnoreCase))
		{
			SchemaPath = TEXT("/Game/StreamerGirl/Anim/ExperimentalStateMachineData/PSS_HMS58_Idles.PSS_HMS58_Idles");
		}
		else if (DatabaseName.Contains(TEXT("Transition"), ESearchCase::IgnoreCase))
		{
			SchemaPath = TEXT("/Game/StreamerGirl/Anim/ExperimentalStateMachineData/PSS_HMS58_Transitions.PSS_HMS58_Transitions");
		}

		return SchemaPath ? LoadObject<UPoseSearchSchema>(nullptr, SchemaPath) : nullptr;
	}

	void FixChooserTableUE58Recursive(
		UChooserTable* Chooser,
		TSet<UChooserTable*>& Visited,
		int32& PoseMatchColumnCount,
		int32& DatabaseCount)
	{
		if (!IsValid(Chooser) || Visited.Contains(Chooser))
		{
			return;
		}
		Visited.Add(Chooser);
		Chooser->Modify();

		TSet<UPoseSearchDatabase*> UpdatedDatabases;
		for (FInstancedStruct& ColumnStruct : Chooser->ColumnsStructs)
		{
			if (FPoseSearchColumn* PoseMatchColumn = ColumnStruct.GetMutablePtr<FPoseSearchColumn>())
			{
				++PoseMatchColumnCount;
				PoseMatchColumn->MaxNumberOfResults = 1;

				auto FixDatabase = [&UpdatedDatabases, &DatabaseCount](UPoseSearchDatabase* Database)
				{
					if (!IsValid(Database) || UpdatedDatabases.Contains(Database))
					{
						return;
					}
					UpdatedDatabases.Add(Database);
					if (UPoseSearchSchema* Schema = FindUE58SchemaForDatabase(Database->GetName()))
					{
						Database->Modify();
						Database->Schema = Schema;
						Database->PostEditChange();
						++DatabaseCount;
					}
				};

				for (int32 RowIndex = 0; RowIndex < PoseMatchColumn->GetNumRows(); ++RowIndex)
				{
					FixDatabase(PoseMatchColumn->GetDatabase(RowIndex));
				}
				FixDatabase(PoseMatchColumn->GetDatabase(ChooserColumn_SpecialIndex_Fallback));
			}
		}

#if WITH_EDITORONLY_DATA
		for (FInstancedStruct& ResultStruct : Chooser->ResultsStructs)
		{
			if (FNestedChooser* Nested = ResultStruct.GetMutablePtr<FNestedChooser>())
			{
				FixChooserTableUE58Recursive(Nested->Chooser, Visited, PoseMatchColumnCount, DatabaseCount);
			}
			else if (FEvaluateChooser* Referenced = ResultStruct.GetMutablePtr<FEvaluateChooser>())
			{
				FixChooserTableUE58Recursive(Referenced->Chooser, Visited, PoseMatchColumnCount, DatabaseCount);
			}
		}
#endif

		Chooser->PostEditChange();
	}

	bool NormalizeContentPath(const FString& InPath, FString& OutPath)
	{
		OutPath = InPath;
		OutPath.TrimStartAndEndInline();
		OutPath.ReplaceInline(TEXT("\\"), TEXT("/"));

		if (!OutPath.StartsWith(TEXT("/")))
		{
			FString LongPackagePath;
			const FString AbsoluteCandidate = FPaths::ConvertRelativePathToFull(OutPath);
			if (FPackageName::TryConvertFilenameToLongPackageName(AbsoluteCandidate, LongPackagePath))
			{
				OutPath = MoveTemp(LongPackagePath);
			}
			else
			{
				OutPath = TEXT("/") + OutPath;
			}
		}

		while (OutPath.EndsWith(TEXT("/")) && OutPath.Len() > 1)
		{
			OutPath.LeftChopInline(1);
		}

		return FPackageName::IsValidLongPackageName(OutPath, false);
	}

	TArray<FAssetData> FindAnimationAssets(const FString& SourcePath, const bool bRecursive)
	{
		FARFilter Filter;
		Filter.PackagePaths.Add(FName(*SourcePath));
		Filter.ClassPaths.Add(UAnimationAsset::StaticClass()->GetClassPathName());
		Filter.bRecursivePaths = bRecursive;
		Filter.bRecursiveClasses = true;

		TArray<FAssetData> Assets;
		FAssetRegistryModule& AssetRegistryModule =
			FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		AssetRegistryModule.Get().GetAssets(Filter, Assets);
		return Assets;
	}

	void ShowRetargetMessage(const FText& Message)
	{
		FMessageDialog::Open(EAppMsgType::Ok, Message, LOCTEXT("BatchRetargetTitle", "HMS 批量动画重定向"));
	}

	struct FRootMotionPostProcessStats
	{
		int32 SettingsCopied = 0;
		int32 TrajectoriesCopied = 0;
		int32 ValidationWarnings = 0;
	};

	FTransform ExtractTotalRootMotion(const UAnimSequence& Sequence)
	{
		FAnimExtractContext ExtractionContext(0.0, false, {}, false);
		return Sequence.ExtractRootMotionFromRange(0.0, Sequence.GetPlayLength(), ExtractionContext);
	}

	bool CopySourceRootTrack(UAnimSequence& SourceSequence, UAnimSequence& TargetSequence)
	{
		const USkeleton* SourceSkeleton = SourceSequence.GetSkeleton();
		const USkeleton* TargetSkeleton = TargetSequence.GetSkeleton();
		if (!IsValid(SourceSkeleton) || !IsValid(TargetSkeleton)
			|| SourceSkeleton->GetReferenceSkeleton().GetNum() == 0
			|| TargetSkeleton->GetReferenceSkeleton().GetNum() == 0)
		{
			return false;
		}

		const FName SourceRootBone = SourceSkeleton->GetReferenceSkeleton().GetBoneName(0);
		const FName TargetRootBone = TargetSkeleton->GetReferenceSkeleton().GetBoneName(0);
		const IAnimationDataModel* SourceModel = SourceSequence.GetDataModel();
		const IAnimationDataModel* TargetModel = TargetSequence.GetDataModel();
		if (!SourceModel || !TargetModel || !SourceModel->IsValidBoneTrackName(SourceRootBone))
		{
			return false;
		}

		TArray<FTransform> SourceRootTransforms;
		SourceModel->GetBoneTrackTransforms(SourceRootBone, SourceRootTransforms);
		if (SourceRootTransforms.IsEmpty())
		{
			return false;
		}

		FTransform TargetFirstKey = TargetSkeleton->GetReferenceSkeleton().GetRefBonePose()[0];
		if (TargetModel->IsValidBoneTrackName(TargetRootBone))
		{
			TArray<FTransform> ExistingTargetRootTransforms;
			TargetModel->GetBoneTrackTransforms(TargetRootBone, ExistingTargetRootTransforms);
			if (!ExistingTargetRootTransforms.IsEmpty())
			{
				TargetFirstKey = ExistingTargetRootTransforms[0];
			}
		}

		const FTransform SourceFirstKey = SourceRootTransforms[0];
		const int32 NumberOfKeys = SourceRootTransforms.Num();
		TArray<FVector3f> TargetPositions;
		TArray<FQuat4f> TargetRotations;
		TArray<FVector3f> TargetScales;
		TargetPositions.Reserve(NumberOfKeys);
		TargetRotations.Reserve(NumberOfKeys);
		TargetScales.Reserve(NumberOfKeys);

		for (int32 KeyIndex = 0; KeyIndex < NumberOfKeys; ++KeyIndex)
		{
			const FTransform& SourceKey = SourceRootTransforms[KeyIndex];
			const FTransform SourceDelta = SourceKey.GetRelativeTransform(SourceFirstKey);
			const FTransform TargetKey = SourceDelta * TargetFirstKey;
			TargetPositions.Add(FVector3f(TargetKey.GetTranslation()));
			TargetRotations.Add(FQuat4f(TargetKey.GetRotation().GetNormalized()));
			TargetScales.Add(FVector3f(TargetKey.GetScale3D()));
		}

		TargetSequence.Modify();
		IAnimationDataController& Controller = TargetSequence.GetController();
		Controller.OpenBracket(LOCTEXT("PreserveRetargetRootTrajectory", "保留重定向动画 Root 轨迹"), false);
		if (!TargetModel->IsValidBoneTrackName(TargetRootBone))
		{
			Controller.AddBoneCurve(TargetRootBone, false);
		}
		const bool bCopied = Controller.SetBoneTrackKeys(
			TargetRootBone, TargetPositions, TargetRotations, TargetScales, false);
		Controller.CloseBracket(false);
		return bCopied;
	}

	void PostProcessRetargetedRootMotion(
		UAnimSequence& SourceSequence,
		UAnimSequence& TargetSequence,
		const UHMS_BatchRetargetSettings& Settings,
		FRootMotionPostProcessStats& Stats,
		TArray<UPackage*>& OutPackagesToSave)
	{
		const FTransform SourceRootMotionBefore = ExtractTotalRootMotion(SourceSequence);
		const FTransform TargetRootMotionBefore = ExtractTotalRootMotion(TargetSequence);

		if (Settings.bPreserveRootMotionSettings)
		{
			TargetSequence.Modify();
			TargetSequence.bEnableRootMotion = SourceSequence.bEnableRootMotion;
			TargetSequence.RootMotionRootLock = SourceSequence.RootMotionRootLock;
			TargetSequence.bForceRootLock = SourceSequence.bForceRootLock;
			TargetSequence.bUseNormalizedRootMotionScale = SourceSequence.bUseNormalizedRootMotionScale;
			++Stats.SettingsCopied;
		}

		if (Settings.bPreserveRootMotionTrajectory && SourceSequence.bEnableRootMotion)
		{
			if (CopySourceRootTrack(SourceSequence, TargetSequence))
			{
				++Stats.TrajectoriesCopied;
			}
			else
			{
				++Stats.ValidationWarnings;
				UE_LOG(LogTemp, Warning,
					TEXT("[HMS 批量重定向][RootMotion] 无法复制 Root 轨迹：Source=%s Target=%s。请检查两个 Skeleton 的根骨及动画轨道。"),
					*SourceSequence.GetPathName(), *TargetSequence.GetPathName());
			}
		}

		TargetSequence.PostEditChange();
		TargetSequence.MarkPackageDirty();
		OutPackagesToSave.AddUnique(TargetSequence.GetOutermost());

		if (Settings.bValidateRootMotion)
		{
			const FTransform TargetRootMotionAfter = ExtractTotalRootMotion(TargetSequence);
			const FVector SourceTranslation = SourceRootMotionBefore.GetTranslation();
			const FVector TargetTranslationBefore = TargetRootMotionBefore.GetTranslation();
			const FVector TargetTranslationAfter = TargetRootMotionAfter.GetTranslation();
			const double SourceYaw = FMath::UnwindDegrees(SourceRootMotionBefore.Rotator().Yaw);
			const double TargetYawBefore = FMath::UnwindDegrees(TargetRootMotionBefore.Rotator().Yaw);
			const double TargetYawAfter = FMath::UnwindDegrees(TargetRootMotionAfter.Rotator().Yaw);
			const bool bTranslationMismatch =
				FVector::Dist(SourceTranslation, TargetTranslationAfter) > 5.0;
			const bool bRotationMismatch =
				FMath::Abs(FMath::FindDeltaAngleDegrees(SourceYaw, TargetYawAfter)) > 2.0;
			if (SourceSequence.bEnableRootMotion && (bTranslationMismatch || bRotationMismatch))
			{
				++Stats.ValidationWarnings;
			}

			if (bTranslationMismatch || bRotationMismatch)
			{
				UE_LOG(LogTemp, Warning,
					TEXT("[HMS 批量重定向][RootMotion] %s -> %s | Source=(%.1f,%.1f,%.1f Yaw=%.1f) | 修复前=(%.1f,%.1f,%.1f Yaw=%.1f) | 修复后=(%.1f,%.1f,%.1f Yaw=%.1f) | 结果=需要检查"),
					*SourceSequence.GetName(), *TargetSequence.GetName(),
					SourceTranslation.X, SourceTranslation.Y, SourceTranslation.Z, SourceYaw,
					TargetTranslationBefore.X, TargetTranslationBefore.Y, TargetTranslationBefore.Z, TargetYawBefore,
					TargetTranslationAfter.X, TargetTranslationAfter.Y, TargetTranslationAfter.Z, TargetYawAfter);
			}
		}
	}

	bool ExecuteBatchRetarget(const UHMS_BatchRetargetSettings& Settings)
	{
		FString SourcePath;
		FString TargetPath;
		if (!NormalizeContentPath(Settings.SourceFolder.Path, SourcePath))
		{
			ShowRetargetMessage(LOCTEXT("InvalidSourcePath", "源动画文件夹不是有效的内容浏览器路径。示例：/Game/Animations/Source"));
			return false;
		}
		if (!NormalizeContentPath(Settings.TargetFolder.Path, TargetPath))
		{
			ShowRetargetMessage(LOCTEXT("InvalidTargetPath", "目标文件夹不是有效的内容浏览器路径。示例：/Game/Animations/Retargeted"));
			return false;
		}
		if (SourcePath == TargetPath || TargetPath.StartsWith(SourcePath + TEXT("/")))
		{
			ShowRetargetMessage(LOCTEXT("TargetInsideSource", "目标文件夹不能与源文件夹相同，也不能位于源文件夹内部。"));
			return false;
		}

		UIKRetargeter* Retargeter = Settings.Retargeter;
		if (!IsValid(Retargeter))
		{
			ShowRetargetMessage(LOCTEXT("MissingRetargeter", "请选择一个有效的 IK 重定向器。"));
			return false;
		}

		USkeletalMesh* SourceMesh = Retargeter->GetPreviewMesh(ERetargetSourceOrTarget::Source);
		USkeletalMesh* TargetMesh = Retargeter->GetPreviewMesh(ERetargetSourceOrTarget::Target);
		if (!IsValid(SourceMesh) || !IsValid(TargetMesh) || SourceMesh == TargetMesh)
		{
			ShowRetargetMessage(LOCTEXT("MissingPreviewMeshes", "IK 重定向器必须配置不同的源预览 Mesh 和目标预览 Mesh。"));
			return false;
		}

		const TArray<FAssetData> Assets = FindAnimationAssets(SourcePath, Settings.bRecursive);
		if (Assets.IsEmpty())
		{
			ShowRetargetMessage(FText::Format(
				LOCTEXT("NoAnimations", "在 {0} 中没有找到动画资源。"),
				FText::FromString(SourcePath)));
			return false;
		}

		TMap<FString, TArray<FAssetData>> AssetsByTargetPath;
		for (const FAssetData& Asset : Assets)
		{
			FString AssetTargetPath = TargetPath;
			if (Settings.bPreserveSubfolders)
			{
				const FString SourceAssetPath = Asset.PackagePath.ToString();
				if (SourceAssetPath.StartsWith(SourcePath))
				{
					AssetTargetPath += SourceAssetPath.RightChop(SourcePath.Len());
				}
			}
			AssetsByTargetPath.FindOrAdd(AssetTargetPath).Add(Asset);
		}

		int32 CreatedAssetCount = 0;
		FRootMotionPostProcessStats RootMotionStats;
		TArray<UPackage*> PackagesToSave;
		for (const TPair<FString, TArray<FAssetData>>& Pair : AssetsByTargetPath)
		{
			FIKRetargetBatchOperationInputs Inputs;
			Inputs.AssetsToRetarget = Pair.Value;
			Inputs.SourceMesh = SourceMesh;
			Inputs.TargetMesh = TargetMesh;
			Inputs.IKRetargetAsset = Retargeter;
			Inputs.Search = Settings.Search;
			Inputs.Replace = Settings.Replace;
			Inputs.Prefix = Settings.Prefix;
			Inputs.Suffix = Settings.Suffix;
			Inputs.TargetPath = Pair.Key;
			Inputs.bUseSourcePath = false;
			Inputs.bIncludeReferencedAssets = Settings.bIncludeReferencedAssets;
			Inputs.bOverwriteExistingFiles = Settings.bOverwriteExistingAssets;
			Inputs.bRetainAdditiveFlags = Settings.bRetainAdditiveFlags;

			EditorAnimUtils::FNameDuplicationRule NameRule;
			NameRule.Prefix = Settings.Prefix;
			NameRule.Suffix = Settings.Suffix;
			NameRule.ReplaceFrom = Settings.Search;
			NameRule.ReplaceTo = Settings.Replace;
			TMap<FName, UAnimSequence*> SourceSequencesByOutputName;
			for (const FAssetData& SourceAssetData : Pair.Value)
			{
				if (UAnimSequence* SourceSequence = Cast<UAnimSequence>(SourceAssetData.GetAsset()))
				{
					SourceSequencesByOutputName.Add(FName(*NameRule.Rename(SourceSequence)), SourceSequence);
				}
			}

			const TArray<FAssetData> CreatedAssets = UIKRetargetBatchOperation::RunBatchRetarget(Inputs);
			CreatedAssetCount += CreatedAssets.Num();
			for (const FAssetData& CreatedAssetData : CreatedAssets)
			{
				UAnimSequence* TargetSequence = Cast<UAnimSequence>(CreatedAssetData.GetAsset());
				UAnimSequence* const* SourceSequence =
					TargetSequence ? SourceSequencesByOutputName.Find(CreatedAssetData.AssetName) : nullptr;
				if (TargetSequence && SourceSequence && *SourceSequence)
				{
					PostProcessRetargetedRootMotion(
						**SourceSequence, *TargetSequence, Settings, RootMotionStats, PackagesToSave);
				}
			}
		}

		if (!PackagesToSave.IsEmpty())
		{
			UEditorLoadingAndSavingUtils::SavePackages(PackagesToSave, true);
		}

		ShowRetargetMessage(FText::Format(
			LOCTEXT("BatchRetargetComplete", "批量重定向完成。\n扫描动画：{0}\n生成资源：{1}\n复制根运动设置：{2}\n修复 Root 轨迹：{3}\n根运动警告：{4}\n目标路径：{5}"),
			FText::AsNumber(Assets.Num()),
			FText::AsNumber(CreatedAssetCount),
			FText::AsNumber(RootMotionStats.SettingsCopied),
			FText::AsNumber(RootMotionStats.TrajectoriesCopied),
			FText::AsNumber(RootMotionStats.ValidationWarnings),
			FText::FromString(TargetPath)));
		return true;
	}

	class SHMSBatchRetargetWindow final : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SHMSBatchRetargetWindow) {}
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			Settings.Reset(NewObject<UHMS_BatchRetargetSettings>(GetTransientPackage()));

			FPropertyEditorModule& PropertyEditor =
				FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
			FDetailsViewArgs DetailsArgs;
			DetailsArgs.bAllowSearch = true;
			DetailsArgs.bHideSelectionTip = true;
			DetailsArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
			DetailsView = PropertyEditor.CreateDetailView(DetailsArgs);
			DetailsView->SetObject(Settings.Get());

			ChildSlot
			[
				SNew(SBorder)
				.Padding(10.0f)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.FillHeight(1.0f)
					[
						DetailsView.ToSharedRef()
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 10.0f, 0.0f, 0.0f)
					.HAlign(HAlign_Right)
					[
						SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "PrimaryButton")
						.Text(LOCTEXT("RunBatchRetarget", "开始批量重定向"))
						.ToolTipText(LOCTEXT("RunBatchRetargetTooltip", "扫描源文件夹并使用所选 IK 重定向器生成动画"))
						.OnClicked_Lambda([this]()
						{
							ExecuteBatchRetarget(*Settings.Get());
							return FReply::Handled();
						})
					]
				]
			];
		}

	private:
		TStrongObjectPtr<UHMS_BatchRetargetSettings> Settings;
		TSharedPtr<IDetailsView> DetailsView;
	};
}

void FHybridMotionSystemEditorModule::TranslateSampleRagdollComments(const TArray<FString>& Args)
{
	const TCHAR* BlueprintPath = TEXT("/Game/Blueprints/SandboxCharacter_Mover_Ragdoll.SandboxCharacter_Mover_Ragdoll");
	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, BlueprintPath);
	if (!Blueprint)
	{
		UE_LOG(LogTemp, Error, TEXT("[HMS 注释翻译] 无法加载 %s"), BlueprintPath);
		return;
	}

	const TMap<FString, FString> Translations = {
		{TEXT("Physics / Ragdoll Events"), TEXT("物理与布娃娃事件")},
		{TEXT("Input Actions"), TEXT("输入事件")},
		{TEXT("Trigger Ragdoll"), TEXT("触发布娃娃")},
		{TEXT("Interaction Input"), TEXT("交互输入")},
		{TEXT("Takedown Input"), TEXT("击倒输入")},
		{TEXT("Entered Ragdoll Successfully Check"), TEXT("检查是否成功进入布娃娃模式")},
		{TEXT("Events for playing ragdoll sounds, can be removed safely."), TEXT("播放布娃娃碰撞音效的事件；不需要音效时可以安全删除。")},
		{TEXT("Initialize the character with the default physics profile, which is set via a cvar."), TEXT("用控制台变量指定的默认物理配置初始化角色。")},
		{TEXT("Set the default physics profile (the profile used when the character is not in ragdoll) when the console variable changes. Can be removed safely."), TEXT("控制台变量改变时，更新角色非布娃娃状态使用的默认物理配置；不需要运行时切换时可以安全删除。")},
		{TEXT("Here we make the PCC tick after the ABP, so that curve values can drive physics multipliers on this frame"), TEXT("让 Physics Control Component 在动画蓝图之后更新，使本帧动画曲线能立即驱动物理强度倍率。")},
		{TEXT("This function creates controls and modifiers on the PCC from the PCA, which is set on the PCC"), TEXT("根据 Physics Control Component 上配置的 Physics Control Asset，创建控制器和 Body Modifier。")},
		{TEXT("Cache the new phyisics profile to apply and then invoke the profile via the physics control component. Profiles in the PCC are treated like a recipe that gets applied rather than a state, therefore the PCC does not have a concept of \"active\" profiles. Therefore, we keep track of the last applied profile manually for use elsewhere in the BP."), TEXT("记录准备应用的物理配置，再让 Physics Control Component 执行它。PCC 把配置当作一次性“配方”而不是持续状态，因此它不知道当前激活的是哪个配置；蓝图需要自行保存最后一次应用的配置名。")},
		{TEXT("If the applied profile was Physical Animation (normal animation with a strong physics layer ontop), we disable collision between both legs so that they do not get caught on each other, and set the constraint profile to free so that there are no restrictions on the joint limits."), TEXT("应用 Physical Animation 配置时：关闭双腿之间的碰撞，避免互相卡住；同时把约束设为 Free，取消关节角度限制。")},
		{TEXT("If the applied profile was Ragdoll, we enable collision between both legs, and reset the constraint profile back to default so that the joints have proper limits when simulating."), TEXT("应用 Ragdoll 配置时：重新开启双腿之间的碰撞，并恢复默认约束，让物理模拟时的关节限制正确生效。")},
		{TEXT("This event is responsible for putting the character into the ragdoll state by setting the physics profile and queuing a movement mode change."), TEXT("进入布娃娃的总入口：切换物理配置，并请求 Mover 进入 Ragdoll 移动模式。")},
		{TEXT("Step 1: Stop any active montage if set on the event input."), TEXT("步骤 1：如果事件传入了正在播放的 Montage，先停止它。")},
		{TEXT("Step 2: Set the ragdolls initial injury state based on input, which will change poses in the Anim Graph."), TEXT("步骤 2：根据输入设置初始受伤状态，动画蓝图会据此改变布娃娃姿势。")},
		{TEXT("Step 3: Set the Physics Profile to Ragdoll. Although the PCC will also set the skel mesh to simulate physics, it does so when the PCC ticks, not immediately on this event. This means any physical impulses that are applied to the mesh immediately after this even is called will get lost. Therefore, we start simulating physics immediately in addition to setting the profile."), TEXT("步骤 3：应用 Ragdoll 物理配置。PCC 要到自身 Tick 时才会开启骨骼物理；为避免事件后立刻施加的冲量丢失，这里还要立即主动开启物理模拟。")},
		{TEXT("Step 4: Queue the next movement mode to Ragdoll. Since Mover has already updated at this point in the frame, and queued movement mode changes are not guaranteed, we also need to trigger a safetey check next frame to make sure we successfully entered the ragdoll mode, else we switch back to normal physics settings"), TEXT("步骤 4：请求下一帧切换到 Ragdoll 移动模式。本帧 Mover 已更新完，而且排队的切换不保证成功，所以还要在下一帧检查；失败时恢复普通物理配置。")},
		{TEXT("Step 5: To prevent erradic capsule rotations on the first few frames of ragdoll, we set a bool that is used in \"Get_RagdollTargetOrientation\" which prevents the capsule from rotating, and then reset it after a small delay."), TEXT("步骤 5：布娃娃开始的前几帧先锁住胶囊旋转，避免方向乱跳；短暂延迟后再允许 Get_RagdollTargetOrientation 更新朝向。")},
		{TEXT("Wait till the next frame after ragdoll is triggered to ensure the movement mode was successfully changed to Ragdoll, else revert the physics profile back to the default mode."), TEXT("触发布娃娃后的下一帧确认 Mover 是否真的进入 Ragdoll；如果失败，就恢复默认物理配置。")},
		{TEXT("The event to exit ragdoll simply Queues up a movement mode change, rather than make any changes to physics settings. This is because movment mode changes can come from multiple sources, and physics settings will need setting whenever the previous state was Ragdoll."), TEXT("退出布娃娃事件只请求切换移动模式，不直接修改物理。移动模式可能被多种逻辑改变，因此统一在“前一个模式是 Ragdoll”时执行完整退出处理。")},
		{TEXT("This event is called from \"On_MovementModeChanged_PostFinalize\", whenever the previous mode was Ragdoll. This ensures that the character can properly blend out of ragdoll whenever any gameplay logic sets the movement mode to something else."), TEXT("只要 On_MovementModeChanged_PostFinalize 发现前一个模式是 Ragdoll，就调用此事件。无论哪段玩法逻辑切走 Ragdoll，都能统一、正确地恢复。")},
		{TEXT("Step 1: Ensure that the capsules collision is reset to the correct profile."), TEXT("步骤 1：把胶囊碰撞恢复为角色正常使用的配置。")},
		{TEXT("Step 2: Get the Anim Instance and cache the characters current physical pose into a pose snapshot that can be used in the anim graph, and override the Pose History node with the current physical pose so that motion matched Get-Ups can be performed."), TEXT("步骤 2：获取动画实例，把当前物理姿势保存为 Pose Snapshot；同时用当前物理姿势覆盖 Pose History，让起身动画可以进行姿势搜索与动作匹配。")},
		{TEXT("Step 3: Evaluate a chooser to select a proper Get Up montage and play the result."), TEXT("步骤 3：执行 Chooser，根据当前姿势搜索并播放最合适的起身 Montage。")},
		{TEXT("selected animation selected by the motion matching alghoritm that matches the query (trajectory + historical pose) the best"), TEXT("动作匹配根据查询条件（轨迹 + 历史姿势）选出的最合适动画。")},
		{TEXT("Step 4: Set the character back to the default physics profile. PCC uses bone velocities to multiply the strengths of the linear and angular motors, which helps physics keep up with animation. Because we are blending instantly to a pose snapshot in the animation graph, those velocites will be high for one frame. Therefore, we disable those multipliers and then wait till the next frame to switch profiles, to reduce physical jitter. This delay is NOT necessary if the default profile is fully kinematic."), TEXT("步骤 4：恢复默认物理配置。PCC 会用骨骼速度增强线性/角度马达；切入 Pose Snapshot 的首帧骨骼速度会异常大，所以先关闭速度倍率，下一帧再切配置以减少抖动。若默认配置完全是运动学模式，则无需延迟。")},
		{TEXT("Find the skeletal center of mass and compare to last frame's value to determine the ragdoll's COM velocity. This is used for determining capsule rotation when the ragdoll is rolling on the ground, since a single bone (such as the pelvis) can often be almost stationary when the character is rolling."), TEXT("用骨骼质心与上一帧质心的差计算布娃娃整体速度。角色在地面翻滚时，pelvis 等单个骨骼可能几乎不动，因此胶囊朝向应参考整体质心速度。")},
		{TEXT("Get the spines velocity for use in later functions. Although we just calculated the COM velocity, getting the bones velocity directly from the physics system is often more stable (especially when physics is substepped), and so it is used in the cases where COM velocity is not necessary."), TEXT("读取脊柱骨骼速度供后续逻辑使用。虽然已计算质心速度，但直接读取物理骨骼速度通常更稳定，尤其开启物理子步时；不需要整体速度的地方优先使用它。")},
		{TEXT("Perform a projectile trace in the direction of the ragdolls velocity, to determine future points of impact. A socket is used for the trace origin rather than a bone so that it's location and rotation can be easily adjusted. \"Ragdoll Time to Impact\" is how long in seconds it will take to hit whatever object was found by the trace."), TEXT("沿布娃娃速度方向做抛射物预测，估算下一次碰撞位置。起点使用可调 Socket 而不是骨骼；Ragdoll Time to Impact 表示预计多少秒后撞到检测到的物体。")},
		{TEXT("Based on the Impact location and trace origin, determine the relative impact direction, which represents the relative angle from the trace orgin to the impact point. This direction is then converted to Pitch and Yaw angles in a 2D vector, for easy use within animation graphs."), TEXT("根据检测起点与碰撞点计算角色局部空间中的碰撞方向，再转换为 Pitch/Yaw 二维角度，供动画蓝图直接使用。")},
		{TEXT("Convert the Ragdoll Roll Force into a torque and apply it to the spine. The added check for physics simulations is to prevent warnings if somehow the ragdoll movement mode is entered before starting physics simulation on the character."), TEXT("把 Ragdoll Roll Force 转换为扭矩并施加到脊柱。施加前检查物理是否已启动，避免移动模式先进入 Ragdoll 时产生警告。")},
		{TEXT("Use interpolation to smooth out changes in the target auto roll force. Interp speed is high when the force increases, but low when it decreases, which keeps the character rolling for a bit longer after getting to flatter ground."), TEXT("平滑自动翻滚力：增大时快速响应，减小时慢慢衰减，使角色到达较平地面后还能继续滚动一小段。")},
		{TEXT("If the ragdoll falls from a great height, the velocity will continue to increase without stopping. This can lead to the ragdoll going through ground collision depending on physics settings like substepping or CCD. Therefore, we disable gravity once we reach a certain fall speed which prevents the speed from increasing further."), TEXT("高处坠落时速度会持续增加，可能因子步或 CCD 设置而穿透地面；达到设定下落速度后暂时关闭重力，防止速度继续增长。")},
		{TEXT("This function uses curves from the animation graph to drive strength multpliers for on the physics controls for different body parts via the physics control component. Right now only the controls used in the Ragdoll profile are dynamically modified, but the same concept can be applied to any control for any profile."), TEXT("通过动画曲线动态控制各身体部位的 Physics Control 强度倍率。目前只调整 Ragdoll 配置中的控制器，同样方法也可用于其他物理配置。")},
		{TEXT("This event is called from \"Ragdoll_UpdateBehaviors\" whenever the ragdoll velocity is under a threshold. When called, it performs a single check after a delay to see if the ragdoll is still under that velocity. If it is, exit the ragdoll, if not, the gate is reset and the check is ab le to be performed after another delay. This logic is best suited to be done in an AI behavior tree, but this just gives us quick and simple functionality."), TEXT("当 Ragdoll_UpdateBehaviors 发现速度低于阈值时调用。延迟后再确认一次：仍然低速就起身，否则重置 Gate，之后可再次检查。正式 AI 项目更适合把这段逻辑放进行为树。")},
		{TEXT("Try to exit the ragdoll when jump is pressed and the character is in the ragdoll movement mode and also on the ground."), TEXT("按下跳跃键时，如果角色处于 Ragdoll 移动模式且已经落地，就尝试退出布娃娃并起身。")},
		{TEXT("This function performs a small trace from the characters head to between the feet to find obstacles (meshes using the obstacle collision type), sending the character into ragdoll if it does. This allows obstacles to partially overlap the character and slightly collide with limbs, only ragdolling the character if it gets too close."), TEXT("从角色头部到双脚之间做一段小范围检测，寻找 Obstacle 碰撞类型的物体。允许障碍轻微接触或部分穿入四肢，只有距离过近时才让角色进入布娃娃。")},
		{TEXT("Perform a simple check for all \"SandboxCharacter_Mover_Ragdoll\" pawns in a 2 meter radius."), TEXT("简单查找半径 2 米内所有 SandboxCharacter_Mover_Ragdoll 角色。")},
		{TEXT("Perform a final trace against the CharacterCapsule collision profile to see if any geometry is blocking the path to the other actor."), TEXT("最后按 CharacterCapsule 碰撞配置做一次检测，确认前往另一个角色的路径没有被场景几何体挡住。")},
		{TEXT("Convert the floor normal to an angle, which is generally easier to reason about."), TEXT("把地面法线转换成坡度角，便于后续判断斜坡陡峭程度。")},
		{TEXT("Multiply the normal (direction) of the slope by the angle, which is mapped to a range, to get a normalized target role force. Here we can define how steep of a slope is needed to start auto roll, and the slope angle for which the auto roll force should be the max."), TEXT("用斜坡方向乘以映射后的坡度角，得到标准化的目标翻滚力。这里可以设置多陡才开始自动翻滚，以及达到多大坡度时使用最大翻滚力。")},
		{TEXT("Only check for interactable characters if certain conditions are met."), TEXT("只有满足前置条件时，才搜索可交互角色。")},
		{TEXT("For each of those pawns, check to see if they can be interacted with."), TEXT("逐个检查找到的 Pawn 是否允许交互。")},
		{TEXT("Perform a simple check to see if the other pawn is within half a meter up or down relative to this one, and within a 60 degrees relative to this pawns forward rotation."), TEXT("简单判断另一个 Pawn 与本角色的高度差是否在上下 0.5 米内，并且位于角色前方约 60 度范围内。")},
		{TEXT("If all of those conditions succeed, add this actor to the array of Interactable Actors."), TEXT("全部条件通过后，把该角色加入“可交互角色”数组。")},
		{TEXT("Clear the current array of Interactable actors so that each frame starts with a fresh check"), TEXT("每帧检测前清空旧的可交互角色数组，保证结果来自本帧重新检测。")},
		{TEXT("In absense of a HUD, perform a simple debug draw to indicate that this actor can be interacted with. This can be removed safely."), TEXT("没有 HUD 时，用简单调试绘制提示该角色可以交互；正式项目中可以安全删除。")},
		{TEXT("This function just performs a simple check to find other pawns of this same type to \"interact\" with, and is a stand-in for a more robust interaction system"), TEXT("此函数只是查找同类型 Pawn 并进行交互的简化示例，用来临时代替完整的交互系统。")},
		{TEXT("Evaluate Chooser for Valid Interaction Databases. The Chooser Output array contains roles which are defined on each row."), TEXT("执行 Chooser，筛选可用的交互数据库。Chooser 输出数组包含每一行定义的角色信息。")},
		{TEXT("For each chooser result (database) add it to the query array."), TEXT("把每个 Chooser 结果（数据库）加入动作匹配查询数组。")},
		{TEXT("For this index of chooser result, add this characters anim instance to the roles, using the \"Initiator Roles\" defined in the chooser outputs"), TEXT("对当前 Chooser 结果，把本角色的动画实例按输出中定义的 Initiator Roles（发起者角色）加入角色列表。")},
		{TEXT("For this index of chooser result, add each of the other characters anim instance's to the roles, using the \"Target Roles\" defined in the chooser outputs"), TEXT("对当前 Chooser 结果，把其他角色的动画实例按输出中定义的 Target Roles（目标角色）加入角色列表。")},
		{TEXT("These nodes are responsble for constructing the Motion Match Mulit queries based on the chooser result and ouputs. The \"Motion Match Multi Queries\" is an array of structs that contain a Databases with an array of roles, which are themselves a struct that contain an anim context (anim Instance) and role name."), TEXT("这些节点根据 Chooser 结果构建多人动作匹配查询。每条查询包含一个数据库和若干角色；每个角色又包含动画上下文（Anim Instance）与角色名。")},
		{TEXT("Perform a motion match multi using the queries built above, and cache the result, which is an array containing data for each character in the matched interaction."), TEXT("使用上面构建的查询执行多人动作匹配，并缓存结果；结果数组包含本次交互中每个角色的数据。")},
		{TEXT("For each of the MM results, trigger a montage to play on each of the characters in the interaction."), TEXT("遍历动作匹配结果，让参与交互的每个角色播放各自对应的 Montage。")},
		{TEXT("If the current result is for any character that isnt this one (the initiator), set up a tick prerequiste so that the other actor's anim instance updates first, which is needed for proper IK, and also disable collision between the other actor and the initiator."), TEXT("如果当前结果属于其他角色而不是发起者，就设置 Tick 前置关系，让对方动画实例先更新以保证 IK 正确；同时关闭对方与发起者之间的碰撞。")},
		{TEXT("On Begin Play"), TEXT("游戏开始时")},
		{TEXT("Setting this component to tick after the skeletal mesh and then binding to its event dispatcher gives us an event that is called after the ABP updates"), TEXT("让该组件在骨骼网格之后 Tick，并绑定它的事件分发器，从而获得一个“动画蓝图更新完成后”调用的事件。")},
		{TEXT("Event Tick"), TEXT("每帧更新")},
		{TEXT("This event is called after the ABP updates but before the PCC does."), TEXT("此事件在动画蓝图更新之后、Physics Control Component 更新之前调用。")},
		{TEXT("Try a multicharacter interaction with the array of interactable actors (updated on tick) if there is any."), TEXT("如果本帧存在可交互角色，就使用该数组尝试执行多人交互。")},
		{TEXT("This event is called from \"Try Multi Character Interaction\" and simply plays the montage from the current MMI (Motion Matching Interaction) result."), TEXT("由 Try Multi Character Interaction 调用，负责播放当前 MMI（动作匹配交互）结果中的 Montage。")},
		{TEXT("For Demo Only, can be removed safely"), TEXT("仅用于演示，正式项目中可以安全删除。")},
		{TEXT("Character Interaction Events"), TEXT("角色交互事件")}
	};

	TArray<UEdGraph*> Graphs;
	Blueprint->GetAllGraphs(Graphs);
	int32 TranslatedCount = 0;
	int32 UntranslatedEnglishCount = 0;
	Blueprint->Modify();
	for (UEdGraph* Graph : Graphs)
	{
		if (!Graph)
		{
			continue;
		}
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			UEdGraphNode_Comment* Comment = Cast<UEdGraphNode_Comment>(Node);
			if (!Comment)
			{
				continue;
			}
			const FString Original = Comment->NodeComment.TrimStartAndEnd();
			if (const FString* Chinese = Translations.Find(Original))
			{
				Comment->Modify();
				Comment->NodeComment = *Chinese;
				++TranslatedCount;
			}
			bool bLooksEnglish = !Original.IsEmpty();
			for (const TCHAR Character : Original)
			{
				if (Character > 127)
				{
					bLooksEnglish = false;
					break;
				}
			}
			if (!Translations.Contains(Original) && bLooksEnglish)
			{
				++UntranslatedEnglishCount;
				UE_LOG(LogTemp, Warning, TEXT("[HMS 注释翻译][未匹配] Graph=%s Text=%s"),
					*Graph->GetName(), *Original.Replace(TEXT("\n"), TEXT("\\n")));
			}
		}
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	Blueprint->MarkPackageDirty();
	const FString Filename = FPackageName::LongPackageNameToFilename(
		Blueprint->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	const bool bSaved = UPackage::SavePackage(Blueprint->GetOutermost(), Blueprint, *Filename, SaveArgs);
	UE_LOG(LogTemp, Display, TEXT("[HMS 注释翻译] 已翻译=%d 未匹配英文=%d 保存=%s 资产=%s"),
		TranslatedCount, UntranslatedEnglishCount, bSaved ? TEXT("成功") : TEXT("失败"), BlueprintPath);
}

void FHybridMotionSystemEditorModule::StartupModule()
{
	FixChooser58ConsoleCommand = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("hms.FixChooser58"),
		TEXT("Align a Chooser table's Pose Match columns and transient databases with the UE 5.8 GASP layout. Optional argument: asset path."),
		FConsoleCommandWithArgsDelegate::CreateRaw(this, &FHybridMotionSystemEditorModule::FixChooserForUE58),
		ECVF_Default);

	FixRagdollChooserConsoleCommand = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("hms.FixRagdollChooser58"),
		TEXT("Configure the HMS ragdoll get-up chooser with StreamerGirl montages and the UE 5.8 pose-search schema."),
		FConsoleCommandWithArgsDelegate::CreateRaw(this, &FHybridMotionSystemEditorModule::FixRagdollGetUpChooser),
		ECVF_Default);

	TranslateRagdollCommentsConsoleCommand = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("hms.TranslateSampleRagdollCommentsZH"),
		TEXT("Translate all comment boxes in SandboxCharacter_Mover_Ragdoll into plain Chinese."),
		FConsoleCommandWithArgsDelegate::CreateRaw(this, &FHybridMotionSystemEditorModule::TranslateSampleRagdollComments),
		ECVF_Default);

	// Startup console commands run before editor modules have necessarily registered
	// their commands. Use an explicit one-shot flag so project migrations run after
	// the editor and the MCP console command are both available.
	if (FParse::Param(FCommandLine::Get(), TEXT("HMSFixChooser58")))
	{
		StartupChooserMigrationHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateRaw(this, &FHybridMotionSystemEditorModule::RunStartupChooserMigration),
			3.0f);
	}

	UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FHybridMotionSystemEditorModule::RegisterMenus));

	IAnimationEditorModule& AnimationEditorModule =
		FModuleManager::LoadModuleChecked<IAnimationEditorModule>("AnimationEditor");

	AnimationEditorModule.GetAllAnimationEditorToolbarExtenders().Add(
		IAnimationEditorModule::FAnimationEditorToolbarExtender::CreateRaw(
			this,
			&FHybridMotionSystemEditorModule::ExtendAnimationEditorToolbar));
}

void FHybridMotionSystemEditorModule::ShutdownModule()
{
	if (StartupChooserMigrationHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(StartupChooserMigrationHandle);
		StartupChooserMigrationHandle.Reset();
	}

	if (FixChooser58ConsoleCommand)
	{
		IConsoleManager::Get().UnregisterConsoleObject(FixChooser58ConsoleCommand);
		FixChooser58ConsoleCommand = nullptr;
	}

	if (FixRagdollChooserConsoleCommand)
	{
		IConsoleManager::Get().UnregisterConsoleObject(FixRagdollChooserConsoleCommand);
		FixRagdollChooserConsoleCommand = nullptr;
	}

	if (TranslateRagdollCommentsConsoleCommand)
	{
		IConsoleManager::Get().UnregisterConsoleObject(TranslateRagdollCommentsConsoleCommand);
		TranslateRagdollCommentsConsoleCommand = nullptr;
	}

	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);

	if (FModuleManager::Get().IsModuleLoaded("AnimationEditor"))
	{
		IAnimationEditorModule& AnimationEditorModule =
			FModuleManager::GetModuleChecked<IAnimationEditorModule>("AnimationEditor");

		AnimationEditorModule.GetAllAnimationEditorToolbarExtenders().RemoveAll(
			[this](const IAnimationEditorModule::FAnimationEditorToolbarExtender& Delegate)
			{
				return Delegate.IsBoundToObject(this);
			});
	}
}

bool FHybridMotionSystemEditorModule::RunStartupChooserMigration(float DeltaTime)
{
	StartupChooserMigrationHandle.Reset();
	FixChooserForUE58({});
	if (GEngine)
	{
		GEngine->Exec(nullptr, TEXT("ModelContextProtocol.StartServer"));
	}
	return false;
}

void FHybridMotionSystemEditorModule::FixChooserForUE58(const TArray<FString>& Args)
{
	const FString ChooserPath = Args.IsEmpty()
		? TEXT("/Game/StreamerGirl/HMS/HMS_ChooserTable.HMS_ChooserTable")
		: Args[0];
	UChooserTable* Chooser = LoadObject<UChooserTable>(nullptr, *ChooserPath);
	if (!IsValid(Chooser))
	{
		UE_LOG(LogTemp, Error, TEXT("[HMS 5.8 Chooser] Cannot load %s"), *ChooserPath);
		return;
	}

	TSet<UChooserTable*> Visited;
	int32 PoseMatchColumnCount = 0;
	int32 DatabaseCount = 0;
	FixChooserTableUE58Recursive(Chooser, Visited, PoseMatchColumnCount, DatabaseCount);
	Chooser->MarkPackageDirty();
	UE_LOG(LogTemp, Warning,
		TEXT("[HMS 5.8 Chooser] Fixed %s: Tables=%d PoseMatchColumns=%d Databases=%d. Save the asset to persist changes."),
		*ChooserPath, Visited.Num(), PoseMatchColumnCount, DatabaseCount);
}

void FHybridMotionSystemEditorModule::FixRagdollGetUpChooser(const TArray<FString>& Args)
{
	const TCHAR* ChooserPath = TEXT("/Game/StreamerGirl/HMS/CHT_HMS_GetUpMontages.CHT_HMS_GetUpMontages");
	const TCHAR* SchemaPath = TEXT("/Game/StreamerGirl/HMS/PSS_HMS_Ragdoll.PSS_HMS_Ragdoll");
	const TCHAR* MontagePaths[] =
	{
		TEXT("/Game/StreamerGirl/Anim/Ragdoll/AM_M_ragdoll_getup_stand_F.AM_M_ragdoll_getup_stand_F"),
		TEXT("/Game/StreamerGirl/Anim/Ragdoll/AM_M_ragdoll_getup_stand_B.AM_M_ragdoll_getup_stand_B"),
		TEXT("/Game/StreamerGirl/Anim/Ragdoll/AM_M_ragdoll_getup_roll_R.AM_M_ragdoll_getup_roll_R"),
		TEXT("/Game/StreamerGirl/Anim/Ragdoll/AM_M_ragdoll_getup_roll_L.AM_M_ragdoll_getup_roll_L")
	};

	UChooserTable* Chooser = LoadObject<UChooserTable>(nullptr, ChooserPath);
	UPoseSearchSchema* Schema = LoadObject<UPoseSearchSchema>(nullptr, SchemaPath);
	if (!IsValid(Chooser) || !IsValid(Schema))
	{
		UE_LOG(LogTemp, Error, TEXT("[HMS Ragdoll Chooser] Missing chooser or schema. Chooser=%s Schema=%s"),
			*GetNameSafe(Chooser), *GetNameSafe(Schema));
		return;
	}

	TArray<UAnimationAsset*> Montages;
	for (const TCHAR* MontagePath : MontagePaths)
	{
		UAnimationAsset* Montage = LoadObject<UAnimationAsset>(nullptr, MontagePath);
		if (!IsValid(Montage))
		{
			UE_LOG(LogTemp, Error, TEXT("[HMS Ragdoll Chooser] Missing montage %s"), MontagePath);
			return;
		}
		Montages.Add(Montage);
	}

	Chooser->Modify();
#if WITH_EDITORONLY_DATA
	if (Chooser->ResultsStructs.Num() != Montages.Num())
	{
		UE_LOG(LogTemp, Error, TEXT("[HMS Ragdoll Chooser] Expected %d rows but found %d"),
			Montages.Num(), Chooser->ResultsStructs.Num());
		return;
	}

	for (int32 RowIndex = 0; RowIndex < Montages.Num(); ++RowIndex)
	{
		FAssetChooser* AssetResult = Chooser->ResultsStructs[RowIndex].GetMutablePtr<FAssetChooser>();
		if (!AssetResult)
		{
			UE_LOG(LogTemp, Error, TEXT("[HMS Ragdoll Chooser] Row %d is not an Asset result"), RowIndex);
			return;
		}
		AssetResult->Asset = Montages[RowIndex];
	}
#endif

	int32 PoseColumnCount = 0;
	TSet<UPoseSearchDatabase*> Databases;
	for (FInstancedStruct& ColumnStruct : Chooser->ColumnsStructs)
	{
		if (FPoseSearchColumn* PoseColumn = ColumnStruct.GetMutablePtr<FPoseSearchColumn>())
		{
			++PoseColumnCount;
			PoseColumn->MaxNumberOfResults = 1;
			FArrayProperty* RowValuesProperty = CastField<FArrayProperty>(
				FPoseSearchColumn::StaticStruct()->FindPropertyByName(TEXT("RowValues")));
			FScriptArrayHelper RowValues(
				RowValuesProperty,
				RowValuesProperty ? RowValuesProperty->ContainerPtrToValuePtr<void>(PoseColumn) : nullptr);
			for (int32 RowIndex = 0; RowIndex < Montages.Num() && RowIndex < PoseColumn->GetNumRows(); ++RowIndex)
			{
				if (RowValuesProperty && RowValues.IsValidIndex(RowIndex))
				{
					FPoseSearchColumnRow* Row = reinterpret_cast<FPoseSearchColumnRow*>(RowValues.GetRawPtr(RowIndex));
					Row->Data.AnimAsset = Montages[RowIndex];
				}
				if (UPoseSearchDatabase* Database = PoseColumn->GetDatabase(RowIndex))
				{
					Databases.Add(Database);
				}
			}
		}
	}

	for (UPoseSearchDatabase* Database : Databases)
	{
		Database->Modify();
		Database->Schema = Schema;
		Database->PostEditChange();
	}

	Chooser->PostEditChange();
	Chooser->Compile(true);
	Chooser->MarkPackageDirty();
	UE_LOG(LogTemp, Warning,
		TEXT("[HMS Ragdoll Chooser] Configured %s Rows=%d PoseColumns=%d Databases=%d Schema=%s"),
		ChooserPath, Montages.Num(), PoseColumnCount, Databases.Num(), *Schema->GetPathName());
}

void FHybridMotionSystemEditorModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);
	UToolMenu* ToolsMenu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Tools"));
	FToolMenuSection& Section = ToolsMenu->FindOrAddSection(
		TEXT("HybridMotionSystemTools"),
		LOCTEXT("HMSToolsSection", "Hybrid Motion System"));
	Section.AddMenuEntry(
		TEXT("HMSBatchAnimationRetarget"),
		LOCTEXT("HMSBatchAnimationRetargetLabel", "批量动画重定向"),
		LOCTEXT("HMSBatchAnimationRetargetTooltip", "将指定内容文件夹中的动画通过 IK 重定向器批量输出到另一个文件夹"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("GenericCurveEditor.TabIcon")),
		FUIAction(FExecuteAction::CreateRaw(this, &FHybridMotionSystemEditorModule::OpenBatchRetargetWindow)));
}

void FHybridMotionSystemEditorModule::OpenBatchRetargetWindow()
{
	const TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("HMSBatchRetargetWindowTitle", "HMS 批量动画重定向"))
		.ClientSize(FVector2D(620.0f, 650.0f))
		.SupportsMaximize(false)
		.SupportsMinimize(false)
		[
			SNew(SHMSBatchRetargetWindow)
		];

	FSlateApplication::Get().AddWindow(Window);
}

TSharedRef<FExtender> FHybridMotionSystemEditorModule::ExtendAnimationEditorToolbar(
	TSharedRef<FUICommandList> CommandList,
	TSharedRef<IAnimationEditor> AnimationEditor)
{
	TSharedRef<FExtender> Extender = MakeShared<FExtender>();

	Extender->AddToolBarExtension(
		"Asset",
		EExtensionHook::After,
		CommandList,
		FToolBarExtensionDelegate::CreateRaw(
			this,
			&FHybridMotionSystemEditorModule::AddToolbarButton,
			TWeakPtr<IAnimationEditor>(AnimationEditor)));

	return Extender;
}

void FHybridMotionSystemEditorModule::AddToolbarButton(
	FToolBarBuilder& ToolbarBuilder,
	TWeakPtr<IAnimationEditor> WeakAnimationEditor)
{
	ToolbarBuilder.AddComboButton(
		FUIAction(),
		FOnGetContent::CreateRaw(
			this,
			&FHybridMotionSystemEditorModule::GenerateCreateCurvesMenu,
			WeakAnimationEditor),
		LOCTEXT("HMSToolsMenu_Label", "HMS工具"),
		LOCTEXT("HMSToolsMenu_Tooltip", "动画曲线、智能对象通知与轨迹工具"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Plus"),
		false
	);
}

TSharedRef<SWidget> FHybridMotionSystemEditorModule::GenerateCreateCurvesMenu(
	TWeakPtr<IAnimationEditor> WeakAnimationEditor)
{
	FMenuBuilder MenuBuilder(true, nullptr);

	// ==================== 一级菜单 ====================
	MenuBuilder.BeginSection("HMS_Curves", LOCTEXT("HMS_Curves_Section", "HMS 曲线工具"));
	{
		// 创建「骨骼权重曲线」子菜单
		MenuBuilder.AddSubMenu(
			LOCTEXT("BoneWeightCurves_Label", "骨骼权重曲线"),
			LOCTEXT("BoneWeightCurves_Tooltip", "创建 Hybrid Motion System 相关的骨骼权重曲线"),
			FNewMenuDelegate::CreateRaw(
				this,
				&FHybridMotionSystemEditorModule::GenerateBoneWeightCurvesSubMenu,
				WeakAnimationEditor)
		);

		MenuBuilder.AddSubMenu(
			LOCTEXT("TurnCurves_Label", "转身曲线"),
			LOCTEXT("TurnCurves_Tooltip", "为原地转身动画创建相关曲线"),
			FNewMenuDelegate::CreateRaw(this, &FHybridMotionSystemEditorModule::GenerateTurnCurvesSubMenu, WeakAnimationEditor)
		);
	}
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection("HMS_Interaction", LOCTEXT("HMSInteractionSection", "智能对象交互"));
	MenuBuilder.AddMenuEntry(
		LOCTEXT("HMSInteractionAnimationLabel", "创建进入通知与轨迹…"),
		LOCTEXT("HMSInteractionAnimationTooltip", "设置允许接入与接触时间，一键生成 Pose Search 接入限制、Motion Warping 窗口和根轨迹参考曲线"),
		FSlateIcon(), FUIAction(FExecuteAction::CreateRaw(this,
			&FHybridMotionSystemEditorModule::OpenInteractionAnimationWindow, WeakAnimationEditor)));
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

void FHybridMotionSystemEditorModule::OpenInteractionAnimationWindow(TWeakPtr<IAnimationEditor> WeakAnimationEditor)
{
	const TSharedPtr<IAnimationEditor> Editor = WeakAnimationEditor.Pin();
	const TArray<UObject*>* Objects = Editor ? Editor->GetObjectsCurrentlyBeingEdited() : nullptr;
	if (Objects)
	{
		for (UObject* Object : *Objects)
		{
			if (UAnimSequence* Sequence = Cast<UAnimSequence>(Object))
			{
				UHMS_InteractionAnimationTools::OpenWindow(Sequence);
				return;
			}
		}
	}
	FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("HMSInteractionNeedsSequence", "请先打开源动画序列。Montage 的通知时间还涉及分段映射，本工具在动画序列上生成数据。"));
}

void FHybridMotionSystemEditorModule::GenerateBoneWeightCurvesSubMenu(
	FMenuBuilder& MenuBuilder,
	TWeakPtr<IAnimationEditor> WeakAnimationEditor)
{
	MenuBuilder.BeginSection("HMS_BoneWeightCurves", LOCTEXT("HMS_BoneWeightCurves_Section", "骨骼权重曲线"));
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("CreateLimbWeightCurves_Label", "创建躯干权重曲线"),
			LOCTEXT("CreateLimbWeightCurves_Tooltip", "创建 spine_01 到 spine_05 曲线"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateRaw(
				this, &FHybridMotionSystemEditorModule::OnClick_CreateLimbWeightCurves, WeakAnimationEditor))
		);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("CreateArmWeightCurves_Label", "创建双臂权重曲线"),
			LOCTEXT("CreateArmWeightCurves_Tooltip", "创建左右手臂相关骨骼曲线"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateRaw(
				this, &FHybridMotionSystemEditorModule::OnClick_CreateArmWeightCurves, WeakAnimationEditor))
		);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("CreatePelvisWeightCurves_Label", "创建盆骨权重曲线"),
			LOCTEXT("CreatePelvisWeightCurves_Tooltip", "创建 pelvis 曲线"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateRaw(
				this, &FHybridMotionSystemEditorModule::OnClick_CreatePelvisWeightCurves, WeakAnimationEditor))
		);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("CreateLegWeightCurves_Label", "创建双腿权重曲线"),
			LOCTEXT("CreateLegWeightCurves_Tooltip", "创建左右腿相关骨骼曲线"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateRaw(
				this, &FHybridMotionSystemEditorModule::OnClick_CreateLegWeightCurves, WeakAnimationEditor))
		);
	}
	MenuBuilder.EndSection();
}

void FHybridMotionSystemEditorModule::OnClick_CreateLimbWeightCurves(
	TWeakPtr<IAnimationEditor> WeakAnimationEditor)
{
	const TArray<FName> CurveNames =
	{
		TEXT("spine_01"),
		TEXT("spine_02"),
		TEXT("spine_03"),
		TEXT("spine_04"),
		TEXT("spine_05")
	};

	CreateCurvesForEditedObjects(WeakAnimationEditor, CurveNames, 0.0f);
}

void FHybridMotionSystemEditorModule::OnClick_CreateArmWeightCurves(
	TWeakPtr<IAnimationEditor> WeakAnimationEditor)
{
	const TArray<FName> CurveNames =
	{
		TEXT("clavicle_l"),
		TEXT("upperarm_l"),
		TEXT("lowerarm_l"),
		TEXT("hand_l"),

		TEXT("clavicle_r"),
		TEXT("upperarm_r"),
		TEXT("lowerarm_r"),
		TEXT("hand_r")
	};

	CreateCurvesForEditedObjects(WeakAnimationEditor, CurveNames, 0.0f);
}

void FHybridMotionSystemEditorModule::OnClick_CreatePelvisWeightCurves(
	TWeakPtr<IAnimationEditor> WeakAnimationEditor)
{
	const TArray<FName> CurveNames =
	{
		TEXT("pelvis")
	};

	CreateCurvesForEditedObjects(WeakAnimationEditor, CurveNames, 0.0f);
}

void FHybridMotionSystemEditorModule::OnClick_CreateLegWeightCurves(
	TWeakPtr<IAnimationEditor> WeakAnimationEditor)
{
	const TArray<FName> CurveNames =
	{
		TEXT("thigh_l"),
		TEXT("calf_l"),
		TEXT("foot_l"),
		TEXT("ball_l"),

		TEXT("thigh_r"),
		TEXT("calf_r"),
		TEXT("foot_r"),
		TEXT("ball_r")
	};

	CreateCurvesForEditedObjects(WeakAnimationEditor, CurveNames, 0.0f);
}

void FHybridMotionSystemEditorModule::CreateCurvesForEditedObjects(
	TWeakPtr<IAnimationEditor> WeakAnimationEditor,
	const TArray<FName>& CurveNames,
	float DefaultValue) const
{
	TSharedPtr<IAnimationEditor> AnimationEditor = WeakAnimationEditor.Pin();
	if (!AnimationEditor.IsValid())
	{
		return;
	}

	const TArray<UObject*>* EditedObjectsPtr = AnimationEditor->GetObjectsCurrentlyBeingEdited();
	if (!EditedObjectsPtr)
	{
		return;
	}

	for (UObject* Object : *EditedObjectsPtr)
	{
		if (UAnimSequence* AnimSequence = Cast<UAnimSequence>(Object))
		{
			for (const FName CurveName : CurveNames)
			{
				EnsureFloatCurve(AnimSequence, CurveName, DefaultValue);
			}

			AnimSequence->MarkPackageDirty();
			AnimSequence->PostEditChange();
		}
	}
}

void FHybridMotionSystemEditorModule::EnsureFloatCurve(
	UAnimSequence* AnimSequence,
	FName CurveName,
	float DefaultValue) const
{
	IAnimationDataController& Controller = AnimSequence->GetController();
	const FAnimationCurveIdentifier CurveId(CurveName, ERawCurveTrackTypes::RCT_Float);

	Controller.OpenBracket(FText::Format(
		LOCTEXT("AddCurveBracket", "Add Curve {0}"),
		FText::FromName(CurveName)));

	Controller.AddCurve(CurveId, 0, false);

	FRichCurveKey StartKey;
	StartKey.Time = 0.0f;
	StartKey.Value = DefaultValue;

	Controller.SetCurveKey(CurveId, StartKey, false);
	Controller.CloseBracket();
}

void FHybridMotionSystemEditorModule::GenerateTurnCurvesSubMenu(
	FMenuBuilder& MenuBuilder,
	TWeakPtr<IAnimationEditor> WeakAnimationEditor)
{
	MenuBuilder.BeginSection("HMS_TurnCurves", LOCTEXT("HMS_TurnCurves_Section", "转身曲线"));
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("CreateTurnInPlaceSteeringCurve_Label", "创建 Enable_TurnInPlaceSteering 曲线"),
			LOCTEXT("CreateTurnInPlaceSteeringCurve_Tooltip",
				"智能创建曲线：开始为1，根骨骼旋转停止后3帧变为0"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateRaw(
				this, &FHybridMotionSystemEditorModule::OnClick_CreateTurnInPlaceSteeringCurve, WeakAnimationEditor))
		);
	}
	MenuBuilder.EndSection();
}

void FHybridMotionSystemEditorModule::OnClick_CreateTurnInPlaceSteeringCurve(
	TWeakPtr<IAnimationEditor> WeakAnimationEditor)
{
	TSharedPtr<IAnimationEditor> AnimationEditor = WeakAnimationEditor.Pin();
	if (!AnimationEditor.IsValid()) return;

	const TArray<UObject*>* EditedObjects = AnimationEditor->GetObjectsCurrentlyBeingEdited();
	if (!EditedObjects) return;

	for (UObject* Object : *EditedObjects)
	{
		if (UAnimSequence* AnimSequence = Cast<UAnimSequence>(Object))
		{
			CreateEnableTurnInPlaceSteeringCurve(AnimSequence);
			AnimSequence->MarkPackageDirty();
			AnimSequence->PostEditChange();
		}
	}
}
void FHybridMotionSystemEditorModule::CreateEnableTurnInPlaceSteeringCurve(UAnimSequence* AnimSequence) const
{
	if (!AnimSequence) return;

	IAnimationDataController& Controller = AnimSequence->GetController();
	const FAnimationCurveIdentifier CurveId(TEXT("Enable_TurnInPlaceSteering"), ERawCurveTrackTypes::RCT_Float);

	const float FrameRate = AnimSequence->GetSamplingFrameRate().AsInterval();
	const float AnimLength = AnimSequence->GetPlayLength();
	const int32 TotalFrames = AnimSequence->GetNumberOfSampledKeys();

	Controller.OpenBracket(LOCTEXT("CreateTurnSteeringCurve", "Create Enable_TurnInPlaceSteering Curve"));

	// 删除旧曲线
	Controller.RemoveCurve(CurveId);
	Controller.AddCurve(CurveId, 0, false);

	// ==================== 检测根骨骼旋转停止时间 ====================
	float LastSignificantRotationTime = 0.0f;
	const float RotationThreshold = 0.8f; // 可根据需要调整敏感度

	FTransform PreviousRootTransform = FTransform::Identity;

	for (int32 Frame = 0; Frame < TotalFrames; ++Frame)
	{
		const float Time = Frame * FrameRate;

		FTransform CurrentRootTransform = FTransform::Identity;

		if (AnimSequence->HasRootMotion())
		{
			const double EndTime = FMath::Min(static_cast<double>(Time + FrameRate), static_cast<double>(AnimLength));
			CurrentRootTransform = AnimSequence->ExtractRootMotionFromRange(
				static_cast<double>(Time),
				EndTime,
				FAnimExtractContext(static_cast<double>(Time), false));
		}

		const float DeltaYaw = FMath::FindDeltaAngleDegrees(
			PreviousRootTransform.Rotator().Yaw,
			CurrentRootTransform.Rotator().Yaw);

		if (FMath::Abs(DeltaYaw) > RotationThreshold)
		{
			LastSignificantRotationTime = Time;
		}

		PreviousRootTransform = CurrentRootTransform;
	}

	// 旋转停止后延迟3帧的时间点
	const float DropTime = FMath::Min(LastSignificantRotationTime + (FrameRate * 3.0f), AnimLength);

	// ==================== 创建曲线关键帧（关键修改） ====================

	// 第1个关键帧：从0开始就为1
	FRichCurveKey StartKey;
	StartKey.Time = 0.0f;
	StartKey.Value = 1.0f;
	StartKey.InterpMode = ERichCurveInterpMode::RCIM_Constant; // 保持为1
	Controller.SetCurveKey(CurveId, StartKey, false);

	// 第2个关键帧：旋转停止后3帧，突然变为0（使用 Constant 模式）
	FRichCurveKey EndKey;
	EndKey.Time = DropTime;
	EndKey.Value = 0.0f;
	EndKey.InterpMode = ERichCurveInterpMode::RCIM_Constant;   // 【核心】突然从1跳到0
	Controller.SetCurveKey(CurveId, EndKey, false);

	Controller.CloseBracket();

	UE_LOG(LogTemp, Log, TEXT("[HMS Editor] 已为 [%s] 创建 Enable_TurnInPlaceSteering 曲线（突然切换模式），旋转停止时间: %.3fs，归零时间: %.3fs"),
		*AnimSequence->GetName(), LastSignificantRotationTime, DropTime);
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FHybridMotionSystemEditorModule, HybridMotionSystemEditor)
