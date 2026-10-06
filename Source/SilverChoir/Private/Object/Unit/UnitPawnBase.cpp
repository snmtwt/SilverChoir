#include "Object/Unit/UnitPawnBase.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/DecalComponent.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
#include "Components/HMS_CharacterMoverComponent.h"
#include "Components/SIS_UnitInventoryComponent.h"
#include "Structs/HMS_MoverStructs.h"
#include "SubSystem/PlayerUnitSubSystem/PlayerUnitLibrary.h"
#include "Components/HMS_NavMoverComponent.h"
#include "Components/HMS_AnimationDataComponent.h"
#include "Object/Unit/UnitAIController.h"
#include "NavigationSystem.h"
#include "Navigation/PathFollowingComponent.h"
#include "Engine/SkeletalMesh.h"

AUnitPawnBase::AUnitPawnBase()
{
    PrimaryActorTick.bCanEverTick=false;
    bReplicates=true;
    SetReplicatingMovement(false); // Mover owns movement replication.
    bCanAffectNavigationGeneration=false;
    CollisionComponent=CreateDefaultSubobject<UCapsuleComponent>(TEXT("Collision"));
    CollisionComponent->InitCapsuleSize(42.f,92.f);
    CollisionComponent->SetCollisionProfileName(TEXT("Pawn"));
    CollisionComponent->SetCanEverAffectNavigation(false);
    SetRootComponent(CollisionComponent);
    MeshComponent=CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Mesh"));
    MeshComponent->SetupAttachment(CollisionComponent);
    MeshComponent->SetRelativeLocation(FVector(0,0,-92));
    MeshComponent->SetRelativeRotation(FRotator(0,-90,0));
    MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    MoverComponent=CreateDefaultSubobject<UHMS_CharacterMoverComponent>(TEXT("Mover"));
    NavMoverComponent=CreateDefaultSubobject<UHMS_NavMoverComponent>(TEXT("NavMover"));
    AnimationDataComponent=CreateDefaultSubobject<UHMS_AnimationDataComponent>(TEXT("HMSAnimationData"));
    AnimationDataComponent->bUseControllerData=false;
    AIControllerClass=AUnitAIController::StaticClass();
    AutoPossessAI=EAutoPossessAI::PlacedInWorldOrSpawned;
    UnitInventoryComponent=CreateDefaultSubobject<USIS_UnitInventoryComponent>(TEXT("UnitInventoryComponent"));
    SelectionDecal=CreateDefaultSubobject<UDecalComponent>(TEXT("SelectionDecal"));
    SelectionDecal->SetupAttachment(CollisionComponent);
    SelectionDecal->SetRelativeLocation(FVector(0,0,-92));
    SelectionDecal->SetRelativeRotation(FRotator(-90,0,0));
    SelectionDecal->DecalSize=FVector(16,68,68);
    SelectionDecal->SetFadeScreenSize(0.f);
    SelectionDecal->SetVisibility(false);
    SelectionDecal->SetHiddenInGame(true);
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> SelectionMaterial(
        TEXT("/Game/System/Object/Unit/Materials/M_UnitSelectionDecal.M_UnitSelectionDecal"));
    if (SelectionMaterial.Succeeded()) SelectionDecal->SetDecalMaterial(SelectionMaterial.Object);
}
USkeletalMeshComponent* AUnitPawnBase::AddWearableMesh(USkeletalMesh* SkeletalMesh, FGuid Key)
{
    if (!Key.IsValid() || !IsValid(SkeletalMesh) || !IsValid(MeshComponent)
        || IsActorBeingDestroyed() || !GetWorld()) return nullptr;

    // Create the replacement first so an invalid request cannot strip an existing item.
    auto* Component = NewObject<USkeletalMeshComponent>(this, NAME_None, RF_Transient);
    Component->SetupAttachment(MeshComponent);
    Component->SetRelativeTransform(FTransform::Identity);
    Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Component->SetGenerateOverlapEvents(false);
    Component->SetCanEverAffectNavigation(false);
    // Configure before assigning the asset/registering: follow the body without an
    // independent AnimBP/rigid-body simulation, but retain mesh-authored cloth.
    Component->SetAnimationMode(EAnimationMode::AnimationCustomMode);
    Component->SetDisablePostProcessBlueprint(true);
    Component->SetAllowClothActors(true);
    Component->bDisableClothSimulation = false;
    Component->bEnablePhysicsOnDedicatedServer = false;
    Component->bUpdateOverlapsOnAnimationFinalize = false;
    Component->bUpdateJointsFromAnimation = false;
    Component->SetSimulatePhysics(false);
    Component->SetEnableGravity(false);
    Component->SetNotifyRigidBodyCollision(false);
    Component->SetIsReplicated(false);
    Component->SetSkeletalMesh(SkeletalMesh);
    Component->SetLeaderPoseComponent(MeshComponent, true, false);
    // Keep the engine's follower tick for cloth, LOD and morph/material curves.
    // Bounds remain clothing-specific so wide skirts/coats are not culled with the body.
    AddInstanceComponent(Component);
    Component->RegisterComponent();
    if (!Component->IsRegistered())
    {
        RemoveInstanceComponent(Component);
        Component->DestroyComponent();
        return nullptr;
    }

    RemoveWearableMesh(Key);
    WearableMeshes.Add(Key, Component);
    return Component;
}

