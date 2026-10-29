#include "ParametricRoadPreview.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SplineMeshComponent.h"
#include "Components/MeshComponent.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Hologram/FGBlueprintHologram.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
    void VisualOnly(UPrimitiveComponent* Component)
    {
        Component->SetFlags(RF_Transient | RF_TextExportTransient);
        Component->SetMobility(EComponentMobility::Movable);
        Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Component->SetGenerateOverlapEvents(false);
        Component->SetCanEverAffectNavigation(false);
        Component->SetComponentTickEnabled(false);
        Component->SetCastShadow(false);
        Component->SetVisibility(true, false);
        Component->SetHiddenInGame(false, false);
    }
}

bool UParametricRoadPreview::FAppearance::operator==(const FAppearance& Other) const
{
    return Materials == Other.Materials && PrimitiveData == Other.PrimitiveData
        && Stencil == Other.Stencil && StencilWriteMask == Other.StencilWriteMask
        && bCustomDepth == Other.bCustomDepth;
}

UMeshComponent* UParametricRoadPreview::SnapshotTemplate(UMeshComponent* Source, UObject* Outer)
{
    // Duplicate the concrete type, preserving spline deformation and native ISM
    // instance/custom data. Templates never register or enter a render scene.
    UMeshComponent* Snapshot = DuplicateObject<UMeshComponent>(Source, Outer);
    Snapshot->ClearFlags(RF_Public | RF_Standalone | RF_Transactional);
    Snapshot->SetupAttachment(nullptr);
    Snapshot->SetRelativeTransform(FTransform::Identity);
    VisualOnly(Snapshot);
    // The runtime primitive-data array is Transient and is skipped by normal
    // duplication. Capture it explicitly instead of losing native customization.
    Snapshot->SetCustomPrimitiveDataFloatArray(0, Source->GetCustomPrimitiveData().Data);
    return Snapshot;
}

UParametricRoadPreview::FAppearance UParametricRoadPreview::CaptureAppearance(
    UMeshComponent* Source, TMap<UMaterialInterface*, UMaterialInterface*>& MaterialCache)
{
    FAppearance Result;
    for (int32 I = 0; I < Source->GetNumMaterials(); ++I)
    {
        UMaterialInterface* Material = Source->GetMaterial(I);
        UMaterialInterface** Existing = MaterialCache.Find(Material);
        if (Existing) { Result.Materials.Add(*Existing); continue; }
        UMaterialInterface* Captured = Material;
        if (auto* Dynamic = Cast<UMaterialInstanceDynamic>(Material))
        {
            // Do not retain a native mutable MID that another blueprint load or
            // subsequent material-state change can reuse. Preserve every authored
            // parameter type through duplication, rather than copying only floats.
            Captured = DuplicateObject<UMaterialInstanceDynamic>(Dynamic, this);
            Captured->ClearFlags(RF_Public | RF_Standalone | RF_Transactional);
            Captured->SetFlags(RF_Transient | RF_TextExportTransient);
        }
        MaterialCache.Add(Material, Captured);
        if (Captured) { CapturedMaterials.AddUnique(Captured); }
        Result.Materials.Add(Captured);
    }
    Result.PrimitiveData = Source->GetCustomPrimitiveData().Data;
    Result.bCustomDepth = Source->bRenderCustomDepth;
    Result.Stencil = Source->CustomDepthStencilValue;
    Result.StencilWriteMask = uint8(Source->CustomDepthStencilWriteMask);
    return Result;
}

void UParametricRoadPreview::ApplyAppearance(UMeshComponent* Mesh, const FAppearance& State)
{
    if (!Mesh) { return; }
    for (int32 I = 0; I < State.Materials.Num(); ++I) { Mesh->SetMaterial(I, State.Materials[I]); }
    Mesh->ResetCustomPrimitiveData();
    Mesh->SetCustomPrimitiveDataFloatArray(0, State.PrimitiveData);
    Mesh->SetRenderCustomDepth(State.bCustomDepth);
    Mesh->SetCustomDepthStencilValue(State.Stencil);
    Mesh->SetCustomDepthStencilWriteMask(ERendererStencilMask(State.StencilWriteMask));
}

