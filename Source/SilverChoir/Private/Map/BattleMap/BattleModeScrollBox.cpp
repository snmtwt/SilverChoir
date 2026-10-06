#include "Map/BattleMap/BattleModeScrollBox.h"

#include "Components/ScrollBoxSlot.h"
#include "Widgets/Layout/SScrollBar.h"

template <>
struct TWidgetTypeTraits<class SBattleModeScrollBox>
{
    static constexpr bool SupportsInvalidation() { return true; }
};

/**
 * Input bubbles through this viewport to the usual child/ancestor handlers.
 * In particular, its preview handler must never capture a child's touch input.
 * SScrollBox's Tick and active timer are retained for programmatic animation;
 * none of its input paths may start panning or accumulate inertial velocity.
 */
class SBattleModeScrollBox final : public SScrollBox
{
public:
    using FArguments = SScrollBox::FArguments;

    void Construct(const FArguments& InArgs)
    {
        SScrollBox::Construct(InArgs);
        bCanSupportFocus = false;
    }

    virtual bool SupportsKeyboardFocus() const override { return false; }

    virtual FReply OnPreviewMouseButtonDown(const FGeometry&, const FPointerEvent&) override
    {
        return FReply::Unhandled();
    }

    virtual FReply OnMouseButtonDown(const FGeometry&, const FPointerEvent&) override
    {
        // The stock handler also stops programmatic animation on mouse down.
        return FReply::Unhandled();
    }

    virtual FReply OnMouseButtonUp(const FGeometry&, const FPointerEvent&) override
    {
        return FReply::Unhandled();
    }

    virtual FReply OnMouseMove(const FGeometry&, const FPointerEvent&) override
    {
        return FReply::Unhandled();
    }

    virtual void OnMouseEnter(const FGeometry& Geometry, const FPointerEvent& Event) override
    {
        // Skip SScrollBox's touch ownership, but preserve ordinary hover state.
        SCompoundWidget::OnMouseEnter(Geometry, Event);
    }

    virtual void OnMouseLeave(const FPointerEvent& Event) override
    {
        SCompoundWidget::OnMouseLeave(Event);
    }

    virtual FReply OnMouseWheel(const FGeometry&, const FPointerEvent&) override
    {
        // Nested lists have already had the opportunity to consume this event.
        return FReply::Unhandled();
    }

    virtual FReply OnAnalogValueChanged(const FGeometry&, const FAnalogInputEvent&) override
    {
        return FReply::Unhandled();
    }

    virtual FReply OnTouchStarted(const FGeometry&, const FPointerEvent&) override
    {
        return FReply::Unhandled();
    }

    virtual FReply OnTouchMoved(const FGeometry&, const FPointerEvent&) override
    {
        return FReply::Unhandled();
    }

    virtual FReply OnTouchEnded(const FGeometry&, const FPointerEvent&) override
    {
        return FReply::Unhandled();
    }

    virtual FReply OnTouchGesture(const FGeometry&, const FPointerEvent&) override
    {
        return FReply::Unhandled();
    }

    virtual void OnDragEnter(const FGeometry& Geometry, const FDragDropEvent& Event) override
    {
        SCompoundWidget::OnDragEnter(Geometry, Event);
    }

    virtual void OnDragLeave(const FDragDropEvent& Event) override
    {
        SCompoundWidget::OnDragLeave(Event);
    }

    virtual FReply OnDragOver(const FGeometry&, const FDragDropEvent&) override
    {
        // Do not add edge scrolling or claim an inventory drag/drop operation.
        return FReply::Unhandled();
    }

    virtual FReply OnDrop(const FGeometry&, const FDragDropEvent&) override
    {
        return FReply::Unhandled();
    }

    virtual FNavigationReply OnNavigation(const FGeometry& Geometry, const FNavigationEvent& Event) override
    {
        // Normal navigation rules still apply; only SScrollBox's implicit
        // ScrollDescendantIntoView is bypassed. Nested navigation is unaffected.
        return SCompoundWidget::OnNavigation(Geometry, Event);
    }

    virtual void OnFocusChanging(const FWeakWidgetPath& PreviousPath, const FWidgetPath& NewPath,
        const FFocusEvent& Event) override
    {
        SCompoundWidget::OnFocusChanging(PreviousPath, NewPath, Event);
        OnScrollBoxFocusChanging.ExecuteIfBound(PreviousPath, NewPath);
    }

