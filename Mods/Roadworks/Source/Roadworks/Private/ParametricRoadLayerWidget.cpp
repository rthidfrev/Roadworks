#include "ParametricRoadLayerWidget.h"

#include "ParametricRoadSession.h"
#include "ParametricRoadWidgets.h"
#include "ParametricRoadUIStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/CheckBox.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScaleBox.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetSwitcher.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Paths.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SLeafWidget.h"
#include "Brushes/SlateRoundedBoxBrush.h"

namespace
{
    using namespace ParametricRoadUI;
    enum EWorksheetCommand { CloseTerminal, Page, Point = 3, Adjustment, Decrement, Increment,
        AxisToggle, AddLayer, RemoveLayer, Layer, Browse, PreviewBlueprint, UseBlueprint, Category,
        ClearSearch, PresetSelect, PresetSaveNew, PresetUpdate, PresetLoad, PresetConfirmLoad,
        PresetCancelLoad };
    constexpr int32 SpacingField = 12, FirstField = 13, ForwardField = 14, RightField = 15, UpField = 16;
    UTextBlock* WorksheetLabel(UWidgetTree* T, const FString& Value, int32 Size = 13, bool Bold = false, FLinearColor Color = Ink)
    {
        auto* W = T->ConstructWidget<UTextBlock>(); W->SetText(FText::FromString(Value));
        W->SetColorAndOpacity(FSlateColor(Color));
        W->SetFont(FCoreStyle::GetDefaultFontStyle(Bold ? TEXT("Bold") : TEXT("Regular"), Size));
        W->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis); return W;
    }
    UBorder* WorksheetPanel(UWidgetTree* T, FLinearColor Color, FMargin Padding = FMargin(0))
    { auto* B = T->ConstructWidget<UBorder>(); B->SetBrushColor(Color); B->SetPadding(Padding); return B; }
    USizeBox* WorksheetSize(UWidgetTree* T, UWidget* Child, float Width = 0, float Height = 0)
    {
        auto* B = T->ConstructWidget<USizeBox>(); if (Width > 0) B->SetWidthOverride(Width);
        if (Height > 0) B->SetHeightOverride(Height); B->AddChild(Child); return B;
    }
    void WorksheetButtonStyle(UButton* B, bool Selected = false)
    {
        FButtonStyle S = FCoreStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Button"));
        S.Normal = FSlateRoundedBoxBrush(White, 0.f, Muted, .75f);
        S.Normal.TintColor = FSlateColor(Selected ? Orange : Paper);
        S.Hovered = S.Normal; S.Hovered.TintColor = FSlateColor(Selected ? Orange : White);
        S.Pressed = S.Normal; S.Pressed.TintColor = FSlateColor(Mist);
        S.Disabled = S.Normal; S.Disabled.TintColor = FSlateColor(Cream);
        S.NormalPadding = S.PressedPadding = FMargin(6, 3);
        S.NormalForeground = S.HoveredForeground = S.PressedForeground = FSlateColor(Ink);
        S.DisabledForeground = FSlateColor(Muted); B->SetStyle(S);
    }
    void WorksheetCloseStyle(UButton* B)
    {
        FButtonStyle Style = FCoreStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Button"));
        Style.Normal = FSlateRoundedBoxBrush(Paper, 0.f, Muted, 1.f);
        Style.Hovered = FSlateRoundedBoxBrush(White, 0.f, Ink, 1.f);
        Style.Pressed = FSlateRoundedBoxBrush(Mist, 0.f, Ink, 1.f);
        Style.Disabled = FSlateRoundedBoxBrush(Cream, 0.f, Muted, 1.f);
        Style.NormalPadding = FMargin(7);
        Style.PressedPadding = FMargin(8, 8, 6, 6);
        Style.NormalForeground = Style.HoveredForeground = Style.PressedForeground = FSlateColor(Ink);
        Style.DisabledForeground = FSlateColor(Muted); B->SetStyle(Style);
    }
    void WorksheetTreeButtonStyle(UButton* B, bool Selected = false)
    {
        FButtonStyle Style;
        Style.Normal = FSlateRoundedBoxBrush(Selected ? Orange : Paper, 0.f);
        Style.Hovered = FSlateRoundedBoxBrush(Selected ? Orange : Mist, 0.f);
        Style.Pressed = FSlateRoundedBoxBrush(Cream, 0.f);
        Style.Disabled = Style.Normal;
        Style.NormalPadding = Style.PressedPadding = FMargin(5, 3);
        Style.NormalForeground = Style.HoveredForeground = Style.PressedForeground = FSlateColor(Ink);
        Style.DisabledForeground = FSlateColor(Muted); B->SetStyle(Style);
    }
    UBorder* WorksheetSection(UWidgetTree* T, UTextBlock* Heading, UWidget* Child)
    {
        auto* Frame = WorksheetPanel(T, Muted, FMargin(1)); auto* Body = T->ConstructWidget<UVerticalBox>(); Frame->AddChild(Body);
        auto* Header = WorksheetPanel(T, Cream, FMargin(8, 5)); Header->AddChild(Heading);
        Body->AddChildToVerticalBox(WorksheetSize(T, Header, 0, 30));
        auto* Content = WorksheetPanel(T, Paper, FMargin(7)); Content->AddChild(Child);
        Body->AddChildToVerticalBox(Content)->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); return Frame;
    }
    UCheckBox* WorksheetCheck(UWidgetTree* T, const FString& Text)
    {
        auto* C = T->ConstructWidget<UCheckBox>();
        FCheckBoxStyle Style; Style.CheckBoxType = ESlateCheckBoxType::ToggleButton;
        Style.UncheckedImage = Style.UncheckedHoveredImage = Style.UncheckedPressedImage = FSlateNoResource();
        Style.CheckedImage = Style.CheckedHoveredImage = Style.CheckedPressedImage = FSlateNoResource();
        Style.UndeterminedImage = Style.UndeterminedHoveredImage = Style.UndeterminedPressedImage = FSlateNoResource();
        Style.ForegroundColor = Style.HoveredForeground = Style.PressedForeground = FSlateColor(Ink);
        Style.CheckedForeground = Style.CheckedHoveredForeground = Style.CheckedPressedForeground = FSlateColor(Ink);
        C->SetWidgetStyle(Style);
        auto* Row = T->ConstructWidget<UHorizontalBox>(); C->AddChild(Row);
        auto* Mark = T->ConstructWidget<UParametricRoadCheckMark>(); Mark->SetTarget(C);
        Mark->SetVisibility(ESlateVisibility::HitTestInvisible);
        Row->AddChildToHorizontalBox(WorksheetSize(T, Mark, 18, 18))->SetVerticalAlignment(VAlign_Center);
        auto* CaptionSlot = Row->AddChildToHorizontalBox(WorksheetLabel(T, Text)); CaptionSlot->SetPadding(FMargin(6, 0)); CaptionSlot->SetVerticalAlignment(VAlign_Center);
        return C;
    }
    void WorksheetFill(UHorizontalBox* Row, UWidget* Child, float Weight = 1, FMargin Padding = FMargin(0))
    { auto* NativeSlot = Row->AddChildToHorizontalBox(Child); FSlateChildSize FillSize(ESlateSizeRule::Fill); FillSize.Value = Weight; NativeSlot->SetSize(FillSize); NativeSlot->SetPadding(Padding); }
    void WorksheetFill(UVerticalBox* Column, UWidget* Child, float Weight = 1, FMargin Padding = FMargin(0))
    { auto* NativeSlot = Column->AddChildToVerticalBox(Child); FSlateChildSize FillSize(ESlateSizeRule::Fill); FillSize.Value = Weight; NativeSlot->SetSize(FillSize); NativeSlot->SetPadding(Padding); }

    class SWorksheetCheckMark : public SLeafWidget
    {
    public:
        SLATE_BEGIN_ARGS(SWorksheetCheckMark) {} SLATE_ARGUMENT(TWeakObjectPtr<UCheckBox>, Check) SLATE_END_ARGS()
        void Construct(const FArguments& Args) { Check = Args._Check; }
        FVector2D ComputeDesiredSize(float) const override { return FVector2D(18, 18); }
        int32 OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& E,
            int32 L, const FWidgetStyle& Style, bool bEnabled) const override
        {
            const FLinearColor Color = (bEnabled ? Ink : Muted) * Style.GetColorAndOpacityTint();
            FSlateDrawElement::MakeBox(E, L, G.ToPaintGeometry(), FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox")),
                ESlateDrawEffect::None, White * Style.GetColorAndOpacityTint());
            FSlateDrawElement::MakeLines(E, L + 1, G.ToPaintGeometry(), TArray<FVector2D>{{1,1},{17,1},{17,17},{1,17},{1,1}}, ESlateDrawEffect::None, Color, true, 1);
            if (Check.IsValid() && Check->IsChecked())
                FSlateDrawElement::MakeLines(E, L + 2, G.ToPaintGeometry(), TArray<FVector2D>{{4,9},{8,13},{14,5}}, ESlateDrawEffect::None, Color, true, 2);
            return L + 2;
        }
    private: TWeakObjectPtr<UCheckBox> Check;
    };

    class SWorksheetCloseMark : public SLeafWidget
    {
    public:
        SLATE_BEGIN_ARGS(SWorksheetCloseMark) {} SLATE_END_ARGS()
        void Construct(const FArguments&) {}
        FVector2D ComputeDesiredSize(float) const override { return FVector2D(14, 14); }
        int32 OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& E,
            int32 L, const FWidgetStyle& Style, bool bEnabled) const override
        {
            const FVector2D Center = G.GetLocalSize() * .5;
            const FLinearColor Color = (bEnabled ? Ink : Muted) * Style.GetColorAndOpacityTint();
            FSlateDrawElement::MakeLines(E, L, G.ToPaintGeometry(),
                TArray<FVector2D>{Center + FVector2D(-5,-5), Center + FVector2D(5,5)},
                ESlateDrawEffect::None, Color, true, 2.f);
            FSlateDrawElement::MakeLines(E, L, G.ToPaintGeometry(),
                TArray<FVector2D>{Center + FVector2D(-5,5), Center + FVector2D(5,-5)},
                ESlateDrawEffect::None, Color, true, 2.f);
            return L;
        }
    };

    class SWorksheetTreeGuides : public SLeafWidget
    {
    public:
        SLATE_BEGIN_ARGS(SWorksheetTreeGuides) {} SLATE_ARGUMENT(int32, Rows) SLATE_ARGUMENT(bool, Header) SLATE_END_ARGS()
        void Construct(const FArguments& Args) { Rows = Args._Rows; bHeader = Args._Header; }
        FVector2D ComputeDesiredSize(float) const override { return FVector2D(26, Rows * 31); }
        int32 OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& E,
            int32 L, const FWidgetStyle& Style, bool) const override
        {
            const int32 First = bHeader ? 1 : 0;
            if (Rows <= First) return L;
            const FLinearColor Color = Muted * Style.GetColorAndOpacityTint();
            const float X = 12.f, Bottom = (Rows - .5f) * 31.f;
            FSlateDrawElement::MakeLines(E, L, G.ToPaintGeometry(), TArray<FVector2D>{{X, bHeader ? 31.f : 0.f},{X, Bottom}}, ESlateDrawEffect::None, Color, false, 1.f);
            for (int32 I = First; I < Rows; ++I)
            {
                const float Y = (I + .5f) * 31.f;
                FSlateDrawElement::MakeLines(E, L, G.ToPaintGeometry(), TArray<FVector2D>{{X,Y},{25,Y}}, ESlateDrawEffect::None, Color, false, 1.f);
            }
            return L;
        }
    private: int32 Rows = 0; bool bHeader = false;
    };

    class SLayerStations : public SLeafWidget
    {
    public:
        SLATE_BEGIN_ARGS(SLayerStations) {} SLATE_ARGUMENT(TWeakObjectPtr<UParametricRoadSession>, Session) SLATE_END_ARGS()
        void Construct(const FArguments& Args) { Session = Args._Session; }
        FVector2D ComputeDesiredSize(float) const override { return FVector2D(400, 220); }
        int32 OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& E,
            int32 L, const FWidgetStyle& Style, bool) const override
        {
            const FVector2D S = G.GetLocalSize();
            const auto Line = [&](TArray<FVector2D> P, FLinearColor C, float W = 1)
            { FSlateDrawElement::MakeLines(E, L, G.ToPaintGeometry(), P, ESlateDrawEffect::None, C * Style.GetColorAndOpacityTint(), true, W); };
            const auto Text = [&](FVector2D P, const FString& V, FLinearColor C)
            { FSlateDrawElement::MakeText(E, L, G.ToPaintGeometry(FVector2D(150, 24), FSlateLayoutTransform(P)), V,
                FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 12), ESlateDrawEffect::None, C * Style.GetColorAndOpacityTint()); };
            for (float X = 0; X < S.X; X += 20) Line({{X, 0}, {X, S.Y}}, Mist);
            for (float Y = 0; Y < S.Y; Y += 20) Line({{0, Y}, {S.X, Y}}, Mist);
            ++L;
            const auto Settings = Session.IsValid() ? Session->GetLayerSettings() : FParametricRoadLayerSettings();
            const double Spacing = FMath::Max(.1, Settings.SpacingMetres), First = FMath::Max(0.0, Settings.FirstPlacementMetres);
            const double Left = 28, Right = S.X - 28, Y = S.Y * .58, Range = First + 3 * Spacing;
            const auto XAt = [&](double D) { return Left + (Right - Left) * D / Range; };
            Line({{Left, Y}, {Right, Y}}, Ink, 12); Line({{Left, Y}, {Right, Y}}, Cream, 9);
            Line({{Left, Y - 20}, {Left, Y + 12}}, Ink, 2); Text({Left, Y + 20}, TEXT("Start"), Ink);
            for (int32 I = 0; I < 4; ++I)
            {
                const float X = XAt(First + I * Spacing);
                Line({{X, Y - 28}, {X, Y - 2}}, Ink, 11); Line({{X, Y - 27}, {X, Y - 2}}, Cream, 8);
                Line({{X, Y - 50}, {X, Y - 32}}, Muted);
            }
            ++L;
            const float A = XAt(First), B = XAt(First + Spacing), Measure = Y - 48;
            Line({{A, Measure}, {B, Measure}}, Orange, 2);
            for (const float X : {A, B}) Line({{X, Measure - 5}, {X, Measure + 5}}, Orange, 2);
            Text({(A + B) * .5 - 24, Measure - 23}, FString::Printf(TEXT("%.1f m"), Spacing), Ink);
            Text({Left, 14}, FString::Printf(TEXT("First placement: %.1f m"), First), Muted);
            return L;
        }
    private: TWeakObjectPtr<UParametricRoadSession> Session;
    };
}

