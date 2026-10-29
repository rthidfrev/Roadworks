#include "ParametricRoadGeometry.h"

namespace
{
    constexpr double MaximumLength = 100000.0;
    constexpr int32 HardSegmentLimit = 512;
    constexpr int32 ArcSampleLimit = 8192;
    // Arc/transport evaluation belongs to the centreline, independently of
    // road slice length, overlap or any layer's station spacing.
    constexpr double MaximumPathChord = 50.0;
    constexpr double JoinTolerance = 0.001;

    struct FCurvePoint
    {
        double T = 0.0;
        double Distance = 0.0;
        FVector Position = FVector::ZeroVector;
        FVector Forward = FVector::ForwardVector;
        FQuat TransportFrame = FQuat::Identity;
    };

    struct FCurve
    {
        FVector Start, End, StartTangent, EndTangent;
        TArray<FCurvePoint> Points;
        double EndTwist = 0.0;

        bool Evaluate(double T, FCurvePoint& Point) const
        {
            const double T2 = T * T;
            const double T3 = T2 * T;
            Point.T = T;
            Point.Position = (2 * T3 - 3 * T2 + 1) * Start
                + (T3 - 2 * T2 + T) * StartTangent
                + (-2 * T3 + 3 * T2) * End + (T3 - T2) * EndTangent;
            const FVector Derivative = (6 * T2 - 6 * T) * Start
                + (3 * T2 - 4 * T + 1) * StartTangent
                + (-6 * T2 + 6 * T) * End + (3 * T2 - 2 * T) * EndTangent;
            if (Derivative.ContainsNaN() || Derivative.SizeSquared() < 0.0001)
            {
                return false;
            }
            Point.Forward = Derivative.GetSafeNormal();
            return !Point.Position.ContainsNaN();
        }

        bool Subdivide(const FCurvePoint& A, const FCurvePoint& B, double MaximumChord, int32 Depth)
        {
            FCurvePoint Middle;
            if (!Evaluate((A.T + B.T) * 0.5, Middle))
            {
                return false;
            }
            const bool bFine = (B.Position - A.Position).Size() <= MaximumChord
                && (Middle.Position - (A.Position + B.Position) * 0.5).Size() <= 0.25
                && FVector::DotProduct(A.Forward, Middle.Forward) >= 0.9998476951563913
                && FVector::DotProduct(Middle.Forward, B.Forward) >= 0.9998476951563913;
            if (bFine)
            {
                if (Points.Num() >= ArcSampleLimit)
                {
                    return false;
                }
                Points.Add(B);
                return true;
            }
            return Depth < 16 && Subdivide(A, Middle, MaximumChord, Depth + 1)
                && Subdivide(Middle, B, MaximumChord, Depth + 1);
        }

        FTransform AtDistance(double Distance) const
        {
            int32 Low = 0, High = Points.Num() - 1;
            while (High - Low > 1)
            {
                const int32 Middle = (Low + High) / 2;
                if (Points[Middle].Distance < Distance)
                {
                    Low = Middle;
                }
                else
                {
                    High = Middle;
                }
            }
            const FCurvePoint& A = Points[Low];
            const FCurvePoint& B = Points[High];
            const double Alpha = FMath::Clamp((Distance - A.Distance) / (B.Distance - A.Distance), 0.0, 1.0);
            FCurvePoint Point;
            Evaluate(FMath::Lerp(A.T, B.T, Alpha), Point);
            FQuat Frame = FQuat::Slerp(A.TransportFrame, B.TransportFrame, Alpha).GetNormalized();
            Frame = (FQuat::FindBetweenNormals(Frame.GetAxisX(), Point.Forward) * Frame).GetNormalized();
            const double Fraction = Distance / Points.Last().Distance;
            const double Smooth = Fraction * Fraction * (3.0 - 2.0 * Fraction);
            Frame = (FQuat(Point.Forward, EndTwist * Smooth) * Frame).GetNormalized();
            return FTransform(Frame, Point.Position);
        }
    };

    bool IsValidPose(const FTransform& Pose)
    {
        return !Pose.ContainsNaN() && Pose.GetRotation().IsNormalized()
            && Pose.GetScale3D().Equals(FVector::OneVector, 0.000001)
            && Pose.GetLocation().GetAbsMax() <= 1000000000.0;
    }

