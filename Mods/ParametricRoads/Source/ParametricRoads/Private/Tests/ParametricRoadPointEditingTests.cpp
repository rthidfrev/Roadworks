#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "ParametricRoadSession.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParametricRoadPointEditingTest, "ParametricRoads.Editor.PointEditing",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParametricRoadPointEditingTest::RunTest(const FString&)
{
    TStrongObjectPtr<UParametricRoadSession> Session(NewObject<UParametricRoadSession>());
    Session->bAssessed = true;
    Session->Phase = EParametricRoadPhase::Ready;
    Session->EndCenter = FVector(6000, 0, 0);
    Session->CursorCenter = Session->EndCenter;
    Session->bCursorValid = true;
    for (int32 I = 1; I <= 3; ++I)
    {
        FParametricRoadWaypoint Waypoint;
        Waypoint.Point.Pose = FTransform(FRotator(0, 0, 2 * I), FVector(I * 1500, 0, 0));
        Session->IntermediatePoints.Add(Waypoint);
    }
    TestEqual(TEXT("Ribbon includes both endpoints and all fixed points"), Session->GetPointCount(), 5);
    for (int32 I = 0; I < 10; ++I)
    {
        TestEqual(TEXT("Existing point action cycles in path order and wraps"), Session->GetSelectedPointIndex(), I % 5);
        Session->CyclePoint();
    }
    Session->CyclePoint(-1);
    TestEqual(TEXT("Reverse selection wraps to arrival"), Session->GetSelectedPointIndex(), 4);
    Session->SelectPoint(2);
    const FTransform Earlier = Session->IntermediatePoints[0].Point.Pose;
    const FTransform Later = Session->IntermediatePoints[2].Point.Pose;
    const FVector Arrival = Session->EndCenter;
    TestTrue(TEXT("Intermediate heading starts automatic"), Session->IsSelectedPointAutomatic(0));
    TestTrue(TEXT("Intermediate grade starts automatic"), Session->IsSelectedPointAutomatic(1));
    Session->Adjustment = EParametricRoadAdjustment::Heading;
    Session->AdjustValue(1);
    TestFalse(TEXT("Editing heading pins that component"), Session->IsSelectedPointAutomatic(0));
    TestTrue(TEXT("Editing heading retains automatic grade"), Session->IsSelectedPointAutomatic(1));
    TestTrue(TEXT("Selected heading is the value applied to its control pose"),
        FMath::IsNearlyEqual(Session->GetPointPose(2).Rotator().Yaw, 5.0));
    Session->Adjustment = EParametricRoadAdjustment::Grade;
    Session->AdjustValue(1);
    TestTrue(TEXT("Selected grade is applied rather than overwritten by automatic tangents"),
        FMath::IsNearlyEqual(Session->GetPointPose(2).Rotator().Pitch, 1.0));
    Session->Adjustment = EParametricRoadAdjustment::Bank;
    Session->AdjustValue(1);
    TestTrue(TEXT("Bank is local to the selected middle point"),
        FMath::IsNearlyEqual(Session->GetPointPose(2).Rotator().Roll, 5.0));
    Session->Adjustment = EParametricRoadAdjustment::Height;
    Session->AdjustValue(2);
    TestTrue(TEXT("Height is an offset from the captured point, not a new aim position"),
        Session->GetPointPose(2).GetLocation().Equals(FVector(3000, 0, 100)));
    Session->SelectPoint(4); Session->SelectPoint(2);
    TestTrue(TEXT("Reselecting a point retains its height offset"),
        FMath::IsNearlyEqual(Session->GetSelectedAdjustmentNumber(EParametricRoadAdjustment::Height), 1.0));
    TestTrue(TEXT("Earlier fixed points remain untouched"), Session->IntermediatePoints[0].Point.Pose.Equals(Earlier));
    TestTrue(TEXT("Later fixed points remain untouched"), Session->IntermediatePoints[2].Point.Pose.Equals(Later));
    TestTrue(TEXT("Arrival remains fixed while editing an old point"), Session->EndCenter.Equals(Arrival));
    TestEqual(TEXT("Point edits retain confirmation phase"), Session->Phase, EParametricRoadPhase::Ready);

    const auto Edited = Session->IntermediatePoints[1];
    Session->Adjustment = EParametricRoadAdjustment::Overlap;
    Session->AdjustValue(1);
    TestTrue(TEXT("Overlap changes the route setting"), FMath::IsNearlyEqual(Session->Parameters.MinimumOverlap, 25.0));
    TestTrue(TEXT("Global setting edits retain the point pose"), Session->IntermediatePoints[1].Point.Pose.Equals(Edited.Point.Pose));
    TestEqual(TEXT("Global edits retain point selection"), Session->GetSelectedPointIndex(), 2);
    Session->bDirty = false;
    Session->CyclePoint();
    TestFalse(TEXT("Selection alone does not trigger route reconstruction"), Session->bDirty);

    Session->Phase = EParametricRoadPhase::Drawing;
    Session->SelectPoint(1);
    TestTrue(TEXT("Selecting a fixed point preserves the provisional arrival"), Session->CursorCenter.Equals(Arrival));
    Session->Phase = EParametricRoadPhase::Ready;
    Session->AddPoint();
    TestEqual(TEXT("Add point appends the arrival even when an older point was selected"), Session->IntermediatePoints.Num(), 4);
    TestTrue(TEXT("Appended point is the arrival, not the selected old point"), Session->IntermediatePoints.Last().Point.Pose.GetLocation().Equals(Arrival));
    TestEqual(TEXT("New point leaves selection on the continuing end"), Session->GetSelectedPointIndex(), 5);
    Session->SelectPoint(2);
    Session->Secondary();
    TestEqual(TEXT("Undo removes only the last point"), Session->IntermediatePoints.Num(), 3);
    TestTrue(TEXT("Undo retains earlier edited point"), Session->IntermediatePoints[1].Point.Pose.Equals(Edited.Point.Pose));
    TestEqual(TEXT("Undo leaves a valid selection"), Session->GetSelectedPointIndex(), 4);

    Session->SelectPoint(1);
    const FTransform BeforeBuild = Session->GetPointPose(1);
    Session->Phase = EParametricRoadPhase::Building;
    Session->CyclePoint(); Session->AdjustValue(1);
    TestEqual(TEXT("Point selection locks during submitted construction"), Session->GetSelectedPointIndex(), 1);
    TestTrue(TEXT("Submitted point poses cannot be edited"), Session->GetPointPose(1).Equals(BeforeBuild));

    // A just-added point is the preview's end until the live extension moves.
    // Editing that fixed point's height must not invent a return leg to its old
    // captured height, even though its position no longer matches the cursor.
    TStrongObjectPtr<UParametricRoadSession> Prefix(NewObject<UParametricRoadSession>());
    Prefix->bAssessed = true;
    Prefix->Parameters.SliceWidth = 200;
    Prefix->Phase = EParametricRoadPhase::Ready;
    Prefix->EndCenter = FVector(3000, 0, 0);
    Prefix->CursorCenter = FVector(9999, 9999, 0);
    Prefix->bCursorValid = true;
    Prefix->AddPoint();
    TestTrue(TEXT("Appending a confirmed arrival freezes its own anchor rather than the current aim"),
        Prefix->CursorCenter.Equals(FVector(3000, 0, 0)));
    Prefix->SelectPoint(1);
    Prefix->Adjustment = EParametricRoadAdjustment::Height;
    Prefix->AdjustValue(1);
    const FVector RaisedEnd(3000, 0, 50);
    TestTrue(TEXT("Pending prefix survives editing its final fixed point"), Prefix->HasPendingExtension());
    TestTrue(TEXT("The incoming automatic grade responds to the raised point"), Prefix->GetPointPose(1).Rotator().Pitch > 0);
    Prefix->Solve();
    TestEqual(TEXT("Editing the prefix height leaves a solvable preview"), Prefix->Solution.Failure, EParametricRoadFailure::None);
    if (!Prefix->Solution.SegmentPoses.IsEmpty())
    {
        TestTrue(TEXT("The preview ends at the raised fixed point, without returning to its old height"),
            Prefix->Solution.SegmentPoses.Last().GetLocation().Equals(RaisedEnd, 0.000001));
    }
    Prefix->AddPoint();
    TestEqual(TEXT("A pending extension cannot accidentally append the old aim as another point"), Prefix->IntermediatePoints.Num(), 1);
    Prefix->SelectPoint(Prefix->GetPointCount() - 1);
    Prefix->SetParameter(EParametricRoadParameter::EndHeight, 2);
    TestFalse(TEXT("An explicit provisional height edit becomes an actual extension"), Prefix->HasPendingExtension());
    TestNull(TEXT("Point checks do not create a world"), Session->GetWorld());
    return true;
}
#endif
