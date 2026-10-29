#include "ParametricRoadLayerInput.h"

#include "ParametricRoadMonitor.h"
#include "ParametricRoadSession.h"
#include "RoadworksModule.h"
#include "Blueprint/GameViewportSubsystem.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Engine/LocalPlayer.h"
#include "FGPlayerController.h"
#include "FGPlayerInput.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "InputAction.h"
#include "InputCoreTypes.h"
#include "UI/FGGameUI.h"

UParametricRoadLayerInput::UParametricRoadLayerInput(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
    SetIsFocusable(true);
    mUseKeyboard = mUseMouse = true;
    mMouseLockMode = EMouseLockMode::DoNotLock;
    mHideCursorDuringCapture = false;
    mDisablePlayerActions = mDisableBuildGunActions = mDisablePlayerEquipmentManagement = true;
    mDisableVisualizationModeActions = true;
    mFlushMouseKeysOnOpen = mClearBindingsOnDestruct = true;
    mSupportsCaching = false;
    mSupportsStacking = true;
    mRestoreFocusWhenLost = false;
    mHideEnhancedInputButtonBarWhenMenuIsActive = true;
    mDesiredHorizontalAlignment = HAlign_Fill;
    mDesiredVerticalAlignment = VAlign_Fill;
    mDesiredAlignmentSize = FSlateChildSize(ESlateSizeRule::Fill);
}

void UParametricRoadLayerInput::InitializeTarget(UParametricRoadSession* InSession, UParametricRoadMonitor* InMonitor)
{
    Session = InSession;
    Monitor = InMonitor;
}

void UParametricRoadLayerInput::PrepareInput()
{
    ConfigureAction = LoadObject<UInputAction>(nullptr, TEXT("/Roadworks/Inputs/IA_ConfigureLayers.IA_ConfigureLayers"));
    ConfigureKey = FKey();
    ConfigureModifiers.Reset();
    const AFGPlayerController* PC = Cast<AFGPlayerController>(GetOwningPlayer());
    const UFGPlayerInput* Input = PC ? Cast<UFGPlayerInput>(PC->PlayerInput) : nullptr;
    if (Input && ConfigureAction)
    {
        Input->GetCurrentMappingForInputAction(ConfigureAction, ConfigureKey, &ConfigureModifiers, NAME_None, nullptr);
    }
    TArray<FFGKeybinding> Owned;
    if (ConfigureKey.IsValid() && ConfigureModifiers.Num() <= 1)
    {
        FFGKeybinding Binding;
        Binding.Key = ConfigureKey;
        if (!ConfigureModifiers.IsEmpty()) { Binding.ComboKey = ConfigureModifiers[0]; }
        Binding.EvaluationMethod = int32(EKeybindingEvaluationMethod::UsePreviewEvent);
        Binding.Callback = GET_FUNCTION_NAME_CHECKED(ThisClass, ConfigureShortcut);
        Binding.HintText = NSLOCTEXT("Roadworks", "CloseLayerInput", "Close layers");
        Owned.Add(Binding);
    }
    OverwriteKeybindings(Owned);
}

FGameViewportWidgetSlot UParametricRoadLayerInput::MakeViewportSlot()
{
    FGameViewportWidgetSlot Slot;
    Slot.Anchors = FAnchors(0, 0, 1, 1);
    Slot.Offsets = FMargin(0);
    Slot.Alignment = FVector2D::ZeroVector;
    Slot.ZOrder = 30;
    Slot.bAutoRemoveOnWorldRemoved = true;
    return Slot;
}

bool UParametricRoadLayerInput::Present()
{
    if (GetParent() || IsInViewport() || !GetOwningLocalPlayer()) { return false; }
    UGameViewportSubsystem* Viewport = UGameViewportSubsystem::Get(GetWorld());
    if (!Viewport) { return false; }
    Viewport->SetWidgetSlot(this, MakeViewportSlot());
    // Player-screen widgets sit beneath the separate native viewport UI layer.
    // This is an invisible input surface in that same viewport layer instead.
    AddToViewport(30);
    return IsInViewport();
}

TSharedRef<SWidget> UParametricRoadLayerInput::RebuildWidget()
{
    if (!WidgetTree) { WidgetTree = NewObject<UWidgetTree>(this); }
    UBorder* Surface = WidgetTree->ConstructWidget<UBorder>();
    Surface->SetBrushColor(FLinearColor::Transparent);
    Surface->SetPadding(FMargin(0));
    Surface->SetVisibility(ESlateVisibility::Visible);
    WidgetTree->RootWidget = Surface;
    SetVisibility(ESlateVisibility::Visible);
    return Super::RebuildWidget();
}

