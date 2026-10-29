#include "ParametricRoadEligibility.h"

#include "Buildables/FGBuildable.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "FGRecipe.h"
#include "Hologram/FGBlueprintHologram.h"
#include "UObject/UnrealType.h"

namespace
{
    bool IsVanillaPackage(const FString& Package)
    {
        if (Package.StartsWith(TEXT("/Game/FactoryGame/")) || Package.StartsWith(TEXT("/Engine/")))
        {
            return true;
        }
        // Native modules used by the pinned game. Unknown mounts/modules are
        // rejected, including mod assets inside an otherwise vanilla building.
        static const TSet<FString> NativePackages = {
            TEXT("/Script/FactoryGame"), TEXT("/Script/Engine"), TEXT("/Script/CoreUObject"),
            TEXT("/Script/AbstractInstance"), TEXT("/Script/Foliage"), TEXT("/Script/Niagara"),
            TEXT("/Script/PhysicsCore"), TEXT("/Script/GeometryCollectionEngine"),
            TEXT("/Script/AIModule"), TEXT("/Script/GameplayTasks"), TEXT("/Script/NavigationSystem"),
            TEXT("/Script/AkAudio"), TEXT("/Script/AnimGraphRuntime")
        };
        return NativePackages.Contains(Package);
    }

    struct FSavedReferences
    {
        TSet<const UObject*> Seen;
        int32 Budget = 50000;
        FString Rejected;

        bool Package(const FString& Name)
        {
            if (!Name.IsEmpty() && !IsVanillaPackage(Name))
            {
                Rejected = Name;
                return false;
            }
            return true;
        }

        bool Object(UObject* Value)
        {
            if (!Value || Seen.Contains(Value))
            {
                return true;
            }
            if (Seen.Num() >= 2048 || --Budget < 0)
            {
                Rejected = TEXT("too many references");
                return false;
            }
            Seen.Add(Value);
            if (!Package(Value->GetClass()->GetOutermost()->GetName()))
            {
                return false;
            }
            if (Value->IsA<UClass>() || Value->IsAsset())
            {
                return Package(Value->GetOutermost()->GetName());
            }
            for (TFieldIterator<FProperty> Field(Value->GetClass()); Field; ++Field)
            {
                if (Field->HasAnyPropertyFlags(CPF_SaveGame))
                {
                    for (int32 I = 0; I < Field->ArrayDim; ++I)
                    {
                        if (!Property(*Field, Field->ContainerPtrToValuePtr<void>(Value, I)))
                        {
                            return false;
                        }
                    }
                }
            }
            return true;
        }

        bool Property(const FProperty* Field, const void* Data)
        {
            if (--Budget < 0)
            {
                Rejected = TEXT("too much data");
                return false;
            }
            if (const FSoftObjectProperty* Soft = CastField<FSoftObjectProperty>(Field))
            {
                return Package(Soft->GetPropertyValue(Data).ToSoftObjectPath().GetLongPackageName());
            }
            if (const FObjectPropertyBase* Ref = CastField<FObjectPropertyBase>(Field))
            {
                return Object(Ref->GetObjectPropertyValue(Data));
            }
            if (CastField<FInterfaceProperty>(Field))
            {
                return Object(static_cast<const FScriptInterface*>(Data)->GetObject());
            }
            if (const FStructProperty* Struct = CastField<FStructProperty>(Field))
            {
                for (TFieldIterator<FProperty> Inner(Struct->Struct); Inner; ++Inner)
                {
                    for (int32 I = 0; I < Inner->ArrayDim; ++I)
                    {
                        if (!Property(*Inner, Inner->ContainerPtrToValuePtr<void>(Data, I)))
                        {
                            return false;
                        }
                    }
                }
            }
            else if (const FArrayProperty* Array = CastField<FArrayProperty>(Field))
            {
                FScriptArrayHelper Helper(Array, Data);
                for (int32 I = 0; I < Helper.Num(); ++I)
                {
                    if (!Property(Array->Inner, Helper.GetRawPtr(I))) { return false; }
                }
            }
            else if (const FMapProperty* Map = CastField<FMapProperty>(Field))
            {
                FScriptMapHelper Helper(Map, Data);
                for (int32 I = 0; I < Helper.GetMaxIndex(); ++I)
                {
                    if (Helper.IsValidIndex(I) && (!Property(Map->KeyProp, Helper.GetKeyPtr(I))
                        || !Property(Map->ValueProp, Helper.GetValuePtr(I)))) { return false; }
                }
            }
            else if (const FSetProperty* Set = CastField<FSetProperty>(Field))
            {
                FScriptSetHelper Helper(Set, Data);
                for (int32 I = 0; I < Helper.GetMaxIndex(); ++I)
                {
                    if (Helper.IsValidIndex(I) && !Property(Set->ElementProp, Helper.GetElementPtr(I))) { return false; }
                }
            }
            return true;
        }
    };
}

bool ParametricRoadEligibility::Inspect(AFGBlueprintHologram* Hologram, FBox& OutLocalBounds,
    int32& OutMembers, FText& OutReason)
{
    OutLocalBounds = FBox(ForceInit);
    OutMembers = Hologram ? Hologram->mBuildableToNewRoot.Num() : 0;
    if (!Hologram || OutMembers == 0)
    {
        OutReason = NSLOCTEXT("ParametricRoads", "EmptyBlueprint", "Blueprint empty or still loading.");
        return false;
    }
    if (OutMembers > 1024)
    {
        OutReason = NSLOCTEXT("ParametricRoads", "LargeBlueprint", "This blueprint exceeds 1,024 elements per slice.");
        return false;
    }
    FSavedReferences References;
    for (const auto& Pair : Hologram->mBuildableToNewRoot)
    {
        AFGBuildable* Buildable = Pair.Key;
        if (!IsValid(Buildable) || !References.Object(Buildable)
            || !References.Object(Buildable->GetBuiltWithRecipe().Get()))
        {
            OutReason = FText::Format(NSLOCTEXT("ParametricRoads", "ModdedBlueprint",
                "Blueprint content is not vanilla or cannot be verified: {0}"), FText::FromString(References.Rejected));
            return false;
        }
        TArray<USceneComponent*> Components;
        USceneComponent* Root = Pair.Value;
        if (!Root) { continue; }
        Root->GetChildrenComponents(true, Components);
        Components.Add(Root);
        for (USceneComponent* Component : Components)
        {
            if (const UStaticMeshComponent* Mesh = Cast<UStaticMeshComponent>(Component); Mesh && Mesh->GetStaticMesh())
            {
                OutLocalBounds += Mesh->CalcBounds(Mesh->GetComponentTransform()
                    .GetRelativeTransform(Hologram->GetActorTransform())).GetBox();
            }
        }
    }
    if (!OutLocalBounds.IsValid || OutLocalBounds.GetSize().ContainsNaN()
        || OutLocalBounds.GetCenter().ContainsNaN() || OutLocalBounds.GetSize().GetMin() < 0.1
        || OutLocalBounds.GetSize().GetAbsMax() > 100000.0 || OutLocalBounds.GetCenter().GetAbsMax() > 100000.0)
    {
        OutReason = NSLOCTEXT("ParametricRoads", "UnknownBlueprintBounds", "Unable to measure the road deck in this blueprint.");
        return false;
    }
    return true;
}
