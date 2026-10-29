#include "ParametricRoadGeometry.h"

namespace
{
    FQuat BlueprintRoadBasis(bool bForwardY)
    {
        // Travel opposes the selected local blueprint axis.
        // Share this basis and its inverse so reversing travel never spins the
        // first slice, mirrors the deck or changes its blueprint-local centre.
        return FRotator(0, bForwardY ? 270.0 : 180.0, 0).Quaternion();
    }

    bool IsRigidDeckPose(const FTransform& Pose, const FVector& LocalCenter)
    {
        return !Pose.ContainsNaN() && Pose.GetRotation().IsNormalized()
            && Pose.GetScale3D().Equals(FVector::OneVector, 0.000001)
            && !LocalCenter.ContainsNaN() && LocalCenter.GetAbsMax() <= 100000.0;
    }
}

bool UParametricRoadGeometry::TryMakeRoadPose(const FTransform& BlueprintPose,
    double PitchDegrees, double RollDegrees, FTransform& OutPose)
{
    OutPose = BlueprintPose;
    if (BlueprintPose.ContainsNaN() || !BlueprintPose.GetRotation().IsNormalized()
        || !FMath::IsFinite(PitchDegrees) || !FMath::IsFinite(RollDegrees))
    {
        return false;
    }

    const FQuat LocalRotation = FRotator(PitchDegrees, 0.0, RollDegrees).Quaternion();
    OutPose.SetRotation((BlueprintPose.GetRotation() * LocalRotation).GetNormalized());
    return true;
}

bool UParametricRoadGeometry::TryMakeRoadFrame(const FTransform& BlueprintPose,
    const FVector& LocalCenter, bool bForwardY, FTransform& OutFrame)
{
    OutFrame = BlueprintPose;
    if (!IsRigidDeckPose(BlueprintPose, LocalCenter)) { return false; }
    const FQuat Rotation = (BlueprintPose.GetRotation() * BlueprintRoadBasis(bForwardY)).GetNormalized();
    OutFrame = FTransform(Rotation, BlueprintPose.TransformPosition(LocalCenter));
    return true;
}

bool UParametricRoadGeometry::TryMakeBlueprintPose(const FTransform& RoadFrame,
    const FVector& LocalCenter, bool bForwardY, FTransform& OutPose)
{
    OutPose = RoadFrame;
    if (!IsRigidDeckPose(RoadFrame, LocalCenter)) { return false; }
    const FQuat Rotation = (RoadFrame.GetRotation()
        * BlueprintRoadBasis(bForwardY).Inverse()).GetNormalized();
    OutPose = FTransform(Rotation, RoadFrame.GetLocation() - Rotation.RotateVector(LocalCenter));
    return true;
}
