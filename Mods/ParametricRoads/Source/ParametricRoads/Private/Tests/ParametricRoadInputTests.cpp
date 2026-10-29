#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "ParametricRoadInputContext.h"
#include "Engine/AssetManager.h"
#include "InputAction.h"
#include "InputTriggers.h"
#include "Misc/AutomationTest.h"
#include "PlayerMappableKeySettings.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParametricRoadInputAssetsTest, "ParametricRoads.Editor.InputAssets",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParametricRoadInputAssetsTest::RunTest(const FString&)
{
    UParametricRoadInputContext* Context = LoadObject<UParametricRoadInputContext>(nullptr,
        TEXT("/ParametricRoads/Inputs/MC_Roads.MC_Roads"));
    if (!TestNotNull(TEXT("Authored native mapping context loads"), Context)) { return false; }
    TestEqual(TEXT("Native Controls category"), Context->mDisplayName.ToString(), FString(TEXT("Roadworks")));
    TestTrue(TEXT("Custom menu priority after game categories"), Context->mMenuPriority >= 200);
    TestEqual(TEXT("Child follows the native build-state context instead of invalid direct registration"),
        Context->mParentContext.ToSoftObjectPath().ToString(),
        FString(TEXT("/Game/FactoryGame/Inputs/Equipment/Buildgun/Build/MC_BuildGunBuild.MC_BuildGunBuild")));
    TestEqual(TEXT("Primary type discoverable by native input settings"), Context->GetPrimaryAssetId().PrimaryAssetType,
        FPrimaryAssetType(TEXT("FGChildInputMappingContext")));
    auto& Manager = UAssetManager::Get();
    Manager.ScanPathsForPrimaryAssets(FPrimaryAssetType(TEXT("FGChildInputMappingContext")),
        {TEXT("/ParametricRoads/Inputs")}, UFGChildInputMappingContext::StaticClass(), false, false, true);
    TestEqual(TEXT("Primary asset registry resolves our context"),
        Manager.GetPrimaryAssetPath(Context->GetPrimaryAssetId()), FSoftObjectPath(Context));
    const TArray<FEnhancedActionKeyMapping>& Mappings = Context->GetMappings();
    TestEqual(TEXT("Eight distinct remappable actions including selector and layer configuration"), Mappings.Num(), 8);
    TSet<FName> Names;
    const TArray<FName> Identities = {TEXT("ParametricRoads_NextSetting"), TEXT("ParametricRoads_SwitchEndpoint"),
        TEXT("ParametricRoads_AddPoint"), TEXT("ParametricRoads_TogglePrecision"), TEXT("ParametricRoads_IncreaseValue"),
        TEXT("ParametricRoads_DecreaseValue"), TEXT("ParametricRoads_SelectSetting"), TEXT("ParametricRoads_ConfigureLayers")};
    const TArray<FKey> Defaults = {EKeys::NumPadOne, EKeys::NumPadTwo, EKeys::NumPadZero,
        EKeys::Decimal, EKeys::Add, EKeys::Subtract, EKeys::LeftShift, EKeys::NumPadThree};
    for (int32 I = 0; I < Mappings.Num(); ++I)
    {
        const UInputAction* Action = Mappings[I].Action;
        if (!TestNotNull(TEXT("Mapping references a cooked action"), Action)) { return false; }
        const UPlayerMappableKeySettings* Settings = Action->GetPlayerMappableKeySettings();
        if (!TestNotNull(TEXT("Settings on action, as required in Satisfactory 1.2"), Settings)) { return false; }
        TestTrue(TEXT("Native rebind enabled"), Settings->IsPlayerMappable);
        TestFalse(TEXT("Player mapping names are unique"), Names.Contains(Settings->Name));
        TestTrue(TEXT("Names are scoped to our mod"), Settings->Name.ToString().StartsWith(TEXT("ParametricRoads_")));
        if (Identities.IsValidIndex(I)) { TestEqual(TEXT("Each action retains its stable remapping identity"), Settings->Name, Identities[I]); }
        Names.Add(Settings->Name);
        TestFalse(TEXT("No action consumes native game input"), Action->bConsumeInput);
        TestFalse(TEXT("No action runs while paused"), Action->bTriggerWhenPaused);
        TestEqual(TEXT("Remapping category remains native Roadworks"), Settings->DisplayCategory.ToString(), FString(TEXT("Roadworks")));
        if (Defaults.IsValidIndex(I)) { TestEqual(TEXT("Existing defaults preserved; layer configuration defaults to Num 3"), Mappings[I].Key, Defaults[I]); }
        if (I == 4 || I == 5)
        {
            TestTrue(TEXT("Holding +/- repeats"), Action->Triggers.ContainsByPredicate([](const UInputTrigger* Trigger)
                { return Trigger && Trigger->IsA<UInputTriggerPulse>(); }));
        }
        if (I == 6)
        {
            TestEqual(TEXT("Held selector has a stable remapping identity"), Settings->Name, FName(TEXT("ParametricRoads_SelectSetting")));
            TestTrue(TEXT("Selector keeps its evaluated value for the entire hold"), Action->Triggers.IsEmpty());
        }
        if (I == 7)
        {
            TestEqual(TEXT("Layer configuration action can be reused by the UI context"), Action->GetPathName(),
                FString(TEXT("/ParametricRoads/Inputs/IA_ConfigureLayers.IA_ConfigureLayers")));
            TestEqual(TEXT("Layer configuration is Boolean"), Action->ValueType, EInputActionValueType::Boolean);
            TestEqual(TEXT("Layer configuration has an English remapping label"), Settings->DisplayName.ToString(), FString(TEXT("Open terminal")));
            TestTrue(TEXT("Layer configuration uses Started without an authored trigger"), Action->Triggers.IsEmpty());
            TestTrue(TEXT("Layer configuration has no authored modifiers"), Action->Modifiers.IsEmpty());
            TestTrue(TEXT("Layer mapping adds no trigger"), Mappings[I].Triggers.IsEmpty());
            TestTrue(TEXT("Layer mapping adds no modifier"), Mappings[I].Modifiers.IsEmpty());
            TestTrue(TEXT("Layer mapping inherits the authored action's settings"), Mappings[I].GetPlayerMappableKeySettings() == Settings);
        }
    }
    return true;
}
#endif
