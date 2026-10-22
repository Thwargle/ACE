#include "Mods/ACEPluginStyle.h"
#include "Mods/ACEPluginCosmetics.h"
#include "Mods/ACEPluginSubsystem.h"
#include "Engine/GameInstance.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
FString CosmeticPanelField(const TSharedPtr<FJsonObject>& O,const TCHAR* Key,const FString& Default=FString())
{ FString Value;return O&&O->TryGetStringField(Key,Value)?Value:Default; }
#if PLATFORM_WINDOWS
bool Name(const FString& Value)
{ if(Value.IsEmpty()||Value.Len()>64)return false;for(TCHAR C:Value)if(!FChar::IsAlnum(C)&&C!='_'&&C!='-')return false;return true; }
#endif
TSharedRef<SWidget> Label(const FString& Text)
{ return SNew(SACEPluginText).Font(FACEPluginStyle::NativeFont("Regular")).Text(FText::FromString(Text)).AutoWrapText(true); }
}

bool UACEPluginCosmetics::Initialize(UACEPluginSubsystem* InHost,const FACEClientPlugin& Plugin,FString& Error)
{
#if !PLATFORM_WINDOWS
    Error=TEXT("Local cosmetic companion tools API 1 requires Windows x64.");return false;
#else
    Host=InHost;PluginId=Plugin.Id;PluginName=Plugin.Name;PluginDirectory=FPaths::ConvertRelativePathToFull(Plugin.Directory);
    const TSharedPtr<FJsonObject>* Config=nullptr;
    if(!Plugin.Manifest->TryGetObjectField(TEXT("cosmetics"),Config)) {Error=TEXT("Missing cosmetics configuration.");return false;}
    Configuration=*Config;
    Configuration->TryGetBoolField(TEXT("paperdoll"),bPaperdoll);
    const FString Namespace=CosmeticPanelField(Configuration,TEXT("cache_namespace")),Helper=CosmeticPanelField(Configuration,TEXT("helper"));
    if(!Name(Namespace)||Helper.IsEmpty()||!FPaths::IsRelative(Helper)||Helper.Contains(TEXT(".."))||Helper.Contains(TEXT(":"))||!Helper.EndsWith(TEXT(".exe")))
    {Error=TEXT("Use a safe cache namespace and a relative bundled .exe path.");return false;}
    HelperPath=FPaths::ConvertRelativePathToFull(PluginDirectory/Helper);
    CacheRoot=FPaths::Combine(FPlatformMisc::GetEnvironmentVariable(TEXT("LOCALAPPDATA")),Namespace);
    if(!FPaths::IsUnderDirectory(HelperPath,PluginDirectory)||!LocalFile(HelperPath)||!LocalFile(CacheRoot))
    {Error=TEXT("Companion tools and caches must use local paths without symlinks or junctions.");return false;}
    SourceLabel=CosmeticPanelField(Configuration,TEXT("source_label"),TEXT("source"));
    const TArray<TSharedPtr<FJsonValue>>* Values=nullptr;
    if(!Configuration->TryGetArrayField(TEXT("fields"),Values)||Values->Num()>24||Values->IsEmpty())
    {Error=TEXT("Supply 1–24 converter fields.");return false;}
    TSet<FString> Options;
    for(const auto& Value:*Values)
    {
        auto O=Value->AsObject();const FString Key=CosmeticPanelField(O,TEXT("key")),Option=CosmeticPanelField(O,TEXT("option"));
        if(!O||!Name(Key)||Inputs.Contains(Key)||!Option.StartsWith(TEXT("--"))||!Name(Option.Mid(2))||Options.Contains(Option))
        {Error=TEXT("Converter fields need unique keys and --option names.");return false;}
        Inputs.Add(Key,CosmeticPanelField(O,TEXT("default")));Options.Add(Option);Fields.Add(O);
    }
    if(Configuration->TryGetArrayField(TEXT("presets"),Values))
    {
        if(Values->Num()>32){Error=TEXT("At most 32 presets are supported.");return false;}
        for(const auto& Value:*Values)if(auto O=Value->AsObject())Presets.Add(O);
    }
    RegisterLuaService();
    Ticker=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this,&UACEPluginCosmetics::Tick),0.f);
    return true;
#endif
}

