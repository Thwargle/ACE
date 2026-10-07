#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Slate/WidgetRenderer.h"
#include "RenderingThread.h"
#include "ImageUtils.h"
#include "ShaderCompiler.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"
#include "ACECharacterCreation.h"
#include "ACEPlayerController.h"
#include "ACEDatSubsystem.h"
#include "ACEClientSubsystem.h"
#include "ACESession.h"
#include "UI/ACEUICanvasWidget.h"
#include "UI/ACEUICharGenBinder.h"
#include "UI/ACEUIGameplayBinder.h"
#include "UI/ACEUIElementManager.h"
#include "UI/ACEUILayoutResolver.h"
#include "UI/ACEUIResourceResolver.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEBarberTest,"ACE.RetailParity.Barber",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACEBarberTest::RunTest(const FString&)
{
    TGuardValue<uint64> FrameGuard(GFrameCounter,GFrameCounter);
    UWorld::InitializationValues Values;Values.AllowAudioPlayback(false).RequiresHitProxies(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* GI=NewObject<UGameInstance>(GEngine);World->SetGameInstance(GI);GI->Init();
    ON_SCOPE_EXIT {GI->Shutdown();GEngine->DestroyWorldContext(World);World->DestroyWorld(false);};
    auto* Dat=GI->GetSubsystem<UACEDatSubsystem>();
    if(!TestTrue(TEXT("Retail DAT opens"),Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call"))))return false;
    FACECharacterCreation Model;FString Error;if(!TestTrue(TEXT("Chargen DAT decodes"),Model.Load(*Dat->GetPortalDat(),Error)))return false;
    using B=FACEBarberProfile;
    int32 Styles=0;
    for(const auto& Heritage:Model.Heritages)for(const auto& Sex:Heritage.Value.Sexes)
    {
        Model.SelectHeritage(Heritage.Key);Model.SelectSex(Sex.Key);
        for(int32 Hair=0;Hair<Sex.Value.Hair.Num();++Hair)
        {
            Model.Selection.HairStyle=Hair;Model.Selection.SkinHue=.23;Model.Selection.HairHue=.77;
            const auto P=Model.MakeBarberProfile();
            FACECharacterCreation Restored=Model;
            Restored.Selection.HairStyle=0;
            TestTrue(TEXT("Server appearance restores"),Restored.RestoreBarber(P,Heritage.Key,Sex.Key));
            const auto RoundTrip=Restored.MakeBarberProfile();
            for(int32 I=0;I<B::Count;++I)TestEqual(*FString::Printf(TEXT("Heritage %u sex %u hair %d field %d round trips"),Heritage.Key,Sex.Key,Hair,I),RoundTrip.Values[I],P.Values[I]);
            const auto Initial=Restored.Selection;auto Custom=P;Custom.Values[B::Option2]=123;Custom.Values[B::NoseTexture]=0x0500ABCD;
            const auto Unchanged=Restored.BuildBarberProfile(Custom,Initial,false);
            for(int32 I=0;I<B::Count;++I)TestEqual(TEXT("Untouched server data is preserved"),Unchanged.Values[I],Custom.Values[I]);
            ++Styles;
        }
    }
    TestTrue(TEXT("Multiple heritage hairstyles exercised"),Styles>100);
    TestEqual(TEXT("Female shadow crown removal"),B::EffectSetup(5,2,0,false),0x02001970u);
    TestEqual(TEXT("Male penumbra no crown"),B::EffectSetup(10,1,0,true),0x02001A5Du);
    TestEqual(TEXT("Female undead zombie no flame"),B::EffectSetup(11,2,0x02001AA1,true),0x02001AA2u);
    Model.SelectHeritage(1);Model.SelectSex(1);const auto Original=Model.MakeBarberProfile();
    const auto Initial=Model.Selection;Model.Selection.HairStyle=1;
    const auto Edited=Model.BuildBarberProfile(Original,Initial,false);
    TestEqual(TEXT("Changing hair does not change skin"),Edited.Values[B::SkinPalette],Original.Values[B::SkinPalette]);
    TestEqual(TEXT("Changing hair selects DAT head"),Edited.Values[B::Head],Model.MakeBarberProfile().Values[B::Head]);

    auto* Client=NewObject<UACEClientSubsystem>(GI);Client->Session=MakeShared<FACESession>();
    auto& S=*Client->Session;S.State=EACESessionState::InWorld;S.PlayerGuid=1234;S.PlayerVitals.HeritageGroup=1;S.PlayerVitals.Gender=1;
    auto Event=[&](const B& P)
    {FACEBinaryWriter W;W.WriteUInt32(1234);W.WriteUInt32(1);W.WriteUInt32(0x75);for(uint32 V:P.Values)W.WriteUInt32(V);return W.GetData();};
    const auto Bytes=Event(Original);
    for(int32 Cut=0;Cut<Bytes.Num();++Cut)
    {auto Partial=Bytes;Partial.SetNum(Cut);FACEBinaryReader R(Partial);S.HandleGameEvent(R);TestFalse(TEXT("Truncated barber event stays closed"),S.IsBarberOpen());}
    auto Open=[&](const B& P){auto Data=Event(P);FACEBinaryReader R(Data);S.HandleGameEvent(R);};
    Open(Original);TestTrue(TEXT("Complete event opens barber"),S.IsBarberOpen());TestEqual(TEXT("Only complete event publishes revision"),S.GetBarberRevision(),uint64(1));
    auto* Resources=NewObject<UACEUIResourceResolver>();Resources->Initialize(Dat);
    auto* Manager=NewObject<UACEUIElementManager>();Manager->Initialize();
    auto* Layout=NewObject<UACEUILayoutResolver>();Layout->Initialize(Dat,Manager);Layout->LoadLayout(0x21000005);
    Client->UIElementManager=Manager;Client->UIResourceResolver=Resources;
    auto* Canvas=NewObject<UACEUICanvasWidget>();Canvas->Initialize();Canvas->InitializeCanvas(Manager);Canvas->SetResourceResolver(Resources);auto Slate=Canvas->TakeWidget();
    auto* PC=World->SpawnActor<AACEPlayerController>();auto* Binder=NewObject<UACEUICharGenBinder>();
    if(!TestTrue(TEXT("Retail barber layout initializes"),Binder->InitializeBarber(Client,Canvas,PC)))return false;
    Canvas->SetBarberBinder(Binder);
    ON_SCOPE_EXIT {Binder->Shutdown();Canvas->SetBarberBinder(nullptr);Manager->Shutdown();};
    Binder->Tick(.1f);
    TestTrue(TEXT("Gameplay remains visible"),Manager->FindElementByName(TEXT("RootGameplay_Field"))->bVisible);
    TestTrue(TEXT("Barber face controls found"),Binder->Find(TEXT("HairSpin")).IsValid());
    TestFalse(TEXT("Ordinary race has no effect toggle"),bool(Binder->Find(TEXT("BarberOption1"))->bVisible));
    TestEqual(TEXT("Barber preview has square aspect"),Binder->Target->SizeX,Binder->Target->SizeY);
    const auto Before=Binder->Model.Selection;
    auto Foreign=MakeShared<FACEUIElement>();Foreign->ElementName=TEXT("ColorSpot2");Binder->Activate(Foreign);
    TestEqual(TEXT("Gameplay controls cannot edit barber"),Binder->Model.Selection.HairColor,Before.HairColor);
    if(FApp::CanEverRender())
    {
        FWidgetRenderer Renderer(true,true);auto* Target=FWidgetRenderer::CreateTargetFor(FVector2D(1200,900),TF_Bilinear,true);
        for(int32 I=0;I<20;++I){++GFrameCounter;Binder->Tick(1.f/30);Canvas->NativeTick(Canvas->GetCachedGeometry(),1.f/30);World->Tick(LEVELTICK_All,1.f/30);if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();Renderer.DrawWidget(Target,Slate,FVector2D(1200,900),1.f/30);FlushRenderingCommands();}
        TArray<FColor> Pixels;Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels);TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(1200,900,Pixels,PNG);
        const FString Path=FPaths::ProjectSavedDir()/TEXT("Automation/Barber");IFileManager::Get().MakeDirectory(*Path,true);FFileHelper::SaveArrayToFile(PNG,*(Path/TEXT("Human.png")));
        Binder->Capture->ClearShowOnlyComponents();Binder->Capture->CaptureScene();FlushRenderingCommands();
        Renderer.DrawWidget(Target,Slate,FVector2D(1200,900),0);FlushRenderingCommands();
        TArray<FColor> WithoutModel;Target->GameThread_GetRenderTargetResource()->ReadPixels(WithoutModel);int32 ModelPixels=0;
        if(Pixels.Num()==WithoutModel.Num())for(int32 I=0;I<Pixels.Num();++I)
            if(FMath::Max3(FMath::Abs(int(Pixels[I].R)-WithoutModel[I].R),FMath::Abs(int(Pixels[I].G)-WithoutModel[I].G),FMath::Abs(int(Pixels[I].B)-WithoutModel[I].B))>24)++ModelPixels;
        TestTrue(*FString::Printf(TEXT("Composed barber shows head (%d pixels)"),ModelPixels),ModelPixels>500);
    }
    const uint32 Sequence=S.NextPacketSequence;Binder->Activate(Binder->Find(TEXT("BarberCancelButton")));
    TestFalse(TEXT("Cancel closes"),S.IsBarberOpen());TestEqual(TEXT("Cancel sends no action"),S.NextPacketSequence,Sequence);
    TestFalse(TEXT("Closed barber cannot submit"),S.FinishBarber(Edited));
    Binder->Shutdown();Open(Original);TestTrue(TEXT("Barber can reopen"),Binder->InitializeBarber(Client,Canvas,PC));
    auto* Sockets=ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);auto Address=Sockets->CreateInternetAddr();bool Valid=false;Address->SetIp(TEXT("127.0.0.1"),Valid);Address->SetPort(0);
    auto* Receiver=Sockets->CreateSocket(NAME_DGram,TEXT("Barber receiver"),false);if(!Receiver)return false;
    ON_SCOPE_EXIT {Receiver->Close();Sockets->DestroySocket(Receiver);};
    if(!TestTrue(TEXT("Loopback binds"),Valid && Receiver->Bind(*Address)))return false;Receiver->GetAddress(*Address);
    S.SocketC2S=Sockets->CreateSocket(NAME_DGram,TEXT("Barber sender"),false);if(!S.SocketC2S)return false;
    ON_SCOPE_EXIT {S.SocketC2S->Close();Sockets->DestroySocket(S.SocketC2S);S.SocketC2S=nullptr;};
    S.ServerC2SAddr=Address;S.IssacClient=MakeUnique<FACEIsaac>(123);
    Binder->Model.Selection.HairStyle=1;const auto Expected=Binder->Model.BuildBarberProfile(Original,Binder->BarberInitial,false);
    Binder->Activate(Binder->Find(TEXT("BarberApplyButton")));
    const auto* Packet=S.CachedC2SPackets.Find(S.NextPacketSequence-1);
    if(TestNotNull(TEXT("Apply sends appearance"),Packet))
    {
        FACEBinaryReader R(Packet->Payload);R.Skip(14);TestEqual(TEXT("Retail weenie queue"),R.ReadUInt16(),uint16(ACEQueue::WeenieQueue));
        R.Skip(8);TestEqual(TEXT("FinishBarber opcode"),R.ReadUInt32(),0x311u);
        for(int32 I=0;I<B::Count;++I)TestEqual(*FString::Printf(TEXT("Wire appearance field %d"),I),R.ReadUInt32(),Expected.Values[I]);
        TestEqual(TEXT("Exactly sixteen fields"),R.Remaining(),0);
    }
    TestFalse(TEXT("Apply closes barber"),S.IsBarberOpen());
    TestEqual(TEXT("Apply awaits server appearance"),S.GetBarberProfile().Values[B::Head],Original.Values[B::Head]);
    Binder->Shutdown();Canvas->SetBarberBinder(nullptr);
    auto* Gameplay=NewObject<UACEUIGameplayBinder>();Gameplay->Initialize(Client,Manager,Canvas,PC);Canvas->SetGameplayBinder(Gameplay);
    ON_SCOPE_EXIT {Gameplay->Shutdown();Canvas->SetGameplayBinder(nullptr);};
    // Exercise the actual gameplay owner, including replacing an open request.
    Model.SelectHeritage(9);Model.SelectSex(2);auto Empyrean=Model.MakeBarberProfile();Empyrean.Values[B::Option1]=1;
    S.PlayerVitals.HeritageGroup=9;S.PlayerVitals.Gender=2;Open(Empyrean);Gameplay->RefreshBarber();
    if(TestNotNull(TEXT("Incoming event opens gameplay barber"),Gameplay->Barber.Get()))
    {
        TestTrue(TEXT("Empyrean checkbox visible"),bool(Gameplay->Barber->Find(TEXT("BarberOption1"))->bVisible));
        TestTrue(TEXT("Server earthbound option restored"),Gameplay->Barber->bSuppressEffect);
        TestEqual(TEXT("Option layout reserves retail checkbox row"),Gameplay->Barber->Find(TEXT("BarberApplyButton"))->Y,298);
        Gameplay->Barber->Activate(Gameplay->Barber->Find(TEXT("BarberOption1")));
        TestEqual(TEXT("Earthbound change serialized"),Gameplay->Barber->Model.BuildBarberProfile(Empyrean,Gameplay->Barber->BarberInitial,Gameplay->Barber->bSuppressEffect).Values[B::Option1],0u);
        Gameplay->Barber->Activate(Gameplay->Barber->Find(TEXT("RotateClockwise")));
        Gameplay->Barber->MouseUp();TestEqual(TEXT("Retail rotation persists after release"),Gameplay->Barber->RotateDirection,1);
        Gameplay->Barber->Activate(Gameplay->Barber->Find(TEXT("RotateClockwise")));
        TestEqual(TEXT("Second rotation click stops"),Gameplay->Barber->RotateDirection,0);
        auto* Old=Gameplay->Barber.Get();Open(Empyrean);Gameplay->RefreshBarber();
        TestTrue(TEXT("New server request replaces editor"),Old!=Gameplay->Barber.Get());
        TestTrue(TEXT("Reopening restores authoritative option"),Gameplay->Barber->bSuppressEffect);
    }
    S.CloseBarber();Gameplay->RefreshBarber();
    TestNull(TEXT("Closed request destroys preview binder"),Gameplay->Barber.Get());
    TestNull(TEXT("Gameplay regains ordinary input"),Canvas->GetAppearanceInputBinder());
    return true;
}
#endif
