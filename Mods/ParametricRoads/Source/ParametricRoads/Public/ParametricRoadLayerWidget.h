#pragma once

#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Components/SpinBox.h"
#include "ParametricRoadPresets.h"
#include "ParametricRoadLayerWidget.generated.h"

class UParametricRoadSession;
class UParametricRoadLayerWidget;
class UParametricRoadDiagramWidget;
class UEditableTextBox;
class UScrollBox;
class UTextBlock;
class UBorder;
class UWidgetSwitcher;
class UCheckBox;
class UImage;
class UHorizontalBox;

/** One continuous vector guide across a fixed hierarchy; never a disclosure. */
UCLASS(Transient)
class PARAMETRICROADS_API UParametricRoadTreeGuides : public UWidget
{
    GENERATED_BODY()
public:
    void Configure(int32 InRows, bool InHeader = false) { Rows = InRows; bHeader = InHeader; }
    int32 GetRows() const { return Rows; }
protected:
    TSharedRef<SWidget> RebuildWidget() override;
private:
    int32 Rows = 0;
    bool bHeader = false;
};

/** Centred native vector window cross; no font glyphs or image assets. */
UCLASS(Transient)
class PARAMETRICROADS_API UParametricRoadCloseMark : public UWidget
{
    GENERATED_BODY()
protected:
    TSharedRef<SWidget> RebuildWidget() override;
};

/** Resolution-independent check mark, painted from the native checkbox state. */
UCLASS(Transient)
class PARAMETRICROADS_API UParametricRoadCheckMark : public UWidget
{
    GENERATED_BODY()
public:
    void SetTarget(UCheckBox* InCheck);
protected:
    TSharedRef<SWidget> RebuildWidget() override;
private:
    UPROPERTY(Transient) TWeakObjectPtr<UCheckBox> Check;
};

/** Dispatch data stays with the native hit target, independent of list indices. */
UCLASS(Transient)
class PARAMETRICROADS_API UParametricRoadWorksheetButton : public UButton
{
    GENERATED_BODY()
public:
    void SetTarget(UParametricRoadLayerWidget* Widget, int32 Command, int32 Index = 0, const FString& Key = FString());
private:
    UFUNCTION() void Activate();
    UPROPERTY(Transient) TWeakObjectPtr<UParametricRoadLayerWidget> Target;
    int32 Action = 0, Argument = 0;
    FString Identity;
};

/** Native editable number with no range-dependent mouse dragging. */
UCLASS(Transient)
class PARAMETRICROADS_API UParametricRoadNumberBox : public USpinBox
{
    GENERATED_BODY()
public:
    void SetTarget(UParametricRoadLayerWidget* Widget, int32 Index);
private:
    UFUNCTION() void Commit(float InValue, ETextCommit::Type Method);
    UPROPERTY(Transient) TWeakObjectPtr<UParametricRoadLayerWidget> Target;
    int32 Field = 0;
};

/** Flat vector station schematic; no scene, texture or game actors. */
UCLASS(Transient)
class PARAMETRICROADS_API UParametricRoadLayerDiagram : public UWidget
{
    GENERATED_BODY()
public:
    void SetSession(UParametricRoadSession* InSession) { Session = InSession; }
protected:
    TSharedRef<SWidget> RebuildWidget() override;
private:
    UPROPERTY(Transient) TWeakObjectPtr<UParametricRoadSession> Session;
};

/** Route worksheet and layer editor, rendered inside the physical monitor only. */
UCLASS(Transient)
class PARAMETRICROADS_API UParametricRoadLayerWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    explicit UParametricRoadLayerWidget(const FObjectInitializer& ObjectInitializer);
    void SetSession(UParametricRoadSession* InSession);
    void SetPassive(bool InPassive);
    bool IsPassive() const { return bPassive; }
    void RefreshDisplay();
    void Activate(int32 Command, int32 Index = 0, const FString& Key = FString());
    void CommitNumber(int32 Field, float Value);
protected:
    TSharedRef<SWidget> RebuildWidget() override;
    void NativeTick(const FGeometry& Geometry, float DeltaSeconds) override;
