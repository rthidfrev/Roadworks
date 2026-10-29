#include "ParametricRoadPresets.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include <cmath>
#include <limits>

#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#elif PLATFORM_UNIX || PLATFORM_MAC
#include <unistd.h>
#endif

namespace
{
    constexpr int64 MaximumBytes = 64 * 1024;
    constexpr int32 MaximumFiles = 256;

    bool Fail(FText& Error, const FString& Message)
    {
        Error = FText::FromString(Message);
        return false;
    }

    bool ValidId(const FString& Id)
    {
        FGuid Guid;
        return Id.Len() == 36 && FGuid::ParseExact(Id, EGuidFormats::DigitsWithHyphensLower, Guid)
            && Guid.IsValid() && Guid.ToString(EGuidFormats::DigitsWithHyphensLower).Equals(Id, ESearchCase::CaseSensitive);
    }

    bool PlainText(const FString& Value, int32 MaximumTextLength, bool bAllowEmpty)
    {
        if (Value.Len() > MaximumTextLength || (!bAllowEmpty && Value.TrimStartAndEnd().IsEmpty())) return false;
        for (const TCHAR Character : Value) if (FChar::IsControl(Character)) return false;
        return true;
    }

    bool InRange(double Value, double Minimum, double Maximum)
    {
        return FMath::IsFinite(Value) && Value >= Minimum && Value <= Maximum;
    }

    const FJsonValue* Field(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, EJson Type)
    {
        if (!Object.IsValid()) return nullptr;
        const TSharedPtr<FJsonValue>* Value = Object->Values.Find(Key);
        return Value && Value->IsValid() && (*Value)->Type == Type ? Value->Get() : nullptr;
    }

    bool String(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, FString& Out)
    {
        const FJsonValue* Value = Field(Object, Key, EJson::String);
        return Value && Value->TryGetString(Out);
    }

    bool Number(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, double& Out)
    {
        const FJsonValue* Value = Field(Object, Key, EJson::Number);
        return Value && Value->TryGetNumber(Out) && FMath::IsFinite(Out);
    }

    bool Boolean(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, bool& Out)
    {
        const FJsonValue* Value = Field(Object, Key, EJson::Boolean);
        return Value && Value->TryGetBool(Out);
    }

    TSharedPtr<FJsonObject> ObjectField(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key)
    {
        const FJsonValue* Value = Field(Object, Key, EJson::Object);
        return Value ? Value->AsObject() : nullptr;
    }

    FString Root(const FString& Override)
    {
        FString Result = FPaths::ConvertRelativePathToFull(Override.IsEmpty() ? ParametricRoadPresets::Directory() : Override);
        FPaths::NormalizeDirectoryName(Result);
        return Result;
    }

    FString Path(const FString& Directory, const FString& Id)
    {
        // Only canonical GUIDs reach this helper: user names never form paths.
        check(ValidId(Id));
        return FPaths::Combine(Directory, Id + TEXT(".json"));
    }

    bool Symlink(const FString& Filename)
    {
        return FPlatformFileManager::Get().GetPlatformFile().IsSymlink(*Filename) == ESymlinkResult::Symlink;
    }

    bool ReadBounded(const FString& Filename, FString& Json, FText& Error)
    {
        IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
        if (Symlink(Filename)) return Fail(Error, TEXT("Preset files cannot be symbolic links."));
        TUniquePtr<IFileHandle> Handle(Files.OpenRead(*Filename));
        if (!Handle) return Fail(Error, TEXT("The preset file could not be opened."));
        const int64 Size = Handle->Size();
        if (Size <= 0 || Size > MaximumBytes) return Fail(Error, TEXT("A preset file must contain at most 64 KiB of JSON."));
        TArray<uint8> Bytes;
        Bytes.SetNumUninitialized(static_cast<int32>(Size));
        if (!Handle->Read(Bytes.GetData(), Size)) return Fail(Error, TEXT("The preset file could not be read completely."));
        FString Result;
        FFileHelper::BufferToString(Result, Bytes.GetData(), Bytes.Num());
        Json = MoveTemp(Result);
        return true;
    }

    bool ReadPreset(const FString& Filename, const FString& ExpectedId, FParametricRoadPreset& Preset, FText& Error,
        FString* Source = nullptr)
    {
        FString Json;
        FParametricRoadPreset Result;
        if (!ReadBounded(Filename, Json, Error) || !ParametricRoadPresets::Decode(Json, Result, Error)) return false;
        if (!Result.Id.Equals(ExpectedId, ESearchCase::CaseSensitive)) return Fail(Error, TEXT("The preset ID does not match its filename."));
        if (Source) *Source = MoveTemp(Json);
        Preset = MoveTemp(Result);
        return true;
    }

