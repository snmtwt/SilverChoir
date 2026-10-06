#include "OperationsCommandRoomSceneEventsCommandlet.h"
#include "GameMainMapArchitectureCommandlet.h"

UOperationsCommandRoomSceneEventsCommandlet::UOperationsCommandRoomSceneEventsCommandlet() { IsClient = false; IsEditor = true; LogToConsole = true; }
int32 UOperationsCommandRoomSceneEventsCommandlet::Main(const FString& Params)
{
    // Retain legacy entry points without restoring obsolete native room nodes.
    return NewObject<UGameMainMapArchitectureCommandlet>()->Main(Params);
}