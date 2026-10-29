#include "ParametricRoadSession.h"
#include "ParametricRoadNativeHud.h"
#include "ParametricRoadLayerInput.h"
#include "ParametricRoadLayerWidget.h"

#include "ParametricRoadBuildMode.h"
#include "ParametricRoadEligibility.h"
#include "ParametricRoadWidgets.h"
#include "ParametricRoadPreview.h"
#include "ParametricRoadDirection.h"
#include "ParametricRoadMonitor.h"
#include "RoadworksModule.h"
#include "Components/SceneComponent.h"
#include "HAL/PlatformTime.h"
#include "Equipment/FGBuildGunBuild.h"
#include "FGCentralStorageSubsystem.h"
#include "FGBlueprintSubsystem.h"
#include "FGCharacterPlayer.h"
#include "FGConstructDisqualifier.h"
#include "FGInventoryComponent.h"
#include "FGPlayerController.h"
#include "Hologram/FGBlueprintHologram.h"
#include "Resources/FGItemDescriptor.h"

namespace
{
    constexpr int32 PreviewMemberBudget = 32768;

    FText NativePlacementFailure(const AFGBlueprintHologram* Hologram, int32 ExpectedMembers)
    {
        if (!IsValid(Hologram) || Hologram->mBuildableToNewRoot.Num() != ExpectedMembers)
        {
            return NSLOCTEXT("Roadworks", "SliceStillLoading", "The blueprint for this slice is not fully loaded.");
        }
        TArray<TSubclassOf<UFGConstructDisqualifier>> Disqualifiers;
        Hologram->GetConstructDisqualifiers(Disqualifiers);
        for (const auto& Disqualifier : Disqualifiers)
        {
            if (Disqualifier && !UFGConstructDisqualifier::GetIsSoftDisqualifier(Disqualifier))
            {
                return UFGConstructDisqualifier::GetDisqualifyingText(Disqualifier);
            }
        }
        return NSLOCTEXT("Roadworks", "UnknownNativeRefusal", "The game cannot build this slice yet.");
    }

    FText CurveError(EParametricRoadFailure Failure)
    {
        switch (Failure)
        {
        case EParametricRoadFailure::DegenerateCurve:
            return NSLOCTEXT("Roadworks", "CuspError", "Curve too tight: move the end further away or adjust its angles.");
        case EParametricRoadFailure::RouteTooLong:
            return NSLOCTEXT("Roadworks", "LengthError", "The road exceeds 1 km. Build it in shorter sections.");
        case EParametricRoadFailure::GradeLimit:
            return NSLOCTEXT("Roadworks", "GradeError", "The curve exceeds the 45° grade limit.");
        case EParametricRoadFailure::BankLimit:
            return NSLOCTEXT("Roadworks", "BankError", "The curve exceeds the 60° bank limit.");
        case EParametricRoadFailure::SegmentLimit:
            return NSLOCTEXT("Roadworks", "SegmentsError", "Too many slices: shorten the road or soften its transitions.");
        default:
            return NSLOCTEXT("Roadworks", "ParameterError", "Check the deck dimensions and settings.");
        }
    }

    AFGPlayerController* Controller(UFGBuildGunStateBuild* State)
    {
        AFGCharacterPlayer* Player = State && State->GetBuildGun() ? State->GetBuildGun()->GetInstigatorCharacter() : nullptr;
        return Player ? Cast<AFGPlayerController>(Player->GetController()) : nullptr;
    }
}

UWorld* UParametricRoadSession::GetWorld() const
{
    return GetOuter() ? GetOuter()->GetWorld() : nullptr;
}

void UParametricRoadSession::Initialize(UFGBuildGunStateBuild* State, AFGBlueprintHologram* Hologram)
{
    BuildState = State;
    Source = Hologram;
    if (auto* Subsystem = AFGBlueprintSubsystem::GetBlueprintSubsystem(this))
    { Subsystem->OnBlueprintDescriptorUpdated.AddUniqueDynamic(this, &ThisClass::OnNativeBlueprintUpdated); }
    if (Hologram)
    {
        bOriginalSourceNeedsFloor = Hologram->GetNeedsValidFloor();
        Hologram->SetNeedsValidFloor(false);
        bSourceFloorOverridden = true;
    }
    Status = NSLOCTEXT("Roadworks", "Loading", "Loading blueprint…");
    if (AFGPlayerController* PC = Controller(State))
    {
        StatusWidget = CreateWidget<UParametricRoadStatusWidget>(PC);
        StatusWidget->SetSession(this);
        StatusWidget->SetVisibility(ESlateVisibility::HitTestInvisible);
        Monitor = NewObject<UParametricRoadMonitor>(this);
        if (!Monitor->Initialize(this, State->GetBuildGun(), StatusWidget))
        {
            UE_LOG(LogRoadworks, Warning, TEXT("Road monitor unavailable: %s. Using the local HUD readout."), *Monitor->GetUnavailableReason().ToString());
            Monitor->Clear(); Monitor = nullptr;
            StatusWidget->AddToPlayerScreen(20);
            StatusWidget->SetPositionInViewport(FVector2D(30, 150));
            StatusWidget->SetDesiredSizeInViewport(FVector2D(512, 344));
        }
    }
    BindControls();
    AssessBlueprint();
}

bool UParametricRoadSession::IsActive() const
{
    UFGBuildGunStateBuild* State = BuildState.Get();
    AFGBuildGun* Gun = State ? State->GetBuildGun() : nullptr;
    AFGCharacterPlayer* Player = Gun ? Gun->GetInstigatorCharacter() : nullptr;
    return !bShuttingDown && Player && Player->IsLocallyControlled() && Source.IsValid()
        && Gun->GetCurrentState() == State && State->GetHologram() == Source.Get()
        && Gun->GetCurrentBuildGunMode() == UParametricRoadBuildMode::StaticClass();
}

bool UParametricRoadSession::AssessBlueprint()
{
    AFGBlueprintHologram* Hologram = Source.Get();
    FBox Bounds;
    FText Reason;
    if (!ParametricRoadEligibility::Inspect(Hologram, Bounds, MemberCount, Reason))
    {
        Status = Reason;
        bBlueprintRejected = Hologram && Hologram->mBuildableToNewRoot.Num() > 0;
        return false;
    }
    if (!Hologram->mBlueprintDescriptor)
    {
        return false;
    }
    const FVector Size = Bounds.GetSize();
    LocalCenter = Bounds.GetCenter();
    BaseCenterZ = LocalCenter.Z;
    bForwardY = Size.Y < Size.X;
    Parameters.SliceLength = FMath::Clamp(bForwardY ? Size.Y : Size.X, 1.0, 100000.0);
    Parameters.SliceWidth = FMath::Clamp(bForwardY ? Size.X : Size.Y, 1.0, 100000.0);
    Parameters.SliceHeight = FMath::Clamp(Size.Z, 0.1, 100000.0);
    Parameters.MinimumOverlap = FMath::Min(20.0, Parameters.SliceLength * 0.1);
    if (!InitializeRoadOrientation(Hologram->GetActorTransform()))
    {
        Status = NSLOCTEXT("Roadworks", "InvalidBlueprintPose", "The blueprint needs a rigid placement transform.");
        bBlueprintRejected = true;
        return false;
    }
    BlueprintName = Hologram->mBlueprintDescName.IsEmpty()
        ? Hologram->mBlueprintDescriptor->GetBlueprintNameAsString() : Hologram->mBlueprintDescName;
    SliceCost = Hologram->GetCost(true);
    VisualPreview = NewObject<UParametricRoadPreview>(this);
    if (!VisualPreview->Initialize(Hologram))
    {
        VisualPreview->Clear(); VisualPreview = nullptr;
        Status = NSLOCTEXT("Roadworks", "NoVisuals", "The blueprint has no usable preview meshes.");
        bBlueprintRejected = true;
        return false;
    }
    DirectionMarkers = NewObject<UParametricRoadDirection>(this);
    if (!DirectionMarkers->Initialize(Hologram)) { DirectionMarkers = nullptr; }

    bAssessed = true;
    MarkDirty();
    return true;
}

bool UParametricRoadSession::InitializeRoadOrientation(const FTransform& BlueprintPose)
{
    FTransform RoadFrame;
    if (!UParametricRoadGeometry::TryMakeRoadFrame(BlueprintPose, LocalCenter, bForwardY, RoadFrame)) { return false; }
    const FRotator Angles = RoadFrame.Rotator();
    StartHeading = EndHeading = Angles.Yaw;
    StartGrade = EndGrade = Angles.Pitch;
    StartBank = EndBank = Angles.Roll;
    return true;
}

FTransform UParametricRoadSession::RoadPoseAt(const FVector& Center, bool bStart) const
{
    return FTransform(FRotator(bStart ? StartGrade : EndGrade, bStart ? StartHeading : EndHeading,
        bStart ? StartBank : EndBank), Center + FVector(0, 0, bStart ? StartHeight : EndHeight));
}

FParametricRoadRequest UParametricRoadSession::ControlPointRequest() const
{
    FParametricRoadRequest Request = Parameters;
    Request.StartPose = RoadPoseAt(Phase == EParametricRoadPhase::Start ? CursorCenter : StartCenter, true);
    Request.EndPose = RoadPoseAt(Phase == EParametricRoadPhase::Drawing ? CursorCenter : EndCenter, false);
    return Request;
}

TArray<FParametricRoadControlPoint> UParametricRoadSession::ControlPoints() const
{
    TArray<FParametricRoadControlPoint> Points;
    Points.Reserve(IntermediatePoints.Num());
    for (const auto& Waypoint : IntermediatePoints) { Points.Add(Waypoint.Point); }
    return Points;
}

