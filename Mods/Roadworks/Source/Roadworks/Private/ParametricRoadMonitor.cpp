#include "ParametricRoadMonitor.h"

#include "ParametricRoadSession.h"
#include "ParametricRoadWidgets.h"
#include "RoadworksModule.h"
#include "Blueprint/UserWidget.h"
#include "Camera/CameraComponent.h"
#include "Components/SceneComponent.h"
#include "Components/AudioComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Equipment/FGBuildGun.h"
#include "FGCharacterPlayer.h"
#include "FGHUD.h"
#include "Hologram/FGBlueprintHologram.h"
#include "UObject/UnrealType.h"
#include "ParametricRoadAudioSettings.h"
#include "FGOptionInterface.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "FGPlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "SceneView.h"
#include "Misc/ScopeExit.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
    constexpr float ReadoutInterval = 0.1f;
    // Blender's front-view right is -Y; Unreal's is +Y. V2 imports use the
    // standard handedness conversion, including the readable baked markings.
    const FVector ArmLocation(5.8, -19, 0);
    const FVector HousingHingeLocation(0, 11.6, 0);
    const FVector HousingLocation(-3.5, 15.5, 0);
    const FVector MountLocation(6.8, -20, -1);
    const FVector2D ScreenPixels(1024, 688);
    // Recess the display behind the modeled lip (-2.10), ahead of the opaque
    // backing (-1.52). Input projection uses this same component transform.
    const FVector ScreenLocation(-1.95, 0, 0);
    // The engine widget quad faces +X. The Blender housing opens towards -X.
    // Scale is set once, rather than resizing the render target during animation.
    const FTransform ScreenPose(FRotator(0, 180, 0), ScreenLocation,
        FVector(1, 29.0 / 1024.0, 19.4 / 688.0));

    TArray<FVector> MakeMonitorCorners(const UStaticMesh* Housing, const UStaticMesh* Arm, const UStaticMesh* Mount)
    {
        TArray<FVector> Corners;
        const UStaticMesh* Meshes[] = {Housing, Arm, Mount};
        const FVector Locations[] = {ArmLocation + HousingHingeLocation + HousingLocation, ArmLocation, MountLocation};
        for (int32 Index = 0; Index < UE_ARRAY_COUNT(Meshes); ++Index)
        {
            const FBox Bounds = Meshes[Index]->GetBoundingBox();
            for (int32 Corner = 0; Corner < 8; ++Corner)
            {
                Corners.Add(FVector((Corner & 1) ? Bounds.Max.X : Bounds.Min.X,
                    (Corner & 2) ? Bounds.Max.Y : Bounds.Min.Y, (Corner & 4) ? Bounds.Max.Z : Bounds.Min.Z) + Locations[Index]);
            }
        }
        return Corners;
    }

    FTransform MakeMonitorViewPose(float Fold, double Aspect, TConstArrayView<FVector> Corners)
    {
        const FRotator Rotation(8, -22, 0);
        constexpr double Scale = 0.70, Distance = 60.0, HousingLeft = 0.045, Top = 0.07;
        Aspect = FMath::Clamp(Aspect, 1.0, 4.0);
        double Y = -40, Z = 16;
        if (!Corners.IsEmpty())
        {
            Y = -DBL_MAX; Z = DBL_MAX;
            for (int32 Index = 0; Index < Corners.Num(); ++Index)
            {
                const FVector& Corner = Corners[Index];
                const FVector P = Rotation.RotateVector(Corner * Scale);
                // The first eight corners are the housing. Anchor its readable
                // frame, allowing the unsupported mount to disappear offscreen.
                // Panini already fixes equipment FOV at 90 degrees.
                if (Index < 8)
                {
                    Y = FMath::Max(Y, (2 * HousingLeft - 1) * (Distance + P.X) - P.Y);
                }
                Z = FMath::Min(Z, (1 - 2 * Top) * (Distance + P.X) / Aspect - P.Z);
            }
        }
        return FTransform(Rotation, FVector(Distance, Y, Z) + FVector(0, -9, 28) * Fold, FVector(Scale));
    }

    FVector2D ProjectMonitorPoint(const FVector& Point, double Aspect)
    {
        return FVector2D(0.5 + 0.5 * Point.Y / Point.X, 0.5 - 0.5 * Aspect * Point.Z / Point.X);
    }

    struct FRoadMonitorPanini
    {
        double Depth = 0, Skew = 0, Scale = 1, FOVMulti = 1, ScalingFudge = 1;
    };
    struct FRoadMonitorProjectedVertex
    {
        FVector2D Position = FVector2D::ZeroVector, UV = FVector2D::ZeroVector;
        double InverseW = 0;
    };
    FVector2D RoadMonitorPaniniProjection(FVector2D OM, double Depth, double Skew)
    {
        // Pinned Engine/Shaders/Private/PaniniProjection.ush:43.
        const double InvLength = 1 / FMath::Sqrt(1 + OM.X * OM.X);
        const double SinPhi = OM.X * InvLength, TanTheta = OM.Y * InvLength;
        const double CosPhi = FMath::Sqrt(FMath::Max(0.0, 1 - SinPhi * SinPhi));
        const double Factor = (Depth + 1) / (Depth + CosPhi);
        return Factor * FVector2D(SinPhi, FMath::Lerp(TanTheta, TanTheta / CosPhi, Skew));
    }
    bool ProjectRoadMonitorVertex(FVector ViewPoint, FVector2D UV, const FSceneViewProjectionData& Projection,
        const FRoadMonitorPanini& Panini, FRoadMonitorProjectedVertex& Result)
    {
        // Exact pinned MF_PaniniProjection graph: camera material space is
        // right/up/forward, and WPO leaves camera depth unchanged. Readout in
            if (ViewPoint.Z <= 0 || !Projection.IsValidViewRectangle()) { return false; }
        const FMatrix InverseProjection = Projection.ProjectionMatrix.Inverse();
        const FVector2D TanHalfFOV(InverseProjection.M[0][0], InverseProjection.M[1][1]);
        if (TanHalfFOV.X <= 0 || TanHalfFOV.Y <= 0) { return false; }
        const double Boundary = RoadMonitorPaniniProjection(TanHalfFOV, Panini.Depth, Panini.Skew).X;
        if (!FMath::IsFinite(Boundary) || FMath::Abs(Boundary) < 1e-8) { return false; }
        const double A = Panini.Scale * TanHalfFOV.X / Boundary;
        const double K = FMath::Lerp(A, A * (1 + (TanHalfFOV.X - 1) * Panini.FOVMulti), Panini.ScalingFudge);
        const FVector2D P = RoadMonitorPaniniProjection(FVector2D(ViewPoint.X, ViewPoint.Y) / ViewPoint.Z, Panini.Depth, Panini.Skew);
        const FVector4 Clip = Projection.ProjectionMatrix.TransformFVector4(FVector4(P.X * ViewPoint.Z * K, P.Y * ViewPoint.Z * K, ViewPoint.Z, 1));
        if (Clip.W <= 1e-8 || Clip.ContainsNaN()) { return false; }
        const FIntRect& ViewRect = Projection.GetViewRect();
        const FIntRect& Constrained = Projection.GetConstrainedViewRect();
        if (ViewRect.Width() <= 0 || ViewRect.Height() <= 0) { return false; }
        const FVector2D ScreenUV(.5 + .5 * Clip.X / Clip.W, .5 - .5 * Clip.Y / Clip.W);
        Result.Position = (FVector2D(Constrained.Min - ViewRect.Min)
            + ScreenUV * FVector2D(Constrained.Size())) / FVector2D(ViewRect.Size());
        Result.UV = UV; Result.InverseW = 1 / Clip.W;
        return !Result.Position.ContainsNaN() && FMath::IsFinite(K);
    }
    bool RoadMonitorTriangleUV(FVector2D Pointer, const FRoadMonitorProjectedVertex& A,
        const FRoadMonitorProjectedVertex& B, const FRoadMonitorProjectedVertex& C, FVector2D& UV, bool bAllowOutside)
    {
        const FVector2D AB = B.Position - A.Position, AC = C.Position - A.Position, AP = Pointer - A.Position;
        const double Determinant = AB.X * AC.Y - AB.Y * AC.X;
        if (FMath::Abs(Determinant) < 1e-12) { return false; }
        const double V = (AP.X * AC.Y - AP.Y * AC.X) / Determinant;
        const double W = (AB.X * AP.Y - AB.Y * AP.X) / Determinant;
        const double U = 1 - V - W;
        if (!bAllowOutside && (U < -1e-7 || V < -1e-7 || W < -1e-7)) { return false; }
        const double Weight = U * A.InverseW + V * B.InverseW + W * C.InverseW;
        if (FMath::Abs(Weight) < 1e-12) { return false; }
        UV = (U * A.UV * A.InverseW + V * B.UV * B.InverseW + W * C.UV * C.InverseW) / Weight;
        return !UV.ContainsNaN();
    }

    FTransform MakeExpandedMonitorPose(double Aspect, TConstArrayView<FVector> Corners)
    {
        Aspect = FMath::Clamp(Aspect, 1.0, 4.0);
        FBox HousingBounds(ForceInit);
        for (int32 I = 0; I < FMath::Min(8, Corners.Num()); ++I) { HousingBounds += Corners[I]; }
        const FVector Center = HousingBounds.IsValid ? HousingBounds.GetCenter() : FVector(2.3, 8.1, -0.7);
        const auto Pose = [Center](double Scale)
        {
            return FTransform(FRotator::ZeroRotator, FVector(80, -Center.Y * Scale, -Center.Z * Scale), FVector(Scale));
        };
        double Low = 0.01, High = 8;
        for (int32 Iteration = 0; Iteration < 32; ++Iteration)
        {
            const double Scale = (Low + High) * 0.5;
            const FTransform Candidate = Pose(Scale);
            bool bFits = HousingBounds.IsValid != 0;
            for (int32 I = 0; I < FMath::Min(8, Corners.Num()); ++I)
            {
                const FVector P = Candidate.TransformPosition(Corners[I]);
                const FVector2D UV = ProjectMonitorPoint(P, Aspect);
                bFits &= P.X > 1 && UV.X >= 0.07 && UV.X <= 0.93 && UV.Y >= 0.07 && UV.Y <= 0.93;
            }
            if (bFits) { Low = Scale; } else { High = Scale; }
        }
        return Pose(Low);
    }

    FTransform MakeMonitorTransitionPose(const FTransform& Compact, const FTransform& Expanded, float Progress)
    {
        if (Progress <= 0) { return Compact; }
        if (Progress >= 1) { return Expanded; }
        const auto Motion = ParametricRoadMonitorAnimation::Expanded(Progress);
        const FVector Reference = ArmLocation + HousingHingeLocation + HousingLocation;
        const FQuat Rotation = FRotator(0, 0, Motion.RollOffset).Quaternion()
            * FQuat::Slerp(Compact.GetRotation(), Expanded.GetRotation(), Motion.RotationAlpha);
        const FVector Scale = FMath::Lerp(Compact.GetScale3D(), Expanded.GetScale3D(), Motion.ScaleAlpha);
        const FVector Center = FMath::Lerp(Compact.TransformPosition(Reference), Expanded.TransformPosition(Reference),
            Motion.PositionAlpha) + FVector(Motion.DepthOffset, 0, Motion.LiftOffset);
        // Blend around the housing centre, so separate rotation/scale timing
        // cannot drag the screen sideways around the assembly's remote origin.
        return FTransform(Rotation, Center - Rotation.RotateVector(Reference * Scale), Scale);
    }

    FBox2D ExpandedScreenRect(const FTransform& Pose, double Aspect)
    {
        FBox2D Rect(ForceInit);
        const FVector Center = ArmLocation + HousingHingeLocation + HousingLocation + ScreenLocation;
        for (int32 Corner = 0; Corner < 4; ++Corner)
        {
            const FVector Point = Pose.TransformPosition(Center + FVector(0, (Corner & 1) ? 14.5 : -14.5,
                (Corner & 2) ? 9.7 : -9.7));
            if (Point.X <= 1) { return FBox2D(ForceInit); }
            Rect += ProjectMonitorPoint(Point, Aspect);
        }
        return Rect;
    }

    double MonitorViewAspect(const AFGCharacterPlayer* Character)
    {
        const AFGPlayerController* PC = Character ? Cast<AFGPlayerController>(Character->GetController()) : nullptr;
        int32 Width = 0, Height = 0;
        if (PC) { PC->GetViewportSize(Width, Height); }
        return Width > 0 && Height > 0 ? double(Width) / Height : 16.0 / 9.0;
    }

    void ConfigureVisual(UPrimitiveComponent* Component)
    {
        Component->SetFlags(RF_Transient | RF_TextExportTransient | RF_DuplicateTransient);
        Component->SetMobility(EComponentMobility::Movable);
        Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Component->SetGenerateOverlapEvents(false);
        Component->SetCanEverAffectNavigation(false);
        Component->SetComponentTickEnabled(false);
        Component->SetCastShadow(false);
        Component->SetReceivesDecals(false);
        Component->SetVisibleInRayTracing(false);
        Component->SetOnlyOwnerSee(true);
        // Native hologram outlines use the nearest custom-depth/stencil sample.
        // Our opaque accessory writes depth with the neutral stencil, so a
        // hologram behind it cannot win that post-process mask.
        Component->SetRenderCustomDepth(true);
        Component->SetCustomDepthStencilWriteMask(ERendererStencilMask::ERSM_Default);
        Component->SetCustomDepthStencilValue(0);
        Component->SetBoundsScale(3.0f); // Panini WPO may move vertices outside the undeformed mesh bounds.
        // CSS first-person materials already apply Panini projection. Enabling
        // generic UE first-person reprojection here would apply a second transform.
    }

    USceneComponent* MakePivot(AActor* Owner, USceneComponent* Parent, FName Name, FVector Location)
    {
        USceneComponent* Pivot = NewObject<USceneComponent>(Owner, Name,
            RF_Transient | RF_TextExportTransient | RF_DuplicateTransient);
        Owner->AddInstanceComponent(Pivot);
        Pivot->SetMobility(EComponentMobility::Movable);
        if (Parent) { Pivot->SetupAttachment(Parent); }
        else { Owner->SetRootComponent(Pivot); }
        Pivot->SetRelativeLocation(Location);
        Pivot->RegisterComponent();
        return Pivot;
    }

    bool AssignMaterials(UStaticMeshComponent* Component, const TMap<FName, UMaterialInterface*>& Materials)
    {
        const TArray<FName> Slots = Component->GetMaterialSlotNames();
        if (Slots.IsEmpty()) { return false; }
        for (int32 Index = 0; Index < Slots.Num(); ++Index)
        {
            UMaterialInterface* const* Material = Materials.Find(Slots[Index]);
            if (!Material || !*Material) { return false; }
            // Override this transient component only, never the imported mesh/CDO.
            Component->SetMaterial(Index, *Material);
        }
        return true;
    }

    UStaticMeshComponent* MakeMesh(AActor* Owner, USceneComponent* Parent, FName Name,
        UStaticMesh* Mesh, FVector Location, const TMap<FName, UMaterialInterface*>& Materials)
    {
        UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(Owner, Name,
            RF_Transient | RF_TextExportTransient | RF_DuplicateTransient);
        Owner->AddInstanceComponent(Component);
        Component->SetupAttachment(Parent);
        Component->SetStaticMesh(Mesh);
        Component->SetRelativeLocation(Location);
        ConfigureVisual(Component);
        if (!AssignMaterials(Component, Materials)) { return nullptr; }
        Component->RegisterComponent();
        return Component;
    }
}

