#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "HAL/PlatformProcess.h"
#include "H5UI_RuntimeView.h"
#include "H5UI_View.h"
#include "SH5UI_View.h"
#include "Interfaces/IPluginManager.h"
#include "Modules/ModuleManager.h"
#include "RmlUi/Core/Animation.h"
#include "RmlUi/Core/PropertyDictionary.h"
#include "RmlUi/Core/StyleSheetSpecification.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FH5UI_StandardCssAnimationTest,
	"H5UIPlugin.Runtime.StandardCssAnimations",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FH5UI_StandardCssAnimationTest::RunTest(const FString& Parameters)
{
	TestFalse(TEXT("CEF is off by default"), GetDefault<UH5UI_View>()->bEnableBrowserSubviews);
	const auto Plugin = IPluginManager::Get().FindPlugin(TEXT("H5UIPlugin"));
	TestTrue(TEXT("H5UI plugin descriptor exists"), Plugin.IsValid());
	if (Plugin)
	{
		TestTrue(TEXT("CEF is retained as a lazily loaded module"), Plugin->GetDescriptor().Modules.ContainsByPredicate(
			[](const FModuleDescriptor& Module) { return Module.Name == TEXT("H5UIPluginCEF") && Module.LoadingPhase == ELoadingPhase::None; }));
	}
	const TSharedRef<SH5UI_View> NativeSlate = SNew(SH5UI_View);
	TestTrue(TEXT("Native page can contain an inactive iframe without loading CEF"),
		NativeSlate->LoadString(TEXT("<html><body><iframe src='about:blank' style='width:100px;height:100px;'></iframe></body></html>"), TEXT("coui://uiresources/")));
	NativeSlate->Tick(FGeometry::MakeRoot(FVector2f(1600, 900), FSlateLayoutTransform()), FPlatformTime::Seconds(), 0.016f);
	TestFalse(TEXT("H5UI CEF module is not loaded"), FModuleManager::Get().IsModuleLoaded(TEXT("H5UIPluginCEF")));
	NativeSlate->Close();
	FH5UI_RuntimeView View([] {}, [this](const FString& Error) { AddError(Error); }, [](const FH5UI_Event&) {});
	View.SetJavaScriptEnabled(false);
	const FString Html = TEXT(
		"<html><head><style>"
		"@keyframes slide { 0% { left: 0px; } 100% { left: 1000px; } }"
		"html,body { margin: 0; width: 100%; height: 100%; }"
		"div { position: absolute; width: 10px; height: 10px; }"
		"#forward { animation: slide 3000ms linear infinite normal running; }"
		"#reverse { animation: slide 3000ms linear infinite reverse; }"
		"</style></head><body><div id='forward'></div><div id='reverse'></div></body></html>");
	if (!TestTrue(TEXT("Standard CSS animations load without JS"), View.LoadString(Html, TEXT("coui://uiresources/")))) { return false; }
	View.Update({1200, 900}, FPlatformTime::Seconds());
	const float ForwardBefore = View.GetElementBorderPositionForTesting(TEXT("forward")).X;
	const float ReverseBefore = View.GetElementBorderPositionForTesting(TEXT("reverse")).X;
	TestTrue(TEXT("Reverse begins at the last keyframe"), ReverseBefore > 900.0f && ForwardBefore < 100.0f);
	FPlatformProcess::Sleep(0.15f);
	View.Update({1200, 900}, FPlatformTime::Seconds());
	TestTrue(TEXT("Normal animation progresses towards the last keyframe"),
		View.GetElementBorderPositionForTesting(TEXT("forward")).X > ForwardBefore + 1.0f);
	TestTrue(TEXT("Reverse animation progresses towards the first keyframe"),
		View.GetElementBorderPositionForTesting(TEXT("reverse")).X < ReverseBefore - 1.0f);

	for (const char* Value : {"orbit 24000ms linear infinite", "24s linear infinite orbit", "orbit 24s linear infinite normal running"})
	{
		Rml::PropertyDictionary Properties;
		if (TestTrue(TEXT("Standard shorthand accepts animation name in either position"),
			Rml::StyleSheetSpecification::ParsePropertyDeclaration(Properties, "animation", Value)))
		{
			const auto& List = Properties.GetProperty(Rml::PropertyId::Animation)->Get<Rml::AnimationList>();
			if (!TestEqual(TEXT("One animation parsed"), static_cast<int32>(List.size()), 1)) { continue; }
			TestTrue(TEXT("Timing keywords do not overwrite the animation name"), List[0].name == "orbit");
			TestEqual(TEXT("Milliseconds convert to seconds"), List[0].duration, 24.0f);
			TestTrue(TEXT("linear uses a linear tween"), List[0].tween == Rml::Tween(Rml::Tween::Linear, Rml::Tween::InOut));
		}
	}

	Rml::PropertyDictionary ReverseProperties;
	if (TestTrue(TEXT("Alternate reverse and negative millisecond delay parse"),
		Rml::StyleSheetSpecification::ParsePropertyDeclaration(ReverseProperties, "animation", "orbit 2s linear -250ms infinite alternate-reverse")))
	{
		const Rml::Property* Parsed = ReverseProperties.GetProperty(Rml::PropertyId::Animation);
		const auto& List = Parsed->Get<Rml::AnimationList>();
		if (TestEqual(TEXT("One reverse animation parsed"), static_cast<int32>(List.size()), 1))
		{
			TestTrue(TEXT("Both direction flags are retained"), List[0].alternate && List[0].reverse);
			TestEqual(TEXT("Negative delay retains sign and unit"), List[0].delay, -0.25f);
			Rml::PropertyDictionary RoundTrip;
			if (TestTrue(TEXT("Animation serialization is parseable"),
				Rml::StyleSheetSpecification::ParsePropertyDeclaration(RoundTrip, "animation", Parsed->ToString())))
			{
				TestTrue(TEXT("Serialization preserves direction and negative delay"),
					RoundTrip.GetProperty(Rml::PropertyId::Animation)->Get<Rml::AnimationList>() == List);
			}
		}
	}

	Rml::PropertyDictionary TransitionProperties;
	if (TestTrue(TEXT("Transitions also accept linear and milliseconds"),
		Rml::StyleSheetSpecification::ParsePropertyDeclaration(TransitionProperties, "transition", "opacity 250ms linear 50ms")))
	{
		const auto& List = TransitionProperties.GetProperty(Rml::PropertyId::Transition)->Get<Rml::TransitionList>();
		if (TestEqual(TEXT("One transition parsed"), static_cast<int32>(List.transitions.size()), 1))
		{
			TestEqual(TEXT("Transition duration is seconds"), List.transitions[0].duration, 0.25f);
			TestTrue(TEXT("Transition delay is seconds"), FMath::IsNearlyEqual(List.transitions[0].delay, 0.05f));
		}
	}
	View.Close();
	return true;
}
#endif