bool UParametricRoadSession::HasPendingExtension() const
{
    const FVector Center = Phase == EParametricRoadPhase::Drawing ? CursorCenter : EndCenter;
    return bPendingExtension && !IntermediatePoints.IsEmpty() && Center.Equals(ExtensionOrigin, 1.0);
}

int32 UParametricRoadSession::GetSelectedPointIndex() const
{
    return IntermediatePoints.IsValidIndex(SelectedIntermediatePoint) ? SelectedIntermediatePoint + 1
        : (bEditStart ? 0 : GetPointCount() - 1);
}

FText UParametricRoadSession::GetPointLabel(int32 Index) const
{
    if (Index <= 0) { return NSLOCTEXT("Roadworks", "StartColumn", "START"); }
    if (Index >= GetPointCount() - 1) { return NSLOCTEXT("Roadworks", "EndColumn", "END"); }
    return FText::Format(NSLOCTEXT("Roadworks", "PointLabel", "P{0}"), FText::AsNumber(Index));
}

void UParametricRoadSession::SelectPoint(int32 Index)
{
    if (!bAssessed || IsBuilding() || IsLayerConfigurationOpen() || bPresetSwitchPending) { return; }
    Index = FMath::Clamp(Index, 0, GetPointCount() - 1);
    SelectPointInternal(Index);
}

void UParametricRoadSession::SelectPointInternal(int32 Index)
{
    SelectedIntermediatePoint = Index > 0 && Index < GetPointCount() - 1 ? Index - 1 : INDEX_NONE;
    bEditStart = Index == 0;
}

bool UParametricRoadSession::CanEditFromWidget() const
{
    return bAssessed && !bShuttingDown && !IsBuilding() && bLayerConfigurationOpen
        && IsValid(LayerWidget) && IsValid(LayerInput) && LayerInput->mInteractObject == this
        && IsLayerInputOwner(LayerInput) && IsActive();
}

void UParametricRoadSession::SelectPointFromWidget(int32 Index)
{
    if (CanEditFromWidget() && Index >= 0 && Index < GetPointCount()) { SelectPointInternal(Index); }
}

void UParametricRoadSession::SelectAdjustmentFromWidget(int32 Index)
{
    if (CanEditFromWidget() && Index >= 0 && Index < int32(EParametricRoadAdjustment::Count))
    { Adjustment = EParametricRoadAdjustment(Index); }
}

void UParametricRoadSession::SetFineAdjustmentFromWidget(bool bFine)
{
    if (CanEditFromWidget()) { bFineAdjustment = bFine; }
}

void UParametricRoadSession::CyclePoint(int32 Delta)
{
    const int32 Count = GetPointCount();
    SelectPoint((GetSelectedPointIndex() + Delta % Count + Count) % Count);
}

FTransform UParametricRoadSession::GetPointPose(int32 Index) const
{
    const FParametricRoadRequest Request = ControlPointRequest();
    if (Index <= 0) { return Request.StartPose; }
    if (Index >= GetPointCount() - 1) { return Request.EndPose; }
    const auto& Point = IntermediatePoints[Index - 1].Point;
    FTransform Pose = Point.Pose;
    // Immediately after adding the last point, the live end coincides with it.
    // The preview prefix uses its incoming tangent until the end moves away.
    if (Index == GetPointCount() - 2 && HasPendingExtension())
    {
        const FVector Previous = Index == 1 ? Request.StartPose.GetLocation() : IntermediatePoints[Index - 2].Point.Pose.GetLocation();
        const FVector Direction = Pose.GetLocation() - Previous;
        if (!Direction.IsNearlyZero())
        {
            FRotator Angles = Pose.Rotator();
            if (!Point.bManualHeading) { Angles.Yaw = Direction.Rotation().Yaw; }
            if (!Point.bManualGrade) { Angles.Pitch = Direction.Rotation().Pitch; }
            Pose.SetRotation(Angles.Quaternion());
        }
        return Pose;
    }
    TArray<FTransform> Resolved;
    if (UParametricRoadGeometry::ResolveControlPointPoses(Request, ControlPoints(), Resolved) == EParametricRoadFailure::None)
    { return Resolved[Index]; }
    return Pose;
}

bool UParametricRoadSession::IsSelectedPointAutomatic(int32 Index) const
{
    if (!IntermediatePoints.IsValidIndex(SelectedIntermediatePoint)) { return false; }
    const auto& Point = IntermediatePoints[SelectedIntermediatePoint].Point;
    return (Index == 0 && !Point.bManualHeading) || (Index == 1 && !Point.bManualGrade);
}

FTransform UParametricRoadSession::ToBlueprintPose(const FTransform& RoadPose) const
{
    FTransform Pose;
    ensure(UParametricRoadGeometry::TryMakeBlueprintPose(RoadPose, LocalCenter, bForwardY, Pose));
    return Pose;
}

void UParametricRoadSession::UpdateNativePlacement(const FHitResult&, bool bValid)
{
    if (!bAssessed || !Source.IsValid() || IsLayerConfigurationOpen()) { return; }
    // Inspecting a fixed point must not move the provisional end with the aim.
    if (Phase == EParametricRoadPhase::Drawing && GetSelectedPointIndex() != GetPointCount() - 1)
    { SetSourcePreviewVisible(false); return; }
    if (!bValid)
    {
        if (bCursorValid && (Phase == EParametricRoadPhase::Start || Phase == EParametricRoadPhase::Drawing)) { MarkDirty(); }
        bCursorValid = false;
        // An invalid trace has no new native placement transform. Re-reading
        // our previous offset transform would apply the height a second time.
        return;
    }
    AFGBlueprintHologram* Hologram = Source.Get();
    const FVector NewCenter = Hologram->GetActorTransform().TransformPosition(LocalCenter);
    if (Phase == EParametricRoadPhase::Drawing && bPendingExtension && !NewCenter.Equals(ExtensionOrigin, 1.0))
    { bPendingExtension = false; }
    if (!NewCenter.Equals(CursorCenter, 0.5) || bValid != bCursorValid)
    {
        CursorCenter = NewCenter;
        bCursorValid = bValid;
        if (Phase == EParametricRoadPhase::Start || Phase == EParametricRoadPhase::Drawing) { MarkDirty(); }
    }
    if (Phase == EParametricRoadPhase::Start)
    {
        SetSourcePreviewVisible(true);
        if (bValid) { Hologram->SetActorHiddenInGame(false); }
        Hologram->SetActorTransform(ToBlueprintPose(RoadPoseAt(CursorCenter, true)));
        Hologram->UpdateRotationValuesFromTransform();
    }
    else
    {
        SetSourcePreviewVisible(false);
    }
}

void UParametricRoadSession::SetSourcePreviewVisible(bool bVisible)
{
    if (bVisible)
    {
        for (const auto& Entry : SourceComponentVisibility)
        {
            if (USceneComponent* Component = Entry.Key.Get()) { Component->SetVisibility(Entry.Value, false); }
        }
        SourceComponentVisibility.Reset();
        return;
    }
    // Native validation interprets a hidden actor as InvalidAimLocation. Hide
    // only its visuals, preserving the gun's genuine hit/placement state and
    // each component's original visibility for cancellation or mode changes.
    if (!SourceComponentVisibility.IsEmpty()) { return; }
    TArray<AFGHologram*> Holograms;
    if (Source.IsValid()) { Holograms.Add(Source.Get()); }
    for (int32 I = 0; I < Holograms.Num() && I < 1024; ++I)
    {
        AFGHologram* Hologram = Holograms[I];
        if (!IsValid(Hologram)) { continue; }
        TInlineComponentArray<USceneComponent*> Components(Hologram);
        for (USceneComponent* Component : Components)
        {
            if (!IsValid(Component)) { continue; }
            const TWeakObjectPtr<USceneComponent> Key(Component);
            if (!SourceComponentVisibility.Contains(Key))
            {
                SourceComponentVisibility.Add(Key, Component->IsVisible());
            }
            Component->SetVisibility(false, false);
        }
        for (AFGHologram* Child : Hologram->GetHologramChildren()) { Holograms.AddUnique(Child); }
    }
}

void UParametricRoadSession::MarkDirty()
{
    bDirty = true;
    bPreviewsValid = false;
}

void UParametricRoadSession::RefreshInvalidCursor()
{
    UFGBuildGunStateBuild* State = BuildState.Get();
    AFGBlueprintHologram* Hologram = Source.Get();
    AFGBuildGun* Gun = State ? State->GetBuildGun() : nullptr;
    if (Gun && Hologram)
    {
        // OnInvalidHitResult is too short for a native detour in CL 513885;
        // hooking it aborts SML initialization. Read the existing trace instead.
        // Valid placement still owns the center through PostHologramPlacement.
        const FHitResult& Hit = Gun->GetHitResult();
        if (!Hologram->IsValidHitResult(Hit)) { UpdateNativePlacement(Hit, false); }
    }
}

