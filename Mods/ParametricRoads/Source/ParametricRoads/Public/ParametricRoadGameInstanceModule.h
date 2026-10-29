#pragma once

#include "Module/GameInstanceModule.h"
#include "ParametricRoadGameInstanceModule.generated.h"

/** Application-lifetime registration only; no saved road actors or custom RPCs. */
UCLASS()
class PARAMETRICROADS_API UParametricRoadGameInstanceModule : public UGameInstanceModule
{
    GENERATED_BODY()

public:
    UParametricRoadGameInstanceModule();
    void DispatchLifecycleEvent(ELifecyclePhase Phase) override;
};