    bool IsValidRequest(const FParametricRoadRequest& R)
    {
        const double Values[] = {R.SliceLength, R.SliceWidth, R.SliceHeight, R.MinimumOverlap,
            R.MaximumEdgeStep, R.TangentScale, R.MaximumGradeDegrees, R.MaximumBankDegrees};
        for (const double Value : Values)
        {
            if (!FMath::IsFinite(Value))
            {
                return false;
            }
        }
        return IsValidPose(R.StartPose) && IsValidPose(R.EndPose)
            && !R.SliceCenter.ContainsNaN() && R.SliceCenter.GetAbsMax() <= 100000.0
            && R.SliceLength >= 1.0 && R.SliceLength <= 100000.0
            && R.SliceWidth >= 1.0 && R.SliceWidth <= 100000.0
            && R.SliceHeight >= 0.1 && R.SliceHeight <= 100000.0
            && R.MinimumOverlap >= 0.0 && R.MinimumOverlap < R.SliceLength
            && R.MaximumEdgeStep >= 0.01 && R.MaximumEdgeStep <= 10000.0
            && R.TangentScale >= 0.05 && R.TangentScale <= 2.0
            && R.MaximumGradeDegrees > 0.0 && R.MaximumGradeDegrees < 89.0
            && R.MaximumBankDegrees >= 0.0 && R.MaximumBankDegrees < 89.0
            && R.MaximumSegments >= 1 && R.MaximumSegments <= HardSegmentLimit;
    }

    EParametricRoadFailure CheckFrame(const FTransform& Frame, const FParametricRoadRequest& R)
    {
        const FVector Forward = Frame.GetRotation().GetAxisX();
        if (FMath::Abs(Forward.Z) > FMath::Sin(FMath::DegreesToRadians(R.MaximumGradeDegrees)) + 0.0000001)
        {
            return EParametricRoadFailure::GradeLimit;
        }
        const FVector LevelUp = (FVector::UpVector - Forward * Forward.Z).GetSafeNormal();
        if (FVector::DotProduct(LevelUp, Frame.GetRotation().GetAxisZ())
            < FMath::Cos(FMath::DegreesToRadians(R.MaximumBankDegrees)) - 0.0000001)
        {
            return EParametricRoadFailure::BankLimit;
        }
        return EParametricRoadFailure::None;
    }

    // Separating-axis test for actual intersection of the two oriented slab boxes.
    // This is a proxy for rectangular road decks, not arbitrary blueprint meshes.
    bool BoxesIntersect(const FTransform& A, const FTransform& B, const FVector& Half)
    {
        const FVector AxesA[] = {A.GetRotation().GetAxisX(), A.GetRotation().GetAxisY(), A.GetRotation().GetAxisZ()};
        const FVector AxesB[] = {B.GetRotation().GetAxisX(), B.GetRotation().GetAxisY(), B.GetRotation().GetAxisZ()};
        const FVector Delta = B.GetLocation() - A.GetLocation();
        auto Separates = [&](const FVector& Axis)
        {
            if (Axis.SizeSquared() < 0.000000000001)
            {
                return false;
            }
            double Radius = 0.0;
            for (int32 I = 0; I < 3; ++I)
            {
                Radius += Half[I] * (FMath::Abs(FVector::DotProduct(Axis, AxesA[I]))
                    + FMath::Abs(FVector::DotProduct(Axis, AxesB[I])));
            }
            return FMath::Abs(FVector::DotProduct(Delta, Axis)) > Radius + JoinTolerance * Axis.Size();
        };
        for (int32 I = 0; I < 3; ++I)
        {
            if (Separates(AxesA[I]) || Separates(AxesB[I]))
            {
                return false;
            }
            for (int32 J = 0; J < 3; ++J)
            {
                if (Separates(FVector::CrossProduct(AxesA[I], AxesB[J])))
                {
                    return false;
                }
            }
        }
        return true;
    }

