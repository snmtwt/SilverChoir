#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/LocalPlayer.h"
#include "Map/GameMainMap/GameMainMapPlayerController.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "FCS_FreeCameraPawn.h"

class FVerifyMiddleMouse : public IAutomationLatentCommand
{
    FAutomationTestBase* Test;
    TWeakObjectPtr<AGameMainMapPlayerController> Controller;
    int32 Phase=0;
public:
    FVerifyMiddleMouse(FAutomationTestBase* InTest,AGameMainMapPlayerController* InPC):Test(InTest),Controller(InPC) {}
    virtual bool Update() override
    {
        auto* PC=Controller.Get();
        auto* Camera=PC?Cast<AFCS_FreeCameraPawn>(PC->GetPawn()):nullptr;
        auto* Sub=PC?ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()):nullptr;
        auto* Action=LoadObject<UInputAction>(nullptr,TEXT("/Game/System/Input/Common/IA_MouseMiddle"));
        if (!Camera || !Sub || !Action) { Test->AddError(TEXT("Missing player camera, local input subsystem or middle action"));return true; }
        if (Phase==0)
        {
            Test->TestTrue(TEXT("Controller Blueprint enables common mapping"),Sub->HasMappingContext(PC->CommonInputMappingContext));
            Test->TestFalse(TEXT("Rotation initially off"),Camera->bMouseRotateMode);
            Sub->InjectInputForAction(Action,FInputActionValue(true),{},{});Phase=1;return false;
        }
        if (Phase==1)
        {
            Test->TestTrue(TEXT("Started event enables rotation"),Camera->bMouseRotateMode);
            Sub->InjectInputForAction(Action,FInputActionValue(false),{},{});Phase=2;return false;
        }
        Test->TestFalse(TEXT("Completed event disables rotation"),Camera->bMouseRotateMode);
        return true;
    }
};
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMiddleMouseInputTest,"SilverChoir.Input.MiddleMouseCamera",EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FMiddleMouseInputTest::RunTest(const FString& Parameters)
{
    for (const auto& Context:GEngine->GetWorldContexts())
        if (Context.WorldType==EWorldType::Game && Context.World())
            if (auto* PC=Cast<AGameMainMapPlayerController>(Context.World()->GetFirstPlayerController()))
            { ADD_LATENT_AUTOMATION_COMMAND(FVerifyMiddleMouse(this,PC));return true; }
    AddError(TEXT("Requires GameMainMap game world"));return false;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"
#include "InputCoreTypes.h"

class FVerifyMiddleMouseMotion : public IAutomationLatentCommand
{
    FAutomationTestBase* Test;
    TWeakObjectPtr<AGameMainMapPlayerController> Controller;
    int32 Phase=0;
    float InitialYaw=0, RotatedYaw=0;
public:
    FVerifyMiddleMouseMotion(FAutomationTestBase* T,AGameMainMapPlayerController* PC):Test(T),Controller(PC) {}
    virtual bool Update() override
    {
        auto* PC=Controller.Get();
        auto* Camera=PC?Cast<AFCS_FreeCameraPawn>(PC->GetPawn()):nullptr;
        UGameViewportClient* Client=PC && PC->GetLocalPlayer()?PC->GetLocalPlayer()->ViewportClient:nullptr;
        FViewport* Viewport=Client?Client->Viewport:nullptr;
        if (!Camera || !Viewport) { Test->AddError(TEXT("Requires rendered GameMainMap viewport"));return true; }
        if (Phase==0)
        {
            Test->TestFalse(TEXT("Capture must not freeze visible cursor coordinates"),Client->HideCursorDuringCapture());

            InitialYaw=Camera->GetActorRotation().Yaw;
            Viewport->SetMouse(200,200);
            PC->InputKey(FInputKeyEventArgs(Viewport,FInputDeviceId::CreateFromInternalId(0),EKeys::MiddleMouseButton,IE_Pressed,1.f,false,FPlatformTime::Cycles64()));
            ++Phase;return false;
        }
        if (Phase==1)
        {
            Test->TestTrue(TEXT("Physical middle key mapping starts rotation"),Camera->bMouseRotateMode);
            Viewport->SetMouse(250,220);++Phase;return false;
        }
        if (Phase==2)
        {
            RotatedYaw=Camera->GetActorRotation().Yaw;
            Test->TestFalse(TEXT("Moving mouse changes actual camera yaw"),FMath::IsNearlyEqual(InitialYaw,RotatedYaw));
            PC->InputKey(FInputKeyEventArgs(Viewport,FInputDeviceId::CreateFromInternalId(0),EKeys::MiddleMouseButton,IE_Released,0.f,false,FPlatformTime::Cycles64()));
            ++Phase;return false;
        }
        if (Phase==3)
        {
            Test->TestFalse(TEXT("Physical middle release stops rotation"),Camera->bMouseRotateMode);
            RotatedYaw=Camera->GetActorRotation().Yaw;
            Viewport->SetMouse(280,240);++Phase;return false;
        }
        Test->TestTrue(TEXT("Mouse movement after release does not rotate"),FMath::IsNearlyEqual(RotatedYaw,Camera->GetActorRotation().Yaw));
        return true;
    }
};
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMiddleMouseMotionTest,"SilverChoir.Input.MiddleMouseMotion",EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FMiddleMouseMotionTest::RunTest(const FString& Parameters)
{
    for (const auto& Context:GEngine->GetWorldContexts())
        if (Context.WorldType==EWorldType::Game && Context.World())
            if (auto* PC=Cast<AGameMainMapPlayerController>(Context.World()->GetFirstPlayerController()))
            { ADD_LATENT_AUTOMATION_COMMAND(FVerifyMiddleMouseMotion(this,PC));return true; }
    AddError(TEXT("Requires GameMainMap game world"));return false;
}
#endif

