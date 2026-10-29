#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "ParametricRoadMonitorAnimation.h"
#include "Misc/AutomationTest.h"
#include <limits>

// Pure timing and hinge math only. No world, pawn, input or play session.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParametricRoadMonitorAnimationTest,
    "ParametricRoads.Editor.MonitorDeployment",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParametricRoadMonitorAnimationTest::RunTest(const FString&)
{
    using namespace ParametricRoadMonitorAnimation;
    const auto Same = [](const FPose& A, const FPose& B)
    {
        return FMath::IsNearlyEqual(A.EntryFold, B.EntryFold, 0.00001f)
            && FMath::IsNearlyEqual(A.ArmRoll, B.ArmRoll, 0.00001f)
            && FMath::IsNearlyEqual(A.HousingRoll, B.HousingRoll, 0.00001f);
    };
    const FPose Folded;
    TestTrue(TEXT("Initial pose is folded"), Same(Deploy(0), Folded));
    TestTrue(TEXT("Negative and invalid time cannot expose the display"),
        Same(Deploy(-1), Folded) && Same(Deploy(std::numeric_limits<float>::quiet_NaN()), Folded));
    TestTrue(TEXT("Entry moves while both hinges remain folded"), Deploy(.1f).EntryFold < 1
        && Deploy(.1f).EntryFold > 0 && Deploy(.1f).ArmRoll == 95 && Deploy(.1f).HousingRoll == -80);
    TestTrue(TEXT("Entrance finishes before hinge deployment"), Deploy(.23f).EntryFold == 0
        && Deploy(.23f).ArmRoll == 95 && Deploy(.23f).HousingRoll == -80);
    TestTrue(TEXT("Arm begins before housing"), Deploy(.30f).ArmRoll < 95 && Deploy(.30f).HousingRoll == -80);
    TestTrue(TEXT("Deployed rest retains original geometry"), Same(Deploy(1), {0, 0, 0})
        && Same(Deploy(2), {0, 0, 0}));
    float FirstOvershoot = 0, ReturnLobe = 0;
    for (int32 I = 0; I <= 1000; ++I)
    {
        const float Time = I / 1000.0f;
        const FPose P = Deploy(Time);
        if (!std::isfinite(P.EntryFold) || !std::isfinite(P.ArmRoll) || !std::isfinite(P.HousingRoll)
            || P.EntryFold < 0 || P.EntryFold > 1 || P.ArmRoll < -1.6f || P.ArmRoll > 95.001f
            || P.HousingRoll < -80.001f || P.HousingRoll > 1.1f)
        { AddError(TEXT("Deployment exceeds a bounded mechanical envelope")); return false; }
        if (Time >= .74f)
        {
            FirstOvershoot = FMath::Min(FirstOvershoot, P.ArmRoll);
            ReturnLobe = FMath::Max(ReturnLobe, P.ArmRoll);
            if (FMath::Abs(P.ArmRoll + P.HousingRoll) > .55f)
            { AddError(TEXT("Settling excessively rotates the readable housing")); return false; }
        }
        const FPose End = Retract(P, 1);
        if (!Same(Retract(P, 0), P) || !Same(End, Folded))
        { AddError(TEXT("Cancellation does not join continuously to the reached pose and folded endpoint")); return false; }
        const FPose Middle = Retract(P, .5f);
        if (Middle.ArmRoll < P.ArmRoll || Middle.ArmRoll > 95.001f
            || Middle.HousingRoll > P.HousingRoll || Middle.HousingRoll < -80.001f)
        { AddError(TEXT("Retraction replays a rebound instead of folding directly")); return false; }
    }
    TestTrue(TEXT("A small visible overshoot exists"), FirstOvershoot < -.5f && FirstOvershoot > -1.6f);
    TestTrue(TEXT("The opposite return is smaller and damped"), ReturnLobe > .05f && ReturnLobe < -FirstOvershoot * .4f);
    for (const float Boundary : {0.0f, .23f, .36f, .66f, .74f, 1.0f})
    {
        const FPose Before = Deploy(Boundary - .0001f), After = Deploy(Boundary + .0001f);
        TestTrue(TEXT("Phase boundaries have no velocity kick"),
            FMath::Abs(After.ArmRoll - Before.ArmRoll) < .15f
            && FMath::Abs(After.HousingRoll - Before.HousingRoll) < .15f);
    }
    for (const int32 FPS : {30, 60, 144})
    {
        float Progress = 0;
        const int32 Frames = FMath::CeilToInt(DeploySeconds * FPS) + 1;
        for (int32 Frame = 0; Frame < Frames; ++Frame)
        { Progress = FMath::Min(1.0f, Progress + 1.0f / FPS / DeploySeconds); }
        TestTrue(TEXT("Common frame rates end in the exact same resting pose"), Same(Deploy(Progress), {0, 0, 0}));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParametricRoadMonitorExpansionTest,
    "ParametricRoads.Editor.MonitorExpansion",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParametricRoadMonitorExpansionTest::RunTest(const FString&)
{
    using namespace ParametricRoadMonitorAnimation;
    const auto AtRest = [](const FExpansionMotion& P, float Alpha)
    {
        return P.PositionAlpha == Alpha && P.RotationAlpha == Alpha && P.ScaleAlpha == Alpha
            && P.DepthOffset == 0 && P.LiftOffset == 0 && P.RollOffset == 0
            && P.ArmRoll == 0 && P.HousingRoll == 0;
    };
    TestTrue(TEXT("Compact endpoint adds no motion"), AtRest(Expanded(0), 0));
    TestTrue(TEXT("Expanded endpoint is exactly settled"), AtRest(Expanded(1), 1));
    TestTrue(TEXT("Out-of-range and invalid phase stays bounded"),
        AtRest(Expanded(-1), 0) && AtRest(Expanded(2), 1)
        && AtRest(Expanded(std::numeric_limits<float>::quiet_NaN()), 0));
    TestTrue(TEXT("The frame faces the reader before its growth finishes"),
        Expanded(.78f).RotationAlpha == 1 && Expanded(.78f).ScaleAlpha < 1);
    float FurthestPosition = 0, LargestScale = 0;
    for (int32 I = 0; I <= 2000; ++I)
    {
        const auto P = Expanded(I / 2000.0f);
        const float Values[] = {P.PositionAlpha, P.RotationAlpha, P.ScaleAlpha, P.DepthOffset,
            P.LiftOffset, P.RollOffset, P.ArmRoll, P.HousingRoll};
        for (const float Value : Values)
        { if (!std::isfinite(Value)) { AddError(TEXT("Expanded profile is nonfinite")); return false; } }
        if (P.PositionAlpha < 0 || P.PositionAlpha > 1.015f || P.ScaleAlpha < 0 || P.ScaleAlpha > 1.01f
            || P.RotationAlpha < 0 || P.RotationAlpha > 1 || FMath::Abs(P.DepthOffset) > 4.2f
            || P.LiftOffset < 0 || P.LiftOffset > 1.251f || FMath::Abs(P.RollOffset) > 1.9f
            || FMath::Abs(P.ArmRoll) > 4.1f || FMath::Abs(P.HousingRoll) > 3.5f
            || FMath::Abs(P.ArmRoll + P.HousingRoll) > .8f)
        { AddError(TEXT("Expanded motion exceeds its bounded mechanical envelope")); return false; }
        FurthestPosition = FMath::Max(FurthestPosition, P.PositionAlpha);
        LargestScale = FMath::Max(LargestScale, P.ScaleAlpha);
    }
    TestTrue(TEXT("Final approach has a small position overshoot"), FurthestPosition > 1.001f && FurthestPosition < 1.015f);
    TestTrue(TEXT("Final growth overshoot stays below one percent"), LargestScale > 1.001f && LargestScale < 1.01f);
    for (const int32 FPS : {30, 60, 144})
    {
        float Progress = 0;
        for (int32 Frame = 0; Frame <= FMath::CeilToInt(ExpandSeconds * FPS); ++Frame)
        { Progress = FMath::Min(1.0f, Progress + 1.0f / FPS / ExpandSeconds); }
        TestTrue(TEXT("Opening reaches the exact stable input surface at common frame rates"), AtRest(Expanded(Progress), 1));
        for (int32 Frame = 0; Frame <= FMath::CeilToInt(CollapseSeconds * FPS); ++Frame)
        { Progress = FMath::Max(0.0f, Progress - 1.0f / FPS / CollapseSeconds); }
        TestTrue(TEXT("Closing returns to the unchanged compact pose"), AtRest(Expanded(Progress), 0));
    }
    return true;
}
#endif
