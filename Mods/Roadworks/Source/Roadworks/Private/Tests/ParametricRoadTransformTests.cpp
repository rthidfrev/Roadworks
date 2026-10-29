#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "ParametricRoadGeometry.h"
#include "ParametricRoadSession.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParametricRoadTransformTest, "Roadworks.Editor.RouteDirectionTransforms",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParametricRoadTransformTest::RunTest(const FString&)
{
    // Asymmetric geometry and a nonzero root catch accidental centre shifts,
    // reflections and a visual-only 180 degree flip of the first blueprint.
    const FVector LocalCenter(170, -230, 75);
    const FVector Root(1234, -567, 890);
    const FRotator Rotations[] = {FRotator(0, 37, 0), FRotator(13, 47, -18), FRotator(-21, -137, 26)};
    for (bool bForwardY : {false, true})
    {
        for (const FRotator& Rotation : Rotations)
        {
            const FTransform NativePose(Rotation, Root);
            FTransform RoadFrame, RoundTrip;
            if (!TestTrue(TEXT("A rigid native blueprint maps to a travel frame"),
                UParametricRoadGeometry::TryMakeRoadFrame(NativePose, LocalCenter, bForwardY, RoadFrame))) { return false; }
            const FVector NativeForward = NativePose.GetRotation().RotateVector(bForwardY ? FVector::RightVector : FVector::ForwardVector);
            TestTrue(TEXT("Road travels opposite the selected blueprint axis"),
                RoadFrame.GetRotation().GetAxisX().Equals(-NativeForward, 0.000001));
            TestTrue(TEXT("Reversing travel retains deck up without reflection"),
                RoadFrame.GetRotation().GetAxisZ().Equals(NativePose.GetRotation().GetAxisZ(), 0.000001));
            TestTrue(TEXT("Road origin is the actual transformed deck centre"),
                RoadFrame.GetLocation().Equals(NativePose.TransformPosition(LocalCenter), 0.000001));
            TestTrue(TEXT("Travel frame maps back to native placement"),
                UParametricRoadGeometry::TryMakeBlueprintPose(RoadFrame, LocalCenter, bForwardY, RoundTrip));
            TestTrue(TEXT("Both directions round-trip rotation and offset root"), RoundTrip.Equals(NativePose, 0.000001));

            TStrongObjectPtr<UParametricRoadSession> Session(NewObject<UParametricRoadSession>());
            Session->LocalCenter = LocalCenter;
            Session->bForwardY = bForwardY;
            Session->bAssessed = true;
            TestTrue(TEXT("Session uses the same basis for its initial angles"), Session->InitializeRoadOrientation(NativePose));
            const FTransform FirstSlice = Session->ToBlueprintPose(Session->RoadPoseAt(RoadFrame.GetLocation(), true));
            TestTrue(TEXT("Entering road mode retains the first slice's native pose"), FirstSlice.Equals(NativePose, 0.000001));
            TestTrue(TEXT("Default end orientation follows the same travel direction"),
                Session->RoadPoseAt(RoadFrame.GetLocation(), false).GetRotation().Equals(RoadFrame.GetRotation(), 0.000001));
            Session->SetParameter(EParametricRoadParameter::EndHeading, Session->EndHeading + 11);
            Session->SetParameter(EParametricRoadParameter::EndGrade, -9);
            Session->SetParameter(EParametricRoadParameter::EndBank, 17);
            Session->SetParameter(EParametricRoadParameter::EndHeight, 2);
            const FTransform NativeEnd = Session->ToBlueprintPose(Session->RoadPoseAt(RoadFrame.GetLocation(), false));
            const FTransform FixedPoint(FRotator(3, 40, 7), FVector(300, 400, 500));
            FParametricRoadWaypoint FixedWaypoint;
            FixedWaypoint.Point.Pose = FixedPoint;
            Session->IntermediatePoints.Add(FixedWaypoint);
            const bool bExplicitEnd = Session->bEndExplicit;
            Session->ToggleForwardAxis();
            TestTrue(TEXT("Axis changes retain the tilted start blueprint pose"),
                Session->ToBlueprintPose(Session->RoadPoseAt(RoadFrame.GetLocation(), true)).Equals(NativePose, 0.000001));
            TestTrue(TEXT("Axis changes retain the independent tilted end blueprint pose and height"),
                Session->ToBlueprintPose(Session->RoadPoseAt(RoadFrame.GetLocation(), false)).Equals(NativeEnd, 0.000001));
            TestEqual(TEXT("Axis changes retain the explicit end setting"), Session->bEndExplicit, bExplicitEnd);
            TestTrue(TEXT("Axis changes leave fixed waypoint state untouched"),
                Session->IntermediatePoints.Num() == 1 && Session->IntermediatePoints[0].Point.Pose.Equals(FixedPoint, 0.000001));
            Session->ToggleForwardAxis();
            TestTrue(TEXT("Tilted start returns without rotation or centre drift after two axis changes"),
                Session->ToBlueprintPose(Session->RoadPoseAt(RoadFrame.GetLocation(), true)).Equals(NativePose, 0.000001));
            TestTrue(TEXT("Tilted end returns without rotation or centre drift after two axis changes"),
                Session->ToBlueprintPose(Session->RoadPoseAt(RoadFrame.GetLocation(), false)).Equals(NativeEnd, 0.000001));
            TestNull(TEXT("Orientation checks do not create a world"), Session->GetWorld());
        }

        TStrongObjectPtr<UParametricRoadSession> Session(NewObject<UParametricRoadSession>());
        Session->LocalCenter = LocalCenter;
        Session->bForwardY = bForwardY;
        Session->bAssessed = true;
        const FTransform NativeStart(FRotator(0, 37, 0), Root);
        FTransform StartFrame;
        UParametricRoadGeometry::TryMakeRoadFrame(NativeStart, LocalCenter, bForwardY, StartFrame);
        Session->InitializeRoadOrientation(NativeStart);
        Session->SetParameter(EParametricRoadParameter::StartGrade, 15);
        Session->SetParameter(EParametricRoadParameter::StartBank, 20);
        const FTransform SlopedRoad = Session->RoadPoseAt(StartFrame.GetLocation(), true);
        TestTrue(TEXT("Positive grade rises along the new direction of travel"), SlopedRoad.GetRotation().GetAxisX().Z > 0);
        TestTrue(TEXT("Positive bank retains the road frame's right-edge-down convention"), SlopedRoad.GetRotation().GetAxisY().Z < 0);
        FTransform SlopedRoundTrip;
        UParametricRoadGeometry::TryMakeRoadFrame(Session->ToBlueprintPose(SlopedRoad), LocalCenter, bForwardY, SlopedRoundTrip);
        TestTrue(TEXT("Pitch and bank edits survive native placement conversion"), SlopedRoundTrip.Equals(SlopedRoad, 0.000001));

        Session->InitializeRoadOrientation(NativeStart);
        Session->ToggleForwardAxis();
        TestTrue(TEXT("Changing the horizontal slice axis retains the initial native yaw and centre"),
            Session->ToBlueprintPose(Session->RoadPoseAt(StartFrame.GetLocation(), true)).Equals(NativeStart, 0.000001));
        Session->ToggleForwardAxis();
        TestTrue(TEXT("Changing axes back does not accumulate a half turn"),
            Session->ToBlueprintPose(Session->RoadPoseAt(StartFrame.GetLocation(), true)).Equals(NativeStart, 0.000001));

        FParametricRoadRequest Request;
        Request.StartPose = StartFrame;
        Request.EndPose = StartFrame;
        const FVector Travel = StartFrame.GetRotation().GetAxisX();
        Request.EndPose.AddToTranslation(Travel * 6000);
        FTransform Intermediate = StartFrame;
        Intermediate.AddToTranslation(Travel * 3000);
        const FParametricRoadSolution Solution = UParametricRoadGeometry::GenerateRouteThroughPoints(Request, {Intermediate});
        TestEqual(TEXT("An outward route with an intermediate point is solvable"), Solution.Failure, EParametricRoadFailure::None);
        if (TestTrue(TEXT("The outward route contains slices"), !Solution.SegmentPoses.IsEmpty()))
        {
            FTransform NativeFirst;
            UParametricRoadGeometry::TryMakeBlueprintPose(Solution.SegmentPoses[0], LocalCenter, bForwardY, NativeFirst);
            TestTrue(TEXT("Waypoint solving preserves the first blueprint orientation"), NativeFirst.Equals(NativeStart, 0.000001));
            TestTrue(TEXT("Waypoint route ends along reversed travel rather than behind the start"),
                Solution.SegmentPoses.Last().GetLocation().Equals(Request.EndPose.GetLocation(), 0.001));
            for (const FTransform& Pose : Solution.SegmentPoses)
            {
                TestTrue(TEXT("Every slice keeps a consistent route tangent through the waypoint"),
                    Pose.GetRotation().GetAxisX().Equals(Travel, 0.000001));
            }
        }
    }
    FTransform InvalidResult;
    const FTransform Scaled(FQuat::Identity, Root, FVector(2, 1, 1));
    TestFalse(TEXT("Nonrigid input cannot silently change blueprint scale"),
        UParametricRoadGeometry::TryMakeRoadFrame(Scaled, LocalCenter, false, InvalidResult));
    return !HasAnyErrors();
}

#endif
