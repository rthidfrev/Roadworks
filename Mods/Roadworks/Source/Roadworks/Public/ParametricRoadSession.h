#pragma once

#include "CoreMinimal.h"
#include "FGBuildableSubsystem.h"
#include "ItemAmount.h"
#include "Input/FGBoundMappingContextHandle.h"
#include "ParametricRoadGeometry.h"
#include "ParametricRoadLayerTypes.h"
#include "ParametricRoadPresets.h"
#include "Subsystems/WorldSubsystem.h"
#include "ParametricRoadSession.generated.h"

class AFGBlueprintHologram;
class AFGHologram;
class UFGBlueprintDescriptor;
class UParametricRoadLayerWidget;
class UParametricRoadLayerInput;
class UParametricRoadNativeHud;
class UFGGameUI;
class USceneComponent;
class UFGBuildGunStateBuild;
class UParametricRoadStatusWidget;
class UFGInputMappingContext;
class UInputAction;
class UInputMappingContext;
struct FInputActionValue;
class UEnhancedInputComponent;
class UParametricRoadPreview;
class UParametricRoadDirection;
class UParametricRoadMonitor;
class UUserWidget;
class AFGPlayerController;
class AFGBuildGun;

UENUM()
enum class EParametricRoadParameter : uint8
{
    StartHeading, StartGrade, StartBank, EndHeading, EndGrade, EndBank,
    StartHeight, EndHeight, Length, Width, Thickness, Overlap, EdgeStep, CurveTension, DeckPlane
};

UENUM()
enum class EParametricRoadPhase : uint8
{
    Start, Drawing, Ready, Building
};

enum class EParametricRoadAdjustment : uint8
{
    Heading, Grade, Bank, Height, Tension, Overlap, Length, Width, Thickness, EdgeStep, DeckPlane, Axis, Count
};

/** A fixed centre with separately editable height above its captured anchor. */
struct FParametricRoadWaypoint
{
    FParametricRoadControlPoint Point;
    double HeightOffset = 0;
};

/** Local transient tool state. It is neither a saved object nor a network subobject. */
UCLASS(Transient)
class ROADWORKS_API UParametricRoadSession : public UObject
{
    GENERATED_BODY()
public:
    void Initialize(UFGBuildGunStateBuild* State, AFGBlueprintHologram* Hologram);
    UWorld* GetWorld() const override;
    bool IsActive() const;
    void Tick(float DeltaSeconds);
    void UpdateNativePlacement(const FHitResult& Hit, bool bValid);
    void Primary();
    void Secondary();
    bool ConsumesSecondary() const { return IsLayerConfigurationOpen() || Phase != EParametricRoadPhase::Start; }
    void AdjustValue(int32 Delta);
    void CycleAdjustment(int32 Delta = 1);
    bool CanUseNativeRotation() const;
    bool IsSelectingAdjustment() const;
    // Keeps the existing hook entry point; wheel edits the selected value now.
    void RotateHeading(int32 Delta);
    void ApplyWheel(int32 Delta, bool bSelectParameter);
    void Shutdown();
    void RestoreNativeServerSource();

    void AcknowledgeBlueprint(const FString& BlueprintName, int32 NumConstructed);
    void RefuseConstruction(FNetConstructionID Id);
    void ObserveNativeConstruction(FNetConstructionID Id);

