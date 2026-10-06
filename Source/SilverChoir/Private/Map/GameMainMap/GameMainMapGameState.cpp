#include "Map/GameMainMap/GameMainMapGameState.h"

void AGameMainMapGameState::SetCurrentMapType(EGameMainMapType MapType)
{
	if (MapType == EGameMainMapType::None || MapType == EGameMainMapType::Base || MapType == EGameMainMapType::Battle)
	{
		if (CurrentMapType == MapType) return;
		const EGameMainMapType Previous = CurrentMapType;
		CurrentMapType = MapType;
		OnCurrentMapTypeChanged.Broadcast(Previous, CurrentMapType);
	}
}


