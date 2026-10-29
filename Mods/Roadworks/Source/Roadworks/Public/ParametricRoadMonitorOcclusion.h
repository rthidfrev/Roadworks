#pragma once
#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"

/** Own only hidden-in-game flags on native blueprint helper components. Session
 * preview visibility uses SetVisibility independently; never hide whole actors. */
struct FParametricRoadMonitorOcclusion
{
    struct FEntry { TWeakObjectPtr<UPrimitiveComponent> Component; bool bHidden; };
    TArray<FEntry> Entries;
    void Hide(UPrimitiveComponent* Component)
    {
        if (!IsValid(Component)) { return; }
        if (!Entries.ContainsByPredicate([Component](const auto& Entry) { return Entry.Component == Component; }))
            Entries.Add({Component,Component->bHiddenInGame != 0});
        Component->SetHiddenInGame(true,false);
    }
    void ApplyTargets(const TArray<UPrimitiveComponent*>& Targets)
    {
        for (int32 Index = Entries.Num() - 1; Index >= 0; --Index)
        {
            const FEntry& Entry = Entries[Index];
            UPrimitiveComponent* Component = Entry.Component.Get();
            if (!Component || !Targets.Contains(Component))
            {
                if (Component && Component->bHiddenInGame)
                    Component->SetHiddenInGame(Entry.bHidden,false);
                Entries.RemoveAtSwap(Index);
            }
        }
        for (UPrimitiveComponent* Component : Targets) { Hide(Component); }
    }
    void Restore()
    {
        for (const auto& Entry : Entries)
            if (auto* Component = Entry.Component.Get(); Component && Component->bHiddenInGame)
                Component->SetHiddenInGame(Entry.bHidden,false);
        Entries.Reset();
    }
};
