#include "ParametricRoadWidgets.h"
#include "ParametricRoadUIStyle.h"
#include "ParametricRoadLayerWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScaleBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SLeafWidget.h"

namespace
{
    using namespace ParametricRoadUI;

    class SRoadDiagram : public SLeafWidget
    {
    public:
        SLATE_BEGIN_ARGS(SRoadDiagram) {} SLATE_ARGUMENT(TWeakObjectPtr<UParametricRoadSession>, Session) SLATE_END_ARGS()
        void Construct(const FArguments& Args) { Session = Args._Session; }
        FVector2D ComputeDesiredSize(float) const override { return FVector2D(580, 380); }
        int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&, FSlateWindowElementList& Elements,
            int32 Layer, const FWidgetStyle& Style, bool) const override
        {
            const FVector2D Size = Geometry.GetLocalSize();
            if (Size.X < 120 || Size.Y < 180) { return Layer; }
            const auto Line = [&](const TArray<FVector2D>& Points, FLinearColor Color, float Width = 1)
            { FSlateDrawElement::MakeLines(Elements, Layer, Geometry.ToPaintGeometry(), Points,
                ESlateDrawEffect::None, Color * Style.GetColorAndOpacityTint(), true, Width); };
            const auto Label = [&](FVector2D Position, const FString& Value, int32 FontSize = 12, FLinearColor Color = Ink)
            { FSlateDrawElement::MakeText(Elements, Layer, Geometry.ToPaintGeometry(FVector2D(Size.X, 30), FSlateLayoutTransform(Position)),
                Value, FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), FontSize), ESlateDrawEffect::None, Color * Style.GetColorAndOpacityTint()); };
            const auto CenteredLabel = [&](FVector2D Position, const FString& Value, int32 FontSize = 16)
            {
                const auto Font = FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), FontSize);
                const FVector2D Extent = FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Value, Font);
                Label(Position - FVector2D(Extent.X * .5, 0), Value, FontSize);
            };
            const auto Box = [&](FVector2D Position, FVector2D Extent, FLinearColor Color)
            { FSlateDrawElement::MakeBox(Elements, Layer, Geometry.ToPaintGeometry(Extent, FSlateLayoutTransform(Position)),
                FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox")), ESlateDrawEffect::None, Color * Style.GetColorAndOpacityTint()); };
            const auto Dashed = [&](FVector2D A, FVector2D B, FLinearColor Color)
            {
                const FVector2D D = B - A; const double Length = D.Size();
                for (double T = 0; T < Length; T += 15)
                { Line({A + D * (T / Length), A + D * (FMath::Min(T + 8, Length) / Length)}, Color); }
            };
            const auto Arrow = [&](FVector2D A, FVector2D B, FLinearColor Color, bool bDouble = false)
            {
                const FVector2D D = (B - A).GetSafeNormal(), N(-D.Y, D.X);
                Line({A, B}, Color, 2);
                Line({B - D * 7 + N * 4, B, B - D * 7 - N * 4}, Color, 2);
                if (bDouble) { Line({A + D * 7 + N * 4, A, A + D * 7 - N * 4}, Color, 2); }
            };
            const auto Marker = [&](FVector2D P, bool bSelected)
            {
                TArray<FVector2D> Circle;
                for (int32 I = 0; I <= 24; ++I)
                { const double A = I * 2 * UE_PI / 24; Circle.Add(P + FVector2D(FMath::Cos(A), FMath::Sin(A)) * 7); }
                Line(Circle, bSelected ? Orange : Ink, 2);
                Box(P - FVector2D(2, 2), FVector2D(4, 4), bSelected ? Orange : Ink);
            };
            const auto Deck = [&](FVector2D A, FVector2D B, float Width = 12)
            { Line({A, B}, Ink, Width + 3); Line({A, B}, Mist, Width); };
            Box(FVector2D::ZeroVector, Size, White);
            for (float X = 0; X < Size.X; X += 20) { Line({FVector2D(X, 0), FVector2D(X, Size.Y)}, FLinearColor(Muted.R, Muted.G, Muted.B, .13f)); }
            for (float Y = 0; Y < Size.Y; Y += 20) { Line({FVector2D(0, Y), FVector2D(Size.X, Y)}, FLinearColor(Muted.R, Muted.G, Muted.B, .13f)); }
            ++Layer;
            const UParametricRoadSession* State = Session.Get();
            const int32 Selected = State ? State->GetAdjustmentIndex() : 0;
            const auto Adjustment = EParametricRoadAdjustment(Selected);
            const double Value = State ? State->GetDiagramValue(Selected) : 0;
            Label(FVector2D(16, 12), UParametricRoadDiagramWidget::GetDiagramCaption(Adjustment).ToString(), 14);
            const FVector2D Origin(34, 62), Plot(Size.X - 68, Size.Y - 126);
            const auto P = [&](double X, double Y) { return Origin + FVector2D(Plot.X * X, Plot.Y * Y); };
            const FString Readout = State ? State->GetSelectedAdjustmentValue(Selected).ToString() : TEXT("0.0");
            const FString Point = State ? State->GetSelectedPointLabel().ToString() : TEXT("START");
            if (Selected == 0)
            {
                const int32 Count = State ? FMath::Max(2, State->GetPointCount()) : 2;
                const int32 Index = State ? State->GetSelectedPointIndex() : 0;
                const int32 First = FMath::Clamp(Index, 0, Count - 2), Last = First + 1;
                const double StartYaw = State ? State->GetPointPose(First).Rotator().Yaw : 0;
                const double EndYaw = State ? State->GetPointPose(Last).Rotator().Yaw : 0;
                const auto Shape = UParametricRoadDiagramWidget::MakeHeadingDiagram(Plot, StartYaw, EndYaw);
                const FVector2D A = Origin + Shape.Start, B = Origin + Shape.StartControl;
                const FVector2D C = Origin + Shape.EndControl, D = Origin + Shape.End;
                FSlateDrawElement::MakeCubicBezierSpline(Elements, Layer, Geometry.ToPaintGeometry(), A, B, C, D, 18,
                    ESlateDrawEffect::None, Ink * Style.GetColorAndOpacityTint());
                FSlateDrawElement::MakeCubicBezierSpline(Elements, Layer, Geometry.ToPaintGeometry(), A, B, C, D, 14,
                    ESlateDrawEffect::None, Mist * Style.GetColorAndOpacityTint());
                ++Layer;
                TArray<FVector2D> Road;
                for (int32 I = 0; I <= 64; ++I)
                { const double T = I / 64.0, U = 1 - T; Road.Add(A * (U * U * U) + B * (3 * U * U * T) + C * (3 * U * T * T) + D * (T * T * T)); }
                for (int32 I = 2; I + 3 < 63; I += 7) { Line({Road[I], Road[I + 3]}, Ink); }
                for (int32 Side = 0; Side < 2; ++Side)
                {
                    const FVector2D Anchor = Side ? D : A, Direction = Side ? Shape.EndDirection : Shape.StartDirection;
                    const FVector2D Across(-Direction.Y, Direction.X);
                    Line({Anchor - Across * 10, Anchor + Across * 10}, Ink, 2);
                    Arrow(Anchor, Anchor + Direction * 44, Index == (Side ? Last : First) ? Orange : Ink);
                    Marker(Anchor, Index == (Side ? Last : First));
                    Label(Anchor + FVector2D(-18, -32), State ? State->GetPointLabel(Side ? Last : First).ToString() : (Side ? TEXT("END") : TEXT("START")), 12);
                }
                Label(P(.23, .86), FString::Printf(TEXT("Relative bend: %.1f°"), FMath::FindDeltaAngleDegrees(StartYaw, EndYaw)), 13, Muted);
            }
            else if (Selected == 1 || Selected == 2)
            {
                // Along-travel cross-section: positive UE roll lowers road-right.
                const float Angle = FMath::DegreesToRadians(float(Selected == 2 ? -Value : Value));
                const float Radius = UParametricRoadDiagramWidget::FitSectionRadius(Plot, Angle);
                const FVector2D Center = P(.5, .5), D(FMath::Cos(Angle), -FMath::Sin(Angle)), N(-D.Y, D.X);
                const FVector2D A = Center - D * Radius, B = Center + D * Radius;
                Dashed(P(.06, .5), P(.94, .5), Muted);
                Deck(A, B);
                Line({A - N * 10, A + N * 10}, Ink, 2); Line({B - N * 10, B + N * 10}, Ink, 2);
                Marker(Center, true);
                Label(A + FVector2D(-8, -32), Selected == 2 ? TEXT("Road left") : TEXT("Back"), 12);
                Label(B + FVector2D(-40, -32), Selected == 2 ? TEXT("Road right") : TEXT("Travel"), 12);
                const double ArcRadius = FMath::Min(56.0, Plot.Y * .3);
                TArray<FVector2D> Arc;
                for (int32 I = 0; I <= 24; ++I)
                { const double R = Angle * I / 24; Arc.Add(Center + FVector2D(FMath::Cos(R), -FMath::Sin(R)) * ArcRadius); }
                if (FMath::Abs(Angle) > .001f) { Line(Arc, Orange, 2); }
                // A dedicated annotation row stays clear of the deck at both
                // positive and negative grade/bank, including the angle limits.
                CenteredLabel(P(.5, .94), Readout);
                Label(Center + FVector2D(-18, -31), Point, 12);
            }
            else if (Selected == 3)
            {
                const double ReferenceY = .58;
                const double Shift = FMath::Clamp(Value / FMath::Max(5.0, FMath::Abs(Value)), -1.0, 1.0) * .30;
                const FVector2D A = P(.1, ReferenceY - Shift), B = P(.9, ReferenceY - Shift);
                Dashed(P(.1, ReferenceY), P(.9, ReferenceY), Muted);
                Deck(A, B); Marker((A + B) * .5, true);
                Label(P(.1, ReferenceY) + FVector2D(0, 14), TEXT("Captured point level"), 12, Muted);
                if (FMath::Abs(Shift) > .001)
                { Arrow(P(.65, ReferenceY), P(.65, ReferenceY - Shift), Orange, true); }
                Label((A + B) * .5 + FVector2D(-10, -32), Point, 12);
                Label(P(.68, ReferenceY - Shift * .5) + FVector2D(0, -8), Readout, 16);
            }
            else if (Selected == 4)
            {
                const double Handle = FMath::Clamp(Value, .1, 4.0) * .075 + .05;
                const FVector2D A = P(.1, .7), B = P(.1 + Handle, .7), C = P(.9 - Handle, .3), D = P(.9, .3);
                FSlateDrawElement::MakeCubicBezierSpline(Elements, Layer, Geometry.ToPaintGeometry(), A, B, C, D, 14, ESlateDrawEffect::None, Mist * Style.GetColorAndOpacityTint());
                Line({A, B}, Orange, 2); Line({C, D}, Orange, 2);
                Marker(A, false); Marker(D, true); Box(B - FVector2D(3, 3), FVector2D(6, 6), Orange); Box(C - FVector2D(3, 3), FVector2D(6, 6), Orange);
                Label(P(.12, .83), TEXT("Tangent handles"), 12); Label(P(.5, .1), Readout, 16);
            }
            else if (Selected == 5 || Selected == 6)
            {
                Box(P(.07, .35), FVector2D(Plot.X * .48, Plot.Y * .25), Mist);
                Box(P(.45, .35), FVector2D(Plot.X * .48, Plot.Y * .25), Cream);
                Line({P(.07, .35), P(.55, .35), P(.55, .6), P(.07, .6), P(.07, .35)}, Ink);
                Line({P(.45, .35), P(.93, .35), P(.93, .6), P(.45, .6), P(.45, .35)}, Ink);
                if (Selected == 5) { Box(P(.45, .35), FVector2D(Plot.X * .1, Plot.Y * .25), Orange); }
                const double From = Selected == 5 ? .45 : .07, To = .55;
                Arrow(P(From, .76), P(To, .76), Orange, true); CenteredLabel(P((From + To) * .5, .83), Readout);
                Label(P(.12, .19), TEXT("Slice A"), 12); Label(P(.68, .19), TEXT("Slice B"), 12);
            }
            else if (Selected == 7 || Selected == 8 || Selected == 10)
            {
                const FVector2D A = P(.1, .42), B = P(.86, .42);
                if (Selected != 10) { Deck(A, B, Selected == 8 ? 38 : 18); }
                if (Selected == 7) { Arrow(P(.1, .72), P(.86, .72), Orange, true); Label(P(.35, .79), Readout, 16); }
                else if (Selected == 8)
                { Arrow(B + FVector2D(18, -19), B + FVector2D(18, 19), Orange, true); Label(P(.53, .7), Readout, 16); }
                else
                {
                    const double Reference = .62, DeckY = Reference - FMath::Clamp(Value / FMath::Max(5.0, FMath::Abs(Value)), -1.0, 1.0) * .3;
                    Dashed(P(.1, Reference), P(.86, Reference), Muted); Deck(P(.1, DeckY), P(.86, DeckY), 12);
                    if (FMath::Abs(Value) > .0001) { Arrow(P(.65, Reference), P(.65, DeckY), Orange, true); }
                    Marker(P(.2, Reference), false);
                    Label(P(.12, Reference) + FVector2D(0, 14), TEXT("Blueprint origin"), 12, Muted);
                    // Zero and small offsets share the deck/reference line;
                    // place their number below both rather than on that line.
                    const double ReadoutY = FMath::Min(P(.68, FMath::Max(Reference, DeckY)).Y + 16, Size.Y - 68);
                    Label(FVector2D(P(.68, .5).X, ReadoutY), Readout, 16);
                }
                Label(P(.1, .19), Selected == 8 ? TEXT("Slice section") : TEXT("Deck surface"), 12);
            }
            else if (Selected == 9)
            {
                Deck(P(.1, .58), P(.5, .58)); Deck(P(.5, .36), P(.9, .36));
                Dashed(P(.5, .58), P(.9, .58), Muted); Arrow(P(.62, .58), P(.62, .36), Orange, true);
                Label(P(.65, .44), Readout, 16); Label(P(.11, .76), TEXT("Maximum joint mismatch"), 12);
            }
            else
            {
                Line({P(.2, .2), P(.8, .2), P(.8, .8), P(.2, .8), P(.2, .2)}, Muted, 2);
                const bool bY = Readout == TEXT("Y");
                Arrow(P(.3, .68), P(.73, .68), bY ? Ink : Orange); Arrow(P(.3, .68), P(.3, .27), bY ? Orange : Ink);
                Label(P(.75, .64), TEXT("X"), 16); Label(P(.27, .16), TEXT("Y"), 16);
                Label(P(.42, .36), TEXT("Blueprint"), 14); Label(P(.42, .48), TEXT("travel axis: ") + Readout, 13);
            }
            ++Layer;
            return Layer;
        }
    private:
        TWeakObjectPtr<UParametricRoadSession> Session;
    };
}

