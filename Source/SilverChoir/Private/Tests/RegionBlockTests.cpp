#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Algo/Reverse.h"
#include "Map/BaseMap/RegionBlockGeometry.h"
#include "Map/BaseMap/RegionBlock.h"
#include "Map/BaseMap/RegionNameWidget.h"
#include "Components/SplineComponent.h"
#include "Components/WidgetComponent.h"
#include "Components/EHBGeneratedMeshComponent.h"
#include "Engine/Engine.h"
#include "Camera/CameraActor.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ImageUtils.h"
#include "Serialization/BufferArchive.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "CoreGlobals.h"
#include "Tests/RegionClickTestActor.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegionGeometryTest,"SilverChoir.Regions.Geometry",EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FRegionGeometryTest::RunTest(const FString& Parameters)
{
	RegionBlockGeometry::FMesh Mesh; FText Error;
	TArray<FVector2D> Rectangle={{-500,-500},{500,-500},{500,500},{-500,500}};
	TestTrue(TEXT("Rectangle builds"),RegionBlockGeometry::Build(Rectangle,200,Mesh,Error));
	TestEqual(TEXT("Closed rectangular prism has 12 triangles"),Mesh.Indices.Num(),36);
	TestTrue(TEXT("Rectangle label centered on top"),Mesh.LabelPosition.Equals(FVector(0,0,200)));
	for (int32 I=0; I<Mesh.Indices.Num(); I+=3)
	{
		const FVector A=Mesh.Vertices[Mesh.Indices[I]], B=Mesh.Vertices[Mesh.Indices[I+1]], C=Mesh.Vertices[Mesh.Indices[I+2]];
		TestTrue(TEXT("Face winding follows UE clockwise convention"),FVector::DotProduct(FVector::CrossProduct(B-A,C-A),Mesh.Normals[Mesh.Indices[I]])<0);
	}
	TArray<FVector2D> Concave={{-500,-500},{500,-500},{500,0},{0,0},{0,500},{-500,500}};
	TestTrue(TEXT("Concave polygon builds"),RegionBlockGeometry::Build(Concave,150,Mesh,Error));
	TestEqual(TEXT("Concave prism caps and walls"),Mesh.Indices.Num(),60);
	double CapArea=0;
	for (int32 I=0; I<Mesh.Indices.Num(); I+=3)
	{
		if (Mesh.Normals[Mesh.Indices[I]].Z>.9) { const FVector A=Mesh.Vertices[Mesh.Indices[I]], B=Mesh.Vertices[Mesh.Indices[I+1]], C=Mesh.Vertices[Mesh.Indices[I+2]]; CapArea+=FVector::CrossProduct(B-A,C-A).Size()*.5; }
	}
	TestTrue(TEXT("L-shaped cap covers exactly three quarters of square"),FMath::IsNearlyEqual(CapArea,750000.,.01));
	Algo::Reverse(Concave);
	TestTrue(TEXT("Clockwise polygon builds"),RegionBlockGeometry::Build(Concave,150,Mesh,Error));
	TestFalse(TEXT("Crossing boundary rejected"),RegionBlockGeometry::Build({{0,0},{100,100},{0,100},{100,0}},100,Mesh,Error));
	TestFalse(TEXT("Line rejected"),RegionBlockGeometry::Build({{0,0},{100,0},{200,0}},100,Mesh,Error));
	TestFalse(TEXT("Zero height rejected"),RegionBlockGeometry::Build(Rectangle,0,Mesh,Error));
	const FVector2D First=Rectangle[0]; Rectangle.Add(First);
	TestTrue(TEXT("Duplicate closing vertex accepted"),RegionBlockGeometry::Build(Rectangle,200,Mesh,Error));
	return true;
}

