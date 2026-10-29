#include "ParametricRoadAudioSettings.h"
#include "FGGameUserSettings.h"
#include "Settings/FGUserSettingApplyType.h"
#include "ParametricRoadMonitorAnimation.h"

UParametricRoadAudioCategory::UParametricRoadAudioCategory()
{
    mDisplayName = NSLOCTEXT("Roadworks", "AudioCategory", "Roadworks");
    mMenuPriority = 60;
}

float ParametricRoadAudio::AppliedGain()
{
    const UFGGameUserSettings* Settings = UFGGameUserSettings::GetFGGameUserSettings();
    if (!Settings || !Settings->HasInitialized()) { return 0; }
    // Applied option IDs differ from the legacy Wwise bus IDs in SDK assets.
    // Find first: missing optional settings must not spam a log every frame.
    const auto Read = [Settings](const TCHAR* Id, float Default)
    {
        const UFGUserSettingApplyType* Setting = Settings->FindUserSetting(Id);
        if (!Setting) { return Default; }
        const FVariant Value = Setting->GetAppliedValue();
        return Value.GetType() == EVariantTypes::Float ? Value.GetValue<float>() : Default;
    };
    using ParametricRoadMonitorAnimation::Unit;
    return .55f * Unit(Read(TEXT("RTPC.Menu_Volume_Master"), 1))
        * Unit(Read(TEXT("RTPC.Menu_Volume_SoundEffects"), 1))
        * Unit(Read(TEXT("RTPC.Menu_Volume_Equipment"), 1)) * Unit(Read(VolumeId, 1));
}
