#pragma once

#include "CoreMinimal.h"
#include "ItemAmount.h"
#include "ParametricRoadLayerTypes.generated.h"

class UFGBlueprintDescriptor;
class UParametricRoadPreview;

/** Native catalog identity is separate from the user's displayed/filtered name. */
struct FParametricRoadBlueprintEntry
{
    FString Name;
    FText DisplayName;
    FText Description;
    TWeakObjectPtr<UObject> Icon;
    FLinearColor Color = FLinearColor::White;
    FString CategoryKey;
    FText CategoryName;
    FString SubCategoryKey;
    FText SubCategoryName;
    int32 CategoryOrder = MAX_int32;
    int32 SubCategoryOrder = MAX_int32;
    // Cached native metadata: identities and icon IDs also participate in revisions.
    TWeakObjectPtr<UFGBlueprintDescriptor> Descriptor;
    int32 IconId = INDEX_NONE;
    float MenuPriority = 0;
};

struct FParametricRoadBlueprintSubCategory
{
    FString Key;
    FText DisplayName;
    int32 Order = 0;
    float MenuPriority = 0;
    bool bUndefined = false;
    TWeakObjectPtr<UObject> NativeCategory;
};

/** Ordered native browsing tree, including empty and undefined categories. */
struct FParametricRoadBlueprintCategory
{
    FString Key;
    FText DisplayName;
    TWeakObjectPtr<UObject> Icon;
    int32 IconId = INDEX_NONE;
    int32 Order = 0;
    float MenuPriority = 0;
    bool bUndefined = false;
    TWeakObjectPtr<UObject> NativeCategory;
    TArray<FParametricRoadBlueprintSubCategory> SubCategories;
};

/** Independently spaced, session-local decoration settings. */
struct FParametricRoadLayerSettings
{
    FString BlueprintName;
    double SpacingMetres = 12.0;
    double FirstPlacementMetres = 6.0;
    /** Full road-frame translation: tangent/forward, right, normal/up. */
    FVector OffsetMetres = FVector::ZeroVector;
    bool bEnabled = false;
    bool bFollowGrade = true;
    bool bFollowBank = true;
};

/** Each layer owns its snapshot even when two layers use the same blueprint.
 * Only object references need reflection; no layer data is saved or replicated. */
USTRUCT()
struct FParametricRoadLayerState
{
    GENERATED_BODY()
    FParametricRoadLayerSettings Settings;
    TArray<FTransform> PreviewPoses;
    UPROPERTY(Transient) TObjectPtr<UFGBlueprintDescriptor> Descriptor;
    UPROPERTY(Transient) TObjectPtr<UParametricRoadPreview> Preview;
    UPROPERTY(Transient) TArray<FItemAmount> Cost;
    FString LoadedName;
    FText Status;
    int32 MemberCount = 0;
    int32 VisualRevision = 0;
    bool bRejected = false;
    bool bSolutionValid = true;
};
