#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Object/Unit/UnitPawnBase.h"
#include "SubSystem/PlayerUnitSubSystem/UnitSpawner.h"
#include "SubSystem/PlayerUnitSubSystem/UnitSquadSpawner.h"
#include "SubSystem/PlayerUnitSubSystem/UnitDisplayStand.h"
#include "SubSystem/PlayerUnitSubSystem/PlayerUnitLibrary.h"
#include "SubSystem/PlayerUnitSubSystem/PlayerUnitManagerBase.h"
#include "SubSystem/PlayerSquadSubSystem/PlayerSquadLibrary.h"
#include "SubSystem/PlayerSquadSubSystem/PlayerSquadManagerBase.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/HMS_CharacterMoverComponent.h"
#include "Components/HMS_AnimationDataComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HMS_AnimInstance.h"
#include "UObject/UnrealType.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnitSpawnerFormationTest,
    "SilverChoir.Player.UnitSpawner.Formation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FUnitSpawnerFormationTest::RunTest(const FString& Parameters)
{
    const TArray<FVector> Positions = AUnitSquadSpawner::BuildFormationOffsets(4, 3, FVector2D(200., 150.));
    if (!TestEqual(TEXT("One position per member"), Positions.Num(), 4)) return false;
    FVector Center = FVector::ZeroVector;
    for (const FVector& Position : Positions) Center += Position;
    TestTrue(TEXT("Partial final row keeps the entire formation centered"), Center.IsNearlyZero());
    TestTrue(TEXT("Column spacing is preserved"), (Positions[1] - Positions[0]).Equals(FVector(0., 150., 0.)));
    TestEqual(TEXT("Row spacing is preserved"), Positions[0].X - Positions[3].X, 200.);
    TestEqual(TEXT("Single member on final row is horizontally centered"), Positions[3].Y, 0.);
    TestTrue(TEXT("Zero columns rejected"), AUnitSquadSpawner::BuildFormationOffsets(4, 0, FVector2D(100., 100.)).IsEmpty());
    TestTrue(TEXT("Negative spacing rejected"), AUnitSquadSpawner::BuildFormationOffsets(4, 2, FVector2D(-1., 100.)).IsEmpty());
    TestTrue(TEXT("Empty squad has no positions"), AUnitSquadSpawner::BuildFormationOffsets(0, 3, FVector2D(100., 100.)).IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnitSpawnerRuntimeTest,
    "SilverChoir.Player.UnitSpawner.Runtime", EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FUnitSpawnerRuntimeTest::RunTest(const FString& Parameters)
{
    UWorld* World = nullptr;
    for (const auto& Context : GEngine->GetWorldContexts())
        if (Context.WorldType == EWorldType::Game) { World = Context.World(); break; }
    if (!TestNotNull(TEXT("Game world"), World)) return false;
    auto* UnitManager = UPlayerUnitLibrary::GetPlayerUnitManager(World);
    auto* SquadManager = UPlayerSquadLibrary::GetPlayerSquadManager(World);
    if (!TestNotNull(TEXT("Unit manager"), UnitManager) || !TestNotNull(TEXT("Squad manager"), SquadManager)) return false;
    UClass* DisplayStandClass = LoadClass<AUnitDisplayStand>(nullptr,
        TEXT("/Game/System/SubSystem/PlayerUnitSubSystem/BP_UnitDisplayStand.BP_UnitDisplayStand_C"));
    if (!TestNotNull(TEXT("Existing display stand Blueprint loads after reparenting native base"), DisplayStandClass)) return false;

    FText Error;
    const FName Tile(*FString::Printf(TEXT("SpawnerTest_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits)));
    FUnitTemplate Template;
    Template.EntityData.UnitPawnClass = AUnitPawnBase::StaticClass();
    TArray<FUnitData> Records = UPlayerUnitLibrary::CreateUnitDataBatch(Template, 3);
    for (FUnitData& Record : Records) Record.RuntimeData.TileId = Tile;
    if (!TestTrue(TEXT("Load fixture records"), UnitManager->LoadUnitData(Records, Error))) return false;
    FGuid SquadId;
    if (!TestTrue(TEXT("Create fixture squad"), SquadManager->CreateSquad(FText::FromString(TEXT("Spawner test")), nullptr, Tile, SquadId, Error))) return false;
    for (const FUnitData& Record : Records)
        TestTrue(TEXT("Add fixture member"), SquadManager->AddUnitToSquad(Record.UnitId, SquadId, Error));

    const TSharedPtr<FUnitData> Shared = UnitManager->GetUnitDataShared(Records[0].UnitId);
    AUnitSpawner* Spawner = World->SpawnActor<AUnitSpawner>();
    AUnitDisplayStand* Stand = World->SpawnActor<AUnitDisplayStand>(DisplayStandClass);
    AUnitSquadSpawner* SquadSpawner = World->SpawnActor<AUnitSquadSpawner>();
    if (!Spawner || !Stand || !SquadSpawner)
    {
        if (Spawner) Spawner->Destroy();
        if (Stand) Stand->Destroy();
        if (SquadSpawner) SquadSpawner->Destroy();
        SquadManager->RemoveSquad(SquadId, Error);
        AddError(TEXT("Failed to create test spawners"));
        return false;
    }

    // The shared record is available before FinishSpawning dispatches any Blueprint BeginPlay.
    const FTransform ProbeTransform(FVector(2000., 0., 200.));
    AUnitPawnBase* Deferred = World->SpawnActorDeferred<AUnitPawnBase>(AUnitPawnBase::StaticClass(), ProbeTransform,
        Spawner, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (TestNotNull(TEXT("Deferred unit"), Deferred))
    {
        TestFalse(TEXT("Deferred unit has not begun play"), Deferred->HasActorBegunPlay());
        TestTrue(TEXT("Deferred shared binding succeeds"), Deferred->BindUnitDataShared(Shared));
        TestTrue(TEXT("Canonical data exists before BeginPlay"), Deferred->GetUnitDataShared() == Shared);
        Deferred->FinishSpawning(ProbeTransform);
        TestTrue(TEXT("Finished unit begins play with same data"), Deferred->HasActorBegunPlay() && Deferred->GetUnitDataShared() == Shared);
        Deferred->Destroy();
    }

    AUnitPawnBase* Unit = Spawner->SpawnUnitShared(Shared, Error);
    if (TestNotNull(TEXT("Shared unit spawned"), Unit))
    {
        TestTrue(TEXT("No copy of unit record"), Unit->GetUnitDataShared() == Shared);
        TestTrue(TEXT("Battle unit is selectable"), Unit->bCanBeSelected);
        TestTrue(TEXT("Unit remains in spawner level"), Unit->GetLevel() == Spawner->GetLevel());
        TestNull(TEXT("Invalid data does not create a unit"), Spawner->SpawnUnitShared(nullptr, Error));
        TestTrue(TEXT("Invalid request preserves previous unit"), Spawner->GetSpawnedUnit() == Unit);
        AUnitPawnBase* Replacement = Spawner->SpawnUnitById(Records[1].UnitId, Error);
        TestNotNull(TEXT("Replacement spawned"), Replacement);
        TestTrue(TEXT("Replacement destroys old unit"), Unit->IsActorBeingDestroyed());
        Spawner->ClearSpawnedUnit();
        TestNull(TEXT("Clear removes tracked unit"), Spawner->GetSpawnedUnit());
        if (Replacement) TestTrue(TEXT("Clear destroys replacement"), Replacement->IsActorBeingDestroyed());
    }
    AUnitPawnBase* Preview = Stand->DisplayUnit(Shared->UnitId);
    if (TestNotNull(TEXT("Existing display API still works"), Preview))
    {
        TestTrue(TEXT("Preview uses shared data"), Preview->GetUnitDataShared() == Shared);
        TestTrue(TEXT("Preview attaches to existing display stand Blueprint"), Preview->GetAttachParentActor() == Stand);
        TestFalse(TEXT("Preview collision stays disabled"), Preview->GetActorEnableCollision());
        TestFalse(TEXT("Display preview cannot be box selected"), Preview->bCanBeSelected);
        TestNull(TEXT("Bad display ID fails"), Stand->DisplayUnit(FGuid::NewGuid()));
        TestTrue(TEXT("Bad display ID preserves existing preview"), Stand->GetDisplayedUnit() == Preview);
        Shared->SetSelected(true);
        TestTrue(TEXT("Shared state is visible to preview"), Preview->IsUnitSelected());
        Shared->SetSelected(false);
    }

    SquadSpawner->SetActorTransform(FTransform(FRotator(0., 90., 0.), FVector(4000., 5000., -20000.), FVector(2.)));
    SquadSpawner->Spacing = FVector2D(200., 150.);
    const TArray<FTransform> Locations = SquadSpawner->GetFormationTransforms(3);
    if (TestEqual(TEXT("Three formation transforms"), Locations.Num(), 3))
    {
        TestTrue(TEXT("Rotation and loaded-map height are included"), Locations[0].GetLocation().Equals(FVector(4150., 5000., -19908.)));
        TestTrue(TEXT("Actor scale does not multiply configured spacing"), (Locations[1].GetLocation() - Locations[0].GetLocation()).Equals(FVector(-150., 0., 0.)));
    }
    if (TestTrue(TEXT("Spawn whole squad"), SquadSpawner->SpawnSquad(SquadId, Error)))
    {
        const TArray<AUnitPawnBase*> Previous = SquadSpawner->GetSpawnedUnits();
        const TArray<AUnitSpawner*> PreviousSpawners = SquadSpawner->GetUnitSpawners();
        TestEqual(TEXT("One unit generator per member"), PreviousSpawners.Num(), 3);
        TestEqual(TEXT("One pawn per member"), Previous.Num(), 3);
        for (int32 Index = 0; Index < Previous.Num(); ++Index)
        {
            TestTrue(TEXT("Squad preserves member order and authority"), Previous[Index]->GetUnitDataShared() == UnitManager->GetUnitDataShared(Records[Index].UnitId));
            TestTrue(TEXT("Child uses owner level"), Previous[Index]->GetLevel() == SquadSpawner->GetLevel());
        }
        TestFalse(TEXT("Unknown squad rejected"), SquadSpawner->SpawnSquad(FGuid::NewGuid(), Error));
        TestTrue(TEXT("Unknown squad keeps current team"), SquadSpawner->GetSpawnedUnits() == Previous);

        int32 CreatedUnits = 0;
        TArray<TWeakObjectPtr<AActor>> Attempts;
        const FDelegateHandle FailHandle = World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateLambda([&](AActor* Actor)
        {
            if (Actor->IsA<AUnitSpawner>() || Actor->IsA<AUnitPawnBase>()) Attempts.Add(Actor);
            if (Actor->IsA<AUnitPawnBase>() && ++CreatedUnits == 2) Actor->Destroy();
        }));
        TestFalse(TEXT("Member creation failure rejects whole new squad"), SquadSpawner->SpawnSquad(SquadId, Error));
        World->RemoveOnActorSpawnedHandler(FailHandle);
        TestEqual(TEXT("Failure injected after one successful member"), CreatedUnits, 2);
        TestTrue(TEXT("Partial failure keeps previous squad"), SquadSpawner->GetSpawnedUnits() == Previous);
        for (const TWeakObjectPtr<AActor>& Attempt : Attempts)
            TestTrue(TEXT("Failed attempt leaves no live pawn or child generator"), !Attempt.IsValid() || Attempt->IsActorBeingDestroyed());

        TestTrue(TEXT("Rebuilding squad succeeds"), SquadSpawner->SpawnSquad(SquadId, Error));
        for (AUnitPawnBase* Old : Previous) TestTrue(TEXT("Rebuild cleans old units"), Old->IsActorBeingDestroyed());
        for (AUnitSpawner* Old : PreviousSpawners) TestTrue(TEXT("Rebuild cleans old child generators"), Old->IsActorBeingDestroyed());
        const TArray<AUnitPawnBase*> NewUnits = SquadSpawner->GetSpawnedUnits();
        const TArray<AUnitSpawner*> NewSpawners = SquadSpawner->GetUnitSpawners();
        SquadSpawner->Destroy();
        for (AUnitPawnBase* Old : NewUnits) TestTrue(TEXT("Owner destruction cleans squad units"), Old->IsActorBeingDestroyed());
        for (AUnitSpawner* Old : NewSpawners) TestTrue(TEXT("Owner destruction cleans child generators"), Old->IsActorBeingDestroyed());
    }
    if (IsValid(SquadSpawner) && !SquadSpawner->IsActorBeingDestroyed()) SquadSpawner->Destroy();
    Stand->Destroy();
    Spawner->Destroy();
    SquadManager->RemoveSquad(SquadId, Error);
    AddInfo(TEXT("UNIT_SPAWNER_OK shared binding, display compatibility, rotated placement, rollback and cleanup"));
    return true;
}
namespace DisplayStandTest
{
class FRun final : public IAutomationLatentCommand
{
public:
    explicit FRun(FAutomationTestBase* InTest) : Test(InTest) {}
    ~FRun() override
    {
        if (Camera.IsValid()) Camera->Destroy();
        if (Stand.IsValid())
        {
            Stand->ClearDisplayedUnit();
            Stand->SetActorTransform(OriginalTransform);
        }
    }
    bool Update() override
    {
        if (!Stand.IsValid())
        {
            UWorld* World = nullptr;
            for (const auto& Context : GEngine->GetWorldContexts())
                if (Context.WorldType == EWorldType::Game) { World = Context.World(); break; }
            if (!Test->TestNotNull(TEXT("Game world"), World)) return true;
            for (TActorIterator<AUnitDisplayStand> It(World); It; ++It) { Stand = *It; break; }
            if (!Test->TestTrue(TEXT("Actual display stand placed in L_BaseTemplate"), Stand.IsValid())) return true;
            OriginalTransform = Stand->GetActorTransform();
            auto* PawnClass = LoadClass<AUnitPawnBase>(nullptr,
                TEXT("/Game/System/Object/Unit/Character/BP_UnitPawn.BP_UnitPawn_C"));
            if (!Test->TestNotNull(TEXT("Real unit Blueprint"), PawnClass)) return true;
            auto Data = MakeShared<FUnitData>();
            Data->UnitId = FGuid::NewGuid();
            Data->EntityData.UnitPawnClass = PawnClass;
            FText Error;
            Unit = Stand->SpawnUnitShared(Data, Error);
            if (!Test->TestTrue(FString(TEXT("Spawn display unit: ")) + Error.ToString(), Unit.IsValid())) return true;
            Unit->MeshComponent->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
            Test->TestNotNull(TEXT("Circular platform asset bound"), Stand->PlatformMesh->GetStaticMesh().Get());
            if (!Stand->PlatformMesh->GetStaticMesh()) return true;
            const FVector Top = Stand->PlatformMesh->GetComponentTransform().TransformPosition(
                FVector(0, 0, Stand->PlatformMesh->GetStaticMesh()->GetBoundingBox().Max.Z));
            Test->TestTrue(TEXT("Capsule feet align with platform top"), Unit->GetNavAgentLocation().Equals(Top, .1));
            Test->TestFalse(TEXT("Display has no Mover simulation"), Unit->MoverComponent->IsRegistered());
            Test->TestNull(TEXT("Display does not spawn AI controller"), Unit->GetController());
            const FTransform PlatformBefore = Stand->PlatformMesh->GetComponentTransform();
            const FVector Before = Unit->GetActorLocation();
            const float YawBefore = Unit->GetActorRotation().Yaw;
            Stand->MouseRotationSensitivity = .35f;
            Stand->RotateDisplayedUnit(100.f);
            Test->TestTrue(TEXT("Mouse delta rotates by configured sensitivity"),
                FMath::IsNearlyEqual(FMath::FindDeltaAngleDegrees(YawBefore, Unit->GetActorRotation().Yaw), 35.f, .01f));
            Test->TestTrue(TEXT("Rotation does not translate the unit"), Unit->GetActorLocation().Equals(Before));
            Test->TestTrue(TEXT("Rotation leaves the platform stationary"), Stand->PlatformMesh->GetComponentTransform().Equals(PlatformBefore));
            APlayerController* PC = World->GetFirstPlayerController();
            Test->TestTrue(TEXT("Begin mouse rotation"), Stand->BeginMouseRotation(PC));
            Test->TestTrue(TEXT("Repeated begin is safe"), Stand->BeginMouseRotation(PC));
            Test->TestTrue(TEXT("Rotation state and tick enabled"), Stand->IsMouseRotating() && Stand->IsActorTickEnabled());
            Stand->EndMouseRotation();
            Test->TestFalse(TEXT("End disables rotation tick"), Stand->IsMouseRotating() || Stand->IsActorTickEnabled());
            Test->TestFalse(TEXT("Null controller rejected"), Stand->BeginMouseRotation(nullptr));
            Stand->AddActorWorldOffset(FVector(0,0,-20000));
            Test->TestTrue(TEXT("Preview follows a shifted base map"), Unit->GetActorLocation().Equals(Before + FVector(0,0,-20000), .1));
            ExpectedLocation = Unit->GetActorLocation();
            StartTime = World->GetTimeSeconds();
            return false;
        }
        if (!Test->TestTrue(TEXT("Preview remains alive"), Unit.IsValid())) return true;
        if (bCapturing)
        {
            const float Elapsed = Unit->GetWorld()->GetTimeSeconds() - StartTime;
            if (Elapsed < 2.f) return false;
            if (!bRequestedScreenshot)
            {
                FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("DisplayStandUpgrade/Preview.png"), false, false);
                bRequestedScreenshot = true;
                return false;
            }
            return Elapsed > 3.f;
        }
        if (Unit->GetWorld()->GetTimeSeconds() - StartTime < 3.f) return false;
        Test->TestTrue(TEXT("Display never falls or drifts over three simulated seconds"), Unit->GetActorLocation().Equals(ExpectedLocation, .1));
        FHMS_AnimationProperties Properties;
        Unit->AnimationDataComponent->GetAnimationProperties(Properties);
        Test->TestTrue(TEXT("Animation data stays grounded"), Properties.MovementMode == EHMS_MovementMode::OnGround);
        auto* Anim = Cast<UHMS_AnimInstance>(Unit->MeshComponent->GetAnimInstance());
        if (Test->TestNotNull(TEXT("Migrated animation blueprint runs"), Anim))
        {
            const auto* Mode = FindFProperty<FEnumProperty>(Anim->GetClass(), TEXT("MovementMode"));
            if (Test->TestNotNull(TEXT("Animation movement mode property"), Mode))
                Test->TestTrue(TEXT("Animation instance plays ground state, not falling"),
                    Mode->GetUnderlyingProperty()->GetSignedIntPropertyValue(Mode->ContainerPtrToValuePtr<void>(Anim)) == static_cast<int64>(EHMS_MovementMode::OnGround));
            const auto* Velocity = FindFProperty<FStructProperty>(Anim->GetClass(), TEXT("Velocity"));
            if (Test->TestNotNull(TEXT("Animation velocity property"), Velocity))
                Test->TestTrue(TEXT("Idle animation velocity stays zero"), Velocity->ContainerPtrToValuePtr<FVector>(Anim)->IsNearlyZero());
        }
        if (FParse::Param(FCommandLine::Get(), TEXT("DisplayStandCapture")))
        {
            Stand->SetActorTransform(OriginalTransform);
            Unit->SetActorRotation(FRotator::ZeroRotator);
            const FVector Target = Unit->GetActorLocation() + FVector(0,0,-25);
            const FVector ViewLocation = Target + FVector(340,-160,120);
            Camera = Unit->GetWorld()->SpawnActor<ACameraActor>(ViewLocation, (Target - ViewLocation).Rotation());
            Camera->GetCameraComponent()->SetFieldOfView(42.f);
            Unit->GetWorld()->GetFirstPlayerController()->SetViewTarget(Camera.Get());
            StartTime = Unit->GetWorld()->GetTimeSeconds();
            bCapturing = true;
            return false;
        }
        Stand->BeginMouseRotation(Unit->GetWorld()->GetFirstPlayerController());
        Stand->ClearDisplayedUnit();
        Test->TestNull(TEXT("Clear destroys preview"), Stand->GetDisplayedUnit());
        Test->TestFalse(TEXT("Clear stops active rotation"), Stand->IsMouseRotating() || Stand->IsActorTickEnabled());
        Test->AddInfo(TEXT("DISPLAY_STAND_OK real base-map Blueprint, feet placement, grounded animation, rotation lifecycle, map offset"));
        return true;
    }
private:
    FAutomationTestBase* Test;
    TWeakObjectPtr<AUnitDisplayStand> Stand;
    TWeakObjectPtr<AUnitPawnBase> Unit;
    TWeakObjectPtr<ACameraActor> Camera;
    FTransform OriginalTransform;
    FVector ExpectedLocation;
    float StartTime = 0.f;
    bool bCapturing = false;
    bool bRequestedScreenshot = false;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnitDisplayStandPreviewTest,
    "SilverChoir.Player.UnitSpawner.DisplayStand", EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FUnitDisplayStandPreviewTest::RunTest(const FString& Parameters)
{
    ADD_LATENT_AUTOMATION_COMMAND(DisplayStandTest::FRun(this));
    return true;
}
#endif
