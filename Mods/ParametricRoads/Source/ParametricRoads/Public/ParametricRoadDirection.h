#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ParametricRoadDirection.generated.h"
class AFGBlueprintHologram;
class UStaticMeshComponent;
class AActor;

namespace ParametricRoadDirection
{
    inline constexpr double MarkerLength = 160.0;
    inline constexpr double MarkerWidth = 64.0;
    inline constexpr int32 MarkerCount = 1;

    /** The engine plane is 100 cm square in XY. The material explicitly draws
     * its tip along local +X; canonical road +X is already the travel tangent. */
    inline FTransform MarkerPose(const FTransform& RoadFrame, double SliceLength)
    {
        return FTransform(RoadFrame.GetRotation(),
            RoadFrame.GetLocation()
                + RoadFrame.GetUnitAxis(EAxis::X) * (FMath::Max(0.0, SliceLength) * .5 + MarkerLength * .5 + 20.0)
                + RoadFrame.GetUnitAxis(EAxis::Z) * 5.0,
            FVector(MarkerLength / 100.0, MarkerWidth / 100.0, 1.0));
    }
}

/** One disposable local direction cue, never a construction child or saved actor. */
UCLASS(Transient)
class PARAMETRICROADS_API UParametricRoadDirection : public UObject
{
    GENERATED_BODY()
public:
    bool Initialize(AFGBlueprintHologram* Source);
    void Update(const TOptional<FTransform>& RoadFrame, double SliceLength);
    void Clear();
    void BeginDestroy() override { Clear(); Super::BeginDestroy(); }
private:
    UPROPERTY(Transient) TObjectPtr<AActor> Presentation;
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Marker;
};