void UParametricRoadWorksheetButton::SetTarget(UParametricRoadLayerWidget* W, int32 C, int32 I, const FString& K)
{ Target = W; Action = C; Argument = I; Identity = K; OnClicked.AddUniqueDynamic(this, &ThisClass::Activate); }
void UParametricRoadWorksheetButton::Activate()
{ if (Target.IsValid()) Target->Activate(Action, Argument, Identity); }
void UParametricRoadNumberBox::SetTarget(UParametricRoadLayerWidget* W, int32 I)
{ Target = W; Field = I; OnValueCommitted.AddUniqueDynamic(this, &ThisClass::Commit); }
void UParametricRoadNumberBox::Commit(float V, ETextCommit::Type)
{ if (Target.IsValid()) Target->CommitNumber(Field, V); }
void UParametricRoadCheckMark::SetTarget(UCheckBox* InCheck) { Check = InCheck; }
TSharedRef<SWidget> UParametricRoadCheckMark::RebuildWidget() { return SNew(SWorksheetCheckMark).Check(Check); }
TSharedRef<SWidget> UParametricRoadCloseMark::RebuildWidget()
{ return SNew(SWorksheetCloseMark); }
TSharedRef<SWidget> UParametricRoadTreeGuides::RebuildWidget()
{ return SNew(SWorksheetTreeGuides).Rows(Rows).Header(bHeader); }
TSharedRef<SWidget> UParametricRoadLayerDiagram::RebuildWidget() { return SNew(SLayerStations).Session(Session); }
UParametricRoadLayerWidget::UParametricRoadLayerWidget(const FObjectInitializer& O) : Super(O) { SetIsFocusable(true); }
void UParametricRoadLayerWidget::SetSession(UParametricRoadSession* S)
{
    if (Session.Get() != S)
    {
        Session = S; CatalogRevision = MAX_uint32; LastPointCount = LastSelectedPoint = INDEX_NONE; LastLayerSignature.Reset();
    }
    if (Diagram) Diagram->SetSession(S); if (Stations) Stations->SetSession(S); RefreshDisplay();
}
void UParametricRoadLayerWidget::SetPassive(bool InPassive)
{
    bPassive = InPassive; SetIsFocusable(!bPassive);
    SetVisibility(bPassive ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Visible);
}
UParametricRoadWorksheetButton* UParametricRoadLayerWidget::Button(const FString& Text, int32 C, int32 I, const FString& K)
{
    auto* B = WidgetTree->ConstructWidget<UParametricRoadWorksheetButton>(); B->SetTarget(this, C, I, K); WorksheetButtonStyle(B);
    auto* T = WorksheetLabel(WidgetTree, Text); B->AddChild(T); return B;
}
UWidget* UParametricRoadLayerWidget::Number(int32 Field, const FString& Text, const FString& Unit, float Min, float Max, float Step)
{
    auto* Card = WorksheetPanel(WidgetTree, Paper, FMargin(3)); auto* Body = WidgetTree->ConstructWidget<UVerticalBox>(); Card->AddChild(Body);
    if (Field < 12) Body->AddChildToVerticalBox(Button(Text + (Unit.IsEmpty() ? TEXT("") : TEXT(" (") + Unit + TEXT(")")), Adjustment, Field));
    else Body->AddChildToVerticalBox(WorksheetSize(WidgetTree, WorksheetLabel(WidgetTree, Text + TEXT(" (m)"), 12), 0, 22));
    auto* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
    auto* Spin = WidgetTree->ConstructWidget<UParametricRoadNumberBox>(); Spin->SetTarget(this, Field);
    Spin->SetEnableSlider(false); Spin->SetClearKeyboardFocusOnCommit(true); Spin->SetSelectAllTextOnCommit(true);
    Spin->SetMinValue(Min); Spin->SetMaxValue(Max); Spin->SetDelta(Step);
    Spin->SetMinFractionalDigits(Field == 4 || Field == 5 ? 2 : 1); Spin->SetMaxFractionalDigits(3);
    Spin->SetMinDesiredWidth(40); Spin->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 15)); Spin->SetForegroundColor(FSlateColor(Ink));
    FSpinBoxStyle Style = FCoreStyle::Get().GetWidgetStyle<FSpinBoxStyle>(TEXT("SpinBox"));
    Style.BackgroundBrush = *FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox")); Style.BackgroundBrush.TintColor = FSlateColor(White);
    Style.ActiveBackgroundBrush = Style.HoveredBackgroundBrush = Style.BackgroundBrush;
    Style.InactiveFillBrush = Style.ActiveFillBrush = Style.HoveredFillBrush = FSlateNoResource();
    Style.ForegroundColor = FSlateColor(Ink); Style.TextPadding = FMargin(5, 1); Style.InsetPadding = FMargin(1); Spin->SetWidgetStyle(Style);
    WorksheetFill(Row, Spin); Row->AddChildToHorizontalBox(WorksheetSize(WidgetTree, Button(TEXT("−"), Decrement, Field), 26));
    Row->AddChildToHorizontalBox(WorksheetSize(WidgetTree, Button(TEXT("+"), Increment, Field), 26));
    Body->AddChildToVerticalBox(WorksheetSize(WidgetTree, Row, 0, 30));
    Numbers[Field] = Spin; NumberPanels[Field] = Card; NumberSteps[Field] = Step;
    Card->SetToolTipText(FText::FromString(TEXT("Type a value and press Enter, or use − / + for a fixed step.")));
    return Card;
}
TSharedRef<SWidget> UParametricRoadLayerWidget::RebuildWidget()
{
    if (!WidgetTree) WidgetTree = NewObject<UWidgetTree>(this);
    Numbers.SetNum(17); NumberPanels.SetNum(17); NumberSteps.SetNum(17);
    auto* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(); auto* Fit = WidgetTree->ConstructWidget<UScaleBox>();
    Fit->SetStretch(EStretch::ScaleToFit); Surface = Fit;
    auto* NativeSlot = Canvas->AddChildToCanvas(Fit); NativeSlot->SetAnchors(FAnchors(0, 0, 1, 1)); NativeSlot->SetOffsets(FMargin(0));
    auto* Frame = WorksheetPanel(WidgetTree, Cream, FMargin(4)); Fit->AddChild(WorksheetSize(WidgetTree, Frame, 1024, 688));
    auto* Body = WidgetTree->ConstructWidget<UVerticalBox>(); Frame->AddChild(Body);
    auto* Header = WidgetTree->ConstructWidget<UHorizontalBox>(); Title = WorksheetLabel(WidgetTree, TEXT("Roadworks"), 19, true);
    auto* TitleSlot = Header->AddChildToHorizontalBox(Title); TitleSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); TitleSlot->SetVerticalAlignment(VAlign_Center); TitleSlot->SetPadding(FMargin(8, 0));
    HeaderChrome = WidgetTree->ConstructWidget<UHorizontalBox>(); HeaderChrome->SetVisibility(ESlateVisibility::HitTestInvisible);
    for (const TCHAR* Name : {TEXT("File"), TEXT("Edit"), TEXT("View"), TEXT("Tools"), TEXT("Help")})
    {
        auto* CaptionSlot = HeaderChrome->AddChildToHorizontalBox(WorksheetLabel(WidgetTree, Name, 12));
        CaptionSlot->SetVerticalAlignment(VAlign_Center); CaptionSlot->SetPadding(FMargin(9, 0));
    }
    auto* ChromeSlot = Header->AddChildToHorizontalBox(HeaderChrome); ChromeSlot->SetHorizontalAlignment(HAlign_Right); ChromeSlot->SetVerticalAlignment(VAlign_Center);
    CloseButton = Button(TEXT(""), CloseTerminal); CloseButton->ClearChildren(); WorksheetCloseStyle(CloseButton);
    auto* Close = WidgetTree->ConstructWidget<UParametricRoadCloseMark>(); Close->SetVisibility(ESlateVisibility::HitTestInvisible);
    auto* CloseSlot = CastChecked<UButtonSlot>(CloseButton->AddChild(Close)); CloseSlot->SetPadding(FMargin(0));
    CloseSlot->SetHorizontalAlignment(HAlign_Center); CloseSlot->SetVerticalAlignment(VAlign_Center);
    CloseButton->SetToolTipText(FText::FromString(TEXT("Close terminal")));
    auto* CloseHeaderSlot = Header->AddChildToHorizontalBox(WorksheetSize(WidgetTree, CloseButton, 28, 28));
    CloseHeaderSlot->SetPadding(FMargin(3, 0, 2, 0)); CloseHeaderSlot->SetVerticalAlignment(VAlign_Center);
    Body->AddChildToVerticalBox(WorksheetSize(WidgetTree, Header, 0, 40));
    Pages = WidgetTree->ConstructWidget<UWidgetSwitcher>(); Pages->AddChild(MakeRoutePage()); Pages->AddChild(MakeLayerPage());
    Pages->AddChild(MakeLibraryPage()); Pages->AddChild(MakePresetPage()); WorksheetFill(Body, Pages, 1, FMargin(0, 2));
    auto* FooterPanel = WorksheetPanel(WidgetTree, Paper, FMargin(8, 4)); Footer = WorksheetLabel(WidgetTree, TEXT(""), 11); FooterPanel->AddChild(Footer);
    Body->AddChildToVerticalBox(WorksheetSize(WidgetTree, FooterPanel, 0, 26)); WidgetTree->RootWidget = Canvas;
    CatalogRevision = MAX_uint32; LastPointCount = INDEX_NONE; LastLayerSignature.Reset(); RefreshDisplay(); SetPage(CurrentPage);
    return Super::RebuildWidget();
}
UWidget* UParametricRoadLayerWidget::MakeRoutePage()
{
    auto* Columns = WidgetTree->ConstructWidget<UHorizontalBox>();
    auto* Tree = WidgetTree->ConstructWidget<UVerticalBox>(); BlueprintName = WorksheetLabel(WidgetTree, TEXT(""), 13, true);
    auto* RouteHeader = WorksheetPanel(WidgetTree, Paper, FMargin(8, 0)); RouteHeader->AddChild(BlueprintName);
    Tree->AddChildToVerticalBox(WorksheetSize(WidgetTree, RouteHeader, 0, 34));
    PointTree = WidgetTree->ConstructWidget<UScrollBox>(); WorksheetFill(Tree, PointTree);
    Tree->AddChildToVerticalBox(WorksheetSize(WidgetTree, Button(TEXT("Layers…"), Page, 1), 0, 32));
    PresetNavigation = Button(TEXT("Presets…"), Page, 3);
    Tree->AddChildToVerticalBox(WorksheetSize(WidgetTree, PresetNavigation, 0, 32))->SetPadding(FMargin(0, 4, 0, 0));
    Columns->AddChildToHorizontalBox(WorksheetSize(WidgetTree, WorksheetSection(WidgetTree, WorksheetLabel(WidgetTree, TEXT("Route"), 15, true), Tree), 178))->SetPadding(FMargin(0, 0, 5, 0));
    auto* Center = WidgetTree->ConstructWidget<UVerticalBox>(); Phase = WorksheetLabel(WidgetTree, TEXT(""), 15, true);
    auto* Drawing = WidgetTree->ConstructWidget<UVerticalBox>();
    auto* PaperPanel = WorksheetPanel(WidgetTree, White); Diagram = WidgetTree->ConstructWidget<UParametricRoadDiagramWidget>(); Diagram->SetSession(Session.Get());
    PaperPanel->AddChild(Diagram); WorksheetFill(Drawing, PaperPanel);
    WorksheetFill(Center, WorksheetSection(WidgetTree, Phase, Drawing));
    auto* PointGrid = WidgetTree->ConstructWidget<UUniformGridPanel>(); PointGrid->SetSlotPadding(FMargin(2));
    const TCHAR* Labels[] = {TEXT("Heading"), TEXT("Grade"), TEXT("Bank"), TEXT("Height")};
    const float Min[] = {-180, -45, -60, -500}, Max[] = {180, 45, 60, 500};
    for (int32 N = 0; N < 4; ++N) { auto* Cell = PointGrid->AddChildToUniformGrid(Number(N, Labels[N], N < 3 ? TEXT("°") : TEXT("m"), Min[N], Max[N], N == 0 ? 5 : N == 3 ? .5 : 1), N % 2, N / 2); Cell->SetHorizontalAlignment(HAlign_Fill); Cell->SetVerticalAlignment(VAlign_Fill); }
    SelectedPoint = WorksheetLabel(WidgetTree, TEXT("Selected point"), 14, true);
    Center->AddChildToVerticalBox(WorksheetSize(WidgetTree, WorksheetSection(WidgetTree, SelectedPoint, PointGrid), 0, 172))->SetPadding(FMargin(0, 5, 0, 0));
    WorksheetFill(Columns, Center, 1, FMargin(0, 0, 5, 0));
    auto* Globals = WidgetTree->ConstructWidget<UVerticalBox>();
    const TCHAR* Names[] = {TEXT("Tension"), TEXT("Overlap"), TEXT("Slice length"), TEXT("Width"), TEXT("Thickness"), TEXT("Max edge step"), TEXT("Deck plane")};
    const float Mins[] = {.05f, 0, .01f, .01f, .001f, .01f, -500}, Maxs[] = {2, 1000, 1000, 1000, 1000, 100, 500};
    const float Steps[] = {.05f, .05f, .1f, .1f, .1f, .5f, .1f};
    for (int32 N = 0; N < 7; ++N) Globals->AddChildToVerticalBox(Number(N + 4, Names[N], N == 0 ? TEXT("") : N == 5 ? TEXT("cm") : TEXT("m"), Mins[N], Maxs[N], Steps[N]));
    Axis = Button(TEXT("Axis: X"), AxisToggle); Globals->AddChildToVerticalBox(WorksheetSize(WidgetTree, Axis, 0, 31))->SetPadding(FMargin(3, 5));
    Fine = WorksheetCheck(WidgetTree, TEXT("Fine step ×0.1")); Fine->OnCheckStateChanged.AddDynamic(this, &ThisClass::FineChanged); Globals->AddChildToVerticalBox(Fine);
    Columns->AddChildToHorizontalBox(WorksheetSize(WidgetTree, WorksheetSection(WidgetTree, WorksheetLabel(WidgetTree, TEXT("Whole route"), 15, true), Globals), 226));
    return Columns;
}
UWidget* UParametricRoadLayerWidget::MakeLayerPage()
{
    auto* Columns = WidgetTree->ConstructWidget<UHorizontalBox>(); auto* Tree = WidgetTree->ConstructWidget<UVerticalBox>();
    Tree->AddChildToVerticalBox(WorksheetSize(WidgetTree, WorksheetLabel(WidgetTree, TEXT("Decoration layers"), 12, true), 0, 30));
    LayerTree = WidgetTree->ConstructWidget<UScrollBox>(); WorksheetFill(Tree, LayerTree);
    auto* Actions = WidgetTree->ConstructWidget<UHorizontalBox>(); AddLayerButton = Button(TEXT("Add…"), AddLayer); RemoveLayerButton = Button(TEXT("Remove"), RemoveLayer);
    WorksheetFill(Actions, AddLayerButton, 1, FMargin(0, 0, 3, 0)); WorksheetFill(Actions, RemoveLayerButton);
    Tree->AddChildToVerticalBox(WorksheetSize(WidgetTree, Actions, 0, 32));
    Columns->AddChildToHorizontalBox(WorksheetSize(WidgetTree, WorksheetSection(WidgetTree, WorksheetLabel(WidgetTree, TEXT("Layers"), 15, true), Tree), 178))->SetPadding(FMargin(0, 0, 5, 0));
    auto* Center = WidgetTree->ConstructWidget<UVerticalBox>(); auto* Drawing = WorksheetPanel(WidgetTree, White);
    Stations = WidgetTree->ConstructWidget<UParametricRoadLayerDiagram>(); Stations->SetSession(Session.Get()); Drawing->AddChild(Stations);
    WorksheetFill(Center, WorksheetSection(WidgetTree, WorksheetLabel(WidgetTree, TEXT("Layer configuration"), 15, true), Drawing));
    auto* Controls = WidgetTree->ConstructWidget<UVerticalBox>(); LayerControls = Controls;
    Enabled = WorksheetCheck(WidgetTree, TEXT("Enabled")); Enabled->OnCheckStateChanged.AddDynamic(this, &ThisClass::EnabledChanged); Controls->AddChildToVerticalBox(WorksheetSize(WidgetTree, Enabled, 0, 30));
    auto* Distances = WidgetTree->ConstructWidget<UHorizontalBox>();
    WorksheetFill(Distances, Number(SpacingField, TEXT("Spacing"), TEXT("m"), .1, 1000, .5), 1, FMargin(0, 0, 6, 0));
    WorksheetFill(Distances, Number(FirstField, TEXT("First placement"), TEXT("m"), 0, 1000, .5)); Controls->AddChildToVerticalBox(Distances);
    LayerName = WorksheetLabel(WidgetTree, TEXT("Selected layer"), 14, true);
    Center->AddChildToVerticalBox(WorksheetSize(WidgetTree, WorksheetSection(WidgetTree, LayerName, Controls), 0, 146))->SetPadding(FMargin(0, 5, 0, 0));
    WorksheetFill(Columns, Center, 1, FMargin(0, 0, 5, 0));
    auto* Sidebar = WidgetTree->ConstructWidget<UVerticalBox>(); LayerSidebar = Sidebar;
    Sidebar->AddChildToVerticalBox(WorksheetSize(WidgetTree, WorksheetLabel(WidgetTree, TEXT("Blueprint"), 13, true), 0, 27));
    Sidebar->AddChildToVerticalBox(WorksheetSize(WidgetTree, Button(TEXT("Change…"), Browse), 0, 32));
    Sidebar->AddChildToVerticalBox(WorksheetSize(WidgetTree, WorksheetLabel(WidgetTree, TEXT("Position offset"), 14, true), 0, 42));
    Sidebar->AddChildToVerticalBox(Number(ForwardField, TEXT("Forward"), TEXT("m"), -1000, 1000, .1));
    Sidebar->AddChildToVerticalBox(Number(RightField, TEXT("Right"), TEXT("m"), -1000, 1000, .1));
    Sidebar->AddChildToVerticalBox(Number(UpField, TEXT("Up"), TEXT("m"), -1000, 1000, .1));
    Sidebar->AddChildToVerticalBox(WorksheetSize(WidgetTree, WorksheetLabel(WidgetTree, TEXT("Orientation"), 14, true), 0, 42));
    FollowGrade = WorksheetCheck(WidgetTree, TEXT("Follow grade")); FollowGrade->OnCheckStateChanged.AddDynamic(this, &ThisClass::GradeChanged);
    FollowBank = WorksheetCheck(WidgetTree, TEXT("Follow bank")); FollowBank->OnCheckStateChanged.AddDynamic(this, &ThisClass::BankChanged);
    Sidebar->AddChildToVerticalBox(WorksheetSize(WidgetTree, FollowGrade, 0, 31)); Sidebar->AddChildToVerticalBox(WorksheetSize(WidgetTree, FollowBank, 0, 31));
    LayerSummary = WorksheetLabel(WidgetTree, TEXT(""), 12, false, Muted); LayerSummary->SetAutoWrapText(true);
    WorksheetFill(Sidebar, LayerSummary, 1, FMargin(0, 12));
    Sidebar->AddChildToVerticalBox(WorksheetSize(WidgetTree, Button(TEXT("Back to route"), Page, 0), 0, 32));
    Columns->AddChildToHorizontalBox(WorksheetSize(WidgetTree, WorksheetSection(WidgetTree, WorksheetLabel(WidgetTree, TEXT("Layer settings"), 15, true), Sidebar), 242));
    return Columns;
}
UWidget* UParametricRoadLayerWidget::MakeLibraryPage()
{
    auto* Columns = WidgetTree->ConstructWidget<UHorizontalBox>(); CategoryTree = WidgetTree->ConstructWidget<UScrollBox>();
    Columns->AddChildToHorizontalBox(WorksheetSize(WidgetTree, WorksheetSection(WidgetTree, WorksheetLabel(WidgetTree, TEXT("Categories"), 15, true), CategoryTree), 194))->SetPadding(FMargin(0, 0, 5, 0));
    auto* Library = WidgetTree->ConstructWidget<UVerticalBox>(); auto* SearchRow = WidgetTree->ConstructWidget<UHorizontalBox>();
    Search = WidgetTree->ConstructWidget<UEditableTextBox>(); Search->SetHintText(FText::FromString(TEXT("Search blueprints…")));
    FEditableTextBoxStyle Style = FCoreStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>(TEXT("NormalEditableTextBox"));
    Style.BackgroundImageNormal = *FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox")); Style.BackgroundImageNormal.TintColor = FSlateColor(White);
    Style.BackgroundImageFocused = Style.BackgroundImageHovered = Style.BackgroundImageReadOnly = Style.BackgroundImageNormal;
    Style.Padding = FMargin(6, 5); Style.SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 14)); Search->WidgetStyle = Style; Search->SetForegroundColor(Ink);
     Search->OnTextChanged.AddDynamic(this, &ThisClass::SearchChanged);
    WorksheetFill(SearchRow, Search); SearchRow->AddChildToHorizontalBox(Button(TEXT("Clear"), ClearSearch));
    Library->AddChildToVerticalBox(WorksheetSize(WidgetTree, SearchRow, 0, 34))->SetPadding(FMargin(0, 0, 0, 8));
    CatalogCount = WorksheetLabel(WidgetTree, TEXT(""), 11, false, Muted); Library->AddChildToVerticalBox(WorksheetSize(WidgetTree, CatalogCount, 0, 23));
    Catalog = WidgetTree->ConstructWidget<UScrollBox>(); WorksheetFill(Library, Catalog);
    WorksheetFill(Columns, WorksheetSection(WidgetTree, WorksheetLabel(WidgetTree, TEXT("Blueprint library"), 15, true), Library), 1, FMargin(0, 0, 5, 0));
    auto* Preview = WidgetTree->ConstructWidget<UVerticalBox>(); PreviewIcon = WidgetTree->ConstructWidget<UImage>();
    PreviewIcon->SetDesiredSizeOverride(FVector2D(120, 120)); auto* Icon = WidgetTree->ConstructWidget<UScaleBox>(); Icon->SetStretch(EStretch::ScaleToFit); Icon->AddChild(PreviewIcon);
    auto* IconFrame = WorksheetPanel(WidgetTree, White, FMargin(35)); IconFrame->AddChild(Icon); Preview->AddChildToVerticalBox(WorksheetSize(WidgetTree, IconFrame, 0, 188));
    PreviewName = WorksheetLabel(WidgetTree, TEXT("Select a blueprint"), 15, true); PreviewName->SetAutoWrapText(true); Preview->AddChildToVerticalBox(WorksheetSize(WidgetTree, PreviewName, 0, 56));
    PreviewCategory = WorksheetLabel(WidgetTree, TEXT(""), 12, false, Muted); PreviewCategory->SetAutoWrapText(true); Preview->AddChildToVerticalBox(WorksheetSize(WidgetTree, PreviewCategory, 0, 56));
    PreviewDescription = WorksheetLabel(WidgetTree, TEXT(""), 12); PreviewDescription->SetAutoWrapText(true); WorksheetFill(Preview, PreviewDescription);
    UseBlueprintButton = Button(TEXT("Use blueprint"), UseBlueprint); Preview->AddChildToVerticalBox(WorksheetSize(WidgetTree, UseBlueprintButton, 0, 34))->SetPadding(FMargin(0, 6));
    Preview->AddChildToVerticalBox(WorksheetSize(WidgetTree, Button(TEXT("Cancel"), Page, 1), 0, 32));
    Columns->AddChildToHorizontalBox(WorksheetSize(WidgetTree, WorksheetSection(WidgetTree, WorksheetLabel(WidgetTree, TEXT("Selected blueprint"), 15, true), Preview), 252)); return Columns;
}
UWidget* UParametricRoadLayerWidget::MakePresetPage()
{
    auto* Columns = WidgetTree->ConstructWidget<UHorizontalBox>();
    auto* Library = WidgetTree->ConstructWidget<UVerticalBox>();
    PresetList = WidgetTree->ConstructWidget<UScrollBox>(); WorksheetFill(Library, PresetList);
    Library->AddChildToVerticalBox(WorksheetSize(WidgetTree, Button(TEXT("Back to route"), Page, 0), 0, 32))->SetPadding(FMargin(0, 8, 0, 0));
    Columns->AddChildToHorizontalBox(WorksheetSize(WidgetTree, WorksheetSection(WidgetTree,
        WorksheetLabel(WidgetTree, TEXT("Saved presets"), 15, true), Library), 252))->SetPadding(FMargin(0, 0, 5, 0));

    auto* Editor = WidgetTree->ConstructWidget<UVerticalBox>();
    auto* NameRow = WidgetTree->ConstructWidget<UHorizontalBox>();
    auto* Caption = NameRow->AddChildToHorizontalBox(WorksheetSize(WidgetTree, WorksheetLabel(WidgetTree, TEXT("Name"), 13, true), 64));
    Caption->SetVerticalAlignment(VAlign_Center);
    PresetName = WidgetTree->ConstructWidget<UEditableTextBox>();
    PresetName->SetHintText(FText::FromString(TEXT("Preset name")));
    PresetName->SetClearKeyboardFocusOnCommit(true); PresetName->SetSelectAllTextWhenFocused(true);
    FEditableTextBoxStyle Style = FCoreStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>(TEXT("NormalEditableTextBox"));
    Style.BackgroundImageNormal = *FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox"));
    Style.BackgroundImageNormal.TintColor = FSlateColor(White);
    Style.BackgroundImageFocused = Style.BackgroundImageHovered = Style.BackgroundImageReadOnly = Style.BackgroundImageNormal;
    Style.Padding = FMargin(6, 5); Style.SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 14));
    PresetName->WidgetStyle = Style; PresetName->SetForegroundColor(Ink);
    PresetName->OnTextChanged.AddDynamic(this, &ThisClass::PresetNameChanged);
    WorksheetFill(NameRow, PresetName);
    Editor->AddChildToVerticalBox(WorksheetSize(WidgetTree, NameRow, 0, 36))->SetPadding(FMargin(0, 0, 0, 8));

    PresetDetails = WidgetTree->ConstructWidget<UScrollBox>();
    WorksheetFill(Editor, WorksheetSection(WidgetTree, WorksheetLabel(WidgetTree, TEXT("Composition"), 14, true), PresetDetails));

    auto* Confirmation = WidgetTree->ConstructWidget<UVerticalBox>();
    PresetConfirmationText = WorksheetLabel(WidgetTree, TEXT(""), 13, true); PresetConfirmationText->SetAutoWrapText(true);
    Confirmation->AddChildToVerticalBox(PresetConfirmationText)->SetPadding(FMargin(0, 0, 0, 6));
    auto* ConfirmationActions = WidgetTree->ConstructWidget<UHorizontalBox>();
    WorksheetFill(ConfirmationActions, Button(TEXT("Replace preview"), PresetConfirmLoad), 1, FMargin(0, 0, 5, 0));
    WorksheetFill(ConfirmationActions, Button(TEXT("Cancel"), PresetCancelLoad));
    Confirmation->AddChildToVerticalBox(WorksheetSize(WidgetTree, ConfirmationActions, 0, 32));
    auto* ConfirmationFrame = WorksheetPanel(WidgetTree, Cream, FMargin(8)); ConfirmationFrame->AddChild(Confirmation);
    PresetConfirmation = ConfirmationFrame; PresetConfirmation->SetVisibility(ESlateVisibility::Collapsed);
    Editor->AddChildToVerticalBox(PresetConfirmation)->SetPadding(FMargin(0, 8, 0, 0));

    auto* Actions = WidgetTree->ConstructWidget<UHorizontalBox>();
    SaveNewPresetButton = Button(TEXT("Save new"), PresetSaveNew);
    UpdatePresetButton = Button(TEXT("Update selected"), PresetUpdate);
    LoadPresetButton = Button(TEXT("Load preset"), PresetLoad);
    WorksheetFill(Actions, SaveNewPresetButton, 1, FMargin(0, 0, 5, 0));
    WorksheetFill(Actions, UpdatePresetButton, 1, FMargin(0, 0, 5, 0)); WorksheetFill(Actions, LoadPresetButton);
    SaveNewPresetButton->SetToolTipText(FText::FromString(TEXT("Save the current road and decoration layers as a new preset.")));
    UpdatePresetButton->SetToolTipText(FText::FromString(TEXT("Replace this preset with the current road and decoration layers. Keeps a backup.")));
    LoadPresetButton->SetToolTipText(FText::FromString(TEXT("Start a new route using this preset.")));
    Editor->AddChildToVerticalBox(WorksheetSize(WidgetTree, Actions, 0, 36))->SetPadding(FMargin(0, 8, 0, 0));
    PresetMessage = WorksheetLabel(WidgetTree, TEXT(""), 12); PresetMessage->SetAutoWrapText(true);
    Editor->AddChildToVerticalBox(WorksheetSize(WidgetTree, PresetMessage, 0, 48))->SetPadding(FMargin(0, 8, 0, 0));
    WorksheetFill(Columns, WorksheetSection(WidgetTree, WorksheetLabel(WidgetTree, TEXT("Road preset"), 15, true), Editor));
    RebuildPresetList(); return Columns;
}
void UParametricRoadLayerWidget::ReadPresetList()
{
    // This is called only by an explicit page entry or a successful disk write.
    // Compact widgets and the display timer must never enumerate user files.
    if (bPassive || !Session.IsValid() || !Session->CanManagePresets()) return;
    const FString PreviousId = SelectedPresetId;
    ParametricRoadPresets::List(SavedPresets, PresetReadDiagnostic);
    if (!SavedPresets.ContainsByPredicate([&](const auto& P) { return P.Id == SelectedPresetId; })) SelectedPresetId.Reset();
    if (SelectedPresetId.IsEmpty() && SavedPresets.ContainsByPredicate([&](const auto& P) { return P.Id == Session->GetActivePresetId(); }))
        SelectedPresetId = Session->GetActivePresetId();
    RebuildPresetList();
    if (SelectedPresetId != PreviousId || (PresetName && PresetName->GetText().IsEmpty())) SelectPreset(SelectedPresetId);
    else UpdatePresetDetails();
    if (PresetMessage) PresetMessage->SetText(!PresetReferenceError.IsEmpty() ? PresetReferenceError : PresetReadDiagnostic);
}
void UParametricRoadLayerWidget::RebuildPresetList()
{
    if (!PresetList) return;
    const float Offset = PresetList->GetScrollOffset(); PresetList->ClearChildren(); PresetButtons.Reset();
    for (const auto& P : SavedPresets)
    {
        auto* B = Button(P.Name, PresetSelect, 0, P.Id); WorksheetTreeButtonStyle(B, SelectedPresetId == P.Id);
        CastChecked<UButtonSlot>(B->GetContent()->Slot)->SetHorizontalAlignment(HAlign_Left);
        B->SetToolTipText(FText::FromString(P.Name)); PresetList->AddChild(WorksheetSize(WidgetTree, B, 0, 36)); PresetButtons.Add(B);
    }
    if (SavedPresets.IsEmpty()) PresetList->AddChild(WorksheetLabel(WidgetTree, TEXT("No saved presets"), 13, false, Muted));
    PresetList->SetScrollOffset(Offset);
}
void UParametricRoadLayerWidget::SelectPreset(const FString& Id)
{
    SelectedPresetId = Id; PendingLoadPresetId.Reset();
    if (PresetConfirmation) PresetConfirmation->SetVisibility(ESlateVisibility::Collapsed);
    const auto* P = SavedPresets.FindByPredicate([&](const auto& Entry) { return Entry.Id == Id; });
    if (!P) SelectedPresetId.Reset();
    if (PresetName) PresetName->SetText(FText::FromString(P ? P->Name : Session.IsValid() ? Session->GetRoadBlueprintName() : FString()));
    for (int32 I = 0; I < PresetButtons.Num(); ++I) WorksheetTreeButtonStyle(PresetButtons[I], SavedPresets[I].Id == SelectedPresetId);
    UpdatePresetDetails();
    if (PresetMessage) PresetMessage->SetText(!PresetReferenceError.IsEmpty() ? PresetReferenceError : PresetReadDiagnostic);
    RefreshPresetActions();
}
void UParametricRoadLayerWidget::UpdatePresetDetails()
{
    if (!PresetDetails) return;
    const auto* Selected = SavedPresets.FindByPredicate([&](const auto& P) { return P.Id == SelectedPresetId; });
    const auto Composition = Selected ? *Selected : Session.IsValid() ? Session->CapturePreset(FString()) : FParametricRoadPreset();
    const float Offset = PresetDetails->GetScrollOffset(); PresetDetails->ClearChildren();
    const auto Row = [&](const FString& Text, bool bBold = false)
    {
        auto* Label = WorksheetLabel(WidgetTree, Text, bBold ? 14 : 12, bBold); Label->SetAutoWrapText(true);
        auto* Panel = WorksheetPanel(WidgetTree, Paper, FMargin(5, bBold ? 7 : 3)); Panel->AddChild(Label); PresetDetails->AddChild(Panel);
    };
    Row(TEXT("Base road"), true); Row(Composition.RoadBlueprintName.IsEmpty() ? TEXT("No road blueprint") : Composition.RoadBlueprintName);
    const auto& R = Composition.Road;
    Row(FString::Printf(TEXT("Slice %.3g m  ·  Width %.3g m  ·  Thickness %.3g m"), R.LengthMetres, R.WidthMetres, R.ThicknessMetres));
    Row(FString::Printf(TEXT("Overlap %.3g m  ·  Max edge step %.3g cm  ·  Deck plane %.3g m"), R.OverlapMetres, R.MaxEdgeStepMetres * 100, R.DeckPlaneMetres));
    Row(FString::Printf(TEXT("Tension %.3g  ·  Axis %s"), R.CurveTension, R.bForwardY ? TEXT("Y") : TEXT("X")));
    for (int32 I = 0; I < Composition.Layers.Num(); ++I)
    {
        const auto& L = Composition.Layers[I];
        Row(FString::Printf(TEXT("Layer %d  ·  %s"), I + 1, L.BlueprintName.IsEmpty() ? TEXT("Empty layer") : *L.BlueprintName), true);
        Row(FString::Printf(TEXT("%s  ·  Spacing %.3g m  ·  First placement %.3g m"), L.bEnabled ? TEXT("Enabled") : TEXT("Disabled"), L.SpacingMetres, L.FirstPlacementMetres));
        Row(FString::Printf(TEXT("Offset: Forward %.3g m  ·  Right %.3g m  ·  Up %.3g m"), L.OffsetMetres.X, L.OffsetMetres.Y, L.OffsetMetres.Z));
        Row(FString::Printf(TEXT("Follow grade: %s  ·  Follow bank: %s"), L.bFollowGrade ? TEXT("On") : TEXT("Off"), L.bFollowBank ? TEXT("On") : TEXT("Off")));
    }
    PresetDetails->SetScrollOffset(Offset); PresetReferenceError = FText::GetEmpty();
    if (Selected && Session.IsValid()) Session->ValidatePresetBlueprints(*Selected, PresetReferenceError);
}
void UParametricRoadLayerWidget::RefreshPresetActions()
{
    if (!SaveNewPresetButton) return;
    const bool Editable = !bPassive && Session.IsValid() && Session->CanManagePresets();
    const bool Selected = SavedPresets.ContainsByPredicate([&](const auto& P) { return P.Id == SelectedPresetId; });
    const bool Named = PresetName && !PresetName->GetText().ToString().TrimStartAndEnd().IsEmpty();
    const bool Confirming = !PendingLoadPresetId.IsEmpty();
    PresetName->SetIsReadOnly(!Editable);
    if (PresetMessage) PresetMessage->SetToolTipText(PresetMessage->GetText());
    SaveNewPresetButton->SetIsEnabled(Editable && Named && !Confirming);
    UpdatePresetButton->SetIsEnabled(Editable && Named && Selected && !Confirming);
    LoadPresetButton->SetIsEnabled(Editable && Selected && PresetReferenceError.IsEmpty() && !Confirming);
}
void UParametricRoadLayerWidget::PresetNameChanged(const FText&) { RefreshPresetActions(); }
void UParametricRoadLayerWidget::SavePreset(bool bUpdate)
{
    if (bPassive || !Session.IsValid() || !Session->CanManagePresets() || !PresetName) return;
    if (bUpdate && !SavedPresets.ContainsByPredicate([&](const auto& P) { return P.Id == SelectedPresetId; })) return;
    auto Preset = Session->CapturePreset(PresetName->GetText().ToString().TrimStartAndEnd());
    if (bUpdate) Preset.Id = SelectedPresetId;
    FText Error;
    if (!ParametricRoadPresets::Save(Preset, bUpdate, Error)) { PresetMessage->SetText(Error); return; }
    Session->RememberSavedPresetFromWidget(Preset);
    SelectedPresetId = Preset.Id; ReadPresetList(); SelectPreset(Preset.Id);
    PresetMessage->SetText(PresetReadDiagnostic.IsEmpty() ? FText::FromString(bUpdate ? TEXT("Preset updated.") : TEXT("Preset saved.")) : PresetReadDiagnostic);
}
void UParametricRoadLayerWidget::LoadPreset(bool bConfirmed)
{
    if (bPassive || !Session.IsValid() || !Session->CanManagePresets()) return;
    const FString Id = bConfirmed ? PendingLoadPresetId : SelectedPresetId;
    if (Id.IsEmpty() || (bConfirmed && Id != SelectedPresetId)) return;
    FParametricRoadPreset Preset; FText Error;
    if (!ParametricRoadPresets::Load(Id, Preset, Error) || !Session->ValidatePresetBlueprints(Preset, Error))
    {
        PendingLoadPresetId.Reset(); PresetConfirmation->SetVisibility(ESlateVisibility::Collapsed);
        PresetMessage->SetText(Error); RefreshPresetActions(); return;
    }
    if (!bConfirmed && Session->HasRoutePreview())
    {
        PendingLoadPresetId = Id;
        PresetConfirmationText->SetText(FText::FromString(TEXT("Replace the current route preview?")));
        PresetConfirmation->SetVisibility(ESlateVisibility::Visible); PresetMessage->SetText(FText::GetEmpty()); RefreshPresetActions(); return;
    }
    if (Session->LoadPresetFromWidget(Preset, Error)) return; // Native handoff can close and release this widget.
    PendingLoadPresetId.Reset(); PresetConfirmation->SetVisibility(ESlateVisibility::Collapsed);
    PresetMessage->SetText(Error); RefreshPresetActions();
}
void UParametricRoadLayerWidget::SetPage(int32 Value)
{
    const int32 PreviousPage = CurrentPage;
    CurrentPage = FMath::Clamp(Value, 0, 3); if (Pages) Pages->SetActiveWidgetIndex(CurrentPage);
    if (CurrentPage != 3) { PendingLoadPresetId.Reset(); if (PresetConfirmation) PresetConfirmation->SetVisibility(ESlateVisibility::Collapsed); }
    if (CurrentPage == 3 && PreviousPage != 3 && !bPassive) ReadPresetList();
    RefreshDisplay();
}
void UParametricRoadLayerWidget::RebuildPoints()
{
    if (!PointTree || !Session.IsValid()) return;
    const float OldOffset = PointTree->GetScrollOffset();
    PointTree->ClearChildren(); PointButtons.Reset(); LastPointCount = Session->GetPointCount();
    auto* Overlay = WidgetTree->ConstructWidget<UOverlay>();
    auto* Rows = WidgetTree->ConstructWidget<UVerticalBox>(); Overlay->AddChildToOverlay(Rows)->SetHorizontalAlignment(HAlign_Fill);
    for (int32 I = 0; I < LastPointCount; ++I)
    {
        auto* B = Button(Session->GetPointLabel(I).ToString(), Point, I); WorksheetTreeButtonStyle(B);
        CastChecked<UButtonSlot>(B->GetContent()->Slot)->SetHorizontalAlignment(HAlign_Left);
        CastChecked<UButtonSlot>(B->GetContent()->Slot)->SetPadding(FMargin(29, 0, 3, 0));
        Rows->AddChildToVerticalBox(WorksheetSize(WidgetTree, B, 0, 31)); PointButtons.Add(B);
    }
    PointGuides = WidgetTree->ConstructWidget<UParametricRoadTreeGuides>(); PointGuides->Configure(LastPointCount);
    PointGuides->SetVisibility(ESlateVisibility::HitTestInvisible); Overlay->AddChildToOverlay(PointGuides);
    PointTree->AddChild(Overlay); PointTree->SetScrollOffset(OldOffset); LastSelectedPoint = INDEX_NONE;
}
void UParametricRoadLayerWidget::RebuildLayers()
{
    if (!LayerTree || !Session.IsValid()) return;
    LayerTree->ClearChildren(); LayerButtons.Reset();
    if (!Session->GetLayerCount())
    {
        auto* Empty = WorksheetLabel(WidgetTree, TEXT("No layers"), 12, false, Muted); LayerTree->AddChild(Empty);
    }
    for (int32 I = 0; I < Session->GetLayerCount(); ++I)
    {
        const auto& Settings = Session->GetLayerSettings(I);
        FString Name = Settings.BlueprintName.IsEmpty() ? TEXT("Empty layer") : Settings.BlueprintName;
        for (const auto& E : Session->GetBlueprintCatalog()) if (E.Name == Settings.BlueprintName) { Name = E.DisplayName.ToString(); break; }
        auto* B = Button(FString::Printf(TEXT("%d  %s%s"), I + 1, *Name, Settings.bEnabled ? TEXT("") : TEXT(" (off)")), Layer, I);
        WorksheetTreeButtonStyle(B);
        CastChecked<UButtonSlot>(B->GetContent()->Slot)->SetHorizontalAlignment(HAlign_Left);
        B->SetToolTipText(FText::FromString(Name)); LayerTree->AddChild(WorksheetSize(WidgetTree, B, 0, 38)); LayerButtons.Add(B);
    }
}
void UParametricRoadLayerWidget::RebuildCategories()
{
    if (!CategoryTree || !Session.IsValid()) return;
    const float OldOffset = CategoryTree->GetScrollOffset();
    CategoryTree->ClearChildren(); CategoryButtons.Reset();
    auto* All = Button(TEXT("All blueprints"), Category); WorksheetTreeButtonStyle(All, CategoryFilter.IsEmpty());
    CastChecked<UButtonSlot>(All->GetContent()->Slot)->SetHorizontalAlignment(HAlign_Left);
    CategoryTree->AddChild(WorksheetSize(WidgetTree, All, 0, 32)); CategoryButtons.Add(All);
    for (const auto& C : Session->GetBlueprintCategories())
    {
        auto* Overlay = WidgetTree->ConstructWidget<UOverlay>(); auto* Rows = WidgetTree->ConstructWidget<UVerticalBox>(); Overlay->AddChildToOverlay(Rows)->SetHorizontalAlignment(HAlign_Fill);
        auto* B = Button(C.DisplayName.ToString(), Category, 0, C.Key); WorksheetTreeButtonStyle(B, CategoryFilter == C.Key && SubCategoryFilter.IsEmpty());
        CastChecked<UButtonSlot>(B->GetContent()->Slot)->SetHorizontalAlignment(HAlign_Left);
        Rows->AddChildToVerticalBox(WorksheetSize(WidgetTree, B, 0, 31)); CategoryButtons.Add(B);
        for (int32 I = 0; I < C.SubCategories.Num(); ++I)
        {
            const auto& Sub = C.SubCategories[I]; auto* Child = Button(Sub.DisplayName.ToString(), Category, I + 1, C.Key);
            WorksheetTreeButtonStyle(Child, CategoryFilter == C.Key && SubCategoryFilter == Sub.Key);
            CastChecked<UButtonSlot>(Child->GetContent()->Slot)->SetHorizontalAlignment(HAlign_Left);
            CastChecked<UButtonSlot>(Child->GetContent()->Slot)->SetPadding(FMargin(29, 0, 3, 0));
            Rows->AddChildToVerticalBox(WorksheetSize(WidgetTree, Child, 0, 31)); CategoryButtons.Add(Child);
        }
        auto* Guides = WidgetTree->ConstructWidget<UParametricRoadTreeGuides>(); Guides->Configure(C.SubCategories.Num() + 1, true);
        Guides->SetVisibility(ESlateVisibility::HitTestInvisible); Overlay->AddChildToOverlay(Guides);
        CategoryTree->AddChild(Overlay);
    }
    CategoryTree->SetScrollOffset(OldOffset);
}
void UParametricRoadLayerWidget::RebuildCatalog()
{
    if (!Catalog || !Session.IsValid()) return;
    const float OldOffset = Catalog->GetScrollOffset(); Catalog->ClearChildren(); CatalogButtons.Reset(); VisibleBlueprints.Reset();
    for (const auto& Entry : Session->GetBlueprintCatalog())
    {
        if (!CategoryFilter.IsEmpty() && Entry.CategoryKey != CategoryFilter) continue;
        if (!SubCategoryFilter.IsEmpty() && Entry.SubCategoryKey != SubCategoryFilter) continue;
        if (!SearchQuery.IsEmpty() && !Entry.Name.Contains(SearchQuery, ESearchCase::IgnoreCase)
            && !Entry.DisplayName.ToString().Contains(SearchQuery, ESearchCase::IgnoreCase)) continue;
        auto* B = Button(TEXT(""), PreviewBlueprint, 0, Entry.Name); B->ClearChildren();
        auto* Row = WidgetTree->ConstructWidget<UHorizontalBox>(); CastChecked<UButtonSlot>(B->AddChild(Row))->SetHorizontalAlignment(HAlign_Fill);
        auto* Icon = WidgetTree->ConstructWidget<UImage>();
        if (auto* T = Cast<UTexture2D>(Entry.Icon.Get())) Icon->SetBrushFromTexture(T);
        else if (auto* M = Cast<UMaterialInterface>(Entry.Icon.Get())) Icon->SetBrushFromMaterial(M);
        else { Icon->SetBrush(*FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox"))); Icon->SetColorAndOpacity(Mist); }
        Row->AddChildToHorizontalBox(WorksheetSize(WidgetTree, Icon, 28, 28))->SetPadding(FMargin(2, 0, 8, 0));
        auto* Name = WorksheetLabel(WidgetTree, Entry.DisplayName.IsEmpty() ? Entry.Name : Entry.DisplayName.ToString(), 13);
        WorksheetFill(Row, Name); Row->AddChildToHorizontalBox(WorksheetSize(WidgetTree, WorksheetLabel(WidgetTree, Entry.SubCategoryName.IsEmpty()
            ? Entry.CategoryName.ToString() : Entry.SubCategoryName.ToString(), 10, false, Muted), 120));
        B->SetToolTipText(Entry.Description); WorksheetButtonStyle(B, Entry.Name == PendingBlueprint);
        Catalog->AddChild(WorksheetSize(WidgetTree, B, 0, 40)); CatalogButtons.Add(B); VisibleBlueprints.Add(Entry.Name);
    }
    if (VisibleBlueprints.IsEmpty())
    {
        auto* Empty = WorksheetLabel(WidgetTree, Session->GetBlueprintCatalog().IsEmpty() ? TEXT("No blueprints") : TEXT("No matches"), 13, false, Muted);
        Empty->SetAutoWrapText(true); Catalog->AddChild(Empty);
    }
    Catalog->SetScrollOffset(OldOffset); CatalogCount->SetText(FText::FromString(FString::Printf(TEXT("%d blueprints"), VisibleBlueprints.Num())));
    UpdateBlueprintPreview();
}
void UParametricRoadLayerWidget::UpdateBlueprintPreview()
{
    if (!PreviewIcon || !Session.IsValid()) return;
    const auto* E = Session->GetBlueprintCatalog().FindByPredicate([&](const auto& V) { return V.Name == PendingBlueprint; });
    PreviewIcon->SetVisibility(E && E->Icon.IsValid() ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
    if (E)
    {
        if (auto* T = Cast<UTexture2D>(E->Icon.Get())) PreviewIcon->SetBrushFromTexture(T);
        else if (auto* M = Cast<UMaterialInterface>(E->Icon.Get())) PreviewIcon->SetBrushFromMaterial(M);
    }
    PreviewName->SetText(E ? E->DisplayName : FText::FromString(TEXT("Select a blueprint")));
    PreviewCategory->SetText(E ? FText::FromString(E->CategoryName.ToString() + (E->SubCategoryName.IsEmpty() ? TEXT("") : TEXT(" / ") + E->SubCategoryName.ToString())) : FText::GetEmpty());
    PreviewDescription->SetText(E ? E->Description : FText::GetEmpty());
    UseBlueprintButton->SetIsEnabled(E && Session->GetSelectedLayerIndex() != INDEX_NONE && !Session->IsBuilding());
    for (int32 I = 0; I < CatalogButtons.Num(); ++I) WorksheetButtonStyle(CatalogButtons[I], VisibleBlueprints[I] == PendingBlueprint);
}
float UParametricRoadLayerWidget::ReadNumber(int32 Field) const
{
    if (!Session.IsValid()) return 0;
    if (Field < 11) return float(Session->GetDiagramValue(Field));
    const auto& S = Session->GetLayerSettings();
    switch (Field)
    {
    case SpacingField: return S.SpacingMetres; case FirstField: return S.FirstPlacementMetres;
    case ForwardField: return S.OffsetMetres.X; case RightField: return S.OffsetMetres.Y; case UpField: return S.OffsetMetres.Z;
    default: return 0;
    }
}
void UParametricRoadLayerWidget::RefreshDisplay()
{
    auto* S = Session.Get(); if (!S || !Pages) return;
    bUpdating = true;
    Title->SetText(FText::FromString(TEXT("Roadworks"))); BlueprintName->SetText(FText::FromString(S->GetRoadBlueprintName()));
    if (LastPointCount != S->GetPointCount()) RebuildPoints();
    for (int32 I = 0; I < PointButtons.Num(); ++I) WorksheetTreeButtonStyle(PointButtons[I], S->GetSelectedPointIndex() == I);
    if (LastSelectedPoint != S->GetSelectedPointIndex() && PointButtons.IsValidIndex(S->GetSelectedPointIndex()))
    {
        LastSelectedPoint = S->GetSelectedPointIndex();
        PointTree->ScrollWidgetIntoView(PointButtons[LastSelectedPoint], false, EDescendantScrollDestination::Center);
    }
    Phase->SetText(S->GetPhaseText()); SelectedPoint->SetText(FText::FromString(TEXT("Selected point: ") + S->GetSelectedPointLabel().ToString()));
    for (int32 I = 0; I < Numbers.Num(); ++I)
    {
        auto* Spin = Numbers[I].Get(); if (!Spin) continue;
        if (!Spin->HasAnyUserFocus() && !Spin->HasFocusedDescendants() && Spin->GetValue() != ReadNumber(I)) Spin->SetValue(ReadNumber(I));
        NumberPanels[I]->SetBrushColor(I < 11 && I == S->GetAdjustmentIndex() ? Orange : Paper);
        NumberPanels[I]->SetIsEnabled(!S->IsBuilding() && (I < 12 || !S->GetLayerSettings().BlueprintName.IsEmpty()));
        if (I < 2)
        {
            auto* Select = CastChecked<UButton>(CastChecked<UPanelWidget>(NumberPanels[I]->GetContent())->GetChildAt(0));
            CastChecked<UTextBlock>(Select->GetContent())->SetText(FText::FromString(S->GetAdjustmentLabel(I).ToString()
                + TEXT(" (°)") + (S->IsSelectedPointAutomatic(I) ? TEXT(" · AUTO") : TEXT(""))));
        }
    }
    CastChecked<UTextBlock>(Axis->GetContent())->SetText(FText::FromString(S->IsForwardY() ? TEXT("Axis: Y") : TEXT("Axis: X")));
    WorksheetButtonStyle(Axis, S->GetAdjustmentIndex() == 11); Fine->SetIsChecked(S->UsesFineAdjustment());
    if (CatalogRevision != S->GetBlueprintCatalogRevision())
    {
        CatalogRevision = S->GetBlueprintCatalogRevision(); RebuildCategories(); RebuildCatalog(); LastLayerSignature.Reset();
        if (CurrentPage == 3)
        {
            UpdatePresetDetails();
            PresetMessage->SetText(PresetReferenceError.IsEmpty() ? PresetReadDiagnostic : PresetReferenceError);
        }
    }
    FString Signature = FString::FromInt(S->GetLayerCount());
    for (int32 I = 0; I < S->GetLayerCount(); ++I)
    { const auto& L = S->GetLayerSettings(I); Signature += TEXT("|") + L.BlueprintName + (L.bEnabled ? TEXT("1") : TEXT("0")); }
    if (LastLayerSignature != Signature) { LastLayerSignature = Signature; RebuildLayers(); }
    for (int32 I = 0; I < LayerButtons.Num(); ++I) WorksheetTreeButtonStyle(LayerButtons[I], I == S->GetSelectedLayerIndex());
    const auto& L = S->GetLayerSettings(); const bool HasLayer = S->GetSelectedLayerIndex() != INDEX_NONE;
    FString Name = L.BlueprintName.IsEmpty() ? TEXT("Empty layer") : L.BlueprintName;
    for (const auto& E : S->GetBlueprintCatalog()) if (E.Name == L.BlueprintName) { Name = E.DisplayName.ToString(); break; }
    LayerName->SetText(FText::FromString(HasLayer ? FString::Printf(TEXT("Layer %d: %s"), S->GetSelectedLayerIndex() + 1, *Name) : TEXT("No layer selected")));
    LayerSummary->SetText(HasLayer ? S->GetLayerStatus() : FText::GetEmpty());
    LayerControls->SetIsEnabled(HasLayer && !L.BlueprintName.IsEmpty() && !S->IsBuilding());
    Enabled->SetIsChecked(L.bEnabled); FollowGrade->SetIsChecked(L.bFollowGrade); FollowBank->SetIsChecked(L.bFollowBank);
    FollowGrade->SetIsEnabled(HasLayer && !L.BlueprintName.IsEmpty() && !S->IsBuilding());
    FollowBank->SetIsEnabled(HasLayer && !L.BlueprintName.IsEmpty() && !S->IsBuilding());
    AddLayerButton->SetIsEnabled(!S->IsBuilding() && S->GetLayerCount() < UParametricRoadSession::MaximumLayers);
    RemoveLayerButton->SetIsEnabled(!S->IsBuilding() && HasLayer);
    PresetNavigation->SetIsEnabled(!S->IsBuilding()); RefreshPresetActions();
    FString Text = S->GetRouteSummary().ToString();
    if (CurrentPage == 1) Text = FString::Printf(TEXT("%d placements · %d decoration layers"), S->GetTotalLayerPlacementCount(), S->GetLayerCount());
    if (CurrentPage == 2) Text = FString::Printf(TEXT("Assign to layer %d · %s"), S->GetSelectedLayerIndex() + 1, *Name);
    if (CurrentPage == 3) Text = FString::Printf(TEXT("%d saved presets"), SavedPresets.Num());
    if (CurrentPage == 0 && !S->GetPanelStatus().IsEmpty()) Text += TEXT("  |  ") + S->GetPanelStatus().ToString();
    Footer->SetText(FText::FromString(Text));
    Footer->SetToolTipText(CurrentPage == 3 ? FText::FromString(FPaths::ConvertRelativePathToFull(ParametricRoadPresets::Directory())) : S->GetCostText());
    Diagram->InvalidateLayoutAndVolatility(); Stations->InvalidateLayoutAndVolatility(); bUpdating = false;
}
void UParametricRoadLayerWidget::Activate(int32 Command, int32 Index, const FString& Key)
{
    auto* S = Session.Get(); if (!S || bPassive) return;
    switch (Command)
    {
    case CloseTerminal: S->CloseLayerConfiguration(); return;
    case Page: SetPage(Index); return;
    case Point: S->SelectPointFromWidget(Index); break;
    case Adjustment: S->SelectAdjustmentFromWidget(Index); break;
    case Decrement: case Increment:
        if (Numbers.IsValidIndex(Index) && Numbers[Index]) CommitNumber(Index, ReadNumber(Index) + NumberSteps[Index] * (S->UsesFineAdjustment() ? .1f : 1.0f) * (Command == Increment ? 1 : -1)); break;
    case AxisToggle: S->SelectAdjustmentFromWidget(11); S->SetAdjustmentFromWidget(11, S->IsForwardY() ? 0 : 1); break;
    case AddLayer: S->AddLayer(); break;
    case RemoveLayer: S->RemoveSelectedLayer(); break;
    case Layer: S->SelectLayer(Index); break;
    case Browse: PendingBlueprint = S->GetLayerSettings().BlueprintName; SetPage(2); UpdateBlueprintPreview(); break;
    case PreviewBlueprint: PendingBlueprint = Key; UpdateBlueprintPreview(); break;
    case UseBlueprint:
        if (S->GetBlueprintCatalog().ContainsByPredicate([&](const auto& E) { return E.Name == PendingBlueprint; }) && S->GetSelectedLayerIndex() != INDEX_NONE)
        { S->SelectDecorationBlueprint(PendingBlueprint); SetPage(1); } break;
    case Category:
        CategoryFilter = Key; SubCategoryFilter.Reset();
        for (const auto& C : S->GetBlueprintCategories()) if (C.Key == Key && C.SubCategories.IsValidIndex(Index - 1)) SubCategoryFilter = C.SubCategories[Index - 1].Key;
        RebuildCategories(); RebuildCatalog(); break;
    case ClearSearch: Search->SetText(FText::GetEmpty()); SearchChanged(FText::GetEmpty()); break;
    case PresetSelect: SelectPreset(Key); break;
    case PresetSaveNew: SavePreset(false); return;
    case PresetUpdate: SavePreset(true); return;
    case PresetLoad: LoadPreset(false); return;
    case PresetConfirmLoad: LoadPreset(true); return;
    case PresetCancelLoad:
        PendingLoadPresetId.Reset(); PresetConfirmation->SetVisibility(ESlateVisibility::Collapsed);
        PresetMessage->SetText(!PresetReferenceError.IsEmpty() ? PresetReferenceError : PresetReadDiagnostic); break;
    default: break;
    }
    RefreshDisplay();
}
void UParametricRoadLayerWidget::CommitNumber(int32 Field, float Value)
{
    if (bUpdating || bPassive || !FMath::IsFinite(Value) || !Session.IsValid()) return;
    // Native text focus-loss commits even an unchanged value. Do not select a
    // new adjustment, pin an automatic tangent or rebuild the route for that.
    if (Numbers.IsValidIndex(Field))
    {
        FNumberFormattingOptions Format; Format.UseGrouping = false;
        Format.MinimumFractionalDigits = 0; Format.MaximumFractionalDigits = 3;
        // Text-mode focus loss reparses the displayed (rounded) string. Keep
        // hidden canonical precision if the visible number did not change.
        if (FText::AsNumber(Value, &Format).EqualTo(FText::AsNumber(ReadNumber(Field), &Format))) return;
    }
    auto* S = Session.Get();
    if (Field < 11) { S->SelectAdjustmentFromWidget(Field); S->SetAdjustmentFromWidget(Field, Value); }
    else switch (Field)
    {
    case SpacingField: S->SetLayerSpacing(Value); break;
    case FirstField: S->SetLayerFirstPlacement(Value); break;
    case ForwardField: S->SetLayerOffset(0, Value); break;
    case RightField: S->SetLayerOffset(1, Value); break;
    case UpField: S->SetLayerOffset(2, Value); break;
    default: return;
    }
    RefreshDisplay();
}
void UParametricRoadLayerWidget::SearchChanged(const FText& V) { SearchQuery = V.ToString().TrimStartAndEnd(); RebuildCatalog(); }
void UParametricRoadLayerWidget::EnabledChanged(bool V) { if (!bUpdating && !bPassive && Session.IsValid()) { Session->SetLayerEnabled(V); RefreshDisplay(); } }
void UParametricRoadLayerWidget::GradeChanged(bool V) { if (!bUpdating && !bPassive && Session.IsValid()) { Session->SetLayerFollowGrade(V); RefreshDisplay(); } }
void UParametricRoadLayerWidget::BankChanged(bool V) { if (!bUpdating && !bPassive && Session.IsValid()) { Session->SetLayerFollowBank(V); RefreshDisplay(); } }
void UParametricRoadLayerWidget::FineChanged(bool V) { if (!bUpdating && !bPassive && Session.IsValid()) { Session->SetFineAdjustmentFromWidget(V); } }
void UParametricRoadLayerWidget::NativeTick(const FGeometry& Geometry, float DeltaSeconds)
{
    Super::NativeTick(Geometry, DeltaSeconds);
    if (bPassive) return;
    if (Session.IsValid()) Session->RefreshBlueprintCatalogIfNeeded(DeltaSeconds);
    RefreshSeconds -= DeltaSeconds; if (RefreshSeconds <= 0) { RefreshSeconds = .1f; RefreshDisplay(); }
}
