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
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEUCMProfilePanelTest, "ACE.Plugins.LinkedProfilePanel",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACEUCMProfilePanelTest::RunTest(const FString&)
{
    auto* GI=NewObject<UGameInstance>();GI->Init();ON_SCOPE_EXIT { GI->Shutdown(); };
    auto* Host=GI->GetSubsystem<UACEPluginSubsystem>();
    for(const auto& Plugin:Host->Plugins)if(Plugin->Id==TEXT("ucm"))
    {
        Plugin->ProfileName=TEXT("Olthoi_Hunt");Plugin->ProfileLinks=MakeShared<FJsonObject>();
        for(const auto& Section:ACEUCMProfiles::Sections())Plugin->ProfileLinks->SetStringField(Section,
            Section==TEXT("Character")?TEXT("PlayerNameVOID"):Section==TEXT("Routes")?TEXT("Olthoi120"):TEXT("Shared"));
    }
    if(FApp::CanEverRender())
    {
        FWidgetRenderer Renderer(false,true);
        const FString Folder=FPaths::ProjectSavedDir()/TEXT("UCMProfileReview/UI");IFileManager::Get().MakeDirectory(*Folder,true);
        for(const TCHAR* Page:{TEXT("Profiles"),TEXT("Combat"),TEXT("Loot")})for(int Width:{780,460})
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
