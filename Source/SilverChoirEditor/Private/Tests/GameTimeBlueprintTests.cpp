#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Components/TextBlock.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GTS_TimeLibrary.h"
#include "GTS_TimeManager.h"
#include "GTS_TimeSubsystem.h"
#include "GTS_TimeTask.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameTimeBlueprintCallbackTest,
    "GameTimeSystem.Blueprint.Callback",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGameTimeBlueprintCallbackTest::RunTest(const FString& Parameters)
{
    // Build an actual Blueprint override in memory. No generated test asset is saved to the project.
    TestTrue(TEXT("Native task CDO advertises world-context support"),
        GetDefault<UGTS_TimeTask>()->ImplementsGetWorld());
    TestFalse(TEXT("Task class does not force explicit world-context pins"),
        UGTS_TimeTask::StaticClass()->HasMetaDataHierarchical(TEXT("ShowWorldContextPin")) != nullptr);

    TStrongObjectPtr<UBlueprint> Blueprint(FKismetEditorUtilities::CreateBlueprint(
        UGTS_TimeTask::StaticClass(), GetTransientPackage(),
        MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), TEXT("GTS_CallbackTest")),
        BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(),
        TEXT("GameTimeBlueprintTest")));
    if (!TestNotNull(TEXT("Create transient task Blueprint"), Blueprint.Get())) return false;
    Blueprint->SetFlags(RF_Transient);

    FEdGraphPinType BoolType;
    BoolType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
    FEdGraphPinType DateType;
    DateType.PinCategory = UEdGraphSchema_K2::PC_Struct;
    DateType.PinSubCategoryObject = TBaseStructure<FDateTime>::Get();
    if (!TestTrue(TEXT("Add callback flag"), FBlueprintEditorUtils::AddMemberVariable(Blueprint.Get(), TEXT("bCallbackReached"), BoolType))
        || !TestTrue(TEXT("Add context date result"), FBlueprintEditorUtils::AddMemberVariable(Blueprint.Get(), TEXT("ObservedTime"), DateType))
        || !TestTrue(TEXT("Add received deadline"), FBlueprintEditorUtils::AddMemberVariable(Blueprint.Get(), TEXT("ReceivedDeadline"), DateType))) return false;

    UEdGraph* Graph = Blueprint->UbergraphPages.IsEmpty() ? nullptr : Blueprint->UbergraphPages[0];
    if (!Graph)
    {
        Graph = FBlueprintEditorUtils::CreateNewGraph(Blueprint.Get(), TEXT("EventGraph"),
            UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
        FBlueprintEditorUtils::AddUbergraphPage(Blueprint.Get(), Graph);
    }
    if (!TestNotNull(TEXT("Create event graph"), Graph)) return false;

    FGraphNodeCreator<UK2Node_Event> EventCreator(*Graph);
    UK2Node_Event* Event = EventCreator.CreateNode();
    Event->EventReference.SetExternalMember(GET_FUNCTION_NAME_CHECKED(UGTS_TimeTask, OnTimeReached), UGTS_TimeTask::StaticClass());
    Event->bOverrideFunction = true;
    EventCreator.Finalize();

    auto AddSet = [Graph](FName Variable)
    {
        FGraphNodeCreator<UK2Node_VariableSet> Creator(*Graph);
        UK2Node_VariableSet* Node = Creator.CreateNode();
        Node->VariableReference.SetSelfMember(Variable);
        Creator.Finalize();
        return Node;
    };
    UK2Node_VariableSet* SetFlag = AddSet(TEXT("bCallbackReached"));
    UK2Node_VariableSet* SetObservedTime = AddSet(TEXT("ObservedTime"));
    UK2Node_VariableSet* SetDeadline = AddSet(TEXT("ReceivedDeadline"));

    FGraphNodeCreator<UK2Node_CallFunction> TimeCreator(*Graph);
    UK2Node_CallFunction* ReadCurrentTime = TimeCreator.CreateNode();
    ReadCurrentTime->SetFromFunction(UGTS_TimeLibrary::StaticClass()->FindFunctionByName(
        GET_FUNCTION_NAME_CHECKED(UGTS_TimeLibrary, GetCurrentTime)));
    TimeCreator.Finalize();

    const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
    auto Connect = [&](UEdGraphNode* From, FName FromName, UEdGraphNode* To, FName ToName)
    {
        UEdGraphPin* Output = From->FindPin(FromName);
        UEdGraphPin* Input = To->FindPin(ToName);
        return TestTrue(*FString::Printf(TEXT("Wire %s to %s"), *FromName.ToString(), *ToName.ToString()),
            Output && Input && Schema->TryCreateConnection(Output, Input));
    };
    UEdGraphPin* FlagInput = SetFlag->FindPin(TEXT("bCallbackReached"));
    if (!TestNotNull(TEXT("Boolean setter has its input pin"), FlagInput)) return false;
    Schema->TrySetDefaultValue(*FlagInput, TEXT("true"));
    bool bConnected = Connect(Event, UEdGraphSchema_K2::PN_Then, SetFlag, UEdGraphSchema_K2::PN_Execute);
    bConnected &= Connect(SetFlag, UEdGraphSchema_K2::PN_Then, SetObservedTime, UEdGraphSchema_K2::PN_Execute);
    bConnected &= Connect(SetObservedTime, UEdGraphSchema_K2::PN_Then, SetDeadline, UEdGraphSchema_K2::PN_Execute);
    bConnected &= Connect(ReadCurrentTime, UEdGraphSchema_K2::PN_ReturnValue, SetObservedTime, TEXT("ObservedTime"));
    bConnected &= Connect(Event, TEXT("ScheduledTime"), SetDeadline, TEXT("ReceivedDeadline"));
    if (!bConnected) return false;

    UEdGraphPin* WorldPin = ReadCurrentTime->FindPin(TEXT("WorldContextObject"));
    if (!TestNotNull(TEXT("World-context call has its implicit context pin"), WorldPin)) return false;
    TestTrue(TEXT("World-context pin is hidden on task Blueprint"), WorldPin->bHidden);
    TestTrue(TEXT("World-context pin has no explicit connection"), WorldPin->LinkedTo.IsEmpty());

    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint.Get());
    FCompilerResultsLog Results;
    FKismetEditorUtilities::CompileBlueprint(Blueprint.Get(), EBlueprintCompileOptions::None, &Results);
    TestEqual(TEXT("Task Blueprint compiles without errors"), Results.NumErrors, 0);
    TestEqual(TEXT("Implicit world context emits no unsafe-context warning"), Results.NumWarnings, 0);
    if (!TestTrue(TEXT("Compiled task class exists"), Blueprint->GeneratedClass != nullptr && Blueprint->Status != BS_Error)) return false;
    TestTrue(TEXT("Blueprint task CDO retains world-context support"),
        Blueprint->GeneratedClass->GetDefaultObject()->ImplementsGetWorld());

    TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>(GEngine));
    Instance->InitializeStandalone();
    UWorld* World = Instance->GetWorld();
    ON_SCOPE_EXIT
    {
        Instance->Shutdown();
        if (World)
        {
            GEngine->DestroyWorldContext(World);
            World->DestroyWorld(false);
        }
    };
    UGTS_TimeSubsystem* Subsystem = Instance->GetSubsystem<UGTS_TimeSubsystem>();
    if (!TestTrue(TEXT("Standalone game instance creates a ready time subsystem"), Subsystem && Subsystem->IsReady())) return false;
    UGTS_TimeManager* Manager = Subsystem->GetManager();
    const FDateTime Start(2049, 1, 1, 12, 0, 0);
    const FDateTime Deadline = Start + FTimespan::FromSeconds(60.0);
    TestTrue(TEXT("Assign deterministic game date"), Manager->SetCurrentTime(Start));
    TestTrue(TEXT("Set sixty-times game calendar speed"), Manager->SetTimeScale(60.0));
    Manager->SetTimePaused(false);

    TStrongObjectPtr<UGTS_TimeTask> Task(UGTS_TimeLibrary::CreateTimeTask(World, Blueprint->GeneratedClass.Get()));
    if (!TestNotNull(TEXT("Library constructs concrete Blueprint task"), Task.Get())) return false;
    TestEqual(TEXT("Task resolves world through manager and game instance"), Task->GetWorld(), World);
    TestEqual(TEXT("Task resolves its owning time subsystem through the library"), UGTS_TimeLibrary::GetTimeSubsystem(Task.Get()), Subsystem);
    FBoolProperty* Flag = FindFProperty<FBoolProperty>(Task->GetClass(), TEXT("bCallbackReached"));
    FStructProperty* Observed = FindFProperty<FStructProperty>(Task->GetClass(), TEXT("ObservedTime"));
    FStructProperty* Received = FindFProperty<FStructProperty>(Task->GetClass(), TEXT("ReceivedDeadline"));
    if (!TestTrue(TEXT("Compiled Blueprint result variables are present"), Flag && Observed && Received)) return false;
    TestFalse(TEXT("Blueprint flag is false before dispatch"), Flag->GetPropertyValue_InContainer(Task.Get()));
    const FGuid Id = UGTS_TimeLibrary::RegisterTimeTask(Task.Get(), Task.Get(), Deadline);
    if (!TestTrue(TEXT("Blueprint task registered using its own world context"), Id.IsValid())) return false;
    Manager->AdvanceTime(0.5);
    TestFalse(TEXT("Blueprint callback does not run early"), Flag->GetPropertyValue_InContainer(Task.Get()));
    Manager->AdvanceTime(0.5);
    TestTrue(TEXT("Scheduled dispatch executes Blueprint event override"), Flag->GetPropertyValue_InContainer(Task.Get()));
    TestEqual(TEXT("Unwired implicit world context reads the correct game calendar"),
        *Observed->ContainerPtrToValuePtr<FDateTime>(Task.Get()), Deadline);
    TestEqual(TEXT("Blueprint receives the registered deadline"),
        *Received->ContainerPtrToValuePtr<FDateTime>(Task.Get()), Deadline);
    TestFalse(TEXT("Completed Blueprint task is removed from registration"), Manager->IsTimeTaskRegistered(Id));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameTimeTextRegistrationNodeTest,
    "GameTimeSystem.Blueprint.TextRegistrationNode",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGameTimeTextRegistrationNodeTest::RunTest(const FString& Parameters)
{
    UFunction* RegisterFunction = UGTS_TimeLibrary::StaticClass()->FindFunctionByName(
        GET_FUNCTION_NAME_CHECKED(UGTS_TimeLibrary, RegisterTimeTextBlock));
    if (!TestNotNull(TEXT("Time text registration is reflected"), RegisterFunction)) return false;
    TestTrue(TEXT("Time text registration is Blueprint callable"), RegisterFunction->HasAnyFunctionFlags(FUNC_BlueprintCallable));
    TestFalse(TEXT("Registration remains an execution node"), RegisterFunction->HasAnyFunctionFlags(FUNC_BlueprintPure));
    TestEqual(TEXT("Registration declares implicit world context"), RegisterFunction->GetMetaData(TEXT("WorldContext")), FString(TEXT("WorldContextObject")));
    TestNotNull(TEXT("Registration format is an FText parameter"), FindFProperty<FTextProperty>(RegisterFunction, TEXT("Format")));
    FObjectPropertyBase* WidgetParameter = FindFProperty<FObjectPropertyBase>(RegisterFunction, TEXT("TextBlock"));
    if (!TestNotNull(TEXT("Registration widget parameter is reflected"), WidgetParameter)) return false;
    TestEqual(TEXT("Registration accepts a TextBlock reference"), WidgetParameter->PropertyClass.Get(), UTextBlock::StaticClass());

    TStrongObjectPtr<UBlueprint> Blueprint(FKismetEditorUtilities::CreateBlueprint(
        UGTS_TimeTask::StaticClass(), GetTransientPackage(),
        MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), TEXT("GTS_TextRegistrationTest")),
        BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(), TEXT("GameTimeTextNodeTest")));
    if (!TestNotNull(TEXT("Create transient Blueprint for text node validation"), Blueprint.Get())) return false;
    Blueprint->SetFlags(RF_Transient);
    FEdGraphPinType TextBlockType;
    TextBlockType.PinCategory = UEdGraphSchema_K2::PC_Object;
    TextBlockType.PinSubCategoryObject = UTextBlock::StaticClass();
    if (!TestTrue(TEXT("Create a widget reference variable"),
        FBlueprintEditorUtils::AddMemberVariable(Blueprint.Get(), TEXT("ClockLabel"), TextBlockType))) return false;

    UEdGraph* Graph = Blueprint->UbergraphPages.IsEmpty() ? nullptr : Blueprint->UbergraphPages[0];
    if (!Graph)
    {
        Graph = FBlueprintEditorUtils::CreateNewGraph(Blueprint.Get(), TEXT("EventGraph"), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
        FBlueprintEditorUtils::AddUbergraphPage(Blueprint.Get(), Graph);
    }
    if (!TestNotNull(TEXT("Text node has a valid event graph"), Graph)) return false;

    FGraphNodeCreator<UK2Node_Event> EventCreator(*Graph);
    UK2Node_Event* Event = EventCreator.CreateNode();
    Event->EventReference.SetExternalMember(GET_FUNCTION_NAME_CHECKED(UGTS_TimeTask, OnTimeReached), UGTS_TimeTask::StaticClass());
    Event->bOverrideFunction = true;
    EventCreator.Finalize();

    FGraphNodeCreator<UK2Node_VariableGet> WidgetCreator(*Graph);
    UK2Node_VariableGet* Widget = WidgetCreator.CreateNode();
    Widget->VariableReference.SetSelfMember(TEXT("ClockLabel"));
    WidgetCreator.Finalize();

    auto AddCall = [Graph](UFunction* Function)
    {
        FGraphNodeCreator<UK2Node_CallFunction> Creator(*Graph);
        UK2Node_CallFunction* Node = Creator.CreateNode();
        Node->SetFromFunction(Function);
        Creator.Finalize();
        return Node;
    };
    UK2Node_CallFunction* Register = AddCall(RegisterFunction);
    const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
    if (!TestTrue(TEXT("Registration is executable from a Blueprint event"),
        Schema->TryCreateConnection(Event->FindPinChecked(UEdGraphSchema_K2::PN_Then), Register->GetExecPin()))
        || !TestTrue(TEXT("TextBlock variable connects without a cast"),
        Schema->TryCreateConnection(Widget->GetValuePin(), Register->FindPinChecked(TEXT("TextBlock"))))) return false;

    const FString ExpectedFormat(TEXT("{Year}-{Month}-{Day} {Hour}:{Minute}:{Second}"));
    auto CheckDefaults = [&](UK2Node_CallFunction* Node, const TCHAR* Label)
    {
        UEdGraphPin* FormatPin = Node->FindPin(TEXT("Format"));
        UEdGraphPin* WorldPin = Node->FindPin(TEXT("WorldContextObject"));
        if (!TestNotNull(*FString::Printf(TEXT("%s exposes a format pin"), Label), FormatPin)
            || !TestNotNull(*FString::Printf(TEXT("%s has an implicit context pin"), Label), WorldPin)) return;
        TestEqual(*FString::Printf(TEXT("%s format pin is Text"), Label), FormatPin->PinType.PinCategory, UEdGraphSchema_K2::PC_Text);
        TestEqual(*FString::Printf(TEXT("%s metadata initializes the FText literal"), Label), FormatPin->DefaultTextValue.ToString(), ExpectedFormat);
        TestTrue(*FString::Printf(TEXT("%s hides world context"), Label), WorldPin->bHidden);
        TestTrue(*FString::Printf(TEXT("%s needs no explicit context connection"), Label), WorldPin->LinkedTo.IsEmpty());
    };
    CheckDefaults(Register, TEXT("RegisterTimeTextBlock"));
    UFunction* FormatFunction = UGTS_TimeLibrary::StaticClass()->FindFunctionByName(
        GET_FUNCTION_NAME_CHECKED(UGTS_TimeLibrary, FormatCurrentTime));
    if (!TestNotNull(TEXT("Standalone formatter is reflected"), FormatFunction)) return false;
    CheckDefaults(AddCall(FormatFunction), TEXT("FormatCurrentTime"));

    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint.Get());
    FCompilerResultsLog Results;
    FKismetEditorUtilities::CompileBlueprint(Blueprint.Get(), EBlueprintCompileOptions::None, &Results);
    TestEqual(TEXT("Text registration Blueprint compiles without errors"), Results.NumErrors, 0);
    TestEqual(TEXT("Text registration needs no context/default-value warnings"), Results.NumWarnings, 0);
    TestTrue(TEXT("Text registration Blueprint has a usable generated class"), Blueprint->GeneratedClass != nullptr && Blueprint->Status != BS_Error);
    return true;
}
#endif
