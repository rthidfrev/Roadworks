#pragma once

#include "CoreMinimal.h"

class AFGBlueprintHologram;

/** Inspect source blueprint objects and saved references without loading a game/save. */
namespace ParametricRoadEligibility
{
    bool Inspect(AFGBlueprintHologram* Hologram, FBox& OutLocalBounds, int32& OutMembers, FText& OutReason);
}
