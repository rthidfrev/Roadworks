#pragma once
#include "ParametricRoadMonitorAnimation.h"

namespace ParametricRoadMonitorPower
{
    constexpr float OffSeconds = .14f;
    constexpr float OpenSoundPhase = .23f;
    struct FPose { float Reveal = 0, Content = 0, Glow = 0; };
    inline FPose On(float Deployment)
    {
        using namespace ParametricRoadMonitorAnimation;
        const float T = Unit(Deployment);
        return {Window(T, .56f, .64f) * (.012f + .988f * Window(T, .64f, .88f)),
            Window(T, .72f, .96f), .20f * Window(T, .56f, .65f) * (1 - Window(T, .84f, 1))};
    }
    inline FPose Off(const FPose& Start, float Progress)
    {
        using namespace ParametricRoadMonitorAnimation;
        const float T = Unit(Progress);
        return {Start.Reveal * (1 - Window(T, .08f, .82f)),
            Start.Content * (1 - Window(T, 0, .45f)),
            (Start.Glow * (1 - Window(T, 0, .25f)) + .14f * Start.Content * Window(T, 0, .25f))
                * (1 - Window(T, .70f, 1))};
    }
    inline float MechanicalTime(float Elapsed, float Delay)
    {
        return Elapsed > Delay ? Elapsed - Delay : 0;
    }
    inline float AudioGain(float Master, float Effects)
    {
        return .55f * ParametricRoadMonitorAnimation::Unit(Master) * ParametricRoadMonitorAnimation::Unit(Effects);
    }
}
