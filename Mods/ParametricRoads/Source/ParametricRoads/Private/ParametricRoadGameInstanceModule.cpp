#include "ParametricRoadGameInstanceModule.h"
#include "ParametricRoadsModule.h"

UParametricRoadGameInstanceModule::UParametricRoadGameInstanceModule()
{
    bRootModule = true;
}

void UParametricRoadGameInstanceModule::DispatchLifecycleEvent(ELifecyclePhase Phase)
{
    Super::DispatchLifecycleEvent(Phase);
    if (!WITH_EDITOR && Phase == ELifecyclePhase::INITIALIZATION)
    {
        FModuleManager::GetModuleChecked<FParametricRoadsModule>(TEXT("ParametricRoads"))
            .InstallGameplayHooks();
    }
}
