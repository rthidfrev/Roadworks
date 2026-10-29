#include "ParametricRoadSession.h"
#include "ParametricRoadLayerWidget.h"
#include "ParametricRoadLayerInput.h"
#include "ParametricRoadMonitor.h"
#include "ParametricRoadPreview.h"
#include "ParametricRoadEligibility.h"
#include "ParametricRoadsModule.h"
#include "Equipment/FGBuildGunBuild.h"
#include "FGBlueprintSubsystem.h"
#include "FGPlayerController.h"
#include "Hologram/FGBlueprintHologram.h"
#include "UI/FGGameUI.h"
#include "Components/SceneComponent.h"

void UParametricRoadSession::ToggleLayerConfiguration()
{
    if (bLayerConfigurationOpen) { CloseLayerConfiguration(); return; }
    if (!bAssessed || IsBuilding() || !CanAcceptRoadInput()) { return; }
    AFGPlayerController* PC = InputController.Get();
    UFGGameUI* UI = PC ? PC->GetGameUI() : nullptr;
    if (!UI) { return; }
    RefreshBlueprintCatalog();
    LayerWidget = CreateWidget<UParametricRoadLayerWidget>(PC);
    if (!LayerWidget) { return; }
    LayerWidget->SetSession(this);
    LayerInput = CreateWidget<UParametricRoadLayerInput>(PC);
    if (!LayerInput) { LayerWidget = nullptr; return; }
    LayerInput->mInteractObject = this;
    LayerInput->InitializeTarget(this, Monitor);
    LayerInput->PrepareInput();
    LayerGameUI = UI;
    bLayerConfigurationOpen = true;
    // Content belongs to the physical screen. Only a transparent input owner
    // enters the viewport, above native GameUI, to forward real cursor events.
    if (!Monitor || !Monitor->SetConfigurationWidget(LayerWidget) || !LayerInput->Present())
    {
        UE_LOG(LogParametricRoads, Error, TEXT("3D layer configuration could not initialize; restoring construction input."));
        CloseLayerConfiguration();
        Status = NSLOCTEXT("ParametricRoads", "LayerSurfaceFailed", "The monitor configuration could not be opened. Construction controls restored.");
        NoticeSeconds = 10;
        return;
    }
    if (!UI->GetInteractWidgetStack().Contains(LayerInput.Get())) { UI->AddInteractWidget(LayerInput); }
    LayerInput->ActivateInput();
}

void UParametricRoadSession::CloseLayerConfiguration()
{
    if (!bLayerConfigurationOpen && !LayerWidget && !LayerInput) { return; }
    // Clear session ownership before detachment triggers native callbacks.
    bLayerConfigurationOpen = false;
    UParametricRoadLayerInput* Input = LayerInput;
    LayerInput = nullptr;
    LayerWidget = nullptr;
    UFGGameUI* UI = LayerGameUI.Get();
    LayerGameUI.Reset();
    if (Monitor) { Monitor->SetConfigurationWidget(nullptr); }
    if (UI && Input && UI->GetInteractWidgetStack().Contains(Input)) { UI->RemoveInteractWidget(Input); }
    if (Input) { Input->RemoveFromParent(); Input->ReleaseInput(); }
    // Opening/closing alone preserves every point, phase and cached solution.
}

FParametricRoadLayerState* UParametricRoadSession::SelectedLayer()
{
    return Layers.IsValidIndex(SelectedLayerIndex) ? &Layers[SelectedLayerIndex] : nullptr;
}

const FParametricRoadLayerSettings& UParametricRoadSession::GetLayerSettings(int32 Index) const
{
    static const FParametricRoadLayerSettings Empty;
    return Layers.IsValidIndex(Index) ? Layers[Index].Settings : Empty;
}

void UParametricRoadSession::SelectLayer(int32 Index)
{
    if (Layers.IsValidIndex(Index)) { SelectedLayerIndex = Index; }
    // Selection is UI state only: keep every snapshot, point and solution.
}

