#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "Templates/SharedPointer.h"
#include "ParametricRoadGeometry.generated.h"

/** All distances are centimetres. Anchors locate the centres of the first/last slice. */
USTRUCT(BlueprintType)
struct ROADWORKS_API FParametricRoadRequest
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road")
    FTransform StartPose = FTransform::Identity;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road")
    FTransform EndPose = FTransform::Identity;

    /** Blueprint-local centre of the rectangular road slab, before orientation. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road")
    FVector SliceCenter = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road")
    double SliceLength = 200.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road")
    double SliceWidth = 1600.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road")
    double SliceHeight = 100.0;

    /** Minimum longitudinal overlap, evaluated at all four slab corners. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road")
    double MinimumOverlap = 20.0;

    /** Maximum height discontinuity of the slab's centre plane at either edge. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road")
    double MaximumEdgeStep = 5.0;

    /** Endpoint tangent length / distance between anchors, for the cubic Hermite curve. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road")
    double TangentScale = 1.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road")
    double MaximumGradeDegrees = 45.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road")
    double MaximumBankDegrees = 60.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road")
    int32 MaximumSegments = 256;
};

/** An intermediate deck centre with independently automatic or manual tangent
 * components. Roll/bank always comes from Pose; positions are never moved. */
USTRUCT(BlueprintType)
struct ROADWORKS_API FParametricRoadControlPoint
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road")
    FTransform Pose = FTransform::Identity;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road")
    bool bManualHeading = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road")
    bool bManualGrade = false;
};

UENUM(BlueprintType)
enum class EParametricRoadFailure : uint8
{
    None,
    InvalidInput,
    DegenerateCurve,
    RouteTooLong,
    GradeLimit,
    BankLimit,
    SegmentLimit
};

USTRUCT(BlueprintType)
struct ROADWORKS_API FParametricRoadSolution
{
    GENERATED_BODY()

    /** Blueprint root poses; no actor is spawned or changed by this solver. */
    UPROPERTY(BlueprintReadOnly, Category = "Road")
    TArray<FTransform> SegmentPoses;

    UPROPERTY(BlueprintReadOnly, Category = "Road")
    double Length = 0.0;

    UPROPERTY(BlueprintReadOnly, Category = "Road")
    double MinimumMeasuredOverlap = 0.0;

    UPROPERTY(BlueprintReadOnly, Category = "Road")
    double MaximumMeasuredEdgeStep = 0.0;

    UPROPERTY(BlueprintReadOnly, Category = "Road")
    EParametricRoadFailure Failure = EParametricRoadFailure::None;
};

struct FParametricRoadPathData;

/** Immutable evaluated centreline shared by road and decorative layers. This
 * C++ value is transient tool data, not a saved or Blueprint asset. */
struct ROADWORKS_API FParametricRoadPath
{
    double GetLength() const;
    bool IsValid() const;

private:
    TSharedPtr<const FParametricRoadPathData> Data;
    friend class UParametricRoadGeometry;
};

/** Geometry is independent of world construction and does not mutate a save. */
UCLASS()
class ROADWORKS_API UParametricRoadGeometry : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /** Apply grade and bank in the blueprint's local frame, retaining its origin and scale. */
    UFUNCTION(BlueprintPure, Category = "Roadworks|Geometry")
    static bool TryMakeRoadPose(const FTransform& BlueprintPose, double PitchDegrees,
                               double RollDegrees, FTransform& OutPose);

    /** Canonical road X travels opposite the blueprint's selected local +X/+Y;
     * Z retains the blueprint's up direction and the origin is the deck centre. */
    UFUNCTION(BlueprintPure, Category = "Roadworks|Geometry")
    static bool TryMakeRoadFrame(const FTransform& BlueprintPose, const FVector& LocalCenter,
                                 bool bForwardY, FTransform& OutFrame);

    /** Inverse of TryMakeRoadFrame: retain the blueprint's orientation and origin
     * while mapping the solver's canonical road frame back to native placement. */
    UFUNCTION(BlueprintPure, Category = "Roadworks|Geometry")
    static bool TryMakeBlueprintPose(const FTransform& RoadFrame, const FVector& LocalCenter,
                                    bool bForwardY, FTransform& OutPose);

    /** Bounded solver. Failure returns no partial list of buildable poses. */
    UFUNCTION(BlueprintCallable, Category = "Roadworks|Geometry")
    static FParametricRoadSolution GenerateRoute(const FParametricRoadRequest& Request);

    /** Resolve complete anchor frames without changing inputs. Automatic
     * intermediate components follow adjacent chord bisectors; manual heading
     * and grade are honored independently. Request endpoints are unchanged.
     * Failure clears OutAnchors. Grade/bank limits are enforced by generation. */
    UFUNCTION(BlueprintCallable, Category = "Roadworks|Geometry")
    static EParametricRoadFailure ResolveControlPointPoses(const FParametricRoadRequest& Request,
        const TArray<FParametricRoadControlPoint>& ControlPoints, TArray<FTransform>& OutAnchors);

    /** Up to 30 editable intermediate points; bounded spans share the full
     * resolved joint frame and emit each shared slice once. */
    UFUNCTION(BlueprintCallable, Category = "Roadworks|Geometry")
    static FParametricRoadSolution GenerateRouteThroughControlPoints(const FParametricRoadRequest& Request,
        const TArray<FParametricRoadControlPoint>& ControlPoints);

    /** Solve once and retain the same bounded curves/transport frames for all
     * layers. Failure clears OutPath. Returned poses use the existing road API. */
    static FParametricRoadSolution GenerateRouteWithPath(const FParametricRoadRequest& Request,
        const TArray<FParametricRoadControlPoint>& ControlPoints, FParametricRoadPath& OutPath);

    /** Distance in centimetres along the complete 3D centreline; no re-solve.
     * Samples are canonical road/deck frames, independent of SliceCenter. */
    static bool SamplePath(const FParametricRoadPath& Path, double Distance, FTransform& OutRoadFrame);

    /** Emit First + k*Spacing across all spans without restarting at joins or
     * forcing a final placement. MaximumPlacements is 0..512. Failure clears
     * output; zero placements is valid. Offset components are limited to ±1km
     * and translate in the full sampled road frame BEFORE grade/bank filtering.
     * They do not alter cumulative station distances or blueprint orientation. */
    static EParametricRoadFailure DistributeLayer(const FParametricRoadPath& Path, double SpacingCM,
        double FirstCM, bool bFollowGrade, bool bFollowBank, int32 MaximumPlacements,
        TArray<FTransform>& OutRoadFrames, const FVector& OffsetCentimetres = FVector::ZeroVector);

    /** Up to 30 intermediate deck centres. Heading/grade use the bisector of
     * adjacent chords; each point's roll is retained. Shared joint slices are
     * emitted once. Length and segment limits apply to the entire path. */
    UFUNCTION(BlueprintCallable, Category = "Roadworks|Geometry")
    static FParametricRoadSolution GenerateRouteThroughPoints(const FParametricRoadRequest& Request,
        const TArray<FTransform>& IntermediatePoints);
};
