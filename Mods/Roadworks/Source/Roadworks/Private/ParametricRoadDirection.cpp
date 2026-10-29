#include "ParametricRoadDirection.h"
#include "RoadworksModule.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Hologram/FGBlueprintHologram.h"
#include "Materials/MaterialInterface.h"

bool UParametricRoadDirection::Initialize(AFGBlueprintHologram* Source)
{
    Clear();
    if (!Source || !Source->GetWorld() || Source->GetNetMode() == NM_DedicatedServer) { return false; }
    // Unlike the exported FactoryGame helpers, this is the engine's real XY
    // plane. Its material defines a flat +X arrow without blueprint transforms,
    // scale, pivot, custom primitive data or native outline flags.
    UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Roadworks/Monitor/Meshes/SM_RoadDirectionPlane.SM_RoadDirectionPlane"));
    UMaterialInterface* DirectionMaterial = LoadObject<UMaterialInterface>(nullptr,
        TEXT("/Roadworks/Monitor/Materials/M_RoadDirectionHologram.M_RoadDirectionHologram"));
    if (!Mesh || !DirectionMaterial) { return false; }
    FActorSpawnParameters Spawn;
    Spawn.Owner = Source->GetOwner();
    Spawn.ObjectFlags = RF_Transient | RF_TextExportTransient | RF_DuplicateTransient;
    Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    Presentation = Source->GetWorld()->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Spawn);
    if (!Presentation) { return false; }
    Presentation->SetReplicates(false); Presentation->SetActorTickEnabled(false); Presentation->SetActorEnableCollision(false);
    Marker = NewObject<UStaticMeshComponent>(Presentation, NAME_None,
        RF_Transient | RF_TextExportTransient | RF_DuplicateTransient);
    Presentation->SetRootComponent(Marker); Presentation->AddInstanceComponent(Marker);
    Marker->SetStaticMesh(Mesh); Marker->SetMaterial(0, DirectionMaterial);
    Marker->SetMobility(EComponentMobility::Movable); Marker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Marker->SetGenerateOverlapEvents(false); Marker->SetCanEverAffectNavigation(false);
    Marker->SetComponentTickEnabled(false); Marker->SetCastShadow(false); Marker->SetIsReplicated(false);
    Marker->SetRenderCustomDepth(false); Marker->SetHiddenInGame(false); Marker->SetVisibility(false);
    Marker->RegisterComponent();
    UE_LOG(LogRoadworks, Log, TEXT("Road direction uses one flat 160 x 64 cm +X marker."));
    return true;
}

void UParametricRoadDirection::Update(const TOptional<FTransform>& RoadFrame, double SliceLength)
{
    if (!IsValid(Marker)) { return; }
    const bool bShow = RoadFrame.IsSet() && !RoadFrame->ContainsNaN() && FMath::IsFinite(SliceLength);
    Marker->SetVisibility(bShow);
    if (bShow) { Marker->SetWorldTransform(ParametricRoadDirection::MarkerPose(RoadFrame.GetValue(), SliceLength),
        false, nullptr, ETeleportType::TeleportPhysics); }
}
void UParametricRoadDirection::Clear()
{
    if (IsValid(Presentation)) { Presentation->Destroy(); }
    Presentation = nullptr; Marker = nullptr;
}
