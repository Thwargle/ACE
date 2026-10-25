#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "ACETestDatDirectory.h"
#include "Mods/ACEPluginSubsystem.h"
#include "Mods/ACEPluginContent.h"
#include "Mods/ACEPluginVM.h"
#include "Mods/ACEPluginStyle.h"
#include "ACEClientSubsystem.h"
#include "ACEDatSubsystem.h"
#include "ACESession.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Slate/WidgetRenderer.h"
#include "ImageUtils.h"
#include "RenderingThread.h"
#include "HAL/FileManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEFoundryTest,"ACE.Plugins.Foundry",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACEFoundryTest::RunTest(const FString&)
{
    auto Json=[](const FString& T){TSharedPtr<FJsonObject> J;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(T),J);return J;};
    auto* W=UWorld::CreateWorld(EWorldType::Game,false);auto& Context=GEngine->CreateNewWorldContext(EWorldType::Game);
    auto* GI=NewObject<UGameInstance>(GEngine);W->SetGameInstance(GI);Context.OwningGameInstance=GI;Context.SetCurrentWorld(W);GI->OnWorldChanged(nullptr,W);GI->Init();
    ON_SCOPE_EXIT {GI->Shutdown();GEngine->DestroyWorldContext(W);W->DestroyWorld(false);};
    auto* H=GI->GetSubsystem<UACEPluginSubsystem>();auto* C=GI->GetSubsystem<UACEClientSubsystem>();
    H->StorageRoot=FPaths::ProjectSavedDir()/TEXT("Automation/Foundry")/FGuid::NewGuid().ToString();H->Settings=MakeShared<FJsonObject>();H->Discover();
    auto P=H->Find(TEXT("foundry"));if(!TestTrue(TEXT("Foundry is bundled and disabled by default"),P&&!P->Enabled&&P->Companion))return false;
    TestTrue(TEXT("Foundry requests a minimal snapshot"),P->SelectiveSnapshot&&P->SnapshotFields.Num()==2);
    C->Session=MakeShared<FACESession>();auto& S=*C->Session;S.State=EACESessionState::InWorld;S.PlayerGuid=1234;
    P->Player=1234;P->VMSession=C->Session;P->Enabled=P->Running=true;
    FString Source,Error;FFileHelper::LoadFileToString(Source,*(P->Directory/TEXT("main.lua")));FACEPluginVM VM;
    if(!TestTrue(TEXT("Foundry Lua loads"),VM.Load(Source,Error)))return false;
    auto Input=Json(TEXT(R"({"time":1,"player":1234,"state":{},"position":{"cell":2096365581},"selection":{"id":1879048200,"name":"Workshop chest","wcid":123,"object_type":"chest"},"events":[]})"));
    TSharedPtr<FJsonObject> Out;double Clock=1;
    auto Step=[&](const TCHAR* Events)
    {
        Input->SetNumberField(TEXT("time"),Clock+=2);Input->SetArrayField(TEXT("events"),Json(FString(TEXT("{\"events\":"))+Events+TEXT("}"))->GetArrayField(TEXT("events")));
        if(!VM.Step(Input,P->Profile,Out,Error)){AddError(TEXT("Foundry Lua: ")+Error);UE_LOG(LogTemp,Error,TEXT("Foundry Lua: %s"),*Error);return false;}
        if(!Out || !H->ApplyCompanionOutput(*P,Out,Error)){AddError(TEXT("Foundry presentation: ")+Error);UE_LOG(LogTemp,Error,TEXT("Foundry presentation: %s"),*Error);return false;}
        return true;
    };
    if(!Step(TEXT("[]")))return false;
    TestFalse(TEXT("Opening the workshop performs no commands or file scans"),Out->HasField(TEXT("requests")));
    if(!Step(TEXT(R"([{"type":"click","id":"use_selected"},{"type":"click","id":"createinst"}])")))return false;
    TestFalse(TEXT("Persistent creation waits for explicit confirmation"),Out->HasField(TEXT("requests")));
    if(!Step(TEXT(R"([{"type":"click","id":"confirm"}])")))return false;
    TestEqual(TEXT("Create instance uses the selected WCID"),Out->GetArrayField(TEXT("requests"))[0]->AsObject()->GetStringField(TEXT("text")),FString(TEXT("/createinst 123")));
    if(!Step(TEXT(R"([{"type":"click","id":"nudge_ne"}])")))return false;
    auto Request=Out->GetArrayField(TEXT("requests"))[0]->AsObject();
    TestEqual(TEXT("Placement records the intended object"),Request->GetNumberField(TEXT("target")),1879048200.);
    TestEqual(TEXT("Nudge uses ACE's direction and distance"),Request->GetStringField(TEXT("text")),FString(TEXT("/nudge ne 0.1")));
    if(!Step(TEXT(R"([{"type":"change","id":"quest","value":"testquest"},{"type":"click","id":"import_json_quests"},{"type":"click","id":"confirm"}])")))return false;
    TestEqual(TEXT("Quest import uses current ACE argument order"),Out->GetArrayField(TEXT("requests"))[0]->AsObject()->GetStringField(TEXT("text")),FString(TEXT("/import-json quest testquest")));
    if(!Step(TEXT(R"([{"type":"click","id":"get_parent"},{"type":"change","id":"child","value":"456"},{"type":"click","id":"link"},{"type":"click","id":"confirm"}])")))return false;
    TestEqual(TEXT("Generator link captures selected GUID"),Out->GetArrayField(TEXT("requests"))[0]->AsObject()->GetStringField(TEXT("text")),FString(TEXT("/createinst -p 0x70000008 -c 456")));
    if(!Step(TEXT(R"([{"type":"click","id":"delete"}])")))return false;Input->GetObjectField(TEXT("selection"))->SetNumberField(TEXT("id"),99);
    if(!Step(TEXT(R"([{"type":"click","id":"confirm"}])")))return false;TestFalse(TEXT("Changing selection cancels a pending destructive edit"),Out->HasField(TEXT("requests")));
    bool Target=false;
    for(const FString Text:{TEXT("/shutdown"),TEXT("/deletecharacter somebody"),TEXT("/fetchstring database_password"),TEXT("/create 1\n/delete"),TEXT("/import-json invalid 1")})
        TestFalse(TEXT("Content capability rejects unrelated or multi-line commands"),ACEPluginContent::Command(Text,Target));
    for(const FString Text:{TEXT("/create 1"),TEXT("/nudge here .1"),TEXT("/rotate 90"),TEXT("/export-sql landblock 7D64"),TEXT("/removeenc")})
        TestTrue(TEXT("Reference workflows use supported ACE content commands"),ACEPluginContent::Command(Text,Target));
    for(const FString Name:{TEXT("../outside.sql"),TEXT("C:/secret.json"),TEXT("json/../../outside.sql"),TEXT("json/tool.exe"),TEXT("json/test.sql\n")})
        TestFalse(TEXT("Content file access rejects traversal and executables"),ACEPluginContent::RelativeFile(Name));
    TestTrue(TEXT("Standard content file path accepted"),ACEPluginContent::RelativeFile(TEXT("json/weenies/123 - chest.json")));
    const FString Root=H->ContentRoot(*P);IFileManager::Get().MakeDirectory(*(Root/TEXT("json/weenies")),true);
    FFileHelper::SaveStringToFile(TEXT("{}"),*(Root/TEXT("json/weenies/123 - chest.json")));
    auto Reply=MakeShared<FJsonObject>();auto List=Json(TEXT(R"({"operation":"list","kind":"weenies","format":"json","filter":"chest"})"));
    TestTrue(TEXT("Native content list reads the configured workspace"),H->ContentService(*P,List,Reply,Error));
    TestEqual(TEXT("File result has relative path"),Reply->GetArrayField(TEXT("files"))[0]->AsString(),FString(TEXT("json/weenies/123 - chest.json")));
    P->Permissions.Remove(TEXT("admin_content"));TestFalse(TEXT("File service requires explicit permission"),H->ContentService(*P,List,Reply,Error));P->Permissions.Add(TEXT("admin_content"));
    auto WrongTarget=Json(TEXT(R"({"operation":"command","text":"/delete","target":99})"));
    TestFalse(TEXT("Stale selection is rejected before sending anything"),H->ContentService(*P,WrongTarget,Reply,Error));
    if(FApp::CanEverRender())
    {
        GI->GetSubsystem<UACEDatSubsystem>()->LoadDatDirectory(ACETestDatDirectory());
        Step(TEXT("[]"));auto Panel=H->MakeCompanionPanel(P->Id);
        TFunction<TSharedPtr<SWidget>(TSharedRef<SWidget>,FName)> Find=[&](TSharedRef<SWidget> Widget,FName Tag)->TSharedPtr<SWidget>
        {if(Widget->GetTag()==Tag)return Widget;auto* Children=Widget->GetChildren();for(int32 I=0;I<Children->Num();++I)if(auto Found=Find(Children->GetChildAt(I),Tag))return Found;return nullptr;};
        FWidgetRenderer Renderer(false,true);const FVector2D Size(760,700);auto* Texture=FWidgetRenderer::CreateTargetFor(Size,TF_Bilinear,true);
        const FString Folder=FPaths::ProjectSavedDir()/TEXT("FoundryReview");IFileManager::Get().MakeDirectory(*Folder,true);
        for(const TCHAR* Page:{TEXT("content"),TEXT("placement"),TEXT("landblocks"),TEXT("quests"),TEXT("generators"),TEXT("files"),TEXT("help")})
        {
            Renderer.DrawWidget(Texture,Panel,Size,0);FlushRenderingCommands();
            auto Button=StaticCastSharedPtr<SACEPluginTab>(Find(Panel,FName(*(FString(TEXT("PluginTab.tabs."))+Page))));
            if(!TestTrue(TEXT("Workshop page tab exists"),Button.IsValid()))continue;
            Button->SimulateClick();
            for(int I=0;I<3;++I){Renderer.DrawWidget(Texture,Panel,Size,0);FlushRenderingCommands();}
            TArray<FColor> Pixels;FReadSurfaceDataFlags Flags;Flags.SetLinearToGamma(false);Texture->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags);
            TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(760,700,Pixels,PNG);FFileHelper::SaveArrayToFile(PNG,*(Folder/(FString(Page)+TEXT(".png"))));
        }
        Texture->ReleaseResource();
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEContentPerformanceTest,"ACE.Performance.ContentResourceLifetime",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACEContentPerformanceTest::RunTest(const FString&)
{
    TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>());
    TStrongObjectPtr<UACEDatSubsystem> Dat(NewObject<UACEDatSubsystem>(GI.Get()));
    auto* Base=Dat->EnsureAceOutdoorLitMaskedMaterialBase();
    TStrongObjectPtr<UMaterialInstanceDynamic> Visible(UMaterialInstanceDynamic::Create(Base,Dat.Get()));
    Dat->OutdoorLitMaterialCache.Add(1,Visible.Get());
    for(int Pass=0;Pass<12;++Pass)
    {
        for(uint32 I=0;I<200;++I)
        {
            const uint32 Key=100+Pass*200+I;
            Dat->BuildingShellMaterialCache.Add(Key,UMaterialInstanceDynamic::Create(Base,Dat.Get()));
            Dat->EnvCellMaterialCache.Add(Key,UMaterialInstanceDynamic::Create(Base,Dat.Get()));
            Dat->ParticleMaterialCache.Add(Key,UMaterialInstanceDynamic::Create(Base,Dat.Get()));
        }
        Dat->TrimTextureResolverCaches(64,64,64ll<<20);
        const int32 Strong=Dat->TexturedMaterialCache.Num()+Dat->BuildingShellMaterialCache.Num()+Dat->OutdoorLitMaterialCache.Num()+Dat->EnvCellMaterialCache.Num()+Dat->ParticleMaterialCache.Num();
        TestTrue(TEXT("Area changes cannot grow warm material references past budget"),Strong<=64);
        Dat->SetWorldEmissiveScale(.8f,.65f,FLinearColor(.4f,.5f,.6f));
        TestEqual(TEXT("Visible evicted materials still receive daylight"),Visible->K2_GetScalarParameterValue(TEXT("EmissiveStrength")),.65f);
        TestTrue(TEXT("A visible evicted material is reused, never duplicated"),Dat->FindSurfaceMaterial(Dat->OutdoorLitMaterialCache,2,1)==Visible.Get());
        CollectGarbage(RF_NoFlags);
        Dat->TrimTextureResolverCaches(64,64,64ll<<20);
        TestTrue(TEXT("Weak identities shrink after unused areas are collected"),Dat->SurfaceMaterials.Num()<=65);
    }
    return true;
}
#endif
