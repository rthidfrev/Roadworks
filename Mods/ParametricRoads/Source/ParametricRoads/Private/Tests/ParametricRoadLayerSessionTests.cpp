#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "ParametricRoadSession.h"
#include <limits>
#include "Resources/FGItemDescriptor.h"
#include "Equipment/FGBuildGunBuild.h"
#include "FGFactoryBlueprintTypes.h"
#include "UObject/UnrealType.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParametricRoadLayerSessionTest, "ParametricRoads.Editor.BlueprintLayerSession",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParametricRoadLayerSessionTest::RunTest(const FString&)
{
    TStrongObjectPtr<UParametricRoadSession> Session(NewObject<UParametricRoadSession>());
    Session->bAssessed = true;
    Session->Phase = EParametricRoadPhase::Ready;
    Session->StartCenter = FVector(11, 22, 33);
    Session->EndCenter = FVector(7000, 600, 80);
    Session->IntermediatePoints.AddDefaulted();
    Session->SelectPoint(1);
    Session->bDirty = false;
    Session->bLayerConfigurationOpen = true;
    Session->Primary(); Session->AddPoint(); Session->AdjustValue(1); Session->CyclePoint();
    Session->SetParameter(EParametricRoadParameter::StartHeading, 100);
    TestEqual(TEXT("Configuration blocks construction/point/parameter input"), Session->Phase, EParametricRoadPhase::Ready);
    TestEqual(TEXT("Configuration retains selected point"), Session->GetSelectedPointIndex(), 1);
    TestEqual(TEXT("Configuration retains fixed centres"), Session->StartCenter, FVector(11,22,33));
    TestEqual(TEXT("Configuration retains point list"), Session->IntermediatePoints.Num(), 1);
    TestEqual(TEXT("Configuration blocks heading mutation"), Session->StartHeading, 0.0);
    Session->SetLayerSpacing(18); Session->SetLayerFirstPlacement(5);
    Session->SetLayerFollowGrade(false); Session->SetLayerFollowBank(false);
    TestEqual(TEXT("Layer edits work while the native menu gates road input"), Session->Layers[0].Settings.SpacingMetres, 18.0);
    TestEqual(TEXT("First station is metres from the complete route start"), Session->Layers[0].Settings.FirstPlacementMetres, 5.0);
    TestTrue(TEXT("Layer settings invalidate the dependent preview"), Session->bDirty);
    Session->CloseLayerConfiguration();
    TestEqual(TEXT("Closing preserves phase"), Session->Phase, EParametricRoadPhase::Ready);
    TestEqual(TEXT("Closing preserves selected point"), Session->GetSelectedPointIndex(), 1);
    TestEqual(TEXT("Closing preserves centres"), Session->EndCenter, FVector(7000,600,80));
    Session->SetLayerSpacing(-1); Session->SetLayerFirstPlacement(-1);
    TestEqual(TEXT("Spacing has a finite positive minimum"), Session->Layers[0].Settings.SpacingMetres, .1);
    TestEqual(TEXT("First station cannot precede the road"), Session->Layers[0].Settings.FirstPlacementMetres, 0.0);
    Session->SetLayerSpacing(std::numeric_limits<double>::infinity());
    TestEqual(TEXT("Invalid numeric input is ignored"), Session->Layers[0].Settings.SpacingMetres, .1);

    Session->Layers[0].Settings.bEnabled = true;
    Session->PreviewPoses = {FTransform(FVector(100,0,0)), FTransform(FVector(200,0,0))};
    Session->Layers[0].PreviewPoses = {FTransform(FVector(1200,10,30))};
    Session->BlueprintName = TEXT("Road"); Session->Layers[0].LoadedName = TEXT("Pillar");
    Session->MemberCount = 2; Session->Layers[0].MemberCount = 3;
    Session->SliceCost = {FItemAmount(UFGItemDescriptor::StaticClass(), 4)};
    Session->Layers[0].Cost = {FItemAmount(UFGItemDescriptor::StaticClass(), 7)};
    TestEqual(TEXT("Mixed queue contains road then independently spaced layer"), Session->GetBuildJobCount(), 3);
    TestEqual(TEXT("Layer job retains its actual source identity"), Session->GetBuildJobName(2), FString(TEXT("Pillar")));
    TestEqual(TEXT("Layer job retains its solved native-root pose"), Session->GetBuildJobPose(2).GetLocation(), FVector(1200,10,30));
    TestEqual(TEXT("Mixed costs sum both sources including shared items"), Session->RequiredJobCost(0,3).FindRef(UFGItemDescriptor::StaticClass()), int64(15));
    TestEqual(TEXT("Per-request affordability uses the actual job"), Session->RequiredJobCost(2,1).FindRef(UFGItemDescriptor::StaticClass()), int64(7));
    // Exercise the production scope using only UObjects: no actor, controller,
    // world, RPC invocation or native construction. Native ACK labels read this
    // exact active-descriptor field independently of the submitted hologram.
    TStrongObjectPtr<UFGBuildGunStateBuild> NativeState(NewObject<UFGBuildGunStateBuild>());
    TStrongObjectPtr<UFGBlueprintDescriptor> RoadDescriptor(NewObject<UFGBlueprintDescriptor>());
    TStrongObjectPtr<UFGBlueprintDescriptor> PillarDescriptor(NewObject<UFGBlueprintDescriptor>());
    TestNull(TEXT("Unowned native metadata fixture has no world"), NativeState->GetWorld());
    UFGBlueprintDescriptor* OriginalDescriptor = NativeState->GetActiveBlueprintDescriptor();
    Session->WithNativeBuildContext(NativeState.Get(), nullptr, RoadDescriptor.Get(), [&]
    {
        TestTrue(TEXT("Road native context labels its own blueprint"), NativeState->GetActiveBlueprintDescriptor() == RoadDescriptor.Get());
        Session->WithNativeBuildContext(NativeState.Get(), nullptr, PillarDescriptor.Get(), [&]
        {
            TestTrue(TEXT("Decoration context changes the native ACK metadata, not only the hologram"),
                NativeState->GetActiveBlueprintDescriptor() == PillarDescriptor.Get());
            return;
        });
        TestTrue(TEXT("A completed decoration restores the surrounding road context"), NativeState->GetActiveBlueprintDescriptor() == RoadDescriptor.Get());
    });
    TestTrue(TEXT("Native transaction restores the exact user's original descriptor"), NativeState->GetActiveBlueprintDescriptor() == OriginalDescriptor);
    for (const auto& Contract : TArray<TPair<UClass*, FName>>{
        {AFGBuildGun::StaticClass(), TEXT("Server_SetDesiredBlueprint")},
        {AFGBuildGun::StaticClass(), TEXT("Server_GotoBuildState")},
        {UFGBuildGunStateBuild::StaticClass(), TEXT("Server_ConstructHologram")}})
    {
        UFunction* Function = Contract.Key->FindFunctionByName(Contract.Value);
        TestNotNull(TEXT("Native source/construct RPC is reflected"), Function);
        if (Function) { TestTrue(TEXT("Source selection/respawn and construction are native reliable server calls"),
            Function->HasAllFunctionFlags(FUNC_Net | FUNC_NetServer | FUNC_NetReliable)); }
    }
    Session->Phase = EParametricRoadPhase::Building;
    Session->NextSlice = 2; Session->ConfirmedSlices = 2;
    Session->bAwaitingAck = true; Session->PendingBlueprintName = TEXT("Pillar");
    Session->AcknowledgeBlueprint(TEXT("Road"),1);
    TestEqual(TEXT("An acknowledgement for another source cannot advance the queue"), Session->NextSlice, 2);
    Session->AcknowledgeBlueprint(TEXT("Pillar"),1);
    TestEqual(TEXT("Only the in-flight source advances its job"), Session->NextSlice, 3);
    Session->AcknowledgeBlueprint(TEXT("Pillar"),1);
    TestEqual(TEXT("A duplicate without an outstanding request cannot advance"), Session->NextSlice, 3);
    Session->SetLayerSpacing(40);
    TestEqual(TEXT("Building freezes the job settings"), Session->Layers[0].Settings.SpacingMetres, .1);
    TestNull(TEXT("Backend checks create no gameplay world"), Session->GetWorld());
    return true;
}
#endif
