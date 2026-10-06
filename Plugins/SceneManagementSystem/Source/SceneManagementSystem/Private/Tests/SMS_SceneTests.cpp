#include "SMS_SceneBase.h"
#include "SMS_SceneManager.h"
#include "SMS_SceneSubsystem.h"
#include "SMS_SceneLibrary.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSMSConfigTest,"SceneManagementSystem.Configuration",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FSMSConfigTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<USMS_SceneManager> M(NewObject<USMS_SceneManager>());
	M->SceneClasses={USMS_SceneBase::StaticClass()};
	TestFalse(TEXT("Abstract scene rejected"),M->InitializeManager());
	M->SceneClasses={nullptr}; TestFalse(TEXT("Null class rejected"),M->InitializeManager());
	M->SceneClasses.Empty(); TestTrue(TEXT("Empty configuration is valid"),M->InitializeManager());
	TestFalse(TEXT("Cannot reinitialize live registry"),M->InitializeManager());
	TestNull(TEXT("Missing context safely returns no scene"),USMS_SceneLibrary::GetCurrentScene(nullptr));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSMSWorldTest,"SceneManagementSystem.WorldIntegration",EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FSMSWorldTest::RunTest(const FString& Parameters)
{
	for (const auto& Context:GEngine->GetWorldContexts())
	{
		if (Context.WorldType!=EWorldType::Game || !Context.World()) continue;
		auto* World=Context.World(); auto* Sub=USMS_SceneLibrary::GetSceneSubsystem(World);
		if (!TestNotNull(TEXT("Runtime subsystem exists"),Sub)) return false;
		TestTrue(TEXT("Configured manager initialized"),Sub->IsReady());
		TestEqual(TEXT("Manager resolves runtime World"),Sub->GetManager()->GetWorld(),World);
		TestFalse(TEXT("Invalid tag rejected"),USMS_SceneLibrary::SwitchScene(World,FGameplayTag()));
		return true;
	}
	AddError(TEXT("Requires a game World")); return false;
}
#endif
