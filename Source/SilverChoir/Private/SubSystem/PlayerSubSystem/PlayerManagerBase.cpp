#include "SubSystem/PlayerSubSystem/PlayerManagerBase.h"


#include "Components/SIS_UnitInventoryComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogPlayerSubsystem, Log, All);

UPlayerManagerBase::UPlayerManagerBase() { InventoryPages.SetNum(5); }
bool UPlayerManagerBase::IsInventoryPageInitialized(USIS_UnitInventoryComponent* InventoryComponent) const
{
    return IsValid(InventoryComponent) && InitializedInventoryComponent.Get()==InventoryComponent;
}
void UPlayerManagerBase::MarkInventoryPageInitialized(USIS_UnitInventoryComponent* InventoryComponent)
{
    InitializedInventoryComponent=InventoryComponent;
}
void UPlayerManagerBase::ResetInventoryPageInitialization()
{
    InitializedInventoryComponent.Reset();
}
bool UPlayerManagerBase::ReadInventoryPage(int32 PageIndex,TArray<FSIS_ItemData>& Items) const
{
    Items.Reset();
    if (!InventoryPages.IsValidIndex(PageIndex)) return false;
    Items=InventoryPages[PageIndex].Items;
    return true;
}
bool UPlayerManagerBase::WriteInventoryPage(int32 PageIndex,const TArray<FSIS_ItemData>& Items)
{
    if (!InventoryPages.IsValidIndex(PageIndex)) return false;
    InventoryPages[PageIndex].Items=Items;
    return true;
}
bool UPlayerManagerBase::SetActiveInventoryPage(int32 PageIndex)
{
    if (!InventoryPages.IsValidIndex(PageIndex)) return false;
    ActiveInventoryPage=PageIndex;
    return true;
}

UWorld* UPlayerManagerBase::GetWorld() const
{
	const UObject* Outer = GetOuter();
	return !IsTemplate() && IsValid(Outer) ? Outer->GetWorld() : nullptr;
}

bool UPlayerManagerBase::InitializeGame_Implementation()
{
	return true;
}

bool UPlayerManagerBase::LoadGame_Implementation(const FString& SlotName, int32 UserIndex)
{
	return false;
}

bool UPlayerManagerBase::SaveGame_Implementation(const FString& SlotName, int32 UserIndex)
{
	return false;
}

void UPlayerManagerBase::LoadNewGameData_Implementation()
{
	UE_LOG(LogPlayerSubsystem, Error, TEXT("需要在蓝图中加载新游戏数据"));
}
