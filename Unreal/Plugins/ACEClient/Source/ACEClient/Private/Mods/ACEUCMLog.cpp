#include "Mods/ACEUCMLog.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonSerializer.h"

namespace
{
    FString Bounded(const FString& Text)
    { return Text.Left(FACEUCMLog::TextLimit).Replace(TEXT("\r"),TEXT(" ")).Replace(TEXT("\n"),TEXT(" ")); }
    TSharedRef<FJsonObject> Encode(const FACEUCMLogEntry& E)
    {
        auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("time"),E.Time.ToIso8601());
        O->SetNumberField(TEXT("level"),int32(E.Level));O->SetStringField(TEXT("message"),E.Message);
        O->SetStringField(TEXT("context"),E.Context);O->SetNumberField(TEXT("repeats"),E.Repeats);return O;
    }
    TSharedPtr<FACEUCMLogEntry> Decode(const TSharedPtr<FJsonValue>& Value)
    {
        if(!Value||Value->Type!=EJson::Object)return nullptr;
        auto O=Value->AsObject();auto E=MakeShared<FACEUCMLogEntry>();FString Time;double Level=0,Repeats=1;
        if(!O->TryGetStringField(TEXT("time"),Time)||!FDateTime::ParseIso8601(*Time,E->Time)
            ||!O->TryGetStringField(TEXT("message"),E->Message)||!O->TryGetNumberField(TEXT("level"),Level)
            ||Level<0||Level>2||FMath::FloorToDouble(Level)!=Level)return nullptr;
        O->TryGetStringField(TEXT("context"),E->Context);O->TryGetNumberField(TEXT("repeats"),Repeats);
        E->Message=Bounded(E->Message);E->Context=Bounded(E->Context);E->Level=EACEUCMLogLevel(int32(Level));
        E->Repeats=FMath::IsFinite(Repeats)?int32(FMath::Clamp(Repeats,1.,1000000.)):1;return E;
    }
}
FString FACEUCMLogEntry::ToText() const
{
    const TCHAR* Label=Level==EACEUCMLogLevel::Error?TEXT("ERROR"):Level==EACEUCMLogLevel::Warning?TEXT("WARN"):TEXT("INFO");
    return FString::Printf(TEXT("%s  %s  %s%s%s"),*Time.ToString(TEXT("%Y-%m-%d %H:%M:%S")),Label,*Message,
        Repeats>1?*FString::Printf(TEXT(" (x%d)"),Repeats):TEXT(""),Context.IsEmpty()?TEXT(""):*(TEXT("\n")+Context));
}
void FACEUCMLog::Add(const FString& Message,const FString& Context,EACEUCMLogLevel Level,FDateTime Time)
{
    if(Message.IsEmpty())return;
    const FString M=Bounded(Message),C=Bounded(Context);
    TSharedPtr<FACEUCMLogEntry> E;
    if(!Entries.IsEmpty()&&Entries.Last()->Message==M&&Entries.Last()->Context==C&&Entries.Last()->Level==Level)
    {
        E=Entries.Last();E->Time=Time;E->Repeats=FMath::Min(1000000,E->Repeats+1);
    }
    else
    {
        E=MakeShared<FACEUCMLogEntry>();E->Time=Time;E->Message=M;E->Context=C;E->Level=Level;
        if(Entries.Num()>=Capacity)Entries.RemoveAt(0,Entries.Num()-Capacity+1,EAllowShrinking::No);
        Entries.Add(E);
    }
    // A continuing combat cycle must not erase the explanation for a paused route.
    if(Level!=EACEUCMLogLevel::Info)LastProblem=MakeShared<FACEUCMLogEntry>(*E);
    ++Revision;Dirty=true;
}
void FACEUCMLog::Clear() { Entries.Reset();LastProblem.Reset();++Revision;Dirty=true; }
bool FACEUCMLog::Save(const FString& Path)
{
    auto Root=MakeShared<FJsonObject>();TArray<TSharedPtr<FJsonValue>> Rows;
    for(const auto& E:Entries)Rows.Add(MakeShared<FJsonValueObject>(Encode(*E)));
    Root->SetArrayField(TEXT("events"),Rows);if(LastProblem)Root->SetObjectField(TEXT("last_problem"),Encode(*LastProblem));
    FString Json;FJsonSerializer::Serialize(Root,TJsonWriterFactory<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json));
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path),true);
    const FString Temp=Path+TEXT(".tmp");
    const bool Ok=FFileHelper::SaveStringToFile(Json,*Temp,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
        &&IFileManager::Get().Move(*Path,*Temp,true,true);
    if(Ok)Dirty=false;return Ok;
}
bool FACEUCMLog::Load(const FString& Path)
{
    if(IFileManager::Get().FileSize(*Path)>FileLimit)return false;
    FString Json;TSharedPtr<FJsonObject> Root;const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;
    if(!FFileHelper::LoadFileToString(Json,*Path)||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root)
        ||!Root||!Root->TryGetArrayField(TEXT("events"),Rows))return false;
    Entries.Reset();LastProblem.Reset();
    for(int32 I=FMath::Max(0,Rows->Num()-Capacity);I<Rows->Num();++I)if(auto E=Decode((*Rows)[I]))Entries.Add(E);
    LastProblem=Decode(Root->TryGetField(TEXT("last_problem")));++Revision;Dirty=false;return true;
}
