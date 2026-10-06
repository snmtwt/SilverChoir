#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Tools/SIS_ItemModelCapturer.h"
#include "Tools/SIS_ItemModelCapturerSubsystem.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Components/MeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "MaterialShared.h"
#include "SceneInterface.h"

class FWaitForItemCaptures : public IAutomationLatentCommand
{
    FAutomationTestBase* Test;
    TWeakObjectPtr<UMaterialInstanceDynamic> First,Second;
    double Start=FPlatformTime::Seconds();
public:
    FWaitForItemCaptures(FAutomationTestBase* T,UMaterialInstanceDynamic* A,UMaterialInstanceDynamic* B):Test(T),First(A),Second(B) {}
    bool Update() override
    {
        auto* A=First.Get();auto* B=Second.Get();
        auto* ActorA=A?Cast<ASIS_ItemModelCapturer>(A->GetOuter()):nullptr;
        auto* ActorB=B?Cast<ASIS_ItemModelCapturer>(B->GetOuter()):nullptr;
        if (ActorA && ActorB && !ActorA->bIsInUse && !ActorB->bIsInUse)
        {
            Test->TestTrue(TEXT("Concurrent requests retain separate render targets"),A!=B);
            auto* RT=Cast<UTextureRenderTarget2D>(A->K2_GetTextureParameterValue(TEXT("InputTexture")));
            if (Test->TestNotNull(TEXT("Capture render target"),RT))
            {
                TArray<FColor> Pixels;RT->GameThread_GetRenderTargetResource()->ReadPixels(Pixels);
                bool NonBlack=false;for(auto& Pixel:Pixels){NonBlack|=Pixel.R>8 || Pixel.G>8 || Pixel.B>8;Pixel.A=255;}
                Test->TestTrue(TEXT("Captured visible pixels"),NonBlack);
                TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(RT->SizeX,RT->SizeY,Pixels,PNG);
                const FString Dir=FPaths::ProjectSavedDir()/TEXT("Screenshots/ItemCapture");
                IFileManager::Get().MakeDirectory(*Dir,true);
                FFileHelper::SaveArrayToFile(PNG,*(Dir/TEXT("ShirtCapture.png")));
            }
            return true;
        }
        if(FPlatformTime::Seconds()-Start>90){Test->AddError(TEXT("Capture readiness timed out; see SIS resource-wait reason"));return true;}
        return false;
    }
};
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FItemCaptureReadinessTest,"SilverChoir.Inventory.CaptureReadiness",EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FItemCaptureReadinessTest::RunTest(const FString& Parameters)
{
    UWorld* World=nullptr;for(auto& C:GEngine->GetWorldContexts())if(C.WorldType==EWorldType::Game){World=C.World();break;}
    if(!TestNotNull(TEXT("Game world"),World))return false;
    auto* System=World->GetSubsystem<USIS_ItemModelCapturerSubsystem>();
    struct { ASIS_ItemModelCapturer* ReturnValue=nullptr; } GetParams;
    System->ProcessEvent(System->FindFunctionChecked(TEXT("GetStaticCapturer")),&GetParams);
    auto* Capturer=GetParams.ReturnValue;
    if(!TestNotNull(TEXT("Capturer"),Capturer))return false;
    FSIS_ItemTemplate Item;Item.ItemActorClass=LoadClass<AActor>(nullptr,TEXT("/Game/System/Object/Items/BP_衬衣.BP_衬衣_C"));
    if(!TestNotNull(TEXT("Actual shirt blueprint"),Item.ItemActorClass.Get()))return false;
    Item.ImageConfig.bUseSceneCapture=true;
    FSIS_MaterialCacheKey A;A.BaseTemplateGuid=Item.TemplateGuid;
    auto* Material=Capturer->CreateStaticCaptureMaterial(Item,{},false,{256,256},true);
    if(!TestNotNull(TEXT("Pending first capture material"),Material))return false;
    for(auto WeakPrim:Capturer->SceneCaptureComponent->ShowOnlyComponents)
        if(auto* Mesh=Cast<UMeshComponent>(WeakPrim.Get())) for(auto* Mat:Mesh->GetMaterials()) if(Mat)
        {
            auto* R=Mat->GetMaterialResource(World->Scene->GetShaderPlatform());
            UE_LOG(LogTemp,Display,TEXT("CAPTURE_DIAGNOSTIC %s platform=%d resource=%d shaderMap=%d complete=%d finished=%d"),*Mat->GetPathName(),int32(World->Scene->GetShaderPlatform()),R!=nullptr,R && R->GetGameThreadShaderMap()!=nullptr,R && R->IsGameThreadShaderMapComplete(),R && R->IsCompilationFinished());
        }
    TestTrue(TEXT("Capture remains pending in creation frame"),Capturer->bIsInUse);
    Item.TemplateGuid=FGuid::NewGuid();FSIS_MaterialCacheKey B;B.BaseTemplateGuid=Item.TemplateGuid;
    auto* Other=Capturer->CreateStaticCaptureMaterial(Item,{},false,{256,256},true);
    TestNotNull(TEXT("Concurrent pending material"),Other);
    TestTrue(TEXT("First request is not overwritten"),Material!=Other);
    ADD_LATENT_AUTOMATION_COMMAND(FWaitForItemCaptures(this,Material,Other));
    return true;
}
#endif