bool UParametricRoadSession::AddLayer()
{
    if (IsBuilding() || Layers.Num() >= MaximumLayers) { return false; }
    SelectedLayerIndex = Layers.AddDefaulted();
    return true; // An empty disabled layer adds no preview/build work.
}

bool UParametricRoadSession::RemoveSelectedLayer()
{
    if (IsBuilding() || !Layers.IsValidIndex(SelectedLayerIndex)) { return false; }
    const int32 Removed = SelectedLayerIndex;
    const bool bAffectedSolution = Layers[Removed].Settings.bEnabled;
    ClearLayerTemplate(Removed);
    Layers.RemoveAt(Removed);
    if (LoadingLayerIndex > Removed) { --LoadingLayerIndex; }
    SelectedLayerIndex = Layers.IsEmpty() ? INDEX_NONE : FMath::Min(Removed, Layers.Num() - 1);
    if (bAffectedSolution) { MarkDirty(); }
    return true;
}

int32 UParametricRoadSession::GetLayerPlacementCount(int32 Index) const
{
    return Layers.IsValidIndex(Index) && Layers[Index].Settings.bEnabled ? Layers[Index].PreviewPoses.Num() : 0;
}

int32 UParametricRoadSession::GetTotalLayerPlacementCount() const
{
    int32 Total = 0;
    for (const FParametricRoadLayerState& Layer : Layers)
    { if (Layer.Settings.bEnabled) { Total += Layer.PreviewPoses.Num(); } }
    return Total;
}

void UParametricRoadSession::CancelLayerLoad()
{
    if (IsValid(LayerLoadingHologram)) { LayerLoadingHologram->Destroy(); }
    LayerLoadingHologram = nullptr;
    LoadingLayerIndex = INDEX_NONE;
    LayerLoadSeconds = 0;
}

void UParametricRoadSession::ClearLayerTemplate(int32 Index)
{
    if (!Layers.IsValidIndex(Index)) { return; }
    if (LoadingLayerIndex == Index) { CancelLayerLoad(); }
    FParametricRoadLayerState& Layer = Layers[Index];
    if (Layer.Preview) { Layer.Preview->Clear(); }
    Layer.Preview = nullptr;
    Layer.Descriptor = nullptr;
    Layer.Cost.Reset(); Layer.PreviewPoses.Reset();
    Layer.LoadedName.Reset(); Layer.MemberCount = 0; Layer.VisualRevision = 0;
    Layer.Status = FText::GetEmpty();
    Layer.bRejected = false; Layer.bSolutionValid = !Layer.Settings.bEnabled;
}

void UParametricRoadSession::ClearLayerTemplates()
{
    CancelLayerLoad();
    for (int32 Index = 0; Index < Layers.Num(); ++Index) { ClearLayerTemplate(Index); }
    ChangedLayerSources.Reset();
}

void UParametricRoadSession::SelectDecorationBlueprint(const FString& Name)
{
    FParametricRoadLayerState* Layer = SelectedLayer();
    if (IsBuilding() || !Layer || Name == Layer->Settings.BlueprintName) { return; }
    ClearLayerTemplate(SelectedLayerIndex);
    Layer->Settings.BlueprintName = Name;
    Layer->Settings.bEnabled = !Name.IsEmpty();
    MarkDirty();
}