TSharedRef<SWidget> UParametricRoadDiagramWidget::RebuildWidget()
{
    return SNew(SRoadDiagram).Session(Session);
}

FText UParametricRoadDiagramWidget::GetDiagramCaption(EParametricRoadAdjustment Adjustment)
{
    static const TCHAR* Captions[] = {
        TEXT("Top view / Relative heading"), TEXT("Side view / Grade"), TEXT("Cross-section / Bank"), TEXT("Side view / Height offset"),
        TEXT("Top view / Tangent tension"), TEXT("Top view / Slice overlap"), TEXT("Top view / Slice length"), TEXT("Cross-section / Width"),
        TEXT("Cross-section / Thickness"), TEXT("Side view / Edge step"), TEXT("Cross-section / Deck reference"), TEXT("Top view / Forward axis")};
    return FText::FromString(Captions[FMath::Clamp(int32(Adjustment), 0, int32(UE_ARRAY_COUNT(Captions)) - 1)]);
}

FText UParametricRoadDiagramWidget::GetDiagramExplanation(EParametricRoadAdjustment Adjustment)
{
    static const TCHAR* Explanations[] = {
        TEXT("Adjacent point tangents define the bend. This is not a world compass."),
        TEXT("Grade tilts the road along travel. Positive values rise forward."),
        TEXT("Looking along travel. Positive bank lowers the road's right edge."),
        TEXT("Height shifts this point above or below its captured level."),
        TEXT("Tension changes tangent handle length and curve shape. Not to scale."),
        TEXT("Overlap is shared cover between successive road slices. Not to scale."),
        TEXT("Slice length is the blueprint's travel extent. Not to scale."),
        TEXT("Width is the road's full lateral span. Not to scale."),
        TEXT("Thickness is the blueprint's vertical extent. Not to scale."),
        TEXT("Edge step limits vertical mismatch at slice joins. Not to scale."),
        TEXT("Deck plane locates the driving surface from the blueprint origin."),
        TEXT("Choose the blueprint's local X or Y axis for road travel.")};
    return FText::FromString(Explanations[FMath::Clamp(int32(Adjustment), 0, int32(UE_ARRAY_COUNT(Explanations)) - 1)]);
}

