#include "Mods/ACEUCMProfiles.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

namespace ACEUCMProfiles
{
namespace
{
    bool Equal(const TSharedPtr<FJsonObject>& A, const TSharedPtr<FJsonObject>& B)
    { return A && B && FJsonValue::CompareEqual(FJsonValueObject(A), FJsonValueObject(B)); }
    FString Encode(const TSharedPtr<FJsonObject>& Object)
    {
        FString Text;
        if (Object) FJsonSerializer::Serialize(Object.ToSharedRef(), TJsonWriterFactory<>::Create(&Text));
        return Text;
    }
    TSharedPtr<FJsonObject> Read(const FString& Path)
    {
        FString Text; TSharedPtr<FJsonObject> Object;
        if (IFileManager::Get().FileSize(*Path) > 8 * 1024 * 1024) return nullptr;
        if (FFileHelper::LoadFileToString(Text, *Path)) FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Object);
        return Object;
    }
    bool Write(const FString& Path, const TSharedPtr<FJsonObject>& Object, bool Exclusive = false)
    {
        if (Exclusive && IFileManager::Get().FileExists(*Path)) return false;
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
        if (Exclusive) return FFileHelper::SaveStringToFile(Encode(Object), *Path,
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_NoReplaceExisting);
        const FString Temp = Path + TEXT(".tmp");
        return FFileHelper::SaveStringToFile(Encode(Object), *Temp, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
            && IFileManager::Get().Move(*Path, *Temp, true, true);
    }
    FString Path(const FString& Root, const FString& Section, const FString& Name)
    { return Root / TEXT("Profiles") / Section / (Name + TEXT(".json")); }
    TSharedPtr<FJsonObject> ReadSection(const FString& Root, const FString& Section, const FString& Name, FString& Error)
    {
        auto Part = SafeName(Name) ? Read(Path(Root, Section, Name)) : nullptr;
        if (!Part) { Error = TEXT("Cannot load ") + Section + TEXT(" profile: ") + Name; return nullptr; }
        for (const auto& Pair : Part->Values) if (SectionFor(FString(Pair.Key)) != Section)
        { Error = Section + TEXT(" profile contains a setting from another section: ") + FString(Pair.Key); return nullptr; }
        return Part;
    }
    void Replace(TSharedPtr<FJsonObject>& Profile, const FString& Section, const TSharedPtr<FJsonObject>& Part)
    {
        TArray<FString> Keys; for (const auto& Pair : Profile->Values) Keys.Add(FString(Pair.Key));
        for (const auto& Key : Keys) if (SectionFor(Key) == Section) Profile->RemoveField(Key);
        for (const auto& Pair : Part->Values) Profile->Values.Add(Pair.Key, Pair.Value);
    }
}
const TArray<FString>& Sections()
{
    static const TArray<FString> Names = {TEXT("Character"), TEXT("Routes"), TEXT("Loot"), TEXT("Monsters"), TEXT("Metas"), TEXT("Vendors")};
    return Names;
}
bool SafeName(const FString& Name)
{
    if (Name.IsEmpty() || Name.Len() > 64) return false;
    for (TCHAR C : Name) if (!(FChar::IsAlnum(C) || C == '_' || C == '-')) return false;
    return true;
}
FString SectionFor(const FString& Key)
{
    // Run activity switches stay local to the setup; shared rules do not toggle
    // another hunt's activities when their settings are edited.
    static const TSet<FString> Run = {TEXT("buffing"), TEXT("combat"), TEXT("recovery"), TEXT("navigation"),
        TEXT("looting"), TEXT("buff_others"), TEXT("vendor_restock"), TEXT("meta_enabled"), TEXT("idle_peace"),
        TEXT("import_source"), TEXT("ucm_format_version"), TEXT("loot_revision")};
    if (Run.Contains(Key)) return TEXT("ucm");
    if (Key == TEXT("monsters") || Key == TEXT("monster_catalog")) return TEXT("Monsters");
    if (Key.StartsWith(TEXT("vendor_"))) return TEXT("Vendors");
    if (Key == TEXT("states") || Key == TEXT("initial_state") || Key.StartsWith(TEXT("vt_"))
        || Key.StartsWith(TEXT("meta_")) || Key == TEXT("met_source")) return TEXT("Metas");
    static const TSet<FString> Route = {TEXT("route"), TEXT("navigation_mode"), TEXT("loop_route"), TEXT("reverse_route"),
        TEXT("waypoint_radius"), TEXT("open_doors"), TEXT("nav_priority"), TEXT("nav_source")};
    if (Route.Contains(Key) || Key.StartsWith(TEXT("route_")) || Key.StartsWith(TEXT("follow_"))) return TEXT("Routes");
    static const TSet<FString> Loot = {TEXT("salvage_policy"), TEXT("salvage"), TEXT("autostack"), TEXT("autocram"), TEXT("utl_source"),
        TEXT("learn_scrolls"), TEXT("read_unknown_scrolls"), TEXT("move_loose_items"), TEXT("combine_salvage")};
    if (Loot.Contains(Key) || Key.StartsWith(TEXT("loot_")) || Key.StartsWith(TEXT("salvage_"))) return TEXT("Loot");
    return TEXT("Character"); // Includes future/unknown character options without dropping data.
}
TSharedPtr<FJsonObject> Copy(const TSharedPtr<FJsonObject>& Object)
{
    TSharedPtr<FJsonObject> Result;
    FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Encode(Object)), Result);
    return Result;
}
TSharedPtr<FJsonObject> Extract(const TSharedPtr<FJsonObject>& Profile, const FString& Section)
{
    auto Result = MakeShared<FJsonObject>();
    if (Profile) for (const auto& Pair : Profile->Values) if (SectionFor(FString(Pair.Key)) == Section) Result->Values.Add(Pair.Key, Pair.Value);
    return Result;
}
TArray<FString> List(const FString& Root, const FString& Section)
{
    TArray<FString> Files;
    if (Section != TEXT("ucm") && !Sections().Contains(Section)) return Files;
    IFileManager::Get().FindFiles(Files, *(Root / TEXT("Profiles") / Section / TEXT("*.json")), true, false);
    for (auto& File : Files) File = FPaths::GetBaseFilename(File);
    Files.RemoveAll([](const FString& Name) { return !SafeName(Name); }); Files.Sort();
    return Files;
}
FString UniqueName(const FString& Root, const FString& Section, const FString& Name)
{
    FString Stem;
    for (TCHAR C : Name.Left(48)) Stem.AppendChar(FChar::IsAlnum(C) || C == '_' || C == '-' ? C : '_');
    if (Stem.IsEmpty()) Stem = TEXT("Imported");
    for (int32 N = 1; N < 10000; ++N)
    {
        FString Candidate = Stem + (N == 1 ? FString() : FString::Printf(TEXT("_%d"), N));
        if (!IFileManager::Get().FileExists(*Path(Root, Section, Candidate))) return Candidate;
    }
    return FString();
}
bool Load(const FString& Root, const FString& Name, TSharedPtr<FJsonObject>& Profile, TSharedPtr<FJsonObject>& Links, FString& Error)
{
    auto Manifest = SafeName(Name) ? Read(Path(Root, TEXT("ucm"), Name)) : nullptr;
    if (!Manifest) { Error = TEXT("Cannot read UCM setup: ") + Name; return false; }
    const TSharedPtr<FJsonObject>* Refs = nullptr;
    if (!Manifest->HasField(TEXT("profile_refs")))
    {
        // The host validates legacy data before calling Save to migrate it.
        Profile = Manifest; Links = MakeShared<FJsonObject>(); return true;
    }
    double Version = 0;
    if (!Manifest->TryGetNumberField(TEXT("ucm_setup_version"), Version) || Version != 2
        || !Manifest->TryGetObjectField(TEXT("profile_refs"), Refs) || (*Refs)->Values.Num() != Sections().Num())
    { Error = TEXT("Invalid linked UCM setup: ") + Name; return false; }
    const TSharedPtr<FJsonObject>* Run = nullptr;
    if (!Manifest->TryGetObjectField(TEXT("run"), Run)) { Error = TEXT("Missing UCM run settings"); return false; }
    auto Resolved = Copy(*Run);
    for (const auto& Pair : Resolved->Values) if (SectionFor(FString(Pair.Key)) != TEXT("ucm"))
    { Error = TEXT("Invalid UCM run setting: ") + FString(Pair.Key); return false; }
    for (const auto& Section : Sections())
    {
        FString PartName;
        if (!(*Refs)->TryGetStringField(Section, PartName)) { Error = TEXT("Missing profile reference: ") + Section; return false; }
        auto Part = ReadSection(Root, Section, PartName, Error); if (!Part) return false;
        Replace(Resolved, Section, Part);
    }
    if (Encode(Resolved).Len() > 8 * 1024 * 1024) { Error = TEXT("Resolved UCM setup exceeds 8 MiB"); return false; }
    Profile = Resolved; Links = Copy(*Refs); return true;
}
bool Save(const FString& Root, const FString& Name, TSharedPtr<FJsonObject>& Profile, TSharedPtr<FJsonObject>& Links,
    const TSharedPtr<FJsonObject>& Baseline, FString& Error, TFunction<bool(const TSharedPtr<FJsonObject>&)> Validate)
{
    if (!SafeName(Name) || !Profile) { Error = TEXT("Invalid UCM setup name or data"); return false; }
    auto NextLinks = Links ? Copy(Links) : MakeShared<FJsonObject>();
    auto Resolved = Copy(Profile);
    TMap<FString, TSharedPtr<FJsonObject>> Writes, Previous;
    for (const auto& Section : Sections())
    {
        FString PartName; const bool Linked = NextLinks->TryGetStringField(Section, PartName);
        auto Part = Extract(Profile, Section);
        if (!Linked)
        {
            PartName = UniqueName(Root, Section, Name);
            if (PartName.IsEmpty()) { Error = TEXT("Cannot allocate profile name"); return false; }
            NextLinks->SetStringField(Section, PartName);
        }
        auto Disk = Linked ? ReadSection(Root, Section, PartName, Error) : nullptr;
        if (Linked && !Disk) return false;
        // Only changed sections are written. A different setup may have updated
        // an unchanged shared section since this setup was loaded.
        if (Linked && Baseline && Equal(Part, Extract(Baseline, Section)))
        { Replace(Resolved, Section, Disk); continue; }
        if (Linked && Baseline && !Equal(Disk, Extract(Baseline, Section)) && !Equal(Disk, Part))
        { Error = TEXT("Shared ") + Section + TEXT(" profile changed on disk. Reload linked profiles before editing."); return false; }
        const FString Target = Path(Root, Section, PartName);
        Writes.Add(Target, Part); Previous.Add(Target, Disk);
    }
    if (Encode(Resolved).Len() > 8 * 1024 * 1024 || (Validate && !Validate(Resolved)))
    { Error = TEXT("Invalid linked profile data; no changes were saved. Correct the file and reload."); return false; }
    const FString SetupPath = Path(Root, TEXT("ucm"), Name);
    auto OldSetup = Read(SetupPath);
    if (OldSetup && !OldSetup->HasField(TEXT("profile_refs")))
    {
        const FString BackupName = UniqueName(Root, TEXT("Legacy/ucm"), Name);
        if (BackupName.IsEmpty() || !Write(Path(Root, TEXT("Legacy/ucm"), BackupName), OldSetup, true))
        { Error = TEXT("Cannot back up the original UCM setup; migration was not applied"); return false; }
    }
    auto Manifest = MakeShared<FJsonObject>();
    Manifest->SetNumberField(TEXT("ucm_setup_version"), 2);
    Manifest->SetObjectField(TEXT("profile_refs"), NextLinks);
    Manifest->SetObjectField(TEXT("run"), Extract(Resolved, TEXT("ucm")));
    TArray<FString> Applied;
    auto Rollback = [&]()
    {
        for (const auto& Target : Applied)
            if (Previous[Target]) Write(Target, Previous[Target]); else IFileManager::Get().Delete(*Target);
    };
    for (const auto& Entry : Writes)
    {
        if (!Write(Entry.Key, Entry.Value, !Previous[Entry.Key]))
        { Rollback(); Error = TEXT("Could not save shared profile: ") + Entry.Key; return false; }
        Applied.Add(Entry.Key);
    }
    if (!Write(SetupPath, Manifest)) { Rollback(); Error = TEXT("Could not save UCM setup"); return false; }
    Profile = Resolved; Links = NextLinks; return true;
}
bool Select(const FString& Root, const FString& Section, const FString& Name, TSharedPtr<FJsonObject>& Profile,
    TSharedPtr<FJsonObject>& Links, FString& Error)
{
    if (!Sections().Contains(Section)) { Error = TEXT("Unknown profile section"); return false; }
    auto Part = ReadSection(Root, Section, Name, Error); if (!Part) return false;
    Profile = Copy(Profile); Replace(Profile, Section, Part);
    Links = Links ? Copy(Links) : MakeShared<FJsonObject>(); Links->SetStringField(Section, Name); return true;
}
bool SaveSectionAs(const FString& Root, const FString& Section, const FString& Name,
    const TSharedPtr<FJsonObject>& Profile, FString& Error)
{
    if (!SafeName(Name) || !Sections().Contains(Section)) { Error = TEXT("Use a name of letters, numbers, _ or - (maximum 64)."); return false; }
    auto Part = Extract(Profile, Section);
    if (Section == TEXT("Loot")) { Part->SetStringField(TEXT("loot_profile"), Name); Part->RemoveField(TEXT("loot_modified")); }
    if (!Write(Path(Root, Section, Name), Part, true))
    { Error = TEXT("Profile already exists or could not be created. Choose a new name; edits save to the active profile."); return false; }
    return true;
}
bool EnsureDefaults(const FString& Root, const TSharedPtr<FJsonObject>& Defaults, FString& Error)
{
    if (!Defaults) { Error = TEXT("Cannot read bundled UCM defaults"); return false; }
    for (const auto& Section : Sections())
    {
        if (IFileManager::Get().FileExists(*Path(Root, Section, TEXT("Default"))))
        { if (!ReadSection(Root, Section, TEXT("Default"), Error)) return false; }
        else if (!SaveSectionAs(Root, Section, TEXT("Default"), Defaults, Error)) return false;
    }
    return true;
}
bool DeleteSection(const FString& Root, const FString& Section, const FString& Name, FString& Error)
{
    if (!Sections().Contains(Section) || !SafeName(Name) || Name.Equals(TEXT("Default"), ESearchCase::IgnoreCase))
    { Error = TEXT("The Default profile cannot be deleted. Choose a named profile."); return false; }
    if (!ReadSection(Root, Section, TEXT("Default"), Error) || !ReadSection(Root, Section, Name, Error)) return false;
    TMap<FString, TSharedPtr<FJsonObject>> Before, After;
    for (const auto& Setup : List(Root, TEXT("ucm")))
    {
        const FString File = Path(Root, TEXT("ucm"), Setup); auto Manifest = Read(File);
        if (!Manifest) { Error = TEXT("Cannot inspect setup before deleting shared profile: ") + Setup; return false; }
        const TSharedPtr<FJsonObject>* Refs = nullptr;
        if (!Manifest->HasField(TEXT("profile_refs"))) continue; // Legacy setup embeds its own data.
        if (!Manifest->TryGetObjectField(TEXT("profile_refs"), Refs)) { Error = TEXT("Invalid setup: ") + Setup; return false; }
        FString Linked; if (!(*Refs)->TryGetStringField(Section, Linked) || !Linked.Equals(Name, ESearchCase::IgnoreCase)) continue;
        Before.Add(File, Manifest); auto Updated = Copy(Manifest);
        Updated->GetObjectField(TEXT("profile_refs"))->SetStringField(Section, TEXT("Default")); After.Add(File, Updated);
    }
    TArray<FString> Applied;
    auto Rollback = [&]() { for (const auto& File : Applied) Write(File, Before[File]); };
    for (const auto& Entry : After)
    {
        if (!Write(Entry.Key, Entry.Value)) { Rollback(); Error = TEXT("Could not update linked setups; profile retained."); return false; }
        Applied.Add(Entry.Key);
    }
    const FString Backup = Path(Root, TEXT("Deleted") / Section, Name + TEXT("_") + FGuid::NewGuid().ToString(EGuidFormats::Digits));
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Backup), true);
    if (!IFileManager::Get().Move(*Backup, *Path(Root, Section, Name), false, false))
    { Rollback(); Error = TEXT("Could not archive deleted profile; linked setups were restored."); return false; }
    return true;
}
}
