#include "ParametricRoadSession.h"
#include "ParametricRoadBuildMode.h"
#include "RoadworksModule.h"
#include "Equipment/FGBuildGunBuild.h"
#include "Equipment/FGBuildGun.h"
#include "Engine/World.h"
#include "FGBlueprintSubsystem.h"
#include "FGCharacterPlayer.h"
#include "Hologram/FGBlueprintHologram.h"

static_assert(UParametricRoadSession::MaximumLayers == ParametricRoadPresets::MaximumLayers);

FParametricRoadPreset UParametricRoadSession::CapturePreset(const FString& Name) const
{
    FParametricRoadPreset Preset;
    Preset.Name = Name.TrimStartAndEnd();
    Preset.RoadBlueprintName = BlueprintName;
    Preset.Road.LengthMetres = Parameters.SliceLength / 100;
    Preset.Road.WidthMetres = Parameters.SliceWidth / 100;
    Preset.Road.ThicknessMetres = Parameters.SliceHeight / 100;
    Preset.Road.OverlapMetres = Parameters.MinimumOverlap / 100;
    Preset.Road.MaxEdgeStepMetres = Parameters.MaximumEdgeStep / 100;
    Preset.Road.DeckPlaneMetres = (LocalCenter.Z - BaseCenterZ) / 100;
    Preset.Road.CurveTension = Parameters.TangentScale;
    Preset.Road.bForwardY = bForwardY;
    for (const auto& Layer : Layers) { Preset.Layers.Add(Layer.Settings); }
    return Preset;
}

bool UParametricRoadSession::CanManagePresets() const
{
    return CanEditFromWidget() && !bPresetSwitchPending;
}

void UParametricRoadSession::RememberSavedPresetFromWidget(const FParametricRoadPreset& Preset)
{
    if (CanManagePresets() && Preset.RoadBlueprintName == BlueprintName)
    { ActivePresetId = Preset.Id; ActivePresetName = Preset.Name; }
}

bool UParametricRoadSession::ValidatePresetBlueprints(const FParametricRoadPreset& Preset, FText& Error) const
{
    if (!ParametricRoadPresets::Validate(Preset, Error)) { return false; }
    const auto Exists = [&](const FString& Name)
    { return BlueprintCatalog.ContainsByPredicate([&](const auto& Entry) { return Entry.Name == Name; }); };
    TArray<FString> Missing;
    if (!Exists(Preset.RoadBlueprintName)) { Missing.Add(Preset.RoadBlueprintName); }
    for (const auto& Layer : Preset.Layers)
        if (!Layer.BlueprintName.IsEmpty() && !Exists(Layer.BlueprintName)) { Missing.AddUnique(Layer.BlueprintName); }
    if (!Missing.IsEmpty())
    {
        Error = FText::Format(NSLOCTEXT("Roadworks", "PresetMissingBlueprints", "Missing blueprints: {0}"),
            FText::FromString(FString::Join(Missing, TEXT(", "))));
        return false;
    }
    Error = FText::GetEmpty();
    return true;
}

bool UParametricRoadSession::LoadPresetFromWidget(const FParametricRoadPreset& Preset, FText& Error)
{
    if (!CanManagePresets())
    {
        Error = NSLOCTEXT("Roadworks", "PresetInputUnavailable", "Presets are unavailable while construction or another menu owns input.");
        return false;
    }
    RefreshBlueprintCatalog();
    if (!ValidatePresetBlueprints(Preset, Error)) { return false; }
    UParametricRoadWorldSubsystem* Owner = GetWorld() ? GetWorld()->GetSubsystem<UParametricRoadWorldSubsystem>() : nullptr;
    if (!Owner)
    {
        Error = NSLOCTEXT("Roadworks", "PresetWorldUnavailable", "The route tool is no longer available.");
        return false;
    }
    if (!Owner->QueuePreset(this, Preset, Error)) { return false; }
    bPresetSwitchPending = true;
    // Selection is deferred to the world tick, after the virtual Slate pointer
    // callback unwinds. Never recreate native equipment inside that callback.
    CloseLayerConfiguration();
    return true;
}