void UParametricRoadSession::Solve()
{
    bDirty = false;
    ++SolutionRevision;
    ValidatedCount = 0;
    bPreviewsValid = bValidationPoseReady = bValidationFailed = false;
    ValidationFailure = FText::GetEmpty();
    bLayerSolutionValid = false;
    for (FParametricRoadLayerState& Layer : Layers) { Layer.bSolutionValid = !Layer.Settings.bEnabled; Layer.VisualRevision = 0; }
    if (Phase == EParametricRoadPhase::Start)
    {
        Solution = FParametricRoadSolution();
        EvaluatedPath = FParametricRoadPath();
        ClearPreviews();
        if (NoticeSeconds <= 0)
        {
            Status = bCursorValid ? NSLOCTEXT("Roadworks", "ChooseStart", "Set the start of the road.")
                : NSLOCTEXT("Roadworks", "AimAtSurface", "Aim at a surface to set the start.");
        }
        return;
    }
    Parameters.StartPose = RoadPoseAt(StartCenter, true);
    Parameters.EndPose = RoadPoseAt(Phase == EParametricRoadPhase::Drawing ? CursorCenter : EndCenter, false);
    TArray<FParametricRoadControlPoint> Points = ControlPoints();
    // Immediately after adding a point the cursor still occupies that point.
    // Show the fixed prefix until the live extension has nonzero length.
    if (!Points.IsEmpty() && (HasPendingExtension()
        || FVector::Distance(Points.Last().Pose.GetLocation(), Parameters.EndPose.GetLocation()) < 1.0))
    {
        // The last fixed point is temporarily the end of the preview prefix.
        // Automatic components follow the only available, incoming chord.
        const FParametricRoadControlPoint Last = Points.Pop();
        Parameters.EndPose = Last.Pose;
        const FVector Previous = Points.IsEmpty() ? Parameters.StartPose.GetLocation() : Points.Last().Pose.GetLocation();
        FRotator Angles = (Last.Pose.GetLocation() - Previous).Rotation();
        const FRotator Stored = Last.Pose.Rotator();
        if (Last.bManualHeading) { Angles.Yaw = Stored.Yaw; }
        if (Last.bManualGrade) { Angles.Pitch = Stored.Pitch; }
        Angles.Roll = Stored.Roll;
        Parameters.EndPose.SetRotation(Angles.Quaternion());
    }
    for (FParametricRoadLayerState& Layer : Layers) { Layer.PreviewPoses.Reset(); }
    EvaluatedPath = FParametricRoadPath();
    Solution = UParametricRoadGeometry::GenerateRouteWithPath(Parameters, Points, EvaluatedPath);
    if (Solution.Failure != EParametricRoadFailure::None)
    {
        ClearPreviews();
        Status = CurveError(Solution.Failure);
        return;
    }
    if (int64(Solution.SegmentPoses.Num()) * MemberCount > PreviewMemberBudget)
    {
        Solution.SegmentPoses.Reset();
        ClearPreviews();
        Status = NSLOCTEXT("Roadworks", "PreviewBudget", "Preview too large: shorten the road or simplify the blueprint slice.");
        return;
    }
    int32 RemainingMembers = PreviewMemberBudget - Solution.SegmentPoses.Num() * MemberCount;
    for (int32 Index = 0; Index < Layers.Num(); ++Index)
    {
        FParametricRoadLayerState& Layer = Layers[Index];
        if (!Layer.Settings.bEnabled) { continue; }
        if (!Layer.Preview || Layer.LoadedName != Layer.Settings.BlueprintName || Layer.bRejected || Layer.MemberCount <= 0)
        {
            ClearPreviews();
            Status = FText::Format(NSLOCTEXT("Roadworks", "LayerNotReady", "Layer {0}: {1}"),
                FText::AsNumber(Index + 1), GetLayerStatus(Index));
            return;
        }
        const int32 Maximum = FMath::Min(512, RemainingMembers / Layer.MemberCount);
        TArray<FTransform> Frames;
        const auto Failure = UParametricRoadGeometry::DistributeLayer(EvaluatedPath,
            Layer.Settings.SpacingMetres * 100, Layer.Settings.FirstPlacementMetres * 100,
            Layer.Settings.bFollowGrade, Layer.Settings.bFollowBank, Maximum, Frames, Layer.Settings.OffsetMetres * 100);
        if (Failure != EParametricRoadFailure::None)
        {
            Layer.Status = NSLOCTEXT("Roadworks", "LayerBudget", "Too many decoration placements. Increase spacing or shorten the road.");
            ClearPreviews();
            Status = FText::Format(NSLOCTEXT("Roadworks", "LayerNotReady", "Layer {0}: {1}"),
                FText::AsNumber(Index + 1), Layer.Status);
            return;
        }
        for (const FTransform& Frame : Frames) { Layer.PreviewPoses.Add(ToBlueprintPose(Frame)); }
        RemainingMembers -= Frames.Num() * Layer.MemberCount;
        Layer.Status = FText::GetEmpty();
        Layer.bSolutionValid = true;
    }
    bLayerSolutionValid = true;
    PreviewRevision = 0;
    ValidationDelay = 0.1;
    Status = NSLOCTEXT("Roadworks", "PreparingPreview", "Updating preview…");
}

AFGBlueprintHologram* UParametricRoadSession::SpawnNativePreview(const FTransform& Pose, UFGBlueprintDescriptor* Descriptor)
{
    AFGBlueprintHologram* Hologram = Source.Get();
    UFGBuildGunStateBuild* State = BuildState.Get();
    if (!Hologram || !State || !Hologram->mBlueprintDescriptor) { return nullptr; }
    UFGBlueprintDescriptor* SelectedDescriptor = Descriptor ? Descriptor : Hologram->mBlueprintDescriptor.Get();
    TGuardValue<bool> LoadingOwnBlueprint(bLoadingNativePreview, true);
    AFGHologram* Spawned = AFGHologram::SpawnHologramFromRecipe(Hologram->GetRecipe(), State->GetBuildGun(),
        Pose.GetLocation(), Hologram->GetConstructionInstigator(), [SelectedDescriptor](AFGHologram* Actor)
        {
            Actor->SetFlags(RF_Transient | RF_TextExportTransient);
            Actor->SetReplicates(false);
            // BeginPlay performs the actual native blueprint load. The setter
            // only assigns metadata, so choose the source BEFORE finish-spawn.
            if (auto* Blueprint = Cast<AFGBlueprintHologram>(Actor)) { Blueprint->SetBlueprintDescriptor(SelectedDescriptor); }
        });
    AFGBlueprintHologram* Preview = Cast<AFGBlueprintHologram>(Spawned);
    if (!Preview || Preview->mBlueprintDescriptor != SelectedDescriptor
        || Preview->mBlueprintDescName != SelectedDescriptor->GetBlueprintNameAsString())
    {
        if (Spawned) { Spawned->Destroy(); }
        return nullptr;
    }
    Preview->SetActorTickEnabled(false);
    Preview->SetBuildModeOverride(Preview->GetDefaultBuildGunMode());
    // A road slice follows the solved 3D frame, not the terrain underneath it.
    // Keep native clearance/cost checks; only the ground-support test differs.
    Preview->SetNeedsValidFloor(false);
    Preview->SetActorTransform(Pose);
    Preview->UpdateRotationValuesFromTransform();
    Preview->SetDisabled(false);
    Preview->SetActorHiddenInGame(false);
    return Preview;
}

