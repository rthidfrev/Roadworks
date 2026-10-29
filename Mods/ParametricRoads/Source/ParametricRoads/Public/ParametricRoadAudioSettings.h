#pragma once
#include "CoreMinimal.h"
#include "Settings/FGUserSettingCategory.h"
#include "ParametricRoadAudioSettings.generated.h"

/** Native local audio subcategory; the saved slider is an FGUserSetting asset. */
UCLASS()
class PARAMETRICROADS_API UParametricRoadAudioCategory : public UFGUserSettingCategory
{
    GENERATED_BODY()
public:
    UParametricRoadAudioCategory();
};

namespace ParametricRoadAudio
{
    inline constexpr const TCHAR* VolumeId = TEXT("ParametricRoads.MonitorVolume");
    float AppliedGain();
}
