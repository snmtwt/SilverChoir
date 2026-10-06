#include "SubSystem/PlayerUnitSubSystem/UnitDisplayStand.h"
#include "SubSystem/PlayerUnitSubSystem/PlayerUnitLibrary.h"
#include "SubSystem/PlayerUnitSubSystem/PlayerUnitSubsystem.h"
#include "Object/Unit/UnitPawnBase.h"
#include "Components/SceneComponent.h"
#include "Components/DecalComponent.h"
#include "Components/HMS_CharacterMoverComponent.h"
#include "Components/HMS_AnimationDataComponent.h"
#include "Components/HMS_NavMoverComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/PlayerController.h"
#include "UObject/ConstructorHelpers.h"

AUnitDisplayStand::AUnitDisplayStand()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = false;
    PrimaryActorTick.bTickEvenWhenPaused = true;
    PrimaryActorTick.TickGroup = TG_PostUpdateWork;
    // Keep the old default-subobject name so existing scene blueprints retain their anchor edits.
    DisplayAnchor = CreateDefaultSubobject<USceneComponent>(TEXT("DisplayAnchor"));
    DisplayAnchor->SetupAttachment(RootComponent);
    PlatformMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlatformMesh"));
    PlatformMesh->SetupAttachment(DisplayAnchor);
    PlatformMesh->SetCollisionProfileName(TEXT("BlockAll"));
    PlatformMesh->SetCanEverAffectNavigation(false);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> PlatformAsset(
        TEXT("/Game/System/Object/Unit/Display/SM_UnitDisplayPlatform.SM_UnitDisplayPlatform"));
    if (PlatformAsset.Succeeded()) PlatformMesh->SetStaticMesh(PlatformAsset.Object);
}

void AUnitDisplayStand::BeginPlay()
{
    if (auto* System = UPlayerUnitLibrary::GetPlayerUnitSubsystem(this)) System->RegisterDisplayStand(this);
    Super::BeginPlay();
}

AUnitPawnBase* AUnitDisplayStand::DisplayUnit(FGuid UnitId)
{
    FText Error;
    return SpawnUnitById(UnitId, Error);
}

void AUnitDisplayStand::ClearDisplayedUnit() { EndMouseRotation(); ClearSpawnedUnit(); }
AUnitPawnBase* AUnitDisplayStand::GetDisplayedUnit() const { return GetSpawnedUnit(); }

FTransform AUnitDisplayStand::GetUnitSpawnTransform() const
{
    return IsValid(DisplayAnchor) ? DisplayAnchor->GetComponentTransform() : Super::GetUnitSpawnTransform();
}

void AUnitDisplayStand::ConfigureDeferredUnit(AUnitPawnBase* Unit)
{
    Super::ConfigureDeferredUnit(Unit);
    Unit->bCanBeSelected = false;
    Unit->AutoPossessPlayer = EAutoReceiveInput::Disabled;
    Unit->AutoPossessAI = EAutoPossessAI::Disabled;
    Unit->SetReplicates(false);
    Unit->SetActorEnableCollision(false);
    if (IsValid(Unit->AnimationDataComponent))
    {
        // A preview never simulates a floor hit; it must not inherit Mover's initial Falling mode.
        Unit->AnimationDataComponent->bAutoDetectMoverMovementMode = false;
        Unit->AnimationDataComponent->SetMovementMode(EHMS_MovementMode::OnGround);
    }
    if (IsValid(Unit->MoverComponent))
    {
        // Deferred actors may already have registered their native components.
        if (!Unit->MoverComponent->IsRegistered()) Unit->MoverComponent->SetAutoActivate(false);
        Unit->MoverComponent->Deactivate();
    }
}