class FRegionVisualCheck : public IAutomationLatentCommand
{
public:
	FRegionVisualCheck(FAutomationTestBase* T,ARegionBlock* R,ACameraActor* C,APlayerController* PC,AActor* Original)
		: Test(T),Region(R),Camera(C),Player(PC),OldView(Original),Started(FPlatformTime::Seconds()),StepStarted(Started)
	{
		bOldMouseOverEvents = PC->bEnableMouseOverEvents;
		PC->bEnableMouseOverEvents = false; // Synthetic hover events below must not race the real cursor.
	}
	~FRegionVisualCheck() override { Cleanup(); }
	bool Update() override
	{
		if (!Region.IsValid()) { Test->AddError(TEXT("Region disappeared")); return true; }
		const double Now=FPlatformTime::Seconds(), Elapsed=Now-StepStarted;
		if (Now-Started>35) { Test->AddError(FString::Printf(TEXT("Region redraw timed out at step %d"),Step)); return true; }
		auto* Widget=Region->NameWidget.Get();
		auto* Label=Cast<URegionNameWidget>(Widget->GetUserWidgetObject());
		const bool bRedrawFinished=GFrameCounter>StepFrame && !Widget->IsComponentTickEnabled();
		TArray<FColor> Pixels;
		switch (Step)
		{
		case 0:
			if (Elapsed<4 || !bRedrawFinished) return false;
			if (!ReadPixels(Pixels)) return true;
			NormalPixels=Pixels;
			Test->TestTrue(TEXT("Initial native label renders visible pixels"),HasText(Pixels));
			Test->TestTrue(TEXT("Native static label uses manual redraw"),Widget->GetManuallyRedraw());
			SaveLabelTexture();
			FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/RegionBlock/Normal.png"),false,false);
			Advance(); break;
		case 1:
			if (Elapsed<.2) return false;
			Test->TestFalse(TEXT("Unchanged native label has no component tick"),Widget->IsComponentTickEnabled());
			if (!ReadPixels(Pixels)) return true;
			Test->TestTrue(TEXT("Idle label retains the rendered pixels"),Pixels==NormalPixels);
			Region->VolumeMesh->OnBeginCursorOver.Broadcast(Region->VolumeMesh);
			Test->TestTrue(TEXT("Hover highlights region and text"),Region->bHovered && Label && Label->bIsHighlighted);
			Test->TestTrue(TEXT("Hover wakes one label render"),Widget->IsComponentTickEnabled());
			Advance(); break;
		case 2:
			if (!bRedrawFinished) return false;
			if (!ReadPixels(Pixels)) return true;
			Test->TestTrue(TEXT("Hover changes rendered text pixels"),HasText(Pixels) && Pixels!=NormalPixels);
			FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/RegionBlock/Hover.png"),false,false);
			Advance(); break;
		case 3:
			if (!bHoverExitRequested)
			{
				if (GFrameCounter<=StepFrame) return false;
				Region->VolumeMesh->OnEndCursorOver.Broadcast(Region->VolumeMesh);
				bHoverExitRequested=true; StepFrame=GFrameCounter; return false;
			}
			if (!bRedrawFinished) return false;
			if (!ReadPixels(Pixels)) return true;
			Test->TestTrue(TEXT("Hover exit restores normal rendered pixels"),Pixels==NormalPixels);
			Region->VolumeMesh->OnClicked.Broadcast(Region->VolumeMesh,EKeys::LeftMouseButton);
			Test->TestTrue(TEXT("Click triggers label highlight and redraw"),Label && Label->bIsHighlighted && Widget->IsComponentTickEnabled());
			if (auto* Material=Cast<UMaterialInstanceDynamic>(Region->VolumeMesh->GetMaterial(0)))
			{
				float Glow=0; Material->GetScalarParameterValue(TEXT("GlowStrength"),Glow);
				Test->TestTrue(TEXT("Click triggers volume visual pulse"),Glow>3.f);
			}
			Advance(); break;
		case 4:
			// The 0.15 s timer may already have queued the normal redraw after the pulse
			// was rendered. Inspect the texture before requiring the component to sleep.
			if (GFrameCounter<=StepFrame) return false;
			if (!ReadPixels(Pixels)) return true;
			if (Pixels==NormalPixels && Label && Label->bIsHighlighted) return false;
			Test->TestTrue(TEXT("Click pulse changes rendered text pixels"),HasText(Pixels) && Pixels!=NormalPixels);
			Advance(); break;
		case 5:
			if (!Label || Label->bIsHighlighted || !bRedrawFinished) return false;
			if (!ReadPixels(Pixels)) return true;
			Test->TestTrue(TEXT("Click timer restores normal rendered pixels and sleeps"),Pixels==NormalPixels);
			Region->SetRegionName(FText::FromString(TEXT("仓储区 / STORAGE")));
			Advance(); break;
		case 6:
			if (!bRedrawFinished) return false;
			if (!ReadPixels(Pixels)) return true;
			Test->TestTrue(TEXT("Rename updates visible rendered glyphs"),HasText(Pixels) && Pixels!=NormalPixels);
			RenamedPixels=Pixels;
			Region->HideRegion();
			Test->TestFalse(TEXT("Hidden region not hovered"),Region->bHovered);
			Test->TestFalse(TEXT("Hidden name widget"),Widget->IsVisible());
			Test->TestFalse(TEXT("Hidden name stops component tick"),Widget->IsComponentTickEnabled());
			Test->TestEqual(TEXT("Hidden mesh disables query collision"),Region->VolumeMesh->GetCollisionEnabled(),ECollisionEnabled::NoCollision);
			Region->VolumeMesh->OnBeginCursorOver.Broadcast(Region->VolumeMesh);
			Region->VolumeMesh->OnClicked.Broadcast(Region->VolumeMesh,EKeys::LeftMouseButton);
			Test->TestFalse(TEXT("Hidden region rejects hover and click highlight"),Region->bHovered || (Label && Label->bIsHighlighted));
			if (auto* Material=Cast<UMaterialInstanceDynamic>(Region->VolumeMesh->GetMaterial(0)))
			{
				float Glow=0; Material->GetScalarParameterValue(TEXT("GlowStrength"),Glow);
				Test->TestTrue(TEXT("Hidden region rejects volume click pulse"),FMath::IsNearlyEqual(Glow,1.2f));
			}
			Region->SetRegionName(FText::FromString(TEXT("重新开放 / REOPENED")));
			Test->TestFalse(TEXT("Rename while hidden does not wake component tick"),Widget->IsComponentTickEnabled());
			Advance(); break;
		case 7:
			if (Elapsed<.2) return false;
			if (!ReadPixels(Pixels)) return true;
			Test->TestTrue(TEXT("Hidden label does not redraw its old texture"),Pixels==RenamedPixels);
			Region->ShowRegion();
			Test->TestTrue(TEXT("Show restores region and query collision"),Region->IsRegionShown() && Region->VolumeMesh->GetCollisionEnabled()==ECollisionEnabled::QueryOnly);
			Test->TestTrue(TEXT("Show wakes a render of the latest text"),Widget->IsComponentTickEnabled());
			Advance(); break;
		case 8:
			if (!bRedrawFinished) return false;
			if (!ReadPixels(Pixels)) return true;
			Test->TestTrue(TEXT("Show renders the name changed while hidden"),HasText(Pixels) && Pixels!=RenamedPixels);
			Region->bContinuouslyUpdateName=true; Region->RebuildVolume();
			Advance(); break;
		case 9:
			if (Elapsed<.2) return false;
			Test->TestTrue(TEXT("Explicit continuous native label keeps ticking"),Widget->IsComponentTickEnabled() && !Widget->GetManuallyRedraw());
			if (!ReadPixels(Pixels)) return true;
			Test->TestTrue(TEXT("Continuous native label still renders text"),HasText(Pixels));
			Region->bContinuouslyUpdateName=false; Region->RebuildVolume();
			Advance(); break;
		case 10:
			if (!bRedrawFinished) return false;
			Test->TestTrue(TEXT("Disabling continuous override restores manual redraw"),Widget->GetManuallyRedraw());
			Region->NameWidgetClass=LoadClass<URegionNameWidget>(nullptr,TEXT("/Game/System/Map/BaseMap/UI/WBP_RegionName.WBP_RegionName_C"));
			if (!Test->TestNotNull(TEXT("Custom region name Blueprint"),Region->NameWidgetClass.Get())) return true;
			Region->RebuildVolume();
			Advance(); break;
		case 11:
			if (Elapsed<.3) return false;
			Test->TestTrue(TEXT("Custom name Blueprint retains continuous tick by default"),Widget->IsComponentTickEnabled() && !Widget->GetManuallyRedraw());
			if (!ReadPixels(Pixels)) return true;
			Test->TestTrue(TEXT("Custom Blueprint label renders visible pixels"),HasText(Pixels));
			Region->HideRegion();
			Test->TestFalse(TEXT("Hidden custom label also stops ticking"),Widget->IsComponentTickEnabled());
			Region->ShowRegion(); Advance(); break;
		case 12:
			if (Elapsed<.2) return false;
			Test->TestTrue(TEXT("Show resumes continuous custom label tick"),Widget->IsComponentTickEnabled());
			if (!ReadPixels(Pixels)) return true;
			Test->TestTrue(TEXT("Shown custom Blueprint label renders visible pixels"),HasText(Pixels));
			return true;
		}
		return false;
	}
private:
	void Advance() { ++Step; StepStarted=FPlatformTime::Seconds(); StepFrame=GFrameCounter; }
	bool ReadPixels(TArray<FColor>& Pixels)
	{
		auto* RT=Region->NameWidget->GetRenderTarget();
		if (!Test->TestNotNull(TEXT("Label render target exists"),RT)) return false;
		return Test->TestTrue(TEXT("Read actual label render target pixels"),RT->GameThread_GetRenderTargetResource()->ReadPixels(Pixels) && !Pixels.IsEmpty());
	}
	static bool HasText(const TArray<FColor>& Pixels)
	{
		return Pixels.ContainsByPredicate([](const FColor& P) { return P.A>16 && (P.R>16 || P.G>16 || P.B>16); });
	}
	void SaveLabelTexture()
	{
		const FString Directory=FPaths::ProjectSavedDir()/TEXT("Screenshots/RegionBlock");
		IFileManager::Get().MakeDirectory(*Directory,true);
		FBufferArchive Buffer;
		FImageUtils::ExportRenderTarget2DAsPNG(Region->NameWidget->GetRenderTarget(),Buffer);
		FFileHelper::SaveArrayToFile(Buffer,*(Directory/TEXT("LabelTexture.png")));
	}
	void Cleanup()
	{
		if (Player.IsValid())
		{
			Player->bEnableMouseOverEvents=bOldMouseOverEvents;
			if (OldView.IsValid()) Player->SetViewTarget(OldView.Get());
		}
		if (Region.IsValid()) Region->Destroy();
		if (Camera.IsValid()) Camera->Destroy();
	}
	FAutomationTestBase* Test;
	TWeakObjectPtr<ARegionBlock> Region;
	TWeakObjectPtr<ACameraActor> Camera;
	TWeakObjectPtr<APlayerController> Player;
	TWeakObjectPtr<AActor> OldView;
	double Started,StepStarted;
	uint64 StepFrame=0;
	bool bOldMouseOverEvents=false;
	bool bHoverExitRequested=false;
	TArray<FColor> NormalPixels,RenamedPixels;
	int32 Step=0;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegionRuntimeTest,"SilverChoir.Regions.Runtime",EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FRegionRuntimeTest::RunTest(const FString& Parameters)
{
	for (const auto& Context : GEngine->GetWorldContexts())
	{
		if (Context.WorldType!=EWorldType::Game || !Context.World()) { continue; }
		auto* World=Context.World(); auto* PC=World->GetFirstPlayerController();
		if (!PC) { continue; }
		UClass* Class=LoadClass<ARegionBlock>(nullptr,TEXT("/Game/System/Map/BaseMap/BP_RegionBlock.BP_RegionBlock_C"));
		if (!TestNotNull(TEXT("Region Blueprint asset"),Class)) { return false; }
		auto* Region=World->SpawnActor<ARegionBlock>(Class,FVector(0,0,10000),FRotator::ZeroRotator);
		if (!TestNotNull(TEXT("Region instance"),Region)) return false;
		// Exercise the native on-demand path even when the project Blueprint chooses a custom label.
		Region->NameWidgetClass=URegionNameWidget::StaticClass();
		Region->bContinuouslyUpdateName=false;
		Region->SetRegionName(FText::FromString(TEXT("指挥中心 / COMMAND")));
		Region->RectangleWidth=1000; Region->RectangleLength=700; Region->GenerateRectangle();
		TestTrue(TEXT("Rectangle generated closed spline"),Region->Boundary->IsClosedLoop() && Region->Boundary->GetNumberOfSplinePoints()==4);
		TestTrue(TEXT("Name faces upward"),Region->NameWidget->GetForwardVector().Equals(FVector::UpVector,.001));
		TestTrue(TEXT("Name defaults to 180-degree planar rotation"),Region->NameWidget->GetRightVector().Equals(-FVector::RightVector,.001));
		TestTrue(TEXT("Name material assigned"),Region->NameWidget->GetMaterial(0) && Region->NameWidget->GetMaterial(0)->GetName()==TEXT("M_3DText"));
		FHitResult Hit; FCollisionQueryParams Query; Query.bTraceComplex=true;
		TestTrue(TEXT("Top hit by visibility ray"),World->LineTraceSingleByChannel(Hit,FVector(0,0,11000),FVector(0,0,9900),ECC_Visibility,Query) && Hit.GetActor()==Region);
		Region->VolumeMesh->ResetMeshUpdateStats();
		Region->RebuildVolume();
		TestTrue(TEXT("EHB skips identical mesh submission"),Region->VolumeMesh->GetMeshStats().SkippedIdenticalSectionUpdateCount>0);
		for (int I=0; I<4; ++I) { Region->Boundary->SetSplinePointType(I,ESplinePointType::Curve,false); }
		Region->Boundary->UpdateSpline();
		TestTrue(TEXT("Curved closed spline builds"),Region->RebuildVolume());
		Region->Boundary->SetClosedLoop(false);
		TestFalse(TEXT("Open spline rejected"),Region->RebuildVolume());
		TestEqual(TEXT("Invalid spline clears collision"),Region->VolumeMesh->GetCollisionEnabled(),ECollisionEnabled::NoCollision);
		Region->Boundary->SetClosedLoop(true);
		Region->Boundary->ClearSplinePoints(false);
		for (FVector P : {FVector(-500,-500,0),FVector(500,-500,0),FVector(500,0,0),FVector(0,0,0),FVector(0,500,0),FVector(-500,500,0)}) { Region->Boundary->AddSplinePoint(P,ESplineCoordinateSpace::Local,false); }
		for (int I=0; I<6; ++I) { Region->Boundary->SetSplinePointType(I,ESplinePointType::Linear,false); }
		Region->Boundary->UpdateSpline(); TestTrue(TEXT("Concave volume generated"),Region->RebuildVolume());
		World->LineTraceSingleByChannel(Hit,FVector(250,250,11000),FVector(250,250,9900),ECC_Visibility,Query);
		TestTrue(TEXT("Concave notch has no phantom collision"),Hit.GetActor()!=Region);
		Region->HideRegion();
		World->LineTraceSingleByChannel(Hit,FVector(-250,-250,11000),FVector(-250,-250,9900),ECC_Visibility,Query);
		TestTrue(TEXT("Hidden region not hit by real ray"),Hit.GetActor()!=Region);
		Region->ShowRegion();
		auto* Camera=World->SpawnActor<ACameraActor>();
		const FVector Target=Region->GetActorLocation()+FVector(0,0,100), Location=Target+FVector(1200,-1600,1800);
		Camera->SetActorLocationAndRotation(Location,(Target-Location).Rotation());
		auto* OldView=PC->GetViewTarget(); PC->SetViewTarget(Camera);
		ADD_LATENT_AUTOMATION_COMMAND(FRegionVisualCheck(this,Region,Camera,PC,OldView)); return true;
	}
	AddError(TEXT("Requires game world")); return false;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegionClickOverrideTest,"SilverChoir.Regions.ClickOverride",EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FRegionClickOverrideTest::RunTest(const FString& Parameters)
{
	for (const auto& Context:GEngine->GetWorldContexts())
	{
		if (Context.WorldType!=EWorldType::Game || !Context.World()) continue;
		auto* Region=Context.World()->SpawnActor<ARegionClickTestActor>();
		if (!TestNotNull(TEXT("Click test region"),Region)) return false;
		auto* Function=Region->FindFunction(TEXT("ReceiveRegionClicked"));
		TestTrue(TEXT("Click hook can be overridden in Blueprint"),Function && Function->HasAnyFunctionFlags(FUNC_BlueprintEvent));
		Region->VolumeMesh->OnClicked.Broadcast(Region->VolumeMesh,EKeys::LeftMouseButton);
		TestEqual(TEXT("Left click invokes override once"),Region->ClickCount,1);
		Region->VolumeMesh->OnClicked.Broadcast(Region->VolumeMesh,EKeys::RightMouseButton);
		TestEqual(TEXT("Other buttons do not invoke override"),Region->ClickCount,1);
		Region->HideRegion();
		Region->VolumeMesh->OnClicked.Broadcast(Region->VolumeMesh,EKeys::LeftMouseButton);
		TestEqual(TEXT("Hidden region does not invoke override"),Region->ClickCount,1);
		Region->ShowRegion();
		Region->VolumeMesh->OnClicked.Broadcast(Region->VolumeMesh,EKeys::LeftMouseButton);
		TestEqual(TEXT("Showing region restores click override"),Region->ClickCount,2);
		Region->Destroy(); return true;
	}
	AddError(TEXT("Requires game world")); return false;
}
#endif

