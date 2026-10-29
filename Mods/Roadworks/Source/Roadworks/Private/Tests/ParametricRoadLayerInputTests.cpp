#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "ParametricRoadLayerInput.h"
#include "ParametricRoadMonitor.h"
#include "ParametricRoadSession.h"
#include "Blueprint/GameViewportSubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/EditableTextBox.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "Input/Events.h"
#include "Layout/Geometry.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Widgets/SVirtualWindow.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParametricRoadLayerInputTest, "Roadworks.Editor.LayerInput",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParametricRoadLayerInputTest::RunTest(const FString&)
{
    // Exercise real Slate geometry: desktop offsets and DPI scaling must not
    // alter the normalized pointer used by the world monitor.
    FVector2D Pointer;
    const FGeometry Desktop = FGeometry::MakeRoot(FVector2D(1280, 720), FSlateLayoutTransform(1.5f, FVector2D(130, 70)));
    TestTrue(TEXT("A scaled, offset viewport accepts finite pointer coordinates"),
        UParametricRoadLayerInput::NormalizePointer(Desktop, Desktop.LocalToAbsolute(FVector2D(320, 540)), Pointer));
    TestTrue(TEXT("Pointer normalization accounts for both DPI and desktop origin"), Pointer.Equals(FVector2D(.25, .75), .000001));
    TestTrue(TEXT("Captured drags retain coordinates beyond the viewport"),
        UParametricRoadLayerInput::NormalizePointer(Desktop, Desktop.LocalToAbsolute(FVector2D(-128, 792)), Pointer));
    TestTrue(TEXT("An outside pointer is not clamped onto a screen-edge control"), Pointer.Equals(FVector2D(-.1, 1.1), .000001));
    const FGeometry Empty = FGeometry::MakeRoot(FVector2D(0, 720), FSlateLayoutTransform());
    TestFalse(TEXT("Zero allocation cannot manufacture a valid monitor pointer"),
        UParametricRoadLayerInput::NormalizePointer(Empty, FVector2D::ZeroVector, Pointer));
    TestFalse(TEXT("Nonfinite hardware coordinates are rejected"),
        UParametricRoadLayerInput::NormalizePointer(Desktop, FVector2D(std::numeric_limits<double>::infinity(), 1), Pointer));

    TStrongObjectPtr<UParametricRoadLayerInput> Host(NewObject<UParametricRoadLayerInput>());
    Host->Initialize();
    const TSharedRef<SWidget> SlateHost = Host->TakeWidget();
    TestNull(TEXT("Input host fixture has no gameplay world"), Host->GetWorld());
    TestTrue(TEXT("Native modal gates own the real mouse and keyboard"), Host->mUseMouse && Host->mUseKeyboard
        && Host->mDisablePlayerActions && Host->mDisableBuildGunActions && Host->mDisablePlayerEquipmentManagement);
    TestFalse(TEXT("Focus loss cannot trigger automatic focus stealing from another native menu"), Host->mRestoreFocusWhenLost);
    TestTrue(TEXT("The real Slate host accepts keyboard focus without any content control"),
        Host->IsFocusable() && SlateHost->SupportsKeyboardFocus());
    // FactoryGame's editor SDK SetDefaultFocusWidget and SetupDefaultFocus are
    // empty generated bodies. They cannot qualify shipping focus acquisition;
    // the engine-backed focusability above is the available worldless contract.
    UBorder* Surface = Cast<UBorder>(Host->WidgetTree->RootWidget);
    TestNotNull(TEXT("Host has one transparent event surface"), Surface);
    if (Surface)
    {
        TestEqual(TEXT("The event surface paints no visible panel"), Surface->GetBrushColor().A, 0.0f);
        TestEqual(TEXT("Transparent surface remains hit-testable"), Surface->GetVisibility(), ESlateVisibility::Visible);
        TestNull(TEXT("No configuration content is duplicated into the viewport"), Surface->GetContent());
    }
    TestTrue(TEXT("Transient native owner cannot become a persistent saved object"), Host->GetClass()->HasAnyClassFlags(CLASS_Transient));
    for (TFieldIterator<FProperty> Property(Host->GetClass(), EFieldIteratorFlags::ExcludeSuper); Property; ++Property)
    {
        TestFalse(TEXT("Input ownership is not saved or replicated"), Property->HasAnyPropertyFlags(CPF_SaveGame | CPF_Net));
    }
    TestFalse(TEXT("A worldless host cannot claim viewport presentation"), Host->Present());
    Host->ActivateInput();
    TestFalse(TEXT("Input cannot activate without an owning viewport"), Host->bInputActive);
    Host->ReleaseInput(); Host->ReleaseInput();
    TestTrue(TEXT("Repeated release of unacquired ownership preserves native defaults"), Host->mUseMouse && Host->mUseKeyboard);

    // Remapping, including multiple modifiers, is resolved before modal gates.
    // Check the event matcher without querying any physical keyboard state.
    Host->ConfigureKey = EKeys::K;
    Host->ConfigureModifiers = {EKeys::LeftControl, EKeys::LeftShift};
    const FModifierKeysState CtrlShift(true, false, true, false, false, false, false, false, false);
    const FModifierKeysState CtrlOnly(false, false, true, false, false, false, false, false, false);
    const FKeyEvent Match(EKeys::K, CtrlShift, 0, false, 0, 0);
    const FKeyEvent Missing(EKeys::K, CtrlOnly, 0, false, 0, 0);
    const FKeyEvent Different(EKeys::J, CtrlShift, 0, false, 0, 0);
    TestTrue(TEXT("Remapped close action accepts its complete modifier chord"), Host->IsCloseInput(EKeys::K, Match));
    TestFalse(TEXT("Missing one required modifier does not close configuration"), Host->IsCloseInput(EKeys::K, Missing));
    TestFalse(TEXT("A different key does not inherit the close mapping"), Host->IsCloseInput(EKeys::J, Different));
    TestFalse(TEXT("An unowned host cannot consume normal keyboard input"), Host->NativeOnKeyDown(Desktop, Match).IsEventHandled());
    TestFalse(TEXT("An unowned host cannot consume an unrelated preview key"), Host->NativeOnPreviewKeyDown(Desktop, Different).IsEventHandled());

    // Real host Preview and native keybinding callbacks see a distinct virtual
    // user's native editable text focus. Direct virtual Char injection alone
    // would bypass both closure paths and missed the original Num 3 regression.
    TStrongObjectPtr<UParametricRoadSession> Owner(NewObject<UParametricRoadSession>());
    TStrongObjectPtr<UParametricRoadMonitor> Monitor(NewObject<UParametricRoadMonitor>());
    TStrongObjectPtr<UParametricRoadMonitorInteraction> Interaction(NewObject<UParametricRoadMonitorInteraction>());
    TStrongObjectPtr<UParametricRoadLayerInput> Content(NewObject<UParametricRoadLayerInput>());
    Content->Initialize();
    const TSharedRef<SWidget> SlateContent = Content->TakeWidget();
    UEditableTextBox* TextEntry = Content->WidgetTree->ConstructWidget<UEditableTextBox>();
    TextEntry->SetText(FText::FromString(TEXT("12")));
    CastChecked<UBorder>(Content->WidgetTree->RootWidget)->AddChild(TextEntry);
    TSharedRef<SVirtualWindow> Window = SNew(SVirtualWindow).Size(FVector2D(600, 200));
    Window->SetIsFocusable(true); Window->SetContent(SlateContent);
    FSlateApplication& Slate = FSlateApplication::Get();
    const TSharedPtr<SWidget> HardwareFocus = Slate.GetKeyboardFocusedWidget();
    Slate.RegisterVirtualWindow(Window);
    const TSharedRef<FSlateVirtualUserHandle> User = Slate.FindOrCreateVirtualUser(90325);
    ON_SCOPE_EXIT
    {
        Owner->CloseLayerConfiguration();
        Slate.ClearUserFocus(User->GetUserIndex());
        Monitor->ConfigurationInteraction = nullptr;
        Monitor->ConfigurationWidget.Reset();
        Interaction->VirtualUser.Reset();
        Slate.UnregisterVirtualWindow(Window);
    };
    Interaction->VirtualUser = User;
    Monitor->ConfigurationInteraction = Interaction.Get();
    Monitor->ConfigurationWidget = Content.Get();
    Host->InitializeTarget(Owner.Get(), Monitor.Get());
    Host->bInputActive = true;
    Owner->bLayerConfigurationOpen = true;
    Owner->LayerInput = Host.Get();
    Host->ConfigureKey = EKeys::NumPadThree;
    Host->ConfigureModifiers.Reset();
    Window->SlatePrepass(1);
    Slate.SetUserFocus(User->GetUserIndex(), TextEntry->TakeWidget());
    if (!TestTrue(TEXT("Native text entry receives the actual monitor virtual user's focus"), Monitor->IsConfigurationTextEntryFocused()))
    { return false; }
    TestNotEqual(TEXT("Logical WIC user index is distinct from its allocated Slate user"), User->GetUserIndex(), User->GetVirtualUserIndex());
    const FKeyEvent Digit(EKeys::NumPadThree, FModifierKeysState(), 0, false, '3', 0);
    TestFalse(TEXT("Host preview permits a typed numpad digit instead of closing"), Host->NativeOnPreviewKeyDown(Desktop, Digit).IsEventHandled());
    TestFalse(TEXT("Native keybinding callback also permits a typed numpad digit"), Host->ConfigureShortcut().NativeReply.IsEventHandled());
    TestTrue(TEXT("A printable toggle preserves the owned configuration"), Owner->IsLayerConfigurationOpen());
    const FKeyEvent Decimal(EKeys::Decimal, FModifierKeysState(), 0, false, '.', 0);
    TestFalse(TEXT("The terminal forwards a decimal to native text editing"), Host->NativeOnPreviewKeyDown(Desktop, Decimal).IsEventHandled());
    TestFalse(TEXT("Typing a decimal leaves precision unchanged"), Owner->UsesFineAdjustment());
    Host->ConfigureKey = EKeys::K;
    const FKeyEvent Letter(EKeys::K, FModifierKeysState(), 0, false, 'k', 0);
    TestFalse(TEXT("Remapped printable toggles also remain text while editing"), Host->NativeOnPreviewKeyDown(Desktop, Letter).IsEventHandled());
    TestFalse(TEXT("Remapped printable native callbacks preserve the edit"), Host->ConfigureShortcut().NativeReply.IsEventHandled());
    const FKeyEvent Escape(EKeys::Escape, FModifierKeysState(), 0, false, 0, 0);
    const FKeyEvent RepeatedEscape(EKeys::Escape, FModifierKeysState(), 0, true, 0, 0);
    TestTrue(TEXT("First Escape is consumed by the native text edit"), Host->NativeOnPreviewKeyDown(Desktop, Escape).IsEventHandled());
    TestFalse(TEXT("Finishing text entry releases only the monitor's virtual text focus"), Monitor->IsConfigurationTextEntryFocused());
    TestTrue(TEXT("First Escape keeps configuration open"), Owner->IsLayerConfigurationOpen());
    TestTrue(TEXT("Finishing virtual text leaves the hardware keyboard focus unchanged"), Slate.GetKeyboardFocusedWidget() == HardwareFocus);
    TestFalse(TEXT("The terminal has no decimal placement shortcut outside text editing"), Host->NativeOnPreviewKeyDown(Desktop, Decimal).IsEventHandled());
    TestFalse(TEXT("A decimal outside text editing leaves precision unchanged"), Owner->UsesFineAdjustment());
    const FKeyEvent RepeatedDecimal(EKeys::Decimal, FModifierKeysState(), 0, true, '.', 0);
    Owner->bFineAdjustment = true;
    TestFalse(TEXT("Decimal repeats remain ordinary native control input"), Host->NativeOnPreviewKeyDown(Desktop, RepeatedDecimal).IsEventHandled());
    TestTrue(TEXT("Native control input preserves the checkbox's existing precision state"), Owner->UsesFineAdjustment());
    Host->NativeOnPreviewKeyDown(Desktop, RepeatedEscape);
    TestTrue(TEXT("Holding Escape cannot immediately close after finishing the edit"), Owner->IsLayerConfigurationOpen());
    Host->NativeOnPreviewKeyDown(Desktop, Escape);
    TestFalse(TEXT("A subsequent Escape closes the owned configuration"), Owner->IsLayerConfigurationOpen());
    TestNull(TEXT("Text routing fixture has no gameplay world"), Owner->GetWorld());
    Host->NativeDestruct();
    TestFalse(TEXT("Destruction clears native input ownership"), Host->bInputActive);
    TestTrue(TEXT("Destruction clears only owned bindings and held keys"), Host->Keybindings.IsEmpty()
        && Host->PointerKeys.IsEmpty() && Host->KeyboardKeys.IsEmpty() && !Host->ConfigureKey.IsValid());
    return true;
}

#endif
