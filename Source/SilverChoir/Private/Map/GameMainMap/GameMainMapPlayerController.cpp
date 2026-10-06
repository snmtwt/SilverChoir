#include "Map/GameMainMap/GameMainMapPlayerController.h"
#include "Map/GameMainMap/GameMainMapGameMode.h"
#include "GameFramework/Pawn.h"
#include "FCS_FreeCameraPawn.h"
#include "UIBasic/MapWidgetBase.h"
#include "Map/BaseMap/BaseMapWidget.h"

#include "Map/BaseMap/RegionInteraction.h"
#include "Map/BattleMap/BattleMapWidget.h"
#include "Map/BattleMap/BattleUnitSelectionComponent.h"
#include "Components/SIS_PlayerInventoryManager.h"

#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "Engine/LocalPlayer.h"
#include "Object/Unit/UnitPawnBase.h"
#include "NavigationSystem.h"
#include "NavigationData.h"
#include "Engine/World.h"
#include "MTS_SubMapSubsystem.h"
#include "Engine/LevelStreamingDynamic.h"

AGameMainMapPlayerController::AGameMainMapPlayerController()
{
	PlayerInventoryManager=CreateDefaultSubobject<USIS_PlayerInventoryManager>(TEXT("PlayerInventoryManager"));
	UnitSelection=CreateDefaultSubobject<UBattleUnitSelectionComponent>(TEXT("UnitSelection"));
}

int32 AGameMainMapPlayerController::MoveSelectedUnitsToCursor(FText& OutError)
{
	OutError=FText::GetEmpty();
	if (!UnitSelection || !UnitSelection->CanIssueWorldCommand())
	{ OutError=FText::FromString(TEXT("当前状态或鼠标所在界面不接受移动命令。")); return 0; }
	FHitResult Hit;
	if (!GetHitResultUnderCursor(ECC_Visibility,false,Hit) || !Hit.bBlockingHit
		|| Hit.ImpactNormal.Z<.4 || Cast<AUnitPawnBase>(Hit.GetActor()))
	{ OutError=FText::FromString(TEXT("鼠标未指向可通行地面。")); return 0; }
	const auto* State=GetWorld()->GetGameState<AGameMainMapGameState>();
	const auto* Sub=GetWorld()->GetSubsystem<UMTS_SubMapSubsystem>();
	if (Hit.GetActor() && Hit.GetActor()->GetLevel()!=GetWorld()->PersistentLevel && State && Sub && !State->ActiveMapID.IsNone())
	{
		FMTS_SubMapInfo Map;
		if (!Sub->GetSubMapByKey(State->ActiveMapID,Map) || !Map.StreamingLevel || Map.StreamingLevel->GetLoadedLevel()!=Hit.GetActor()->GetLevel())
		{ OutError=FText::FromString(TEXT("目标不在当前战斗子地图。")); return 0; }
	}
	return MoveSelectedUnitsToLocation(Hit.ImpactPoint,OutError);
}

int32 AGameMainMapPlayerController::MoveSelectedUnitsToLocation(FVector Destination,FText& OutError)
{
	OutError=FText::GetEmpty();
	if (!HasAuthority() || !UnitSelection || !UnitSelection->CanIssueWorldCommand(false) || Destination.ContainsNaN())
	{ OutError=FText::FromString(TEXT("当前状态不能下达移动命令。")); return 0; }
	TArray<AUnitPawnBase*> Units=UnitSelection->GetSelectedUnits();
	if (Units.IsEmpty()) { OutError=FText::FromString(TEXT("请先选择单位。")); return 0; }
	auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!Nav)
	{ OutError=FText::FromString(TEXT("当前主地图未启用导航系统。")); return 0; }
	if (!IsValid(Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate)))
	{ OutError=FText::FromString(TEXT("当前世界的导航数据尚未就绪，请等待子地图导航初始化。")); return 0; }
	FNavLocation Center;
	if (!Nav->ProjectPointToNavigation(Destination,Center,FVector(120,120,180)))
	{
		OutError=FText::FromString(Nav->IsNavigationBuildInProgress()
			? TEXT("目标区域的导航网格正在生成，请稍后重试。")
			: TEXT("目标位置不在可通行导航范围内，请选择导航覆盖的地面。"));
		return 0;
	}
	Units.Sort([](const AUnitPawnBase& A,const AUnitPawnBase& B){return A.GetPathName()<B.GetPathName();});
	const int32 Columns=FMath::CeilToInt(FMath::Sqrt(static_cast<float>(Units.Num())));
	const int32 Rows=FMath::DivideAndRoundUp(Units.Num(),Columns);
	const float Spacing=FMath::IsFinite(MoveOrderSpacing)?FMath::Max(100.f,MoveOrderSpacing):140.f;
	int32 Accepted=0;
	for (int32 Index=0;Index<Units.Num();++Index)
	{
		const int32 Row=Index/Columns, RowSize=FMath::Min(Columns,Units.Num()-Row*Columns);
		const FVector Offset((Index%Columns-(RowSize-1)*.5f)*Spacing,(Row-(Rows-1)*.5f)*Spacing,0);
		FNavLocation Goal;
		if (Nav->ProjectPointToNavigation(Center.Location+Offset,Goal,FVector(Spacing*.45f,Spacing*.45f,120))
			&& Units[Index]->RequestMoveToLocation(Goal.Location)) ++Accepted;
	}
	if (Accepted<Units.Num()) OutError=FText::FromString(FString::Printf(TEXT("%d/%d 个单位已接受移动命令，其余单位没有完整可达路径。"),Accepted,Units.Num()));
	return Accepted;
}

void AGameMainMapPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (!IsLocalController()) { return; }
	FInputModeGameAndUI InputMode;
	// FreeCamera's visible-cursor mode reads screen positions. Implicit cursor
	// hiding enables relative mouse capture and freezes those positions.
	InputMode.SetHideCursorDuringCapture(false);
	SetInputMode(InputMode);
	bShowMouseCursor = true;
	bEnableMouseOverEvents = true;
	bEnableClickEvents = true;
	GetWorld()->GameStateSetEvent.AddUObject(this, &ThisClass::BindMapTypeState);
	BindMapTypeState(GetWorld()->GetGameState());
}

void AGameMainMapPlayerController::BindMapTypeState(AGameStateBase* State)
{
	if (InputMapState.IsValid())
	{
		InputMapState->OnCurrentMapTypeChanged.RemoveDynamic(this, &ThisClass::HandleMapTypeChanged);
		InputMapState->OnActiveMapChanged.RemoveDynamic(this, &ThisClass::HandleSelectionMapChanged);
	}
	InputMapState = Cast<AGameMainMapGameState>(State);
	if (InputMapState.IsValid()) InputMapState->OnCurrentMapTypeChanged.AddUniqueDynamic(this, &ThisClass::HandleMapTypeChanged);
	if (InputMapState.IsValid()) InputMapState->OnActiveMapChanged.AddUniqueDynamic(this, &ThisClass::HandleSelectionMapChanged);
	ApplyMapInputContexts(InputMapState.IsValid() ? InputMapState->GetCurrentMapType() : EGameMainMapType::None);
}

void AGameMainMapPlayerController::HandleMapTypeChanged(EGameMainMapType PreviousType, EGameMainMapType CurrentType)
{
	if (UnitSelection) { UnitSelection->CancelBoxSelection(); UnitSelection->ClearUnitSelection(); }
	OnInputStateReset();
	ApplyMapInputContexts(CurrentType);
}

void AGameMainMapPlayerController::HandleSelectionMapChanged(FName PreviousMapID, FName CurrentMapID)
{
	if (PreviousMapID!=CurrentMapID && UnitSelection) { UnitSelection->CancelBoxSelection(); UnitSelection->ClearUnitSelection(); }
}

void AGameMainMapPlayerController::ApplyMapInputContexts(EGameMainMapType MapType)
{
	auto* Input = IsLocalController() && GetLocalPlayer() ? GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	if (!Input) return;
	FModifyContextOptions Options;
	Options.bIgnoreAllPressedKeysUntilRelease = true;
	// Common actions are evaluated before scene actions, even for older Blueprint defaults.
	// Common mouse actions must not consume input so a scene can also bind the same key.
	const int32 ScenePriority=FMath::Min(MapInputPriority,MAX_int32-1);
	const int32 CommonPriority=FMath::Max(CommonInputPriority,ScenePriority+1);
	auto EnsureContext=[&](UInputMappingContext* Context,int32 Priority)
	{
		int32 ExistingPriority=0;
		if (Context && (!Input->HasMappingContext(Context,ExistingPriority) || ExistingPriority!=Priority))
			Input->AddMappingContext(Context,Priority,Options);
	};
	// Keep common input registered across switches; do not clear UI/plugin contexts.
	EnsureContext(CommonInputMappingContext,CommonPriority);
	UInputMappingContext* Desired = MapType == EGameMainMapType::Base ? BaseInputMappingContext.Get()
		: MapType == EGameMainMapType::Battle ? BattleInputMappingContext.Get() : nullptr;
	for (UInputMappingContext* Context : {BaseInputMappingContext.Get(), BattleInputMappingContext.Get()})
		if (Context && Context != Desired && Context != CommonInputMappingContext) Input->RemoveMappingContext(Context, Options);
	if (Desired && Desired != CommonInputMappingContext) EnsureContext(Desired,ScenePriority);
}