    bool CheckJoin(const FTransform& A, const FTransform& B, const FParametricRoadRequest& R,
        double& Overlap, double& EdgeStep)
    {
        const FQuat QA = A.GetRotation(), QB = B.GetRotation();
        const FVector XA = QA.GetAxisX(), XB = QB.GetAxisX();
        const FVector YA = QA.GetAxisY(), YB = QB.GetAxisY();
        const FVector ZA = QA.GetAxisZ(), ZB = QB.GetAxisZ();
        // Restrict frame changes before using a common seam section.
        if (FVector::DotProduct(XA, XB) < 0.9659258262890683
            || FVector::DotProduct(ZA, ZB) < 0.9659258262890683)
        {
            return false;
        }
        const FVector SeamForward = (XA + XB).GetSafeNormal();
        const FVector SeamRight = FVector::CrossProduct(ZA + ZB, SeamForward).GetSafeNormal();
        const FVector SeamUp = FVector::CrossProduct(SeamForward, SeamRight);
        const FVector Delta = B.GetLocation() - A.GetLocation();
        const double LargestGap = FVector::DotProduct(Delta - (XA + XB) * (R.SliceLength * 0.5), SeamForward)
            + FMath::Abs(FVector::DotProduct(YB - YA, SeamForward)) * R.SliceWidth * 0.5
            + FMath::Abs(FVector::DotProduct(ZB - ZA, SeamForward)) * R.SliceHeight * 0.5;
        Overlap = -LargestGap;
        EdgeStep = 0.0;
        const FVector Middle = (A.GetLocation() + B.GetLocation()) * 0.5;
        for (const double Side : {-1.0, 1.0})
        {
            const FVector Edge = Middle + SeamRight * (Side * R.SliceWidth * 0.5);
            const double HeightA = FVector::DotProduct(ZA, A.GetLocation() - Edge) / FVector::DotProduct(ZA, SeamUp);
            const double HeightB = FVector::DotProduct(ZB, B.GetLocation() - Edge) / FVector::DotProduct(ZB, SeamUp);
            EdgeStep = FMath::Max(EdgeStep, FMath::Abs(HeightB - HeightA));
        }
        return Overlap + JoinTolerance >= R.MinimumOverlap
            && EdgeStep <= R.MaximumEdgeStep + JoinTolerance
            && BoxesIntersect(A, B, FVector(R.SliceLength, R.SliceWidth, R.SliceHeight) * 0.5);
    }
}

struct FParametricRoadPathData
{
    struct FSpan
    {
        double StartDistance = 0.0;
        FCurve Curve;
    };
    TArray<FSpan> Spans;
    FTransform FirstFrame = FTransform::Identity;
    double Length = 0.0;
};

double FParametricRoadPath::GetLength() const { return Data ? Data->Length : 0.0; }
bool FParametricRoadPath::IsValid() const { return Data.IsValid(); }

