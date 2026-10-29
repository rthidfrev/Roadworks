#include "ParametricRoadSession.h"
#include "ParametricRoadInputContext.h"
#include "RoadworksModule.h"

#include "EnhancedInputComponent.h"
#include "Equipment/FGBuildGunBuild.h"
#include "FGCharacterPlayer.h"
#include "FGPlayerController.h"
#include "FGPlayerInput.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputTriggers.h"
#include "Input/FGInputMappingContext.h"
#include "Input/FGChildInputMappingContext.h"

namespace
{
    constexpr const TCHAR* ActionNames[] = {TEXT("NextSetting"), TEXT("SwitchEndpoint"), TEXT("AddPoint"),
        TEXT("TogglePrecision"), TEXT("IncreaseValue"), TEXT("DecreaseValue"), TEXT("SelectSetting"), TEXT("ConfigureLayers")};
    // Eight authored actions and one session-local wheel observer.
    constexpr int32 OwnedBindingCount = UE_ARRAY_COUNT(ActionNames) + 1;
}

void UParametricRoadSession::BindControls()
{
    AFGBuildGun* Gun = BuildState.IsValid() ? BuildState->GetBuildGun() : nullptr;
    AFGCharacterPlayer* Player = Gun ? Gun->GetInstigatorCharacter() : nullptr;
    AFGPlayerController* PC = Player ? Cast<AFGPlayerController>(Player->GetController()) : nullptr;
    UEnhancedInputComponent* Component = Gun ? Cast<UEnhancedInputComponent>(Gun->InputComponent) : nullptr;
    if (!PC || !Component) { return; } // Equipment may still be initializing. Tick retries.
    int32 OwnedBindings = 0;
    for (const auto& Binding : Component->GetActionEventBindings()) { OwnedBindings += Binding->IsBoundToObject(this) ? 1 : 0; }
    if (BoundInput.Get() == Component && InputController.Get() == PC && WheelSelectionHandle.IsValid()
        && InputActions.Num() == UE_ARRAY_COUNT(ActionNames) && OwnedBindings == OwnedBindingCount)
    {
        // Binding requests rebuild the mappings at the end of the frame. Inspect
        // the evaluated list once, after giving that rebuild time to complete.
        if (!bLoggedWheelContext && ++WheelContextCheckTicks >= 3)
        {
            LogWheelContext(TEXT("post-rebuild"));
            bLoggedWheelContext = true;
        }
        return;
    }
    UnbindControls();
    if (!InputContext)
    {
        InputContext = LoadObject<UFGInputMappingContext>(nullptr, TEXT("/Roadworks/Inputs/MC_Roads.MC_Roads"));
        InputActions.Reset();
        for (const TCHAR* Name : ActionNames)
        {
            const FString Path = FString::Printf(TEXT("/Roadworks/Inputs/IA_%s.IA_%s"), Name, Name);
            InputActions.Add(LoadObject<UInputAction>(nullptr, *Path));
        }
    }
    if (!InputContext || InputActions.Num() != UE_ARRAY_COUNT(ActionNames) || InputActions.Contains(nullptr))
    {
        UE_LOG(LogRoadworks, Error, TEXT("Road input assets incomplete; bindings were not installed."));
        return;
    }
    InputController = PC;
    // Child contexts are activated by their native parent, never bound directly.
    // BindMappingContext explicitly rejects UFGChildInputMappingContext in CL 513885.
    EnsureWheelSelectionContext();
    // This ordinary, transient context exists only for this local road session.
    // The eight authored/remappable actions still follow their native child context.
    // Observe the physical wheel without adding chord blockers to native input.
    // The handler chooses selection from the current remappable selector value.
    WheelSelectionHandle = PC->BindMappingContext(WheelSelectionContext, 1000, this);
    BindActionHandlers(Component);
    UE_LOG(LogRoadworks, Verbose, TEXT("Road controls bound: component=%s, native=%s, parent=%s, paused=%d, buildGate=%d"),
        *Component->GetPathName(), *Gun->GetPathName(),
        *CastChecked<UFGChildInputMappingContext>(InputContext)->mParentContext.ToString(), PC->IsPauseMenuOpen(), PC->GetDisabledInputGate().mBuildGun);
    UE_LOG(LogRoadworks, Verbose, TEXT("Road selection wheel context bound: handle=%d, selector=%s"),
        WheelSelectionHandle.IsValid(), *InputActions[6]->GetPathName());
}

void UParametricRoadSession::EnsureWheelSelectionContext()
{
    if (WheelSelectionContext) { return; }
    // This context's lifetime and handlers already follow the native road/input
    // gate; project input-mode tag changes must not silently remove its wheel.
    WheelSelectionContext = NewObject<UParametricRoadWheelContext>(this, NAME_None, RF_Transient);
    WheelSelectionAction = NewObject<UInputAction>(this, NAME_None, RF_Transient);
    WheelSelectionAction->ValueType = EInputActionValueType::Axis1D;
    WheelSelectionAction->bConsumeInput = false;
    WheelSelectionAction->bTriggerWhenPaused = false;
    WheelSelectionContext->MapKey(WheelSelectionAction, EKeys::MouseWheelAxis);
}

