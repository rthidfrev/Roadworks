#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "ParametricRoadMonitorSway.h"
#include "Misc/AutomationTest.h"
#include <limits>

// Synthetic samples and native spring math only; no world, player or gameplay.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParametricRoadMonitorSwayTest,
    "Roadworks.Editor.MonitorSway",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParametricRoadMonitorSwayTest::RunTest(const FString&)
{
    using namespace ParametricRoadMonitorSway;
    FInput Input; Input.bVisible = Input.bCanDrive = true; Input.PoseWeight = 1;
    const auto Quiet = [](const FState& State)
    {
        return State.ArmRoll == 0 && State.HousingRoll == 0 && State.Arm == 0 && State.Residual == 0
            && State.ArmSpring.Velocity == 0 && State.ResidualSpring.Velocity == 0;
    };
    const auto Turn = [&](FState& State)
    {
        Input = FInput(); Input.bVisible = Input.bCanDrive = true; Input.PoseWeight = 1;
        State.Reset(); State.Tick(Input, 1.f / 60);
        for (int Frame = 1; Frame <= 20; ++Frame)
        {
            Input.CameraRotation = FRotator(0, Frame, 0).Quaternion(); State.Tick(Input, 1.f / 60);
        }
    };
    FState State;
    for (int Frame = 0; Frame < 120; ++Frame) { State.Tick(Input, 1.f / 60); }
    TestTrue(TEXT("An untouched compact display has exactly zero idle drift"), Quiet(State));

    // Independent damped step-response oracle, allowing the native solver's
    // documented exponential approximation. Constant 60 deg/s yaw, no gait.
    const double Time = 1.0 / 6, Omega = 2 * PI * 3.2, Damping = .72;
    const double WD = Omega * FMath::Sqrt(1 - Damping * Damping);
    const double Expected = -.132 * (1 - FMath::Exp(-Damping * Omega * Time)
        * (FMath::Cos(WD * Time) + Damping * Omega / WD * FMath::Sin(WD * Time)));
    for (int Hz : {30, 60, 144})
    {
        State.Reset(); Input = FInput(); Input.bVisible = Input.bCanDrive = true; Input.PoseWeight = 1;
        State.Tick(Input, 1.f / Hz);
        for (int Frame = 1; Frame <= Hz / 6; ++Frame)
        {
            Input.CameraRotation = FRotator(0, 60.0 * Frame / Hz, 0).Quaternion(); State.Tick(Input, 1.f / Hz);
        }
        TestTrue(FString::Printf(TEXT("%d Hz matches the physical spring step response"), Hz), FMath::Abs(State.ArmRoll - Expected) < .0008);
    }
    Turn(State);
    TestTrue(TEXT("Turning produces a small lagging support response"), State.ArmRoll < -.05f && State.ArmRoll > -.2f);
    bool bCrossedRest = false;
    for (int Frame = 0; Frame < 180; ++Frame)
    {
        State.Tick(Input, 1.f / 60); bCrossedRest |= State.ArmRoll > .0001f;
    }
    TestTrue(TEXT("Stopping gives a tiny opposite damped return"), bCrossedRest);
    TestTrue(TEXT("The spring settles to exact rest after stopping"), Quiet(State));

    State.Reset(); Input = FInput(); Input.bVisible = Input.bCanDrive = true; Input.PoseWeight = 1;
    Input.bGrounded = true; Input.Velocity = FVector(500, 0, 0);
    for (int Frame = 0; Frame < 120; ++Frame) { State.Tick(Input, 1.f / 60); }
    TestTrue(TEXT("Being carried without movement intent does not invent footsteps"), Quiet(State));
    Input.bGrounded = false; Input.Acceleration = FVector(1000, 0, 0);
    for (int Frame = 0; Frame < 120; ++Frame) { State.Tick(Input, 1.f / 60); }
    TestTrue(TEXT("Airborne movement does not invent footsteps"), Quiet(State));
    Input.bGrounded = true;
    float WalkRange = 0;
    for (int Frame = 0; Frame < 120; ++Frame)
    {
        State.Tick(Input, 1.f / 60); WalkRange = FMath::Max(WalkRange, FMath::Abs(State.ArmRoll));
    }
    TestTrue(TEXT("Grounded movement intent adds a restrained walking cycle"), WalkRange > .03f && WalkRange < .3f);

    for (int Hz : {30, 60, 144})
    {
        State.Reset(); bool bBounded = true;
        for (int Frame = 0; Frame < Hz * 10; ++Frame)
        {
            const double T = double(Frame) / Hz;
            Input.CameraRotation = FRotator(65 * FMath::Sin(T * 5), 180 * FMath::Sin(T * 4), 0).Quaternion();
            Input.Acceleration = FVector(4000 * FMath::Sin(T * 7), 4000 * FMath::Cos(T * 9), 0);
            Input.Velocity = FVector(900, 0, 0); Input.Location += Input.Velocity / Hz;
            State.Tick(Input, 1.f / Hz);
            bBounded &= FMath::IsFinite(State.ArmRoll) && FMath::IsFinite(State.HousingRoll)
                && FMath::Abs(State.ArmRoll) <= .75001f && FMath::Abs(State.Residual) <= .18001f
                && FMath::Abs(State.ArmRoll + State.HousingRoll) <= .33001f;
        }
        TestTrue(TEXT("Combined rapid look/walk forcing stays finite and within readable angle limits"), bBounded);
    }

    FState Wrapped, Plain;
    Input = FInput(); Input.bVisible = Input.bCanDrive = true; Input.PoseWeight = 1;
    Input.CameraRotation = FRotator(0, 179, 0).Quaternion(); Wrapped.Tick(Input, 1.f / 60);
    Input.CameraRotation = FRotator(0, -179, 0).Quaternion(); Wrapped.Tick(Input, 1.f / 60);
    Input.CameraRotation = FQuat::Identity; Plain.Tick(Input, 1.f / 60);
    Input.CameraRotation = FRotator(0, 2, 0).Quaternion(); Plain.Tick(Input, 1.f / 60);
    TestTrue(TEXT("Crossing the yaw wrap is an ordinary two-degree turn"), FMath::IsNearlyEqual(Wrapped.ArmRoll, Plain.ArmRoll, .00001f));
    const FQuat Q = Input.CameraRotation; Input.CameraRotation = FQuat(-Q.X, -Q.Y, -Q.Z, -Q.W);
    Plain.Reset(); Input.CameraRotation = Q; Plain.Tick(Input, 1.f / 60);
    Input.CameraRotation = FQuat(-Q.X, -Q.Y, -Q.Z, -Q.W); Plain.Tick(Input, 1.f / 60);
    TestTrue(TEXT("Equivalent quaternion signs cannot create a look impulse"), Quiet(Plain));

    for (float BadDelta : {0.f, -.01f, .11f, std::numeric_limits<float>::quiet_NaN()})
    {
        Turn(State); State.Tick(Input, BadDelta);
        TestTrue(TEXT("Invalid time or a hitch discards motion rather than catching up"), Quiet(State));
        State.Tick(Input, 1.f / 60); TestTrue(TEXT("Resuming establishes a fresh quiet baseline"), Quiet(State));
    }
    Turn(State); Input.CameraRotation = FRotator(0, 150, 0).Quaternion(); State.Tick(Input, 1.f / 60);
    TestTrue(TEXT("A camera cut resets rather than kicking the spring"), Quiet(State));
    Turn(State); Input.Location = FVector(10000, 0, 0); State.Tick(Input, 1.f / 60);
    TestTrue(TEXT("A teleport resets rather than kicking the spring"), Quiet(State));
    Turn(State); Input.bVisible = false; State.Tick(Input, 1.f / 60);
    Input.bVisible = true; Input.CameraRotation = FRotator(0, 180, 0).Quaternion(); State.Tick(Input, 1.f / 60);
    TestTrue(TEXT("Hidden/pause motion is not replayed when shown"), Quiet(State));

    Turn(State); const float BeforeModal = State.ArmRoll;
    Input.bCanDrive = false; State.Tick(Input, 1.f / 60);
    TestTrue(TEXT("Opening configuration initially centres continuously"), State.ArmRoll < 0 && FMath::Abs(State.ArmRoll - BeforeModal) < .05f);
    for (int Frame = 0; Frame <= 80; ++Frame)
    {
        const float Expansion = .2f + Frame * .01f;
        Input.PoseWeight = Weight(1, Expansion, false, 0);
        Input.CameraRotation = FRotator(Frame, Frame * 2, 0).Quaternion();
        Input.Velocity = FVector(500, 0, 0); Input.Acceleration = FVector(1000, 0, 0); Input.bGrounded = true;
        State.Tick(Input, 1.f / 60);
        TestTrue(TEXT("Settings have exactly zero hinge offsets and no stored spring energy"), Quiet(State));
    }
    Input.PoseWeight = 1; Input.Velocity = Input.Acceleration = FVector::ZeroVector; Input.bCanDrive = true;
    State.Tick(Input, 1.f / 60);
    TestTrue(TEXT("Returning to compact does not release a deferred modal impulse"), Quiet(State));
    TestEqual(TEXT("Deployment preserves the accepted mechanical animation"), Weight(.99f, 0, false, 0), 0.f);
    TestEqual(TEXT("Retraction fades any remaining sway"), Weight(1, 0, true, .3f), 0.f);
    return !HasAnyErrors();
}
#endif
