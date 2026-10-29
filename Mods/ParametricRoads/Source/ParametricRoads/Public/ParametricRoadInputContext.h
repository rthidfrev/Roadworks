#pragma once

#include "Input/FGChildInputMappingContext.h"
#include "InputMappingContext.h"
#include "ParametricRoadInputContext.generated.h"

/** Explicit identity also works in the editor SDK, whose FactoryGame method
 * bodies are stubs. This type is input data only, never a saved/net object. */
UCLASS()
class PARAMETRICROADS_API UParametricRoadInputContext : public UFGChildInputMappingContext
{
    GENERATED_BODY()
public:
    FPrimaryAssetId GetPrimaryAssetId() const override
    {
        return FPrimaryAssetId(FPrimaryAssetType(TEXT("FGChildInputMappingContext")), GetFName());
    }
};

/** Session-local wheel observation; native session gates decide when it can act. */
UCLASS(Transient)
class PARAMETRICROADS_API UParametricRoadWheelContext : public UInputMappingContext
{
    GENERATED_BODY()
public:
    UParametricRoadWheelContext()
    {
        InputModeFilterOptions = EMappingContextInputModeFilterOptions::DoNotFilter;
    }
};
