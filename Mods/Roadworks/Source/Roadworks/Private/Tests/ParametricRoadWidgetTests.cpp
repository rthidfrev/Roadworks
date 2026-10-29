#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "ParametricRoadWidgets.h"
#include "ParametricRoadLayerWidget.h"
#include "Components/Button.h"
#include "Components/SpinBox.h"
#include "Components/ScrollBox.h"
#include "Components/CheckBox.h"
#include "ParametricRoadUIStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RenderingThread.h"
#include "Slate/WidgetRenderer.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include "Serialization/BufferArchive.h"

// Worldless Slate layout/render checks. No player, play session or construction.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParametricRoadWidgetTest, "Roadworks.Editor.WidgetLayout",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParametricRoadWidgetTest::RunTest(const FString&)
{
    if (!FSlateApplication::IsInitialized()) { AddError(TEXT("Rendering is required.")); return false; }
    TStrongObjectPtr<UParametricRoadSession> Session(NewObject<UParametricRoadSession>());
    TestNull(TEXT("Session has no game world"), Session->GetWorld());
    for (TFieldIterator<FProperty> Property(Session->GetClass(), EFieldIteratorFlags::ExcludeSuper); Property; ++Property)
    {
        TestFalse(TEXT("Presentation state is not saved"), Property->HasAnyPropertyFlags(CPF_SaveGame));
        TestFalse(TEXT("Presentation state is not replicated"), Property->HasAnyPropertyFlags(CPF_Net));
    }
    Session->bAssessed = true; Session->Phase = EParametricRoadPhase::Ready;
    Session->BlueprintName = TEXT("EXAMPLE ROAD");
    Session->SetParameter(EParametricRoadParameter::StartHeading,25);
    Session->SetParameter(EParametricRoadParameter::EndHeading,70);
    Session->SetParameter(EParametricRoadParameter::EndGrade,-8);
    Session->SelectPoint(Session->GetPointCount()-1);
    TStrongObjectPtr<UParametricRoadStatusWidget> Status(NewObject<UParametricRoadStatusWidget>());
    Status->Initialize(); Status->SetSession(Session.Get());
    TSharedRef<SWidget> Slate = Status->TakeWidget();
    auto* Compact = Status->GetWorksheet();
    if (!TestNotNull(TEXT("Compact renders the shared native worksheet"), Compact)) return false;
    TestFalse(TEXT("Compact never takes keyboard focus"), Status->IsFocusable() || Compact->IsFocusable());
    TestEqual(TEXT("Compact descendants never intercept placement input"), Compact->GetVisibility(), ESlateVisibility::HitTestInvisible);
    TStrongObjectPtr<UParametricRoadLayerWidget> Expanded(NewObject<UParametricRoadLayerWidget>());
    Expanded->Initialize(); Expanded->SetSession(Session.Get());
    const auto ExpandedSlate = Expanded->TakeWidget();
    TestEqual(TEXT("Window title is fixed rather than repeating the blueprint"), Compact->Title->GetText().ToString(), FString(TEXT("Roadworks")));
    TestEqual(TEXT("Both sizes use the identical native widget class"), Compact->GetClass(), Expanded->GetClass());
    TSet<FString> Captions;
    for (int32 I=0; I<int32(EParametricRoadAdjustment::Count); ++I)
    {
        const FString Caption = UParametricRoadDiagramWidget::GetDiagramCaption(EParametricRoadAdjustment(I)).ToString();
        TestFalse(TEXT("Every parameter has a diagram caption"), Caption.IsEmpty()); Captions.Add(Caption);
    }
    TestEqual(TEXT("Twelve distinct flat diagrams are retained"), Captions.Num(),12);
    // The actual gameplay limits are grade +/-45 and bank +/-60 degrees.
    // The full section, its thickness and end markers must remain visible.
    for (const FVector2D DiagramSize : {FVector2D(650, 128), FVector2D(650, 160)})
    {
        const FVector2D SectionCenter(DiagramSize.X * 0.5, DiagramSize.Y * 0.56);
        for (float Degrees : {-60.0f, -45.0f, 0.0f, 45.0f, 60.0f})
        {
            const float Angle = FMath::DegreesToRadians(Degrees);
            const float Radius = UParametricRoadDiagramWidget::FitSectionRadius(DiagramSize, Angle);
            TestTrue(TEXT("Section stays visible at the configured angle limits"), Radius > 0);
            const FVector2D Offset(FMath::Cos(Angle) * Radius, -FMath::Sin(Angle) * Radius);
            for (const FVector2D Endpoint : {SectionCenter - Offset, SectionCenter + Offset})
            {
                TestTrue(TEXT("Deck endpoint and stroke fit horizontally"), Endpoint.X >= 17 && Endpoint.X <= DiagramSize.X - 17);
                TestTrue(TEXT("Deck thickness and end marker fit vertically"), Endpoint.Y >= 17 && Endpoint.Y <= DiagramSize.Y - 17);
            }
        }
    }
    const FVector2D PlanSize(650, 128);
    const auto Reference = UParametricRoadDiagramWidget::MakeHeadingDiagram(PlanSize, 0, 0);
    for (double Heading : {-180.0, -90.0, 0.0, 90.0, 170.0})
    {
        const auto Straight = UParametricRoadDiagramWidget::MakeHeadingDiagram(PlanSize, Heading, Heading);
        TestTrue(TEXT("Equal absolute headings always draw the same straight road"), Straight.Start.Equals(Reference.Start)
            && Straight.StartControl.Equals(Reference.StartControl) && Straight.EndControl.Equals(Reference.EndControl) && Straight.End.Equals(Reference.End));
        TestTrue(TEXT("Straight schematic has attached collinear tangents"), FMath::IsNearlyEqual(Straight.StartControl.Y, Straight.Start.Y)
            && FMath::IsNearlyEqual(Straight.EndControl.Y, Straight.Start.Y));
    }
    const auto Bend = UParametricRoadDiagramWidget::MakeHeadingDiagram(PlanSize, 25, 70);
    for (double Rotation : {-180.0, -90.0, 15.0, 90.0, 170.0, 359.0})
    {
        const auto Rotated = UParametricRoadDiagramWidget::MakeHeadingDiagram(PlanSize, 25 + Rotation, 70 + Rotation);
        TestTrue(TEXT("Bending schematic is independent of common world rotation"), Rotated.StartControl.Equals(Bend.StartControl)
            && Rotated.EndControl.Equals(Bend.EndControl));
    }
    for (double StartHeading : {-180.0, -100.0, -90.0, -45.0, 0.0, 45.0, 90.0, 170.0})
    {
        for (double EndHeading : {-180.0, -100.0, -90.0, 0.0, 90.0, 170.0})
        {
            const auto Shape = UParametricRoadDiagramWidget::MakeHeadingDiagram(PlanSize, StartHeading, EndHeading);
            TestTrue(TEXT("Heading adjustments never move schematic endpoint anchors"), Shape.Start.Equals(Reference.Start) && Shape.End.Equals(Reference.End));
            TestTrue(TEXT("Curve starts along its attached heading arrow"), (Shape.StartControl - Shape.Start).GetSafeNormal().Equals(Shape.StartDirection));
            TestTrue(TEXT("Curve arrives along its attached heading arrow"), (Shape.End - Shape.EndControl).GetSafeNormal().Equals(Shape.EndDirection));
            for (const FVector2D Control : {Shape.Start, Shape.StartControl, Shape.EndControl, Shape.End})
            {
                TestTrue(TEXT("Heading curve and its stroke fit inside the diagram"),
                    Control.X >= 18 - UE_KINDA_SMALL_NUMBER && Control.X <= PlanSize.X - 18 + UE_KINDA_SMALL_NUMBER
                    && Control.Y >= 18 - UE_KINDA_SMALL_NUMBER && Control.Y <= PlanSize.Y - 18 + UE_KINDA_SMALL_NUMBER);
            }
        }
    }

    const uint32 Revision = Status->GetDisplayRevision();
    TestFalse(TEXT("Unchanged readouts do not request another render"),Status->RefreshDisplay());
    TestEqual(TEXT("Unchanged readouts retain revision"),Status->GetDisplayRevision(),Revision);
    Session->bFineAdjustment = true;
    TestTrue(TEXT("Fine step invalidates compact rendering"),Status->RefreshDisplay());
    TestTrue(TEXT("Fine step readout is synchronized"),Compact->Fine->IsChecked());
    Session->bFineAdjustment = false; Status->RefreshDisplay();
    TestFalse(TEXT("Standard step is synchronized too"),Compact->Fine->IsChecked());
    FWidgetRenderer Renderer(false); const FVector2D DrawSize(1024,688);
    TStrongObjectPtr<UTextureRenderTarget2D> Target(FWidgetRenderer::CreateTargetFor(DrawSize,TF_Bilinear,false));
    TStrongObjectPtr<UTextureRenderTarget2D> ExpandedTarget(FWidgetRenderer::CreateTargetFor(DrawSize,TF_Bilinear,false));
    const FString Folder = FPaths::ProjectSavedDir()/TEXT("RoadworksValidation"); IFileManager::Get().MakeDirectory(*Folder,true);
    const auto Render = [&](EParametricRoadAdjustment Selection,const TCHAR* Name)
    {
        Session->Adjustment=Selection; Status->RefreshDisplay(); Expanded->RefreshDisplay();
        for (int32 Pass=0;Pass<4;++Pass)
        {
            Renderer.DrawWidget(Target.Get(),Slate,DrawSize,1.f/60);
            Renderer.DrawWidget(ExpandedTarget.Get(),ExpandedSlate,DrawSize,1.f/60);
        }
        FlushRenderingCommands();
        for (int32 I=0;I<11;++I)
        {
            const auto* A=Compact->Numbers[I].Get(); const auto* B=Expanded->Numbers[I].Get();
            const auto& GA=A->GetCachedGeometry(); const auto& GB=B->GetCachedGeometry();
            TestTrue(TEXT("Compact and expanded fields have identical logical bounds"),GA.GetAbsolutePosition().Equals(GB.GetAbsolutePosition(),.1)
                &&GA.GetAbsoluteSize().Equals(GB.GetAbsoluteSize(),.1));
            TestTrue(TEXT("Numeric fields retain a readable width"),GA.GetLocalSize().X>30);
            TestEqual(TEXT("Both sizes show identical numbers"),A->GetValue(),B->GetValue());
        }
        const auto Bounds=Compact->Diagram->GetCachedGeometry().GetLocalSize();
        TestTrue(TEXT("Flat diagram retains readable space"),Bounds.X>450&&Bounds.Y>250);
        TestEqual(TEXT("Every point has a selector"),Compact->PointButtons.Num(),Session->GetPointCount());
        TArray<FColor> CompactPixels,ExpandedPixels;
        Target->GameThread_GetRenderTargetResource()->ReadPixels(CompactPixels);
        ExpandedTarget->GameThread_GetRenderTargetResource()->ReadPixels(ExpandedPixels);
        TestTrue(TEXT("Both monitor sizes render exactly the same route surface"),CompactPixels==ExpandedPixels);
        FBufferArchive PNG; TestTrue(TEXT("Shared worksheet exports"),FImageUtils::ExportRenderTarget2DAsPNG(Target.Get(),PNG));
        TestTrue(TEXT("Shared worksheet capture saved"),FFileHelper::SaveArrayToFile(PNG,*(Folder/Name)));
    };
    Render(EParametricRoadAdjustment::Heading,TEXT("monitor-ui.png"));
    Render(EParametricRoadAdjustment::Grade,TEXT("monitor-ui-grade.png"));
    Render(EParametricRoadAdjustment::Bank,TEXT("monitor-ui-bank.png"));
    Render(EParametricRoadAdjustment::Height,TEXT("monitor-ui-height.png"));
    for(int32 I=4;I<int32(EParametricRoadAdjustment::Count);++I)
    { const FString Name=FString::Printf(TEXT("monitor-ui-global-%02d.png"),I); Render(EParametricRoadAdjustment(I),*Name); }
    for(int32 I=1;I<=30;++I)
    {
        FParametricRoadWaypoint Point; Point.Point.Pose=FTransform(FVector(I*600,FMath::Sin(I*.25)*3000,I*25));
        Session->IntermediatePoints.Add(Point);
    }
    Session->SelectPoint(17); Render(EParametricRoadAdjustment::EdgeStep,TEXT("monitor-ui-32points.png"));
    TestNull(TEXT("Rendering created no game world"),Session->GetWorld());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParametricRoadControlsTest, "Roadworks.Editor.DirectControls",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParametricRoadControlsTest::RunTest(const FString&)
{
    TStrongObjectPtr<UParametricRoadSession> Fresh(NewObject<UParametricRoadSession>());
    Fresh->bAssessed = true;
    Fresh->SetParameter(EParametricRoadParameter::StartHeading, 45);
    TestEqual(TEXT("Untouched arrival inherits the initial heading"), Fresh->EndHeading, 45.0);
    Fresh->SetParameter(EParametricRoadParameter::EndHeading, 90);
    Fresh->SetParameter(EParametricRoadParameter::StartHeading, 60);
    TestEqual(TEXT("An explicitly adjusted arrival is not overwritten"), Fresh->EndHeading, 90.0);
    Fresh->CursorCenter = FVector(100, 200, 300);
    Fresh->bCursorValid = true;
    Fresh->Primary();
    TestEqual(TEXT("First click starts the path"), Fresh->Phase, EParametricRoadPhase::Drawing);
    TestTrue(TEXT("First clicked position is the start, never the end"), Fresh->StartCenter.Equals(FVector(100, 200, 300)));
    Fresh->CursorCenter = FVector(900, 1000, 1100);
    Fresh->Primary();
    TestEqual(TEXT("Second click fixes the end before construction"), Fresh->Phase, EParametricRoadPhase::Ready);
    TestTrue(TEXT("Start stays at the first clicked position"), Fresh->StartCenter.Equals(FVector(100, 200, 300)));
    TestTrue(TEXT("End is the second clicked position"), Fresh->EndCenter.Equals(FVector(900, 1000, 1100)));

    TStrongObjectPtr<UParametricRoadSession> Session(NewObject<UParametricRoadSession>());
    Session->bAssessed = true;
    Session->Phase = EParametricRoadPhase::Ready;
    Session->bEditStart = false;
    const FVector Start(123, 456, 789), End(1000, 2000, 3000);
    Session->StartCenter = Start;
    Session->EndCenter = End;
    // Regressions: deck-plane and axis changes used to erase placement progress.
    Session->SetParameter(EParametricRoadParameter::DeckPlane, 2);
    Session->ToggleForwardAxis();
    Session->SetParameter(EParametricRoadParameter::EndGrade, 10);
    Session->Adjustment = EParametricRoadAdjustment::Grade;
    Session->AdjustValue(1);
    TestEqual(TEXT("Value action edits selected endpoint"), Session->GetParameter(EParametricRoadParameter::EndGrade), 11.0);
    TestEqual(TEXT("Other endpoint is preserved"), Session->GetParameter(EParametricRoadParameter::StartGrade), 0.0);
    const double Heading = Session->StartHeading;
    for (int32 I = 0; I < int32(EParametricRoadAdjustment::Count); ++I) { Session->CycleAdjustment(); }
    TestEqual(TEXT("Selecting controls never adjusts geometry"), Session->StartHeading, Heading);
    TestEqual(TEXT("Parameter changes preserve confirmed endpoint phase"), Session->Phase, EParametricRoadPhase::Ready);
    TestTrue(TEXT("Start anchor survives all adjustments"), Session->StartCenter.Equals(Start));
    TestTrue(TEXT("End anchor survives all adjustments"), Session->EndCenter.Equals(End));
    for (EParametricRoadPhase Editable : {EParametricRoadPhase::Start, EParametricRoadPhase::Drawing, EParametricRoadPhase::Ready})
    {
        Session->Phase = Editable;
        Session->Adjustment = EParametricRoadAdjustment::Bank;
        const double Before = Session->EndBank;
        Session->ApplyWheel(1, true);
        TestEqual(TEXT("Held wheel selects without changing the previous parameter"), Session->EndBank, Before);
        TestEqual(TEXT("Held wheel advances selection"), Session->Adjustment, EParametricRoadAdjustment::Height);
        Session->ApplyWheel(-1, true);
        Session->ApplyWheel(1, false);
        TestEqual(TEXT("Released wheel adjusts the selected parameter in every editable phase"), Session->EndBank, Before + 1.0);
        TestEqual(TEXT("Wheel changes preserve the placement step"), Session->Phase, Editable);
        TestTrue(TEXT("Wheel changes preserve the first anchor"), Session->StartCenter.Equals(Start));
        TestTrue(TEXT("Wheel changes preserve the last anchor"), Session->EndCenter.Equals(End));
    }
    Session->Phase = EParametricRoadPhase::Ready;
    Session->AddPoint();
    TestEqual(TEXT("Adding the frozen arrival creates one waypoint"), Session->IntermediatePoints.Num(), 1);
    TestEqual(TEXT("Adding a point resumes live extension"), Session->Phase, EParametricRoadPhase::Drawing);
    TestTrue(TEXT("Waypoint captures arrival position"), Session->IntermediatePoints[0].Point.Pose.GetLocation().Equals(End));
    Session->Phase = EParametricRoadPhase::Ready;
    Session->Secondary();
    TestEqual(TEXT("Back from confirmation preserves all waypoints"), Session->IntermediatePoints.Num(), 1);
    Session->Secondary();
    TestEqual(TEXT("Back while drawing removes the last waypoint"), Session->IntermediatePoints.Num(), 0);
    TestEqual(TEXT("Removing a waypoint retains the start"), Session->Phase, EParametricRoadPhase::Drawing);
    Session->Secondary();
    TestEqual(TEXT("Back with no intermediate point returns to start"), Session->Phase, EParametricRoadPhase::Start);
    Session->Phase = EParametricRoadPhase::Ready;
    Session->SetParameter(EParametricRoadParameter::EndGrade, 1000);
    TestEqual(TEXT("Direct angles remain bounded"), Session->EndGrade, 45.0);
    Session->Phase = EParametricRoadPhase::Building;
    Session->AdjustValue(-1);
    Session->SetParameter(EParametricRoadParameter::EndGrade, 0);
    TestEqual(TEXT("Submitted routes cannot be adjusted mid-construction"), Session->EndGrade, 45.0);
    TestNull(TEXT("Parameter checks created no world"), Session->GetWorld());
    return true;
}

#endif
