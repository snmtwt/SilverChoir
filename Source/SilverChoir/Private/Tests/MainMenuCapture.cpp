// Editor-only visual verification. Not compiled into packaged games.
#if WITH_EDITOR
#include "Containers/Ticker.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "UIBasic/MenuLoadingWidget.h"

static FAutoConsoleCommand CaptureMenuCommand(TEXT("SilverChoir.CaptureMenu"),
	TEXT("Save two time-separated menu screenshots and exit this preview process."),
	FConsoleCommandDelegate::CreateLambda([]
	{
		// Never terminate an interactive editor session through this QA command.
		if (!FParse::Param(FCommandLine::Get(), TEXT("game"))) { return; }
		if (FParse::Param(FCommandLine::Get(), TEXT("MenuCaptureLoading")))
		{
			for (const auto& Context : GEngine->GetWorldContexts())
			{
				if (Context.WorldType != EWorldType::Game || !Context.World()) { continue; }
				UClass* Class = LoadClass<UMenuLoadingWidget>(nullptr, TEXT("/Game/System/UIBasic/WBP_MenuLoading.WBP_MenuLoading_C"));
				if (Class)
				{
					auto* Widget = CreateWidget<UMenuLoadingWidget>(Context.World(), Class);
					Widget->AddToViewport(10000);
					FMTS_MapTransitionPayload Payload;
					Payload.Progress = .42f;
					Payload.LoadingContent = FText::FromString(TEXT("正在加载基地资源…"));
					Widget->ApplyTransitionPayload(Payload);
				}
			}
		}
		FString Tag = TEXT("Menu");
		FParse::Value(FCommandLine::Get(), TEXT("MenuCaptureTag="), Tag);
		Tag = FPaths::MakeValidFileName(Tag);
		const bool bSequence = FParse::Param(FCommandLine::Get(), TEXT("MenuCaptureSequence"));
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
			[Tag, bSequence, Elapsed = 0.0f, Step = 0](float Delta) mutable
			{
				Elapsed += Delta;
				if (bSequence)
				{
					if (Step < 60 && Elapsed >= 0.5f + Step * 0.1f)
					{
						FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Screenshots/MenuQA") /
							FString::Printf(TEXT("%s_%03d.png"), *Tag, Step++), true, false);
					}
					else if (Step >= 60 && Elapsed > 6.7f) { FPlatformMisc::RequestExit(false); return false; }
					return true;
				}
				if (Step == 0 && Elapsed > 1.0f)
				{
					FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Screenshots/MenuQA") / (Tag + TEXT("_1.png")), true, false);
					++Step;
				}
				else if (Step == 1 && Elapsed > 3.0f)
				{
					FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Screenshots/MenuQA") / (Tag + TEXT("_2.png")), true, false);
					++Step;
				}
				else if (Step == 2 && Elapsed > 4.0f) { FPlatformMisc::RequestExit(false); return false; }
				return true;
			}));
	}));
#endif