void UACEPluginCosmetics::RegisterLuaService()
{
    auto Adapter=MakeShared<FACEPluginLocalService>();Adapter->Permissions={TEXT("local_cosmetics"),TEXT("local_tools")};
    TWeakObjectPtr<UACEPluginCosmetics> Self(this);
    Adapter->Snapshot=[Self](){return Self.IsValid()?Self->ServiceSnapshot():MakeShared<FJsonObject>();};
    Adapter->Execute=[Self](const TSharedPtr<FJsonObject>& I,FString& Error)
    {if(!Self.IsValid()){Error=TEXT("Cosmetic runtime unavailable");return false;}return Self->ServiceExecute(I,Error);};
    Adapter->Stop=[Self](){if(Self.IsValid())Self->Disable();};
    Adapter->MakePanel=[Self]()->TSharedRef<SWidget>{return Self.IsValid()?Self->MakePanel():Label(TEXT("Cosmetic runtime unavailable"));};
    Host->RegisterLocalService(PluginId,Adapter);
}

TSharedPtr<FJsonObject> UACEPluginCosmetics::ServiceSnapshot() const
{
    auto Result=MakeShared<FJsonObject>();
    Result->SetNumberField(TEXT("service_api"),1);
    Result->SetBoolField(TEXT("busy"),ToolProcess.IsValid());
    Result->SetBoolField(TEXT("has_selection"),FPaths::FileExists(CacheRoot/TEXT("active.json")));
    Result->SetNumberField(TEXT("request"),PendingRequest?RequestSerial:0);
    Result->SetStringField(TEXT("status"),Status);
    return Result;
}

void UACEPluginCosmetics::QueueRequest(const FString& Operation)
{
    if(!Host||!Host->Plugins.ContainsByPredicate([this](const auto& P){return P->Id==PluginId&&P->Enabled&&P->Running;}))
    {Tell(TEXT("Enable ")+PluginName+TEXT(" in Plugins first."));return;}
    if(ToolProcess.IsValid()&&Operation!=TEXT("off"))return;
    if(Operation==TEXT("off"))Disable();
    PendingRequest=MakeShared<FJsonObject>();
    PendingRequest->SetStringField(TEXT("operation"),Operation);
    for(const auto& Pair:Inputs)PendingRequest->SetStringField(Pair.Key,Pair.Value.TrimStartAndEnd());
    if(bPaperdoll && (Operation==TEXT("convert")||Operation==TEXT("preview")))
        if(auto* Selected=SlotSelections.Find(SelectedSlot);Selected&&Selected->IsValid())
            PendingRequest->SetObjectField(TEXT("catalog_item"),*Selected);
    if(bPaperdoll && Operation==TEXT("apply-outfit"))
    {
        auto Selections=MakeShared<FJsonObject>();
        for(const auto& Pair:SlotSelections)
        {
            const FString Target=Pair.Key==TEXT("offhand") && CosmeticPanelField(Pair.Value,TEXT("sourceSlot"))==TEXT("shield")?TEXT("shield"):Pair.Key;
            Selections->SetStringField(Target,CosmeticPanelField(Pair.Value,TEXT("cacheId")));
        }
        PendingRequest->SetObjectField(TEXT("selections"),Selections);
    }
    if(Operation==TEXT("clear-slot")){SlotSelections.Remove(SelectedSlot);PendingRequest->SetStringField(TEXT("slot"),SelectedSlot);}
    if(++RequestSerial==0)++RequestSerial;
}

