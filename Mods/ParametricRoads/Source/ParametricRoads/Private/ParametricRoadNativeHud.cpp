#include "ParametricRoadNativeHud.h"
#include "ParametricRoadNativeHints.h"
#include "ParametricRoadsModule.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Overlay.h"
#include "Components/PanelWidget.h"
#include "FGPlayerController.h"
#include "FGCharacterPlayer.h"
#include "Equipment/FGBuildGun.h"
#include "FGEquipmentChild.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "HAL/PlatformTime.h"
#include "UI/FGGameUI.h"
#include "UI/FGInteractWidget.h"

namespace
{
    constexpr const TCHAR* CrosshairClassPath = TEXT("/Game/FactoryGame/Interface/UI/InGame/-Shared/BPW_Crosshair.BPW_Crosshair_C");

    bool IsInside(const UWidget* Widget, const UWidget* Container)
    {
        for (const UWidget* Current = Widget; Current; Current = Current->GetParent())
        {
            if (Current == Container) { return true; }
        }
        return false;
    }
}

bool UParametricRoadNativeHud::ResolveBuildPanel(const UWidgetTree* Tree, UWidget*& OutPanel)
{
    OutPanel = nullptr;
    UWidget* Center = Tree ? Tree->FindWidget(TEXT("mBuildUIPanel")) : nullptr;
    UWidget* Hints = Tree ? Tree->FindWidget(TEXT("BuildDismantleHints")) : nullptr;
    // Verified against CL513885's real template: the whole center panel is a
    // sibling of the native keyboard/gamepad hint branch, not its ancestor.
    if (!Cast<UOverlay>(Center) || !Hints || !Center->GetParent()
        || Center->GetParent() != Hints->GetParent() || IsInside(Hints, Center)) { return false; }
    for (const TCHAR* Name : {TEXT("mBuildingNameText"), TEXT("mBuildModeContainer"), TEXT("mDisqualifiers")})
    {
        const UWidget* Child = Tree->FindWidget(Name);
        if (!Child || !IsInside(Child, Center)) { return false; }
    }
    OutPanel = Center;
    return true;
}

void UParametricRoadNativeHud::CollectTargets(AFGPlayerController* Player, bool bExpanded, TArray<UWidget*>& OutTargets)
{
    // Placement labels/refusals and the crosshair remain native in compact
    // mode. Only the expanded screen owns their presentation override.
    if (!bExpanded) { return; }
    UFGGameUI* UI = Player ? Player->GetGameUI() : nullptr;
    TSet<UUserWidget*> Visited;
    TFunction<void(UUserWidget*)> Visit = [&](UUserWidget* Widget)
    {
        if (!IsValid(Widget) || Visited.Contains(Widget)) { return; }
        Visited.Add(Widget);
        // A hidden native widget may become visible later in this same mode.
        // Discover it too, but never inspect another player's presentation.
        if (Widget->GetOwningPlayer() == Player)
        {
            const FString ClassPath = Widget->GetClass()->GetPathName();
            if (ClassPath == ParametricRoadNativeHints::BuildModePath)
            {
                UWidget* Center = nullptr;
                if (ResolveBuildPanel(Widget->WidgetTree, Center)) { OutTargets.AddUnique(Center); }
                else if (!bLoggedContractFailure)
                {
                    UE_LOG(LogParametricRoads, Warning, TEXT("Native Build Gun center layout differs from the pinned contract; its presentation was left unchanged."));
                    bLoggedContractFailure = true;
                }
            }
            else if (bExpanded && ClassPath == CrosshairClassPath) { OutTargets.AddUnique(Widget); }
        }
        if (Widget->WidgetTree)
        {
            Widget->WidgetTree->ForEachWidget([&](UWidget* Child)
                { if (UUserWidget* Nested = Cast<UUserWidget>(Child)) { Visit(Nested); } });
        }
    };
    Visit(UI);
    if (UI) { for (UFGInteractWidget* Widget : UI->GetInteractWidgetStack()) { Visit(Widget); } }
}

void UParametricRoadNativeHud::Restore(const FSuppressedWidget& Entry)
{
    if (UWidget* Widget = Entry.Widget.Get(); Widget && Widget->GetRenderOpacity() == 0)
    {
        // A later writer owns a nonzero value: don't overwrite it on teardown.
        Widget->SetRenderOpacity(Entry.PreviousOpacity);
    }
}

void UParametricRoadNativeHud::ApplyTargets(const TArray<UWidget*>& Targets, bool bExpanded)
{
    const TArray<UWidget*> NoTargets;
    const TArray<UWidget*>& EffectiveTargets = bExpanded ? Targets : NoTargets;
    for (int32 Index = Suppressed.Num() - 1; Index >= 0; --Index)
    {
        if (!Suppressed[Index].Widget.IsValid() || !EffectiveTargets.Contains(Suppressed[Index].Widget.Get()))
        {
            Restore(Suppressed[Index]);
            Suppressed.RemoveAt(Index);
        }
    }
    for (UWidget* Widget : EffectiveTargets)
    {
        if (!IsValid(Widget) || Suppressed.ContainsByPredicate([&](const FSuppressedWidget& Entry)
            { return Entry.Widget == Widget; })) { continue; }
        const float PreviousOpacity = Widget->GetRenderOpacity();
        if (!FMath::IsFinite(PreviousOpacity)) { continue; }
        Suppressed.Add({Widget, PreviousOpacity});
        if (!bLoggedTarget)
        {
            UE_LOG(LogParametricRoads, Verbose, TEXT("Expanded Roads screen scopes native placement presentation to local widgets: %s (previous opacity %.3f). Native hint branch unchanged."),
                *Widget->GetPathName(), PreviousOpacity);
            bLoggedTarget = true;
        }
    }
    SuppressTrackedWidgets();
}