bool UParametricRoadSession::ApplyPresetSettings(const FParametricRoadPreset& Preset, FText& Error)
{
    if (!bAssessed || IsBuilding() || BlueprintName != Preset.RoadBlueprintName
        || !ValidatePresetBlueprints(Preset, Error)) { return false; }
    ClearPreviews();
    ClearLayerTemplates();
    Layers.Reset();
    for (const auto& Settings : Preset.Layers) { Layers.AddDefaulted_GetRef().Settings = Settings; }
    SelectedLayerIndex = Layers.IsEmpty() ? INDEX_NONE : 0;
    Parameters.SliceLength = Preset.Road.LengthMetres * 100;
    Parameters.SliceWidth = Preset.Road.WidthMetres * 100;
    Parameters.SliceHeight = Preset.Road.ThicknessMetres * 100;
    Parameters.MinimumOverlap = Preset.Road.OverlapMetres * 100;
    Parameters.MaximumEdgeStep = Preset.Road.MaxEdgeStepMetres * 100;
    Parameters.TangentScale = Preset.Road.CurveTension;
    LocalCenter.Z = FMath::Clamp(BaseCenterZ + Preset.Road.DeckPlaneMetres * 100, -100000.0, 100000.0);
    bForwardY = Preset.Road.bForwardY;
    StartHeight = EndHeight = 0;
    StartHeading = EndHeading = StartGrade = EndGrade = StartBank = EndBank = 0;
    if (Source.IsValid()) { InitializeRoadOrientation(Source->GetActorTransform()); }
    IntermediatePoints.Reset();
    SelectedIntermediatePoint = INDEX_NONE;
    bPendingExtension = bEndExplicit = bEndHeadingExplicit = bEndGradeExplicit = false;
    Phase = EParametricRoadPhase::Start;
    bEditStart = true;
    bCursorValid = false;
    Adjustment = EParametricRoadAdjustment::Heading;
    EvaluatedPath = FParametricRoadPath();
    Solution = FParametricRoadSolution();
    bLayerSolutionValid = true;
    ActivePresetId = Preset.Id;
    ActivePresetName = Preset.Name;
    MarkDirty();
    Error = FText::GetEmpty();
    return true;
}

bool UParametricRoadWorldSubsystem::HasPresetRequest(AFGBuildGun* Gun) const
{
    return PresetRequests.ContainsByPredicate([&](const auto& Request) { return Request.Gun.Get() == Gun; });
}

bool UParametricRoadWorldSubsystem::IsPresetLoading(AFGBuildGun* Gun)
{
    UWorld* World = Gun ? Gun->GetWorld() : nullptr;
    const auto* Owner = World ? World->GetSubsystem<UParametricRoadWorldSubsystem>() : nullptr;
    return Owner && Owner->HasPresetRequest(Gun);
}

bool UParametricRoadWorldSubsystem::QueuePreset(UParametricRoadSession* Origin,
    const FParametricRoadPreset& Preset, FText& Error)
{
    UFGBuildGunStateBuild* State = Origin ? Origin->GetBuildState() : nullptr;
    AFGBuildGun* Gun = State ? State->GetBuildGun() : nullptr;
    AFGCharacterPlayer* Player = Gun ? Gun->GetInstigatorCharacter() : nullptr;
    if (!Origin || !Origin->CanManagePresets() || !Player || !Player->IsLocallyControlled()
        || !State->GetActiveRecipe() || HasPresetRequest(Gun) || PresetRequests.Num() >= 4)
    {
        Error = NSLOCTEXT("Roadworks", "PresetQueueUnavailable", "A preset cannot be loaded right now.");
        return false;
    }
    auto& Request = PresetRequests.AddDefaulted_GetRef();
    Request.Gun = Gun;
    Request.OriginalSource = Origin->GetSource();
    Request.Recipe = State->GetActiveRecipe().Get();
    Request.Preset = Preset;
    Error = FText::GetEmpty();
    return true;
}