    bool ReplaceFile(const FString& Destination, const FString& Temporary)
    {
        IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
        if (Symlink(Destination) || Files.DirectoryExists(*Destination)
            || (Files.FileExists(*Destination) && Files.IsReadOnly(*Destination))) return false;
#if PLATFORM_WINDOWS
        // IFileManager::Move deletes the old destination before attempting its
        // rename. Keep it intact on failure with the platform's replace rename.
        return !!::MoveFileExW(*Temporary, *Destination, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
#else
        // Both files are in the same directory, so Unix uses rename rather than
        // its cross-device copy fallback. The old destination survives failure.
        return Files.MoveFile(*Destination, *Temporary);
#endif
    }

    bool WriteTemporary(const FString& Filename, const FString& Json, FText& Error)
    {
        if (!FFileHelper::SaveStringToFile(Json, *Filename, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
            &IFileManager::Get(), FILEWRITE_NoReplaceExisting | FILEWRITE_Silent))
            return Fail(Error, TEXT("The temporary preset file could not be written."));
        FString Readback;
        if (!ReadBounded(Filename, Readback, Error) || !Readback.Equals(Json, ESearchCase::CaseSensitive))
            return Fail(Error, TEXT("The temporary preset file did not pass its read-back check."));
        return true;
    }

    bool InstallNewFile(const FString& Destination, const FString& Temporary)
    {
#if PLATFORM_WINDOWS
        // No REPLACE_EXISTING: even another writer arriving after our checks
        // cannot turn a new save into an implicit update.
        return !!::MoveFileExW(*Temporary, *Destination, MOVEFILE_WRITE_THROUGH);
#elif PLATFORM_UNIX || PLATFORM_MAC
        // Unix rename replaces an existing destination. A same-directory hard
        // link instead installs the complete staged file exclusively, then the
        // caller's scope cleanup removes its temporary name.
        return ::link(TCHAR_TO_UTF8(*Temporary), TCHAR_TO_UTF8(*Destination)) == 0;
#else
        return false;
#endif
    }
}

FString ParametricRoadPresets::Directory()
{
    return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("ParametricRoads"), TEXT("Presets"));
}

bool ParametricRoadPresets::Validate(const FParametricRoadPreset& Preset, FText& Error)
{
    Error = FText::GetEmpty();
    if (!Preset.Id.IsEmpty() && !ValidId(Preset.Id)) return Fail(Error, TEXT("The preset ID is invalid."));
    if (!PlainText(Preset.Name, 96, false) || Preset.Name != Preset.Name.TrimStartAndEnd())
        return Fail(Error, TEXT("Enter a preset name of 1 to 96 characters, without control characters or surrounding spaces."));
    if (!PlainText(Preset.RoadBlueprintName, 512, false)) return Fail(Error, TEXT("The base road blueprint name is invalid."));
    const FParametricRoadPresetRoadSettings& R = Preset.Road;
    // Native settings are centimetres. Converting (length_cm * .95) / 100
    // can land one representable double above .95 * (length_cm / 100).
    const double MaximumOverlap = std::nextafter(.95 * R.LengthMetres, std::numeric_limits<double>::infinity());
    if (!InRange(R.LengthMetres, .01, 1000) || !InRange(R.WidthMetres, .01, 1000)
        || !InRange(R.ThicknessMetres, .001, 1000) || !InRange(R.OverlapMetres, 0, MaximumOverlap)
        || !InRange(R.MaxEdgeStepMetres, .0001, 1) || !InRange(R.DeckPlaneMetres, -500, 500)
        || !InRange(R.CurveTension, .05, 2)) return Fail(Error, TEXT("The preset contains road settings outside the supported ranges."));
    if (Preset.Layers.Num() > MaximumLayers) return Fail(Error, TEXT("A preset can contain at most eight decoration layers."));
    for (int32 Index = 0; Index < Preset.Layers.Num(); ++Index)
    {
        const FParametricRoadLayerSettings& Layer = Preset.Layers[Index];
        if (!PlainText(Layer.BlueprintName, 512, !Layer.bEnabled)
            || (!Layer.BlueprintName.IsEmpty() && Layer.BlueprintName.TrimStartAndEnd().IsEmpty())
            || !InRange(Layer.SpacingMetres, .1, 1000) || !InRange(Layer.FirstPlacementMetres, 0, 1000)
            || !InRange(Layer.OffsetMetres.X, -1000, 1000) || !InRange(Layer.OffsetMetres.Y, -1000, 1000)
            || !InRange(Layer.OffsetMetres.Z, -1000, 1000))
            return Fail(Error, FString::Printf(TEXT("Decoration layer %d has invalid settings."), Index + 1));
    }
    return true;
}