void UParametricRoadLayerInput::ActivateInput()
{
    if (bInputActive || !GetOwningPlayer() || !IsInViewport()) { return; }
    bInputActive = true;
    mUseKeyboard = mUseMouse = true;
    // The native setter requires a cached focusable Slate widget. RebuildWidget
    // runs before that cache exists; Present has now attached the real host.
    SetDefaultFocusWidget(this);
    SetInputMode();
    // Real keyboard focus stays here. The monitor owns the distinct virtual
    // focus used by its controls. Native SetupDefaultFocus searches for content
    // controls; this intentionally empty host must instead focus itself.
    SetUserFocus(GetOwningPlayer());
}

void UParametricRoadLayerInput::ReleaseForwardedKeys()
{
    const TSet<FKey> HeldPointer = MoveTemp(PointerKeys);
    const TSet<FKey> HeldKeyboard = MoveTemp(KeyboardKeys);
    PointerKeys.Reset();
    KeyboardKeys.Reset();
    if (UParametricRoadMonitor* Target = Monitor.Get())
    {
        for (const FKey& Key : HeldPointer) { Target->ReleaseConfigurationPointerKey(Key); }
        for (const FKey& Key : HeldKeyboard) { Target->ReleaseConfigurationKey(Key); }
    }
}

void UParametricRoadLayerInput::ReleaseInput()
{
    if (bReleasingInput) { return; }
    TGuardValue<bool> Releasing(bReleasingInput, true);
    ReleaseForwardedKeys();
    if (FSlateApplication::IsInitialized())
    {
        if (ULocalPlayer* LocalPlayer = GetOwningLocalPlayer())
        {
            const TSharedPtr<FSlateUser> User = LocalPlayer->GetSlateUser();
            if (User && User->DoesWidgetHaveCursorCapture(GetCachedWidget())) { User->ReleaseCursorCapture(); }
        }
    }
    if (!bInputActive) { return; }
    bInputActive = false;
    mUseKeyboard = mUseMouse = false;
    SetInputMode();
    if (AFGPlayerController* PC = Cast<AFGPlayerController>(GetOwningPlayer()))
    {
        if (UFGGameUI* UI = PC->GetGameUI())
        {
            const TArray<UFGInteractWidget*> Stack = UI->GetInteractWidgetStack();
            for (int32 Index = Stack.Num() - 1; Index >= 0; --Index)
            {
                if (IsValid(Stack[Index]) && Stack[Index] != this)
                {
                    Stack[Index]->SetInputMode();
                    Stack[Index]->SetupDefaultFocus();
                    break;
                }
            }
        }
    }
}

bool UParametricRoadLayerInput::IsOwnedInput() const
{
    const UParametricRoadSession* Owner = Session.Get();
    return bInputActive && !bReleasingInput && Owner && Owner->IsLayerInputOwner(this);
}

void UParametricRoadLayerInput::CloseOwnedConfiguration()
{
    if (UParametricRoadSession* Owner = Session.Get(); Owner && Owner->IsLayerInputOwner(this))
    {
        Owner->CloseLayerConfiguration();
    }
}

bool UParametricRoadLayerInput::NormalizePointer(const FGeometry& Geometry, const FVector2D& AbsolutePosition, FVector2D& OutPosition)
{
    const FVector2D Size = Geometry.GetLocalSize();
    if (!FMath::IsFinite(Size.X) || !FMath::IsFinite(Size.Y) || Size.X <= 0 || Size.Y <= 0) { return false; }
    const FVector2D Position = Geometry.AbsoluteToLocal(AbsolutePosition);
    if (!FMath::IsFinite(Position.X) || !FMath::IsFinite(Position.Y)) { return false; }
    // Preserve outside coordinates during a captured drag, rather than clamping
    // them onto edge buttons. Releases must still reach the virtual user.
    OutPosition = Position / Size;
    return true;
}

bool UParametricRoadLayerInput::RoutePointer(const FGeometry& Geometry, const FPointerEvent& Event)
{
    FVector2D Position;
    UParametricRoadMonitor* Target = Monitor.Get();
    if (!IsOwnedInput() || !Target) { return false; }
    Target->SetConfigurationModifiers(Event.GetModifierKeys());
    return Target->IsExpandedReady() && NormalizePointer(Geometry, Event.GetScreenSpacePosition(), Position)
        && Target->RouteConfigurationPointer(Position);
}

bool UParametricRoadLayerInput::IsCloseInput(const FKey& Key, const FInputEvent& Event) const
{
    return MatchesShortcut(Key, Event, ConfigureKey, ConfigureModifiers);
}

