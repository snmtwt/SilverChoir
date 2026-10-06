#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Subsystems/WorldSubsystem.h"
#include "MTS_MapTransitionTypes.h"
#include "MTS_SubMapDataAsset.h"
#include "MTS_SubMapSubsystem.generated.h"

class UMTS_SubMapDataAsset;
class ULevelStreamingDynamic;
class UMTS_SubMapHandler;
class APlayerController;

UENUM(BlueprintType)
enum class EMTS_SubMapState : uint8
{
	Loading UMETA(DisplayName="加载中"),
	Loaded UMETA(DisplayName="已加载"),
	Unloading UMETA(DisplayName="卸载中"),
	Unloaded UMETA(DisplayName="已卸载"),
	Failed UMETA(DisplayName="加载失败"),
    WaitingForUnload UMETA(DisplayName="等待旧子地图卸载")
};

/** 加载时复制资产元数据，运行中修改资产不会改变实例的 ID、名称和 Tag。 */
USTRUCT(BlueprintType)
struct MAPTRANSITIONSYSTEM_API FMTS_SubMapInfo
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category="子地图") FName MapID;
	UPROPERTY(BlueprintReadOnly, Category="子地图") FName MapName;
	UPROPERTY(BlueprintReadOnly, Category="子地图") FGameplayTagContainer MapTags;
	UPROPERTY(BlueprintReadOnly, Category="子地图") TSoftObjectPtr<UWorld> MapAsset;
	UPROPERTY(BlueprintReadOnly, Category="子地图") FTransform Transform;
	UPROPERTY(BlueprintReadOnly, Category="子地图") EMTS_SubMapState State = EMTS_SubMapState::Loading;
	UPROPERTY(BlueprintReadOnly, Category="子地图") TObjectPtr<ULevelStreamingDynamic> StreamingLevel;
	UPROPERTY(BlueprintReadOnly, Category="子地图") FMTS_MapTransitionPayload LoadingPayload;
	UPROPERTY(BlueprintReadOnly, Category="子地图") float AutomaticProgressMax = 1.0f;
	UPROPERTY(BlueprintReadOnly, Category="子地图") bool bIsVisible = false;
	UPROPERTY(BlueprintReadOnly, Category="子地图") bool bShouldBeVisible = true;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMTS_SubMapStateChanged, const FMTS_SubMapInfo&, Map, const FText&, Error);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMTS_SubMapProgressChanged, FName, MapID, const FMTS_MapTransitionPayload&, Payload);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMTS_SubMapVisibilityChanged, const FMTS_SubMapInfo&, Map);

USTRUCT()
struct FMTS_SubMapOperation
{
    GENERATED_BODY()
    UPROPERTY() FMTS_SubMapLoadRequest Request;
    UPROPERTY() TArray<FName> WaitingForIDs;
    UPROPERTY() TObjectPtr<UMTS_MapLoadingWidget> Widget;
    UPROPERTY() TArray<TWeakObjectPtr<APlayerController>> LockedControllers;
    float DisplayProgress = 0;
    double LastUpdateTime = 0;
    double CloseAt = 0;
    bool bReadyRequested = false;
    bool bFailureNotified = false;
    bool bForeground = false;
};

