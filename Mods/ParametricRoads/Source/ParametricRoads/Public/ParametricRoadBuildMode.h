#pragma once

#include "Hologram/FGHologramBuildModeDescriptor.h"
#include "ParametricRoadBuildMode.generated.h"

/** Client tool identity. This class must not be exported in native construction. */
UCLASS()
class PARAMETRICROADS_API UParametricRoadBuildMode : public UFGHologramBuildModeDescriptor
{
    GENERATED_BODY()

public:
    UParametricRoadBuildMode();
};