void UParametricRoadNativeHud::SuppressTrackedWidgets()
{
    for (const FSuppressedWidget& Entry : Suppressed)
    {
        if (UWidget* Widget = Entry.Widget.Get(); Widget && Widget->GetRenderOpacity() != 0) { Widget->SetRenderOpacity(0); }
    }
}

void UParametricRoadNativeHud::ApplyViewTargets(TArray<TWeakObjectPtr<UPrimitiveComponent>>& Hidden,
    TArray<TWeakObjectPtr<UPrimitiveComponent>>& Owned, const TArray<UPrimitiveComponent*>& Targets)
{
    for (int32 Index = Owned.Num() - 1; Index >= 0; --Index)
    {
        if (!Owned[Index].IsValid() || !Targets.Contains(Owned[Index].Get()))
        {
            Hidden.RemoveSingle(Owned[Index]);
            Owned.RemoveAt(Index);
        }
    }
    for (UPrimitiveComponent* Component : Targets)
    {
        // Existing exclusions belong to the game or another local presentation
        // owner. Only record additions made here, never restore a global flag.
        if (IsValid(Component) && !Hidden.Contains(Component))
        {
            Hidden.Add(Component);
            Owned.AddUnique(Component);
        }
    }
}

void UParametricRoadNativeHud::UpdateEquipmentView(AFGPlayerController* Player, bool bExpanded)
{
    TArray<UPrimitiveComponent*> Targets;
    AFGCharacterPlayer* Character = Player ? Cast<AFGCharacterPlayer>(Player->GetPawn()) : nullptr;
    AFGBuildGun* Gun = Character ? Character->GetBuildGun() : nullptr;
    if (bExpanded && Character && Character->IsLocallyControlled() && Character->IsAliveAndWell()
        && Character->IsFirstPerson() && Character->GetTargetCameraMode() == ECameraMode::ECM_FirstPerson
        && Gun && Gun->GetInstigatorCharacter() == Character)
    {
        if (USkeletalMeshComponent* Arms = Character->GetMesh1P())
        {
            Targets.AddUnique(Arms);
            TArray<USceneComponent*> Children;
            Arms->GetChildrenComponents(true, Children);
            for (USceneComponent* Child : Children)
            {
                if (Child && Child->GetOwner() == Character)
                { if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Child)) { Targets.AddUnique(Primitive); } }
            }
        }
        // Own components only: the monitor and native holograms are separate
        // actors and must stay visible and interactive. No actor hiding or unequip.
        TInlineComponentArray<UPrimitiveComponent*> GunPrimitives(Gun);
        for (UPrimitiveComponent* Primitive : GunPrimitives) { Targets.AddUnique(Primitive); }
        if (AFGEquipmentChild* ChildEquipment = Gun->mChildEquipment)
        {
            TInlineComponentArray<UPrimitiveComponent*> ChildPrimitives(ChildEquipment);
            for (UPrimitiveComponent* Primitive : ChildPrimitives) { Targets.AddUnique(Primitive); }
        }
    }
    ApplyViewTargets(Player->HiddenPrimitiveComponents, OwnedHiddenPrimitives, Targets);
}

void UParametricRoadNativeHud::Update(AFGPlayerController* Player, bool bExpanded)
{
    if (!IsValid(Player) || !Player->IsLocalController()) { Clear(); return; }
    if (OwningPlayer.Get() != Player) { Clear(); OwningPlayer = Player; }
    UpdateEquipmentView(Player, bExpanded);
    UFGGameUI* UI = Player->GetGameUI();
    const double Now = FPlatformTime::Seconds();
    if (GameUI.Get() != UI || bWasExpanded != bExpanded || Now >= NextDiscoverySeconds)
    {
        TArray<UWidget*> Targets;
        CollectTargets(Player, bExpanded, Targets);
        ApplyTargets(Targets, bExpanded);
        GameUI = UI;
        bWasExpanded = bExpanded;
        NextDiscoverySeconds = Now + .2;
    }
    else { SuppressTrackedWidgets(); }
}

void UParametricRoadNativeHud::Clear()
{
    if (AFGPlayerController* Player = OwningPlayer.Get())
    { ApplyViewTargets(Player->HiddenPrimitiveComponents, OwnedHiddenPrimitives, {}); }
    OwnedHiddenPrimitives.Reset();
    for (const FSuppressedWidget& Entry : Suppressed) { Restore(Entry); }
    Suppressed.Reset();
    OwningPlayer.Reset();
    GameUI.Reset();
    NextDiscoverySeconds = 0;
    bWasExpanded = false;
}

void UParametricRoadNativeHud::BeginDestroy()
{
    Clear();
    Super::BeginDestroy();
}
