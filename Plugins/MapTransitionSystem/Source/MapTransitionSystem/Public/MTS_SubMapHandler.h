#pragma once

#include "CoreMinimal.h"
#include "MTS_SubMapSubsystem.h"
#include "MTS_SubMapHandler.generated.h"

/** Each instance retains its handler until actual unload, including after loading completes. */
UCLASS(Blueprintable, BlueprintType, meta=(DisplayName="子地图资源处理类"))
class MAPTRANSITIONSYSTEM_API UMTS_SubMapHandler : public UObject
{
	GENERATED_BODY()
public:
	virtual UWorld* GetWorld() const override;
	void InitializeHandler(UMTS_SubMapSubsystem* InSubsystem) { Subsystem = InSubsystem; }
    UFUNCTION(BlueprintPure, Category="地图切换|子地图", meta=(DisplayName="获取所属子地图ID")) FName GetMapID() const { return AssignedMapID; }
    /** Validates this object as well as the ID, so an old handler cannot address a replacement instance. */
    UFUNCTION(BlueprintPure, Category="地图切换|子地图", meta=(DisplayName="获取本子地图信息"))
    bool GetSubMapInfo(FMTS_SubMapInfo& OutMap) const;
    UFUNCTION(BlueprintPure, Category="地图切换|子地图", meta=(DisplayName="本子地图是否初始化完成"))
    bool IsSubMapReady() const;
    UFUNCTION(BlueprintPure, Category="地图切换|子地图", meta=(DisplayName="计算本子地图世界变换", AutoCreateRefTerm="LocalTransform"))
    bool GetWorldTransform(const FTransform& LocalTransform, FTransform& OutWorldTransform) const;
    UFUNCTION(BlueprintPure, Category="地图切换|子地图", meta=(DisplayName="获取处理对象最近错误"))
    FText GetLastHandlerError() const { return LastHandlerError; }
    UFUNCTION(BlueprintCallable, Category="地图切换|子地图", meta=(DisplayName="更新本子地图加载进度", AutoCreateRefTerm="Content")) bool UpdateLoadingProgress(float Progress, const FText& Content);
    UFUNCTION(BlueprintCallable, Category="地图切换|子地图", meta=(DisplayName="通知本子地图初始化完成")) bool FinishLoading();
    UFUNCTION(BlueprintCallable, Category="地图切换|子地图", meta=(DisplayName="报告本子地图初始化失败", AutoCreateRefTerm="Error")) bool FailLoading(const FText& Error);
    UFUNCTION(BlueprintCallable, Category="地图切换|子地图", meta=(DisplayName="卸载本子地图")) bool UnloadMap();
    /** Progress is Completed here; game code may activate the map before the loading screen closes. */
    UFUNCTION(BlueprintNativeEvent, Category="地图切换|子地图", meta=(DisplayName="子地图初始化完成"))
    void OnSubMapReady(UMTS_SubMapSubsystem* MapSubsystem, const FMTS_SubMapInfo& Map);
    virtual void OnSubMapReady_Implementation(UMTS_SubMapSubsystem* MapSubsystem, const FMTS_SubMapInfo& Map) {}
    UFUNCTION(BlueprintNativeEvent, Category="地图切换|子地图", meta=(DisplayName="子地图开始卸载"))
    void OnSubMapUnloading(UMTS_SubMapSubsystem* MapSubsystem, const FMTS_SubMapInfo& Map);
    virtual void OnSubMapUnloading_Implementation(UMTS_SubMapSubsystem* MapSubsystem, const FMTS_SubMapInfo& Map) {}
    UFUNCTION(BlueprintNativeEvent, Category="地图切换|子地图", meta=(DisplayName="子地图卸载完成"))
    void OnSubMapUnloaded(UMTS_SubMapSubsystem* MapSubsystem, const FMTS_SubMapInfo& Map);
    virtual void OnSubMapUnloaded_Implementation(UMTS_SubMapSubsystem* MapSubsystem, const FMTS_SubMapInfo& Map) {}

	UFUNCTION(BlueprintNativeEvent, Category="地图切换|子地图", meta=(DisplayName="预加载子地图资源"))
	bool PreloadSubMapResources(UMTS_SubMapSubsystem* MapSubsystem, const FMTS_SubMapInfo& Map);
	virtual bool PreloadSubMapResources_Implementation(UMTS_SubMapSubsystem* MapSubsystem, const FMTS_SubMapInfo& Map) { return true; }

	/** 关卡已加载且可见；此处可继续异步初始化并通过 GetMapID 设置剩余进度。 */
	UFUNCTION(BlueprintNativeEvent, Category="地图切换|子地图", meta=(DisplayName="子地图加载完成"))
	void OnSubMapLoaded(UMTS_SubMapSubsystem* MapSubsystem, const FMTS_SubMapInfo& Map);
	virtual void OnSubMapLoaded_Implementation(UMTS_SubMapSubsystem* MapSubsystem, const FMTS_SubMapInfo& Map) {}

	/** 请求接受后加载失败或未完成时被卸载，恰好调用一次。 */
	UFUNCTION(BlueprintNativeEvent, Category="地图切换|子地图", meta=(DisplayName="子地图加载失败或取消"))
	void OnSubMapLoadFailed(UMTS_SubMapSubsystem* MapSubsystem, const FMTS_SubMapInfo& Map, const FText& Error);
	virtual void OnSubMapLoadFailed_Implementation(UMTS_SubMapSubsystem* MapSubsystem, const FMTS_SubMapInfo& Map, const FText& Error) {}
protected:
    /** Called once by the subsystem on failure, unload, external removal or world teardown,
     * before Blueprint callbacks. Native-owned resources are cleaned even if BP omits a parent call. */
    virtual void OnReleaseResources() {}
    bool RejectOperation(const FText& Error, FText& OutError);
    void ClearHandlerError() { LastHandlerError = FText(); }
private:
    friend class UMTS_SubMapSubsystem;
    void ReleaseNativeResources();
    bool bNativeResourcesReleased = false;
    UPROPERTY(Transient) FText LastHandlerError;
    FName AssignedMapID;
	UPROPERTY(Transient) TObjectPtr<UMTS_SubMapSubsystem> Subsystem;
};
