#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "ParametricRoadSession.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParametricRoadPresetSessionTest, "Roadworks.Editor.PresetSession",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParametricRoadPresetSessionTest::RunTest(const FString&)
{
    TStrongObjectPtr<UParametricRoadSession> State(NewObject<UParametricRoadSession>());
    State->bAssessed = true;
    State->BlueprintName = TEXT("native-road-id");
    State->Parameters.SliceLength = 812.3456789;
    State->Parameters.SliceWidth = 1634.567891;
    State->Parameters.SliceHeight = 123.456789;
    State->Parameters.MinimumOverlap = 23.456789;
    State->Parameters.MaximumEdgeStep = 4.567891;
    State->Parameters.TangentScale = 1.123456789;
    State->BaseCenterZ = 50;
    State->LocalCenter.Z = 67.8912345;
    State->bForwardY = true;
    State->Layers.Reset();
    for (int32 Index = 0; Index < 2; ++Index)
    {
        auto& Layer = State->Layers.AddDefaulted_GetRef().Settings;
        Layer.BlueprintName = TEXT("native-pillar-id");
        Layer.bEnabled = true;
        Layer.SpacingMetres = 21.123456789 + Index;
        Layer.FirstPlacementMetres = 2.3456789;
        Layer.OffsetMetres = FVector(-1.123456789, Index ? 3.4567891 : -3.4567891, .45678912);
        Layer.bFollowGrade = Index == 0;
        Layer.bFollowBank = Index != 0;
    }
    State->Phase = EParametricRoadPhase::Ready;
    State->IntermediatePoints.AddDefaulted();
    State->SelectedIntermediatePoint = 0;
    FParametricRoadPreset Preset = State->CapturePreset(TEXT("  Road + pillars  "));
    TestTrue(TEXT("Capture creates no disk identity implicitly"), Preset.Id.IsEmpty());
    TestEqual(TEXT("Preset names are trimmed"), Preset.Name, FString(TEXT("Road + pillars")));
    TestEqual(TEXT("Native blueprint identity is kept separately from its label"), Preset.RoadBlueprintName, State->BlueprintName);
    TestEqual(TEXT("Independent layers may share a native blueprint identity"), Preset.Layers.Num(), 2);
    TestEqual(TEXT("Capture converts edge-step centimetres to explicit disk metres"), Preset.Road.MaxEdgeStepMetres, State->Parameters.MaximumEdgeStep / 100);
    TestEqual(TEXT("Capture keeps signed local-frame offsets without displayed rounding"), Preset.Layers[0].OffsetMetres, State->Layers[0].Settings.OffsetMetres);

    FText Error;
    TestFalse(TEXT("A worldless configuration cannot trigger live native equipment or file actions"), State->CanManagePresets());
    TestFalse(TEXT("The public load gate rejects a stale/unowned modal caller"), State->LoadPresetFromWidget(Preset, Error));
    TestEqual(TEXT("Rejected load preserves the draft phase"), State->Phase, EParametricRoadPhase::Ready);
    TestEqual(TEXT("Rejected load preserves its points"), State->GetPointCount(), 3);
    auto& Road = State->BlueprintCatalog.AddDefaulted_GetRef(); Road.Name = Preset.RoadBlueprintName;
    TestFalse(TEXT("Missing decoration references block the entire composition"), State->ValidatePresetBlueprints(Preset, Error));
    TestTrue(TEXT("Missing-reference diagnostic contains its native identity"), Error.ToString().Contains(TEXT("native-pillar-id")));
    Road.DisplayName = FText::FromString(TEXT("Same displayed label"));
    auto& Pillar = State->BlueprintCatalog.AddDefaulted_GetRef(); Pillar.Name = Preset.Layers[0].BlueprintName;
    Pillar.DisplayName = FText::FromString(TEXT("Same displayed label"));
    TestTrue(TEXT("Availability resolves exact identities despite equal displayed names"), State->ValidatePresetBlueprints(Preset, Error));
    FParametricRoadPreset Invalid = Preset; Invalid.Layers[1].SpacingMetres = -1;
    TestFalse(TEXT("Bad data cannot partially replace layers or the current path"), State->ApplyPresetSettings(Invalid, Error));
    TestEqual(TEXT("Invalid composition leaves the draft intact"), State->GetPointCount(), 3);
    State->Parameters.SliceLength = 200;
    TestTrue(TEXT("Validated composition applies as one canonical settings change"), State->ApplyPresetSettings(Preset, Error));
    TestTrue(TEXT("Loading begins a new route without captured anchors"), !State->HasRoutePreview() && State->GetPointCount() == 2);
    TestEqual(TEXT("Loading keeps the preset's forward axis"), State->bForwardY, Preset.Road.bForwardY);
    TestEqual(TEXT("Length is restored without UI float truncation"), State->Parameters.SliceLength, Preset.Road.LengthMetres * 100);
    TestEqual(TEXT("Edge-step metres become native centimetres again"), State->Parameters.MaximumEdgeStep, Preset.Road.MaxEdgeStepMetres * 100);
    TestEqual(TEXT("Deck-plane offset is relative to the native blueprint centre"), State->LocalCenter.Z, State->BaseCenterZ + Preset.Road.DeckPlaneMetres * 100);
    TestEqual(TEXT("Layer order and independent offsets survive the application"), State->Layers[1].Settings.OffsetMetres, Preset.Layers[1].OffsetMetres);
    TestTrue(TEXT("Layer grade/bank flags survive independently"), State->Layers[0].Settings.bFollowGrade && !State->Layers[0].Settings.bFollowBank
        && !State->Layers[1].Settings.bFollowGrade && State->Layers[1].Settings.bFollowBank);
    TestEqual(TEXT("The loaded template name remains available when reopening the terminal"), State->GetActivePresetName(), Preset.Name);
    State->Phase = EParametricRoadPhase::Building;
    TestFalse(TEXT("Presets cannot alter a native construction queue"), State->ApplyPresetSettings(Preset, Error));
    TestNull(TEXT("All composition checks remain worldless"), State->GetWorld());
    return !HasAnyErrors();
}
#endif
