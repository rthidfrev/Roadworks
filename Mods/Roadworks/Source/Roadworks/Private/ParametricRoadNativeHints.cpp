#include "ParametricRoadNativeHints.h"
#include "ParametricRoadNativeHud.h"
#include "ParametricRoadSession.h"
#include "ParametricRoadWidgets.h"
#include "RoadworksModule.h"
#include "Blueprint/WidgetTree.h"
#include "FGInputLibrary.h"
#include "FGPlayerController.h"
#include "FGPlayerInput.h"
#include "GameFramework/HUD.h"
#include "InputAction.h"
#include "UI/FGGameUI.h"
#include "UI/FGInteractWidget.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

namespace
{
    // Optional legacy next/+/- bindings remain in Controls, without duplicating
    // the wheel workflow or crowding the native Build Gun hint row.
    constexpr int32 DisplayedShortcuts[] = {8, 3, 4, 5, 9};
    struct FHintSchema
    {
        UFunction* Function = nullptr;
        FArrayProperty* Parameter = nullptr;
        FArrayProperty* Cached = nullptr;
        FTextProperty* Action = nullptr;
        FTextProperty* Key = nullptr;
        bool Resolve(UClass* Class)
        {
            Function = Class ? Class->FindFunctionByName(TEXT("SetKeybindingHints")) : nullptr;
            Cached = Class ? FindFProperty<FArrayProperty>(Class, TEXT("mCachedKeybinds")) : nullptr;
            if (!Function || Function->NumParms != 1 || !Cached) { return false; }
            for (TFieldIterator<FProperty> It(Function); It; ++It)
            { if (It->HasAnyPropertyFlags(CPF_Parm)) { Parameter = CastField<FArrayProperty>(*It); break; } }
            const auto* Element = CastField<FStructProperty>(Cached->Inner);
            const auto* Input = Parameter ? CastField<FStructProperty>(Parameter->Inner) : nullptr;
            if (!Element || !Input || Element->Struct != Input->Struct) { return false; }
            for (TFieldIterator<FProperty> It(Element->Struct); It; ++It)
            {
                if (It->GetName().StartsWith(TEXT("Action_"))) { Action = CastField<FTextProperty>(*It); }
                if (It->GetName().StartsWith(TEXT("KeyBinding_"))) { Key = CastField<FTextProperty>(*It); }
            }
            return Action && Key;
        }
    };
    bool IsOwned(const FText& Text)
    {
        const auto Namespace = FTextInspector::GetNamespace(Text);
        return Namespace.IsSet() && Namespace.GetValue() == TEXT("Roadworks.NativeHint");
    }
    UUserWidget* FindBuildHints(UFGGameUI* UI, AFGPlayerController* PC)
    {
        UUserWidget* Result = nullptr;
        TSet<UUserWidget*> Visited;
        TFunction<void(UUserWidget*)> Visit = [&](UUserWidget* Widget)
        {
            if (!Widget || Result || Visited.Contains(Widget) || !Widget->IsVisible()) { return; }
            Visited.Add(Widget);
            if (Widget->GetClass()->GetFName() == TEXT("Widget_BuildMode_C") && Widget->GetOwningPlayer() == PC)
            { Result = Widget; return; }
            if (Widget->WidgetTree) { Widget->WidgetTree->ForEachWidget([&](UWidget* Child)
                { if (auto* Nested = Cast<UUserWidget>(Child)) { Visit(Nested); } }); }
        };
        Visit(UI);
        if (UI) { for (UFGInteractWidget* Widget : UI->GetInteractWidgetStack()) { Visit(Widget); } }
        return Result;
    }
}

bool ParametricRoadNativeHints::ResolveContract(UClass* Class, FString& Error)
{
    FHintSchema Schema;
    if (Schema.Resolve(Class)) { return true; }
    Error = TEXT("Widget_BuildMode must expose SetKeybindingHints(array<Struct_KeybindingHint>) and mCachedKeybinds with FText Action/KeyBinding fields.");
    return false;
}

