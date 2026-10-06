#include "MTS_SubMapBlueprintLibrary.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

UMTS_SubMapHandler* UMTS_SubMapBlueprintLibrary::CreateSubMapHandler(const UObject* Context,TSubclassOf<UMTS_SubMapHandler> Class)
{auto* S=GetSubMapSubsystem(Context);return S?S->CreateSubMapHandler(Class):nullptr;}
bool UMTS_SubMapBlueprintLibrary::LoadSubMapByHandlerObject(const UObject* Context,const FMTS_SubMapLoadRequest& Request,
    UMTS_SubMapHandler* Handler,const TArray<FName>& UnloadIDs,FText& Error)
{if(auto* S=GetSubMapSubsystem(Context))return S->LoadSubMapByHandlerObject(Request,Handler,UnloadIDs,Error);Error=FText::FromString(TEXT("当前世界没有子地图子系统。"));return false;}
UMTS_SubMapHandler* UMTS_SubMapBlueprintLibrary::GetSubMapHandler(const UObject* Context,FName ID)
{auto* S=GetSubMapSubsystem(Context);return S?S->GetSubMapHandler(ID):nullptr;}
TArray<FName> UMTS_SubMapBlueprintLibrary::GetSubMapIDsByTag(const UObject* Context,FGameplayTag Tag,bool Exact)
{auto* S=GetSubMapSubsystem(Context);return S?S->GetSubMapIDsByTag(Tag,Exact):TArray<FName>();}
bool UMTS_SubMapBlueprintLibrary::IsSubMapTransitionInProgress(const UObject* Context)
{auto* S=GetSubMapSubsystem(Context);return S && S->IsSubMapTransitionInProgress();}

bool UMTS_SubMapBlueprintLibrary::LoadSubMapWithHandler(const UObject* WorldContextObject,
	const UMTS_SubMapDataAsset* MapAsset, FVector Location, FRotator Rotation,
	TSubclassOf<UMTS_SubMapHandler> HandlerClass, FText& OutError, float AutomaticProgressMax)
{
	if (UMTS_SubMapSubsystem* Subsystem = GetSubMapSubsystem(WorldContextObject))
	{
		return Subsystem->LoadSubMapWithHandler(MapAsset, Location, Rotation, HandlerClass, OutError, AutomaticProgressMax);
	}
	OutError = FText::FromString(TEXT("当前世界没有可用的子地图子系统。"));
	return false;
}

bool UMTS_SubMapBlueprintLibrary::SetSubMapProgress(const UObject* WorldContextObject, FName MapID,
	float Progress, const FText& LoadingContent)
{
	UMTS_SubMapSubsystem* Subsystem = GetSubMapSubsystem(WorldContextObject);
	return Subsystem && Subsystem->SetSubMapProgress(MapID, Progress, LoadingContent);
}

UMTS_SubMapSubsystem* UMTS_SubMapBlueprintLibrary::GetSubMapSubsystem(const UObject* WorldContextObject)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<UMTS_SubMapSubsystem>() : nullptr;
}

bool UMTS_SubMapBlueprintLibrary::LoadSubMap(const UObject* WorldContextObject, const UMTS_SubMapDataAsset* MapAsset, FVector Location, FRotator Rotation, FText& OutError)
{
	if (UMTS_SubMapSubsystem* Subsystem = GetSubMapSubsystem(WorldContextObject))
	{
		return Subsystem->LoadSubMap(MapAsset, Location, Rotation, OutError);
	}
	OutError = FText::FromString(TEXT("无法从世界上下文获取子地图子系统。"));
	return false;
}

TArray<FMTS_SubMapInfo> UMTS_SubMapBlueprintLibrary::GetSubMaps(const UObject* WorldContextObject)
{
	if (UMTS_SubMapSubsystem* Subsystem = GetSubMapSubsystem(WorldContextObject))
	{
		return Subsystem->GetSubMaps();
	}
	return {};
}

TArray<FName> UMTS_SubMapBlueprintLibrary::GetAllSubMapIDs(const UObject* WorldContextObject)
{
	if (UMTS_SubMapSubsystem* Subsystem = GetSubMapSubsystem(WorldContextObject))
	{
		return Subsystem->GetAllSubMapIDs();
	}
	return {};
}

TArray<FName> UMTS_SubMapBlueprintLibrary::GetAllSubMapNames(const UObject* WorldContextObject)
{
	if (UMTS_SubMapSubsystem* Subsystem = GetSubMapSubsystem(WorldContextObject))
	{
		return Subsystem->GetAllSubMapNames();
	}
	return {};
}

TArray<FMTS_SubMapInfo> UMTS_SubMapBlueprintLibrary::GetSubMapsByTag(const UObject* WorldContextObject, FGameplayTag Tag, bool bExactMatch)
{
	if (UMTS_SubMapSubsystem* Subsystem = GetSubMapSubsystem(WorldContextObject))
	{
		return Subsystem->GetSubMapsByTag(Tag, bExactMatch);
	}
	return {};
}

bool UMTS_SubMapBlueprintLibrary::UnloadSubMapByID(const UObject* WorldContextObject, FName MapID)
{
	if (UMTS_SubMapSubsystem* Subsystem = GetSubMapSubsystem(WorldContextObject))
	{
		return Subsystem->UnloadSubMapByID(MapID);
	}
	return false;
}

int32 UMTS_SubMapBlueprintLibrary::UnloadSubMapsByName(const UObject* WorldContextObject, FName MapName)
{
	if (UMTS_SubMapSubsystem* Subsystem = GetSubMapSubsystem(WorldContextObject))
	{
		return Subsystem->UnloadSubMapsByName(MapName);
	}
	return 0;
}

int32 UMTS_SubMapBlueprintLibrary::UnloadSubMapsByTag(const UObject* WorldContextObject, FGameplayTag Tag, bool bExactMatch)
{
	if (UMTS_SubMapSubsystem* Subsystem = GetSubMapSubsystem(WorldContextObject))
	{
		return Subsystem->UnloadSubMapsByTag(Tag, bExactMatch);
	}
	return 0;
}

bool UMTS_SubMapBlueprintLibrary::GetSubMapByKey(const UObject* WorldContextObject, FName Key, FMTS_SubMapInfo& OutMap)
{
    OutMap = FMTS_SubMapInfo();
    auto* System = GetSubMapSubsystem(WorldContextObject);
    return System && System->GetSubMapByKey(Key, OutMap);
}

bool UMTS_SubMapBlueprintLibrary::SetSubMapVisibleByKey(const UObject* WorldContextObject, FName Key, bool bVisible, FText& OutError)
{
    if (auto* System = GetSubMapSubsystem(WorldContextObject)) { return System->SetSubMapVisibleByKey(Key, bVisible, OutError); }
    OutError = FText::FromString(TEXT("No valid submap world.")); return false;
}

bool UMTS_SubMapBlueprintLibrary::HideSubMapByKey(const UObject* WorldContextObject, FName Key, FText& OutError)
{
    return SetSubMapVisibleByKey(WorldContextObject, Key, false, OutError);
}

bool UMTS_SubMapBlueprintLibrary::ShowSubMapByKey(const UObject* WorldContextObject, FName Key, FText& OutError)
{
    return SetSubMapVisibleByKey(WorldContextObject, Key, true, OutError);
}