    double GetParameter(EParametricRoadParameter Parameter) const;
    void SetParameter(EParametricRoadParameter Parameter, double Value);
    void ToggleForwardAxis();
    bool IsForwardY() const { return bForwardY; }
    bool IsBuilding() const { return Phase == EParametricRoadPhase::Building; }
    const FText& GetStatus() const { return Status; }
    FText GetDetails() const;
    FText GetCostText() const;
    FText GetControlsText() const;
    FText GetPhaseText() const;
    FText GetShortcutKey(int32 Index) const;
    FText GetShortcutLabel(int32 Index) const;
    FText GetPanelStatus() const;
    FText GetPrimaryHint() const;
    FText GetSecondaryHint() const;
    FText GetAdjustmentLabel(int32 Index) const;
    FText GetAdjustmentValue(int32 Index, bool bStart) const;
    FText GetRouteSummary() const;
    FText GetSelectionSummary() const;
    double GetDiagramValue(int32 Index) const;
    int32 GetShortcutCount() const { return 10; }
    bool EditsStart() const { return bEditStart; }
    int32 GetAdjustmentIndex() const { return int32(Adjustment); }
    bool UsesFineAdjustment() const { return bFineAdjustment; }
    int32 GetPointCount() const { return IntermediatePoints.Num() + 2; }
    int32 GetSelectedPointIndex() const;
    FText GetPointLabel(int32 Index) const;
    FText GetSelectedPointLabel() const { return GetPointLabel(GetSelectedPointIndex()); }
    FTransform GetPointPose(int32 Index) const;
    FText GetSelectedAdjustmentValue(int32 Index) const;
    bool IsSelectedPointAutomatic(int32 AdjustmentIndex) const;
    void SelectPoint(int32 Index);
    void CyclePoint(int32 Delta = 1);
    // Expanded editor actions; ordinary Build Gun input remains modal-gated.
    void SelectPointFromWidget(int32 Index);
    void SelectAdjustmentFromWidget(int32 Index);
    void SetAdjustmentFromWidget(int32 Index, double Value);
    void SetFineAdjustmentFromWidget(bool bFine);

    const TArray<FParametricRoadBlueprintEntry>& GetBlueprintCatalog() const { return BlueprintCatalog; }
    const TArray<FParametricRoadBlueprintCategory>& GetBlueprintCategories() const { return BlueprintCategories; }
    uint32 GetBlueprintCatalogRevision() const { return BlueprintCatalogRevision; }
    void RefreshBlueprintCatalogIfNeeded(float DeltaSeconds);
    static constexpr int32 MaximumLayers = 8;
    int32 GetLayerCount() const { return Layers.Num(); }
    int32 GetSelectedLayerIndex() const { return SelectedLayerIndex; }
    void SelectLayer(int32 Index);
    bool AddLayer();
    bool RemoveSelectedLayer();
    const FParametricRoadLayerSettings& GetLayerSettings() const { return GetLayerSettings(SelectedLayerIndex); }
    const FParametricRoadLayerSettings& GetLayerSettings(int32 Index) const;
    FString GetRoadBlueprintName() const { return BlueprintName; }
    void RefreshBlueprintCatalog();
    void SelectDecorationBlueprint(const FString& Name);
    void SetLayerSpacing(double Metres);
    void SetLayerFirstPlacement(double Metres);
    void SetLayerOffset(int32 Axis, double Metres);
    void SetLayerEnabled(bool bEnabled);
    void SetLayerFollowGrade(bool bFollow);
    void SetLayerFollowBank(bool bFollow);
    FText GetLayerStatus() const;
    FText GetLayerStatus(int32 Index) const;
    int32 GetLayerPlacementCount() const { return GetLayerPlacementCount(SelectedLayerIndex); }
    int32 GetLayerPlacementCount(int32 Index) const;
    int32 GetTotalLayerPlacementCount() const;
    UFUNCTION() void ToggleLayerConfiguration();
    void CloseLayerConfiguration();
    bool IsLayerConfigurationOpen() const { return bLayerConfigurationOpen; }
    bool IsLayerInputOwner(const UParametricRoadLayerInput* Input) const { return bLayerConfigurationOpen && LayerInput.Get() == Input; }

    FParametricRoadPreset CapturePreset(const FString& Name) const;
    bool CanManagePresets() const;
    bool ValidatePresetBlueprints(const FParametricRoadPreset& Preset, FText& Error) const;
    bool LoadPresetFromWidget(const FParametricRoadPreset& Preset, FText& Error);
    bool HasRoutePreview() const { return Phase != EParametricRoadPhase::Start || !IntermediatePoints.IsEmpty(); }
    const FString& GetActivePresetId() const { return ActivePresetId; }
    const FString& GetActivePresetName() const { return ActivePresetName; }
    void RememberSavedPresetFromWidget(const FParametricRoadPreset& Preset);

