#include "GridStrategyMapSystem/Data/GSMSettings.h"

#include "GridStrategyMapSystem/Display3D/GSMTile3D.h"
#include "GridStrategyMapSystem/Display2D/GSMTileContextMenu.h"

UGSMSettings::UGSMSettings()
{
	DefaultTileActorClass = AGSMTile3D::StaticClass();
	DefaultTileContextMenuClass = nullptr;
	bFlyingNavigationRequiresWalkableGoal = false;
}

TSubclassOf<AGSMTile3D> UGSMSettings::GetDefaultTileActorClass()
{
	const UGSMSettings* Settings = GetDefault<UGSMSettings>();
	if (Settings && Settings->DefaultTileActorClass)
	{
		return Settings->DefaultTileActorClass;
	}

	return AGSMTile3D::StaticClass();
}

TSubclassOf<UGSMTileContextMenu> UGSMSettings::GetDefaultTileContextMenuClass()
{
	const UGSMSettings* Settings = GetDefault<UGSMSettings>();
	if (Settings && Settings->DefaultTileContextMenuClass)
	{
		return Settings->DefaultTileContextMenuClass;
	}

	return nullptr;
}

bool UGSMSettings::ShouldFlyingNavigationRequireWalkableGoal()
{
	return false;
}
