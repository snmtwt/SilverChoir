#pragma once
#include "MainMapBlueprintBuilder.h"
#include "Map/GameMainMap/GameMainMapLibrary.h"
#include "Map/GameMainMap/GameMainMapPlayerController.h"
#include "Map/BaseMap/BaseMapWidget.h"
#include "Map/BaseMap/RegionBlock.h"
#include "Map/BaseMap/SceneUI/OperationsCommandRoom/OperationsCommandRoomWidget.h"
#include "SMS_SceneBase.h"
#include "FCS_FreeCameraPawn.h"
#include "FCS_FreeCameraBlueprintLibrary.h"
#include "Kismet/GameplayStatics.h"

namespace MainMapRoomMigration
{
using namespace MainMapBP;
inline const TCHAR* EventPage = TEXT("OperationsRoomBlueprintEvents");
inline bool Build(UBlueprint* BP, UBlueprint* PCBlueprint)
{
    FString Interior;
    auto* OldCDO = BP->GeneratedClass->GetDefaultObject();
    auto* CameraProperty = FindFProperty<FStructProperty>(BP->GeneratedClass, TEXT("InteriorCameraState"));
    if (!CameraProperty) return false;
    CameraProperty->ExportText_InContainer(0, Interior, OldCDO, nullptr, nullptr, PPF_None);
    const FGameplayTag SceneTag = CastChecked<USMS_SceneBase>(OldCDO)->SceneTag;
    // Replace only the known room lifecycle graphs; keep all other authored graphs/variables.
    for (FName Name : {FName(TEXT("EnterScene")), FName(TEXT("LeaveScene")), FName(TEXT("OperationsRoomLifecycleEvents"))})
        if (auto* G = Find(BP, Name)) FBlueprintEditorUtils::RemoveGraph(BP, G, EGraphRemoveFlags::None);
    BP->ParentClass = USMS_SceneBase::StaticClass();
    // Rebuild inherited fields before adding Blueprint variables with the old names.
    if (!Compile(BP)) return false;
    Var(BP, TEXT("InteriorCameraState"), Struct(FFCS_CameraState::StaticStruct()), Interior, TEXT("房间内部相机状态"), true, TEXT("作战指挥室|配置"));
    Var(BP, TEXT("RoomMinPitch"), Real(), TEXT("-90"), TEXT("房间临时俯仰下限"), true, TEXT("作战指挥室|配置"));
    Var(BP, TEXT("BaseUI"), Obj(UBaseMapWidget::StaticClass()), TEXT("None"), TEXT("基地UI缓存"));
    Var(BP, TEXT("RoomUI"), Obj(UOperationsCommandRoomWidget::StaticClass()), TEXT("None"), TEXT("作战指挥室UI缓存"));
    Var(BP, TEXT("RoomCamera"), Obj(AFCS_FreeCameraPawn::StaticClass()), TEXT("None"), TEXT("相机缓存"));
    Var(BP, TEXT("ReturnCameraState"), Struct(FFCS_CameraState::StaticStruct()), TEXT(""), TEXT("进入前相机状态"));
    Var(BP, TEXT("FlowStage"), Int(), TEXT("0"), TEXT("阶段：0未开始/1进入/2活动/3退出/4结束"));
    for (const TCHAR* Name : {TEXT("UiFinish"), TEXT("CameraFinish"), TEXT("EntryRoomStarted"), TEXT("LeaveQueued"), TEXT("CameraCaptured"), TEXT("PreviousMovementDisabled"), TEXT("PreviousRotationDisabled")})
        Var(BP, Name, Bool(), TEXT("false"), Name);
    Var(BP, TEXT("PreviousMinPitch"), Real(), TEXT("0"), TEXT("进入前俯仰下限"));
    Var(BP, TEXT("PreviousMaxPitch"), Real(), TEXT("0"), TEXT("进入前俯仰上限"));
    Var(BP, TEXT("SavedRegions"), Type(UEdGraphSchema_K2::PC_Object, ARegionBlock::StaticClass(), EPinContainerType::Array), TEXT(""), TEXT("隐藏的基地区域"));
    Var(BP, TEXT("SavedRegionVisibility"), Type(UEdGraphSchema_K2::PC_Boolean, nullptr, EPinContainerType::Array), TEXT(""), TEXT("区域进入前可见性"));
    TArray<FName> Names = {TEXT("Room_CacheContext"), TEXT("Room_CacheCamera"), TEXT("Room_MoveAbove"), TEXT("Room_CameraPositioned"), TEXT("Room_CameraEntered"), TEXT("Room_CameraReturned"),
        TEXT("Room_HideRegions"), TEXT("Room_RestoreRegions"), TEXT("Room_LoadUI"), TEXT("Room_UnloadUI"), TEXT("Room_UILoaded"), TEXT("Room_UIUnloaded"),
        TEXT("Room_UnbindUI"), TEXT("Room_TryFinishEnter"), TEXT("Room_TryFinishLeave"), TEXT("Room_RestoreCamera"), TEXT("Room_FinalizeLeave")};
    Declare(BP, TEXT("EnterScene"), {}, {}, USMS_SceneBase::StaticClass());
    Declare(BP, TEXT("LeaveScene"), {}, {}, USMS_SceneBase::StaticClass());
    Declare(BP, TEXT("Room_SetBackButton"), {{TEXT("Visible"), Bool(), TEXT("false")}});
    Declare(BP, TEXT("Room_SetControllerInput"), {{TEXT("Enabled"), Bool(), TEXT("false")}});
    for (auto Name : Names) Declare(BP, Name);
    auto* Events = FBlueprintEditorUtils::CreateNewGraph(BP, EventPage, UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
    FBlueprintEditorUtils::AddUbergraphPage(BP, Events);
    FGraph EventsBuilder(BP, Events);
    auto* Deferred = EventsBuilder.Node<UK2Node_CustomEvent>([](auto* N) { N->CustomFunctionName = TEXT("Room_DeferredLeave"); });
    if (!Compile(BP)) return false;
    const auto CameraCall = [](FGraph& G, FName Function)
    { auto* N = G.Call(AFCS_FreeCameraPawn::StaticClass(), Function); Link(G.Get(TEXT("RoomCamera")), P(N, TEXT("self"))); return N; };
    const auto Back = [](FGraph& G, UEdGraphPin* Flow, bool Visible)
    { auto* N = G.Call(TEXT("Room_SetBackButton")); D(N, TEXT("Visible"), Visible ? TEXT("true") : TEXT("false")); return G.Exec(Flow, N); };
    const auto Move = [&](FGraph& G, UEdGraphPin* Flow, UEdGraphPin* State, FName Callback)
    {
        auto* Guard = G.Branch(Flow, G.Valid(G.Get(TEXT("RoomCamera"))));
        auto* N = CameraCall(G, TEXT("MoveToCameraState")); Link(State, P(N, TEXT("InCameraState")));
        auto* Delegate = G.Delegate(Callback); Link(Delegate->GetDelegateOutPin(), P(N, TEXT("MoveFinished"))); Delegate->HandleAnyChangeWithoutNotifying();
        auto* Done = G.Exec(Guard->GetThenPin(), N);
        G.Invoke(Guard->GetElsePin(), Callback); return Done;
    };
    {
        FGraph G(BP, TEXT("Room_SetBackButton"));
        auto* Guard = G.Branch(G.Start(), G.Valid(G.Get(TEXT("BaseUI"))));
        auto* N = G.Call(UBaseMapWidget::StaticClass(), TEXT("SetBackButtonVisible")); Link(G.Get(TEXT("BaseUI")), P(N, TEXT("self"))); Link(P(G.Entry, TEXT("Visible")), P(N, TEXT("bVisible"))); G.Exec(Guard->GetThenPin(), N);
    }
    {
        FGraph G(BP, TEXT("Room_SetControllerInput"));
        auto* PC = G.Call(UGameMainMapLibrary::StaticClass(), TEXT("GetGameMainMapPlayerController"));
        auto* Cast = G.CastTo(PCBlueprint->GeneratedClass, PC->GetReturnValuePin(), G.Start());
        auto* N = G.Call(PCBlueprint->GeneratedClass, TEXT("CommandMap_SetRoomInput"));
        Link(Cast->GetCastResultPin(), P(N, TEXT("self"))); Link(P(G.Entry, TEXT("Enabled")), P(N, TEXT("Enabled"))); G.Exec(Cast->GetValidCastPin(), N);
    }
    {
        FGraph G(BP, TEXT("Room_CacheContext"));
        auto* PC = G.Call(UGameMainMapLibrary::StaticClass(), TEXT("GetGameMainMapPlayerController"));
        auto* Guard = G.Branch(G.Start(), G.Valid(PC->GetReturnValuePin()));
        G.Set(Guard->GetThenPin(), TEXT("BaseUI"), TEXT(""), G.Get(TEXT("BaseWidget"), AGameMainMapPlayerController::StaticClass(), PC->GetReturnValuePin()));
        G.Comment(TEXT("像小队会议室一样缓存基地UI。主地图对象通过函数库获取，不在场景C++中保存。"));
    }
    {
        FGraph G(BP, TEXT("Room_CacheCamera"));
        auto* Camera = G.Call(UFCS_FreeCameraBlueprintLibrary::StaticClass(), TEXT("GetFreeCamera"));
        auto* Flow = G.Set(G.Start(), TEXT("RoomCamera"), TEXT(""), Camera->GetReturnValuePin());
        auto* Guard = G.Branch(Flow, G.Valid(G.Get(TEXT("RoomCamera"))));
        Flow = G.Set(Guard->GetThenPin(), TEXT("ReturnCameraState"), TEXT(""), CameraCall(G, TEXT("GetCurrentCameraState"))->GetReturnValuePin());
        Flow = G.Set(Flow, TEXT("PreviousMovementDisabled"), TEXT(""), CameraCall(G, TEXT("IsCameraMovementDisabled"))->GetReturnValuePin());
        Flow = G.Set(Flow, TEXT("PreviousRotationDisabled"), TEXT(""), CameraCall(G, TEXT("IsCameraRotationDisabled"))->GetReturnValuePin());
        auto* Limits = CameraCall(G, TEXT("GetPitchLimits"));
        Flow = G.Set(Flow, TEXT("PreviousMinPitch"), TEXT(""), P(Limits, TEXT("OutMinPitch")));
        Flow = G.Set(Flow, TEXT("PreviousMaxPitch"), TEXT(""), P(Limits, TEXT("OutMaxPitch")));
        Flow = G.Set(Flow, TEXT("CameraCaptured"), TEXT("true"));
        for (FName Function : {FName(TEXT("SetCameraMovementDisabled")), FName(TEXT("SetCameraRotationDisabled"))})
        { auto* N = CameraCall(G, Function); D(N, TEXT("bDisabled"), TEXT("true")); Flow = G.Exec(Flow, N); }
        auto* SetLimits = CameraCall(G, TEXT("SetPitchLimits"));
        Link(G.Math(TEXT("FMin"), G.Get(TEXT("PreviousMinPitch")), G.Get(TEXT("RoomMinPitch"))), P(SetLimits, TEXT("InMinPitch")));
        Link(G.Get(TEXT("PreviousMaxPitch")), P(SetLimits, TEXT("InMaxPitch"))); G.Exec(Flow, SetLimits);
        G.Comment(TEXT("保存运行时相机状态、控制锁和俯仰范围，再临时扩展下限到 RoomMinPitch（默认-90）。退出恢复保存值。"));
    }
    {
        FGraph G(BP, TEXT("EnterScene"));
        auto* Flow = G.Set(G.Start(), TEXT("FlowStage"), TEXT("1"));
        for (FName Name : {FName(TEXT("UiFinish")), FName(TEXT("CameraFinish")), FName(TEXT("EntryRoomStarted")), FName(TEXT("LeaveQueued")), FName(TEXT("CameraCaptured"))}) Flow = G.Set(Flow, Name, TEXT("false"));
        Flow = G.Invoke(Flow, TEXT("Room_UnbindUI")); Flow = G.Set(Flow, TEXT("RoomUI"), TEXT("None"));
        Flow = G.Invoke(Flow, TEXT("Room_CacheContext")); Flow = Back(G, Flow, false);
        auto* Input = G.Call(TEXT("Room_SetControllerInput")); D(Input, TEXT("Enabled"), TEXT("true")); Flow = G.Exec(Flow, Input);
        Flow = G.Invoke(Flow, TEXT("Room_CacheCamera")); Flow = G.Invoke(Flow, TEXT("Room_MoveAbove"));
        G.Return(Flow, {}, {{TEXT("ReturnValue"), TEXT("false")}});
        G.Comment(TEXT("进入第一段：重置蓝图状态、缓存UI和返回视角、隐藏返回按钮、锁定相机，然后只移动到房间位置。返回 false 等待异步完成。"));
    }
    {
        FGraph G(BP, TEXT("Room_MoveAbove"));
        auto* InteriorState = G.Break(FFCS_CameraState::StaticStruct(), G.Get(TEXT("InteriorCameraState")));
        auto* ReturnState = G.Break(FFCS_CameraState::StaticStruct(), G.Get(TEXT("ReturnCameraState")));
        auto* State = G.Node<UK2Node_MakeStruct>([](auto* N) { N->StructType = FFCS_CameraState::StaticStruct(); });
        for (FName Name : {FName(TEXT("Location")), FName(TEXT("MoveSpeed")), FName(TEXT("RotationSpeed")), FName(TEXT("ZoomSpeed"))}) Link(P(InteriorState, Name), P(State, Name));
        for (FName Name : {FName(TEXT("Yaw")), FName(TEXT("Pitch")), FName(TEXT("TargetArmLength"))}) Link(P(ReturnState, Name), P(State, Name));
        Move(G, G.Start(), P(State, TEXT("FCS_CameraState")), TEXT("Room_CameraPositioned"));
        G.Comment(TEXT("第一段仅取房间位置与作者配置的速度，保留进入前角度和臂长；完成回调才开始第二段缩进。"));
    }
    {
        FGraph G(BP, TEXT("Room_CameraPositioned"));
        auto* Guard = G.Branch(G.Start(), G.Both(G.Stage(1), G.Not(G.Get(TEXT("EntryRoomStarted")))));
        auto* Flow = G.Set(Guard->GetThenPin(), TEXT("EntryRoomStarted"), TEXT("true"));
        Flow = G.Invoke(Flow, TEXT("Room_HideRegions"));
        auto* Seq = G.Sequence(Flow);
        G.Invoke(Seq->GetThenPinGivenIndex(0), TEXT("Room_LoadUI"));
        Move(G, Seq->GetThenPinGivenIndex(1), G.Get(TEXT("InteriorCameraState")), TEXT("Room_CameraEntered"));
        G.Comment(TEXT("到达房间位置后，隐藏区域、创建右侧UI并开始缩进。CameraFinish 与 UiFinish 都完成后才通知进入完成。"));
    }
    for (const auto& Spec : TArray<TTuple<FName, int32, FName, FName>>{
        {TEXT("Room_CameraEntered"),1,TEXT("CameraFinish"),TEXT("Room_TryFinishEnter")}, {TEXT("Room_UILoaded"),1,TEXT("UiFinish"),TEXT("Room_TryFinishEnter")},
        {TEXT("Room_CameraReturned"),3,TEXT("CameraFinish"),TEXT("Room_TryFinishLeave")}, {TEXT("Room_UIUnloaded"),3,TEXT("UiFinish"),TEXT("Room_TryFinishLeave")}})
    {
        FGraph G(BP, Spec.Get<0>()); auto* Guard = G.Branch(G.Start(), G.Stage(Spec.Get<1>()));
        G.Invoke(G.Set(Guard->GetThenPin(), Spec.Get<2>(), TEXT("true")), Spec.Get<3>());
    }
    {
        FGraph G(BP, TEXT("Room_LoadUI"));
        auto* Guard = G.Branch(G.Start(), G.Valid(G.Get(TEXT("BaseUI"))));
        auto* Create = G.Call(UBaseMapWidget::StaticClass(), TEXT("CreateSceneUIByTag"));
        Link(G.Get(TEXT("BaseUI")), P(Create, TEXT("self"))); Link(G.Get(TEXT("SceneTag")), P(Create, TEXT("SceneTag")));
        auto* Cast = G.CastTo(UOperationsCommandRoomWidget::StaticClass(), Create->GetReturnValuePin(), G.Exec(Guard->GetThenPin(), Create));
        auto* Flow = G.Set(Cast->GetValidCastPin(), TEXT("RoomUI"), TEXT(""), Cast->GetCastResultPin());
        Flow = G.Bind(Flow, UBaseSceneWidget::StaticClass(), G.Get(TEXT("RoomUI")), TEXT("OnLoadCompleted"), TEXT("Room_UILoaded"));
        Flow = G.Bind(Flow, UBaseSceneWidget::StaticClass(), G.Get(TEXT("RoomUI")), TEXT("OnUnloadCompleted"), TEXT("Room_UIUnloaded"));
        auto* Loaded = G.Call(UBaseSceneWidget::StaticClass(), TEXT("IsSceneUILoaded")); Link(G.Get(TEXT("RoomUI")), P(Loaded, TEXT("self")));
        G.Invoke(G.Branch(Flow, Loaded->GetReturnValuePin())->GetThenPin(), TEXT("Room_UILoaded"));
        G.Invoke(Guard->GetElsePin(), TEXT("Room_UILoaded")); G.Invoke(Cast->GetInvalidCastPin(), TEXT("Room_UILoaded"));
        G.Comment(TEXT("缓存房间UI并绑定加载/卸载完成。复用已加载UI时补发本蓝图完成函数，异步动画仍正常等待。"));
    }
    {
        FGraph G(BP, TEXT("Room_UnbindUI"));
        auto* Guard = G.Branch(G.Start(), G.Valid(G.Get(TEXT("RoomUI"))));
        auto* Flow = G.Bind(Guard->GetThenPin(), UBaseSceneWidget::StaticClass(), G.Get(TEXT("RoomUI")), TEXT("OnLoadCompleted"), TEXT("Room_UILoaded"), true);
        G.Bind(Flow, UBaseSceneWidget::StaticClass(), G.Get(TEXT("RoomUI")), TEXT("OnUnloadCompleted"), TEXT("Room_UIUnloaded"), true);
    }
    {
        FGraph G(BP, TEXT("Room_TryFinishEnter"));
        auto* Guard = G.Branch(G.Start(), G.Both(G.Stage(1), G.Both(G.Get(TEXT("UiFinish")), G.Get(TEXT("CameraFinish")))));
        auto* Flow = G.Set(Guard->GetThenPin(), TEXT("FlowStage"), TEXT("2"));
        G.Exec(Back(G, Flow, true), G.Call(USMS_SceneBase::StaticClass(), TEXT("NotifyEnterCompleted")));
    }
    {
        FGraph G(BP, TEXT("LeaveScene"));
        auto* Flow = G.Set(G.Start(), TEXT("FlowStage"), TEXT("3")); Flow = G.Set(Flow, TEXT("CameraFinish"), TEXT("false"));
        Flow = G.Set(Flow, TEXT("UiFinish"), TEXT("false")); Flow = G.Set(Flow, TEXT("LeaveQueued"), TEXT("false")); Flow = Back(G, Flow, false);
        auto* Seq = G.Sequence(Flow, 3);
        Move(G, Seq->GetThenPinGivenIndex(0), G.Get(TEXT("ReturnCameraState")), TEXT("Room_CameraReturned"));
        G.Invoke(Seq->GetThenPinGivenIndex(1), TEXT("Room_UnloadUI"));
        G.Return(Seq->GetThenPinGivenIndex(2), {}, {{TEXT("ReturnValue"),TEXT("false")}});
        G.Comment(TEXT("退出：相机退回实际进入前视角，并行卸载自身UI；相机与UI都完成后，下一帧恢复状态并通知场景管理器。"));
    }
    {
        FGraph G(BP, TEXT("Room_UnloadUI"));
        auto* Guard = G.Branch(G.Start(), G.Both(G.Valid(G.Get(TEXT("BaseUI"))), G.Valid(G.Get(TEXT("RoomUI")))));
        auto* Current = G.Call(UBaseMapWidget::StaticClass(), TEXT("GetCurrentSceneUI")); Link(G.Get(TEXT("BaseUI")), P(Current, TEXT("self")));
        auto* Own = G.Branch(Guard->GetThenPin(), G.Math(TEXT("EqualEqual_ObjectObject"), Current->GetReturnValuePin(), G.Get(TEXT("RoomUI"))));
        auto* Unload = G.Call(UBaseMapWidget::StaticClass(), TEXT("UnloadCurrentSceneUI")); Link(G.Get(TEXT("BaseUI")), P(Unload, TEXT("self"))); G.Exec(Own->GetThenPin(), Unload);
        G.Invoke(Guard->GetElsePin(), TEXT("Room_UIUnloaded")); G.Invoke(Own->GetElsePin(), TEXT("Room_UIUnloaded"));
    }
    {
        FGraph G(BP, TEXT("Room_TryFinishLeave"));
        auto* Guard = G.Branch(G.Start(), G.Both(G.Both(G.Stage(3), G.Not(G.Get(TEXT("LeaveQueued")))), G.Both(G.Get(TEXT("UiFinish")), G.Get(TEXT("CameraFinish")))));
        G.Invoke(G.Set(Guard->GetThenPin(), TEXT("LeaveQueued"), TEXT("true")), TEXT("Room_DeferredLeave"));
    }
    {
        FGraph G(BP, Events); G.Comment(TEXT("下一帧再退出：先让基地宿主UI释放卸载锁，避免同步回调重入下一场景。"));
        auto* Delay = G.Call(UKismetSystemLibrary::StaticClass(), TEXT("DelayUntilNextTick"));
        G.Invoke(G.Exec(P(Deferred, TEXT("then")), Delay), TEXT("Room_FinalizeLeave"));
    }
    {
        FGraph G(BP, TEXT("Room_RestoreCamera"));
        auto* Guard = G.Branch(G.Start(), G.Both(G.Get(TEXT("CameraCaptured")), G.Valid(G.Get(TEXT("RoomCamera")))));
        auto* Limits = CameraCall(G, TEXT("SetPitchLimits")); Link(G.Get(TEXT("PreviousMinPitch")), P(Limits, TEXT("InMinPitch"))); Link(G.Get(TEXT("PreviousMaxPitch")), P(Limits, TEXT("InMaxPitch")));
        auto* Flow = G.Exec(Guard->GetThenPin(), Limits);
        for (bool Movement : {true,false})
        { auto* N = CameraCall(G, Movement ? TEXT("SetCameraMovementDisabled") : TEXT("SetCameraRotationDisabled")); Link(G.Get(Movement ? TEXT("PreviousMovementDisabled") : TEXT("PreviousRotationDisabled")), P(N, TEXT("bDisabled"))); Flow = G.Exec(Flow, N); }
        G.Set(Flow, TEXT("CameraCaptured"), TEXT("false"));
    }
    {
        FGraph G(BP, TEXT("Room_HideRegions"));
        auto* Flow = G.Exec(G.Start(), G.Array(TEXT("Array_Clear"), G.Get(TEXT("SavedRegions"))));
        Flow = G.Exec(Flow, G.Array(TEXT("Array_Clear"), G.Get(TEXT("SavedRegionVisibility"))));
        auto* Actors = G.Call(UGameplayStatics::StaticClass(), TEXT("GetAllActorsOfClass")); P(Actors, TEXT("ActorClass"))->DefaultObject = ARegionBlock::StaticClass(); Actors->ReconstructNode();
        auto* Loop = G.Loop(G.Exec(Flow, Actors), P(Actors, TEXT("OutActors")));
        auto* Cast = G.CastTo(ARegionBlock::StaticClass(), P(Loop, TEXT("Array Element")), P(Loop, TEXT("LoopBody")));
        auto* Add = G.Array(TEXT("Array_Add"), G.Get(TEXT("SavedRegions"))); Link(Cast->GetCastResultPin(), P(Add, TEXT("NewItem"))); Flow = G.Exec(Cast->GetValidCastPin(), Add);
        auto* Visible = G.Call(ARegionBlock::StaticClass(), TEXT("IsRegionShown")); Link(Cast->GetCastResultPin(), P(Visible, TEXT("self")));
        Add = G.Array(TEXT("Array_Add"), G.Get(TEXT("SavedRegionVisibility"))); Link(Visible->GetReturnValuePin(), P(Add, TEXT("NewItem"))); Flow = G.Exec(Flow, Add);
        auto* Hide = G.Call(ARegionBlock::StaticClass(), TEXT("SetRegionShown")); Link(Cast->GetCastResultPin(), P(Hide, TEXT("self"))); D(Hide, TEXT("bShown"), TEXT("false")); G.Exec(Flow, Hide);
    }
    {
        FGraph G(BP, TEXT("Room_RestoreRegions")); auto* Loop = G.Loop(G.Start(), G.Get(TEXT("SavedRegions")));
        auto* Region = P(Loop, TEXT("Array Element")); auto* Guard = G.Branch(P(Loop, TEXT("LoopBody")), G.Valid(Region));
        auto* Visible = G.Array(TEXT("Array_Get"), G.Get(TEXT("SavedRegionVisibility"))); Link(P(Loop, TEXT("Array Index")), P(Visible, TEXT("Index")));
        auto* Restore = G.Call(ARegionBlock::StaticClass(), TEXT("SetRegionShown")); Link(Region, P(Restore, TEXT("self"))); Link(P(Visible, TEXT("Item")), P(Restore, TEXT("bShown"))); G.Exec(Guard->GetThenPin(), Restore);
        auto* Flow = G.Exec(P(Loop, TEXT("Completed")), G.Array(TEXT("Array_Clear"), G.Get(TEXT("SavedRegions")))); G.Exec(Flow, G.Array(TEXT("Array_Clear"), G.Get(TEXT("SavedRegionVisibility"))));
    }
    {
        FGraph G(BP, TEXT("Room_FinalizeLeave")); auto* Guard = G.Branch(G.Start(), G.Both(G.Stage(3), G.Get(TEXT("LeaveQueued"))));
        auto* Flow = G.Set(Guard->GetThenPin(), TEXT("FlowStage"), TEXT("4"));
        Flow = G.Invoke(Flow, TEXT("Room_UnbindUI")); Flow = G.Invoke(Flow, TEXT("Room_RestoreRegions")); Flow = G.Invoke(Flow, TEXT("Room_RestoreCamera"));
        auto* Input = G.Call(TEXT("Room_SetControllerInput")); D(Input, TEXT("Enabled"), TEXT("false")); Flow = G.Exec(Flow, Input);
        for (FName Name : {FName(TEXT("BaseUI")), FName(TEXT("RoomUI")), FName(TEXT("RoomCamera"))}) Flow = G.Set(Flow, Name, TEXT("None"));
        G.Exec(Flow, G.Call(USMS_SceneBase::StaticClass(), TEXT("NotifyLeaveCompleted")));
    }
    if (!Compile(BP)) return false;
    CastChecked<USMS_SceneBase>(BP->GeneratedClass->GetDefaultObject())->SceneTag = SceneTag;
    // Explicitly restore the author's camera, including values stored only in the old CDO.
    if (auto* Prop = FindFProperty<FStructProperty>(BP->GeneratedClass, TEXT("InteriorCameraState")))
        Prop->ImportText_Direct(*Interior, Prop->ContainerPtrToValuePtr<void>(BP->GeneratedClass->GetDefaultObject()), BP->GeneratedClass->GetDefaultObject(), PPF_None);
    return OK;
}
}
