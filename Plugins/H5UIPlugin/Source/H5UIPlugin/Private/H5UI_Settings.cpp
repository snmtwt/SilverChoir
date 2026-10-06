#include "H5UI_Settings.h"

UH5UI_Settings::UH5UI_Settings()
	: ResourceDirectory(TEXT("UI"))
	, ActiveUpdateRate(60)
	, IdleUpdateRate(10)
	, IdleAfterSeconds(0.5f)
	, JavaScriptMemoryLimitMB(16)
	, JavaScriptExecutionTimeLimitMilliseconds(50.0f)
	, JavaScriptPromiseJobTimeLimitMilliseconds(16.0f)
	, JavaScriptInitialExecutionTimeLimitMilliseconds(500.0f)
	, JavaScriptMaxCallbacksPerFrame(100)
	, bAllowAbsoluteFilePaths(false)
{
}

FName UH5UI_Settings::GetCategoryName() const
{
	return TEXT("Plugins");
}
