#include "ParametricRoadSession.h"
#include "FGBlueprintSubsystem.h"
#include "FGFactoryBlueprintTypes.h"
#include "FGIconDatabaseSubsystem.h"
#include "UObject/UnrealType.h"

namespace
{
    constexpr float CatalogPollInterval = .5f;
    const FString UnassignedKey(TEXT("catalog:unassigned"));
    float Priority(float Value) { return FMath::IsFinite(Value) ? Value : MAX_flt; }
    FText UnassignedName() { return NSLOCTEXT("Roadworks", "CatalogUnassigned", "Unassigned"); }
    FString CategoryKey(const UObject* Object) { return TEXT("category:") + Object->GetPathName(); }
    FString SubCategoryKey(const FString& Parent, const UObject* Object)
    { return Parent + TEXT("/subcategory:") + Object->GetPathName(); }
    bool SameText(const FText& A, const FText& B) { return A.ToString() == B.ToString(); }
    bool SameEntry(const FParametricRoadBlueprintEntry& A, const FParametricRoadBlueprintEntry& B)
    {
        return A.Name == B.Name && SameText(A.DisplayName, B.DisplayName) && SameText(A.Description, B.Description)
            && A.Icon == B.Icon && A.IconId == B.IconId && A.Color == B.Color && A.Descriptor == B.Descriptor
            && A.MenuPriority == B.MenuPriority && A.CategoryKey == B.CategoryKey && A.SubCategoryKey == B.SubCategoryKey
            && SameText(A.CategoryName, B.CategoryName) && SameText(A.SubCategoryName, B.SubCategoryName)
            && A.CategoryOrder == B.CategoryOrder && A.SubCategoryOrder == B.SubCategoryOrder;
    }
    bool SameSubCategory(const FParametricRoadBlueprintSubCategory& A, const FParametricRoadBlueprintSubCategory& B)
    {
        return A.Key == B.Key && SameText(A.DisplayName, B.DisplayName) && A.Order == B.Order
            && A.MenuPriority == B.MenuPriority && A.bUndefined == B.bUndefined && A.NativeCategory == B.NativeCategory;
    }
    template<typename T, typename Equal>
    bool SameArray(const TArray<T>& A, const TArray<T>& B, Equal Predicate)
    {
        if (A.Num() != B.Num()) { return false; }
        for (int32 I = 0; I < A.Num(); ++I) { if (!Predicate(A[I], B[I])) { return false; } }
        return true;
    }
    bool SameCategory(const FParametricRoadBlueprintCategory& A, const FParametricRoadBlueprintCategory& B)
    {
        return A.Key == B.Key && SameText(A.DisplayName, B.DisplayName) && A.Icon == B.Icon && A.IconId == B.IconId
            && A.Order == B.Order && A.MenuPriority == B.MenuPriority && A.bUndefined == B.bUndefined
            && A.NativeCategory == B.NativeCategory && SameArray(A.SubCategories, B.SubCategories, SameSubCategory);
    }
}

