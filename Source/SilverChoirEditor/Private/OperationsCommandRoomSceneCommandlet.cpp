#include "OperationsCommandRoomSceneCommandlet.h"
#include "GameMainMapArchitectureCommandlet.h"

UOperationsCommandRoomSceneCommandlet::UOperationsCommandRoomSceneCommandlet() { IsClient = false; IsEditor = true; LogToConsole = true; }
int32 UOperationsCommandRoomSceneCommandlet::Main(const FString& Params)
{
    // Retain legacy entry points without restoring obsolete native room nodes.
    return NewObject<UGameMainMapArchitectureCommandlet>()->Main(Params);
}