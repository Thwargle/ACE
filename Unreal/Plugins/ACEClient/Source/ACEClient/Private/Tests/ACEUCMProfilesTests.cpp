#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "HAL/FileManager.h"
#include "Mods/ACEUCMProfiles.h"
#include "Serialization/JsonSerializer.h"
#include "Mods/ACEPluginSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Slate/WidgetRenderer.h"
#include "ImageUtils.h"
#include "RenderingThread.h"
#include "Misc/App.h"
#include "Widgets/Text/STextBlock.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEUCMProfileStorageTest, "ACE.Plugins.LinkedProfiles",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACEUCMProfileStorageTest::RunTest(const FString&)
{
    using namespace ACEUCMProfiles;
    const FString Root = FPaths::ProjectSavedDir()/TEXT("Automation/UCMProfiles")/FGuid::NewGuid().ToString(EGuidFormats::Digits);
    IFileManager::Get().MakeDirectory(*(Root/TEXT("Profiles/ucm")), true);
    ON_SCOPE_EXIT { IFileManager::Get().DeleteDirectory(*Root, false, true); };
    const FString Legacy = TEXT(R"({"buffing":true,"combat":"auto","skill_margin":30,"summon_pets":true,"route":[{"cell":100,"x":1,"y":2,"z":3}],"navigation":true,"loot_rules":[{"label":"Copper","material":59}],"salvage_policy":{"default":[{"min":7,"max":10}]},"monsters":[{"name":"Rat","damage_type":1}],"states":[{"name":"Default"}],"vendor_rules":[{"item_name":"Scarab","quantity":100}],"future_character_option":42})");
    const FString Setup = Root/TEXT("Profiles/ucm/First.json");
    FFileHelper::SaveStringToFile(Legacy,*Setup);
    TSharedPtr<FJsonObject> Profile,Links; FString Error;
    TestTrue(TEXT("Existing monolithic setup loads"),Load(Root,TEXT("First"),Profile,Links,Error));
    auto Original=Copy(Profile);
    TestTrue(TEXT("Migration writes linked setup"),Save(Root,TEXT("First"),Profile,Links,nullptr,Error));
    FString Backup;FFileHelper::LoadFileToString(Backup,*(Root/TEXT("Profiles/Legacy/ucm/First.json")));
    TSharedPtr<FJsonObject> BackupObject;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Backup),BackupObject);
    TestTrue(TEXT("Migration preserves all original data in backup"),BackupObject && FJsonValue::CompareEqual(FJsonValueObject(Original),FJsonValueObject(BackupObject)));
    TestEqual(TEXT("All six sections have references"),Links->Values.Num(),6);
    TSharedPtr<FJsonObject> Reload,ReloadLinks;
    TestTrue(TEXT("Linked setup resolves"),Load(Root,TEXT("First"),Reload,ReloadLinks,Error));
    TestTrue(TEXT("Every setting survives migration including unknown future options"),FJsonValue::CompareEqual(FJsonValueObject(Original),FJsonValueObject(Reload)));
    TestFalse(TEXT("Character file does not contain route data"),Extract(Reload,TEXT("Character"))->HasField(TEXT("route")));
    TestTrue(TEXT("Run activity flags remain independent"),Extract(Reload,TEXT("ucm"))->HasField(TEXT("navigation")));
    auto Baseline=Copy(Profile);
    Profile->SetBoolField(TEXT("navigation"),false);
    TestTrue(TEXT("Second setup shares existing profiles"),Save(Root,TEXT("Second"),Profile,Links,Baseline,Error));
    Baseline=Copy(Profile);
    Profile->SetNumberField(TEXT("skill_margin"),12);
    Profile->GetArrayField(TEXT("vendor_rules"))[0]->AsObject()->SetNumberField(TEXT("quantity"),200);
    TestTrue(TEXT("Character and vendor edits save through shared references"),Save(Root,TEXT("Second"),Profile,Links,Baseline,Error));
    TestTrue(TEXT("First setup picks up shared changes"),Load(Root,TEXT("First"),Reload,ReloadLinks,Error));
    TestEqual(TEXT("Character changes propagate"),Reload->GetNumberField(TEXT("skill_margin")),12.);
    TestEqual(TEXT("Vendor changes propagate"),Reload->GetArrayField(TEXT("vendor_rules"))[0]->AsObject()->GetNumberField(TEXT("quantity")),200.);
    TestTrue(TEXT("Shared edits do not turn off another setup's navigation"),Reload->GetBoolField(TEXT("navigation")));
    TestTrue(TEXT("Create a separate route variation"),SaveSectionAs(Root,TEXT("Routes"),TEXT("Cave"),Profile,Error));
    TestFalse(TEXT("Save copy cannot overwrite a profile"),SaveSectionAs(Root,TEXT("Routes"),TEXT("Cave"),Profile,Error));
    TestTrue(TEXT("Swap only route reference"),Select(Root,TEXT("Routes"),TEXT("Cave"),Profile,Links,Error));
    Baseline=Copy(Profile);
    Profile->GetArrayField(TEXT("route"))[0]->AsObject()->SetNumberField(TEXT("x"),9);
    TestTrue(TEXT("Route variation saves independently"),Save(Root,TEXT("Second"),Profile,Links,Baseline,Error));
    Load(Root,TEXT("First"),Reload,ReloadLinks,Error);
    TestEqual(TEXT("Original route is unchanged"),Reload->GetArrayField(TEXT("route"))[0]->AsObject()->GetNumberField(TEXT("x")),1.);
    // Simulate an editor holding an old snapshot while another setup changes shared settings.
    auto Stale=Copy(Reload),StaleLinks=Copy(ReloadLinks),StaleBase=Copy(Reload);
    auto FreshBase=Copy(Reload);Reload->SetNumberField(TEXT("skill_margin"),5);
    Save(Root,TEXT("First"),Reload,ReloadLinks,FreshBase,Error);
    Stale->SetBoolField(TEXT("navigation"),false);
    TestTrue(TEXT("Editing a run flag does not overwrite a newer shared file"),Save(Root,TEXT("First"),Stale,StaleLinks,StaleBase,Error));
    TestEqual(TEXT("Unchanged shared section refreshes from disk"),Stale->GetNumberField(TEXT("skill_margin")),5.);
    Stale=Copy(StaleBase);Stale->SetNumberField(TEXT("skill_margin"),99);
    TestFalse(TEXT("Conflicting stale edits require reload"),Save(Root,TEXT("First"),Stale,StaleLinks,StaleBase,Error));
    Load(Root,TEXT("First"),Reload,ReloadLinks,Error);
    TestEqual(TEXT("Rejected stale edit leaves shared data intact"),Reload->GetNumberField(TEXT("skill_margin")),5.);
    const auto Unchanged=Copy(Reload);
    TestFalse(TEXT("Missing selection does not silently fall back"),Select(Root,TEXT("Loot"),TEXT("missing"),Reload,ReloadLinks,Error));
    TestTrue(TEXT("Failed selection preserves current data"),FJsonValue::CompareEqual(FJsonValueObject(Reload),FJsonValueObject(Unchanged)));
    TestFalse(TEXT("Path traversal rejected"),Select(Root,TEXT("Loot"),TEXT("../Character/First"),Reload,ReloadLinks,Error));
    FFileHelper::SaveStringToFile(TEXT("{\"skill_margin\":1}"),*(Root/TEXT("Profiles/Loot/Wrong.json")));
    TestFalse(TEXT("Wrong section contents cannot overwrite character settings"),Select(Root,TEXT("Loot"),TEXT("Wrong"),Reload,ReloadLinks,Error));
    FFileHelper::SaveStringToFile(TEXT("not json"),*(Root/TEXT("Profiles/Routes/Cave.json")));
    TestFalse(TEXT("Corrupt linked file prevents partial setup activation"),Load(Root,TEXT("Second"),Reload,ReloadLinks,Error));
    TestTrue(TEXT("Failed load keeps previously resolved profile"),FJsonValue::CompareEqual(FJsonValueObject(Reload),FJsonValueObject(Unchanged)));
    TestEqual(TEXT("Protections belong to Character"),SectionFor(TEXT("protection_profile")),FString(TEXT("Character")));
    TestEqual(TEXT("Custom banes belong to Character"),SectionFor(TEXT("bane_custom")),FString(TEXT("Character")));
    {
        auto* GI=NewObject<UGameInstance>();GI->Init();ON_SCOPE_EXIT{GI->Shutdown();};
        auto* H=GI->GetSubsystem<UACEPluginSubsystem>();H->StorageRoot=Root/TEXT("Host");H->Settings=MakeShared<FJsonObject>();
        TestTrue(TEXT("New setup creates Default references"),H->NewUCMSetup(TEXT("Hunt")));
        auto P=H->Find(TEXT("ucm"));if(!P)return false;
        for(const auto& Section:Sections())TestEqual(TEXT("New setup selects Default in every section"),H->UCMSectionName(Section),FString(TEXT("Default")));
        TestTrue(TEXT("New Character profile"),H->NewUCMSection(TEXT("Character"),TEXT("Hero")));
        P->Profile->SetNumberField(TEXT("protection_profile"),4);P->Profile->SetStringField(TEXT("bane_custom"),TEXT("BPS"));
        TestTrue(TEXT("Save current setup"),H->SaveProfile(P->Id,P->ProfileName,H->ProfileJson(P->Id),false));
        TestTrue(TEXT("New route creates empty file"),H->NewUCMSection(TEXT("Routes"),TEXT("OldPath")));
        auto Point=MakeShared<FJsonObject>();Point->SetNumberField(TEXT("cell"),0x7d640001);Point->SetNumberField(TEXT("x"),1);Point->SetNumberField(TEXT("y"),2);Point->SetNumberField(TEXT("z"),3);
        P->Profile->SetArrayField(TEXT("route"),{MakeShared<FJsonValueObject>(Point)});
        TestTrue(TEXT("Save populated route"),H->SaveProfile(P->Id,P->ProfileName,H->ProfileJson(P->Id),false));
        TestTrue(TEXT("Another new route starts blank"),H->NewUCMSection(TEXT("Routes"),TEXT("Blank")));
        TestEqual(TEXT("New route never copies previous waypoints"),P->Profile->GetArrayField(TEXT("route")).Num(),0);
        TestEqual(TEXT("Route creation preserves Character reference"),H->UCMSectionName(TEXT("Character")),FString(TEXT("Hero")));
        TestFalse(TEXT("New cannot overwrite an existing profile"),H->NewUCMSection(TEXT("Routes"),TEXT("OldPath")));
        H->SelectUCMSection(TEXT("Routes"),TEXT("OldPath"));TestEqual(TEXT("Original route remains intact"),P->Profile->GetArrayField(TEXT("route")).Num(),1);
        const FString Folder=H->UserDirectory()/TEXT("ImportInbox");IFileManager::Get().MakeDirectory(*Folder,true);
        const FString Nav=TEXT("uTank2 NAV 1.2\n2\n2\n0\n0\n0\n0.05\n0\n3\n0.01\n0\n0.05\n0\n1500\n");
        FFileHelper::SaveStringToFile(Nav,*(Folder/TEXT("Loop.nav")));
        TestTrue(TEXT("Folder import creates route section"),H->ImportUCMSection(TEXT("Loop.nav")));
        TestEqual(TEXT("Import preserves setup name"),P->ProfileName,FString(TEXT("Hunt")));
        TestEqual(TEXT("Import uses source file stem"),H->UCMSectionName(TEXT("Routes")),FString(TEXT("Loop")));
        TestEqual(TEXT("Imported points loaded for world preview"),P->Profile->GetArrayField(TEXT("route")).Num(),2);
        TestTrue(TEXT("Imported route has world preview"),P->Profile->GetBoolField(TEXT("route_preview")));
        TestTrue(TEXT("Repeated import creates distinct file"),H->ImportUCMSection(TEXT("Loop.nav")));
        TestEqual(TEXT("Repeated import adds suffix"),H->UCMSectionName(TEXT("Routes")),FString(TEXT("Loop_2")));
        FString AfterImport;FFileHelper::LoadFileToString(AfterImport,*(Folder/TEXT("Loop.nav")));TestEqual(TEXT("Import leaves original nav unchanged"),AfterImport,Nav);
        FFileHelper::SaveStringToFile(TEXT("UTL\r\n1\r\n1\r\nCopper\r\n\r\n0;1;12\r\n8\r\n6\r\n131\r\n"),*(Folder/TEXT("Copper.utl")));
        TestTrue(TEXT("Folder import creates Loot section"),H->ImportUCMSection(TEXT("Copper.utl")));
        TestEqual(TEXT("Loot import keeps selected route"),H->UCMSectionName(TEXT("Routes")),FString(TEXT("Loop_2")));
        TestEqual(TEXT("Loot import names active profile"),H->UCMSectionName(TEXT("Loot")),FString(TEXT("Copper")));
        TestEqual(TEXT("Loot rules loaded"),P->Profile->GetArrayField(TEXT("loot_rules")).Num(),1);
        FFileHelper::SaveStringToFile(TEXT("bad nav"),*(Folder/TEXT("Broken.nav")));
        TestFalse(TEXT("Broken import rejected without changing selected profile"),H->ImportUCMSection(TEXT("Broken.nav")));
        TestEqual(TEXT("Failed import preserves active route"),H->UCMSectionName(TEXT("Routes")),FString(TEXT("Loop_2")));
        TestTrue(TEXT("Second new setup"),H->NewUCMSetup(TEXT("Other")));
        for(const auto& Section:Sections())TestEqual(TEXT("Second setup does not inherit previous choices"),H->UCMSectionName(Section),FString(TEXT("Default")));
        TestTrue(TEXT("Share imported route between setups"),H->SelectUCMSection(TEXT("Routes"),TEXT("Loop_2")));
        TestTrue(TEXT("Delete referenced route"),H->DeleteUCMSection(TEXT("Routes"),TEXT("Loop_2")));
        TestEqual(TEXT("Active setup switches deleted route to Default"),H->UCMSectionName(TEXT("Routes")),FString(TEXT("Default")));
        TestFalse(TEXT("Deleted profile leaves chooser"),H->UCMSectionProfiles(TEXT("Routes")).Contains(TEXT("Loop_2")));
        TestTrue(TEXT("Other linked setup remains loadable"),H->LoadProfile(TEXT("ucm"),TEXT("Hunt")));
        TestEqual(TEXT("Other setup references Default after delete"),H->UCMSectionName(TEXT("Routes")),FString(TEXT("Default")));
        TestEqual(TEXT("Delete leaves unrelated Character intact"),H->UCMSectionName(TEXT("Character")),FString(TEXT("Hero")));
        TestEqual(TEXT("Protections survive setup switches"),P->Profile->GetNumberField(TEXT("protection_profile")),4.);
        TestEqual(TEXT("Custom banes survive setup switches"),P->Profile->GetStringField(TEXT("bane_custom")),FString(TEXT("BPS")));
        TArray<FString> Backups;IFileManager::Get().FindFiles(Backups,*(H->UserDirectory()/TEXT("Profiles/Deleted/Routes/*.json")),true,false);
        TestEqual(TEXT("Deleted file is recoverable"),Backups.Num(),1);
        TestFalse(TEXT("Default cannot be deleted"),H->DeleteUCMSection(TEXT("Routes"),TEXT("Default")));
        TestFalse(TEXT("New setup cannot overwrite Hunt"),H->NewUCMSetup(TEXT("Hunt")));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEUCMProfilePanelTest, "ACE.Plugins.LinkedProfilePanel",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACEUCMProfilePanelTest::RunTest(const FString&)
{
    auto* GI=NewObject<UGameInstance>();GI->Init();ON_SCOPE_EXIT { GI->Shutdown(); };
    auto* Host=GI->GetSubsystem<UACEPluginSubsystem>();
    auto CollectText=[](const TSharedRef<SWidget>& Root)
    {
        FString Result;TFunction<void(const TSharedRef<SWidget>&)> Visit;
        Visit=[&](const TSharedRef<SWidget>& Widget){
            if(Widget->GetType()==FName(TEXT("STextBlock")))Result+=StaticCastSharedRef<STextBlock>(Widget)->GetText().ToString()+TEXT("\n");
            auto Children=Widget->GetChildren();for(int32 I=0;I<Children->Num();++I)Visit(Children->GetChildAt(I));
        };Visit(Root);return Result;
    };
    for(const auto& Plugin:Host->Plugins)if(Plugin->Id==TEXT("ucm"))
    {
        Plugin->ProfileName=TEXT("Olthoi_Hunt");Plugin->ProfileLinks=MakeShared<FJsonObject>();
        for(const auto& Section:ACEUCMProfiles::Sections())Plugin->ProfileLinks->SetStringField(Section,
            Section==TEXT("Character")?TEXT("PlayerNameVOID"):Section==TEXT("Routes")?TEXT("Olthoi120"):TEXT("Shared"));
    }
    for(const TCHAR* Page:{TEXT("Buffs"),TEXT("Buff others"),TEXT("Combat"),TEXT("Recovery"),TEXT("Route"),TEXT("Loot"),TEXT("Vendors"),TEXT("Monsters"),TEXT("Metas")})
    {
        const FString Text=CollectText(Host->MakeUCMPanel(Page));
        TestTrue(*(FString(Page)+TEXT(" has New profile")),Text.Contains(TEXT("+ New\n")));
        TestTrue(*(FString(Page)+TEXT(" has Delete profile")),Text.Contains(TEXT("Delete\n")));
        if(FString(Page)==TEXT("Route"))TestFalse(TEXT("Route has no duplicate import selector"),Text.Contains(TEXT("Navigation route\n")));
        if(FString(Page)==TEXT("Monsters"))TestTrue(TEXT("Monsters can add current target"),Text.Contains(TEXT("Add selected monster")));
        if(FString(Page)==TEXT("Buffs"))TestTrue(TEXT("Buffs includes protection choices"),Text.Contains(TEXT("Protections:"))&&Text.Contains(TEXT("Banes:")));
    }
    const FString Overview=CollectText(Host->MakeUCMPanel(TEXT("Overview")));
    TestFalse(TEXT("Overview no longer includes Open Log"),Overview.Contains(TEXT("Open UCM Log")));
    TestFalse(TEXT("Overview no longer includes protection selectors"),Overview.Contains(TEXT("Protections:"))||Overview.Contains(TEXT("Banes:")));
    const FString SetupText=CollectText(Host->MakeUCMPanel(TEXT("Profiles")));
    TestTrue(TEXT("Setup supports new and current-save controls"),SetupText.Contains(TEXT("New setup\n"))&&SetupText.Contains(TEXT("Save current setup")));
    TestFalse(TEXT("Setup omits redundant save-copy controls"),SetupText.Contains(TEXT("Save copy"))||SetupText.Contains(TEXT("Save setup as")));
    TestTrue(TEXT("Setup includes explicit vTank import"),SetupText.Contains(TEXT("Import vTank file")));
    if(FApp::CanEverRender())
    {
        FWidgetRenderer Renderer(false,true);
        const FString Folder=FPaths::ProjectSavedDir()/TEXT("UCMProfileReview/UI");IFileManager::Get().MakeDirectory(*Folder,true);
        for(const TCHAR* Page:{TEXT("Profiles"),TEXT("Route"),TEXT("Buffs"),TEXT("Buff others"),TEXT("Combat"),TEXT("Recovery"),TEXT("Loot"),TEXT("Vendors"),TEXT("Monsters"),TEXT("Import vTank")})for(int Width:{780,460})
        {
            auto Panel=Host->MakeUCMPanel(Page);const FVector2D Size(Width,900);
            auto* Target=FWidgetRenderer::CreateTargetFor(Size,TF_Bilinear,true);
            for(int Pass=0;Pass<3;++Pass){Renderer.DrawWidget(Target,Panel,Size,0);FlushRenderingCommands();}
            TArray<FColor> Pixels;FReadSurfaceDataFlags Flags;Flags.SetLinearToGamma(false);
            Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags);
            TestEqual(TEXT("Shared profile controls render at both window widths"),Pixels.Num(),Width*900);
            TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(Width,900,Pixels,PNG);
            FFileHelper::SaveArrayToFile(PNG,*(Folder/FString::Printf(TEXT("%s-%d.png"),Page,Width)));Target->ReleaseResource();
        }
    }
    return true;
}
#endif