bool ParametricRoadPresets::Encode(const FParametricRoadPreset& Preset, FString& Json, FText& Error)
{
    if (!Validate(Preset, Error)) return false;
    if (!ValidId(Preset.Id)) return Fail(Error, TEXT("Assign a valid preset ID before serializing it."));
    const TSharedRef<FJsonObject> Document = MakeShared<FJsonObject>();
    Document->SetNumberField(TEXT("version"), SchemaVersion);
    Document->SetStringField(TEXT("id"), Preset.Id);
    Document->SetStringField(TEXT("name"), Preset.Name);
    Document->SetStringField(TEXT("roadBlueprint"), Preset.RoadBlueprintName);
    const TSharedRef<FJsonObject> Road = MakeShared<FJsonObject>();
    Road->SetNumberField(TEXT("lengthMetres"), Preset.Road.LengthMetres);
    Road->SetNumberField(TEXT("widthMetres"), Preset.Road.WidthMetres);
    Road->SetNumberField(TEXT("thicknessMetres"), Preset.Road.ThicknessMetres);
    Road->SetNumberField(TEXT("overlapMetres"), Preset.Road.OverlapMetres);
    Road->SetNumberField(TEXT("maxEdgeStepMetres"), Preset.Road.MaxEdgeStepMetres);
    Road->SetNumberField(TEXT("deckPlaneMetres"), Preset.Road.DeckPlaneMetres);
    Road->SetNumberField(TEXT("curveTension"), Preset.Road.CurveTension);
    Road->SetBoolField(TEXT("forwardY"), Preset.Road.bForwardY);
    Document->SetObjectField(TEXT("road"), Road);
    TArray<TSharedPtr<FJsonValue>> Layers;
    for (const FParametricRoadLayerSettings& Layer : Preset.Layers)
    {
        const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
        Object->SetStringField(TEXT("blueprint"), Layer.BlueprintName);
        Object->SetNumberField(TEXT("spacingMetres"), Layer.SpacingMetres);
        Object->SetNumberField(TEXT("firstPlacementMetres"), Layer.FirstPlacementMetres);
        const TSharedRef<FJsonObject> Offset = MakeShared<FJsonObject>();
        Offset->SetNumberField(TEXT("forward"), Layer.OffsetMetres.X);
        Offset->SetNumberField(TEXT("right"), Layer.OffsetMetres.Y);
        Offset->SetNumberField(TEXT("up"), Layer.OffsetMetres.Z);
        Object->SetObjectField(TEXT("offsetMetres"), Offset);
        Object->SetBoolField(TEXT("enabled"), Layer.bEnabled);
        Object->SetBoolField(TEXT("followGrade"), Layer.bFollowGrade);
        Object->SetBoolField(TEXT("followBank"), Layer.bFollowBank);
        Layers.Add(MakeShared<FJsonValueObject>(Object));
    }
    Document->SetArrayField(TEXT("layers"), Layers);
    FString Result;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
    if (!FJsonSerializer::Serialize(Document, Writer)) return Fail(Error, TEXT("The preset could not be serialized."));
    if (FTCHARToUTF8(*Result).Length() > MaximumBytes) return Fail(Error, TEXT("The preset exceeds the 64 KiB file limit."));
    Json = MoveTemp(Result);
    return true;
}

