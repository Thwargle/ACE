#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "HAL/FileManager.h"
#include "Mods/ACEPluginSubsystem.h"
#include "Mods/ACEPluginCosmetics.h"
#include "Engine/GameInstance.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/BufferArchive.h"
#include "ImageUtils.h"
#include "ACEDatSubsystem.h"
#include "ACEWorldEntityActor.h"
#include "ACECharacterAppearanceComponent.h"
#include "ACEScriptComponent.h"
#include "ACEParticleBatchComponent.h"
#include "ACEEffectLightSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/Texture2D.h"
#include "HAL/IConsoleManager.h"
#include "Widgets/SWindow.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Images/SImage.h"
#include "Framework/Application/SlateApplication.h"
#include "Slate/WidgetRenderer.h"
#include "RenderingThread.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEPluginCosmeticsTest,"ACE.Plugins.CosmeticAPI",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FACEPluginCosmeticsTest::RunTest(const FString&)
{
#if !PLATFORM_WINDOWS
    AddInfo(TEXT("Windows companion-tool API unavailable on this platform."));return true;

#else
    auto* GI=NewObject<UGameInstance>();GI->Init();auto* H=GI->GetSubsystem<UACEPluginSubsystem>();
    ON_SCOPE_EXIT {GI->Shutdown();};
    const FString Namespace=TEXT("ACECosmeticTest-")+FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString Root=FPaths::Combine(FPlatformMisc::GetEnvironmentVariable(TEXT("LOCALAPPDATA")),Namespace);
    H->StorageRoot=Root/TEXT("ClientPlugins");H->Settings=MakeShared<FJsonObject>();
    const FString Dir=H->UserDirectory()/TEXT("Installed/sample_mesh");IFileManager::Get().MakeDirectory(*Dir,true);
    const FString Manifest=FString::Printf(TEXT(R"({"api":1,"id":"sample_mesh","name":"Independent mesh provider","mode":"local","service":"local_cosmetics","service_api":1,"permissions":["local_cosmetics","local_tools"],"cosmetics":{"helper":"tools/Converter.exe","cache_namespace":"%s","fields":[{"key":"model","option":"--model","default":"sample"}]}})"),*Namespace);
    FFileHelper::SaveStringToFile(Manifest,*(Dir/TEXT("plugin.json")));
    FFileHelper::SaveStringToFile(TEXT("return function() end"),*(Dir/TEXT("main.lua")));
    H->Discover();auto* Service=H->GetLocalCosmetic(TEXT("sample_mesh"));
    if(!TestNotNull(TEXT("Unknown third-party ID gets the shared cosmetic API without native registration"),Service))return false;
    TestFalse(TEXT("UNC path rejected"),Service->LocalFile(TEXT("\\\\server\\share\\mesh.acwm")));
    TestFalse(TEXT("Relative file rejected"),Service->LocalFile(TEXT("mesh.acwm")));
    FString Error;auto Bad=MakeShared<FJsonObject>();Bad->SetStringField(TEXT("action"),TEXT("console"));
    TestFalse(TEXT("Console commands are outside the cosmetic API"),Service->ServiceExecute(Bad,Error));
    Bad->SetStringField(TEXT("action"),TEXT("local_cosmetics"));Bad->SetStringField(TEXT("operation"),TEXT("convert"));
    TestFalse(TEXT("Lua cannot directly construct native converter arguments"),Service->ServiceExecute(Bad,Error));
    FACEClientPlugin P;P.Id=TEXT("escape");P.Directory=Dir;
    FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Manifest),P.Manifest);
    auto Config=P.Manifest->GetObjectField(TEXT("cosmetics"));Config->SetStringField(TEXT("helper"),TEXT("../escape.exe"));
    auto* Invalid=NewObject<UACEPluginCosmetics>(H);
    TestFalse(TEXT("Converter traversal rejected at discovery"),Invalid->Initialize(H,P,Error));
    Config->SetStringField(TEXT("helper"),TEXT("tools/Converter.exe"));Config->SetStringField(TEXT("cache_namespace"),TEXT("../escape"));
    TestFalse(TEXT("Cache namespace traversal rejected"),Invalid->Initialize(H,P,Error));
    const FString Id=FGuid::NewGuid().ToString(EGuidFormats::Digits),Cache=Root/TEXT("cache")/Id,Stamp=Root/TEXT("source.txt");
    IFileManager::Get().MakeDirectory(*Cache,true);FFileHelper::SaveStringToFile(TEXT("synthetic source version 1"),*Stamp);
    FBufferArchive Bytes;uint32 Magic=0x4d574341;int32 Version=1,Count=3,Sections=1;Bytes<<Magic<<Version<<Count<<Sections;
    const float Vertices[3][8]={{0,0,0,0,0,1,0,0},{1,0,0,0,0,1,1,0},{0,1,0,0,0,1,0,1}};
    for(const auto& Vertex:Vertices)for(float Value:Vertex)Bytes<<Value;
    int32 Blend=0,I0=0,I1=2,I2=1;Bytes<<Blend<<Count<<I0<<I1<<I2;
    FFileHelper::SaveArrayToFile(Bytes,*(Cache/TEXT("mesh.acwm")));
    TArray<FColor> Pixels;Pixels.Init(FColor::Blue,4);TArray64<uint8> Png;FImageUtils::PNGCompressImageArray(2,2,Pixels,Png);
    FFileHelper::SaveArrayToFile(Png,*(Cache/TEXT("texture-0.png")));
    auto Receipt=MakeShared<FJsonObject>();Receipt->SetNumberField(TEXT("schema"),1);Receipt->SetStringField(TEXT("id"),Id);
    Receipt->SetStringField(TEXT("directory"),Cache);Receipt->SetStringField(TEXT("slot"),TEXT("weapon"));Receipt->SetStringField(TEXT("sourceStamp"),Stamp);
    auto Encode=[&](){FString Json;FJsonSerializer::Serialize(Receipt,TJsonWriterFactory<>::Create(&Json));return Json;};
    TestTrue(TEXT("Source-independent synthetic mesh loads"),Service->LoadVerified(Encode()));
    TestTrue(TEXT("No source-specific sword rotation in the client"),Service->Fit.Equals(FTransform::Identity));
    Service->Disable();Receipt->SetStringField(TEXT("directory"),Root);
    TestFalse(TEXT("Receipt cannot redirect the mesh outside its GUID cache"),Service->LoadVerified(Encode()));
    Receipt->SetStringField(TEXT("directory"),Cache);Receipt->SetNumberField(TEXT("schema"),2);
    TestFalse(TEXT("Unknown mesh receipt schema rejected"),Service->LoadVerified(Encode()));
    Receipt->SetNumberField(TEXT("schema"),1);
    // A corrupt PNG claiming a huge image must be rejected before decoder allocation.
    Png[16]=0x7f;Png[17]=0xff;Png[18]=0xff;Png[19]=0xff;FFileHelper::SaveArrayToFile(Png,*(Cache/TEXT("texture-0.png")));
    TestFalse(TEXT("Oversized texture rejected before decoding"),Service->LoadVerified(Encode()));
    Service->Disable();
    Png.Empty();FImageUtils::PNGCompressImageArray(2,2,Pixels,Png);FFileHelper::SaveArrayToFile(Png,*(Cache/TEXT("texture-0.png")));
    Service->bPaperdoll=true;
    auto Second=MakeShared<FJsonObject>(*Receipt);Second->SetStringField(TEXT("slot"),TEXT("shield"));
    auto Outfit=MakeShared<FJsonObject>();Outfit->SetStringField(TEXT("slot"),TEXT("weapon"));
    Outfit->SetArrayField(TEXT("appearances"),{MakeShared<FJsonValueObject>(Receipt),MakeShared<FJsonValueObject>(Second)});
    auto OutfitJson=[&](){FString J;FJsonSerializer::Serialize(Outfit,TJsonWriterFactory<>::Create(&J));return J;};
    TestTrue(TEXT("Two independent slot renderers load as one outfit"),Service->LoadVerified(OutfitJson()));
    TestEqual(TEXT("Main hand and shield coexist"),Service->SlotRenderers.Num(),2);
    TestTrue(TEXT("Saved outfit restores both paperdoll model icons without a catalog"),Service->SlotSelections.Contains(TEXT("weapon"))&&Service->SlotSelections.Contains(TEXT("offhand")));
    TestEqual(TEXT("Saved icon uses the verified cache ID"),Service->SlotSelections[TEXT("weapon")]->GetStringField(TEXT("cacheId")),Id);
    Receipt->SetStringField(TEXT("appearanceName"),TEXT("Worn Shortsword"));Receipt->SetStringField(TEXT("catalogId"),TEXT("item:25:1542:0"));
    TestTrue(TEXT("Named saved outfit reloads"),Service->LoadVerified(OutfitJson()));
    TestEqual(TEXT("Item name survives reload without loading the catalog first"),Service->SlotSelections[TEXT("weapon")]->GetStringField(TEXT("name")),FString(TEXT("Worn Shortsword")));
    auto* Previous=Service->RendererForSlot(TEXT("weapon"));
    Second->SetStringField(TEXT("directory"),Root);
    TestFalse(TEXT("One malformed outfit slot rejects the whole replacement"),Service->LoadVerified(OutfitJson()));
    TestTrue(TEXT("Failed replacement preserves existing renderer"),Service->RendererForSlot(TEXT("weapon"))==Previous);
    Second->SetStringField(TEXT("directory"),Cache);Second->SetStringField(TEXT("slot"),TEXT("weapon"));
    TestFalse(TEXT("Duplicate target slots rejected"),Service->LoadVerified(OutfitJson()));
    Second->SetStringField(TEXT("slot"),TEXT("offhand"));Receipt->SetStringField(TEXT("slot"),TEXT("shield"));
    TestFalse(TEXT("Shield and offhand cannot own the same hand"),Service->LoadVerified(OutfitJson()));
    Receipt->SetStringField(TEXT("slot"),TEXT("weapon"));
    TestTrue(TEXT("Independent main and offhand weapons supported"),Service->LoadVerified(OutfitJson()));
    const FString CatalogJson=FString::Printf(TEXT(R"({"items":[{"id":"1","name":"Sample weapon","sourceSlot":"weapon","targets":["weapon","offhand"],"supported":true,"cacheId":"%s","values":{"model":"1"}},{"id":"2","name":"Sample shield","sourceSlot":"shield","targets":["shield"],"supported":false,"reason":"Missing texture"}]})"),*Id);
    Service->UpdateCatalog(CatalogJson);Service->SelectSlot(TEXT("weapon"));
    TestEqual(TEXT("Main-hand dropdown excludes shields"),Service->SlotOptions.Num(),1);
    Service->SelectCatalogItem(Service->SlotOptions[0]);Service->SelectSlot(TEXT("offhand"));
    TestEqual(TEXT("Unavailable shield is excluded from offhand choices"),Service->SlotOptions.Num(),1);
    Service->SelectSlot(TEXT("head"));TestTrue(TEXT("Unsupported body slot has no fabricated choices"),Service->SlotOptions.IsEmpty());
    Service->SelectSlot(TEXT("weapon"));TestTrue(TEXT("Choice retained when changing paperdoll slots"),Service->SlotSelections.Contains(TEXT("weapon")));
    Service->CatalogFilter=TEXT("shield");Service->SelectSlot(TEXT("offhand"));
    TestEqual(TEXT("Search never exposes unavailable models"),Service->SlotOptions.Num(),0);
    Service->CatalogFilter.Empty();Service->UpdateCatalog(CatalogJson);
    TestTrue(TEXT("Catalog reload preserves the selected model"),Service->SlotSelections.Contains(TEXT("weapon")));
    TSharedPtr<FJsonObject> Large;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(CatalogJson),Large);
    TArray<TSharedPtr<FJsonValue>> Many;for(int32 I=0;I<18000;++I)Many.Add(Large->GetArrayField(TEXT("items"))[0]);
    Large->SetArrayField(TEXT("items"),Many);FString LargeJson;FJsonSerializer::Serialize(Large.ToSharedRef(),TJsonWriterFactory<>::Create(&LargeJson));
    Service->UpdateCatalog(LargeJson);TestEqual(TEXT("Complete catalog is not truncated at the old 16384 entry limit"),Service->Catalog.Num(),18000);
    auto Missing=MakeShared<FJsonObject>();Missing->SetStringField(TEXT("id"),TEXT("item:6473:11840:0"));
    Missing->SetStringField(TEXT("name"),TEXT("Armor of the Fang [Item 6473]"));Missing->SetStringField(TEXT("cacheId"),Id);
    Missing->SetStringField(TEXT("sourceSlot"),TEXT("chest"));Missing->SetBoolField(TEXT("supported"),false);
    Missing->SetStringField(TEXT("reason"),TEXT("Required local texture is missing"));
    Missing->SetArrayField(TEXT("targets"),{MakeShared<FJsonValueString>(TEXT("chest"))});
    Large->SetArrayField(TEXT("items"),{MakeShared<FJsonValueObject>(Missing)});
    FJsonSerializer::Serialize(Large.ToSharedRef(),TJsonWriterFactory<>::Create(&LargeJson));Service->UpdateCatalog(LargeJson);
    Service->CatalogFilter=TEXT("6473");Service->SelectSlot(TEXT("chest"));
    TestEqual(TEXT("Named missing item remains searchable by ID"),Service->SlotOptions.Num(),1);
    Service->SelectCatalogItem(Missing);
    TestFalse(TEXT("Missing item cannot overwrite a draft outfit"),Service->SlotSelections.Contains(TEXT("chest")));
    TestTrue(TEXT("Missing item explains the unavailable asset"),Service->Status.Contains(TEXT("texture is missing")));
    // Exercise the real searchable popup: typing must filter in place and must
    // not silently change the outfit before Enter/click confirms a choice.
    auto Named=MakeShared<FJsonObject>(*Missing);Named->SetBoolField(TEXT("supported"),true);
    Named->SetStringField(TEXT("name"),TEXT("Armor of the Fang"));Named->SetStringField(TEXT("kind"),TEXT("item"));
    auto SearchValues=MakeShared<FJsonObject>();SearchValues->SetStringField(TEXT("item"),TEXT("6473"));Named->SetObjectField(TEXT("values"),SearchValues);
    auto Raw=MakeShared<FJsonObject>(*Named);Raw->SetStringField(TEXT("id"),TEXT("raw-model"));Raw->SetStringField(TEXT("kind"),TEXT("model"));Raw->SetStringField(TEXT("name"),TEXT("shirt_model"));
    Large->SetArrayField(TEXT("items"),{MakeShared<FJsonValueObject>(Named),MakeShared<FJsonValueObject>(Raw)});
    FJsonSerializer::Serialize(Large.ToSharedRef(),TJsonWriterFactory<>::Create(&LargeJson));Service->CatalogFilter.Empty();Service->UpdateCatalog(LargeJson);
    TestEqual(TEXT("Unnamed model filenames are excluded by default"),Service->SlotOptions.Num(),1);
    Service->bShowModelVariants=true;Service->SelectSlot(TEXT("chest"));TestEqual(TEXT("Advanced model variants remain accessible"),Service->SlotOptions.Num(),2);
    Service->bShowModelVariants=false;Service->SelectSlot(TEXT("chest"));
    auto Picker=Service->MakePaperdollPanel();auto Combo=Service->ItemCombo.Pin();
    TestTrue(TEXT("Dropdown search includes item ID even though label omits it"),Service->ComboOptions->Num()==1&&(*Service->ComboOptions)[0]->Contains(TEXT("6473")));
    if(FSlateApplication::IsInitialized()&&FApp::CanEverRender())
    {
        auto& Slate=FSlateApplication::Get();const auto OldFocus=Slate.GetUserFocusedWidget(0);
        auto Window=SNew(SWindow).ClientSize(FVector2D(900,1200))[Picker];Slate.AddWindow(Window,false);
        ON_SCOPE_EXIT{Combo->SetIsOpen(false);Slate.RequestDestroyWindow(Window);if(OldFocus)Slate.SetUserFocus(0,OldFocus);};
        FWidgetRenderer Renderer;Renderer.DrawWidget(Window,FVector2D(900,1200));FlushRenderingCommands();
        TFunction<TSharedPtr<SWidget>(TSharedRef<SWidget>,FName)> FindWidget;
        FindWidget=[&](TSharedRef<SWidget> W,FName Type)->TSharedPtr<SWidget>
        {
            if(W->GetType()==Type)return W;
            auto* Children=W->GetChildren();for(int32 I=0;I<Children->Num();++I)
                if(auto Found=FindWidget(Children->GetChildAt(I),Type))return Found;
            return nullptr;
        };
        auto OpenButton=FindWidget(Combo.ToSharedRef(),TEXT("SButton"));
        if(OpenButton)Slate.SetUserFocus(0,OpenButton);
        auto Key=[&](FKey K){Slate.ProcessKeyDownEvent(FKeyEvent(K,FModifierKeysState(),0,false,0,0));Slate.ProcessKeyUpEvent(FKeyEvent(K,FModifierKeysState(),0,false,0,0));};
        Key(EKeys::Enter);
        TestTrue(TEXT("Dropdown opens from the keyboard"),Combo->IsOpen());
        const auto Focus=Slate.GetUserFocusedWidget(0);
        TestTrue(TEXT("Opening dropdown puts typing focus in its search field"),Focus&&Focus->GetTypeAsString().Contains(TEXT("EditableText")));
        for(TCHAR C:FString(TEXT("fang 6473")))Slate.ProcessKeyCharEvent(FCharacterEvent(C,FModifierKeysState(),0,false));
        TestFalse(TEXT("Typing alone does not select or apply an appearance"),Service->SlotSelections.Contains(TEXT("chest")));
        Key(EKeys::Enter);
        TestTrue(TEXT("Enter selects the named item matching the typed name and item ID"),Service->SlotSelections.FindRef(TEXT("chest"))==Service->Catalog[0]);
        TestFalse(TEXT("Committing a search result closes the dropdown"),Combo->IsOpen());
    }
    Service->CatalogFilter.Empty();Service->UpdateCatalog(CatalogJson);
    // A separate body cache proves ACWM/2 parsing and per-slot part restrictions.
    const FString BodyId=FGuid::NewGuid().ToString(EGuidFormats::Digits),BodyDir=Root/TEXT("cache")/BodyId;
    IFileManager::Get().MakeDirectory(*BodyDir,true);FFileHelper::SaveArrayToFile(Png,*(BodyDir/TEXT("texture-0.png")));
    auto Head=MakeShared<FJsonObject>(*Receipt);Head->SetStringField(TEXT("id"),BodyId);Head->SetStringField(TEXT("directory"),BodyDir);Head->SetStringField(TEXT("slot"),TEXT("head"));
    auto WriteBody=[&](int32 Part)
    {
        FBufferArchive B;int32 V=2;B<<Magic<<V<<Count<<Sections;
        for(const auto& Vertex:Vertices)for(float Value:Vertex)B<<Value;
        B<<Part<<Blend<<Count<<I0<<I1<<I2;FFileHelper::SaveArrayToFile(B,*(BodyDir/TEXT("mesh.acwm")));
    };
    WriteBody(16);Outfit->SetArrayField(TEXT("appearances"),{MakeShared<FJsonValueObject>(Receipt),MakeShared<FJsonValueObject>(Head)});
    TestTrue(TEXT("Body and held interchange versions coexist"),Service->LoadVerified(OutfitJson()));
    TestEqual(TEXT("Helmet sections retain the animated head index"),Service->RendererForSlot(TEXT("head"))->Sections[0].BodyPart,16);
    auto* PreviousHead=Service->RendererForSlot(TEXT("head"));WriteBody(10);
    TestFalse(TEXT("Helmet cannot redirect itself to another body part"),Service->LoadVerified(OutfitJson()));
    TestTrue(TEXT("Invalid body replacement preserves current outfit"),Service->RendererForSlot(TEXT("head"))==PreviousHead);
    auto WriteSkin=[&](int32 Bone,float Weight,int32 V=3,int32 SkinPart=-1,bool BadReference=false)
    {
        FBufferArchive B;int32 BindingCount=1,Part=9;B<<Magic<<V<<Count<<Sections;
        for(const auto& Vertex:Vertices)for(float Value:Vertex)B<<Value;
        B<<BindingCount<<Part;float Zero=0,End=25;B<<Zero<<Zero<<Zero<<Zero<<Zero<<End;
        if(V>=5){float Min=BadReference?20:-10,Max=10;B<<Min<<Min<<Min<<Max<<Max<<Max;}
        for(int32 Vertex=0;Vertex<3;++Vertex)for(int32 Influence=0;Influence<4;++Influence)
        {int32 Index=Influence==0?Bone:0;float Value=Influence==0?Weight:0;B<<Index<<Value;}
        B<<SkinPart<<Blend<<Count<<I0<<I1<<I2;
        FFileHelper::SaveArrayToFile(B,*(BodyDir/TEXT("mesh.acwm")));
    };
    WriteSkin(0,1);
    TestTrue(TEXT("Skinned armor loads with normalized source weights"),Service->LoadVerified(OutfitJson()));
    auto* PreviousSkin=Service->RendererForSlot(TEXT("head"));
    WriteSkin(1,1);TestFalse(TEXT("Out-of-range skin bone rejects entire outfit"),Service->LoadVerified(OutfitJson()));
    TestTrue(TEXT("Bad skin keeps previously applied outfit"),Service->RendererForSlot(TEXT("head"))==PreviousSkin);
    WriteSkin(0,.2f);TestFalse(TEXT("Incomplete skin influence sum is rejected"),Service->LoadVerified(OutfitJson()));
    WriteSkin(0,1);
    WriteSkin(0,1,4,16);
    TestTrue(TEXT("Composite ACWM4 accepts a valid rigid section alongside skin data"),Service->LoadVerified(OutfitJson()));
    TestEqual(TEXT("Composite rigid section keeps the head-local attachment"),Service->RendererForSlot(TEXT("head"))->Sections[0].BodyPart,16);
    WriteSkin(0,1,4,10);
    TestFalse(TEXT("Composite rigid sections still enforce their outfit slot"),Service->LoadVerified(OutfitJson()));
    WriteSkin(0,1,3,16);
    TestFalse(TEXT("Version 3 cannot silently interpret rigid section coordinates"),Service->LoadVerified(OutfitJson()));
    WriteSkin(0,1,5);
    TestTrue(TEXT("Anatomical ACWM5 loads reference body bounds"),Service->LoadVerified(OutfitJson()));
    TestEqual(TEXT("Reference geometry reaches the fitting stage"),Service->RendererForSlot(TEXT("head"))->SkinBindings[0].ReferenceMax,FVector(10));
    WriteSkin(0,1,5,-1,true);
    TestFalse(TEXT("Inverted reference body bounds reject the whole outfit"),Service->LoadVerified(OutfitJson()));
    WriteSkin(0,1);
    TArray<TSharedPtr<FJsonValue>> Full{MakeShared<FJsonValueObject>(Receipt)};
    for(const TCHAR* Target:{TEXT("head"),TEXT("shoulders"),TEXT("chest"),TEXT("arms"),TEXT("hands"),TEXT("waist"),TEXT("legs"),TEXT("feet"),TEXT("back")})
    {auto O=MakeShared<FJsonObject>(*Head);O->SetStringField(TEXT("slot"),Target);Full.Add(MakeShared<FJsonValueObject>(O));}
    Outfit->SetArrayField(TEXT("appearances"),Full);
    TestTrue(TEXT("All body slots apply together in one verified outfit"),Service->LoadVerified(OutfitJson()));
    TestEqual(TEXT("Outfits are no longer limited to seven slots"),Service->SlotRenderers.Num(),10);
    auto* Applied=Service->RendererForSlot(TEXT("weapon"));Service->SelectSlot(TEXT("weapon"));
    if(!Service->SlotOptions.IsEmpty())Service->SelectCatalogItem(Service->SlotOptions[0]);
    TestTrue(TEXT("Draft picker changes do not replace applied renderers"),Service->RendererForSlot(TEXT("weapon"))==Applied);
    Outfit->SetArrayField(TEXT("appearances"),{});Service->ToolCommand=TEXT("apply-outfit");
    TestTrue(TEXT("Applying a cleared draft restores all original slots"),Service->LoadVerified(OutfitJson()));
    TestTrue(TEXT("Cleared outfit removes saved paperdoll icons"),Service->SlotSelections.IsEmpty());
    Service->ToolCommand=TEXT("verify-active");TestTrue(TEXT("Empty saved outfit reloads cleanly"),Service->LoadVerified(OutfitJson()));
    Service->Disable();TestTrue(TEXT("Disable releases every slot renderer"),Service->SlotRenderers.IsEmpty());
    return true;