bool UParametricRoadMonitorInteraction::DispatchKey(FKey Key, bool bPressed, bool bRepeat)
{
    if (!CanSendInput()) { return false; }
    bool bHasKeyCode, bHasCharCode;
    uint32 KeyCode, CharCode;
    GetKeyAndCharCodes(Key, bHasKeyCode, KeyCode, bHasCharCode, CharCode);
    const FKeyEvent Event(Key, ModifierKeys, VirtualUser->GetUserIndex(), bRepeat, CharCode, KeyCode);
    // WIC::PressKey also synthesizes a character. The physical native modal
    // forwards its real translated OnKeyChar separately, exactly once.
    return bPressed ? FSlateApplication::Get().ProcessKeyDownEvent(Event)
        : FSlateApplication::Get().ProcessKeyUpEvent(Event);
}

bool UParametricRoadMonitor::Initialize(UParametricRoadSession* InSession, AFGBuildGun* InGun, UUserWidget* InWidget)
{
    Clear();
    UnavailableReason = FText::GetEmpty();
    const auto Fail = [this](const FText& Reason)
    {
        Clear();
        UnavailableReason = Reason;
        UE_LOG(LogRoadworks, Warning, TEXT("3D monitor unavailable; retaining the HUD: %s"), *Reason.ToString());
        return false;
    };
    AFGCharacterPlayer* LocalCharacter = IsValid(InGun) ? InGun->GetInstigatorCharacter() : nullptr;
    AFGPlayerController* PC = LocalCharacter ? Cast<AFGPlayerController>(LocalCharacter->GetController()) : nullptr;
    UWorld* World = IsValid(InGun) ? InGun->GetWorld() : nullptr;
    UCameraComponent* LocalCamera = LocalCharacter ? LocalCharacter->FindComponentByClass<UCameraComponent>() : nullptr;
    if (!IsValid(InSession) || !IsValid(InWidget) || !World || !World->IsGameWorld() || World->bIsTearingDown
        || World->GetNetMode() == NM_DedicatedServer || !PC || !PC->IsLocalController()
        || !LocalCharacter->IsLocallyControlled() || !IsValid(LocalCamera))
    {
        return Fail(NSLOCTEXT("Roadworks", "MonitorNoLocalView", "A local player view is required for the 3D monitor."));
    }

    UStaticMesh* Housing = LoadObject<UStaticMesh>(nullptr,
        TEXT("/Roadworks/Monitor/Meshes/SM_RoadMonitorHousing.SM_RoadMonitorHousing"));
    UStaticMesh* Arm = LoadObject<UStaticMesh>(nullptr,
        TEXT("/Roadworks/Monitor/Meshes/SM_RoadMonitorArm.SM_RoadMonitorArm"));
    UStaticMesh* Mount = LoadObject<UStaticMesh>(nullptr,
        TEXT("/Roadworks/Monitor/Meshes/SM_RoadMonitorMount.SM_RoadMonitorMount"));
    UMaterialInterface* WidgetMaterial = LoadObject<UMaterialInterface>(nullptr,
        TEXT("/Roadworks/Monitor/Materials/M_RoadMonitorWidget.M_RoadMonitorWidget"));
    TMap<FName, UMaterialInterface*> Materials;
    UMaterialInterface* V2Material = LoadObject<UMaterialInterface>(nullptr,
        TEXT("/Roadworks/Monitor/Materials/MI_RoadMonitor_V2.MI_RoadMonitor_V2"));
    Materials.Add(FName(TEXT("PR2_Baked_PBR")), V2Material);
    if (!Housing || !Arm || !Mount || !WidgetMaterial || !V2Material)
    {
        return Fail(NSLOCTEXT("Roadworks", "MonitorAssetsMissing", "The 3D monitor assets are unavailable."));
    }
    const FVector HousingSize = Housing->GetBoundingBox().GetSize();
    if (HousingSize.X < 2 || HousingSize.X > 9 || HousingSize.Y < 28 || HousingSize.Y > 42
        || HousingSize.Z < 22 || HousingSize.Z > 34)
    {
        return Fail(NSLOCTEXT("Roadworks", "MonitorImportMismatch", "The monitor mesh scale or axes do not match its screen."));
    }

    DeploySound = LoadObject<USoundBase>(nullptr, TEXT("/Roadworks/Monitor/Audio/SW_RoadMonitorDeploy.SW_RoadMonitorDeploy"));
    RetractSound = LoadObject<USoundBase>(nullptr, TEXT("/Roadworks/Monitor/Audio/SW_RoadMonitorRetract.SW_RoadMonitorRetract"));
    Session = InSession;
    Gun = InGun;
    Player = LocalCharacter;
    Camera = LocalCamera;
    Widget = InWidget;
    GeometryCorners = MakeMonitorCorners(Housing, Arm, Mount);
    LayoutAspect = MonitorViewAspect(LocalCharacter);
    FActorSpawnParameters Spawn;
    Spawn.Owner = LocalCharacter;
    Spawn.Instigator = LocalCharacter;
    Spawn.ObjectFlags |= RF_Transient | RF_TextExportTransient | RF_DuplicateTransient;
    Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    // Use the engine class; there is no custom saved/replicated actor class.
    Presentation = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Spawn);
    if (!Presentation) { return Fail(NSLOCTEXT("Roadworks", "MonitorSpawnFailed", "The local monitor could not be created.")); }
    Presentation->SetReplicates(false);
    Presentation->SetActorEnableCollision(false);
    Presentation->SetActorTickEnabled(false);
    Presentation->SetActorHiddenInGame(true);
    ViewRoot = MakePivot(Presentation, nullptr, TEXT("RoadMonitorView"), FVector::ZeroVector);
    ViewRoot->AttachToComponent(LocalCamera, FAttachmentTransformRules::SnapToTargetNotIncludingScale);

    // These assembly transforms match the separate Blender part origins. Both
    // bearings rotate around local X; the second counter-rotates the screen.
    ArmPivot = MakePivot(Presentation, ViewRoot, TEXT("RoadMonitorArmHinge"), ArmLocation);
    HousingPivot = MakePivot(Presentation, ArmPivot, TEXT("RoadMonitorHousingHinge"), HousingHingeLocation);
    UStaticMeshComponent* HousingComponent = MakeMesh(Presentation, HousingPivot, TEXT("RoadMonitorHousing"),
        Housing, HousingLocation, Materials);
    if (!HousingComponent || !MakeMesh(Presentation, ArmPivot, TEXT("RoadMonitorArm"), Arm, FVector::ZeroVector, Materials)
        || !MakeMesh(Presentation, ViewRoot, TEXT("RoadMonitorMount"), Mount, MountLocation, Materials))
    {
        return Fail(NSLOCTEXT("Roadworks", "MonitorSlotsMismatch", "The monitor material slots do not match the imported model."));
    }

    Screen = NewObject<UWidgetComponent>(Presentation, TEXT("RoadMonitorScreen"),
        RF_Transient | RF_TextExportTransient | RF_DuplicateTransient);
    Presentation->AddInstanceComponent(Screen);
    Screen->SetupAttachment(HousingComponent);
    ConfigureVisual(Screen);
    Screen->SetWidgetSpace(EWidgetSpace::World);
    Screen->SetGeometryMode(EWidgetGeometryMode::Plane);
    Screen->SetDrawSize(ScreenPixels);
    Screen->SetDrawAtDesiredSize(false);
    Screen->SetPivot(FVector2D(0.5, 0.5));
    Screen->SetBlendMode(EWidgetBlendMode::Opaque);
    Screen->SetTwoSided(false);
    Screen->SetWindowFocusable(false);
    Screen->SetOwnerPlayer(PC->GetLocalPlayer());
    Screen->SetTickWhenOffscreen(false);
    Screen->SetManuallyRedraw(true);
    Screen->SetRedrawTime(ReadoutInterval);
    Screen->SetTickMode(ETickMode::Disabled);
    Screen->SetRelativeTransform(ScreenPose);
    Screen->SetMaterial(0, WidgetMaterial);
    // UWidgetComponent's engine default disables hardware input. Never inherit
    // a focus-taking alternative if that contract changes in a future engine.
    if (Screen->GetReceiveHardwareInput())
    {
        return Fail(NSLOCTEXT("Roadworks", "MonitorInputMismatch", "The monitor cannot use a non-interactive display."));
    }
    Screen->RegisterComponent();
    InWidget->RemoveFromParent();
    InWidget->SetIsFocusable(false);
    InWidget->SetVisibility(ESlateVisibility::HitTestInvisible);
    Screen->SetWidget(InWidget);
    Screen->SetWindowVisibility(EWindowVisibility::SelfHitTestInvisible);
    Screen->SetVisibility(false, false);
    Screen->SetTickMode(ETickMode::Disabled);
    WorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddUObject(this, &UParametricRoadMonitor::OnWorldCleanup);
    ApplyPose();
    UE_LOG(LogRoadworks, Log, TEXT("Created local transient 3D monitor: 1024x688, manually redrawn at up to 10 Hz."));
    LogInitialCameras();
    return true;
}

