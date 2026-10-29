#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "ParametricRoadNativeHud.h"
#include "ParametricRoadNativeHints.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/Overlay.h"
#include "Components/TextBlock.h"
#include "Components/StaticMeshComponent.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParametricRoadNativeHudTest, "Roadworks.Editor.NativeHudPresentation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParametricRoadNativeHudTest::RunTest(const FString&)
{
    UWidgetBlueprintGeneratedClass* NativeClass = LoadObject<UWidgetBlueprintGeneratedClass>(nullptr, ParametricRoadNativeHints::BuildModePath);
    if (!TestNotNull(TEXT("The pinned native Build Mode widget class loads"), NativeClass)) { return false; }
    UWidget* NativeCenter = nullptr;
    TestTrue(TEXT("Actual template separates the center labels/refusals from the bottom native hints"),
        UParametricRoadNativeHud::ResolveBuildPanel(NativeClass->GetWidgetTreeArchetype(), NativeCenter));
    TestNotNull(TEXT("Actual template exposes the scoped central branch"), NativeCenter);
    // Template reflection proves the target hierarchy only. No native
    // Blueprint logic, shipping UI, gameplay, or hardware input is executed.

    TStrongObjectPtr<UWidgetTree> Fixture(NewObject<UWidgetTree>());
    UWidgetTree* Tree = Fixture.Get();
    UCanvasPanel* Root = Tree->ConstructWidget<UCanvasPanel>(); Tree->RootWidget = Root;
    UOverlay* Center = Tree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("mBuildUIPanel")); Root->AddChild(Center);
    UOverlay* Hints = Tree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("BuildDismantleHints")); Root->AddChild(Hints);
    UTextBlock* Name = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("mBuildingNameText")); Center->AddChild(Name);
    UOverlay* Mode = Tree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("mBuildModeContainer")); Center->AddChild(Mode);
    UOverlay* Refusals = Tree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("mDisqualifiers")); Center->AddChild(Refusals);
    UTextBlock* Crosshair = Tree->ConstructWidget<UTextBlock>(); Root->AddChild(Crosshair);
    UOverlay* Replacement = Tree->ConstructWidget<UOverlay>(); Root->AddChild(Replacement);
    Center->SetRenderOpacity(.37f); Hints->SetRenderOpacity(.73f);
    Crosshair->SetRenderOpacity(.42f); Replacement->SetRenderOpacity(.62f);
    Center->SetVisibility(ESlateVisibility::HitTestInvisible);
    Mode->SetVisibility(ESlateVisibility::Collapsed);
    Name->SetText(FText::FromString(TEXT("Native blueprint title")));
    const TSharedRef<SWidget> SlateRoot = Root->TakeWidget();

    UWidget* Resolved = nullptr;
    TestTrue(TEXT("The live UMG grouping fixture satisfies the same branch contract"), UParametricRoadNativeHud::ResolveBuildPanel(Tree, Resolved));
    TestEqual(TEXT("The contract selects only the center container"), Resolved, static_cast<UWidget*>(Center));
    TStrongObjectPtr<UParametricRoadNativeHud> Scope(NewObject<UParametricRoadNativeHud>());
    TestNull(TEXT("Presentation fixture has no gameplay world"), Scope->GetWorld());
    TestTrue(TEXT("The helper cannot be persisted as a saved class"), Scope->GetClass()->HasAnyClassFlags(CLASS_Transient));
    Scope->ApplyTargets({Center}, true);
    TestEqual(TEXT("Only the center container becomes transparent"), Center->GetRenderOpacity(), 0.0f);
    TestEqual(TEXT("The real Slate center receives the rendering override"), Center->GetCachedWidget()->GetRenderOpacity(), 0.0f);
    TestEqual(TEXT("Native bottom hint opacity is untouched"), Hints->GetRenderOpacity(), .73f);
    TestEqual(TEXT("Native visibility remains available to its own binding"), Center->GetVisibility(), ESlateVisibility::HitTestInvisible);
    TestEqual(TEXT("Native child visibility and mode state are untouched"), Mode->GetVisibility(), ESlateVisibility::Collapsed);
    TestEqual(TEXT("Native title data is not rewritten"), Name->GetText().ToString(), FString(TEXT("Native blueprint title")));
    Scope->ApplyTargets({Center}, true); Scope->Clear(); Scope->Clear();
    TestEqual(TEXT("Repeated discovery and release restore the original nondefault opacity"), Center->GetRenderOpacity(), .37f);

    Scope->ApplyTargets({Center, Crosshair}, true);
    Scope->ApplyTargets({Center, Crosshair}, false);
    TestEqual(TEXT("Closing expanded configuration restores its crosshair independently"), Crosshair->GetRenderOpacity(), .42f);
    TestEqual(TEXT("Compact mode restores its native placement title, build mode and refusal branch"), Center->GetRenderOpacity(), .37f);
    Scope->ApplyTargets({Replacement}, true);
    TestEqual(TEXT("Replacing a native widget restores the previous instance"), Center->GetRenderOpacity(), .37f);
    TestEqual(TEXT("The new native widget receives the scoped override"), Replacement->GetRenderOpacity(), 0.0f);
    Scope->Clear();
    TestEqual(TEXT("The replacement instance also restores its own exact opacity"), Replacement->GetRenderOpacity(), .62f);

    Scope->ApplyTargets({Center, Crosshair}, false);
    TestEqual(TEXT("Compact discovery cannot suppress a newly encountered placement panel"), Center->GetRenderOpacity(), .37f);
    TestEqual(TEXT("Compact discovery cannot suppress a newly encountered crosshair"), Crosshair->GetRenderOpacity(), .42f);
    TestTrue(TEXT("Compact mode owns no widget opacity overrides"), Scope->Suppressed.IsEmpty());

    Scope->ApplyTargets({Center}, true); Center->SetRenderOpacity(.91f); Scope->Clear();
    TestEqual(TEXT("Teardown does not overwrite an opacity changed by another writer"), Center->GetRenderOpacity(), .91f);
    Scope->ApplyTargets({Center}, true); Scope->Update(nullptr, false);
    TestEqual(TEXT("Losing the local owner restores an outstanding override"), Center->GetRenderOpacity(), .91f);
    TestTrue(TEXT("No weak targets remain after owner loss"), Scope->Suppressed.IsEmpty());
    Root->RemoveChild(Hints); Center->AddChild(Hints);
    TestFalse(TEXT("A changed native layout cannot accidentally hide its shortcut branch"), UParametricRoadNativeHud::ResolveBuildPanel(Tree, Resolved));

    TStrongObjectPtr<UStaticMeshComponent> Arms(NewObject<UStaticMeshComponent>());
    TStrongObjectPtr<UStaticMeshComponent> Tool(NewObject<UStaticMeshComponent>());
    TStrongObjectPtr<UStaticMeshComponent> Other(NewObject<UStaticMeshComponent>());
    TArray<TWeakObjectPtr<UPrimitiveComponent>> ViewHidden{Other.Get()};
    TArray<TWeakObjectPtr<UPrimitiveComponent>> Owned;
    const TArray<TWeakObjectPtr<UPrimitiveComponent>> AnotherView = ViewHidden;
    UParametricRoadNativeHud::ApplyViewTargets(ViewHidden, Owned, {Arms.Get(), Tool.Get(), Other.Get()});
    TestEqual(TEXT("The local view excludes arms and tool alongside an existing exclusion"), ViewHidden.Num(), 3);
    TestEqual(TEXT("Only newly owned view exclusions are tracked"), Owned.Num(), 2);
    TestFalse(TEXT("A view exclusion never changes the arms component hidden flag"), bool(Arms->bHiddenInGame));
    TestTrue(TEXT("A view exclusion leaves component visibility and gameplay active"), Tool->IsVisible());
    TestEqual(TEXT("Another player's view exclusion list is unchanged"), AnotherView.Num(), 1);
    UParametricRoadNativeHud::ApplyViewTargets(ViewHidden, Owned, {Arms.Get(), Tool.Get()});
    TestEqual(TEXT("Repeated expanded updates add no duplicate exclusion"), ViewHidden.Num(), 3);
    UParametricRoadNativeHud::ApplyViewTargets(ViewHidden, Owned, {Tool.Get()});
    TestFalse(TEXT("Replacing an arms mesh restores the previous view membership"), ViewHidden.Contains(Arms.Get()));
    TestTrue(TEXT("A preexisting exclusion remains untouched during reconciliation"), ViewHidden.Contains(Other.Get()));
    UParametricRoadNativeHud::ApplyViewTargets(ViewHidden, Owned, {});
    TestEqual(TEXT("Close removes only our view exclusions"), ViewHidden.Num(), 1);
    TestTrue(TEXT("The original external exclusion survives close"), ViewHidden.Contains(Other.Get()));
    TestTrue(TEXT("No owned view resources remain after close"), Owned.IsEmpty());
    return !HasAnyErrors();
}

#endif