void UParametricRoadSession::UpdatePreviews()
{
    if (Solution.SegmentPoses.IsEmpty() || !BuildState.IsValid() || !VisualPreview) { return; }
    if (!bLayerSolutionValid || LayerLoadingHologram || FindLayerNeedingTemplate() != INDEX_NONE) { return; }
    if (PreviewRevision != SolutionRevision)
    {
        PreviewPoses.Reset(Solution.SegmentPoses.Num());
        for (const FTransform& Pose : Solution.SegmentPoses) { PreviewPoses.Add(ToBlueprintPose(Pose)); }
        PreviewRevision = SolutionRevision;
    }
    const int32 Desired = GetBuildJobCount();
    VisualPreview->Update(PreviewPoses);
    // Share the previous one-layer work window across all decorations. Each
    // native component operation remains indivisible, as in the existing preview.
    const double LayerDeadline = FPlatformTime::Seconds() + .002;
    for (int32 Visited = 0; Visited < Layers.Num(); ++Visited)
    {
        LayerPreviewCursor %= Layers.Num();
        FParametricRoadLayerState& Layer = Layers[LayerPreviewCursor];
        LayerPreviewCursor = (LayerPreviewCursor + 1) % Layers.Num();
        if (Layer.Preview)
        {
            Layer.Preview->Update(Layer.Settings.bEnabled ? Layer.PreviewPoses : TArray<FTransform>());
            Layer.VisualRevision = SolutionRevision;
        }
        if (FPlatformTime::Seconds() >= LayerDeadline) { break; }
    }
    bool bLayersReady = true;
    for (const FParametricRoadLayerState& Layer : Layers)
    { if (Layer.Settings.bEnabled && !Layer.PreviewPoses.IsEmpty()
        && (!Layer.Preview || Layer.VisualRevision != SolutionRevision || !Layer.Preview->IsReady())) { bLayersReady = false; } }
    if (!VisualPreview->IsReady() || !bLayersReady)
    { Status = NSLOCTEXT("Roadworks", "PreparingPreview", "Updating preview…"); return; }
    if (Phase == EParametricRoadPhase::Drawing)
    {
        SetPreviewAppearance(uint8(EHologramMaterialState::HMS_WARNING));
        if (NoticeSeconds <= 0) { Status = NSLOCTEXT("Roadworks", "LivePreview", "Set the end to check placement."); }
        return;
    }
    if (bValidationFailed || bPreviewsValid || ValidatedCount >= Desired) { return; }
    SetPreviewAppearance(uint8(EHologramMaterialState::HMS_WARNING));
    if (!IsBuildJobSourceCurrent(ValidatedCount))
    {
        bValidationFailed = true;
        Status = NSLOCTEXT("Roadworks", "ValidationSourceChanged", "Blueprint changed or is unavailable. Reopen configuration to refresh the layer.");
        SetPreviewAppearance(uint8(EHologramMaterialState::HMS_ERROR));
        return;
    }
    // One native validator, re-created only at a source boundary. Cached visual
    // templates and the immutable curve need no blueprint-world reload per pose.
    const bool bNextLayer = IsLayerJob(ValidatedCount);
    if (IsValid(ValidationHologram) && (bValidationLayer != bNextLayer || ValidationBlueprintName != GetBuildJobName(ValidatedCount)))
    { ValidationHologram->Destroy(); ValidationHologram = nullptr; bValidationPoseReady = false; }
    if (!IsValid(ValidationHologram))
    {
        ValidationHologram = SpawnNativePreview(GetBuildJobPose(ValidatedCount), GetBuildJobDescriptor(ValidatedCount));
        if (!ValidationHologram) { return; }
        bValidationLayer = bNextLayer;
        ValidationBlueprintName = GetBuildJobName(ValidatedCount);
        ValidationHologram->SetActorHiddenInGame(false);
        TInlineComponentArray<USceneComponent*> Components(ValidationHologram);
        for (USceneComponent* Component : Components) { Component->SetVisibility(false, false); }
        ValidationDelay = 0.1;
        return;
    }
    if (ValidationDelay > 0) { return; }
    if (!bValidationPoseReady)
    {
        ValidationHologram->SetActorTransform(GetBuildJobPose(ValidatedCount));
        ValidationHologram->UpdateRotationValuesFromTransform();
        bValidationPoseReady = true;
        Status = FText::Format(NSLOCTEXT("Roadworks", "CheckingRoute", "Checking placement {0} / {1}…"),
            FText::AsNumber(ValidatedCount + 1), FText::AsNumber(Desired));
        return;
    }
    ValidationHologram->ResetConstructDisqualifiers();
    ValidationHologram->ValidatePlacementAndCost(BuildState->GetBuildGun()->GetInventory());
    bValidationPoseReady = false;
    const int32 ExpectedMembers = GetBuildJobMemberCount(ValidatedCount);
    if (ValidationHologram->mBuildableToNewRoot.Num() != ExpectedMembers || !ValidationHologram->CanConstruct())
    {
        bValidationFailed = true;
        ValidationFailure = FText::Format(NSLOCTEXT("Roadworks", "NativePlacementRefused", "{0}, placement {1} refused: {2}"),
            FText::FromString(GetBuildJobName(ValidatedCount)), FText::AsNumber(ValidatedCount + 1),
            NativePlacementFailure(ValidationHologram, ExpectedMembers));
        Status = ValidationFailure;
        SetPreviewAppearance(uint8(EHologramMaterialState::HMS_ERROR));
        return;
    }
    ++ValidatedCount;
    if (ValidatedCount == Desired)
    {
        bPreviewsValid = true;
        const bool bAffordable = CanAffordJobs(0, Desired);
        SetPreviewAppearance(uint8(bAffordable ? EHologramMaterialState::HMS_OK : EHologramMaterialState::HMS_ERROR));
        Status = bAffordable ? NSLOCTEXT("Roadworks", "ConfirmRoute", "End set. Confirm to build the road.")
            : NSLOCTEXT("Roadworks", "CannotAffordRoute", "Not enough materials for the entire road.");
    }
}

bool UParametricRoadSession::CanAfford(int32 Count) const
{
    // Existing road callers pass the road count; include every enabled layer.
    return CanAffordJobs(0, Count == Solution.SegmentPoses.Num() ? GetBuildJobCount() : Count);
}

bool UParametricRoadSession::CanAffordJobs(int32 First, int32 Count) const
{
    UFGInventoryComponent* Inventory = BuildState.IsValid() ? BuildState->GetBuildGun()->GetInventory() : nullptr;
    if (!Inventory) { return false; }
    if (Inventory->GetNoBuildCost()) { return true; }
    AFGCentralStorageSubsystem* Depot = AFGCentralStorageSubsystem::Get(GetWorld());
    for (const auto& Item : RequiredJobCost(First, Count))
    {
        if (!Item.Key || Item.Value < 0 || Item.Value > MAX_int32) { return false; }
        const int64 Available = int64(Inventory->GetNumItems(Item.Key))
            + (Depot ? Depot->GetNumItemsFromCentralStorage(Item.Key) : 0);
        if (Item.Value > Available) { return false; }
    }
    return true;
}

void UParametricRoadSession::Tick(float DeltaSeconds)
{
    BindControls();
    if (InputController.IsValid() && InputController->IsPauseMenuOpen() && IsLayerConfigurationOpen()) { CloseLayerConfiguration(); }
    UpdateHudVisibility();
    if (Monitor)
    {
        Monitor->Tick(DeltaSeconds);
        if (!Monitor->IsValidMonitor())
        {
            CloseLayerConfiguration();
            Monitor = nullptr;
            if (StatusWidget && IsActive())
            {
                StatusWidget->AddToPlayerScreen(20);
                StatusWidget->SetPositionInViewport(FVector2D(30, 150));
                StatusWidget->SetDesiredSizeInViewport(FVector2D(512, 344));
            }
        }
    }
    HintRefreshSeconds -= DeltaSeconds;
    if (HintRefreshSeconds <= 0) { UpdateNativeHints(); HintRefreshSeconds = 0.2; }
    if (!bAssessed)
    {
        if (!bBlueprintRejected) { AssessBlueprint(); }
        return;
    }
    InvalidateChangedLayerSources();
    if (LayerLoadingHologram)
    {
        LayerLoadSeconds += DeltaSeconds;
        if (LayerLoadSeconds > 15.0)
        {
            if (Layers.IsValidIndex(LoadingLayerIndex))
            {
                Layers[LoadingLayerIndex].bRejected = true;
                Layers[LoadingLayerIndex].Status = NSLOCTEXT("Roadworks", "LayerLoadTimeout", "Decoration blueprint loading timed out. Disable the layer or choose another blueprint.");
            }
            CancelLayerLoad();
            MarkDirty();
        }
    }
    EnsureLayerTemplate();
    if (!IsLayerConfigurationOpen() && (Phase == EParametricRoadPhase::Start || Phase == EParametricRoadPhase::Drawing))
    {
        RefreshInvalidCursor();
    }
    if (NoticeSeconds > 0)
    {
        NoticeSeconds -= DeltaSeconds;
        if (NoticeSeconds <= 0) { MarkDirty(); }
    }
    if (IsBuilding())
    {
        if (DirectionMarkers) { DirectionMarkers->Update({}, Parameters.SliceLength); }
        if (bServerRefused)
        {
            StopConstruction(NSLOCTEXT("Roadworks", "ServerRefused", "The server refused the next slice. Construction stopped."));
            return;
        }
        SendDelay -= DeltaSeconds;
        if (bAwaitingAck)
        {
            PendingSeconds += DeltaSeconds;
            if (PendingSeconds > 15.0)
            {
                StopConstruction(FText::Format(NSLOCTEXT("Roadworks", "BlueprintAckTimeout",
                    "No confirmation for {0} at placement {1}. Check the last placement before building again."),
                    FText::FromString(PendingBlueprintName), FText::AsNumber(NextSlice + 1)));
            }
        }
        else if (SendDelay <= 0) { SendNextSlice(); }
        return;
    }
    SolveDelay -= DeltaSeconds;
    ValidationDelay -= DeltaSeconds;
    if (bDirty && SolveDelay <= 0)
    {
        Solve();
        SolveDelay = 0.1;
    }
    if (Phase != EParametricRoadPhase::Start) { UpdatePreviews(); }
    if (DirectionMarkers)
    {
        TOptional<FTransform> DirectionFrame;
        if (!IsLayerConfigurationOpen() && Phase == EParametricRoadPhase::Start && bCursorValid)
        {
            DirectionFrame = ControlPointRequest().StartPose;
        }
        else if (!IsLayerConfigurationOpen() && EvaluatedPath.IsValid())
        {
            FTransform EndFrame;
            if (UParametricRoadGeometry::SamplePath(EvaluatedPath, EvaluatedPath.GetLength(), EndFrame))
            { DirectionFrame = EndFrame; }
        }
        DirectionMarkers->Update(DirectionFrame, Parameters.SliceLength);
    }
}

void UParametricRoadSession::Primary()
{
    if (!bAssessed || IsBuilding() || IsLayerConfigurationOpen() || bPresetSwitchPending) { return; }
    RefreshInvalidCursor();
    NoticeSeconds = 0;
    if (Phase == EParametricRoadPhase::Start)
    {
        if (!bCursorValid) { return; }
        if (!bEndExplicit)
        {
            EndHeading = StartHeading;
            EndGrade = StartGrade;
            EndBank = StartBank;
            EndHeight = StartHeight;
        }
        StartCenter = CursorCenter;
        Phase = EParametricRoadPhase::Drawing;
        SelectPoint(GetPointCount() - 1);
        MarkDirty();
    }
    else if (Phase == EParametricRoadPhase::Drawing)
    {
        if (!bCursorValid) { return; }
        EndCenter = CursorCenter;
        Phase = EParametricRoadPhase::Ready;
        MarkDirty();
    }
    else { BeginConstruction(); }
}