void UParametricRoadSession::UpdateNativeHints(bool bRemove)
{
    AFGPlayerController* PC = InputController.Get();
    UUserWidget* Widget = NativeHintWidget.Get();
    if (!bRemove && (!Widget || !Widget->IsVisible()))
    {
        if (Widget) { UpdateNativeHints(true); }
        Widget = PC ? FindBuildHints(PC->GetGameUI(), PC) : nullptr;
        NativeHintWidget = Widget;
    }
    if (!Widget)
    {
        if (!bRemove && !bLoggedHintMissing) { UE_LOG(LogRoadworks, Warning, TEXT("Native Build Gun widget not found in the local UI yet; retrying without creating a substitute row.")); bLoggedHintMissing = true; }
        return;
    }
    if (!bRemove && !bLoggedHintFound) { UE_LOG(LogRoadworks, Verbose, TEXT("Native Build Gun keyboard hint target: %s"), *Widget->GetPathName()); bLoggedHintFound = true; }
    FHintSchema Schema;
    if (!Schema.Resolve(Widget->GetClass())) { UE_LOG(LogRoadworks, Error, TEXT("Native Build Gun hint schema changed; no entries were modified.")); return; }
    void* Existing = Schema.Cached->ContainerPtrToValuePtr<void>(Widget);
    FScriptArrayHelper Current(Schema.Cached, Existing);
    FStructOnScope Frame(Schema.Function); // Initializes and destroys FText/array parameters, including on early return.
    FScriptArrayHelper Entries(Schema.Parameter, Schema.Parameter->ContainerPtrToValuePtr<void>(Frame.GetStructMemory()));
    bool bChanged = false;
    if ((NativePrimaryLabel.IsEmpty() || NativeRotationLabel.IsEmpty()) && Current.Num() > 0)
    {
        if (auto* Standard = FindFProperty<FArrayProperty>(Widget->GetClass(), TEXT("mStandardKeybinds")))
        {
            FScriptArrayHelper Base(Standard, Standard->ContainerPtrToValuePtr<void>(Widget));
            if (Base.Num() > 0)
            {
                const FText BuildLabel = Schema.Action->GetPropertyValue_InContainer(Base.GetRawPtr(0));
                if (!BuildLabel.IsEmpty() && BuildLabel.EqualTo(Schema.Action->GetPropertyValue_InContainer(Current.GetRawPtr(0)))) { NativePrimaryLabel = BuildLabel; }
            }
            // CL513885 Widget_BuildMode CDO [1] is the string-table RotateHologram
            // entry. Require the same actual label in the live list before editing.
            if (Base.Num() > 1 && NativeRotationLabel.IsEmpty())
            {
                const FText RotateLabel = Schema.Action->GetPropertyValue_InContainer(Base.GetRawPtr(1));
                for (int32 I = 0; I < Current.Num(); ++I)
                {
                    if (!RotateLabel.IsEmpty() && RotateLabel.EqualTo(Schema.Action->GetPropertyValue_InContainer(Current.GetRawPtr(I))))
                    { NativeRotationLabel = RotateLabel; break; }
                }
            }
        }
    }
    const FText PrimaryLabel = bRemove ? NativePrimaryLabel
        : GetPrimaryHint();
    const FText RotationLabel = bRemove ? NativeRotationLabel
        : NSLOCTEXT("Roadworks", "NativeWheelAdjust", "Adjust value");
    TArray<FText> ExpectedKeys, ExpectedLabels;
    if (!bRemove)
    {
        for (int32 Index : DisplayedShortcuts)
        {
            // Precision is a checkbox-only option in the terminal. Do not
            // advertise an inactive key or rebuild the native hint bar every
            // time the checkbox changes while that modal surface owns input.
            if (Index == 5 && IsLayerConfigurationOpen()) continue;
            ExpectedKeys.Add(GetShortcutKey(Index)); ExpectedLabels.Add(GetShortcutLabel(Index));
        }
    }
    const int32 RoadHintCount = ExpectedLabels.Num();
    int32 OwnedCount = 0;
    for (int32 I = 0; I < Current.Num(); ++I)
    {
        const uint8* From = Current.GetRawPtr(I);
        const FText Label = Schema.Action->GetPropertyValue_InContainer(From);
        const auto LabelID = FTextInspector::GetKey(Label);
        const auto LabelNamespace = FTextInspector::GetNamespace(Label);
        const bool bOurPrimary = LabelNamespace.IsSet() && LabelNamespace.GetValue() == TEXT("Roadworks") && LabelID.IsSet()
            && (LabelID.GetValue() == TEXT("HintStart") || LabelID.GetValue() == TEXT("HintEnd")
                || LabelID.GetValue() == TEXT("HintConfirm") || LabelID.GetValue() == TEXT("HintBuilding"));
        const bool bPrimary = !NativePrimaryLabel.IsEmpty() && (Label.EqualTo(NativePrimaryLabel)
            || bOurPrimary);
        if (bPrimary)
        {
            uint8* To = Entries.GetRawPtr(Entries.AddValue());
            Schema.Cached->Inner->CopyCompleteValue(To, From);
            Schema.Action->SetPropertyValue_InContainer(To, PrimaryLabel);
            bChanged |= !Label.EqualTo(PrimaryLabel);
            continue;
        }
        const bool bOurRotation = LabelNamespace.IsSet() && LabelNamespace.GetValue() == TEXT("Roadworks")
            && LabelID.IsSet() && LabelID.GetValue() == TEXT("NativeWheelAdjust");
        if (!NativeRotationLabel.IsEmpty() && (bOurRotation || Label.EqualTo(NativeRotationLabel)))
        {
            uint8* To = Entries.GetRawPtr(Entries.AddValue());
            Schema.Cached->Inner->CopyCompleteValue(To, From);
            Schema.Action->SetPropertyValue_InContainer(To, RotationLabel);
            bChanged |= !Label.EqualTo(RotationLabel);
            continue;
        }
        if (IsOwned(Label))
        {
            if (!bRemove && ExpectedLabels.IsValidIndex(OwnedCount))
            { bChanged |= !Label.EqualTo(ExpectedLabels[OwnedCount]) || !Schema.Key->GetPropertyValue_InContainer(From).EqualTo(ExpectedKeys[OwnedCount]); }
            ++OwnedCount;
            continue;
        }
        const int32 N = Entries.AddValue();
        Schema.Cached->Inner->CopyCompleteValue(Entries.GetRawPtr(N), From);
    }
    bChanged |= bRemove ? OwnedCount > 0 : OwnedCount != RoadHintCount;
    if (!bChanged) { return; } // No widget rebuild per frame/timer when unchanged.
    if (!bRemove)
    {
        for (int32 I = 0; I < RoadHintCount; ++I)
        {
            uint8* Entry = Entries.GetRawPtr(Entries.AddValue());
            Schema.Action->SetPropertyValue_InContainer(Entry, ExpectedLabels[I]);
            Schema.Key->SetPropertyValue_InContainer(Entry, ExpectedKeys[I]);
        }
    }
    Widget->ProcessEvent(Schema.Function, Frame.GetStructMemory());
    UE_LOG(LogRoadworks, Verbose, TEXT("Native Build Gun hints updated: widget=%s, roadEntries=%d"), *Widget->GetPathName(), bRemove ? 0 : RoadHintCount);
    if (bRemove) { NativeHintWidget.Reset(); NativePrimaryLabel = NativeRotationLabel = FText::GetEmpty(); }
}