bool UParametricRoadPreview::Initialize(AFGBlueprintHologram* Source)
{
    Clear();
    if (!Source || !Source->GetWorld()) { return false; }
    FActorSpawnParameters Spawn;
    Spawn.ObjectFlags |= RF_Transient | RF_TextExportTransient;
    Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    Owner = Source->GetWorld()->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Spawn);
    if (!Owner) { return false; }
    Owner->SetReplicates(false);
    Owner->SetActorEnableCollision(false);
    Owner->SetActorTickEnabled(false);
    USceneComponent* Root = NewObject<USceneComponent>(Owner);
    Owner->SetRootComponent(Root);
    Owner->AddInstanceComponent(Root);
    Root->RegisterComponent();
    struct FSourceMesh
    {
        UMeshComponent* Mesh = nullptr;
        FTransform LocalPose;
        bool bInstance = false;
        TArray<FAppearance> Appearances;
    };
    TArray<FSourceMesh> SourceMeshes;
    TSet<UMeshComponent*> Seen;
    for (const auto& Member : Source->mBuildableToNewRoot)
    {
        if (!Member.Value) { continue; }
        TArray<USceneComponent*> Components;
        Member.Value->GetChildrenComponents(true, Components);
        Components.Add(Member.Value);
        for (USceneComponent* Component : Components)
        {
            UMeshComponent* Mesh = Cast<UMeshComponent>(Component);
            if (!Mesh || Seen.Contains(Mesh) || !Mesh->IsVisible() || Mesh->bHiddenInGame) { continue; }
            Seen.Add(Mesh);
            UStaticMeshComponent* Static = Cast<UStaticMeshComponent>(Mesh);
            FSourceMesh& Entry = SourceMeshes.AddDefaulted_GetRef();
            Entry.Mesh = Mesh;
            Entry.LocalPose = Mesh->GetComponentTransform().GetRelativeTransform(Source->GetActorTransform());
            Entry.bInstance = Static && Static->GetStaticMesh() && !Mesh->IsA<USplineMeshComponent>() && !Mesh->IsA<UInstancedStaticMeshComponent>();
        }
    }
    const EHologramMaterialState OriginalState = Source->GetHologramMaterialState();
    for (EHologramMaterialState State : {EHologramMaterialState::HMS_OK,
        EHologramMaterialState::HMS_WARNING, EHologramMaterialState::HMS_ERROR})
    {
        Source->SetPlacementMaterialState(State);
        TMap<UMaterialInterface*, UMaterialInterface*> MaterialCache;
        for (FSourceMesh& Entry : SourceMeshes) { Entry.Appearances.Add(CaptureAppearance(Entry.Mesh, MaterialCache)); }
    }
    Source->SetPlacementMaterialState(OriginalState);
    const uint8 InitialState = uint8(OriginalState) <= uint8(EHologramMaterialState::HMS_ERROR) ? uint8(OriginalState) : 0;
    for (FSourceMesh& Entry : SourceMeshes)
    {
        UStaticMeshComponent* Static = Cast<UStaticMeshComponent>(Entry.Mesh);
        FGroup* Shared = nullptr;
        if (Entry.bInstance)
        {
            for (FGroup& Existing : Groups)
            {
                auto* Batch = Existing.Instances.Get();
                if (Batch && Batch->GetStaticMesh() == Static->GetStaticMesh()
                    && Existing.Appearances == Entry.Appearances) { Shared = &Existing; break; }
            }
        }
        if (Shared) { Shared->LocalPoses.Add(Entry.LocalPose); continue; }
        FGroup& Group = Groups.AddDefaulted_GetRef();
        UMeshComponent* Template = SnapshotTemplate(Entry.Mesh, Owner);
        Owner->AddInstanceComponent(Template);
        SnapshotTemplates.Add(Template);
        Group.Template = Template;
        Group.Appearances = MoveTemp(Entry.Appearances);
        ApplyAppearance(Template, Group.Appearances[InitialState]);
        Group.LocalPoses.Add(Entry.LocalPose);
        if (Entry.bInstance)
        {
            auto* Instances = NewObject<UInstancedStaticMeshComponent>(Owner);
            Owner->AddInstanceComponent(Instances);
            Instances->SetupAttachment(Root);
            Instances->SetStaticMesh(Static->GetStaticMesh());
            VisualOnly(Instances);
            ApplyAppearance(Instances, Group.Appearances[InitialState]);
            Instances->RegisterComponent();
            Group.Instances = Instances;
        }
    }
    Appearance = InitialState;
    Owner->SetActorHiddenInGame(true);
    return !Groups.IsEmpty();
}

