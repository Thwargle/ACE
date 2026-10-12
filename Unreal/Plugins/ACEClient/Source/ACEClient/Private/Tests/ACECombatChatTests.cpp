#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ACESession.h"
#include "ACERetailChat.h"
#include "ACECombatStance.h"
#include "Protocol/ACECombatChat.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACECombatChatTest, "ACE.RetailParity.CombatChat",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACECombatChatTest::RunTest(const FString&)
{
    // Independent snapshot of all 34 slots in ChatInterface::BuildChatColorLookupTable.
    const FColor Palette[] = {
        {127,255,126}, {255,255,255}, {255,255,62}, {210,210,99},
        {255,126,255}, {255,62,62}, {62,190,255}, {255,149,149},
        {210,210,199}, {62,220,220}, {180,220,239}, {237,146,30},
        {244,117,113}, {255,0,0}};
    const int32 Slot[] = {0,0,1,2,3,4,5,6,7,7,2,3,8,9,10,5,0,6,11,2,0,5,12,0,0,0,13,10,10,10,10,2,10,11};
    for (int32 Type=0;Type<UE_ARRAY_COUNT(Slot);++Type)
        TestTrue(*FString::Printf(TEXT("Retail LogTextType %d color"),Type),ACERetailChat::Color(Type).ToFColorSRGB()==Palette[Slot[Type]]);
    TestTrue(TEXT("Unknown server extensions retain default green"),ACERetailChat::Color(99).ToFColorSRGB()==Palette[0]);

    FACESession S;S.State=EACESessionState::InWorld;S.PlayerGuid=100;
    FString Last;int32 Type=-1,FilterType=-1,Messages=0,Damage=0;
    S.OnChatMessage.AddLambda([&](const FString& Text,const FString&,int32 T){Last=Text;Type=T;++Messages;});
    S.OnRoutedChatMessage.AddLambda([&](const FString&,const FString&,int32 T,int32 Filter)
        {TestEqual(TEXT("Routing preserves the original observer type"),T,Type);FilterType=Filter;});
    S.OnCombatFeedback.AddLambda([&](const FString&,int32 Amount,bool,bool){Damage=Amount;});
    auto Event=[&](uint32 Id,const FACEBinaryWriter& Body)
    {
        FACEBinaryWriter W;W.WriteUInt32(ACEOpcode::GameEvent);W.WriteUInt32(100);W.WriteUInt32(1);W.WriteUInt32(Id);W.WriteBytes(Body.GetData());
        S.HandleGameMessage(W.GetData());
    };
    auto PublicDeath=[&](int32 Victim,int32 Killer,bool Truncated=false)
    {
        FACEBinaryWriter W;W.WriteUInt32(ACEOpcode::PlayerKilled);W.WriteString16L(TEXT("A Player died!"));W.WriteInt32(Victim);
        if (!Truncated) W.WriteInt32(Killer);
        S.HandleGameMessage(W.GetData());
    };
    FACEBinaryWriter Death;Death.WriteString16L(TEXT("Your attack killed the Drudge!"));
    for (uint32 Id:{ACEGameEvent::VictimNotification,ACEGameEvent::KillerNotification})
    {
        Event(Id,Death);TestEqual(TEXT("Personal death notice retains retail Default color/type"),Type,0);
        TestEqual(TEXT("Killing blows route as outgoing combat; own death remains Gameplay"),FilterType,
            Id==ACEGameEvent::KillerNotification?ACEChatMessageType::CombatSelf:ACEChatMessageType::Broadcast);
        TestEqual(TEXT("Server death wording is preserved"),Last,FString(TEXT("Your attack killed the Drudge!")));
    }
    PublicDeath(101,102);TestEqual(TEXT("Nearby deaths appear once"),Messages,3);
    TestEqual(TEXT("Nearby player death is also green"),Type,0);
    PublicDeath(100,102);PublicDeath(101,100);PublicDeath(101,102,true);
    TestEqual(TEXT("Victim/killer do not repeat broadcast; truncated packet is ignored"),Messages,3);

    // Retail's attacker/defender wording deliberately differs. High bits and
    // Overpower are not invented text; physical effects come from server flags.
    for (uint64 Flags=0;Flags<16;++Flags) for (bool Critical:{false,true})
    {
        FACEBinaryWriter Hit;Hit.WriteString16L(TEXT("Drudge"));Hit.WriteUInt32(4);Hit.WriteDouble(.2);Hit.WriteUInt32(1);
        Hit.WriteUInt32(Critical?1:0);Hit.WriteUInt64(Flags | (1ull<<48));Event(ACEGameEvent::AttackerNotification,Hit);
        FString Expected=Critical?TEXT("Critical hit!  "):TEXT("");
        if (Flags&4) Expected+=TEXT("Sneak Attack! ");if (Flags&2) Expected+=TEXT("Recklessness! ");
        Expected+=TEXT("You bash Drudge for 1 point of bludgeoning damage!");
        if (Flags&1) Expected+=TEXT(" Your target's Critical Protection augmentation allows them to avoid your critical hit!");
        TestEqual(TEXT("Attacker skills match retail prefix order, singular and protection"),Last,Expected);
        TestEqual(TEXT("Attacker remains Combat_Self"),Type,22);TestEqual(TEXT("Damage remains the server amount"),Damage,1);

        FACEBinaryWriter Incoming;Incoming.WriteString16L(TEXT("Drudge"));Incoming.WriteUInt32(4);Incoming.WriteDouble(.2);Incoming.WriteUInt32(42);
        Incoming.WriteUInt32(1);Incoming.WriteUInt32(Critical?1:0);Incoming.WriteUInt64(Flags);Event(ACEGameEvent::DefenderNotification,Incoming);
        Expected=Critical?TEXT("Critical hit! "):TEXT("");
        if (Flags&4) Expected+=TEXT("Sneak Attack! ");if (Flags&2) Expected+=TEXT("Reckless! ");
        Expected+=TEXT("Drudge bashes your chest for 42 points of bludgeoning damage!");
        if (Flags&1) Expected+=TEXT(" Your Critical Protection augmentation allows you to avoid a critical hit!");
        TestEqual(TEXT("Defender skills and Critical Protection use the victim's perspective"),Last,Expected);
        TestEqual(TEXT("Defender remains Combat_Enemy"),Type,21);
    }
    TestEqual(TEXT("Olthoi body parts are not clamped to foot"),ACECombatChat::DamageLocationName(20),FString(TEXT("claw")));
    TestEqual(TEXT("Electrical uses retail wording"),ACECombatChat::DamageTypeName(64),FString(TEXT("electrical")));
    TestEqual(TEXT("Combined damage labels retain retail order"),ACECombatChat::DamageTypeName(67),FString(TEXT("slashing/piercing/electrical")));
    TestEqual(TEXT("Unknown damage has no invented type label"),ACECombatChat::FormatAttackerNotification(TEXT("Drudge"),0,.2f,2,false,0),
        FString(TEXT("You hit Drudge for 2 points of damage!")));

    for (uint32 Channel:{ACETurbineChat::Allegiance,ACETurbineChat::General,ACETurbineChat::Trade,
        ACETurbineChat::LFG,ACETurbineChat::Roleplay,ACETurbineChat::Society,ACETurbineChat::Olthoi})
    {
        FACEBinaryWriter W;W.WriteUInt32(ACEOpcode::TurbineChat);W.WriteUInt32(0);W.WriteUInt32(ACETurbineChat::BlobEventBinary);
        for (int32 I=0;I<7;++I) W.WriteUInt32(0);
        W.WriteUInt32(Channel);W.WritePackedUnicode(TEXT("Friend"));W.WritePackedUnicode(TEXT("Hello"));
        W.WriteUInt32(12);W.WriteInt32(101);W.WriteUInt32(0);W.WriteUInt32(Channel);S.HandleGameMessage(W.GetData());
        const int32 Expected=Channel==ACETurbineChat::Allegiance||Channel==ACETurbineChat::Olthoi?18:
            Channel==ACETurbineChat::Society?32:int32(Channel)+25;
        TestEqual(TEXT("ChatRoomTracker global channel has the retail color/filter category"),Type,Expected);
    }
    // Dirty Fighting is not a physical-hit condition. Its text/effects arrive
    // separately; preserve the spell name, channel and all four effect scripts.
    int32 Scripts=0;S.OnPlayEffect.AddLambda([&](int32 Guid,int32 Id,float Intensity)
        {TestEqual(TEXT("DF effect targets the server object"),Guid,101);TestTrue(TEXT("DF script retained"),Id>=0xAA&&Id<=0xAD);TestEqual(TEXT("DF intensity retained"),Intensity,1.f);++Scripts;});
    for (const TCHAR* Name:{TEXT("Unbalancing Blow"),TEXT("Bleeding Assault"),TEXT("Traumatic Assault"),TEXT("Blinding Blow")})
    {
        const FString Message=FString::Printf(TEXT("Dirty Fighting! Player delivers a %s to Drudge!"),Name);
        FACEBinaryWriter W;W.WriteUInt32(ACEOpcode::ServerMessage);W.WriteString16L(Message);W.WriteUInt32(6);S.HandleGameMessage(W.GetData());
        TestEqual(TEXT("DF text passes through unchanged"),Last,Message);TestEqual(TEXT("DF uses Combat red"),Type,6);
    }
    for (uint32 Id=0xAA;Id<=0xAD;++Id)
    {FACEBinaryWriter W;W.WriteUInt32(ACEOpcode::PlayEffect);W.WriteInt32(101);W.WriteUInt32(Id);W.WriteFloat(1);S.HandleGameMessage(W.GetData());}
    TestEqual(TEXT("All Dirty Fighting effect scripts delivered"),Scripts,4);

    auto* Sockets=ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    FSocket* Receiver=Sockets->CreateSocket(NAME_DGram,TEXT("Combat parity receiver"),false);
    ON_SCOPE_EXIT {S.Disconnect();if(Receiver){Receiver->Close();Sockets->DestroySocket(Receiver);}};
    auto Address=Sockets->CreateInternetAddr();bool Valid=false;Address->SetIp(TEXT("127.0.0.1"),Valid);Address->SetPort(0);
    if (!TestTrue(TEXT("Combat receiver binds"),Receiver&&Receiver->Bind(*Address))) return false;
    Receiver->GetAddress(*Address);S.SocketC2S=Sockets->CreateSocket(NAME_DGram,TEXT("Combat parity sender"),false);
    S.ServerC2SAddr=Address;S.IssacClient=MakeUnique<FACEIsaac>(123u);
    for (bool Missile:{false,true}) for (uint32 Height:{1u,2u,3u}) for (float Power:{.1f,.5f,.9f,1.f})
    {
        if (Missile) S.SendTargetedMissileAttack(101,Height,Power);else S.SendTargetedMeleeAttack(101,Height,Power);
        uint32 LastPacket=0;for(const auto& P:S.CachedC2SPackets) LastPacket=FMath::Max(LastPacket,P.Key);
        if (!TestTrue(TEXT("Attack packet serialized"),S.CachedC2SPackets.Contains(LastPacket))) return false;
        FACEBinaryReader R(S.CachedC2SPackets[LastPacket].Payload);R.Skip(16);
        TestEqual(TEXT("Retail game action envelope"),R.ReadUInt32(),ACEOpcode::GameAction);R.ReadUInt32();
        TestEqual(TEXT("Weapon mode selects correct action"),R.ReadUInt32(),Missile?ACEGameAction::TargetedMissileAttack:ACEGameAction::TargetedMeleeAttack);
        TestEqual(TEXT("Target reaches server"),R.ReadUInt32(),101u);TestEqual(TEXT("Dirty Fighting height reaches server"),R.ReadUInt32(),Height);
        TestEqual(TEXT("Recklessness power reaches server unmodified"),R.ReadFloat(),Power);
    }
    FACEWorldObject Main;Main.ItemType=ACEItemType::MeleeWeapon;Main.CurrentWieldedLocation=ACEEquipMask::MeleeWeapon;
    FACEWorldObject Left=Main;Left.CurrentWieldedLocation=ACEEquipMask::Shield;
    TestEqual(TEXT("Offhand weapon selects retail dual wield stance"),ACECombatStance::ResolveForCombatMode({Main,Left},2),ACEMotion::StanceDualWield);
    Left.ItemType=ACEItemType::Armor;Left.CombatUse=4; // CombatUse.Shield
    TestNotEqual(TEXT("A shield is not a dual wield weapon"),ACECombatStance::ResolveForCombatMode({Main,Left},2),ACEMotion::StanceDualWield);
    return true;
}
#endif
