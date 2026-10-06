#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Data/Units/UnitStructs.h"
#include "Object/Unit/UnitPawnBase.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "SubSystem/PlayerUnitSubSystem/PlayerUnitManagerBase.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnitSelectionDataTest,"SilverChoir.Units.Selection.SharedData",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FUnitSelectionDataTest::RunTest(const FString& Parameters)
{
    FUnitData Unit;
    Unit.UnitId=FGuid::NewGuid();
    int32 SelectedCount=0, DeselectedCount=0, DataChangedCount=0;
    Unit.OnDataChanged.AddLambda([&](FGuid){ ++DataChangedCount; });
    Unit.OnSelected.AddLambda([&](FGuid Id)
    {
        ++SelectedCount;
        TestEqual(TEXT("Selection identifies the shared record"),Id,Unit.UnitId);
        TestTrue(TEXT("Listeners see selected state before notification"),Unit.IsSelected());
        TestFalse(TEXT("Nested selection change is rejected"),Unit.SetSelected(false));
    });
    Unit.OnDeselected.AddLambda([&](FGuid Id)
    {
        ++DeselectedCount;
        TestEqual(TEXT("Deselection identifies the shared record"),Id,Unit.UnitId);
        TestFalse(TEXT("Listeners see deselected state before notification"),Unit.IsSelected());
        TestFalse(TEXT("Nested deselection change is rejected"),Unit.SetSelected(true));
    });
    TestFalse(TEXT("Records start deselected"),Unit.IsSelected());
    TestFalse(TEXT("Repeated deselection does not notify"),Unit.SetSelected(false));
    TestTrue(TEXT("Selection changes state"),Unit.SetSelected(true));
    TestFalse(TEXT("Repeated selection does not notify"),Unit.SetSelected(true));
    TestTrue(TEXT("Deselection changes state"),Unit.SetSelected(false));
    TestEqual(TEXT("One selected event"),SelectedCount,1);
    TestEqual(TEXT("One deselected event"),DeselectedCount,1);
    TestEqual(TEXT("Transient selection avoids rebuilding business-data consumers"),DataChangedCount,0);

    Unit.SetSelected(true);
    FUnitData Copy(Unit);
    TestFalse(TEXT("Copy constructors do not copy selection"),Copy.IsSelected());
    TestFalse(TEXT("Copy constructors do not copy selected listeners"),Copy.OnSelected.IsBound());
    TestFalse(TEXT("Copy constructors do not copy deselected listeners"),Copy.OnDeselected.IsBound());
    Copy.RuntimeData.CurrentHealth=65.f;
    Unit=Copy;
    TestTrue(TEXT("Assignment preserves destination runtime selection"),Unit.IsSelected());
    TestTrue(TEXT("Assignment preserves destination listeners"),Unit.OnSelected.IsBound() && Unit.OnDeselected.IsBound());
    TestEqual(TEXT("Assignment still copies business data"),Unit.RuntimeData.CurrentHealth,65.f);
    Unit.SetSelected(false);
    TestEqual(TEXT("Original listeners remain functional after assignment"),DeselectedCount,2);

    Unit.SetSelected(true);
    TArray<uint8> Bytes;
    {
        FMemoryWriter Writer(Bytes,true);
        FObjectAndNameAsStringProxyArchive Archive(Writer,false);
        Archive.ArIsSaveGame=true;
        FUnitData::StaticStruct()->SerializeItem(Archive,&Unit,nullptr);
    }
    FUnitData Restored;
    {
        FMemoryReader Reader(Bytes,true);
        FObjectAndNameAsStringProxyArchive Archive(Reader,false);
        Archive.ArIsSaveGame=true;
        FUnitData::StaticStruct()->SerializeItem(Archive,&Restored,nullptr);
    }
    TestEqual(TEXT("Save game retains business identity"),Restored.UnitId,Unit.UnitId);
    TestFalse(TEXT("Save game does not retain selection"),Restored.IsSelected());
    TestFalse(TEXT("Save game does not restore listeners"),Restored.OnSelected.IsBound() || Restored.OnDeselected.IsBound());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnitSelectionReferenceTest,"SilverChoir.Units.Selection.BlueprintReference",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FUnitSelectionReferenceTest::RunTest(const FString& Parameters)
{
    TSharedPtr<FUnitData> First=MakeShared<FUnitData>();
    TSharedPtr<FUnitData> Second=MakeShared<FUnitData>();
    First->UnitId=FGuid::NewGuid(); Second->UnitId=FGuid::NewGuid();
    TStrongObjectPtr<UUnitDataReference> Reference(NewObject<UUnitDataReference>());
    Reference->Initialize(First);
    TestTrue(TEXT("Reference binds selected and deselected notifications"),
        First->OnSelected.IsBoundToObject(Reference.Get()) && First->OnDeselected.IsBoundToObject(Reference.Get()));
    TestTrue(TEXT("Blueprint reference changes the canonical state"),Reference->SetSelected(true));
    TestTrue(TEXT("All consumers see selection"),First->IsSelected() && Reference->IsSelected());
    TestFalse(TEXT("Blueprint snapshots do not retain runtime selection"),Reference->GetSnapshot().IsSelected());
    Reference->Initialize(Second);
    TestFalse(TEXT("Rebinding removes selected callback on old data"),First->OnSelected.IsBoundToObject(Reference.Get()));
    TestFalse(TEXT("Rebinding removes deselected callback on old data"),First->OnDeselected.IsBoundToObject(Reference.Get()));
    TestTrue(TEXT("Rebinding preserves old data selection for other consumers"),First->IsSelected());
    TestFalse(TEXT("Reference reads new data selection"),Reference->IsSelected());
    Reference->SetSelected(true);
    Reference->Initialize(nullptr);
    TestFalse(TEXT("Released reference cannot mutate stale data"),Reference->SetSelected(false));
    TestTrue(TEXT("Releasing reference preserves shared selection"),Second->IsSelected());
    TestFalse(TEXT("Release removes all native subscriptions"),
        Second->OnDataChanged.IsBoundToObject(Reference.Get()) || Second->OnSelected.IsBoundToObject(Reference.Get())
        || Second->OnDeselected.IsBoundToObject(Reference.Get()));
    Reference->Initialize(Second);
    Reference.Reset();
    CollectGarbage(RF_NoFlags);
    TestFalse(TEXT("Destroying bridge removes selected subscription"),Second->OnSelected.IsBound());
    TestFalse(TEXT("Destroying bridge removes deselected subscription"),Second->OnDeselected.IsBound());
    TestTrue(TEXT("Destroying bridge does not deselect another consumer"),Second->IsSelected());

    for (const TCHAR* Event : {TEXT("OnSelected"),TEXT("OnDeselected")})
    {
        const FMulticastDelegateProperty* Property=FindFProperty<FMulticastDelegateProperty>(UUnitDataReference::StaticClass(),Event);
        if (TestNotNull(TEXT("Selection event is reflected"),Property))
            TestTrue(TEXT("Selection event is bindable in Blueprint"),Property->HasAnyPropertyFlags(CPF_BlueprintAssignable));
    }
    for (const TCHAR* Event : {TEXT("OnUnitSelected"),TEXT("OnUnitDeselected")})
    {
        const UFunction* Function=AUnitPawnBase::StaticClass()->FindFunctionByName(Event);
        if (TestNotNull(TEXT("Pawn selection event exists"),Function))
        {
            TestTrue(TEXT("Pawn event is overridable in Blueprint"),Function->HasAnyFunctionFlags(FUNC_BlueprintEvent));
            TestFalse(TEXT("Selection visuals are left to Blueprint"),Function->HasAnyFunctionFlags(FUNC_Native));
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnitSelectionReloadTest,"SilverChoir.Units.Selection.ReloadPreservesSelection",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FUnitSelectionReloadTest::RunTest(const FString& Parameters)
{
    FUnitData Record; Record.UnitId=FGuid::NewGuid();
    TStrongObjectPtr<UPlayerUnitManagerBase> Manager(NewObject<UPlayerUnitManagerBase>());
    FText Error;
    if (!TestTrue(TEXT("Load canonical record"),Manager->LoadUnitData({Record},Error))) return false;
    const auto Shared=Manager->GetUnitDataShared(Record.UnitId);
    Shared->SetSelected(true);
    int32 DeselectedCount=0;
    const FDelegateHandle Handle=Shared->OnDeselected.AddLambda([&](FGuid){++DeselectedCount;});
    Record.RuntimeData.CurrentHealth=12.f;
    TestTrue(TEXT("Reload succeeds"),Manager->LoadUnitData({Record},Error));
    TestTrue(TEXT("Reload preserves canonical allocation"),Shared==Manager->GetUnitDataShared(Record.UnitId));
    TestTrue(TEXT("Reload preserves current selection"),Shared->IsSelected());
    TestEqual(TEXT("Reload updates business fields"),Shared->RuntimeData.CurrentHealth,12.f);
    TestEqual(TEXT("Reload does not emit selection events"),DeselectedCount,0);
    Shared->SetSelected(false);
    TestEqual(TEXT("Reload preserves original selection listeners"),DeselectedCount,1);
    Shared->OnDeselected.Remove(Handle);
    return true;
}
#endif
