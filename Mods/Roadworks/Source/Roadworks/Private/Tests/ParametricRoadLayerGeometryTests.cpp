#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "ParametricRoadGeometry.h"
#include "Misc/AutomationTest.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParametricRoadLayerGeometryTest, "Roadworks.Editor.BlueprintLayerGeometry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParametricRoadLayerGeometryTest::RunTest(const FString&)
{
    FParametricRoadRequest Graded;
    const FVector Origin(1234, -567, 890);
    const FRotator Orientation(30, 0, 20);
    const FVector Forward = Orientation.Vector();
    Graded.StartPose = FTransform(Orientation, Origin);
    Graded.EndPose = FTransform(Orientation, Origin + Forward * 10000);
    Graded.SliceWidth = 200;
    Graded.SliceCenter = FVector(110, -50, 80);
    FParametricRoadPath Path;
    const FParametricRoadSolution Road = UParametricRoadGeometry::GenerateRouteWithPath(Graded, {}, Path);
    if (!TestEqual(TEXT("Analytic graded road solves with a retained path"), Road.Failure, EParametricRoadFailure::None)) { return false; }
    TestTrue(TEXT("Successful road retains its evaluated path"), Path.IsValid());
    TestTrue(TEXT("Path length is 3D centreline distance"), FMath::IsNearlyEqual(Path.GetLength(), 10000.0, 0.000001));
    TestTrue(TEXT("Road and path report the same cumulative length"), FMath::IsNearlyEqual(Path.GetLength(), Road.Length, 0.000001));
    FTransform Sample;
    TestTrue(TEXT("The cached path supports interior sampling"), UParametricRoadGeometry::SamplePath(Path, 2500, Sample));
    TestTrue(TEXT("Grade sampling uses arc distance rather than horizontal distance"), Sample.GetLocation().Equals(Origin + Forward * 2500, 0.000001));
    TestTrue(TEXT("Transported straight grade/bank orientation is preserved"), Sample.GetRotation().Equals(Orientation.Quaternion(), 0.000001));
    UParametricRoadGeometry::SamplePath(Path, 0, Sample);
    TestTrue(TEXT("Path stations use the deck centre before blueprint-origin offsets"), Sample.Equals(Graded.StartPose, 0.000001));
    TestFalse(TEXT("A nonzero SliceCenter does not recenter decoration stations"), Sample.GetLocation().Equals(Road.SegmentPoses[0].GetLocation(), 0.000001));

    TArray<FTransform> Frames;
    for (bool bGrade : {false, true})
    {
        for (bool bBank : {false, true})
        {
            TestEqual(TEXT("Layer grade and bank options distribute independently"),
                UParametricRoadGeometry::DistributeLayer(Path, 1200, 600, bGrade, bBank, 512, Frames), EParametricRoadFailure::None);
            TestEqual(TEXT("12 m spacing from 6 m gives eight placements on 100 m"), Frames.Num(), 8);
            const FQuat Expected = FRotator(bGrade ? 30 : 0, 0, bBank ? 20 : 0).Quaternion();
            for (int32 I = 0; I < Frames.Num(); ++I)
            {
                TestTrue(TEXT("Layer stations follow the same 3D centreline regardless of orientation flags"),
                    Frames[I].GetLocation().Equals(Origin + Forward * (600 + 1200 * I), 0.000001));
                TestTrue(TEXT("Heading stays on the route and only requested angles follow it"), Frames[I].GetRotation().Equals(Expected, 0.000001));
            }
            TestTrue(TEXT("An off-sequence final endpoint is never forced"), Frames.Last().GetLocation().Equals(Origin + Forward * 9000, 0.000001));
        }
    }

    // A join at 35 m deliberately falls between the global 30 m/42 m stations.
    FParametricRoadRequest Joined;
    Joined.EndPose.SetLocation(FVector(10000, 0, 0));
    Joined.SliceWidth = 200;
    FParametricRoadControlPoint Point;
    Point.Pose.SetLocation(FVector(3500, 0, 0));
    FParametricRoadPath JoinedPath;
    const FParametricRoadSolution JoinedRoad = UParametricRoadGeometry::GenerateRouteWithPath(Joined, {Point}, JoinedPath);
    if (!TestEqual(TEXT("A nonuniform multipoint path is retained"), JoinedRoad.Failure, EParametricRoadFailure::None)) { return false; }
    TestEqual(TEXT("A layer carries one station sequence through point joins"),
        UParametricRoadGeometry::DistributeLayer(JoinedPath, 1200, 600, true, true, 512, Frames), EParametricRoadFailure::None);
    TestEqual(TEXT("Intermediate points do not restart the placement phase"), Frames.Num(), 8);
    for (int32 I = 0; I < Frames.Num(); ++I)
    {
        TestTrue(TEXT("Multipoint stations retain the analytic global phase"), Frames[I].GetLocation().Equals(FVector(600 + 1200 * I, 0, 0), 0.000001));
    }
    TestEqual(TEXT("A station exactly on a join is emitted once"),
        UParametricRoadGeometry::DistributeLayer(JoinedPath, 5000, 3500, true, true, 512, Frames), EParametricRoadFailure::None);
    TestEqual(TEXT("Join distribution contains only its two scheduled stations"), Frames.Num(), 2);
    TestTrue(TEXT("The join's first station is not shifted"), Frames[0].GetLocation().Equals(Point.Pose.GetLocation(), 0.000001));
    TestTrue(TEXT("The following station uses remaining global distance"), Frames[1].GetLocation().Equals(FVector(8500, 0, 0), 0.000001));

    Point.Pose = FTransform(FRotator(8, 20, 5), FVector(5000, 0, 0));
    Point.bManualHeading = Point.bManualGrade = true;
    FParametricRoadPath ManualPath;
    const FParametricRoadSolution ManualRoad = UParametricRoadGeometry::GenerateRouteWithPath(Joined, {Point}, ManualPath);
    TestEqual(TEXT("A manual 3D joint retains its sampled path"), ManualRoad.Failure, EParametricRoadFailure::None);
    FParametricRoadRequest Prefix = Joined;
    Prefix.EndPose = Point.Pose;
    const FParametricRoadSolution PrefixRoad = UParametricRoadGeometry::GenerateRoute(Prefix);
    if (ManualRoad.Failure == EParametricRoadFailure::None && PrefixRoad.Failure == EParametricRoadFailure::None)
    {
        UParametricRoadGeometry::SamplePath(ManualPath, PrefixRoad.Length, Sample);
        TestTrue(TEXT("Cached spans share the authored full joint frame"), Sample.Equals(Point.Pose, 0.000001));
        FTransform Before, After;
        UParametricRoadGeometry::SamplePath(ManualPath, PrefixRoad.Length - 0.01, Before);
        UParametricRoadGeometry::SamplePath(ManualPath, PrefixRoad.Length + 0.01, After);
        TestTrue(TEXT("Path positions remain continuous across the cached span boundary"), FVector::Distance(Before.GetLocation(), After.GetLocation()) < 0.021);
        TestTrue(TEXT("Heading/grade/bank remain continuous across the cached span boundary"), Before.GetRotation().Equals(After.GetRotation(), 0.0001));
    }

    // Curvature makes arc-table dependence on road slice dimensions observable.
    FParametricRoadRequest Curved;
    Curved.StartPose = FTransform(FRotator(10, 0, 0), FVector::ZeroVector);
    Curved.EndPose = FTransform(FRotator(10, 90, 10), FVector(4000, 4000, 1000));
    Curved.SliceLength = 400; Curved.SliceWidth = 50; Curved.SliceHeight = 50;
    Curved.MaximumSegments = 512;
    FParametricRoadPath Sparse, Dense;
    const FParametricRoadSolution SparseRoad = UParametricRoadGeometry::GenerateRouteWithPath(Curved, {}, Sparse);
    Curved.SliceLength = 200; Curved.MinimumOverlap = 100;
    const FParametricRoadSolution DenseRoad = UParametricRoadGeometry::GenerateRouteWithPath(Curved, {}, Dense);
    TestEqual(TEXT("Sparse curved road solves"), SparseRoad.Failure, EParametricRoadFailure::None);
    TestEqual(TEXT("Dense overlapped curved road solves"), DenseRoad.Failure, EParametricRoadFailure::None);
    if (Sparse.IsValid() && Dense.IsValid())
    {
        TestTrue(TEXT("Changing road overlap and slice length changes road work"), SparseRoad.SegmentPoses.Num() != DenseRoad.SegmentPoses.Num());
        TestTrue(TEXT("Road density does not change the evaluated path length"), FMath::IsNearlyEqual(Sparse.GetLength(), Dense.GetLength(), 0.0000001));
        for (double Fraction : {0.13, 0.5, 0.91})
        {
            FTransform A, B;
            UParametricRoadGeometry::SamplePath(Sparse, Sparse.GetLength() * Fraction, A);
            UParametricRoadGeometry::SamplePath(Dense, Sparse.GetLength() * Fraction, B);
            TestTrue(TEXT("Road density does not move or rotate decoration stations"), A.Equals(B, 0.000001));
        }
    }

    TestEqual(TEXT("An initial distance beyond the route legitimately produces zero placements"),
        UParametricRoadGeometry::DistributeLayer(Path, 1200, 10001, true, true, 0, Frames), EParametricRoadFailure::None);
    TestTrue(TEXT("Zero placements clear previous output"), Frames.IsEmpty());
    auto RejectLayer = [&](double Spacing, double First, int32 Budget, EParametricRoadFailure Expected)
    {
        Frames = {FTransform::Identity};
        TestEqual(TEXT("Invalid or over-budget layer is refused before placement work"),
            UParametricRoadGeometry::DistributeLayer(Path, Spacing, First, true, true, Budget, Frames), Expected);
        TestTrue(TEXT("A refused layer returns no partial placements"), Frames.IsEmpty());
    };
    RejectLayer(0, 600, 512, EParametricRoadFailure::InvalidInput);
    RejectLayer(-1, 600, 512, EParametricRoadFailure::InvalidInput);
    RejectLayer(std::numeric_limits<double>::quiet_NaN(), 600, 512, EParametricRoadFailure::InvalidInput);
    RejectLayer(1200, -1, 512, EParametricRoadFailure::InvalidInput);
    RejectLayer(1200, std::numeric_limits<double>::infinity(), 512, EParametricRoadFailure::InvalidInput);
    RejectLayer(1200, 600, -1, EParametricRoadFailure::InvalidInput);
    RejectLayer(1200, 600, 513, EParametricRoadFailure::InvalidInput);
    RejectLayer(1200, 600, 7, EParametricRoadFailure::SegmentLimit);
    RejectLayer(1200, 600, 0, EParametricRoadFailure::SegmentLimit);
    RejectLayer(std::numeric_limits<double>::min(), 0, 512, EParametricRoadFailure::SegmentLimit);
    TestFalse(TEXT("Negative sample distances are refused"), UParametricRoadGeometry::SamplePath(Path, -1, Sample));
    TestFalse(TEXT("Sample distances beyond the end are refused"), UParametricRoadGeometry::SamplePath(Path, Path.GetLength() + 1, Sample));
    TestFalse(TEXT("Nonfinite sample distances are refused"), UParametricRoadGeometry::SamplePath(Path, std::numeric_limits<double>::quiet_NaN(), Sample));
    FParametricRoadPath Empty;
    TestFalse(TEXT("An unevaluated path is not sampleable"), UParametricRoadGeometry::SamplePath(Empty, 0, Sample));
    TestEqual(TEXT("An unevaluated path cannot distribute a layer"),
        UParametricRoadGeometry::DistributeLayer(Empty, 1200, 600, true, true, 512, Frames), EParametricRoadFailure::InvalidInput);
    FParametricRoadPath Single;
    UParametricRoadGeometry::GenerateRouteWithPath(FParametricRoadRequest(), {}, Single);
    TestTrue(TEXT("The single-slice road also has a valid zero-length path"), Single.IsValid() && Single.GetLength() == 0);
    TestEqual(TEXT("Distance zero can place once on a single-slice road"),
        UParametricRoadGeometry::DistributeLayer(Single, 1200, 0, true, true, 1, Frames), EParametricRoadFailure::None);
    TestEqual(TEXT("Zero-length path emits exactly one scheduled placement"), Frames.Num(), 1);

    const FParametricRoadPath Snapshot = Path;
    Graded.SliceLength = 0;
    const FParametricRoadSolution Failed = UParametricRoadGeometry::GenerateRouteWithPath(Graded, {}, Path);
    TestEqual(TEXT("Invalid road re-solve is refused"), Failed.Failure, EParametricRoadFailure::InvalidInput);
    TestFalse(TEXT("Failed solve clears its output path"), Path.IsValid());
    TestTrue(TEXT("An already shared immutable snapshot retains its evaluated data"), Snapshot.IsValid() && FMath::IsNearlyEqual(Snapshot.GetLength(), 10000.0, 0.000001));
    return !HasAnyErrors();
}

#endif