void UParametricRoadSession::Secondary()
{
    if (IsLayerConfigurationOpen()) { CloseLayerConfiguration(); return; }
    if (IsBuilding())
    {
        StopConstruction(NSLOCTEXT("Roadworks", "QueueCancelled",
            "Remaining slices cancelled. A request already sent may still complete."));
        return;
    }
    if (Phase == EParametricRoadPhase::Ready) { Phase = EParametricRoadPhase::Drawing; }
    else if (!IntermediatePoints.IsEmpty()) { IntermediatePoints.Pop(); bPendingExtension = false; }
    else { Phase = EParametricRoadPhase::Start; bPendingExtension = false; }
    SelectPoint(Phase == EParametricRoadPhase::Start ? 0 : GetPointCount() - 1);
    MarkDirty();
    if (Phase == EParametricRoadPhase::Start)
    {
        SetSourcePreviewVisible(true);
        if (Source.IsValid()) { Source->SetActorHiddenInGame(false); }
    }
}

namespace
{
    EParametricRoadParameter AdjustmentParameter(EParametricRoadAdjustment Adjustment, bool bStart)
    {
        using A = EParametricRoadAdjustment;
        using P = EParametricRoadParameter;
        switch (Adjustment)
        {
        case A::Heading: return bStart ? P::StartHeading : P::EndHeading;
        case A::Grade: return bStart ? P::StartGrade : P::EndGrade;
        case A::Bank: return bStart ? P::StartBank : P::EndBank;
        case A::Height: return bStart ? P::StartHeight : P::EndHeight;
        case A::Tension: return P::CurveTension;
        case A::Overlap: return P::Overlap;
        case A::Length: return P::Length;
        case A::Width: return P::Width;
        case A::Thickness: return P::Thickness;
        case A::EdgeStep: return P::EdgeStep;
        default: return P::DeckPlane;
        }
    }

    double AdjustmentStep(EParametricRoadAdjustment Adjustment)
    {
        using A = EParametricRoadAdjustment;
        switch (Adjustment)
        {
        case A::Heading: return 5;
        case A::Grade: case A::Bank: return 1;
        case A::Height: case A::EdgeStep: return 0.5;
        case A::Tension: case A::Overlap: return 0.05;
        default: return 0.1;
        }
    }
}

void UParametricRoadSession::CycleAdjustment(int32 Delta)
{
    if (!bAssessed || IsBuilding() || IsLayerConfigurationOpen() || Delta == 0) { return; }
    constexpr int32 Count = int32(EParametricRoadAdjustment::Count);
    Adjustment = EParametricRoadAdjustment((int32(Adjustment) + Delta % Count + Count) % Count);
}

bool UParametricRoadSession::CanUseNativeRotation() const
{
    AFGPlayerController* PC = Controller(BuildState.Get());
    if (!CanAcceptRoadInput()) { return false; }
    if (IsSelectingAdjustment()) { return true; }
    // Modified wheel belongs to the game's hotbars/other native actions.
    // This is only a pass-through guard, never a shortcut of our own.
    return !PC || !(PC->IsInputKeyDown(EKeys::LeftAlt) || PC->IsInputKeyDown(EKeys::RightAlt)
        || PC->IsInputKeyDown(EKeys::LeftControl) || PC->IsInputKeyDown(EKeys::RightControl)
        || PC->IsInputKeyDown(EKeys::LeftShift) || PC->IsInputKeyDown(EKeys::RightShift));
}

void UParametricRoadSession::RotateHeading(int32 Delta)
{
    // The scoped Enhanced Input wheel handler owns selection. Consume a native Scroll
    // callback during the hold without cycling a second time.
    if (!IsSelectingAdjustment()) { ApplyWheel(Delta, false); }
}

void UParametricRoadSession::ApplyWheel(int32 Delta, bool bSelectParameter)
{
    if (bSelectParameter) { CycleAdjustment(Delta); }
    else { AdjustValue(Delta); }
}

void UParametricRoadSession::AdjustValue(int32 Delta)
{
    if (!bAssessed || IsBuilding() || IsLayerConfigurationOpen() || Delta == 0) { return; }
    if (Adjustment == EParametricRoadAdjustment::Axis) { ToggleForwardAxis(); return; }
    const double Value = GetSelectedAdjustmentNumber(Adjustment)
        + FMath::Clamp(Delta, -10, 10) * AdjustmentStep(Adjustment) * (bFineAdjustment ? 0.1 : 1.0);
    SetAdjustmentInternal(Adjustment, Value);
}

void UParametricRoadSession::SetAdjustmentFromWidget(int32 Index, double Value)
{
    if (!CanEditFromWidget() || Index < 0 || Index >= int32(EParametricRoadAdjustment::Count) || !FMath::IsFinite(Value)) { return; }
    const auto Selected = EParametricRoadAdjustment(Index);
    if (Selected == EParametricRoadAdjustment::Axis && Value != 0 && Value != 1) { return; }
    Adjustment = Selected;
    SetAdjustmentInternal(Selected, Value);
}

void UParametricRoadSession::SetAdjustmentInternal(EParametricRoadAdjustment Selected, double Value)
{
    if (!FMath::IsFinite(Value)) { return; }
    if (Selected == EParametricRoadAdjustment::Axis)
    {
        if ((Value == 0 || Value == 1) && (Value == 1) != bForwardY) { ToggleForwardAxisInternal(); }
    }
    else if (int32(Selected) < 4 && IntermediatePoints.IsValidIndex(SelectedIntermediatePoint))
    { SetSelectedPointValueInternal(Selected, Value); }
    else { SetParameterInternal(AdjustmentParameter(Selected, bEditStart), Value); }
}

double UParametricRoadSession::GetSelectedAdjustmentNumber(EParametricRoadAdjustment Selected) const
{
    if (int32(Selected) < 4 && IntermediatePoints.IsValidIndex(SelectedIntermediatePoint))
    {
        const FRotator Angles = GetPointPose(GetSelectedPointIndex()).Rotator();
        switch (Selected)
        {
        case EParametricRoadAdjustment::Heading: return Angles.Yaw;
        case EParametricRoadAdjustment::Grade: return Angles.Pitch;
        case EParametricRoadAdjustment::Bank: return Angles.Roll;
        default: return IntermediatePoints[SelectedIntermediatePoint].HeightOffset / 100.0;
        }
    }
    return GetParameter(AdjustmentParameter(Selected, bEditStart));
}

void UParametricRoadSession::SetSelectedPointValueInternal(EParametricRoadAdjustment Selected, double Value)
{
    if (!FMath::IsFinite(Value) || !IntermediatePoints.IsValidIndex(SelectedIntermediatePoint)) { return; }
    auto& Waypoint = IntermediatePoints[SelectedIntermediatePoint];
    FRotator Angles = GetPointPose(GetSelectedPointIndex()).Rotator();
    switch (Selected)
    {
    case EParametricRoadAdjustment::Heading:
        Angles.Yaw = FRotator::NormalizeAxis(Value); Waypoint.Point.bManualHeading = true; break;
    case EParametricRoadAdjustment::Grade:
        Angles.Pitch = FMath::Clamp(Value, -45.0, 45.0); Waypoint.Point.bManualGrade = true; break;
    case EParametricRoadAdjustment::Bank: Angles.Roll = FMath::Clamp(Value, -60.0, 60.0); break;
    case EParametricRoadAdjustment::Height:
    {
        const double Height = FMath::Clamp(Value, -500.0, 500.0) * 100;
        Waypoint.Point.Pose.AddToTranslation(FVector(0, 0, Height - Waypoint.HeightOffset));
        Waypoint.HeightOffset = Height;
        break;
    }
    default: return;
    }
    Waypoint.Point.Pose.SetRotation(Angles.Quaternion());
    MarkDirty();
}

FText UParametricRoadSession::GetPhaseText() const
{
    switch (Phase)
    {
    case EParametricRoadPhase::Start: return NSLOCTEXT("Roadworks", "PhaseStart", "PLACE START");
    case EParametricRoadPhase::Drawing: return NSLOCTEXT("Roadworks", "PhaseEnd", "PLACE NEXT POINT");
    case EParametricRoadPhase::Ready: return NSLOCTEXT("Roadworks", "PhaseReady", "CONFIRM ROUTE");
    default: return NSLOCTEXT("Roadworks", "PhaseBuild", "BUILDING");
    }
}

FText UParametricRoadSession::GetPrimaryHint() const
{
    switch (Phase)
    {
    case EParametricRoadPhase::Start: return NSLOCTEXT("Roadworks", "HintStart", "Set start");
    case EParametricRoadPhase::Drawing: return NSLOCTEXT("Roadworks", "HintEnd", "Set end");
    case EParametricRoadPhase::Ready: return NSLOCTEXT("Roadworks", "HintConfirm", "Build road");
    default: return NSLOCTEXT("Roadworks", "HintBuilding", "Building road");
    }
}

FText UParametricRoadSession::GetSecondaryHint() const
{
    switch (Phase)
    {
    case EParametricRoadPhase::Start: return NSLOCTEXT("Roadworks", "HintClose", "Close");
    case EParametricRoadPhase::Drawing:
        return IntermediatePoints.IsEmpty() ? NSLOCTEXT("Roadworks", "HintUndoStart", "Move start")
            : NSLOCTEXT("Roadworks", "HintUndoPoint", "Remove last point");
    case EParametricRoadPhase::Ready: return NSLOCTEXT("Roadworks", "HintEditEnd", "Move end");
    default: return NSLOCTEXT("Roadworks", "HintStop", "Stop building");
    }
}

FText UParametricRoadSession::GetPanelStatus() const
{
    if (bAssessed && IsActive() && !BoundInput.IsValid())
    { return NSLOCTEXT("Roadworks", "InputNotReady", "Build Gun controls are not ready."); }
    if (Phase == EParametricRoadPhase::Start && bAssessed && NoticeSeconds <= 0) { return FText::GetEmpty(); }
    if (Phase != EParametricRoadPhase::Start && bPreviewsValid && !bDirty && !IsBuilding()
        && NoticeSeconds <= 0 && CanAfford(Solution.SegmentPoses.Num())) { return FText::GetEmpty(); }
    return Status;
}