bool UParametricRoadMonitor::IsValidMonitor() const
{
    return IsValid(Presentation) && !Presentation->IsActorBeingDestroyed() && IsValid(Screen)
        && IsValid(ViewRoot) && IsValid(ArmPivot) && IsValid(HousingPivot) && Widget.IsValid();
}

void UParametricRoadMonitor::SetExpanded(bool bExpanded)
{
    bExpandedTarget = bExpanded && !bRetracting;
}

bool UParametricRoadMonitor::IsExpandedReady() const
{
    return IsValidMonitor() && bVisible && bExpandedTarget && !bRetracting && Expansion >= 1.0f && Deployment >= 1.0f;
}

FBox2D UParametricRoadMonitor::GetConfigurationRect() const
{
    return IsExpandedReady() ? ExpandedScreenRect(MakeExpandedMonitorPose(LayoutAspect, GeometryCorners), LayoutAspect)
        : FBox2D(ForceInit);
}

bool UParametricRoadMonitor::SetConfigurationWidget(UUserWidget* ConfigOrNull)
{
    if (bConfigurationDetaching) { return ConfigOrNull == nullptr; }
    if (ConfigurationWidget.Get() == ConfigOrNull && (ConfigOrNull ? ConfigurationInteraction != nullptr : ConfigurationInteraction == nullptr))
    { if (!ConfigOrNull) { SetExpanded(false); } return true; }
    // Stop presentation and dispatch before a released button or NativeDestruct
    // can re-enter the session's close path.
    bConfigurationDetaching = true;
    if (IsValid(Screen)) { Screen->SetVisibility(false, false); Screen->SetComponentTickEnabled(false); }
    bScreenVisible = false;
    SetExpanded(false);
    ConfigurationWidget.Reset();
    bConfigurationCleanupPending = true;
    // A button/Enter callback may close from inside Slate's event route. Its
    // ProcessReply still needs the virtual user and widget path after that
    // callback returns. Only the outermost completed native route may release
    // either resource.
    if (ConfigurationDispatchDepth > 0) { return ConfigOrNull == nullptr; }
    FinalizeConfigurationDetach();
    if (!ConfigOrNull) { return true; }
    if (!IsValidMonitor() || bRetracting || ConfigOrNull->GetParent() || ConfigOrNull->IsInViewport()
        || !FSlateApplication::IsInitialized()) { return false; }
    PaniniCollection = LoadObject<UMaterialParameterCollection>(nullptr,
        TEXT("/Game/FactoryGame/-Shared/Collections/MPC_Panini.MPC_Panini"));
    if (!PaniniCollection) { return false; }
    // FindOrCreateVirtualUser shares an existing index. Hold a fresh handle
    // until our native component activates so another mod's pointer/focus is
    // never acquired or unregistered by this monitor.
    TSharedPtr<FSlateVirtualUserHandle> ReservedUser;
    int32 UserIndex = INDEX_NONE;
    for (int32 CandidateIndex = 0; CandidateIndex < 128; ++CandidateIndex)
    {
        TSharedRef<FSlateVirtualUserHandle> Candidate = FSlateApplication::Get().FindOrCreateVirtualUser(CandidateIndex);
        if (Candidate.GetSharedReferenceCount() == 1) { ReservedUser = Candidate; UserIndex = CandidateIndex; break; }
    }
    if (UserIndex == INDEX_NONE) { return false; }
    ConfigurationInteraction = NewObject<UParametricRoadMonitorInteraction>(Presentation, NAME_None,
        RF_Transient | RF_TextExportTransient | RF_DuplicateTransient);
    Presentation->AddInstanceComponent(ConfigurationInteraction);
    ConfigurationInteraction->SetupAttachment(ViewRoot);
    ConfigurationInteraction->SetAutoActivate(false);
    ConfigurationInteraction->InteractionSource = EWidgetInteractionSource::Custom;
    ConfigurationInteraction->VirtualUserIndex = UserIndex;
    ConfigurationInteraction->PointerIndex = 0;
    ConfigurationInteraction->bEnableHitTesting = true;
    ConfigurationInteraction->bShowDebug = false;
    ConfigurationInteraction->RegisterComponent();
    ConfigurationInteraction->Activate();
    ConfigurationInteraction->SetComponentTickEnabled(false); // Event-driven, no scene trace.
    ConfigurationWidget = ConfigOrNull;
    ConfigOrNull->SetVisibility(ESlateVisibility::Visible);
    Screen->SetWidget(ConfigOrNull);
    Screen->SetWindowFocusable(true);
    Screen->SetWindowVisibility(EWindowVisibility::SelfHitTestInvisible);
    Screen->SetManuallyRedraw(false); Screen->SetRedrawTime(1.0f / 30);
    Screen->RequestRenderUpdate();
    SetExpanded(true);
    return true;
}

void UParametricRoadMonitor::FinalizeConfigurationDetach()
{
    if (!bConfigurationCleanupPending || ConfigurationDispatchDepth > 0) { return; }
    bConfigurationCleanupPending = false;
    ClearConfigurationInteraction();
    if (IsValid(Screen))
    {
        Screen->SetWindowFocusable(false);
        Screen->SetWidget(Widget.Get());
        Screen->SetManuallyRedraw(true); Screen->SetRedrawTime(ReadoutInterval);
        Screen->SetTickMode(ETickMode::Disabled);
        // A closed configuration can have the same status revision as before
        // opening. Force the new widget to replace the old render-target pixels.
        RenderedRevision = MAX_uint32;
        ReadoutSeconds = 0;
        Screen->RequestRenderUpdate();
        bScreenVisible = bVisible && Deployment >= 0.55f;
        Screen->SetVisibility(bScreenVisible, false);
    }
    bConfigurationDetaching = false;
}

void UParametricRoadMonitor::FinishConfigurationDispatch()
{
    check(ConfigurationDispatchDepth > 0);
    --ConfigurationDispatchDepth;
    if (ConfigurationDispatchDepth > 0) { return; }
    if (bMonitorCleanupPending)
    {
        bMonitorCleanupPending = false;
        Clear();
    }
    else { FinalizeConfigurationDetach(); }
}

void UParametricRoadMonitor::ClearConfigurationInteraction()
{
    bConfigurationPointerOnScreen = false;
    UParametricRoadMonitorInteraction* Interaction = ConfigurationInteraction;
    ConfigurationInteraction = nullptr;
    const TSet<FKey> PointerKeys = MoveTemp(ConfigurationPointerKeys);
    const TSet<FKey> KeyboardKeys = MoveTemp(ConfigurationKeyboardKeys);
    if (!IsValid(Interaction)) { return; }
    for (FKey Key : PointerKeys) { Interaction->ReleasePointerKey(Key); }
    for (FKey Key : KeyboardKeys) { Interaction->DispatchKey(Key, false, false); }
    Interaction->SetCustomHitResult(FHitResult()); Interaction->UpdatePointer();
    Interaction->Deactivate(); Interaction->DestroyComponent();
}

bool UParametricRoadMonitor::CanRouteConfiguration() const
{
    return !bConfigurationDetaching && ConfigurationWidget.IsValid() && IsValid(ConfigurationInteraction) && IsExpandedReady();
}