void UParametricRoadPreview::Update(const TArray<FTransform>& Poses)
{
    if (!Owner) { return; }
    if (Poses.IsEmpty()) { Owner->SetActorHiddenInGame(true); LastPoses.Reset(); NextGroup = 0; return; }
    bool bChanged = LastPoses.Num() != Poses.Num();
    if (!bChanged) { for (int32 I = 0; I < Poses.Num(); ++I) { if (!Poses[I].Equals(LastPoses[I], 0.01)) { bChanged = true; break; } } }
    if (bChanged) { LastPoses = Poses; NextGroup = 0; }
    if (NextGroup >= Groups.Num()) { Owner->SetActorHiddenInGame(false); return; }
    const double Deadline = FPlatformTime::Seconds() + 0.002;
    for (; NextGroup < Groups.Num(); ++NextGroup)
    {
        FGroup& Group = Groups[NextGroup];
        TArray<FTransform> Transforms; Transforms.Reserve(Poses.Num() * Group.LocalPoses.Num());
        for (const FTransform& Pose : Poses) { for (const FTransform& Local : Group.LocalPoses) { Transforms.Add(Local * Pose); } }
        if (auto* Instances = Group.Instances.Get())
        {
            if (Instances->GetInstanceCount() != Transforms.Num())
            {
                Instances->ClearInstances();
                Instances->AddInstances(Transforms, false, false, false);
            }
            else { Instances->BatchUpdateInstancesTransforms(0, Transforms, false, true, true); }
        }
        else if (auto* Template = Group.Template.Get())
        {
            while (Group.Copies.Num() < Poses.Num() && FPlatformTime::Seconds() < Deadline)
            {
                auto* Copy = DuplicateObject<UMeshComponent>(Template, Owner);
                Owner->AddInstanceComponent(Copy);
                Copy->SetupAttachment(Owner->GetRootComponent());
                VisualOnly(Copy);
                if (Group.Appearances.IsValidIndex(Appearance)) { ApplyAppearance(Copy, Group.Appearances[Appearance]); }
                Copy->RegisterComponent();
                Group.Copies.Add(Copy);
            }
            for (int32 I = 0; I < Group.Copies.Num(); ++I)
            {
                if (auto* Copy = Group.Copies[I].Get())
                {
                    Copy->SetVisibility(I < Transforms.Num(), false);
                    if (Transforms.IsValidIndex(I)) { Copy->SetWorldTransform(Transforms[I], false, nullptr, ETeleportType::TeleportPhysics); }
                }
            }
            if (Group.Copies.Num() < Poses.Num()) { return; }
        }
        if (FPlatformTime::Seconds() >= Deadline) { ++NextGroup; return; }
    }
    Owner->SetActorHiddenInGame(false);
}

void UParametricRoadPreview::SetAppearance(AFGBlueprintHologram*, uint8 MaterialState)
{
    if (Appearance == MaterialState || MaterialState > uint8(EHologramMaterialState::HMS_ERROR)) { return; }
    Appearance = MaterialState;
    for (FGroup& Group : Groups)
    {
        if (!Group.Appearances.IsValidIndex(MaterialState)) { continue; }
        const FAppearance& State = Group.Appearances[MaterialState];
        ApplyAppearance(Group.Template.Get(), State);
        ApplyAppearance(Group.Instances.Get(), State);
        for (auto& Copy : Group.Copies) { ApplyAppearance(Copy.Get(), State); }
    }
}

void UParametricRoadPreview::HideSlice(int32 Slice)
{
    for (FGroup& Group : Groups)
    {
        const int32 First = Slice * Group.LocalPoses.Num();
        if (auto* Instances = Group.Instances.Get(); Instances && Slice >= 0 && First < Instances->GetInstanceCount())
        {
            TArray<FTransform> Hidden;
            Hidden.Init(FTransform(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector), Group.LocalPoses.Num());
            Instances->BatchUpdateInstancesTransforms(First, Hidden, false, true, true);
        }
        else if (Group.Copies.IsValidIndex(Slice)) { if (auto* Copy = Group.Copies[Slice].Get()) { Copy->SetVisibility(false); } }
    }
}

void UParametricRoadPreview::Clear()
{
    if (IsValid(Owner)) { Owner->Destroy(); }
    Owner = nullptr;
    SnapshotTemplates.Reset(); CapturedMaterials.Reset();
    Groups.Reset(); LastPoses.Reset(); Appearance = 255; NextGroup = 0;
}
