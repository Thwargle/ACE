#include "Mods/ACEPluginLocalService.h"
#include "Mods/ACEPluginSubsystem.h"
#include "Mods/ACEPluginCosmetics.h"
#include "ACEPluginVM.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Widgets/Text/STextBlock.h"

void UACEPluginSubsystem::RegisterLocalService(const FString& Id, const TSharedRef<FACEPluginLocalService>& Service)
{
    if(LocalServices.Contains(Id)) UnregisterLocalService(Id);
    LocalServices.Add(Id,Service);
    if(auto P=Find(Id); P&&P->LocalService) P->LocalServiceStopped=false;
}

void UACEPluginSubsystem::UnregisterLocalService(const FString& Id)
{
    if(auto P=Find(Id); P&&P->LocalService) Stop(Id,TEXT("Client capability unloaded"));
    LocalServices.Remove(Id);
}

bool UACEPluginSubsystem::StartLocalService(FACEClientPlugin& P)
{
    P.LocalServiceStopped=true; // A failed script/capability must not retry every tick.
    const auto* Service=LocalServices.Find(P.Id);
    TArray<FString> Required; if(Service) Required=(*Service)->Permissions; Required.Sort();
    if(!P.Enabled || !Service || P.Permissions!=Required || (*Service)->Api!=1)
    { if(!P.Status.StartsWith(TEXT("Plugin configuration:"))) P.Status=TEXT("Requires a client build with the declared local service API and capabilities. Update the client, then re-enable this plugin."); return false; }
    if(P.Running) Stop(P.Id);
    FString Source,Error; const FString Path=P.Directory/TEXT("main.lua");
    if(IFileManager::Get().FileSize(*Path)>256*1024 || !FFileHelper::LoadFileToString(Source,*Path))
    { P.Status=TEXT("Cannot read main.lua (maximum 256 KiB)"); return false; }
    auto VM=MakeShared<FACEPluginVM>();
    if(!VM->Load(Source,Error)) { P.Status=TEXT("Script load failed: ")+Error; return false; }
    P.VM=VM; P.Running=true; P.LocalServiceStopped=false; P.NextDecision=0; P.Status=TEXT("Starting local cosmetics");
    return true;
}

void UACEPluginSubsystem::TickLocalServices()
{
    const double Now=FPlatformTime::Seconds();
    for(const auto& P:Plugins) if(P->LocalService && P->Enabled)
    {
        if(!P->Running && !P->LocalServiceStopped) StartLocalService(*P);
        if(!P->Running || Now<P->NextDecision) continue;
        P->NextDecision=Now+.5;
        const auto* Service=LocalServices.Find(P->Id);
        if(!Service) { Stop(P->Id,TEXT("Client capability unavailable")); continue; }
        auto Snapshot=(*Service)->Snapshot(); TSharedPtr<FJsonObject> Intent; FString Error;
        if(!P->VM->Step(Snapshot,P->Profile,Intent,Error) || (Intent && !(*Service)->Execute(Intent,Error)))
        { Stop(P->Id,TEXT("Local plugin stopped: ")+Error); continue; }
        if(Snapshot) Snapshot->TryGetStringField(TEXT("status"),P->Status);
    }
}

TSharedRef<SWidget> UACEPluginSubsystem::MakeLocalServicePanel(const FString& Id)
{
    if(auto P=Find(Id); P&&P->LocalService&&P->Enabled)
        if(const auto* Service=LocalServices.Find(Id)) return (*Service)->MakePanel();
    return SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(
        TEXT("This plugin's local service is unavailable. Check its status and required client API, then reinstall or update the plugin/client.")));
}

void UACEPluginSubsystem::CreateLocalCosmetic(FACEClientPlugin& P)
{
    auto* Service=NewObject<UACEPluginCosmetics>(this); FString Error;
    if(!Service->Initialize(this,P,Error)) { P.Status=TEXT("Plugin configuration: ")+Error; P.LocalServiceStopped=true; return; }
    LocalCosmetics.Add(P.Id,Service);
}

void UACEPluginSubsystem::ReleaseLocalCosmetics()
{
    for(auto& Pair:LocalCosmetics) { UnregisterLocalService(Pair.Key); Pair.Value->Shutdown(); }
    LocalCosmetics.Empty();
}

UACEPluginCosmetics* UACEPluginSubsystem::GetLocalCosmetic(const FString& Id) const
{ const auto* Found=LocalCosmetics.Find(Id); return Found?Found->Get():nullptr; }

void UACEPluginSubsystem::ClaimLocalCosmetic(const FString& Id)
{
    // A single owner prevents two providers from saving/restoring each other's
    // hidden original mesh state. The most recently verified selection wins.
    for(auto& Pair:LocalCosmetics) if(Pair.Key!=Id) Pair.Value->Disable();
}
