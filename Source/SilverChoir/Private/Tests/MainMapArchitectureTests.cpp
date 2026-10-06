#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/LocalPlayer.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "Map/GameMainMap/GameMainMapLibrary.h"
#include "Map/GameMainMap/GameMainMapGameMode.h"
#include "Map/GameMainMap/GameMainMapPlayerController.h"
#include "Map/GameMainMap/GameMainMapPlayerState.h"
#include "SMS_SceneBase.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMainMapMappingLifecycleTest, "SilverChoir.Input.MainMapMappingLifecycle", EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FMainMapMappingLifecycleTest::RunTest(const FString& Parameters)
{
    UWorld* World = nullptr;
    for (const auto& Context : GEngine->GetWorldContexts())
        if (Context.WorldType == EWorldType::Game && Context.World() && Context.World()->GetGameState<AGameMainMapGameState>()) { World = Context.World(); break; }
    auto* PC = World ? Cast<AGameMainMapPlayerController>(World->GetFirstPlayerController()) : nullptr;
    auto* Input = PC && PC->GetLocalPlayer() ? PC->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
    if (!TestNotNull(TEXT("Requires actual GameMainMap local input subsystem"), Input)) return false;
    auto* State = World->GetGameState<AGameMainMapGameState>();
    const auto Original = State->GetCurrentMapType();
    TestNotNull(TEXT("Common mapping configured"), PC->CommonInputMappingContext.Get());
    TestNotNull(TEXT("Base mapping configured"), PC->BaseInputMappingContext.Get());
    TestNotNull(TEXT("Battle mapping configured"), PC->BattleInputMappingContext.Get());
    TestTrue(TEXT("BeginPlay activates common mapping before any mode request"), Input->HasMappingContext(PC->CommonInputMappingContext));
    TStrongObjectPtr<UInputMappingContext> Unrelated(NewObject<UInputMappingContext>());
    Input->AddMappingContext(Unrelated.Get(), 77);
    for (const auto Mode : {EGameMainMapType::None,EGameMainMapType::Base,EGameMainMapType::Battle,EGameMainMapType::Base,EGameMainMapType::Base,EGameMainMapType::None})
    {
        State->SetCurrentMapType(Mode);
        TestTrue(TEXT("Common mapping survives every mode switch"), Input->HasMappingContext(PC->CommonInputMappingContext));
        TestEqual(TEXT("Only base mode activates base mapping"), Input->HasMappingContext(PC->BaseInputMappingContext), Mode == EGameMainMapType::Base);
        TestEqual(TEXT("Only battle mode activates battle mapping"), Input->HasMappingContext(PC->BattleInputMappingContext), Mode == EGameMainMapType::Battle);
        TestTrue(TEXT("Mode switching preserves unrelated UI/plugin contexts"), Input->HasMappingContext(Unrelated.Get()));
        TestTrue(TEXT("Map-type library reflects the authoritative state"), UGameMainMapLibrary::GetCurrentMapType(PC) == Mode);
    }
    State->SetCurrentMapType(static_cast<EGameMainMapType>(255));
    TestTrue(TEXT("Invalid map type is ignored"), State->GetCurrentMapType() == EGameMainMapType::None);
    State->SetCurrentMapType(Original);
    Input->RemoveMappingContext(Unrelated.Get());

    AGameMainMapGameMode* Mode = nullptr; AGameMainMapGameState* GotState = nullptr;
    AGameMainMapPlayerController* GotPC = nullptr; AGameMainMapPlayerState* PlayerState = nullptr; APawn* Pawn = nullptr;
    UGameMainMapLibrary::GetGameMainMapObjects(PC, Mode, GotState, GotPC, PlayerState, Pawn);
    TestTrue(TEXT("Combined node resolves the actual GameMode"), Mode && Mode == World->GetAuthGameMode());
    TestTrue(TEXT("Combined node resolves the actual GameState"), GotState == State);
    TestTrue(TEXT("Combined node resolves the actual controller"), GotPC == PC);
    TestTrue(TEXT("Combined node resolves controller-owned PlayerState"), PlayerState && PlayerState == PC->PlayerState);
    TestTrue(TEXT("Combined node resolves possessed Pawn"), Pawn && Pawn == PC->GetPawn());
    TestTrue(TEXT("Single-object getter uses the same player"), UGameMainMapLibrary::GetGameMainMapPlayerState(PC) == PlayerState);
    TestNull(TEXT("Invalid player index does not select another player"), UGameMainMapLibrary::GetGameMainMapPlayerController(PC, -1));
    UGameMainMapLibrary::GetGameMainMapObjects(nullptr, Mode, GotState, GotPC, PlayerState, Pawn);
    TestTrue(TEXT("Invalid world clears every output"), !Mode && !GotState && !GotPC && !PlayerState && !Pawn);
    TestTrue(TEXT("No-world current map type is None"), UGameMainMapLibrary::GetCurrentMapType(nullptr) == EGameMainMapType::None);
    for (const TCHAR* Name : {TEXT("BeginCommandMapPointer"), TEXT("UpdateCommandMapPointer"), TEXT("ReleaseCommandMapPointer"), TEXT("CancelCommandMapPointer"), TEXT("QueryCommandMapPointer")})
        TestNull(FString(TEXT("Native controller has no sandbox node: ")) + Name, AGameMainMapPlayerController::StaticClass()->FindFunctionByName(Name));
    for (const TCHAR* Name : {TEXT("CommandMap_BeginPointer"),TEXT("CommandMap_UpdatePointer"),TEXT("CommandMap_ReleasePointer"),TEXT("CommandMap_CancelPointer"),TEXT("CommandMap_QueryPointer"),TEXT("CommandMap_FindSandbox")})
    {
        auto* Function = PC->FindFunction(Name);
        TestTrue(FString(Name) + TEXT(" is owned by the controller Blueprint"), Function && Function->GetOuterUClass() == PC->GetClass() && !Function->HasAnyFunctionFlags(FUNC_Native) && !Function->Script.IsEmpty());
    }
    UClass* Room = LoadClass<USMS_SceneBase>(nullptr,TEXT("/Game/System/Map/BaseMap/Scene/BP_作战指挥室_Scene.BP_作战指挥室_Scene_C"));
    TestTrue(TEXT("Room directly inherits the same scene base as other rooms"), Room && Room->GetSuperClass() == USMS_SceneBase::StaticClass());
    for (const TCHAR* Name : {TEXT("InteriorCameraState"),TEXT("ReturnCameraState"),TEXT("UiFinish"),TEXT("CameraFinish"),TEXT("FlowStage"),TEXT("PreviousMinPitch"),TEXT("PreviousMaxPitch"),TEXT("BaseUI"),TEXT("RoomUI")})
    {
        const FProperty* Property = Room ? FindFProperty<FProperty>(Room,Name) : nullptr;
        TestTrue(FString(Name) + TEXT(" is declared in the scene Blueprint"), Property && Property->GetOwnerClass() == Room);
    }
    return !HasAnyErrors();
}
#endif
