#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MTS_SubMapSubsystem.h"
#include "MTS_SubMapBlueprintLibrary.generated.h"

UCLASS(meta=(DisplayName="MTS 子地图函数库"))
class MAPTRANSITIONSYSTEM_API UMTS_SubMapBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="地图切换|子地图", meta=(WorldContext="WorldContextObject", DisplayName="创建子地图资源处理对象", DeterminesOutputType="HandlerClass"))
    static UMTS_SubMapHandler* CreateSubMapHandler(const UObject* WorldContextObject,TSubclassOf<UMTS_SubMapHandler> HandlerClass);
    UFUNCTION(BlueprintCallable, Category="地图切换|子地图", meta=(WorldContext="WorldContextObject", DisplayName="按请求与处理对象加载子地图", AutoCreateRefTerm="UnloadMapIDs"))
    static bool LoadSubMapByHandlerObject(const UObject* WorldContextObject,const FMTS_SubMapLoadRequest& Request,UMTS_SubMapHandler* Handler,const TArray<FName>& UnloadMapIDs,FText& OutError);
    UFUNCTION(BlueprintPure, Category="地图切换|子地图", meta=(WorldContext="WorldContextObject", DisplayName="根据ID获取子地图处理对象"))
    static UMTS_SubMapHandler* GetSubMapHandler(const UObject* WorldContextObject,FName MapID);
    UFUNCTION(BlueprintPure, Category="地图切换|子地图", meta=(WorldContext="WorldContextObject", DisplayName="根据Tag获取子地图ID"))
    static TArray<FName> GetSubMapIDsByTag(const UObject* WorldContextObject,FGameplayTag Tag,bool bExactMatch=false);
    UFUNCTION(BlueprintPure, Category="地图切换|子地图", meta=(WorldContext="WorldContextObject", DisplayName="是否正在切换子地图"))
    static bool IsSubMapTransitionInProgress(const UObject* WorldContextObject);
	UFUNCTION(BlueprintPure, Category="地图切换|子地图", meta=(WorldContext="WorldContextObject", DisplayName="根据Key获取子地图"))
	static bool GetSubMapByKey(const UObject* WorldContextObject, FName Key, FMTS_SubMapInfo& OutMap);
	UFUNCTION(BlueprintCallable, Category="地图切换|子地图", meta=(WorldContext="WorldContextObject", DisplayName="根据Key设置子地图显示"))
	static bool SetSubMapVisibleByKey(const UObject* WorldContextObject, FName Key, bool bVisible, FText& OutError);
	UFUNCTION(BlueprintCallable, Category="地图切换|子地图", meta=(WorldContext="WorldContextObject", DisplayName="根据Key隐藏子地图"))
	static bool HideSubMapByKey(const UObject* WorldContextObject, FName Key, FText& OutError);
	UFUNCTION(BlueprintCallable, Category="地图切换|子地图", meta=(WorldContext="WorldContextObject", DisplayName="根据Key显示子地图"))
	static bool ShowSubMapByKey(const UObject* WorldContextObject, FName Key, FText& OutError);
	UFUNCTION(BlueprintPure, Category="地图切换|子地图", meta=(WorldContext="WorldContextObject", DisplayName="获取子地图子系统"))
	static UMTS_SubMapSubsystem* GetSubMapSubsystem(const UObject* WorldContextObject);
	UFUNCTION(BlueprintCallable, Category="地图切换|子地图", meta=(WorldContext="WorldContextObject", DisplayName="加载子地图"))
	static bool LoadSubMap(const UObject* WorldContextObject, const UMTS_SubMapDataAsset* MapAsset, FVector Location, FRotator Rotation, FText& OutError);
	UFUNCTION(BlueprintCallable, Category="地图切换|子地图", meta=(WorldContext="WorldContextObject", DisplayName="按处理类加载子地图"))
	static bool LoadSubMapWithHandler(const UObject* WorldContextObject, const UMTS_SubMapDataAsset* MapAsset,
		FVector Location, FRotator Rotation, TSubclassOf<UMTS_SubMapHandler> HandlerClass, FText& OutError, float AutomaticProgressMax = 1.0f);
	UFUNCTION(BlueprintCallable, Category="地图切换|子地图", meta=(WorldContext="WorldContextObject", DisplayName="设置子地图加载进度"))
	static bool SetSubMapProgress(const UObject* WorldContextObject, FName MapID, float Progress, const FText& LoadingContent);
	UFUNCTION(BlueprintPure, Category="地图切换|子地图", meta=(WorldContext="WorldContextObject", DisplayName="获取所有子地图"))
	static TArray<FMTS_SubMapInfo> GetSubMaps(const UObject* WorldContextObject);
	UFUNCTION(BlueprintPure, Category="地图切换|子地图", meta=(WorldContext="WorldContextObject", DisplayName="获取所有子地图ID"))
	static TArray<FName> GetAllSubMapIDs(const UObject* WorldContextObject);
	UFUNCTION(BlueprintPure, Category="地图切换|子地图", meta=(WorldContext="WorldContextObject", DisplayName="获取所有子地图名"))
	static TArray<FName> GetAllSubMapNames(const UObject* WorldContextObject);
	UFUNCTION(BlueprintPure, Category="地图切换|子地图", meta=(WorldContext="WorldContextObject", DisplayName="根据Tag获取子地图"))
	static TArray<FMTS_SubMapInfo> GetSubMapsByTag(const UObject* WorldContextObject, FGameplayTag Tag, bool bExactMatch = false);
	UFUNCTION(BlueprintCallable, Category="地图切换|子地图", meta=(WorldContext="WorldContextObject", DisplayName="根据ID卸载子地图"))
	static bool UnloadSubMapByID(const UObject* WorldContextObject, FName MapID);
	UFUNCTION(BlueprintCallable, Category="地图切换|子地图", meta=(WorldContext="WorldContextObject", DisplayName="根据名称卸载子地图"))
	static int32 UnloadSubMapsByName(const UObject* WorldContextObject, FName MapName);
	UFUNCTION(BlueprintCallable, Category="地图切换|子地图", meta=(WorldContext="WorldContextObject", DisplayName="根据Tag卸载子地图"))
	static int32 UnloadSubMapsByTag(const UObject* WorldContextObject, FGameplayTag Tag, bool bExactMatch = false);
};
