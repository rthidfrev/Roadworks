#pragma once
#include "CoreMinimal.h"
class UUserWidget;
namespace ParametricRoadNativeHints
{
    inline constexpr const TCHAR* BuildModePath = TEXT("/Game/FactoryGame/Interface/UI/InGame/BuildMenu/Widget_BuildMode.Widget_BuildMode_C");
    bool ResolveContract(UClass* Class, FString& Error);
}