FText UParametricRoadSession::GetAdjustmentLabel(int32 Index) const
{
    static const TCHAR* Labels[] = {TEXT("Heading"), TEXT("Grade"), TEXT("Bank"), TEXT("Height"),
        TEXT("Tension"), TEXT("Overlap"), TEXT("Slice length"), TEXT("Width"), TEXT("Thickness"),
        TEXT("Max. edge step"), TEXT("Deck plane"), TEXT("Axis")};
    return FText::FromString(Labels[FMath::Clamp(Index, 0, 11)]);
}

namespace
{
    FText FormatRoadValue(int32 Index, double Value)
    {
        if (FMath::Abs(Value) < 0.000001) { Value = 0.0; }
        FNumberFormattingOptions Format;
        Format.MinimumFractionalDigits = Index == 4 || Index == 5 ? 2 : 1;
        Format.MaximumFractionalDigits = Index < 3 ? 1 : (Index == 4 || Index == 5 ? 3 : 2);
        Format.UseGrouping = false;
        const FText Number = FText::AsNumber(Value, &Format);
        if (Index < 3) { return FText::Format(NSLOCTEXT("Roadworks", "AngleValue", "{0}°"), Number); }
        if (Index == 4) { return Number; }
        if (Index == 9) { return FText::Format(NSLOCTEXT("Roadworks", "CentimetreValue", "{0} cm"), Number); }
        return FText::Format(NSLOCTEXT("Roadworks", "MetreValue", "{0} m"), Number);
    }
}

FText UParametricRoadSession::GetAdjustmentValue(int32 Index, bool bStart) const
{
    const auto Selected = EParametricRoadAdjustment(FMath::Clamp(Index, 0, 11));
    if (Selected == EParametricRoadAdjustment::Axis) { return FText::FromString(bForwardY ? TEXT("Y") : TEXT("X")); }
    return FormatRoadValue(Index, GetParameter(AdjustmentParameter(Selected, bStart)));
}

FText UParametricRoadSession::GetSelectedAdjustmentValue(int32 Index) const
{
    const auto Selected = EParametricRoadAdjustment(FMath::Clamp(Index, 0, 11));
    if (Selected == EParametricRoadAdjustment::Axis) { return FText::FromString(bForwardY ? TEXT("Y") : TEXT("X")); }
    return FormatRoadValue(Index, GetSelectedAdjustmentNumber(Selected));
}

FText UParametricRoadSession::GetControlsText() const
{
    return FText::Format(NSLOCTEXT("Roadworks", "CompactSelection", "{0} · {1} intermediate point(s)"),
        bFineAdjustment ? NSLOCTEXT("Roadworks", "FineReadout", "FINE STEP × 0.1")
            : NSLOCTEXT("Roadworks", "NormalReadout", "NORMAL STEP"), FText::AsNumber(IntermediatePoints.Num()));
}

FText UParametricRoadSession::GetRouteSummary() const
{
    FNumberFormattingOptions Format;
    Format.MaximumFractionalDigits = 1;
    if (IsBuilding())
    {
        return FText::Format(NSLOCTEXT("Roadworks", "MonitorProgress", "{0} / {1} placements built"),
            FText::AsNumber(ConfirmedSlices), FText::AsNumber(GetBuildJobCount()));
    }
    return FText::Format(NSLOCTEXT("Roadworks", "MonitorSummary", "{0} m  /  {1} slices  /  {2} waypoints"),
        FText::AsNumber(Solution.Length / 100.0, &Format), FText::AsNumber(Solution.SegmentPoses.Num()),
        FText::AsNumber(IntermediatePoints.Num()));
}

FText UParametricRoadSession::GetSelectionSummary() const
{
    return FText::Format(NSLOCTEXT("Roadworks", "MonitorSelection", "{0}  /  {1}"),
        GetAdjustmentLabel(GetAdjustmentIndex()), GetSelectedAdjustmentValue(GetAdjustmentIndex()));
}

double UParametricRoadSession::GetDiagramValue(int32 Index) const
{
    return GetSelectedAdjustmentNumber(EParametricRoadAdjustment(FMath::Clamp(Index, 0, 10)));
}

void UParametricRoadSession::BeginConstruction()
{
    if (IsLayerConfigurationOpen() || bDirty || Solution.SegmentPoses.IsEmpty() || !bPreviewsValid || !CanAffordJobs(0, GetBuildJobCount())) { return; }
    if (IsValid(ValidationHologram)) { ValidationHologram->Destroy(); ValidationHologram = nullptr; }
    Phase = EParametricRoadPhase::Building;
    NextSlice = ConfirmedSlices = 0;
    bAwaitingAck = false;
    bServerRefused = false;
    SendDelay = 0;
    Status = NSLOCTEXT("Roadworks", "Building", "Building… Cancel stops the remaining slices.");
}

void UParametricRoadSession::WithNativeBuildContext(UFGBuildGunStateBuild* State, AFGHologram* Hologram,
    UFGBlueprintDescriptor* Descriptor, TFunctionRef<void()> Work)
{
    // Native Construct reads its hologram; the success RPC labels the operation
    // from mActiveBlueprintDescriptor. They must describe the SAME submitted job.
    // Preserve the user's active/last-selection state without invoking selection
    // setters. Scope these native fields over the transaction and restore both.
    // Native callbacks can run before mHologram is restored (or after the game
    // replaces the submitted hologram). Keep this session addressable by state
    // throughout that exact scope, including nested calls.
    TGuardValue<bool> BuildContext(bNativeBuildContextActive, true);
    TGuardValue<TObjectPtr<AFGHologram>> CurrentHologram(State->mHologram, Hologram);
    TGuardValue<TObjectPtr<UFGBlueprintDescriptor>> CurrentBlueprint(State->mActiveBlueprintDescriptor, Descriptor);
    Work();
}

void UParametricRoadSession::QueueNativeServerSource(const FString& Name)
{
    UFGBuildGunStateBuild* State = BuildState.Get();
    AFGBuildGun* Gun = State ? State->GetBuildGun() : nullptr;
    if (!Gun || Gun->GetNetMode() != NM_Client || QueuedServerBlueprintName == Name || !Source.IsValid()) { return; }
    FRoadworksModule::QueueNativeServerBlueprint(Gun, Name, Source->GetRecipe());
    QueuedServerBlueprintName = Name;
    UE_LOG(LogRoadworks, Verbose, TEXT("Road native server source queued: blueprint=%s"), *Name);
}

void UParametricRoadSession::RestoreNativeServerSource()
{
    if (!QueuedServerBlueprintName.IsEmpty() && QueuedServerBlueprintName != BlueprintName
        && GetWorld() && !GetWorld()->bIsTearingDown && BuildState.IsValid()
        && BuildState->GetBuildGun() && BuildState->GetBuildGun()->GetCurrentState() == BuildState.Get())
    { QueueNativeServerSource(BlueprintName); }
}

void UParametricRoadSession::SendNextSlice()
{
    if (IsValid(PendingHologram)) { PendingHologram->Destroy(); }
    PendingHologram = nullptr;
    if (NextSlice >= GetBuildJobCount())
    {
        StopConstruction(NSLOCTEXT("Roadworks", "Built", "Road built. Choose the start of the next road."));
        return;
    }
    UFGBuildGunStateBuild* State = BuildState.Get();
    if (!IsBuildJobSourceCurrent(NextSlice))
    {
        StopConstruction(NSLOCTEXT("Roadworks", "BlueprintSourceChanged", "Blueprint changed or is unavailable. Remaining placements stopped."));
        return;
    }
    if (!State || !CanAffordJobs(NextSlice, 1))
    {
        StopConstruction(NSLOCTEXT("Roadworks", "MaterialsChanged", "Construction stopped: materials or build context unavailable."));
        return;
    }
    const double WorkStarted = FPlatformTime::Seconds();
    const FTransform Pose = GetBuildJobPose(NextSlice);
    PendingBlueprintName = GetBuildJobName(NextSlice);
    PendingMemberCount = GetBuildJobMemberCount(NextSlice);
    PendingHologram = SpawnNativePreview(Pose, GetBuildJobDescriptor(NextSlice));
    if (!PendingHologram)
    {
        StopConstruction(NSLOCTEXT("Roadworks", "NativeHologramFailed", "Construction stopped: native hologram unavailable."));
        return;
    }
    PendingHologram->ResetConstructDisqualifiers();
    PendingHologram->ValidatePlacementAndCost(State->GetBuildGun()->GetInventory());
    if (PendingHologram->mBuildableToNewRoot.Num() != PendingMemberCount || !PendingHologram->CanConstruct())
    {
        StopConstruction(FText::Format(NSLOCTEXT("Roadworks", "NativeQueueSliceRefused",
            "Construction stopped: slice {0} refused. {1}"), FText::AsNumber(NextSlice + 1),
            NativePlacementFailure(PendingHologram, PendingMemberCount)));
        return;
    }
    // Reliable native source selection/respawn is sent only at source boundaries
    // on clients. A vanilla host then constructs and acknowledges that source.
    QueueNativeServerSource(PendingBlueprintName);
    PendingSeconds = 0;
    bAwaitingAck = true;
    bHasNativeId = false;
    {
        // Let the native build step allocate its construction ID and choose its
        // authority/client serialization path. Its archive is not exported by
        // the SDK; neither recreate that private protocol nor call Construct.
        WithNativeBuildContext(State, PendingHologram, GetBuildJobDescriptor(NextSlice), [&]
        {
            const auto* Subsystem = AFGBlueprintSubsystem::GetBlueprintSubsystem(this);
            UE_LOG(LogRoadworks, Verbose, TEXT("Road build request: job=%d/%d layer=%d blueprint=%s active=%s payload=%s members=%d"),
                NextSlice + 1, GetBuildJobCount(), GetBuildJobLayer(NextSlice) + 1, *PendingBlueprintName,
                State->GetActiveBlueprintDescriptor() ? *State->GetActiveBlueprintDescriptor()->GetBlueprintNameAsString() : TEXT("none"),
                Subsystem ? *Subsystem->mStoredBlueprintData.BlueprintHeader.BlueprintName : TEXT("none"), PendingMemberCount);
            State->InternalExecuteDuBuildStepInput(false);
            AFGHologram* NativeReplacement = State->mHologram;
            if (IsValid(NativeReplacement) && NativeReplacement != PendingHologram && NativeReplacement != Source.Get())
            {
                NativeReplacement->Destroy();
            }
        });
    }
    NativeBuildWorkSeconds = FPlatformTime::Seconds() - WorkStarted;
    if (!bAwaitingAck) { SendDelay = FMath::Clamp(NativeBuildWorkSeconds * 4.0, 0.1, 0.5); }
}

