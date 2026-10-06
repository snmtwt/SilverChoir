// Fill out your copyright notice in the Description page of Project Settings.


#include "UpperBodyAnimInstance.h"
#include "ChooserFunctionLibrary.h"

void UUpperBodyAnimInstance::UpdateOverlayerAnim()
{
	CurrentOverlayAnim = nullptr;

	if (!IsValid(ChooserTable))
	{
		return;
	}

	FChooserEvaluationContext Context;
	Context.AddObjectParam(this);

	const FInstancedStruct ChooserStruct =
		UChooserFunctionLibrary::MakeEvaluateChooser(ChooserTable);

	const TArray<UObject*> FoundObjects =
		UChooserFunctionLibrary::EvaluateObjectChooserBaseMulti(
			Context,
			ChooserStruct,
			UAnimationAsset::StaticClass()
		);

	if (FoundObjects.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("[Overlayer] No animation found"));
		return;
	}

	CurrentOverlayAnim = Cast<UAnimationAsset>(FoundObjects[0]);

	if (!CurrentOverlayAnim)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Overlayer] Cast failed, object is not AnimationAsset"));
	}
}