    virtual void OnAddedMetadata(const TSharedRef<ISlateMetaData>& Metadata) override
    {
        // SScrollBox otherwise enables offscreen navigation and paints its
        // scroll panel into a hit-test area spanning every mode page.
        SCompoundWidget::OnAddedMetadata(Metadata);
    }

    virtual void OnRemovedMetadata(const TSharedRef<ISlateMetaData>& Metadata) override
    {
        SCompoundWidget::OnRemovedMetadata(Metadata);
    }

    virtual FSlateRect GetHitTestBoundingRect() const override
    {
        return SCompoundWidget::GetHitTestBoundingRect();
    }
};

UBattleModeScrollBox::UBattleModeScrollBox(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    InitBackPadScrolling(false);
    InitFrontPadScrolling(false);
    ApplyPagingInputPolicy();
}

void UBattleModeScrollBox::ApplyPagingInputPolicy()
{
    SetScrollBarVisibility(ESlateVisibility::Collapsed);
    SetAlwaysShowScrollbar(false);
    SetAlwaysShowScrollbarTrack(false);
    SetConsumeMouseWheel(EConsumeMouseWheel::Never);
    SetAllowRightClickDragScrolling(false);
    SetAllowOverscroll(false);
    SetIsTouchScrollingEnabled(false);
    SetConsumePointerInput(false);
    SetAnalogMouseWheelKey(FKey());
    SetIsFocusable(false);
    SetScrollWhenFocusChanges(EScrollWhenFocusChanges::NoScroll);
}

TSharedRef<SWidget> UBattleModeScrollBox::RebuildWidget()
{
    // SScrollBox uses the bar to store its animated offset even when invisible.
    // Supplying an unparented external bar prevents inherited nonvirtual setters
    // (including orientation changes) from ever inserting an interactive bar.
    const TSharedRef<SScrollBar> HiddenBar = SNew(SScrollBar)
        .Style(&GetWidgetBarStyle())
        .Orientation(GetOrientation())
        .Visibility(EVisibility::Collapsed);

    MyScrollBox = SNew(SBattleModeScrollBox)
        .Style(&GetWidgetStyle())
        .ScrollBarStyle(&GetWidgetBarStyle())
        .ExternalScrollbar(HiddenBar)
        .Orientation(GetOrientation())
        .ScrollBarVisibility(EVisibility::Collapsed)
        .ScrollBarAlwaysVisible(false)
        .ScrollBarRightClickDragAllowed(false)
        .ConsumeMouseWheel(EConsumeMouseWheel::Never)
        .AllowOverscroll(EAllowOverscroll::No)
        .BackPadScrolling(false)
        .FrontPadScrolling(false)
        .EnableTouchScrolling(false)
        .ConsumePointerInput(false)
        .ScrollWhenFocusChanges(EScrollWhenFocusChanges::NoScroll)
        .NavigationDestination(GetNavigationDestination())
        .NavigationScrollPadding(GetNavigationScrollPadding())
        .ScrollAnimationInterpSpeed(GetScrollAnimationInterpolationSpeed())
        .OnUserScrolled(BIND_UOBJECT_DELEGATE(FOnUserScrolled, SlateHandleUserScrolled))
        .OnScrollBarVisibilityChanged(BIND_UOBJECT_DELEGATE(FOnScrollBarVisibilityChanged, SlateHandleScrollBarVisibilityChanged))
        .OnFocusReceived(BIND_UOBJECT_DELEGATE(FOnScrollBoxFocusReceived, SlateHandleFocusReceived))
        .OnFocusLost(BIND_UOBJECT_DELEGATE(FOnScrollBoxFocusLost, SlateHandleFocusLost))
        .OnFocusChanging(BIND_UOBJECT_DELEGATE(FOnScrollBoxFocusChanging, SlateHandleFocusChanging));

    for (UPanelSlot* PanelSlot : Slots)
    {
        if (UScrollBoxSlot* ScrollSlot = Cast<UScrollBoxSlot>(PanelSlot))
        {
            ScrollSlot->Parent = this;
            ScrollSlot->BuildSlot(MyScrollBox.ToSharedRef());
        }
    }
    return MyScrollBox.ToSharedRef();
}

void UBattleModeScrollBox::SynchronizeProperties()
{
    // These defaults also keep the Designer honest. Input overrides enforce the
    // policy even if a caller later changes an inherited, nonvirtual setter.
    ApplyPagingInputPolicy();
    Super::SynchronizeProperties();
}

#if WITH_EDITOR
const FText UBattleModeScrollBox::GetPaletteCategory()
{
    return NSLOCTEXT("SilverChoir", "BattleUIPaletteCategory", "战斗UI");
}
#endif