void UParametricRoadWorldSubsystem::TickPresetRequests(float DeltaSeconds)
{
    for (int32 Index = PresetRequests.Num() - 1; Index >= 0; --Index)
    {
        auto& Request = PresetRequests[Index];
        AFGBuildGun* Gun = Request.Gun.Get();
        const auto Finish = [&](const FText& Message, bool bSuccess)
        {
            for (UParametricRoadSession* Session : Sessions)
            {
                if (Session->BuildState.IsValid() && Session->BuildState->GetBuildGun() == Gun)
                {
                    Session->bPresetSwitchPending = false;
                    if (!Session->bShuttingDown)
                    { Session->Status = Message; Session->NoticeSeconds = 10; }
                }
            }
            UE_LOG(LogRoadworks, Display, TEXT("Preset handoff %s: %s"), bSuccess ? TEXT("completed") : TEXT("stopped"), *Message.ToString());
            PresetRequests.RemoveAt(Index);
        };
        Request.Seconds += FMath::IsFinite(DeltaSeconds) ? FMath::Max(0.f, DeltaSeconds) : 0.f;
        AFGCharacterPlayer* Player = Gun ? Gun->GetInstigatorCharacter() : nullptr;
        UFGBuildGunStateBuild* State = Gun ? Cast<UFGBuildGunStateBuild>(Gun->GetCurrentState()) : nullptr;
        if (!Player || !Player->IsLocallyControlled() || !Player->IsAliveAndWell() || !State || Request.Seconds > 15)
        { Finish(NSLOCTEXT("Roadworks", "PresetSelectionStopped", "Preset loading stopped or timed out."), false); continue; }
        if (!Request.Recipe.IsValid() || State->GetActiveRecipe().Get() != Request.Recipe.Get())
        { Finish(NSLOCTEXT("Roadworks", "PresetRecipeChanged", "Preset loading cancelled because another building was selected."), false); continue; }
        const UClass* ExpectedMode = Request.bModeRequested ? UParametricRoadBuildMode::StaticClass() : Request.ModeAfterSelection.Get();
        if (Request.bSelectionRequested && Gun->GetCurrentBuildGunMode().Get() != ExpectedMode)
        { Finish(NSLOCTEXT("Roadworks", "PresetModeChanged", "Preset loading cancelled because the build mode changed."), false); continue; }
        AFGBlueprintHologram* Hologram = Cast<AFGBlueprintHologram>(State->GetHologram());
        if (!Request.bSelectionRequested)
        {
            if (!Hologram || !Request.OriginalSource.IsValid() || Hologram != Request.OriginalSource.Get() || !State->GetActiveRecipe()
                || Gun->GetCurrentBuildGunMode() != UParametricRoadBuildMode::StaticClass())
            { Finish(NSLOCTEXT("Roadworks", "PresetSelectionChanged", "Preset loading cancelled because the selected tool changed."), false); continue; }
            AFGBlueprintSubsystem* Blueprints = AFGBlueprintSubsystem::GetBlueprintSubsystem(this);
            if (!Blueprints || !Blueprints->GetBlueprintDescriptorByNameString(Request.Preset.RoadBlueprintName))
            { Finish(NSLOCTEXT("Roadworks", "PresetBaseMissing", "The preset's base blueprint is no longer available."), false); continue; }
            UParametricRoadSession* Origin = Find(State);
            FText AvailabilityError;
            if (Origin) { Origin->RefreshBlueprintCatalog(); }
            if (!Origin || !Origin->ValidatePresetBlueprints(Request.Preset, AvailabilityError))
            {
                if (AvailabilityError.IsEmpty()) { AvailabilityError = NSLOCTEXT("Roadworks", "PresetOriginLost", "The original route preview is no longer available."); }
                Finish(AvailabilityError, false); continue;
            }
            if (Origin->BlueprintName == Request.Preset.RoadBlueprintName)
            {
                // Reuse an already loaded base. Native selection may keep the
                // same hologram when the requested blueprint has not changed.
                if (!Origin->ApplyPresetSettings(Request.Preset, AvailabilityError))
                { Finish(AvailabilityError, false); continue; }
                Finish(FText::Format(NSLOCTEXT("Roadworks", "PresetLoaded", "Preset loaded: {0}"),
                    FText::FromString(Request.Preset.Name)), true);
                continue;
            }
            const TSubclassOf<UFGRecipe> Recipe = State->GetActiveRecipe();
            Request.bSelectionRequested = true;
            // Release validators/layer loads before the native shared blueprint
            // world changes. The queued composition outlives this old session.
            for (UParametricRoadSession* Session : Sessions)
                if (Session->GetBuildState() == State && Session->GetSource() == Hologram) { Session->Shutdown(); }
            Gun->SetDesiredBlueprint(Request.Preset.RoadBlueprintName);
            Gun->GotoBuildState(Recipe);
            Request.ModeAfterSelection = Gun->GetCurrentBuildGunMode().Get();
            auto* NextState = Cast<UFGBuildGunStateBuild>(Gun->GetCurrentState());
            auto* NextHologram = NextState ? Cast<AFGBlueprintHologram>(NextState->GetHologram()) : nullptr;
            auto* NextDescriptor = NextState ? NextState->GetActiveBlueprintDescriptor() : nullptr;
            if (NextHologram && NextHologram != Request.OriginalSource.Get() && NextDescriptor
                && NextDescriptor->GetBlueprintNameAsString() == Request.Preset.RoadBlueprintName)
            {
                Request.bModeRequested = true;
                Gun->SetCurrentBuildGunMode(UParametricRoadBuildMode::StaticClass());
            }
            continue;
        }
        if (!Hologram || Hologram == Request.OriginalSource.Get()) { continue; }
        UFGBlueprintDescriptor* Active = State->GetActiveBlueprintDescriptor();
        if (!Active || Active->GetBlueprintNameAsString() != Request.Preset.RoadBlueprintName)
        { Finish(NSLOCTEXT("Roadworks", "PresetOtherBlueprint", "Preset loading cancelled because another blueprint was selected."), false); continue; }
        if (!Request.bModeRequested)
        {
            Request.bModeRequested = true;
            Gun->SetCurrentBuildGunMode(UParametricRoadBuildMode::StaticClass());
        }
        if (Gun->GetCurrentBuildGunMode() != UParametricRoadBuildMode::StaticClass())
        { Finish(NSLOCTEXT("Roadworks", "PresetModeChanged", "Preset loading cancelled because the build mode changed."), false); continue; }
        UParametricRoadSession* Session = Find(State, true);
        if (!Session) { continue; }
        if (Session->bBlueprintRejected)
        { Finish(Session->Status, false); continue; }
        if (!Session->bAssessed) { continue; }
        Session->RefreshBlueprintCatalog();
        FText Error;
        if (!Session->ApplyPresetSettings(Request.Preset, Error))
        {
            if (Error.IsEmpty()) { Error = NSLOCTEXT("Roadworks", "PresetApplyFailed", "The preset could not be applied to this blueprint."); }
            Finish(Error, false); continue;
        }
        Finish(FText::Format(NSLOCTEXT("Roadworks", "PresetLoaded", "Preset loaded: {0}"), FText::FromString(Request.Preset.Name)), true);
    }
}
