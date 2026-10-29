#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "ParametricRoadPresets.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include <cmath>
#include <limits>

namespace
{
    FParametricRoadPreset Composition()
    {
        FParametricRoadPreset Preset;
        Preset.Id = TEXT("ad7b038e-9e49-4369-a17b-1443fa21950b");
        Preset.Name = TEXT("Café bridge");
        Preset.RoadBlueprintName = TEXT("Road/native identity_01");
        Preset.Road.LengthMetres = 8.123456789123456;
        Preset.Road.WidthMetres = 17.98765432101234;
        Preset.Road.ThicknessMetres = .14567890123456789;
        Preset.Road.OverlapMetres = .2543210987654321;
        Preset.Road.MaxEdgeStepMetres = .03456789123456789;
        Preset.Road.DeckPlaneMetres = -2.123456789012345;
        Preset.Road.CurveTension = .9123456789012345;
        Preset.Road.bForwardY = true;
        for (int32 Index = 0; Index < ParametricRoadPresets::MaximumLayers; ++Index)
        {
            FParametricRoadLayerSettings& Layer = Preset.Layers.AddDefaulted_GetRef();
            // Repeated native identities are independent layers, never a map key.
            Layer.BlueprintName = Index == 7 ? FString() : TEXT("Decoration/shared native identity");
            Layer.bEnabled = Index != 7;
            Layer.SpacingMetres = 12.123456789012345 + Index;
            Layer.FirstPlacementMetres = 3.567890123456789 + Index;
            Layer.OffsetMetres = FVector(-4.123456789012345 + Index, .12345678901234567, 5.987654321012345);
            Layer.bFollowGrade = Index % 2 == 0;
            Layer.bFollowBank = Index % 3 == 0;
        }
        return Preset;
    }

