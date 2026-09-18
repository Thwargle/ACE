#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ACESession.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEInteractionRecoveryTest, "ACE.Network.InteractionRecovery",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACEInteractionRecoveryTest::RunTest(const FString&)
{
    FACESession Session; Session.State = EACESessionState::InWorld; Session.PlayerGuid = 100;
    auto Deliver = [&](uint32 Opcode, uint32 Type, uint32 Context, const FString& Prompt)
    {
        FACEBinaryWriter W; W.WriteUInt32(100); W.WriteUInt32(1); W.WriteUInt32(Opcode);
        W.WriteUInt32(Type); W.WriteUInt32(Context); if (Opcode == 0x274) W.WriteString16L(Prompt);
        FACEBinaryReader R(W.GetData()); Session.HandleGameEvent(R);
    };
    Deliver(0x274,7,29,TEXT("Would you like to start a monster fight?"));
    Deliver(0x274,7,29,TEXT("Would you like to start a monster fight?"));
    TestEqual(TEXT("Duplicate request creates one prompt"), Session.GetConfirmations().Num(),1);
    TestFalse(TEXT("Wrong context cannot answer another question"),Session.RespondToConfirmation(7,28,true));
    const uint32 Before=Session.NextGameActionSequence;
    TestTrue(TEXT("Accept sends the pending reply"),Session.RespondToConfirmation(7,29,true));
    TestEqual(TEXT("One confirmation action is sent"),Session.NextGameActionSequence,Before+1);
    TestFalse(TEXT("Repeated click cannot submit twice"),Session.RespondToConfirmation(7,29,true));
    Deliver(0x274,7,30,TEXT("Next fight?")); Deliver(0x274,5,31,TEXT("Craft?"));
    Deliver(0x276,7,30,TEXT(""));
    TestEqual(TEXT("Server Done only withdraws the matching prompt"),Session.GetConfirmations().Num(),1);
    TestTrue(TEXT("Decline also closes the pending request"),Session.RespondToConfirmation(5,31,false));
    Session.LastServerPacketAt=100.;
    TestFalse(TEXT("Normal packet gap is tolerated"),Session.HasConnectionTimedOut(119.));
    TestTrue(TEXT("Suspension/lost server expires with wall time"),Session.HasConnectionTimedOut(121.));
    Session.LastServerPacketAt=120.; TestFalse(TEXT("Fresh received packet restores liveness"),Session.HasConnectionTimedOut(121.));
    Session.State=EACESessionState::Failed; TestFalse(TEXT("Authentication failure cannot start a reconnect loop"),Session.HasConnectionTimedOut(200.));
    Session.State=EACESessionState::InWorld;
    int32 Speech=0,Chat=0,Damage=0;
    Session.OnNPCSpeech.AddLambda([&](int32 Guid,const FString& Text){if(Guid==200 && Text==TEXT("Welcome")) ++Speech;});
    Session.OnChatMessage.AddLambda([&](const FString&,const FString&,int32){++Chat;});
    Session.OnCombatFeedback.AddLambda([&](const FString&,int32 Amount,bool,bool){Damage=Amount;});
    FACEWorldObject NPC; NPC.Guid=200; NPC.ItemType=ACEItemType::Creature; Session.WorldObjects.Add(200,NPC);
    FACEBinaryWriter Tell; Tell.WriteString16L(TEXT("Welcome"));Tell.WriteString16L(TEXT("Bookie"));
    Tell.WriteUInt32(200);Tell.WriteUInt32(100);Tell.WriteUInt32(ACEChatMessageType::Tell);Tell.WriteUInt32(0);
    FACEBinaryReader TellReader(Tell.GetData());Session.HandleTell(TellReader);
    TestEqual(TEXT("NPC speech is available above the exact sender"),Speech,1);
    TestEqual(TEXT("NPC speech remains in the regular chat log"),Chat,1);
    FACEBinaryWriter Hit;Hit.WriteString16L(TEXT("Drudge"));Hit.WriteUInt32(1);Hit.WriteDouble(.2);Hit.WriteUInt32(42);Hit.WriteUInt32(0);Hit.WriteUInt64(0);
    FACEBinaryReader HitReader(Hit.GetData());Session.HandleCombatAttackerNotification(HitReader);
    TestEqual(TEXT("Floating damage uses the server amount"),Damage,42);
    TestEqual(TEXT("Combat feedback also remains in chat"),Chat,2);
    int32 HealthEvents=0,LastGuid=0,LastChange=0; uint32 LastFlags=0;
    Session.OnHealthFeedback.AddLambda([&](int32 Guid,int32 Change,uint32 Flags){++HealthEvents;LastGuid=Guid;LastChange=Change;LastFlags=Flags;});
    auto DeliverHealth=[&](int32 Guid,int32 Change,uint32 Flags,int32 Trim=0)
    {
        FACEBinaryWriter W;W.WriteUInt32(100);W.WriteUInt32(1);W.WriteUInt32(0xF7D2);
        W.WriteUInt32(Guid);W.WriteInt32(Change);W.WriteUInt32(Flags);
        auto Bytes=W.GetData();if(Trim)Bytes.SetNum(Bytes.Num()-Trim);
        FACEBinaryReader Reader(Bytes);Session.HandleGameEvent(Reader);
    };
    Session.VRCapabilities=63;DeliverHealth(200,-30,1);TestEqual(TEXT("Legacy capabilities cannot receive health extensions"),HealthEvents,0);
    Session.VRCapabilities=127;DeliverHealth(200,-30,3);
    TestEqual(TEXT("Magic critical damage is delivered once"),HealthEvents,1);
    TestEqual(TEXT("Numbers carry the exact target ID, not its name"),LastGuid,200);
    TestEqual(TEXT("Damage retains its signed amount"),LastChange,-30);TestEqual(TEXT("Magic and critical flags survive transport"),LastFlags,3u);
    DeliverHealth(100,40,1);TestEqual(TEXT("Healing is delivered for the local player"),LastChange,40);
    DeliverHealth(999,-30,0);DeliverHealth(200,-30,8);DeliverHealth(200,0,0);DeliverHealth(200,-30,0,1);
    TestEqual(TEXT("Unknown targets, flags, empty damage and truncated packets are ignored"),HealthEvents,2);
    Session.Disconnect(); TestTrue(TEXT("Disconnect withdraws all prompts"),Session.GetConfirmations().IsEmpty());
    Session.State=EACESessionState::AwaitConnectRequest;Session.LoginRequestAt=FPlatformTime::Seconds()-21.;Session.Tick(.016f);
    TestEqual(TEXT("An unavailable server cannot leave recovery connecting forever"),Session.State,EACESessionState::Failed);
    return true;
}
#endif