void AUnitDisplayStand::ConfigureFinishedUnit(AUnitPawnBase* Unit)
{
    Super::ConfigureFinishedUnit(Unit);
    EndMouseRotation();
    Unit->StopUnitMovement();
    Unit->bCanBeSelected = false;
    // Construction scripts can override the deferred defaults; previews never show tactical selection.
    if (IsValid(Unit->SelectionDecal))
    {
        Unit->SelectionDecal->SetVisibility(false);
        Unit->SelectionDecal->SetHiddenInGame(true);
    }
    if (IsValid(Unit->MoverComponent))
    {
        Unit->MoverComponent->Deactivate();
        Unit->MoverComponent->SetComponentTickEnabled(false);
        // Network Prediction can drive Mover even when its component tick is disabled.
        Unit->MoverComponent->UnregisterComponent();
        if (Unit->MoverComponent->HasBeenInitialized()) Unit->MoverComponent->UninitializeComponent();
    }
    Unit->SetActorEnableCollision(false);
    if (IsValid(Unit->NavMoverComponent)) Unit->NavMoverComponent->Deactivate();
    if (IsValid(Unit->AnimationDataComponent))
    {
        Unit->AnimationDataComponent->bAutoDetectMoverMovementMode = false;
        Unit->AnimationDataComponent->SetMovementMode(EHMS_MovementMode::OnGround);
        Unit->AnimationDataComponent->SetWorldSpaceMovementInput(FVector::ZeroVector);
        Unit->AnimationDataComponent->SetFacingAndAimingDirections(Unit->GetActorForwardVector(), Unit->GetActorForwardVector());
    }
    // DisplayAnchor describes the stand's floor, whereas a Pawn's origin is its capsule center.
    // Compute this after Construction so Blueprint capsule dimensions and platform edits are honored.
    const FTransform AnchorTransform = GetUnitSpawnTransform();
    FVector Feet = AnchorTransform.GetLocation();
    if (IsValid(PlatformMesh) && PlatformMesh->GetStaticMesh())
    {
        const float Top = PlatformMesh->GetStaticMesh()->GetBoundingBox().Max.Z;
        Feet = PlatformMesh->GetComponentTransform().TransformPosition(FVector(0., 0., Top));
    }
    const float HalfHeight = IsValid(Unit->CollisionComponent) ? Unit->CollisionComponent->GetUnscaledCapsuleHalfHeight() : 0.f;
    const float HeightOffset = FMath::IsFinite(DisplayHeightOffset) ? DisplayHeightOffset : 0.f;
    Unit->SetActorLocation(Feet + Unit->GetActorTransform().TransformVector(FVector(0., 0., HalfHeight + HeightOffset)),
        false, nullptr, ETeleportType::TeleportPhysics);
    Unit->AttachToComponent(DisplayAnchor, FAttachmentTransformRules::KeepWorldTransform);
}

bool AUnitDisplayStand::BeginMouseRotation(APlayerController* PlayerController, bool bInvertHorizontalRotation)
{
    if (!IsValid(PlayerController) || !PlayerController->IsLocalController()
        || PlayerController->GetWorld() != GetWorld() || !GetDisplayedUnit() || IsActorBeingDestroyed()) return false;
    bInvertMouseHorizontalRotation = bInvertHorizontalRotation;
    if (bMouseRotating && RotationController == PlayerController) return true;
    RotationController = PlayerController;
    bHasMousePosition = PlayerController->GetMousePosition(PreviousMousePosition.X, PreviousMousePosition.Y);
    bMouseRotating = true;
    SetActorTickEnabled(true);
    return true;
}

void AUnitDisplayStand::EndMouseRotation()
{
    bMouseRotating = false;
    bInvertMouseHorizontalRotation = false;
    bHasMousePosition = false;
    RotationController.Reset();
    SetActorTickEnabled(false);
}

void AUnitDisplayStand::RotateDisplayedUnit(float MouseDeltaX)
{
    AUnitPawnBase* Unit = GetDisplayedUnit();
    const float Yaw = MouseDeltaX * MouseRotationSensitivity;
    if (!Unit || !FMath::IsFinite(Yaw) || FMath::IsNearlyZero(Yaw)) return;
    Unit->AddActorLocalRotation(FRotator(0., FMath::UnwindDegrees(Yaw), 0.), false, nullptr, ETeleportType::TeleportPhysics);
    if (IsValid(Unit->AnimationDataComponent))
        Unit->AnimationDataComponent->SetFacingAndAimingDirections(Unit->GetActorForwardVector(), Unit->GetActorForwardVector());
}

void AUnitDisplayStand::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    APlayerController* PC = RotationController.Get();
    if (!bMouseRotating || !IsValid(PC) || !GetDisplayedUnit()) { EndMouseRotation(); return; }
    float DeltaX = 0.f, DeltaY = 0.f;
    FVector2f Position = FVector2f::ZeroVector;
    const bool bPositionAvailable = PC->bShowMouseCursor && PC->GetMousePosition(Position.X, Position.Y);
    if (bPositionAvailable)
    {
        if (bHasMousePosition) DeltaX = Position.X - PreviousMousePosition.X;
        PreviousMousePosition = Position;
    }
    else PC->GetInputMouseDelta(DeltaX, DeltaY);
    bHasMousePosition = bPositionAvailable;
    RotateDisplayedUnit(bInvertMouseHorizontalRotation ? -DeltaX : DeltaX);
}

void AUnitDisplayStand::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    EndMouseRotation();
    if (auto* System = UPlayerUnitLibrary::GetPlayerUnitSubsystem(this)) System->UnregisterDisplayStand(this);
    Super::EndPlay(EndPlayReason);
}