bool UParametricRoadMonitor::RouteConfigurationPointer(FVector2D NormalizedViewport)
{
    bConfigurationPointerOnScreen = false;
    if (!IsValid(ConfigurationInteraction) || bConfigurationDetaching) { return false; }
    ++ConfigurationDispatchDepth;
    ON_SCOPE_EXIT { FinishConfigurationDispatch(); };
    FHitResult Hit;
    FVector2D UV;
    const auto UpdateHit = [this, &Hit]()
    {
        ConfigurationInteraction->SetCustomHitResult(Hit);
        ConfigurationInteraction->UpdatePointer();
        return bConfigurationPointerOnScreen;
    };
    if (!CanRouteConfiguration() || NormalizedViewport.ContainsNaN() || !PaniniCollection) { return UpdateHit(); }
    const AFGCharacterPlayer* Character = Player.Get();
    const AFGPlayerController* PC = Character ? Cast<AFGPlayerController>(Character->GetController()) : nullptr;
    ULocalPlayer* LocalPlayer = PC ? PC->GetLocalPlayer() : nullptr;
    FSceneViewProjectionData Projection;
    if (!LocalPlayer || !LocalPlayer->ViewportClient || !LocalPlayer->ViewportClient->Viewport
        || !LocalPlayer->GetProjectionData(LocalPlayer->ViewportClient->Viewport, Projection)
        || !Projection.IsPerspectiveProjection()) { return UpdateHit(); }
    const UWorld* World = Presentation->GetWorld();
    UMaterialParameterCollectionInstance* Instance = World ? World->GetParameterCollectionInstance(PaniniCollection) : nullptr;
    float Depth, Skew, Scale;
    if (!Instance || !Instance->GetScalarParameterValue(TEXT("Depth"), Depth)
        || !Instance->GetScalarParameterValue(TEXT("Skew"), Skew)
        || !Instance->GetScalarParameterValue(TEXT("Scale"), Scale)) { return UpdateHit(); }
    FRoadMonitorPanini Panini;
    Panini.Depth = Depth; Panini.Skew = Skew; Panini.Scale = Scale;
    if (UMaterialInterface* Material = Screen->GetMaterialInstance())
    {
        float Value = 1;
        if (Material->GetScalarParameterValue(FMaterialParameterInfo(TEXT("FOV Multi")), Value)) { Panini.FOVMulti = Value; }
        if (Material->GetScalarParameterValue(FMaterialParameterInfo(TEXT("Scaling Fudge")), Value)) { Panini.ScalingFudge = Value; }
    }
    const FVector2D DrawSize = Screen->GetCurrentDrawSize();
    if (DrawSize.X <= 0 || DrawSize.Y <= 0) { return UpdateHit(); }
    const FVector2D Pivot = Screen->GetPivot();
    // Match WidgetComponent.cpp's exact two-triangle mesh and UV topology.
    // WPO only runs at its four vertices; a continuous inverse Panini ray is
    // therefore insufficient when Depth/Skew are nonzero.
    const FVector2D Coordinates[] = {FVector2D(0, 0), FVector2D(0, 1), FVector2D(1, 1), FVector2D(1, 0)};
    FRoadMonitorProjectedVertex Vertices[4];
    const FTransform ScreenToWorld = Screen->GetComponentTransform();
    for (int32 Index = 0; Index < 4; ++Index)
    {
        const FVector2D& Coordinate = Coordinates[Index];
        const FVector WorldPoint = ScreenToWorld.TransformPosition(FVector(0,
            (Pivot.X - Coordinate.X) * DrawSize.X, (Pivot.Y - Coordinate.Y) * DrawSize.Y));
        const FVector ViewPoint = Projection.ViewRotationMatrix.TransformVector(WorldPoint - Projection.ViewOrigin);
        if (!ProjectRoadMonitorVertex(ViewPoint, Coordinate, Projection, Panini, Vertices[Index])) { return UpdateHit(); }
    }
    bConfigurationPointerOnScreen = RoadMonitorTriangleUV(NormalizedViewport, Vertices[0], Vertices[1], Vertices[2], UV, false)
        || RoadMonitorTriangleUV(NormalizedViewport, Vertices[0], Vertices[2], Vertices[3], UV, false);
    // Captured spin-box drags still receive coordinates after leaving the quad.
    const bool bCaptured = !ConfigurationPointerKeys.IsEmpty();
    if (!bConfigurationPointerOnScreen && (!bCaptured
        || !RoadMonitorTriangleUV(NormalizedViewport, Vertices[0], Vertices[1], Vertices[2], UV, true))) { return UpdateHit(); }
    const FVector WorldHit = ScreenToWorld.TransformPosition(FVector(0, (Pivot.X - UV.X) * DrawSize.X, (Pivot.Y - UV.Y) * DrawSize.Y));
    Hit = FHitResult(Presentation, Screen, WorldHit, Screen->GetForwardVector());
    Hit.bBlockingHit = true; Hit.Location = Hit.ImpactPoint = WorldHit;
    Hit.TraceStart = Projection.ViewOrigin; Hit.TraceEnd = WorldHit; Hit.Time = 1;
    return UpdateHit();
}

bool UParametricRoadMonitor::PressConfigurationPointerKey(FKey Key)
{
    if (!CanRouteConfiguration() || !bConfigurationPointerOnScreen || !Key.IsValid()) { return false; }
    ++ConfigurationDispatchDepth;
    ON_SCOPE_EXIT { FinishConfigurationDispatch(); };
    ConfigurationPointerKeys.Add(Key);
    ConfigurationInteraction->PressPointerKey(Key);
    return true;
}
void UParametricRoadMonitor::ReleaseConfigurationPointerKey(FKey Key)
{
    if (bConfigurationDetaching) { return; } // Final cleanup releases every held key.
    ConfigurationPointerKeys.Remove(Key);
    if (IsValid(ConfigurationInteraction))
    {
        ++ConfigurationDispatchDepth;
        ON_SCOPE_EXIT { FinishConfigurationDispatch(); };
        ConfigurationInteraction->ReleasePointerKey(Key);
    }
}
void UParametricRoadMonitor::ScrollConfiguration(float Delta)
{
    if (CanRouteConfiguration() && bConfigurationPointerOnScreen && FMath::IsFinite(Delta))
    {
        ++ConfigurationDispatchDepth;
        ON_SCOPE_EXIT { FinishConfigurationDispatch(); };
        ConfigurationInteraction->ScrollWheel(Delta);
    }
}
bool UParametricRoadMonitor::PressConfigurationKey(FKey Key, bool bRepeat)
{
    if (!CanRouteConfiguration() || !Key.IsValid()) { return false; }
    ++ConfigurationDispatchDepth;
    ON_SCOPE_EXIT { FinishConfigurationDispatch(); };
    ConfigurationKeyboardKeys.Add(Key);
    return ConfigurationInteraction->DispatchKey(Key, true, bRepeat);
}
bool UParametricRoadMonitor::ReleaseConfigurationKey(FKey Key)
{
    if (bConfigurationDetaching) { return false; }
    ConfigurationKeyboardKeys.Remove(Key);
    if (!IsValid(ConfigurationInteraction)) { return false; }
    ++ConfigurationDispatchDepth;
    ON_SCOPE_EXIT { FinishConfigurationDispatch(); };
    return ConfigurationInteraction->DispatchKey(Key, false, false);
}
bool UParametricRoadMonitor::SendConfigurationChar(FString Characters, bool bRepeat)
{
    if (!CanRouteConfiguration()) { return false; }
    ++ConfigurationDispatchDepth;
    ON_SCOPE_EXIT { FinishConfigurationDispatch(); };
    return ConfigurationInteraction->SendKeyChar(Characters, bRepeat);
}
void UParametricRoadMonitor::SetConfigurationModifiers(const FModifierKeysState& Modifiers)
{ if (!bConfigurationDetaching && IsValid(ConfigurationInteraction)) { ConfigurationInteraction->SetEventModifiers(Modifiers); } }

bool UParametricRoadMonitor::IsConfigurationTextEntryFocused() const
{
    if (bConfigurationDetaching || !ConfigurationWidget.IsValid() || !IsValid(ConfigurationInteraction)
        || !ConfigurationInteraction->VirtualUser.IsValid() || !FSlateApplication::IsInitialized()) { return false; }
    const TSharedPtr<SWidget> Root = ConfigurationWidget->GetCachedWidget();
    const uint32 User = ConfigurationInteraction->VirtualUser->GetUserIndex();
    const TSharedPtr<SWidget> Focused = FSlateApplication::Get().GetUserFocusedWidget(User);
    if (!Root || !Focused || (Focused != Root
        && !FSlateApplication::Get().HasUserFocusedDescendants(Root.ToSharedRef(), User))) { return false; }
    // Native search fields and numeric entry boxes put focus on these inner
    // Slate controls. The real keyboard focus remains on the invisible host.
    const FName Type = Focused->GetType();
    return Type == FName(TEXT("SEditableText")) || Type == FName(TEXT("SMultiLineEditableText"));
}

bool UParametricRoadMonitor::FinishConfigurationTextEntry()
{
    if (!IsConfigurationTextEntryFocused()) { return false; }
    const TSharedPtr<FSlateVirtualUserHandle> User = ConfigurationInteraction->VirtualUser;
    const TSharedPtr<SWidget> Focused = FSlateApplication::Get().GetUserFocusedWidget(User->GetUserIndex());
    ++ConfigurationDispatchDepth;
    ON_SCOPE_EXIT { FinishConfigurationDispatch(); };
    ConfigurationInteraction->DispatchKey(EKeys::Escape, true, false);
    ConfigurationInteraction->DispatchKey(EKeys::Escape, false, false);
    // Escape can merely revert or clear a selection while retaining text focus.
    // End that edit explicitly so a subsequent Escape can close the panel.
    if (FSlateApplication::Get().GetUserFocusedWidget(User->GetUserIndex()) == Focused)
    {
        FSlateApplication::Get().ClearUserFocus(User->GetUserIndex());
    }
    return true;
}

bool UParametricRoadMonitor::ShouldShow() const
{
    AFGCharacterPlayer* LocalCharacter = Player.Get();
    AFGBuildGun* LocalGun = Gun.Get();
    AFGPlayerController* PC = LocalCharacter ? Cast<AFGPlayerController>(LocalCharacter->GetController()) : nullptr;
    UWorld* World = Presentation ? Presentation->GetWorld() : nullptr;
    if (!World || World->IsPaused() || !PC || !PC->IsLocalController() || PC->IsPauseMenuOpen()
        || !LocalCharacter->IsLocallyControlled() || !LocalCharacter->IsFirstPerson()
        || LocalCharacter->GetTargetCameraMode() != ECameraMode::ECM_FirstPerson || !LocalCharacter->IsAliveAndWell()) { return false; }
    if (const AHUD* HUD = PC->GetHUD(); HUD && !HUD->bShowHUD) { return false; }
    // GetHUDVisibility describes the general pawn HUD, which may be hidden
    // while the native Build Gun UI is active. Respect explicit hide modes
    // without treating that unrelated layer as an equipment-visibility gate.
    if (AFGHUD* HUD = Cast<AFGHUD>(PC->GetHUD()); HUD
        && (HUD->GetHiddenHUDMode() || HUD->GetPumpiMode() || HUD->GetPartialPumpiMode())) { return false; }
    return bRetracting || (LocalGun && (!LocalGun->IsHidden() || bExpandedTarget || Expansion > 0)
        && Session.IsValid() && Session->IsActive());
}

void UParametricRoadMonitor::LogInitialCameras() const
{
    if (!UE_LOG_ACTIVE(LogRoadworks, Verbose)) { return; }
    const UCameraComponent* Selected = Camera.Get();
    const AFGCharacterPlayer* Character = Player.Get();
    if (!Selected || !Character) { return; }
    TArray<UCameraComponent*> Cameras;
    Character->GetComponents<UCameraComponent>(Cameras);
    FString Candidates;
    for (int32 I = 0; I < FMath::Min(Cameras.Num(), 16); ++I)
    {
        const UCameraComponent* Candidate = Cameras[I];
        if (!Candidate) { continue; }
        Candidates += FString::Printf(TEXT("%s%s(active=%d,owner=%s,world=%s,FOV=%.1f)"),
            Candidates.IsEmpty() ? TEXT("") : TEXT("; "), *Candidate->GetName(), Candidate->IsActive(),
            *GetNameSafe(Candidate->GetOwner()), *Candidate->GetComponentLocation().ToCompactString(), Candidate->FieldOfView);
    }
    UE_LOG(LogRoadworks, Verbose,
        TEXT("Road monitor camera: selected=%s active=%d owner=%s parent=%s relative=%s world=%s FOV=%.1f candidates(%d)=[%s]"),
        *Selected->GetPathName(), Selected->IsActive(), *GetNameSafe(Selected->GetOwner()), *GetNameSafe(Selected->GetAttachParent()),
        *Selected->GetRelativeLocation().ToCompactString(), *Selected->GetComponentLocation().ToCompactString(),
        Selected->FieldOfView, Cameras.Num(), *Candidates);
}

