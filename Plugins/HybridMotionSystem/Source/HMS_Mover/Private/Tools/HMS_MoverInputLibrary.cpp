// Fill out your copyright notice in the Description page of Project Settings.


#include "Tools/HMS_MoverInputLibrary.h"
#include "DefaultMovementSet/CharacterMoverComponent.h"
#include "DefaultMovementSet/NavMoverComponent.h"


// ==================== 玩家输入版本（已移除 AimDirection） ====================
void UHMS_MoverInputLibrary::ProducePlayerHMSInput(
    AController* Controller,
    APawn* OwningPawn,
    FVector2D PlayerMoveInput,
    EHMS_RotationMode RotationMode,
    EHMS_Gait Gait,
    float AimTurnThresholdDegrees,
    FMoverInputCmdContext& OutInputCmd)
{
    FCharacterDefaultInputs DefaultInputs;
    FHMS_MoverInput NavInputs;

    FVector MoveDirection = FVector::ZeroVector;

    // 1. 计算世界移动方向（基于 Controller）
    if (Controller)
    {
        FRotator ControlRot = Controller->GetControlRotation();
        ControlRot.Pitch = 0.f;
        ControlRot.Roll = 0.f;

        FVector Forward = FRotationMatrix(ControlRot).GetUnitAxis(EAxis::X);
        FVector Right = FRotationMatrix(ControlRot).GetUnitAxis(EAxis::Y);

        MoveDirection = (Forward * PlayerMoveInput.Y + Right * PlayerMoveInput.X).GetSafeNormal();
    }
    else
    {
        MoveDirection = FVector(PlayerMoveInput.Y, PlayerMoveInput.X, 0.f).GetSafeNormal();
    }

    DefaultInputs.SetMoveInput(EMoveInputType::DirectionalIntent, MoveDirection);

    // 2. 根据 RotationMode 计算朝向意图
    FVector OrientationIntent = FVector::ZeroVector;

    switch (RotationMode)
    {
    case EHMS_RotationMode::OrientToMovement:
        OrientationIntent = MoveDirection;
        break;

    case EHMS_RotationMode::Strafe:
    case EHMS_RotationMode::Aim:          // Aim 模式也使用 Controller 前向
        if (Controller)
        {
            FRotator ControlRot = Controller->GetControlRotation();
            ControlRot.Pitch = 0.f;
            ControlRot.Roll = 0.f;
            OrientationIntent = FRotationMatrix(ControlRot).GetUnitAxis(EAxis::X);
        }
        else if (OwningPawn)
        {
            OrientationIntent = OwningPawn->GetActorForwardVector();
        }
        break;
    }

    DefaultInputs.OrientationIntent = OrientationIntent;

    // 3. 填充自定义输入结构体
    NavInputs.bHasNavigationInput = !MoveDirection.IsNearlyZero();
    NavInputs.WorldMoveDirection = MoveDirection;
    NavInputs.RequestedVelocity = FVector::ZeroVector;
    NavInputs.RequestedSpeed = 0.f;
    NavInputs.bOrientToMovement = (RotationMode == EHMS_RotationMode::OrientToMovement);
    NavInputs.RotationMode = RotationMode;
    NavInputs.Gait = Gait;
    if (const UCharacterMoverComponent* CharacterMover =
        OwningPawn ? OwningPawn->FindComponentByClass<UCharacterMoverComponent>() : nullptr)
    {
        NavInputs.bWantsToCrouch = CharacterMover->GetCrouchIntent();
    }

    NavInputs.bHasAimTarget = (RotationMode == EHMS_RotationMode::Aim);
    NavInputs.AimTargetLocation = FVector::ZeroVector;
    NavInputs.AimDirection = OrientationIntent;   // 用当前朝向填充
    NavInputs.AimTurnThresholdDegrees = AimTurnThresholdDegrees;

    // 4. 写入 InputCollection
    FCharacterDefaultInputs& DefaultRef = OutInputCmd.InputCollection.FindOrAddMutableDataByType<FCharacterDefaultInputs>();
    DefaultRef = DefaultInputs;

    FHMS_MoverInput& NavRef = OutInputCmd.InputCollection.FindOrAddMutableDataByType<FHMS_MoverInput>();
    NavRef = NavInputs;
}

