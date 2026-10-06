#pragma once

#include "CoreMinimal.h"

/** Add Blueprint presentation hooks and a vehicle-panel host without rebuilding authored layouts. */
bool UpgradeBattlePanelAssets();

/** Narrow preplaced-vehicle/image upgrade. -Inspect and -Validate never save assets. */
int32 RunBattleVehiclePresentationUpgrade(const FString& Params);

/** Repair the inspected VehiclePanel instance padding only. Read-only unless -Apply is explicit. */
int32 RunBattlePanelLayoutRepair(const FString& Params);