bool UACEPluginCosmetics::ServiceExecute(const TSharedPtr<FJsonObject>& Intent,FString& Error)
{
    if(CosmeticPanelField(Intent,TEXT("action"))!=TEXT("local_cosmetics")){Error=TEXT("Only local cosmetic requests are permitted");return false;}
    FString Operation=CosmeticPanelField(Intent,TEXT("operation"));TSharedPtr<FJsonObject> Request;
    if(Intent->HasField(TEXT("request")))
    {
        double Serial=0;
        if(!Intent->TryGetNumberField(TEXT("request"),Serial)||!PendingRequest||Serial!=RequestSerial)
        {Error=TEXT("Invalid or expired cosmetic request");return false;}
        Request=PendingRequest;PendingRequest.Reset();Operation=CosmeticPanelField(Request,TEXT("operation"));
    }
    else if(Operation!=TEXT("scan")&&Operation!=TEXT("reload"))
    {Error=TEXT("This operation requires a request from the cosmetic window");return false;}
    if(Operation==TEXT("off")){Disable();return true;}
    if(ToolProcess.IsValid()){Tell(TEXT("A local check is already running."));return true;}
    if(Operation==TEXT("reload")){RunTool({TEXT("verify-active")},true);return true;}
    if(Operation==TEXT("scan"))
    {
        TArray<FString> Args{TEXT("scan")};const FString Root=CosmeticPanelField(Request,TEXT("root"));
        if(!Root.IsEmpty())Args.Append({TEXT("--root"),Root});RunTool(Args);return true;
    }
    if(bPaperdoll && (Operation==TEXT("scan-items")||Operation==TEXT("catalog-items")))
    {
        TArray<FString> Args{Operation};
        for(const TCHAR* Key:{TEXT("root"),TEXT("product"),TEXT("listfile"),TEXT("filter")})
        {
            if(Operation==TEXT("catalog-items")&&FString(Key)!=TEXT("root")&&FString(Key)!=TEXT("product"))continue;
            const FString Value=CosmeticPanelField(Request,Key);if(!Value.IsEmpty())Args.Append({FString(TEXT("--"))+Key,Value});
        }
        if(CosmeticPanelField(Request,TEXT("root")).IsEmpty()){Tell(TEXT("Choose a local installation before scanning items."));return true;}
        RunTool(Args);return true;
    }
    if(bPaperdoll && Operation==TEXT("clear-slot"))
    {RunTool({TEXT("clear-slot"),TEXT("--slot"),CosmeticPanelField(Request,TEXT("slot"))},true);return true;}
    if(bPaperdoll && Operation==TEXT("apply-outfit"))
    {
        const TSharedPtr<FJsonObject>* Selections=nullptr;
        if(!Request->TryGetObjectField(TEXT("selections"),Selections)){Error=TEXT("Missing outfit selections");return false;}
        TArray<FString> Args{TEXT("apply-outfit")};
        for(const auto& Pair:(*Selections)->Values)Args.Append({TEXT("--selection"),FString(*Pair.Key)+TEXT("=")+Pair.Value->AsString()});
        RunTool(Args,true);return true;
    }
    if(bPaperdoll && Operation==TEXT("preview"))
    {
        const TSharedPtr<FJsonObject>* Item=nullptr;
        if(!Request->TryGetObjectField(TEXT("catalog_item"),Item)){Tell(TEXT("Choose an appearance first."));return true;}
        PreviewCacheId=CosmeticPanelField(*Item,TEXT("cacheId"));
        RunTool({TEXT("prepare-appearance"),TEXT("--id"),PreviewCacheId});return true;
    }
    if(Operation!=TEXT("convert")){Error=TEXT("Unknown cosmetic operation");return false;}
    const TSharedPtr<FJsonObject>* Entry=nullptr;
    if(bPaperdoll&&Request->TryGetObjectField(TEXT("catalog_item"),Entry))
    {
        bool Supported=false;(*Entry)->TryGetBoolField(TEXT("supported"),Supported);
        if(!Supported){Tell(CosmeticPanelField(*Entry,TEXT("reason"),TEXT("This appearance is not supported.")));return true;}
        RunTool({TEXT("convert-select"),TEXT("--appearance"),CosmeticPanelField(*Entry,TEXT("cacheId")),TEXT("--slot"),CosmeticPanelField(Request,TEXT("slot"))},true);return true;
    }
    TArray<FString> Args{TEXT("convert-select")};
    for(const auto& O:Fields)
    {
        bool ScanOnly=false;O->TryGetBoolField(TEXT("scan_only"),ScanOnly);if(ScanOnly)continue;
        const FString Key=CosmeticPanelField(O,TEXT("key")),Value=CosmeticPanelField(Request,*Key);bool Required=false;O->TryGetBoolField(TEXT("required"),Required);
        if(Value.IsEmpty())
        {if(Required){Tell(TEXT("Enter ")+CosmeticPanelField(O,TEXT("label"),Key)+TEXT(" before converting."));return true;}continue;}
        Args.Append({CosmeticPanelField(O,TEXT("option")),CosmeticPanelField(O,TEXT("prefix"))+Value});
    }
    RunTool(Args,true);return true;
}

void UACEPluginCosmetics::UpdateInstallations(const FString& Json)
{
    TSharedPtr<FJsonObject> Result;const TArray<TSharedPtr<FJsonValue>>* Found=nullptr;
    if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Result)||!Result||!Result->TryGetArrayField(TEXT("installations"),Found))
    {Tell(TEXT("Invalid installation discovery response."));return;}
    Installations.Reset();
    for(const auto& Value:*Found)if(auto O=Value->AsObject();O&&!CosmeticPanelField(O,TEXT("root")).IsEmpty()&&Installations.Num()<32)Installations.Add(O);
    if(Installations.Num()==1)for(const TCHAR* Key:{TEXT("root"),TEXT("product")})if(Inputs.Contains(Key))Inputs[Key]=CosmeticPanelField(Installations[0],Key);
    if(auto List=InstallationList.Pin())
    {
        List->ClearChildren();TWeakObjectPtr<UACEPluginCosmetics> Self(this);
        for(const auto& Install:Installations)
            List->AddSlot().AutoHeight().Padding(0,3)[SNew(SACEPluginButton).ContentPadding(FMargin(FACEPluginStyle::ButtonPadding)).ButtonStyle(&FACEPluginStyle::Current()->Button).IsFocusable(false)
                .OnClicked_Lambda([Self,Install](){if(Self.IsValid())for(const TCHAR* Key:{TEXT("root"),TEXT("product")})if(Self->Inputs.Contains(Key))Self->Inputs[Key]=CosmeticPanelField(Install,Key);return FReply::Handled();})
                [Label(CosmeticPanelField(Install,TEXT("product"))+TEXT(" ")+CosmeticPanelField(Install,TEXT("version"))+TEXT(" — ")+CosmeticPanelField(Install,TEXT("root")))]];
    }
    Tell(Installations.IsEmpty()?TEXT("No usable ")+SourceLabel+TEXT(" installation found. Enter its local root folder and search again."):
        FString::Printf(TEXT("Found %d local installation(s). Choose one, then convert an appearance."),Installations.Num()));
    if(bPaperdoll&&!Inputs.FindRef(TEXT("root")).IsEmpty()&&!Inputs.FindRef(TEXT("product")).IsEmpty())
        RunTool({TEXT("catalog-items"),TEXT("--root"),Inputs.FindRef(TEXT("root")),TEXT("--product"),Inputs.FindRef(TEXT("product"))});

}