    UFGBuildGunStateBuild* GetBuildState() const { return BuildState.Get(); }
    AFGBlueprintHologram* GetSource() const { return Source.Get(); }
    void CollectNativePreviewHolograms(TArray<AFGBlueprintHologram*>& Out) const;

private:
    friend class UParametricRoadWorldSubsystem;
    friend class FParametricRoadPresetSessionTest;
    friend class FParametricRoadConstructionContextTest;
    friend class FParametricRoadControlsTest;
    friend class FParametricRoadWidgetTest;
    friend class FParametricRoadNativeContractsTest;
    friend class FParametricRoadInputBindingTest;
    friend class FParametricRoadTransformTest;
    friend class FParametricRoadPointEditingTest;
    friend class FParametricRoadLayerSessionTest;
    friend class FParametricRoadMultiLayerSessionTest;
    friend class FParametricRoadLayerOffsetTest;
    friend class FParametricRoadLayerWidgetTest;
    friend class FParametricRoadLayerInputTest;
    friend class FParametricRoadWidgetEditingTest;
    bool CanEditFromWidget() const;
    bool ApplyPresetSettings(const FParametricRoadPreset& Preset, FText& Error);
    bool MatchesBuildContext(const UFGBuildGunStateBuild* State, const AFGHologram* Hologram) const;
    void SelectPointInternal(int32 Index);
    void SetParameterInternal(EParametricRoadParameter Parameter, double Value);
    void SetSelectedPointValueInternal(EParametricRoadAdjustment Selected, double Value);
    void SetAdjustmentInternal(EParametricRoadAdjustment Selected, double Value);
    void ToggleForwardAxisInternal();
    bool CanAcceptRoadInput() const;
    void BindControls();
    void UnbindControls();
    void BindActionHandlers(UEnhancedInputComponent* Component);
    void EnsureWheelSelectionContext();
    void LogWheelContext(const TCHAR* Reason, float RawValue = 0) const;
    bool HandleAction(int32 Index);
    void UpdateHudVisibility();
    void UpdateNativeHints(bool bRemove = false);
    UFUNCTION() void NextSetting();
    UFUNCTION() void SwitchEndpoint();
    UFUNCTION() void AddPoint();
    UFUNCTION() void TogglePrecision();
    UFUNCTION() void IncreaseValue();
    UFUNCTION() void DecreaseValue();
    UFUNCTION() void SelectSetting();
    void SelectSettingWheel(const FInputActionValue& Value);
    bool AssessBlueprint();
    bool InitializeRoadOrientation(const FTransform& BlueprintPose);
    void RefreshInvalidCursor();
    void SetSourcePreviewVisible(bool bVisible);
    void Solve();
    void UpdatePreviews();
    void ClearPreviews();
    void BeginConstruction();
    void SendNextSlice();
    void WithNativeBuildContext(UFGBuildGunStateBuild* State, AFGHologram* Hologram,
        UFGBlueprintDescriptor* Descriptor, TFunctionRef<void()> Work);
    void QueueNativeServerSource(const FString& Name);
    void StopConstruction(const FText& Reason);
    bool CanAfford(int32 Count) const;
    bool CanAffordJobs(int32 First, int32 Count) const;
    TMap<UClass*, int64> RequiredJobCost(int32 First, int32 Count) const;
    int32 GetBuildJobCount() const;
    int32 GetBuildJobLayer(int32 Index, int32* OutStation = nullptr) const;
    bool IsLayerJob(int32 Index) const;
    FTransform GetBuildJobPose(int32 Index) const;
    UFGBlueprintDescriptor* GetBuildJobDescriptor(int32 Index) const;
    FString GetBuildJobName(int32 Index) const;
    int32 GetBuildJobMemberCount(int32 Index) const;
    void EnsureLayerTemplate();
    int32 FindLayerNeedingTemplate() const;
    void ClearLayerTemplate(int32 Index);
    void ClearLayerTemplates();
    void CancelLayerLoad();
    void InvalidateChangedLayerSources();
    FParametricRoadLayerState* SelectedLayer();
    UFUNCTION() void OnNativeBlueprintUpdated(UFGBlueprintDescriptor* Descriptor);
    bool IsBuildJobSourceCurrent(int32 Index) const;
    void SetPreviewAppearance(uint8 State);
    AFGBlueprintHologram* SpawnNativePreview(const FTransform& Pose, UFGBlueprintDescriptor* Descriptor = nullptr);
    FTransform ToBlueprintPose(const FTransform& RoadPose) const;
    FTransform RoadPoseAt(const FVector& Center, bool bStart) const;
    FParametricRoadRequest ControlPointRequest() const;
    TArray<FParametricRoadControlPoint> ControlPoints() const;
    bool HasPendingExtension() const;
    double GetSelectedAdjustmentNumber(EParametricRoadAdjustment Selected) const;
    void MarkDirty();