bool ParametricRoadPresets::Decode(const FString& Json, FParametricRoadPreset& Preset, FText& Error)
{
    Error = FText::GetEmpty();
    if (Json.IsEmpty() || Json.Len() > MaximumBytes || FTCHARToUTF8(*Json).Length() > MaximumBytes)
        return Fail(Error, TEXT("A preset must contain at most 64 KiB of JSON."));
    TSharedPtr<FJsonObject> Document;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
    if (!FJsonSerializer::Deserialize(Reader, Document) || !Document.IsValid())
        return Fail(Error, TEXT("The preset is not valid JSON."));
    double Version = 0;
    if (!Number(Document, TEXT("version"), Version) || Version != SchemaVersion)
        return Fail(Error, TEXT("This preset uses an unsupported format version."));
    FParametricRoadPreset Result;
    if (!String(Document, TEXT("id"), Result.Id) || !ValidId(Result.Id)
        || !String(Document, TEXT("name"), Result.Name) || !String(Document, TEXT("roadBlueprint"), Result.RoadBlueprintName))
        return Fail(Error, TEXT("The preset is missing a valid ID, name or base road blueprint."));
    const TSharedPtr<FJsonObject> Road = ObjectField(Document, TEXT("road"));
    if (!Number(Road, TEXT("lengthMetres"), Result.Road.LengthMetres)
        || !Number(Road, TEXT("widthMetres"), Result.Road.WidthMetres)
        || !Number(Road, TEXT("thicknessMetres"), Result.Road.ThicknessMetres)
        || !Number(Road, TEXT("overlapMetres"), Result.Road.OverlapMetres)
        || !Number(Road, TEXT("maxEdgeStepMetres"), Result.Road.MaxEdgeStepMetres)
        || !Number(Road, TEXT("deckPlaneMetres"), Result.Road.DeckPlaneMetres)
        || !Number(Road, TEXT("curveTension"), Result.Road.CurveTension)
        || !Boolean(Road, TEXT("forwardY"), Result.Road.bForwardY))
        return Fail(Error, TEXT("The preset has missing or incorrectly typed road settings."));
    const FJsonValue* LayerValue = Field(Document, TEXT("layers"), EJson::Array);
    if (!LayerValue || LayerValue->AsArray().Num() > MaximumLayers)
        return Fail(Error, TEXT("The preset must provide an array of at most eight decoration layers."));
    for (const TSharedPtr<FJsonValue>& Value : LayerValue->AsArray())
    {
        if (!Value.IsValid() || Value->Type != EJson::Object) return Fail(Error, TEXT("A decoration layer is not a JSON object."));
        const TSharedPtr<FJsonObject> Object = Value->AsObject();
        const TSharedPtr<FJsonObject> Offset = ObjectField(Object, TEXT("offsetMetres"));
        FParametricRoadLayerSettings Layer;
        if (!String(Object, TEXT("blueprint"), Layer.BlueprintName)
            || !Number(Object, TEXT("spacingMetres"), Layer.SpacingMetres)
            || !Number(Object, TEXT("firstPlacementMetres"), Layer.FirstPlacementMetres)
            || !Number(Offset, TEXT("forward"), Layer.OffsetMetres.X)
            || !Number(Offset, TEXT("right"), Layer.OffsetMetres.Y)
            || !Number(Offset, TEXT("up"), Layer.OffsetMetres.Z)
            || !Boolean(Object, TEXT("enabled"), Layer.bEnabled)
            || !Boolean(Object, TEXT("followGrade"), Layer.bFollowGrade)
            || !Boolean(Object, TEXT("followBank"), Layer.bFollowBank))
            return Fail(Error, TEXT("The preset has missing or incorrectly typed decoration settings."));
        Result.Layers.Add(MoveTemp(Layer));
    }
    if (!Validate(Result, Error)) return false;
    Preset = MoveTemp(Result);
    return true;
}

bool ParametricRoadPresets::Save(FParametricRoadPreset& Preset, bool bOverwrite, FText& Error, const FString& RootDirectory)
{
    Error = FText::GetEmpty();
    FParametricRoadPreset Candidate = Preset;
    Candidate.Name.TrimStartAndEndInline();
    if (!Validate(Candidate, Error)) return false;
    if (Candidate.Id.IsEmpty())
    {
        if (bOverwrite) return Fail(Error, TEXT("Choose an existing preset before replacing it."));
        Candidate.Id = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
    }
    FString Json;
    if (!Encode(Candidate, Json, Error)) return false;
    IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
    const FString Folder = Root(RootDirectory);
    const FString Filename = Path(Folder, Candidate.Id);
    const FString Backup = Filename + TEXT(".bak");
    if (Symlink(Folder) || Symlink(Filename) || Symlink(Backup)) return Fail(Error, TEXT("Preset storage cannot use symbolic links."));
    const bool bExists = Files.FileExists(*Filename);
    if (bExists && !bOverwrite) return Fail(Error, TEXT("A preset with this ID already exists. Choose Replace to update it."));
    if (!bExists && bOverwrite) return Fail(Error, TEXT("The preset to replace no longer exists. Save it as a new preset."));
    FParametricRoadPreset Previous;
    FString PreviousJson;
    if (bExists && !ReadPreset(Filename, Candidate.Id, Previous, Error, &PreviousJson)) return false;
    if (Files.DirectoryExists(*Filename) || (bExists && Files.IsReadOnly(*Filename)))
        return Fail(Error, TEXT("The preset destination is not writable."));
    if (!Files.DirectoryExists(*Folder) && !Files.CreateDirectoryTree(*Folder))
        return Fail(Error, TEXT("The preset directory could not be created."));
    const FString Token = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
    const FString Temporary = Filename + TEXT(".") + Token + TEXT(".tmp");
    const FString BackupTemporary = Backup + TEXT(".") + Token + TEXT(".tmp");
    ON_SCOPE_EXIT { Files.DeleteFile(*Temporary); Files.DeleteFile(*BackupTemporary); };
    if (!WriteTemporary(Temporary, Json, Error)) return false;
    if (bExists)
    {
        // Parse the actual file again: a stale library selection must not replace
        // externally edited, corrupt or newer-format data.
        FString CurrentJson;
        if (!ReadPreset(Filename, Candidate.Id, Previous, Error, &CurrentJson)) return false;
        if (!CurrentJson.Equals(PreviousJson, ESearchCase::CaseSensitive)) return Fail(Error, TEXT("The preset changed while it was being saved. Reload it before replacing it."));
        if (!WriteTemporary(BackupTemporary, PreviousJson, Error) || !ReplaceFile(Backup, BackupTemporary))
            return Fail(Error, TEXT("The previous preset could not be backed up. Its original file has been kept."));
        if (!ReplaceFile(Filename, Temporary))
            return Fail(Error, TEXT("The preset could not be replaced. Its original file and backup have been kept."));
    }
    else
    {
        // The ID is a random GUID, independent of the user-visible preset name.
        // Recheck after staging so an existing file is never intentionally replaced.
        if (Files.FileExists(*Filename) || Files.DirectoryExists(*Filename) || Symlink(Filename)
            || !InstallNewFile(Filename, Temporary)) return Fail(Error, TEXT("The new preset could not be installed without replacing an existing file."));
    }
    Preset = MoveTemp(Candidate);
    return true;
}

