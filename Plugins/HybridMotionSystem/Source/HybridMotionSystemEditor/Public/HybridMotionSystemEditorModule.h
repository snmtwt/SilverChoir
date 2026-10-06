#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Modules/ModuleManager.h"
#include "IAnimationEditor.h"

class FUICommandList;
class FExtender;
class FToolBarBuilder;
class FMenuBuilder;
class UAnimSequence;
class IConsoleObject;

class FHybridMotionSystemEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void RegisterMenus();
	void OpenBatchRetargetWindow();
	void OpenInteractionAnimationWindow(TWeakPtr<IAnimationEditor> WeakAnimationEditor);
	void FixChooserForUE58(const TArray<FString>& Args);
	void FixRagdollGetUpChooser(const TArray<FString>& Args);
	void TranslateSampleRagdollComments(const TArray<FString>& Args);
	bool RunStartupChooserMigration(float DeltaTime);
	IConsoleObject* FixChooser58ConsoleCommand = nullptr;
	IConsoleObject* FixRagdollChooserConsoleCommand = nullptr;
	IConsoleObject* TranslateRagdollCommentsConsoleCommand = nullptr;
	FTSTicker::FDelegateHandle StartupChooserMigrationHandle;

	TSharedRef<FExtender> ExtendAnimationEditorToolbar(
		TSharedRef<FUICommandList> CommandList,
		TSharedRef<IAnimationEditor> AnimationEditor);

	void AddToolbarButton(
		FToolBarBuilder& ToolbarBuilder,
		TWeakPtr<IAnimationEditor> WeakAnimationEditor);

	TSharedRef<SWidget> GenerateCreateCurvesMenu(
		TWeakPtr<IAnimationEditor> WeakAnimationEditor);

#pragma region 骨格权重曲线

	void GenerateBoneWeightCurvesSubMenu(
		FMenuBuilder& MenuBuilder,
		TWeakPtr<IAnimationEditor> WeakAnimationEditor);

	void OnClick_CreateLimbWeightCurves(
		TWeakPtr<IAnimationEditor> WeakAnimationEditor);

	void OnClick_CreateArmWeightCurves(
		TWeakPtr<IAnimationEditor> WeakAnimationEditor);

	void OnClick_CreatePelvisWeightCurves(
		TWeakPtr<IAnimationEditor> WeakAnimationEditor);

	void OnClick_CreateLegWeightCurves(
		TWeakPtr<IAnimationEditor> WeakAnimationEditor);

	void CreateCurvesForEditedObjects(
		TWeakPtr<IAnimationEditor> WeakAnimationEditor,
		const TArray<FName>& CurveNames,
		float DefaultValue) const;

	void EnsureFloatCurve(UAnimSequence* AnimSequence, FName CurveName, float DefaultValue) const;

#pragma endregion

#pragma region 转身曲线

	void GenerateTurnCurvesSubMenu(
		FMenuBuilder& MenuBuilder,
		TWeakPtr<IAnimationEditor> WeakAnimationEditor);

	void OnClick_CreateTurnInPlaceSteeringCurve(TWeakPtr<IAnimationEditor> WeakAnimationEditor);

	void CreateEnableTurnInPlaceSteeringCurve(UAnimSequence* AnimSequence) const;
#pragma endregion

};
