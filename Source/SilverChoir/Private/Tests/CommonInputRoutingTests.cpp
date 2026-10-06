#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "Map/GameMainMap/GameMainMapPlayerController.h"
#include "MTS_SubMapSubsystem.h"

namespace CommonInputRouting
{
class FCheck final : public IAutomationLatentCommand
{
public:
    explicit FCheck(FAutomationTestBase* InTest):Test(InTest){}
    ~FCheck()
    {
        if(PC.IsValid())
        {
            for(const FKey Key:Keys) Send(Key,IE_Released);
            PC->SetPause(false);
            if(auto* Input=Cast<UEnhancedInputComponent>(PC->InputComponent))for(uint32 Handle:Handles)Input->RemoveBindingByHandle(Handle);
            if(auto* State=PC->GetWorld()->GetGameState<AGameMainMapGameState>())State->SetCurrentMapType(Original);
        }
    }
    bool Update() override
    {
        if(FPlatformTime::Seconds()-Started>90){Test->AddError(TEXT("Input routing test timed out"));return true;}
        if(!PC.IsValid())
        {
            for(const auto& Context:GEngine->GetWorldContexts())if(Context.WorldType==EWorldType::Game)
                PC=Cast<AGameMainMapPlayerController>(Context.World()->GetFirstPlayerController());
            if(!PC.IsValid() || !PC->InputComponent)return false;
            auto* Input=Cast<UEnhancedInputComponent>(PC->InputComponent);
            auto* State=PC->GetWorld()->GetGameState<AGameMainMapGameState>();
            if(!Input || !State)return false;
            Original=State->GetCurrentMapType();
            if(!Test->TestTrue(TEXT("Uses the actual controller Blueprint"),PC->GetClass()->ClassGeneratedBy!=nullptr))return true;
            const TCHAR* Paths[]={TEXT("/Game/System/Input/Common/IA_MouseLeft"),TEXT("/Game/System/Input/Common/IA_MouseMiddle"),TEXT("/Game/System/Input/Common/IA_MouseRight"),TEXT("/Game/System/Input/Battle/IA_BattleMove")};
            for(int I=0;I<4;++I)
            {
                auto* Action=LoadObject<UInputAction>(nullptr,Paths[I]);
                if(!Test->TestNotNull(TEXT("Input action exists"),Action))return true;
                Handles.Add(Input->BindActionValueLambda(Action,ETriggerEvent::Started,[this,I](const FInputActionValue&){++Down[I];}).GetHandle());
                Handles.Add(Input->BindActionValueLambda(Action,ETriggerEvent::Completed,[this,I](const FInputActionValue&){++Up[I];}).GetHandle());
            }
        }
        auto* State=PC->GetWorld()->GetGameState<AGameMainMapGameState>();
        auto* Input=PC->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
        if(PC->GetWorld()->GetSubsystem<UMTS_SubMapSubsystem>()->IsSubMapTransitionInProgress())return false;
        if(Step==0)
        {
            if(Case==UE_ARRAY_COUNT(Modes))return true;
            PC->SetPause(false);
            State->SetCurrentMapType(Modes[Case]);
            if(Case==4)PC->SetPause(true);
            Test->TestTrue(TEXT("Common context remains registered"),Input->HasMappingContext(PC->CommonInputMappingContext));
            Test->TestEqual(TEXT("Base context matches map type"),Input->HasMappingContext(PC->BaseInputMappingContext),Modes[Case]==EGameMainMapType::Base);
            Test->TestEqual(TEXT("Battle context matches map type"),Input->HasMappingContext(PC->BattleInputMappingContext),Modes[Case]==EGameMainMapType::Battle);
            for(int I=0;I<4;++I){Down[I]=0;Up[I]=0;}
            Step=1;Frames=0;return false;
        }
        if(++Frames<4)return false;
        Frames=0;
        if(Step==1){for(const auto Key:Keys)Send(Key,IE_Pressed);Step=2;return false;}
        if(Step==2){for(const auto Key:Keys)Send(Key,IE_Released);Step=3;return false;}
        for(int I=0;I<3;++I)
        {
            Test->TestEqual(FString::Printf(TEXT("Case %d common button %d Started once"),Case,I),Down[I],1);
            Test->TestEqual(FString::Printf(TEXT("Case %d common button %d Completed once"),Case,I),Up[I],1);
        }
        const int ExpectedMove=Modes[Case]==EGameMainMapType::Battle && Case!=4?1:0;
        Test->TestEqual(TEXT("Battle action coexists with common right-click only during unpaused battle"),Down[3],ExpectedMove);
        UE_LOG(LogTemp,Display,TEXT("COMMON_INPUT_CASE %d common=%d,%d,%d battle=%d"),Case,Down[0],Down[1],Down[2],Down[3]);
        ++Case;Step=0;return false;
    }
private:
    void Send(FKey Key,EInputEvent Event)
    {
        auto* Viewport=PC->GetWorld()->GetGameViewport()->Viewport;
        PC->InputKey(FInputKeyEventArgs(Viewport,FInputDeviceId::CreateFromInternalId(0),Key,Event,Event==IE_Pressed?1.f:0.f,false,FPlatformTime::Cycles64()));
    }
    FAutomationTestBase* Test;
    TWeakObjectPtr<AGameMainMapPlayerController> PC;
    TArray<uint32> Handles;
    const FKey Keys[3]={EKeys::LeftMouseButton,EKeys::MiddleMouseButton,EKeys::RightMouseButton};
    const EGameMainMapType Modes[6]={EGameMainMapType::None,EGameMainMapType::Base,EGameMainMapType::Battle,EGameMainMapType::Base,EGameMainMapType::Battle,EGameMainMapType::None};
    EGameMainMapType Original=EGameMainMapType::None;
    int Down[4]={},Up[4]={},Case=0,Step=0,Frames=0;
    double Started=FPlatformTime::Seconds();
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCommonInputRoutingTest,"SilverChoir.Input.CommonAcrossMapTypes",EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FCommonInputRoutingTest::RunTest(const FString&)
{ ADD_LATENT_AUTOMATION_COMMAND(CommonInputRouting::FCheck(this));return true; }
#endif
