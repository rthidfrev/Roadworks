#include "RoadworksModule.h"
#include "Engine/AssetManager.h"
#include "Input/FGChildInputMappingContext.h"
#include "Settings/FGUserSetting.h"
#include "Engine/AssetManagerSettings.h"

#include "ParametricRoadBuildMode.h"
#include "ParametricRoadSession.h"
#include "Equipment/FGBuildGun.h"
#include "Equipment/FGBuildGunBuild.h"
#include "FGCharacterPlayer.h"
#include "Hologram/FGBlueprintHologram.h"
#include "Net/Core/PropertyConditions/PropertyConditions.h"
#include "Patching/NativeHookManager.h"
#include "UObject/UnrealType.h"
#include "UObject/CoreRedirects.h"

DEFINE_LOG_CATEGORY(LogRoadworks);

namespace
{
    TSet<TWeakObjectPtr<AFGBuildGun>> SuppressedModeReplication;

    bool IsLocalRoadPreview(const AFGBlueprintHologram* Hologram)
    {
        const auto* Session = UParametricRoadWorldSubsystem::Find(const_cast<AFGBlueprintHologram*>(Hologram));
        return Session && Session->GetSource() == Hologram && Session->IsActive();
    }

    bool IsLocalRoadGun(const AFGBuildGun* Gun)
    {
        const AFGCharacterPlayer* Player = Gun ? Gun->GetInstigatorCharacter() : nullptr;
        return Player && Player->IsLocallyControlled()
            && (Gun->GetCurrentBuildGunMode() == UParametricRoadBuildMode::StaticClass()
                || UParametricRoadWorldSubsystem::IsPresetLoading(const_cast<AFGBuildGun*>(Gun)));
    }
}

void FRoadworksModule::QueueNativeServerBlueprint(AFGBuildGun* Gun, const FString& Name, TSubclassOf<UFGRecipe> Recipe)
{
    if (!Gun || Gun->GetNetMode() != NM_Client || Name.IsEmpty() || !Recipe) { return; }
    // This is the game's own GotoBuildState ordering. Selection alone updates
    // metadata; the same blueprint recipe deliberately respawns the native
    // server hologram. Both reliable calls precede the construct subobject RPC.
    Gun->Server_SetDesiredBlueprint(Name);
    Gun->Server_GotoBuildState(Recipe);
}

void FRoadworksModule::StartupModule()
{
    // The pre-release plugin used a different mount and script package. Keep
    // old soft references resolvable without loading both plugin versions.
    const FCoreRedirect Redirects[] = {
        {ECoreRedirectFlags::Type_Package, TEXT("/Script/ParametricRoads"), TEXT("/Script/Roadworks")},
        {ECoreRedirectFlags::Type_Package | ECoreRedirectFlags::Option_MatchPrefix,
            TEXT("/ParametricRoads/"), TEXT("/Roadworks/")}
    };
    FCoreRedirects::AddRedirectList(Redirects, TEXT("RoadworksPreReleaseRename"));
#if WITH_EDITOR
    // PCM asset cooking/reflection also runs with -nosound, which skips the
    // audio-device decoder registration. This loads only the native CPU factory.
    FModuleManager::Get().LoadModuleChecked<IModuleInterface>(TEXT("AdpcmAudioDecoder"));
#endif
    // Extend only the native type's scan directories before StartInitialLoading
    // broadcasts readiness to user settings. This is process-local discovery
    // metadata; do not SaveConfig or register after settings have initialized.
    for (FPrimaryAssetTypeInfo& Type : GetMutableDefault<UAssetManagerSettings>()->PrimaryAssetTypesToScan)
    {
        if (Type.PrimaryAssetType == FPrimaryAssetType(TEXT("FGUserSetting"))
            && !Type.GetDirectories().ContainsByPredicate([](const FDirectoryPath& Path) { return Path.Path == TEXT("/Roadworks/Settings"); }))
        {
            FDirectoryPath Path; Path.Path = TEXT("/Roadworks/Settings"); Type.GetDirectories().Add(Path);
        }
    }
    UE_LOG(LogRoadworks, Display, TEXT("Roadworks module loaded; gameplay hooks await SML initialization."));
}

