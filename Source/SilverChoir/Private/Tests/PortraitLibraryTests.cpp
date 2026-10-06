#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "UIBasic/PortraitLibrary.h"
#include "Engine/Texture2D.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Components/Image.h"
#include "Map/BaseMap/SceneUI/PersonnelPreparationRoom/Components/PersonnelPortraitWidget.h"
#include "Map/BaseMap/SceneUI/PersonnelPreparationRoom/Components/PersonnelListEntryWidget.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPortraitFormatsTest,"SilverChoir.UI.PortraitFormats",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FPortraitFormatsTest::RunTest(const FString& Parameters)
{
    FUnitProfile Profile;Profile.PortraitTexture=UTexture2D::CreateTransient(180,320);
    auto Full=UPortraitLibrary::GetFullPortrait(Profile);auto Square=UPortraitLibrary::GetSquarePortrait(Profile);
    const FBox2f FullUV=Full.GetUVRegion(), UV=Square.GetUVRegion();
    TestTrue(TEXT("Full portrait keeps all pixels and original aspect"),FVector2f(Full.GetImageSize())==FVector2f(180,320)&&!FullUV.bIsValid);
    TestTrue(TEXT("Square view reuses source texture"),Square.GetResourceObject()==Profile.PortraitTexture.Get());
    TestTrue(TEXT("Square brush has square dimensions"),Square.GetImageSize().X==Square.GetImageSize().Y);
    TestTrue(TEXT("Face crop is valid"),UV.bIsValid!=0);
    TestTrue(TEXT("Crop preserves square pixels"),FMath::IsNearlyEqual(UV.GetSize().X*180,UV.GetSize().Y*320));
    TestTrue(TEXT("Head-biased default avoids cutting off top of head"),UV.Min.Y==0.f);
    Profile.PortraitCropFocus=FVector2D(.5,.25);Profile.PortraitCropScale=.85f;
    const FBox2f Focused=UPortraitLibrary::GetSquarePortrait(Profile).GetUVRegion();
    TestTrue(TEXT("Authored crop scale retains a pixel-square face view"),FMath::IsNearlyEqual(Focused.GetSize().X*180,Focused.GetSize().Y*320,.001f));
    TestTrue(TEXT("Face crop honors authored short-edge fraction"),FMath::IsNearlyEqual(Focused.GetSize().X,.85f));
    Profile.PortraitCropFocus=FVector2D(8,-4);const FBox2f Clamped=UPortraitLibrary::GetSquarePortrait(Profile).GetUVRegion();
    TestTrue(TEXT("Out-of-range focal point cannot sample outside the texture"),Clamped.Min.X>=0&&Clamped.Min.Y>=0&&Clamped.Max.X<=1&&Clamped.Max.Y<=1);
    Profile.SquarePortraitTexture=UTexture2D::CreateTransient(96,96);TestTrue(TEXT("Optional authored square override is used"),UPortraitLibrary::GetSquarePortrait(Profile).GetResourceObject()==Profile.SquarePortraitTexture.Get());
    TestTrue(TEXT("Missing portrait produces no stale image"),UPortraitLibrary::GetFullPortrait(FUnitProfile()).GetResourceObject()==nullptr);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPortraitAssetsTest,"SilverChoir.UI.PortraitAssets",EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FPortraitAssetsTest::RunTest(const FString& Parameters)
{
    auto* Table=LoadObject<UDataTable>(nullptr,TEXT("/Game/System/SubSystem/PlayerUnitSubSystem/DT_UnitTemplates"));
    if(!TestNotNull(TEXT("Unit portrait templates"),Table))return false;
    UWorld* World=nullptr;for(const auto& Context:GEngine->GetWorldContexts())if(Context.WorldType==EWorldType::Game){World=Context.World();break;}
    if(!TestNotNull(TEXT("Game world for portrait creation"),World))return false;
    auto* Class=LoadClass<UPersonnelPortraitWidget>(nullptr,TEXT("/Game/System/Map/BaseMap/UI/SceneUI/PersonnelPreparationRoom/Components/WBP_PersonnelPortrait.WBP_PersonnelPortrait_C"));
    auto* RowClass=LoadClass<UPersonnelListEntryWidget>(nullptr,TEXT("/Game/System/Map/BaseMap/UI/SceneUI/PersonnelPreparationRoom/Components/WBP_PersonnelListEntry.WBP_PersonnelListEntry_C"));
    for(const auto& Pair:Table->GetRowMap())
    {
        const auto& Profile=reinterpret_cast<const FUnitTemplate*>(Pair.Value)->Profile;
        if(!TestNotNull(TEXT("Every unit has portrait artwork"),Profile.PortraitTexture.Get()))continue;
        const auto Size=Profile.PortraitTexture->GetImportedSize();
        TestTrue(TEXT("New source artwork is 3:4"),Size.Y>Size.X && FMath::Abs(double(Size.X)/Size.Y-3./4.)<.001);
        TestEqual(TEXT("Imported portrait templates retain calibrated face crop scale"),Profile.PortraitCropScale,1.f);
        TestTrue(TEXT("Imported portrait templates retain calibrated face focus"),Profile.PortraitCropFocus.Equals(FVector2D(.5,.4),.0001));
        const FBox2f UV=UPortraitLibrary::GetSquarePortrait(Profile).GetUVRegion();
        TestTrue(TEXT("Square crop retains authored complete head and chin"),UV.IsInside(FVector2f(.12f,.04f))&&UV.IsInside(FVector2f(.88f,.67f)));
        auto* Widget=CreateWidget<UPersonnelPortraitWidget>(World->GetFirstPlayerController(),Class);
        Widget->SetUnitPortrait(Profile);Widget->TakeWidget();
        auto* Picture=Cast<UImage>(Widget->GetWidgetFromName(TEXT("PortraitImage")));
        if(TestNotNull(TEXT("Shared avatar image"),Picture))
        {
            const FBox2f Actual=Picture->GetBrush().GetUVRegion();
            TestTrue(TEXT("Constructing a row preserves the profile face crop"),Actual.Min.Equals(UV.Min,.0001f)&&Actual.Max.Equals(UV.Max,.0001f));
        }
        auto* Row=CreateWidget<UPersonnelListEntryWidget>(World->GetFirstPlayerController(),RowClass);
        auto Data=MakeShared<FUnitData>();Data->Profile=Profile;Data->RuntimeData.TileId=TEXT("Base");
        Row->SetUnitData(Data,TEXT("Base"));Row->TakeWidget();Row->SynchronizeProperties();
        auto* ListPortrait=Cast<UPersonnelPortraitWidget>(Row->GetWidgetFromName(TEXT("ListPortrait")));
        if(TestNotNull(TEXT("Roster avatar"),ListPortrait))
        {
            TestTrue(TEXT("Roster uses square portrait format"),ListPortrait->PortraitFormat==EPortraitFormat::Square);
            auto* ListImage=Cast<UImage>(ListPortrait->GetWidgetFromName(TEXT("PortraitImage")));
            if(TestNotNull(TEXT("Roster portrait image"),ListImage))
            {
                const FBox2f Actual=ListImage->GetBrush().GetUVRegion();
                TestTrue(TEXT("Roster refresh preserves the calibrated face crop"),Actual.Min.Equals(UV.Min,.0001f)&&Actual.Max.Equals(UV.Max,.0001f));
            }
        }
        Row->ReleaseUnitData();
    }
    return true;
}
#endif