void UParametricRoadSession::RebuildBlueprintCatalog(TArray<FParametricRoadBlueprintEntry> Entries,
    TConstArrayView<UFGBlueprintCategory*> Categories, bool bIconsReady, TFunctionRef<UObject*(int32)> ResolveIcon)
{
    TArray<UFGBlueprintCategory*> Ordered;
    for (UFGBlueprintCategory* Category : Categories) { if (IsValid(Category)) { Ordered.AddUnique(Category); } }
    Ordered.StableSort([](const UFGBlueprintCategory& A, const UFGBlueprintCategory& B)
    { return Priority(A.GetMenuPriorityFromInstance()) < Priority(B.GetMenuPriorityFromInstance()); });
    TArray<FParametricRoadBlueprintCategory> Groups;
    TMap<UFGBlueprintDescriptor*, FIntPoint> Placement;
    FIntPoint Undefined(INDEX_NONE, INDEX_NONE);
    for (UFGBlueprintCategory* Native : Ordered)
    {
        const int32 CategoryIndex = Groups.Num();
        FParametricRoadBlueprintCategory& Category = Groups.AddDefaulted_GetRef();
        Category.Key = CategoryKey(Native); Category.DisplayName = Native->GetCategoryNameFromInstance();
        Category.NativeCategory = Native; Category.Order = CategoryIndex;
        Category.MenuPriority = Priority(Native->GetMenuPriorityFromInstance());
        Category.bUndefined = Native->IsUndefined(); Category.IconId = Native->GetIconIdFromInstance();
        Category.Icon = ResolveIcon(Category.IconId);
        if (Category.bUndefined && Undefined.X == INDEX_NONE) { Undefined.X = CategoryIndex; }
        TArray<UFGBlueprintSubCategory*> NativeSubs, Subs;
        Native->GetSubCategories(NativeSubs); // Pinned inline copies cached objects; never generates categories.
        for (UFGBlueprintSubCategory* Sub : NativeSubs) { if (IsValid(Sub)) { Subs.AddUnique(Sub); } }
        Subs.StableSort([](const UFGBlueprintSubCategory& A, const UFGBlueprintSubCategory& B)
        { return Priority(A.GetMenuPriorityFromInstance()) < Priority(B.GetMenuPriorityFromInstance()); });
        for (UFGBlueprintSubCategory* NativeSub : Subs)
        {
            const int32 SubIndex = Category.SubCategories.Num();
            FParametricRoadBlueprintSubCategory& Sub = Category.SubCategories.AddDefaulted_GetRef();
            Sub.Key = SubCategoryKey(Category.Key, NativeSub); Sub.DisplayName = NativeSub->GetCategoryNameFromInstance();
            Sub.Order = SubIndex; Sub.MenuPriority = Priority(NativeSub->GetMenuPriorityFromInstance());
            Sub.bUndefined = NativeSub->IsUndefined(); Sub.NativeCategory = NativeSub;
            if (Category.bUndefined && Sub.bUndefined && Undefined.X == CategoryIndex && Undefined.Y == INDEX_NONE)
            { Undefined.Y = SubIndex; }
            for (UFGBlueprintDescriptor* Descriptor : NativeSub->GetBlueprintDescriptors())
            { if (IsValid(Descriptor) && !Placement.Contains(Descriptor)) { Placement.Add(Descriptor, FIntPoint(CategoryIndex, SubIndex)); } }
        }
    }
    // Preserve native Undefined objects if present. A local fallback is only a
    // browsing record: the native GetSubCategoryForUndefined can mutate the save.
    if (Undefined.X == INDEX_NONE)
    {
        Undefined.X = Groups.Num();
        auto& Category = Groups.AddDefaulted_GetRef();
        Category.Key = UnassignedKey; Category.DisplayName = UnassignedName();
        Category.Order = Undefined.X; Category.bUndefined = true;
    }
    if (Undefined.Y == INDEX_NONE)
    {
        auto& Category = Groups[Undefined.X]; Undefined.Y = Category.SubCategories.Num();
        auto& Sub = Category.SubCategories.AddDefaulted_GetRef();
        Sub.Key = Category.Key + TEXT("/unassigned"); Sub.DisplayName = UnassignedName();
        Sub.Order = Undefined.Y; Sub.bUndefined = true;
    }
    for (auto& Entry : Entries)
    {
        const FIntPoint* Assigned = Placement.Find(Entry.Descriptor.Get());
        const FIntPoint Indices = Assigned ? *Assigned : Undefined;
        const auto& Category = Groups[Indices.X]; const auto& Sub = Category.SubCategories[Indices.Y];
        Entry.CategoryKey = Category.Key; Entry.CategoryName = Category.DisplayName; Entry.CategoryOrder = Category.Order;
        Entry.SubCategoryKey = Sub.Key; Entry.SubCategoryName = Sub.DisplayName; Entry.SubCategoryOrder = Sub.Order;
        Entry.MenuPriority = Priority(Entry.MenuPriority); Entry.Icon = ResolveIcon(Entry.IconId);
    }
    Entries.StableSort([](const auto& A, const auto& B)
    {
        if (A.CategoryOrder != B.CategoryOrder) { return A.CategoryOrder < B.CategoryOrder; }
        if (A.SubCategoryOrder != B.SubCategoryOrder) { return A.SubCategoryOrder < B.SubCategoryOrder; }
        return A.MenuPriority < B.MenuPriority;
    });
    if (bBlueprintCatalogIconsReady != bIconsReady || !SameArray(BlueprintCatalog, Entries, SameEntry)
        || !SameArray(BlueprintCategories, Groups, SameCategory))
    {
        BlueprintCatalog = MoveTemp(Entries); BlueprintCategories = MoveTemp(Groups);
        bBlueprintCatalogIconsReady = bIconsReady;
        ++BlueprintCatalogRevision;
    }
}

