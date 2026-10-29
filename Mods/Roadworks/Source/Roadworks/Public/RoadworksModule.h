#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class AFGBuildGun;
class UFGRecipe;

DECLARE_LOG_CATEGORY_EXTERN(LogRoadworks, Log, All);

class FRoadworksModule : public IModuleInterface
{
public:
    void StartupModule() override;
    void ShutdownModule() override;
    bool SupportsDynamicReloading() override { return false; }

    /** Called once the SML game instance has initialized, never in the editor. */
    void InstallGameplayHooks();
    /** Existing reliable vanilla RPCs, on the same gun channel as construction. */
    static void QueueNativeServerBlueprint(AFGBuildGun* Gun, const FString& Name, TSubclassOf<UFGRecipe> Recipe);

private:
    bool bHooksInstalled = false;
    TArray<TFunction<void()>> RemoveHooks;
};