void UParametricRoadMonitor::LogVisibilityState(bool bShow)
{
    if (!UE_LOG_ACTIVE(LogRoadworks, Verbose)) { return; }
    const AFGCharacterPlayer* Character = Player.Get();
    const AFGBuildGun* LocalGun = Gun.Get();
    AFGPlayerController* PC = Character ? Cast<AFGPlayerController>(Character->GetController()) : nullptr;
    const UWorld* World = Presentation ? Presentation->GetWorld() : nullptr;
    AHUD* HUD = PC ? PC->GetHUD() : nullptr;
    AFGHUD* FactoryHUD = Cast<AFGHUD>(HUD);
    const int32 Current = Character ? int32(Character->GetCameraMode()) : INDEX_NONE;
    const int32 Target = Character ? int32(Character->GetTargetCameraMode()) : INDEX_NONE;
    const bool bPaused = World && World->IsPaused();
    const bool bLocalPC = PC && PC->IsLocalController();
    const bool bPauseMenu = PC && PC->IsPauseMenuOpen();
    const bool bLocalPawn = Character && Character->IsLocallyControlled();
    const bool bFirstPerson = Character && Character->IsFirstPerson();
    const bool bAlive = Character && Character->IsAliveAndWell();
    const bool bHudShow = !HUD || HUD->bShowHUD;
    const bool bHudVisible = !FactoryHUD || FactoryHUD->GetHUDVisibility();
    const bool bHiddenHud = FactoryHUD && FactoryHUD->GetHiddenHUDMode();
    const bool bPumpi = FactoryHUD && FactoryHUD->GetPumpiMode();
    const bool bPartialPumpi = FactoryHUD && FactoryHUD->GetPartialPumpiMode();
    const bool bGunHidden = LocalGun && LocalGun->IsHidden();
    const bool bSessionActive = Session.IsValid() && Session->IsActive();
    // Bits report failed functional gates in the same order as ShouldShow.
    // Ownership bits are separate: they describe rendering, not an added gate.
    const uint32 Mask = (!World ? 1u << 0 : 0) | (bPaused ? 1u << 1 : 0) | (!PC ? 1u << 2 : 0)
        | (!bLocalPC ? 1u << 3 : 0) | (bPauseMenu ? 1u << 4 : 0) | (!bLocalPawn ? 1u << 5 : 0)
        | (!bFirstPerson ? 1u << 6 : 0) | (Target != int32(ECameraMode::ECM_FirstPerson) ? 1u << 7 : 0)
        | (!bAlive ? 1u << 8 : 0) | (!bHudShow ? 1u << 9 : 0) | (bHiddenHud ? 1u << 10 : 0)
        | (bPumpi ? 1u << 11 : 0) | (!bRetracting && !LocalGun ? 1u << 12 : 0)
        | (!bRetracting && bGunHidden && !bExpandedTarget && Expansion <= 0 ? 1u << 13 : 0) | (!bRetracting && !Session.IsValid() ? 1u << 14 : 0)
        | (!bRetracting && !bSessionActive ? 1u << 15 : 0) | (bPartialPumpi ? 1u << 16 : 0);
    AActor* ViewTarget = PC ? PC->GetViewTarget() : nullptr;
    const bool bOwnedView = Presentation && ViewTarget && Presentation->IsOwnedBy(ViewTarget);
    const bool bOwnedPC = Presentation && PC && Presentation->IsOwnedBy(PC);
    const uint8 OwnerMask = uint8((bOwnedView ? 1 : 0) | (bOwnedPC ? 2 : 0) | (bRetracting ? 4 : 0) | (bShow ? 8 : 0));
    if (DiagnosticGateMask == Mask && DiagnosticCurrentMode == Current && DiagnosticTargetMode == Target
        && DiagnosticViewTarget.Get() == ViewTarget && DiagnosticOwnerMask == OwnerMask) { return; }
    DiagnosticGateMask = Mask; DiagnosticCurrentMode = Current; DiagnosticTargetMode = Target;
    DiagnosticViewTarget = ViewTarget; DiagnosticOwnerMask = OwnerMask;
    UE_LOG(LogRoadworks, Verbose,
        TEXT("Road monitor visibility: show=%d failedGates=0x%05x current=%d target=%d localPC=%d paused=%d pauseMenu=%d localPawn=%d first=%d alive=%d hudShow=%d hudVisible=%d hiddenHUD=%d pumpi=%d partialPumpi=%d gunHidden=%d sessionActive=%d retract=%d viewTarget=%s ownedView=%d ownedPC=%d"),
        bShow, Mask, Current, Target, bLocalPC, bPaused, bPauseMenu, bLocalPawn, bFirstPerson, bAlive,
        bHudShow, bHudVisible, bHiddenHud, bPumpi, bPartialPumpi, bGunHidden, bSessionActive, bRetracting, *GetNameSafe(ViewTarget), bOwnedView, bOwnedPC);
}

void UParametricRoadMonitor::LogRenderState() const
{
    if (!UE_LOG_ACTIVE(LogRoadworks, Verbose)) { return; }
    if (!IsValidMonitor()) { return; }
    const UTextureRenderTarget2D* Target = Screen->GetRenderTarget();
    const AFGCharacterPlayer* Character = Player.Get();
    AFGPlayerController* PC = Character ? Cast<AFGPlayerController>(Character->GetController()) : nullptr;
    FVector2D Pixel(-1, -1);
    const bool bProjected = PC && PC->ProjectWorldLocationToScreen(Screen->GetComponentLocation(), Pixel, true);
    int32 Width = 0, Height = 0;
    if (PC) { PC->GetViewportSize(Width, Height); }
    FString Frames;
    TArray<UStaticMeshComponent*> Meshes;
    Presentation->GetComponents<UStaticMeshComponent>(Meshes);
    for (UStaticMeshComponent* Mesh : Meshes)
    {
        if (!Mesh) { continue; }
        Frames += FString::Printf(TEXT("%s%s(visible=%d,hidden=%d,render=%.3f,onScreen=%.3f)"),
            Frames.IsEmpty() ? TEXT("") : TEXT("; "), *Mesh->GetName(), Mesh->IsVisible(), Mesh->bHiddenInGame,
            Mesh->GetLastRenderTime(), Mesh->GetLastRenderTimeOnScreen());
    }
    const UWorld* World = Presentation->GetWorld();
    UE_LOG(LogRoadworks, Verbose,
        TEXT("Road monitor render snapshot: deployment=%.3f actorHidden=%d screenVisible=%d screenHidden=%d registered=%d tick=%d target=%d targetSize=%dx%d render=%.3f onScreen=%.3f worldTime=%.3f screenWorld=%s projected=%d pixel=%s viewport=%dx%d frames=[%s]"),
        Deployment, Presentation->IsHidden(), Screen->IsVisible(), Screen->bHiddenInGame, Screen->IsRegistered(),
        Screen->IsComponentTickEnabled(), IsValid(Target), Target ? Target->SizeX : 0, Target ? Target->SizeY : 0,
        Screen->GetLastRenderTime(), Screen->GetLastRenderTimeOnScreen(), World ? World->GetTimeSeconds() : -1.0,
        *Screen->GetComponentLocation().ToCompactString(), bProjected, *Pixel.ToString(), Width, Height, *Frames);
}

void UParametricRoadMonitor::ApplyPose()
{
    if (!ViewRoot || !ArmPivot || !HousingPivot) { return; }
    const auto Mechanical = bRetracting
        ? ParametricRoadMonitorAnimation::Retract(RetractionStartPose, RetractionProgress)
        : ParametricRoadMonitorAnimation::Deploy(Deployment);
    const auto ExpandedMotion = ParametricRoadMonitorAnimation::Expanded(Expansion);
    const float HingeWeight = FMath::SmoothStep(0.70f, 1.0f, Deployment);
    ViewRoot->SetRelativeTransform(MakeMonitorTransitionPose(
        MakeMonitorViewPose(Mechanical.EntryFold, LayoutAspect, GeometryCorners),
        MakeExpandedMonitorPose(LayoutAspect, GeometryCorners), Expansion));
    ArmPivot->SetRelativeRotation(FRotator(0, 0, Mechanical.ArmRoll + ExpandedMotion.ArmRoll * HingeWeight + Sway.ArmRoll));
    HousingPivot->SetRelativeRotation(FRotator(0, 0, Mechanical.HousingRoll + ExpandedMotion.HousingRoll * HingeWeight + Sway.HousingRoll));
    Power = bRetracting ? ParametricRoadMonitorPower::Off(RetractionStartPower,
        PowerOffDelay > 0 ? RetractionElapsed / PowerOffDelay : 1) : ParametricRoadMonitorPower::On(Deployment);
    if (Screen)
    {
        Screen->SetTintColorAndOpacity(FLinearColor::White);
        if (UMaterialInstanceDynamic* Material = Screen->GetMaterialInstance())
        {
            Material->SetScalarParameterValue(TEXT("PowerReveal"), Power.Reveal);
            Material->SetScalarParameterValue(TEXT("PowerContent"), Power.Content);
            Material->SetScalarParameterValue(TEXT("PowerGlow"), Power.Glow);
        }
    }
}

bool UParametricRoadMonitor::UpdateSway(float DeltaSeconds, bool bShow)
{
    const float PreviousArm = Sway.ArmRoll, PreviousHousing = Sway.HousingRoll;
    ParametricRoadMonitorSway::FInput Input;
    Input.CameraRotation = Camera->GetComponentQuat();
    Input.Location = Player->GetActorLocation();
    Input.Velocity = Player->GetVelocity();
    if (const UCharacterMovementComponent* Movement = Player->GetCharacterMovement())
    {
        Input.Acceleration = Movement->GetCurrentAcceleration();
        Input.bGrounded = Movement->IsMovingOnGround();
    }
    Input.bVisible = bShow;
    Input.bCanDrive = !bRetracting && Deployment >= 1 && Expansion == 0
        && !bExpandedTarget && !ConfigurationWidget.IsValid() && !bConfigurationDetaching;
    // Retraction starts at the current deployment; its own envelope fades the
    // remaining offset without replacing the accepted mechanical animation.
    Input.PoseWeight = ParametricRoadMonitorSway::Weight(
        bRetracting ? RetractionStartDeployment : Deployment, Expansion, bRetracting, RetractionProgress);
    Sway.Tick(Input, DeltaSeconds);
    return Sway.ArmRoll != PreviousArm || Sway.HousingRoll != PreviousHousing;
}