void UParametricRoadSession::AcknowledgeBlueprint(const FString& Name, int32 NumConstructed)
{
    if (!IsBuilding() || !bAwaitingAck) { return; }
    if (Name != PendingBlueprintName)
    {
        UE_LOG(LogRoadworks, Warning, TEXT("Road build acknowledgement ignored: job=%d expected=%s received=%s blueprintCount=%d"),
            NextSlice + 1, *PendingBlueprintName, *Name, NumConstructed);
        return;
    }
    UE_LOG(LogRoadworks, Verbose, TEXT("Road build acknowledged: job=%d/%d blueprint=%s blueprintCount=%d"),
        NextSlice + 1, GetBuildJobCount(), *Name, NumConstructed);
    if (NumConstructed <= 0)
    {
        bServerRefused = true;
        return;
    }
    bAwaitingAck = false;
    int32 LayerStation;
    const int32 LayerIndex = GetBuildJobLayer(NextSlice, &LayerStation);
    if (LayerIndex != INDEX_NONE) { if (Layers[LayerIndex].Preview) { Layers[LayerIndex].Preview->HideSlice(LayerStation); } }
    else if (VisualPreview) { VisualPreview->HideSlice(NextSlice); }
    ++ConfirmedSlices;
    ++NextSlice;
    SendDelay = FMath::Clamp(NativeBuildWorkSeconds * 4.0, 0.1, 0.5);
    // Keep the native hologram alive until the transaction stack has unwound.
    // It is released at the start of the next send or during shutdown.
}

void UParametricRoadSession::RefuseConstruction(FNetConstructionID Id)
{
    if (IsBuilding() && bAwaitingAck && bHasNativeId
        && Id.Client_ID == PendingId.Client_ID && Id.NetPlayerID == PendingId.NetPlayerID)
    {
        UE_LOG(LogRoadworks, Warning, TEXT("Road native construction refused: job=%d blueprint=%s id=%u player=%u"),
            NextSlice + 1, *PendingBlueprintName, uint32(Id.Client_ID), uint32(Id.NetPlayerID));
        bServerRefused = true;
    }
}

void UParametricRoadSession::ObserveNativeConstruction(FNetConstructionID Id)
{
    if (IsBuilding() && bAwaitingAck && BuildState.IsValid() && BuildState->GetHologram() == PendingHologram)
    {
        PendingId = Id;
        bHasNativeId = true;
    }
}

void UParametricRoadSession::StopConstruction(const FText& Reason)
{
    const int32 Total = GetBuildJobCount();
    UE_LOG(LogRoadworks, Display, TEXT("Road build queue stopped: confirmed=%d/%d pending=%s awaiting=%d reason=%s"),
        ConfirmedSlices, Total, *PendingBlueprintName, bAwaitingAck, *Reason.ToString());
    RestoreNativeServerSource();
    Status = FText::Format(NSLOCTEXT("Roadworks", "ConstructionStopped", "{0}\n{1}/{2} placements confirmed."),
        Reason, FText::AsNumber(ConfirmedSlices), FText::AsNumber(Total));
    NoticeSeconds = 30;
    bAwaitingAck = false;
    Phase = EParametricRoadPhase::Start;
    bEditStart = true;
    bEndExplicit = false;
    IntermediatePoints.Reset();
    SelectedIntermediatePoint = INDEX_NONE;
    bPendingExtension = false;
    bEndHeadingExplicit = bEndGradeExplicit = false;
    // No automatic retry of a partially completed or uncertain transaction.
    if (IsValid(PendingHologram)) { PendingHologram->Destroy(); }
    PendingHologram = nullptr;
    ClearPreviews();
    Solution = FParametricRoadSolution();
    SetSourcePreviewVisible(true);
    if (Source.IsValid()) { Source->SetActorHiddenInGame(false); }
    MarkDirty();
}

void UParametricRoadSession::CollectNativePreviewHolograms(TArray<AFGBlueprintHologram*>& Out) const
{
    for (AFGBlueprintHologram* Hologram : {Source.Get(), ValidationHologram.Get(), LayerLoadingHologram.Get(), PendingHologram.Get()})
        if (IsValid(Hologram)) { Out.AddUnique(Hologram); }
}

void UParametricRoadSession::ClearPreviews()
{
    if (VisualPreview) { VisualPreview->Update({}); }
    if (DirectionMarkers) { DirectionMarkers->Update({}, Parameters.SliceLength); }
    for (FParametricRoadLayerState& Layer : Layers)
    {
        if (Layer.Preview) { Layer.Preview->Update({}); }
        Layer.PreviewPoses.Reset();
        Layer.VisualRevision = 0;
    }
    if (IsValid(ValidationHologram)) { ValidationHologram->Destroy(); }
    ValidationHologram = nullptr;
    PreviewPoses.Reset(); PreviewRevision = 0;
    bPreviewsValid = bValidationPoseReady = bValidationFailed = false;
    ValidatedCount = 0;
}

void UParametricRoadSession::Shutdown()
{
    bShuttingDown = true;
    if (NativeHud) { NativeHud->Clear(); NativeHud = nullptr; }
    if (auto* Subsystem = AFGBlueprintSubsystem::GetBlueprintSubsystem(this))
    { Subsystem->OnBlueprintDescriptorUpdated.RemoveDynamic(this, &ThisClass::OnNativeBlueprintUpdated); }
    CloseLayerConfiguration();
    ClearLayerTemplates();
    UpdateNativeHints(true);
    UnbindControls();
    if (VisualPreview) { VisualPreview->Clear(); VisualPreview = nullptr; }
    if (DirectionMarkers) { DirectionMarkers->Clear(); DirectionMarkers = nullptr; }
    InputActions.Reset();
    InputContext = nullptr;
    bAwaitingAck = false;
    ClearPreviews();
    if (IsValid(PendingHologram)) { PendingHologram->Destroy(); }
    PendingHologram = nullptr;
    if (Monitor) { Monitor->BeginRetract(); Monitor = nullptr; }
    else if (StatusWidget) { StatusWidget->RemoveFromParent(); }
    StatusWidget = nullptr;
    SetSourcePreviewVisible(true);
    if (Source.IsValid())
    {
        Source->SetActorHiddenInGame(false);
        if (bSourceFloorOverridden) { Source->SetNeedsValidFloor(bOriginalSourceNeedsFloor); }
    }
    bSourceFloorOverridden = false;
}

double UParametricRoadSession::GetParameter(EParametricRoadParameter P) const
{
    switch (P)
    {
    case EParametricRoadParameter::StartHeading: return StartHeading;
    case EParametricRoadParameter::StartGrade: return StartGrade;
    case EParametricRoadParameter::StartBank: return StartBank;
    case EParametricRoadParameter::EndHeading: return EndHeading;
    case EParametricRoadParameter::EndGrade: return EndGrade;
    case EParametricRoadParameter::EndBank: return EndBank;
    case EParametricRoadParameter::StartHeight: return StartHeight / 100;
    case EParametricRoadParameter::EndHeight: return EndHeight / 100;
    case EParametricRoadParameter::Length: return Parameters.SliceLength / 100;
    case EParametricRoadParameter::Width: return Parameters.SliceWidth / 100;
    case EParametricRoadParameter::Thickness: return Parameters.SliceHeight / 100;
    case EParametricRoadParameter::Overlap: return Parameters.MinimumOverlap / 100;
    case EParametricRoadParameter::EdgeStep: return Parameters.MaximumEdgeStep;
    case EParametricRoadParameter::DeckPlane: return (LocalCenter.Z - BaseCenterZ) / 100;
    default: return Parameters.TangentScale;
    }
}

void UParametricRoadSession::SetParameter(EParametricRoadParameter P, double Value)
{
    if (!bAssessed || IsBuilding() || IsLayerConfigurationOpen() || !FMath::IsFinite(Value)) { return; }
    SetParameterInternal(P, Value);
}