void UParametricRoadSession::SetLayerSpacing(double Metres)
{
    FParametricRoadLayerState* Layer = SelectedLayer();
    if (IsBuilding() || !Layer || !FMath::IsFinite(Metres)) { return; }
    const double Value = FMath::Clamp(Metres, .1, 1000.0);
    if (!FMath::IsNearlyEqual(Value, Layer->Settings.SpacingMetres)) { Layer->Settings.SpacingMetres = Value; MarkDirty(); }
}
void UParametricRoadSession::SetLayerFirstPlacement(double Metres)
{
    FParametricRoadLayerState* Layer = SelectedLayer();
    if (IsBuilding() || !Layer || !FMath::IsFinite(Metres)) { return; }
    const double Value = FMath::Clamp(Metres, 0.0, 1000.0);
    if (!FMath::IsNearlyEqual(Value, Layer->Settings.FirstPlacementMetres)) { Layer->Settings.FirstPlacementMetres = Value; MarkDirty(); }
}
void UParametricRoadSession::SetLayerOffset(int32 Axis, double Metres)
{
    FParametricRoadLayerState* Layer = SelectedLayer();
    if (IsBuilding() || !Layer || Axis < 0 || Axis > 2 || !FMath::IsFinite(Metres)) { return; }
    const double Value = FMath::Clamp(Metres, -1000.0, 1000.0);
    if (Layer->Settings.OffsetMetres[Axis] != Value) { Layer->Settings.OffsetMetres[Axis] = Value; MarkDirty(); }
}
void UParametricRoadSession::SetLayerEnabled(bool bEnabled)
{
    FParametricRoadLayerState* Layer = SelectedLayer();
    if (!IsBuilding() && Layer && Layer->Settings.bEnabled != bEnabled)
    {
        if (bEnabled && Layer->bRejected) { ClearLayerTemplate(SelectedLayerIndex); }
        if (!bEnabled && LoadingLayerIndex == SelectedLayerIndex) { CancelLayerLoad(); }
        Layer->Settings.bEnabled = bEnabled; MarkDirty();
    }
}
void UParametricRoadSession::SetLayerFollowGrade(bool bFollow)
{
    FParametricRoadLayerState* Layer = SelectedLayer();
    if (!IsBuilding() && Layer && Layer->Settings.bFollowGrade != bFollow) { Layer->Settings.bFollowGrade = bFollow; MarkDirty(); }
}
void UParametricRoadSession::SetLayerFollowBank(bool bFollow)
{
    FParametricRoadLayerState* Layer = SelectedLayer();
    if (!IsBuilding() && Layer && Layer->Settings.bFollowBank != bFollow) { Layer->Settings.bFollowBank = bFollow; MarkDirty(); }
}
FText UParametricRoadSession::GetLayerStatus() const { return GetLayerStatus(SelectedLayerIndex); }
FText UParametricRoadSession::GetLayerStatus(int32 Index) const
{
    if (!Layers.IsValidIndex(Index)) { return NSLOCTEXT("ParametricRoads", "NoLayer", "Add a decoration layer to configure it."); }
    const FParametricRoadLayerState& Layer = Layers[Index];
    if (!Layer.Settings.bEnabled) { return NSLOCTEXT("ParametricRoads", "LayerDisabled", "Decoration layer disabled."); }
    if (Layer.Settings.BlueprintName.IsEmpty()) { return NSLOCTEXT("ParametricRoads", "LayerChoose", "Choose a decoration blueprint."); }
    if (!Layer.Status.IsEmpty()) { return Layer.Status; }
    return FText::Format(NSLOCTEXT("ParametricRoads", "LayerStationSummary", "{0} placements along the 3D centreline"),
        FText::AsNumber(Layer.PreviewPoses.Num()));
}

int32 UParametricRoadSession::FindLayerNeedingTemplate() const
{
    for (int32 Index = 0; Index < Layers.Num(); ++Index)
    {
        const FParametricRoadLayerState& Layer = Layers[Index];
        if (Layer.Settings.bEnabled && !Layer.Settings.BlueprintName.IsEmpty() && !Layer.bRejected
            && (!Layer.Preview || Layer.LoadedName != Layer.Settings.BlueprintName)) { return Index; }
    }
    return INDEX_NONE;
}

