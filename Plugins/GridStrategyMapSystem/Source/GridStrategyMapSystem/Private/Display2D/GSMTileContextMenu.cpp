#include "GridStrategyMapSystem/Display2D/GSMTileContextMenu.h"

#include "Components/VerticalBox.h"
#include "Framework/Application/SlateApplication.h"
#include "GridStrategyMapSystem/Display2D/GSMTileMenuButton.h"

DEFINE_LOG_CATEGORY_STATIC(LogGSMTileContextMenu, Log, All);

void UGSMTileContextMenu::NativeConstruct()
{
	Super::NativeConstruct();
	bConstructed = true;
	RefreshMenuButtons();
}

void UGSMTileContextMenu::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	const bool bAnyMouseButtonPressed = FSlateApplication::Get().GetPressedMouseButtons().Num() > 0;
	if (!bAnyMouseButtonPressed)
	{
		bFirstUpdateAfterOpen = false;
		return;
	}

	if (!bFirstUpdateAfterOpen && GetWorld() && !CloseSelfTimerHandle.IsValid())
	{
		GetWorld()->GetTimerManager().SetTimer(
			CloseSelfTimerHandle,
			this,
			&UGSMTileContextMenu::OnDelayCloseSelf,
			CloseDelayAfterMouseClick,
			false
		);
	}
}

void UGSMTileContextMenu::InitializeMapTileMenu(const FGSMTileMenuContext& InMenuContext)
{
	MenuContext = InMenuContext;
	if (bConstructed)
	{
		RefreshMenuButtons();
	}
}

void UGSMTileContextMenu::RefreshMenuButtons()
{
	if (!bConstructed)
	{
		return;
	}

	bool bClearExistingButtons = true;
	TArray<UGSMTileMenuButton*> MenuButtons;
	GetMapTileMenuButtons(MenuContext, MenuButtons, bClearExistingButtons);
	BatchAddMapTileMenuButtons(MenuButtons, bClearExistingButtons);
}

void UGSMTileContextMenu::BatchAddMapTileMenuButtons(
	const TArray<UGSMTileMenuButton*>& InMenuButtons,
	bool bClearExistingButtons
)
{
	if (!MenuButtonVerticalBox)
	{
		UE_LOG(
			LogGSMTileContextMenu,
			Warning,
			TEXT("%s is missing required VerticalBox widget named MenuButtonVerticalBox."),
			*GetName()
		);
		return;
	}

	if (bClearExistingButtons)
	{
		MenuButtonVerticalBox->ClearChildren();
	}

	for (UGSMTileMenuButton* MenuButton : InMenuButtons)
	{
		if (!IsValid(MenuButton))
		{
			continue;
		}

		RegisterMapTileMenuButton(MenuButton);
		MenuButtonVerticalBox->AddChild(MenuButton);
	}
}

void UGSMTileContextMenu::RegisterMapTileMenuButton(UGSMTileMenuButton* InMenuButton)
{
	if (!IsValid(InMenuButton))
	{
		return;
	}

	InMenuButton->SetMenuContext(MenuContext, this);
}

void UGSMTileContextMenu::GetMapTileMenuButtons_Implementation(
	const FGSMTileMenuContext& InMenuContext,
	TArray<UGSMTileMenuButton*>& OutMenuButtons,
	bool& bClearExistingButtons
)
{
	(void)InMenuContext;
	(void)OutMenuButtons;
	(void)bClearExistingButtons;
}

void UGSMTileContextMenu::OnDelayCloseSelf()
{
	RemoveFromParent();

	if (GetWorld() && CloseSelfTimerHandle.IsValid())
	{
		GetWorld()->GetTimerManager().ClearTimer(CloseSelfTimerHandle);
	}
}
