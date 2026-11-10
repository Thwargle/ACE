#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "ACESession.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "Common/UdpSocketBuilder.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACESocialNetworkParityTest, "ACE.Network.SocialParity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FACESocialNetworkParityTest::RunTest(const FString&)
{
    FACESession Session;
    Session.PlayerGuid=0x50000001;
    int32 FellowNotices=0,FriendNotices=0;
    Session.OnFellowshipChanged.AddLambda([&](){++FellowNotices;});
    Session.OnFriendsChanged.AddLambda([&](){++FriendNotices;});
    auto Event=[&](uint32 Opcode,const TArray<uint8>& Payload,int32 Length=INDEX_NONE)
    {
        FACEBinaryWriter W;W.WriteUInt32(Session.PlayerGuid);W.WriteUInt32(1);W.WriteUInt32(Opcode);
        W.WriteBytes(Payload.GetData(),Length==INDEX_NONE?Payload.Num():Length);
        FACEBinaryReader R(W.GetData());Session.HandleGameEvent(R);
    };
    auto Member=[](FACEBinaryWriter& W,uint32 Guid,const FString& Name,uint32 Health)
    {
        W.WriteUInt32(Guid);W.WriteUInt32(100);W.WriteUInt32(200);W.WriteUInt32(275);
        for(uint32 Vital:{500u,400u,300u,Health,210u,110u})W.WriteUInt32(Vital);
        W.WriteUInt32(16);W.WriteString16L(Name);
    };
    auto Full=[&](const FString& Name)
    {
        FACEBinaryWriter W;W.WriteUInt16(12);W.WriteUInt16(16);
        for(uint32 I=0;I<12;++I)Member(W,0x50000001+I,FString::Printf(TEXT("Fellow %u"),I),310+I);
        W.WriteString16L(Name);W.WriteUInt32(Session.PlayerGuid);
        for(uint32 Flag:{1u,0u,1u,1u})W.WriteUInt32(Flag);
        W.WriteUInt16(1);W.WriteUInt16(16);W.WriteUInt32(0x50000100);W.WriteInt32(123);
        return W;
    };
    const auto Original=Full(TEXT("Existing fellowship"));
    Event(ACEGameEvent::FellowshipFullUpdate,Original.GetData());
    TestEqual(TEXT("Retail full roster includes members beyond nine"),Session.GetFellowship().Members.Num(),12);
    TestTrue(TEXT("Retail form without ACE lock extension is complete"),Session.GetFellowship().bValid && FellowNotices==1);
    TestTrue(TEXT("Roster flags and leadership are decoded together"),Session.GetFellowship().LeaderGuid==Session.PlayerGuid && Session.GetFellowship().bShareXP && !Session.GetFellowship().bEvenShare && Session.GetFellowship().bOpen && Session.GetFellowship().bLocked);
    const double Received=Session.GetFellowship().Members[0].VitalsReceivedAt;
    TestTrue(TEXT("Committed vitals have a freshness timestamp"),Received>0);
    auto Replacement=Full(TEXT("Replacement fellowship"));
    const int32 RetailSize=Replacement.GetData().Num();
    Replacement.WriteUInt16(1);Replacement.WriteUInt16(32);Replacement.WriteString16L(TEXT("Quest lock"));
    for(uint32 Field:{0u,0u,0u,123u,4u})Replacement.WriteUInt32(Field);
    for(int32 Size=0;Size<Replacement.GetData().Num();++Size)
    {
        // Ending exactly after the departed table is a valid retail packet.
        if(Size==RetailSize)continue;
        Event(ACEGameEvent::FellowshipFullUpdate,Replacement.GetData(),Size);
        if(!TestTrue(FString::Printf(TEXT("Truncated fellowship at byte %d leaves the live roster intact"),Size),
            FellowNotices==1 && Session.GetFellowship().Name==TEXT("Existing fellowship") && Session.GetFellowship().Members.Num()==12
            && Session.GetFellowship().LeaderGuid==Session.PlayerGuid && Session.GetFellowship().Members[0].VitalsReceivedAt==Received))return false;
    }
    Event(ACEGameEvent::FellowshipFullUpdate,Replacement.GetData());
    TestEqual(TEXT("Complete ACE lock extension commits once"),FellowNotices,2);
    TestEqual(TEXT("Complete replacement installs its metadata"),Session.GetFellowship().Name,FString(TEXT("Replacement fellowship")));

    FACEBinaryWriter Update;Member(Update,0x50000002,TEXT("Updated fellow"),75);Update.WriteUInt32(3);
    const double OldTimestamp=Session.GetFellowship().Members[1].VitalsReceivedAt;
    for(int32 Size=0;Size<Update.GetData().Num();++Size)
    {
        Event(ACEGameEvent::FellowshipUpdateFellow,Update.GetData(),Size);
        if(!TestTrue(TEXT("Incomplete fellow record/type cannot replace or freshen vitals"),FellowNotices==2
            && Session.GetFellowship().Members[1].HealthCur==311 && Session.GetFellowship().Members[1].VitalsReceivedAt==OldTimestamp))return false;
    }
    Event(ACEGameEvent::FellowshipUpdateFellow,Update.GetData());
    TestTrue(TEXT("Vitals update replaces only the addressed fellow"),FellowNotices==3 && Session.GetFellowship().Members.Num()==12
        && Session.GetFellowship().Members[1].HealthCur==75 && Session.GetFellowship().Members[0].HealthCur==310);
    TestEqual(TEXT("Member update preserves fellowship metadata"),Session.GetFellowship().Name,FString(TEXT("Replacement fellowship")));
    FACEBinaryWriter Added;Member(Added,0x50000200,TEXT("New fellow"),450);Added.WriteUInt32(1);
    Event(ACEGameEvent::FellowshipUpdateFellow,Added.GetData());
    Event(ACEGameEvent::FellowshipUpdateFellow,Added.GetData());
    TestEqual(TEXT("Repeated fellow update cannot duplicate a roster member"),Session.GetFellowship().Members.Num(),13);
    FACEBinaryWriter Quit;Quit.WriteUInt32(0x50000200);Event(ACEGameEvent::FellowshipQuit,Quit.GetData());
    TestEqual(TEXT("Quit removes just the addressed fellow"),Session.GetFellowship().Members.Num(),12);
    Quit=FACEBinaryWriter();Quit.WriteUInt32(Session.PlayerGuid);Event(ACEGameEvent::FellowshipDismiss,Quit.GetData());
    TestTrue(TEXT("Local dismissal clears the fellowship"),!Session.GetFellowship().bValid && Session.GetFellowship().Members.IsEmpty());

    auto Friend=[](FACEBinaryWriter& W,uint32 Guid,const FString& Name,bool Online)
    {
        W.WriteUInt32(Guid);W.WriteUInt32(Online?1:0);W.WriteUInt32(0);W.WriteString16L(Name);
        W.WriteUInt32(2);W.WriteUInt32(10);W.WriteUInt32(11); // nonempty friend list
        W.WriteUInt32(1);W.WriteUInt32(12); // nonempty inverse list
    };
    FACEBinaryWriter Friends;Friends.WriteUInt32(2);
    Friend(Friends,50,TEXT("First friend"),true);Friend(Friends,51,TEXT("Second friend"),false);Friends.WriteUInt32(0);
    Event(ACEGameEvent::FriendsListUpdate,Friends.GetData());
    TestTrue(TEXT("Full friend list decodes nested lists and online status"),FriendNotices==1 && Session.GetFriends().Num()==2
        && Session.GetFriends()[0].bOnline && !Session.GetFriends()[1].bOnline);
    FACEBinaryWriter Status;Status.WriteUInt32(1);Friend(Status,51,TEXT("Second friend"),true);Status.WriteUInt32(4);
    for(const FACEBinaryWriter* Wire:{&Friends,&Status})
        for(int32 Size=0;Size<Wire->GetData().Num();++Size)
        {
            Event(ACEGameEvent::FriendsListUpdate,Wire->GetData(),Size);
            if(!TestTrue(TEXT("Truncated friend payload/type never clears or partly replaces the list"),FriendNotices==1
                && Session.GetFriends().Num()==2 && Session.GetFriends()[0].Name==TEXT("First friend") && !Session.GetFriends()[1].bOnline))return false;
        }
    FACEBinaryWriter BadCount;BadCount.WriteUInt32(MAX_uint32);BadCount.WriteUInt32(0);
    Event(ACEGameEvent::FriendsListUpdate,BadCount.GetData());
    TestEqual(TEXT("Impossible friend count cannot allocate or notify"),FriendNotices,1);
    Event(ACEGameEvent::FriendsListUpdate,Status.GetData());
    TestTrue(TEXT("Presence delta changes one friend without replacing the list"),FriendNotices==2 && Session.GetFriends().Num()==2 && Session.GetFriends()[1].bOnline);
    for(uint32 Kind:{2u,1u,3u,1u,99u})
    {
        FACEBinaryWriter Delta;Delta.WriteUInt32(1);Friend(Delta,51,TEXT("Second friend"),false);Delta.WriteUInt32(Kind);
        const int32 Before=FriendNotices;Event(ACEGameEvent::FriendsListUpdate,Delta.GetData());
        TestEqual(TEXT("Only known friend deltas notify"),FriendNotices,Before+(Kind<=4?1:0));
        TestEqual(TEXT("Normal and silent removals preserve unrelated friends"),Session.GetFriends().Num(),(Kind==2||Kind==3)?1:2);
    }
    FACEBinaryWriter EmptyFriends;EmptyFriends.WriteUInt32(0);EmptyFriends.WriteUInt32(0);
    Event(ACEGameEvent::FriendsListUpdate,EmptyFriends.GetData());
    TestTrue(TEXT("Explicit empty full list really clears friends"),Session.GetFriends().IsEmpty());

    int32 AllegianceNotices=0;
    TArray<FString> Chat;
    Session.OnAllegianceChanged.AddLambda([&](){++AllegianceNotices;});
    Session.OnChatMessage.AddLambda([&](const FString& Text,const FString&,int32 Type)
    {
        TestEqual(TEXT("Allegiance info uses retail system chat"),Type,ACEChatMessageType::System);
        Chat.Add(Text);
    });
    auto AllegianceMember=[](FACEBinaryWriter& W,uint32 Guid,const TCHAR* Name,uint16 Rank,uint8 Heritage,uint8 Gender,bool Online)
    {
        W.WriteUInt32(Guid);W.WriteUInt32(101);W.WriteUInt32(202);W.WriteUInt32(8|(Online?1:0));
        W.WriteUInt8(Gender);W.WriteUInt8(Heritage);W.WriteUInt16(Rank);W.WriteUInt32(275);
        W.WriteUInt16(300);W.WriteUInt16(400);W.WriteUInt64(0);W.WriteString16L(Name);
    };
    auto Profile=[&](FACEBinaryWriter& W,uint16 Version,bool Empty=false)
    {
        W.WriteUInt32(Empty?0:6);W.WriteUInt32(Empty?0:2);W.WriteUInt16(Empty?0:6);W.WriteUInt16(Version);
        if(Version>=6){W.WriteUInt16(1);W.WriteUInt16(23);W.WriteUInt32(20);W.WriteUInt32(2);}
        else if(Version>=1)W.WriteUInt32(0);
        if(Version>=9){W.WriteUInt32(1);W.WriteString16L(TEXT("Officer title"));}
        if(Version>=2)for(int32 I=0;I<4;++I)W.WriteUInt32(0);
        if(Version>=3){W.WriteString16L(TEXT("MOTD"));W.WriteString16L(TEXT("Setter"));}
        if(Version>=4)W.WriteUInt32(123);
        if(Version>=7)for(int32 I=0;I<8;++I)W.WriteUInt32(0);
        if(Version>=8){W.WriteString16L(Empty?TEXT(""):TEXT("Our allegiance"));W.WriteUInt32(1234);}
        if(Version>=10)W.WriteUInt32(0);
        if(Version>=11)W.WriteUInt32(0);
        if(Empty)return;
        AllegianceMember(W,10,TEXT("Monarch"),9,1,2,true);
        W.WriteUInt32(10);AllegianceMember(W,20,TEXT("Patron"),5,3,1,false);
        W.WriteUInt32(20);AllegianceMember(W,30,TEXT("Target"),3,1,1,true);
        W.WriteUInt32(30);AllegianceMember(W,40,TEXT("Vassal"),2,2,2,true);
        W.WriteUInt32(20);AllegianceMember(W,50,TEXT("Sibling"),1,1,1,false);
        W.WriteUInt32(40);AllegianceMember(W,60,TEXT("Grandchild"),1,1,1,false);
    };
    Session.PlayerGuid=10;
    FACEBinaryWriter OwnProfile;OwnProfile.WriteUInt32(9);Profile(OwnProfile,11);
    Event(ACEGameEvent::AllegianceUpdate,OwnProfile.GetData());
    TestTrue(TEXT("Own allegiance update resolves monarch self and direct vassals"),AllegianceNotices==1
        && Session.GetAllegiance().Self.Guid==10 && Session.GetAllegiance().Vassals.Num()==1 && Session.GetAllegiance().Vassals[0].Guid==20);
    TestTrue(TEXT("Monarch retains own cached and passed-up XP"),Session.GetAllegiance().SelfCPCached==101 && Session.GetAllegiance().SelfCPTithed==202);
    for(int32 Size=0;Size<OwnProfile.GetData().Num();++Size)
    {
        Event(ACEGameEvent::AllegianceUpdate,OwnProfile.GetData(),Size);
        if(!TestTrue(TEXT("Incomplete allegiance update preserves the complete current hierarchy"),AllegianceNotices==1
            && Session.GetAllegiance().Self.Guid==10 && Session.GetAllegiance().AllegianceName==TEXT("Our allegiance") && Session.GetAllegiance().Vassals.Num()==1))return false;
    }
    for(uint16 Version=0;Version<=11;++Version)
    {
        Chat.Reset();FACEBinaryWriter Query;Query.WriteUInt32(30);Profile(Query,Version);
        Event(ACEGameEvent::AllegianceInfoResponse,Query.GetData());
        TestTrue(FString::Printf(TEXT("Retail hierarchy version %u produces the requested member report"),Version),Chat.Num()==5
            && Chat[1]==TEXT("Allegiance information for Baron Target *:") && Chat[2]==TEXT("   Patron: Ta-chueh Patron")
            && Chat[3]==TEXT("   Vassals:") && Chat[4]==TEXT("      Shayka Vassal *"));
        TestTrue(TEXT("Query neither replaces the player's allegiance nor broadcasts a panel refresh"),AllegianceNotices==1
            && Session.GetAllegiance().Self.Guid==10 && Session.GetAllegiance().Vassals[0].Guid==20);
        if(Version==11)
        {
            for(int32 Size=0;Size<Query.GetData().Num();++Size)
            {
                Chat.Reset();Event(ACEGameEvent::AllegianceInfoResponse,Query.GetData(),Size);
                if(!TestTrue(TEXT("Truncated info response emits no partial report"),Chat.IsEmpty() && AllegianceNotices==1))return false;
            }
        }
    }
    Chat.Reset();FACEBinaryWriter Missing;Missing.WriteUInt32(999);Profile(Missing,11);
    Event(ACEGameEvent::AllegianceInfoResponse,Missing.GetData());
    TestTrue(TEXT("Unknown query target does not report another member's information"),Chat.IsEmpty());
    FACEBinaryWriter RootQuery;RootQuery.WriteUInt32(10);Profile(RootQuery,11);
    Event(ACEGameEvent::AllegianceInfoResponse,RootQuery.GetData());
    TestTrue(TEXT("Monarch query omits patron and includes its direct vassal"),Chat.Num()==4
        && Chat[1]==TEXT("Allegiance information for Queen Monarch *:") && Chat[3]==TEXT("      Ta-chueh Patron"));
    FACEBinaryWriter EmptyAllegiance;EmptyAllegiance.WriteUInt32(0);Profile(EmptyAllegiance,11,true);
    Event(ACEGameEvent::AllegianceUpdate,EmptyAllegiance.GetData());
    TestTrue(TEXT("Complete empty allegiance update clears prior membership"),AllegianceNotices==2 && !Session.GetAllegiance().bValid && Session.GetAllegiance().Vassals.IsEmpty());

    // A visible HUD/plugin can subscribe while disconnected. The server's
    // panel-open flag belongs to one login and must be restored on the next.
    auto* Sockets=ISocketSubsystem::Get();
    auto* Receiver=FUdpSocketBuilder(TEXT("Social parity receiver")).AsNonBlocking().BoundToPort(0).Build();
    ON_SCOPE_EXIT {Session.State=EACESessionState::Disconnected;if(Receiver){Receiver->Close();Sockets->DestroySocket(Receiver);}};
    if(!TestNotNull(TEXT("Social loopback receiver"),Receiver))return false;
    Session.SocketC2S=FUdpSocketBuilder(TEXT("Social parity sender")).AsNonBlocking().BoundToPort(0).Build();
    if(!TestNotNull(TEXT("Social loopback sender"),Session.SocketC2S))return false;
    Session.ServerC2SAddr=Sockets->CreateInternetAddr();Session.ServerC2SAddr->SetIp(0x7f000001);Session.ServerC2SAddr->SetPort(Receiver->GetPortNo());
    Session.IssacClient=MakeUnique<FACEIsaac>(123u);
    FString CommandError;
    Session.State=EACESessionState::InWorld;
    Session.SendSocialCommand(TEXT("allegiance"),TEXT("info \"Target\""),CommandError);
    TestTrue(TEXT("Allegiance info command accepts a quoted name"),CommandError.IsEmpty());
    bool SentQuery=false;
    for(const auto& Packet:Session.CachedC2SPackets)
    {
        FACEBinaryReader R(Packet.Value.Payload);R.Skip(24);
        if(R.ReadUInt32()==0x027B)SentQuery=R.ReadString16L()==TEXT("Target") && R.Remaining()==0;
    }
    TestTrue(TEXT("Info request sends the retail opcode and exact target name"),SentQuery);
    Session.State=EACESessionState::Disconnected;Session.CachedC2SPackets.Reset();
    auto Subscriptions=[&](bool Enabled)
    {
        int32 Count=0;
        for(const auto& Packet:Session.CachedC2SPackets)
        {
            FACEBinaryReader R(Packet.Value.Payload);if(!R.CanRead(32))continue;R.Skip(16);
            if(R.ReadUInt32()!=ACEOpcode::GameAction)continue;R.ReadUInt32();
            if(R.ReadUInt32()==ACEGameAction::FellowshipUpdateRequest && R.ReadUInt32()==(Enabled?1u:0u))++Count;
        }
        return Count;
    };
    Session.SendFellowshipUpdateRequest(true);
    TestEqual(TEXT("Pre-login subscription sends no gameplay packet"),Session.CachedC2SPackets.Num(),0);
    auto Enter=[&]()
    {
        Session.PlayerGuid=0x50000001;Session.PlayerPosition.CellId=0x7D640001;
        Session.SetState(EACESessionState::EnteringWorld);Session.MaybeEnterWorldComplete();
    };
    Enter();TestEqual(TEXT("World entry restores requested live vitals"),Subscriptions(true),1);
    Session.MaybeEnterWorldComplete();TestEqual(TEXT("Completed entry cannot repeatedly subscribe"),Subscriptions(true),1);
    Session.SetState(EACESessionState::CharacterSelect);Session.ClearWorldState();Session.CachedC2SPackets.Reset();
    Enter();TestEqual(TEXT("Relog restores the existing subscriber's server flag"),Subscriptions(true),1);
    Session.SendFellowshipUpdateRequest(false);
    TestEqual(TEXT("Final unsubscribe is sent in world"),Subscriptions(false),1);
    Session.SetState(EACESessionState::CharacterSelect);Session.CachedC2SPackets.Reset();Enter();
    TestEqual(TEXT("No subscriber means no request after relog"),Subscriptions(true),0);
    Session.SetState(EACESessionState::CharacterSelect);Session.SendFellowshipUpdateRequest(true);Session.SendFellowshipUpdateRequest(false);
    Session.CachedC2SPackets.Reset();Enter();
    TestEqual(TEXT("Cancelled offline subscription stays cancelled"),Subscriptions(true),0);
    return true;
}
#endif