void UParametricRoadSession::EnsureLayerTemplate()
{
    // The native blueprint world is shared. No template transaction may overlap
    // a validator or an outstanding construction request, even for equal names.
    if (IsBuilding() || PendingHologram || bAwaitingAck) { return; }
    if (LayerLoadingHologram && (!Layers.IsValidIndex(LoadingLayerIndex)
        || !Layers[LoadingLayerIndex].Settings.bEnabled)) { CancelLayerLoad(); }
    if (!LayerLoadingHologram)
    {
        LoadingLayerIndex = FindLayerNeedingTemplate();
        if (LoadingLayerIndex == INDEX_NONE) { return; }
        FParametricRoadLayerState& Layer = Layers[LoadingLayerIndex];
        AFGBlueprintSubsystem* Subsystem = AFGBlueprintSubsystem::GetBlueprintSubsystem(this);
        Layer.Descriptor = Subsystem ? Subsystem->GetBlueprintDescriptorByNameString(Layer.Settings.BlueprintName) : nullptr;
        if (!Layer.Descriptor)
        {
            Layer.Status = NSLOCTEXT("ParametricRoads", "LayerMissing", "Decoration blueprint is unavailable. Select another blueprint or disable the layer.");
            Layer.bRejected = true; LoadingLayerIndex = INDEX_NONE; MarkDirty(); return;
        }
        if (IsValid(ValidationHologram)) { ValidationHologram->Destroy(); ValidationHologram = nullptr; }
        bValidationPoseReady = false;
        LayerLoadingHologram = SpawnNativePreview(ToBlueprintPose(RoadPoseAt(StartCenter, true)), Layer.Descriptor);
        if (!LayerLoadingHologram)
        {
            Layer.Status = NSLOCTEXT("ParametricRoads", "LayerLoadFailed", "Decoration blueprint could not be loaded.");
            Layer.bRejected = true; LoadingLayerIndex = INDEX_NONE; MarkDirty(); return;
        }
        LayerLoadingHologram->SetActorHiddenInGame(true);
        LayerLoadSeconds = 0;
        Layer.Status = NSLOCTEXT("ParametricRoads", "LayerLoading", "Loading decoration blueprint…");
        return;
    }
    if (LayerLoadingHologram->mBuildableToNewRoot.IsEmpty()) { return; }
    FParametricRoadLayerState& Layer = Layers[LoadingLayerIndex];
    FBox Bounds;
    FText Reason;
    if (!ParametricRoadEligibility::Inspect(LayerLoadingHologram, Bounds, Layer.MemberCount, Reason))
    {
        Layer.Status = Reason; Layer.bRejected = true;
    }
    else
    {
        Layer.Preview = NewObject<UParametricRoadPreview>(this);
        if (!Layer.Preview->Initialize(LayerLoadingHologram))
        {
            Layer.Preview->Clear(); Layer.Preview = nullptr;
            Layer.Status = NSLOCTEXT("ParametricRoads", "LayerNoVisuals", "Decoration blueprint has no usable preview meshes.");
            Layer.bRejected = true;
        }
        else
        {
            Layer.Cost = LayerLoadingHologram->GetCost(true);
            Layer.LoadedName = Layer.Settings.BlueprintName;
            Layer.Status = FText::GetEmpty();
        }
    }
    // Snapshot all references/costs before a different source can load next tick.
    CancelLayerLoad();
    MarkDirty();
}