void UParametricRoadMonitor::StopAudio()
{
    for (UAudioComponent* Audio : {DeployAudio.Get(), RetractAudio.Get()})
    {
        if (IsValid(Audio)) { Audio->Stop(); Audio->DestroyComponent(); }
    }
    DeployAudio = RetractAudio = nullptr;
}

void UParametricRoadMonitor::UpdateAudio(bool bShow)
{
    if (!bShow) { StopAudio(); return; }
    const float Gain = ParametricRoadAudio::AppliedGain();
    const auto Play = [this, Gain](USoundBase* Sound, float StartTime) -> UAudioComponent*
    {
        if (!Sound || Gain <= 0) { return nullptr; }
        UAudioComponent* Audio = UGameplayStatics::CreateSound2D(Presentation, Sound, Gain, 1, StartTime,
            nullptr, false, false);
        if (Audio)
        {
            Audio->SetFlags(RF_Transient | RF_TextExportTransient | RF_DuplicateTransient);
            Audio->bStopWhenOwnerDestroyed = true;
            Audio->bIsUISound = false; // Native pause/focus behaviour, local non-spatial playback.
            Audio->Play(StartTime);
        }
        return Audio;
    };
    if (!bRetracting && !bDeployAudioPlayed && Deployment >= ParametricRoadMonitorPower::OpenSoundPhase)
    {
        bDeployAudioPlayed = true;
        const float Offset = (Deployment - ParametricRoadMonitorPower::OpenSoundPhase)
            * ParametricRoadMonitorAnimation::DeploySeconds;
        if (Offset < .54f) { DeployAudio = Play(DeploySound, Offset); }
    }
    if (bRetracting && !bRetractAudioPlayed && RetractionElapsed >= .03f)
    {
        bRetractAudioPlayed = true;
        RetractAudio = Play(RetractSound, FMath::Max(0.f, RetractionElapsed - .03f));
    }
    for (UAudioComponent* Audio : {DeployAudio.Get(), RetractAudio.Get()})
    {
        if (IsValid(Audio)) { Audio->SetVolumeMultiplier(Gain); }
    }
}

void UParametricRoadMonitor::UpdateHologramOcclusion(bool bShow)
{
    if (!bShow) { HologramOcclusion.Restore(); return; }
    // These native helpers intentionally draw through ordinary scene depth.
    // Keep the bounds available for placement in compact mode; suppress them
    // only while the expanded physical screen or its transition covers them.
    // Our single depth-tested cue replaces the native direction helper.
    TArray<UPrimitiveComponent*> Targets;
    TArray<AFGBlueprintHologram*> Holograms;
    if (Session.IsValid()) { Session->CollectNativePreviewHolograms(Holograms); }
    for (AFGBlueprintHologram* Source : Holograms)
    {
        for (const TCHAR* Name : {TEXT("mBlueprintBoundsMesh"), TEXT("mBlueprintDirectionMesh")})
        {
            if (FCString::Strcmp(Name, TEXT("mBlueprintBoundsMesh")) == 0 && !bExpandedTarget && Expansion <= 0) { continue; }
            if (const auto* Property = FindFProperty<FObjectPropertyBase>(Source->GetClass(), Name))
                if (UPrimitiveComponent* Component = Cast<UPrimitiveComponent>(Property->GetObjectPropertyValue_InContainer(Source)))
                    Targets.AddUnique(Component);
        }
    }
    HologramOcclusion.ApplyTargets(Targets);
}

void UParametricRoadMonitor::Tick(float DeltaSeconds)
{
    // The retiring monitor owns its one ticker after the session lets go. Avoid
    // double advancement if Shutdown and the subsystem tick share a frame.
    if (bRetracting && !bInRetractionTicker) { return; }
    UWorld* World = Presentation ? Presentation->GetWorld() : nullptr;
    if (!IsValidMonitor() || !World || World->bIsTearingDown || !Player.IsValid() || !Camera.IsValid()
        || Player->GetWorld() != World || Player->FindComponentByClass<UCameraComponent>() != Camera.Get()) { Clear(); return; }
    if (!bRetracting && (!Session.IsValid() || !Session->IsActive() || !Gun.IsValid()
        || Gun->GetInstigatorCharacter() != Player.Get())) { BeginRetract(); return; }
    const float Step = FMath::IsFinite(DeltaSeconds) ? FMath::Clamp(DeltaSeconds, 0.0f, 0.1f) : 0.0f;
    const double CurrentAspect = MonitorViewAspect(Player.Get());
    if (!FMath::IsNearlyEqual(CurrentAspect, LayoutAspect))
    {
        LayoutAspect = CurrentAspect;
        ApplyPose();
    }
    const bool bShow = ShouldShow();
    LogVisibilityState(bShow);
    UpdateHologramOcclusion(bShow);
    if (bShow && !bLoggedRenderState)
    {
        DiagnosticVisibleSeconds += Step;
        if (DiagnosticVisibleSeconds >= 0.75f) { LogRenderState(); bLoggedRenderState = true; }
    }
    const bool bJustShown = bShow && !bVisible;
    if (bVisible != bShow)
    {
        bVisible = bShow;
        Presentation->SetActorHiddenInGame(!bShow);
        if (!bShow && Screen) { Screen->SetComponentTickEnabled(false); }
    }
    // A hidden/paused/third-person view does no UI drawing. Retraction still
    // finishes in real time so it cannot retain objects indefinitely in a menu.
    bool bPoseChanged = false;
    if (bShow || bRetracting)
    {
        const float Previous = Deployment, PreviousExpansion = Expansion;
        if (bRetracting)
        {
            const float PreviousElapsed = RetractionElapsed;
            RetractionElapsed += Step;
            const float MechanicalStep = ParametricRoadMonitorPower::MechanicalTime(RetractionElapsed, PowerOffDelay)
                - ParametricRoadMonitorPower::MechanicalTime(PreviousElapsed, PowerOffDelay);
            RetractionProgress = FMath::Min(1.0f, RetractionProgress + MechanicalStep / ParametricRoadMonitorAnimation::RetractSeconds);
            Deployment = RetractionStartDeployment * (1.0f - RetractionProgress);
        }
        else
        {
            Deployment = FMath::Min(1.0f, Deployment + Step / ParametricRoadMonitorAnimation::DeploySeconds);
        }
        const float ExpansionSeconds = bRetracting ? ParametricRoadMonitorAnimation::RetractSeconds
            : bExpandedTarget ? ParametricRoadMonitorAnimation::ExpandSeconds : ParametricRoadMonitorAnimation::CollapseSeconds;
        Expansion = FMath::Clamp(Expansion + (bExpandedTarget && !bRetracting ? Step : -Step)
            / ExpansionSeconds, 0.0f, 1.0f);
        bPoseChanged = bRetracting || Deployment != Previous || Expansion != PreviousExpansion;
    }
    const bool bSwayChanged = UpdateSway(DeltaSeconds, bShow);
    if (bPoseChanged || bSwayChanged) { ApplyPose(); }
    UpdateAudio(bShow);
    if (bRetracting && Deployment <= 0) { Clear(); return; }
    const bool bConfiguration = ConfigurationWidget.IsValid() && !bConfigurationDetaching;
    // Keep content on the physical surface during travel. Input readiness is
    // separately gated by IsExpandedReady until all motion has reached rest.
    const bool bShowScreen = bShow && Deployment >= 0.55f && !bConfigurationDetaching;
    if (bScreenVisible != bShowScreen)
    {
        bScreenVisible = bShowScreen;
        Screen->SetVisibility(bShowScreen, false);
        if (!bShowScreen) { Screen->SetComponentTickEnabled(false); }
    }
    if (!bShowScreen) { return; }
    if (bConfiguration)
    {
        // One physical render target; native UMG needs regular ticks for hover,
        // spin-box dragging and caret blink while the modal is interactive.
        Screen->SetTickMode(ETickMode::Enabled);
        Screen->SetComponentTickEnabled(true);
        return;
    }
    ReadoutSeconds -= Step;
    if (ReadoutSeconds <= 0 || bJustShown)
    {
        ReadoutSeconds = ReadoutInterval;
        bool bChanged = true;
        if (UParametricRoadStatusWidget* Readout = Cast<UParametricRoadStatusWidget>(Widget.Get()))
        {
            Readout->RefreshDisplay();
            const uint32 Revision = Readout->GetDisplayRevision();
            bChanged = Revision != RenderedRevision;
            RenderedRevision = Revision;
        }
        if (bChanged || bJustShown || !Screen->GetRenderTarget()) { Screen->RequestRenderUpdate(); }
    }
}

void UParametricRoadMonitor::BeginRetract()
{
    if (bRetracting) { return; }
    SetConfigurationWidget(nullptr);
    if (!IsValidMonitor() || Deployment <= 0) { Clear(); return; }
    RetractionStartPower = Power;
    PowerOffDelay = Power.Reveal > 0 || Power.Content > 0 || Power.Glow > 0 ? ParametricRoadMonitorPower::OffSeconds : 0;
    RetractionElapsed = 0;
    bRetractAudioPlayed = !bDeployAudioPlayed;
    if (IsValid(DeployAudio)) { DeployAudio->FadeOut(.03f, 0); }
    RetractionStartPose = ParametricRoadMonitorAnimation::Deploy(Deployment);
    RetractionStartDeployment = Deployment;
    RetractionProgress = 0;
    bRetracting = true;
    bExpandedTarget = false;
    Session.Reset();
    // A strong local lifetime only for this short teardown animation. No saved
    // object, native equipment field or network object acquires a reference.
    RetractionTicker = FTSTicker::GetCoreTicker().AddTicker(TEXT("RoadMonitorRetract"), 0.0f,
        [KeepAlive = TStrongObjectPtr<UParametricRoadMonitor>(this)](float Delta) mutable
        {
            UParametricRoadMonitor* Monitor = KeepAlive.Get();
            Monitor->bInRetractionTicker = true;
            Monitor->Tick(Delta);
            Monitor->bInRetractionTicker = false;
            const bool bContinue = Monitor->IsValidMonitor();
            if (!bContinue) { Monitor->RetractionTicker.Reset(); }
            return bContinue;
        });
}

void UParametricRoadMonitor::OnWorldCleanup(UWorld* World, bool, bool)
{
    if (Presentation && Presentation->GetWorld() == World) { Clear(); }
}

