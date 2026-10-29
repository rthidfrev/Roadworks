#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "ParametricRoadSession.h"
#include "ParametricRoadLayerInput.h"
#include "ParametricRoadLayerWidget.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParametricRoadWidgetEditingTest, "Roadworks.Editor.WidgetEditing",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParametricRoadWidgetEditingTest::RunTest(const FString&)
{
    TStrongObjectPtr<UParametricRoadSession> State(NewObject<UParametricRoadSession>());
    State->bAssessed = true;
    State->Phase = EParametricRoadPhase::Ready;
    State->EndCenter = FVector(6000, 0, 400);
    State->CursorCenter = State->EndCenter;
    FParametricRoadWaypoint Middle;
    Middle.Point.Pose = FTransform(FRotator(0, 0, 7), FVector(3000, 400, 300));
    State->IntermediatePoints.Add(Middle);
    State->SelectPoint(1);
    const FTransform Initial = State->GetPointPose(1);
    State->bLayerConfigurationOpen = true;
    State->bDirty = false;

    State->SelectPoint(0);
    State->AdjustValue(1);
    State->SetParameter(EParametricRoadParameter::EndGrade, 25);
    State->ToggleForwardAxis();
    TestEqual(TEXT("Ordinary gameplay input retains its modal selection gate"), State->GetSelectedPointIndex(), 1);
    TestTrue(TEXT("Ordinary gameplay mutations cannot leak through the configuration"), State->GetPointPose(1).Equals(Initial)
        && !State->bForwardY && !State->bDirty);

    // The widget uses the same mutation body after ownership is checked. It
    // never clears the modal flag to borrow the gameplay input entry points.
    State->SetAdjustmentInternal(EParametricRoadAdjustment::Heading, 25);
    TestTrue(TEXT("An explicit widget heading pins only heading"), State->IntermediatePoints[0].Point.bManualHeading
        && !State->IntermediatePoints[0].Point.bManualGrade
        && FMath::IsNearlyEqual(State->GetPointPose(1).Rotator().Yaw, 25.0));
    State->SetAdjustmentInternal(EParametricRoadAdjustment::Bank, 200);
    TestTrue(TEXT("Widget bank uses the canonical clamp and preserves automatic grade"),
        FMath::IsNearlyEqual(State->GetPointPose(1).Rotator().Roll, 60.0)
        && !State->IntermediatePoints[0].Point.bManualGrade);
    State->SetAdjustmentInternal(EParametricRoadAdjustment::Grade, -80);
    State->SetAdjustmentInternal(EParametricRoadAdjustment::Height, 2.5);
    TestTrue(TEXT("Widget grade and height apply locally with canonical units and limits"),
        State->IntermediatePoints[0].Point.bManualGrade
        && FMath::IsNearlyEqual(State->GetPointPose(1).Rotator().Pitch, -45.0)
        && State->GetPointPose(1).GetLocation().Equals(FVector(3000, 400, 550)));
    TestTrue(TEXT("Modal editing retains route endpoints and confirmation phase"), State->bLayerConfigurationOpen
        && State->Phase == EParametricRoadPhase::Ready && State->EndCenter.Equals(FVector(6000, 0, 400)));

    const FTransform Edited = State->GetPointPose(1);
    State->SetAdjustmentInternal(EParametricRoadAdjustment::Length, 2);
    State->SetAdjustmentInternal(EParametricRoadAdjustment::Overlap, 500);
    State->SetAdjustmentInternal(EParametricRoadAdjustment::EdgeStep, -4);
    TestTrue(TEXT("Global dimensions keep native overlap coupling and centimetre edge-step units"),
        FMath::IsNearlyEqual(State->Parameters.SliceLength, 200.0)
        && FMath::IsNearlyEqual(State->Parameters.MinimumOverlap, 190.0)
        && FMath::IsNearlyEqual(State->Parameters.MaximumEdgeStep, .01));
    TestTrue(TEXT("Global edits preserve the selected point pose and index"), State->GetPointPose(1).Equals(Edited)
        && State->GetSelectedPointIndex() == 1);

    State->SelectPointInternal(0);
    State->Phase = EParametricRoadPhase::Start;
    State->SetAdjustmentInternal(EParametricRoadAdjustment::Grade, 12);
    TestTrue(TEXT("Shared start edits keep the original automatic arrival coupling"),
        FMath::IsNearlyEqual(State->StartGrade, 12.0) && FMath::IsNearlyEqual(State->EndGrade, 12.0));
    State->SelectPointInternal(State->GetPointCount() - 1);
    State->SetAdjustmentInternal(EParametricRoadAdjustment::Grade, 20);
    State->SelectPointInternal(0);
    State->SetAdjustmentInternal(EParametricRoadAdjustment::Grade, 15);
    TestTrue(TEXT("An explicitly edited arrival survives subsequent start edits"),
        FMath::IsNearlyEqual(State->StartGrade, 15.0) && FMath::IsNearlyEqual(State->EndGrade, 20.0));

    // Supply real transient ownership objects but no player/world. Public UI
    // entry points must still refuse an inactive configuration, not create an
    // alternate route around the native active-build-state contract.
    TStrongObjectPtr<UParametricRoadLayerInput> Input(NewObject<UParametricRoadLayerInput>());
    TStrongObjectPtr<UParametricRoadLayerWidget> Widget(NewObject<UParametricRoadLayerWidget>());
    State->LayerInput = Input.Get(); State->LayerWidget = Widget.Get();
    Input->mInteractObject = State.Get();
    const int32 Selected = State->GetSelectedPointIndex();
    const int32 Adjustment = State->GetAdjustmentIndex();
    const double Grade = State->StartGrade;
    State->SelectPointFromWidget(1);
    State->SelectAdjustmentFromWidget(2);
    State->SetAdjustmentFromWidget(1, 30);
    State->SetFineAdjustmentFromWidget(true);
    TestFalse(TEXT("A worldless ownership fixture is not an active editable game session"), State->CanEditFromWidget());
    TestTrue(TEXT("Widget APIs reject inactive ownership without mutating selection or values"),
        State->GetSelectedPointIndex() == Selected && State->GetAdjustmentIndex() == Adjustment
        && State->StartGrade == Grade && !State->bFineAdjustment);
    State->SetAdjustmentFromWidget(-1, 1);
    State->SetAdjustmentFromWidget(12, 1);
    State->SetAdjustmentFromWidget(1, std::numeric_limits<double>::quiet_NaN());
    State->SetAdjustmentFromWidget(11, .5);
    State->Phase = EParametricRoadPhase::Building;
    State->SelectPointFromWidget(1);
    State->SetAdjustmentFromWidget(1, 10);
    TestTrue(TEXT("Malformed or submitted widget edits cannot mutate the route"), State->GetSelectedPointIndex() == Selected
        && State->StartGrade == Grade && !State->bForwardY);
    TestNull(TEXT("Editing checks create no gameplay world"), State->GetWorld());
    return true;
}
#endif
