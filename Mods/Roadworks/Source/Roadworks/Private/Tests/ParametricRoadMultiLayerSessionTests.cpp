#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "ParametricRoadSession.h"
#include "ParametricRoadPreview.h"
#include "FGFactoryBlueprintTypes.h"
#include "Resources/FGItemDescriptor.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParametricRoadMultiLayerSessionTest, "Roadworks.Editor.MultipleBlueprintLayers",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParametricRoadMultiLayerSessionTest::RunTest(const FString&)
{
    TStrongObjectPtr<UParametricRoadSession> State(NewObject<UParametricRoadSession>());
    TestEqual(TEXT("An existing single-layer workflow starts with one empty slot"), State->GetLayerCount(), 1);
    State->bAssessed = true; State->Phase = EParametricRoadPhase::Ready;
    State->StartCenter = FVector(10, 20, 30); State->EndCenter = FVector(6000, 0, 0);
    State->IntermediatePoints.AddDefaulted(); State->SelectPoint(1);
    State->SelectDecorationBlueprint(TEXT("Pillar"));
    State->SetLayerSpacing(12); State->SetLayerFirstPlacement(6);
    State->Layers[0].Preview = NewObject<UParametricRoadPreview>(State.Get());
    State->Layers[0].Descriptor = NewObject<UFGBlueprintDescriptor>(State.Get());
    State->Layers[0].LoadedName = TEXT("Pillar"); State->Layers[0].MemberCount = 3;
    State->Layers[0].PreviewPoses = {FTransform(FVector(1200, 0, 0)), FTransform(FVector(2400, 0, 0))};
    State->Layers[0].Cost = {FItemAmount(UFGItemDescriptor::StaticClass(), 7)};
    const UParametricRoadPreview* OriginalSnapshot = State->Layers[0].Preview;
    TestTrue(TEXT("A second independent layer can be appended"), State->AddLayer());
    State->SelectDecorationBlueprint(TEXT("Beam")); State->SetLayerSpacing(24);
    State->SetLayerFollowGrade(false);
    State->Layers[1].LoadedName = TEXT("Beam"); State->Layers[1].MemberCount = 4;
    State->Layers[1].PreviewPoses = {FTransform(FVector(1800, 10, 0))};
    State->Layers[1].Cost = {FItemAmount(UFGItemDescriptor::StaticClass(), 11)};
    State->Layers[1].Descriptor = NewObject<UFGBlueprintDescriptor>(State.Get());
    TestTrue(TEXT("The same blueprint may be used on a third independent layer"), State->AddLayer());
    State->SelectDecorationBlueprint(TEXT("Pillar")); State->SetLayerSpacing(18); State->SetLayerFirstPlacement(3);
    State->SetLayerFollowBank(false);
    State->Layers[2].LoadedName = TEXT("Pillar"); State->Layers[2].MemberCount = 3;
    State->Layers[2].PreviewPoses = {FTransform(FVector(300, 0, 0))};
    State->Layers[2].Cost = {FItemAmount(UFGItemDescriptor::StaticClass(), 7)};
    State->Layers[2].Descriptor = NewObject<UFGBlueprintDescriptor>(State.Get());
    State->AddLayer();
    State->Layers[3].Settings.BlueprintName = TEXT("Unavailable but disabled");
    State->Layers[3].PreviewPoses = {FTransform::Identity}; // Stale disabled work must never enter a queue.
    State->Layers[3].bRejected = true;
    State->BlueprintName = TEXT("Road"); State->MemberCount = 2;
    State->PreviewPoses = {FTransform(FVector(100, 0, 0)), FTransform(FVector(200, 0, 0))};
    State->SliceCost = {FItemAmount(UFGItemDescriptor::StaticClass(), 4)};
    State->bDirty = false; const int32 Revision = State->SolutionRevision;
    State->SelectLayer(0); State->SelectLayer(2); State->SelectLayer(-20);
    TestEqual(TEXT("Invalid selection preserves the current layer"), State->GetSelectedLayerIndex(), 2);
    TestFalse(TEXT("Selection does not invalidate the road or validation"), State->bDirty);
    TestEqual(TEXT("Selection preserves the solution revision"), State->SolutionRevision, Revision);
    TestTrue(TEXT("Selection keeps the loaded preview snapshot"), State->Layers[0].Preview == OriginalSnapshot);
    TestEqual(TEXT("Same-blueprint layers retain independent station rules"), State->GetLayerSettings(0).FirstPlacementMetres, 6.0);
    TestEqual(TEXT("Selected layer exposes its own station rules"), State->GetLayerSettings().FirstPlacementMetres, 3.0);
    TestTrue(TEXT("Independent grade and bank flags do not bleed between layers"), !State->GetLayerSettings(1).bFollowGrade
        && State->GetLayerSettings(0).bFollowGrade && !State->GetLayerSettings(2).bFollowBank && State->GetLayerSettings(0).bFollowBank);
    TestEqual(TEXT("Road placements precede all enabled layers; disabled work is excluded"), State->GetBuildJobCount(), 6);
    TestEqual(TEXT("Aggregate layer count excludes stale disabled stations"), State->GetTotalLayerPlacementCount(), 4);
    TestEqual(TEXT("Mixed costs include every layer even repeated blueprint identities"), State->RequiredJobCost(0, 6).FindRef(UFGItemDescriptor::StaticClass()), int64(40));
    TestEqual(TEXT("A cost subrange crosses layer boundaries with each actual source"), State->RequiredJobCost(3, 3).FindRef(UFGItemDescriptor::StaticClass()), int64(25));
    int32 Station = INDEX_NONE;
    TestEqual(TEXT("A repeated blueprint still resolves to its specific layer"), State->GetBuildJobLayer(5, &Station), 2);
    TestEqual(TEXT("Station indexing restarts inside the selected source's own preview"), Station, 0);
    TestEqual(TEXT("A repeated blueprint retains its independent pose"), State->GetBuildJobPose(5).GetLocation(), FVector(300, 0, 0));
    TestTrue(TEXT("A repeated name does not alias the previous layer descriptor"), State->GetBuildJobDescriptor(5) == State->Layers[2].Descriptor);
    State->SelectLayer(1); State->RemoveSelectedLayer();
    TestEqual(TEXT("Removing a layer selects the nearest remaining layer"), State->GetSelectedLayerIndex(), 1);
    TestEqual(TEXT("Removing a layer retains the next layer's values"), State->GetLayerSettings().FirstPlacementMetres, 3.0);
    TestEqual(TEXT("Removing one source removes only its cost and jobs"), State->RequiredJobCost(0, 10).FindRef(UFGItemDescriptor::StaticClass()), int64(29));
    TestEqual(TEXT("Layer management retains route points and confirmation phase"), State->Phase, EParametricRoadPhase::Ready);
    TestEqual(TEXT("Layer management retains road point selection"), State->GetSelectedPointIndex(), 1);
    TestEqual(TEXT("Layer management retains the start anchor"), State->StartCenter, FVector(10, 20, 30));

    State->Phase = EParametricRoadPhase::Building;
    TestFalse(TEXT("Construction freezes layer addition"), State->AddLayer());
    TestFalse(TEXT("Construction freezes layer removal"), State->RemoveSelectedLayer());
    State->SetLayerSpacing(40); State->SelectDecorationBlueprint(TEXT("Replacement"));
    TestEqual(TEXT("Construction freezes source and station settings"), State->GetLayerSettings().BlueprintName, FString(TEXT("Pillar")));
    TestEqual(TEXT("Construction retains each submitted station sequence"), State->GetLayerSettings().SpacingMetres, 18.0);
    State->Phase = EParametricRoadPhase::Ready;
    State->Layers[1].Preview = NewObject<UParametricRoadPreview>(State.Get());
    TestEqual(TEXT("A loaded source and a disabled missing source need no template load"), State->FindLayerNeedingTemplate(), INDEX_NONE);
    State->ChangedLayerSources.Add(TEXT("Pillar")); State->InvalidateChangedLayerSources();
    TestTrue(TEXT("A source update invalidates every layer sharing that identity"), !State->Layers[0].Preview && !State->Layers[1].Preview
        && !State->Layers[0].Descriptor && !State->Layers[1].Descriptor);
    TestTrue(TEXT("An unrelated disabled source retains its explicit refusal state"), State->Layers[2].bRejected);
    TestEqual(TEXT("The first enabled missing snapshot is loaded first"), State->FindLayerNeedingTemplate(), 0);
    State->Layers[0].Preview = NewObject<UParametricRoadPreview>(State.Get()); State->Layers[0].LoadedName = TEXT("Pillar");
    TestEqual(TEXT("Once snapshotted, loading proceeds to the next independent layer"), State->FindLayerNeedingTemplate(), 1);
    while (State->GetLayerCount() < UParametricRoadSession::MaximumLayers) { TestTrue(TEXT("Layers can fill the bounded collection"), State->AddLayer()); }
    TestFalse(TEXT("The ninth layer cannot allocate preview/build work"), State->AddLayer());
    while (State->GetLayerCount()) { TestTrue(TEXT("Every layer can be removed"), State->RemoveSelectedLayer()); }
    TestEqual(TEXT("Removing the last layer leaves no dangling selection"), State->GetSelectedLayerIndex(), INDEX_NONE);
    State->SetLayerSpacing(12); State->SelectDecorationBlueprint(TEXT("Ignored without a layer"));
    TestTrue(TEXT("Empty-collection access is safe and disabled"), State->GetLayerSettings().BlueprintName.IsEmpty() && !State->GetLayerSettings().bEnabled);
    TestTrue(TEXT("Adding after an empty collection creates a usable selection"), State->AddLayer());
    TestEqual(TEXT("The restored first layer is selected"), State->GetSelectedLayerIndex(), 0);

    // Exercise the actual mathematical solve and aggregate member budget, with
    // worldless metadata snapshots only. No native actor or gameplay is created.
    TStrongObjectPtr<UParametricRoadSession> Budget(NewObject<UParametricRoadSession>());
    Budget->bAssessed = true; Budget->Phase = EParametricRoadPhase::Ready; Budget->EndCenter = FVector(10000, 0, 0); Budget->MemberCount = 1;
    Budget->AddLayer();
    for (FParametricRoadLayerState& Layer : Budget->Layers)
    {
        Layer.Settings.BlueprintName = Layer.LoadedName = TEXT("Pillar"); Layer.Settings.bEnabled = true;
        Layer.Preview = NewObject<UParametricRoadPreview>(Budget.Get()); Layer.MemberCount = 2200;
    }
    Budget->Solve();
    TestEqual(TEXT("The road geometry itself succeeds in the aggregate-budget case"), Budget->Solution.Failure, EParametricRoadFailure::None);
    TestFalse(TEXT("Individually valid layers cannot exceed the shared member budget together"), Budget->bLayerSolutionValid);
    TestEqual(TEXT("A failed aggregate does not leave a partially buildable layer list"), Budget->GetTotalLayerPlacementCount(), 0);
    TestTrue(TEXT("The layer that exhausts the shared budget carries the refusal"), !Budget->Layers[1].Status.IsEmpty());
    Budget->SelectLayer(1); Budget->SetLayerEnabled(false); Budget->Solve();
    TestTrue(TEXT("Disabling the excess layer restores the existing one-layer solve"), Budget->bLayerSolutionValid);
    TestEqual(TEXT("The first layer's actual cumulative stations remain unchanged"), Budget->GetLayerPlacementCount(0), 8);
    TestEqual(TEXT("The disabled layer contributes no placements"), Budget->GetLayerPlacementCount(1), 0);
    Budget->SetLayerEnabled(true); Budget->ClearLayerTemplate(1);
    Budget->Layers[1].bRejected = true;
    Budget->Layers[1].Status = FText::FromString(TEXT("Native blueprint unavailable"));
    Budget->Solve();
    TestFalse(TEXT("An enabled missing/refused layer blocks the entire preview"), Budget->bLayerSolutionValid);
    TestEqual(TEXT("A missing later source leaves no earlier layer's partial jobs"), Budget->GetTotalLayerPlacementCount(), 0);
    TestTrue(TEXT("Global feedback identifies the affected layer"), Budget->GetStatus().ToString().Contains(TEXT("Layer 2")));
    Budget->Layers[1].bRejected = false;
    Budget->Layers[1].Preview = NewObject<UParametricRoadPreview>(Budget.Get());
    Budget->Layers[1].LoadedName = TEXT("Pillar"); Budget->Layers[1].MemberCount = 2200;
    Budget->SetLayerFirstPlacement(200); Budget->Solve();
    TestTrue(TEXT("An enabled layer whose first station is beyond the end is valid"), Budget->bLayerSolutionValid);
    TestEqual(TEXT("A zero-station layer needs no validator/build job"), Budget->GetLayerPlacementCount(1), 0);
    TestEqual(TEXT("Zero-station layers do not consume another layer's station sequence"), Budget->GetTotalLayerPlacementCount(), 8);
    TestNull(TEXT("All backend checks remain worldless"), Budget->GetWorld());
    for (TFieldIterator<FProperty> Property(FParametricRoadLayerState::StaticStruct()); Property; ++Property)
    { TestFalse(TEXT("Layer object references are neither saved nor replicated"), Property->HasAnyPropertyFlags(CPF_SaveGame | CPF_Net)); }
    return !HasAnyErrors();
}
#endif
