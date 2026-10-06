#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/PlatformProcess.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Layout/Geometry.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Rendering/DrawElements.h"
#include "RmlUi/Core/PropertyDictionary.h"
#include "RmlUi/Core/StyleSheetSpecification.h"
#include "UObject/StrongObjectPtr.h"
#include "H5UI_BrowserBridge.h"
#include "H5UI_Interfaces.h"
#include "H5UI_RuntimeView.h"
#include "H5UI_Module.h"
#include "H5UI_View.h"
#include "Tests/H5UI_EventHandlerTestTypes.h"

void UH5UI_StructArrayEventTestHandler::ReceiveStructArray(
	const TArray<FH5UI_TestStructArgument>& Values)
{
	ReceivedValues = Values;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FH5UI_ExternalTextureLifetimeTest,
	"H5UIPlugin.Runtime.ExternalTextureLifetime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FH5UI_BrowserHostRelayTest,
	"H5UIPlugin.Runtime.BrowserHostRelay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FH5UI_StructArrayEventArgumentTest,
	"H5UIPlugin.Runtime.StructArrayEventArgument",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FH5UI_ViewportResizeEventTest,
	"H5UIPlugin.Runtime.ViewportResizeEvent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FH5UI_ScaledDocumentCoordinateTest,
	"H5UIPlugin.Runtime.ScaledDocumentCoordinates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FH5UI_BaseControlInventoryLayoutTest,
	"H5UIPlugin.Runtime.BaseControlInventoryLayout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FH5UI_RepeatedPointerSessionsTest,
	"H5UIPlugin.Runtime.RepeatedPointerSessions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FH5UI_BaseControlInventoryLayoutTest::RunTest(const FString& Parameters)
{
	FH5UI_RuntimeView View([]() {}, [](const FString&) {}, [](const FH5UI_Event&) {});
	TestTrue(
		TEXT("BaseControl inventory layout document loads"),
		View.LoadURL(TEXT("coui://uiresources/BaseControl/base-control.html")));
	View.Update(FVector2D(1920.0f, 1080.0f), FPlatformTime::Seconds());
	TestTrue(
		TEXT("BaseControl enters the readiness-room inventory state"),
		View.DispatchHtmlEvent(
			TEXT("SilverBaseHUDState"),
			TEXT("{\"scene\":\"underground\",\"location\":\"readiness-room\"}")));
	TestTrue(
		TEXT("BaseControl accepts the registered base slot size"),
		View.DispatchHtmlEvent(
			TEXT("SISH5UI.Inventory.LayoutConfiguration"),
			TEXT("{\"protocolVersion\":1,\"baseSlotWidth\":30,\"baseSlotHeight\":30,\"referenceViewportHeight\":1080}")));
	View.Update(FVector2D(1920.0f, 1080.0f), FPlatformTime::Seconds() + 0.1);
	FString CellSetupResult;
	FString CellSetupError;
	TestTrue(
		TEXT("BaseControl inventory cell can be selected for geometry verification"),
		View.ExecuteJavaScript(
			TEXT("document.querySelector('.inventory-grid-cell').id='layout-test-cell';'ready'"),
			CellSetupResult,
			CellSetupError));

	TestTrue(
		TEXT("Inventory cells use BaseSlotSize at 1080p"),
		View.GetElementBorderSizeForTesting(TEXT("layout-test-cell")).Equals(
			FVector2D(30.0f, 30.0f), 0.1f));
	TestTrue(
		TEXT("Normal equipment slots use 80 by 80 design pixels"),
		View.GetElementBorderSizeForTesting(TEXT("equipment-head")).Equals(
			FVector2D(80.0f, 80.0f), 0.1f));
	TestTrue(
		TEXT("Hand equipment slots use 160 by 80 design pixels"),
		View.GetElementBorderSizeForTesting(TEXT("equipment-left-hand")).Equals(
			FVector2D(160.0f, 80.0f), 0.1f));

	View.Update(FVector2D(960.0f, 540.0f), FPlatformTime::Seconds() + 0.2);
	FString ResizeResult;
	FString ResizeError;
	TestTrue(
		TEXT("BaseControl receives the native H5 viewport resize notification"),
		View.ExecuteJavaScript(
			TEXT("document.documentElement.dispatchEvent(new Event('resize'));String(innerHeight)"),
			ResizeResult,
			ResizeError));
	View.Update(FVector2D(960.0f, 540.0f), FPlatformTime::Seconds() + 0.3);
	View.Update(FVector2D(960.0f, 540.0f), FPlatformTime::Seconds() + 0.4);
	AddInfo(FString::Printf(
		TEXT("Scaled BaseControl geometry: cell=%s normal=%s hand=%s"),
		*View.GetElementBorderSizeForTesting(TEXT("layout-test-cell")).ToString(),
		*View.GetElementBorderSizeForTesting(TEXT("equipment-head")).ToString(),
		*View.GetElementBorderSizeForTesting(TEXT("equipment-left-hand")).ToString()));
	TestTrue(
		TEXT("Inventory cells keep their authored size below the reference height"),
		View.GetElementBorderSizeForTesting(TEXT("layout-test-cell")).Equals(
			FVector2D(30.0f, 30.0f), 0.1f));
	TestTrue(
		TEXT("Normal equipment slots keep their authored size below the reference height"),
		View.GetElementBorderSizeForTesting(TEXT("equipment-head")).Equals(
			FVector2D(80.0f, 80.0f), 0.1f));
	TestTrue(
		TEXT("Hand equipment slots keep their authored size below the reference height"),
		View.GetElementBorderSizeForTesting(TEXT("equipment-left-hand")).Equals(
			FVector2D(160.0f, 80.0f), 0.1f));

	View.Update(FVector2D(3840.0f, 2160.0f), FPlatformTime::Seconds() + 0.5);
	View.ExecuteJavaScript(
		TEXT("document.documentElement.dispatchEvent(new Event('resize'));String(innerHeight)"),
		ResizeResult,
		ResizeError);
	View.Update(FVector2D(3840.0f, 2160.0f), FPlatformTime::Seconds() + 0.6);
	View.Update(FVector2D(3840.0f, 2160.0f), FPlatformTime::Seconds() + 0.7);
	TestTrue(
		TEXT("Inventory cells enlarge above the reference height"),
		View.GetElementBorderSizeForTesting(TEXT("layout-test-cell")).Equals(
			FVector2D(60.0f, 60.0f), 0.1f));
	TestTrue(
		TEXT("Normal equipment slots enlarge above the reference height"),
		View.GetElementBorderSizeForTesting(TEXT("equipment-head")).Equals(
			FVector2D(160.0f, 160.0f), 0.1f));
	TestTrue(
		TEXT("Hand equipment slots preserve their two-to-one ratio when enlarged"),
		View.GetElementBorderSizeForTesting(TEXT("equipment-left-hand")).Equals(
			FVector2D(320.0f, 160.0f), 0.1f));
	return true;
}

bool FH5UI_RepeatedPointerSessionsTest::RunTest(const FString& Parameters)
{
	FH5UI_Event CapturedEvent;
	int32 CapturedEventCount = 0;
	FH5UI_RuntimeView View(
		[]() {},
		[](const FString&) {},
		[&CapturedEvent, &CapturedEventCount](const FH5UI_Event& Event)
		{
			CapturedEvent = Event;
			++CapturedEventCount;
		});

	TestTrue(
		TEXT("Repeated pointer-session page loads"),
		View.LoadURL(TEXT("coui://uiresources/BaseControl/base-control.html")));
	View.Update(FVector2D(1920.0f, 1080.0f), FPlatformTime::Seconds());
	TestTrue(
		TEXT("Repeated pointer-session page enters inventory mode"),
		View.DispatchHtmlEvent(
			TEXT("SilverBaseHUDState"),
			TEXT("{\"scene\":\"underground\",\"location\":\"readiness-room\"}")));
	View.Update(FVector2D(1920.0f, 1080.0f), FPlatformTime::Seconds() + 0.1);

	const FVector2D ItemPosition =
		View.GetElementBorderPositionForTesting(TEXT("inventory-item-sample-rifle"));
	const FVector2D ItemSize =
		View.GetElementBorderSizeForTesting(TEXT("inventory-item-sample-rifle"));
	const FVector2D DragStart = ItemPosition + ItemSize * 0.5f;
	FString DropCellSetupResult;
	FString DropCellSetupError;
	TestTrue(
		TEXT("Repeated pointer-session drop cell can be addressed"),
		View.ExecuteJavaScript(
			TEXT("var cs=document.querySelectorAll('.inventory-grid-cell');var c=cs[cs.length-1];")
				TEXT("if(c)c.id='repeated-pointer-drop-cell';c?'ready':'';"),
			DropCellSetupResult,
			DropCellSetupError));
	const FVector2D DropCellPosition =
		View.GetElementBorderPositionForTesting(TEXT("repeated-pointer-drop-cell"));
	const FVector2D DropCellSize =
		View.GetElementBorderSizeForTesting(TEXT("repeated-pointer-drop-cell"));
	const FVector2D DropPosition = DropCellPosition + DropCellSize * 0.5f;
	const FVector2D TargetItemPosition =
		View.GetElementBorderPositionForTesting(TEXT("inventory-item-sample-ammo"));
	const FVector2D TargetItemSize =
		View.GetElementBorderSizeForTesting(TEXT("inventory-item-sample-ammo"));
	const FVector2D TargetItemCenter =
		TargetItemPosition + TargetItemSize * 0.5f;

	auto RunDragSession = [
		&View,
		&CapturedEvent,
		&CapturedEventCount,
		this,
		&DragStart,
		&TargetItemCenter,
		&DropPosition
	](const FString& SessionId, const FVector2D& MoveDelta, const TCHAR* Label)
	{
		CapturedEvent = FH5UI_Event();
		CapturedEventCount = 0;
		TestTrue(
			*FString::Printf(TEXT("%s accepts pointer down"), Label),
			View.ProcessMouseButtonDownForTesting(DragStart));
		View.ProcessMouseMoveForTesting(DragStart + MoveDelta);
		TestEqual(
			*FString::Printf(TEXT("%s emits one begin"), Label),
			CapturedEventCount,
			1);
		TestEqual(
			*FString::Printf(TEXT("%s reaches the inventory handler"), Label),
			CapturedEvent.Name,
			FName(TEXT("DragBegin")));
		TestTrue(
			*FString::Printf(TEXT("%s is acknowledged"), Label),
			View.DispatchHtmlEvent(
				TEXT("SISH5UI.Drag.State"),
				FString::Printf(
					TEXT("{\"protocolVersion\":1,\"accepted\":true,\"sessionId\":\"%s\",\"itemId\":\"sample-rifle\"}"),
					*SessionId)));
		View.ProcessMouseMoveForTesting(TargetItemCenter);
		TestEqual(
			*FString::Printf(TEXT("%s reaches the item-target probe handler"), Label),
			CapturedEvent.Name,
			FName(TEXT("DragProbeRequest")));
		TestTrue(
			*FString::Printf(TEXT("%s identifies the hovered item target"), Label),
			CapturedEvent.Payload.Contains(TEXT("targetItemId")) &&
				CapturedEvent.Payload.Contains(TEXT("sample-ammo")));
		View.ProcessMouseMoveForTesting(DropPosition);
		TestTrue(
			*FString::Printf(TEXT("%s evaluates its candidate before mouse up"), Label),
			CapturedEventCount > 1);
		TestEqual(
			*FString::Printf(TEXT("%s reaches the drop-probe handler from pointer movement"), Label),
			CapturedEvent.Name,
			FName(TEXT("DragProbeRequest")));
		View.ProcessMouseButtonUpForTesting(DropPosition);
		TestEqual(
			*FString::Printf(TEXT("%s emits an explicit end"), Label),
			CapturedEvent.Name,
			FName(TEXT("DragEnd")));
		TestTrue(
			*FString::Printf(TEXT("%s result clears H5 ownership"), Label),
			View.DispatchHtmlEvent(
				TEXT("SISH5UI.Drag.Result"),
				FString::Printf(
					TEXT("{\"protocolVersion\":1,\"sessionId\":\"%s\",\"itemId\":\"sample-rifle\",\"reason\":\"test\"}"),
					*SessionId)));
	};

	RunDragSession(
		TEXT("10000000-0000-0000-0000-000000000001"),
		FVector2D(14.0f, 2.0f),
		TEXT("First inventory drag"));
	RunDragSession(
		TEXT("20000000-0000-0000-0000-000000000002"),
		FVector2D(-16.0f, 5.0f),
		TEXT("Second inventory drag"));

	TestTrue(
		TEXT("Repeated pointer-session page opens a detail for first-click close"),
		View.DispatchHtmlEvent(
			TEXT("SISH5UI.ItemDetail.Show"),
			TEXT("{\"protocolVersion\":1,\"sessionId\":\"30000000-0000-0000-0000-000000000003\",")
				TEXT("\"itemId\":\"sample-rifle\",\"displayName\":\"Rifle\",\"interactivePreview\":true,")
				TEXT("\"previewTexture\":\"\",\"properties\":[],\"attachmentSlots\":[]}")));
	View.Update(FVector2D(1920.0f, 1080.0f), FPlatformTime::Seconds() + 0.2);

	const FVector2D ClosePosition =
		View.GetElementBorderPositionForTesting(TEXT("item-detail-close"));
	const FVector2D CloseSize =
		View.GetElementBorderSizeForTesting(TEXT("item-detail-close"));
	const FVector2D CloseCenter = ClosePosition + CloseSize * 0.5f;
	CapturedEvent = FH5UI_Event();
	CapturedEventCount = 0;
	TestTrue(
		TEXT("Detail close accepts the first pointer down after opening"),
		View.ProcessMouseButtonDownForTesting(CloseCenter));
	TestEqual(
		TEXT("First detail close reaches the detail handler immediately"),
		CapturedEvent.Name,
		FName(TEXT("CloseItemDetail")));
	View.ProcessMouseButtonUpForTesting(CloseCenter);

	TestTrue(
		TEXT("Repeated pointer-session page reopens an interactive detail"),
		View.DispatchHtmlEvent(
			TEXT("SISH5UI.ItemDetail.Show"),
			TEXT("{\"protocolVersion\":1,\"sessionId\":\"40000000-0000-0000-0000-000000000004\",")
				TEXT("\"itemId\":\"sample-rifle\",\"displayName\":\"Rifle\",\"interactivePreview\":true,")
				TEXT("\"previewTexture\":\"\",\"properties\":[],\"attachmentSlots\":[]}")));
	View.Update(FVector2D(1920.0f, 1080.0f), FPlatformTime::Seconds() + 0.3);

	FString SetupResult;
	FString SetupError;
	TestTrue(
		TEXT("Repeated pointer-session detail preview can be addressed"),
		View.ExecuteJavaScript(
			TEXT("var p=document.querySelector('.item-detail-preview');if(p)p.id='repeated-pointer-preview';p?'ready':'';"),
			SetupResult,
			SetupError));
	const FVector2D PreviewPosition =
		View.GetElementBorderPositionForTesting(TEXT("repeated-pointer-preview"));
	const FVector2D PreviewSize =
		View.GetElementBorderSizeForTesting(TEXT("repeated-pointer-preview"));
	const FVector2D RotateStart = PreviewPosition + PreviewSize * 0.5f;

	auto RunRotationSession = [
		&View,
		&CapturedEvent,
		&CapturedEventCount,
		this,
		&RotateStart
	](const FVector2D& MoveDelta, const TCHAR* Label)
	{
		CapturedEvent = FH5UI_Event();
		CapturedEventCount = 0;
		TestTrue(
			*FString::Printf(TEXT("%s accepts pointer down"), Label),
			View.ProcessMouseButtonDownForTesting(RotateStart));
		// Consume the CSS interaction-class update from mousedown first. The
		// following move changes only the external render target and emits an
		// Unreal event, so it independently verifies paint-only invalidation.
		View.Update(
			FVector2D(1920.0f, 1080.0f),
			FPlatformTime::Seconds());
		// The first captured move establishes a document-space baseline. The
		// second move must stream rotation before mouseup.
		View.ProcessMouseMoveForTesting(
			RotateStart + FVector2D(FMath::Sign(MoveDelta.X), 0.0f));
		View.ProcessMouseMoveForTesting(RotateStart + MoveDelta);
		TestTrue(
			*FString::Printf(TEXT("%s streams before mouse up"), Label),
			CapturedEventCount > 0);
		TestEqual(
			*FString::Printf(TEXT("%s reaches the detail handler"), Label),
			CapturedEvent.Name,
			FName(TEXT("RotateItemDetail")));
		TestTrue(
			*FString::Printf(TEXT("%s requests an intermediate repaint"), Label),
			View.Update(
				FVector2D(1920.0f, 1080.0f),
				FPlatformTime::Seconds()));
		View.ProcessMouseButtonUpForTesting(RotateStart + MoveDelta);
	};

	RunRotationSession(FVector2D(20.0f, 3.0f), TEXT("First detail rotation"));
	RunRotationSession(FVector2D(-22.0f, 8.0f), TEXT("Second detail rotation"));
	return true;
}

bool FH5UI_ScaledDocumentCoordinateTest::RunTest(const FString& Parameters)
{
	FH5UI_RuntimeView View([]() {}, [](const FString&) {}, [](const FH5UI_Event&) {});
	TestTrue(
		TEXT("Scaled coordinate test document loads"),
		View.LoadString(
			TEXT("<html><body><div id='target'>Target</div></body></html>"),
			TEXT("h5ui://plugin/scaled-coordinate-test.html")));
	View.Update(FVector2D(4324.0f, 2252.0f), FPlatformTime::Seconds(), 0.4375f, 1.0f);
	TestTrue(
		TEXT("Slate-local coordinates convert to browser CSS pixels"),
		View.LocalToDocumentPosition(FVector2D(1264.0f, 656.0f)).Equals(
			FVector2D(553.0f, 287.0f),
			0.01f));
	return true;
}

bool FH5UI_ViewportResizeEventTest::RunTest(const FString& Parameters)
{
	FH5UI_RuntimeView View([]() {}, [](const FString&) {}, [](const FH5UI_Event&) {});
	TestTrue(
		TEXT("Viewport resize test document loads"),
		View.LoadString(
			TEXT("<html><body><div id='content'>Viewport</div></body></html>"),
			TEXT("h5ui://plugin/viewport-resize-test.html")));
	View.Update(FVector2D(640.0, 480.0), FPlatformTime::Seconds(), 1.0f, 1.0f);

	FString Result;
	FString Error;
	TestTrue(
		TEXT("JavaScript can subscribe to the standard viewport resize event"),
		View.ExecuteJavaScript(
			TEXT("globalThis.__h5uiResizeCount=0;addEventListener('resize',function(){globalThis.__h5uiResizeCount++;});'ready'"),
			Result,
			Error));

	View.Update(FVector2D(800.0, 600.0), FPlatformTime::Seconds() + 0.1, 1.0f, 1.0f);
	TestTrue(
		TEXT("Changed UE view metrics can be read after resize"),
		View.ExecuteJavaScript(
			TEXT("[innerWidth,innerHeight,globalThis.__h5uiResizeCount].join('|')"),
			Result,
			Error));
	TestEqual(
		TEXT("One UE view-size change emits one resize event with current dimensions"),
		Result,
		FString(TEXT("800|600|1")));

	View.Update(FVector2D(800.0, 600.0), FPlatformTime::Seconds() + 0.2, 1.5f, 2.0f);
	TestTrue(
		TEXT("Render-scale-only changes leave the resize count readable"),
		View.ExecuteJavaScript(
			TEXT("String(globalThis.__h5uiResizeCount)"),
			Result,
			Error));
	TestEqual(TEXT("Render-scale-only changes do not emit resize"), Result, FString(TEXT("1")));
	return true;
}

bool FH5UI_StructArrayEventArgumentTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UH5UI_View> View(NewObject<UH5UI_View>(GetTransientPackage()));
	TStrongObjectPtr<UH5UI_StructArrayEventTestHandler> Handler(
		NewObject<UH5UI_StructArrayEventTestHandler>(View.Get()));
	TestTrue(TEXT("Struct-array test handler registers"), View->RegisterEventHandler(TEXT("Test"), Handler.Get()));

	TStrongObjectPtr<UH5UI_BrowserBridge> Bridge(NewObject<UH5UI_BrowserBridge>(GetTransientPackage()));
	Bridge->Initialize(View.Get());
	Bridge->Emit(
		TEXT("Test"),
		TEXT("ReceiveStructArray"),
		TEXT("[[{\"slotId\":\"head\",\"elementId\":\"equipment-head\"},"
			"{\"slotId\":\"left-hand\",\"elementId\":\"equipment-left-hand\"}]]"),
		TEXT("struct-array-test"));

	TestEqual(TEXT("Both struct entries are received"), Handler->ReceivedValues.Num(), 2);
	if (Handler->ReceivedValues.Num() == 2)
	{
		TestEqual(TEXT("First struct SlotId is converted"), Handler->ReceivedValues[0].SlotId, FString(TEXT("head")));
		TestEqual(
			TEXT("Second struct ElementId is converted"),
			Handler->ReceivedValues[1].ElementId,
			FString(TEXT("equipment-left-hand")));
	}
	return true;
}

