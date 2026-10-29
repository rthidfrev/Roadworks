#pragma once

#include "UI/FGInteractWidget.h"
#include "ParametricRoadLayerInput.generated.h"

class UInputAction;
class UParametricRoadMonitor;
class UParametricRoadSession;
struct FGameViewportWidgetSlot;

/** Transparent native modal owner. The monitor's world widget draws all content;
 * this surface forwards real input to its separate Slate virtual user. */
UCLASS(Transient)
class ROADWORKS_API UParametricRoadLayerInput : public UFGInteractWidget
{
    GENERATED_BODY()
public:
    explicit UParametricRoadLayerInput(const FObjectInitializer& ObjectInitializer);
    void InitializeTarget(UParametricRoadSession* InSession, UParametricRoadMonitor* InMonitor);
    void PrepareInput();
    bool Present();
    void ActivateInput();
    void ReleaseInput();
    void OnEscapePressed_Implementation() override;

protected:
    TSharedRef<SWidget> RebuildWidget() override;
    void NativeConstruct() override;
    void NativeDestruct() override;
    void NativeTick(const FGeometry& Geometry, float DeltaSeconds) override;
    void NativeOnFocusLost(const FFocusEvent& Event) override;
    void NativeOnMouseCaptureLost(const FCaptureLostEvent& Event) override;
    FReply NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
    FReply NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
    FReply NativeOnKeyUp(const FGeometry& Geometry, const FKeyEvent& Event) override;
    FReply NativeOnKeyChar(const FGeometry& Geometry, const FCharacterEvent& Event) override;
    FReply NativeOnPreviewMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
    FReply NativeOnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
    FReply NativeOnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event) override;
    FReply NativeOnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event) override;
    FReply NativeOnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event) override;

private:
    friend class FParametricRoadLayerInputTest;
    UFUNCTION() FEventReply ConfigureShortcut();
    static FGameViewportWidgetSlot MakeViewportSlot();
    static bool NormalizePointer(const FGeometry& Geometry, const FVector2D& AbsolutePosition, FVector2D& OutPosition);
    bool IsOwnedInput() const;
    bool RoutePointer(const FGeometry& Geometry, const FPointerEvent& Event);
    bool IsCloseInput(const FKey& Key, const FInputEvent& Event) const;
    static bool MatchesShortcut(const FKey& Key, const FInputEvent& Event, const FKey& MappedKey, const TArray<FKey>& Modifiers);
    bool IsTextEntryShortcut() const;
    bool FinishTextEntry();
    void CloseOwnedConfiguration();
    void ReleaseForwardedKeys();

    UPROPERTY(Transient) TWeakObjectPtr<UParametricRoadSession> Session;
    UPROPERTY(Transient) TWeakObjectPtr<UParametricRoadMonitor> Monitor;
    UPROPERTY(Transient) TObjectPtr<UInputAction> ConfigureAction;
    FKey ConfigureKey;
    TArray<FKey> ConfigureModifiers;
    TSet<FKey> PointerKeys;
    TSet<FKey> KeyboardKeys;
    bool bInputActive = false;
    bool bReleasingInput = false;
    bool bLoggedPointer = false;
};
