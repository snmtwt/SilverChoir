#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "H5UI_RuntimeView.h"
#include "RmlUi/Core/ElementDocument.h"
#include "RmlUi/Core/StringUtilities.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FH5UI_BindingSynchronizationPerformanceTest,
	"H5UIPlugin.Runtime.BindingSynchronizationPerformance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FH5UI_BindingSynchronizationPerformanceTest::RunTest(const FString& Parameters)
{
	FH5UI_RuntimeView View([] {}, [this](const FString& Error) { AddError(Error); }, [](const FH5UI_Event&) {});
	View.SetJavaScriptEnabled(false);
	if (!TestTrue(TEXT("Binding fixture loads"), View.LoadString(
		TEXT("<html><head></head><body><div id='label' data-bind='name'>placeholder</div>")
		TEXT("<input id='value' data-bind-value='count' value='old'/><div id='host'></div></body></html>"),
		TEXT("coui://uiresources/binding-performance.html")))) return false;
	View.SetData(TEXT("name"), TEXT("Pilot <A> & \"B\""));
	View.SetData(TEXT("count"), TEXT("7"));
	View.SynchronizeModels();
	Rml::Element* Label = View.Document->GetElementById("label");
	Rml::Element* Input = View.Document->GetElementById("value");
	Rml::Element* Host = View.Document->GetElementById("host");
	if (!TestNotNull(TEXT("Label exists"), Label) || !TestNotNull(TEXT("Input exists"), Input)
		|| !TestNotNull(TEXT("Dynamic host exists"), Host)) return false;
	const Rml::String Expected = Rml::StringUtilities::EncodeRml("Pilot <A> & \"B\"");
	TestTrue(TEXT("Bindings preserve literal text and escape markup"), Label->GetInnerRML() == Expected);
	Rml::Element* OriginalText = Label->GetFirstChild();
	TestNotNull(TEXT("Initial binding creates a real text node"), OriginalText);
	const uint64 WritesBefore = View.BindingContentWriteCount;
	for (int32 Index = 0; Index < 100; ++Index)
	{
		View.SetData(TEXT("name"), TEXT("Pilot <A> & \"B\""));
		View.SynchronizeModels();
	}
	TestEqual(TEXT("One hundred unchanged synchronizations perform no DOM rebuild"), View.BindingContentWriteCount, WritesBefore);
	TestTrue(TEXT("Unchanged binding retains the original text node"), Label->GetFirstChild() == OriginalText);
	TestTrue(TEXT("Unchanged value binding retains its value"), Input->GetAttribute<Rml::String>("value", "") == "7");
	AddInfo(FString::Printf(TEXT("H5UIBindingWork,unchanged_syncs=100,content_writes=%llu"), View.BindingContentWriteCount - WritesBefore));

	Label->SetInnerRML("external edit");
	Input->SetAttribute("value", Rml::String("external edit"));
	View.SynchronizeModels();
	TestEqual(TEXT("External DOM edit is corrected even when the model did not change"), View.BindingContentWriteCount, WritesBefore + 1);
	TestTrue(TEXT("External text edit is restored from the model"), Label->GetInnerRML() == Expected);
	TestTrue(TEXT("External value edit is restored from the model"), Input->GetAttribute<Rml::String>("value", "") == "7");
	Host->SetInnerRML("<span id='late' data-bind='name'>new node</span>");
	View.SynchronizeModels();
	TestTrue(TEXT("Dynamically inserted binding reads existing model data"), View.Document->GetElementById("late")->GetInnerRML() == Expected);
	Host->SetInnerRML("<span id='late' data-bind='name'>replacement node</span>");
	View.SynchronizeModels();
	TestTrue(TEXT("Replacing a bound node never leaves a stale cached binding"), View.Document->GetElementById("late")->GetInnerRML() == Expected);
	const uint64 BeforeChange = View.BindingContentWriteCount;
	View.SetData(TEXT("name"), TEXT("Updated pilot"));
	View.SynchronizeModels();
	TestEqual(TEXT("Changed data updates both current bindings exactly once"), View.BindingContentWriteCount, BeforeChange + 2);
	TestTrue(TEXT("Changed data reaches the replacement node"), View.Document->GetElementById("late")->GetInnerRML() == "Updated pilot");
	View.SetData(TEXT("name"), TEXT(" \t\r\n"));
	View.SynchronizeModels();
	TestTrue(TEXT("Whitespace-only binding uses Rml's empty-node representation"), Label->GetInnerRML().empty());
	const uint64 BeforeBlankSync = View.BindingContentWriteCount;
	for (int32 Index = 0; Index < 10; ++Index) View.SynchronizeModels();
	TestEqual(TEXT("Whitespace-only values do not rewrite on every synchronization"), View.BindingContentWriteCount, BeforeBlankSync);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FH5UI_DynamicPerformanceBindingsTest,
	"H5UIPlugin.Runtime.DynamicPerformanceBindings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FH5UI_DynamicPerformanceBindingsTest::RunTest(const FString& Parameters)
{
	FH5UI_RuntimeView View([] {}, [this](const FString& Error) { AddError(Error); }, [](const FH5UI_Event&) {});
	View.SetJavaScriptEnabled(false);
	FString Html = TEXT("<html><head></head><body><div id='ordinary'></div><div id='stats'></div>");
	for (int32 Index = 0; Index < 100; ++Index) Html += TEXT("<div><span>Decoration</span></div>");
	Html += TEXT("</body></html>");
	if (!TestTrue(TEXT("Decorative fixture loads"), View.LoadString(Html, TEXT("coui://uiresources/periodic-bindings.html")))) return false;
	for (int32 Index = 1; Index <= 10; ++Index) View.PublishPerformanceStats(Index);
	TestEqual(TEXT("Unbound decoration receives no periodic model synchronizations"), View.PeriodicBindingElementSyncCount, uint64(0));
	TestEqual(TEXT("No consumer and no JavaScript skips performance string publishing"), View.PerformanceModelPublishCount, uint64(0));
	TestEqual(TEXT("Decorative nodes are never rebuilt by performance reporting"), View.BindingContentWriteCount, uint64(0));
	AddInfo(TEXT("H5UIBindingWork,decorative_nodes=200,publishes=10,binding_syncs=0,content_writes=0,perf_formats=0"));

	Rml::Element* Ordinary = View.Document->GetElementById("ordinary");
	Rml::Element* Stats = View.Document->GetElementById("stats");
	if (!TestNotNull(TEXT("Ordinary host exists"), Ordinary) || !TestNotNull(TEXT("Stats host exists"), Stats)) return false;
	View.SetData(TEXT("unit.name"), TEXT("Late pilot"));
	Ordinary->SetInnerRML("<span id='unit' data-bind='unit.name'>placeholder</span>");
	View.PublishPerformanceStats(11.0);
	TestTrue(TEXT("Periodic refresh still finds newly inserted ordinary bindings"), View.Document->GetElementById("unit")->GetInnerRML() == "Late pilot");
	TestEqual(TEXT("Ordinary binding does not require performance formatting"), View.PerformanceModelPublishCount, uint64(0));
	View.PerformanceStats.Vertices = 123;
	View.PerformanceStats.DrawBatches = 7;
	// Replacing the parent's content removes its bound descendant. Discovery must
	// recurse after synchronizing the parent, never retain dangling node pointers.
	Stats->SetInnerRML("<div id='vertices' data-bind='perf.vertices'><span data-bind='perf.batches'>old</span></div>"
		"<input id='batches' data-bind-value='perf.batches'/>");
	View.PublishPerformanceStats(12.0);
	TestTrue(TEXT("Dynamically inserted performance text is populated"), View.Document->GetElementById("vertices")->GetInnerRML() == "123");
	TestTrue(TEXT("Performance value bindings are also populated"), View.Document->GetElementById("batches")->GetAttribute<Rml::String>("value", "") == "7");
	TestEqual(TEXT("One publish formats statistics once for all consumers"), View.PerformanceModelPublishCount, uint64(1));
	Rml::Element* Vertices = View.Document->GetElementById("vertices");
	Rml::Element* TextNode = Vertices->GetFirstChild();
	const uint64 BeforeRepeatedStats = View.BindingContentWriteCount;
	View.PublishPerformanceStats(13.0);
	TestEqual(TEXT("Unchanged performance values do not rebuild any bound text"), View.BindingContentWriteCount, BeforeRepeatedStats);
	TestTrue(TEXT("Statistics text node survives repeated publication"), Vertices->GetFirstChild() == TextNode);
	Vertices->SetAttribute("data-bind", Rml::String("perf.batches"));
	View.PublishPerformanceStats(14.0);
	TestTrue(TEXT("Renaming a binding is discovered without a model value change"), Vertices->GetInnerRML() == "7");
	Stats->SetInnerRML("");
	const uint64 BeforeRemoval = View.PerformanceModelPublishCount;
	View.SetData(TEXT("unit.name"), TEXT("Changed after removal"));
	View.PublishPerformanceStats(15.0);
	TestEqual(TEXT("Removing all performance consumers stops formatting again"), View.PerformanceModelPublishCount, BeforeRemoval);
	TestTrue(TEXT("Ordinary SetData still propagates on the periodic refresh"), View.Document->GetElementById("unit")->GetInnerRML() == "Changed after removal");

	FH5UI_RuntimeView ScriptedView([] {}, [this](const FString& Error) { AddError(Error); }, [](const FH5UI_Event&) {});
	if (!TestTrue(TEXT("JavaScript-only statistics fixture loads"), ScriptedView.LoadString(
		TEXT("<html><head></head><body></body></html>"), TEXT("coui://uiresources/scripted-stats.html")))) return false;
	ScriptedView.PerformanceStats.Vertices = 456;
	ScriptedView.PublishPerformanceStats(1.0);
	FString Result, Error;
	TestTrue(TEXT("JavaScript retains access to statistics without DOM consumers"), ScriptedView.ExecuteJavaScript(TEXT("ue.getData('perf.vertices')"), Result, Error));
	TestEqual(TEXT("JavaScript receives the latest statistics"), Result, FString(TEXT("456")));
	TestEqual(TEXT("JavaScript-only page still skips periodic DOM binding synchronization"), ScriptedView.PeriodicBindingElementSyncCount, uint64(0));
	return true;
}
#endif
