#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ACECharacterCreation.h"
#include "ACEDatSubsystem.h"
#include "ACESession.h"
#include "ACEClientSubsystem.h"
#include "ACEPlayerController.h"
#include "UI/ACEUICharGenBinder.h"
#include "UI/ACEUICanvasWidget.h"
#include "UI/ACEUIElementManager.h"
#include "UI/ACEUILayoutResolver.h"
#include "UI/ACEUIResourceResolver.h"
#include "UI/ACERetailTextBlock.h"
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Slate/WidgetRenderer.h"
#include "Blueprint/WidgetTree.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ShaderCompiler.h"
#include "RenderingThread.h"
#include "Protocol/ACEHash32.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"
#include "InputCoreTypes.h"
#include "HAL/FileManager.h"
#include "ACECharacterAppearanceComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/PrimitiveComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/Material.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACERetailCharacterCreationTest,"ACE.RetailParity.CharacterCreationData",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FACERetailCharacterCreationTest::RunTest(const FString&)
{
    auto GI=NewObject<UGameInstance>();auto Dat=NewObject<UACEDatSubsystem>(GI);if(!Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call")))return false;
    FACECharacterCreation Model;FString Error;if(!TestTrue(*Error,Model.Load(*Dat->GetPortalDat(),Error))){AddError(Error);return false;}
    TestEqual(TEXT("All retail heritages decoded"),Model.Heritages.Num(),13);TestEqual(TEXT("Retail starting areas"),Model.StartAreas.Num(),5);
    // Export decoded garment parts, replacement textures and every shade/palette
    // for the independent ACE.DatLoader oracle (diag/CreationAppearanceDump).
    TArray<FString> ClothingRows;TSet<uint64> ClothingCases;
    for(const auto& H:Model.Heritages)for(const auto& S:H.Value.Sexes)for(const auto& Gear:S.Value.Gear)for(const auto& G:Gear)
    {
        const uint64 Key=(uint64(G.ClothingTable)<<32)|S.Value.Setup;if(ClothingCases.Contains(Key))continue;ClothingCases.Add(Key);
        TArray<uint32> Colors;FACEObjDesc Empty;TestTrue(TEXT("Clothing color list decodes"),Model.ApplyClothing(G.ClothingTable,S.Value.Setup,0,0,Empty,&Colors));
        for(uint32 Color:Colors)for(int Shade=0;Shade<3;++Shade)
        {
            FACEObjDesc Look;TestTrue(TEXT("Starter garment appearance decodes"),Model.ApplyClothing(G.ClothingTable,S.Value.Setup,Color,Shade*.5,Look));
            const FString Prefix=FString::Printf(TEXT("%u,%u,%u,%d,"),G.ClothingTable,S.Value.Setup,Color,Shade);
            for(auto Part:Look.AnimPartChanges)ClothingRows.Add(Prefix+FString::Printf(TEXT("part,%u,%u"),Part.PartIndex,uint32(Part.PartId)));
            for(auto Tex:Look.TextureChanges)ClothingRows.Add(Prefix+FString::Printf(TEXT("texture,%u,%u,%u"),Tex.PartIndex,uint32(Tex.OldTexture),uint32(Tex.NewTexture)));
            for(auto Pal:Look.SubPalettes)ClothingRows.Add(Prefix+FString::Printf(TEXT("palette,%d,%d,%u"),Pal.Offset,Pal.NumColors,uint32(Pal.SubPaletteId)));
        }
    }
    FFileHelper::SaveStringArrayToFile(ClothingRows,*(FPaths::ProjectSavedDir()/TEXT("Automation/CreationClothingActual.csv")));
    TestTrue(TEXT("Language strings read directly from language DAT"),Model.LoadStrings(Dat->GetDatDirectory()));
    TestTrue(TEXT("Retail heritage prose available"),Model.Text(TEXT("ID_CharGen_AluvianText")).Len()>100);
    TestEqual(TEXT("Retail name case and prefix exceptions"),Model.FormatName(TEXT("mCArthur o'NEILL IV")),FString(TEXT("McArthur o'Neill IV")));
    TestEqual(TEXT("Retail filters unsupported punctuation and duplicate separators"),Model.FormatName(TEXT("  ALICE--BOB123  ")),FString(TEXT("Alice-Bob")));
    TestFalse(TEXT("Name input rejects numbers"),Model.IsNameCharacter('7'));
    Model.SelectHeritage(1);Model.SelectSex(1);Model.Selection.HairStyle=2;Model.Selection.Eyes=1;Model.Selection.SkinHue=.37;
    Model.SelectProfession(4);Model.SelectSex(2);
    TestEqual(TEXT("Gender preserves compatible hair choice"),Model.Selection.HairStyle,2u);
    TestEqual(TEXT("Gender preserves compatible face choice"),Model.Selection.Eyes,1u);
    TestEqual(TEXT("Gender preserves selected hue"),Model.Selection.SkinHue,.37);
    Model.SelectHeritage(3);TestEqual(TEXT("Heritage keeps the selected profession"),Model.Selection.Profession,4);
    const auto BeforeFit=Model.Selection;Model.FitProfession();
    TestEqual(TEXT("Fitting recognizes the retail profession"),Model.Selection.Profession,4);
    TestTrue(TEXT("Fitting never reallocates skills"),Model.Selection.Skills==BeforeFit.Skills);
    for(int A=0;A<6;++A)TestEqual(TEXT("Fitting never reallocates attributes"),Model.Selection.Attributes[A],BeforeFit.Attributes[A]);
    Model.RandomizeProfession();TestTrue(TEXT("Random profession excludes custom and previous choice"),Model.Selection.Profession>0&&Model.Selection.Profession!=4);
    Model.RandomizeSkills();Model.Selection.Name=TEXT("Random Skills");TestTrue(TEXT("Random skills respect all credit and heritage constraints"),Model.Validate(Error));
    for(int I=0;I<20;++I)
    {
        Model.Selection.Name=TEXT("Discarded Draft");Model.Selection.Slot=3;Model.RandomizeCharacter();
        TestTrue(TEXT("Retail random heritage chooses the four base cultures"),Model.Selection.Heritage>=1&&Model.Selection.Heritage<=4);
        TestTrue(TEXT("Whole-character randomization clears name"),Model.Selection.Name.IsEmpty());
        TestEqual(TEXT("Randomization retains account slot"),Model.Selection.Slot,3u);
        TestTrue(TEXT("Initial character spends attribute budget"),Model.AttributeCredits()==0);
        TestTrue(TEXT("Initial town is primary for heritage"),Model.Heritage()->PrimaryStarts.Contains(Model.Selection.StartArea));
        Model.Selection.Name=TEXT("Random Hero");TestTrue(TEXT("Complete randomized character valid"),Model.Validate(Error));
    }
    for(const auto& H:Model.Heritages)
    {
        Model.SelectHeritage(H.Key);Model.Selection.Name=TEXT("Creation Test");
        for(const auto& S:H.Value.Sexes){Model.SelectSex(S.Key);for(int I=0;I<H.Value.Professions.Num();++I){Model.SelectProfession(I);bool Valid=Model.Validate(Error);if(!Valid)AddError(FString::Printf(TEXT("Retail default %u/%u/%d: %s credits=%d"),H.Key,S.Key,I,*Error,Model.SkillCredits()));}}
        FACEWorldObject Object;TestTrue(TEXT("Appearance builds for every heritage"),Model.BuildAppearance(Object));TestTrue(TEXT("Preview setup is authored"),Object.SetupId!=0);
        Model.RandomizeAppearance();TestTrue(TEXT("Random choices stay valid"),Model.Validate(Error));
    }
    Model.SelectHeritage(1);Model.SelectProfession(0);Model.Selection.Name=TEXT("Cooking Test");
    // These bytes are a server/retail String16L fixture, not a writer-reader-only round trip.
    FACEBinaryWriter Encoded;Encoded.WriteString16L(TEXT("Ren\u00e9 \u0152\u2019"));
    const TArray<uint8> ExpectedName={7,0,'R','e','n',0xe9,' ',0x8c,0x92,0,0,0};
    TestTrue(TEXT("Names use Windows-1252 byte lengths and padding"),Encoded.GetData()==ExpectedName);
    FACEBinaryReader NameReader(ExpectedName);TestEqual(TEXT("Names decode independently of system locale"),NameReader.ReadString16L(),FString(TEXT("Ren\u00e9 \u0152\u2019")));
    TestEqual(TEXT("Free trained Run preserved"),Model.Selection.Skills[14],2u);TestFalse(TEXT("Cannot untrain heritage free skill"),Model.SetSkill(14,1));
    Model.Heritages[1].SkillCosts.Add(39,FIntPoint(0,0));Model.SelectProfession(0);
    TestEqual(TEXT("Free specialization preserved by reset"),Model.Selection.Skills[39],3u);
    TestFalse(TEXT("Free specialized skill cannot be demoted"),Model.SetSkill(39,2));
    Model.Heritages[1].SkillCosts.Remove(39);Model.SelectProfession(0);
    TestTrue(TEXT("Cooking trains"),Model.SetSkill(39,2));const int TrainedCredits=Model.SkillCredits();
    TestTrue(TEXT("Cooking specializes"),Model.SetSkill(39,3));TestEqual(TEXT("Specialization charges only difference"),Model.SkillCredits(),TrainedCredits-Model.SkillCost(39).Y+Model.SkillCost(39).X);
    Model.SetAttribute(2,100);Model.SetAttribute(4,100);TestEqual(TEXT("Creation Cooking formula and specialization bonus"),Model.SkillValue(39),77);
    Model.Selection.Locked[2]=true;Model.SetAttribute(2,10);TestEqual(TEXT("Locked attribute cannot be dragged"),Model.Selection.Attributes[2],100);
    for(int I=0;I<6;++I)Model.SetAttribute(I,100);TestTrue(TEXT("Sliders cannot overspend budget"),Model.AttributeCredits()>=0);TestEqual(TEXT("Borrowing preserves locked Coordination"),Model.Selection.Attributes[2],100);
    TArray<uint8> Blob;Dat->GetPortalDat()->ReadFile(0x0e000002,Blob);FACECharacterCreation Broken;
    for(int Length:{0,5,100,1000,Blob.Num()-1}){auto Cut=Blob;Cut.SetNum(Length);TestFalse(TEXT("Truncated creation DAT rejected"),Broken.LoadBytes(Cut,Error));}
    FACEBinaryWriter W;W.WriteString16L(TEXT("account"));Model.Selection.Write(W);FFileHelper::SaveArrayToFile(W.GetData(),*(FPaths::ProjectSavedDir()/TEXT("Automation/CharacterCreatePayload.bin")));
    FACEBinaryReader R(W.GetData());TestEqual(TEXT("Account String16L before creation data"),R.ReadString16L(),FString(TEXT("account")));TestEqual(TEXT("Version"),R.ReadUInt32(),1u);TestEqual(TEXT("Heritage wire value"),R.ReadUInt32(),1u);TestEqual(TEXT("Sex wire value"),R.ReadUInt32(),Model.Selection.Sex);
    R.Skip(14*4+6*8);TestEqual(TEXT("Profession after six hue doubles"),R.ReadInt32(),Model.Selection.Profession);for(int A:Model.Selection.Attributes)TestEqual(TEXT("Attribute wire order"),R.ReadUInt32(),uint32(A));R.Skip(8);TestEqual(TEXT("Full indexed skill vector"),R.ReadUInt32(),uint32(Model.Selection.Skills.Num()));for(uint32 L:Model.Selection.Skills)TestEqual(TEXT("Skill levels"),R.ReadUInt32(),L);TestEqual(TEXT("Name retains padded String16L"),R.ReadString16L(),Model.Selection.Name);TestEqual(TEXT("Start area"),R.ReadUInt32(),Model.Selection.StartArea);TestEqual(TEXT("No forged admin privileges"),R.ReadUInt32(),0u);TestEqual(TEXT("No forged sentinel privileges"),R.ReadUInt32(),0u);TestEqual(TEXT("Payload exhausted"),R.Remaining(),0);
    FACESession Session;Session.State=EACESessionState::CharacterSelect;Session.bCharacterCreationPending=true;
    int Events=0;uint32 LastResult=0;Session.OnCharacterCreated.AddLambda([&](uint32 Code,const FACECharacterInfo&){++Events;LastResult=Code;});
    FACEBinaryWriter Reply;Reply.WriteUInt32(1);Reply.WriteUInt32(1234);Reply.WriteString16L(TEXT("Created Hero"));Reply.WriteUInt32(0);
    auto Truncated=Reply.GetData();Truncated.Pop();FACEBinaryReader Bad(Truncated);Session.HandleCharacterCreated(Bad);TestEqual(TEXT("Partial success cannot create a phantom character"),Events,0);TestTrue(TEXT("Partial response stays pending"),Session.bCharacterCreationPending);
    FACEBinaryWriter Reject;Reject.WriteUInt32(3);FACEBinaryReader Failed(Reject.GetData());Session.HandleCharacterCreated(Failed);TestEqual(TEXT("Name collision result delivered"),LastResult,3u);TestFalse(TEXT("Name collision permits retry"),Session.bCharacterCreationPending);TestEqual(TEXT("Failure does not add character"),Session.Characters.Num(),0);
    Session.bCharacterCreationPending=true;FACEBinaryReader Good(Reply.GetData());Session.HandleCharacterCreated(Good);TestEqual(TEXT("Success adds server assigned GUID"),Session.Characters[0].CharacterId,1234);TestEqual(TEXT("Success event once"),Events,2);FACEBinaryReader Duplicate(Reply.GetData());Session.HandleCharacterCreated(Duplicate);TestEqual(TEXT("Duplicate response ignored"),Events,2);
    // Exercise CreateCharacter itself over UDP, including the authenticated checksum,
    // reliable packet cache, UI queue, payload and duplicate-submit suppression.
    auto Sockets=ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    auto Receiver=Sockets->CreateSocket(NAME_DGram,TEXT("Character creation receiver"),false);
    if(!TestNotNull(TEXT("Creation loopback receiver"),Receiver))return false;
    auto Address=Sockets->CreateInternetAddr();bool Valid=false;Address->SetIp(TEXT("127.0.0.1"),Valid);Address->SetPort(0);
    if(!Receiver->Bind(*Address)){Sockets->DestroySocket(Receiver);AddError(TEXT("Creation loopback bind failed"));return false;}
    Receiver->GetAddress(*Address);Receiver->SetNonBlocking(true);
    FACESession Sender;Sender.State=EACESessionState::CharacterSelect;Sender.AccountName=TEXT("account");Sender.ServerC2SAddr=Address;
    TestFalse(TEXT("Disconnected transport cannot leave creation pending"),Sender.CreateCharacter(Model.Selection));
    TestFalse(TEXT("Disconnected creation did not become pending"),Sender.IsCharacterCreationPending());
    Sender.SocketC2S=Sockets->CreateSocket(NAME_DGram,TEXT("Character creation sender"),false);
    const uint8 Seed[]={1,2,3,4};Sender.IssacClient=MakeUnique<FACEIsaac>(Seed);FACEIsaac ExpectedKeys(Seed);
    Sender.CharacterSlotCount=0;TestFalse(TEXT("Full account cannot create"),Sender.CreateCharacter(Model.Selection));Sender.CharacterSlotCount=11;
    TestTrue(TEXT("Finish sends a real creation request"),Sender.CreateCharacter(Model.Selection));
    const uint32 Sequence=Sender.NextFragmentSequence;TestFalse(TEXT("Pending submission cannot send twice"),Sender.CreateCharacter(Model.Selection));TestEqual(TEXT("Duplicate Finish sends no fragment"),Sender.NextFragmentSequence,Sequence);
    uint8 Buffer[2048];int Read=0;const double Deadline=FPlatformTime::Seconds()+1;
    while(!Receiver->Recv(Buffer,sizeof(Buffer),Read)&&FPlatformTime::Seconds()<Deadline)FPlatformProcess::Sleep(.001f);
    TestTrue(TEXT("Creation request reaches the wire"),Read>36);
    if(Read>36){FACEBinaryReader Packet(Buffer,Read);uint32 Seq=Packet.ReadUInt32();auto Flags=static_cast<EACEPacketHeaderFlags>(Packet.ReadUInt32());uint32 Hash=Packet.ReadUInt32();uint16 Id=Packet.ReadUInt16(),Stamp=Packet.ReadUInt16(),Size=Packet.ReadUInt16(),Iteration=Packet.ReadUInt16();TestTrue(TEXT("Creation checksum authenticated"),EnumHasAnyFlags(Flags,EACEPacketHeaderFlags::EncryptedChecksum));TestEqual(TEXT("Creation checksum uses the negotiated ISAAC stream"),Hash,FACESession::HeaderHash32(Seq,Flags,Id,Stamp,Size,Iteration)+(FACEHash32::Calculate(Buffer+20,Size)^ExpectedKeys.Next()));Packet.Skip(14);TestEqual(TEXT("Creation uses UI queue"),Packet.ReadUInt16(),ACEQueue::UIQueue);TestEqual(TEXT("Creation opcode F656"),Packet.ReadUInt32(),0xf656u);TestTrue(TEXT("Account and every customization reach the wire intact"),Packet.ReadBytes(Packet.Remaining())==W.GetData());TestTrue(TEXT("Creation request retained for retransmission"),Sender.CachedC2SPackets.Contains(Seq));}
    Receiver->Close();Sockets->DestroySocket(Receiver);
    // Emit every heritage/gender variant for independent ACE.Entity parser validation.
    for(const auto& H:Model.Heritages)for(const auto& Sex:H.Value.Sexes){Model.SelectHeritage(H.Key);Model.SelectSex(Sex.Key);Model.RandomizeAppearance();Model.Selection.Name=TEXT("Ren\u00e9 Creation");FACEBinaryWriter Payload;Payload.WriteString16L(TEXT("fixture"));Model.Selection.Write(Payload);FFileHelper::SaveArrayToFile(Payload.GetData(),*(FPaths::ProjectSavedDir()/FString::Printf(TEXT("Automation/CharacterCreate-%u-%u.bin"),H.Key,Sex.Key)));}
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACERetailCharacterCreationScreenTest,"ACE.RetailParity.CharacterCreationScreens",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FACERetailCharacterCreationScreenTest::RunTest(const FString&)
{
    UWorld::InitializationValues Values;Values.AllowAudioPlayback(false).RequiresHitProxies(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
    auto World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto GI=NewObject<UGameInstance>(GEngine);World->SetGameInstance(GI);GI->Init();auto Dat=GI->GetSubsystem<UACEDatSubsystem>();Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call"));
    auto Resources=NewObject<UACEUIResourceResolver>();Resources->Initialize(Dat);auto Manager=NewObject<UACEUIElementManager>();Manager->Initialize();auto Layout=NewObject<UACEUILayoutResolver>();Layout->Initialize(Dat,Manager);Layout->LoadLayout(0x21000038);
    auto Client=NewObject<UACEClientSubsystem>(GI);Client->Session=MakeShared<FACESession>();Client->UIElementManager=Manager;Client->UIResourceResolver=Resources;
    auto PC=World->SpawnActor<AACEPlayerController>();auto Canvas=NewObject<UACEUICanvasWidget>();Canvas->Initialize();Canvas->InitializeCanvas(Manager);Canvas->SetResourceResolver(Resources);auto Slate=Canvas->TakeWidget();auto Binder=NewObject<UACEUICharGenBinder>();
    bool Started=Binder->Initialize(Client,Canvas,PC);TestTrue(TEXT("Creation binder initialized from DAT"),Started);if(!Started){GI->Shutdown();GEngine->DestroyWorldContext(World);World->DestroyWorld(false);return false;}
    Canvas->SetCharGenBinder(Binder);
    TestTrue(TEXT("Creation canvas accepts real Slate keyboard focus"),Slate->SupportsKeyboardFocus());
    // A real gameplay camera remains far from the examination viewport actor.
    // The previous fixture had no camera, bypassing world-distance animation culling.
    if(!PC->PlayerCameraManager)PC->PlayerCameraManager=World->SpawnActor<APlayerCameraManager>();
    PC->PlayerCameraManager->InitializeFor(PC);
    World->AddController(PC);
    TestEqual(TEXT("Fixture exercises the gameplay camera"),World->GetFirstPlayerController(),static_cast<APlayerController*>(PC));
    Binder->Model.SelectHeritage(1);Binder->Model.SelectSex(1);Binder->bClothes=true;Binder->SetPage(4);Binder->RefreshPreview(1.f/30);
    auto Appearance=Binder->Preview->FindComponentByClass<UACECharacterAppearanceComponent>();
    const uint32 PreviewAnim=Binder->Model.MappedAsset(0x25000010,0x10000006);
    TestTrue(TEXT("Preview is beyond world animation culling distance"),FVector::DistSquared(Binder->Preview->GetActorLocation(),PC->PlayerCameraManager->GetCameraLocation())>FMath::Square(8000.f));
    for(float Time:{0.f,.2f,.5f})
    {
        Appearance->SetPreviewAnimation(PreviewAnim,true);Appearance->TickComponent(Time,LEVELTICK_All,nullptr);
        TArray<FTransform> Expected;int32 Count=0;
        if(TestTrue(TEXT("Retail creation sequence decodes"),Dat->EvaluateAnimationLoop(PreviewAnim,Time,17,Expected,100,Count)))
            for(int Part=0;Part<Count;++Part){FTransform Actual;TestTrue(TEXT("Preview part exists"),Appearance->GetPartCurrentTransform(Part,Actual));TestTrue(*FString::Printf(TEXT("Part %d matches retail frame at %.1fs"),Part,Time),Actual.Equals(Expected[Part],.001f));}
    }
    Appearance->SetPreviewAnimation(Binder->Model.MappedAsset(0x25000010,0x10000005),false);
    TestFalse(TEXT("Face examination stops at authored rest frame"),Appearance->IsComponentTickEnabled());
    Binder->PreviewAnimationId=0;
    Binder->bPreviewDirty=true;
    auto Capture=[&](int Page){Binder->SetPage(Page);if(!FApp::CanEverRender())return;FWidgetRenderer Renderer(true,true);auto Target=FWidgetRenderer::CreateTargetFor(FVector2D(1600,1200),TF_Bilinear,true);for(int Pass=0;Pass<30;++Pass){Canvas->NativeTick(Canvas->GetCachedGeometry(),1.f/30);World->Tick(LEVELTICK_All,1.f/30);if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();Renderer.DrawWidget(Target,Slate,FVector2D(1600,1200),1.f/30);FlushRenderingCommands();}TArray<FColor> Pixels;Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels);TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(1600,1200,Pixels,PNG);FFileHelper::SaveArrayToFile(PNG,*(FPaths::ProjectSavedDir()/FString::Printf(TEXT("Automation/RetailParity/CharacterCreation%d.png"),Page)));};
    for(int Page=1;Page<=6;++Page)Capture(Page);
    auto NameField=Manager->FindElementByName(TEXT("NameTextBox"));
    TestEqual(TEXT("Native name prompt resolves bracket escapes"),Binder->Labels[NameField->InstanceId]->GetText().ToString(),FString(TEXT("[ Name ]")));
    Binder->SetPage(1);Binder->Activate(Manager->FindElementByName(TEXT("RadioSho")));TestEqual(TEXT("Heritage button selects correct wire ID"),Binder->Model.Selection.Heritage,3u);
    Binder->SetPage(2);Binder->Activate(Manager->FindElementByName(TEXT("WarButton")));TestEqual(TEXT("Profession button selects DAT template"),Binder->Model.Selection.Profession,4);
    Binder->SetPage(4);Binder->Activate(Manager->FindElementByName(TEXT("FemaleButton")));TestEqual(TEXT("Gender button applies new appearance set"),Binder->Model.Selection.Sex,2u);
    Binder->SetPage(6);for(TCHAR C:FString(TEXT("New Hero")))Binder->KeyChar(FCharacterEvent(C,FModifierKeysState(),0,false));TestEqual(TEXT("Summary accepts typed name"),Binder->Model.Selection.Name,FString(TEXT("New Hero")));
    // Route actual character events through Slate instead of calling the binder.
    auto InputWindow=SNew(SWindow).ClientSize(FVector2D(1600,1200))[Slate];
    FSlateApplication::Get().AddWindow(InputWindow,false);
    FSlateApplication::Get().SetKeyboardFocus(Slate,EFocusCause::SetDirectly);
    TestTrue(TEXT("Slate focus reaches the creation canvas"),FSlateApplication::Get().GetKeyboardFocusedWidget()==Slate);
    Binder->Model.Selection.Name.Empty();Binder->NameCaret=Binder->NameAnchor=0;
    for(TCHAR C:FString(TEXT("Typed Hero")))FSlateApplication::Get().ProcessKeyCharEvent(FCharacterEvent(C,FModifierKeysState(),0,false));
    TestEqual(TEXT("Summary receives keyboard text through Slate"),Binder->Model.Selection.Name,FString(TEXT("Typed Hero")));
    InputWindow->SetContent(SNullWidget::NullWidget);FSlateApplication::Get().RequestDestroyWindow(InputWindow);
    const auto Refresh=[&](){Canvas->NativeTick(Canvas->GetCachedGeometry(),1.f/30);};
    const auto Click=[&](TSharedPtr<FACEUIElement> E){if(!TestTrue(TEXT("Mouse target exists"),E.IsValid()))return;Refresh();FVector2D P=Canvas->LayoutToViewport(FVector2D(E->GetScreenOrigin())+FVector2D(E->Width*.5f,E->Height*.5f));const auto& Geometry=Canvas->GetCachedGeometry();P=Geometry.LocalToAbsolute(P);TSet<FKey> Buttons;Buttons.Add(EKeys::LeftMouseButton);FPointerEvent Down(0,P,P,Buttons,EKeys::LeftMouseButton,0,FModifierKeysState());auto Reply=Canvas->NativeOnMouseButtonDown(Geometry,Down);TestTrue(TEXT("Creation click retains keyboard focus"),Reply.GetUserFocusRecepient()==Slate);Canvas->NativeOnMouseButtonUp(Geometry,FPointerEvent(0,P,P,TSet<FKey>(),EKeys::LeftMouseButton,0,FModifierKeysState()));Refresh();};
    Click(Manager->FindElementByName(TEXT("CGAppearanceButton")));Click(Manager->FindElementByName(TEXT("ClothesButton")));
    Click(Manager->FindElementByName(TEXT("ShirtSpin")));TestEqual(TEXT("Shirt label selects shirt color controls"),Binder->ColorField,6);
    const auto ShirtColors=Binder->Model.GearColors(1);
    if(TestTrue(TEXT("Starter shirt has retail color choices"),ShirtColors.Num()>=8))
    {
        Click(Manager->FindElementByName(TEXT("ColorSpot2")));
        TestEqual(TEXT("Second swatch applies its palette template"),Binder->Model.Selection.GearColor[1],ShirtColors[1]);
        Capture(4);IFileManager::Get().Copy(*(FPaths::ProjectSavedDir()/TEXT("Automation/RetailParity/CreationClothesColor2.png")),*(FPaths::ProjectSavedDir()/TEXT("Automation/RetailParity/CharacterCreation4.png")));
        FACEWorldObject First;Binder->Model.BuildAppearance(First);
        Click(Manager->FindElementByName(TEXT("ColorSpot8")));
        TestEqual(TEXT("Eighth swatch applies its palette template"),Binder->Model.Selection.GearColor[1],ShirtColors[7]);
        auto Gradient=Manager->FindElementByName(TEXT("GradientScroll"));
        Binder->MouseDown(FVector2D(Gradient->GetScreenOrigin())+FVector2D(Gradient->Width*.5,74));Binder->MouseUp();
        TestEqual(TEXT("Shade control reaches its final retail palette"),Binder->Model.Selection.GearHue[1],1.);
        Capture(4);IFileManager::Get().Copy(*(FPaths::ProjectSavedDir()/TEXT("Automation/RetailParity/CreationClothesColor8.png")),*(FPaths::ProjectSavedDir()/TEXT("Automation/RetailParity/CharacterCreation4.png")));
        FACEWorldObject Second;Binder->Model.BuildAppearance(Second);
        TestNotEqual(TEXT("Swatch/shade alters the generated appearance"),First.Appearance.GetContentHash(),Second.Appearance.GetContentHash());
        TestEqual(TEXT("Rendered preview receives selected palette"),Appearance->GetAppliedAppearanceHash(),Second.Appearance.GetContentHash());
        if(auto* Torso=Cast<UPrimitiveComponent>(Appearance->GetPartMesh(9)))if(auto* Material=Torso->GetMaterial(0))
        {
            TestTrue(TEXT("Clothes use retail examination lighting"),Material->GetMaterial()->GetName().StartsWith(TEXT("M_ACEExamination_")));
            float Fog=0.f;TestTrue(TEXT("World fog cannot recolor the clothing preview"),!Material->GetScalarParameterValue(TEXT("FogAmount"),Fog)||Fog==0.f);
        }
    }
    Click(Manager->FindElementByName(TEXT("CGHeritageButton")));TestEqual(TEXT("Mouse navigation reaches heritage page"),Binder->GetPage(),1);
    Click(Manager->FindElementByName(TEXT("RadioSho")));TestEqual(TEXT("Mouse hits retail portrait radio"),Binder->Model.Selection.Heritage,3u);
    Click(Manager->FindElementByName(TEXT("CGProfessionButton")));Click(Manager->FindElementByName(TEXT("CustomButton")));
    Click(Manager->FindElementByName(TEXT("CGSkillsButton")));TestEqual(TEXT("Mouse navigation reaches skills"),Binder->GetPage(),3);
    Binder->SkillScroll=0;Refresh();TSharedPtr<FACEUIElement> Up;
    for(auto Row:Binder->SkillRows)if(Row&&Row->bVisible)if(auto Arrow=Binder->Child(Row,0x10000304);Arrow&&Arrow->bActivatable){Up=Arrow;break;}
    if(TestTrue(TEXT("Native training arrow visible and affordable"),Up.IsValid())){const int Skill=FCString::Atoi(*Up->Parent.Pin()->ElementName.Mid(13));const uint32 Before=Binder->Model.Selection.Skills[Skill];Click(Up);TestEqual(TEXT("Mouse training arrow changes the actual skill choice"),Binder->Model.Selection.Skills[Skill],Before+1);}
    Click(Manager->FindElementByName(TEXT("CGSummaryButton")));TestEqual(TEXT("Mouse navigation reaches summary"),Binder->GetPage(),6);
    Binder->Model.Selection.Name=TEXT("New Hero");Click(Manager->FindElementByName(TEXT("NameTextBox")));
    FModifierKeysState Control(false,false,true,false,false,false,false,false,false);
    Binder->KeyDown(FKeyEvent(EKeys::A,Control,0,false,0,0));Binder->KeyChar(FCharacterEvent(8,FModifierKeysState(),0,false));TestEqual(TEXT("Control character cannot erase selected name"),Binder->Model.Selection.Name,FString(TEXT("New Hero")));
    Binder->KeyChar(FCharacterEvent('Z',FModifierKeysState(),0,false));TestEqual(TEXT("Typing replaces the selected name"),Binder->Model.Selection.Name,FString(TEXT("Z")));
    Binder->KeyChar(FCharacterEvent('9',FModifierKeysState(),0,false));TestEqual(TEXT("Name input ignores unsupported digits"),Binder->Model.Selection.Name,FString(TEXT("Z")));
    Binder->Model.Selection.Name=TEXT("Drag Name");Binder->NameCaret=Binder->NameAnchor=0;Refresh();
    const auto NameBox=Manager->FindElementByName(TEXT("NameTextBox"));const FVector2D NameOrigin(NameBox->GetScreenOrigin());
    Binder->MouseDown(NameOrigin+FVector2D(1,NameBox->Height*.5));Binder->MouseMove(NameOrigin+FVector2D(NameBox->Width-1,NameBox->Height*.5));Binder->MouseUp();
    TestEqual(TEXT("Dragging selects from name start"),Binder->NameAnchor,0);TestEqual(TEXT("Dragging selects through name end"),Binder->NameCaret,9);
    Binder->KeyChar(FCharacterEvent('A',FModifierKeysState(),0,false));TestEqual(TEXT("Typing replaces dragged name selection"),Binder->Model.Selection.Name,FString(TEXT("A")));
    Binder->Model.Selection.Name=TEXT("");Binder->Submit();TestEqual(TEXT("Missing name opens native dialog"),Binder->DialogAction,4);Binder->CloseDialog(false);
    Binder->Model.Selection.Name=TEXT("[ Creation Hero ]");Binder->Model.SelectProfession(0);Binder->Submit();TestEqual(TEXT("Unspent points require retail confirmation"),Binder->DialogAction,1);TestEqual(TEXT("Retail bracket and whitespace trimming"),Binder->Model.Selection.Name,FString(TEXT("Creation Hero")));Binder->CloseDialog(false);
    Binder->Model.SelectHeritage(6);Binder->SetPage(4);Refresh();TestFalse(TEXT("Gear Knights do not offer clothing"),bool(Manager->FindElementByName(TEXT("ClothesButton"))->bVisible));TestFalse(TEXT("Gear Knight nose picker hidden"),bool(Manager->FindElementByName(TEXT("NoseSpin"))->bVisible));
    Binder->Model.SelectHeritage(1);Refresh();TestTrue(TEXT("Human clothing restored after special heritage"),bool(Manager->FindElementByName(TEXT("ClothesButton"))->bVisible));
    auto Instructions=Manager->FindElementByName(TEXT("AppearanceTextBox"));const float ZoomBefore=Binder->Zoom;
    Binder->MouseWheel(FVector2D(Instructions->GetScreenOrigin())+FVector2D(5,5),-1);
    TestEqual(TEXT("Appearance instructions scroll without zooming the model"),Binder->Zoom,ZoomBefore);
    Binder->SetPage(1);Refresh();auto Help=Manager->FindElementByName(TEXT("CGHelpButton"));
    Binder->MouseMove(FVector2D(Help->GetScreenOrigin())+FVector2D(5,5));Binder->RefreshTooltip(.3f);
    TestTrue(TEXT("Creation tooltip uses the native DAT frame"),Binder->Tooltip.IsValid()&&Binder->Tooltip->bVisible&&Binder->Tooltip->ImageFileId==0x06004cc2);
    Binder->SetPage(6);Binder->Model.Selection.Name=TEXT("Reserved Name");
    Client->Session->OnCharacterCreated.Broadcast(3,FACECharacterInfo());
    TestEqual(TEXT("Server name rejection opens native error dialog"),Binder->DialogAction,4);
    TestEqual(TEXT("Server rejection retains the complete name draft"),Binder->Model.Selection.Name,FString(TEXT("Reserved Name")));Binder->CloseDialog(false);
    Binder->Model.SelectHeritage(12);Binder->SetPage(1);Binder->SetPage(2);TestEqual(TEXT("Olthoi skips attributes and skills"),Binder->GetPage(),4);Binder->SetPage(5);TestEqual(TEXT("Olthoi skips towns"),Binder->GetPage(),6);
    // Finish must submit the actual edited draft, not a detached/default data model.
    auto Session=Client->Session;auto Sockets=ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    auto Receiver=Sockets->CreateSocket(NAME_DGram,TEXT("Creation UI receiver"),false);
    if(TestNotNull(TEXT("UI creation loopback socket"),Receiver))
    {
        auto Address=Sockets->CreateInternetAddr();bool ValidIp=false;Address->SetIp(TEXT("127.0.0.1"),ValidIp);Address->SetPort(0);
        if(TestTrue(TEXT("UI loopback bind"),Receiver->Bind(*Address)))
        {
            Receiver->GetAddress(*Address);Receiver->SetNonBlocking(true);Session->State=EACESessionState::CharacterSelect;
            Session->SocketC2S=Sockets->CreateSocket(NAME_DGram,TEXT("Creation UI sender"),false);Session->ServerC2SAddr=Address;
            const uint8 Seed[]={3,1,4,1};Session->IssacClient=MakeUnique<FACEIsaac>(Seed);Session->AccountName=TEXT("fixture");
            Binder->Model.SelectHeritage(3);Binder->Model.SelectSex(2);Binder->Model.SelectProfession(4);Binder->SetPage(6);
            Binder->Model.Selection.Name=TEXT("new CREATION");Binder->Model.Selection.StartArea=2;
            Click(Manager->FindElementByName(TEXT("CGFinishButton")));
            TestTrue(TEXT("Finish starts one pending network request"),Session->IsCharacterCreationPending());
            FACEBinaryWriter Expected;Expected.WriteString16L(TEXT("fixture"));Binder->Model.Selection.Write(Expected);
            uint8 Buffer[2048];int Read=0;const double Deadline=FPlatformTime::Seconds()+1;
            while(!Receiver->Recv(Buffer,sizeof(Buffer),Read)&&FPlatformTime::Seconds()<Deadline)FPlatformProcess::Sleep(.001f);
            if(TestTrue(TEXT("Finish reaches the socket"),Read>40))
            {
                FACEBinaryReader Packet(Buffer,Read);Packet.Skip(36);TestEqual(TEXT("Finish sends F656"),Packet.ReadUInt32(),0xf656u);
                TestTrue(TEXT("Every UI selection reaches the request intact"),Packet.ReadBytes(Packet.Remaining())==Expected.GetData());
            }
            FACEBinaryWriter Rejected;Rejected.WriteUInt32(3);FACEBinaryReader Reject(Rejected.GetData());Session->HandleCharacterCreated(Reject);
            TestEqual(TEXT("Wire rejection opens creation dialog"),Binder->DialogAction,4);Binder->CloseDialog(false);
            TestEqual(TEXT("Rejected Finish preserves appearance"),Binder->Model.Selection.Sex,2u);
            Binder->Model.Selection.Name=TEXT("Retry Hero");Click(Manager->FindElementByName(TEXT("CGFinishButton")));
            FACEBinaryWriter Accepted;Accepted.WriteUInt32(1);Accepted.WriteUInt32(0x50000001);Accepted.WriteString16L(TEXT("Retry Hero"));Accepted.WriteUInt32(0);
            FACEBinaryReader Success(Accepted.GetData());Session->HandleCharacterCreated(Success);
            TestTrue(TEXT("Accepted character returns UI to character selection"),Binder->bReturn);
            if(TestTrue(TEXT("Successful Finish adds an account entry"),Session->Characters.Num()==1))TestEqual(TEXT("Server identity added to account roster"),Session->Characters.Last().CharacterId,int32(0x50000001));
        }
        Receiver->Close();Sockets->DestroySocket(Receiver);
    }
    Binder->Shutdown();Canvas->SetCharGenBinder(nullptr);GI->Shutdown();GEngine->DestroyWorldContext(World);World->DestroyWorld(false);return true;
}
#endif