private:
    friend class FParametricRoadLayerWidgetTest;
    friend class FParametricRoadWidgetTest;
    UFUNCTION() void SearchChanged(const FText& Value);
    UFUNCTION() void EnabledChanged(bool Value);
    UFUNCTION() void GradeChanged(bool Value);
    UFUNCTION() void BankChanged(bool Value);
    UFUNCTION() void FineChanged(bool Value);
    UFUNCTION() void PresetNameChanged(const FText& Value);
    UWidget* MakeRoutePage();
    UWidget* MakeLayerPage();
    UWidget* MakeLibraryPage();
    UWidget* MakePresetPage();
    UParametricRoadWorksheetButton* Button(const FString& Label, int32 Command, int32 Index = 0, const FString& Key = FString());
    UWidget* Number(int32 Field, const FString& Label, const FString& Unit, float Minimum, float Maximum, float Step);
    void SetPage(int32 Page);
    void RebuildPoints();
    void RebuildLayers();
    void RebuildCategories();
    void RebuildCatalog();
    void UpdateBlueprintPreview();
    void ReadPresetList();
    void RebuildPresetList();
    void SelectPreset(const FString& Id);
    void UpdatePresetDetails();
    void RefreshPresetActions();
    void SavePreset(bool bUpdate);
    void LoadPreset(bool bConfirmed);
    float ReadNumber(int32 Field) const;
    UPROPERTY(Transient) TWeakObjectPtr<UParametricRoadSession> Session;
    UPROPERTY(Transient) TObjectPtr<UWidgetSwitcher> Pages;
    UPROPERTY(Transient) TObjectPtr<UWidget> Surface;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Title;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Footer;
    UPROPERTY(Transient) TObjectPtr<UHorizontalBox> HeaderChrome;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Phase;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> SelectedPoint;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> BlueprintName;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> LayerName;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> LayerSummary;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> PreviewName;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> PreviewCategory;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> PreviewDescription;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> CatalogCount;
    UPROPERTY(Transient) TObjectPtr<UImage> PreviewIcon;
    UPROPERTY(Transient) TObjectPtr<UScrollBox> PointTree;
    UPROPERTY(Transient) TObjectPtr<UScrollBox> LayerTree;
    UPROPERTY(Transient) TObjectPtr<UScrollBox> CategoryTree;
    UPROPERTY(Transient) TObjectPtr<UScrollBox> Catalog;
    UPROPERTY(Transient) TObjectPtr<UEditableTextBox> Search;
    UPROPERTY(Transient) TObjectPtr<UEditableTextBox> PresetName;
    UPROPERTY(Transient) TObjectPtr<UScrollBox> PresetList;
    UPROPERTY(Transient) TObjectPtr<UScrollBox> PresetDetails;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> PresetMessage;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> PresetConfirmationText;
    UPROPERTY(Transient) TObjectPtr<UWidget> PresetConfirmation;
    UPROPERTY(Transient) TObjectPtr<UParametricRoadWorksheetButton> PresetNavigation;
    UPROPERTY(Transient) TObjectPtr<UParametricRoadWorksheetButton> SaveNewPresetButton;
    UPROPERTY(Transient) TObjectPtr<UParametricRoadWorksheetButton> UpdatePresetButton;
    UPROPERTY(Transient) TObjectPtr<UParametricRoadWorksheetButton> LoadPresetButton;
    UPROPERTY(Transient) TArray<TObjectPtr<UParametricRoadWorksheetButton>> PresetButtons;
    UPROPERTY(Transient) TObjectPtr<UWidget> LayerControls;
    UPROPERTY(Transient) TObjectPtr<UWidget> LayerSidebar;
    UPROPERTY(Transient) TObjectPtr<UParametricRoadWorksheetButton> AddLayerButton;
    UPROPERTY(Transient) TObjectPtr<UParametricRoadWorksheetButton> RemoveLayerButton;
    UPROPERTY(Transient) TObjectPtr<UParametricRoadWorksheetButton> UseBlueprintButton;
    UPROPERTY(Transient) TObjectPtr<UParametricRoadWorksheetButton> Axis;
    UPROPERTY(Transient) TObjectPtr<UParametricRoadWorksheetButton> CloseButton;
    UPROPERTY(Transient) TObjectPtr<UParametricRoadTreeGuides> PointGuides;
    UPROPERTY(Transient) TObjectPtr<UCheckBox> Enabled;
    UPROPERTY(Transient) TObjectPtr<UCheckBox> FollowGrade;
    UPROPERTY(Transient) TObjectPtr<UCheckBox> FollowBank;
    UPROPERTY(Transient) TObjectPtr<UCheckBox> Fine;
    UPROPERTY(Transient) TObjectPtr<UParametricRoadDiagramWidget> Diagram;
    UPROPERTY(Transient) TObjectPtr<UParametricRoadLayerDiagram> Stations;
    UPROPERTY(Transient) TArray<TObjectPtr<UParametricRoadNumberBox>> Numbers;
    UPROPERTY(Transient) TArray<TObjectPtr<UBorder>> NumberPanels;
    UPROPERTY(Transient) TArray<TObjectPtr<UParametricRoadWorksheetButton>> PointButtons;
    UPROPERTY(Transient) TArray<TObjectPtr<UParametricRoadWorksheetButton>> LayerButtons;
    UPROPERTY(Transient) TArray<TObjectPtr<UParametricRoadWorksheetButton>> CatalogButtons;
    UPROPERTY(Transient) TArray<TObjectPtr<UParametricRoadWorksheetButton>> CategoryButtons;
    TArray<FString> VisibleBlueprints;
    TArray<float> NumberSteps;
    FString SearchQuery, CategoryFilter, SubCategoryFilter, PendingBlueprint;
    FString LastLayerSignature;
    TArray<FParametricRoadPreset> SavedPresets;
    FString SelectedPresetId, PendingLoadPresetId;
    FText PresetReadDiagnostic, PresetReferenceError;
    int32 CurrentPage = 0, LastPointCount = INDEX_NONE, LastSelectedPoint = INDEX_NONE;
    uint32 CatalogRevision = MAX_uint32;
    float RefreshSeconds = 0;
    bool bUpdating = false, bPassive = false;
};