float UParametricRoadDiagramWidget::FitSectionRadius(const FVector2D& Size, float AngleRadians)
{
    // Reserve room for the shifted deck thickness and the endpoint markers.
    constexpr float Margin = 18.0f;
    const float HalfWidth = FMath::Max(0.0f, float(Size.X * 0.5) - Margin);
    const float HalfHeight = FMath::Max(0.0f, float(Size.Y * 0.44) - Margin);
    const float Sine = FMath::Abs(FMath::Sin(AngleRadians));
    const float Cosine = FMath::Abs(FMath::Cos(AngleRadians));
    float Radius = FMath::Max(0.0f, float(Size.X * 0.34));
    if (Cosine > UE_SMALL_NUMBER) { Radius = FMath::Min(Radius, HalfWidth / Cosine); }
    if (Sine > UE_SMALL_NUMBER) { Radius = FMath::Min(Radius, HalfHeight / Sine); }
    return Radius;
}

FParametricRoadHeadingDiagram UParametricRoadDiagramWidget::MakeHeadingDiagram(const FVector2D& Size,
    double StartDegrees, double EndDegrees)
{
    FParametricRoadHeadingDiagram Result;
    Result.Start = FVector2D(Size.X * 0.18, Size.Y * 0.5);
    Result.End = FVector2D(Size.X * 0.82, Size.Y * 0.5);
    const auto Direction = [](double Degrees)
    {
        const double Radians = FMath::DegreesToRadians(FMath::IsFinite(Degrees) ? FRotator::NormalizeAxis(Degrees) : 0.0);
        return FVector2D(FMath::Cos(Radians), -FMath::Sin(Radians));
    };
    // Rotate both headings around their common circular midpoint. Equal world
    // headings always depict a straight road, even at the +/-180 degree seam.
    // The numeric parameter cards keep their absolute world yaw unchanged.
    const double Delta = FMath::IsFinite(StartDegrees) && FMath::IsFinite(EndDegrees)
        ? FMath::FindDeltaAngleDegrees(StartDegrees, EndDegrees) : 0.0;
    Result.StartDirection = Direction(-Delta * 0.5);
    Result.EndDirection = Direction(Delta * 0.5);
    // Keep both anchors fixed while rotating a heading. The control points fit
    // inside the frame, so the entire Bezier stays inside their convex hull.
    const auto Control = [&Size](FVector2D Anchor, FVector2D Tangent)
    {
        double Length = FMath::Min(Size.X * 0.22, Size.Y * 0.85);
        constexpr double Margin = 18;
        for (int32 Axis = 0; Axis < 2; ++Axis)
        {
            if (Tangent[Axis] > UE_SMALL_NUMBER) { Length = FMath::Min(Length, (Size[Axis] - Margin - Anchor[Axis]) / Tangent[Axis]); }
            else if (Tangent[Axis] < -UE_SMALL_NUMBER) { Length = FMath::Min(Length, (Margin - Anchor[Axis]) / Tangent[Axis]); }
        }
        return Anchor + Tangent * FMath::Max(0.0, Length);
    };
    Result.StartControl = Control(Result.Start, Result.StartDirection);
    Result.EndControl = Control(Result.End, -Result.EndDirection);
    return Result;
}

