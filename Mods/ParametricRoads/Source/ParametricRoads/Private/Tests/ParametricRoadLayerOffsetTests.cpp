#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "ParametricRoadSession.h"
#include "ParametricRoadPreview.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParametricRoadLayerOffsetTest, "ParametricRoads.Editor.BlueprintLayerOffsets",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParametricRoadLayerOffsetTest::RunTest(const FString&)
{
    const FVector Origin(1234, -567, 890);
    const FRotator Angles(30, 90, 20);
    FParametricRoadRequest Request;
    Request.StartPose = FTransform(Angles, Origin);
    Request.EndPose = FTransform(Angles, Origin + Angles.Vector() * 10000);
    Request.SliceWidth = 200;
    FParametricRoadPath Path;
    const auto Road = UParametricRoadGeometry::GenerateRouteWithPath(Request, {}, Path);
    if (!TestEqual(TEXT("A rising banked road supplies the offset frame"), Road.Failure, EParametricRoadFailure::None)) { return false; }
    const FVector Offset(120, 450, -80);
    // Analytic axes for yaw90/grade30/bank20, independent of the transform helper.
    const double CP = FMath::Cos(FMath::DegreesToRadians(30.0)), SP = .5;
    const double CR = FMath::Cos(FMath::DegreesToRadians(20.0)), SR = FMath::Sin(FMath::DegreesToRadians(20.0));
    const FVector ExpectedShift = FVector(0, CP, SP) * Offset.X
        + FVector(-CR, SR * SP, -SR * CP) * Offset.Y + FVector(-SR, -CR * SP, CR * CP) * Offset.Z;
    TArray<FTransform> Default, Zero, Shifted;
    for (bool bGrade : {false, true})
    {
        for (bool bBank : {false, true})
        {
            UParametricRoadGeometry::DistributeLayer(Path, 1200, 600, bGrade, bBank, 512, Default);
            UParametricRoadGeometry::DistributeLayer(Path, 1200, 600, bGrade, bBank, 512, Zero, FVector::ZeroVector);
            TestEqual(TEXT("A local offset can distribute on a rising banked road"),
                UParametricRoadGeometry::DistributeLayer(Path, 1200, 600, bGrade, bBank, 512, Shifted, Offset), EParametricRoadFailure::None);
            TestEqual(TEXT("Offsets retain the cumulative station count"), Shifted.Num(), 8);
            for (int32 Index = 0; Index < Shifted.Num(); ++Index)
            {
                TestTrue(TEXT("Explicit zero is exactly equivalent to the existing default"), Zero[Index].Equals(Default[Index], 0));
                const FVector ExpectedCenter = Origin + Angles.Vector() * (600 + 1200 * Index) + ExpectedShift;
                TestTrue(TEXT("Forward/right/up use the full road frame even when object angles are leveled"),
                    Shifted[Index].GetLocation().Equals(ExpectedCenter, .000001));
                TestTrue(TEXT("Offsets do not change the configured object orientation"),
                    Shifted[Index].GetRotation().Equals(Default[Index].GetRotation(), .000001));
            }
        }
    }

    // Reversed native +X/+Y traversal and a nonzero authored origin must retain
    // the same physical root rotation and shift, rather than recentering decor.
    const FTransform Native(FRotator(15, 33, -12), FVector(400, 700, 100));
    const FVector LocalCenter(170, -95, 60);
    for (bool bForwardY : {false, true})
    {
        FTransform Frame;
        TestTrue(TEXT("Both native blueprint axes can establish a road frame"),
            UParametricRoadGeometry::TryMakeRoadFrame(Native, LocalCenter, bForwardY, Frame));
        FParametricRoadRequest NativeRequest;
        NativeRequest.StartPose = Frame; NativeRequest.EndPose = Frame;
        NativeRequest.EndPose.AddToTranslation(Frame.GetRotation().GetAxisX() * 3000);
        NativeRequest.SliceWidth = 100;
        FParametricRoadPath NativePath;
        TestEqual(TEXT("A tilted native origin retains a solvable path"),
            UParametricRoadGeometry::GenerateRouteWithPath(NativeRequest, {}, NativePath).Failure, EParametricRoadFailure::None);
        UParametricRoadGeometry::DistributeLayer(NativePath, 1200, 0, true, true, 512, Shifted, Offset);
        if (Shifted.IsEmpty()) { AddError(TEXT("Native-axis offset fixture emitted no stations")); continue; }
        FTransform Placed;
        TestTrue(TEXT("Shifted road frame converts back into a native blueprint root"),
            UParametricRoadGeometry::TryMakeBlueprintPose(Shifted[0], LocalCenter, bForwardY, Placed));
        const FVector SelectedAxis = bForwardY ? FVector::YAxisVector : FVector::XAxisVector;
        const FVector NativeRight = bForwardY ? FVector::XAxisVector : -FVector::YAxisVector;
        const FVector NativeShift = Native.GetRotation().RotateVector(-SelectedAxis * Offset.X + NativeRight * Offset.Y + FVector::ZAxisVector * Offset.Z);
        TestTrue(TEXT("Positive forward follows reversed native traversal on either selected axis"),
            Placed.GetLocation().Equals(Native.GetLocation() + NativeShift, .000001));
        TestTrue(TEXT("Offsets preserve the source blueprint's physical orientation"), Placed.GetRotation().Equals(Native.GetRotation(), .000001));
        TestTrue(TEXT("The authored center lands on the intended offset deck frame"),
            Placed.TransformPosition(LocalCenter).Equals(Frame.GetLocation() + NativeShift, .000001));
    }
    for (const FVector Invalid : {FVector(100001, 0, 0), FVector(0, -100001, 0),
        FVector(0, 0, std::numeric_limits<double>::infinity()), FVector(std::numeric_limits<double>::quiet_NaN(), 0, 0)})
    {
        Shifted = {FTransform::Identity};
        TestEqual(TEXT("Nonfinite or out-of-bound offsets are refused before placement work"),
            UParametricRoadGeometry::DistributeLayer(Path, 1200, 600, true, true, 512, Shifted, Invalid), EParametricRoadFailure::InvalidInput);
        TestTrue(TEXT("Invalid offsets leave no partial jobs"), Shifted.IsEmpty());
    }
    TestEqual(TEXT("The inclusive finite per-axis bound is accepted"),
        UParametricRoadGeometry::DistributeLayer(Path, 1200, 600, true, true, 512, Shifted, FVector(100000, -100000, 100000)), EParametricRoadFailure::None);
    TestEqual(TEXT("Offsets never force an extra endpoint or alter a valid empty sequence"),
        UParametricRoadGeometry::DistributeLayer(Path, 1200, 10001, true, true, 0, Shifted, Offset), EParametricRoadFailure::None);
    TestTrue(TEXT("An offset layer beyond the end still has zero stations"), Shifted.IsEmpty());

    TStrongObjectPtr<UParametricRoadSession> State(NewObject<UParametricRoadSession>());
    State->bAssessed = true; State->Phase = EParametricRoadPhase::Ready;
    State->StartCenter = Origin; State->IntermediatePoints.AddDefaulted();
    State->Layers[0].Preview = NewObject<UParametricRoadPreview>(State.Get());
    const UParametricRoadPreview* Snapshot = State->Layers[0].Preview;
    TestTrue(TEXT("Existing layers start with exact zero offsets"), State->GetLayerSettings().OffsetMetres.IsZero());
    State->SetLayerOffset(0, 1.2); State->SetLayerOffset(1, 4.5); State->SetLayerOffset(2, -.8);
    TestTrue(TEXT("Three translations are independent metre values"), State->GetLayerSettings().OffsetMetres.Equals(Offset / 100, .000001));
    TestTrue(TEXT("Offset edits invalidate placement without reloading the source snapshot"), State->bDirty && State->Layers[0].Preview == Snapshot);
    TestEqual(TEXT("Offset edits retain the placement phase"), State->Phase, EParametricRoadPhase::Ready);
    TestEqual(TEXT("Offset edits retain fixed points"), State->IntermediatePoints.Num(), 1);
    TestEqual(TEXT("Offset edits retain the road origin"), State->StartCenter, Origin);
    State->AddLayer(); State->SetLayerOffset(1, -5); State->SelectLayer(0);
    TestTrue(TEXT("A second layer's offset does not mutate the first"), State->GetLayerSettings().OffsetMetres.Equals(Offset / 100, .000001));
    TestTrue(TEXT("Each selected layer retains its own offsets"), State->GetLayerSettings(1).OffsetMetres.Equals(FVector(0, -5, 0), .000001));
    State->bDirty = false;
    State->SetLayerOffset(-1, 4); State->SetLayerOffset(3, 4);
    State->SetLayerOffset(0, std::numeric_limits<double>::infinity()); State->SetLayerOffset(1, std::numeric_limits<double>::quiet_NaN());
    TestFalse(TEXT("Invalid axis and numeric edits do not invalidate an unchanged solution"), State->bDirty);
    State->SetLayerOffset(0, 1500); State->SetLayerOffset(1, -1500);
    TestTrue(TEXT("Selected offsets are clamped to the shared per-axis metre bound"), State->GetLayerSettings().OffsetMetres.Equals(FVector(1000, -1000, -.8), .000001));
    State->Phase = EParametricRoadPhase::Building; State->SetLayerOffset(2, 20);
    TestEqual(TEXT("An outstanding build freezes offset settings"), State->GetLayerSettings().OffsetMetres.Z, -.8);
    State->Phase = EParametricRoadPhase::Ready;
    State->IntermediatePoints.Reset(); State->EndCenter = Origin + Angles.Vector() * 10000;
    State->StartHeading = State->EndHeading = 90; State->StartGrade = State->EndGrade = 30;
    State->StartBank = State->EndBank = 20; State->MemberCount = 1; State->Parameters.SliceWidth = 200;
    State->SetLayerOffset(0, 1.2); State->SetLayerOffset(1, 4.5); State->SetLayerOffset(2, -.8);
    State->SetLayerFollowGrade(false); State->SetLayerFollowBank(false);
    for (FParametricRoadLayerState& Layer : State->Layers)
    {
        Layer.Settings.BlueprintName = Layer.LoadedName = TEXT("Pillar"); Layer.Settings.bEnabled = true;
        Layer.MemberCount = 1;
        if (!Layer.Preview) { Layer.Preview = NewObject<UParametricRoadPreview>(State.Get()); }
    }
    State->Solve();
    TestTrue(TEXT("The session integrates independently offset layers into the existing solve"), State->bLayerSolutionValid);
    TestEqual(TEXT("Session offsets preserve the scheduled placement count"), State->GetTotalLayerPlacementCount(), 16);
    if (!State->Layers[0].PreviewPoses.IsEmpty() && !State->Layers[1].PreviewPoses.IsEmpty())
    {
        FTransform FirstFrame, SecondFrame;
        UParametricRoadGeometry::TryMakeRoadFrame(State->Layers[0].PreviewPoses[0], State->LocalCenter, State->bForwardY, FirstFrame);
        UParametricRoadGeometry::TryMakeRoadFrame(State->Layers[1].PreviewPoses[0], State->LocalCenter, State->bForwardY, SecondFrame);
        const FVector Station = Origin + Angles.Vector() * 600;
        TestTrue(TEXT("Session converts metre offsets before native root placement without leveling their position"),
            FirstFrame.GetLocation().Equals(Station + ExpectedShift, .000001));
        TestTrue(TEXT("A second same-blueprint layer keeps its own offset trajectory"),
            SecondFrame.GetLocation().Equals(Station + FVector(-CR, SR * SP, -SR * CP) * -500, .000001));
        TestTrue(TEXT("The session retains orientation choices independently of offsets"),
            FirstFrame.GetRotation().Equals(FRotator(0, 90, 0).Quaternion(), .000001)
            && SecondFrame.GetRotation().Equals(Angles.Quaternion(), .000001));
    }
    TestNull(TEXT("Offset checks remain worldless"), State->GetWorld());
    return !HasAnyErrors();
}
#endif
