#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ParametricRoadNativeHud.generated.h"

class AFGPlayerController;
class UFGGameUI;
class UWidget;
class UWidgetTree;
class UPrimitiveComponent;

/** Local presentation override only. Native costs, refusals and key hints keep
 * updating normally; no build state or visibility binding is changed. */
UCLASS(Transient)
class ROADWORKS_API UParametricRoadNativeHud : public UObject
{
    GENERATED_BODY()
public:
    void Update(AFGPlayerController* Player, bool bExpanded);
    void Clear();
    void BeginDestroy() override;

private:
    friend class FParametricRoadNativeHudTest;
    struct FSuppressedWidget
    {
        TWeakObjectPtr<UWidget> Widget;
        float PreviousOpacity = 1;
    };
    static bool ResolveBuildPanel(const UWidgetTree* Tree, UWidget*& OutPanel);
    void CollectTargets(AFGPlayerController* Player, bool bExpanded, TArray<UWidget*>& OutTargets);
    void ApplyTargets(const TArray<UWidget*>& Targets, bool bExpanded);
    void SuppressTrackedWidgets();
    static void Restore(const FSuppressedWidget& Entry);
    void UpdateEquipmentView(AFGPlayerController* Player, bool bExpanded);
    static void ApplyViewTargets(TArray<TWeakObjectPtr<UPrimitiveComponent>>& Hidden,
        TArray<TWeakObjectPtr<UPrimitiveComponent>>& Owned, const TArray<UPrimitiveComponent*>& Targets);

    TArray<FSuppressedWidget> Suppressed;
    TArray<TWeakObjectPtr<UPrimitiveComponent>> OwnedHiddenPrimitives;
    TWeakObjectPtr<AFGPlayerController> OwningPlayer;
    TWeakObjectPtr<UFGGameUI> GameUI;
    double NextDiscoverySeconds = 0;
    bool bWasExpanded = false;
    bool bLoggedContractFailure = false;
    bool bLoggedTarget = false;
};
