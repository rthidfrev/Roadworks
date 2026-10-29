#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "ParametricRoadMonitorPower.h"
#include "Misc/AutomationTest.h"
#include "Sound/SoundWave.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParametricRoadMonitorPowerTest,
    "ParametricRoads.Editor.MonitorPower", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FParametricRoadMonitorPowerTest::RunTest(const FString&)
{
    using namespace ParametricRoadMonitorPower;
    const auto Same = [](const FPose& A, const FPose& B)
    {
        return FMath::IsNearlyEqual(A.Reveal,B.Reveal,.00001f) && FMath::IsNearlyEqual(A.Content,B.Content,.00001f)
            && FMath::IsNearlyEqual(A.Glow,B.Glow,.00001f);
    };
    TestTrue(TEXT("The folded monitor starts black"), Same(On(0),{}));
    TestTrue(TEXT("The opened monitor exactly restores the original content"), Same(On(1),{1,1,0}));
    TestTrue(TEXT("Power begins as a thin line before the content"), On(.66f).Reveal > 0 && On(.66f).Reveal < .03f && On(.66f).Content == 0 && On(.66f).Glow > 0);
    TestTrue(TEXT("The readable content arrives after the reveal begins"), On(.85f).Reveal > .85f && On(.85f).Content > .5f);
    float PreviousReveal=0, PreviousContent=0;
    for (int Frame=0;Frame<=240;++Frame)
    {
        const auto P=On(Frame/240.f);
        TestTrue(TEXT("Power reveal/content progress without flicker or brightness overshoot"), P.Reveal >= PreviousReveal && P.Content >= PreviousContent
            && P.Reveal <= 1 && P.Content <= 1 && P.Glow >= 0 && P.Glow <= .20001f);
        PreviousReveal=P.Reveal;PreviousContent=P.Content;
    }
    for (float Phase : {0.f,.3f,.57f,.66f,.8f,1.f})
    {
        const auto Start=On(Phase);
        TestTrue(TEXT("Cancelling at any stage starts shutdown at its reached light state"),Same(Off(Start,0),Start));
        TestTrue(TEXT("Shutdown always ends exactly black"),Same(Off(Start,1),{}));
        float Previous=Start.Content;
        for(int Frame=0;Frame<=60;++Frame)
        {
            const auto P=Off(Start,Frame/60.f);
            TestTrue(TEXT("Shutdown never restores content which was already hidden"),P.Content <= Previous && P.Content >= 0 && P.Reveal <= Start.Reveal && P.Reveal >= 0 && P.Glow <= .20001f);
            Previous=P.Content;
        }
    }
    for (int Hz : {30,60,144})
    {
        float PreviousElapsed=0, Mechanical=0;
        for (int Frame=0;Frame<Hz;++Frame)
        {
            const float Elapsed=(Frame+1.f)/Hz;
            Mechanical += MechanicalTime(Elapsed,OffSeconds)-MechanicalTime(PreviousElapsed,OffSeconds);
            if (Elapsed <= OffSeconds) TestEqual(TEXT("The support waits until the screen is dark"),Mechanical,0.f);
            PreviousElapsed=Elapsed;
        }
        TestTrue(TEXT("Shutdown hold does not accumulate frame-rate timing error"),FMath::IsNearlyEqual(Mechanical,1-OffSeconds,.00001f));
    }
    TestEqual(TEXT("Either native game volume can mute the effect"),AudioGain(0,1)+AudioGain(1,0),0.f);
    TestTrue(TEXT("Applied game gain stays below the discreet maximum"),FMath::IsNearlyEqual(AudioGain(.5f,.5f),.1375f));
    TestEqual(TEXT("Malformed volume fails quiet"),AudioGain(std::numeric_limits<float>::quiet_NaN(),1),0.f);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParametricRoadMonitorAudioAssetsTest,
    "ParametricRoads.Editor.MonitorAudioAssets", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FParametricRoadMonitorAudioAssetsTest::RunTest(const FString&)
{
    for (const auto& Name : {TEXT("Deploy"),TEXT("Retract")})
    {
        const FString Path=FString::Printf(TEXT("/ParametricRoads/Monitor/Audio/SW_RoadMonitor%s.SW_RoadMonitor%s"),Name,Name);
        const USoundWave* Sound=LoadObject<USoundWave>(nullptr,*Path);
        if(!TestNotNull(TEXT("The owned local monitor sound exists"),Sound))return false;
        TestFalse(TEXT("No monitor sound can loop"),Sound->bLooping);
        TestEqual(TEXT("The effect is mono"),Sound->NumChannels,1);
        TestTrue(TEXT("The effect is a brief bounded one-shot"),Sound->Duration > .4f && Sound->Duration <= .55f);
        TestEqual(TEXT("The short effect is inline, with no delayed streaming dependency"),Sound->GetLoadingBehavior(),ESoundWaveLoadingBehavior::ForceInline);
        TestEqual(TEXT("The effect uses the native PCM decoder"),Sound->GetSoundAssetCompressionType(),ESoundAssetCompressionType::PCM);
    }
    return !HasAnyErrors();
}
#endif