namespace
{
FParametricRoadSolution GenerateSpan(const FParametricRoadRequest& R, FCurve* OutCurve = nullptr)
{
    FParametricRoadSolution Result;
    auto Fail = [&](EParametricRoadFailure Failure)
    {
        Result.SegmentPoses.Reset();
        Result.MinimumMeasuredOverlap = Result.MaximumMeasuredEdgeStep = 0.0;
        Result.Failure = Failure;
        return Result;
    };
    if (!IsValidRequest(R))
    {
        return Fail(EParametricRoadFailure::InvalidInput);
    }
    const double Chord = FVector::Distance(R.StartPose.GetLocation(), R.EndPose.GetLocation());
    if (Chord > MaximumLength)
    {
        return Fail(EParametricRoadFailure::RouteTooLong);
    }
    if (Chord < 1.0)
    {
        // A single anchor is a single slice, never a hidden in-place twist.
        if (!R.StartPose.GetLocation().Equals(R.EndPose.GetLocation(), 0.000001)
            || !R.StartPose.GetRotation().Equals(R.EndPose.GetRotation(), 0.000001))
        {
            return Fail(EParametricRoadFailure::DegenerateCurve);
        }
        const EParametricRoadFailure FrameFailure = CheckFrame(R.StartPose, R);
        if (FrameFailure != EParametricRoadFailure::None)
        {
            return Fail(FrameFailure);
        }
        Result.SegmentPoses.Add(FTransform(R.StartPose.GetRotation(), R.StartPose.GetLocation()
            - R.StartPose.GetRotation().RotateVector(R.SliceCenter)));
        return Result;
    }

    FCurve Curve;
    Curve.Start = R.StartPose.GetLocation();
    Curve.End = R.EndPose.GetLocation();
    Curve.StartTangent = R.StartPose.GetRotation().GetAxisX() * (Chord * R.TangentScale);
    Curve.EndTangent = R.EndPose.GetRotation().GetAxisX() * (Chord * R.TangentScale);
    FCurvePoint First, Last;
    if (!Curve.Evaluate(0.0, First) || !Curve.Evaluate(1.0, Last))
    {
        return Fail(EParametricRoadFailure::DegenerateCurve);
    }
    Curve.Points.Add(First);
    if (!Curve.Subdivide(First, Last, MaximumPathChord, 0))
    {
        return Fail(EParametricRoadFailure::DegenerateCurve);
    }

    Curve.Points[0].TransportFrame = R.StartPose.GetRotation();
    for (int32 I = 1; I < Curve.Points.Num(); ++I)
    {
        FCurvePoint& Point = Curve.Points[I];
        const FCurvePoint& Previous = Curve.Points[I - 1];
        Point.Distance = Previous.Distance + FVector::Distance(Previous.Position, Point.Position);
        Point.TransportFrame = (FQuat::FindBetweenNormals(Previous.Forward, Point.Forward)
            * Previous.TransportFrame).GetNormalized();
    }
    Result.Length = Curve.Points.Last().Distance;
    if (Result.Length > MaximumLength)
    {
        return Fail(EParametricRoadFailure::RouteTooLong);
    }
    const FVector EndForward = Last.Forward;
    const FVector TransportUp = Curve.Points.Last().TransportFrame.GetAxisZ();
    const FVector WantedUp = R.EndPose.GetRotation().GetAxisZ();
    Curve.EndTwist = FMath::Atan2(FVector::DotProduct(EndForward, FVector::CrossProduct(TransportUp, WantedUp)),
        FVector::DotProduct(TransportUp, WantedUp));
    for (const FCurvePoint& Point : Curve.Points)
    {
        const EParametricRoadFailure FrameFailure = CheckFrame(Curve.AtDistance(Point.Distance), R);
        if (FrameFailure != EParametricRoadFailure::None)
        {
            return Fail(FrameFailure);
        }
    }

    const double RequiredIntervals = FMath::CeilToDouble(Result.Length / (R.SliceLength - R.MinimumOverlap));
    if (RequiredIntervals >= R.MaximumSegments)
    {
        return Fail(EParametricRoadFailure::SegmentLimit);
    }
    const double MaximumSpacing = R.SliceLength - R.MinimumOverlap;
    TArray<double> Stations;
    TArray<FTransform> Poses;
    const int32 InitialCount = static_cast<int32>(RequiredIntervals) + 1;
    bool bUniformValid = true;
    for (int32 I = 0; I < InitialCount; ++I)
    {
        Stations.Add(Result.Length * I / (InitialCount - 1));
        Poses.Add(Curve.AtDistance(Stations.Last()));
        double Overlap, Step;
        if (I > 0 && !CheckJoin(Poses[I - 1], Poses[I], R, Overlap, Step)) { bUniformValid = false; }
    }
    if (!bUniformValid)
    {
        // Halving a failing interval could place two slices where a slightly
        // shorter step needed only one. Advance close to the largest valid
        // local step instead; no join tolerance is relaxed to save buildings.
        Stations.SetNum(1);
        Poses.SetNum(1);
        while (Stations.Last() < Result.Length)
        {
            if (Stations.Num() >= R.MaximumSegments) { return Fail(EParametricRoadFailure::SegmentLimit); }
            const double Current = Stations.Last();
            const double Target = FMath::Min(Current + MaximumSpacing, Result.Length);
            FTransform Next = Curve.AtDistance(Target);
            double Overlap, Step;
            double Accepted = Target;
            if (!CheckJoin(Poses.Last(), Next, R, Overlap, Step))
            {
                double Low = Current, High = Target;
                for (int32 Search = 0; Search < 24 && High - Low > 0.001; ++Search)
                {
                    const double Middle = (Low + High) * 0.5;
                    const FTransform Candidate = Curve.AtDistance(Middle);
                    if (CheckJoin(Poses.Last(), Candidate, R, Overlap, Step)) { Low = Middle; }
                    else { High = Middle; }
                }
                Accepted = Low;
                if (Accepted - Current < 0.001) { return Fail(EParametricRoadFailure::SegmentLimit); }
                Next = Curve.AtDistance(Accepted);
            }
            const EParametricRoadFailure FrameFailure = CheckFrame(Next, R);
            if (FrameFailure != EParametricRoadFailure::None) { return Fail(FrameFailure); }
            Stations.Add(Accepted);
            Poses.Add(Next);
        }
    }
    // Remove any locally redundant interior pose, including a short final
    // interval, only when both the arc span and the complete seam check pass.
    for (int32 I = 1; I + 1 < Poses.Num();)
    {
        double Overlap, Step;
        if (Stations[I + 1] - Stations[I - 1] <= MaximumSpacing + JoinTolerance
            && CheckJoin(Poses[I - 1], Poses[I + 1], R, Overlap, Step))
        {
            Poses.RemoveAt(I);
            Stations.RemoveAt(I);
            I = FMath::Max(1, I - 1);
        }
        else { ++I; }
    }
    Result.MinimumMeasuredOverlap = TNumericLimits<double>::Max();
    for (int32 I = 0; I < Poses.Num(); ++I)
    {
        const EParametricRoadFailure FrameFailure = CheckFrame(Poses[I], R);
        if (FrameFailure != EParametricRoadFailure::None) { return Fail(FrameFailure); }
        if (I > 0)
        {
            double Overlap, Step;
            if (!CheckJoin(Poses[I - 1], Poses[I], R, Overlap, Step)) { return Fail(EParametricRoadFailure::SegmentLimit); }
            Result.MinimumMeasuredOverlap = FMath::Min(Result.MinimumMeasuredOverlap, Overlap);
            Result.MaximumMeasuredEdgeStep = FMath::Max(Result.MaximumMeasuredEdgeStep, Step);
        }
    }
    for (FTransform& Pose : Poses) { Pose.AddToTranslation(-Pose.GetRotation().RotateVector(R.SliceCenter)); }
    Result.SegmentPoses = MoveTemp(Poses);
    if (OutCurve) { *OutCurve = MoveTemp(Curve); }
    return Result;
}
}

