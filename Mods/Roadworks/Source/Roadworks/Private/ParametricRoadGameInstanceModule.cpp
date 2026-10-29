#include "ParametricRoadGameInstanceModule.h"
#include "RoadworksModule.h"

UParametricRoadGameInstanceModule::UParametricRoadGameInstanceModule()
{
    bRootModule = true;
}

void UParametricRoadGameInstanceModule::DispatchLifecycleEvent(ELifecyclePhase Phase)
{
    Super::DispatchLifecycleEvent(Phase);
    if (!WITH_EDITOR && Phase == ELifecyclePhase::INITIALIZATION)
    {
        FModuleManager::GetModuleChecked<FRoadworksModule>(TEXT("Roadworks"))
            .InstallGameplayHooks();
    }
}