bool FH5UI_BrowserHostRelayTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UH5UI_View> View(NewObject<UH5UI_View>(GetTransientPackage()));
	const TSharedRef<SWidget> SlateView = View->TakeWidget();
	TestTrue(
		TEXT("Host relay test document loads into a UMG H5UI view"),
		View->LoadString(
			TEXT("<html><body><div id='relay-result'>waiting</div><script>document.addEventListener('RelayReached',function(e){document.getElementById('relay-result').textContent=e.detail;});</script></body></html>"),
			TEXT("coui://uiresources/host-relay-test.html")));

	TStrongObjectPtr<UH5UI_BrowserBridge> Bridge(NewObject<UH5UI_BrowserBridge>(GetTransientPackage()));
	Bridge->Initialize(View.Get());
	Bridge->Emit(
		TEXT("H5UIHost"),
		TEXT("DispatchHtmlEvent"),
		TEXT("[\"RelayReached\",\"strategy\"]"),
		TEXT("switch-system"));

	FString Result;
	FString Error;
	TestTrue(
		TEXT("CEF host relay result can be inspected in the native document"),
		View->ExecuteJavaScript(
			TEXT("document.getElementById('relay-result').textContent"),
			Result,
			Error));
	TestEqual(TEXT("CEF host relay dispatches event detail without an EventHandler"), Result, FString(TEXT("strategy")));
	View->ReleaseSlateResources(true);
	return true;
}