void UParametricRoadMonitor::Clear()
{
    bConfigurationDetaching = true;
    if (IsValid(Screen)) { Screen->SetVisibility(false, false); Screen->SetComponentTickEnabled(false); }
    ConfigurationWidget.Reset();
    if (ConfigurationDispatchDepth > 0)
    {
        bMonitorCleanupPending = true;
        bConfigurationCleanupPending = true;
        return;
    }
    bConfigurationCleanupPending = bMonitorCleanupPending = false;
    ClearConfigurationInteraction();
    StopAudio(); DeploySound = RetractSound = nullptr;
    HologramOcclusion.Restore();
    PaniniCollection = nullptr;
    if (RetractionTicker.IsValid() && !bInRetractionTicker) { FTSTicker::RemoveTicker(RetractionTicker); }
    RetractionTicker.Reset();
    if (WorldCleanupHandle.IsValid())
    {
        FWorldDelegates::OnWorldCleanup.Remove(WorldCleanupHandle);
        WorldCleanupHandle.Reset();
    }
    if (IsValid(Screen)) { Screen->SetWidget(nullptr); Screen->SetComponentTickEnabled(false); }
    if (IsValid(Presentation) && !Presentation->IsActorBeingDestroyed()) { Presentation->Destroy(); }
    Presentation = nullptr; ViewRoot = nullptr; ArmPivot = nullptr; HousingPivot = nullptr; Screen = nullptr;
    Session.Reset(); Gun.Reset(); Player.Reset(); Camera.Reset(); Widget.Reset();
    Deployment = Expansion = ReadoutSeconds = 0;
    Sway.Reset();
    RetractionStartPose = {};
    RetractionStartDeployment = RetractionProgress = RetractionElapsed = PowerOffDelay = 0;
    Power = RetractionStartPower = {};
    bDeployAudioPlayed = bRetractAudioPlayed = false;
    GeometryCorners.Reset(); LayoutAspect = 16.0 / 9.0;
    RenderedRevision = MAX_uint32;
    DiagnosticGateMask = MAX_uint32;
    DiagnosticCurrentMode = DiagnosticTargetMode = INDEX_NONE;
    DiagnosticOwnerMask = MAX_uint8;
    DiagnosticViewTarget.Reset();
    DiagnosticVisibleSeconds = 0;
    bLoggedRenderState = false;
    bRetracting = bVisible = bScreenVisible = bExpandedTarget = false;
    bConfigurationDetaching = false;
}

void UParametricRoadMonitor::BeginDestroy()
{
    Clear();
    Super::BeginDestroy();
}

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Math/PerspectiveMatrix.h"
#include "UObject/UnrealType.h"

