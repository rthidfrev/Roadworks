#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "ParametricRoadDirection.h"
#include "ParametricRoadAudioSettings.h"
#include "ParametricRoadGeometry.h"
#include "Settings/FGUserSetting.h"
#include "Settings/FGUserSettingApplyType.h"
#include "FGGameUserSettings.h"
#include "Engine/AssetManager.h"
#include "Misc/AutomationTest.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionSceneTexture.h"
#include "ParametricRoadMonitorOcclusion.h"
#include "Components/StaticMeshComponent.h"
#include "Hologram/FGBlueprintHologram.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include "Engine/StaticMesh.h"
#include <limits>
#include "Materials/MaterialExpressionAbs.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionMax.h"
#include "Materials/MaterialExpressionMin.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionSaturate.h"
#include "Materials/MaterialExpressionSubtract.h"
#include "Materials/MaterialExpressionTransformPosition.h"
#include "Materials/MaterialExpressionWorldPosition.h"

namespace
{
    // Read the saved material graph, rather than test a second hand-written
    // arrow formula. This checks its real silhouette, axis and transparent
    // corners without a gameplay world or a guessed native mesh orientation.
    double ReadArrowOpacity(const FExpressionInput& Input, const FVector& Position, int32 Depth = 0)
    {
        if (!Input.Expression || Depth > 32) { return std::numeric_limits<double>::quiet_NaN(); }
        const auto Read = [&Position, Depth](const FExpressionInput& Next)
        { return ReadArrowOpacity(Next, Position, Depth + 1); };
        const UMaterialExpression* Expression = Input.Expression;
        if (const auto* Constant = Cast<UMaterialExpressionConstant>(Expression)) { return Constant->R; }
        if (const auto* Mask = Cast<UMaterialExpressionComponentMask>(Expression))
        {
            const auto* Transform = Cast<UMaterialExpressionTransformPosition>(Mask->Input.Expression);
            if (!Transform || Transform->TransformSourceType != TRANSFORMPOSSOURCE_World
                || Transform->TransformType != TRANSFORMPOSSOURCE_Local
                || !Cast<UMaterialExpressionWorldPosition>(Transform->Input.Expression)
                || Mask->B || Mask->A || Mask->R == Mask->G) { return std::numeric_limits<double>::quiet_NaN(); }
            return Mask->R ? Position.X : Position.Y;
        }
        if (const auto* Absolute = Cast<UMaterialExpressionAbs>(Expression)) { return FMath::Abs(Read(Absolute->Input)); }
        if (const auto* Saturate = Cast<UMaterialExpressionSaturate>(Expression)) { return FMath::Clamp(Read(Saturate->Input), 0., 1.); }
        if (const auto* Add = Cast<UMaterialExpressionAdd>(Expression)) { return Read(Add->A) + Read(Add->B); }
        if (const auto* Subtract = Cast<UMaterialExpressionSubtract>(Expression)) { return Read(Subtract->A) - Read(Subtract->B); }
        if (const auto* Multiply = Cast<UMaterialExpressionMultiply>(Expression)) { return Read(Multiply->A) * Read(Multiply->B); }
        if (const auto* Minimum = Cast<UMaterialExpressionMin>(Expression)) { return FMath::Min(Read(Minimum->A), Read(Minimum->B)); }
        if (const auto* Maximum = Cast<UMaterialExpressionMax>(Expression)) { return FMath::Max(Read(Maximum->A), Read(Maximum->B)); }
        return std::numeric_limits<double>::quiet_NaN();
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParametricRoadDirectionTest,"ParametricRoads.Editor.DirectionMarkers",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FParametricRoadDirectionTest::RunTest(const FString&)
{
    TestEqual(TEXT("Exactly one direction marker is supported"), ParametricRoadDirection::MarkerCount, 1);
    const UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/ParametricRoads/Monitor/Meshes/SM_RoadDirectionPlane.SM_RoadDirectionPlane"));
    if (!TestNotNull(TEXT("The real engine plane exists, independent of FactoryGame SDK placeholders"), Plane)) { return false; }
    const FBox Bounds = Plane->GetBoundingBox();
    TestTrue(TEXT("The engine mesh lies flat in XY and is the expected 100 cm square"),
        Bounds.GetSize().Equals(FVector(100,100,0), .001));
    const auto* MarkerProperty = FindFProperty<FObjectPropertyBase>(UParametricRoadDirection::StaticClass(), TEXT("Marker"));
    TestTrue(TEXT("The presentation owns a single mesh component, not a marker array"),
        MarkerProperty && MarkerProperty->PropertyClass == UStaticMeshComponent::StaticClass());
    for (bool bForwardY : {false,true})
    for (const FRotator& Angles : {FRotator(0,0,0),FRotator(22,135,35),FRotator(-30,-179,-40)})
    {
        const FTransform Blueprint(Angles,FVector(500,700,1100)); FTransform Frame;
        TestTrue(TEXT("Blueprint has a canonical direction frame"),
            UParametricRoadGeometry::TryMakeRoadFrame(Blueprint,FVector::ZeroVector,bForwardY,Frame));
        const auto Pose = ParametricRoadDirection::MarkerPose(Frame, 800);
        const FVector Expected = -(bForwardY ? Blueprint.GetUnitAxis(EAxis::Y) : Blueprint.GetUnitAxis(EAxis::X));
        const FVector Tip = Pose.TransformPosition(FVector(50,0,0));
        const FVector Tail = Pose.TransformPosition(FVector(-50,0,0));
        TestTrue(TEXT("The actual material tip points along road travel for X/Y, grade and bank"),
            (Tip - Tail).GetSafeNormal().Equals(Expected, .00001));
        TestTrue(TEXT("Arrow is 160 cm long, independent of blueprint width and native helper scale"),
            FMath::IsNearlyEqual((Tip - Tail).Size(), 160., .00001));
        TestTrue(TEXT("Arrow is 64 cm wide and lies in the road surface plane"),
            FMath::IsNearlyEqual((Pose.TransformPosition(FVector(0,50,0)) - Pose.TransformPosition(FVector(0,-50,0))).Size(), 64., .00001)
            && Pose.GetUnitAxis(EAxis::Z).Equals(Frame.GetUnitAxis(EAxis::Z), .00001));
        const FVector Relative = Frame.InverseTransformPosition(Pose.GetLocation());
        TestTrue(TEXT("The enlarged cue remains ahead of the final slice and 5 cm above the canonical deck reference"),
            Relative.Equals(FVector(500,0,5), .00001));
        TestTrue(TEXT("Enlarging the cue preserves 20 cm of clearance from the final slice edge"),
            Frame.InverseTransformPosition(Tail).Equals(FVector(420,0,5), .00001));
    }
    const UMaterial* Material = LoadObject<UMaterial>(nullptr,
        TEXT("/ParametricRoads/Monitor/Materials/M_RoadDirectionHologram.M_RoadDirectionHologram"));
    if (!TestNotNull(TEXT("The explicit arrow silhouette material exists"), Material)) { return false; }
    const auto& Opacity = Material->GetEditorOnlyData()->Opacity;
    for (const FVector& Inside : {FVector(-49,0,0),FVector(49,0,0),FVector(11,45,0),FVector(11,-45,0)})
    {
        TestTrue(TEXT("The saved material contains the shaft and broad forward head"),
            ReadArrowOpacity(Opacity, Inside) > .6);
    }
    for (const FVector& Outside : {FVector(-49,40,0),FVector(-49,-40,0),FVector(49,10,0),FVector(49,-10,0)})
    {
        TestTrue(TEXT("The saved material has transparent corners and narrows only toward +X"),
            ReadArrowOpacity(Opacity, Outside) == 0.);
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParametricRoadVolumeSettingTest,"ParametricRoads.Editor.MonitorVolumeSetting",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FParametricRoadVolumeSettingTest::RunTest(const FString&)
{
    const auto* Setting=LoadObject<UFGUserSetting>(nullptr,TEXT("/ParametricRoads/Settings/US_RoadMonitorVolume.US_RoadMonitorVolume"));
    if(!TestNotNull(TEXT("Saved native audio setting exists"),Setting))return false;
    TArray<FPrimaryAssetId> Registered;
    UAssetManager::Get().GetPrimaryAssetIdList(FPrimaryAssetType(TEXT("FGUserSetting")),Registered);
    TestTrue(TEXT("Native discovery registers the setting before options collect their values"),Registered.Contains(FPrimaryAssetId(TEXT("FGUserSetting"),TEXT("US_RoadMonitorVolume"))));
    TestEqual(TEXT("Owned setting has a stable distinct identity"),Setting->StrId,FString(ParametricRoadAudio::VolumeId));
    TestTrue(TEXT("Setting belongs to local game options"),Setting->ManagerTypeAvailability==UFGGameUserSettings::StaticClass()&&!Setting->IsSettingSessionWide);
    const auto* Slider=Cast<UFGUserSetting_Slider>(Setting->ValueSelector);
    if(!TestNotNull(TEXT("Native numeric volume slider is configured"),Slider))return false;
    TestTrue(TEXT("Native normalized range is displayed as percent, defaulting on"),Slider->MinValue==0&&Slider->MaxValue==1&&Slider->MaxDisplayValue==100&&Slider->DefaultSliderValue==100&&Slider->ShowZeroAsOff);
    TestEqual(TEXT("Native settings apply immediately"),Setting->ApplyType.ToSoftObjectPath(),FSoftObjectPath(UFGUserSettingApplyType_UpdateInstantly::StaticClass()));
    TestEqual(TEXT("There is one unambiguous native menu location"),Setting->WidgetsToCreate.Num(),1);
    if(Setting->WidgetsToCreate.Num()==1)
    {
        const auto& Location=Setting->WidgetsToCreate[0];
        TestEqual(TEXT("Setting appears in native Audio"),Location.CategoryClass->GetName(),FString(TEXT("USC_Audio_C")));
        TestTrue(TEXT("Setting has its own Roadworks audio subcategory"),Location.SubCategoryClass==UParametricRoadAudioCategory::StaticClass());
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParametricRoadOutlineAssetsTest,"ParametricRoads.Editor.MonitorOcclusionAssets",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FParametricRoadOutlineAssetsTest::RunTest(const FString&)
{
    const UMaterial* Screen=LoadObject<UMaterial>(nullptr,TEXT("/ParametricRoads/Monitor/Materials/M_RoadMonitorWidget.M_RoadMonitorWidget"));
    if(!TestNotNull(TEXT("Physical screen material exists"),Screen))return false;
    TestTrue(TEXT("Physical screen writes opaque scene depth without bypassing depth testing"),Screen->BlendMode==BLEND_Opaque&&!Screen->bDisableDepthTest);
    const UMaterial* Outline=LoadObject<UMaterial>(nullptr,TEXT("/Game/FactoryGame/PostProcess/Materials/PPM_OutlineHologram.PPM_OutlineHologram"));
    if(!TestNotNull(TEXT("Native hologram post-process exists"),Outline))return false;
    // Native SDK post-process graphs are partial exports, so they cannot
    // establish final-game masking. Check our depth-tested arrow and the
    // ownership/restoration contract of the targeted native-helper correction.
    const UMaterial* Arrow=LoadObject<UMaterial>(nullptr,TEXT("/ParametricRoads/Monitor/Materials/M_RoadDirectionHologram.M_RoadDirectionHologram"));
    if(!TestNotNull(TEXT("Dedicated direction hologram exists"),Arrow))return false;
    TestTrue(TEXT("Arrow is holographic but respects physical depth"),Arrow->BlendMode==BLEND_Translucent&&!Arrow->bDisableDepthTest);
    for(const TCHAR* Name:{TEXT("mBlueprintBoundsMesh"),TEXT("mBlueprintDirectionMesh")})
        TestNotNull(TEXT("Targeted native helper remains reflectable"),FindFProperty<FObjectPropertyBase>(AFGBlueprintHologram::StaticClass(),Name));
    TStrongObjectPtr<UStaticMeshComponent> Visible(NewObject<UStaticMeshComponent>()), Hidden(NewObject<UStaticMeshComponent>());
    Visible->SetHiddenInGame(false); Hidden->SetHiddenInGame(true);
    FParametricRoadMonitorOcclusion Scope;
    Scope.Hide(Visible.Get()); Scope.Hide(Hidden.Get()); Scope.Hide(Visible.Get());
    TestTrue(TEXT("Helpers are hidden while the monitor owns them"),Visible->bHiddenInGame&&Hidden->bHiddenInGame);
    TestEqual(TEXT("Repeated frames retain one original state per helper"),Scope.Entries.Num(),2);
    Visible->SetVisibility(false); // An independent session preview update.
    Scope.Restore();
    TestTrue(TEXT("Exit restores each original hidden flag"),!Visible->bHiddenInGame&&Hidden->bHiddenInGame);
    TestFalse(TEXT("Restoration preserves independent session visibility"),Visible->IsVisible());
    Scope.Restore(); TestTrue(TEXT("Repeated cleanup is harmless"),Scope.Entries.IsEmpty());

    Visible->SetHiddenInGame(false); Hidden->SetHiddenInGame(false);
    Scope.ApplyTargets({Visible.Get(),Hidden.Get()});
    Scope.ApplyTargets({Hidden.Get()});
    TestFalse(TEXT("Expanded-to-compact reconciliation restores the native bounds"),bool(Visible->bHiddenInGame));
    TestTrue(TEXT("Compact mode still suppresses the duplicate native direction helper"),bool(Hidden->bHiddenInGame));
    TestEqual(TEXT("Compact mode retains ownership of only the replacement direction helper"),Scope.Entries.Num(),1);
    TestFalse(TEXT("Reconciliation preserves the session's independent bounds visibility"),Visible->IsVisible());
    Scope.ApplyTargets({Visible.Get(),Hidden.Get()});
    TestTrue(TEXT("Reopening configuration reacquires the bounds without losing the direction helper"),Visible->bHiddenInGame&&Hidden->bHiddenInGame);
    Scope.ApplyTargets({});
    TestTrue(TEXT("Leaving the monitor restores both helpers and releases every scoped entry"),!Visible->bHiddenInGame&&!Hidden->bHiddenInGame&&Scope.Entries.IsEmpty());
    return !HasAnyErrors();
}
#endif
