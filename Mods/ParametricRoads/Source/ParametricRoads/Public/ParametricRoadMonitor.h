#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Components/WidgetInteractionComponent.h"
#include "UObject/Object.h"
#include "ParametricRoadMonitorAnimation.h"
#include "ParametricRoadMonitorSway.h"
#include "ParametricRoadMonitorPower.h"
#include "ParametricRoadMonitorOcclusion.h"
#include "ParametricRoadMonitor.generated.h"

class AActor;
class UAudioComponent;
class USoundBase;
class AFGBuildGun;
class AFGCharacterPlayer;
class UCameraComponent;
class UParametricRoadSession;
class USceneComponent;
class UUserWidget;
class UWidgetComponent;
class UMaterialParameterCollection;

/** Native UMG virtual pointer with event-only keyboard dispatch. */
UCLASS(Transient)
class PARAMETRICROADS_API UParametricRoadMonitorInteraction : public UWidgetInteractionComponent
{
    GENERATED_BODY()
public:
    void SetEventModifiers(const FModifierKeysState& Value) { ModifierKeys = Value; }
    bool DispatchKey(FKey Key, bool bPressed, bool bRepeat);
    void UpdatePointer() { SimulatePointerMovement(); }
private:
    friend class UParametricRoadMonitor;
    friend class FParametricRoadLayerInputTest;
};

/** A local, disposable view accessory. The engine actor and all components are
 * transient; neither equipment/inventory nor saved native buildings reference it. */
UCLASS(Transient)
class PARAMETRICROADS_API UParametricRoadMonitor : public UObject
{
    GENERATED_BODY()
public:
    /** Does not take the widget out of its viewport until all required assets
     * and the local presentation have been created successfully. */
    bool Initialize(UParametricRoadSession* InSession, AFGBuildGun* InGun, UUserWidget* InWidget);
    void Tick(float DeltaSeconds);
    /** Completes its short animation independently after the session releases it. */
    void BeginRetract();
    void Clear();
    bool IsValidMonitor() const;
    bool IsVisible() const { return bVisible && IsValidMonitor(); }
    /** Local presentation only; the native interact-widget stack owns input. */
    void SetExpanded(bool bExpanded);
    bool IsExpandedReady() const;
    /** Normalized native viewport rectangle at the modeled glass plane.
     * Invalid until the frame is fully expanded, settled and visible. */
    FBox2D GetConfigurationRect() const;
    bool SetConfigurationWidget(UUserWidget* ConfigOrNull);
    bool RouteConfigurationPointer(FVector2D NormalizedViewport);
    bool PressConfigurationPointerKey(FKey Key);
    void ReleaseConfigurationPointerKey(FKey Key);
    void ScrollConfiguration(float Delta);
    bool PressConfigurationKey(FKey Key, bool bRepeat = false);
    bool ReleaseConfigurationKey(FKey Key);
    bool SendConfigurationChar(FString Characters, bool bRepeat = false);
    void SetConfigurationModifiers(const FModifierKeysState& Modifiers);
    /** Queries only this monitor's virtual Slate user, never hardware focus. */
    bool IsConfigurationTextEntryFocused() const;
    /** Finishes/cancels the focused text edit before Escape can close the panel. */
    bool FinishConfigurationTextEntry();
    const FText& GetUnavailableReason() const { return UnavailableReason; }
    void BeginDestroy() override;

private:
    void ApplyPose();
    bool UpdateSway(float DeltaSeconds, bool bShow);
    void UpdateAudio(bool bShow);
    void StopAudio();
    void UpdateHologramOcclusion(bool bShow);
    bool ShouldShow() const;
    void LogInitialCameras() const;
    void LogVisibilityState(bool bShow);
    void LogRenderState() const;
    void OnWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources);
    void ClearConfigurationInteraction();
    void FinishConfigurationDispatch();
    void FinalizeConfigurationDetach();
    bool CanRouteConfiguration() const;
    friend class FParametricRoadMonitorDispatchTest;
    friend class FParametricRoadLayerInputTest;

    UPROPERTY(Transient) TObjectPtr<AActor> Presentation;
    UPROPERTY(Transient) TObjectPtr<USoundBase> DeploySound;
    UPROPERTY(Transient) TObjectPtr<USoundBase> RetractSound;
    UPROPERTY(Transient) TObjectPtr<UAudioComponent> DeployAudio;
    UPROPERTY(Transient) TObjectPtr<UAudioComponent> RetractAudio;
    UPROPERTY(Transient) TObjectPtr<USceneComponent> ViewRoot;
    UPROPERTY(Transient) TObjectPtr<USceneComponent> ArmPivot;
    UPROPERTY(Transient) TObjectPtr<USceneComponent> HousingPivot;
    UPROPERTY(Transient) TObjectPtr<UWidgetComponent> Screen;
    UPROPERTY(Transient) TObjectPtr<UParametricRoadMonitorInteraction> ConfigurationInteraction;
    UPROPERTY(Transient) TObjectPtr<UMaterialParameterCollection> PaniniCollection;
    UPROPERTY(Transient) TWeakObjectPtr<UParametricRoadSession> Session;
    UPROPERTY(Transient) TWeakObjectPtr<AFGBuildGun> Gun;
    UPROPERTY(Transient) TWeakObjectPtr<AFGCharacterPlayer> Player;
    UPROPERTY(Transient) TWeakObjectPtr<UCameraComponent> Camera;
    UPROPERTY(Transient) TWeakObjectPtr<UUserWidget> Widget;
    UPROPERTY(Transient) TWeakObjectPtr<UUserWidget> ConfigurationWidget;
    UPROPERTY(Transient) TWeakObjectPtr<AActor> DiagnosticViewTarget;
    FTSTicker::FDelegateHandle RetractionTicker;
    FDelegateHandle WorldCleanupHandle;
    FText UnavailableReason;
    TArray<FVector> GeometryCorners;
    TSet<FKey> ConfigurationPointerKeys;
    TSet<FKey> ConfigurationKeyboardKeys;
    int32 ConfigurationDispatchDepth = 0;
    double LayoutAspect = 16.0 / 9.0;
    ParametricRoadMonitorAnimation::FPose RetractionStartPose;
    float RetractionStartDeployment = 0;
    float RetractionProgress = 0;
    float RetractionElapsed = 0, PowerOffDelay = 0;
    ParametricRoadMonitorPower::FPose Power, RetractionStartPower;
    float Deployment = 0;
    float Expansion = 0;
    float ReadoutSeconds = 0;
    ParametricRoadMonitorSway::FState Sway;
    FParametricRoadMonitorOcclusion HologramOcclusion;
    uint32 RenderedRevision = MAX_uint32;
    uint32 DiagnosticGateMask = MAX_uint32;
    int32 DiagnosticCurrentMode = INDEX_NONE, DiagnosticTargetMode = INDEX_NONE;
    uint8 DiagnosticOwnerMask = MAX_uint8;
    float DiagnosticVisibleSeconds = 0;
    bool bLoggedRenderState = false;
    bool bRetracting = false;
    bool bDeployAudioPlayed = false, bRetractAudioPlayed = false;
    bool bInRetractionTicker = false;
    bool bVisible = false;
    bool bScreenVisible = false;
    bool bExpandedTarget = false;
    bool bConfigurationDetaching = false;
    bool bConfigurationPointerOnScreen = false;
    bool bConfigurationCleanupPending = false;
    bool bMonitorCleanupPending = false;
};