// ==================== AI导航版本（已移除 AimDirection） ====================
void UHMS_MoverInputLibrary::ProduceNavHMSInput(
    AController* Controller,
    APawn* OwningPawn,
    UNavMoverComponent* NavMover,
    EHMS_RotationMode RotationMode,
    EHMS_Gait Gait,
    float AimTurnThresholdDegrees,
    FMoverInputCmdContext& OutInputCmd)
{
    FCharacterDefaultInputs DefaultInputs;
    FHMS_MoverInput NavInputs;

    FVector MoveInputIntent = FVector::ZeroVector;
    FVector MoveInputVelocity = FVector::ZeroVector;
    FVector MoveDirection = FVector::ZeroVector;

    // 1. 从 NavMover 获取导航数据
    if (NavMover && NavMover->ConsumeNavMovementData(MoveInputIntent, MoveInputVelocity))
    {
        MoveInputIntent.Z = 0.f;
        MoveInputVelocity.Z = 0.f;

        if (!MoveInputVelocity.IsNearlyZero())
            MoveDirection = MoveInputVelocity.GetSafeNormal();
        else if (!MoveInputIntent.IsNearlyZero())
            MoveDirection = MoveInputIntent.GetSafeNormal();
    }

    DefaultInputs.SetMoveInput(EMoveInputType::DirectionalIntent, MoveDirection);

    // 2. 根据 RotationMode 计算朝向意图
    FVector OrientationIntent = FVector::ZeroVector;

    switch (RotationMode)
    {
    case EHMS_RotationMode::OrientToMovement:
        OrientationIntent = MoveDirection;
        break;

    case EHMS_RotationMode::Strafe:
    case EHMS_RotationMode::Aim:
        if (Controller)
        {
            FRotator ControlRot = Controller->GetControlRotation();
            ControlRot.Pitch = 0.f;
            ControlRot.Roll = 0.f;
            OrientationIntent = FRotationMatrix(ControlRot).GetUnitAxis(EAxis::X);
        }
        else if (OwningPawn)
        {
            OrientationIntent = OwningPawn->GetActorForwardVector();
        }
        break;
    }

    DefaultInputs.OrientationIntent = OrientationIntent;

    // 3. 填充自定义输入结构体
    NavInputs.bHasNavigationInput = !MoveDirection.IsNearlyZero();
    NavInputs.WorldMoveDirection = MoveDirection;
    NavInputs.RequestedVelocity = MoveInputVelocity;
    NavInputs.RequestedSpeed = MoveInputVelocity.Size2D();
    NavInputs.bOrientToMovement = (RotationMode == EHMS_RotationMode::OrientToMovement);
    NavInputs.RotationMode = RotationMode;
    NavInputs.Gait = Gait;
    if (const UCharacterMoverComponent* CharacterMover =
        OwningPawn ? OwningPawn->FindComponentByClass<UCharacterMoverComponent>() : nullptr)
    {
        NavInputs.bWantsToCrouch = CharacterMover->GetCrouchIntent();
    }

    NavInputs.bHasAimTarget = (RotationMode == EHMS_RotationMode::Aim);
    NavInputs.AimTargetLocation = FVector::ZeroVector;
    NavInputs.AimDirection = OrientationIntent;
    NavInputs.AimTurnThresholdDegrees = AimTurnThresholdDegrees;

    // 4. 写入 InputCollection
    FCharacterDefaultInputs& DefaultRef = OutInputCmd.InputCollection.FindOrAddMutableDataByType<FCharacterDefaultInputs>();
    DefaultRef = DefaultInputs;

    FHMS_MoverInput& NavRef = OutInputCmd.InputCollection.FindOrAddMutableDataByType<FHMS_MoverInput>();
    NavRef = NavInputs;
}