void UParametricRoadStatusWidget::SetSession(UParametricRoadSession* InSession)
{
    Session = InSession;
    LastPresentation.Reset();
    if (Worksheet) { Worksheet->SetSession(InSession); }
}

TSharedRef<SWidget> UParametricRoadStatusWidget::RebuildWidget()
{
    if (!WidgetTree) { WidgetTree = NewObject<UWidgetTree>(this); }
    Worksheet = WidgetTree->ConstructWidget<UParametricRoadLayerWidget>();
    Worksheet->Initialize();
    Worksheet->SetPassive(true);
    Worksheet->SetSession(Session.Get());
    WidgetTree->RootWidget = Worksheet;
    SetIsFocusable(false);
    SetVisibility(ESlateVisibility::HitTestInvisible);
    LastPresentation.Reset();
    TSharedRef<SWidget> Result = Super::RebuildWidget();
    RefreshDisplay();
    return Result;
}

bool UParametricRoadStatusWidget::RefreshDisplay()
{
    const auto* State = Session.Get();
    if (!State || !Worksheet) { return false; }
    // The component redraws only when visible information changes. Full-precision
    // diagram inputs are included because neighbouring point tangents may change
    // without changing the selected numeric readout.
    FString Presentation = FString::Printf(TEXT("%d|%d|%d|%d|%d"), State->GetAdjustmentIndex(),
        State->GetSelectedPointIndex(), State->GetPointCount(), State->UsesFineAdjustment(), State->IsBuilding());
    for (const FText& Text : {State->GetPhaseText(), State->GetRouteSummary(), State->GetPanelStatus(), State->GetCostText()})
    { Presentation += TEXT("|") + Text.ToString(); }
    Presentation += TEXT("|") + State->GetRoadBlueprintName();
    for (int32 I = 0; I < int32(EParametricRoadAdjustment::Count); ++I)
    {
        Presentation += TEXT("|") + State->GetSelectedAdjustmentValue(I).ToString();
        Presentation += FString::Printf(TEXT("|%.17g|%d"), State->GetDiagramValue(I), State->IsSelectedPointAutomatic(I));
    }
    for (int32 I = 0; I < State->GetPointCount(); ++I)
    { Presentation += TEXT("|") + State->GetPointLabel(I).ToString() + TEXT("|") + State->GetPointPose(I).ToString()
        + FString::Printf(TEXT("|%.17g"), State->GetPointPose(I).Rotator().Yaw); }
    if (Presentation == LastPresentation) { return false; }
    LastPresentation = MoveTemp(Presentation);
    Worksheet->RefreshDisplay();
    ++DisplayRevision;
    return true;
}

void UParametricRoadStatusWidget::NativeTick(const FGeometry& Geometry, float DeltaSeconds)
{
    Super::NativeTick(Geometry, DeltaSeconds);
    RefreshSeconds -= DeltaSeconds;
    if (RefreshSeconds <= 0) { RefreshSeconds = 0.1; RefreshDisplay(); }
}