// No world, pawn, play session, native input or construction is created. This
// verifies the disposable presentation contract, not first-person appearance.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParametricRoadMonitorContractTest, "Roadworks.Editor.MonitorContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParametricRoadMonitorContractTest::RunTest(const FString&)
{
    TStrongObjectPtr<UParametricRoadMonitor> Monitor(NewObject<UParametricRoadMonitor>());
    TestTrue(TEXT("The monitor is a transient local object"), Monitor->GetClass()->HasAnyClassFlags(CLASS_Transient));
    for (TFieldIterator<FProperty> Property(Monitor->GetClass(), EFieldIteratorFlags::ExcludeSuper); Property; ++Property)
    {
        TestFalse(FString::Printf(TEXT("%s is not saved"), *Property->GetName()), Property->HasAnyPropertyFlags(CPF_SaveGame));
        TestFalse(FString::Printf(TEXT("%s is not replicated"), *Property->GetName()), Property->HasAnyPropertyFlags(CPF_Net));
    }
    TestFalse(TEXT("An empty monitor has no presentation"), Monitor->IsValidMonitor());
    TestFalse(TEXT("An empty monitor is not visible"), Monitor->IsVisible());
    TestTrue(TEXT("Detaching an empty configuration is harmless"), Monitor->SetConfigurationWidget(nullptr));
    TestFalse(TEXT("Missing monitor cannot route a pointer"), Monitor->RouteConfigurationPointer(FVector2D(.5, .5)));
    TestFalse(TEXT("Missing monitor cannot press a pointer key"), Monitor->PressConfigurationPointerKey(EKeys::LeftMouseButton));
    TestFalse(TEXT("Missing monitor cannot dispatch a keyboard key"), Monitor->PressConfigurationKey(EKeys::A));
    TestFalse(TEXT("Missing monitor cannot dispatch typed characters"), Monitor->SendConfigurationChar(TEXT("12")));
    Monitor->ReleaseConfigurationPointerKey(EKeys::LeftMouseButton);
    Monitor->ReleaseConfigurationKey(EKeys::A);
    Monitor->SetExpanded(true);
    TestFalse(TEXT("Missing presentation cannot expose a modal surface"), Monitor->IsExpandedReady());
    TestFalse(TEXT("Missing presentation has no configuration rectangle"), Monitor->GetConfigurationRect().bIsValid);
    Monitor->BeginRetract();
    Monitor->Tick(0.1f);
    Monitor->Clear();
    Monitor->Clear();
    TestFalse(TEXT("Empty teardown is idempotent"), Monitor->IsValidMonitor());
    TestTrue(TEXT("Widget and housing front face the same side"),
        ScreenPose.TransformVectorNoScale(FVector::ForwardVector).Equals(-FVector::ForwardVector, 0.0001));
    const FVector ScreenExtent = ScreenPose.TransformVector(FVector(0, ScreenPixels.X, ScreenPixels.Y)).GetAbs();
    TestTrue(TEXT("The fixed widget quad fits the modeled aperture"), ScreenExtent.Equals(FVector(0, 29, 19.4), 0.0001));
    TestTrue(TEXT("The screen stays at the recessed display plane"), ScreenPose.GetLocation().Equals(ScreenLocation, 0.0001));
    TestTrue(TEXT("The display is behind the lip and ahead of the opaque backing"), ScreenLocation.X > -2.10 && ScreenLocation.X < -1.52);
    const UWidgetComponent* Defaults = GetDefault<UWidgetComponent>();
    TestFalse(TEXT("Engine world widgets default to no hardware input"), Defaults->GetReceiveHardwareInput());
    const FBoolProperty* Gamma = FindFProperty<FBoolProperty>(UWidgetComponent::StaticClass(), TEXT("bApplyGammaCorrection"));
    if (TestNotNull(TEXT("Pinned world-widget gamma setting is available"), Gamma))
    { TestFalse(TEXT("World-screen sampling keeps the engine linear render path"), Gamma->GetPropertyValue_InContainer(Defaults)); }
    UMaterialInterface* ScreenMaterial = LoadObject<UMaterialInterface>(nullptr,
        TEXT("/Roadworks/Monitor/Materials/M_RoadMonitorWidget.M_RoadMonitorWidget"));
    if (TestNotNull(TEXT("The screen-specific material loads"), ScreenMaterial))
    {
        float Luminance = 0;
        TestTrue(TEXT("The saved material exposes its bounded screen-only luminance"),
            ScreenMaterial->GetScalarParameterValue(FMaterialParameterInfo(TEXT("ScreenLuminance")), Luminance));
        TestTrue(TEXT("The brighter screen white level retains headroom independently of deployment tint"), FMath::IsNearlyEqual(Luminance, .65f, .000001f));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParametricRoadMonitorPointerTest, "Roadworks.Editor.MonitorPointerProjection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParametricRoadMonitorPointerTest::RunTest(const FString&)
{
    // Pure projection/rasterization math. No world, Slate user, interaction
    // component or injected input is created by this test.
    const FVector2D Coordinates[] = {FVector2D(0, 0), FVector2D(0, 1), FVector2D(1, 1), FVector2D(1, 0)};
    const int32 Triangles[2][3] = {{0, 1, 2}, {0, 2, 3}};
    for (double FOV : {70.0, 90.0, 120.0})
    for (double Aspect : {4.0 / 3, 16.0 / 9, 32.0 / 9})
    for (double Scale : {.75, .9822620153, 1.25})
    {
        FSceneViewProjectionData Projection;
        Projection.ProjectionMatrix = FReversedZPerspectiveMatrix(FMath::DegreesToRadians(FOV * .5), Aspect, 1.0, .1);
        Projection.SetViewRectangle(FIntRect(100, 50, 100 + FMath::RoundToInt(Aspect * 900), 950));
        FRoadMonitorPanini Panini; Panini.Scale = Scale;
        FRoadMonitorProjectedVertex Reference;
        const FVector ReferencePoint(15, 7, 80);
        TestTrue(TEXT("Pinned Panini formula projects a forward vertex"), ProjectRoadMonitorVertex(ReferencePoint, FVector2D::ZeroVector, Projection, Panini, Reference));
        const FVector2D Expected(.5 + .5 * ReferencePoint.X / ReferencePoint.Z * Scale,
            .5 - .5 * Aspect * ReferencePoint.Y / ReferencePoint.Z * Scale);
        TestTrue(TEXT("Depth-zero shader mapping is fixed-90 with live model scaling across camera FOVs"), Reference.Position.Equals(Expected, 1e-6));
        // Also exercise an offset player view and constrained camera viewport.
        Projection.SetConstrainedViewRectangle(FIntRect(Projection.GetViewRect().Min + FIntPoint(12, 20),
            Projection.GetViewRect().Max - FIntPoint(12, 20)));
        for (double Depth : {0.0, .5, 1.0})
        for (double Skew : {0.0, 1.0})
        {
            Panini.Depth = Depth; Panini.Skew = Skew;
            Panini.FOVMulti = Depth == .5 ? 1.3 : 1;
            Panini.ScalingFudge = Depth == .5 ? .35 : 1;
            FRoadMonitorProjectedVertex Vertices[4];
            const FTransform Pose(FRotator(9, 14, -6), FVector(2, 1, 80));
            for (int32 I = 0; I < 4; ++I)
            {
                const FVector Point = Pose.TransformPosition(FVector((Coordinates[I].X - .5) * 29, (.5 - Coordinates[I].Y) * 19.4, 0));
                TestTrue(TEXT("All deformed widget triangle vertices project"), ProjectRoadMonitorVertex(Point, Coordinates[I], Projection, Panini, Vertices[I]));
            }
            for (const auto& Triangle : Triangles)
            for (FVector Barycentric : {FVector(.2, .3, .5), FVector(.01, .98, .01), FVector(.499, .002, .499)})
            {
                FVector2D Pointer = FVector2D::ZeroVector, ExpectedUV = FVector2D::ZeroVector;
                double Weight = 0;
                for (int32 I = 0; I < 3; ++I)
                {
                    const auto& Vertex = Vertices[Triangle[I]];
                    const double ProjectedWeight = Barycentric[I] / Vertex.InverseW;
                    Pointer += Vertex.Position * ProjectedWeight; Weight += ProjectedWeight;
                    ExpectedUV += Vertex.UV * Barycentric[I];
                }
                Pointer /= Weight;
                FVector2D MappedUV;
                TestTrue(TEXT("The actual rendered triangle accepts its interior pointer"), RoadMonitorTriangleUV(Pointer,
                    Vertices[Triangle[0]], Vertices[Triangle[1]], Vertices[Triangle[2]], MappedUV, false));
                TestTrue(TEXT("Perspective-correct mapping recovers original widget coordinates"), MappedUV.Equals(ExpectedUV, 1e-6));
                const FVector OriginalPlane(0, (.5 - MappedUV.X) * 1024, (.5 - MappedUV.Y) * 688);
                const FVector2D NativeHit(.5 - OriginalPlane.Y / 1024, .5 - OriginalPlane.Z / 688);
                TestTrue(TEXT("Custom hit converts back to the undeformed plane expected by UWidgetComponent"), NativeHit.Equals(ExpectedUV, 1e-6));
            }
            FVector2D Outside;
            TestFalse(TEXT("Pointer outside the rendered quad cannot begin a click"), RoadMonitorTriangleUV(FVector2D(-2, -2), Vertices[0], Vertices[1], Vertices[2], Outside, false)
                || RoadMonitorTriangleUV(FVector2D(-2, -2), Vertices[0], Vertices[2], Vertices[3], Outside, false));
        }
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParametricRoadMonitorDispatchTest, "Roadworks.Editor.MonitorDispatchLifetime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParametricRoadMonitorDispatchTest::RunTest(const FString&)
{
    // Exercise the production nesting/cleanup state without creating a native
    // Slate user, a registered component, world or injected gameplay input.
    TStrongObjectPtr<UParametricRoadMonitor> Monitor(NewObject<UParametricRoadMonitor>());
    Monitor->ConfigurationDispatchDepth = 2;
    Monitor->ConfigurationPointerKeys.Add(EKeys::LeftMouseButton);
    Monitor->ConfigurationKeyboardKeys.Add(EKeys::A);
    Monitor->Clear();
    TestTrue(TEXT("Close invalidates routing immediately during nested native dispatch"), Monitor->bConfigurationDetaching);
    TestTrue(TEXT("World/actor cleanup waits while native event replies still use their user"), Monitor->bMonitorCleanupPending);
    Monitor->ReleaseConfigurationPointerKey(EKeys::LeftMouseButton);
    Monitor->ReleaseConfigurationKey(EKeys::A);
    TestTrue(TEXT("Reentrant host release preserves held pointer keys for deferred cleanup"), Monitor->ConfigurationPointerKeys.Contains(EKeys::LeftMouseButton));
    TestTrue(TEXT("Reentrant host release preserves held keyboard keys for deferred cleanup"), Monitor->ConfigurationKeyboardKeys.Contains(EKeys::A));
    Monitor->FinishConfigurationDispatch();
    TestEqual(TEXT("Completing an inner dispatch retains the outer event lifetime"), Monitor->ConfigurationDispatchDepth, 1);
    TestTrue(TEXT("Inner completion cannot destroy the virtual input resources"), Monitor->bMonitorCleanupPending && Monitor->bConfigurationCleanupPending);
    Monitor->FinishConfigurationDispatch();
    TestEqual(TEXT("The outer dispatch fully unwinds before cleanup"), Monitor->ConfigurationDispatchDepth, 0);
    TestFalse(TEXT("Cleanup completes after the outer native ProcessReply returned"), Monitor->bMonitorCleanupPending || Monitor->bConfigurationCleanupPending || Monitor->bConfigurationDetaching);
    TestTrue(TEXT("Final cleanup consumes all held-key ownership"), Monitor->ConfigurationPointerKeys.IsEmpty() && Monitor->ConfigurationKeyboardKeys.IsEmpty());
    Monitor->Clear();
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParametricRoadMonitorFramingTest, "Roadworks.Editor.MonitorFraming",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParametricRoadMonitorFramingTest::RunTest(const FString&)
{
    // Validate the requested upper-left layout and left-hand mounting using
    // the real imported V2 bounds, without creating a world or a player.
    const TCHAR* Names[] = {TEXT("Housing"), TEXT("Arm"), TEXT("Mount")};
    const UStaticMesh* Meshes[3] = {};
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(Names); ++Index)
    {
        const FString Path = FString::Printf(TEXT("/Roadworks/Monitor/Meshes/SM_RoadMonitor%s.SM_RoadMonitor%s"), Names[Index], Names[Index]);
        Meshes[Index] = LoadObject<UStaticMesh>(nullptr, *Path);
        if (!TestNotNull(*Path, Meshes[Index])) { return false; }
        const TArray<FStaticMaterial>& Materials = Meshes[Index]->GetStaticMaterials();
        TestEqual(TEXT("V2 has one atlas material slot"), Materials.Num(), 1);
        if (!Materials.IsEmpty()) { TestEqual(TEXT("V2 atlas slot identity"), Materials[0].MaterialSlotName, FName(TEXT("PR2_Baked_PBR"))); }
    }
    const TArray<FVector> Corners = MakeMonitorCorners(Meshes[0], Meshes[1], Meshes[2]);
    for (double Aspect : {4.0 / 3.0, 16.0 / 9.0, 21.0 / 9.0, 32.0 / 9.0})
    {
        const FTransform ViewPose = MakeMonitorViewPose(0, Aspect, Corners);
        FBox2D Projected(ForceInit);
        FBox2D HousingProjected(ForceInit), MountProjected(ForceInit);
        for (int32 Index = 0; Index < Corners.Num(); ++Index)
        {
            const FVector& Corner = Corners[Index];
            const FVector InView = ViewPose.TransformPosition(Corner);
            if (!TestTrue(TEXT("Every deployed part stays in front of the camera"), InView.X > 1)) { return false; }
            const FVector2D Point(0.5 + 0.5 * InView.Y / InView.X,
                0.5 - 0.5 * Aspect * InView.Z / InView.X);
            Projected += Point;
            if (Index < 8) { HousingProjected += Point; }
            if (Index >= 16) { MountProjected += Point; }
        }
        AddInfo(FString::Printf(TEXT("Monitor framing aspect %.3f: min=%s max=%s"), Aspect,
            *Projected.Min.ToString(), *Projected.Max.ToString()));
        TestTrue(TEXT("The readable housing anchors to the left margin"), FMath::IsNearlyEqual(HousingProjected.Min.X, 0.045, 0.0001));
        TestTrue(TEXT("The accessory stays in the left third"), Projected.Max.X <= 0.35);
        TestTrue(TEXT("The accessory anchors to the top margin"), FMath::IsNearlyEqual(Projected.Min.Y, 0.07, 0.0001));
        TestTrue(TEXT("The accessory clears the bottom native hints"), Projected.Max.Y <= 0.70);
        TestTrue(TEXT("Only a small sliver of the mount remains onscreen"), MountProjected.Min.X < 0 && MountProjected.Max.X > 0 && MountProjected.Max.X < 0.025);
        TestTrue(TEXT("At least two thirds of the mount are hidden off the left edge"),
            MountProjected.Max.X / MountProjected.GetSize().X < 1.0 / 3.0);
        const FVector HousingCenter = ViewPose.TransformPosition(ArmLocation + HousingHingeLocation + HousingLocation);
        const FVector ArmCenter = ViewPose.TransformPosition(ArmLocation + Meshes[1]->GetBoundingBox().GetCenter());
        const FVector MountCenter = ViewPose.TransformPosition(MountLocation + Meshes[2]->GetBoundingBox().GetCenter());
        TestTrue(TEXT("The arm appears to the left of the screen"), ArmCenter.Y / ArmCenter.X < HousingCenter.Y / HousingCenter.X);
        TestTrue(TEXT("The mount appears to the left of the screen"), MountCenter.Y / MountCenter.X < HousingCenter.Y / HousingCenter.X);
        const FTransform Expanded = MakeExpandedMonitorPose(Aspect, Corners);
        TestTrue(TEXT("Expanded frame is parallel to the native viewport surface"), Expanded.Rotator().IsNearlyZero());
        for (int32 I = 0; I < 8; ++I)
        {
            const FVector P = Expanded.TransformPosition(Corners[I]);
            const FVector2D UV = ProjectMonitorPoint(P, Aspect);
            TestTrue(TEXT("The expanded housing fits inside the viewport margins"), P.X > 1 && UV.X >= .0699 && UV.X <= .9301 && UV.Y >= .0699 && UV.Y <= .9301);
        }
        const FBox2D Rect = ExpandedScreenRect(Expanded, Aspect);
        TestTrue(TEXT("Expanded configuration matches a visible modeled aperture"), Rect.bIsValid && Rect.Min.X > .07 && Rect.Min.Y > .07
            && Rect.Max.X < .93 && Rect.Max.Y < .93 && Rect.GetSize().Y > .45);
        TestTrue(TEXT("Viewport surface keeps the modeled glass aspect without stretching"),
            FMath::IsNearlyEqual(Rect.GetSize().X * Aspect / Rect.GetSize().Y, 29.0 / 19.4, 0.0001));
        TestTrue(TEXT("Compact transition endpoint preserves the accepted deployed framing"),
            MakeMonitorTransitionPose(ViewPose, Expanded, 0).Equals(ViewPose, 0));
        TestTrue(TEXT("Expanded transition endpoint preserves the exact input surface"),
            MakeMonitorTransitionPose(ViewPose, Expanded, 1).Equals(Expanded, 0));
        const FVector HousingOrigin = ArmLocation + HousingHingeLocation + HousingLocation;
        // Worst independent spring limits, using the actual articulated mesh
        // bounds. No world/gameplay is involved in this viewport projection.
        for (float Arm : {-ParametricRoadMonitorSway::MaxArmDegrees, ParametricRoadMonitorSway::MaxArmDegrees})
        for (float Residual : {-ParametricRoadMonitorSway::MaxResidualDegrees, ParametricRoadMonitorSway::MaxResidualDegrees})
        {
            const FQuat ArmRotation = FRotator(0, 0, Arm).Quaternion();
            const FQuat HousingRotation = FRotator(0, 0, -ParametricRoadMonitorSway::HousingCompensation * Arm + Residual).Quaternion();
            for (int32 I = 0; I < 8; ++I)
            {
                const FVector Assembly = ArmLocation + ArmRotation.RotateVector(HousingHingeLocation
                    + HousingRotation.RotateVector(HousingLocation + Corners[I] - HousingOrigin));
                const FVector Point = ViewPose.TransformPosition(Assembly);
                const FVector2D UV = ProjectMonitorPoint(Point, Aspect);
                TestTrue(TEXT("Maximum compact sway keeps the readable housing inside viewport margins"),
                    Point.X > 1 && UV.X >= .015 && UV.X <= .985 && UV.Y >= .015 && UV.Y <= .985);
            }
        }
        for (int32 Frame = 0; Frame <= 120; ++Frame)
        {
            const float Progress = Frame / 120.0f;
            const auto Motion = ParametricRoadMonitorAnimation::Expanded(Progress);
            const FTransform Travel = MakeMonitorTransitionPose(ViewPose, Expanded, Progress);
            const FQuat ArmRotation = FRotator(0, 0, Motion.ArmRoll).Quaternion();
            const FQuat HousingRotation = FRotator(0, 0, Motion.HousingRoll).Quaternion();
            for (int32 I = 0; I < 8; ++I)
            {
                const FVector MeshCorner = Corners[I] - HousingOrigin;
                const FVector AssemblyPoint = ArmLocation + ArmRotation.RotateVector(HousingHingeLocation
                    + HousingRotation.RotateVector(HousingLocation + MeshCorner));
                const FVector Point = Travel.TransformPosition(AssemblyPoint);
                const FVector2D UV = ProjectMonitorPoint(Point, Aspect);
                if (Point.X <= 1 || UV.X < .015 || UV.X > .985 || UV.Y < .015 || UV.Y > .985)
                {
                    AddError(FString::Printf(TEXT("Expanded travel clips housing: aspect %.3f phase %.3f corner %d uv %s"),
                        Aspect, Progress, I, *UV.ToString()));
                    return false;
                }
            }
        }
    }
    return !HasAnyErrors();
}
#endif
