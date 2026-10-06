#include "H5UI_BrowserModule.h"

#include "Modules/ModuleManager.h"
#include "SWebBrowser.h"
#include "WebBrowserModule.h"

DEFINE_LOG_CATEGORY_STATIC(LogH5UICEF, Log, All);

class FH5UI_BrowserModule final : public IH5UI_BrowserModule
{
public:
	virtual TSharedRef<SWidget> CreateBrowserWidget(
		const FString& InitialURL,
		UObject* BridgeObject,
		int32 FrameRate) override
	{
		// SWebBrowser assumes another module has already initialized WebBrowser.
		// H5UI loads CEF lazily without WebBrowserWidget, so initialize it here.
		IWebBrowserModule& WebBrowserModule = IWebBrowserModule::Get();
		if (!WebBrowserModule.IsWebModuleAvailable())
		{
			UE_LOG(LogH5UICEF, Error, TEXT("CEF WebBrowser module is unavailable; cannot open %s"), *InitialURL);
		}

		TSharedRef<SWebBrowser> Browser = SNew(SWebBrowser)
			.InitialURL(TEXT("about:blank"))
			.ShowControls(false)
			.ShowAddressBar(false)
			.ShowErrorMessage(true)
			.SupportsTransparency(false)
			.SupportsThumbMouseButtonNavigation(false)
			.ShowInitialThrobber(true)
			.BackgroundColor(FColor::Black)
			.BrowserFrameRate(FMath::Clamp(FrameRate, 1, 60))
			.OnLoadStarted(FSimpleDelegate::CreateLambda([InitialURL]()
			{
				UE_LOG(LogH5UICEF, Verbose, TEXT("CEF iframe load started: %s"), *InitialURL);
			}))
			.OnLoadCompleted(FSimpleDelegate::CreateLambda([InitialURL]()
			{
				UE_LOG(LogH5UICEF, Log, TEXT("CEF iframe load completed: %s"), *InitialURL);
			}))
			.OnLoadError(FSimpleDelegate::CreateLambda([InitialURL]()
			{
				UE_LOG(LogH5UICEF, Error, TEXT("CEF iframe load failed: %s"), *InitialURL);
			}));

		if (BridgeObject)
		{
			Browser->BindUObject(TEXT("h5uiframe"), BridgeObject, true);
		}
		Browser->LoadURL(InitialURL);
		return Browser;
	}

	virtual bool ExecuteJavaScript(
		const TSharedPtr<SWidget>& BrowserWidget,
		const FString& Script) override
	{
		if (!BrowserWidget || Script.IsEmpty())
		{
			return false;
		}

		const TSharedPtr<SWebBrowser> Browser = StaticCastSharedPtr<SWebBrowser>(BrowserWidget);
		if (!Browser)
		{
			return false;
		}

		Browser->ExecuteJavascript(Script);
		return true;
	}
};

IMPLEMENT_MODULE(FH5UI_BrowserModule, H5UIPluginCEF)
