#pragma once

#include <algorithm>
#include <cmath>

/** Pure presentation math: no world, input, saved or network state. */
namespace ParametricRoadMonitorAnimation
{
    inline constexpr float DeploySeconds = 0.70f;
    inline constexpr float RetractSeconds = 0.35f;
    inline constexpr float ExpandSeconds = 0.52f;
    inline constexpr float CollapseSeconds = 0.42f;

    struct FPose
    {
        float EntryFold = 1.0f;
        float ArmRoll = 95.0f;
        float HousingRoll = -80.0f;
    };

    inline float Unit(float Value)
    {
        return std::isfinite(Value) ? std::clamp(Value, 0.0f, 1.0f) : 0.0f;
    }

    inline float Ease(float Value)
    {
        const float T = Unit(Value);
        // Zero velocity and acceleration at both ends of each mechanical phase.
        return std::clamp(T * T * T * (T * (T * 6.0f - 15.0f) + 10.0f), 0.0f, 1.0f);
    }

    inline float Window(float Time, float Start, float End)
    {
        return Ease((Time - Start) / (End - Start));
    }

    inline FPose Deploy(float Progress)
    {
        const float T = Unit(Progress);
        if (T <= 0.0f) { return {}; }
        if (T >= 1.0f) { return {0.0f, 0.0f, 0.0f}; }
        // Enter folded before releasing the arm. The housing follows its hinge.
        const float Entry = 1.0f - Window(T, 0.0f, 0.23f);
        const float Arm = 95.0f * (1.0f - Window(T, 0.23f, 0.66f));
        const float Housing = -80.0f * (1.0f - Window(T, 0.36f, 0.74f));
        const float U = Unit((T - 0.74f) / 0.26f);
        // One damped cycle, smoothly gated at both ends. Absolute time avoids
        // integration drift at different frame rates; the end pose is exact.
        const float Settle = std::sin(6.28318530718f * U) * std::exp(-3.0f * U)
            * Window(U, 0.0f, 0.15f) * (1.0f - Window(U, 0.80f, 1.0f));
        return {Entry, Arm - 3.0f * Settle, Housing + 2.0f * Settle};
    }

    inline FPose Retract(const FPose& Start, float Progress)
    {
        const float T = Ease(Progress);
        // Blend from the reached pose: cancellation never replays settling in
        // reverse or snaps a partially unfolded hinge to its terminal angle.
        return {Start.EntryFold + (1.0f - Start.EntryFold) * T,
            Start.ArmRoll + (95.0f - Start.ArmRoll) * T,
            Start.HousingRoll + (-80.0f - Start.HousingRoll) * T};
    }

    struct FExpansionMotion
    {
        float PositionAlpha = 0;
        float RotationAlpha = 0;
        float ScaleAlpha = 0;
        float DepthOffset = 0;
        float LiftOffset = 0;
        float RollOffset = 0;
        float ArmRoll = 0;
        float HousingRoll = 0;
    };

    inline FExpansionMotion Expanded(float Progress)
    {
        const float T = Unit(Progress);
        if (T <= 0) { return {}; }
        if (T >= 1) { return {1, 1, 1}; }
        const float Eased = Ease(T);
        const float Arc = 4.0f * Eased * (1.0f - Eased);
        const float U = Unit((T - 0.78f) / 0.22f);
        const float Settle = std::sin(6.28318530718f * U) * std::exp(-3.0f * U)
            * Window(U, 0.0f, 0.15f) * (1.0f - Window(U, 0.80f, 1.0f));
        // Face the reader before finishing the approach/size change. All
        // offsets disappear at both endpoints; the same path reverses without
        // a positional jump when the target changes during the transition.
        return {Window(T, 0.0f, 0.82f) + 0.012f * Settle,
            Window(T, 0.06f, 0.78f),
            Window(T, 0.12f, 0.86f) + 0.010f * Settle,
            -4.0f * Arc - 0.30f * Settle,
            1.25f * Arc,
            -1.75f * Arc + 0.15f * Settle,
            4.0f * Arc + 0.60f * Settle,
            -3.4f * Arc - 0.40f * Settle};
    }

}