bool FH5UI_ExternalTextureLifetimeTest::RunTest(const FString& Parameters)
{
	FH5UI_RenderInterface RenderInterface;
	UTextureRenderTarget2D* RuntimeTexture = NewObject<UTextureRenderTarget2D>(
		GetTransientPackage(),
		NAME_None,
		RF_Transient);
	RuntimeTexture->InitAutoFormat(16, 16);

	TWeakObjectPtr<UTextureRenderTarget2D> WeakRuntimeTexture = RuntimeTexture;
	const Rml::TextureHandle TextureHandle = RenderInterface.CreateExternalTexture(RuntimeTexture);
	TestNotEqual(TEXT("A runtime render target creates an HTML texture handle"), TextureHandle, Rml::TextureHandle(0));

	RuntimeTexture = nullptr;
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	TestFalse(
		TEXT("An HTML texture handle does not root a transient runtime render target"),
		WeakRuntimeTexture.IsValid());

	RenderInterface.ReleaseTexture(TextureHandle);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FH5UI_RuntimeSmokeTest,
	"H5UIPlugin.Runtime.NativeSmoke",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FH5UI_RenderScaleQualityTest,
	"H5UIPlugin.Runtime.RenderScaleQuality",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FH5UI_BrowserSubviewLayoutTest,
	"H5UIPlugin.Runtime.BrowserSubviewLayout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FH5UI_HtmlCssCompatibilityTest,
	"H5UIPlugin.Runtime.HtmlCssCompatibility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FH5UI_BrowserStyleLayoutParityTest,
	"H5UIPlugin.Runtime.BrowserStyleLayoutParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FH5UI_StrategyControlCompatibilityTest,
	"H5UIPlugin.Runtime.StrategyControlCompatibility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FH5UI_RenderScaleQualityTest::RunTest(const FString& Parameters)
{
	FH5UI_RuntimeView View([]() {}, [](const FString&) {}, [](const FH5UI_Event&) {});
	TestTrue(
		TEXT("Font supersampling test document loads"),
		View.LoadString(
			TEXT("<rml><head><style>#sample { width: 160px; height: 32px; font-family: sans-serif; font-size: 16px; }</style></head><body><div id=\"sample\">Sharp text</div></body></rml>"),
			TEXT("coui://uiresources/render-scale-quality-test.rml")));

	View.Update(FVector2D(640.0, 480.0), FPlatformTime::Seconds(), 0.5f, 1.0f);
	TestTrue(
		TEXT("Baseline font uses a one-times rasterized face"),
		FMath::IsNearlyEqual(View.GetElementFontRasterizationScaleForTesting(TEXT("sample")), 1.0f));

	View.Update(FVector2D(640.0, 480.0), FPlatformTime::Seconds() + 0.1, 0.5f, 4.0f);
	TestTrue(
		TEXT("Maximum render scale selects a four-times supersampled font face"),
		FMath::IsNearlyEqual(View.GetElementFontRasterizationScaleForTesting(TEXT("sample")), 4.0f));
	TestEqual(TEXT("Four-times supersampling reports the expected render width"), View.GetPerformanceStats().RenderSize.X, 1280);
	TestEqual(TEXT("Four-times supersampling reports the expected render height"), View.GetPerformanceStats().RenderSize.Y, 960);
	View.Close();
	return true;
}

bool FH5UI_HtmlCssCompatibilityTest::RunTest(const FString& Parameters)
{
	FString BrowserResetCss = TEXT(
		".one { border: none; background: 0 0; object-fit: cover; object-position: center center; min-height: auto; }"
		".two { border-bottom: none; border: 1px dashed rgba(90, 86, 78, 0.55); }"
		".three { border: 1px dashed #d4cec0; }"
		".four{background:0 0;border:none;border-bottom:none;min-height:auto;}"
		".five{border-bottom:none}");
	H5UIPlugin::NormalizeCssForRml(BrowserResetCss);
	TestFalse(TEXT("Browser reset background shorthand is removed"), BrowserResetCss.Contains(TEXT("background: 0 0"), ESearchCase::IgnoreCase));
	TestFalse(TEXT("Unsupported object-fit is stripped"), BrowserResetCss.Contains(TEXT("object-fit"), ESearchCase::IgnoreCase));
	TestFalse(TEXT("Unsupported object-position is stripped"), BrowserResetCss.Contains(TEXT("object-position"), ESearchCase::IgnoreCase));
	TestFalse(TEXT("Border none shorthand is translated"), BrowserResetCss.Contains(TEXT("border: none"), ESearchCase::IgnoreCase));
	TestFalse(TEXT("Border bottom none shorthand is translated"), BrowserResetCss.Contains(TEXT("border-bottom: none"), ESearchCase::IgnoreCase));
	TestFalse(TEXT("Dashed borders are approximated before RmlUi parsing"), BrowserResetCss.Contains(TEXT(" dashed "), ESearchCase::IgnoreCase));
	TestFalse(TEXT("Auto min-height is translated"), BrowserResetCss.Contains(TEXT("min-height: auto"), ESearchCase::IgnoreCase));
	TestFalse(TEXT("Minified browser reset background shorthand is removed"), BrowserResetCss.Contains(TEXT("background:0 0"), ESearchCase::IgnoreCase));
	TestFalse(TEXT("Minified border none shorthand is translated"), BrowserResetCss.Contains(TEXT("border:none"), ESearchCase::IgnoreCase));
	TestFalse(TEXT("Minified auto min-height is translated"), BrowserResetCss.Contains(TEXT("min-height:auto"), ESearchCase::IgnoreCase));
	TestTrue(TEXT("Border none keeps a zero-width layout intent"), BrowserResetCss.Contains(TEXT("border-width: 0"), ESearchCase::IgnoreCase));
	TestTrue(TEXT("Dashed border keeps its color and width as a solid fallback"), BrowserResetCss.Contains(TEXT("border: 1px solid"), ESearchCase::IgnoreCase));

	FString MixedTransitionCss = TEXT(
		".panel { transition: width 0.24s ease, transform 0.24s cubic-bezier(0.2, 0.7, 0.2, 1), "
		"box-shadow 0.18s ease, opacity 0.16s ease; }");
	H5UIPlugin::NormalizeCssForRml(MixedTransitionCss);
	TestFalse(
		TEXT("Unsafe box-shadow transition item is removed"),
		MixedTransitionCss.Contains(TEXT("box-shadow"), ESearchCase::IgnoreCase));
	TestTrue(
		TEXT("Safe width transition survives a mixed transition list"),
		MixedTransitionCss.Contains(TEXT("width 0.24s ease"), ESearchCase::IgnoreCase));
	TestTrue(
		TEXT("Commas inside cubic-bezier do not split the transform transition"),
		MixedTransitionCss.Contains(TEXT("cubic-bezier(0.2, 0.7, 0.2, 1)"), ESearchCase::IgnoreCase));
	TestTrue(
		TEXT("Safe opacity transition survives a mixed transition list"),
		MixedTransitionCss.Contains(TEXT("opacity 0.16s ease"), ESearchCase::IgnoreCase));

	Rml::PropertyDictionary AnimationDelayProperties;
	TestTrue(
		TEXT("RmlUi accepts the browser animation-delay longhand"),
		Rml::StyleSheetSpecification::ParsePropertyDeclaration(
			AnimationDelayProperties,
			"animation-delay",
			"0.16s, 320ms"));

	FString GeneratedPseudoCss = TEXT(
		".host::before { content: ''; position: absolute; width: 4px; height: 4px; }"
		".host.active::after { content: \"\"; position: absolute; background-color: #fff; }");
	TArray<FH5UI_GeneratedPseudoSelector> GeneratedPseudoRules;
	H5UIPlugin::NormalizeCssForRml(GeneratedPseudoCss, &GeneratedPseudoRules);
	TestFalse(TEXT("Generated pseudo selectors are rewritten before RmlUi parses CSS"), GeneratedPseudoCss.Contains(TEXT("::")));
	TestFalse(TEXT("Generated pseudo content declarations no longer warn"), GeneratedPseudoCss.Contains(TEXT("content:")));
	TestTrue(TEXT("Before selector targets an internal generated element"), GeneratedPseudoCss.Contains(TEXT(".host > .h5ui-generated-before")));
	TestTrue(TEXT("After selector targets an internal generated element"), GeneratedPseudoCss.Contains(TEXT(".host.active > .h5ui-generated-after")));
	TestEqual(TEXT("Both generated pseudo selectors are captured"), GeneratedPseudoRules.Num(), 2);

	FString LoadError;
	FH5UI_RuntimeView View([]() {}, [&LoadError](const FString& Error) { LoadError = Error; }, [](const FH5UI_Event&) {});
	TestTrue(
		TEXT("Browser-authored HTML with void tags and reset CSS loads"),
		View.LoadString(
			TEXT("<!doctype html><html><head><style>"
				"#image { width: 40px; height: 24px; object-fit: cover; object-position: center center; border: 1px dashed rgba(90, 86, 78, 0.55); }"
				"#reset { min-height: auto; background: 0 0; border: none; border-bottom: none; }"
				"</style></head><body><button id=\"host\"><img id=\"image\" src=\"h5ui://plugin/media/showcase.png\"></button><hr id=\"rule\"><div id=\"reset\"></div></body></html>"),
			TEXT("h5ui://plugin/html-css-compatibility-test.html")));
	TestTrue(TEXT("Compatibility load emits no parse failure"), LoadError.IsEmpty());
	View.Update(FVector2D(320.0, 200.0), FPlatformTime::Seconds());
	TestEqual(TEXT("Unclosed HTML img remains addressable after normalization"), View.GetElementAttributeForTesting(TEXT("image"), TEXT("id")), FString(TEXT("image")));
	TestEqual(TEXT("Unclosed HTML hr remains addressable after normalization"), View.GetElementAttributeForTesting(TEXT("rule"), TEXT("id")), FString(TEXT("rule")));

	TestTrue(
		TEXT("Static and script-created generated pseudo elements load"),
		View.LoadString(
			TEXT("<!doctype html><html><head><style>"
				".generated::before { content: ''; position: absolute; width: 3px; height: 3px; background-color: #fff; }"
				".generated::after { content: ''; position: absolute; width: 2px; height: 2px; background-color: #0f0; }"
				"</style></head><body><div id=\"static-generated\" class=\"generated\"></div>"
				"<script>var e=document.createElement('div');e.id='dynamic-generated';e.setAttribute('class','generated');document.body.appendChild(e);</script>"
				"</body></html>"),
			TEXT("h5ui://plugin/generated-pseudo-test.html")));
	View.Update(FVector2D(320.0, 200.0), FPlatformTime::Seconds() + 0.05);
	TestTrue(TEXT("Static ::before creates an internal element"), View.HasGeneratedPseudoElementForTesting(TEXT("static-generated"), true));
	TestTrue(TEXT("Static ::after creates an internal element"), View.HasGeneratedPseudoElementForTesting(TEXT("static-generated"), false));
	TestTrue(TEXT("Dynamic ::before creates an internal element"), View.HasGeneratedPseudoElementForTesting(TEXT("dynamic-generated"), true));
	TestTrue(TEXT("Dynamic ::after creates an internal element"), View.HasGeneratedPseudoElementForTesting(TEXT("dynamic-generated"), false));

	TestTrue(
		TEXT("Browser linear-gradient and rounded geometry load through the native renderer"),
		View.LoadString(
			TEXT("<!doctype html><html><head><style>"
				"html, body { margin: 0; background-color: transparent; }"
				"#gradient-circle { width: 128px; height: 128px; border: 6px solid #d2b478; border-radius: 50%; background-image: linear-gradient(180deg, #10131a 0%, #0a0c10 100%); }"
				"</style></head><body><div id=\"gradient-circle\"></div></body></html>"),
			TEXT("h5ui://plugin/gradient-circle-test.html")));
	View.Update(FVector2D(320.0, 200.0), FPlatformTime::Seconds() + 0.1);
	FSlateWindowElementList GradientCircleElements(nullptr);
	View.Paint(
		FGeometry::MakeRoot(FVector2f(320.0f, 200.0f), FSlateLayoutTransform()),
		GradientCircleElements,
		0);
	const FH5UI_PerformanceStats GradientCircleStats = View.GetPerformanceStats();
	TestTrue(TEXT("Native linear-gradient submits visible geometry"), GradientCircleStats.DrawBatches > 0);
	TestTrue(TEXT("Rounded circles use high-density arc tessellation"), GradientCircleStats.Vertices >= 96);

	View.Close();
	return true;
}

bool FH5UI_BrowserStyleLayoutParityTest::RunTest(const FString& Parameters)
{
	FString LoadError;
	FH5UI_RuntimeView View([]() {}, [&LoadError](const FString& Error) { LoadError = Error; }, [](const FH5UI_Event&) {});
	TestTrue(
		TEXT("Browser-style absolute launcher document loads"),
		View.LoadString(
			TEXT("<!doctype html><html><head><style>"
				"html, body { margin: 0; width: 100%; height: 100%; background-color: transparent; }"
				"#dock { position: absolute; left: 14px; bottom: 14px; display: flex; height: 58px; padding: 0 10px; background-color: rgba(22, 24, 30, 0.92); border: 1px solid rgba(170, 150, 110, 0.28); border-radius: 16px; box-shadow: 0 10px 28px rgba(0, 0, 0, 0.45); }"
				"#launcher { position: absolute; left: 0; bottom: 100%; width: 300px; margin-bottom: 12px; overflow: hidden; background-color: rgba(22, 24, 30, 0.97); border: 1px solid rgba(170, 150, 110, 0.35); border-radius: 14px; box-shadow: 0 16px 36px rgba(0, 0, 0, 0.5); }"
				".head { padding: 16px 16px 14px; border-bottom: 1px solid rgba(170, 150, 110, 0.2); background-color: rgba(36, 32, 26, 0.55); }"
				".list { display: flex; flex-direction: column; padding: 12px 16px 16px; }"
				".item { display: flex; align-items: center; min-height: 52px; padding: 8px; margin-bottom: 6px; }"
				".icon { width: 34px; height: 34px; margin-right: 12px; border: 1px solid #d2b478; border-radius: 10px; }"
				".text { display: flex; flex-direction: column; }"
				".title { font-size: 14px; font-weight: 600; color: #f0e8da; }"
				".sub { margin-top: 2px; font-size: 11px; color: #c2a873; }"
				"</style></head><body><div id=\"dock\"><div id=\"launcher\"><div class=\"head\"><span class=\"title\">SC Command</span><div class=\"sub\">Active Encrypted</div></div><div class=\"list\">"
				"<button class=\"item\"><span class=\"icon\"></span><span class=\"text\"><span class=\"title\">Browser</span><span class=\"sub\">Strategic network access</span></span></button>"
				"<button class=\"item\"><span class=\"icon\"></span><span class=\"text\"><span class=\"title\">Mail</span><span class=\"sub\">Module on standby</span></span></button>"
				"<button class=\"item\"><span class=\"icon\"></span><span class=\"text\"><span class=\"title\">Comms</span><span class=\"sub\">Module on standby</span></span></button>"
				"<button class=\"item\"><span class=\"icon\"></span><span class=\"text\"><span class=\"title\">System Settings</span><span class=\"sub\">Wallpaper and accent theme</span></span></button>"
				"</div></div><button>Start</button></div></body></html>"),
			TEXT("coui://uiresources/browser-style-layout-parity.html")));
	TestTrue(TEXT("Browser-style layout emits no load error"), LoadError.IsEmpty());
	View.Update(FVector2D(394.0, 462.0), FPlatformTime::Seconds());

	const FVector2D DockPosition = View.GetElementBorderPositionForTesting(TEXT("dock"));
	const FVector2D LauncherPosition = View.GetElementBorderPositionForTesting(TEXT("launcher"));
	const FVector2D LauncherSize = View.GetElementBorderSizeForTesting(TEXT("launcher"));
	TestTrue(TEXT("Absolute launcher preserves its declared browser width"), FMath::IsNearlyEqual(LauncherSize.X, 302.0, 1.0));
	TestTrue(TEXT("Absolute launcher auto height is sized from content"), LauncherSize.Y >= 330.0);
	TestTrue(TEXT("Absolute launcher bottom edge is anchored above dock with margin"), LauncherPosition.Y + LauncherSize.Y <= DockPosition.Y - 10.0);

	TestTrue(
		TEXT("CommanderOS V2 document loads for launcher parity checks"),
		View.LoadURL(TEXT("coui://uiresources/CommanderOS/V2/commander-os2.html")));
	TestTrue(TEXT("CommanderOS V2 layout emits no load error"), LoadError.IsEmpty());
	View.Update(FVector2D(648.0, 424.0), FPlatformTime::Seconds() + 0.2);
	TestTrue(TEXT("CommanderOS start button opens the launcher"), View.ClickElementForTesting(TEXT("btn-start")));
	View.Update(FVector2D(648.0, 424.0), FPlatformTime::Seconds() + 0.3);

	const FVector2D CommanderDockPosition = View.GetElementBorderPositionForTesting(TEXT("dock-shell"));
	const FVector2D CommanderLauncherPosition = View.GetElementBorderPositionForTesting(TEXT("start-menu"));
	const FVector2D CommanderLauncherSize = View.GetElementBorderSizeForTesting(TEXT("start-menu"));
	AddInfo(FString::Printf(
		TEXT("CommanderOS launcher metrics: dock=(%.2f, %.2f) launcher=(%.2f, %.2f) size=(%.2f, %.2f)"),
		CommanderDockPosition.X,
		CommanderDockPosition.Y,
		CommanderLauncherPosition.X,
		CommanderLauncherPosition.Y,
		CommanderLauncherSize.X,
		CommanderLauncherSize.Y));
	TestTrue(TEXT("CommanderOS launcher includes the system-action rail in its responsive panel width"), CommanderLauncherSize.X >= 318.0 && CommanderLauncherSize.X <= 322.0);
	TestTrue(TEXT("CommanderOS launcher auto height is sized from its menu content"), CommanderLauncherSize.Y >= 350.0);
	TestTrue(
		TEXT("CommanderOS launcher remains anchored above the dock"),
		CommanderLauncherPosition.Y + CommanderLauncherSize.Y <= CommanderDockPosition.Y - 10.0);
	View.Close();
	return true;
}

bool FH5UI_StrategyControlCompatibilityTest::RunTest(const FString& Parameters)
{
	FString LoadError;
	FH5UI_Event ReadyEvent;
	FH5UI_RuntimeView View(
		[]() {},
		[&LoadError](const FString& Error) { LoadError = Error; },
		[&ReadyEvent](const FH5UI_Event& Event)
		{
			if (Event.EventType == FName(TEXT("StrategyControl")) && Event.Name == FName(TEXT("StrategyControlReady")))
			{
				ReadyEvent = Event;
			}
		});

	TestTrue(
		TEXT("StrategyControl page loads through the native runtime"),
		View.LoadURL(TEXT("coui://uiresources/StrategyControl/strategy-control.html")));
	View.Update(FVector2D(1920.0, 1080.0), FPlatformTime::Seconds() + 0.5);
	const FVector2D InitialCommanderFramePosition =
		View.GetElementBorderPositionForTesting(TEXT("commander-system-frame"));
	TestTrue(
		TEXT("Merged StrategyControl defaults to the full-canvas CommanderOS iframe"),
		FMath::IsNearlyZero(InitialCommanderFramePosition.X, 1.0f));
	TestTrue(
		TEXT("CommanderOS switch event reveals the native strategy system"),
		View.DispatchHtmlEvent(TEXT("CommanderOS.SwitchSystem"), TEXT("strategy")));
	View.Update(FVector2D(1920.0, 1080.0), FPlatformTime::Seconds() + 1.1);
	TestTrue(
		TEXT("CommanderOS iframe enters its animated strategy-active state"),
		View.GetElementAttributeForTesting(TEXT("commander-system-frame"), TEXT("class"))
			.Contains(TEXT("is-strategy-active")));
	FString FinishSwitchResult;
	FString FinishSwitchError;
	TestTrue(
		TEXT("Strategy interaction checks can advance the iframe to its animation endpoint"),
		View.ExecuteJavaScript(
			TEXT("var f=document.getElementById('commander-system-frame'); f.style.display='block'; f.style.transition='none'; f.style.left=(-(window.innerWidth+64))+'px'; var a=document.getElementById('app'); a.style.animation='none'; a.style.transition='none'; a.style.transform='none'; a.style.opacity='1'; var m=document.getElementById('system-switch-mask'); m.style.display='none'; 'finished'"),
			FinishSwitchResult,
			FinishSwitchError));
	View.Update(FVector2D(1920.0, 1080.0), FPlatformTime::Seconds() + 1.7);
	TestTrue(
		TEXT("CommanderOS iframe reaches a width-independent point outside the logical canvas"),
		View.GetElementBorderPositionForTesting(TEXT("commander-system-frame")).X < -1900.0);
	FSlateWindowElementList StrategyControlElements(nullptr);
	View.Paint(
		FGeometry::MakeRoot(FVector2f(1920.0f, 1080.0f), FSlateLayoutTransform()),
		StrategyControlElements,
		0);
	TestTrue(TEXT("StrategyControl page emits no load failure"), LoadError.IsEmpty());
	TestEqual(TEXT("StrategyControl emits its typed ready route"), ReadyEvent.EventType, FName(TEXT("StrategyControl")));
	const FVector2D InitialSidePanelPosition = View.GetElementBorderPositionForTesting(TEXT("side-panel"));
	const FVector2D InitialSidePanelSize = View.GetElementBorderSizeForTesting(TEXT("side-panel"));
	AddInfo(FString::Printf(
		TEXT("StrategyControl initial side-panel geometry before DOM mutation: (%.1f, %.1f) %.1fx%.1f"),
		InitialSidePanelPosition.X,
		InitialSidePanelPosition.Y,
		InitialSidePanelSize.X,
		InitialSidePanelSize.Y));
	TestTrue(
		TEXT("StrategyControl right control panel is initially inside the desktop viewport"),
		InitialSidePanelPosition.X + InitialSidePanelSize.X <= 1920.5);
	FString GeometrySetupResult;
	FString GeometrySetupError;
	TestTrue(
		TEXT("StrategyControl layout containers can be tagged for geometry checks"),
		View.ExecuteJavaScript(
			TEXT("document.querySelector('.sc-map-stage').id='test-map-stage'; document.querySelector('.sc-rail').id='test-rail'; 'tagged'"),
			GeometrySetupResult,
			GeometrySetupError));
	View.Update(FVector2D(1920.0, 1080.0), FPlatformTime::Seconds() + 1.0);
	const FVector2D MapStagePosition = View.GetElementBorderPositionForTesting(TEXT("test-map-stage"));
	const FVector2D MapStageSize = View.GetElementBorderSizeForTesting(TEXT("test-map-stage"));
	const FVector2D RailPosition = View.GetElementBorderPositionForTesting(TEXT("test-rail"));
	const FVector2D RailSize = View.GetElementBorderSizeForTesting(TEXT("test-rail"));
	const FVector2D MapFramePosition = View.GetElementBorderPositionForTesting(TEXT("map-frame"));
	const FVector2D MapFrameSize = View.GetElementBorderSizeForTesting(TEXT("map-frame"));
	const FVector2D SidePanelPosition = View.GetElementBorderPositionForTesting(TEXT("side-panel"));
	const FVector2D SidePanelSize = View.GetElementBorderSizeForTesting(TEXT("side-panel"));
	AddInfo(FString::Printf(
		TEXT("StrategyControl geometry: map-stage=(%.1f, %.1f) %.1fx%.1f, rail=(%.1f, %.1f) %.1fx%.1f, map-frame=(%.1f, %.1f) %.1fx%.1f, side-panel=(%.1f, %.1f) %.1fx%.1f"),
		MapStagePosition.X,
		MapStagePosition.Y,
		MapStageSize.X,
		MapStageSize.Y,
		RailPosition.X,
		RailPosition.Y,
		RailSize.X,
		RailSize.Y,
		MapFramePosition.X,
		MapFramePosition.Y,
		MapFrameSize.X,
		MapFrameSize.Y,
		SidePanelPosition.X,
		SidePanelPosition.Y,
		SidePanelSize.X,
		SidePanelSize.Y));
	TestTrue(TEXT("StrategyControl right control panel receives visible width"), SidePanelSize.X >= 300.0);
	TestTrue(TEXT("StrategyControl right control panel receives visible height"), SidePanelSize.Y >= 300.0);
	TestTrue(TEXT("StrategyControl right control panel is placed to the right of the map"), SidePanelPosition.X > MapFramePosition.X + MapFrameSize.X);
	TestTrue(
		TEXT("StrategyControl right control panel remains inside the desktop viewport"),
		SidePanelPosition.X + SidePanelSize.X <= 1920.5);
	TestTrue(
		TEXT("External CSS ::before rule creates the side-panel decoration"),
		View.HasGeneratedPseudoElementForTesting(TEXT("side-panel"), true));

	const FVector2D MapViewportPosition = View.GetElementBorderPositionForTesting(TEXT("map-viewport"));
	const FVector2D MapViewportSize = View.GetElementBorderSizeForTesting(TEXT("map-viewport"));
	FString MapCellTagResult;
	FString MapCellTagError;
	View.ExecuteJavaScript(
		TEXT("document.querySelector('.sc-map-cell').id='test-map-cell'; 'tagged'"),
		MapCellTagResult,
		MapCellTagError);
	View.Update(FVector2D(1920.0, 1080.0), FPlatformTime::Seconds() + 1.1);
	const FVector2D InitialMapWorldPosition = View.GetElementBorderPositionForTesting(TEXT("map-world"));
	const FVector2D InitialMapBoardPosition = View.GetElementBorderPositionForTesting(TEXT("map-board"));
	const FVector2D InitialMapBoardSize = View.GetElementBorderSizeForTesting(TEXT("map-board"));
	const FVector2D InitialMapCellPosition = View.GetElementBorderPositionForTesting(TEXT("test-map-cell"));
	const FVector2D InitialMapCellSize = View.GetElementBorderSizeForTesting(TEXT("test-map-cell"));
	AddInfo(FString::Printf(
		TEXT("Strategy map geometry: viewport=(%.1f, %.1f) %.1fx%.1f, world=(%.1f, %.1f), board=(%.1f, %.1f) %.1fx%.1f, cell=(%.1f, %.1f) %.1fx%.1f"),
		MapViewportPosition.X, MapViewportPosition.Y, MapViewportSize.X, MapViewportSize.Y,
		InitialMapWorldPosition.X, InitialMapWorldPosition.Y,
		InitialMapBoardPosition.X, InitialMapBoardPosition.Y, InitialMapBoardSize.X, InitialMapBoardSize.Y,
		InitialMapCellPosition.X, InitialMapCellPosition.Y, InitialMapCellSize.X, InitialMapCellSize.Y));
	const FVector2D MapCenter = MapViewportPosition + MapViewportSize * 0.5f;
	const FVector2D MapZoomPivot = MapViewportPosition + FVector2D(MapViewportSize.X * 0.72f, MapViewportSize.Y * 0.37f);
	const FVector2D InitialMapWorldSize = View.GetElementBorderSizeForTesting(TEXT("map-world"));
	const FVector2D InitialPivotMapCoordinate(
		(MapZoomPivot.X - InitialMapWorldPosition.X) / InitialMapWorldSize.X,
		(MapZoomPivot.Y - InitialMapWorldPosition.Y) / InitialMapWorldSize.Y);
	TestTrue(
		TEXT("Mouse wheel over the strategy map is consumed by its browser-standard wheel listener"),
		View.ProcessMouseWheelForTesting(MapZoomPivot, 1.0f));
	View.Update(FVector2D(1920.0, 1080.0), FPlatformTime::Seconds() + 1.25);
	const FVector2D ZoomedMapWorldSize = View.GetElementBorderSizeForTesting(TEXT("map-world"));
	const FVector2D ZoomedMapWorldPosition = View.GetElementBorderPositionForTesting(TEXT("map-world"));
	const FVector2D ZoomedPivotMapCoordinate(
		(MapZoomPivot.X - ZoomedMapWorldPosition.X) / ZoomedMapWorldSize.X,
		(MapZoomPivot.Y - ZoomedMapWorldPosition.Y) / ZoomedMapWorldSize.Y);
	TestTrue(TEXT("Mouse wheel enlarges the strategy map world"), ZoomedMapWorldSize.X > InitialMapWorldSize.X + 10.0f);
	TestTrue(
		TEXT("Wheel zoom preserves the map coordinate beneath the off-center mouse pivot"),
		ZoomedPivotMapCoordinate.Equals(InitialPivotMapCoordinate, 0.002f));
	FString MapRowTagResult;
	FString MapRowTagError;
	TestTrue(
		TEXT("Strategy map row endpoints can be tagged after zoom"),
		View.ExecuteJavaScript(
			TEXT("var cells=document.querySelectorAll('.sc-map-cell'); cells[0].id='test-map-row-first'; cells[23].id='test-map-row-last'; 'tagged'"),
			MapRowTagResult,
			MapRowTagError));
	View.Update(FVector2D(1920.0, 1080.0), FPlatformTime::Seconds() + 1.27);
	const FVector2D FirstRowCellPosition = View.GetElementBorderPositionForTesting(TEXT("test-map-row-first"));
	const FVector2D LastRowCellPosition = View.GetElementBorderPositionForTesting(TEXT("test-map-row-last"));
	const FVector2D LastRowCellSize = View.GetElementBorderSizeForTesting(TEXT("test-map-row-last"));
	const FVector2D ZoomedMapBoardPosition = View.GetElementBorderPositionForTesting(TEXT("map-board"));
	const FVector2D ZoomedMapBoardSize = View.GetElementBorderSizeForTesting(TEXT("map-board"));
	TestTrue(
		TEXT("All 24 cells remain on the same first row at fractional zoom"),
		FMath::IsNearlyEqual(FirstRowCellPosition.Y, LastRowCellPosition.Y, 1.0f));
	TestTrue(
		TEXT("The final grid column remains attached to the map right edge"),
		FMath::IsNearlyEqual(
			LastRowCellPosition.X + LastRowCellSize.X,
			ZoomedMapBoardPosition.X + ZoomedMapBoardSize.X - 2.0f,
			1.0f));
	TestTrue(
		TEXT("Mouse wheel updates the visible strategy map zoom readout"),
		View.GetElementInnerRmlForTesting(TEXT("map-zoom")).Contains(TEXT("1.3")));
	TestTrue(
		TEXT("Opposite mouse wheel notch is consumed by the strategy map"),
		View.ProcessMouseWheelForTesting(MapZoomPivot, -1.0f));
	View.Update(FVector2D(1920.0, 1080.0), FPlatformTime::Seconds() + 1.3);
	TestTrue(
		TEXT("Opposite zoom notches restore the original map size"),
		View.GetElementBorderSizeForTesting(TEXT("map-world")).Equals(InitialMapWorldSize, 1.0f));
	TestTrue(
		TEXT("Opposite zoom notches restore the original map position"),
		View.GetElementBorderPositionForTesting(TEXT("map-world")).Equals(InitialMapWorldPosition, 1.0f));
	TestTrue(
		TEXT("Strategy map can zoom back in after a reversible wheel step"),
		View.ProcessMouseWheelForTesting(MapZoomPivot, 1.0f));
	View.Update(FVector2D(1920.0, 1080.0), FPlatformTime::Seconds() + 1.4);

	const FVector2D MapWorldPositionBeforeDrag = View.GetElementBorderPositionForTesting(TEXT("map-world"));
	TestTrue(TEXT("Strategy map accepts the start of a mouse drag"), View.ProcessMouseButtonDownForTesting(MapCenter));
	View.ProcessMouseMoveForTesting(MapCenter - FVector2D(48.0f, 32.0f));
	View.ProcessMouseButtonUpForTesting(MapCenter - FVector2D(48.0f, 32.0f));
	View.Update(FVector2D(1920.0, 1080.0), FPlatformTime::Seconds() + 1.5);
	const FVector2D MapWorldPositionAfterDrag = View.GetElementBorderPositionForTesting(TEXT("map-world"));
	TestTrue(
		TEXT("Dragging the enlarged strategy map changes its pan position"),
		!MapWorldPositionAfterDrag.Equals(MapWorldPositionBeforeDrag, 1.0f));
	View.Close();

	FH5UI_RuntimeView ScaledView([]() {}, [](const FString&) {}, [](const FH5UI_Event&) {});
	TestTrue(
		TEXT("StrategyControl loads inside a down-scaled UMG design surface"),
		ScaledView.LoadURL(TEXT("coui://uiresources/StrategyControl/strategy-control.html")));
	TestTrue(
		TEXT("Scaled StrategyControl can reveal its native system beneath CommanderOS"),
		ScaledView.DispatchHtmlEvent(TEXT("CommanderOS.SwitchSystem"), TEXT("strategy")));
	FString ScaledFinishSwitchResult;
	FString ScaledFinishSwitchError;
	ScaledView.ExecuteJavaScript(
		TEXT("var f=document.getElementById('commander-system-frame'); f.style.display='block'; f.style.transition='none'; f.style.left=(-(window.innerWidth+64))+'px'; var a=document.getElementById('app'); a.style.animation='none'; a.style.transition='none'; a.style.transform='none'; a.style.opacity='1'; var m=document.getElementById('system-switch-mask'); m.style.display='none'; 'finished'"),
		ScaledFinishSwitchResult,
		ScaledFinishSwitchError);
	ScaledView.Update(FVector2D(4324.0, 2252.0), FPlatformTime::Seconds() + 2.0, 0.4375f, 2.0f);
	FString ScaledViewportResult;
	FString ScaledViewportError;
	TestTrue(
		TEXT("Down-scaled UMG surface publishes browser viewport dimensions"),
		ScaledView.ExecuteJavaScript(TEXT("[innerWidth, innerHeight].join('|')"), ScaledViewportResult, ScaledViewportError));
	const FVector2D ScaledSidePanelPosition = ScaledView.GetElementBorderPositionForTesting(TEXT("side-panel"));
	const FVector2D ScaledSidePanelSize = ScaledView.GetElementBorderSizeForTesting(TEXT("side-panel"));
	const FVector2D ScaledMapFrameSize = ScaledView.GetElementBorderSizeForTesting(TEXT("map-frame"));
	AddInfo(FString::Printf(
		TEXT("Scaled StrategyControl geometry: map-frame=%.1fx%.1f, side-panel=(%.1f, %.1f) %.1fx%.1f"),
		ScaledMapFrameSize.X,
		ScaledMapFrameSize.Y,
		ScaledSidePanelPosition.X,
		ScaledSidePanelPosition.Y,
		ScaledSidePanelSize.X,
		ScaledSidePanelSize.Y));
	TestEqual(TEXT("Down-scaled UMG surface exposes its visible CSS viewport"), ScaledViewportResult, FString(TEXT("1892|985")));
	TestTrue(TEXT("StrategyControl right panel survives fractional UMG down-scaling"), ScaledSidePanelSize.X >= 500.0);
	TestTrue(
		TEXT("StrategyControl right panel remains inside the fractionally scaled CSS viewport"),
		ScaledSidePanelPosition.X + ScaledSidePanelSize.X <= 1892.5);
	TestTrue(
		TEXT("Fractional UMG scaling preserves the browser map frame size"),
		FMath::Abs(ScaledMapFrameSize.X - MapFrameSize.X) <= 50.0f);
	TestTrue(
		TEXT("Fractional UMG scaling preserves the browser side panel size"),
		FMath::Abs(ScaledSidePanelSize.X - SidePanelSize.X) <= 50.0f);
	const FVector2D ScaledMapViewportPosition = ScaledView.GetElementBorderPositionForTesting(TEXT("map-viewport"));
	const FVector2D ScaledMapViewportSize = ScaledView.GetElementBorderSizeForTesting(TEXT("map-viewport"));
	const FVector2D ScaledMapCenterInSlate =
		(ScaledMapViewportPosition + ScaledMapViewportSize * 0.5f) / 0.4375f;
	TestTrue(
		TEXT("Fractional UMG coordinates convert back to browser CSS pixels"),
		ScaledView.LocalToDocumentPosition(ScaledMapCenterInSlate).Equals(
			ScaledMapViewportPosition + ScaledMapViewportSize * 0.5f,
			1.0f));
	const FVector2D ScaledInitialMapWorldSize = ScaledView.GetElementBorderSizeForTesting(TEXT("map-world"));
	TestTrue(
		TEXT("Mouse wheel reaches the strategy map through fractional UMG coordinates"),
		ScaledView.ProcessMouseWheelForTesting(ScaledMapCenterInSlate, 1.0f));
	ScaledView.Update(FVector2D(4324.0, 2252.0), FPlatformTime::Seconds() + 2.25, 0.4375f, 2.0f);
	TestTrue(
		TEXT("Fractionally scaled strategy map zooms under the visible mouse pointer"),
		ScaledView.GetElementBorderSizeForTesting(TEXT("map-world")).X > ScaledInitialMapWorldSize.X + 10.0f);
	const FVector2D ScaledMapWorldPositionBeforeDrag = ScaledView.GetElementBorderPositionForTesting(TEXT("map-world"));
	TestTrue(
		TEXT("Fractionally scaled strategy map accepts mouse drag start"),
		ScaledView.ProcessMouseButtonDownForTesting(ScaledMapCenterInSlate));
	const FVector2D ScaledDragEndInSlate = ScaledMapCenterInSlate - FVector2D(32.0f, 24.0f) / 0.4375f;
	ScaledView.ProcessMouseMoveForTesting(ScaledDragEndInSlate);
	ScaledView.ProcessMouseButtonUpForTesting(ScaledDragEndInSlate);
	ScaledView.Update(FVector2D(4324.0, 2252.0), FPlatformTime::Seconds() + 2.5, 0.4375f, 2.0f);
	TestTrue(
		TEXT("Fractionally scaled strategy map pans with mouse drag"),
		!ScaledView.GetElementBorderPositionForTesting(TEXT("map-world")).Equals(ScaledMapWorldPositionBeforeDrag, 1.0f));
	ScaledView.Close();

	FH5UI_RuntimeView SvgView([]() {}, [](const FString&) {}, [](const FH5UI_Event&) {});
	TestTrue(
		TEXT("External SVG image fixture loads"),
		SvgView.LoadString(
			TEXT("<!doctype html><html><head><style>html,body{margin:0;background-color:#001018;}img{width:240px;height:120px;}</style></head><body>"
				"<script>var image=document.createElement('img');image.id='dynamic-svg';image.src='routes/route-h.svg';image.alt='route-h';document.body.appendChild(image);</script>"
				"</body></html>"),
			TEXT("coui://uiresources/StrategyControl/svg-render-test.html")));
	SvgView.Update(FVector2D(320.0, 180.0), FPlatformTime::Seconds() + 3.0);
	TestEqual(
		TEXT("DOM image src property reflects to the RmlUi src attribute"),
		SvgView.GetElementAttributeForTesting(TEXT("dynamic-svg"), TEXT("src")),
		FString(TEXT("routes/route-h.svg")));
	FSlateWindowElementList SvgElements(nullptr);
	SvgView.Paint(
		FGeometry::MakeRoot(FVector2f(320.0f, 180.0f), FSlateLayoutTransform()),
		SvgElements,
		0);
	TestTrue(TEXT("External SVG fixture submits draw batches"), SvgView.GetPerformanceStats().DrawBatches > 0);
	SvgView.Close();
	return true;
}

bool FH5UI_BrowserSubviewLayoutTest::RunTest(const FString& Parameters)
{
	FH5UI_RuntimeView View([]() {}, [](const FString&) {}, [](const FH5UI_Event&) {});
	TestTrue(
		TEXT("Document with iframe loads"),
		View.LoadString(
			TEXT("<!doctype html><html><head><style>iframe { position: absolute; left: 100px; top: 80px; width: 400px; height: 250px; }</style></head>"
				"<body><iframe id=\"browser\" src=\"coui://uiresources/CommanderOS/V2/commander-os2.html\"></iframe></body></html>"),
			TEXT("coui://uiresources/BaseControl/base-control.html")));
	View.Update(FVector2D(800.0, 600.0), FPlatformTime::Seconds());

	FH5UI_BrowserSubview Subview;
	TestTrue(TEXT("Visible iframe exposes a browser subview"), View.GetBrowserSubview(Subview));
	TestEqual(
		TEXT("Iframe preserves its resource URL"),
		Subview.Source,
		FString(TEXT("coui://uiresources/CommanderOS/V2/commander-os2.html")));
	TestTrue(TEXT("Iframe reports its CSS position"), Subview.Position.Equals(FVector2D(100.0, 80.0), 0.5));
	TestTrue(TEXT("Iframe reports its CSS size"), Subview.Size.Equals(FVector2D(400.0, 250.0), 0.5));

	FString Result;
	FString Error;
	TestTrue(
		TEXT("Page can hide the iframe dynamically"),
		View.ExecuteJavaScript(TEXT("document.getElementById('browser').style.display = 'none'; 'hidden'"), Result, Error));
	View.Update(FVector2D(800.0, 600.0), FPlatformTime::Seconds() + 1.0);
	TestFalse(TEXT("Hidden iframe no longer exposes a browser subview"), View.GetBrowserSubview(Subview));
	View.Close();
	return true;
}

bool FH5UI_RuntimeSmokeTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("Blocked UI resource outside its root"), EAutomationExpectedErrorFlags::Contains, 2);
	AddExpectedError(TEXT("interrupted"), EAutomationExpectedErrorFlags::Contains, 1);

	FH5UI_FileInterface& Files = FH5UI_Module::Get().GetFileInterface();
	const FString DemoPath = Files.ResolvePath(TEXT("h5ui://plugin/demo.rml"));
	const FString ShowcasePath = Files.ResolvePath(TEXT("h5ui://plugin/showcase.html"));
	const FString ShowcaseStylePath = Files.ResolvePath(TEXT("h5ui://plugin/showcase.css"));
	const FString ShowcaseScriptPath = Files.ResolvePath(TEXT("h5ui://plugin/showcase.js"));
	const FString ShowcaseImagePath = Files.ResolvePath(TEXT("h5ui://plugin/media/showcase.png"));
	const FString ShowcaseVideoPath = Files.ResolvePath(TEXT("h5ui://plugin/media/showcase.mp4"));
	const FString EventBridgePath = Files.ResolvePath(TEXT("h5ui://plugin/sdk/h5ui-bridge.js"));
	const FString VueSdkPath = Files.ResolvePath(TEXT("h5ui://plugin/sdk/h5ui-vue.global.js"));
	const FString VueMarkupPath = Files.ResolvePath(TEXT("h5ui://plugin/vue-basic/vue-basic.html"));
	const FString VueStylePath = Files.ResolvePath(TEXT("h5ui://plugin/vue-basic/vue-basic.css"));
	const FString VueScriptPath = Files.ResolvePath(TEXT("h5ui://plugin/vue-basic/vue-basic.js"));
	TestTrue(TEXT("Bundled demo resolves to a file"), FPaths::FileExists(DemoPath));
	TestTrue(TEXT("Bundled showcase resolves to a file"), FPaths::FileExists(ShowcasePath));
	TestTrue(TEXT("Bundled showcase CSS resolves to a file"), FPaths::FileExists(ShowcaseStylePath));
	TestTrue(TEXT("Bundled showcase JavaScript resolves to a file"), FPaths::FileExists(ShowcaseScriptPath));
	TestTrue(TEXT("Bundled showcase image resolves to a file"), FPaths::FileExists(ShowcaseImagePath));
	TestTrue(TEXT("Bundled showcase video resolves to a file"), FPaths::FileExists(ShowcaseVideoPath));
	TestTrue(TEXT("Bundled typed event bridge resolves to a file"), FPaths::FileExists(EventBridgePath));
	TestTrue(TEXT("Bundled Vue SDK resolves to a file"), FPaths::FileExists(VueSdkPath));
	TestTrue(TEXT("Bundled Vue example resolves to a file"), FPaths::FileExists(VueMarkupPath));
	TestTrue(TEXT("Bundled Vue example CSS resolves to a file"), FPaths::FileExists(VueStylePath));
	TestTrue(TEXT("Bundled Vue example JavaScript resolves to a file"), FPaths::FileExists(VueScriptPath));
	FString ShowcaseMarkup;
	TestTrue(TEXT("Bundled showcase markup can be read"), FFileHelper::LoadFileToString(ShowcaseMarkup, *ShowcasePath));
	FString ShowcaseStyle;
	TestTrue(TEXT("Bundled showcase CSS can be read"), FFileHelper::LoadFileToString(ShowcaseStyle, *ShowcaseStylePath));
	FString ShowcaseScript;
	TestTrue(TEXT("Bundled showcase JavaScript can be read"), FFileHelper::LoadFileToString(ShowcaseScript, *ShowcaseScriptPath));
	TestTrue(
		TEXT("Bundled showcase declares a transparent page background"),
		ShowcaseStyle.Contains(TEXT("background-color: transparent"), ESearchCase::CaseSensitive));
	TestTrue(
		TEXT("Bundled showcase declares transparent pointer pass-through surfaces"),
		ShowcaseStyle.Contains(TEXT("pointer-events: none"), ESearchCase::CaseSensitive) &&
			ShowcaseStyle.Contains(TEXT("pointer-events: auto"), ESearchCase::CaseSensitive));
	TestTrue(
		TEXT("Bundled showcase includes native CSS entrance animation"),
		ShowcaseStyle.Contains(TEXT("@keyframes surface-enter"), ESearchCase::CaseSensitive) &&
			ShowcaseStyle.Contains(TEXT("animation: surface-enter"), ESearchCase::CaseSensitive));
	TestTrue(
		TEXT("Bundled showcase includes native CSS brand animation"),
		ShowcaseStyle.Contains(TEXT("@keyframes brand-enter"), ESearchCase::CaseSensitive) &&
			ShowcaseStyle.Contains(TEXT("animation: brand-enter"), ESearchCase::CaseSensitive));
	TestTrue(
		TEXT("Native checkbox and radio indicators keep stable layout while toggled"),
		ShowcaseStyle.Contains(TEXT("flex: 0 0 16px"), ESearchCase::CaseSensitive) &&
			ShowcaseStyle.Contains(TEXT("opacity: 1"), ESearchCase::CaseSensitive) &&
			!ShowcaseStyle.Contains(TEXT(".native-choice input:checked + .native-choice-indicator .native-choice-mark {\n\tdisplay: block"), ESearchCase::CaseSensitive));
	TestTrue(
		TEXT("Showcase uses one page-level scroll container"),
		ShowcaseStyle.Contains(TEXT("overflow: hidden"), ESearchCase::CaseSensitive) &&
			ShowcaseStyle.Contains(TEXT(".viewport"), ESearchCase::CaseSensitive) &&
			ShowcaseStyle.Contains(TEXT("overflow-y: auto"), ESearchCase::CaseSensitive) &&
			!ShowcaseStyle.Contains(TEXT("height: 252px"), ESearchCase::CaseSensitive));
	const FString TrimmedShowcaseMarkup = ShowcaseMarkup.TrimStart();
	TestTrue(
		TEXT("Bundled showcase declares the standard HTML doctype"),
		TrimmedShowcaseMarkup.StartsWith(TEXT("<!doctype html>"), ESearchCase::IgnoreCase));
	TestTrue(
		TEXT("Bundled showcase keeps a browser-compatible HTML root"),
		ShowcaseMarkup.Contains(TEXT("<html>"), ESearchCase::IgnoreCase));
	TestTrue(
		TEXT("Bundled showcase loads the typed event bridge before page code"),
		ShowcaseMarkup.Contains(TEXT("<script src=\"sdk/h5ui-bridge.js\"></script>"), ESearchCase::IgnoreCase) &&
			ShowcaseMarkup.Contains(TEXT("<script src=\"showcase.js\"></script>"), ESearchCase::IgnoreCase));
	TestTrue(
		TEXT("Bundled showcase exercises standard DOM events and timers"),
		ShowcaseScript.Contains(TEXT("addEventListener"), ESearchCase::CaseSensitive) &&
			ShowcaseScript.Contains(TEXT("setTimeout"), ESearchCase::CaseSensitive) &&
			ShowcaseScript.Contains(TEXT("createElement"), ESearchCase::CaseSensitive));
	TestFalse(
		TEXT("Bundled showcase does not advertise unsupported drag and drop"),
		ShowcaseMarkup.Contains(TEXT("native-drag"), ESearchCase::IgnoreCase) ||
			ShowcaseScript.Contains(TEXT("dragstart"), ESearchCase::IgnoreCase) ||
			ShowcaseScript.Contains(TEXT("dataTransfer"), ESearchCase::IgnoreCase));
	TestTrue(
		TEXT("Bundled showcase demonstrates colour palette CSS customization"),
		ShowcaseStyle.Contains(TEXT("#native-color colorpicker"), ESearchCase::CaseSensitive) &&
			ShowcaseStyle.Contains(TEXT("#native-color coloroption"), ESearchCase::CaseSensitive));
	TestTrue(
		TEXT("Bundled showcase exercises the typed Unreal JavaScript bridge"),
		ShowcaseScript.Contains(TEXT("window.H5UI.emit"), ESearchCase::CaseSensitive));
	TestFalse(
		TEXT("Bundled showcase does not expose an RmlUi-only root"),
		ShowcaseMarkup.Contains(TEXT("<rml"), ESearchCase::IgnoreCase));
	FString VueMarkup;
	TestTrue(TEXT("Bundled Vue markup can be read"), FFileHelper::LoadFileToString(VueMarkup, *VueMarkupPath));
	TestTrue(
		TEXT("Bundled Vue example uses a classic script"),
		VueMarkup.Contains(TEXT("<script src=\"vue-basic.js\"></script>"), ESearchCase::IgnoreCase));
	TestFalse(
		TEXT("Bundled Vue example does not require ES modules"),
		VueMarkup.Contains(TEXT("type=\"module\""), ESearchCase::IgnoreCase));
	TestTrue(
		TEXT("Plugin URL traversal is rejected"),
		Files.ResolvePath(TEXT("h5ui://plugin/../../H5UIPlugin.uplugin")).IsEmpty());
	TestTrue(
		TEXT("Project URL traversal is rejected"),
		Files.ResolvePath(TEXT("coui://uiresources/../../SilverChoir.uproject")).IsEmpty());
	FH5UI_SystemInterface PathJoiner;
	Rml::String JoinedResourcePath;
	PathJoiner.JoinPath(
		JoinedResourcePath,
		Rml::String("h5ui://plugin/showcase.html"),
		Rml::String("media/showcase.png"));
	TestEqual(
		TEXT("Relative resources preserve their plugin URL root"),
		FString(UTF8_TO_TCHAR(JoinedResourcePath.c_str())),
		FString(TEXT("h5ui://plugin/media/showcase.png")));

	bool bReady = false;
	FString LoadError;
	FString JavaScriptError;
	FH5UI_Event CapturedEvent;
	int32 CapturedEventCount = 0;
	FH5UI_RuntimeView* RuntimeView = nullptr;
	FH5UI_RuntimeView View(
		[&bReady]() { bReady = true; },
		[&LoadError](const FString& Error) { LoadError = Error; },
		[&CapturedEvent, &CapturedEventCount, &RuntimeView](const FH5UI_Event& Event)
		{
			CapturedEvent = Event;
			++CapturedEventCount;
			if (RuntimeView)
			{
				RuntimeView->SetData(TEXT("last.event"), Event.Name.ToString());
				RuntimeView->SynchronizeModels();
			}
		},
		[&JavaScriptError](const FString& Error) { JavaScriptError = Error; });
	RuntimeView = &View;

	const FString BrowserCompatibilityMarkup = TEXT(
		"<!doctype html><html><head><style>"
		"body { font-family: Roboto, Arial, sans-serif; }"
		"#font-stack { font-size: 20px; }"
		"#percentage-circle { width: 100px; height: 100px; border: 2px solid #48bfff; border-radius: 50%; }"
		"#order-row { display: flex; width: 300px; }"
		".order-item { width: 40px; height: 10px; }"
		"#order-a { order: 3; } #order-b { order: 1; } #order-c { order: 2; }"
		"#single-translate { width: 10px; height: 10px; transform: translate(5px)scale(.9); }"
		"#centered-button { width: 120px; height: 40px; padding: 0; border: 1px solid #48bfff; font-size: 10px; }"
		"#mixed-button { display: flex; align-items: center; justify-content: flex-start; width: 160px; height: 40px; padding: 0; }"
		"#mixed-button span { margin-right: 8px; }"
		"#vshow-flex { display: flex; width: 300px; height: 20px; }"
		"#vshow-flex-left, #vshow-flex-right { width: 90px; }"
		"#vshow-flex-middle { flex: 1; min-width: 40px; }"
		"</style></head><body>"
		"<div id=\"font-stack\">Silver Choir</div>"
		"<div id=\"percentage-circle\"></div>"
		"<div id=\"order-row\"><div id=\"order-a\" class=\"order-item\"></div><div id=\"order-b\" class=\"order-item\"></div><div id=\"order-c\" class=\"order-item\"></div></div>"
		"<div id=\"single-translate\"></div>"
		"<button id=\"centered-button\">Centered</button>"
		"<button id=\"mixed-button\"><span>&lt;</span>Back to menu</button>"
		"<div id=\"vshow-flex\"><div id=\"vshow-flex-left\"></div><div id=\"vshow-flex-middle\"></div><div id=\"vshow-flex-right\"></div></div>"
		"<span id=\"inline-style-read\">waiting</span>"
		"<span id=\"computed-style-read\">waiting</span>"
		"<span id=\"ue-page-event\">waiting</span>"
		"<script>"
		"var vshowFlex = document.getElementById('vshow-flex');"
		"document.getElementById('inline-style-read').textContent = vshowFlex.style.display === '' ? 'inline-empty' : vshowFlex.style.display;"
		"document.getElementById('computed-style-read').textContent = getComputedStyle(vshowFlex).display;"
		"vshowFlex.style.display = 'none'; vshowFlex.style.display = '';"
		"window.addEventListener('AutomationPageEnter', function(event) { document.getElementById('ue-page-event').textContent = event.detail; });"
		"</script>"
		"</body></html>");
	TestTrue(
		TEXT("Browser-compatible font stacks and percentage radii document loads"),
		View.LoadString(BrowserCompatibilityMarkup, TEXT("coui://uiresources/compatibility-test.html")));
	View.Update(FVector2D(640.0, 480.0), FPlatformTime::Seconds());
	TestTrue(
		TEXT("Comma-separated browser font stacks resolve to a native font face"),
		View.HasElementFontFaceForTesting(TEXT("font-stack")));
	TestTrue(
		TEXT("Default font rasterization uses one physical pixel per CSS pixel"),
		FMath::IsNearlyEqual(View.GetElementFontRasterizationScaleForTesting(TEXT("font-stack")), 1.0f));
	TestTrue(
		TEXT("Unreal can dispatch a custom event directly to the loaded page"),
		View.DispatchHtmlEvent(TEXT("AutomationPageEnter"), TEXT("enter-from-unreal")));
	TestEqual(
		TEXT("Page JavaScript receives the Unreal-dispatched event detail"),
		View.GetElementInnerRmlForTesting(TEXT("ue-page-event")),
		FString(TEXT("enter-from-unreal")));
	CapturedEvent = FH5UI_Event();
	CapturedEventCount = 0;
	FString TypedEventResult;
	FString TypedEventError;
	TestTrue(
		TEXT("JavaScript can emit a typed H5 UI event"),
		View.ExecuteJavaScript(
			TEXT("__silverUeEmitTyped('Automation', 'TypedMessage', ['string value', 123], 'typed-source'); 'typed-ok'"),
			TypedEventResult,
			TypedEventError));
	TestEqual(TEXT("Typed event preserves its map key"), CapturedEvent.EventType, FName(TEXT("Automation")));
	TestEqual(TEXT("Typed event preserves its target function name"), CapturedEvent.Name, FName(TEXT("TypedMessage")));
	TestTrue(TEXT("Typed event serializes arguments as an array"), CapturedEvent.Payload.Contains(TEXT("string value")) && CapturedEvent.Payload.Contains(TEXT("123")));
	TestEqual(TEXT("Typed event preserves its source element id"), CapturedEvent.ElementId, FString(TEXT("typed-source")));
	const FVector2D CircleBorderSize = View.GetElementBorderSizeForTesting(TEXT("percentage-circle"));
	const FVector2D CircleBorderPosition = View.GetElementBorderPositionForTesting(TEXT("percentage-circle"));
	FString BoundingRectResult;
	FString BoundingRectError;
	TestTrue(
		TEXT("getBoundingClientRect is available to browser-compatible drag code"),
		View.ExecuteJavaScript(
			TEXT("var r=document.getElementById('percentage-circle').getBoundingClientRect(); [r.left,r.top,r.width,r.height].join('|')"),
			BoundingRectResult,
			BoundingRectError));
	TArray<FString> BoundingRectFields;
	BoundingRectResult.ParseIntoArray(BoundingRectFields, TEXT("|"), false);
	TestEqual(TEXT("getBoundingClientRect returns all four layout fields"), BoundingRectFields.Num(), 4);
	if (BoundingRectFields.Num() == 4)
	{
		TestTrue(
			TEXT("getBoundingClientRect left matches native layout"),
			FMath::IsNearlyEqual(FCString::Atof(*BoundingRectFields[0]), CircleBorderPosition.X, 0.1f));
		TestTrue(
			TEXT("getBoundingClientRect top matches native layout"),
			FMath::IsNearlyEqual(FCString::Atof(*BoundingRectFields[1]), CircleBorderPosition.Y, 0.1f));
		TestTrue(
			TEXT("getBoundingClientRect width matches native layout"),
			FMath::IsNearlyEqual(FCString::Atof(*BoundingRectFields[2]), CircleBorderSize.X, 0.1f));
		TestTrue(
			TEXT("getBoundingClientRect height matches native layout"),
			FMath::IsNearlyEqual(FCString::Atof(*BoundingRectFields[3]), CircleBorderSize.Y, 0.1f));
	}
	const float CircleRadius = View.GetElementTopLeftBorderRadiusForTesting(TEXT("percentage-circle"));
	TestTrue(
		TEXT("Percentage border radius resolves against the rendered element size"),
		FMath::IsNearlyEqual(
			CircleRadius,
			0.5f * FMath::Min(CircleBorderSize.X, CircleBorderSize.Y),
			0.1f));
	const float OrderAX = View.GetElementBorderPositionForTesting(TEXT("order-a")).X;
	const float OrderBX = View.GetElementBorderPositionForTesting(TEXT("order-b")).X;
	const float OrderCX = View.GetElementBorderPositionForTesting(TEXT("order-c")).X;
	TestTrue(TEXT("Flex order changes visual item order while preserving stable layout"), OrderBX < OrderCX && OrderCX < OrderAX);
	TestTrue(
		TEXT("Single-argument translate and adjacent transform functions are accepted"),
		View.HasElementTransformForTesting(TEXT("single-translate")));
	const FVector2D ButtonPosition = View.GetElementBorderPositionForTesting(TEXT("centered-button"));
	const FVector2D ButtonSize = View.GetElementBorderSizeForTesting(TEXT("centered-button"));
	const FVector2D ButtonCenter = ButtonPosition + ButtonSize * 0.5;
	const FVector2D ButtonTextCenter = View.GetDirectTextVisualCenterForTesting(TEXT("centered-button"));
	TestEqual(
		TEXT("Native HTML button creates a rendered line for its direct text node"),
		View.GetDirectTextLineCountForTesting(TEXT("centered-button")),
		1);
	TestTrue(
		TEXT("Native HTML button centers its text horizontally like a browser control"),
		FMath::IsNearlyEqual(ButtonCenter.X, ButtonTextCenter.X, 1.0));
	TestTrue(
		TEXT("Native HTML button centers its text vertically like a browser control"),
		FMath::IsNearlyEqual(ButtonCenter.Y, ButtonTextCenter.Y, 1.0));
	TestEqual(
		TEXT("Mixed flex button keeps the direct label after its icon"),
		View.GetDirectTextLineCountForTesting(TEXT("mixed-button")),
		1);
	TestEqual(
		TEXT("CSSStyleDeclaration reads only inline styles instead of cascaded stylesheet values"),
		View.GetElementInnerRmlForTesting(TEXT("inline-style-read")),
		FString(TEXT("inline-empty")));
	TestEqual(
		TEXT("getComputedStyle exposes the cascaded stylesheet value separately"),
		View.GetElementInnerRmlForTesting(TEXT("computed-style-read")),
		FString(TEXT("flex")));
	const float VShowLeftX = View.GetElementBorderPositionForTesting(TEXT("vshow-flex-left")).X;
	const float VShowMiddleX = View.GetElementBorderPositionForTesting(TEXT("vshow-flex-middle")).X;
	const float VShowRightX = View.GetElementBorderPositionForTesting(TEXT("vshow-flex-right")).X;
	TestTrue(
		TEXT("Clearing an inline display value restores the stylesheet flex layout used by Vue v-show"),
		VShowLeftX < VShowMiddleX && VShowMiddleX < VShowRightX);

	const FString HtmlCssCapabilityMarkup = TEXT(
		"<!doctype html><html><head><title>Capability Test</title>"
		"<style>"
		"#gradient { width: 80px; height: 20px; background-image: linear-gradient(90deg, #1b3049, #5ed1a8); }"
		"#image-fit { width: 80px; height: 20px; background-image: url(h5ui://plugin/media/showcase.png); background-size: cover; }"
		"#sticky-declaration { position: relative; inset: 0; border: 1px dashed rgba(90, 86, 78, 0.55); }"
		"#browser-reset { border: none; border-bottom: none; object-fit: cover; object-position: center center; min-height: auto; background: 0 0; }"
		"</style></head><body>"
		"<header id=\"tag-header\"><nav><a id=\"tag-anchor\">Link</a></nav></header>"
		"<main><section><article><aside><div id=\"tag-div\"><span>Text</span><p>Paragraph</p>"
		"<h1>H1</h1><h2>H2</h2><h3>H3</h3><h4>H4</h4><h5>H5</h5><h6>H6</h6>"
		"<ul><li>One</li></ul><ol><li>Two</li></ol>"
		"<table><thead><tr><th>Head</th></tr></thead><tbody><tr><td>Cell</td></tr></tbody></table>"
		"<button id=\"tag-button\">Button</button><input id=\"tag-input\" /><input id=\"tag-color\" type=\"color\" value=\"#5ed1a8\"/><textarea id=\"tag-textarea\"></textarea>"
		"<select id=\"tag-select\"><option>Option</option></select><label id=\"tag-label\">Label</label>"
		"<img id=\"tag-image\"><progress id=\"tag-progress\" value=\"0.5\"></progress><svg id=\"tag-svg\"></svg>"
		"</div></aside></article></section></main><footer id=\"tag-footer\">Footer</footer>"
		"<form id=\"tag-form\"><fieldset id=\"tag-fieldset\"><legend id=\"tag-legend\">Legend</legend><output id=\"tag-output\">Output</output></fieldset></form>"
		"<figure id=\"tag-figure\"><figcaption id=\"tag-figcaption\">Caption</figcaption></figure><details id=\"tag-details\" open=\"open\"><summary id=\"tag-summary\">Summary</summary></details>"
		"<dl id=\"tag-dl\"><dt id=\"tag-dt\">Term</dt><dd id=\"tag-dd\">Description</dd></dl><blockquote id=\"tag-blockquote\"><cite id=\"tag-cite\">Cite</cite></blockquote>"
		"<pre id=\"tag-pre\"><code id=\"tag-code\">Code</code></pre><hr id=\"tag-hr\"><address id=\"tag-address\">Address</address><div id=\"gradient\"></div><div id=\"image-fit\"></div><div id=\"sticky-declaration\"></div><div id=\"browser-reset\"></div>"
		"</body></html>");
	TestTrue(
		TEXT("HTML/CSS capability document with native HTML tags loads"),
		View.LoadString(HtmlCssCapabilityMarkup, TEXT("h5ui://plugin/html-css-capability-test.html")));
	View.Update(FVector2D(640.0, 480.0), FPlatformTime::Seconds());
	for (const FString& ElementId : {
			FString(TEXT("tag-header")), FString(TEXT("tag-anchor")), FString(TEXT("tag-div")),
			FString(TEXT("tag-button")), FString(TEXT("tag-input")), FString(TEXT("tag-color")), FString(TEXT("tag-textarea")),
			FString(TEXT("tag-select")), FString(TEXT("tag-label")), FString(TEXT("tag-image")),
			FString(TEXT("tag-progress")), FString(TEXT("tag-svg")), FString(TEXT("tag-footer")),
			FString(TEXT("tag-form")), FString(TEXT("tag-fieldset")), FString(TEXT("tag-legend")),
			FString(TEXT("tag-output")), FString(TEXT("tag-figure")), FString(TEXT("tag-figcaption")),
			FString(TEXT("tag-details")), FString(TEXT("tag-summary")), FString(TEXT("tag-dl")),
			FString(TEXT("tag-dt")), FString(TEXT("tag-dd")), FString(TEXT("tag-blockquote")),
			FString(TEXT("tag-cite")), FString(TEXT("tag-pre")), FString(TEXT("tag-code")),
			FString(TEXT("tag-hr")), FString(TEXT("tag-address"))})
	{
		TestEqual(
			FString::Printf(TEXT("HTML capability tag '%s' remains addressable in the DOM"), *ElementId),
			View.GetElementAttributeForTesting(ElementId, TEXT("id")),
			ElementId);
	}
	TestTrue(TEXT("Native colour input can open its preset palette"), View.ClickElementForTesting(TEXT("tag-color")));
	View.Update(FVector2D(640.0, 480.0), FPlatformTime::Seconds());
	const FVector2D ColorPickerSize = View.GetDescendantBorderSizeForTesting(TEXT("tag-color"), TEXT("colorpicker"));
	const FVector2D ColorValueSize = View.GetDescendantBorderSizeForTesting(TEXT("tag-color"), TEXT("colorvalue"));
	TestTrue(TEXT("Native colour palette has visible geometry when opened"), ColorPickerSize.X >= 100.0 && ColorPickerSize.Y >= 40.0);
	TestTrue(TEXT("Native colour input renders the selected-colour preview"), ColorValueSize.X > 0.0 && ColorValueSize.Y > 0.0);
	View.Update(FVector2D(640.0, 480.0), FPlatformTime::Seconds() + 0.1, 2.0f);
	TestTrue(
		TEXT("High-DPI view resolves a scale-specific high-resolution font face"),
		FMath::IsNearlyEqual(View.GetElementFontRasterizationScaleForTesting(TEXT("font-stack")), 2.0f));
	const FVector2D HighDpiCircleBorderSize = View.GetElementBorderSizeForTesting(TEXT("percentage-circle"));
	TestTrue(
		TEXT("High-DPI font rasterization preserves CSS layout dimensions"),
		CircleBorderSize.Equals(HighDpiCircleBorderSize, 0.1));
	View.Update(FVector2D(640.0, 480.0), FPlatformTime::Seconds() + 0.15, 1.25f, 1.5f);
	TestTrue(
		TEXT("Automatic screen density and additional render scale select the expected font resolution"),
		FMath::IsNearlyEqual(View.GetElementFontRasterizationScaleForTesting(TEXT("font-stack")), 1.875f));
	TestTrue(
		TEXT("Automatic screen density reports the effective physical render resolution"),
		View.GetPerformanceStats().RenderSize == FIntPoint(1200, 900));
	FString ViewportMetricsResult;
	FString ViewportMetricsError;
	TestTrue(
		TEXT("JavaScript exposes browser and Silver render scale variables"),
		View.ExecuteJavaScript(
			TEXT("[innerWidth, innerHeight, devicePixelRatio, silverRenderScale, silverEffectivePixelRatio, silverRenderWidth, silverRenderHeight].join('|')"),
			ViewportMetricsResult,
			ViewportMetricsError));
	TestEqual(
		TEXT("JavaScript viewport variables match the automatic render resolution"),
		ViewportMetricsResult,
		FString(TEXT("640|480|1.25|1.5|1.875|1200|900")));
	View.Update(FVector2D(640.0, 480.0), FPlatformTime::Seconds() + 0.2, 0.5f, 4.0f);
	TestTrue(
		TEXT("Maximum render scale selects a four-times supersampled font face under a downscaled UMG parent"),
		FMath::IsNearlyEqual(View.GetElementFontRasterizationScaleForTesting(TEXT("font-stack")), 4.0f));
	View.Update(FVector2D(640.0, 480.0), FPlatformTime::Seconds() + 0.2, 1.0f);

	auto PaintCompatibilityDocument = [&View, this](const FString& Markup, const TCHAR* Description)
	{
		TestTrue(Description, View.LoadString(Markup, TEXT("coui://uiresources/render-fallback-test.html")));
		View.Update(FVector2D(640.0, 480.0), FPlatformTime::Seconds());
		FSlateWindowElementList Elements(nullptr);
		View.Paint(
			FGeometry::MakeRoot(FVector2f(640.0f, 480.0f), FSlateLayoutTransform()),
			Elements,
			0);
		return View.GetPerformanceStats();
	};
	const FString BasicPanelMarkup = TEXT(
		"<!doctype html><html><head><style>"
		"html, body { margin: 0; background-color: transparent; }"
		"#panel { width: 200px; height: 100px; background-color: #050d14e8; border: 1px solid #214d6d; }"
		"</style></head><body><div id=\"panel\"></div></body></html>");
	const FString ShadowPanelMarkup = TEXT(
		"<!doctype html><html><head><style>"
		"html, body { margin: 0; background-color: transparent; }"
		"#panel { width: 200px; height: 100px; background-color: #050d14e8; border: 1px solid #214d6d; box-shadow: 0 0 24px #02070bb8; }"
		"</style></head><body><div id=\"panel\"></div></body></html>");
	const FH5UI_PerformanceStats BasicPanelStats = PaintCompatibilityDocument(
		BasicPanelMarkup,
		TEXT("Basic translucent panel document loads"));
	const FH5UI_PerformanceStats ShadowPanelStats = PaintCompatibilityDocument(
		ShadowPanelMarkup,
		TEXT("Panel with browser box-shadow document loads"));
	TestEqual(
		TEXT("Basic box-shadow fallback remains in the element background draw batch"),
		ShadowPanelStats.DrawBatches,
		BasicPanelStats.DrawBatches);
	TestTrue(
		TEXT("Basic box-shadow fallback adds rounded shadow geometry"),
		ShadowPanelStats.Vertices > BasicPanelStats.Vertices);
	TestTrue(
		TEXT("Basic box-shadow fallback adds shadow triangles without an offscreen layer"),
		ShadowPanelStats.Triangles > BasicPanelStats.Triangles);

	bReady = false;
	LoadError.Reset();
	JavaScriptError.Reset();
	CapturedEvent = FH5UI_Event();
	CapturedEventCount = 0;

	TestTrue(
		TEXT("Silver Choir start menu loads through the configured project resource root"),
		View.LoadURL(TEXT("coui://uiresources/MainMenu/start-menu.html")));
	View.Update(FVector2D(1920.0, 1080.0), FPlatformTime::Seconds());
	TestTrue(TEXT("Silver Choir start menu emits no load error"), LoadError.IsEmpty());
	TestTrue(TEXT("Silver Choir start menu emits no JavaScript error"), JavaScriptError.IsEmpty());
	TestTrue(
		TEXT("Silver Choir start menu starts in the explicit before-enter state"),
		View.GetElementAttributeForTesting(TEXT("start-screen"), TEXT("class")).Contains(TEXT("screen-before-enter")));
	TestTrue(
		TEXT("Unreal can explicitly select the before-enter state"),
		View.DispatchHtmlEvent(TEXT("SilverUIBeforeEnter"), TEXT("{\"reason\":\"automation\"}")));
	TestTrue(
		TEXT("SilverUIBeforeEnter keeps the page at the entrance start frame"),
		View.GetElementAttributeForTesting(TEXT("start-screen"), TEXT("class")).Contains(TEXT("screen-before-enter")));
	TestTrue(
		TEXT("Unreal can start the page entrance through SilverUIEnter"),
		View.DispatchHtmlEvent(TEXT("SilverUIEnter"), TEXT("{\"reason\":\"automation\"}")));
	TestTrue(
		TEXT("SilverUIEnter applies the page entrance animation state"),
		View.GetElementAttributeForTesting(TEXT("start-screen"), TEXT("class")).Contains(TEXT("screen-entering")));
	TestTrue(TEXT("Silver Choir New Game button can be clicked"), View.ClickElementForTesting(TEXT("menu-new-game")));
	FPlatformProcess::SleepNoStats(0.27f);
	View.Update(FVector2D(1920.0, 1080.0), FPlatformTime::Seconds());
	TestEqual(TEXT("NewGame waits while the full-screen exit animation begins"), CapturedEventCount, 0);
	TestTrue(
		TEXT("New Game action applies the full-screen exit state"),
		View.GetElementAttributeForTesting(TEXT("start-screen"), TEXT("class")).Contains(TEXT("screen-exiting")));
	FPlatformProcess::SleepNoStats(0.65f);
	View.Update(FVector2D(1920.0, 1080.0), FPlatformTime::Seconds());
	FPlatformProcess::SleepNoStats(0.1f);
	View.Update(FVector2D(1920.0, 1080.0), FPlatformTime::Seconds());
	TestEqual(TEXT("New Game action emits one typed handler event after the exit animation"), CapturedEventCount, 1);
	TestEqual(TEXT("New Game action uses the NewGame handler type"), CapturedEvent.EventType, FName(TEXT("NewGame")));
	TestEqual(TEXT("New Game action invokes StartGame on its handler"), CapturedEvent.Name, FName(TEXT("StartGame")));
	TestTrue(TEXT("New Game handler payload includes the selected language"), CapturedEvent.Payload.Contains(TEXT("zh-CN")));
	TestTrue(
		TEXT("New Game keeps the page in the after-leave state when its exit animation completes"),
		View.GetElementAttributeForTesting(TEXT("start-screen"), TEXT("class")).Contains(TEXT("screen-after-leave")));
	TestTrue(
		TEXT("Unreal can explicitly select the after-leave state"),
		View.DispatchHtmlEvent(TEXT("SilverUIAfterLeave"), TEXT("{\"reason\":\"automation\"}")));
	TestTrue(
		TEXT("SilverUIAfterLeave keeps the page at the exit end frame"),
		View.GetElementAttributeForTesting(TEXT("start-screen"), TEXT("class")).Contains(TEXT("screen-after-leave")));
	TestTrue(
		TEXT("Unreal can replay the page entrance after leaving"),
		View.DispatchHtmlEvent(TEXT("SilverUIEnter"), TEXT("{\"reason\":\"automation\"}")));
	TestTrue(
		TEXT("SilverUIEnter restores the page entrance animation state"),
		View.GetElementAttributeForTesting(TEXT("start-screen"), TEXT("class")).Contains(TEXT("screen-entering")));
	FPlatformProcess::SleepNoStats(0.7f);
	View.Update(FVector2D(1920.0, 1080.0), FPlatformTime::Seconds());
	TestTrue(
		TEXT("SilverUIEnter keeps its completed entrance state instead of switching back"),
		View.GetElementAttributeForTesting(TEXT("start-screen"), TEXT("class")).Contains(TEXT("screen-entering")));

	bReady = false;
	LoadError.Reset();
	JavaScriptError.Reset();
	CapturedEvent = FH5UI_Event();
	CapturedEventCount = 0;

	TestTrue(
		TEXT("Silver Choir base control HUD loads through the project resource root"),
		View.LoadURL(TEXT("coui://uiresources/BaseControl/base-control.html")));
	View.Update(FVector2D(1920.0, 1080.0), FPlatformTime::Seconds());
	TestTrue(TEXT("Base control HUD emits no load error"), LoadError.IsEmpty());
	TestTrue(TEXT("Base control HUD emits no JavaScript error"), JavaScriptError.IsEmpty());
	TestEqual(
		TEXT("Base control HUD formats the initial currency without relying on browser Intl"),
		View.GetElementInnerRmlForTesting(TEXT("currency-amount")),
		FString(TEXT("128,460")));
	TestEqual(
		TEXT("Base control HUD exposes the initial strategic clock"),
		View.GetElementInnerRmlForTesting(TEXT("strategic-clock")),
		FString(TEXT("21:40")));
	TestEqual(
		TEXT("Base control HUD resolves its currency icon through the Unreal asset URL scheme"),
		View.GetElementAttributeForTesting(TEXT("currency-icon"), TEXT("src")),
		FString(TEXT("ueasset:///Game/Resources/Texture/Currency/val-currency-symbol-white.val-currency-symbol-white")));
	TestFalse(
		TEXT("The clear 3D viewport area does not capture pointer input"),
		View.IsPointerInteractingAtForTesting(FVector2D(960.0, 540.0)));
	TestTrue(
		TEXT("The scene navigation panel captures pointer input"),
		View.IsPointerInteractingAtForTesting(FVector2D(150.0, 180.0)));

	CapturedEvent = FH5UI_Event();
	CapturedEventCount = 0;
	TestTrue(TEXT("Direct scene switch can select the underground base"), View.ClickElementForTesting(TEXT("scene-haven")));
	View.Update(FVector2D(1920.0, 1080.0), FPlatformTime::Seconds());
	TestEqual(TEXT("Scene selection emits one Unreal event"), CapturedEventCount, 1);
	TestEqual(TEXT("Scene selection uses the documented event name"), CapturedEvent.Name, FName(TEXT("BaseSceneChanged")));
	TestTrue(TEXT("Scene selection payload identifies the underground base"), CapturedEvent.Payload.Contains(TEXT("\"scene\":\"underground\"")));
	TestTrue(TEXT("Scene selection payload includes the active room"), CapturedEvent.Payload.Contains(TEXT("\"room\":\"command-room\"")));
	TestTrue(TEXT("Scene selection payload includes the localized scene name"), CapturedEvent.Payload.Contains(TEXT("\"sceneName\":\"地下基地\"")));
	TestTrue(TEXT("Scene selection payload includes the localized room name"), CapturedEvent.Payload.Contains(TEXT("\"roomName\":\"指挥室\"")));
	TestTrue(
		TEXT("Current room card reflects the underground command room"),
		View.GetElementInnerRmlForTesting(TEXT("current-location-toggle")).Contains(TEXT("指挥室")));

	TestTrue(TEXT("Current room selector can be opened"), View.ClickElementForTesting(TEXT("current-location-toggle")));
	View.Update(FVector2D(1920.0, 1080.0), FPlatformTime::Seconds());
	TestEqual(TEXT("Opening the current room selector emits no Unreal event"), CapturedEventCount, 1);
	TestTrue(
		TEXT("Underground room selector exposes its command room camera target"),
		View.GetElementInnerRmlForTesting(TEXT("location-command-room")).Contains(TEXT("指挥室")));
	TestTrue(TEXT("Command room camera target can be selected"), View.ClickElementForTesting(TEXT("location-command-room")));
	View.Update(FVector2D(1920.0, 1080.0), FPlatformTime::Seconds());
	TestEqual(TEXT("Room selection shares the scene navigation event name"), CapturedEvent.Name, FName(TEXT("BaseSceneChanged")));
	TestTrue(TEXT("Room selection payload identifies the current scene"), CapturedEvent.Payload.Contains(TEXT("\"scene\":\"underground\"")));
	TestTrue(TEXT("Room selection payload identifies the command room"), CapturedEvent.Payload.Contains(TEXT("\"room\":\"command-room\"")));

	TestTrue(TEXT("Current room selector can be reopened"), View.ClickElementForTesting(TEXT("current-location-toggle")));
	View.Update(FVector2D(1920.0, 1080.0), FPlatformTime::Seconds());
	TestTrue(TEXT("A non-default underground room can be selected"), View.ClickElementForTesting(TEXT("location-readiness-room")));
	View.Update(FVector2D(1920.0, 1080.0), FPlatformTime::Seconds());
	TestTrue(TEXT("Non-default room selection is emitted"), CapturedEvent.Payload.Contains(TEXT("\"room\":\"readiness-room\"")));
	TestTrue(
		TEXT("Inventory layout accepts the registered SIS base slot size"),
		View.DispatchHtmlEvent(
			TEXT("SISH5UI.Inventory.LayoutConfiguration"),
			TEXT("{\"protocolVersion\":1,\"baseSlotWidth\":30,\"baseSlotHeight\":30,\"referenceViewportHeight\":1080}")));
	View.Update(FVector2D(1920.0, 1080.0), FPlatformTime::Seconds());
	FString InventoryGridProbeResult;
	FString InventoryGridProbeError;
	TestTrue(
		TEXT("Inventory grid diagnostic elements can be identified through the DOM facade"),
		View.ExecuteJavaScript(
			TEXT("document.querySelector('.inventory-container').id='native-inventory-container';")
			TEXT("document.querySelector('.inventory-grid').id='native-inventory-grid';")
			TEXT("document.querySelector('.inventory-grid-cell').id='native-inventory-grid-cell';")
			TEXT("'ready'"),
			InventoryGridProbeResult,
			InventoryGridProbeError));
	View.Update(FVector2D(1920.0, 1080.0), FPlatformTime::Seconds());
	const FVector2D NativeContainerSize = View.GetElementBorderSizeForTesting(TEXT("native-inventory-container"));
	const FVector2D NativeGridPosition = View.GetElementBorderPositionForTesting(TEXT("native-inventory-grid"));
	const FVector2D NativeGridSize = View.GetElementBorderSizeForTesting(TEXT("native-inventory-grid"));
	const FVector2D NativeGridCellSize = View.GetElementBorderSizeForTesting(TEXT("native-inventory-grid-cell"));
	FString ElementFromPointResult;
	FString ElementFromPointError;
	TestTrue(
		TEXT("document.elementFromPoint resolves the native element under view-local coordinates"),
		View.ExecuteJavaScript(
			TEXT("var r=document.querySelector('#native-inventory-grid').getBoundingClientRect();")
			TEXT("var h=document.elementFromPoint(r.left+r.width*0.5,r.top+r.height*0.5);")
			TEXT("var g=h&&h.closest('.inventory-grid'); g?g.id:'';"),
			ElementFromPointResult,
			ElementFromPointError));
	TestEqual(
		TEXT("elementFromPoint hit can be resolved to its inventory grid"),
		ElementFromPointResult,
		FString(TEXT("native-inventory-grid")));
	TestTrue(
		TEXT("Inventory inline-block background expands around its fixed-size grid like Chrome"),
		NativeContainerSize.X >= NativeGridSize.X + 16.0 && NativeContainerSize.X <= NativeGridSize.X + 20.0);
	TestTrue(
		TEXT("Inventory grid cells use the registered SIS base slot dimensions"),
		NativeGridCellSize.Equals(FVector2D(30.0, 30.0), 0.1));

	const FVector2D OriginalPlayerItemPosition =
		View.GetElementBorderPositionForTesting(TEXT("inventory-item-sample-rifle"));
	const FVector2D OriginalCharacterItemPosition =
		View.GetElementBorderPositionForTesting(TEXT("inventory-item-character-sidearm"));
	TestTrue(
		TEXT("A protocol-v1 snapshot initializes the player slot before its runtime inventoryId is known"),
		View.DispatchHtmlEvent(
			TEXT("SISH5UI.Inventory.Snapshot"),
			TEXT("{\"protocolVersion\":1,\"slot\":\"inventory\",\"inventory\":{")
			TEXT("\"inventoryId\":\"runtime-player-inventory\",\"revision\":2,\"items\":[")
			TEXT("{\"itemId\":\"sample-rifle\",\"displayName\":\"Rifle\",\"itemType\":\"Weapon\",")
			TEXT("\"containerIndex\":0,\"row\":1,\"column\":1,\"rowSpan\":2,\"columnSpan\":3,")
			TEXT("\"stackSize\":1,\"maxStackSize\":1}]}}")));
	View.Update(FVector2D(1920.0, 1080.0), FPlatformTime::Seconds());
	const FVector2D UpdatedPlayerItemPosition =
		View.GetElementBorderPositionForTesting(TEXT("inventory-item-sample-rifle"));
	TestTrue(
		TEXT("The raw snapshot updates the matching player inventory item coordinates"),
		UpdatedPlayerItemPosition.Equals(OriginalPlayerItemPosition + FVector2D(30.0, 30.0), 0.1));
	TestTrue(
		TEXT("Updating the player inventory does not mutate the character inventory"),
		View.GetElementBorderPositionForTesting(TEXT("inventory-item-character-sidearm"))
			.Equals(OriginalCharacterItemPosition, 0.1));

	TestTrue(
		TEXT("A protocol-v1 slot initializes character inventory independently of inventoryId"),
		View.DispatchHtmlEvent(
			TEXT("SISH5UI.Inventory.Snapshot"),
			TEXT("{\"protocolVersion\":1,\"slot\":\"characterInventory\",\"inventory\":{")
			TEXT("\"inventoryId\":\"runtime-character-inventory\",\"revision\":2,\"items\":[")
			TEXT("{\"itemId\":\"character-sidearm\",\"displayName\":\"Sidearm\",\"itemType\":\"Weapon\",")
			TEXT("\"containerIndex\":0,\"row\":1,\"column\":1,\"rowSpan\":2,\"columnSpan\":2,")
			TEXT("\"stackSize\":1,\"maxStackSize\":1}]}}")));
	View.Update(FVector2D(1920.0, 1080.0), FPlatformTime::Seconds());
	TestTrue(
		TEXT("The character snapshot updates only the inventory with the matching inventoryId"),
		View.GetElementBorderPositionForTesting(TEXT("inventory-item-character-sidearm"))
			.Equals(OriginalCharacterItemPosition + FVector2D(30.0, 30.0), 0.1));
	TestTrue(
		TEXT("Updating the character inventory preserves the player inventory position"),
		View.GetElementBorderPositionForTesting(TEXT("inventory-item-sample-rifle"))
			.Equals(UpdatedPlayerItemPosition, 0.1));

	const FString SampleDragItemId = TEXT("inventory-item-sample-rifle");
	const FVector2D SampleDragItemPosition = View.GetElementBorderPositionForTesting(SampleDragItemId);
	const FVector2D SampleDragItemSize = View.GetElementBorderSizeForTesting(SampleDragItemId);
	const FVector2D SampleDragStart = SampleDragItemPosition + SampleDragItemSize * 0.5f;
	CapturedEvent = FH5UI_Event();
	CapturedEventCount = 0;
	TestTrue(
		TEXT("Inventory item accepts a native pointer press"),
		View.ProcessMouseButtonDownForTesting(SampleDragStart));
	TestTrue(
		TEXT("Inventory item keeps pointer interaction while the drag threshold is crossed"),
		View.ProcessMouseMoveForTesting(SampleDragStart + FVector2D(12.0f, 0.0f)));
	TestEqual(TEXT("Crossing the inventory drag threshold emits one Unreal event"), CapturedEventCount, 1);
	TestEqual(TEXT("Inventory drag emits the documented begin event"), CapturedEvent.Name, FName(TEXT("SISH5UI.Drag.Begin")));
	TestTrue(
		TEXT("Inventory drag begin identifies protocol v1"),
		CapturedEvent.Payload.Contains(TEXT("\"protocolVersion\":1")));
	TestTrue(
		TEXT("Inventory drag begin payload carries the source item id"),
		CapturedEvent.Payload.Contains(TEXT("\"itemId\":\"sample-rifle\"")));
	TestTrue(
		TEXT("Inventory drag begin payload carries the unambiguous source inventory id"),
		CapturedEvent.Payload.Contains(TEXT("\"inventoryId\":\"runtime-player-inventory\"")));
	TestTrue(
		TEXT("Inventory drag width equals base slot width times item columns"),
		CapturedEvent.Payload.Contains(TEXT("\"visualWidth\":90")));
	TestTrue(
		TEXT("Inventory drag height equals base slot height times item rows"),
		CapturedEvent.Payload.Contains(TEXT("\"visualHeight\":60")));
	TestTrue(
		TEXT("Inventory drag pointer is horizontally centered"),
		CapturedEvent.Payload.Contains(TEXT("\"grabOffsetX\":45")));
	TestTrue(
		TEXT("Inventory drag pointer is vertically centered"),
		CapturedEvent.Payload.Contains(TEXT("\"grabOffsetY\":30")));

	const FString ProbeSessionId = TEXT("11111111-2222-3333-4444-555555555555");
	TestTrue(
		TEXT("Unreal can acknowledge the first inventory pointer session"),
		View.DispatchHtmlEvent(
			TEXT("SISH5UI.Drag.State"),
			FString::Printf(
				TEXT("{\"protocolVersion\":1,\"accepted\":true,\"sessionId\":\"%s\",\"itemId\":\"sample-rifle\"}"),
				*ProbeSessionId)));
	const FVector2D ProbePosition = NativeGridPosition + FVector2D(31.0f, 31.0f);
	const FString DragOverPayload = FString::Printf(
		TEXT("{\"protocolVersion\":1,\"sessionId\":\"%s\",\"itemId\":\"sample-rifle\",\"clientX\":%.3f,\"clientY\":%.3f,")
		TEXT("\"grabOffsetX\":0,\"grabOffsetY\":0,\"rowSpan\":2,\"columnSpan\":3}"),
		*ProbeSessionId,
		ProbePosition.X,
		ProbePosition.Y);
	TestTrue(
		TEXT("A native external drag-over event is delivered to the inventory page"),
		View.DispatchHtmlEvent(TEXT("SISH5UI.Drag.Over"), DragOverPayload));
	TestEqual(
		TEXT("Entering a new candidate cell emits one inventory acceptance probe"),
		CapturedEvent.Name,
		FName(TEXT("SISH5UI.Drag.ProbeRequest")));
	TestTrue(
		TEXT("The acceptance probe identifies the cell under the pointer"),
		CapturedEvent.Payload.Contains(TEXT("\"hoverRow\":1")));
	TestTrue(
		TEXT("The acceptance probe identifies the hovered column"),
		CapturedEvent.Payload.Contains(TEXT("\"hoverColumn\":1")));
	TestTrue(
		TEXT("The acceptance probe preserves the pointer quadrant for native placement"),
		CapturedEvent.Payload.Contains(TEXT("\"quadrant\":\"top-left\"")));
	const int32 ProbeEventCount = CapturedEventCount;
	TestTrue(
		TEXT("A repeated drag-over event is delivered to the inventory page"),
		View.DispatchHtmlEvent(TEXT("SISH5UI.Drag.Over"), DragOverPayload));
	TestEqual(
		TEXT("Remaining in the same candidate cell does not issue another acceptance probe"),
		CapturedEventCount,
		ProbeEventCount);

	const FString ProbeResultPayload = FString::Printf(
		TEXT("{\"protocolVersion\":1,\"sessionId\":\"%s\",\"itemId\":\"sample-rifle\",\"targetInventoryId\":\"runtime-player-inventory\",")
		TEXT("\"targetContainerIndex\":0,\"hoverRow\":1,\"hoverColumn\":1,\"quadrant\":\"top-left\",")
		TEXT("\"targetRow\":0,\"targetColumn\":0,\"rowSpan\":2,\"columnSpan\":3,\"handleMode\":\"move\",\"accepted\":true}"),
		*ProbeSessionId);
	TestTrue(
		TEXT("The inventory page accepts the authoritative probe result"),
		View.DispatchHtmlEvent(TEXT("SISH5UI.Drag.ProbeResult"), ProbeResultPayload));
	FString ValidPreviewCount;
	FString PreviewError;
	TestTrue(
		TEXT("The accepted item footprint can be inspected through the native DOM"),
		View.ExecuteJavaScript(
			TEXT("document.querySelectorAll('.inventory-grid-cell-drop-valid').length"),
			ValidPreviewCount,
			PreviewError));
	TestEqual(
		TEXT("A two-by-three item highlights all six receiving cells"),
		ValidPreviewCount,
		FString(TEXT("6")));

	const FString ReplacementProbeResultPayload = FString::Printf(
		TEXT("{\"protocolVersion\":1,\"sessionId\":\"%s\",\"itemId\":\"sample-rifle\",\"targetInventoryId\":\"runtime-player-inventory\",")
		TEXT("\"targetContainerIndex\":0,\"hoverRow\":1,\"hoverColumn\":1,\"quadrant\":\"top-left\",")
		TEXT("\"targetRow\":0,\"targetColumn\":0,\"rowSpan\":2,\"columnSpan\":3,\"handleMode\":\"replace\",\"willReplace\":true,\"accepted\":true,")
		TEXT("\"moves\":[{\"itemId\":\"sample-rifle\",\"primaryItem\":true,\"targetInventoryId\":\"runtime-player-inventory\",\"containerIndex\":0,\"row\":0,\"column\":0,\"rowSpan\":2,\"columnSpan\":3},")
		TEXT("{\"itemId\":\"replaced-item\",\"primaryItem\":false,\"targetInventoryId\":\"runtime-player-inventory\",\"containerIndex\":0,\"row\":4,\"column\":7,\"rowSpan\":1,\"columnSpan\":1}]}"),
		*ProbeSessionId);
	TestTrue(
		TEXT("The inventory page accepts an authoritative multi-item replacement plan"),
		View.DispatchHtmlEvent(TEXT("SISH5UI.Drag.ProbeResult"), ReplacementProbeResultPayload));
	FString ReplacementPreviewCount;
	TestTrue(
		TEXT("Replacement destinations can be inspected through the native DOM"),
		View.ExecuteJavaScript(
			TEXT("document.querySelectorAll('.inventory-grid-cell-drop-replacement').length"),
			ReplacementPreviewCount,
			PreviewError));
	TestEqual(
		TEXT("The replacement item destination is rendered independently from the primary footprint"),
		ReplacementPreviewCount,
		FString(TEXT("1")));
	TestTrue(
		TEXT("Leaving the HTML view clears its inventory drop preview"),
		View.DispatchHtmlEvent(
			TEXT("SISH5UI.Drag.Leave"),
			FString::Printf(
				TEXT("{\"protocolVersion\":1,\"sessionId\":\"%s\",\"itemId\":\"sample-rifle\"}"),
				*ProbeSessionId)));
	FString PreviewCountAfterLeave;
	TestTrue(
		TEXT("The cleared inventory preview can be inspected through the native DOM"),
		View.ExecuteJavaScript(
			TEXT("document.querySelectorAll('.inventory-grid-cell-drop-preview').length"),
			PreviewCountAfterLeave,
			PreviewError));
	TestEqual(
		TEXT("Leaving the HTML view removes every receiving-cell state"),
		PreviewCountAfterLeave,
		FString(TEXT("0")));
	View.ProcessMouseButtonUpForTesting(SampleDragStart + FVector2D(12.0f, 0.0f));
	TestEqual(TEXT("Releasing the inventory drag emits its end event"), CapturedEvent.Name, FName(TEXT("SISH5UI.Drag.End")));
	TestTrue(
		TEXT("Unreal can authoritatively close the first inventory pointer session"),
		View.DispatchHtmlEvent(
			TEXT("SISH5UI.Drag.Result"),
			FString::Printf(
				TEXT("{\"protocolVersion\":1,\"sessionId\":\"%s\",\"itemId\":\"sample-rifle\",\"reason\":\"test-complete\"}"),
				*ProbeSessionId)));

	TestTrue(TEXT("Switching to Haven uses the direct scene button"), View.ClickElementForTesting(TEXT("scene-underground")));
	View.Update(FVector2D(1920.0, 1080.0), FPlatformTime::Seconds());
	TestTrue(TEXT("Haven scene switch resets to its default hall"), CapturedEvent.Payload.Contains(TEXT("\"room\":\"hall\"")));
	TestTrue(TEXT("Switching back to the underground base uses the direct scene button"), View.ClickElementForTesting(TEXT("scene-haven")));
	View.Update(FVector2D(1920.0, 1080.0), FPlatformTime::Seconds());
	TestTrue(TEXT("Underground scene switch resets to its default command room"), CapturedEvent.Payload.Contains(TEXT("\"room\":\"command-room\"")));

	TestTrue(TEXT("Strategic time can be paused"), View.ClickElementForTesting(TEXT("time-play")));
	View.Update(FVector2D(1920.0, 1080.0), FPlatformTime::Seconds());
	TestEqual(TEXT("Time pause uses the documented event name"), CapturedEvent.Name, FName(TEXT("TimeControlChanged")));
	TestTrue(TEXT("Paused time emits zero speed"), CapturedEvent.Payload.Contains(TEXT("\"speed\":0")));
	TestTrue(TEXT("Paused time emits its paused state"), CapturedEvent.Payload.Contains(TEXT("\"paused\":true")));
	TestEqual(
		TEXT("Pause control updates its visible state"),
		View.GetElementInnerRmlForTesting(TEXT("time-state-label")),
		FString(TEXT("时间停止")));

	TestTrue(TEXT("Fast-forward control selects five-times speed"), View.ClickElementForTesting(TEXT("time-fast")));
	View.Update(FVector2D(1920.0, 1080.0), FPlatformTime::Seconds());
	TestTrue(TEXT("Five-times speed is emitted to Unreal"), CapturedEvent.Payload.Contains(TEXT("\"speed\":5")));
	TestTrue(TEXT("Fast-forward resumes strategic time"), CapturedEvent.Payload.Contains(TEXT("\"paused\":false")));
	TestTrue(TEXT("Fast-forward control selects ten-times speed"), View.ClickElementForTesting(TEXT("time-fast")));
	View.Update(FVector2D(1920.0, 1080.0), FPlatformTime::Seconds());
	TestEqual(
		TEXT("Fast-forward label advances to ten-times speed"),
		View.GetElementInnerRmlForTesting(TEXT("time-speed-label")),
		FString(TEXT("×10")));
	TestTrue(TEXT("Fast-forward control selects twenty-times speed"), View.ClickElementForTesting(TEXT("time-fast")));
	View.Update(FVector2D(1920.0, 1080.0), FPlatformTime::Seconds());
	TestEqual(
		TEXT("Fast-forward label advances to twenty-times speed"),
		View.GetElementInnerRmlForTesting(TEXT("time-speed-label")),
		FString(TEXT("×20")));

	TestTrue(TEXT("Management menu launcher can be clicked"), View.ClickElementForTesting(TEXT("base-menu-open")));
	View.Update(FVector2D(1920.0, 1080.0), FPlatformTime::Seconds());
	TestEqual(TEXT("Management menu launcher uses the documented event name"), CapturedEvent.Name, FName(TEXT("OpenBaseMenuRequested")));
	TestTrue(TEXT("Management menu payload keeps the current scene"), CapturedEvent.Payload.Contains(TEXT("\"scene\":\"underground\"")));
	TestTrue(TEXT("Management menu payload keeps the current camera target"), CapturedEvent.Payload.Contains(TEXT("\"location\":\"command-room\"")));

	TestTrue(
		TEXT("Unreal can push the complete base HUD state through a single HTML event"),
		View.DispatchHtmlEvent(
			TEXT("SilverBaseHUDState"),
			TEXT("{\"scene\":\"haven\",\"location\":\"kitchen\",\"currency\":9876543,\"currencyDelta\":-125,\"paused\":true,\"speed\":0,\"time\":{\"month\":12,\"day\":7,\"hour\":6,\"minute\":5}}")));
	View.Update(FVector2D(1920.0, 1080.0), FPlatformTime::Seconds());
	TestEqual(
		TEXT("UE-pushed currency updates the HUD"),
		View.GetElementInnerRmlForTesting(TEXT("currency-amount")),
		FString(TEXT("9,876,543")));
	TestEqual(
		TEXT("UE-pushed time updates the HUD"),
		View.GetElementInnerRmlForTesting(TEXT("strategic-clock")),
		FString(TEXT("06:05")));
	TestTrue(
		TEXT("UE-pushed Haven state restores the selected scene"),
		View.GetElementInnerRmlForTesting(TEXT("scene-haven")).Contains(TEXT("酒馆")));
	TestTrue(
		TEXT("UE-pushed kitchen target restores the selected location"),
		View.GetElementInnerRmlForTesting(TEXT("current-location-toggle")).Contains(TEXT("厨房")));

	const FString PageDataJson =
		TEXT("{\"currency\":24680,\"currencyDelta\":320,\"time\":{\"month\":3,\"day\":14,\"hour\":15,\"minute\":9}}");
	View.SetData(TEXT("pageData"), PageDataJson);
	TestTrue(
		TEXT("The generic page-data event can update the loaded Vue page"),
		View.DispatchHtmlEvent(TEXT("H5UIPageData"), PageDataJson));
	View.Update(FVector2D(1920.0, 1080.0), FPlatformTime::Seconds());
	TestEqual(
		TEXT("Generic page data updates the base HUD currency"),
		View.GetElementInnerRmlForTesting(TEXT("currency-amount")),
		FString(TEXT("24,680")));
	TestEqual(
		TEXT("Generic page data updates the base HUD clock"),
		View.GetElementInnerRmlForTesting(TEXT("strategic-clock")),
		FString(TEXT("15:09")));

	bReady = false;
	LoadError.Reset();
	JavaScriptError.Reset();
	CapturedEvent = FH5UI_Event();
	CapturedEventCount = 0;

	TestTrue(TEXT("Bundled native document loads"), View.LoadURL(TEXT("h5ui://plugin/demo.rml")));
	TestTrue(TEXT("Ready callback is emitted"), bReady);
	TestTrue(TEXT("No load error is emitted"), LoadError.IsEmpty());
	TestTrue(TEXT("Native demo emits no JavaScript error"), JavaScriptError.IsEmpty());
	TestEqual(TEXT("View enters ready state"), View.GetState(), EH5UI_ViewState::Ready);

	View.SetData(TEXT("player.name"), TEXT("Automation"));
	View.SynchronizeModels();
	TestEqual(TEXT("Model synchronization emits no UE events"), CapturedEventCount, 0);
	TestTrue(TEXT("Input can be focused by element id"), View.FocusElementById(TEXT("player-name")));
	TestTrue(TEXT("Focused text input is detected as editable"), View.HasEditableFocus());
	TestTrue(
		TEXT("Focused input consumes keyboard text"),
		View.ProcessKeyChar(FCharacterEvent(TEXT('A'), FModifierKeysState(), 0u, false)));
	TestEqual(TEXT("Keyboard edit emits the declared UE event"), CapturedEvent.Name, FName(TEXT("PlayerNameChanged")));
	TestEqual(TEXT("Keyboard edit emits exactly one UE event"), CapturedEventCount, 1);
	TestEqual(TEXT("Keyboard event identifies its element"), CapturedEvent.ElementId, FString(TEXT("player-name")));
	TestTrue(TEXT("Keyboard event contains the edited value"), CapturedEvent.Payload.Contains(TEXT("A")));
	View.BlurFocusedElement();
	TestFalse(TEXT("Blurred text input releases editable focus"), View.HasEditableFocus());

	auto MeasureUpdates = [&View](const FVector2D& Size)
	{
		constexpr int32 SampleCount = 60;
		double TotalSeconds = 0.0;
		for (int32 SampleIndex = 0; SampleIndex < SampleCount; ++SampleIndex)
		{
			FPlatformProcess::SleepNoStats(0.005f);
			View.SetData(TEXT("benchmark.frame"), FString::FromInt(SampleIndex));
			const double CurrentTime = FPlatformTime::Seconds();
			const double StartTime = FPlatformTime::Seconds();
			View.Update(Size, CurrentTime);
			TotalSeconds += FPlatformTime::Seconds() - StartTime;
		}
		return TotalSeconds * 1000.0 / static_cast<double>(SampleCount);
	};

	View.SetTargetFrameRate(240);
	const double FullHdUpdateMs = MeasureUpdates(FVector2D(1920.0, 1080.0));
	FSlateWindowElementList FullHdElements(nullptr);
	View.Paint(
		FGeometry::MakeRoot(FVector2f(1920.0f, 1080.0f), FSlateLayoutTransform()),
		FullHdElements,
		0);
	const FH5UI_PerformanceStats FullHdStats = View.GetPerformanceStats();

	const double FourKUpdateMs = MeasureUpdates(FVector2D(3840.0, 2160.0));
	FSlateWindowElementList FourKElements(nullptr);
	View.Paint(
		FGeometry::MakeRoot(FVector2f(3840.0f, 2160.0f), FSlateLayoutTransform()),
		FourKElements,
		0);
	const FH5UI_PerformanceStats FourKStats = View.GetPerformanceStats();

	TestTrue(TEXT("Native render produces draw batches"), FourKStats.DrawBatches > 0);
	TestTrue(TEXT("Native render produces vertices"), FourKStats.Vertices > 0);
	TestTrue(TEXT("Native render produces triangles"), FourKStats.Triangles > 0);
	TestEqual(TEXT("Draw batches are resolution independent"), FourKStats.DrawBatches, FullHdStats.DrawBatches);
	TestEqual(TEXT("Vertex count is resolution independent"), FourKStats.Vertices, FullHdStats.Vertices);
	TestEqual(TEXT("Triangle count is resolution independent"), FourKStats.Triangles, FullHdStats.Triangles);
	AddInfo(FString::Printf(
		TEXT("Native update average: 1080p %.4f ms, 4K %.4f ms; 4K render submission %.4f ms, %d batches, %d vertices."),
		FullHdUpdateMs,
		FourKUpdateMs,
		FourKStats.SubmitMilliseconds,
		FourKStats.DrawBatches,
		FourKStats.Vertices));

	bReady = false;
	LoadError.Reset();
	JavaScriptError.Reset();
	CapturedEventCount = 0;
	TestTrue(TEXT("Full native showcase document loads"), View.LoadURL(TEXT("h5ui://plugin/showcase.html")));
	TestTrue(TEXT("Showcase emits ready callback"), bReady);
	TestTrue(TEXT("Showcase emits no load error"), LoadError.IsEmpty());
	TestTrue(TEXT("Showcase external script emits no JavaScript error"), JavaScriptError.IsEmpty());
	TestEqual(
		TEXT("Showcase external script updates the DOM during load"),
		View.GetElementInnerRmlForTesting(TEXT("js-status")),
		FString(TEXT("JavaScript ready")));
	TestEqual(
		TEXT("Showcase external script records its native runtime"),
		View.GetElementAttributeForTesting(TEXT("js-status"), TEXT("data-runtime")),
		FString(TEXT("h5-ui-plugin")));

	const int32 EventsBeforeJavaScriptClick = CapturedEventCount;
	TestTrue(TEXT("JavaScript demo button can be clicked"), View.ClickElementForTesting(TEXT("js-increment")));
	TestEqual(
		TEXT("JavaScript click listener updates text content"),
		View.GetElementInnerRmlForTesting(TEXT("js-counter")),
		FString(TEXT("1")));
	TestEqual(
		TEXT("JavaScript dataset writes through to a data attribute"),
		View.GetElementAttributeForTesting(TEXT("js-counter"), TEXT("data-count")),
		FString(TEXT("1")));
	TestEqual(TEXT("JavaScript click emits one UE event"), CapturedEventCount, EventsBeforeJavaScriptClick + 1);
	TestEqual(TEXT("JavaScript event keeps its declared name"), CapturedEvent.Name, FName(TEXT("JavaScriptCounterChanged")));
	TestEqual(TEXT("JavaScript event identifies its source element"), CapturedEvent.ElementId, FString(TEXT("js-increment")));
	TestTrue(TEXT("JavaScript object payload is serialized as JSON"), CapturedEvent.Payload.Contains(TEXT("\"count\":1")));

	TestTrue(TEXT("JavaScript can create a DOM node from a click listener"), View.ClickElementForTesting(TEXT("js-create-node")));
	TestEqual(
		TEXT("JavaScript-created DOM node is attached and visible"),
		View.GetElementInnerRmlForTesting(TEXT("js-created-1")),
		FString(TEXT("Node 1")));
	TestEqual(TEXT("DOM creation emits its UE event"), CapturedEvent.Name, FName(TEXT("JavaScriptNodeCreated")));

	TestTrue(TEXT("JavaScript timer button can be clicked"), View.ClickElementForTesting(TEXT("js-deferred")));
	TestEqual(
		TEXT("JavaScript timer exposes its pending state immediately"),
		View.GetElementInnerRmlForTesting(TEXT("js-status")),
		FString(TEXT("Timer is waiting...")));
	FPlatformProcess::SleepNoStats(0.08f);
	View.Update(FVector2D(1920.0, 1080.0), FPlatformTime::Seconds());
	TestEqual(
		TEXT("JavaScript timeout callback updates the DOM"),
		View.GetElementInnerRmlForTesting(TEXT("js-status")),
		FString(TEXT("Timer completed")));
	TestEqual(TEXT("JavaScript timeout emits its UE event"), CapturedEvent.Name, FName(TEXT("JavaScriptTimerCompleted")));

	FString JavaScriptResult;
	FString JavaScriptExecutionError;
	TestTrue(
		TEXT("Blueprint JavaScript API executes DOM and style operations"),
		View.ExecuteJavaScript(
			TEXT("var status = document.querySelector('#js-status'); status.dataset.testValue = 'dom-ok'; status.style.color = '#69b8f2'; status.dataset.testValue;"),
			JavaScriptResult,
			JavaScriptExecutionError));
	TestTrue(TEXT("Blueprint JavaScript API reports no execution error"), JavaScriptExecutionError.IsEmpty());
	TestEqual(TEXT("Blueprint JavaScript API returns the final expression"), JavaScriptResult, FString(TEXT("dom-ok")));
	TestEqual(
		TEXT("Executed JavaScript writes DOM attributes"),
		View.GetElementAttributeForTesting(TEXT("js-status"), TEXT("data-test-value")),
		FString(TEXT("dom-ok")));

	TestTrue(
		TEXT("JavaScript can exchange data with Unreal"),
		View.ExecuteJavaScript(
			TEXT("ue.setData('last.event', 'Set by JavaScript'); ue.getData('last.event');"),
			JavaScriptResult,
			JavaScriptExecutionError));
	TestEqual(TEXT("JavaScript reads back Unreal data"), JavaScriptResult, FString(TEXT("Set by JavaScript")));
	TestEqual(
		TEXT("JavaScript data writes refresh bound page content"),
		View.GetElementInnerRmlForTesting(TEXT("last-event-output")),
		FString(TEXT("Set by JavaScript")));
	TestTrue(TEXT("Interactive showcase emits no JavaScript error"), JavaScriptError.IsEmpty());
	CapturedEventCount = 0;
	View.SetData(TEXT("player.name"), TEXT("Automation Pilot"));
	View.SetData(TEXT("player.level"), TEXT("27"));
	View.SetData(TEXT("system.connected"), TEXT("online"));
	View.SetData(TEXT("session.status"), TEXT("Ready"));
	View.SetData(TEXT("mission.region"), TEXT("coast"));
	View.SetData(TEXT("mission.telemetry"), TEXT("true"));
	View.SetData(TEXT("mission.route"), TEXT("safe"));
	View.SetData(TEXT("mission.signal"), TEXT("65"));
	View.SetData(TEXT("mission.readiness"), TEXT("82"));
	View.SynchronizeModels();
	TestEqual(TEXT("Showcase model synchronization emits no UE events"), CapturedEventCount, 0);
	TestTrue(
		TEXT("Showcase checkbox starts checked from its model"),
		View.HasElementAttributeForTesting(TEXT("telemetry"), TEXT("checked")));
	const int32 EventsBeforeCheckboxClick = CapturedEventCount;
	TestTrue(TEXT("Showcase checkbox can be clicked"), View.ClickElementForTesting(TEXT("telemetry")));
	TestFalse(
		TEXT("Showcase checkbox click clears its checked state"),
		View.HasElementAttributeForTesting(TEXT("telemetry"), TEXT("checked")));
	TestEqual(TEXT("Checkbox click emits one UE event"), CapturedEventCount, EventsBeforeCheckboxClick + 1);
	TestEqual(TEXT("Checkbox event keeps its declared name"), CapturedEvent.Name, FName(TEXT("TelemetryChanged")));
	TestEqual(TEXT("Unchecked checkbox emits false"), CapturedEvent.Payload, FString(TEXT("false")));
	View.SetData(TEXT("mission.telemetry"), TEXT("true"));
	View.SynchronizeModels();
	TestTrue(
		TEXT("Checkbox state can be restored from the UE model"),
		View.HasElementAttributeForTesting(TEXT("telemetry"), TEXT("checked")));

	TestTrue(TEXT("Second radio option can be clicked"), View.ClickElementForTesting(TEXT("route-fast")));
	TestTrue(
		TEXT("Clicked radio option becomes checked"),
		View.HasElementAttributeForTesting(TEXT("route-fast"), TEXT("checked")));
	TestFalse(
		TEXT("Radio group clears the previous option"),
		View.HasElementAttributeForTesting(TEXT("route-safe"), TEXT("checked")));
	TestEqual(TEXT("Radio event keeps its declared name"), CapturedEvent.Name, FName(TEXT("RouteChanged")));
	TestEqual(TEXT("Radio event emits the selected value"), CapturedEvent.Payload, FString(TEXT("fast")));

	View.SetData(TEXT("mission.signal"), TEXT("73"));
	View.SetData(TEXT("mission.readiness"), TEXT("91"));
	View.SynchronizeModels();
	TestTrue(
		TEXT("Range value can be updated from the UE model"),
		View.GetElementValueForTesting(TEXT("signal-range")).StartsWith(TEXT("73")));
	TestEqual(
		TEXT("Progress value follows its bound model"),
		View.GetElementAttributeForTesting(TEXT("readiness-progress"), TEXT("value")),
		FString(TEXT("91")));
	View.Update(FVector2D(1920.0, 1080.0), FPlatformTime::Seconds());
	const FVector2D ShowcaseDocumentSize = View.GetElementBorderSizeForTesting(FString());
	const FVector2D ShowcaseViewportSize = View.GetElementBorderSizeForTesting(TEXT("showcase-viewport"));
	const FVector2D ShowcaseContentSize = View.GetElementBorderSizeForTesting(TEXT("showcase-content"));
	AddInfo(FString::Printf(
		TEXT("Showcase desktop layout: document %.1f x %.1f, viewport %.1f x %.1f, content %.1f x %.1f."),
		ShowcaseDocumentSize.X,
		ShowcaseDocumentSize.Y,
		ShowcaseViewportSize.X,
		ShowcaseViewportSize.Y,
		ShowcaseContentSize.X,
		ShowcaseContentSize.Y));
	TestTrue(TEXT("Showcase document expands to the view width"), ShowcaseDocumentSize.X >= 1900.0);
	TestTrue(TEXT("Showcase viewport expands to the view width"), ShowcaseViewportSize.X >= 1900.0);
	TestTrue(TEXT("Showcase content keeps a usable desktop width"), ShowcaseContentSize.X >= 1000.0);
	TestFalse(
		TEXT("Transparent page margin does not capture pointer input"),
		View.IsPointerInteractingAtForTesting(FVector2D(20.0, 200.0)));
	TestTrue(
		TEXT("Visible hero surface captures pointer input"),
		View.IsPointerInteractingAtForTesting(FVector2D(300.0, 150.0)));

	View.Update(FVector2D(588.0, 823.0), FPlatformTime::Seconds());
	const FVector2D CompactDocumentSize = View.GetElementBorderSizeForTesting(FString());
	const FVector2D CompactViewportSize = View.GetElementBorderSizeForTesting(TEXT("showcase-viewport"));
	const FVector2D CompactContentSize = View.GetElementBorderSizeForTesting(TEXT("showcase-content"));
	AddInfo(FString::Printf(
		TEXT("Showcase compact layout: document %.1f x %.1f, viewport %.1f x %.1f, content %.1f x %.1f."),
		CompactDocumentSize.X,
		CompactDocumentSize.Y,
		CompactViewportSize.X,
		CompactViewportSize.Y,
		CompactContentSize.X,
		CompactContentSize.Y));
	TestTrue(TEXT("Compact showcase document expands to the view width"), CompactDocumentSize.X >= 570.0);
	TestTrue(TEXT("Compact showcase viewport expands to the view width"), CompactViewportSize.X >= 570.0);
	TestTrue(TEXT("Compact showcase content does not collapse"), CompactContentSize.X >= 540.0);
	TestTrue(TEXT("Compact showcase remains vertically scrollable"), CompactContentSize.Y > 823.0);

	View.Update(FVector2D(1920.0, 1080.0), FPlatformTime::Seconds());
	TestTrue(TEXT("Showcase select can open"), View.ClickElementForTesting(TEXT("region")));
	FSlateWindowElementList ShowcaseElements(nullptr);
	View.Paint(
		FGeometry::MakeRoot(FVector2f(1920.0f, 1080.0f), FSlateLayoutTransform()),
		ShowcaseElements,
		0);
	const FH5UI_PerformanceStats ShowcaseStats = View.GetPerformanceStats();
	TestTrue(TEXT("Showcase submits native draw batches"), ShowcaseStats.DrawBatches > 0);
	TestTrue(TEXT("Showcase submits native vertices"), ShowcaseStats.Vertices > 0);
	TestTrue(TEXT("Showcase reports an active JavaScript heap"), ShowcaseStats.JavaScriptHeapBytes > 0);
	TestTrue(TEXT("Showcase reports non-negative JavaScript execution time"), ShowcaseStats.JavaScriptMilliseconds >= 0.0f);
	const FVector2D RangeTrackSize = View.GetDescendantBorderSizeForTesting(TEXT("signal-range"), TEXT("slidertrack"));
	const FVector2D RangeThumbSize = View.GetDescendantBorderSizeForTesting(TEXT("signal-range"), TEXT("sliderbar"));
	const FVector2D SelectBoxSize = View.GetDescendantBorderSizeForTesting(TEXT("region"), TEXT("selectbox"));
	const FVector2D SelectValueSize = View.GetDescendantBorderSizeForTesting(TEXT("region"), TEXT("selectvalue"));
	const FString SelectValueRml = View.GetDescendantInnerRmlForTesting(TEXT("region"), TEXT("selectvalue"));
	const FVector2D ProgressFillSize = View.GetDescendantBorderSizeForTesting(TEXT("readiness-progress"), TEXT("fill"));
	const FVector2D RuntimeIndexFontSize = View.GetDescendantBorderSizeForTesting(TEXT("runtime-index"), TEXT("font"));
	TestTrue(TEXT("Range track has visible geometry"), RangeTrackSize.X >= 100.0 && RangeTrackSize.Y >= 4.0);
	TestTrue(TEXT("Range thumb has visible geometry"), RangeThumbSize.X >= 14.0 && RangeThumbSize.Y >= 14.0);
	TestTrue(TEXT("Select popup has a usable width"), SelectBoxSize.X >= 200.0);
	TestTrue(TEXT("Select popup lays out all options"), SelectBoxSize.Y >= 90.0);
	TestTrue(TEXT("Select value has a usable display box"), SelectValueSize.X >= 200.0 && SelectValueSize.Y >= 30.0);
	TestEqual(TEXT("Select value mirrors the selected option"), SelectValueRml, FString(TEXT("Coastal Array")));
	TestTrue(
		TEXT("Select displayed text stays inside the control"),
		View.IsDescendantInsideElementForTesting(TEXT("region"), TEXT("#text")));
	TestEqual(TEXT("Brand mark keeps its text"), View.GetElementInnerRmlForTesting(TEXT("brand-mark")), FString(TEXT("S")));
	TestTrue(
		TEXT("Brand mark text stays inside its fixed box"),
		View.IsDescendantInsideElementForTesting(TEXT("brand-mark"), TEXT("#text")));
	TestEqual(
		TEXT("Primary button keeps its label"),
		View.GetElementInnerRmlForTesting(TEXT("launch-mission")),
		FString(TEXT("Launch mission")));
	TestTrue(
		TEXT("Primary button label stays inside its fixed box"),
		View.IsDescendantInsideElementForTesting(TEXT("launch-mission"), TEXT("#text")));
	TestTrue(
		TEXT("Legacy font wrapper keeps visible index geometry"),
		RuntimeIndexFontSize.X > 0.0 && RuntimeIndexFontSize.Y > 0.0);
	TestTrue(
		TEXT("Legacy font wrapper stays inside its index badge"),
		View.IsDescendantInsideElementForTesting(TEXT("runtime-index"), TEXT("font")));
	TestTrue(TEXT("Progress fill has visible geometry"), ProgressFillSize.X > 100.0 && ProgressFillSize.Y >= 8.0);
	AddInfo(FString::Printf(
		TEXT("Native controls: range track %.1f x %.1f, thumb %.1f x %.1f, select box %.1f x %.1f, select value %.1f x %.1f '%s', progress fill %.1f x %.1f."),
		RangeTrackSize.X,
		RangeTrackSize.Y,
		RangeThumbSize.X,
		RangeThumbSize.Y,
		SelectBoxSize.X,
		SelectBoxSize.Y,
		SelectValueSize.X,
		SelectValueSize.Y,
		*SelectValueRml,
		ProgressFillSize.X,
		ProgressFillSize.Y));
	AddInfo(FString::Printf(
		TEXT("Showcase render submission: %.4f ms, %d batches, %d vertices."),
		ShowcaseStats.SubmitMilliseconds,
		ShowcaseStats.DrawBatches,
		ShowcaseStats.Vertices));

	TestTrue(
		TEXT("Performance model refresh requests another render"),
		View.Update(FVector2D(1920.0, 1080.0), FPlatformTime::Seconds() + 1.0));
	TestEqual(
		TEXT("Runtime panel receives the measured batch count"),
		View.GetElementInnerRmlForTesting(TEXT("perf-batches")),
		FString::FromInt(ShowcaseStats.DrawBatches));
	TestEqual(
		TEXT("Runtime panel receives the current viewport"),
		View.GetElementInnerRmlForTesting(TEXT("perf-viewport")),
		FString(TEXT("1920 x 1080")));
	TestEqual(
		TEXT("Runtime panel receives the active JavaScript timer count"),
		View.GetElementInnerRmlForTesting(TEXT("perf-js-timers")),
		FString::FromInt(View.GetPerformanceStats().JavaScriptTimers));

	bReady = false;
	LoadError.Reset();
	JavaScriptError.Reset();
	TestTrue(TEXT("Vue 3 example document loads"), View.LoadURL(TEXT("h5ui://plugin/vue-basic/vue-basic.html")));
	TestTrue(TEXT("Vue 3 example emits ready callback"), bReady);
	TestTrue(TEXT("Vue 3 example emits no load error"), LoadError.IsEmpty());
	TestTrue(TEXT("Vue 3 example emits no JavaScript error"), JavaScriptError.IsEmpty());
	TestEqual(
		TEXT("Vue 3 app mounts its reactive root"),
		View.GetElementInnerRmlForTesting(TEXT("vue-runtime-status")),
		FString(TEXT("Ready")));
	TestEqual(
		TEXT("Vue 3 reactive counter starts at zero"),
		View.GetElementInnerRmlForTesting(TEXT("vue-count")),
		FString(TEXT("0")));
	TestTrue(TEXT("Vue 3 click handler can be invoked"), View.ClickElementForTesting(TEXT("vue-increment")));
	View.Update(FVector2D(1920.0, 1080.0), FPlatformTime::Seconds());
	TestEqual(
		TEXT("Vue 3 scheduler commits a reactive update"),
		View.GetElementInnerRmlForTesting(TEXT("vue-count")),
		FString(TEXT("1")));
	TestTrue(TEXT("Vue 3 reactive update emits no JavaScript error"), JavaScriptError.IsEmpty());

	const double RunawayStartSeconds = FPlatformTime::Seconds();
	TestFalse(
		TEXT("Runaway JavaScript is interrupted by the per-view execution budget"),
		View.ExecuteJavaScript(TEXT("while (true) {}"), JavaScriptResult, JavaScriptExecutionError));
	const double RunawayMilliseconds = (FPlatformTime::Seconds() - RunawayStartSeconds) * 1000.0;
	TestTrue(
		TEXT("Runaway JavaScript reports an interrupted exception"),
		JavaScriptExecutionError.Contains(TEXT("interrupted"), ESearchCase::IgnoreCase));
	TestTrue(TEXT("Runaway JavaScript returns control promptly"), RunawayMilliseconds < 250.0);
	TestTrue(
		TEXT("JavaScript context remains usable after an interrupted script"),
		View.ExecuteJavaScript(TEXT("1 + 1"), JavaScriptResult, JavaScriptExecutionError));
	TestEqual(TEXT("Recovered JavaScript context returns values"), JavaScriptResult, FString(TEXT("2")));
	AddInfo(FString::Printf(TEXT("Runaway JavaScript interrupted after %.3f ms."), RunawayMilliseconds));

	View.Close();
	TestEqual(TEXT("View closes cleanly"), View.GetState(), EH5UI_ViewState::Unloaded);
	return true;
}

#endif
