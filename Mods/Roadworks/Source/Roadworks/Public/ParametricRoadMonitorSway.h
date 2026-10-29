#pragma once

#include "CoreMinimal.h"
#include "Kismet/KismetMathLibrary.h"
#include "ParametricRoadMonitorAnimation.h"

/** Worldless local presentation math; no input, world or persistent ownership. */
namespace ParametricRoadMonitorSway
{
    constexpr float MaxArmDegrees = .75f;
    constexpr float MaxResidualDegrees = .18f;
    constexpr float HousingCompensation = .8f;

    inline float Weight(float Deployment, float Expansion, bool bRetracting, float Retraction)
    {
        if (Deployment < 1) { return 0; }
        return (1 - ParametricRoadMonitorAnimation::Window(Expansion, 0, .20f))
            * (bRetracting ? 1 - ParametricRoadMonitorAnimation::Window(Retraction, 0, .30f) : 1);
    }

    struct FInput
    {
        FQuat CameraRotation = FQuat::Identity;
        FVector Location = FVector::ZeroVector;
        FVector Velocity = FVector::ZeroVector;
        FVector Acceleration = FVector::ZeroVector;
        float PoseWeight = 0;
        bool bGrounded = false;
        bool bVisible = false;
        bool bCanDrive = false;
    };

    struct FState
    {
        // Effective hinge offsets. The screen inherits their small net rotation.
        float ArmRoll = 0, HousingRoll = 0;
        float Arm = 0, Residual = 0, Phase = 0;
        FFloatSpringState ArmSpring, ResidualSpring;
        FQuat PreviousCamera = FQuat::Identity;
        FVector PreviousLocation = FVector::ZeroVector;
        bool bPreviousValid = false;

        void ResetMotion()
        {
            ArmRoll = HousingRoll = Arm = Residual = Phase = 0;
            ArmSpring.Reset(); ResidualSpring.Reset();
        }
        void Reset() { ResetMotion(); bPreviousValid = false; }

        static float Spring(float Current, float Target, FFloatSpringState& State,
            float Frequency, float Damping, float Limit, float Delta)
        {
            const float Omega = 2 * PI * Frequency;
            // Native analytic spring, unit mass, no target-velocity extrapolation.
            Current = UKismetMathLibrary::FloatSpringInterp(Current, Target, State,
                Omega * Omega, Damping, Delta, 1, 0, true, -Limit, Limit, false);
            if (Target == 0 && FMath::Abs(Current) < .0001f && FMath::Abs(State.Velocity) < .001f)
            {
                Current = 0; State.Reset();
            }
            return Current;
        }

        void Tick(const FInput& Input, float Delta)
        {
            const bool bValid = FMath::IsFinite(Delta) && Delta > 0 && Delta <= .1f
                && !Input.CameraRotation.ContainsNaN() && Input.CameraRotation.IsNormalized()
                && !Input.Location.ContainsNaN() && !Input.Velocity.ContainsNaN()
                && !Input.Acceleration.ContainsNaN() && FMath::IsFinite(Input.PoseWeight);
            FQuat RotationDelta = FQuat::Identity;
            bool bContinuous = bValid && bPreviousValid && Input.bVisible;
            if (bContinuous)
            {
                RotationDelta = PreviousCamera.Inverse() * Input.CameraRotation;
                RotationDelta.EnforceShortestArcWith(FQuat::Identity);
                const double DistanceLimit = FMath::Max(300.0, Input.Velocity.Size() * Delta * 3 + 100);
                bContinuous = RotationDelta.GetAngle() < FMath::DegreesToRadians(60.0)
                    && FVector::DistSquared(PreviousLocation, Input.Location) <= FMath::Square(DistanceLimit);
            }
            // Keep sampling in a modal view, but never save its forcing for later.
            bPreviousValid = bValid && Input.bVisible;
            if (bValid) { PreviousCamera = Input.CameraRotation; PreviousLocation = Input.Location; }
            if (!bContinuous || Input.PoseWeight <= 0)
            {
                ResetMotion(); return;
            }

            float ArmTarget = 0, ResidualTarget = 0;
            if (Input.bCanDrive)
            {
                FVector Axis; double Angle;
                RotationDelta.ToAxisAndAngle(Axis, Angle);
                const FVector Rate = Axis * (FMath::RadiansToDegrees(Angle) / Delta);
                const auto LookRate = [](double Value)
                {
                    return FMath::Abs(Value) < .8 ? 0.f : float(FMath::Clamp(Value, -240.0, 240.0));
                };
                const float Yaw = LookRate(Rate.Z), Pitch = LookRate(Rate.Y);
                ArmTarget = -.0022f * Yaw + .0014f * Pitch;
                ResidualTarget = .00045f * Yaw - .0007f * Pitch;
                const double Speed = Input.Velocity.Size2D();
                // Native grounded movement intent avoids footsteps while merely
                // carried by a platform/conveyor or moving through the air.
                if (Input.bGrounded && Input.Acceleration.SizeSquared2D() > 1 && Speed > 5)
                {
                    Phase = FMath::Fmod(Phase + 2 * PI * float(FMath::Min(Speed, 900.0)) * Delta / 240, 2 * PI);
                    const float Strength = float(FMath::Clamp((Speed - 5) / 350, 0.0, 1.0));
                    ArmTarget += Strength * (.18f * FMath::Sin(Phase) + .035f * FMath::Sin(2 * Phase));
                    ResidualTarget += .02f * Strength * FMath::Sin(2 * Phase);
                }
                else { Phase = 0; }
                if (Input.bGrounded)
                {
                    const FVector Acceleration = Input.CameraRotation.UnrotateVector(Input.Acceleration.GetClampedToMaxSize(4000));
                    ArmTarget += .00002f * float(Acceleration.X);
                    ResidualTarget += .00002f * float(Acceleration.Y);
                }
            }
            else { Phase = 0; }
            ArmTarget = FMath::Clamp(ArmTarget, -MaxArmDegrees, MaxArmDegrees);
            ResidualTarget = FMath::Clamp(ResidualTarget, -MaxResidualDegrees, MaxResidualDegrees);
            Arm = Spring(Arm, ArmTarget, ArmSpring, 3.2f, .72f, MaxArmDegrees, Delta);
            Residual = Spring(Residual, ResidualTarget, ResidualSpring, 4.f, .82f, MaxResidualDegrees, Delta);
            const float PoseWeight = FMath::Clamp(Input.PoseWeight, 0.f, 1.f);
            ArmRoll = Arm * PoseWeight;
            HousingRoll = (-HousingCompensation * Arm + Residual) * PoseWeight;
        }
    };
}