    UPROPERTY(Transient)
    TWeakObjectPtr<UFGBuildGunStateBuild> BuildState;
    UPROPERTY(Transient)
    TWeakObjectPtr<AFGBlueprintHologram> Source;
    TArray<FTransform> PreviewPoses;
    FParametricRoadPath EvaluatedPath;
    UPROPERTY(Transient) TArray<FParametricRoadLayerState> Layers {FParametricRoadLayerState()};
    int32 SelectedLayerIndex = 0;
    int32 LayerPreviewCursor = 0;
    TArray<FParametricRoadBlueprintEntry> BlueprintCatalog;
    TArray<FParametricRoadBlueprintCategory> BlueprintCategories;
    uint32 BlueprintCatalogRevision = 0;
    float BlueprintCatalogPollSeconds = 0;
    bool bBlueprintCatalogDirty = true;
    bool bBlueprintCatalogIconsReady = false;
    TWeakObjectPtr<UObject> BlueprintCatalogIconDatabase;
    UPROPERTY(Transient) TMap<int32, TObjectPtr<UObject>> BlueprintCatalogIcons;
    TMap<int32, FString> BlueprintCatalogIconPaths;
    friend class FParametricRoadCatalogTest;
    void RebuildBlueprintCatalog(TArray<FParametricRoadBlueprintEntry> Entries,
        TConstArrayView<class UFGBlueprintCategory*> Categories, bool bIconsReady,
        TFunctionRef<UObject*(int32)> ResolveIcon);
    UPROPERTY(Transient) TObjectPtr<AFGBlueprintHologram> LayerLoadingHologram;
    int32 LoadingLayerIndex = INDEX_NONE;
    UPROPERTY(Transient) TObjectPtr<UParametricRoadLayerWidget> LayerWidget;
    UPROPERTY(Transient) TObjectPtr<UParametricRoadLayerInput> LayerInput;
    UPROPERTY(Transient) TObjectPtr<UParametricRoadNativeHud> NativeHud;
    UPROPERTY(Transient) TWeakObjectPtr<UFGGameUI> LayerGameUI;
    FString PendingBlueprintName, QueuedServerBlueprintName;
    int32 PendingMemberCount = 0;
    bool bLayerConfigurationOpen = false, bLayerSolutionValid = true;
    double LayerLoadSeconds = 0;
    FString ValidationBlueprintName;
    bool bValidationLayer = false;
    bool bLoadingNativePreview = false;
    TSet<FString> ChangedLayerSources;
    UPROPERTY(Transient)
    TObjectPtr<UParametricRoadStatusWidget> StatusWidget;
    UPROPERTY(Transient)
    TObjectPtr<AFGBlueprintHologram> PendingHologram;
    UPROPERTY(Transient)
    TArray<FItemAmount> SliceCost;

    UPROPERTY(Transient) TObjectPtr<UFGInputMappingContext> InputContext;
    UPROPERTY(Transient) TArray<TObjectPtr<UInputAction>> InputActions;
    UPROPERTY(Transient) TObjectPtr<UInputMappingContext> WheelSelectionContext;
    UPROPERTY(Transient) TObjectPtr<UInputAction> WheelSelectionAction;
    FBoundMappingContextHandle WheelSelectionHandle;
    UPROPERTY(Transient) TWeakObjectPtr<AFGPlayerController> InputController;
    UPROPERTY(Transient) TWeakObjectPtr<UEnhancedInputComponent> BoundInput;
    UPROPERTY(Transient) TObjectPtr<UParametricRoadPreview> VisualPreview;
    UPROPERTY(Transient) TObjectPtr<UParametricRoadDirection> DirectionMarkers;
    UPROPERTY(Transient) TObjectPtr<UParametricRoadMonitor> Monitor;
    UPROPERTY(Transient) TObjectPtr<AFGBlueprintHologram> ValidationHologram;
    TWeakObjectPtr<UUserWidget> NativeHintWidget;
    double HintRefreshSeconds = 0;
    bool bValidationPoseReady = false, bValidationFailed = false;
    bool bLoggedHintMissing = false, bLoggedHintFound = false;
    double NativeBuildWorkSeconds = 0.025;
    FText ValidationFailure;
    FText NativePrimaryLabel;
    FText NativeRotationLabel;
    uint8 LoggedActions = 0;
    bool bLastInputAllowed = false;
    bool bLoggedWheelSelection = false;
    bool bLoggedWheelInput = false, bLoggedWheelContext = false;
    uint8 WheelContextCheckTicks = 0;
    bool bFineAdjustment = false;
    bool bPresetSwitchPending = false;
    bool bNativeBuildContextActive = false;
    FString ActivePresetId, ActivePresetName;