void UParametricRoadSession::BindActionHandlers(UEnhancedInputComponent* Component)
{
    EnsureWheelSelectionContext();
    Component->ClearBindingsForObject(this);
    BoundInput = Component;
    // Use the equipment component that already receives construction input.
    // Do not bind on the controller below the equipment's input stack entry.
    Component->BindAction(InputActions[0], ETriggerEvent::Started, this, &ThisClass::NextSetting);
    Component->BindAction(InputActions[1], ETriggerEvent::Started, this, &ThisClass::SwitchEndpoint);
    Component->BindAction(InputActions[2], ETriggerEvent::Started, this, &ThisClass::AddPoint);
    Component->BindAction(InputActions[3], ETriggerEvent::Started, this, &ThisClass::TogglePrecision);
    Component->BindAction(InputActions[4], ETriggerEvent::Triggered, this, &ThisClass::IncreaseValue);
    Component->BindAction(InputActions[5], ETriggerEvent::Triggered, this, &ThisClass::DecreaseValue);
    Component->BindAction(InputActions[6], ETriggerEvent::Started, this, &ThisClass::SelectSetting);
    Component->BindAction(InputActions[7], ETriggerEvent::Started, this, &ThisClass::ToggleLayerConfiguration);
    Component->BindAction(WheelSelectionAction, ETriggerEvent::Triggered, this, &ThisClass::SelectSettingWheel);
}

void UParametricRoadSession::UnbindControls()
{
    if (UEnhancedInputComponent* Component = BoundInput.Get()) { Component->ClearBindingsForObject(this); }
    if (AFGPlayerController* PC = InputController.Get(); PC && WheelSelectionHandle.IsValid())
    { PC->UnbindMappingContext(WheelSelectionContext, WheelSelectionHandle); }
    WheelSelectionHandle.Reset();
    WheelContextCheckTicks = 0;
    bLoggedWheelContext = false;
    BoundInput.Reset();
    InputController.Reset();
}

bool UParametricRoadSession::CanAcceptRoadInput() const
{
    AFGPlayerController* PC = InputController.Get();
    // The native equipment/input gate decides whether construction is allowed.
    // A nonempty UI stack alone does not mean a menu is blocking the build gun.
    return !bPresetSwitchPending && !IsLayerConfigurationOpen() && IsActive() && PC && !PC->IsPauseMenuOpen() && !PC->GetDisabledInputGate().mBuildGun;
}

bool UParametricRoadSession::HandleAction(int32 Index)
{
    const bool bAllowed = CanAcceptRoadInput() && !IsBuilding();
    const uint8 Bit = uint8(1 << Index);
    if (!(LoggedActions & Bit) || bAllowed != bLastInputAllowed)
    {
        UE_LOG(LogRoadworks, Verbose, TEXT("Road action %s received: allowed=%d, active=%d, phase=%d"),
            ActionNames[Index], bAllowed, IsActive(), int32(Phase));
        LoggedActions |= Bit;
    }
    bLastInputAllowed = bAllowed;
    return bAllowed;
}

void UParametricRoadSession::NextSetting() { if (HandleAction(0)) { CycleAdjustment(); } }
void UParametricRoadSession::SwitchEndpoint() { if (HandleAction(1)) { CyclePoint(); } }
void UParametricRoadSession::TogglePrecision()
{
    if (HandleAction(3)) { bFineAdjustment = !bFineAdjustment; }
}
void UParametricRoadSession::IncreaseValue() { if (HandleAction(4)) { AdjustValue(1); } }
void UParametricRoadSession::DecreaseValue() { if (HandleAction(5)) { AdjustValue(-1); } }
void UParametricRoadSession::SelectSetting() { HandleAction(6); }

void UParametricRoadSession::SelectSettingWheel(const FInputActionValue& Value)
{
    // Log before routing/rounding so a fractional wheel or an inactive selector
    // cannot be mistaken for a missing Enhanced Input delegate.
    if (!bLoggedWheelInput)
    {
        LogWheelContext(TEXT("first-wheel"), Value.Get<float>());
        bLoggedWheelInput = true;
    }
    if (!IsSelectingAdjustment()) { return; }
    const int32 Delta = FMath::RoundToInt(Value.Get<float>());
    if (!Delta) { return; }
    const int32 Before = GetAdjustmentIndex();
    ApplyWheel(Delta, true);
    if (!bLoggedWheelSelection)
    {
        UE_LOG(LogRoadworks, Verbose, TEXT("Road selection wheel received: delta=%d, selectorHeld=%d, phase=%d, setting=%d->%d"),
            Delta, IsSelectingAdjustment(), int32(Phase), Before, GetAdjustmentIndex());
        bLoggedWheelSelection = true;
    }
}

