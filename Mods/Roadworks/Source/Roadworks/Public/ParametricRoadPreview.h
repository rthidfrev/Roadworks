#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ParametricRoadPreview.generated.h"
class AFGBlueprintHologram;
class AActor;
class UMeshComponent;
class UInstancedStaticMeshComponent;
class UMaterialInterface;

/** Snapshot of the already-loaded native blueprint's visuals. No building,
 * blueprint-world reload, collision, replication or per-slice actor is needed. */
UCLASS(Transient)
class ROADWORKS_API UParametricRoadPreview : public UObject
{
    GENERATED_BODY()
public:
    bool Initialize(AFGBlueprintHologram* Source);
    void Update(const TArray<FTransform>& BlueprintPoses);
    bool IsReady() const { return NextGroup >= Groups.Num(); }
    void HideSlice(int32 Slice);
    void SetAppearance(AFGBlueprintHologram* Source, uint8 MaterialState);
    void Clear();
private:
    friend class FParametricRoadPreviewSnapshotTest;
    struct FAppearance
    {
        // CapturedMaterials retains these references for UObject garbage collection.
        TArray<UMaterialInterface*> Materials;
        TArray<float> PrimitiveData;
        int32 Stencil = 0;
        uint8 StencilWriteMask = 0;
        bool bCustomDepth = false;
        bool operator==(const FAppearance& Other) const;
    };
    struct FGroup
    {
        // Always a local, unregistered snapshot, never a native hologram component.
        TWeakObjectPtr<UMeshComponent> Template;
        TWeakObjectPtr<UInstancedStaticMeshComponent> Instances;
        TArray<TWeakObjectPtr<UMeshComponent>> Copies;
        TArray<FTransform> LocalPoses;
        TArray<FAppearance> Appearances;
    };
    static UMeshComponent* SnapshotTemplate(UMeshComponent* Source, UObject* Outer);
    FAppearance CaptureAppearance(UMeshComponent* Source, TMap<UMaterialInterface*, UMaterialInterface*>& MaterialCache);
    static void ApplyAppearance(UMeshComponent* Mesh, const FAppearance& State);
    UPROPERTY(Transient) TObjectPtr<AActor> Owner;
    UPROPERTY(Transient) TArray<TObjectPtr<UMeshComponent>> SnapshotTemplates;
    UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInterface>> CapturedMaterials;
    TArray<FGroup> Groups;
    TArray<FTransform> LastPoses;
    uint8 Appearance = 255;
    int32 NextGroup = 0;
};