FParametricRoadSolution UParametricRoadGeometry::GenerateRoute(const FParametricRoadRequest& Request)
{
    return GenerateSpan(Request);
}

EParametricRoadFailure UParametricRoadGeometry::ResolveControlPointPoses(
    const FParametricRoadRequest& Request, const TArray<FParametricRoadControlPoint>& ControlPoints,
    TArray<FTransform>& OutAnchors)
{
    OutAnchors.Reset();
    if (!IsValidRequest(Request) || ControlPoints.Num() > 30)
    {
        return EParametricRoadFailure::InvalidInput;
    }
    TArray<FTransform> Anchors;
    Anchors.Reserve(ControlPoints.Num() + 2);
    Anchors.Add(Request.StartPose);
    for (const FParametricRoadControlPoint& Point : ControlPoints)
    {
        if (!IsValidPose(Point.Pose)) { return EParametricRoadFailure::InvalidInput; }
        Anchors.Add(Point.Pose);
    }
    Anchors.Add(Request.EndPose);
    double ChordSum = 0;
    for (int32 I = 1; I < Anchors.Num(); ++I)
    {
        const double Chord = FVector::Distance(Anchors[I - 1].GetLocation(), Anchors[I].GetLocation());
        // With no intermediate point the solver retains its existing single
        // identical-anchor case. Intermediate joints must be distinct.
        if (Chord < 1.0 && (!ControlPoints.IsEmpty()
            || !Request.StartPose.Equals(Request.EndPose, 0.000001)))
        { return EParametricRoadFailure::DegenerateCurve; }
        ChordSum += Chord;
    }
    if (ChordSum > MaximumLength) { return EParametricRoadFailure::RouteTooLong; }
    for (int32 I = 1; I < Anchors.Num() - 1; ++I)
    {
        const FParametricRoadControlPoint& Point = ControlPoints[I - 1];
        if (Point.bManualHeading && Point.bManualGrade) { continue; }
        const FVector Incoming = (Anchors[I].GetLocation() - Anchors[I - 1].GetLocation()).GetSafeNormal();
        const FVector Outgoing = (Anchors[I + 1].GetLocation() - Anchors[I].GetLocation()).GetSafeNormal();
        const FVector Tangent = (Incoming + Outgoing).GetSafeNormal();
        // An automatic component needs a defined bisector. A vertical tangent
        // has a grade but no horizontal heading; never invent one silently.
        if (Tangent.IsNearlyZero() || (!Point.bManualHeading && Tangent.SizeSquared2D() < UE_SMALL_NUMBER))
        { return EParametricRoadFailure::DegenerateCurve; }
        const FRotator Automatic = Tangent.Rotation();
        FRotator Angles = Point.Pose.Rotator();
        if (!Point.bManualHeading) { Angles.Yaw = Automatic.Yaw; }
        if (!Point.bManualGrade) { Angles.Pitch = Automatic.Pitch; }
        Anchors[I].SetRotation(Angles.Quaternion());
    }
    OutAnchors = MoveTemp(Anchors);
    return EParametricRoadFailure::None;
}