void UParametricRoadSession::LogWheelContext(const TCHAR* Reason, float RawValue) const
{
    if (!UE_LOG_ACTIVE(LogRoadworks, Verbose)) { return; }
    const AFGPlayerController* PC = InputController.Get();
    const UFGPlayerInput* Input = PC ? Cast<UFGPlayerInput>(PC->PlayerInput) : nullptr;
    const UInputAction* Selector = InputActions.IsValidIndex(6) ? InputActions[6].Get() : nullptr;
    const FInputActionInstance* SelectorInstance = Input && Selector ? Input->FindActionInstanceData(Selector) : nullptr;
    int32 WheelMappings = 0;
    int32 SelectorMappings = 0;
    FString WheelTriggers;
    if (Input)
    {
        for (const FEnhancedActionKeyMapping& Mapping : Input->GetEnhancedActionMappings())
        {
            WheelMappings += Mapping.Action == WheelSelectionAction ? 1 : 0;
            SelectorMappings += Mapping.Action == Selector ? 1 : 0;
            if (Mapping.Action == WheelSelectionAction)
            {
                for (const UInputTrigger* Trigger : Mapping.Triggers)
                {
                    if (Trigger)
                    {
                        if (!WheelTriggers.IsEmpty()) { WheelTriggers += TEXT(","); }
                        WheelTriggers += Trigger->GetClass()->GetName();
                    }
                }
            }
        }
    }
    UE_LOG(LogRoadworks, Verbose,
        TEXT("Road wheel state (%s): raw=%.4f, allowed=%d, selectorValue=%d, selectorEvent=%d, contextApplied=%d, wheelMappings=%d, selectorMappings=%d, wheelTriggers=%s, phase=%d"),
        Reason, RawValue, CanAcceptRoadInput(), Input && Selector ? Input->GetActionValue(Selector).Get<bool>() : false,
        SelectorInstance ? int32(SelectorInstance->GetTriggerEvent()) : -1,
        Input && Input->GetAppliedInputContextData().Contains(WheelSelectionContext.Get()), WheelMappings, SelectorMappings,
        WheelTriggers.IsEmpty() ? TEXT("none") : *WheelTriggers, int32(Phase));
}

bool UParametricRoadSession::IsSelectingAdjustment() const
{
    const AFGPlayerController* PC = InputController.Get();
    const UFGPlayerInput* Input = PC ? Cast<UFGPlayerInput>(PC->PlayerInput) : nullptr;
    // CSS evaluates all actions before dispatch, and its GetValue uses the real
    // evaluated value by default. This also handles press/release on the same
    // frame as wheel input, even when chord dispatch suppresses selector callbacks.
    return CanAcceptRoadInput() && !IsBuilding() && Input && InputActions.IsValidIndex(6)
        && InputActions[6] && Input->GetActionValue(InputActions[6]).Get<bool>();
}

void UParametricRoadSession::AddPoint()
{
    if (!bAssessed || IsBuilding() || IsLayerConfigurationOpen() || (InputController.IsValid() && !HandleAction(2))) { return; }
    if (Phase == EParametricRoadPhase::Start) { Primary(); return; }
    if (IntermediatePoints.Num() >= 30)
    {
        Status = NSLOCTEXT("Roadworks", "PointLimit", "Maximum 30 intermediate points. Set the end to continue.");
        NoticeSeconds = 3;
        return;
    }
    if (Phase == EParametricRoadPhase::Drawing)
    {
        RefreshInvalidCursor();
        if (!bCursorValid) { return; }
    }
    if (HasPendingExtension()) { return; }
    const FVector Base = Phase == EParametricRoadPhase::Ready ? EndCenter : CursorCenter;
    const FTransform Point = RoadPoseAt(Phase == EParametricRoadPhase::Ready ? EndCenter : CursorCenter, false);
    const FVector Previous = IntermediatePoints.IsEmpty() ? RoadPoseAt(StartCenter, true).GetLocation()
        : IntermediatePoints.Last().Point.Pose.GetLocation();
    if (FVector::Distance(Previous, Point.GetLocation()) < 1.0) { return; }
    FParametricRoadWaypoint Waypoint;
    Waypoint.Point.Pose = Point;
    Waypoint.Point.bManualHeading = bEndHeadingExplicit;
    Waypoint.Point.bManualGrade = bEndGradeExplicit;
    Waypoint.HeightOffset = EndHeight;
    IntermediatePoints.Add(Waypoint);
    ExtensionOrigin = CursorCenter = Base;
    bPendingExtension = true;
    // Manual edits belong to the point just fixed, rather than silently forcing
    // every future intermediate point to use the same tangent.
    bEndHeadingExplicit = bEndGradeExplicit = false;
    Phase = EParametricRoadPhase::Drawing;
    SelectPoint(GetPointCount() - 1);
    NoticeSeconds = 0;
    MarkDirty();
}
