#include "Mods/ACEPluginContent.h"
#include "Mods/ACEPluginSubsystem.h"
#include "ACEClientSubsystem.h"
#include "ACESession.h"
#include "ACEOpcodes.h"
#include "Engine/GameInstance.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#include <shobjidl.h>
#endif

bool ACEPluginContent::Command(const FString& Text, bool& NeedsTarget)
{
    NeedsTarget=false;
    if(Text.IsEmpty() || Text.Len()>500 || !Text.StartsWith(TEXT("/")))return false;
    for(TCHAR Ch:Text)if(FChar::IsControl(Ch))return false;
    TArray<FString> Words;Text.ParseIntoArrayWS(Words);
    if(Words.IsEmpty())return false;
    const FString Verb=Words[0].ToLower();
    const bool Target=TArray<FString>{TEXT("/getinfo"),TEXT("/removeinst"),TEXT("/removeenc"),TEXT("/delete")}.Contains(Verb);
    if(Target){NeedsTarget=true;return Words.Num()==1;}
    if(Verb==TEXT("/nudge") || Verb==TEXT("/rotate"))
    {NeedsTarget=true;return Words.Num()==(Verb==TEXT("/nudge")?3:2);}
    if(TArray<FString>{TEXT("/loc"),TEXT("/myloc"),TEXT("/clearcache"),TEXT("/reload-landblock")}.Contains(Verb))return Words.Num()==1;
    if(Verb==TEXT("/fetchstring"))return Words.Num()==2 && Words[1]==TEXT("content_folder");
    if(TArray<FString>{TEXT("/create"),TEXT("/ci"),TEXT("/addenc")}.Contains(Verb))return Words.Num()==2;
    if(Verb==TEXT("/createinst"))return Words.Num()==2 || (Words.Num()==5&&Words[1]==TEXT("-p")&&Words[3]==TEXT("-c"));
    if(TArray<FString>{TEXT("/import-json"),TEXT("/import-sql"),TEXT("/export-json"),TEXT("/export-sql")}.Contains(Verb))
        return Words.Num()==3 && TArray<FString>{TEXT("weenie"),TEXT("landblock"),TEXT("quest"),TEXT("recipe")}.Contains(Words[1]);
    return false;
}

bool ACEPluginContent::RelativeFile(const FString& Name)
{
    if(Name.IsEmpty() || Name.Len()>256 || !FPaths::IsRelative(Name) || Name.Contains(TEXT(".."))
        || Name.Contains(TEXT(":")) || Name.Contains(TEXT("\\")) || Name.StartsWith(TEXT("/")))return false;
    for(TCHAR Ch:Name)if(FChar::IsControl(Ch)||Ch=='"'||Ch=='*'||Ch=='?')return false;
    const FString Ext=FPaths::GetExtension(Name).ToLower();return Ext==TEXT("json")||Ext==TEXT("sql");
}

FString UACEPluginSubsystem::ContentRoot(const FACEClientPlugin& P) const
{
    FString Root;
    if(Settings)Settings->TryGetStringField(P.Id+TEXT(".content_root"),Root);
    if(Root.IsEmpty())Root=UserDirectory()/TEXT("Content");
    return FPaths::ConvertRelativePathToFull(Root);
}

bool UACEPluginSubsystem::ChooseContentFolder(FACEClientPlugin& P,FString& Error)
{
    if(!P.Permissions.Contains(TEXT("admin_content"))){Error=TEXT("Missing admin_content permission");return false;}
    FString Root;
#if PLATFORM_WINDOWS
    const HRESULT Init=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    IFileOpenDialog* Dialog=nullptr;
    if(SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&Dialog))))
    {
        DWORD Options=0;Dialog->GetOptions(&Options);Dialog->SetOptions(Options|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM|FOS_NOCHANGEDIR);
        Dialog->SetTitle(L"Choose the ACE content folder (contains json and sql folders)");
        if(SUCCEEDED(Dialog->Show(nullptr)))
        {
            IShellItem* Item=nullptr;
            if(SUCCEEDED(Dialog->GetResult(&Item)))
            {PWSTR Path=nullptr;if(SUCCEEDED(Item->GetDisplayName(SIGDN_FILESYSPATH,&Path))){Root=Path;CoTaskMemFree(Path);}Item->Release();}
        }
        Dialog->Release();
    }
    if(SUCCEEDED(Init))CoUninitialize();
    if(Root.IsEmpty()){Error=TEXT("Folder selection cancelled");return false;}
    FPaths::NormalizeDirectoryName(Root);Settings->SetStringField(P.Id+TEXT(".content_root"),Root);SaveSettings();
#else
    Root=ContentRoot(P);IFileManager::Get().MakeDirectory(*Root,true);FPlatformProcess::ExploreFolder(*Root);
#endif
    auto E=MakeShared<FJsonObject>();E->SetStringField(TEXT("type"),TEXT("content_root"));E->SetStringField(TEXT("root"),Root);QueuePluginEvent(P,E);return true;
}

