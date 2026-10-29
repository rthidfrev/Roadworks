#pragma once

#include "Blueprint/UserWidget.h"
#include "ParametricRoadSession.h"
#include "ParametricRoadWidgets.generated.h"

class UTextBlock;
class UBorder;
class USizeBox;
class UUniformGridPanel;
class UParametricRoadLayerWidget;

/** Relative bending schematic, not a world compass. End bars and heading arrows
 * share these exact diagram-space anchors and Bezier controls. */
struct FParametricRoadHeadingDiagram
{
    FVector2D Start, StartControl, EndControl, End;
    FVector2D StartDirection, EndDirection;
};

/** Read-only vector diagram; no texture generation or input focus. */
UCLASS(Transient)
class PARAMETRICROADS_API UParametricRoadDiagramWidget : public UWidget
{
    GENERATED_BODY()
public:
    void SetSession(UParametricRoadSession* InSession) { Session = InSession; }
    static float FitSectionRadius(const FVector2D& Size, float AngleRadians);
    static FParametricRoadHeadingDiagram MakeHeadingDiagram(const FVector2D& Size, double StartDegrees, double EndDegrees);
    static FText GetDiagramCaption(EParametricRoadAdjustment Adjustment);
    static FText GetDiagramExplanation(EParametricRoadAdjustment Adjustment);
protected:
    TSharedRef<SWidget> RebuildWidget() override;
private:
    UPROPERTY(Transient) TWeakObjectPtr<UParametricRoadSession> Session;
};

UCLASS(Transient)
class PARAMETRICROADS_API UParametricRoadStatusWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    void SetSession(UParametricRoadSession* InSession);
    bool RefreshDisplay();
    uint32 GetDisplayRevision() const { return DisplayRevision; }
    UParametricRoadLayerWidget* GetWorksheet() const { return Worksheet; }
protected:
    TSharedRef<SWidget> RebuildWidget() override;
    void NativeTick(const FGeometry& Geometry, float DeltaSeconds) override;
private:
    UPROPERTY(Transient) TWeakObjectPtr<UParametricRoadSession> Session;
    // Both monitor sizes render this same native worksheet. Only hit testing differs.
    UPROPERTY(Transient) TObjectPtr<UParametricRoadLayerWidget> Worksheet;
    FString LastPresentation;
    double RefreshSeconds = 0;
    uint32 DisplayRevision = 0;
};