#endif
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEPluginCosmeticEffectsTest,"ACE.Plugins.CosmeticWeaponEffects",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FACEPluginCosmeticEffectsTest::RunTest(const FString&)
{
    auto* BatchMode=IConsoleManager::Get().FindConsoleVariable(TEXT("ace.Particles.Batched"));
    const int32 SavedBatch=BatchMode->GetInt();
    ON_SCOPE_EXIT {BatchMode->Set(SavedBatch,ECVF_SetByCode);};
    const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    auto& Context=GEngine->CreateNewWorldContext(EWorldType::Game);Context.SetCurrentWorld(World);
    auto* GI=NewObject<UGameInstance>(GEngine);World->SetGameInstance(GI);Context.OwningGameInstance=GI;
    GI->OnWorldChanged(nullptr,World);GI->Init();
    ON_SCOPE_EXIT {GI->Shutdown();GEngine->DestroyWorldContext(World);World->DestroyWorld(false);};
    auto* Dat=GI->GetSubsystem<UACEDatSubsystem>();
    if(!TestTrue(TEXT("Local retail DAT opens"),Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call"))))return false;
    auto* Lights=World->GetSubsystem<UACEEffectLightSubsystem>();
    auto* Mod=NewObject<UACEPluginCosmetics>(GI);Mod->Host=GI->GetSubsystem<UACEPluginSubsystem>();
    Mod->Vertices={FVector(0,0,0),FVector(1,0,0),FVector(0,1,0)};
    Mod->Normals.Init(FVector::UpVector,3);Mod->UVs.Init(FVector2D::ZeroVector,3);
    Mod->Sections.AddDefaulted_GetRef().Indices={0,2,1};Mod->Textures.Add(UTexture2D::CreateTransient(2,2));
    ON_SCOPE_EXIT {Mod->Disable();};
    for(int32 Mode:{0,1})
    {
        BatchMode->Set(Mode,ECVF_SetByCode);
        FACEWorldObject Object;Object.Guid=0x7100C01;Object.SetupId=0x020003CE; // Acid Staff.
        auto* Weapon=World->SpawnActor<AACEWorldEntityActor>();Weapon->InitializeFromObject(Object,100,true);
        auto* Other=World->SpawnActor<AACEWorldEntityActor>();Object.Guid++;Other->InitializeFromObject(Object,100,true);
        auto* FX=Weapon->ScriptComponent.Get();auto* OtherFX=Other->ScriptComponent.Get();
        Weapon->bAttachedToParent=true;Other->bAttachedToParent=true;
        FX->NotifyAppearanceReady(false);OtherFX->NotifyAppearanceReady(false);
        for(int32 Frame=0;Frame<60;++Frame)
        {FX->TickComponent(1.f/30,LEVELTICK_All,nullptr);OtherFX->TickComponent(1.f/30,LEVELTICK_All,nullptr);}
        auto VisibleCount=[](UACEScriptComponent* Scripts)
        {
            int32 Count=0;
            for(const auto& E:Scripts->ActiveEmitters)
            {
                if(E.Batch&&E.Batch->IsVisible()&&!E.Particles.IsEmpty())++Count;
                for(const auto& P:E.Particles)if(P.Mesh.IsValid()&&P.Mesh->IsVisible())++Count;
            }
            return Count;
        };
        if(!TestTrue(TEXT("Real equipped weapon starts with visible elemental effects"),VisibleCount(FX)>0))return false;
        if(Mode)TestTrue(TEXT("Batched rendering is exercised"),FX->ActiveEmitters.ContainsByPredicate([](const auto& E){return E.Batch!=nullptr;}));
        const uint32 EmitterId=FX->ActiveEmitters[0].AuthoredEmitterId;
        const int32 EmitterCount=FX->ActiveEmitters.Num();const float Age=FX->ActiveEmitters[0].Age;
        FX->TickParticleLights();Lights->RefreshLights(.1f);
        TestTrue(TEXT("Original weapon has a particle light"),Lights->GetVisibleLightCount(FX)>0);
        if(!TestTrue(TEXT("Weapon transmog attaches"),Mod->Attach(Weapon)))return false;
        TestEqual(TEXT("Applying transmog immediately hides existing weapon particles"),VisibleCount(FX),0);
        TestEqual(TEXT("Applying transmog retires particle lighting immediately"),Lights->GetVisibleLightCount(FX),0);
        TestEqual(TEXT("Suppression preserves emitter lifetimes"),FX->ActiveEmitters[0].Age,Age);
        TestEqual(TEXT("Suppression does not stop weapon scripts"),FX->ActiveEmitters.Num(),EmitterCount);
        TestTrue(TEXT("Other objects' effects stay visible"),VisibleCount(OtherFX)>0);
        FACEDatAnimationHook Hook;Hook.Type=EACEAnimationHookType::CreateParticle;Hook.Id=EmitterId;Hook.PartIndex=0;
        FX->DispatchAnimationHook(Hook); // New emitter, including a new batch while hidden.
        for(int32 Frame=0;Frame<30;++Frame)FX->TickComponent(1.f/60,LEVELTICK_All,nullptr);
        TestEqual(TEXT("New births and presentation updates cannot reveal hidden particles"),VisibleCount(FX),0);
        TestTrue(TEXT("Hidden particles still advance their authored lifecycle"),FX->ActiveEmitters[0].Age>Age);
        FX->TickParticleLights();Lights->RefreshLights(.1f);
        TestEqual(TEXT("Hidden particle lights are not readmitted"),Lights->GetVisibleLightCount(FX),0);
        // Distance changes must not override suppression, and removing a cosmetic
        // must not reveal an emitter which is still distance-degraded.
        for(auto& E:FX->ActiveEmitters){FX->SetEmitterDegraded(E,true);FX->SetEmitterDegraded(E,false);}
        TestEqual(TEXT("Returning into draw distance keeps transmogrified effects hidden"),VisibleCount(FX),0);
        for(auto& E:FX->ActiveEmitters)FX->SetEmitterDegraded(E,true);
        Mod->RestoreOriginal();
        TestEqual(TEXT("Removing transmog respects distance culling"),VisibleCount(FX),0);
        for(auto& E:FX->ActiveEmitters)FX->SetEmitterDegraded(E,false);
        TestTrue(TEXT("Original weapon effects return without restarting scripts"),VisibleCount(FX)>0);
        FX->TickParticleLights();Lights->RefreshLights(.1f);
        TestTrue(TEXT("Original particle lighting returns"),Lights->GetVisibleLightCount(FX)>0);
        TestTrue(TEXT("Reapplying transmog succeeds"),Mod->Attach(Weapon));
        auto* SecondOwner=NewObject<UACEPluginCosmetics>(GI);FX->SetCosmeticParticlesHidden(SecondOwner,true);
        Mod->RestoreOriginal();TestEqual(TEXT("One provider cannot unhide another provider's replacement"),VisibleCount(FX),0);
        FX->SetCosmeticParticlesHidden(SecondOwner,false);TestTrue(TEXT("Last provider restores effects"),VisibleCount(FX)>0);
        TestTrue(TEXT("Main hand transmog reapplies before swapping"),Mod->Attach(Weapon));
        TestTrue(TEXT("Replacement can move to another held item"),Mod->Attach(Other));
        TestTrue(TEXT("Previously replaced weapon regains its particles"),VisibleCount(FX)>0);
        TestEqual(TEXT("Newly replaced item loses its particles"),VisibleCount(OtherFX),0);
        Mod->RestoreOriginal();Weapon->Destroy();Other->Destroy();
    }
    return !HasAnyErrors();
}
#endif