void AGameMainMapPlayerController::ReleaseMapInputContexts()
{
	if (GetWorld()) GetWorld()->GameStateSetEvent.RemoveAll(this);
	if (InputMapState.IsValid()) InputMapState->OnCurrentMapTypeChanged.RemoveDynamic(this, &ThisClass::HandleMapTypeChanged);
	if (InputMapState.IsValid()) InputMapState->OnActiveMapChanged.RemoveDynamic(this, &ThisClass::HandleSelectionMapChanged);
	InputMapState.Reset();
	if (auto* Input = GetLocalPlayer() ? GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr)
		for (UInputMappingContext* Context : {CommonInputMappingContext.Get(), BaseInputMappingContext.Get(), BattleInputMappingContext.Get()})
			if (Context) Input->RemoveMappingContext(Context);
}

void AGameMainMapPlayerController::PlayerTick(float DeltaTime)
{
    // Base regions use a dedicated query channel through architectural geometry.
    const auto* State = GetWorld()->GetGameState<AGameMainMapGameState>();
    CurrentClickTraceChannel = State && State->IsBaseMap()
        ? RegionInteraction::TraceChannel : DefaultClickTraceChannel.GetValue();
    Super::PlayerTick(DeltaTime);
}

void AGameMainMapPlayerController::FlushPressedKeys()
{
    if (UnitSelection) UnitSelection->CancelBoxSelection();
    OnInputStateReset();
    Super::FlushPressedKeys();
}

void AGameMainMapPlayerController::EnterMap_Implementation(FName MapID, const FTransform& EntryTransform)
{
	if (APawn* ControlledPawn = GetPawn())
	{
		if (auto* CameraPawn = Cast<AFCS_FreeCameraPawn>(ControlledPawn)) { CameraPawn->CancelCameraStateMove(); CameraPawn->TriggerMouseRotate(false); }
		ControlledPawn->SetActorLocationAndRotation(EntryTransform.GetLocation(), EntryTransform.GetRotation(), false, nullptr, ETeleportType::TeleportPhysics);
		SetControlRotation(EntryTransform.Rotator());
		if (auto* CameraPawn = Cast<AFCS_FreeCameraPawn>(ControlledPawn))
		{
			// Map entry is a camera cut; ordinary input may retain its configured lag afterwards.
			CameraPawn->SetCameraStateInstant(CameraPawn->GetCurrentCameraState(), true);
		}
	}
}

void AGameMainMapPlayerController::EndPlay(const EEndPlayReason::Type Reason)
{
    ReleaseMapInputContexts();
    bEndingPlay = true;
    ClearMapUI();
    Super::EndPlay(Reason);
}

void AGameMainMapPlayerController::ClearMapUI()
{
    if (UnitSelection) { UnitSelection->CancelBoxSelection(); UnitSelection->ClearUnitSelection(); }
    OnInputStateReset();
    if (BaseWidget) { BaseWidget->RemoveFromParent(); BaseWidget = nullptr; }
    if (BattleWidget) { BattleWidget->RemoveFromParent(); BattleWidget = nullptr; }
}

bool AGameMainMapPlayerController::SwitchMapUI(EGameMainMapType MapType)
{
	// Construct/Destruct 和初始化蓝图事件均可能同步回调此函数。
	if (bSwitchingMapUI || bEndingPlay || !IsLocalController()) { return false; }
	TGuardValue<bool> SwitchingGuard(bSwitchingMapUI, true);
	if (MapType == EGameMainMapType::None)
	{
		ClearMapUI();
		return true;
	}
	UClass* TargetClass = nullptr;
	UMapWidgetBase* ExistingWidget = nullptr;
	if (MapType == EGameMainMapType::Base) { TargetClass = BaseWidgetClass.Get(); ExistingWidget = BaseWidget; }
	else if (MapType == EGameMainMapType::Battle) { TargetClass = BattleWidgetClass.Get(); ExistingWidget = BattleWidget; }
	else { return false; }
	if (IsValid(ExistingWidget))
	{
		if (!ExistingWidget->IsInViewport()) { ExistingWidget->AddToViewport(1); }
		return !bEndingPlay;
	}
	// 配置缺失时保留现有 UI，避免切换到空白画面。
	if (!TargetClass || TargetClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)) { return false; }
	ClearMapUI();
	if (bEndingPlay) { return false; }
	auto* NewWidget = CreateWidget<UMapWidgetBase>(this, TargetClass);
	if (!NewWidget || bEndingPlay) { return false; }
	if (MapType == EGameMainMapType::Base) { BaseWidget = CastChecked<UBaseMapWidget>(NewWidget); }
	else { BattleWidget = CastChecked<UBattleMapWidget>(NewWidget); }
	NewWidget->InitializeMapUI(GetWorld()->GetGameState<AGameMainMapGameState>());
	if (bEndingPlay) { return false; }
	NewWidget->AddToViewport(1);
	return !bEndingPlay;
}

