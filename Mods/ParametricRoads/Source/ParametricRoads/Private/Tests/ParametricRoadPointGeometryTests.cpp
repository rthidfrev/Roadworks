#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "ParametricRoadGeometry.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParametricRoadPointGeometryTest, "ParametricRoads.Editor.EditablePointGeometry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParametricRoadPointGeometryTest::RunTest(const FString&)
{
    auto ControlPoint = [](const FTransform& Pose, bool bHeading = false, bool bGrade = false)
    {
        FParametricRoadControlPoint Point;
        Point.Pose = Pose;
        Point.bManualHeading = bHeading;
        Point.bManualGrade = bGrade;
        return Point;
    };
    auto AngleMatches = [](double Actual, double Expected)
    {
        return FMath::Abs(FRotator::NormalizeAxis(Actual - Expected)) < 0.000001;
    };
    FParametricRoadRequest Request;
    Request.EndPose.SetLocation(FVector(2000, 1000, 600));
    const FTransform StoredPose(FRotator(-11, 137, 9), FVector(1000, 0, 200));
    const FVector Incoming = FVector(1000, 0, 200).GetSafeNormal();
    const FVector Outgoing = FVector(1000, 1000, 400).GetSafeNormal();
    const FRotator Bisector = (Incoming + Outgoing).GetSafeNormal().Rotation();
    TArray<FTransform> Anchors;
    for (bool bManualHeading : {false, true})
    {
        for (bool bManualGrade : {false, true})
        {
            const TArray<FParametricRoadControlPoint> Points = {ControlPoint(StoredPose, bManualHeading, bManualGrade)};
            TestEqual(TEXT("Independent heading/grade modes resolve"),
                UParametricRoadGeometry::ResolveControlPointPoses(Request, Points, Anchors), EParametricRoadFailure::None);
            if (!TestEqual(TEXT("Resolved anchors include both endpoints and the point"), Anchors.Num(), 3)) { return false; }
            TestTrue(TEXT("Request start remains unchanged"), Anchors[0].Equals(Request.StartPose, 0.000001));
            TestTrue(TEXT("Request end remains unchanged"), Anchors.Last().Equals(Request.EndPose, 0.000001));
            TestTrue(TEXT("Point centre and its edited height are retained"), Anchors[1].GetLocation().Equals(StoredPose.GetLocation(), 0.000001));
            const FRotator Resolved = Anchors[1].Rotator();
            TestTrue(TEXT("Manual heading is honored independently of automatic grade"),
                AngleMatches(Resolved.Yaw, bManualHeading ? 137 : Bisector.Yaw));
            TestTrue(TEXT("Manual grade is honored independently of automatic heading"),
                AngleMatches(Resolved.Pitch, bManualGrade ? -11 : Bisector.Pitch));
            TestTrue(TEXT("Bank is retained in all tangent modes"), AngleMatches(Resolved.Roll, 9));
            TestTrue(TEXT("Resolution never overwrites the stored point"), Points[0].Pose.Equals(StoredPose, 0.000001));
            TestEqual(TEXT("Resolution preserves the manual heading flag"), Points[0].bManualHeading, bManualHeading);
            TestEqual(TEXT("Resolution preserves the manual grade flag"), Points[0].bManualGrade, bManualGrade);
        }
    }

    FParametricRoadControlPoint Raised = ControlPoint(StoredPose);
    Raised.Pose.AddToTranslation(FVector(0, 0, 400));
    TestEqual(TEXT("Changing point height recomputes automatic components"),
        UParametricRoadGeometry::ResolveControlPointPoses(Request, {Raised}, Anchors), EParametricRoadFailure::None);
    TestFalse(TEXT("Automatic grade responds to the new height"), AngleMatches(Anchors[1].Rotator().Pitch, Bisector.Pitch));
    Raised.bManualGrade = true;
    TestEqual(TEXT("A manual grade can be retained while height changes"),
        UParametricRoadGeometry::ResolveControlPointPoses(Request, {Raised}, Anchors), EParametricRoadFailure::None);
    TestTrue(TEXT("Height changes do not replace manual grade"), AngleMatches(Anchors[1].Rotator().Pitch, -11));

    // A shared nonzero heading/grade/bank must be used by both adjoining spans,
    // then appear once in the complete route rather than as two joint slices.
    FParametricRoadRequest Joined;
    Joined.EndPose.SetLocation(FVector(10000, 0, 0));
    Joined.SliceLength = 200;
    Joined.SliceWidth = 200;
    const FTransform ManualJoint(FRotator(8, 20, 5), FVector(5000, 0, 0));
    const TArray<FParametricRoadControlPoint> ManualPoints = {ControlPoint(ManualJoint, true, true)};
    TestEqual(TEXT("Manual joint frames resolve"),
        UParametricRoadGeometry::ResolveControlPointPoses(Joined, ManualPoints, Anchors), EParametricRoadFailure::None);
    TestTrue(TEXT("A fully manual joint retains its complete frame"), Anchors[1].Equals(ManualJoint, 0.000001));
    FParametricRoadRequest FirstSpan = Joined, LastSpan = Joined;
    FirstSpan.EndPose = Anchors[1]; LastSpan.StartPose = Anchors[1];
    const FParametricRoadSolution First = UParametricRoadGeometry::GenerateRoute(FirstSpan);
    const FParametricRoadSolution Last = UParametricRoadGeometry::GenerateRoute(LastSpan);
    const FParametricRoadSolution Complete = UParametricRoadGeometry::GenerateRouteThroughControlPoints(Joined, ManualPoints);
    TestEqual(TEXT("Incoming manual span is solvable"), First.Failure, EParametricRoadFailure::None);
    TestEqual(TEXT("Outgoing manual span is solvable"), Last.Failure, EParametricRoadFailure::None);
    TestEqual(TEXT("Manual joint route is solvable"), Complete.Failure, EParametricRoadFailure::None);
    if (First.Failure == EParametricRoadFailure::None && Last.Failure == EParametricRoadFailure::None
        && Complete.Failure == EParametricRoadFailure::None)
    {
        TestTrue(TEXT("The shared joint has no orientation discontinuity"), First.SegmentPoses.Last().Equals(Last.SegmentPoses[0], 0.000001));
        TestTrue(TEXT("Heading, grade and bank reach the authored joint exactly"), First.SegmentPoses.Last().Equals(ManualJoint, 0.000001));
        int32 JointCount = 0;
        for (const FTransform& Pose : Complete.SegmentPoses)
        {
            if (Pose.GetLocation().Equals(ManualJoint.GetLocation(), 0.000001))
            {
                ++JointCount;
                TestTrue(TEXT("The emitted joint retains all manual rotation components"), Pose.GetRotation().Equals(ManualJoint.GetRotation(), 0.000001));
            }
        }
        TestEqual(TEXT("A shared joint emits one slice"), JointCount, 1);
        TestEqual(TEXT("Only the duplicate shared slice is omitted"), Complete.SegmentPoses.Num(), First.SegmentPoses.Num() + Last.SegmentPoses.Num() - 1);
    }

    const FTransform AutoPose(FRotator(0, 123, 0), FVector(5000, 0, 0));
    const FParametricRoadSolution Legacy = UParametricRoadGeometry::GenerateRouteThroughPoints(Joined, {AutoPose});
    const FParametricRoadSolution Automatic = UParametricRoadGeometry::GenerateRouteThroughControlPoints(Joined, {ControlPoint(AutoPose)});
    TestEqual(TEXT("Legacy automatic point API remains solvable"), Legacy.Failure, EParametricRoadFailure::None);
    TestEqual(TEXT("Legacy and editable automatic routes have the same failure"), Automatic.Failure, Legacy.Failure);
    TestEqual(TEXT("Legacy and editable automatic routes have the same slice count"), Automatic.SegmentPoses.Num(), Legacy.SegmentPoses.Num());
    TestTrue(TEXT("Legacy automatic path length is unchanged"), FMath::IsNearlyEqual(Automatic.Length, Legacy.Length, 0.000001));
    for (int32 I = 0; I < FMath::Min(Automatic.SegmentPoses.Num(), Legacy.SegmentPoses.Num()); ++I)
    {
        TestTrue(TEXT("Legacy automatic slice poses are unchanged"), Automatic.SegmentPoses[I].Equals(Legacy.SegmentPoses[I], 0.000001));
    }

    auto Reject = [&](const TCHAR* Label, const FParametricRoadRequest& InvalidRequest,
        const TArray<FParametricRoadControlPoint>& Points, EParametricRoadFailure Expected)
    {
        Anchors = {FTransform::Identity};
        TestEqual(Label, UParametricRoadGeometry::ResolveControlPointPoses(InvalidRequest, Points, Anchors), Expected);
        TestTrue(TEXT("Failed resolution returns no partial anchors"), Anchors.IsEmpty());
        const FParametricRoadSolution Failed = UParametricRoadGeometry::GenerateRouteThroughControlPoints(InvalidRequest, Points);
        TestEqual(TEXT("Generation propagates point resolution refusal"), Failed.Failure, Expected);
        TestTrue(TEXT("A refused route emits no partial slices"), Failed.SegmentPoses.IsEmpty());
    };
    Reject(TEXT("An intermediate point cannot duplicate the start"), Joined,
        {ControlPoint(Joined.StartPose)}, EParametricRoadFailure::DegenerateCurve);
    Reject(TEXT("Adjacent intermediate points must be distinct"), Joined,
        {ControlPoint(AutoPose), ControlPoint(AutoPose)}, EParametricRoadFailure::DegenerateCurve);
    FParametricRoadControlPoint Scaled = ControlPoint(AutoPose);
    Scaled.Pose.SetScale3D(FVector(2, 1, 1));
    Reject(TEXT("Intermediate scale must be rigid"), Joined, {Scaled}, EParametricRoadFailure::InvalidInput);
    FParametricRoadControlPoint Outside = ControlPoint(AutoPose);
    Outside.Pose.SetLocation(FVector(1000000001.0, 0, 0));
    Reject(TEXT("Intermediate positions respect solver bounds"), Joined, {Outside}, EParametricRoadFailure::InvalidInput);
    TArray<FParametricRoadControlPoint> TooMany;
    TooMany.Init(ControlPoint(AutoPose), 31);
    Reject(TEXT("At most 30 intermediate points can be resolved"), Joined, TooMany, EParametricRoadFailure::InvalidInput);
    FParametricRoadRequest Long = Joined;
    Long.EndPose.SetLocation(FVector(60000, 0, 0));
    Reject(TEXT("The full chord length has a shared 1 km budget"), Long,
        {ControlPoint(FTransform(FVector(-60000, 0, 0)))}, EParametricRoadFailure::RouteTooLong);
    FParametricRoadRequest Reversal = Joined;
    Reversal.EndPose = Reversal.StartPose;
    Reject(TEXT("An automatic reversal has no unique joint tangent"), Reversal,
        {ControlPoint(AutoPose)}, EParametricRoadFailure::DegenerateCurve);
    TestEqual(TEXT("A fully manual point does not require an automatic bisector"),
        UParametricRoadGeometry::ResolveControlPointPoses(Reversal, {ControlPoint(AutoPose, true, true)}, Anchors), EParametricRoadFailure::None);
    FParametricRoadRequest Vertical = Joined;
    Vertical.EndPose.SetLocation(FVector(0, 0, 2000));
    Reject(TEXT("An automatic heading is not invented for a vertical bisector"), Vertical,
        {ControlPoint(FTransform(FVector(0, 0, 1000)))}, EParametricRoadFailure::DegenerateCurve);
    const FParametricRoadSolution Single = UParametricRoadGeometry::GenerateRouteThroughControlPoints(FParametricRoadRequest(), {});
    TestEqual(TEXT("No-point identical anchors retain the existing single-slice case"), Single.SegmentPoses.Num(), 1);
    return !HasAnyErrors();
}

#endif
