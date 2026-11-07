#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonSerializer.h"
#include "Mods/ACEPluginSubsystem.h"
#include "Engine/GameInstance.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableText.h"
#include "Widgets/Text/STextBlock.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEPluginLimitTest,"ACE.Plugins.EnabledLimit",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACEPluginLimitTest::RunTest(const FString&)
{
    auto* GI=NewObject<UGameInstance>();GI->Init();auto* H=GI->GetSubsystem<UACEPluginSubsystem>();
    const FString Root=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("Automation")/(TEXT("PluginLimit-")+FGuid::NewGuid().ToString(EGuidFormats::Digits)));
    ON_SCOPE_EXIT {GI->Shutdown();}; // Keep the isolated source-only fixture for diagnostics.
    H->StorageRoot=Root;H->Settings=MakeShared<FJsonObject>();H->Discover();
    const int32 Bundled=H->Plugins.Num();
    for(const auto& P:H->Plugins)H->SetEnabled(P->Id,false);
    TestEqual(TEXT("Legacy preferences default to 16 enabled plugins"),H->GetEnabledPluginLimit(),16);
    auto Id=[](int32 Index){return FString::Printf(TEXT("limit_test_%02d"),Index);};
    for(int32 I=0;I<24;++I)
    {
        const FString Dir=Root/TEXT("Installed")/Id(I);IFileManager::Get().MakeDirectory(*Dir,true);
        const FString Manifest=FString::Printf(TEXT(R"({"id":"%s","api":2,"mode":"companion","permissions":["ui"]})"),*Id(I));
        TestTrue(TEXT("Create third-party manifest"),FFileHelper::SaveStringToFile(Manifest,*(Dir/TEXT("plugin.json"))));
    }
    H->Discover();
    TestEqual(TEXT("All bundled and 24 third-party plugins are discovered"),H->Plugins.Num(),Bundled+24);
    TestEqual(TEXT("Disabled plugins use no slots"),H->GetEnabledPluginCount(),0);
    auto Last=H->Find(Id(23));
    if(!TestTrue(TEXT("Last third-party plugin is not hidden by discovery order"),Last.IsValid()))return false;
    H->SetEnabled(Id(23),true);
    TestTrue(TEXT("A plugin after the old discovery cutoff can be enabled"),Last->Enabled);
    TestFalse(TEXT("Enabling a plugin does not start its script"),Last->Running);
    TestEqual(TEXT("Enabled tools count without a running script"),H->GetEnabledPluginCount(),1);
    for(int32 I=0;I<15;++I)H->SetEnabled(Id(I),true);
    H->SetEnabled(Id(15),true);
    TestEqual(TEXT("Default limit allows 16 enabled, regardless of disabled entries"),H->GetEnabledPluginCount(),16);
    TestFalse(TEXT("Seventeenth enable is rejected"),H->Find(Id(15))->Enabled);
    TestTrue(TEXT("Rejection explains how to increase the allowance"),H->Notice.Contains(TEXT("Plugin Manager")));
    TestFalse(TEXT("Rejected enable does not persist consent"),H->Settings->HasField(Id(15)));
    H->SetEnabled(Id(0),false);H->SetEnabled(Id(15),true);
    TestTrue(TEXT("Disabling immediately frees a slot"),H->Find(Id(15))->Enabled);
    TestFalse(TEXT("Lower limit cannot silently stop enabled plugins"),H->SetEnabledPluginLimit(4));
    TestEqual(TEXT("Rejected change preserves the limit"),H->GetEnabledPluginLimit(),16);

    if(FSlateApplication::IsInitialized())
    {
        auto Panel=H->MakePanel();
        TFunction<TSharedPtr<SWidget>(TSharedRef<SWidget>,FName,bool)> Find;
        Find=[&](TSharedRef<SWidget> W,FName Name,bool Tag)->TSharedPtr<SWidget>
        {
            if(Tag?W->GetTag()==Name:W->GetType()==Name)return W;
            auto* Children=W->GetChildren();
            for(int32 I=0;I<Children->Num();++I)if(auto Match=Find(Children->GetChildAt(I),Name,Tag))return Match;
            return nullptr;
        };
        auto Limit=Find(Panel,TEXT("PluginEnabledLimit"),true);
        auto Count=StaticCastSharedPtr<STextBlock>(Find(Panel,TEXT("PluginEnabledCount"),true));
        auto Toggle=StaticCastSharedPtr<SCheckBox>(Find(Panel,FName(*(TEXT("PluginEnabled.")+Id(16))),true));
        auto Feedback=StaticCastSharedPtr<STextBlock>(Find(Panel,TEXT("PluginManagerNotice"),true));
        if(!TestTrue(TEXT("Manager exposes limit, count, later plugins and feedback"),Limit&&Count&&Toggle&&Feedback))return false;
        const FKeyEvent Accept(EKeys::SpaceBar,FModifierKeysState(),0,false,0,0);
        auto Click=[&](){Toggle->OnKeyDown(FGeometry(),Accept);Toggle->OnKeyUp(FGeometry(),Accept);};
        Click();
        TestEqual(TEXT("Blocked checkbox stays unchecked"),Toggle->GetCheckedState(),ECheckBoxState::Unchecked);
        TestTrue(TEXT("Blocked checkbox shows feedback without refreshing"),Feedback->GetText().ToString().Contains(TEXT("limit (16) reached")));
        auto Input=StaticCastSharedPtr<SEditableText>(Find(Limit.ToSharedRef(),TEXT("SEditableText"),false));
        if(!TestTrue(TEXT("Limit has an editable numeric field"),Input.IsValid()))return false;
        Input->SetText(FText::FromString(TEXT("32")));
        StaticCastSharedPtr<SWidget>(Input)->OnKeyDown(FGeometry(),FKeyEvent(EKeys::Enter,FModifierKeysState(),0,false,0,0));
        TestEqual(TEXT("Committing manager field raises limit"),H->GetEnabledPluginLimit(),32);
        Click();
        TestTrue(TEXT("Existing manager can enable above 16 without refresh"),H->Find(Id(16))->Enabled);
        TestTrue(TEXT("Live count reflects newly enabled plugin and limit"),Count->GetText().ToString().StartsWith(TEXT("17 / 32")));
    }
    else TestTrue(TEXT("Raise enabled limit without Slate"),H->SetEnabledPluginLimit(32));
    for(int32 I=0;I<24;++I)H->SetEnabled(Id(I),true);
    TestEqual(TEXT("Raised limit permits all 24 third-party plugins"),H->GetEnabledPluginCount(),24);
    auto Reload=[&]()
    {
        FString Json;TSharedPtr<FJsonObject> Saved;
        if(!FFileHelper::LoadFileToString(Json,*(Root/TEXT("settings.json"))) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Saved))return false;
        H->Settings=Saved;H->Discover();return true;
    };
    TestTrue(TEXT("Read preferences from disk and rediscover"),Reload());
    TestEqual(TEXT("Configured limit survives restart"),H->GetEnabledPluginLimit(),32);
    TestEqual(TEXT("Enabled choices survive restart above old limit"),H->GetEnabledPluginCount(),24);

    // A manually lowered setting must not hide the plugin or erase its grant.
    H->Settings->SetNumberField(TEXT("plugins.enabled_limit"),2);H->Discover();
    TestEqual(TEXT("Discovery applies configured enabled allowance"),H->GetEnabledPluginCount(),2);
    TestEqual(TEXT("Even over-limit plugins remain listed"),H->Plugins.Num(),Bundled+24);
    TestTrue(TEXT("Over-limit status explains how to enable"),H->Find(Id(23))->Status.Contains(TEXT("limit")));
    TestEqual(TEXT("Existing permission grant retained"),H->Settings->GetStringField(Id(23)),FString(TEXT("enabled:ui")));
    TestTrue(TEXT("Zero removes enabled limit"),H->SetEnabledPluginLimit(0));H->Discover();
    TestEqual(TEXT("Unlimited restores approved plugins"),H->GetEnabledPluginCount(),24);
    TestTrue(TEXT("Unlimited choice persists"),Reload());
    TestEqual(TEXT("Reloaded unlimited setting"),H->GetEnabledPluginLimit(),0);
    TestFalse(TEXT("Negative configured limit is rejected"),H->SetEnabledPluginLimit(-1));
    H->Settings->SetNumberField(TEXT("plugins.enabled_limit"),-1);
    TestEqual(TEXT("Malformed disk limit safely defaults"),H->GetEnabledPluginLimit(),16);
    H->Settings->SetNumberField(TEXT("plugins.enabled_limit"),1.e30);
    TestEqual(TEXT("Oversize disk limit does not overflow"),H->GetEnabledPluginLimit(),16);
    H->Settings->SetNumberField(TEXT("plugins.enabled_limit"),1.5);
    TestEqual(TEXT("Fractional disk limit is not silently rounded"),H->GetEnabledPluginLimit(),16);
    return true;
}
#endif
