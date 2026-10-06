#include "GridStrategyMapSystem/Display2D/GSMTileMenuButton.h"

#include "GridStrategyMapSystem/Display2D/GSMTileContextMenu.h"

void UGSMTileMenuButton::SetMenuContext(
	const FGSMTileMenuContext& InMenuContext,
	UGSMTileContextMenu* InOwningMenu
)
{
	MenuContext = InMenuContext;
	OwningMenu = InOwningMenu;
	PlayerController = InMenuContext.PlayerController;
	TileActor = InMenuContext.TileActor;
	OwningMap = InMenuContext.OwningMap;
	WorldHitLocation = InMenuContext.WorldHitLocation;
	ScreenPosition = InMenuContext.ScreenPosition;

	OnMenuContextAssigned(MenuContext, OwningMenu);
}

void UGSMTileMenuButton::TriggerOnClickEvent()
{
	bool bHandled = false;
	OnClickMenuButton(MenuContext, OwningMenu, bHandled);
	if (!bHandled)
	{
		OnClicked.Broadcast(this, MenuContext, OwningMenu);
	}
}

void UGSMTileMenuButton::OnClickMenuButton_Implementation(
	const FGSMTileMenuContext& InMenuContext,
	UGSMTileContextMenu* InOwningMenu,
	bool& bHandled
)
{
	(void)InMenuContext;
	(void)InOwningMenu;
	(void)bHandled;
}

void UGSMTileMenuButton::OnMenuContextAssigned_Implementation(
	const FGSMTileMenuContext& InMenuContext,
	UGSMTileContextMenu* InOwningMenu
)
{
	(void)InMenuContext;
	(void)InOwningMenu;
}