bool ParametricRoadPresets::Load(const FString& Id, FParametricRoadPreset& Preset, FText& Error, const FString& RootDirectory)
{
    Error = FText::GetEmpty();
    if (!ValidId(Id)) return Fail(Error, TEXT("The preset ID is invalid."));
    const FString Folder = Root(RootDirectory);
    if (Symlink(Folder)) return Fail(Error, TEXT("Preset storage cannot use symbolic links."));
    return ReadPreset(Path(Folder, Id), Id, Preset, Error);
}

bool ParametricRoadPresets::List(TArray<FParametricRoadPreset>& Presets, FText& Diagnostic, const FString& RootDirectory)
{
    Diagnostic = FText::GetEmpty();
    const FString Folder = Root(RootDirectory);
    IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
    if (Symlink(Folder)) return Fail(Diagnostic, TEXT("Preset storage cannot use symbolic links."));
    if (!Files.DirectoryExists(*Folder))
    {
        if (Files.FileExists(*Folder)) return Fail(Diagnostic, TEXT("The preset directory is occupied by a file."));
        Presets.Reset();
        return true;
    }
    TArray<FString> Names;
    bool bTruncated = false;
    const bool bEnumerated = Files.IterateDirectory(*Folder, [&Names, &bTruncated](const TCHAR* Entry, bool bDirectory)
    {
        const FString Filename = FPaths::GetCleanFilename(Entry);
        if (bDirectory || !Filename.EndsWith(TEXT(".json"), ESearchCase::IgnoreCase)) return true;
        if (Names.Num() >= MaximumFiles) { bTruncated = true; return false; }
        Names.Add(Filename);
        return true;
    });
    if (!bEnumerated && !bTruncated) return Fail(Diagnostic, TEXT("The preset directory could not be read."));
    TArray<FParametricRoadPreset> Result;
    int32 InvalidFiles = 0;
    for (const FString& Filename : Names)
    {
        const FString Id = FPaths::GetBaseFilename(Filename);
        FParametricRoadPreset Preset;
        FText Error;
        if (!ValidId(Id) || !ReadPreset(FPaths::Combine(Folder, Filename), Id, Preset, Error)) { ++InvalidFiles; continue; }
        Result.Add(MoveTemp(Preset));
    }
    Result.Sort([](const FParametricRoadPreset& A, const FParametricRoadPreset& B)
    {
        const int32 Order = A.Name.Compare(B.Name, ESearchCase::IgnoreCase);
        return Order == 0 ? A.Id < B.Id : Order < 0;
    });
    FString Message;
    if (InvalidFiles) Message = FString::Printf(TEXT("Skipped %d invalid or unsupported preset file(s)."), InvalidFiles);
    if (bTruncated)
    {
        if (!Message.IsEmpty()) Message += TEXT(" ");
        Message += TEXT("Only the first 256 preset files were checked; this folder contains more files.");
    }
    Diagnostic = FText::FromString(Message);
    Presets = MoveTemp(Result);
    return true;
}
