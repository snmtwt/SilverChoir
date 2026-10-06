// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AnimGraphNode_BlendListBase.h"
#include "AnimNode_CurveDrivenBoneBlend.h"
#include "HMS_CurveDrivenBoneBlend.generated.h"

/**
 * 
 */
UCLASS(MinimalAPI)
class UHMS_CurveDrivenBoneBlend : public UAnimGraphNode_BlendListBase
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, Category = Settings)
    FAnimNode_CurveDrivenBoneBlend Node;

    virtual FLinearColor GetNodeTitleColor() const override;

    virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override
    {
        return FText::FromString(TEXT("Curve Driven Bone Blend"));
    }

    virtual FText GetTooltipText() const override
    {
        return FText::FromString(TEXT("Minimal custom anim node"));
    }

    virtual FString GetNodeCategory() const override
    {
        return TEXT("HybridMotionSystem");
    }
};
