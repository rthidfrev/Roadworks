#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "ParametricRoadSession.h"
#include "Equipment/FGBuildGunBuild.h"
#include "FGFactoryBlueprintTypes.h"
#include "Hologram/FGBlueprintHologram.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParametricRoadConstructionContextTest,
    "ParametricRoads.Editor.ConstructionContextRouting",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParametricRoadConstructionContextTest::RunTest(const FString&)
{
    TStrongObjectPtr<UParametricRoadSession> Session(NewObject<UParametricRoadSession>());
    TStrongObjectPtr<UFGBuildGunStateBuild> State(NewObject<UFGBuildGunStateBuild>());
    TStrongObjectPtr<UFGBuildGunStateBuild> OtherState(NewObject<UFGBuildGunStateBuild>());
    TStrongObjectPtr<UFGBlueprintDescriptor> Descriptor(NewObject<UFGBlueprintDescriptor>());
    // Class defaults supply distinct identity tokens only. Never spawn actors,
    // mutate these defaults, create a world, invoke RPCs or construct buildings.
    AFGBlueprintHologram* Source = GetMutableDefault<AFGBlueprintHologram>();
    AFGHologram* Submitted = GetMutableDefault<AFGHologram>();
    const auto* HologramProperty = FindFProperty<FObjectPropertyBase>(UFGBuildGunStateBuild::StaticClass(), TEXT("mHologram"));
    if (!TestNotNull(TEXT("The native hologram property is reflected"), HologramProperty)) return false;
    const auto CurrentHologram = [&]
    { return Cast<AFGHologram>(HologramProperty->GetObjectPropertyValue_InContainer(State.Get())); };
    Session->BuildState = State.Get(); Session->Source = Source;
    HologramProperty->SetObjectPropertyValue_InContainer(State.Get(), Source);
    Session->BlueprintName = TEXT("Road");
    Session->PreviewPoses = {FTransform::Identity, FTransform(FVector(800, 0, 0))};
    Session->Phase = EParametricRoadPhase::Building;
    Session->bAwaitingAck = true; Session->PendingBlueprintName = TEXT("Road");
    TestTrue(TEXT("The ordinary source belongs to its original build state"),
        Session->MatchesBuildContext(State.Get(), CurrentHologram()));
    TestFalse(TEXT("An unrelated build state never owns this queue"),
        Session->MatchesBuildContext(OtherState.Get(), Source));
    Session->WithNativeBuildContext(State.Get(), Submitted, Descriptor.Get(), [&]
    {
        TestTrue(TEXT("The exact production lookup predicate retains the submitting session"),
            Session->MatchesBuildContext(State.Get(), CurrentHologram()));
        TestFalse(TEXT("The dev34 strict source-equality rule loses the callback in this scope"),
            Session->GetSource() == CurrentHologram());
        // Native construction may replace/remove its borrowed hologram before
        // delivering the synchronous callback. The scope still owns that state.
        Session->WithNativeBuildContext(State.Get(), nullptr, Descriptor.Get(), [&]
        {
            if (Session->MatchesBuildContext(State.Get(), CurrentHologram()))
                Session->AcknowledgeBlueprint(TEXT("Road"), 1);
        });
        TestEqual(TEXT("A synchronous confirmation advances the first job once"), Session->NextSlice, 1);
        TestFalse(TEXT("The synchronous confirmation releases the wait"), Session->bAwaitingAck);
        TestTrue(TEXT("Nested scopes restore surrounding ownership"),
            Session->MatchesBuildContext(State.Get(), CurrentHologram()));
    });
    TestTrue(TEXT("The scope restores the user's actual hologram"), CurrentHologram() == Source);
    TestFalse(TEXT("A different blueprint outside the transaction remains stale"),
        Session->MatchesBuildContext(State.Get(), Submitted));
    TestFalse(TEXT("A missing source outside the transaction remains stale"),
        Session->MatchesBuildContext(State.Get(), nullptr));
    Session->bAwaitingAck = true;
    if (Session->MatchesBuildContext(State.Get(), CurrentHologram()))
        Session->AcknowledgeBlueprint(TEXT("Road"), 1);
    TestEqual(TEXT("An asynchronous confirmation after scope restoration advances the next job"), Session->NextSlice, 2);
    Session->AcknowledgeBlueprint(TEXT("Road"), 1);
    TestEqual(TEXT("A duplicate confirmation cannot advance again"), Session->ConfirmedSlices, 2);
    Session->bShuttingDown = true;
    Session->WithNativeBuildContext(State.Get(), Submitted, Descriptor.Get(), [&]
    {
        TestFalse(TEXT("Shutdown sessions are rejected even within a native transaction"),
            Session->MatchesBuildContext(State.Get(), CurrentHologram()));
    });
    TestNull(TEXT("The session fixture never creates a gameplay world"), Session->GetWorld());
    TestNull(TEXT("The native state fixture never creates a gameplay world"), State->GetWorld());
    return !HasAnyErrors();
}
#endif