TSharedRef<SWidget> UACEPluginCosmetics::MakePanel()
{
    if(bPaperdoll)return MakePaperdollPanel();
    TWeakObjectPtr<UACEPluginCosmetics> Self(this);auto Body=SNew(SVerticalBox);
    auto Add=[&](TSharedRef<SWidget> W){Body->AddSlot().AutoHeight().Padding(4)[W];};
    auto Button=[&](const FString& Caption,TFunction<void()> Action,bool bDuringWork=false)->TSharedRef<SWidget>
    {return SNew(SACEPluginButton).ContentPadding(FMargin(FACEPluginStyle::ButtonPadding)).ButtonStyle(&FACEPluginStyle::Current()->Button).IsFocusable(false).IsEnabled_Lambda([Self,bDuringWork](){return Self.IsValid()&&(bDuringWork||!Self->ToolProcess.IsValid());})
        .OnClicked_Lambda([Self,Action](){if(Self.IsValid())Action();return FReply::Handled();})[Label(Caption)];};
    Add(Label(TEXT("Local appearance only · one weapon or shield · disable in Plugins to restore AC gear")));
    Add(SNew(SACEPluginText).Font(FACEPluginStyle::NativeFont("Regular")).AutoWrapText(true).Text_Lambda([Self](){return FText::FromString(Self.IsValid()?Self->Status:TEXT("Unavailable"));}));
    Add(Button(TEXT("Search local ")+SourceLabel+TEXT(" installations"),[Self](){Self->QueueRequest(TEXT("scan"));}));
    auto List=SNew(SVerticalBox);InstallationList=List;Add(List);
    for(const auto& Install:Installations)
        List->AddSlot().AutoHeight().Padding(0,3)[Button(CosmeticPanelField(Install,TEXT("product"))+TEXT(" — ")+CosmeticPanelField(Install,TEXT("root")),[Self,Install](){for(const TCHAR* Key:{TEXT("root"),TEXT("product")})if(Self->Inputs.Contains(Key))Self->Inputs[Key]=CosmeticPanelField(Install,Key);})];
    for(const auto& Preset:Presets)
        Add(Button(CosmeticPanelField(Preset,TEXT("name")),[Self,Preset](){const TSharedPtr<FJsonObject>* Values=nullptr;if(Preset->TryGetObjectField(TEXT("values"),Values))for(auto& Pair:Self->Inputs){FString Value;if((*Values)->TryGetStringField(Pair.Key,Value))Pair.Value=Value;}}));
    Add(Label(CosmeticPanelField(Configuration,TEXT("help"))));
    for(const auto& O:Fields)
    {
        const FString Key=CosmeticPanelField(O,TEXT("key"));Add(Label(CosmeticPanelField(O,TEXT("label"),Key)));
        Add(SNew(SEditableTextBox).Style(&FACEPluginStyle::Current()->Edit).Text_Lambda([Self,Key](){return FText::FromString(Self.IsValid()?Self->Inputs.FindRef(Key):FString());})
            .OnTextChanged_Lambda([Self,Key](const FText& T){if(Self.IsValid())Self->Inputs[Key]=T.ToString();}));
    }
    Add(Button(TEXT("Convert and wear"),[Self](){Self->QueueRequest(TEXT("convert"));}));
    Add(Button(TEXT("Recheck and wear saved selection"),[Self](){Self->QueueRequest(TEXT("reload"));}));
    Add(Button(TEXT("Restore AC appearance / cancel conversion"),[Self](){Self->QueueRequest(TEXT("off"));},true));
    return Body;
}