bool UParametricRoadLayerInput::MatchesShortcut(const FKey& Key, const FInputEvent& Event,
    const FKey& MappedKey, const TArray<FKey>& Modifiers)
{
    if (!MappedKey.IsValid() || Key != MappedKey) { return false; }
    for (const FKey& Modifier : Modifiers)
    {
        if ((Modifier == EKeys::LeftShift || Modifier == EKeys::RightShift) && Event.IsShiftDown()) { continue; }
        if ((Modifier == EKeys::LeftControl || Modifier == EKeys::RightControl) && Event.IsControlDown()) { continue; }
        if ((Modifier == EKeys::LeftAlt || Modifier == EKeys::RightAlt) && Event.IsAltDown()) { continue; }
        if ((Modifier == EKeys::LeftCommand || Modifier == EKeys::RightCommand) && Event.IsCommandDown()) { continue; }
        return false;
    }
    return true;
}

FEventReply UParametricRoadLayerInput::ConfigureShortcut()
{
    if (!IsOwnedInput() || IsTextEntryShortcut()) { return UWidgetBlueprintLibrary::Unhandled(); }
    if (ConfigureKey == EKeys::Escape && FinishTextEntry()) { return UWidgetBlueprintLibrary::Handled(); }
    CloseOwnedConfiguration();
    return UWidgetBlueprintLibrary::Handled();
}

bool UParametricRoadLayerInput::IsTextEntryShortcut() const
{
    const UParametricRoadMonitor* Target = Monitor.Get();
    if (!Target || !Target->IsConfigurationTextEntryFocused()) { return false; }
    const uint32* KeyCode = nullptr;
    const uint32* CharCode = nullptr;
    FInputKeyManager::Get().GetCodesFromKey(ConfigureKey, KeyCode, CharCode);
    if (CharCode && *CharCode >= 32 && *CharCode != 127) { return true; }
    // Platform character maps describe the main keyboard, not the numpad.
    // Those native keys still produce text through the hardware Char event.
    const FKey NumpadTextKeys[] = {EKeys::NumPadZero, EKeys::NumPadOne, EKeys::NumPadTwo, EKeys::NumPadThree,
        EKeys::NumPadFour, EKeys::NumPadFive, EKeys::NumPadSix, EKeys::NumPadSeven, EKeys::NumPadEight,
        EKeys::NumPadNine, EKeys::Multiply, EKeys::Add, EKeys::Subtract, EKeys::Decimal, EKeys::Divide};
    for (const FKey& Key : NumpadTextKeys) { if (ConfigureKey == Key) { return true; } }
    return false;
}

bool UParametricRoadLayerInput::FinishTextEntry()
{
    UParametricRoadMonitor* Target = Monitor.Get();
    return IsOwnedInput() && Target && Target->FinishConfigurationTextEntry();
}

void UParametricRoadLayerInput::OnEscapePressed_Implementation()
{
    if (IsOwnedInput() && !FinishTextEntry()) { CloseOwnedConfiguration(); }
}

FReply UParametricRoadLayerInput::NativeOnPreviewKeyDown(const FGeometry&, const FKeyEvent& Event)
{
    if (!IsOwnedInput()) { return FReply::Unhandled(); }
    if (UParametricRoadMonitor* Target = Monitor.Get()) { Target->SetConfigurationModifiers(Event.GetModifierKeys()); }
    if (Event.GetKey() == EKeys::Escape)
    {
        if (!Event.IsRepeat() && !FinishTextEntry()) { CloseOwnedConfiguration(); }
        return FReply::Handled();
    }
    if (IsCloseInput(Event.GetKey(), Event) && !IsTextEntryShortcut())
    {
        if (!Event.IsRepeat()) { CloseOwnedConfiguration(); }
        return FReply::Handled();
    }
    // Placement shortcuts remain modal-gated. Other keys belong to the native
    // focused control; precision is changed only by the terminal's checkbox.
    return FReply::Unhandled();
}

FReply UParametricRoadLayerInput::NativeOnKeyDown(const FGeometry&, const FKeyEvent& Event)
{
    if (!IsOwnedInput()) { return FReply::Unhandled(); }
    if (UParametricRoadMonitor* Target = Monitor.Get(); Target && Target->IsExpandedReady())
    {
        Target->SetConfigurationModifiers(Event.GetModifierKeys());
        KeyboardKeys.Add(Event.GetKey());
        Target->PressConfigurationKey(Event.GetKey(), Event.IsRepeat());
    }
    return FReply::Handled();
}

FReply UParametricRoadLayerInput::NativeOnKeyUp(const FGeometry&, const FKeyEvent& Event)
{
    if (!IsOwnedInput()) { return FReply::Unhandled(); }
    if (KeyboardKeys.Remove(Event.GetKey()))
    {
        if (UParametricRoadMonitor* Target = Monitor.Get())
        {
            Target->SetConfigurationModifiers(Event.GetModifierKeys());
            Target->ReleaseConfigurationKey(Event.GetKey());
        }
    }
    return FReply::Handled();
}

