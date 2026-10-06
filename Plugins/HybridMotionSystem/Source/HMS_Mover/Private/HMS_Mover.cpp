// Copyright Epic Games, Inc. All Rights Reserved.

#include "HMS_Mover.h"

#define LOCTEXT_NAMESPACE "FHMS_MoverModule"

void FHMS_MoverModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
}

void FHMS_MoverModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FHMS_MoverModule, HMS_Mover)