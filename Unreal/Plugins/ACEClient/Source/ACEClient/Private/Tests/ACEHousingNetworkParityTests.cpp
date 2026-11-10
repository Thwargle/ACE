#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "ACESession.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "Common/UdpSocketBuilder.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEHousingNetworkParityTest,"ACE.Network.HousingParity",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)

bool FACEHousingNetworkParityTest::RunTest(const FString&)
{
    FACESession S; S.PlayerGuid=100; S.State=EACESessionState::InWorld;
    auto* Sockets=ISocketSubsystem::Get();
    auto* Receiver=FUdpSocketBuilder(TEXT("Housing parity receiver")).AsNonBlocking().BoundToPort(0).Build();
    ON_SCOPE_EXIT { S.State=EACESessionState::Disconnected; if(Receiver){Receiver->Close();Sockets->DestroySocket(Receiver);} };
    if (!TestNotNull(TEXT("Loopback receiver"),Receiver)) return false;
    S.SocketC2S=FUdpSocketBuilder(TEXT("Housing parity sender")).AsNonBlocking().BoundToPort(0).Build();
    if (!TestNotNull(TEXT("Loopback sender"),S.SocketC2S)) return false;
    S.ServerC2SAddr=Sockets->CreateInternetAddr(); S.ServerC2SAddr->SetIp(0x7f000001); S.ServerC2SAddr->SetPort(Receiver->GetPortNo());
    S.IssacClient=MakeUnique<FACEIsaac>(123u);
    auto Event=[&](uint32 Opcode,const FACEBinaryWriter& Body,int32 Length=INDEX_NONE)
    {
        FACEBinaryWriter W; W.WriteUInt32(100); W.WriteUInt32(1); W.WriteUInt32(Opcode);
        W.WriteBytes(Body.GetData().GetData(),Length==INDEX_NONE?Body.Num():Length);
        FACEBinaryReader R(W.GetData()); S.HandleGameEvent(R); return R.Remaining();
    };
    // Independent fixtures from HouseData/HousePayment/HAR/GuestInfo::Pack.
    auto Payment=[](FACEBinaryWriter& W,int32 Required,int32 Paid,uint32 Wcid,const FString& Name,const FString& Plural)
    {
        W.WriteInt32(Required); W.WriteInt32(Paid); W.WriteUInt32(Wcid); W.WriteString16L(Name); W.WriteString16L(Plural);
    };
    FACEBinaryWriter Full; Full.WriteUInt32(1700000000); Full.WriteUInt32(1750000000); Full.WriteUInt32(2); Full.WriteUInt32(0);
    Full.WriteUInt32(1); Payment(Full,2,2,111,TEXT("Writ"),TEXT("Writs"));
    Full.WriteUInt32(2); Payment(Full,1000,1000,273,TEXT("Pyreal"),TEXT("Pyreals")); Payment(Full,3,2,112,TEXT("Token"),TEXT("Tokens"));
    Full.WriteUInt32(0x7f7f0001); Full.WriteFloat(12); Full.WriteFloat(24); Full.WriteFloat(36);
    Full.WriteFloat(1); Full.WriteFloat(0); Full.WriteFloat(0); Full.WriteFloat(0);
    S.House.bOwned=true; S.House.bQueried=true; S.House.RentTime=1; S.House.Position.CellId=77;
    int32 Notices=0; S.OnHouseChanged.AddLambda([&](){++Notices;});
    for (int32 Size=0;Size<Full.Num();++Size)
    {
        Event(0x0225,Full,Size);
        if (!TestTrue(TEXT("Every truncated house snapshot preserves data and sends no notice"),Notices==0
            && S.House.bOwned && S.House.RentTime==1 && S.House.Position.CellId==77)) return false;
    }
    TestEqual(TEXT("Full house data consumes both payment lists and position"),Event(0x0225,Full),0);
    if (!TestTrue(TEXT("Owned dwelling snapshot installed"),Notices==1 && S.House.HouseType==2 && S.House.Buy.Num()==1
        && S.House.Rent.Num()==2 && S.House.Rent[1].Paid==2 && S.House.Position.Location==FVector(12,24,36))) return false;
    FACEBinaryWriter BadPosition;
    BadPosition.WriteBytes(Full.GetData().GetData(),Full.Num()-4); BadPosition.WriteFloat(std::numeric_limits<float>::quiet_NaN());
    Event(0x0225,BadPosition);
    TestTrue(TEXT("Nonfinite location/rotation cannot replace the map location"),Notices==1 && S.House.Position.Location==FVector(12,24,36));

    FACEBinaryWriter Rent; Rent.WriteUInt32(1); Payment(Rent,1000,250,273,TEXT("Pyreal"),TEXT("Pyreals"));
    for (int32 Size=0;Size<Rent.Num();++Size)
    {
        Event(0x0228,Rent,Size);
        if (!TestTrue(TEXT("Partial rent list cannot remove or partially replace requirements"),Notices==1 && S.House.Rent.Num()==2
            && S.House.Rent[0].Paid==1000)) return false;
    }
    FACEBinaryWriter Impossible; Impossible.WriteUInt32(MAX_uint32); Event(0x0228,Impossible);
    FACEBinaryWriter Negative; Negative.WriteUInt32(1); Payment(Negative,1000,-1,273,TEXT("Pyreal"),TEXT("Pyreals")); Event(0x0228,Negative);
    TestEqual(TEXT("Invalid counts and payments leave state unchanged"),Notices,1);
    TestEqual(TEXT("Rent replacement fully consumed"),Event(0x0228,Rent),0);
    Event(0x0228,Rent);
    TestTrue(TEXT("Rent replies replace rather than accumulate payments"),Notices==3 && S.House.Rent.Num()==1 && S.House.Rent[0].Paid==250);
    TestTrue(TEXT("Rent change preserves purchase, location and period"),S.House.Buy[0].Paid==2 && S.House.Position.CellId==int32(0x7f7f0001)
        && S.House.RentTime==1750000000 && S.House.HouseType==2);
    FACEBinaryWriter Time; Time.WriteUInt32(1752592000);
    for(int32 Size=0;Size<4;++Size) Event(0x0227,Time,Size);
    TestEqual(TEXT("Truncated period cannot erase payments"),Notices,3);
    S.House.bMaintenanceFree=true;
    TestEqual(TEXT("New period message consumed"),Event(0x0227,Time),0);
    TestTrue(TEXT("New period clears paid values but retains requirements and maintenance-free setting"),Notices==4
        && S.House.RentTime==1752592000 && S.House.Rent[0].Paid==0 && S.House.Rent[0].Required==1000 && S.House.bMaintenanceFree);
    FACEBinaryWriter Empty; Empty.WriteUInt32(0); Event(0x0228,Empty);
    TestTrue(TEXT("Complete empty payment replacement clears old requirements"),S.House.Rent.IsEmpty() && Notices==5);
    FACEBinaryWriter Status; Status.WriteUInt32(1);
    for (int32 Size=0;Size<4;++Size) Event(0x0226,Status,Size);
    TestTrue(TEXT("Truncated status cannot remove ownership"),S.House.bOwned && Notices==5);
    Event(0x0226,Status);
    TestTrue(TEXT("Complete status clears house and records the response"),!S.House.bOwned && S.House.bQueried
        && S.House.StatusError==1 && S.House.Position.CellId==0 && Notices==6);
    Event(0x0227,Time); Event(0x0228,Rent);
    TestTrue(TEXT("Late rent updates cannot create ownership"),!S.House.bOwned && S.House.RentTime==0 && S.House.Rent.IsEmpty() && Notices==6);

    TArray<FString> Chat;
    S.OnChatMessage.AddLambda([&](const FString& Text,const FString& Sender,int32 Type)
    {
        TestTrue(TEXT("House guest report uses retail default chat"),Sender.IsEmpty() && Type==ACEChatMessageType::Broadcast);
        Chat.Add(Text);
    });
    auto Guests=[&](uint32 Version,uint32 Flags,bool WithGuests)
    {
        FACEBinaryWriter W;
        W.WriteUInt32(Version?Version:Flags);
        if(Version){W.WriteUInt32(Flags);W.WriteUInt32(700);}
        W.WriteUInt16(WithGuests?2:0); W.WriteUInt16(64);
        if(WithGuests)
        {
            W.WriteUInt32(0x80000001);W.WriteUInt32(0);W.WriteString16L(TEXT("Alice"));
            W.WriteUInt32(0x80000002);W.WriteUInt32(1);W.WriteString16L(TEXT("B\u00e9atrice"));
        }
        if(Version>=0x10000002){W.WriteUInt32(2);W.WriteUInt32(100);W.WriteUInt32(101);}
        return W;
    };
    for (uint32 Version:{0u,0x10000001u,0x10000002u})
    {
        const auto Body=Guests(Version,5,true); Chat.Reset();
        for(int32 Size=0;Size<Body.Num();++Size)
        {
            Event(0x0257,Body,Size);
            if(!TestTrue(TEXT("Truncated guest names/tables/roommates never print partial results"),Chat.IsEmpty())) return false;
        }
        TestEqual(TEXT("Legacy and versioned HAR messages fully consumed"),Event(0x0257,Body),0);
        const FString Expected=FString(TEXT("Guests:\n  Alice\n  B\u00e9atrice *\n"))
            +(Version?TEXT("Allegiance Permissions: storage access\n"):TEXT(""))+TEXT("[ open house ]\n");
        if (!TestTrue(TEXT("Report retains names, storage stars and permissions without printing roommate IDs"),Chat.Num()==1 && Chat[0]==Expected)) return false;
    }
    Chat.Reset(); Event(0x0257,Guests(0x10000002,2,false));
    TestTrue(TEXT("No guests and allegiance-only permissions are reported"),Chat.Num()==1 && Chat[0]==TEXT("Guests:\n  None\nAllegiance Permissions: guest access\n"));
    Chat.Reset(); Event(0x0257,Guests(0,0,false));
    TestTrue(TEXT("Legacy closed empty house"),Chat.Num()==1 && Chat[0]==TEXT("Guests:\n  None\n"));
    Chat.Reset(); FACEBinaryWriter BadHAR; BadHAR.WriteUInt32(0); BadHAR.WriteUInt16(MAX_uint16); BadHAR.WriteUInt16(64);
    Event(0x0257,BadHAR); TestTrue(TEXT("Impossible guest count emits nothing"),Chat.IsEmpty());
    FACEBinaryWriter Duplicates; Duplicates.WriteUInt32(0x10000001);Duplicates.WriteUInt32(0);Duplicates.WriteUInt32(0);
    Duplicates.WriteUInt16(2);Duplicates.WriteUInt16(64);
    for(uint32 Value:{0u,1u}){Duplicates.WriteUInt32(200);Duplicates.WriteUInt32(Value);Duplicates.WriteString16L(TEXT("Same guest"));}
    Event(0x0257,Duplicates);
    TestTrue(TEXT("Retail hash table rejects duplicate guest IDs without partial output"),Chat.IsEmpty());
    FACEBinaryWriter NoBuckets; NoBuckets.WriteUInt32(0); NoBuckets.WriteUInt16(1); NoBuckets.WriteUInt16(0);
    NoBuckets.WriteUInt32(200); NoBuckets.WriteUInt32(0); NoBuckets.WriteString16L(TEXT("Alice"));
    Event(0x0257,NoBuckets); TestTrue(TEXT("Nonempty guest tables require buckets"),Chat.IsEmpty());

    S.SendSimpleGameAction(ACEGameAction::RequestFullGuestList);
    bool Found=false;
    for(const auto& Packet:S.CachedC2SPackets)
    {
        FACEBinaryReader R(Packet.Value.Payload);
        if(!R.CanRead(28))continue;
        R.Skip(16);if(R.ReadUInt32()!=ACEOpcode::GameAction)continue;R.ReadUInt32();
        if(R.ReadUInt32()==0x024d)Found=R.Remaining()==0;
    }
    TestTrue(TEXT("Guest-list request sends retail action without an extra payload"),Found);
    Event(0x0225,Full);
    FACEBinaryWriter LargePayments; LargePayments.WriteUInt32(257);
    for(int32 I=0;I<257;++I) Payment(LargePayments,1,0,1000+I,TEXT("Token"),TEXT("Tokens"));
    TestEqual(TEXT("Complete custom payment lists are bounded by packet size, not a 256-entry cap"),Event(0x0228,LargePayments),0);
    TestEqual(TEXT("All custom requirements retained"),S.House.Rent.Num(),257);
    S.ClearWorldState();
    TestTrue(TEXT("Logout clears housing state"),!S.House.bOwned && !S.House.bQueried && S.House.Rent.IsEmpty());
    return !HasAnyErrors();
}
#endif
