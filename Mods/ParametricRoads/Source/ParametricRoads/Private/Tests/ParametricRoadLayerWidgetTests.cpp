#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "ParametricRoadLayerWidget.h"
#include "ParametricRoadSession.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Components/CheckBox.h"
#include "Components/WidgetSwitcher.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "Input/HittestGrid.h"
#include "Widgets/SVirtualWindow.h"
#include "Misc/ScopeExit.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RenderingThread.h"
#include "Serialization/BufferArchive.h"
#include "Slate/WidgetRenderer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "UObject/StrongObjectPtr.h"

// Real native widgets and virtual Slate input, with no world, player, equipment,
// modal game stack or construction. Live-session editing is tested separately.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParametricRoadLayerWidgetTest, "ParametricRoads.Editor.LayerWidget",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FParametricRoadLayerWidgetTest::RunTest(const FString&)
{
    if (!FSlateApplication::IsInitialized()) return false;
    TStrongObjectPtr<UParametricRoadSession> State(NewObject<UParametricRoadSession>());
    State->bAssessed = true; State->Phase = EParametricRoadPhase::Ready; State->BlueprintName = TEXT("DEMO 1");
    FParametricRoadWaypoint P; P.Point.Pose = FTransform(FVector(1000, 0, 0)); State->IntermediatePoints.Add(P); State->SelectPoint(1);
    auto& Cat = State->BlueprintCategories.AddDefaulted_GetRef(); Cat.Key = TEXT("native-cat"); Cat.DisplayName = FText::FromString(TEXT("Structures"));
    auto& Sub = Cat.SubCategories.AddDefaulted_GetRef(); Sub.Key = TEXT("native-sub"); Sub.DisplayName = FText::FromString(TEXT("Pillars"));
    for (int32 I = 0; I < 18; ++I)
    {
        auto& E = State->BlueprintCatalog.AddDefaulted_GetRef(); E.Name = FString::Printf(TEXT("native-%02d"), I);
        E.DisplayName = FText::FromString(I < 2 ? TEXT("Same displayed name") : FString::Printf(TEXT("Pillar %02d"), I));
        E.CategoryKey = Cat.Key; E.CategoryName = Cat.DisplayName; E.SubCategoryKey = Sub.Key; E.SubCategoryName = Sub.DisplayName;
    }
    State->SelectDecorationBlueprint(State->BlueprintCatalog[0].Name); State->SetLayerEnabled(true);
    TStrongObjectPtr<UParametricRoadLayerWidget> Widget(NewObject<UParametricRoadLayerWidget>());
    Widget->Initialize(); Widget->SetSession(State.Get()); TSharedRef<SWidget> Slate = Widget->TakeWidget();
    TestNull(TEXT("Configuration remains worldless"), State->GetWorld());
    TestFalse(TEXT("Visible content is never a viewport overlay"), Widget->IsInViewport());
    TestEqual(TEXT("Expanded terminal opens on the route worksheet"), Widget->Pages->GetActiveWidgetIndex(), 0);
    TestEqual(TEXT("Window title does not repeat the blueprint name"), Widget->Title->GetText().ToString(), FString(TEXT("Roadworks")));
    TestEqual(TEXT("Window chrome and its descendants are excluded from native hit testing"), Widget->HeaderChrome->GetVisibility(), ESlateVisibility::HitTestInvisible);
    TArray<UWidget*> AllWidgets; Widget->WidgetTree->GetAllWidgets(AllWidgets);
    for (const TCHAR* Name : {TEXT("File"), TEXT("Edit"), TEXT("View"), TEXT("Tools"), TEXT("Help")})
    {
        int32 Decorations = 0;
        for (auto* NativeWidget : AllWidgets)
        {
            auto* Label = Cast<UTextBlock>(NativeWidget);
            if (!Label || Label->GetText().ToString() != Name) continue;
            bool InButton = false;
            for (UWidget* Parent = Label->GetParent(); Parent; Parent = Parent->GetParent()) InButton |= Parent->IsA<UButton>();
            if (!InButton) ++Decorations;
        }
        TestTrue(FString::Printf(TEXT("%s is decorative window chrome without a clickable button"), Name), Decorations > 0);
    }
    for (int32 Menu = 0; Menu < 5; ++Menu) Widget->Activate(2, Menu);
    TestEqual(TEXT("Obsolete menu dispatch cannot navigate or reveal commands"), Widget->CurrentPage, 0);
    TestEqual(TEXT("Every configured point has a real native selector"), Widget->PointButtons.Num(), 3);
    TestEqual(TEXT("Every adjustment and layer value has an editor"), Widget->Numbers.Num(), 17);
    for (const auto& Spin : Widget->Numbers)
        if (Spin) TestFalse(TEXT("No number exposes range-dependent mouse dragging"), Spin->GetEnableSlider());
    FWidgetRenderer Renderer(false); const FVector2D DrawSize(1024, 688);
    TSharedRef<SVirtualWindow> Window = SNew(SVirtualWindow).Size(DrawSize); Window->SetIsFocusable(true); Window->SetContent(Slate);
    FSlateApplication::Get().RegisterVirtualWindow(Window);
    ON_SCOPE_EXIT { FSlateApplication::Get().UnregisterVirtualWindow(Window); };
    TStrongObjectPtr<UTextureRenderTarget2D> Target(FWidgetRenderer::CreateTargetFor(DrawSize, TF_Bilinear, false));
    const FString Folder = FPaths::ProjectSavedDir() / TEXT("ParametricRoadsValidation"); IFileManager::Get().MakeDirectory(*Folder, true);
    const auto Draw = [&]()
    {
        Widget->RefreshDisplay();
        for (int32 Pass = 0; Pass < 4; ++Pass) Renderer.DrawWindow(Target.Get(), Window->GetHittestGrid(), Window, 1, DrawSize, 1.f / 60);
        FlushRenderingCommands();
    };
    const auto Capture = [&](const TCHAR* Name, const TArray<UWidget*>& Controls)
    {
        Draw(); FBufferArchive PNG;
        TestTrue(TEXT("Offscreen worksheet exports"), FImageUtils::ExportRenderTarget2DAsPNG(Target.Get(), PNG));
        TestTrue(TEXT("Offscreen worksheet is saved"), FFileHelper::SaveArrayToFile(PNG, *(Folder / Name)));
        const auto Extent = Widget->Surface->GetCachedGeometry().GetAbsoluteSize();
        TestTrue(TEXT("Worksheet fills its physical render target"), Extent.Equals(DrawSize, 1));
        for (auto* C : Controls)
        {
            const auto& G = C->GetCachedGeometry(); const auto Pos = G.GetAbsolutePosition(), Size = G.GetAbsoluteSize();
            TestTrue(FString::Printf(TEXT("%s has a usable hit area"), *C->GetName()), Size.X > 24 && Size.Y > 18);
            TestTrue(FString::Printf(TEXT("%s stays inside the physical screen"), *C->GetName()),
                Pos.X >= -1 && Pos.Y >= -1 && Pos.X + Size.X <= DrawSize.X + 1 && Pos.Y + Size.Y <= DrawSize.Y + 1);
        }
    };
    Capture(TEXT("worksheet-route.png"), {Widget->PointTree, Widget->Numbers[0], Widget->Numbers[10], Widget->Axis});
    const auto User = FSlateApplication::Get().FindOrCreateVirtualUser(90321);
    const auto Click = [&](UWidget* Control)
    {
        if (!TestNotNull(TEXT("Native click target exists"), Control)) return;
        const auto& G = Control->GetCachedGeometry(); const auto Pos = G.LocalToAbsolute(G.GetLocalSize() * .5);
        auto Widgets = Window->GetHittestGrid().GetBubblePath(Pos, 0, false, INDEX_NONE);
        TestTrue(TEXT("Real native hit path contains the intended control"), Widgets.ContainsByPredicate([&](const auto& E) { return E.Widget == Control->GetCachedWidget(); }));
        FWidgetPath Path(Widgets); TSet<FKey> Held;
        FSlateApplication::Get().RoutePointerMoveEvent(Path, FPointerEvent(User->GetUserIndex(), 0, Pos, Pos, Held, FKey(), 0, FModifierKeysState()), false);
        Held.Add(EKeys::LeftMouseButton);
        FSlateApplication::Get().RoutePointerDownEvent(Path, FPointerEvent(User->GetUserIndex(), 0, Pos, Pos, Held, EKeys::LeftMouseButton, 0, FModifierKeysState()));
        Held.Reset(); FSlateApplication::Get().RoutePointerUpEvent(Path, FPointerEvent(User->GetUserIndex(), 0, Pos, Pos, Held, EKeys::LeftMouseButton, 0, FModifierKeysState()));
    };
    TestTrue(TEXT("Point selection fills the fixed hierarchy column"),Widget->PointButtons[0]->GetCachedGeometry().GetLocalSize().X>Widget->PointTree->GetCachedGeometry().GetLocalSize().X*.8);
    const int32 SelectedBeforeDecoration = State->GetSelectedPointIndex();
    Widget->Activate(999); Widget->Activate(1000,0,TEXT("native-cat")); Draw();
    TestEqual(TEXT("Old disclosure commands no longer hide route points"),Widget->PointTree->GetVisibility(),ESlateVisibility::Visible);
    TestEqual(TEXT("Fixed tree has one continuous guide for every point row"),Widget->PointGuides->GetRows(),3);
    Widget->SetSession(State.Get()); Draw();
    TestEqual(TEXT("Fixed tree preserves point selection"),State->GetSelectedPointIndex(),SelectedBeforeDecoration);
    TestEqual(TEXT("Decorative menu is aligned to the right"),Widget->HeaderChrome->GetCachedGeometry().GetAbsolutePosition().X>650,true);
    TestTrue(TEXT("Functional close is a separate hit target outside decorative menu"),Widget->CloseButton->GetParent()!=Widget->HeaderChrome);
    State->bFineAdjustment = true; Draw();
    TestTrue(TEXT("Fine-step readout reflects a live enabled precision state"), Widget->Fine->IsChecked());
    State->bFineAdjustment = false; Draw();
    TestFalse(TEXT("Fine-step readout reflects a live disabled precision state"), Widget->Fine->IsChecked());
    Widget->SetPage(1); Capture(TEXT("worksheet-layers.png"), {Widget->LayerTree, Widget->Numbers[12], Widget->Numbers[13], Widget->Numbers[14], Widget->Numbers[16], Widget->FollowBank, Widget->AddLayerButton});
    Widget->SetPage(2); Capture(TEXT("worksheet-library.png"), {Widget->Search, Widget->Catalog, Widget->UseBlueprintButton, Widget->CategoryTree});
    TestEqual(TEXT("Native catalog identities are retained"), Widget->CatalogButtons.Num(), 18);
    TestEqual(TEXT("Fixed hierarchy always exposes category and subcategory"),Widget->CategoryButtons.Num(),3);
    Click(Widget->CategoryButtons.Last()); Draw();
    TestEqual(TEXT("Subcategory selection retains native category identity"), Widget->CategoryFilter, FString(TEXT("native-cat")));
    TestEqual(TEXT("Subcategory selection retains native subcategory identity"), Widget->SubCategoryFilter, FString(TEXT("native-sub")));
    TestEqual(TEXT("Filtering never hides hierarchy entries"),Widget->CategoryButtons.Num(),3);
    Click(Widget->CategoryButtons[0]); Draw();
    TestTrue(TEXT("The All row clears both category filters"), Widget->CategoryFilter.IsEmpty() && Widget->SubCategoryFilter.IsEmpty());
    Widget->PendingBlueprint.Reset(); Widget->UpdateBlueprintPreview();
    TestTrue(TEXT("Empty blueprint preview has no filler paragraph"), Widget->PreviewDescription->GetText().IsEmpty());
    AllWidgets.Reset(); Widget->WidgetTree->GetAllWidgets(AllWidgets);
    for (auto* NativeWidget : AllWidgets)
        if (const auto* Label = Cast<UTextBlock>(NativeWidget))
        {
            const FString Text = Label->GetText().ToString();
            TestFalse(TEXT("Tree connections are vectors rather than Unicode text art"), Text.Contains(TEXT("\u251c")) || Text.Contains(TEXT("\u2514")) || Text.Contains(TEXT("\u25cb")));
            TestFalse(TEXT("The worksheet does not repeat obsolete explanatory filler"), Text.Contains(TEXT("Choose a blueprint from the library, then")) || Text.Contains(TEXT("Use Add")));
        }
    Click(Widget->CatalogButtons[1]); Draw();
    TestEqual(TEXT("Browsing only changes pending preview"), State->GetLayerSettings().BlueprintName, State->BlueprintCatalog[0].Name);
    TestEqual(TEXT("Equal displayed names keep separate pending identities"), Widget->PendingBlueprint, State->BlueprintCatalog[1].Name);
    Click(Widget->UseBlueprintButton); Draw();
    TestEqual(TEXT("Use blueprint assigns exact native identity"), State->GetLayerSettings().BlueprintName, State->BlueprintCatalog[1].Name);
    TestEqual(TEXT("Assigning a source returns to its layer"), Widget->CurrentPage, 1);
    const auto Key = [&](FKey K, FModifierKeysState Modifiers = FModifierKeysState())
    {
        FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(K, Modifiers, User->GetUserIndex(), false, 0, 0));
        FSlateApplication::Get().ProcessKeyUpEvent(FKeyEvent(K, Modifiers, User->GetUserIndex(), false, 0, 0));
    };
    const auto Type = [&](UWidget* Control, const FString& Value)
    {
        Click(Control); Key(EKeys::A, FModifierKeysState(false, false, true, false, false, false, false, false, false));
        for (TCHAR C : Value) FSlateApplication::Get().ProcessKeyCharEvent(FCharacterEvent(C, FModifierKeysState(), User->GetUserIndex(), false));
        const auto Focus = FSlateApplication::Get().GetUserFocusedWidget(User->GetUserIndex()); Widget->RefreshDisplay();
        TestTrue(TEXT("Readout refresh preserves a live virtual numeric/text editor"), Focus.IsValid() && Focus == FSlateApplication::Get().GetUserFocusedWidget(User->GetUserIndex()));
        Key(EKeys::Enter);
    };
    State->SetLayerOffset(2, 1.23456789); Draw();
    const int32 RevisionBeforeFocusLoss = State->SolutionRevision;
    Click(Widget->Numbers[16]); Draw(); Click(Widget->Numbers[14]); Draw();
    TestEqual(TEXT("Leaving an unchanged rounded field preserves full canonical precision"),State->GetLayerSettings().OffsetMetres.Z,1.23456789);
    TestEqual(TEXT("An unchanged numeric focus-loss commit does not recalculate the route"),State->SolutionRevision,RevisionBeforeFocusLoss);
    Type(Widget->Numbers[15], TEXT("-3.25")); Draw();
    TestEqual(TEXT("Native typed number commits an exact signed offset"), State->GetLayerSettings().OffsetMetres.Y, -3.25);
    const auto Before = State->GetLayerSettings().SpacingMetres;
    // The explicit + native button lives after the spin editor in its row.
    auto* Plus = CastChecked<UParametricRoadWorksheetButton>(CastChecked<UContentWidget>(
        CastChecked<UPanelWidget>(Widget->Numbers[12]->GetParent())->GetChildAt(2))->GetContent());
    Click(Plus); Draw(); TestEqual(TEXT("One explicit step changes metres by .5 independent of the full range"), State->GetLayerSettings().SpacingMetres, Before + .5);
    Click(Widget->AddLayerButton); Draw(); TestEqual(TEXT("Add creates an independent layer"), State->GetLayerCount(), 2);
    TestTrue(TEXT("New layer has zero local offsets"), State->GetLayerSettings().OffsetMetres.IsZero());
    Click(Widget->LayerButtons[0]); Draw(); TestEqual(TEXT("Native tree restores the first layer"), State->GetSelectedLayerIndex(), 0);
    TestEqual(TEXT("Independent offset is restored"), Widget->Numbers[15]->GetValue(), -3.25f);
    Widget->SetPage(2); Draw(); Type(Widget->Search, TEXT("17")); Draw();
    TestEqual(TEXT("Native translated typing filters the catalog"), Widget->CatalogButtons.Num(), 1);
    TestEqual(TEXT("Filtered result uses identity rather than original row index"), Widget->VisibleBlueprints[0], State->BlueprintCatalog[17].Name);
    Widget->CategoryFilter = TEXT("no-category"); Widget->RebuildCatalog();
    TestEqual(TEXT("Category filtering composes with search"), Widget->CatalogButtons.Num(), 0);
    Widget->CategoryFilter.Reset(); Widget->Search->SetText(FText::GetEmpty()); Widget->SearchChanged(FText::GetEmpty());
    State->BlueprintCatalog[1].DisplayName = FText::FromString(TEXT("Renamed blueprint")); ++State->BlueprintCatalogRevision;
    Widget->RefreshDisplay(); TestEqual(TEXT("Same-count catalog revisions update native rows"), Widget->CatalogButtons.Num(), 18);
    Widget->SetPage(1); Draw();
    for (int32 Expected = State->GetLayerCount() + 1; Expected <= UParametricRoadSession::MaximumLayers; ++Expected)
    {
        Click(Widget->AddLayerButton); Draw();
        if (!TestEqual(TEXT("Each native Add click appends exactly one independent layer"), State->GetLayerCount(), Expected)) return false;
    }
    Capture(TEXT("worksheet-eight-layers.png"), {Widget->LayerButtons[0], Widget->LayerButtons.Last(), Widget->Numbers[16]});
    TestFalse(TEXT("Layer limit is explicit"), Widget->AddLayerButton->GetIsEnabled());
    // Preset browsing is native UI only. Inject saved compositions rather than
    // reading or writing the player's preset directory from a worldless test.
    FParametricRoadPreset FirstPreset = State->CapturePreset(TEXT("Same preset name"));
    FirstPreset.Id = TEXT("11111111-1111-4111-8111-111111111111");
    FParametricRoadPreset SecondPreset = FirstPreset; SecondPreset.Id = TEXT("22222222-2222-4222-8222-222222222222");
    SecondPreset.RoadBlueprintName = TEXT("Other road");
    Widget->SavedPresets = {FirstPreset, SecondPreset}; Widget->RebuildPresetList();
    Widget->SetPage(3); Draw();
    TestEqual(TEXT("Preset management is a separate page of the physical worksheet"), Widget->Pages->GetActiveWidgetIndex(), 3);
    TestEqual(TEXT("Worldless page entry retains the injected list without touching disk"), Widget->SavedPresets.Num(), 2);
    Click(Widget->PresetButtons[1]); Draw();
    TestEqual(TEXT("Equal preset names retain separate GUID selections"), Widget->SelectedPresetId, SecondPreset.Id);
    TestEqual(TEXT("Preset selection fills the editable name"), Widget->PresetName->GetText().ToString(), SecondPreset.Name);
    TestTrue(TEXT("The composition contains independently configured layers"), Widget->PresetDetails->GetChildrenCount() > SecondPreset.Layers.Num());
    Widget->PresetName->SetText(FText::FromString(TEXT("Edited preset 3"))); Draw();
    TestEqual(TEXT("Timer synchronization does not overwrite an edited preset name"), Widget->PresetName->GetText().ToString(), FString(TEXT("Edited preset 3")));
    TestFalse(TEXT("Saving requires an owned live terminal"), Widget->SaveNewPresetButton->GetIsEnabled());
    TestFalse(TEXT("Updating requires an owned live terminal"), Widget->UpdatePresetButton->GetIsEnabled());
    TestFalse(TEXT("Loading requires an owned live terminal"), Widget->LoadPresetButton->GetIsEnabled());
    Widget->PendingLoadPresetId = SecondPreset.Id;
    Widget->PresetConfirmationText->SetText(FText::FromString(TEXT("Replace the current route preview?")));
    Widget->PresetConfirmation->SetVisibility(ESlateVisibility::Visible); Draw();
    Capture(TEXT("worksheet-presets.png"), {Widget->PresetList, Widget->PresetName, Widget->PresetDetails,
        Widget->SaveNewPresetButton, Widget->UpdatePresetButton, Widget->LoadPresetButton});
    Widget->Activate(21); Draw();
    TestTrue(TEXT("Cancel discards only the pending replacement confirmation"), Widget->PendingLoadPresetId.IsEmpty());
    TestEqual(TEXT("Cancel collapses the inline confirmation"), Widget->PresetConfirmation->GetVisibility(), ESlateVisibility::Collapsed);
    TestEqual(TEXT("Preset browsing preserves point selection"), State->GetSelectedPointIndex(), 1);
    TestEqual(TEXT("Preset browsing preserves the current route"), State->Phase, EParametricRoadPhase::Ready);
    TestEqual(TEXT("Preset browsing preserves all independent layers"), State->GetLayerCount(), UParametricRoadSession::MaximumLayers);
    Widget->SetPage(1); Draw();
    TestEqual(TEXT("Editing layers preserves point selection"), State->GetSelectedPointIndex(), 1);
    TestEqual(TEXT("Editing layers preserves placement phase"), State->Phase, EParametricRoadPhase::Ready);
    TestFalse(TEXT("All rendering remained outside the player viewport"), Widget->IsInViewport());
    const auto PhaseBeforeClose=State->Phase;
    const int32 PointsBeforeClose=State->GetPointCount(), LayersBeforeClose=State->GetLayerCount();
    State->bLayerConfigurationOpen=true; State->LayerWidget=Widget.Get();
    Draw(); Click(Widget->CloseButton);
    TestFalse(TEXT("Real native close-cross click closes configuration"),State->IsLayerConfigurationOpen());
    TestEqual(TEXT("Closing preserves route phase"),State->Phase,PhaseBeforeClose);
    TestEqual(TEXT("Closing preserves all points"),State->GetPointCount(),PointsBeforeClose);
    TestEqual(TEXT("Closing preserves all layers"),State->GetLayerCount(),LayersBeforeClose);
    return true;
}
#endif