    FString Mutate(const FString& Json, TFunctionRef<void(const TSharedPtr<FJsonObject>&)> Edit)
    {
        TSharedPtr<FJsonObject> Document;
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Document);
        check(Document.IsValid());
        Edit(Document);
        FString Result;
        FJsonSerializer::Serialize(Document.ToSharedRef(), TJsonWriterFactory<>::Create(&Result));
        return Result;
    }

    struct FOwnedPresetDirectory
    {
        FString Folder = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectIntermediateDir(),
            TEXT("Automation/ParametricRoadPresets"), FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower)));
        ~FOwnedPresetDirectory() { IFileManager::Get().DeleteDirectory(*Folder, false, true); }
        FString File(const FString& Id) const { return FPaths::Combine(Folder, Id + TEXT(".json")); }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParametricRoadPresetCodecTest, "ParametricRoads.Editor.PresetCodec",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParametricRoadPresetCodecTest::RunTest(const FString&)
{
    FText Error;
    const FParametricRoadPreset Original = Composition();
    FString Json;
    if (!TestTrue(TEXT("A complete eight-layer composition encodes"), ParametricRoadPresets::Encode(Original, Json, Error))) return false;
    FParametricRoadPreset Decoded;
    if (!TestTrue(TEXT("The complete composition decodes"), ParametricRoadPresets::Decode(Json, Decoded, Error))) return false;
    TestTrue(TEXT("The codec stores reusable composition settings, without route anchors or native objects"),
        Json.Contains(TEXT("lengthMetres")) && Json.Contains(TEXT("offsetMetres")) && !Json.Contains(TEXT("routePoints"))
        && !Json.Contains(TEXT("Transform")) && !Json.Contains(TEXT("ObjectPath")));
    TestEqual(TEXT("User-visible Unicode names round-trip"), Decoded.Name, Original.Name);
    TestEqual(TEXT("Native road identity is not replaced by display text"), Decoded.RoadBlueprintName, Original.RoadBlueprintName);
    TestEqual(TEXT("The GUID is preserved"), Decoded.Id, Original.Id);
    TestEqual(TEXT("Length retains double precision"), Decoded.Road.LengthMetres, Original.Road.LengthMetres);
    TestEqual(TEXT("Width retains double precision"), Decoded.Road.WidthMetres, Original.Road.WidthMetres);
    TestEqual(TEXT("Thickness retains double precision"), Decoded.Road.ThicknessMetres, Original.Road.ThicknessMetres);
    TestEqual(TEXT("Overlap retains double precision"), Decoded.Road.OverlapMetres, Original.Road.OverlapMetres);
    TestEqual(TEXT("Edge tolerance retains double precision"), Decoded.Road.MaxEdgeStepMetres, Original.Road.MaxEdgeStepMetres);
    TestEqual(TEXT("Deck plane retains double precision"), Decoded.Road.DeckPlaneMetres, Original.Road.DeckPlaneMetres);
    TestEqual(TEXT("Tension retains double precision"), Decoded.Road.CurveTension, Original.Road.CurveTension);
    TestEqual(TEXT("Blueprint axis is preserved"), Decoded.Road.bForwardY, Original.Road.bForwardY);
    TestEqual(TEXT("Layer order and repeated blueprints survive"), Decoded.Layers.Num(), Original.Layers.Num());
    for (int32 Index = 0; Index < Original.Layers.Num() && Index < Decoded.Layers.Num(); ++Index)
    {
        const FParametricRoadLayerSettings& A = Original.Layers[Index];
        const FParametricRoadLayerSettings& B = Decoded.Layers[Index];
        TestEqual(TEXT("Per-layer native identity survives"), B.BlueprintName, A.BlueprintName);
        TestEqual(TEXT("Each repeated layer retains its own spacing"), B.SpacingMetres, A.SpacingMetres);
        TestEqual(TEXT("Each repeated layer retains its first station"), B.FirstPlacementMetres, A.FirstPlacementMetres);
        TestEqual(TEXT("Each repeated layer retains its forward offset"), B.OffsetMetres.X, A.OffsetMetres.X);
        TestEqual(TEXT("Each repeated layer retains its right offset"), B.OffsetMetres.Y, A.OffsetMetres.Y);
        TestEqual(TEXT("Each repeated layer retains its up offset"), B.OffsetMetres.Z, A.OffsetMetres.Z);
        TestEqual(TEXT("Each repeated layer retains its enabled state"), B.bEnabled, A.bEnabled);
        TestEqual(TEXT("Each repeated layer retains grade following"), B.bFollowGrade, A.bFollowGrade);
        TestEqual(TEXT("Each repeated layer retains bank following"), B.bFollowBank, A.bFollowBank);
    }

    int32 RejectedCase = 0;
    const auto Reject = [this, &Error, &RejectedCase](const FString& Bad)
    {
        FParametricRoadPreset Target = Composition();
        Target.Name = TEXT("Keep this output");
        TestFalse(FString::Printf(TEXT("Invalid input case %d cannot decode"), ++RejectedCase), ParametricRoadPresets::Decode(Bad, Target, Error));
        TestFalse(TEXT("A rejected document supplies a visible error"), Error.IsEmpty());
        TestEqual(TEXT("Rejected input does not partially mutate output"), Target.Name, FString(TEXT("Keep this output")));
        TestEqual(TEXT("Rejected input preserves the existing layers"), Target.Layers.Num(), 8);
    };
    Reject(TEXT("{broken"));
    Reject(TEXT("[]"));
    Reject(Json + TEXT(" trailing data"));
    Reject(Mutate(Json, [](const TSharedPtr<FJsonObject>& D) { D->SetNumberField(TEXT("version"), 2); }));
    Reject(Mutate(Json, [](const TSharedPtr<FJsonObject>& D) { D->SetNumberField(TEXT("version"), 1.5); }));
    Reject(Mutate(Json, [](const TSharedPtr<FJsonObject>& D) { D->SetStringField(TEXT("version"), TEXT("1")); }));
    Reject(Mutate(Json, [](const TSharedPtr<FJsonObject>& D) { D->RemoveField(TEXT("version")); }));
    Reject(Mutate(Json, [](const TSharedPtr<FJsonObject>& D) { D->SetStringField(TEXT("id"), TEXT("../outside")); }));
    Reject(Mutate(Json, [](const TSharedPtr<FJsonObject>& D) { D->SetStringField(TEXT("id"), TEXT("AD7B038E-9E49-4369-A17B-1443FA21950B")); }));
    Reject(Mutate(Json, [](const TSharedPtr<FJsonObject>& D) { D->SetStringField(TEXT("id"), TEXT("00000000-0000-0000-0000-000000000000")); }));
    Reject(Mutate(Json, [](const TSharedPtr<FJsonObject>& D) { D->GetObjectField(TEXT("road"))->SetStringField(TEXT("lengthMetres"), TEXT("8")); }));
    Reject(Mutate(Json, [](const TSharedPtr<FJsonObject>& D) { D->GetObjectField(TEXT("road"))->RemoveField(TEXT("widthMetres")); }));
    Reject(Mutate(Json, [](const TSharedPtr<FJsonObject>& D) { D->GetObjectField(TEXT("road"))->SetNumberField(TEXT("overlapMetres"), 99); }));
    Reject(Mutate(Json, [](const TSharedPtr<FJsonObject>& D) { D->GetObjectField(TEXT("road"))->SetNumberField(TEXT("maxEdgeStepMetres"), 0); }));
    Reject(Mutate(Json, [](const TSharedPtr<FJsonObject>& D) { D->GetArrayField(TEXT("layers"))[0]->AsObject()->SetNumberField(TEXT("enabled"), 1); }));
    Reject(Mutate(Json, [](const TSharedPtr<FJsonObject>& D) { D->GetArrayField(TEXT("layers"))[0]->AsObject()->SetNumberField(TEXT("spacingMetres"), -.1); }));
    Reject(Mutate(Json, [](const TSharedPtr<FJsonObject>& D) { D->GetArrayField(TEXT("layers"))[0]->AsObject()->GetObjectField(TEXT("offsetMetres"))->SetNumberField(TEXT("up"), 1000.1); }));
    Reject(FString::ChrN(65537, TEXT(' ')));
    Reject(TEXT("{\"version\":NaN}"));
    Reject(TEXT("{\"version\":1e999}"));

    FParametricRoadPreset Invalid = Original;
    Invalid.Id.Reset();
    TestTrue(TEXT("A captured composition may have no ID before saving"), ParametricRoadPresets::Validate(Invalid, Error));
    FString KeptJson(TEXT("unchanged"));
    TestFalse(TEXT("An ID-free capture cannot be persisted by Encode"), ParametricRoadPresets::Encode(Invalid, KeptJson, Error));
    TestEqual(TEXT("Failed serialization leaves its output intact"), KeptJson, FString(TEXT("unchanged")));
    Invalid = Original; Invalid.Road.LengthMetres = std::numeric_limits<double>::infinity();
    TestFalse(TEXT("Infinity cannot be serialized"), ParametricRoadPresets::Encode(Invalid, KeptJson, Error));
    Invalid = Original; Invalid.Layers[0].OffsetMetres.Y = std::numeric_limits<double>::quiet_NaN();
    TestFalse(TEXT("NaN offsets cannot be serialized"), ParametricRoadPresets::Encode(Invalid, KeptJson, Error));
    Invalid = Original; Invalid.Name = TEXT("   ");
    TestFalse(TEXT("Blank names are invalid"), ParametricRoadPresets::Validate(Invalid, Error));
    Invalid.Name = TEXT(" untrimmed");
    TestFalse(TEXT("Serialized names are canonical trimmed text"), ParametricRoadPresets::Validate(Invalid, Error));
    Invalid.Name = FString::ChrN(97, TEXT('a'));
    TestFalse(TEXT("Names are bounded"), ParametricRoadPresets::Validate(Invalid, Error));
    Invalid = Original; Invalid.RoadBlueprintName += TEXT("\n");
    TestFalse(TEXT("Native names cannot contain controls"), ParametricRoadPresets::Validate(Invalid, Error));
    Invalid = Original; Invalid.Layers[7].bEnabled = true;
    TestFalse(TEXT("An unassigned layer cannot be enabled"), ParametricRoadPresets::Validate(Invalid, Error));
    Invalid = Original; Invalid.Layers.AddDefaulted();
    TestFalse(TEXT("More than eight layers are rejected"), ParametricRoadPresets::Validate(Invalid, Error));
    Invalid = Original; Invalid.Road.LengthMetres = 35.0 / 100.0; Invalid.Road.OverlapMetres = (35.0 * .95) / 100.0;
    TestTrue(TEXT("Native 95-percent overlap survives the centimetre-to-metre one-ULP conversion"),
        ParametricRoadPresets::Validate(Invalid, Error));
    Invalid.Road.OverlapMetres = std::nextafter(Invalid.Road.OverlapMetres, std::numeric_limits<double>::infinity());
    TestFalse(TEXT("The overlap conversion allowance is limited to one ULP"), ParametricRoadPresets::Validate(Invalid, Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParametricRoadPresetFilesTest, "ParametricRoads.Editor.PresetFiles",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParametricRoadPresetFilesTest::RunTest(const FString&)
{
    FOwnedPresetDirectory Owned;
    IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
    FText Error;
    TArray<FParametricRoadPreset> Listed = {Composition()};
    FParametricRoadPreset Target = Composition();
    TestTrue(TEXT("Listing an absent directory is an empty read"), ParametricRoadPresets::List(Listed, Error, Owned.Folder));
    TestEqual(TEXT("No old result survives an empty listing"), Listed.Num(), 0);
    TestFalse(TEXT("Listing does not create a directory"), Files.DirectoryExists(*Owned.Folder));
    TestFalse(TEXT("Loading an absent file is a normal error"), ParametricRoadPresets::Load(Target.Id, Target, Error, Owned.Folder));
    TestFalse(TEXT("Loading does not create a directory"), Files.DirectoryExists(*Owned.Folder));

    FParametricRoadPreset Preset = Composition();
    Preset.Id.Reset(); Preset.Name = TEXT("  ../a reusable road  ");
    if (!TestTrue(TEXT("New save generates its ID and trims its name"), ParametricRoadPresets::Save(Preset, false, Error, Owned.Folder))) return false;
    TestEqual(TEXT("Only the displayed name is trimmed"), Preset.Name, FString(TEXT("../a reusable road")));
    TestEqual(TEXT("Generated filename uses only a canonical GUID"), Preset.Id.Len(), 36);
    const FString Filename = Owned.File(Preset.Id);
    TestTrue(TEXT("Preset is installed inside the assigned directory"), Files.FileExists(*Filename));
    TestFalse(TEXT("The display name cannot create a sibling path"), Files.FileExists(*FPaths::Combine(FPaths::GetPath(Owned.Folder), TEXT("a reusable road.json"))));
    FString OriginalJson;
    FFileHelper::LoadFileToString(OriginalJson, *Filename);
    TestFalse(TEXT("Saving an existing ID without Replace is rejected"), ParametricRoadPresets::Save(Preset, false, Error, Owned.Folder));
    FString Readback;
    FFileHelper::LoadFileToString(Readback, *Filename);
    TestEqual(TEXT("Rejected duplicate save keeps the original bytes"), Readback, OriginalJson);

    for (const FString& BadId : {FString(TEXT("../outside")), FString(TEXT("/absolute/path")), Preset.Id.ToUpper(), Preset.Id + TEXT("/../another")})
        TestFalse(TEXT("Only canonical IDs can be loaded"), ParametricRoadPresets::Load(BadId, Target, Error, Owned.Folder));
    Preset.Name = TEXT("Revised road"); Preset.Layers[0].SpacingMetres = 42.12345678901234;
    if (!TestTrue(TEXT("Explicit Replace updates the same preset"), ParametricRoadPresets::Save(Preset, true, Error, Owned.Folder))) return false;
    FString BackupJson;
    FFileHelper::LoadFileToString(BackupJson, *(Filename + TEXT(".bak")));
    TestEqual(TEXT("Replace keeps the complete previous JSON as a backup"), BackupJson, OriginalJson);
    TestTrue(TEXT("Load re-reads the updated file"), ParametricRoadPresets::Load(Preset.Id, Target, Error, Owned.Folder));
    TestEqual(TEXT("Loaded preset contains the revised independent settings"), Target.Layers[0].SpacingMetres, Preset.Layers[0].SpacingMetres);

    // A failed backup must stop before touching the main document.
    FFileHelper::LoadFileToString(OriginalJson, *Filename);
    const FString Backup = Filename + TEXT(".bak");
    Files.DeleteFile(*Backup); Files.CreateDirectory(*Backup);
    Preset.Name = TEXT("Must not reach disk");
    TestFalse(TEXT("An unusable backup destination rejects replacement"), ParametricRoadPresets::Save(Preset, true, Error, Owned.Folder));
    FFileHelper::LoadFileToString(Readback, *Filename);
    TestEqual(TEXT("Backup failure preserves the original primary file"), Readback, OriginalJson);
    Files.DeleteDirectory(*Backup);

    TestTrue(TEXT("The fixture can mark its own primary read-only"), Files.SetReadOnly(*Filename, true));
    ON_SCOPE_EXIT { Files.SetReadOnly(*Filename, false); };
    TestFalse(TEXT("Read-only primary is not replaced even on a writable directory"), ParametricRoadPresets::Save(Preset, true, Error, Owned.Folder));
    Files.SetReadOnly(*Filename, false);
    FFileHelper::LoadFileToString(Readback, *Filename);
    TestEqual(TEXT("A rejected write preserves the read-only file"), Readback, OriginalJson);

    const FString Future = Mutate(OriginalJson, [](const TSharedPtr<FJsonObject>& D) { D->SetNumberField(TEXT("version"), 900); });
    FFileHelper::SaveStringToFile(Future, *Filename);
    TestFalse(TEXT("Stale Replace cannot overwrite a newer-format preset"), ParametricRoadPresets::Save(Preset, true, Error, Owned.Folder));
    FFileHelper::LoadFileToString(Readback, *Filename);
    TestEqual(TEXT("Unsupported preset bytes remain intact"), Readback, Future);
    FFileHelper::SaveStringToFile(TEXT("{broken"), *Filename);
    TestFalse(TEXT("Stale Replace cannot overwrite a corrupt file"), ParametricRoadPresets::Save(Preset, true, Error, Owned.Folder));
    FFileHelper::LoadFileToString(Readback, *Filename);
    TestEqual(TEXT("Corrupt bytes remain available for recovery"), Readback, FString(TEXT("{broken")));
    const FString WrongIdentity = Mutate(OriginalJson, [](const TSharedPtr<FJsonObject>& D)
        { D->SetStringField(TEXT("id"), TEXT("3408d43d-978d-413a-a922-008c559b8f03")); });
    FFileHelper::SaveStringToFile(WrongIdentity, *Filename);
    TestFalse(TEXT("Filename and document identities must agree"), ParametricRoadPresets::Load(Preset.Id, Target, Error, Owned.Folder));
    TestFalse(TEXT("Replace cannot overwrite another document identity"), ParametricRoadPresets::Save(Preset, true, Error, Owned.Folder));
    FFileHelper::SaveStringToFile(OriginalJson, *Filename);
    const FString External = Mutate(OriginalJson, [](const TSharedPtr<FJsonObject>& D) { D->SetStringField(TEXT("name"), TEXT("Edited on disk")); });
    FFileHelper::SaveStringToFile(External, *Filename);
    TestTrue(TEXT("Load does not reuse cached library content"), ParametricRoadPresets::Load(Preset.Id, Target, Error, Owned.Folder));
    TestEqual(TEXT("Fresh load sees external valid edits"), Target.Name, FString(TEXT("Edited on disk")));

    TArray<FString> Leftovers;
    IFileManager::Get().FindFiles(Leftovers, *FPaths::Combine(Owned.Folder, TEXT("*.tmp")), true, false);
    TestEqual(TEXT("Successful and failed saves clean their staging files"), Leftovers.Num(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParametricRoadPresetBoundsTest, "ParametricRoads.Editor.PresetStorageBounds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParametricRoadPresetBoundsTest::RunTest(const FString&)
{
    FOwnedPresetDirectory Owned;
    IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
    FText Diagnostic;
    FParametricRoadPreset A = Composition(); A.Id.Reset(); A.Name = TEXT("Zulu");
    FParametricRoadPreset B = Composition(); B.Id.Reset(); B.Name = TEXT("alpha");
    if (!TestTrue(TEXT("First bounded fixture saves"), ParametricRoadPresets::Save(A, false, Diagnostic, Owned.Folder))
        || !TestTrue(TEXT("Second bounded fixture saves"), ParametricRoadPresets::Save(B, false, Diagnostic, Owned.Folder))) return false;
    FFileHelper::SaveStringToFile(TEXT("{}"), *FPaths::Combine(Owned.Folder, TEXT("not-a-guid.json")));
    const FString HugeId = TEXT("00cf21cc-8bd0-478e-8775-415185759b37");
    FFileHelper::SaveStringToFile(FString::ChrN(65537, TEXT('a')), *Owned.File(HugeId), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    FParametricRoadPreset Loaded;
    TestFalse(TEXT("Oversize disk input is rejected before JSON parsing"), ParametricRoadPresets::Load(HugeId, Loaded, Diagnostic, Owned.Folder));
    TArray<FParametricRoadPreset> Presets;
    TestTrue(TEXT("A listing can retain good files alongside rejected files"), ParametricRoadPresets::List(Presets, Diagnostic, Owned.Folder));
    TestEqual(TEXT("Invalid filenames and oversized files are skipped"), Presets.Num(), 2);
    if (Presets.Num() == 2)
    {
        TestEqual(TEXT("Names are sorted without case sensitivity"), Presets[0].Name, FString(TEXT("alpha")));
        TestEqual(TEXT("A second name follows alphabetically"), Presets[1].Name, FString(TEXT("Zulu")));
    }
    TestTrue(TEXT("Skipped documents produce an aggregate visible diagnostic"), Diagnostic.ToString().Contains(TEXT("2 invalid")));
    // Backups and temporary files are not offered as reusable presets.
    FFileHelper::SaveStringToFile(TEXT("ignored"), *(Owned.File(A.Id) + TEXT(".bak")));
    FFileHelper::SaveStringToFile(TEXT("ignored"), *(Owned.File(A.Id) + TEXT(".tmp")));
    TestTrue(TEXT("Backup and staging names do not change the listing"), ParametricRoadPresets::List(Presets, Diagnostic, Owned.Folder));
    TestEqual(TEXT("Only live JSON documents appear in the catalog"), Presets.Num(), 2);
    for (int32 Index = 0; Index < 255; ++Index)
        FFileHelper::SaveStringToFile(TEXT("{}"), *FPaths::Combine(Owned.Folder, FString::Printf(TEXT("invalid-%03d.json"), Index)));
    TestTrue(TEXT("An excessive directory still yields a bounded, diagnosed result"), ParametricRoadPresets::List(Presets, Diagnostic, Owned.Folder));
    TestTrue(TEXT("The directory bound is reported rather than silently truncating"), Diagnostic.ToString().Contains(TEXT("first 256")));
    TestTrue(TEXT("The bounded scan never returns more than 256 presets"), Presets.Num() <= 256);
    return true;
}

#endif