void UParametricRoadSession::SetParameterInternal(EParametricRoadParameter P, double Value)
{
    if (!FMath::IsFinite(Value)) { return; }
    const double Previous = GetParameter(P);
    switch (P)
    {
    case EParametricRoadParameter::StartHeading: StartHeading = FRotator::NormalizeAxis(Value); break;
    case EParametricRoadParameter::StartGrade: StartGrade = FMath::Clamp(Value, -45.0, 45.0); break;
    case EParametricRoadParameter::StartBank: StartBank = FMath::Clamp(Value, -60.0, 60.0); break;
    case EParametricRoadParameter::EndHeading: EndHeading = FRotator::NormalizeAxis(Value); break;
    case EParametricRoadParameter::EndGrade: EndGrade = FMath::Clamp(Value, -45.0, 45.0); break;
    case EParametricRoadParameter::EndBank: EndBank = FMath::Clamp(Value, -60.0, 60.0); break;
    case EParametricRoadParameter::StartHeight: StartHeight = FMath::Clamp(Value, -500.0, 500.0) * 100; break;
    case EParametricRoadParameter::EndHeight: EndHeight = FMath::Clamp(Value, -500.0, 500.0) * 100; break;
    case EParametricRoadParameter::Length: Parameters.SliceLength = FMath::Clamp(Value * 100, 1.0, 100000.0); break;
    case EParametricRoadParameter::Width: Parameters.SliceWidth = FMath::Clamp(Value * 100, 1.0, 100000.0); break;
    case EParametricRoadParameter::Thickness: Parameters.SliceHeight = FMath::Clamp(Value * 100, 0.1, 100000.0); break;
    case EParametricRoadParameter::Overlap: Parameters.MinimumOverlap = FMath::Max(0.0, Value * 100); break;
    case EParametricRoadParameter::EdgeStep: Parameters.MaximumEdgeStep = FMath::Clamp(Value, 0.01, 100.0); break;
    case EParametricRoadParameter::CurveTension: Parameters.TangentScale = FMath::Clamp(Value, 0.05, 2.0); break;
    case EParametricRoadParameter::DeckPlane:
        LocalCenter.Z = FMath::Clamp(BaseCenterZ + FMath::Clamp(Value, -500.0, 500.0) * 100, -100000.0, 100000.0);
        break;
    }
    Parameters.MinimumOverlap = FMath::Min(Parameters.MinimumOverlap, Parameters.SliceLength * 0.95);
    if (P == EParametricRoadParameter::EndHeading || P == EParametricRoadParameter::EndGrade
        || P == EParametricRoadParameter::EndBank || P == EParametricRoadParameter::EndHeight)
    {
        bEndExplicit = true;
    }
    if (P == EParametricRoadParameter::EndHeading) { bEndHeadingExplicit = true; }
    if (P == EParametricRoadParameter::EndGrade) { bEndGradeExplicit = true; }
    if (P == EParametricRoadParameter::EndHeight && !FMath::IsNearlyEqual(Previous, GetParameter(P), 0.000001))
    { bPendingExtension = false; }
    if (Phase == EParametricRoadPhase::Start && !bEndExplicit)
    {
        EndHeading = StartHeading;
        EndGrade = StartGrade;
        EndBank = StartBank;
        EndHeight = StartHeight;
    }
    if (!FMath::IsNearlyEqual(Previous, GetParameter(P), 0.000001)) { MarkDirty(); }
}

void UParametricRoadSession::ToggleForwardAxis()
{
    if (!bAssessed || IsBuilding() || IsLayerConfigurationOpen()) { return; }
    ToggleForwardAxisInternal();
}

void UParametricRoadSession::ToggleForwardAxisInternal()
{
    // Change the coordinate basis, retaining the actual native blueprint pose.
    // Adding world yaw alone would exchange the wrong tilt axes on a sloped or
    // banked deck. Heights/centres and explicit endpoint state remain untouched.
    const FTransform NativeStart = ToBlueprintPose(RoadPoseAt(StartCenter, true));
    const FTransform NativeEnd = ToBlueprintPose(RoadPoseAt(EndCenter, false));
    FTransform StartFrame, EndFrame;
    const bool bNewForwardY = !bForwardY;
    if (!UParametricRoadGeometry::TryMakeRoadFrame(NativeStart, LocalCenter, bNewForwardY, StartFrame)
        || !UParametricRoadGeometry::TryMakeRoadFrame(NativeEnd, LocalCenter, bNewForwardY, EndFrame)) { return; }
    const FRotator StartAngles = StartFrame.Rotator();
    const FRotator EndAngles = EndFrame.Rotator();
    bForwardY = bNewForwardY;
    Swap(Parameters.SliceLength, Parameters.SliceWidth);
    StartHeading = StartAngles.Yaw; StartGrade = StartAngles.Pitch; StartBank = StartAngles.Roll;
    EndHeading = EndAngles.Yaw; EndGrade = EndAngles.Pitch; EndBank = EndAngles.Roll;
    // Intermediate tangent directions are derived from world-space centres.
    // Changing the blueprint axis does not rotate the already fixed path.
    Parameters.MinimumOverlap = FMath::Min(Parameters.MinimumOverlap, Parameters.SliceLength * 0.95);
    MarkDirty();
}

FText UParametricRoadSession::GetDetails() const
{
    if (IsBuilding())
    {
        return FText::Format(NSLOCTEXT("Roadworks", "Progress", "{0}/{1} placements confirmed · one request at a time"),
            FText::AsNumber(ConfirmedSlices), FText::AsNumber(GetBuildJobCount()));
    }
    return FText::Format(NSLOCTEXT("Roadworks", "GeometrySummary", "{0} slices · {1} m\nMin. overlap {2} cm · max. edge step {3} cm"),
        FText::AsNumber(Solution.SegmentPoses.Num()), FText::AsNumber(FMath::RoundToDouble(Solution.Length) / 100),
        FText::AsNumber(FMath::RoundToDouble(Solution.MinimumMeasuredOverlap * 10) / 10),
        FText::AsNumber(FMath::RoundToDouble(Solution.MaximumMeasuredEdgeStep * 10) / 10));
}

FText UParametricRoadSession::GetCostText() const
{
    UFGInventoryComponent* Inventory = BuildState.IsValid() ? BuildState->GetBuildGun()->GetInventory() : nullptr;
    if (Inventory && Inventory->GetNoBuildCost()) { return NSLOCTEXT("Roadworks", "NoCost", "Build cost is disabled in this session."); }
    FString Text;
    for (const auto& Item : RequiredJobCost(0, GetBuildJobCount()))
    {
        if (Item.Key)
        {
            if (!Text.IsEmpty()) { Text += TEXT(" · "); }
            Text += FText::AsNumber(Item.Value).ToString()
                + TEXT(" ") + UFGItemDescriptor::GetAbbreviatedDisplayName(Item.Key).ToString();
        }
    }
    return FText::FromString(Text);
}

bool UParametricRoadWorldSubsystem::DoesSupportWorldType(EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game;
}

bool UParametricRoadSession::MatchesBuildContext(const UFGBuildGunStateBuild* State, const AFGHologram* Hologram) const
{
    return State && GetBuildState() == State && !bShuttingDown
        && (bNativeBuildContextActive || (Source.IsValid() && GetSource() == Hologram));
}

UParametricRoadSession* UParametricRoadWorldSubsystem::Find(UFGBuildGunStateBuild* State, bool bCreate)
{
    UWorld* World = State ? State->GetWorld() : nullptr;
    UParametricRoadWorldSubsystem* Subsystem = World ? World->GetSubsystem<UParametricRoadWorldSubsystem>() : nullptr;
    if (!Subsystem) { return nullptr; }
    for (UParametricRoadSession* Session : Subsystem->Sessions)
    {
        // Native construction temporarily borrows this state with another
        // hologram. Outside that scope, require the actual source so preset
        // selection cannot reuse a session for a replaced hologram.
        if (Session->MatchesBuildContext(State, State->GetHologram())) { return Session; }
    }
    AFGBlueprintHologram* Hologram = Cast<AFGBlueprintHologram>(State->GetHologram());
    AFGCharacterPlayer* Player = State->GetBuildGun()->GetInstigatorCharacter();
    if (bCreate && Hologram && Player && Player->IsLocallyControlled()
        && State->GetBuildGun()->GetCurrentBuildGunMode() == UParametricRoadBuildMode::StaticClass())
    {
        UParametricRoadSession* Session = NewObject<UParametricRoadSession>(Subsystem);
        Subsystem->Sessions.Add(Session);
        Session->bPresetSwitchPending = Subsystem->HasPresetRequest(State->GetBuildGun());
        Session->Initialize(State, Hologram);
        return Session;
    }
    return nullptr;
}

UParametricRoadSession* UParametricRoadWorldSubsystem::Find(AFGBlueprintHologram* Hologram, bool bCreate)
{
    AFGCharacterPlayer* Player = Hologram ? Cast<AFGCharacterPlayer>(Hologram->GetConstructionInstigator()) : nullptr;
    AFGBuildGun* Gun = Player ? Player->GetBuildGun() : nullptr;
    UFGBuildGunStateBuild* State = Gun ? Cast<UFGBuildGunStateBuild>(Gun->GetCurrentState()) : nullptr;
    return State && State->GetHologram() == Hologram ? Find(State, bCreate) : nullptr;
}

void UParametricRoadWorldSubsystem::Tick(float DeltaTime)
{
    for (int32 I = Sessions.Num() - 1; I >= 0; --I)
    {
        if (!Sessions[I]->IsActive())
        {
            Sessions[I]->Shutdown();
            Sessions.RemoveAtSwap(I);
        }
        else { Sessions[I]->Tick(DeltaTime); }
    }
    TickPresetRequests(DeltaTime);
}

TStatId UParametricRoadWorldSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(ParametricRoadSessions, STATGROUP_Tickables);
}

void UParametricRoadWorldSubsystem::Deinitialize()
{
    PresetRequests.Reset();
    for (UParametricRoadSession* Session : Sessions) { Session->Shutdown(); }
    Sessions.Reset();
    Super::Deinitialize();
}