/** 仅管理由本接口创建的动态子地图，生命周期与当前 World 一致。 */
UCLASS(meta=(DisplayName="MTS 子地图子系统"))
class MAPTRANSITIONSYSTEM_API UMTS_SubMapSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="地图切换|子地图", meta=(DisplayName="创建子地图资源处理对象", DeterminesOutputType="HandlerClass"))
    UMTS_SubMapHandler* CreateSubMapHandler(TSubclassOf<UMTS_SubMapHandler> HandlerClass);
    /** Validate the whole request, wait for the specified old IDs to disappear, then stream. */
    UFUNCTION(BlueprintCallable, Category="地图切换|子地图", meta=(DisplayName="按请求与处理对象加载子地图", AutoCreateRefTerm="UnloadMapIDs"))
    bool LoadSubMapByHandlerObject(const FMTS_SubMapLoadRequest& Request, UMTS_SubMapHandler* Handler, const TArray<FName>& UnloadMapIDs, FText& OutError);
    UFUNCTION(BlueprintCallable, Category="地图切换|子地图", meta=(DisplayName="通知子地图初始化完成")) bool NotifySubMapReady(FName MapID);
    UFUNCTION(BlueprintCallable, Category="地图切换|子地图", meta=(DisplayName="报告子地图初始化失败")) bool ReportSubMapFailure(FName MapID, const FText& Error);
    UFUNCTION(BlueprintPure, Category="地图切换|子地图", meta=(DisplayName="是否正在切换子地图")) bool IsSubMapTransitionInProgress() const { return !ForegroundMapID.IsNone(); }
    UFUNCTION(BlueprintPure, Category="地图切换|子地图", meta=(DisplayName="获取子地图加载界面")) UMTS_MapLoadingWidget* GetSubMapLoadingWidget(FName MapID) const;
    UFUNCTION(BlueprintPure, Category="地图切换|子地图", meta=(DisplayName="根据Tag获取子地图ID")) TArray<FName> GetSubMapIDsByTag(FGameplayTag Tag, bool bExactMatch = false) const;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickableWhenPaused() const override { return true; }
	virtual void Deinitialize() override;

	UPROPERTY(BlueprintAssignable, Category="地图切换|子地图", meta=(DisplayName="子地图状态变化"))
	FMTS_SubMapStateChanged OnSubMapStateChanged;

	UPROPERTY(BlueprintAssignable, Category="地图切换|子地图", meta=(DisplayName="子地图加载进度变化"))
	FMTS_SubMapProgressChanged OnSubMapProgressChanged;
	UPROPERTY(BlueprintAssignable, Category="地图切换|子地图", meta=(DisplayName="子地图实际可见性变化"))
	FMTS_SubMapVisibilityChanged OnSubMapVisibilityChanged;
	/** Key 就是加载数据资产的 MapID；未找到时清空输出。 */
	UFUNCTION(BlueprintPure, Category="地图切换|子地图", meta=(DisplayName="根据Key获取子地图"))
	bool GetSubMapByKey(FName Key, FMTS_SubMapInfo& OutMap) const;
	/** 仅允许已完成加载的实例；请求异步应用，不卸载地图。 */
	UFUNCTION(BlueprintCallable, Category="地图切换|子地图", meta=(DisplayName="根据Key设置子地图显示"))
	bool SetSubMapVisibleByKey(FName Key, bool bVisible, FText& OutError);
	UFUNCTION(BlueprintCallable, Category="地图切换|子地图", meta=(DisplayName="根据Key隐藏子地图"))
	bool HideSubMapByKey(FName Key, FText& OutError) { return SetSubMapVisibleByKey(Key, false, OutError); }
	UFUNCTION(BlueprintCallable, Category="地图切换|子地图", meta=(DisplayName="根据Key显示子地图"))
	bool ShowSubMapByKey(FName Key, FText& OutError) { return SetSubMapVisibleByKey(Key, true, OutError); }

	UFUNCTION(BlueprintCallable, Category="地图切换|子地图", meta=(DisplayName="按处理类加载子地图"))
	bool LoadSubMapWithHandler(const UMTS_SubMapDataAsset* MapAsset, FVector Location, FRotator Rotation,
		TSubclassOf<UMTS_SubMapHandler> HandlerClass, FText& OutError, float AutomaticProgressMax = 1.0f);

	/** 手动进度不受自动上限限制；达到 1 且关卡可见后完成流程。 */
	UFUNCTION(BlueprintCallable, Category="地图切换|子地图", meta=(DisplayName="设置子地图加载进度"))
	bool SetSubMapProgress(FName MapID, float Progress, const FText& LoadingContent);

	UFUNCTION(BlueprintPure, Category="地图切换|子地图", meta=(DisplayName="获取子地图资源处理对象"))
	UMTS_SubMapHandler* GetSubMapHandler(FName MapID) const { return Handlers.FindRef(MapID); }

	/** 返回是否接受请求，而非是否加载完成。Loaded 事件表示已加载且可见。 */
	UFUNCTION(BlueprintCallable, Category="地图切换|子地图", meta=(DisplayName="加载子地图"))
	bool LoadSubMap(const UMTS_SubMapDataAsset* MapAsset, FVector Location, FRotator Rotation, FText& OutError);

	/** 包括加载中、已显示和卸载中；完成卸载/失败的记录自动移除。 */
	UFUNCTION(BlueprintPure, Category="地图切换|子地图", meta=(DisplayName="获取所有子地图"))
	TArray<FMTS_SubMapInfo> GetSubMaps() const;
	UFUNCTION(BlueprintPure, Category="地图切换|子地图", meta=(DisplayName="获取所有子地图ID"))
	TArray<FName> GetAllSubMapIDs() const;
	/** 每个实例返回一个名称；允许重名，因此结果可能重复。 */
	UFUNCTION(BlueprintPure, Category="地图切换|子地图", meta=(DisplayName="获取所有子地图名"))
	TArray<FName> GetAllSubMapNames() const;
	UFUNCTION(BlueprintPure, Category="地图切换|子地图", meta=(DisplayName="根据Tag获取子地图"))
	TArray<FMTS_SubMapInfo> GetSubMapsByTag(FGameplayTag Tag, bool bExactMatch = false) const;

	/** 返回是否接受新的卸载请求，允许取消加载中的实例。 */
	UFUNCTION(BlueprintCallable, Category="地图切换|子地图", meta=(DisplayName="根据ID卸载子地图"))
	bool UnloadSubMapByID(FName MapID);
	/** 返回本次新发起卸载的实例数；重复卸载不计数。 */
	UFUNCTION(BlueprintCallable, Category="地图切换|子地图", meta=(DisplayName="根据名称卸载子地图"))
	int32 UnloadSubMapsByName(FName MapName);
	UFUNCTION(BlueprintCallable, Category="地图切换|子地图", meta=(DisplayName="根据Tag卸载子地图"))
	int32 UnloadSubMapsByTag(FGameplayTag Tag, bool bExactMatch = false);
protected:
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
private:
    bool BeginLoad(const FMTS_SubMapLoadRequest& Request, UMTS_SubMapHandler* Handler, const TArray<FName>& UnloadMapIDs, bool bForeground, FText& OutError);
    bool StartStreaming(FName MapID, FText& OutError);
    void ReleasePresentation(FName MapID);
    void TickPresentation(FName MapID);
    UPROPERTY(Transient) TMap<FName, FMTS_SubMapOperation> Operations;
    FName ForegroundMapID;
	UPROPERTY(Transient) TMap<FName, FMTS_SubMapInfo> Maps;
	UPROPERTY(Transient) TMap<FName, TObjectPtr<UMTS_SubMapHandler>> Handlers;
	void UpdateProgress(FName MapID, EMTS_MapTransitionPhase Phase, float Progress, const FText& Content, bool bAutomatic = true);
	void FailHandler(FName MapID, const FMTS_SubMapInfo& Map, const FText& Error);
	void BroadcastState(const FMTS_SubMapInfo& Map, const FText& Error = FText());
};