FParametricRoadSolution UParametricRoadGeometry::GenerateRouteThroughControlPoints(
    const FParametricRoadRequest& Request, const TArray<FParametricRoadControlPoint>& ControlPoints)
{
    FParametricRoadPath Path;
    return GenerateRouteWithPath(Request, ControlPoints, Path);
}

FParametricRoadSolution UParametricRoadGeometry::GenerateRouteWithPath(
    const FParametricRoadRequest& Request, const TArray<FParametricRoadControlPoint>& ControlPoints,
    FParametricRoadPath& OutPath)
{
    OutPath = FParametricRoadPath();
    auto Fail = [](EParametricRoadFailure Failure)
    {
        FParametricRoadSolution Result;
        Result.Failure = Failure;
        return Result;
    };
    TArray<FTransform> Anchors;
    const EParametricRoadFailure ResolutionFailure = ResolveControlPointPoses(Request, ControlPoints, Anchors);
    if (ResolutionFailure != EParametricRoadFailure::None) { return Fail(ResolutionFailure); }
    TSharedPtr<FParametricRoadPathData> Data = MakeShared<FParametricRoadPathData>();
    Data->FirstFrame = Request.StartPose;
    Data->Spans.Reserve(Anchors.Num() - 1);
    FParametricRoadSolution Result;
    Result.MinimumMeasuredOverlap = TNumericLimits<double>::Max();
    for (int32 I = 1; I < Anchors.Num(); ++I)
    {
        FParametricRoadRequest Span = Request;
        Span.StartPose = Anchors[I - 1];
        Span.EndPose = Anchors[I];
        FCurve Curve;
        const FParametricRoadSolution Part = GenerateSpan(Span, &Curve);
        if (Part.Failure != EParametricRoadFailure::None) { return Fail(Part.Failure); }
        if (Curve.Points.Num() >= 2)
        {
            FParametricRoadPathData::FSpan& Cached = Data->Spans.AddDefaulted_GetRef();
            Cached.StartDistance = Result.Length;
            Cached.Curve = MoveTemp(Curve);
        }
        Result.Length += Part.Length;
        if (Result.Length > MaximumLength) { return Fail(EParametricRoadFailure::RouteTooLong); }
        const int32 First = I == 1 ? 0 : 1;
        if (Result.SegmentPoses.Num() + Part.SegmentPoses.Num() - First > Request.MaximumSegments)
        {
            return Fail(EParametricRoadFailure::SegmentLimit);
        }
        // GenerateRoute pins both endpoints exactly, so the discarded first
        // slice of each following span is identical to the previous last slice.
        for (int32 J = First; J < Part.SegmentPoses.Num(); ++J) { Result.SegmentPoses.Add(Part.SegmentPoses[J]); }
        Result.MinimumMeasuredOverlap = FMath::Min(Result.MinimumMeasuredOverlap, Part.MinimumMeasuredOverlap);
        Result.MaximumMeasuredEdgeStep = FMath::Max(Result.MaximumMeasuredEdgeStep, Part.MaximumMeasuredEdgeStep);
    }
    Data->Length = Result.Length;
    OutPath.Data = MoveTemp(Data);
    return Result;
}