void FRoadworksModule::InstallGameplayHooks()
{
    if (WITH_EDITOR || bHooksInstalled)
    {
        return;
    }
    bHooksInstalled = true;
    // This is a regular SML plugin, rather than a GameFeatures-directory
    // plugin. Register its one primary input context explicitly before native
    // Controls discovers FGChildInputMappingContext assets.
    UAssetManager::Get().ScanPathsForPrimaryAssets(FPrimaryAssetType(TEXT("FGChildInputMappingContext")),
        {TEXT("/Roadworks/Inputs")}, UFGChildInputMappingContext::StaticClass(), false, false, true);

    // UFUNCTION wrapper: includes Blueprint overrides and preserves existing modes.
    const auto ModesHandle = SUBSCRIBE_UOBJECT_METHOD_AFTER(AFGHologram, GetSupportedBuildModes,
        [](const AFGHologram* Hologram, TArray<TSubclassOf<UFGBuildGunModeDescriptor>>& Modes)
        {
            if (Cast<AFGBlueprintHologram>(Hologram))
            {
                Modes.AddUnique(UParametricRoadBuildMode::StaticClass());
            }
        });
    RemoveHooks.Add([ModesHandle] {
        UNSUBSCRIBE_UOBJECT_METHOD(AFGHologram, GetSupportedBuildModes, ModesHandle);
    });

    const auto PlacementHandle = SUBSCRIBE_METHOD_VIRTUAL_AFTER(
        AFGBlueprintHologram::PostHologramPlacement, GetMutableDefault<AFGBlueprintHologram>(),
        [](AFGBlueprintHologram* Hologram, const FHitResult& Hit, bool)
        {
            if (UParametricRoadSession* Session = UParametricRoadWorldSubsystem::Find(Hologram, true);
                Session && Session->IsActive())
            {
                Session->UpdateNativePlacement(Hit, Hologram->IsValidHitResult(Hit));
            }
        });
    RemoveHooks.Add([PlacementHandle] {
        UNSUBSCRIBE_METHOD(AFGBlueprintHologram::PostHologramPlacement, PlacementHandle);
    });
    // Reuse the game's rebindable actions only while this local tool is active.
    const auto PrimaryHandle = SUBSCRIBE_UOBJECT_METHOD(UFGBuildGunState, PrimaryFire,
        [](auto& Scope, UFGBuildGunState* State)
        {
            if (State && UParametricRoadWorldSubsystem::IsPresetLoading(State->GetBuildGun())) { Scope.Cancel(); return; }
            if (auto* Session = UParametricRoadWorldSubsystem::Find(Cast<UFGBuildGunStateBuild>(State), true);
                Session && Session->IsActive()) { Scope.Cancel(); Session->Primary(); }
        });
    RemoveHooks.Add([PrimaryHandle] { UNSUBSCRIBE_UOBJECT_METHOD(UFGBuildGunState, PrimaryFire, PrimaryHandle); });
    const auto ReleaseHandle = SUBSCRIBE_UOBJECT_METHOD(UFGBuildGunState, PrimaryFireRelease,
        [](auto& Scope, UFGBuildGunState* State)
        {
            if (State && UParametricRoadWorldSubsystem::IsPresetLoading(State->GetBuildGun())) { Scope.Cancel(); return; }
            if (auto* Session = UParametricRoadWorldSubsystem::Find(Cast<UFGBuildGunStateBuild>(State));
                Session && Session->IsActive()) { Scope.Cancel(); }
        });
    RemoveHooks.Add([ReleaseHandle] { UNSUBSCRIBE_UOBJECT_METHOD(UFGBuildGunState, PrimaryFireRelease, ReleaseHandle); });
    const auto SecondaryHandle = SUBSCRIBE_UOBJECT_METHOD(UFGBuildGunState, SecondaryFire,
        [](auto& Scope, UFGBuildGunState* State)
        {
            if (State && UParametricRoadWorldSubsystem::IsPresetLoading(State->GetBuildGun())) { Scope.Cancel(); return; }
            if (auto* Session = UParametricRoadWorldSubsystem::Find(Cast<UFGBuildGunStateBuild>(State));
                Session && Session->IsActive())
            {
                if (Session->ConsumesSecondary()) { Scope.Cancel(); Session->Secondary(); }
                else { Session->Shutdown(); }
            }
        });
    RemoveHooks.Add([SecondaryHandle] { UNSUBSCRIBE_UOBJECT_METHOD(UFGBuildGunState, SecondaryFire, SecondaryHandle); });
    const auto ScrollHandle = SUBSCRIBE_UOBJECT_METHOD(UFGBuildGunState, Scroll,
        [](auto& Scope, UFGBuildGunState* State, int32 Delta)
        {
            if (auto* Session = UParametricRoadWorldSubsystem::Find(Cast<UFGBuildGunStateBuild>(State));
                Session && Session->IsActive() && Session->CanUseNativeRotation())
            { Scope.Cancel(); Session->RotateHeading(Delta); }
        });
    RemoveHooks.Add([ScrollHandle] { UNSUBSCRIBE_UOBJECT_METHOD(UFGBuildGunState, Scroll, ScrollHandle); });
    const auto EndStateHandle = SUBSCRIBE_UOBJECT_METHOD(UFGBuildGunState, EndState,
        [](auto&, UFGBuildGunState* State)
        {
            if (auto* Session = UParametricRoadWorldSubsystem::Find(Cast<UFGBuildGunStateBuild>(State))) { Session->Shutdown(); }
        });
    RemoveHooks.Add([EndStateHandle] { UNSUBSCRIBE_UOBJECT_METHOD(UFGBuildGunState, EndState, EndStateHandle); });

    // The input RPCs would otherwise execute ordinary single-placement actions
    // on a remote vanilla server even though this client consumed those inputs.
    const auto PrimaryRpcHandle = SUBSCRIBE_UOBJECT_METHOD(AFGBuildGun, Server_PrimaryFire,
        [](auto& Scope, AFGBuildGun* Gun) { if (IsLocalRoadGun(Gun)) { Scope.Cancel(); } });
    RemoveHooks.Add([PrimaryRpcHandle] { UNSUBSCRIBE_UOBJECT_METHOD(AFGBuildGun, Server_PrimaryFire, PrimaryRpcHandle); });
    const auto ReleaseRpcHandle = SUBSCRIBE_UOBJECT_METHOD(AFGBuildGun, Server_PrimaryFireReleased,
        [](auto& Scope, AFGBuildGun* Gun) { if (IsLocalRoadGun(Gun)) { Scope.Cancel(); } });
    RemoveHooks.Add([ReleaseRpcHandle] { UNSUBSCRIBE_UOBJECT_METHOD(AFGBuildGun, Server_PrimaryFireReleased, ReleaseRpcHandle); });
    const auto SecondaryRpcHandle = SUBSCRIBE_UOBJECT_METHOD(AFGBuildGun, Server_SecondaryFire,
        [](auto& Scope, AFGBuildGun* Gun) { if (IsLocalRoadGun(Gun)) { Scope.Cancel(); } });
    RemoveHooks.Add([SecondaryRpcHandle] { UNSUBSCRIBE_UOBJECT_METHOD(AFGBuildGun, Server_SecondaryFire, SecondaryRpcHandle); });

    // Incoming RPC dispatch invokes _Implementation directly. Hooking only the
    // C++ sending wrapper would miss acknowledgements on the builder client.
    const auto BuiltHandle = SUBSCRIBE_METHOD_VIRTUAL_AFTER(
        UFGBuildGunStateBuild::Client_OnBlueprintConstructed_Implementation, GetMutableDefault<UFGBuildGunStateBuild>(),
        [](UFGBuildGunStateBuild* State, const FString& Name, int32 Count)
        {
            if (auto* Session = UParametricRoadWorldSubsystem::Find(State)) { Session->AcknowledgeBlueprint(Name, Count); }
        });
    RemoveHooks.Add([BuiltHandle] { UNSUBSCRIBE_METHOD(UFGBuildGunStateBuild::Client_OnBlueprintConstructed_Implementation, BuiltHandle); });
    const auto FailedHandle = SUBSCRIBE_METHOD_VIRTUAL_AFTER(
        UFGBuildGunStateBuild::Client_OnBuildableFailedConstruction_Implementation, GetMutableDefault<UFGBuildGunStateBuild>(),
        [](UFGBuildGunStateBuild* State, FNetConstructionID Id)
        {
            if (auto* Session = UParametricRoadWorldSubsystem::Find(State)) { Session->RefuseConstruction(Id); }
        });
    RemoveHooks.Add([FailedHandle] { UNSUBSCRIBE_METHOD(UFGBuildGunStateBuild::Client_OnBuildableFailedConstruction_Implementation, FailedHandle); });
    const auto ConstructionHandle = SUBSCRIBE_METHOD(UFGBuildGunStateBuild::InternalConstructHologram,
        [](auto&, UFGBuildGunStateBuild* State, FNetConstructionID Id)
        {
            if (auto* Session = UParametricRoadWorldSubsystem::Find(State)) { Session->ObserveNativeConstruction(Id); }
        });
    RemoveHooks.Add([ConstructionHandle] { UNSUBSCRIBE_METHOD(UFGBuildGunStateBuild::InternalConstructHologram, ConstructionHandle); });

    // Never send the mod's descriptor to a server that only understands vanilla.
    // The queue sends only native recipe/hologram construction requests.
    const auto ModeRpcHandle = SUBSCRIBE_UOBJECT_METHOD(AFGBuildGun, Server_SetCurrentBuildGunMode,
        [](auto& Scope, AFGBuildGun* Gun, TSubclassOf<UFGBuildGunModeDescriptor> Mode)
        {
            if (Mode == UParametricRoadBuildMode::StaticClass())
            {
                Scope.Cancel();
            }
            else if (Gun)
            {
                if (auto* Session = UParametricRoadWorldSubsystem::Find(Cast<UFGBuildGunStateBuild>(Gun->GetCurrentState())))
                {
                    // Restore before the normal mode RPC is sent. EndState on a
                    // different user selection must not send a late build-state
                    // RPC that would override that newer selection.
                    Session->RestoreNativeServerSource();
                    Session->Shutdown();
                }
            }
        });
    RemoveHooks.Add([ModeRpcHandle] {
        UNSUBSCRIBE_UOBJECT_METHOD(AFGBuildGun, Server_SetCurrentBuildGunMode, ModeRpcHandle);
    });

    const auto SerializeHandle = SUBSCRIBE_METHOD_VIRTUAL(
        AFGBlueprintHologram::SerializeConstructMessage, GetMutableDefault<AFGBlueprintHologram>(),
        [](auto& Scope, AFGBlueprintHologram* Hologram, FArchive& Archive, FNetConstructionID Id)
        {
            if (Archive.IsSaving())
            {
                if (auto* Session = UParametricRoadWorldSubsystem::Find(Hologram)) { Session->ObserveNativeConstruction(Id); }
            }
            if (!Archive.IsSaving() || !IsLocalRoadPreview(Hologram))
            {
                return;
            }
            Hologram->SetBuildModeOverride(nullptr);
            Scope(Hologram, Archive, Id);
            Hologram->SetBuildModeOverride(UParametricRoadBuildMode::StaticClass());
        });
    RemoveHooks.Add([SerializeHandle] {
        UNSUBSCRIBE_METHOD(AFGBlueprintHologram::SerializeConstructMessage, SerializeHandle);
    });

    // Hosts must not export their local tool descriptor through equipment state
    // either. This only gates the single mode property while our mode is active.
    const auto ReplicationHandle = SUBSCRIBE_METHOD_VIRTUAL_AFTER(
        AActor::PreReplication, GetMutableDefault<AFGBuildGun>(),
        [](AActor* Actor, IRepChangedPropertyTracker&)
        {
            AFGBuildGun* Gun = Cast<AFGBuildGun>(Actor);
            if (!Gun)
            {
                return;
            }
            const bool bRoadMode = Gun->GetCurrentBuildGunMode() == UParametricRoadBuildMode::StaticClass();
            if (!bRoadMode && !SuppressedModeReplication.Contains(Gun))
            {
                return;
            }
            const FProperty* Property = FindFProperty<FProperty>(AFGBuildGun::StaticClass(), TEXT("mCurrentBuildGunMode"));
            if (ensure(Property && Property->HasAnyPropertyFlags(CPF_Net)))
            {
                UE::Net::Private::FNetPropertyConditionManager::Get()
                    .SetPropertyActiveOverride(Gun, Property->RepIndex, !bRoadMode);
                if (bRoadMode)
                {
                    SuppressedModeReplication.Add(Gun);
                }
                else
                {
                    SuppressedModeReplication.Remove(Gun);
                }
            }
        });
    RemoveHooks.Add([ReplicationHandle] {
        UNSUBSCRIBE_METHOD(AActor::PreReplication, ReplicationHandle);
    });

    UE_LOG(LogRoadworks, Display, TEXT("Roadworks build mode registered."));
}

void FRoadworksModule::ShutdownModule()
{
    for (int32 Index = RemoveHooks.Num() - 1; Index >= 0; --Index)
    {
        RemoveHooks[Index]();
    }
    RemoveHooks.Empty();
    SuppressedModeReplication.Empty();
    bHooksInstalled = false;
}

IMPLEMENT_MODULE(FRoadworksModule, Roadworks)