bool AUnitPawnBase::RemoveWearableMesh(FGuid Key)
{
    TObjectPtr<USkeletalMeshComponent> Component;
    if (!WearableMeshes.RemoveAndCopyValue(Key, Component)) return false;
    if (IsValid(Component))
    {
        Component->SetLeaderPoseComponent(nullptr);
        RemoveInstanceComponent(Component);
        Component->DestroyComponent();
    }
    return true;
}

void AUnitPawnBase::PostInitializeComponents()
{
    Super::PostInitializeComponents();
    MoverComponent->SetUpdatedComponent(CollisionComponent);
    SetMovementGait(MovementGait);
    RefreshSelectionDecalVisibility();
    if (bPendingInitialDataNotification)
    {
        bPendingInitialDataNotification=false;
        TGuardValue<bool> Guard(bBindingUnitData,true);
        ApplySelectionState(UnitData ? UnitData->IsSelected() : bUnitSelected,true);
        if (!IsActorBeingDestroyed()) HandleUnitDataChanged(GetUnitId());
    }
}
void AUnitPawnBase::SetUnitMoveVelocity(FVector WorldVelocity)
{
    if (auto* AI=Cast<AAIController>(GetController())) AI->StopMovement();
    MoveVelocity=WorldVelocity.ContainsNaN()?FVector::ZeroVector:FVector(WorldVelocity.X,WorldVelocity.Y,0);
}
float AUnitPawnBase::GetMovementSpeed() const
{
    const float Speed=MovementGait==EHMS_Gait::Sprint?SprintSpeed:(MovementGait==EHMS_Gait::Walk?WalkSpeed:RunSpeed);
    return FMath::IsFinite(Speed)?FMath::Max(1.f,Speed):375.f;
}
void AUnitPawnBase::SetMovementGait(EHMS_Gait Gait)
{
    MovementGait=Gait;
    if (NavMoverComponent) NavMoverComponent->RequestedMaxSpeed=GetMovementSpeed();
    if (AnimationDataComponent) AnimationDataComponent->SetGait(Gait);
}
FVector AUnitPawnBase::GetNavAgentLocation() const
{
    return CollisionComponent?CollisionComponent->GetComponentLocation()-FVector(0,0,CollisionComponent->GetScaledCapsuleHalfHeight()):GetActorLocation();
}
void AUnitPawnBase::UpdateNavigationRelevance()
{
    if (CollisionComponent) CollisionComponent->SetCanEverAffectNavigation(false);
}
bool AUnitPawnBase::RequestMoveToLocation(FVector Destination,float AcceptanceRadius)
{
    if (!HasAuthority() || !bCanBeSelected || IsActorBeingDestroyed() || Destination.ContainsNaN()
        || !FMath::IsFinite(AcceptanceRadius) || !MoverComponent || !MoverComponent->IsRegistered() || !MoverComponent->IsActive()) return false;
    if (!GetController()) SpawnDefaultController();
    auto* AI=Cast<AAIController>(GetController());
    if (!AI) return false;
    MoveVelocity=FVector::ZeroVector;
    NavMoverComponent->RequestedMaxSpeed=GetMovementSpeed();
    // Reject partial paths: an unreachable order must never silently choose a nearby ledge.
    return AI->MoveToLocation(Destination,FMath::Max(1.f,AcceptanceRadius),false,true,false,true,nullptr,false)!=EPathFollowingRequestResult::Failed;
}
void AUnitPawnBase::StopUnitMovement()
{
    MoveVelocity=FVector::ZeroVector;
    if (auto* AI=Cast<AAIController>(GetController())) AI->StopMovement();
    if (NavMoverComponent) NavMoverComponent->RequestDirectMove(FVector::ZeroVector,false);
    if (AnimationDataComponent) AnimationDataComponent->SetWorldSpaceMovementInput(FVector::ZeroVector);
}
void AUnitPawnBase::ProduceInput_Implementation(int32 SimTimeMs,FMoverInputCmdContext& Result)
{
    FVector Velocity=MoveVelocity;
    if (Velocity.IsNearlyZero() && NavMoverComponent)
    {
        FVector Intent=FVector::ZeroVector,NavVelocity=FVector::ZeroVector;
        NavMoverComponent->RequestedMaxSpeed=GetMovementSpeed();
        if (NavMoverComponent->ConsumeNavMovementData(Intent,NavVelocity))
            Velocity=Intent.IsNearlyZero()?NavVelocity.GetClampedToMaxSize(GetMovementSpeed()):Intent.GetClampedToMaxSize(1.f)*GetMovementSpeed();
    }
    const FVector Direction=Velocity.GetSafeNormal2D();
    auto& Input=Result.InputCollection.FindOrAddMutableDataByType<FCharacterDefaultInputs>();
    Input.SetMoveInput(EMoveInputType::Velocity,Velocity);
    Input.OrientationIntent=Direction.IsNearlyZero()?GetActorForwardVector():Direction;
    Input.ControlRotation=Input.OrientationIntent.ToOrientationRotator();
    auto& HMS=Result.InputCollection.FindOrAddMutableDataByType<FHMS_MoverInput>();
    HMS=FHMS_MoverInput();
    HMS.bHasNavigationInput=!Direction.IsNearlyZero();
    HMS.WorldMoveDirection=Direction; HMS.RequestedVelocity=Velocity; HMS.RequestedSpeed=Velocity.Size2D();
    HMS.Gait=MovementGait;
    HMS.RotationMode=EHMS_RotationMode::OrientToMovement;
    HMS.bOrientToMovement=true;
    HMS.AimDirection=Input.OrientationIntent;
    HMS.bWantsToCrouch=MoverComponent->GetCrouchIntent();
    if (AnimationDataComponent)
    {
        AnimationDataComponent->SetWorldSpaceMovementInput(Direction);
        AnimationDataComponent->SetFacingAndAimingDirections(Input.OrientationIntent,Input.OrientationIntent);
        AnimationDataComponent->SetRotationMode(EHMS_RotationMode::OrientToMovement);
        AnimationDataComponent->SetGait(MovementGait);
    }
}
bool AUnitPawnBase::BindUnitData(FGuid Id)
{
    return BindUnitDataShared(UPlayerUnitLibrary::GetUnitDataShared(this,Id));
}
bool AUnitPawnBase::BindUnitDataShared(const TSharedPtr<FUnitData>& InData)
{
    if (bBindingUnitData || bNotifyingSelection || IsActorBeingDestroyed()) return false;
    TGuardValue<bool> Guard(bBindingUnitData,true);
    // Copy before Release: the caller may pass this Pawn's current shared pointer.
    const TSharedPtr<FUnitData> NewData=InData;
    ReleaseUnitData();
    UnitData=NewData;
    if (!UnitData)
    {
        if (!IsActorInitialized()) { bPendingInitialDataNotification=true; return false; }
        ApplySelectionState(false,true);
        if (!IsActorBeingDestroyed()) OnUnitDataUpdated(FGuid());
        return false;
    }
    DataChangedHandle=UnitData->OnDataChanged.AddUObject(this,&ThisClass::HandleUnitDataChanged);
    SelectedHandle=UnitData->OnSelected.AddUObject(this,&ThisClass::HandleUnitSelected);
    DeselectedHandle=UnitData->OnDeselected.AddUObject(this,&ThisClass::HandleUnitDeselected);
    if (!IsActorInitialized()) { bPendingInitialDataNotification=true; return true; }
    ApplySelectionState(UnitData->IsSelected(),true);
    if (!IsActorBeingDestroyed()) HandleUnitDataChanged(UnitData->UnitId);
    return UnitData.IsValid() && !IsActorBeingDestroyed();
}
FGuid AUnitPawnBase::GetUnitId() const { return UnitData?UnitData->UnitId:FGuid(); }
void AUnitPawnBase::HandleUnitDataChanged(FGuid Id)
{
    if (!IsActorInitialized()) { bPendingInitialDataNotification=true; return; }
    OnUnitDataUpdated(Id);
}
bool AUnitPawnBase::SetUnitSelected(bool bSelected)
{
    if (bNotifyingSelection || IsActorBeingDestroyed() || (bSelected && !bCanBeSelected)) return false;
    // Blueprint callbacks may destroy/rebind consumers while broadcasting.
    const TSharedPtr<FUnitData> CurrentData=UnitData;
    if (CurrentData) return CurrentData->SetSelected(bSelected);
    if (bUnitSelected==bSelected) return false;
    ApplySelectionState(bSelected);
    return true;
}
bool AUnitPawnBase::IsUnitSelected() const { return UnitData?UnitData->IsSelected():bUnitSelected; }
void AUnitPawnBase::HandleUnitSelected(FGuid Id)
{
    if (UnitData && UnitData->UnitId==Id) ApplySelectionState(true);
}
void AUnitPawnBase::HandleUnitDeselected(FGuid Id)
{
    if (UnitData && UnitData->UnitId==Id) ApplySelectionState(false);
}
void AUnitPawnBase::ApplySelectionState(bool bSelected,bool bForceNotify)
{
    if (bNotifyingSelection || IsActorBeingDestroyed() || (!bForceNotify && bUnitSelected==bSelected)) return;
    if (!IsActorInitialized()) { bUnitSelected=bSelected; bPendingInitialDataNotification=true; return; }
    TGuardValue<bool> Guard(bNotifyingSelection,true);
    bUnitSelected=bSelected;
    RefreshSelectionDecalVisibility();
    if (bSelected) OnUnitSelected();
    else OnUnitDeselected();
}
void AUnitPawnBase::RefreshSelectionDecalVisibility()
{
    if (!IsValid(SelectionDecal)) return;
    // Display stands share unit data but must not project battle selection onto their room.
    const bool bShow=bUnitSelected && bCanBeSelected && !IsActorBeingDestroyed();
    SelectionDecal->SetHiddenInGame(!bShow);
    SelectionDecal->SetVisibility(bShow);
}
void AUnitPawnBase::ReleaseUnitData()
{
    if (UnitData)
    {
        UnitData->OnDataChanged.Remove(DataChangedHandle);
        UnitData->OnSelected.Remove(SelectedHandle);
        UnitData->OnDeselected.Remove(DeselectedHandle);
    }
    DataChangedHandle.Reset(); SelectedHandle.Reset(); DeselectedHandle.Reset();
    UnitData.Reset(); bUnitSelected=false; bPendingInitialDataNotification=false;
    RefreshSelectionDecalVisibility();
}
void AUnitPawnBase::EndPlay(const EEndPlayReason::Type Reason)
{
    MoveVelocity=FVector::ZeroVector; ReleaseUnitData(); Super::EndPlay(Reason);
}
void AUnitPawnBase::BeginDestroy() { ReleaseUnitData(); Super::BeginDestroy(); }