FReply UParametricRoadLayerInput::NativeOnKeyChar(const FGeometry&, const FCharacterEvent& Event)
{
    if (!IsOwnedInput()) { return FReply::Unhandled(); }
    if (UParametricRoadMonitor* Target = Monitor.Get(); Target && Target->IsExpandedReady())
    {
        Target->SetConfigurationModifiers(Event.GetModifierKeys());
        Target->SendConfigurationChar(FString::Chr(Event.GetCharacter()), Event.IsRepeat());
    }
    return FReply::Handled();
}

FReply UParametricRoadLayerInput::NativeOnPreviewMouseButtonDown(const FGeometry&, const FPointerEvent& Event)
{
    if (IsOwnedInput() && IsCloseInput(Event.GetEffectingButton(), Event))
    {
        CloseOwnedConfiguration();
        return FReply::Handled();
    }
    return FReply::Unhandled();
}

FReply UParametricRoadLayerInput::NativeOnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
    if (!IsOwnedInput()) { return FReply::Unhandled(); }
    const bool bHit = RoutePointer(Geometry, Event);
    if (!bLoggedPointer)
    {
        bLoggedPointer = true;
        UE_LOG(LogRoadworks, Verbose, TEXT("Road layer pointer received: host=viewport ready=%d screenHit=%d key=%s"),
            Monitor.IsValid() && Monitor->IsExpandedReady(), bHit, *Event.GetEffectingButton().ToString());
    }
    if (bHit)
    {
        PointerKeys.Add(Event.GetEffectingButton());
        Monitor->PressConfigurationPointerKey(Event.GetEffectingButton());
        // A native control can synchronously close/detach the configuration.
        // Never capture the removed host after that callback has returned.
        return IsOwnedInput() ? FReply::Handled().CaptureMouse(TakeWidget()) : FReply::Handled().ReleaseMouseCapture();
    }
    return FReply::Handled();
}

FReply UParametricRoadLayerInput::NativeOnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event)
{
    if (!IsOwnedInput()) { return FReply::Unhandled(); }
    RoutePointer(Geometry, Event);
    if (PointerKeys.Remove(Event.GetEffectingButton()))
    {
        if (UParametricRoadMonitor* Target = Monitor.Get()) { Target->ReleaseConfigurationPointerKey(Event.GetEffectingButton()); }
    }
    return PointerKeys.IsEmpty() ? FReply::Handled().ReleaseMouseCapture() : FReply::Handled();
}

FReply UParametricRoadLayerInput::NativeOnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event)
{
    if (!IsOwnedInput()) { return FReply::Unhandled(); }
    RoutePointer(Geometry, Event);
    return FReply::Handled();
}

FReply UParametricRoadLayerInput::NativeOnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event)
{
    if (!IsOwnedInput()) { return FReply::Unhandled(); }
    if (RoutePointer(Geometry, Event)) { Monitor->ScrollConfiguration(Event.GetWheelDelta()); }
    return FReply::Handled();
}

void UParametricRoadLayerInput::NativeOnMouseCaptureLost(const FCaptureLostEvent& Event)
{
    // Losing real capture (Alt-Tab, another native window) must not leave a
    // virtual drag pressed. Normal mouse-up also releases capture here.
    const TSet<FKey> Held = MoveTemp(PointerKeys);
    PointerKeys.Reset();
    if (UParametricRoadMonitor* Target = Monitor.Get())
    {
        for (const FKey& Key : Held) { Target->ReleaseConfigurationPointerKey(Key); }
    }
    Super::NativeOnMouseCaptureLost(Event);
}

void UParametricRoadLayerInput::NativeOnFocusLost(const FFocusEvent& Event)
{
    Super::NativeOnFocusLost(Event);
    if (IsOwnedInput()) { CloseOwnedConfiguration(); }
}

void UParametricRoadLayerInput::NativeTick(const FGeometry& Geometry, float DeltaSeconds)
{
    Super::NativeTick(Geometry, DeltaSeconds);
    if (!IsOwnedInput()) { return; }
    const AFGPlayerController* PC = Cast<AFGPlayerController>(GetOwningPlayer());
    if (!Monitor.IsValid() || !Monitor->IsValidMonitor() || (PC && (PC->IsPaused() || PC->IsPauseMenuOpen())))
    {
        CloseOwnedConfiguration();
    }
}

void UParametricRoadLayerInput::NativeConstruct() { Super::NativeConstruct(); }

void UParametricRoadLayerInput::NativeDestruct()
{
    ReleaseInput();
    CloseOwnedConfiguration();
    OverwriteKeybindings({});
    Keybindings.Reset();
    ConfigureModifiers.Reset();
    ConfigureKey = FKey();
    ConfigureAction = nullptr;
    Monitor.Reset();
    Session.Reset();
    Super::NativeDestruct();
}
