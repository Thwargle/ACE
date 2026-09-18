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
    // Corrections from an earlier incarnation/teleport must never displace a
    // remote player, even if their independent position sequence is higher.
    FACESession PositionSession;
    FACEWorldObject Remote; Remote.Guid=123; Remote.bHasPhysicsTimestamps=true;
    Remote.PhysicsTimestamps[ACEPhysicsTimeStamp::Instance]=3;
    Remote.PhysicsTimestamps[ACEPhysicsTimeStamp::Teleport]=2;
    Remote.PhysicsTimestamps[ACEPhysicsTimeStamp::Position]=10;
    PositionSession.WorldObjects.Add(Remote.Guid,Remote);
    int32 Corrections=0;
    PositionSession.OnPositionUpdate.AddLambda([&](int32,const FACEPosition&){++Corrections;});
    auto Position=[&](uint16 Instance,uint16 Sequence,uint16 Teleport,float X)
    {
        FACEBinaryWriter W; W.WriteUInt32(123); W.WriteUInt32(0x74); W.WriteUInt32(0x2B120021);
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
    FString LastChat; int32 ChatType = -1;
    ChatSession.OnChatMessage.AddLambda([&](const FString& Message, const FString&, int32 Type) { LastChat = Message; ChatType = Type; });
    FACEBinaryWriter Closed; Closed.WriteUInt32(0x0451);
    FACEBinaryReader ClosedReader(Closed.GetData()); ChatSession.HandleWeenieError(ClosedReader);
    TestEqual(TEXT("Trade close has readable text"), LastChat, FString(TEXT("Trade closed.")));
    FACEBinaryWriter Missing; Missing.WriteUInt32(0x042C);
    FACEBinaryReader MissingReader(Missing.GetData()); ChatSession.HandleWeenieError(MissingReader);
    TestEqual(TEXT("Missing combat target has readable text"),LastChat,FString(TEXT("Target not acquired.")));
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
    Weapon.WriteUInt32(124); Weapon.WriteUInt32(0x21); Weapon.WriteUInt32(1);
    Weapon.WriteUInt16(1); Weapon.WriteUInt16(8); Weapon.WriteUInt32(1); Weapon.WriteUInt32(ACEItemType::MeleeWeapon);
    for (uint32 V : {3u,30u,44u,40u}) Weapon.WriteUInt32(V);
    for (double V : {.3,1.,.8,20.,1.15}) Weapon.WriteDouble(V);
    Weapon.WriteUInt32(1);
    FACEBinaryReader WeaponReader(Weapon.GetData()); AppraisalSession.HandleIdentifyObjectResponse(WeaponReader);
    TestTrue(TEXT("Weapon appraisal marks the decoded profile and estimated range"), Appraisal.bHasWeaponProfile && Appraisal.bWeaponMaxVelocityEstimated);
    TestEqual(TEXT("Weapon item type survives appraisal decoding"), Appraisal.ItemType, int32(ACEItemType::MeleeWeapon));
    TestEqual(TEXT("Enchanted maximum damage is preserved"), Appraisal.Damage, 40);
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
    // Wire fixtures exercise the actual datagram parser, checksum, reorder buffer,
    // message assembler and dispatch. No live server or account is involved.
    auto Fragment=[](uint32 Seq, uint16 Count, uint16 Index, const TArray<uint8>& Bytes)
    {
        FACEBinaryWriter W; W.WriteUInt32(Seq); W.WriteUInt32(0x80000000);
        W.WriteUInt16(Count); W.WriteUInt16(16+Bytes.Num()); W.WriteUInt16(Index);
        W.WriteUInt16(ACEQueue::UIQueue); W.WriteBytes(Bytes); return W.GetData();
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
    Receiver->Close(); Sockets->DestroySocket(Receiver);
    FACEBinaryWriter Expected; Expected.WriteUInt32(0xF656); Expected.WriteBytes(Large);
    TestEqual(TEXT("Large message is sent in three independently retransmittable packets"),Packets,3);
    TestTrue(TEXT("Wire fragments reconstruct the original gameplay payload exactly"),Reconstructed==Expected.GetData());
    return true;
}
#endif