FText UParametricRoadSession::GetShortcutKey(int32 Index) const
{
    AFGPlayerController* PC = InputController.Get();
    const UFGPlayerInput* Input = PC ? Cast<UFGPlayerInput>(PC->PlayerInput) : nullptr;
    const UInputAction* Action = nullptr;
    if (Index < 2)
    {
        Action = LoadObject<UInputAction>(nullptr, Index == 0
            ? TEXT("/Game/FactoryGame/Inputs/Equipment/Buildgun/IA_BuildGun_PrimaryFire.IA_BuildGun_PrimaryFire")
            : TEXT("/Game/FactoryGame/Inputs/Equipment/Buildgun/IA_BuildGun_SecondaryFire.IA_BuildGun_SecondaryFire"));
    }
    else if (InputActions.IsValidIndex(Index - 2)) { Action = InputActions[Index - 2]; }
    FKey Key;
    TArray<FKey> Modifiers;
    if (!Input || !Action || !Input->GetCurrentMappingForInputAction(Action, Key, &Modifiers, NAME_None, nullptr) || !Key.IsValid())
    { return NSLOCTEXT("Roadworks", "UnboundShortcut", "Unbound"); }
    FString Text;
    for (const FKey& Modifier : Modifiers)
    {
        if (Modifier.IsValid()) { Text += UFGInputLibrary::GetAbbreviatedKeyName(Modifier).ToString() + TEXT(" + "); }
    }
    Text += UFGInputLibrary::GetAbbreviatedKeyName(Key).ToString();
    return Index == 8 ? FText::Format(NSLOCTEXT("Roadworks", "SelectorWheelKey", "{0} + Wheel"), FText::FromString(Text))
        : FText::FromString(Text);
}

FText UParametricRoadSession::GetShortcutLabel(int32 Index) const
{
    if (Index == 0) { return GetPrimaryHint(); }
    if (Index == 1) { return GetSecondaryHint(); }
    // State belongs to the worksheet checkbox. A stateful label here would
    // rebuild the entire native hint row on every precision toggle, unlike
    // point/setting selection whose action labels remain stable.
    static const FText Labels[] = {
        NSLOCTEXT("Roadworks.NativeHint", "NextSetting", "Next setting"),
        NSLOCTEXT("Roadworks.NativeHint", "SwitchEndpoint", "Select point"),
        NSLOCTEXT("Roadworks.NativeHint", "AddPoint", "Add point"),
        NSLOCTEXT("Roadworks.NativeHint", "TogglePrecision", "Fine step"),
        NSLOCTEXT("Roadworks.NativeHint", "IncreaseValue", "Increase"),
        NSLOCTEXT("Roadworks.NativeHint", "DecreaseValue", "Decrease"),
        NSLOCTEXT("Roadworks.NativeHint", "SelectSetting", "Select setting"),
        NSLOCTEXT("Roadworks.NativeHint", "ConfigureLayers", "Open terminal")};
    return Labels[FMath::Clamp(Index - 2, 0, 7)];
}


void UParametricRoadSession::UpdateHudVisibility()
{
    AFGPlayerController* PC = InputController.Get();
    if (PC && IsActive())
    {
        if (!NativeHud) { NativeHud = NewObject<UParametricRoadNativeHud>(this); }
        NativeHud->Update(PC, IsLayerConfigurationOpen());
    }
    else if (NativeHud) { NativeHud->Clear(); NativeHud = nullptr; }
    const bool bShow = PC && !PC->IsPauseMenuOpen() && (!PC->GetHUD() || PC->GetHUD()->bShowHUD);
    if (StatusWidget) { StatusWidget->SetVisibility(bShow ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden); }
}