void UParametricRoadSession::RefreshBlueprintCatalog()
{
    UWorld* World = GetWorld();
    AFGBlueprintSubsystem* Subsystem = World ? AFGBlueprintSubsystem::GetBlueprintSubsystem(this) : nullptr;
    if (!Subsystem) { bBlueprintCatalogDirty = true; return; }
    TArray<UFGBlueprintDescriptor*> Descriptors;
    TArray<UFGBlueprintCategory*> Categories;
    // These exact pinned inline APIs only copy the subsystem's cached arrays.
    // Do not call RefreshBlueprintsAndDescriptors or other filesystem refreshes.
    Subsystem->GetBlueprintDescriptors_Internal(Descriptors);
    Subsystem->GetAllBlueprintCategories(Categories);
    AFGIconDatabaseSubsystem* Icons = AFGIconDatabaseSubsystem::Get(World);
    if (BlueprintCatalogIconDatabase.Get() != Icons)
    {
        BlueprintCatalogIconDatabase = Icons; BlueprintCatalogIcons.Reset(); BlueprintCatalogIconPaths.Reset();
    }
    const bool bIconsReady = Icons && Icons->IsInitialized();
    TMap<int32, FIconData> IconSources;
    if (bIconsReady) { for (const FIconData& Data : Icons->GetAllIconData()) { IconSources.Add(Data.ID, Data); } }
    auto ResolveIcon = [&](int32 Id) -> UObject*
    {
        if (!bIconsReady || Id == INDEX_NONE) { return nullptr; }
        const FIconData* Source = IconSources.Find(Id);
        const FString Path = Source ? Source->Texture.ToSoftObjectPath().ToString()
            + TEXT("|") + Source->ItemDescriptor.ToSoftObjectPath().ToString() : FString();
        if (Source && Source->Texture.Get())
        { BlueprintCatalogIconPaths.Add(Id, Path); BlueprintCatalogIcons.Add(Id, Source->Texture.Get()); return Source->Texture.Get(); }
        const FString* Previous = BlueprintCatalogIconPaths.Find(Id);
        if (Previous && *Previous == Path) { return BlueprintCatalogIcons.FindRef(Id).Get(); }
        // Native texture resolution is attempted only for a new/changed source,
        // not on every periodic comparison. Loaded soft references can arrive later.
        UObject* Texture = Icons->GetIconTextureFromIconID(Id);
        BlueprintCatalogIconPaths.Add(Id, Path); BlueprintCatalogIcons.Add(Id, Texture);
        return Texture;
    };
    TArray<FParametricRoadBlueprintEntry> Entries;
    TSet<UFGBlueprintDescriptor*> Seen;
    const FFloatProperty* MenuPriority = FindFProperty<FFloatProperty>(UFGBlueprintDescriptor::StaticClass(), TEXT("mMenuPriority"));
    for (UFGBlueprintDescriptor* Descriptor : Descriptors)
    {
        if (!IsValid(Descriptor) || Seen.Contains(Descriptor)) { continue; }
        Seen.Add(Descriptor);
        auto& Entry = Entries.AddDefaulted_GetRef();
        Entry.Descriptor = Descriptor; Entry.Name = Descriptor->GetBlueprintNameAsString();
        const FString Display = Descriptor->GetFilteredDisplayName();
        Entry.DisplayName = FText::FromString(Display.IsEmpty() ? Entry.Name : Display);
        Entry.Description = Descriptor->GetItemDescriptionFromInstance(); Entry.Color = Descriptor->GetColorOnInstance();
        Entry.IconId = Descriptor->GetIconID();
        Entry.MenuPriority = MenuPriority ? MenuPriority->GetPropertyValue_InContainer(Descriptor) : 0;
    }
    RebuildBlueprintCatalog(MoveTemp(Entries), Categories, bIconsReady, ResolveIcon);
    bBlueprintCatalogDirty = false; BlueprintCatalogPollSeconds = 0;
    // Keep the existing source replacement/deletion/recovery contract separate
    // from browsing revisions. Never rebuild native blueprint-loading worlds here.
    for (const FParametricRoadLayerState& Layer : Layers)
    {
        if (Layer.Settings.BlueprintName.IsEmpty()) { continue; }
        UFGBlueprintDescriptor* Current = nullptr;
        for (UFGBlueprintDescriptor* Descriptor : Descriptors)
        { if (IsValid(Descriptor) && Descriptor->GetBlueprintNameAsString().Equals(Layer.Settings.BlueprintName, ESearchCase::IgnoreCase))
            { Current = Descriptor; break; } }
        if ((Layer.Descriptor && Current != Layer.Descriptor) || (Layer.bRejected && Current))
        { ChangedLayerSources.Add(Layer.Settings.BlueprintName); }
    }
}

void UParametricRoadSession::RefreshBlueprintCatalogIfNeeded(float DeltaSeconds)
{
    if (!IsLayerConfigurationOpen()) { return; }
    if (FMath::IsFinite(DeltaSeconds) && DeltaSeconds > 0) { BlueprintCatalogPollSeconds += DeltaSeconds; }
    if (bBlueprintCatalogDirty || BlueprintCatalogPollSeconds >= CatalogPollInterval) { RefreshBlueprintCatalog(); }
}

void UParametricRoadSession::OnNativeBlueprintUpdated(UFGBlueprintDescriptor* Descriptor)
{
    bBlueprintCatalogDirty = true;
    // Preserve deferred layer-source invalidation; native own-template loading
    // must not recursively discard the snapshot being captured.
    if (bLoadingNativePreview) { return; }
    const FString Name = Descriptor ? Descriptor->GetBlueprintNameAsString() : FString();
    for (const FParametricRoadLayerState& Layer : Layers)
    {
        if (!Layer.Settings.BlueprintName.IsEmpty() && (!Descriptor || Name == Layer.Settings.BlueprintName))
        { ChangedLayerSources.Add(Layer.Settings.BlueprintName); }
    }
}
