#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "ParametricRoadPreview.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SplineMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Hologram/FGHologram.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "UObject/GarbageCollection.h"
#include "UObject/StrongObjectPtr.h"

// Unregistered component/data copies only: no world, actor, native blueprint
// subsystem, render scene, input stack or construction transaction is created.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParametricRoadPreviewSnapshotTest, "Roadworks.Editor.PreviewSnapshotLifetime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParametricRoadPreviewSnapshotTest::RunTest(const FString&)
{
    TStrongObjectPtr<UParametricRoadPreview> Preview(NewObject<UParametricRoadPreview>());
    TStrongObjectPtr<USplineMeshComponent> Native(NewObject<USplineMeshComponent>());
    UStaticMesh* Geometry = NewObject<UStaticMesh>(Preview.Get());
    Geometry->GetStaticMaterials().Add(FStaticMaterial(UMaterial::GetDefaultMaterial(MD_Surface)));
    Native->SetStaticMesh(Geometry);
    const FVector Start(10, 20, 30), End(400, -50, 60);
    const FVector StartTangent(180, 20, 10), EndTangent(100, -40, 30);
    Native->SetStartAndEnd(Start, StartTangent, End, EndTangent, false);
    UMaterialInstanceDynamic* Mutable = UMaterialInstanceDynamic::Create(UMaterial::GetDefaultMaterial(MD_Surface), Native.Get());
    Native->SetMaterial(0, Mutable);
    auto& Group = Preview->Groups.AddDefaulted_GetRef();
    for (int32 I = 0; I < 3; ++I)
    {
        Native->SetCustomPrimitiveDataFloat(0, 0.25f + I);
        Native->SetRenderCustomDepth(true);
        Native->SetCustomDepthStencilValue(10 + I);
        Native->SetCustomDepthStencilWriteMask(ERendererStencilMask::ERSM_255);
        Mutable->SetScalarParameterValue(TEXT("SnapshotValue"), 0.5f + I);
        TMap<UMaterialInterface*, UMaterialInterface*> Cache;
        Group.Appearances.Add(Preview->CaptureAppearance(Native.Get(), Cache));
        TestTrue(TEXT("Mutable native materials are cloned instead of retained"), Group.Appearances.Last().Materials[0] != Mutable);
    }
    TestTrue(TEXT("Different states retain independent material snapshots"),
        Group.Appearances[0].Materials[0] != Group.Appearances[2].Materials[0]);
    auto* Template = CastChecked<USplineMeshComponent>(Preview->SnapshotTemplate(Native.Get(), Preview.Get()));
    Preview->SnapshotTemplates.Add(Template);
    Group.Template = Template;
    Preview->ApplyAppearance(Template, Group.Appearances[0]);
    auto* Copy = CastChecked<USplineMeshComponent>(Preview->SnapshotTemplate(Template, Preview.Get()));
    Preview->SnapshotTemplates.Add(Copy);
    Group.Copies.Add(Copy);
    auto* Batch = NewObject<UInstancedStaticMeshComponent>(Preview.Get());
    Batch->SetStaticMesh(Geometry);
    Preview->SnapshotTemplates.Add(Batch);
    Group.Instances = Batch;
    const TArray<UMeshComponent*> Outputs = {Template, Copy, Batch};

    // Changing or releasing the native source must not affect deferred copies
    // or the appearance of a source loaded before another layer blueprint.
    Native->SetStartAndEnd(FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector, false);
    Mutable->SetScalarParameterValue(TEXT("SnapshotValue"), 99);
    TWeakObjectPtr<USplineMeshComponent> FormerSource = Native.Get();
    TWeakObjectPtr<UMaterialInstanceDynamic> FormerMaterial = Mutable;
    Native.Reset();
    Mutable = nullptr;
    CollectGarbage(RF_NoFlags);
    TestFalse(TEXT("The snapshot does not keep its native source alive"), FormerSource.IsValid());
    TestFalse(TEXT("The snapshot does not keep a native mutable material alive"), FormerMaterial.IsValid());
    TestTrue(TEXT("Local snapshot components survive garbage collection"), IsValid(Template) && IsValid(Copy) && IsValid(Batch));
    TestNull(TEXT("The template is detached from the original attachment hierarchy"), Template->GetAttachParent());
    TestFalse(TEXT("The template is never registered"), Template->IsRegistered());
    TestTrue(TEXT("Spline shape survives native-source mutation and collection"),
        Copy->GetStartPosition().Equals(Start) && Copy->GetEndPosition().Equals(End)
        && Copy->GetStartTangent().Equals(StartTangent) && Copy->GetEndTangent().Equals(EndTangent));
    for (int32 I = 0; I < 3; ++I)
    {
        Preview->SetAppearance(nullptr, uint8(I));
        for (UMeshComponent* Mesh : Outputs)
        {
            TestEqual(TEXT("Native stencil is retained for each state"), Mesh->CustomDepthStencilValue, 10 + I);
            TestTrue(TEXT("Native custom depth remains enabled"), Mesh->bRenderCustomDepth);
            TestEqual(TEXT("Native stencil write mask is retained"), Mesh->CustomDepthStencilWriteMask, ERendererStencilMask::ERSM_255);
            TestEqual(TEXT("Native runtime primitive data is retained"), Mesh->GetCustomPrimitiveData().Data[0], 0.25f + I);
            TestEqual(TEXT("Each output uses its captured state material"), Mesh->GetMaterial(0), Group.Appearances[I].Materials[0]);
        }
        auto* Captured = CastChecked<UMaterialInstanceDynamic>(Group.Appearances[I].Materials[0]);
        TestEqual(TEXT("A captured MID keeps the parameter value of its state"), Captured->K2_GetScalarParameterValue(TEXT("SnapshotValue")), 0.5f + I);
    }

    TStrongObjectPtr<UInstancedStaticMeshComponent> NativeInstances(NewObject<UInstancedStaticMeshComponent>());
    NativeInstances->SetStaticMesh(Geometry);
    NativeInstances->SetNumCustomDataFloats(1);
    const FTransform InstancePose(FRotator(3, 15, 8), FVector(100, 200, 300));
    NativeInstances->AddInstance(InstancePose);
    NativeInstances->SetCustomDataValue(0, 0, 0.75f);
    auto* InstanceTemplate = CastChecked<UInstancedStaticMeshComponent>(Preview->SnapshotTemplate(NativeInstances.Get(), Preview.Get()));
    Preview->SnapshotTemplates.Add(InstanceTemplate);
    NativeInstances->ClearInstances();
    NativeInstances.Reset();
    FTransform CopiedPose;
    TestEqual(TEXT("A native ISM fallback retains its instances"), InstanceTemplate->GetInstanceCount(), 1);
    TestTrue(TEXT("A native ISM fallback retains its local instance pose"),
        InstanceTemplate->GetInstanceTransform(0, CopiedPose) && CopiedPose.Equals(InstancePose, 0.001));
    TestEqual(TEXT("A native ISM fallback retains per-instance custom data"), InstanceTemplate->PerInstanceSMCustomData[0], 0.75f);
    Preview->Clear();
    TestTrue(TEXT("Clear releases the reflected snapshot/material retention arrays"),
        Preview->SnapshotTemplates.IsEmpty() && Preview->CapturedMaterials.IsEmpty());
    return true;
}
#endif
