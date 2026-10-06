#pragma once

#include "CoreMinimal.h"
#include "GridStrategyMapSystem/Data/GSMTypes.h"
#include "UObject/Object.h"
#include "GSMNavigationMoveData.generated.h"

class UGSMTileData;
struct FGSMPathResult;

UCLASS(BlueprintType, Blueprintable, meta = (DisplayName = "网格策略地图导航移动数据"))
class GRIDSTRATEGYMAPSYSTEM_API UGSMNavigationMoveData : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "网格策略地图|路径导航", meta = (DisplayName = "设置导航路径"))
	void SetNavigationPath(
		UPARAM(DisplayName = "路径瓦片数据") const TArray<UGSMTileData*>& InPathTiles
	);

	void SetNavigationPathFromResult(const FGSMPathResult& PathResult);

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|路径导航", meta = (DisplayName = "清空导航路径"))
	void ClearNavigationPathData();

	UFUNCTION(BlueprintPure, Category = "网格策略地图|路径导航", meta = (DisplayName = "是否有导航路径"))
	bool HasNavigationPath() const { return PathTileIds.Num() >= 2 && PathTiles.Num() >= 2; }

	UFUNCTION(BlueprintPure, Category = "网格策略地图|路径导航", meta = (DisplayName = "获取路径瓦片ID"))
	TArray<FName> GetPathTileIds() const { return PathTileIds; }

	UFUNCTION(BlueprintPure, Category = "网格策略地图|路径导航", meta = (DisplayName = "获取路径瓦片对象"))
	TArray<UGSMTileData*> GetPathTiles() const;

	UFUNCTION(BlueprintPure, Category = "网格策略地图|路径导航", meta = (DisplayName = "获取路径连接类型"))
	TArray<EGSMNavigationLinkType> GetPathLinkTypes() const { return PathLinkTypes; }

	UFUNCTION(BlueprintImplementableEvent, Category = "网格策略地图|路径导航", meta = (DisplayName = "当导航路径更新"))
	void OnNavigationPathUpdated();

	UFUNCTION(BlueprintImplementableEvent, Category = "网格策略地图|路径导航", meta = (DisplayName = "当导航路径清空"))
	void OnNavigationPathCleared();

protected:
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "网格策略地图|路径导航", meta = (DisplayName = "路径瓦片ID"))
	TArray<FName> PathTileIds;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "网格策略地图|路径导航", meta = (DisplayName = "路径瓦片对象"))
	TArray<TObjectPtr<UGSMTileData>> PathTiles;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "网格策略地图|路径导航", meta = (DisplayName = "路径连接类型"))
	TArray<EGSMNavigationLinkType> PathLinkTypes;
};