bool UParametricRoadGeometry::SamplePath(const FParametricRoadPath& Path, double Distance, FTransform& OutRoadFrame)
{
    OutRoadFrame = FTransform::Identity;
    if (!Path.Data || !FMath::IsFinite(Distance) || Distance < 0.0 || Distance > Path.Data->Length) { return false; }
    const FParametricRoadPathData& Data = *Path.Data;
    if (Data.Spans.IsEmpty())
    {
        OutRoadFrame = Data.FirstFrame;
        return true;
    }
    int32 Low = 0, High = Data.Spans.Num();
    while (High - Low > 1)
    {
        const int32 Middle = (Low + High) / 2;
        if (Data.Spans[Middle].StartDistance <= Distance) { Low = Middle; }
        else { High = Middle; }
    }
    const FParametricRoadPathData::FSpan& Span = Data.Spans[Low];
    const double LocalDistance = FMath::Clamp(Distance - Span.StartDistance, 0.0, Span.Curve.Points.Last().Distance);
    OutRoadFrame = Span.Curve.AtDistance(LocalDistance);
    return true;
}

EParametricRoadFailure UParametricRoadGeometry::DistributeLayer(const FParametricRoadPath& Path, double SpacingCM,
    double FirstCM, bool bFollowGrade, bool bFollowBank, int32 MaximumPlacements, TArray<FTransform>& OutRoadFrames,
    const FVector& OffsetCentimetres)
{
    OutRoadFrames.Reset();
    if (!Path.IsValid() || !FMath::IsFinite(SpacingCM) || SpacingCM <= 0.0 || !FMath::IsFinite(FirstCM)
        || FirstCM < 0.0 || MaximumPlacements < 0 || MaximumPlacements > HardSegmentLimit
        || OffsetCentimetres.ContainsNaN() || OffsetCentimetres.GetAbsMax() > 100000.0)
    { return EParametricRoadFailure::InvalidInput; }
    const double Length = Path.GetLength();
    if (FirstCM > Length) { return EParametricRoadFailure::None; }
    const double Intervals = (Length - FirstCM) / SpacingCM;
    // Check the count before casting or allocating; tiny positive spacing can
    // overflow the quotient even though both individual inputs are finite.
    if (!FMath::IsFinite(Intervals) || FMath::FloorToDouble(Intervals) + 1.0 > MaximumPlacements)
    { return EParametricRoadFailure::SegmentLimit; }
    const int32 Count = int32(FMath::FloorToDouble(Intervals)) + 1;
    OutRoadFrames.Reserve(Count);
    for (int32 Index = 0; Index < Count; ++Index)
    {
        FTransform Frame;
        // The count defines the station sequence. Clamp only multiplication
        // roundoff at its final station; no extra endpoint is introduced.
        const double Distance = FMath::Min(Length, FirstCM + Index * SpacingCM);
        if (!SamplePath(Path, Distance, Frame))
        {
            OutRoadFrames.Reset();
            return EParametricRoadFailure::InvalidInput;
        }
        // Translation follows the road's full transported frame. Object
        // leveling below changes its orientation, never the offset trajectory.
        if (!OffsetCentimetres.IsZero()) { Frame.AddToTranslation(Frame.TransformVectorNoScale(OffsetCentimetres)); }
        if (!bFollowGrade || !bFollowBank)
        {
            FRotator Angles = Frame.Rotator();
            if (!bFollowGrade) { Angles.Pitch = 0.0; }
            if (!bFollowBank) { Angles.Roll = 0.0; }
            Frame.SetRotation(Angles.Quaternion());
        }
        OutRoadFrames.Add(Frame);
    }
    return EParametricRoadFailure::None;
}

FParametricRoadSolution UParametricRoadGeometry::GenerateRouteThroughPoints(
    const FParametricRoadRequest& Request, const TArray<FTransform>& IntermediatePoints)
{
    if (IntermediatePoints.Num() > 30)
    {
        FParametricRoadSolution Result;
        Result.Failure = EParametricRoadFailure::InvalidInput;
        return Result;
    }
    TArray<FParametricRoadControlPoint> ControlPoints;
    ControlPoints.Reserve(IntermediatePoints.Num());
    for (const FTransform& Pose : IntermediatePoints)
    {
        FParametricRoadControlPoint& Point = ControlPoints.AddDefaulted_GetRef();
        Point.Pose = Pose;
    }
    return GenerateRouteThroughControlPoints(Request, ControlPoints);
}