    // Heading/grade are automatic until individually edited. Fixed centres and
    // offsets survive selecting/editing earlier points and adding later ones.
    TArray<FParametricRoadWaypoint> IntermediatePoints;
    int32 SelectedIntermediatePoint = INDEX_NONE;
    bool bPendingExtension = false;
    FVector ExtensionOrigin = FVector::ZeroVector;

    FParametricRoadRequest Parameters;
    FParametricRoadSolution Solution;
    EParametricRoadPhase Phase = EParametricRoadPhase::Start;
    EParametricRoadAdjustment Adjustment = EParametricRoadAdjustment::Heading;
    bool bEditStart = true;
    bool bEndExplicit = false;
    bool bEndHeadingExplicit = false, bEndGradeExplicit = false;
    FVector LocalCenter = FVector::ZeroVector;
    FVector CursorCenter = FVector::ZeroVector;
    FVector StartCenter = FVector::ZeroVector;
    FVector EndCenter = FVector::ZeroVector;
    double StartHeading = 0, StartGrade = 0, StartBank = 0;
    double EndHeading = 0, EndGrade = 0, EndBank = 0;
    double StartHeight = 0, EndHeight = 0, BaseCenterZ = 0;
    double SolveDelay = 0, ValidationDelay = 0, PendingSeconds = 0, SendDelay = 0, NoticeSeconds = 0;
    int32 PreviewRevision = 0, SolutionRevision = 1, ValidatedCount = 0;
    int32 MemberCount = 0, NextSlice = 0, ConfirmedSlices = 0;
    bool bForwardY = false, bAssessed = false, bBlueprintRejected = false, bCursorValid = false;
    bool bDirty = true, bPreviewsValid = false, bAwaitingAck = false, bShuttingDown = false, bServerRefused = false;
    bool bHasNativeId = false;
    bool bSourceFloorOverridden = false, bOriginalSourceNeedsFloor = false;
    FString BlueprintName;
    FNetConstructionID PendingId;
    FText Status;
    TMap<TWeakObjectPtr<USceneComponent>, bool> SourceComponentVisibility;
};

/** Owns only local sessions; no SML actor subsystem or persistent road actor. */
UCLASS(Transient)
class ROADWORKS_API UParametricRoadWorldSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    static UParametricRoadSession* Find(UFGBuildGunStateBuild* State, bool bCreate = false);
    static UParametricRoadSession* Find(AFGBlueprintHologram* Hologram, bool bCreate = false);
    static bool IsPresetLoading(AFGBuildGun* Gun);
    bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
    void Tick(float DeltaTime) override;
    TStatId GetStatId() const override;
    void Deinitialize() override;
private:
    friend class UParametricRoadSession;
    struct FQueuedPreset
    {
        TWeakObjectPtr<AFGBuildGun> Gun;
        TWeakObjectPtr<AFGBlueprintHologram> OriginalSource;
        TWeakObjectPtr<UClass> Recipe;
        TWeakObjectPtr<UClass> ModeAfterSelection;
        FParametricRoadPreset Preset;
        double Seconds = 0;
        bool bSelectionRequested = false;
        bool bModeRequested = false;
    };
    bool QueuePreset(UParametricRoadSession* Origin, const FParametricRoadPreset& Preset, FText& Error);
    void TickPresetRequests(float DeltaSeconds);
    bool HasPresetRequest(AFGBuildGun* Gun) const;
    TArray<FQueuedPreset> PresetRequests;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UParametricRoadSession>> Sessions;
};