bool UACEPluginSubsystem::ContentService(FACEClientPlugin& P,const TSharedPtr<FJsonObject>& R,const TSharedPtr<FJsonObject>& Reply,FString& Error)
{
    const auto Str=[&](const TCHAR* Key){FString V;R->TryGetStringField(Key,V);return V;};
    const FString Op=Str(TEXT("operation"));
    if(!P.Permissions.Contains(TEXT("admin_content"))){Error=TEXT("Missing admin_content permission");return false;}
    if(Op==TEXT("command"))
    {
        auto* C=GetGameInstance()->GetSubsystem<UACEClientSubsystem>();auto S=C->GetSession();
        if(!P.Enabled||!P.Running||!S||C->GetSessionState()!=EACESessionState::InWorld||P.Player!=C->GetPlayerGuid()||P.VMSession.Pin()!=S)
        {Error=TEXT("Content commands require the active character");return false;}
        const FString Text=Str(TEXT("text"));bool Target=false;
        if(!ACEPluginContent::Command(Text,Target)){Error=TEXT("Unsupported content command or arguments");return false;}
        const double Now=FPlatformTime::Seconds();
        if(Now<P.NextCommand){Error=TEXT("Content command rate limit: one per second");return false;}
        if(Target)
        {
            double Id=0;FACEWorldObject O;
            if(!R->TryGetNumberField(TEXT("target"),Id)||!FMath::IsFinite(Id)||Id<1||Id>MAX_uint32||Id!=FMath::FloorToDouble(Id)
                ||int32(uint32(Id))!=C->GetSelectedObject().Guid||!C->GetWorldObject(int32(uint32(Id)),O)||O.bIsPlayer||O.Guid==C->GetPlayerGuid())
            {Error=TEXT("Select the intended non-player object again");return false;}
            // ACE's legacy commands use the last manual appraisal. Identify and
            // Talk share the ordered WeenieQueue, just like retail. Clear stale
            // health/mana targets too: /delete otherwise prefers those targets.
            if(Text.TrimStartAndEnd().Equals(TEXT("/delete"),ESearchCase::IgnoreCase))
            {S->SendGameActionU32(ACEGameAction::QueryHealth,0);S->SendGameActionU32(ACEGameAction::QueryItemMana,0);}
            S->SendIdentifyObject(O.Guid,false);
        }
        P.NextCommand=Now+1;S->SendTalk(Text);Reply->SetStringField(TEXT("command"),Text);
        // A queued command is not a server acknowledgement; responses stay in chat.
        return true;
    }
    const FString Root=ContentRoot(P);Reply->SetStringField(TEXT("root"),Root);
    if(Op==TEXT("list"))
    {
        const FString Kind=Str(TEXT("kind")),Format=Str(TEXT("format"));
        if(!TArray<FString>{TEXT("weenies"),TEXT("landblocks"),TEXT("quests"),TEXT("recipes")}.Contains(Kind)
            ||(Format!=TEXT("json")&&Format!=TEXT("sql"))){Error=TEXT("Choose a content type and JSON or SQL");return false;}
        const FString Folder=Root/Format/Kind;
        TArray<FString> Files;IFileManager::Get().FindFiles(Files,*(Folder/(TEXT("*.")+Format)),true,false);Files.Sort();
        const FString Filter=Str(TEXT("filter")).Left(128);double Offset=0;R->TryGetNumberField(TEXT("offset"),Offset);
        const int32 Start=FMath::IsFinite(Offset)?FMath::Clamp(int32(FMath::Clamp(Offset,0.,1000000.)),0,1000000):0;
        TArray<TSharedPtr<FJsonValue>> Entries;int32 Total=0;
        for(const auto& File:Files)if(Filter.IsEmpty()||File.Contains(Filter))
        {
            if(Total>=Start&&Entries.Num()<128&&File.Len()<=220)Entries.Add(MakeShared<FJsonValueString>(Format/TEXT("")/Kind/File));
            ++Total;
        }
        Reply->SetArrayField(TEXT("files"),Entries);Reply->SetNumberField(TEXT("total"),Total);return true;
    }
    if(Op==TEXT("edit"))
    {
        const FString Name=Str(TEXT("name"));
        if(!ACEPluginContent::RelativeFile(Name)){Error=TEXT("Choose a JSON or SQL file inside the content folder");return false;}
        const FString File=Root/Name;
        if(!IFileManager::Get().FileExists(*File)){Error=TEXT("File no longer exists");return false;}
#if PLATFORM_WINDOWS
        // SQL file associations may execute queries. Always open a text editor.
        const FString Args=TEXT("\"")+File+TEXT("\"");
        auto Process=FPlatformProcess::CreateProc(TEXT("notepad.exe"),*Args,true,false,false,nullptr,0,nullptr,nullptr);
        const bool Ok=Process.IsValid();if(Ok)FPlatformProcess::CloseProc(Process);return Ok;
#else
        return FPlatformProcess::LaunchFileInDefaultExternalApplication(*File,nullptr,ELaunchVerb::Edit,false);
#endif
    }
    Error=TEXT("Unknown content operation");return false;
}
