#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ACESession.h"
#include "Protocol/ACEHash32.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACERetailNetworkTest, "ACE.RetailParity.NetworkTransport",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACERetailNetworkTest::RunTest(const FString& Parameters)
{
    FACESession ChatSession;
    {
        FACESession Actions;Actions.PlayerGuid=1;
        FACEWorldObject Remote;Remote.Guid=2;Remote.bHasPhysicsTimestamps=true;
        Remote.PhysicsTimestamps[ACEPhysicsTimeStamp::Instance]=3;Actions.WorldObjects.Add(2,Remote);
        FACEObjectMotionState Seen;int32 Received=0;
        Actions.OnMotionUpdate.AddLambda([&](int32,const FACEObjectMotionState& M){Seen=M;++Received;});
        auto Packet=[&](uint16 Movement,uint16 First,uint16 Second,int32 Truncate=0)
        {
            FACEBinaryWriter W;W.WriteUInt32(2);W.WriteUInt16(3);W.WriteUInt16(Movement);
            W.WriteUInt16(0);W.WriteUInt8(0);W.Align();
            W.WriteUInt8(0);W.WriteUInt8(0);W.WriteUInt16(0x3D);
            W.WriteUInt32(2u<<7); // Two MotionItems, with default Ready locomotion.
            W.WriteUInt16(ACEMotion::DeadCommandU16);W.WriteUInt16(First);W.WriteFloat(.75f);
            W.WriteUInt16(ACEMotion::DeadCommandU16);W.WriteUInt16(Second);W.WriteFloat(1.5f);W.Align();
            FACEBinaryReader R(W.GetData().GetData(),W.GetData().Num()-Truncate);Actions.HandleUpdateMotion(R);
        };
        // Use a corpse role to avoid the living-creature death guard: this test
        // isolates command sequencing rather than death-state transitions.
        Actions.WorldObjects[2].ItemType=ACEItemType::Container;
        Actions.WorldObjects[2].ObjectDescriptionFlags=ACEObjectDescFlag::Corpse;
        Packet(1,0x7FFE,0x7FFF);
        TestEqual(TEXT("First wire action keeps its own speed"),Seen.ActionSpeed,.75f);
        TestEqual(TEXT("Follow-up wire action keeps its independent speed"),Seen.FollowupSpeed(0),1.5f);
        TestEqual(TEXT("Both first-time actions are delivered"),Seen.ActionFollowups.Num(),1);
        Packet(2,0x7FFE,0x7FFF);
        TestEqual(TEXT("A newer movement packet does not replay old actions"),Seen.ActionCommand,0);
        TestTrue(TEXT("Repeated actions leave the follow-up queue empty"),Seen.ActionFollowups.IsEmpty());
        Packet(3,0x7FFF,0);
        TestEqual(TEXT("Action stamp rollover keeps the new action"),Seen.ActionSpeed,1.5f);
        TestTrue(TEXT("Rollover does not replay the preceding action"),Seen.ActionFollowups.IsEmpty());
        const int32 Before=Received;
        Packet(4,1,2,1);
        TestEqual(TEXT("Truncated motion cannot consume an action stamp"),Actions.WorldObjects[2].ReceivedActionStamp,uint16(0));
        TestEqual(TEXT("Truncated action list is never presented"),Received,Before);
        Packet(4,1,2);
        TestEqual(TEXT("Valid retransmission delivers both actions"),Seen.ActionFollowups.Num(),1);
        Remote=Actions.WorldObjects[2];Remote.bHasReceivedActionStamp=false;Remote.ReceivedActionStamp=0;
        Actions.UpsertWorldObject(Remote);
        TestEqual(TEXT("Same-incarnation description retains action history"),Actions.WorldObjects[2].ReceivedActionStamp,uint16(2));
        Packet(5,1,2);
        TestEqual(TEXT("Description refresh cannot replay finished actions"),Seen.ActionCommand,0);
        Actions.PlayerGuid=2;
        Packet(6,0x8003,4);
        TestEqual(TEXT("Owner skips autonomous actions within a server-controlled update"),Seen.ActionSpeed,1.5f);
        TestTrue(TEXT("Owner does not queue the autonomous action again"),Seen.ActionFollowups.IsEmpty());
    }
    FACESession DeathSession;
    DeathSession.PlayerGuid=1;
    DeathSession.State=EACESessionState::InWorld;
    int32 DeathUpdates=0, DeletedCreatures=0;
    DeathSession.OnMotionUpdate.AddLambda([&](int32,const FACEObjectMotionState& M)
        { if (M.IsDeathMotion()) ++DeathUpdates; });
    DeathSession.OnObjectDeleted.AddLambda([&](int32){++DeletedCreatures;});
    auto SendMotion=[&](int32 Guid,uint16 Sequence,uint16 Command)
    {
        FACEBinaryWriter W; W.WriteUInt32(Guid); W.WriteUInt16(3); W.WriteUInt16(Sequence);
        W.WriteUInt16(0); W.WriteUInt8(0); W.Align();
        W.WriteUInt8(0); W.WriteUInt8(0); W.WriteUInt16(0x3D); // NonCombat
        W.WriteUInt32(2); W.WriteUInt16(Command); W.Align();
        FACEBinaryReader R(W.GetData()); DeathSession.HandleUpdateMotion(R);
    };
    for (int32 I=0;I<32;++I)
    {
        FACEWorldObject Mob; Mob.Guid=0x50030000+I; Mob.ItemType=ACEItemType::Creature;
        Mob.ObjectDescriptionFlags=ACEObjectDescFlag::Attackable;
        Mob.bHasPhysicsTimestamps=true; Mob.PhysicsTimestamps[ACEPhysicsTimeStamp::Instance]=3;
        DeathSession.WorldObjects.Add(Mob.Guid,Mob);
        DeathSession.SelectObject(Mob.Guid);
        TestEqual(TEXT("Living smite fixture can be selected"),DeathSession.SelectedObject.Guid,Mob.Guid);
        SendMotion(Mob.Guid,1,ACEMotion::DeadCommandU16);
        TestTrue(TEXT("Smite's forward death command records the creature as dying"),DeathSession.WorldObjects[Mob.Guid].bDying);
        TestFalse(TEXT("Dying creature cannot be attacked"),DeathSession.WorldObjects[Mob.Guid].IsAttackable());
        TestEqual(TEXT("Smite releases the selected enemy"),DeathSession.SelectedObject.Guid,0);
        SendMotion(Mob.Guid,2,3);
        TestTrue(TEXT("Late Ready cannot resurrect a slain NPC"),DeathSession.WorldObjects[Mob.Guid].bDying);
        DeathSession.UpsertWorldObject(Mob);
        TestTrue(TEXT("Repeated description retains same-incarnation death"),DeathSession.WorldObjects[Mob.Guid].bDying);
        FACEBinaryWriter Delete; Delete.WriteUInt32(Mob.Guid);
        FACEBinaryReader R(Delete.GetData()); DeathSession.HandleObjectDelete(R);
        TestFalse(TEXT("Removal clears every slain creature record"),DeathSession.WorldObjects.Contains(Mob.Guid));
        ++Mob.PhysicsTimestamps[ACEPhysicsTimeStamp::Instance]; DeathSession.UpsertWorldObject(Mob);
        TestTrue(TEXT("A new incarnation can be attacked again"),DeathSession.WorldObjects[Mob.Guid].IsAttackable());
        DeathSession.WorldObjects.Remove(Mob.Guid);
    }
    TestEqual(TEXT("A mass smite retains every death update"),DeathUpdates,32);
    TestEqual(TEXT("A mass smite removes every dead object"),DeletedCreatures,32);
    // Corpses legitimately use the Dead pose while remaining usable containers.
    FACEWorldObject Corpse; Corpse.Guid=500; Corpse.ItemType=ACEItemType::Container;
    Corpse.ObjectDescriptionFlags=ACEObjectDescFlag::Corpse;
    Corpse.bHasPhysicsTimestamps=true; Corpse.PhysicsTimestamps[ACEPhysicsTimeStamp::Instance]=3;
    DeathSession.WorldObjects.Add(Corpse.Guid,Corpse); DeathSession.SelectObject(Corpse.Guid);
    SendMotion(Corpse.Guid,1,ACEMotion::DeadCommandU16);
    TestFalse(TEXT("Corpse pose does not mark its container as a dying creature"),DeathSession.WorldObjects[Corpse.Guid].bDying);
    TestEqual(TEXT("Corpse death pose preserves its selection markers"),DeathSession.SelectedObject.Guid,Corpse.Guid);
    // Server-authored Creature weenies may intentionally pose as quest remains.
    // These are the public properties of the supplied exports, not a WCID exception.
    for (int32 Type : {ACEItemType::Misc, ACEItemType::Creature})
    {
        FACEWorldObject Reward = Corpse; Reward.Guid = 600 + Type;
        Reward.ItemType = Type; Reward.bHasPosition = true; Reward.ItemUseable = 32;
        Reward.ObjectDescriptionFlags = ACEObjectDescFlag::Stuck;
        DeathSession.UpsertWorldObject(Reward); DeathSession.SelectObject(Reward.Guid);
        for (uint16 Sequence=1; Sequence<=3; ++Sequence)
        {
            SendMotion(Reward.Guid,Sequence,ACEMotion::DeadCommandU16);
            const auto& Posed = DeathSession.WorldObjects[Reward.Guid];
            TestFalse(TEXT("Reward NPC Dead emote is not an authoritative death"),Posed.bDying);
            TestTrue(TEXT("Repeated heartbeat Dead leaves reward selectable"),Posed.IsSelectableWorldObject());
            TestFalse(TEXT("Reward remains is not an attack target"),Posed.IsAttackable());
            TestEqual(TEXT("Dead emote preserves reward selection"),DeathSession.SelectedObject.Guid,Reward.Guid);
            DeathSession.UpsertWorldObject(Posed);
        }
        DeathSession.bUseBusy=false; const auto Before=DeathSession.NextGameActionSequence;
        DeathSession.SendUseItem(Reward.Guid);
        TestEqual(TEXT("Reward use still sends the ordinary server Use action"),DeathSession.NextGameActionSequence,Before+1);
        Reward.ItemUseable=33;
        TestFalse(TEXT("Explicit No-use bit is not exempt from death handling"),Reward.AllowsDeadPoseInteraction());
    }
    // Corrections from an earlier incarnation/teleport must never displace a
    // remote player, even if their independent position sequence is higher.
    FACESession PositionSession;
    FACEWorldObject Remote; Remote.Guid=123; Remote.bHasPhysicsTimestamps=true;
    Remote.PhysicsTimestamps[ACEPhysicsTimeStamp::Instance]=3;
    Remote.PhysicsTimestamps[ACEPhysicsTimeStamp::Teleport]=2;
    Remote.PhysicsTimestamps[ACEPhysicsTimeStamp::Position]=10;
    PositionSession.WorldObjects.Add(Remote.Guid,Remote);
    int32 Corrections=0;
    FACEPosition LastPosition;
    PositionSession.OnPositionUpdate.AddLambda([&](int32,const FACEPosition& P){++Corrections;LastPosition=P;});
    auto Position=[&](uint16 Instance,uint16 Sequence,uint16 Teleport,float X,uint32 Flags=0x74)
    {
        FACEBinaryWriter W; W.WriteUInt32(123); W.WriteUInt32(Flags); W.WriteUInt32(0x2B120021);
        W.WriteFloat(X); W.WriteFloat(90); W.WriteFloat(20); W.WriteFloat(1);
        W.WriteUInt16(Instance); W.WriteUInt16(Sequence); W.WriteUInt16(Teleport); W.WriteUInt16(0);
        FACEBinaryReader R(W.GetData()); PositionSession.HandleUpdatePosition(R);
    };
    Position(3,11,2,45); Position(2,99,2,100); Position(3,100,1,101); Position(3,11,3,102);
    TestEqual(TEXT("Only the matching, current remote correction is accepted"),Corrections,1);
    Position(3,12,2,46);
    TestEqual(TEXT("Rejected stale corrections cannot poison position sequencing"),Corrections,2);
    TestEqual(TEXT("Cached remote world position matches the accepted wire coordinate"),PositionSession.WorldObjects[123].Position.Location.X,46.0);
    PositionSession.WorldObjects[123].PhysicsTimestamps[ACEPhysicsTimeStamp::Position]=65535;
    Position(3,0,2,47);
    TestEqual(TEXT("Remote sequence wraparound accepts the next position"),Corrections,3);
    TestTrue(TEXT("Position event retains explicit grounded contact for presentation"),LastPosition.bHasContactState && LastPosition.bIsGrounded);
    TestTrue(TEXT("Position event forwards teleport epoch without changing wire format"),LastPosition.bHasTeleportSequence && LastPosition.TeleportSequence==2);
    Position(3,1,3,48,0x70);
    TestTrue(TEXT("Airborne and descriptor positions remain distinguishable"),LastPosition.bHasContactState && !LastPosition.bIsGrounded);
    TestEqual(TEXT("A genuine new teleport epoch reaches presentation"),LastPosition.TeleportSequence,uint16(3));
    {
        FACEBinaryWriter Full; Full.WriteUInt32(123); Full.WriteUInt32(0x74); Full.WriteUInt32(0x2B120021);
        Full.WriteFloat(52); Full.WriteFloat(90); Full.WriteFloat(20); Full.WriteFloat(1);
        for (uint16 V : {uint16(3),uint16(2),uint16(3),uint16(0)}) Full.WriteUInt16(V);
        const int32 Before=Corrections;
        for (int32 Bytes=0; Bytes<Full.GetData().Num(); ++Bytes)
        { FACEBinaryReader Short(Full.GetData().GetData(),Bytes); PositionSession.HandleUpdatePosition(Short); }
        TestEqual(TEXT("Every truncated position packet is ignored"),Corrections,Before);
        FACEBinaryReader Complete(Full.GetData()); PositionSession.HandleUpdatePosition(Complete);
        TestEqual(TEXT("Valid position after truncation is still accepted"),Corrections,Before+1);
    }
    {
        FACEBinaryWriter Motion; Motion.WriteUInt32(123); Motion.WriteUInt16(3); Motion.WriteUInt16(9);
        Motion.WriteUInt16(0); Motion.WriteUInt8(0); Motion.Align();
        Motion.WriteUInt8(0); Motion.WriteUInt8(1); Motion.WriteUInt16(0x3D);
        Motion.WriteUInt32(6); Motion.WriteUInt16(7); Motion.WriteFloat(1.5f); Motion.Align(); Motion.WriteUInt32(456);
        int32 Updates=0; PositionSession.OnMotionUpdate.AddLambda([&](int32,const FACEObjectMotionState&){++Updates;});
        for (int32 Bytes=0; Bytes<Motion.GetData().Num(); ++Bytes)
        { FACEBinaryReader Short(Motion.GetData().GetData(),Bytes); PositionSession.HandleUpdateMotion(Short); }
        TestEqual(TEXT("Truncated motion never reaches presentation"),Updates,0);
        TestEqual(TEXT("Truncated motion cannot advance sequence"),PositionSession.WorldObjects[123].PhysicsTimestamps[ACEPhysicsTimeStamp::Movement],uint16(0));
        FACEBinaryReader Complete(Motion.GetData()); PositionSession.HandleUpdateMotion(Complete);
        TestEqual(TEXT("Valid retransmission with the same sequence is accepted"),Updates,1);
    }
    FString LastChat; int32 ChatType = -1;
    ChatSession.OnChatMessage.AddLambda([&](const FString& Message, const FString&, int32 Type) { LastChat = Message; ChatType = Type; });
    FACEBinaryWriter Closed; Closed.WriteUInt32(0x0451);
    FACEBinaryReader ClosedReader(Closed.GetData()); ChatSession.HandleWeenieError(ClosedReader);
    TestEqual(TEXT("Trade close has readable text"), LastChat, FString(TEXT("Trade closed.")));
    FACEBinaryWriter Missing; Missing.WriteUInt32(0x042C);
    FACEBinaryReader MissingReader(Missing.GetData()); ChatSession.HandleWeenieError(MissingReader);
    TestEqual(TEXT("Missing combat target has readable text"),LastChat,FString(TEXT("Target not acquired.")));
    // Full wire event: healing at full health must substitute the target name,
    // then UseDone(success) must release the interaction without a second message.
    int32 FailureMessages = 0;
    ChatSession.OnChatMessage.AddLambda([&](const FString&, const FString&, int32) { ++FailureMessages; });
    auto FailureEvent = [&](uint32 Event, uint32 Code, const TCHAR* Argument)
    {
        FACEBinaryWriter W; W.WriteUInt32(0); W.WriteUInt32(1); W.WriteUInt32(Event); W.WriteUInt32(Code);
        if (Argument) W.WriteString16L(Argument);
        FACEBinaryReader R(W.GetData()); ChatSession.HandleGameEvent(R);
    };
    FailureEvent(ACEGameEvent::WeenieErrorWithString, 0x04FF, TEXT("PlayerName"));
    TestEqual(TEXT("Full-health packet has the retail healing message"), LastChat, FString(TEXT("PlayerName is already at full health!")));
    TestEqual(TEXT("Healing failure uses error chat"), ChatType, ACEChatMessageType::ChatError);
    FailureEvent(ACEGameEvent::UseDone, 0, nullptr);
    TestEqual(TEXT("Successful completion does not add another error"), FailureMessages, 1);
    FailureEvent(ACEGameEvent::UseDone, 0x0500, nullptr);
    TestEqual(TEXT("UseDone also uses the shared failure catalog"), LastChat, FString(TEXT("You aren't ready to heal!")));
    TestEqual(TEXT("UseDone failure has the retail chat destination"), ChatType, ACEChatMessageType::ChatError);
    const int32 BeforeSilent = FailureMessages;
    for (uint32 Code : {0u, 0x003Bu, 0x003Cu, 0x0436u, 0x0511u}) FailureEvent(ACEGameEvent::WeenieError, Code, nullptr);
    TestEqual(TEXT("Portal and explicitly silent statuses produce no errors"), FailureMessages, BeforeSilent);
    FACEBinaryWriter Truncated; Truncated.WriteUInt32(0x04FF); Truncated.WriteUInt16(20); Truncated.WriteUInt8('P');
    FACEBinaryReader TruncatedReader(Truncated.GetData()); ChatSession.HandleWeenieErrorWithString(TruncatedReader);
    FACEBinaryWriter MissingName; MissingName.WriteUInt32(0x04FF);
    FACEBinaryReader MissingNameReader(MissingName.GetData()); ChatSession.HandleWeenieErrorWithString(MissingNameReader);
    TestEqual(TEXT("Truncated string payloads cannot display partial names"), FailureMessages, BeforeSilent);
    FACEBinaryWriter Banner; Banner.WriteString16L(TEXT("Still a banner"));
    FACEBinaryReader BannerReader(Banner.GetData()); ChatSession.HandleTransientString(BannerReader);
    TestEqual(TEXT("Explicit transient messages still use the banner"), ChatType, ACEChatMessageType::TransientInfo);
    for (const auto Channel : {TPair<uint32,int32>(ACETurbineChat::General,ACEChatMessageType::General),
        {ACETurbineChat::Trade,ACEChatMessageType::Trade},{ACETurbineChat::LFG,ACEChatMessageType::LFG},
        {ACETurbineChat::Roleplay,ACEChatMessageType::Roleplay},{ACETurbineChat::Society,ACEChatMessageType::Society}})
    {
        FACEBinaryWriter W; W.WriteUInt32(0);W.WriteUInt32(ACETurbineChat::BlobEventBinary);
        for(int32 I=0;I<7;++I) W.WriteUInt32(0);
        W.WriteUInt32(Channel.Key);W.WritePackedUnicode(TEXT("Remote Friend"));W.WritePackedUnicode(TEXT("hello"));
        W.WriteUInt32(12);W.WriteUInt32(7);W.WriteUInt32(0);W.WriteUInt32(Channel.Key);
        FACEBinaryReader R(W.GetData());ChatSession.HandleTurbineChat(R);
        TestEqual(TEXT("Incoming global chat retains its retail filter type"),ChatType,Channel.Value);
        TestTrue(TEXT("Global channel reply formats the speaker once"),LastChat.EndsWith(TEXT("Remote Friend says, \"hello\"")));
    }
    FACESession TradeSession;
    auto TradeEvent = [&](uint32 Event, TArray<uint32> Values)
    {
        FACEBinaryWriter W; W.WriteUInt32(0); W.WriteUInt32(1); W.WriteUInt32(Event);
        for (uint32 V : Values) W.WriteUInt32(V);
        FACEBinaryReader R(W.GetData()); TradeSession.HandleGameEvent(R);
    };
    TradeEvent(ACEGameEvent::RegisterTrade, {7, 7, 0, 0});
    TradeEvent(ACEGameEvent::AddToTrade, {123, 1, 0});
    TradeEvent(ACEGameEvent::AcceptTrade, {7});
    TradeEvent(ACEGameEvent::DeclineTrade, {7});
    TestEqual(TEXT("Decline keeps trade partner"), TradeSession.GetTradePartnerGuid(), 7);
    TestEqual(TEXT("Decline keeps offered items"), TradeSession.GetTradeSelfItems().Num(), 1);
    TestEqual(TEXT("Decline clears acceptance"), TradeSession.GetTradeAcceptedGuid(), 0);
    TradeEvent(ACEGameEvent::AcceptTrade, {7});
    TradeEvent(ACEGameEvent::ClearTradeAcceptance, {});
    TestEqual(TEXT("Server reset clears acceptance"), TradeSession.GetTradeAcceptedGuid(), 0);
    TestTrue(TEXT("Server reset clears offered items"), TradeSession.GetTradeSelfItems().IsEmpty());
    TradeEvent(ACEGameEvent::AddToTrade, {123, 1, 0});
    TradeEvent(ACEGameEvent::RegisterTrade, {8, 8, 0, 0});
    TestTrue(TEXT("A new trade cannot inherit previous offers"), TradeSession.GetTradeSelfItems().IsEmpty());
    FACEBinaryWriter Leave; Leave.WriteUInt32(0x051C); Leave.WriteString16L(TEXT("LFG"));
    FACEBinaryReader LeaveReader(Leave.GetData()); ChatSession.HandleWeenieErrorWithString(LeaveReader);
    TestEqual(TEXT("Leaving LFG resolves retail notification"), LastChat, FString(TEXT("You have left the LFG channel.")));
    TestEqual(TEXT("Channel departure is normal chat, not an error banner"), ChatType, ACEChatMessageType::System);
    auto Tell=[&](uint32 Id,const TCHAR* Name,int32 Type)
    {
        FACEBinaryWriter W; W.WriteString16L(TEXT("hello")); W.WriteString16L(Name);
        W.WriteUInt32(Id);W.WriteUInt32(0x50000001);W.WriteUInt32(Type);W.WriteUInt32(0);
        FACEBinaryReader R(W.GetData());ChatSession.HandleTell(R);
    };
    Tell(0x50000002,TEXT("Remote Friend"),ACEChatMessageType::Tell);
    Tell(0x71000002,TEXT("Lorca Sammel"),ACEChatMessageType::Emote);
    TestEqual(TEXT("NPC speech cannot redirect reply"),ChatSession.GetLastTellSenderName(),FString(TEXT("Remote Friend")));
    auto Channel=[&](uint32 Id,const TCHAR* Name)
    {
        FACEBinaryWriter W;W.WriteUInt32(Id);W.WriteString16L(Name);W.WriteString16L(TEXT("hello"));
        FACEBinaryReader R(W.GetData());ChatSession.HandleChannelBroadcast(R);
    };
    Channel(ACEChatChannel::Fellow,TEXT("Remote Friend"));
    TestEqual(TEXT("Fellowship keeps its network channel identity"),ChatType,ACEChatMessageType::Fellowship);
    TestEqual(TEXT("Fellowship gets retail prefix"),LastChat,FString(TEXT("[Fellowship] Remote Friend says, \"hello\"")));
    Channel(ACEChatChannel::Patron,TEXT("Remote Friend"));
    TestEqual(TEXT("Patron channel describes the sender's relationship"),LastChat,FString(TEXT("Your vassal Remote Friend says to you, \"hello\"")));
    Channel(ACEChatChannel::CoVassals,TEXT(""));
    TestEqual(TEXT("Server echo has exactly one sender prefix"),LastChat,FString(TEXT("[Co-Vassals] You say, \"hello\"")));
    FACEBinaryWriter Index;Index.WriteUInt32(1);Index.WriteUInt32(1);Index.WriteUInt32(ACEGameEvent::ChannelIndex);
    Index.WriteUInt32(2);Index.WriteString16L(TEXT("Help"));Index.WriteString16L(TEXT("Abuse"));
    FACEBinaryReader IndexReader(Index.GetData());ChatSession.HandleGameEvent(IndexReader);
    TestEqual(TEXT("Channel index reply reaches chat"),LastChat,FString(TEXT("Help, Abuse")));
    // Server CreatureProfile writes two ushort highlight masks after attributes.
    // Value/burden presence must survive decoding so unknown isn't shown as zero.
    FACESession AppraisalSession;
    FACEAppraisalInfo Appraisal;
    AppraisalSession.OnAppraisal.AddLambda([&](const FACEAppraisalInfo& Info){ Appraisal = Info; });
    FACEBinaryWriter Details;
    Details.WriteUInt32(120); Details.WriteUInt32(0x200F); Details.WriteUInt32(1);
    Details.WriteUInt16(2); Details.WriteUInt16(8);
    Details.WriteUInt32(35); Details.WriteUInt32(91); Details.WriteUInt32(125); Details.WriteUInt32(1481800);
    Details.WriteUInt16(1); Details.WriteUInt16(8); Details.WriteUInt32(1); Details.WriteUInt64(191226310247ull);
    Details.WriteUInt16(1); Details.WriteUInt16(8); Details.WriteUInt32(5); Details.WriteUInt32(1);
    Details.WriteUInt16(1); Details.WriteUInt16(8); Details.WriteUInt32(29); Details.WriteDouble(1.25);
    Details.WriteUInt16(1); Details.WriteUInt16(8); Details.WriteUInt32(43); Details.WriteString16L(TEXT("29 March 2019"));
    FACEBinaryReader DetailsReader(Details.GetData()); AppraisalSession.HandleIdentifyObjectResponse(DetailsReader);
    TestEqual(TEXT("Follower data survives appraisal decoding"), Appraisal.IntProperties.FindRef(35), 91);
    TestEqual(TEXT("64-bit appraisal data does not truncate"), Appraisal.Int64Properties.FindRef(1), 191226310247ull);
    TestTrue(TEXT("Appraisal boolean retained"), Appraisal.BoolProperties.FindRef(5));
    TestEqual(TEXT("Appraisal float retained"), Appraisal.FloatProperties.FindRef(29), 1.25);
    TestEqual(TEXT("Birth date retains its property identity"), Appraisal.StringProperties.FindRef(43), FString(TEXT("29 March 2019")));
    FACEBinaryWriter Weapon;
    Weapon.WriteUInt32(124); Weapon.WriteUInt32(0xE21); Weapon.WriteUInt32(1);
    Weapon.WriteUInt16(1); Weapon.WriteUInt16(8); Weapon.WriteUInt32(1); Weapon.WriteUInt32(ACEItemType::MeleeWeapon);
    for (uint32 V : {3u,30u,44u,40u}) Weapon.WriteUInt32(V);
    for (double V : {.3,1.,.8,20.,1.15}) Weapon.WriteDouble(V);
    Weapon.WriteUInt32(1);
    Weapon.WriteUInt32(0x00010001);Weapon.WriteUInt32(0x00080008);Weapon.WriteUInt32(0x10001000);
    FACEBinaryReader WeaponReader(Weapon.GetData()); AppraisalSession.HandleIdentifyObjectResponse(WeaponReader);
    TestTrue(TEXT("Weapon appraisal marks the decoded profile and estimated range"), Appraisal.bHasWeaponProfile && Appraisal.bWeaponMaxVelocityEstimated);
    TestEqual(TEXT("Weapon item type survives appraisal decoding"), Appraisal.ItemType, int32(ACEItemType::MeleeWeapon));
    TestEqual(TEXT("Enchanted maximum damage is preserved"), Appraisal.Damage, 40);
    TestEqual(TEXT("Armor highlight mask retained in wire order"),Appraisal.ArmorEnchantments,0x00010001);
    TestEqual(TEXT("Weapon highlight mask retained in wire order"),Appraisal.WeaponEnchantments,0x00080008);
    TestEqual(TEXT("Resistance highlight mask retained in wire order"),Appraisal.ResistanceEnchantments,0x10001000);
    TestTrue(TEXT("Damage variance and offense survive the double wire fields"), FMath::IsNearlyEqual(Appraisal.DamageVariance,.3f) && FMath::IsNearlyEqual(Appraisal.WeaponOffense,1.15f));
    FACEBinaryWriter Profile;
    Profile.WriteUInt32(123); Profile.WriteUInt32(0x101); Profile.WriteUInt32(1);
    Profile.WriteUInt16(3); Profile.WriteUInt16(8);
    Profile.WriteUInt32(19); Profile.WriteUInt32(150);
    Profile.WriteUInt32(5); Profile.WriteUInt32(200);
    Profile.WriteUInt32(2); Profile.WriteUInt32(30);
    Profile.WriteUInt32(9); Profile.WriteUInt32(150); Profile.WriteUInt32(200);
    for (uint32 V : {200u,150u,75u,140u,100u,120u,90u,80u,110u,100u}) Profile.WriteUInt32(V);
    Profile.WriteUInt16(9); Profile.WriteUInt16(1);
    FACEBinaryReader ProfileReader(Profile.GetData());
    AppraisalSession.HandleIdentifyObjectResponse(ProfileReader);
    TestTrue(TEXT("Appraisal preserves value/burden presence"), Appraisal.bHasValue && Appraisal.bHasBurden);
    TestEqual(TEXT("Appraisal decodes the creature display type"), Appraisal.CreatureType, 30);
    TestEqual(TEXT("Appraisal preserves separate quickness/coordination"), Appraisal.Coordination, 140);
    TestEqual(TEXT("Attribute highlight mask is decoded"), Appraisal.AttributeHighlights, 9);
    TestEqual(TEXT("Attribute buff/debuff color mask is decoded"), Appraisal.AttributeColors, 1);
    TestFalse(TEXT("Non-weapon appraisal cannot reuse the preceding weapon stats"), Appraisal.bHasWeaponProfile);
    TestEqual(TEXT("A subsequent unbuffed item clears highlight masks"),Appraisal.WeaponEnchantments|Appraisal.ArmorEnchantments|Appraisal.ResistanceEnchantments,0);
    // Wire fixtures exercise the actual datagram parser, checksum, reorder buffer,
    // message assembler and dispatch. No live server or account is involved.
    auto Fragment=[](uint32 Seq, uint16 Count, uint16 Index, const TArray<uint8>& Bytes, uint32 Id=0x80000000, uint16 Queue=ACEQueue::UIQueue)
    {
        FACEBinaryWriter W; W.WriteUInt32(Seq); W.WriteUInt32(Id);
        W.WriteUInt16(Count); W.WriteUInt16(16+Bytes.Num()); W.WriteUInt16(Index);
        W.WriteUInt16(Queue); W.WriteBytes(Bytes); return W.GetData();
    };
    auto Packet=[](uint32 Seq, EACEPacketHeaderFlags Flags, const TArray<uint8>& Body)
    {
        const uint32 Hash=FACESession::HeaderHash32(Seq,Flags,0,0,Body.Num(),1)+FACEHash32::Calculate(Body);
        FACEBinaryWriter W; W.WriteUInt32(Seq); W.WriteUInt32(static_cast<uint32>(Flags)); W.WriteUInt32(Hash);
        W.WriteUInt16(0); W.WriteUInt16(0); W.WriteUInt16(Body.Num()); W.WriteUInt16(1); W.WriteBytes(Body);
        return W.GetData();
    };
    auto NameMessage=[](const TCHAR* Name)
    {
        FACEBinaryWriter W; W.WriteUInt32(ACEOpcode::ServerName); W.WriteUInt32(0); W.WriteUInt32(0);
        W.WriteString16L(Name); return W.GetData();
    };
    const auto Message=NameMessage(TEXT("Assembled last"));
    TArray<uint8> Head, Tail; Head.Append(Message.GetData(),12); Tail.Append(Message.GetData()+12,Message.Num()-12);
    const auto P2=Packet(2,EACEPacketHeaderFlags::BlobFragments,Fragment(1,2,0,Head));
    const auto P3=Packet(3,EACEPacketHeaderFlags::BlobFragments,Fragment(2,1,0,NameMessage(TEXT("Between fragments"))));
    const auto P4=Packet(4,EACEPacketHeaderFlags::BlobFragments,Fragment(1,2,1,Tail));
    auto Deliver=[](FACESession& S,const TArray<uint8>& Bytes){ S.HandleDatagram(Bytes.GetData(),Bytes.Num(),true); };
    FACESession Ordered;
    Deliver(Ordered,P4); Deliver(Ordered,P3);
    TestEqual(TEXT("Out-of-order packets do not mutate partial messages"),Ordered.PartialFragments.Num(),0);
    TestTrue(TEXT("Out-of-order packets cannot dispatch world messages"),Ordered.ServerName.IsEmpty());
    Deliver(Ordered,P2);
    TestEqual(TEXT("Message completes at its ordered final fragment, after intervening messages"),Ordered.ServerName,FString(TEXT("Assembled last")));
    TestEqual(TEXT("Contiguous packet cursor drains the reordered stream"),Ordered.LastReceivedPacketSequence,4u);
    TestEqual(TEXT("Completed partial message is released"),Ordered.PartialFragments.Num(),0);
    Deliver(Ordered,P2); Deliver(Ordered,P4);
    TestEqual(TEXT("Duplicates cannot recreate partial-message state"),Ordered.PartialFragments.Num(),0);

    FACESession Identities;
    Deliver(Identities,Packet(2,EACEPacketHeaderFlags::BlobFragments,Fragment(1,2,0,Head,0x00010000)));
    Deliver(Identities,Packet(3,EACEPacketHeaderFlags::BlobFragments,Fragment(1,2,1,Tail,0x00020000)));
    TestTrue(TEXT("Different upper message IDs cannot splice into one game message"),Identities.ServerName.IsEmpty());
    TestEqual(TEXT("Assemblies retain full 64-bit identity"),Identities.PartialFragments.Num(),2);
    Deliver(Identities,Packet(4,EACEPacketHeaderFlags::BlobFragments,Fragment(1,2,1,Tail,0x00010000)));
    TestEqual(TEXT("Matching upper ID completes the original message"),Identities.ServerName,FString(TEXT("Assembled last")));
    TestEqual(TEXT("Completing one assembly retains bytes for the other"),Identities.PartialMessageBytes,Tail.Num());
    Deliver(Identities,Packet(5,EACEPacketHeaderFlags::BlobFragments,Fragment(1,2,0,Head,0x00020000)));
    TestEqual(TEXT("Completed assemblies release all received bytes"),Identities.PartialMessageBytes,0);

    FACESession Versions;
    Deliver(Versions,Packet(2,EACEPacketHeaderFlags::BlobFragments,Fragment(7,2,0,Head,0x8000FFFF)));
    Deliver(Versions,Packet(3,EACEPacketHeaderFlags::BlobFragments,Fragment(7,2,1,Tail,0x80000000)));
    TestTrue(TEXT("Wrapped newer ordering stamp cannot complete an older message"),Versions.ServerName.IsEmpty());
    TestEqual(TEXT("Superseded partial message bytes are reclaimed"),Versions.PartialMessageBytes,Tail.Num());
    Deliver(Versions,Packet(4,EACEPacketHeaderFlags::BlobFragments,Fragment(7,2,0,Head,0x8000FFFF)));
    TestTrue(TEXT("Late old-stamp fragment remains rejected"),Versions.ServerName.IsEmpty());
    Deliver(Versions,Packet(5,EACEPacketHeaderFlags::BlobFragments,Fragment(7,2,0,Head,0x80000000)));
    TestEqual(TEXT("New ordering stamp assembles without mixing old bytes"),Versions.ServerName,FString(TEXT("Assembled last")));
    TestEqual(TEXT("Supersession and completion leave no buffered bytes"),Versions.PartialMessageBytes,0);
    Deliver(Versions,Packet(6,EACEPacketHeaderFlags::BlobFragments,Fragment(7,1,0,NameMessage(TEXT("Obsolete")),0x8000FFFF)));
    TestEqual(TEXT("Obsolete complete ephemeral message cannot roll back state"),Versions.ServerName,FString(TEXT("Assembled last")));
    Versions.ExpireEphemeralMessages(FPlatformTime::Seconds()+11);
    TestTrue(TEXT("Retail five-second ephemeral history expires"),Versions.EphemeralMessages.IsEmpty());

    FACESession Limited;
    FACESession::FReceivedFragment Unfinished;
    Unfinished.Count=2; Unfinished.Queue=ACEQueue::UIQueue; Unfinished.Data=Head;
    for(int32 I=0;I<FACESession::MaxPartialMessages;++I)
    {
        Unfinished.BlobId=uint64(I+1); Limited.ProcessReceivedFragment(Unfinished);
    }
    TestEqual(TEXT("Incomplete message count has a fixed storage budget"),Limited.PartialFragments.Num(),FACESession::MaxPartialMessages);
    Limited.ProcessReceivedFragment(Unfinished);
    TestFalse(TEXT("Duplicate fragment does not exhaust storage budget"),Limited.State==EACESessionState::Failed);
    Unfinished.BlobId=99999; Limited.ProcessReceivedFragment(Unfinished);
    TestTrue(TEXT("Exhausted assembly budget produces an explicit connection error"),Limited.State==EACESessionState::Failed && Limited.ConnectionError.Contains(TEXT("incomplete network messages")));
    TestTrue(TEXT("Exhausted assembly storage is released"),Limited.PartialFragments.IsEmpty() && Limited.PartialMessageBytes==0);
    Deliver(Versions,Packet(7,EACEPacketHeaderFlags::BlobFragments,Fragment(8,2,0,Head)));
    Versions.ClearWorldState();
    TestTrue(TEXT("World exit clears message ordering history and buffered bytes"),Versions.EphemeralMessages.IsEmpty() && Versions.PartialMessageBytes==0 && Versions.PartialFragments.IsEmpty());
    FACESession Queues;
    Deliver(Queues,Packet(2,EACEPacketHeaderFlags::BlobFragments,Fragment(10,1,0,NameMessage(TEXT("Invalid queue")),0x80000000,0)));
    Deliver(Queues,Packet(3,EACEPacketHeaderFlags::BlobFragments,Fragment(11,1,0,NameMessage(TEXT("Invalid queue")),0x80000000,12)));
    TestTrue(TEXT("Unregistered queues cannot dispatch gameplay"),Queues.ServerName.IsEmpty());
    Deliver(Queues,Packet(4,EACEPacketHeaderFlags::BlobFragments,Fragment(12,1,0,NameMessage(TEXT("Valid queue")))));
    TestEqual(TEXT("Unknown queues do not block later valid gameplay packets"),Queues.ServerName,FString(TEXT("Valid queue")));

    FACESession SingleGap;
    Deliver(SingleGap,P3);
    const double RetryTime=FPlatformTime::Seconds()+2;
    SingleGap.RequestMissingS2CPackets(RetryTime);
    TestEqual(TEXT("One missing packet triggers recovery without additional traffic"),SingleGap.LastRequestForRetransmitTime,RetryTime);
    SingleGap.RequestMissingS2CPackets(RetryTime+.5);
    TestEqual(TEXT("Recovery requests are rate limited"),SingleGap.LastRequestForRetransmitTime,RetryTime);
    SingleGap.RequestMissingS2CPackets(RetryTime+2);
    TestEqual(TEXT("A lost recovery request is retried even on an idle connection"),SingleGap.LastRequestForRetransmitTime,RetryTime+2);
    TestEqual(TEXT("Recovery never advances ACK past a hole"),SingleGap.LastReceivedPacketSequence,1u);
    Deliver(SingleGap,P2);
    TestEqual(TEXT("Retransmission releases queued world updates"),SingleGap.ServerName,FString(TEXT("Between fragments")));

    auto RejectPacket=[&](uint32 Seq,uint32 Missing)
    {
        FACEBinaryWriter W; W.WriteUInt32(1); W.WriteUInt32(Missing);
        return Packet(Seq,EACEPacketHeaderFlags::RejectRetransmit,W.GetData());
    };
    FACESession Unrecoverable;
    Deliver(Unrecoverable,P3);
    Unrecoverable.RequestMissingS2CPackets(RetryTime);
    auto Rejection=RejectPacket(4,2), BadRejection=Rejection; BadRejection[8]^=1;
    Deliver(Unrecoverable,BadRejection);
    TestEqual(TEXT("Bad CRC cannot authorize skipping a missing packet"),Unrecoverable.LastReceivedPacketSequence,1u);
    FACEBinaryWriter ShortReject; ShortReject.WriteUInt32(2); ShortReject.WriteUInt32(2);
    Deliver(Unrecoverable,Packet(4,EACEPacketHeaderFlags::RejectRetransmit,ShortReject.GetData()));
    TestEqual(TEXT("Truncated rejection cannot release world updates"),Unrecoverable.LastReceivedPacketSequence,1u);
    Deliver(Unrecoverable,Rejection);
    TestEqual(TEXT("Server rejection releases later gameplay instead of freezing the stream"),Unrecoverable.ServerName,FString(TEXT("Between fragments")));
    TestEqual(TEXT("Rejection itself participates in packet ordering"),Unrecoverable.LastReceivedPacketSequence,4u);
    TestTrue(TEXT("Rejected holes are acknowledged and recovery bookkeeping is cleared"),Unrecoverable.bNeedAck && Unrecoverable.RequestedS2CPackets.IsEmpty() && Unrecoverable.RejectedS2CPackets.IsEmpty());
    Deliver(Unrecoverable,P2);
    TestEqual(TEXT("Late missing packet cannot resurrect stale gameplay"),Unrecoverable.PartialFragments.Num(),0);

    FACESession Unsolicited;
    Deliver(Unsolicited,RejectPacket(4,2));
    TestEqual(TEXT("Unsolicited rejection cannot jump over an unrequested sequence"),Unsolicited.LastReceivedPacketSequence,1u);

    FACESession MultipleGaps;
    Deliver(MultipleGaps,P3);
    Deliver(MultipleGaps,Packet(5,EACEPacketHeaderFlags::BlobFragments,Fragment(3,1,0,NameMessage(TEXT("After both gaps")))));
    MultipleGaps.RequestMissingS2CPackets(RetryTime);
    Deliver(MultipleGaps,RejectPacket(6,4));
    TestEqual(TEXT("Rejecting a later hole does not skip an earlier missing packet"),MultipleGaps.LastReceivedPacketSequence,1u);
    Deliver(MultipleGaps,P4); // Actual data wins over the pending rejection.
    Deliver(MultipleGaps,P2);
    TestEqual(TEXT("Late retransmission is preserved while draining a rejected hole"),MultipleGaps.PartialFragments.Num(),0);
    TestEqual(TEXT("Mixed retransmission and rejection drain all later packets"),MultipleGaps.LastReceivedPacketSequence,6u);
    TestEqual(TEXT("Later gameplay remains current"),MultipleGaps.ServerName,FString(TEXT("After both gaps")));

    FACESession SkipLater;
    Deliver(SkipLater,P3);
    Deliver(SkipLater,Packet(5,EACEPacketHeaderFlags::None,{}));
    SkipLater.RequestMissingS2CPackets(RetryTime);
    Deliver(SkipLater,RejectPacket(6,4));
    Deliver(SkipLater,P2);
    TestEqual(TEXT("A confirmed later hole is skipped after its earlier hole arrives"),SkipLater.LastReceivedPacketSequence,6u);
    SkipLater.Disconnect();
    TestTrue(TEXT("Reconnect clears rejected and requested packet state"),SkipLater.RequestedS2CPackets.IsEmpty() && SkipLater.RejectedS2CPackets.IsEmpty());

    FACESession Damaged;
    auto Bad=P2; Bad.Last()^=0x40;
    Deliver(Damaged,Bad);
    TestEqual(TEXT("Bad CRC cannot poison a future valid reassembly"),Damaged.PartialFragments.Num(),0);
    TestEqual(TEXT("Bad CRC does not acknowledge missing data"),Damaged.LastReceivedPacketSequence,1u);
    Deliver(Damaged,P4); Deliver(Damaged,P3); Deliver(Damaged,P2);
    TestEqual(TEXT("Retransmitted valid bytes recover the complete message"),Damaged.ServerName,FString(TEXT("Assembled last")));
    FACESession Invalid;
    Deliver(Invalid,Packet(2,EACEPacketHeaderFlags::BlobFragments,Fragment(1,2,2,Head)));
    TestEqual(TEXT("Out-of-range fragment index cannot create an incomplete buffer"),Invalid.PartialFragments.Num(),0);
    TestEqual(TEXT("Invalid fragment does not advance packet cursor"),Invalid.LastReceivedPacketSequence,1u);
    FACEBinaryWriter Time; Time.WriteDouble(1000.0);
    auto BadTime=Packet(2,EACEPacketHeaderFlags::TimeSync,Time.GetData()); BadTime[8]^=1;
    Invalid.PortalYearTicksAtConnect=50.0; Deliver(Invalid,BadTime);
    TestEqual(TEXT("Unauthenticated TimeSync cannot change sky/weather time"),Invalid.PortalYearTicksAtConnect,50.0);
    FACEBinaryWriter Time3; Time3.WriteDouble(2000.0);
    Deliver(Invalid,Packet(3,EACEPacketHeaderFlags::TimeSync,Time3.GetData()));
    TestEqual(TEXT("Early TimeSync waits for the contiguous packet stream"),Invalid.PortalYearTicksAtConnect,50.0);
    Deliver(Invalid,Packet(2,EACEPacketHeaderFlags::TimeSync,Time.GetData()));
    TestEqual(TEXT("TimeSync drains in chronological packet order"),Invalid.PortalYearTicksAtConnect,2000.0);

    FACESession Login; Login.State=EACESessionState::CharacterSelect;
    FACECharacterInfo Character; Character.CharacterId=1234; Character.Name=TEXT("Fixture");
    Login.Characters.Add(Character); Login.AccountName=TEXT("Fixture account");
    TestFalse(TEXT("Cannot enter a character absent from the account list"),Login.EnterWorld(9999));
    Login.Characters[0].DeleteSeconds=3600;
    TestFalse(TEXT("Pending-deletion characters cannot enter the world"),Login.EnterWorld(1234));
    Login.Characters[0].DeleteSeconds=0;
    TestTrue(TEXT("Selecting a valid character requests world entry"),Login.EnterWorld(1234));
    TestEqual(TEXT("Only F7C8 is sent before server readiness"),Login.NextFragmentSequence,2u);
    TestFalse(TEXT("F657 has not been sent prematurely"),Login.bEnteredWorldSent);
    TestFalse(TEXT("Repeated enter clicks do not send duplicate requests"),Login.EnterWorld(1234));
    FACEBinaryWriter Ready; Ready.WriteUInt32(ACEOpcode::CharacterEnterWorldServerReady);
    Login.HandleGameMessage(Ready.GetData());
    TestTrue(TEXT("Server readiness releases F657"),Login.bEnteredWorldSent);
    TestEqual(TEXT("Exactly one enter-world message follows readiness"),Login.NextFragmentSequence,3u);
    Login.HandleGameMessage(Ready.GetData());
    TestEqual(TEXT("Duplicate readiness cannot enter a second time"),Login.NextFragmentSequence,3u);
    FACEBinaryWriter Rejected; Rejected.WriteUInt32(ACEOpcode::CharacterError); Rejected.WriteUInt32(1);
    Login.HandleGameMessage(Rejected.GetData());
    TestTrue(TEXT("Rejected character entry returns to character selection"),Login.State==EACESessionState::CharacterSelect);
    TestTrue(TEXT("User can retry character entry after rejection"),Login.EnterWorld(1234));

    // Loopback verifies SendGameMessage itself honors retail's 464-byte payload
    // budget, including the fragment metadata and per-packet checksum.
    auto* Sockets=ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    FSocket* Receiver=Sockets->CreateSocket(NAME_DGram,TEXT("Retail transport fixture"),false);
    if (!TestNotNull(TEXT("Loopback receiver"),Receiver)) return false;
    const auto Address=Sockets->CreateInternetAddr(); bool Valid=false;
    Address->SetIp(TEXT("127.0.0.1"),Valid); Address->SetPort(0);
    if (!Receiver->Bind(*Address)) { Sockets->DestroySocket(Receiver); AddError(TEXT("Loopback bind failed")); return false; }
    Receiver->GetAddress(*Address); Receiver->SetNonBlocking(true);
    FACESession Sender;
    Sender.SocketC2S=Sockets->CreateSocket(NAME_DGram,TEXT("Retail fragment sender"),false);
    Sender.ServerC2SAddr=Address;
    TArray<uint8> Large; Large.SetNumUninitialized(1100);
    for (int32 I=0;I<Large.Num();++I) Large[I]=static_cast<uint8>(I);
    Sender.SendGameMessage(0xF656,Large,ACEQueue::UIQueue,false);
    TArray<uint8> Reconstructed; int32 Packets=0;
    const double Deadline=FPlatformTime::Seconds()+1.0;
    while (Packets<3 && FPlatformTime::Seconds()<Deadline)
    {
        uint8 Buffer[2048]; int32 Read=0;
        if (!Receiver->Recv(Buffer,sizeof(Buffer),Read)) { FPlatformProcess::Sleep(.001f); continue; }
        FACEBinaryReader R(Buffer,Read);
        const uint32 Seq=R.ReadUInt32(); const auto Flags=static_cast<EACEPacketHeaderFlags>(R.ReadUInt32());
        const uint32 Hash=R.ReadUInt32(); const uint16 Id=R.ReadUInt16(); const uint16 Stamp=R.ReadUInt16();
        const uint16 Size=R.ReadUInt16(); const uint16 Iteration=R.ReadUInt16();
        TestTrue(TEXT("Every outbound packet fits the retail payload limit"),Size<=464 && Read==20+Size);
        TestEqual(TEXT("Every fragment packet has a valid checksum"),Hash,
            FACESession::HeaderHash32(Seq,Flags,Id,Stamp,Size,Iteration)+FACEHash32::Calculate(Buffer+20,Size));
        TestEqual(TEXT("Fragments share one message sequence"),R.ReadUInt32(),1u); R.ReadUInt32();
        TestEqual(TEXT("Large message declares three fragments"),R.ReadUInt16(),static_cast<uint16>(3));
        const uint16 FragmentSize=R.ReadUInt16();
        TestEqual(TEXT("Fragments retain index ordering"),R.ReadUInt16(),static_cast<uint16>(Packets));
        TestEqual(TEXT("Fragments retain UI queue"),R.ReadUInt16(),ACEQueue::UIQueue);
        Reconstructed.Append(R.ReadBytes(FragmentSize-16)); ++Packets;
    }
    // Uneven fragment lengths expose a retry checksum error that single-fragment
    // packets cannot. The incoming NAK is encrypted and deliberately out of order.
    auto ReceiveWire=[&]()
    {
        TArray<uint8> Bytes; const double Until=FPlatformTime::Seconds()+1.;
        while(FPlatformTime::Seconds()<Until)
        {
            uint8 Buffer[2048]; int32 Read=0;
            if(Receiver->Recv(Buffer,sizeof(Buffer),Read)) { Bytes.Append(Buffer,Read); break; }
            FPlatformProcess::Sleep(.001f);
        }
        TestFalse(TEXT("Expected UDP packet was received"),Bytes.IsEmpty());
        return Bytes;
    };
    Sender.IssacClient=MakeUnique<FACEIsaac>(456);
    Sender.IssacServer=MakeUnique<FACECryptoSystem>(789);
    const auto FragA=Fragment(10,1,0,TArray<uint8>{1,2,3});
    const auto FragB=Fragment(11,1,0,TArray<uint8>{4,5,6,7,8});
    const uint32 OutboundSequence=Sender.NextPacketSequence;
    Sender.SendRawPacket(EACEPacketHeaderFlags::None,{},TArray<TArray<uint8>>{FragA,FragB},false,true,Sender.ClientId);
    const auto OriginalWire=ReceiveWire();
    const uint32 NextBeforeRetry=Sender.NextPacketSequence;
    const uint32 SplitHash=FACEHash32::Calculate(FragA)+FACEHash32::Calculate(FragB);
    FACEBinaryWriter Nak; Nak.WriteUInt32(1); Nak.WriteUInt32(OutboundSequence);
    const auto NakFlags=EACEPacketHeaderFlags::RequestRetransmit|EACEPacketHeaderFlags::EncryptedChecksum;
    FACEIsaac ServerKeys(789);
    const uint32 NakHash=FACESession::HeaderHash32(99,NakFlags,0,0,Nak.GetData().Num(),1)+(FACEHash32::Calculate(Nak.GetData())^ServerKeys.Next());
    FACEBinaryWriter EncryptedNak; EncryptedNak.WriteUInt32(99); EncryptedNak.WriteUInt32(static_cast<uint32>(NakFlags)); EncryptedNak.WriteUInt32(NakHash);
    EncryptedNak.WriteUInt16(0); EncryptedNak.WriteUInt16(0); EncryptedNak.WriteUInt16(Nak.GetData().Num()); EncryptedNak.WriteUInt16(1); EncryptedNak.WriteBytes(Nak.GetData());
    Deliver(Sender,EncryptedNak.GetData());
    const auto RetryWire=ReceiveWire();
    if(OriginalWire.Num()>=20 && RetryWire.Num()>=20)
    {
        FACEBinaryReader R(RetryWire); const uint32 Seq=R.ReadUInt32(); const auto Flags=static_cast<EACEPacketHeaderFlags>(R.ReadUInt32());
        const uint32 Hash=R.ReadUInt32(); const uint16 Id=R.ReadUInt16(),Stamp=R.ReadUInt16(),Size=R.ReadUInt16(),Iteration=R.ReadUInt16();
        TestEqual(TEXT("Retry uses the original packet sequence"),Seq,OutboundSequence);
        TestTrue(TEXT("Retry retains encrypted checksum and adds retransmission flag"),EnumHasAllFlags(Flags,EACEPacketHeaderFlags::EncryptedChecksum|EACEPacketHeaderFlags::Retransmission));
        FACEIsaac ClientKeys(456);
        TestEqual(TEXT("Retry preserves split payload hash and original ISAAC key"),Hash,FACESession::HeaderHash32(Seq,Flags,Id,Stamp,Size,Iteration)+(SplitHash^ClientKeys.Next()));
        TestTrue(TEXT("Retry preserves every payload byte"),R.ReadBytes(Size)==TArray<uint8>(OriginalWire.GetData()+20,OriginalWire.Num()-20));
    }
    TestEqual(TEXT("Retry does not consume an outgoing packet sequence"),Sender.NextPacketSequence,NextBeforeRetry);
    TestEqual(TEXT("Standalone encrypted NAK does not block incoming gameplay ordering"),Sender.LastReceivedPacketSequence,1u);
    TestTrue(TEXT("Standalone encrypted NAK cannot enter the reorder queue"),Sender.OutOfOrderS2CPackets.IsEmpty());

    FACEBinaryWriter EchoBody; EchoBody.WriteFloat(42.f);
    const float PingToken=Sender.RecordEchoRequest(FPlatformTime::Seconds()-.25);
    EchoBody.WriteFloat(PingToken); EchoBody.WriteFloat(0.f);
    auto EchoPacket=Packet(3,EACEPacketHeaderFlags::EchoRequest|EACEPacketHeaderFlags::EchoResponse,EchoBody.GetData());
    auto BadEcho=EchoPacket; BadEcho[8]^=1;
    const uint32 BeforeEcho=Sender.NextPacketSequence;
    Deliver(Sender,BadEcho);
    TestEqual(TEXT("Invalid checksum cannot elicit an echo reply"),Sender.NextPacketSequence,BeforeEcho);
    TestTrue(TEXT("Invalid echo response leaves the real RTT request pending"),Sender.PendingEchoTimes.Contains(PingToken));
    Deliver(Sender,EchoPacket);
    const auto EchoWire=ReceiveWire();
    TestTrue(TEXT("RTT updates even while gameplay waits on packet loss"),Sender.LinkStatus.bHasPing && Sender.LinkStatus.RoundTripSeconds>=.2f);
    TestEqual(TEXT("Immediate echo does not acknowledge missing gameplay"),Sender.LastReceivedPacketSequence,1u);
    if(EchoWire.Num()>=28)
    {
        FACEBinaryReader EchoReader(EchoWire); EchoReader.Skip(4);
        TestTrue(TEXT("Keepalive produces an encrypted echo response"),EnumHasAllFlags(static_cast<EACEPacketHeaderFlags>(EchoReader.ReadUInt32()),EACEPacketHeaderFlags::EchoResponse|EACEPacketHeaderFlags::EncryptedChecksum));
        EchoReader.Skip(12);
        TestEqual(TEXT("Keepalive echoes the original timestamp"),EchoReader.ReadFloat(),42.f);
    }
    const uint32 AfterEcho=Sender.NextPacketSequence;
    Deliver(Sender,Packet(2,EACEPacketHeaderFlags::None,{}));
    TestEqual(TEXT("Draining reordered gameplay does not repeat an echo reply"),Sender.NextPacketSequence,AfterEcho);
    TestEqual(TEXT("Gameplay ordering catches up normally"),Sender.LastReceivedPacketSequence,3u);

    Sender.State=EACESessionState::InWorld; Sender.PlayerGuid=123;
    Sender.IssacClient=MakeUnique<FACEIsaac>(123);
    FACEWorldObject Self=Remote; Self.PhysicsTimestamps[ACEPhysicsTimeStamp::ServerControl]=5;
    Sender.UpsertWorldObject(Self);
    TestEqual(TEXT("Create seeds the outgoing server-control epoch"),Sender.ServerControlSeq,uint16(5));
    int32 SelfMotions=0; Sender.OnMotionUpdate.AddLambda([&](int32,const FACEObjectMotionState&){++SelfMotions;});
    auto SelfMotion=[&](uint16 Movement,uint16 Control,bool Autonomous)
    {
        FACEBinaryWriter W; W.WriteUInt32(123); W.WriteUInt16(3); W.WriteUInt16(Movement);
        W.WriteUInt16(Control); W.WriteUInt8(Autonomous); W.Align();
        W.WriteUInt8(0); W.WriteUInt8(0); W.WriteUInt16(0x3D); W.WriteUInt32(0);
        FACEBinaryReader R(W.GetData()); Sender.HandleUpdateMotion(R);
    };
    SelfMotion(1,5,false); SelfMotion(2,4,false); SelfMotion(1,6,false);
    TestEqual(TEXT("New motion requires current control and new movement sequence"),SelfMotions,1);
    SelfMotion(2,6,false);
    TestEqual(TEXT("Valid server control is presented"),SelfMotions,2);
    TestEqual(TEXT("Received server control is echoed in future client movement"),Sender.ServerControlSeq,uint16(6));
    SelfMotion(3,6,true);
    TestEqual(TEXT("Own autonomous echo cannot restart local movement"),SelfMotions,2);
    TestEqual(TEXT("Own autonomous echo still advances accepted sequence"),Sender.WorldObjects[123].PhysicsTimestamps[ACEPhysicsTimeStamp::Movement],uint16(3));
    Sender.PlayerPosition.CellId=0x7D640019; Sender.PlayerPosition.Location=FVector(40,40,12);
    Sender.SendAutonomousPosition(true);
    const auto* PositionPacket=Sender.CachedC2SPackets.Find(Sender.NextPacketSequence-1);
    if (!TestNotNull(TEXT("Autonomous position produces a cached wire packet"),PositionPacket))
    { Sockets->DestroySocket(Receiver); return false; }
    FACEBinaryReader Report(PositionPacket->Payload); Report.Skip(60);
    TestEqual(TEXT("AutonomousPosition carries the current incarnation"),Report.ReadUInt16(),uint16(3));
    TestEqual(TEXT("AutonomousPosition carries accepted server control"),Report.ReadUInt16(),uint16(6));
    for(bool Contact:{false,true})
    {
        Sender.SendMoveToState(1,0,0,true,Contact,false);
        Sender.SendStopMovement();
        const auto* Stop=Sender.CachedC2SPackets.Find(Sender.NextPacketSequence-1);
        if(!TestNotNull(TEXT("Movement cancellation produces a packet"),Stop))continue;
        FACEBinaryReader R(Stop->Payload);R.Skip(24);
        TestEqual(TEXT("Cancellation uses retail MoveToState action"),R.ReadUInt32(),ACEGameAction::MoveToState);
        TestEqual(TEXT("Cancellation clears the movement axes"),R.ReadUInt32(),uint32(ACERawMotionFlags::CurrentHoldKey|ACERawMotionFlags::CurrentStyle));
        R.Skip(8+32); // Hold/style and current position.
        TestEqual(TEXT("Stop retains the character incarnation"),R.ReadUInt16(),Sender.InstanceSeq);
        TestEqual(TEXT("Stop retains the server-control epoch"),R.ReadUInt16(),Sender.ServerControlSeq);
        TestEqual(TEXT("Stop retains the teleport epoch"),R.ReadUInt16(),Sender.TeleportSeq);
        TestEqual(TEXT("Stop retains the forced-position epoch"),R.ReadUInt16(),Sender.ForcePositionSeq);
        TestEqual(TEXT("Stopping input never invents ground contact"),R.ReadUInt8(),uint8(Contact));
        TestEqual(TEXT("Future position reports preserve the physical contact state"),Sender.bAutoPosContact,Contact);
    }
    // DreamWeave fast-tick/portal loading exposes stale stationary positions:
    // the wire must carry final physics changes even after movement input ends.
    Sender.bMoving=false; Sender.bForcePositionReporting=false;
    Sender.FlushAutonomousPosition(true);
    auto CountPositions=[&]()
    {
        int32 Count=0;
        for(const auto& Pair:Sender.CachedC2SPackets)
        {
            if(Pair.Value.Payload.Num()<28)continue;
            FACEBinaryReader R(Pair.Value.Payload);R.Skip(24);
            if(R.ReadUInt32()==ACEGameAction::AutonomousPosition)++Count;
        }
        return Count;
    };
    int32 Reports=CountPositions();
    Sender.TickPositionReporting(5.f);
    TestEqual(TEXT("Unchanged stationary players do not flood position packets"),CountPositions(),Reports);
    Sender.PlayerPosition.Location.Z+=.03;
    Sender.TickPositionReporting(.01f);
    TestEqual(TEXT("Ground settling reports without a movement key or jump"),CountPositions(),++Reports);
    Sender.PlayerPosition.Location.X+=.5;
    Sender.TickPositionReporting(.5f);
    TestEqual(TEXT("Same-cell drift respects the retail report interval"),CountPositions(),Reports);
    Sender.TickPositionReporting(.5f);
    TestEqual(TEXT("Final stationary drift reaches the server"),CountPositions(),++Reports);
    Sender.bAutoPosContact=false;
    Sender.TickPositionReporting(.01f);
    TestEqual(TEXT("Loss of ground contact is reported without waiting a second"),CountPositions(),++Reports);
    Sender.SendStopMovement();
    TestTrue(TEXT("Cancelling input while airborne preserves position updates"),Sender.bMoving);
    Sender.bMoving=false; Sender.bAutoPosContact=true;
    ++Sender.PlayerPosition.CellId;
    Sender.TickPositionReporting(.01f);
    TestEqual(TEXT("Landing/cell transition is reported immediately"),CountPositions(),++Reports);
    FACEBinaryReader FinalPose(Sender.CachedC2SPackets[Sender.NextPacketSequence-1].Payload);
    FinalPose.Skip(28);
    TestEqual(TEXT("Final report carries the actual cell"),FinalPose.ReadUInt32(),uint32(Sender.PlayerPosition.CellId));
    TestEqual(TEXT("Final report carries the actual X coordinate"),FinalPose.ReadFloat(),float(Sender.PlayerPosition.Location.X));
    FACEBinaryWriter Portal;Portal.WriteUInt16(10);Portal.Align();
    FACEBinaryReader PortalReader(Portal.GetData());Sender.HandlePlayerTeleport(PortalReader);
    Sender.PlayerPosition.Location.X+=10;
    Sender.TickPositionReporting(2.f);Sender.FlushAutonomousPosition(true);
    TestEqual(TEXT("Portal loading never publishes an unplaced destination"),CountPositions(),Reports);
    Sender.SendLoginComplete();Sender.TickPositionReporting(1.f);
    TestEqual(TEXT("Portal completion sends the new arrival position while idle"),CountPositions(),++Reports);
    Sender.WorldObjects[123].PhysicsState|=ACEPhysicsState::Hidden;
    auto Materialize=[&](uint16 Sequence)
    {
        FACEBinaryWriter W;W.WriteUInt32(123);W.WriteUInt32(ACEPhysicsState::ReportCollisions);
        W.WriteUInt16(3);W.WriteUInt16(Sequence);
        FACEBinaryReader R(W.GetData());Sender.HandleSetState(R);
    };
    Materialize(1);Sender.TickPositionReporting(1.f);
    TestEqual(TEXT("Delayed server materialization refreshes the ignored arrival pose"),CountPositions(),++Reports);
    Materialize(2);Sender.TickPositionReporting(2.f);
    TestEqual(TEXT("Repeated visible-state updates do not create a reporting loop"),CountPositions(),Reports);
    const uint32 BeforeLogout=Sender.NextPacketSequence;
    Sender.bAutoPosContact=false;Sender.RequestLogOff();
    bool LogoutPosition=false;
    for(const auto& Pair:Sender.CachedC2SPackets)
    {
        if(Pair.Key<BeforeLogout||Pair.Value.Payload.Num()<28)continue;
        FACEBinaryReader R(Pair.Value.Payload);R.Skip(24);
        if(R.ReadUInt32()==ACEGameAction::AutonomousPosition)
        {R.Skip(40);TestEqual(TEXT("Logout's final position does not manufacture a landing"),R.ReadUInt8(),uint8(0));LogoutPosition=true;}
    }
    TestTrue(TEXT("Logout flushes its final position"),LogoutPosition);
    Sender.ClearWorldState();
    TestFalse(TEXT("Relog clears the last character's position report"),Sender.bHaveReportedPosition);
    TestFalse(TEXT("Relog clears the previous portal reporting hold"),Sender.bPositionReportingSuspended);
    TestEqual(TEXT("Relog does not reuse another character's server control"),Sender.ServerControlSeq,uint16(0));
    Receiver->Close(); Sockets->DestroySocket(Receiver);
    FACEBinaryWriter Expected; Expected.WriteUInt32(0xF656); Expected.WriteBytes(Large);
    TestEqual(TEXT("Large message is sent in three independently retransmittable packets"),Packets,3);
    TestTrue(TEXT("Wire fragments reconstruct the original gameplay payload exactly"),Reconstructed==Expected.GetData());
    return true;
}
#endif
