#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "ParametricRoadSession.h"
#include "FGFactoryBlueprintTypes.h"
#include "Engine/Texture2D.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParametricRoadCatalogTest, "ParametricRoads.Editor.BlueprintCatalog",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParametricRoadCatalogTest::RunTest(const FString&)
{
    TStrongObjectPtr<UParametricRoadSession> State(NewObject<UParametricRoadSession>());
    TStrongObjectPtr<UFGBlueprintCategory> First(NewObject<UFGBlueprintCategory>());
    TStrongObjectPtr<UFGBlueprintCategory> Second(NewObject<UFGBlueprintCategory>());
    TStrongObjectPtr<UFGBlueprintCategory> Empty(NewObject<UFGBlueprintCategory>());
    TStrongObjectPtr<UFGBlueprintCategory> Undefined(NewObject<UFGBlueprintCategory>());
    TStrongObjectPtr<UFGBlueprintDescriptor> A(NewObject<UFGBlueprintDescriptor>());
    TStrongObjectPtr<UFGBlueprintDescriptor> B(NewObject<UFGBlueprintDescriptor>());
    TStrongObjectPtr<UFGBlueprintDescriptor> Orphan(NewObject<UFGBlueprintDescriptor>());
    TStrongObjectPtr<UTexture2D> Icon(NewObject<UTexture2D>());
    for (UFGBlueprintCategory* Category : {First.Get(), Second.Get()})
    { Category->SetDisplayNameOnInstance(FText::FromString(TEXT("Same name"))); Category->SetPriorityOnInstance(0); }
    Empty->SetDisplayNameOnInstance(FText::FromString(TEXT("Empty category"))); Empty->SetPriorityOnInstance(-10);
    Undefined->SetDisplayNameOnInstance(FText::FromString(TEXT("Native undefined")));
    Undefined->SetPriorityOnInstance(MAX_flt); Undefined->mIsUndefined = 1;
    // This native helper class does not export GetPrivateStaticClass in the
    // pinned SDK library. Use its registered class without requesting that symbol.
    UClass* SubClass = FindObject<UClass>(nullptr, TEXT("/Script/FactoryGame.FGBlueprintSubCategory"));
    if (!TestNotNull(TEXT("The native subcategory class is registered"), SubClass)) { return false; }
    auto AddSub = [SubClass](UFGBlueprintCategory* Category, const TCHAR* Label, float Order)
    {
        auto* Sub = static_cast<UFGBlueprintSubCategory*>(NewObject<UObject>(Category, SubClass));
        Sub->SetDisplayNameOnInstance(FText::FromString(Label)); Sub->SetPriorityOnInstance(Order);
        Sub->SetParentCategory(Category); Category->mSubCategories.Add(Sub);
        return Sub;
    };
    UFGBlueprintSubCategory* Later = AddSub(First.Get(), TEXT("Duplicate section"), 3);
    UFGBlueprintSubCategory* Earlier = AddSub(First.Get(), TEXT("Duplicate section"), -2);
    UFGBlueprintSubCategory* Other = AddSub(Second.Get(), TEXT("Duplicate section"), 0);
    UFGBlueprintSubCategory* Unassigned = AddSub(Undefined.Get(), TEXT("Native unassigned"), MAX_flt);
    Unassigned->mIsUndefined = 1;
    Earlier->AddBlueprintDescriptor(A.Get()); Later->AddBlueprintDescriptor(B.Get());
    const TArray<UFGBlueprintCategory*> Categories{Second.Get(), First.Get(), Undefined.Get(), Empty.Get()};

    // Descriptor implementation bodies in the SDK are stubs. Supply captured
    // metadata directly, then exercise production grouping/revision logic and
    // real inline native category accessors, without claiming native UI execution.
    TArray<FParametricRoadBlueprintEntry> Entries;
    auto Entry = [&](UFGBlueprintDescriptor* Descriptor, const TCHAR* Identity, int32 Id)
    {
        auto& Row = Entries.AddDefaulted_GetRef(); Row.Descriptor = Descriptor; Row.Name = Identity;
        Row.DisplayName = FText::FromString(TEXT("Identical display")); Row.IconId = Id;
    };
    Entry(A.Get(), TEXT("Native A"), 4); Entry(B.Get(), TEXT("Native B"), 5); Entry(Orphan.Get(), TEXT("Native orphan"), 6);
    bool bIconsReady = false; TMap<int32, UObject*> Icons;
    auto Build = [&]
    { State->RebuildBlueprintCatalog(Entries, Categories, bIconsReady, [&](int32 Id) { return bIconsReady ? Icons.FindRef(Id) : nullptr; }); };
    auto Row = [&](const FString& Name) -> const FParametricRoadBlueprintEntry*
    { return State->GetBlueprintCatalog().FindByPredicate([&](const auto& Value) { return Value.Name == Name; }); };
    Build();
    TestEqual(TEXT("Cached native categories include empty and undefined choices"), State->GetBlueprintCategories().Num(), 4);
    TestTrue(TEXT("Negative native menu priority comes first"), State->GetBlueprintCategories()[0].NativeCategory == Empty.Get());
    TestTrue(TEXT("Equal priorities retain native category array order"), State->GetBlueprintCategories()[1].NativeCategory == Second.Get());
    TestEqual(TEXT("Equal category labels have independent technical keys"),
        State->GetBlueprintCategories()[1].DisplayName.ToString(), State->GetBlueprintCategories()[2].DisplayName.ToString());
    TestNotEqual(TEXT("Displayed category names are never used as identity"),
        State->GetBlueprintCategories()[1].Key, State->GetBlueprintCategories()[2].Key);
    const auto& FirstGroup = State->GetBlueprintCategories()[2];
    TestTrue(TEXT("Subcategories follow native priorities rather than label order"), FirstGroup.SubCategories[0].NativeCategory == Earlier);
    TestNotEqual(TEXT("Equal subcategory labels retain independent keys"), FirstGroup.SubCategories[0].Key, FirstGroup.SubCategories[1].Key);
    TestTrue(TEXT("A descriptor omitted from category membership remains selectable"), Row(TEXT("Native orphan")) != nullptr);
    TestEqual(TEXT("An orphan uses the existing native undefined category"), Row(TEXT("Native orphan"))->CategoryName.ToString(), FString(TEXT("Native undefined")));
    TestEqual(TEXT("An orphan uses the existing native undefined subcategory"), Row(TEXT("Native orphan"))->SubCategoryName.ToString(), FString(TEXT("Native unassigned")));
    TestEqual(TEXT("Grouping does not mutate the native category tree"), First->mSubCategories.Num(), 2);
    TestEqual(TEXT("Grouping never inserts orphan descriptors into native save data"), Unassigned->mBlueprintDescriptors.Num(), 0);
    TestEqual(TEXT("Equal blueprint display labels preserve both native identities"), State->GetBlueprintCatalog().Num(), 3);

    uint32 Revision = State->GetBlueprintCatalogRevision();
    Build(); TestEqual(TEXT("An unchanged cached snapshot causes no UI rebuild"), State->GetBlueprintCatalogRevision(), Revision);
    Entries[0].DisplayName = FText::FromString(TEXT("Renamed display")); Build();
    TestTrue(TEXT("Same-count display edits update the revision"), State->GetBlueprintCatalogRevision() > Revision); Revision = State->GetBlueprintCatalogRevision();
    Entries[0].Description = FText::FromString(TEXT("Edited description")); Build();
    TestTrue(TEXT("Descriptions participate in content revisions"), State->GetBlueprintCatalogRevision() > Revision); Revision = State->GetBlueprintCatalogRevision();
    Entries[0].Color = FLinearColor::Red; Build();
    TestTrue(TEXT("Colours participate in content revisions"), State->GetBlueprintCatalogRevision() > Revision); Revision = State->GetBlueprintCatalogRevision();
    Entries[0].Name = TEXT("Renamed native identity"); Build();
    TestTrue(TEXT("Native identity changes are detected without a count change"), State->GetBlueprintCatalogRevision() > Revision); Revision = State->GetBlueprintCatalogRevision();
    Entries[0].IconId = 7; Build();
    TestTrue(TEXT("An icon ID change matters even while both textures are unavailable"), State->GetBlueprintCatalogRevision() > Revision); Revision = State->GetBlueprintCatalogRevision();
    bIconsReady = true; Build();
    TestTrue(TEXT("Late icon database availability refreshes the snapshot"), State->GetBlueprintCatalogRevision() > Revision); Revision = State->GetBlueprintCatalogRevision();
    Icons.Add(7, Icon.Get()); Build();
    TestTrue(TEXT("An icon texture arriving later updates unchanged descriptors"), State->GetBlueprintCatalogRevision() > Revision);
    TestTrue(TEXT("Resolved icon identity reaches the browsing entry"), Row(TEXT("Renamed native identity"))->Icon == Icon.Get()); Revision = State->GetBlueprintCatalogRevision();
    const FString OriginalCategoryKey = Row(TEXT("Renamed native identity"))->CategoryKey;
    const FString OriginalSubCategoryKey = Row(TEXT("Renamed native identity"))->SubCategoryKey;
    First->SetDisplayNameOnInstance(FText::FromString(TEXT("Changed category"))); Build();
    TestTrue(TEXT("Category display edits update rows even with equal counts"), State->GetBlueprintCatalogRevision() > Revision); Revision = State->GetBlueprintCatalogRevision();
    TestEqual(TEXT("Renaming a category preserves its browsing identity"), Row(TEXT("Renamed native identity"))->CategoryKey, OriginalCategoryKey);
    Earlier->SetDisplayNameOnInstance(FText::FromString(TEXT("Changed section"))); Build();
    TestTrue(TEXT("Subcategory display edits update the revision"), State->GetBlueprintCatalogRevision() > Revision); Revision = State->GetBlueprintCatalogRevision();
    TestEqual(TEXT("Renaming a subcategory preserves its browsing identity"), Row(TEXT("Renamed native identity"))->SubCategoryKey, OriginalSubCategoryKey);
    First->SetPriorityOnInstance(-1); Build();
    TestTrue(TEXT("Native category reordering refreshes the UI"), State->GetBlueprintCatalogRevision() > Revision); Revision = State->GetBlueprintCatalogRevision();
    Earlier->SetPriorityOnInstance(4); Build();
    TestTrue(TEXT("Native subcategory reordering refreshes the UI"), State->GetBlueprintCatalogRevision() > Revision); Revision = State->GetBlueprintCatalogRevision();
    Earlier->mBlueprintDescriptors.Reset(); Other->mBlueprintDescriptors.Add(A.Get()); Build();
    TestTrue(TEXT("Moving an existing source between categories changes its revision"), State->GetBlueprintCatalogRevision() > Revision);
    TestEqual(TEXT("Moved entries retain exact native membership"), Row(TEXT("Renamed native identity"))->CategoryName.ToString(), Second->GetCategoryNameFromInstanceAsString());
    Revision = State->GetBlueprintCatalogRevision();
    TStrongObjectPtr<UFGBlueprintDescriptor> Replacement(NewObject<UFGBlueprintDescriptor>());
    Entries[0].Descriptor = Replacement.Get(); Build();
    TestTrue(TEXT("Replacing a native descriptor under an unchanged name is detected"), State->GetBlueprintCatalogRevision() > Revision);
    Entries.RemoveAt(1); Build();
    TestNull(TEXT("A deleted cached descriptor leaves no selectable stale entry"), Row(TEXT("Native B")));
    TestEqual(TEXT("Deletion preserves other available native identities"), State->GetBlueprintCatalog().Num(), 2);
    const FFloatProperty* NativePriority = FindFProperty<FFloatProperty>(UFGBlueprintDescriptor::StaticClass(), TEXT("mMenuPriority"));
    TestNotNull(TEXT("The pinned descriptor priority is readable without generated implementation bodies"), NativePriority);
    A->SetPriorityOnInstance(-.25f);
    if (NativePriority) { TestEqual(TEXT("Native inline priority edits reach the reflected cached value"), NativePriority->GetPropertyValue_InContainer(A.Get()), -.25f); }

    State->RebuildBlueprintCatalog(Entries, {}, bIconsReady, [&](int32 Id) { return Icons.FindRef(Id); });
    TestEqual(TEXT("An uninitialized category cache still provides a local unassigned choice"), State->GetBlueprintCategories().Num(), 1);
    TestTrue(TEXT("The local fallback remains explicitly undefined"), State->GetBlueprintCategories()[0].bUndefined);
    TestEqual(TEXT("Fallback browsing keeps all available blueprints"), State->GetBlueprintCatalog().Num(), 2);
    Revision = State->GetBlueprintCatalogRevision(); State->bBlueprintCatalogDirty = false;
    State->RefreshBlueprintCatalogIfNeeded(10);
    TestEqual(TEXT("Closed configuration performs no periodic catalog work"), State->BlueprintCatalogPollSeconds, 0.0f);
    State->bLayerConfigurationOpen = true; State->RefreshBlueprintCatalogIfNeeded(.25f);
    TestEqual(TEXT("Cached comparison waits for its bounded polling interval"), State->BlueprintCatalogPollSeconds, .25f);
    State->RefreshBlueprintCatalogIfNeeded(std::numeric_limits<float>::infinity());
    TestEqual(TEXT("Nonfinite frame timing cannot poison the refresh timer"), State->BlueprintCatalogPollSeconds, .25f);
    State->OnNativeBlueprintUpdated(nullptr);
    TestTrue(TEXT("A native notification schedules automatic refresh"), State->bBlueprintCatalogDirty);
    State->RefreshBlueprintCatalogIfNeeded(0);
    TestEqual(TEXT("Unavailable world subsystems do not destroy a valid snapshot"), State->GetBlueprintCatalogRevision(), Revision);
    FProperty* Cache = FindFProperty<FProperty>(State->GetClass(), TEXT("BlueprintCatalogIcons"));
    TestNotNull(TEXT("Resolved icon objects have a reflected lifetime owner"), Cache);
    if (Cache) { TestTrue(TEXT("Icon retention is transient and never saved or replicated"),
        Cache->HasAnyPropertyFlags(CPF_Transient) && !Cache->HasAnyPropertyFlags(CPF_SaveGame | CPF_Net)); }
    TestNull(TEXT("All category/revision tests remain worldless"), State->GetWorld());
    return !HasAnyErrors();
}
#endif