int32 UParametricRoadSession::GetBuildJobCount() const { return PreviewPoses.Num() + GetTotalLayerPlacementCount(); }
int32 UParametricRoadSession::GetBuildJobLayer(int32 Index, int32* OutStation) const
{
    if (OutStation) { *OutStation = INDEX_NONE; }
    int32 Station = Index - PreviewPoses.Num();
    if (Station < 0) { return INDEX_NONE; }
    for (int32 LayerIndex = 0; LayerIndex < Layers.Num(); ++LayerIndex)
    {
        const FParametricRoadLayerState& Layer = Layers[LayerIndex];
        if (!Layer.Settings.bEnabled) { continue; }
        if (Station < Layer.PreviewPoses.Num())
        { if (OutStation) { *OutStation = Station; } return LayerIndex; }
        Station -= Layer.PreviewPoses.Num();
    }
    return INDEX_NONE;
}
bool UParametricRoadSession::IsLayerJob(int32 Index) const { return GetBuildJobLayer(Index) != INDEX_NONE; }
FTransform UParametricRoadSession::GetBuildJobPose(int32 Index) const
{
    int32 Station;
    const int32 Layer = GetBuildJobLayer(Index, &Station);
    return Layer != INDEX_NONE ? Layers[Layer].PreviewPoses[Station]
        : (PreviewPoses.IsValidIndex(Index) ? PreviewPoses[Index] : FTransform::Identity);
}
UFGBlueprintDescriptor* UParametricRoadSession::GetBuildJobDescriptor(int32 Index) const
{
    const int32 Layer = GetBuildJobLayer(Index);
    return Layer != INDEX_NONE ? Layers[Layer].Descriptor.Get()
        : (PreviewPoses.IsValidIndex(Index) && Source.IsValid() ? Source->mBlueprintDescriptor.Get() : nullptr);
}
FString UParametricRoadSession::GetBuildJobName(int32 Index) const
{
    const int32 Layer = GetBuildJobLayer(Index);
    return Layer != INDEX_NONE ? Layers[Layer].LoadedName : (PreviewPoses.IsValidIndex(Index) ? BlueprintName : FString());
}
int32 UParametricRoadSession::GetBuildJobMemberCount(int32 Index) const
{
    const int32 Layer = GetBuildJobLayer(Index);
    return Layer != INDEX_NONE ? Layers[Layer].MemberCount : (PreviewPoses.IsValidIndex(Index) ? MemberCount : 0);
}
void UParametricRoadSession::SetPreviewAppearance(uint8 State)
{
    if (VisualPreview) { VisualPreview->SetAppearance(Source.Get(), State); }
    for (FParametricRoadLayerState& Layer : Layers)
    { if (Layer.Preview) { Layer.Preview->SetAppearance(nullptr, State); } }
}

TMap<UClass*, int64> UParametricRoadSession::RequiredJobCost(int32 First, int32 Count) const
{
    TMap<UClass*, int64> Required;
    const int32 Start = FMath::Clamp(First, 0, GetBuildJobCount());
    const int32 End = int32(FMath::Min(int64(GetBuildJobCount()), int64(Start) + FMath::Max(0, Count)));
    for (int32 Index = Start; Index < End; ++Index)
    {
        const int32 Layer = GetBuildJobLayer(Index);
        for (const FItemAmount& Item : Layer != INDEX_NONE ? Layers[Layer].Cost : SliceCost)
        {
            if (!Item.ItemClass || Item.Amount < 0) { Required.FindOrAdd(nullptr) = MAX_int64; continue; }
            Required.FindOrAdd(Item.ItemClass) += Item.Amount;
        }
    }
    return Required;
}

void UParametricRoadSession::InvalidateChangedLayerSources()
{
    if (ChangedLayerSources.IsEmpty()) { return; }
    if (IsBuilding()) { StopConstruction(NSLOCTEXT("ParametricRoads", "LayerChangedDuringBuild", "Decoration blueprint changed. Remaining placements stopped; a sent request may still complete.")); }
    for (int32 Index = 0; Index < Layers.Num(); ++Index)
    { if (ChangedLayerSources.Contains(Layers[Index].Settings.BlueprintName)) { ClearLayerTemplate(Index); } }
    ChangedLayerSources.Reset();
    if (IsValid(ValidationHologram)) { ValidationHologram->Destroy(); ValidationHologram = nullptr; }
    MarkDirty();
}

bool UParametricRoadSession::IsBuildJobSourceCurrent(int32 Index) const
{
    AFGBlueprintSubsystem* Subsystem = AFGBlueprintSubsystem::GetBlueprintSubsystem(GetWorld());
    return Index >= 0 && Index < GetBuildJobCount() && !ChangedLayerSources.Contains(GetBuildJobName(Index)) && Subsystem
        && Subsystem->GetBlueprintDescriptorByNameString(GetBuildJobName(Index)) == GetBuildJobDescriptor(Index);
}
