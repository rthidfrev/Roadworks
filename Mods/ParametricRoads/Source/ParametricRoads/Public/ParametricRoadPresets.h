#pragma once

#include "CoreMinimal.h"
#include "ParametricRoadLayerTypes.h"

/** Reusable slab configuration. Explicit metre units on disk, no route anchors. */
struct FParametricRoadPresetRoadSettings
{
    double LengthMetres = 8;
    double WidthMetres = 8;
    double ThicknessMetres = 1;
    double OverlapMetres = .2;
    double MaxEdgeStepMetres = .05;
    double DeckPlaneMetres = 0;
    double CurveTension = 1;
    bool bForwardY = false;
};

/** Local composition only: native catalog identities and settings, no objects. */
struct FParametricRoadPreset
{
    FString Id;
    FString Name;
    FString RoadBlueprintName;
    FParametricRoadPresetRoadSettings Road;
    TArray<FParametricRoadLayerSettings> Layers;
};

namespace ParametricRoadPresets
{
    inline constexpr int32 SchemaVersion = 1;
    inline constexpr int32 MaximumLayers = 8;
    /** Saved/ParametricRoads/Presets; never the native save or blueprint files. */
    PARAMETRICROADS_API FString Directory();
    PARAMETRICROADS_API bool Validate(const FParametricRoadPreset& Preset, FText& Error);
    PARAMETRICROADS_API bool Encode(const FParametricRoadPreset& Preset, FString& Json, FText& Error);
    PARAMETRICROADS_API bool Decode(const FString& Json, FParametricRoadPreset& Preset, FText& Error);
    /** New IDs are generated for an empty Id. Updates require explicit overwrite
     * and keep the previous file as .json.bak before the temporary-file rename. */
    PARAMETRICROADS_API bool Save(FParametricRoadPreset& Preset, bool bOverwrite, FText& Error,
        const FString& RootDirectory = FString());
    PARAMETRICROADS_API bool Load(const FString& Id, FParametricRoadPreset& Preset, FText& Error,
        const FString& RootDirectory = FString());
    /** Skips bad files with a visible diagnostic. No directory is created by a read. */
    PARAMETRICROADS_API bool List(TArray<FParametricRoadPreset>& Presets, FText& Diagnostic,
        const FString& RootDirectory = FString());
}
