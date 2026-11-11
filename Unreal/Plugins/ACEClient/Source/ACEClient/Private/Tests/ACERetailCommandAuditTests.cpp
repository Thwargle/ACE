#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Misc/ConfigCacheIni.h"
#include "ACEClientSubsystem.h"
#include "ACESession.h"
#include "ACERuntimeOptions.h"
#include "ACETerrainPresenterComponent.h"
#include "ACERetailCommandSyntax.h"
#include "ACERetailCommandHelp.h"
#include "UI/ACEUIGameplayBinder.h"
#include "UI/ACEUICanvasWidget.h"
#include "UI/ACEChatEntry.h"
#include "Blueprint/WidgetTree.h"
#include "Components/ScrollBox.h"
#include "Engine/GameInstance.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "Common/UdpSocketBuilder.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACERetailCommandAuditTest,"ACE.RetailParity.ChatCommandRegistry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACERetailCommandAuditTest::RunTest(const FString&)
{
    const FString OriginalConfig=GGameUserSettingsIni;
    GGameUserSettingsIni=FPaths::ProjectSavedDir()/TEXT("Automation/ChatCommandRegistry.ini");
    FConfigFile EmptyConfig;GConfig->SetFile(GGameUserSettingsIni,&EmptyConfig);
    ON_SCOPE_EXIT { GGameUserSettingsIni=OriginalConfig;ACERuntimeOptions::Apply(); };
    auto* Sockets=ISocketSubsystem::Get();
    auto* Receiver=FUdpSocketBuilder(TEXT("Command audit receiver")).AsNonBlocking().BoundToPort(0).Build();
    ON_SCOPE_EXIT { if (Receiver) {Receiver->Close();Sockets->DestroySocket(Receiver);} };
    if (!TestNotNull(TEXT("Loopback receiver"),Receiver)) return false;
    auto* GI=NewObject<UGameInstance>();auto* Client=NewObject<UACEClientSubsystem>(GI);
    Client->Session=MakeShared<FACESession>();auto& S=*Client->Session;
    S.SocketC2S=FUdpSocketBuilder(TEXT("Command audit sender")).AsNonBlocking().BoundToPort(0).Build();
    if (!TestNotNull(TEXT("Loopback sender"),S.SocketC2S)) return false;
    S.ServerC2SAddr=Sockets->CreateInternetAddr();S.ServerC2SAddr->SetIp(0x7f000001);S.ServerC2SAddr->SetPort(Receiver->GetPortNo());
    S.IssacClient=MakeUnique<FACEIsaac>(123u);S.State=EACESessionState::InWorld;S.PlayerGuid=0x50000001;
    ON_SCOPE_EXIT {S.State=EACESessionState::Disconnected;Client->Session.Reset();};
    auto* B=NewObject<UACEUIGameplayBinder>();B->Client=Client;
    B->Canvas=NewObject<UACEUICanvasWidget>();B->Canvas->WidgetTree=NewObject<UWidgetTree>(B->Canvas);
    B->ChatLog=NewObject<UScrollBox>(B->Canvas);
    B->ChatEntry=NewObject<UACEChatEntry>();
    // Independent registry fixture from retail ClientCommunicationSystem's two
    // registries. Help-only groups are tested as help, never claimed as commands.
    const FString Registry=TEXT("? help commands allegiances allegiance all ab alh ah motd speaker channels a co-vassals covassals covassal c fellowship fellows fellow f group g party monarch m patron p vassals vassal v join leave chatting chat notell reply r rp mr pr retell rt say s tell t send whisper w afk death consent corpse cor die lifestone lif ls marketplace mar mp permit pkarena pka pklarena pla e em emote me emotes fillcomps loadfile friends friends_add friends_remove house hou hslist hor hr hom hoa squelch unsquelch messagetypes message_types msgtypes msg_types status age birth day endurance framerate loc pklite pkl render version saveui loadui saveautoui loadautoui lockui text clear filter unfilter log title index clist on off guild gu general cg trade ct lfg clfg roleplay crp society soc olthoi o");
    TArray<FString> Names;Registry.ParseIntoArrayWS(Names);
    for (const FString& Name:Names)
    {
        TestNotNull(*FString::Printf(TEXT("Retail help covers %s"),*Name),ACERetailCommandHelp::Find(Name));
        S.CachedC2SPackets.Reset();B->ChatDisplayLines[0].Reset();
        B->TryDispatchChatCommand(TEXT("/help ")+Name);
        TestTrue(*FString::Printf(TEXT("%s help is local"),*Name),S.CachedC2SPackets.IsEmpty());
        TestFalse(*FString::Printf(TEXT("%s help displays text"),*Name),B->ChatDisplayLines[0].IsEmpty());
        if (Name==TEXT("commands") || Name==TEXT("allegiances") || Name==TEXT("channels") || Name==TEXT("chatting")
            || Name==TEXT("death") || Name==TEXT("status") || Name==TEXT("text")) continue;
        // Empty arguments must be handled or rejected locally, never forwarded as
        // unknown server chat. Named-command success paths are exercised below.
        S.CachedC2SPackets.Reset();B->TryDispatchChatCommand(TEXT("/")+Name);
        for (const auto& Pair:S.CachedC2SPackets)
        {
            FACEBinaryReader R(Pair.Value.Payload);R.Skip(16);
            if (R.ReadUInt32()==0xF7B1) {R.ReadUInt32();TestNotEqual(*Name,R.ReadUInt32(),0x15u);}
        }
        B->PendingChatConfirmation=0;B->ChatConfirmationStage=0;
    }
    auto Dispatch=[&](const FString& Command)
    {
        S.CachedC2SPackets.Reset();B->TryDispatchChatCommand(Command);
    };
    auto Action=[&](uint32 Expected)
    {
        TArray<uint8> Bytes;
        if (!TestEqual(TEXT("Exactly one reliable request"),S.CachedC2SPackets.Num(),1)) return Bytes;
        FACEBinaryReader R(S.CachedC2SPackets.CreateConstIterator().Value().Payload);R.Skip(14);
        TestEqual(TEXT("Retail weenie queue"),R.ReadUInt16(),uint16(3));
        TestEqual(TEXT("Game action envelope"),R.ReadUInt32(),0xF7B1u);R.ReadUInt32();
        TestEqual(TEXT("Independent retail action opcode"),R.ReadUInt32(),Expected);
        while (R.Remaining()) Bytes.Add(R.ReadUInt8());return Bytes;
    };
    auto Number=[&](const FString& Command,uint32 Opcode,uint32 Expected)
    {
        Dispatch(Command);auto Bytes=Action(Opcode);FACEBinaryReader R(Bytes);
        TestEqual(*Command,R.ReadUInt32(),Expected);TestEqual(TEXT("One uint32 argument"),R.Remaining(),0);
    };
    auto Named=[&](const FString& Command,uint32 Opcode,const FString& Expected)
    {
        Dispatch(Command);auto Bytes=Action(Opcode);FACEBinaryReader R(Bytes);
        TestEqual(*Command,R.ReadString16L(),Expected);TestEqual(TEXT("One string argument"),R.Remaining(),0);
    };
    for (const FString Prefix:{TEXT("/"),TEXT("@")})
    {
        for (auto Pair:{TPair<FString,uint32>(TEXT("consent who"),0x217),{TEXT("consent clear"),0x216},
            {TEXT("house guest show"),0x24d},{TEXT("house storage show"),0x24d},{TEXT("house storage list"),0x24d},
            {TEXT("house storage add -all"),0x25c},{TEXT("house storage remove -all"),0x24c},
            {TEXT("house boot -all"),0x25f},{TEXT("house remove -all"),0x25f},
            {TEXT("house boot_all"),0x25f},{TEXT("house remove_all"),0x25f},{TEXT("all officer list"),0x2a6}})
        { Dispatch(Prefix+Pair.Key);TestEqual(TEXT("Empty retail action payload"),Action(Pair.Value).Num(),0); }
        Named(Prefix+TEXT("permit add \"Two Words\""),0x219,TEXT("Two Words"));
        Named(Prefix+TEXT("PERMIT\tremove\tTwo Words"),0x21a,TEXT("Two Words"));
        Named(Prefix+TEXT("consent remove \"Two Words\""),0x218,TEXT("Two Words"));
        Named(Prefix+TEXT("house guest add \"Two Words\""),0x245,TEXT("Two Words"));
        Named(Prefix+TEXT("house\tremove\t\"Two Words\""),0x24a,TEXT("Two Words"));
        Named(Prefix+TEXT("friends_add \"Two Words\""),0x18,TEXT("Two Words"));
        uint32 Type=0;for (const FString Name:{TEXT("cottage"),TEXT("villa"),TEXT("mansion"),TEXT("apartment")})
        {++Type;Number(Prefix+TEXT("house available ")+Name,0x270,Type);Number(Prefix+TEXT("hslist ")+Name,0x270,Type);}
        S.bAfkMode=false;Number(Prefix+TEXT("afk on"),0xf,1);
        Dispatch(Prefix+TEXT("afk on"));TestTrue(TEXT("AFK on is idempotent"),S.CachedC2SPackets.IsEmpty());
        Number(Prefix+TEXT("afk off"),0xf,0);
        Dispatch(Prefix+TEXT("afk off"));TestTrue(TEXT("AFK off is idempotent"),S.CachedC2SPackets.IsEmpty());
        Number(Prefix+TEXT("afk"),0xf,1);
        Named(Prefix+TEXT("afk msg testing"),0x10,TEXT("testing\n"));
        Named(Prefix+TEXT("afk msg"),0x10,TEXT(""));
        Named(Prefix+TEXT("afk msg ")+FString::ChrN(250,'x'),0x10,FString::ChrN(191,'x')+TEXT("\n"));
        for (const FString Bad:{TEXT("consent remove"),TEXT("permit add"),TEXT("permit wrong Some Player"),TEXT("house"),
            TEXT("house recall extra"),TEXT("house ma extra"),TEXT("hslist"),TEXT("hslist nonsense"),TEXT("house available"),
            TEXT("house available apartnonsense"),TEXT("afk nonsense"),TEXT("die extra"),TEXT("ls extra"),TEXT("hr extra"),TEXT("mp extra"),TEXT("all ho extra")})
        {Dispatch(Prefix+Bad);TestTrue(*FString::Printf(TEXT("Invalid/incomplete command stays local: %s"),*Bad),S.CachedC2SPackets.IsEmpty());}
        Dispatch(Prefix+TEXT("die"));TestTrue(TEXT("Death awaits confirmation"),S.CachedC2SPackets.IsEmpty());
        B->FinishServerConfirmation(false);TestTrue(TEXT("Cancel death sends nothing"),S.CachedC2SPackets.IsEmpty());
        Dispatch(Prefix+TEXT("die"));B->FinishServerConfirmation(true);Action(0x279);
        Dispatch(Prefix+TEXT("house abandon"));TestTrue(TEXT("Abandon awaits first confirmation"),S.CachedC2SPackets.IsEmpty());
        B->FinishServerConfirmation(true);TestTrue(TEXT("Abandon awaits second confirmation"),S.CachedC2SPackets.IsEmpty());
        B->FinishServerConfirmation(false);TestTrue(TEXT("Cancel second confirmation sends nothing"),S.CachedC2SPackets.IsEmpty());
        Dispatch(Prefix+TEXT("house abandon"));B->FinishServerConfirmation(true);B->FinishServerConfirmation(true);Action(0x21f);
    }
    Dispatch(TEXT("/consent on"));
    {auto Bytes=Action(5);FACEBinaryReader R(Bytes);TestEqual(TEXT("AcceptLootPermits option"),R.ReadUInt32(),16u);TestEqual(TEXT("Enable permit acceptance"),R.ReadUInt32(),1u);}
    Dispatch(TEXT("/consent off"));
    {auto Bytes=Action(5);FACEBinaryReader R(Bytes);TestEqual(TEXT("AcceptLootPermits option"),R.ReadUInt32(),16u);TestEqual(TEXT("Disable permit acceptance"),R.ReadUInt32(),0u);}
    Dispatch(TEXT("/friends old"));
    if (TestEqual(TEXT("Legacy friends request"),S.CachedC2SPackets.Num(),1))
    {FACEBinaryReader R(S.CachedC2SPackets.CreateConstIterator().Value().Payload);R.Skip(14);TestEqual(TEXT("Control queue"),R.ReadUInt16(),uint16(2));
     TestEqual(TEXT("Legacy opcode"),R.ReadUInt32(),0xf7cdu);TestEqual(TEXT("List operation"),R.ReadUInt32(),0u);TestEqual(TEXT("No name"),R.ReadString16L(),FString());}
    // Both the initial quality table and later ordered position properties must
    // feed /corpse. They must never move the live player or overwrite another key.
    auto WritePosition=[](FACEBinaryWriter& W,uint32 Cell)
    {W.WriteUInt32(Cell);W.WriteFloat(3);W.WriteFloat(4);W.WriteFloat(5);W.WriteFloat(1);W.WriteFloat(0);W.WriteFloat(0);W.WriteFloat(0);};
    FACEBinaryWriter Login;Login.WriteUInt32(0x20);Login.WriteUInt32(1);
    Login.WriteUInt16(1);Login.WriteUInt16(16);Login.WriteUInt32(14);WritePosition(Login,0x80800001);
    Login.WriteUInt32(0);Login.WriteUInt32(0);FACEBinaryReader LR(Login.GetData());S.HandlePlayerDescription(LR);
    if (TestNotNull(TEXT("Login retains last outside corpse"),S.GetPlayerPositionQuality(14)))
        TestEqual(TEXT("Login corpse cell"),uint32(S.GetPlayerPositionQuality(14)->CellId),0x80800001u);
    auto PositionUpdate=[&](bool Public,uint8 Stamp,uint32 Key,uint32 Cell,int32 Owner)
    {FACEBinaryWriter W;W.WriteUInt32(Public?0x2dc:0x2db);W.WriteUInt8(Stamp);if(Public) W.WriteInt32(Owner);
     W.WriteUInt32(Key);WritePosition(W,Cell);return W.GetData();};
    const FACEPosition Before=S.PlayerPosition;
    S.ReceiveGameMessage(PositionUpdate(false,10,14,0x80800009,S.PlayerGuid));
    S.ReceiveGameMessage(PositionUpdate(false,9,14,0x80800001,S.PlayerGuid));
    S.ReceiveGameMessage(PositionUpdate(false,11,1,0x90900001,S.PlayerGuid));
    S.ReceiveGameMessage(PositionUpdate(true,12,14,0x90900001,S.PlayerGuid+1));
    TestEqual(TEXT("Only newest owned corpse property wins"),uint32(S.GetPlayerPositionQuality(14)->CellId),0x80800009u);
    auto Full=PositionUpdate(false,11,14,0x80800011,S.PlayerGuid);
    for(int32 Size=4;Size<Full.Num();++Size){auto Short=Full;Short.SetNum(Size);S.ReceiveGameMessage(Short);}
    TestEqual(TEXT("Truncation cannot replace corpse"),uint32(S.GetPlayerPositionQuality(14)->CellId),0x80800009u);
    S.ReceiveGameMessage(Full);
    TestEqual(TEXT("Truncation did not consume stamp"),uint32(S.GetPlayerPositionQuality(14)->CellId),0x80800011u);
    TestEqual(TEXT("Position quality never moves live player"),S.PlayerPosition.CellId,Before.CellId);
    B->ChatDisplayLines[0].Reset();Dispatch(TEXT("/cor"));
    TestTrue(TEXT("Corpse command displays coordinates"),!B->ChatDisplayLines[0].IsEmpty() && B->ChatDisplayLines[0].Last().Text.Contains(TEXT("0.5N, 0.7E")));
    Dispatch(TEXT("/render fov 110"));TestEqual(TEXT("FOV preference updates"),ACERuntimeOptions::Get(TEXT("FieldOfViewDegrees")),110.f);
    Dispatch(TEXT("/render radius 8"));TestEqual(TEXT("Landscape preference updates"),ACERuntimeOptions::Get(TEXT("LandscapeRadius")),8.f);
    for(const FString Bad:{TEXT("/render fov 161"),TEXT("/render fov NaN"),TEXT("/render fov 90bad"),TEXT("/render radius 4"),TEXT("/render radius 26")}) Dispatch(Bad);
    TestEqual(TEXT("Invalid FOV preserves preference"),ACERuntimeOptions::Get(TEXT("FieldOfViewDegrees")),110.f);
    TestEqual(TEXT("Invalid radius preserves preference"),ACERuntimeOptions::Get(TEXT("LandscapeRadius")),8.f);
    auto* Terrain=NewObject<UACETerrainPresenterComponent>();
    TestEqual(TEXT("New terrain presenters load persisted radius"),Terrain->LoadRadius,PLATFORM_ANDROID?3:8);
    TestEqual(TEXT("Persisted radius keeps unload hysteresis"),Terrain->UnloadRadius,PLATFORM_ANDROID?3:9);
    Terrain->ApplyLandscapeDrawRadius(25);
    TestEqual(TEXT("Runtime radius updates terrain residency"),Terrain->LoadRadius,PLATFORM_ANDROID?3:25);
    TestEqual(TEXT("Runtime radius keeps unload hysteresis"),Terrain->UnloadRadius,PLATFORM_ANDROID?3:26);
    Terrain->LoadRadius=0;Terrain->ApplyLandscapeDrawRadius(8);
    TestEqual(TEXT("A chat graphics option cannot enable login terrain"),Terrain->LoadRadius,0);
    Dispatch(TEXT("/acehelp"));auto ServerHelp=Action(0x15);FACEBinaryReader HR(ServerHelp);
    TestEqual(TEXT("Server-defined commands still forward"),HR.ReadString16L(),FString(TEXT("@acehelp")));
    S.ClearWorldState();TestNull(TEXT("Another character cannot inherit corpse position"),S.GetPlayerPositionQuality(14));
    return !HasAnyErrors();
}
#endif
