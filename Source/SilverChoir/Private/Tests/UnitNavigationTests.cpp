#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "NavigationSystem.h"
#include "NavigationData.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "Map/GameMainMap/GameMainMapPlayerController.h"
#include "Map/GameMainMap/GameMainMapGameState.h"
#include "Map/BattleMap/BattleUnitSelectionComponent.h"
#include "Object/Unit/UnitPawnBase.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/HMS_NavMoverComponent.h"
#include "HMS_AnimInstance.h"
#include "AIController.h"
#include "MTS_SubMapSubsystem.h"
#include "Engine/LevelStreamingDynamic.h"
#include "UObject/UnrealType.h"

namespace UnitNavigationTest
{
class FRun final : public IAutomationLatentCommand
{
public:
    explicit FRun(FAutomationTestBase* InTest,bool bInStreamed=false):Test(InTest),bStreamed(bInStreamed){}
    virtual ~FRun() override
    {
        for(auto U:Units)if(U.IsValid())U->Destroy();
        if(bStreamed && PC.IsValid()) PC->GetWorld()->GetSubsystem<UMTS_SubMapSubsystem>()->UnloadSubMapByID(TEXT("NavigationAutomation"));
    }
    bool Update() override
    {
        if(FPlatformTime::Seconds()-Started>90){Test->AddError(TEXT("Navigation/animation runtime timed out"));return true;}
        if(!PC.IsValid())
        {
            for(const auto& C:GEngine->GetWorldContexts())if(C.WorldType==EWorldType::Game)PC=Cast<AGameMainMapPlayerController>(C.World()->GetFirstPlayerController());
            if(!PC.IsValid())return false;
        }
        auto* World=PC->GetWorld();auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
        if(!Nav)return false;
        if(Stage==0)
        {
            ULevel* TargetLevel=World->PersistentLevel;
            if(bStreamed)
            {
                auto* Sub=World->GetSubsystem<UMTS_SubMapSubsystem>();
                if(!bLoadRequested)
                {
                    FMTS_SubMapInfo Previous;
                    if(Sub->GetSubMapByKey(TEXT("NavigationAutomation"),Previous))return false;
                    auto* Definition=NewObject<UMTS_SubMapDataAsset>(Sub);
                    Definition->MapID=TEXT("NavigationAutomation");
                    Definition->MapName=TEXT("Navigation Automation T5");
                    Definition->MapAsset=FSoftObjectPath(TEXT("/Game/System/Map/BattleMap/T5/L_T5.L_T5"));
                    FText Error;
                    const bool bAccepted=Sub->LoadSubMap(Definition,FVector(0,0,-20000),FRotator::ZeroRotator,Error);
                    if(!Test->TestTrue(FString(TEXT("Offset submap load accepted: "))+Error.ToString(),bAccepted))return true;
                    bLoadRequested=true;
                }
                FMTS_SubMapInfo Map;
                if(!Sub->GetSubMapByKey(TEXT("NavigationAutomation"),Map) || Map.State!=EMTS_SubMapState::Loaded || !Map.bIsVisible)return false;
                TargetLevel=Map.StreamingLevel->GetLoadedLevel();
            }
            ANavMeshBoundsVolume* Bounds=nullptr;for(TActorIterator<ANavMeshBoundsVolume> It(World);It;++It)if(It->GetLevel()==TargetLevel){Bounds=*It;break;}
            if(!Bounds)return false;
            FNavLocation Center;
            if(!Nav->ProjectPointToNavigation(Bounds->GetActorLocation(),Center,FVector(600,600,1000)))return false;
            if(bStreamed)
            {
                const auto* Data=Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate);
                Test->TestTrue(TEXT("Streamed navigation is owned by the persistent world"),Data && Data->GetLevel()==World->PersistentLevel);
            }
            Origin=Center.Location;
            auto* State=World->GetGameState<AGameMainMapGameState>();
            if(!Test->TestNotNull(TEXT("Main map GameState"),State))return true;
            State->SetCurrentMapType(EGameMainMapType::Battle);State->ActiveMapID=bStreamed?FName(TEXT("NavigationAutomation")):NAME_None;
            if(bStreamed) Test->TestTrue(TEXT("Navigation follows the submap Z offset"),FMath::Abs(Origin.Z+20000)<100);
            auto* Class=LoadClass<AUnitPawnBase>(nullptr,TEXT("/Game/System/Object/Unit/Character/BP_UnitPawn.BP_UnitPawn_C"));
            if(!Test->TestNotNull(TEXT("Migrated unit Blueprint"),Class))return true;
            for(int I=0;I<3;++I)
            {
                FNavLocation Feet;
                if(!Nav->ProjectPointToNavigation(Origin+FVector(-650,(I-1)*230,0),Feet,FVector(150,150,200)))return false;
                FActorSpawnParameters P;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;P.OverrideLevel=TargetLevel;
                auto* U=World->SpawnActor<AUnitPawnBase>(Class,Feet.Location+FVector(0,0,94),FRotator::ZeroRotator,P);
                if(!Test->TestNotNull(TEXT("Unit spawned"),U))return true;
                U->MeshComponent->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
                if(I==0)
                {
                    Clothing=U->AddWearableMesh(U->MeshComponent->GetSkeletalMeshAsset(),FGuid::NewGuid());
                    if(!Test->TestNotNull(TEXT("Animation-following clothing created"),Clothing.Get()))return true;
                    Clothing->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
                }
                Units.Add(U);
            }
            Stage=1;StageAt=FPlatformTime::Seconds();return false;
        }
        if(Stage==1)
        {
            if(FPlatformTime::Seconds()-StageAt<1.5)return false;
            for(auto U:Units)StartLocations.Add(U->GetActorLocation());
            auto* U=Units[0].Get();
            Test->TestNotNull(TEXT("Each unit has an AI controller"),Cast<AAIController>(U->GetController()));
            Test->TestNotNull(TEXT("HMS animation instance is assigned"),Cast<UHMS_AnimInstance>(U->MeshComponent->GetAnimInstance()));
            InitialPose=U->MeshComponent->GetComponentSpaceTransforms();
            PC->UnitSelection->SetSelectedUnits({Units[0].Get(),Units[1].Get()});
            FText Error;
            Test->TestEqual(TEXT("Two selected units accept a navigation order"),PC->MoveSelectedUnitsToLocation(Origin+FVector(650,0,0),Error),2);
            UE_LOG(LogTemp,Display,TEXT("UNIT_NAV_ORDER %s"),*Error.ToString());
            Stage=2;StageAt=FPlatformTime::Seconds();return Test->HasAnyErrors();
        }
        if(Stage==2)
        {
            if(auto* Anim=Cast<UHMS_AnimInstance>(Units[0]->MeshComponent->GetAnimInstance()))
                if(auto* Speed=FindFProperty<FFloatProperty>(Anim->GetClass(),TEXT("Speed2D")))
                    MaxAnimationSpeed=FMath::Max(MaxAnimationSpeed,Speed->GetPropertyValue_InContainer(Anim));
            const auto& Pose=Units[0]->MeshComponent->GetComponentSpaceTransforms();
            if(Pose.Num()==InitialPose.Num())for(int I=0;I<Pose.Num();++I)if(!Pose[I].Equals(InitialPose[I],.01f)){bPoseChanged=true;break;}
            const double Elapsed=FPlatformTime::Seconds()-StageAt;
            if(Elapsed<10)return false;
            for(int I=0;I<2;++I)
            {
                Test->TestTrue(TEXT("Selected unit physically moved"),FVector::Dist2D(Units[I]->GetActorLocation(),StartLocations[I])>500);
                Test->TestTrue(TEXT("Unit reaches the destination area"),FVector::Dist2D(Units[I]->GetActorLocation(),Origin+FVector(650,0,0))<180);
            }
            Test->TestTrue(TEXT("Unselected unit stays in place"),FVector::Dist2D(Units[2]->GetActorLocation(),StartLocations[2])<10);
            Test->TestTrue(TEXT("Animation evaluates changing bone poses while moving"),bPoseChanged);
            Test->TestTrue(TEXT("Animation receives locomotion speed, not only idle poses"),MaxAnimationSpeed>100);
            if(Clothing.IsValid())
            {
                bool bMatches=true;
                auto* Body=Units[0]->MeshComponent.Get();
                for(int Bone=0;Bone<Body->GetNumBones();++Bone)
                    bMatches &= Clothing->GetBoneTransform(Bone).Equals(Body->GetBoneTransform(Bone),.01f);
                Test->TestTrue(TEXT("Optimized clothing follows every animated body bone after moving"),bMatches);
                Test->TestNull(TEXT("Moving clothing never creates an independent AnimInstance"),Clothing->GetAnimInstance());
                Test->TestFalse(TEXT("Moving clothing never creates physics state"),Clothing->IsPhysicsStateCreated());
            }
            FText Error;
            Test->TestEqual(TEXT("Off-navmesh click is rejected"),PC->MoveSelectedUnitsToLocation(Origin+FVector(1000000,1000000,0),Error),0);
            Test->TestFalse(TEXT("Invalid destination reports a reason"),Error.IsEmpty());
            PC->UnitSelection->ClearUnitSelection();
            Test->TestEqual(TEXT("Empty selection sends no order"),PC->MoveSelectedUnitsToLocation(Origin,Error),0);
            UE_LOG(LogTemp,Display,TEXT("UNIT_NAV_RUNTIME_DONE streamed=%d poseChanged=%d animSpeed=%.1f origin=%s"),bStreamed,bPoseChanged,MaxAnimationSpeed,*Origin.ToString());return true;
        }
        return false;
    }
private:
    FAutomationTestBase* Test;TWeakObjectPtr<AGameMainMapPlayerController> PC;
    TArray<TWeakObjectPtr<AUnitPawnBase>> Units;TArray<FVector> StartLocations;TArray<FTransform> InitialPose;
    TWeakObjectPtr<USkeletalMeshComponent> Clothing;
    FVector Origin;int Stage=0;double Started=FPlatformTime::Seconds(),StageAt=0;bool bPoseChanged=false;
    bool bStreamed=false,bLoadRequested=false;float MaxAnimationSpeed=0;
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnitNavigationRuntimeTest,"SilverChoir.Units.Navigation.Runtime",EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FUnitNavigationRuntimeTest::RunTest(const FString&)
{ ADD_LATENT_AUTOMATION_COMMAND(UnitNavigationTest::FRun(this));return true; }
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnitNavigationStreamedTest,"SilverChoir.Units.Navigation.StreamedOffset",EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FUnitNavigationStreamedTest::RunTest(const FString&)
{ ADD_LATENT_AUTOMATION_COMMAND(UnitNavigationTest::FRun(this,true));return true; }
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnitNavigationReloadTest,"SilverChoir.Units.Navigation.StreamedReload",EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FUnitNavigationReloadTest::RunTest(const FString&)
{
    ADD_LATENT_AUTOMATION_COMMAND(UnitNavigationTest::FRun(this,true));
    ADD_LATENT_AUTOMATION_COMMAND(UnitNavigationTest::FRun(this,true));
    return true;
}
#endif
