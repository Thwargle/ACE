#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "ACECharacterCreation.h"
#include "ACECombatStance.h"
#include "ACECombatTargeting.h"
#include "ACEOlthoi.h"
#include "ACEDatSubsystem.h"
#include "ACESession.h"
#include "UI/ACEUIResourceResolver.h"
#include "Engine/GameInstance.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEPlayableOlthoiTest,"ACE.RetailParity.PlayableOlthoi",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACEPlayableOlthoiTest::RunTest(const FString&)
{
    auto* GI=NewObject<UGameInstance>();
    auto* Dat=NewObject<UACEDatSubsystem>(GI);
    if(!TestTrue(TEXT("Retail DAT available"),Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call"))))return false;
    ON_SCOPE_EXIT { Dat->Deinitialize(); };
    FACECharacterCreation Model; FString Error;
    if(!TestTrue(TEXT("Retail creation tables"),Model.Load(*Dat->GetPortalDat(),Error)))return false;
    auto* Resources=NewObject<UACEUIResourceResolver>();Resources->Initialize(Dat);
    for(uint32 Heritage:{12u,13u})
    {
        TestTrue(TEXT("Olthoi heritage can be selected"),Model.SelectHeritage(Heritage));
        Model.Selection.Name=TEXT("Hive Warrior");
        TestTrue(TEXT("Default Olthoi creation accepted"),Model.Validate(Error));
        TestTrue(TEXT("Olthoi character class mapping exists"),Model.Selection.ClassId!=0);
        for(const auto& Sex:Model.Heritage()->Sexes)
        {
            Model.SelectSex(Sex.Key);
            for(int32 Body=0;Body<Model.Sex()->Hair.Num();++Body)
            {
                Model.Selection.HairStyle=Body;
                FACEWorldObject Preview;
                TestTrue(TEXT("Olthoi body customization builds"),Model.BuildAppearance(Preview));
                const auto Built=Dat->GetOrBuildSetupAppearanceShared(Preview.SetupId,Preview.Appearance,100,0);
                if(!TestTrue(TEXT("All customized Olthoi meshes resolve"),Built.IsValid()))continue;
                float Step=0,Height=0,Radius=0;uint32 Animation=0;
                TestTrue(TEXT("Authored Olthoi collision dimensions"),Dat->TryGetSetupPhysics(Preview.SetupId,Step,Height,Radius,Animation));
                TestTrue(TEXT("Olthoi collision has physical extent"),Height>0 && Radius>0);
                TArray<FTransform> Frames;int32 Count=0;
                TestTrue(TEXT("Olthoi idle animation"),Dat->EvaluateIdleMotion(Preview.MotionTableId,.2f,Built->Parts.Num(),Frames,100,Count));
                TestTrue(TEXT("Olthoi paperdoll uses authored animation"),Dat->EvaluateAnimationLoop(Resources->ResolvePaperDollAnimation(Heritage),0,Built->Parts.Num(),Frames,100,Count));
                TestTrue(TEXT("Olthoi preview retains nonhuman parts"),Count>0);
                TestTrue(TEXT("Body choice remains serializable"),Model.Validate(Error));
            }
        }
    }

    // Real retail Acid Spitter spells supplied by the server's 43481 template.
    for(uint32 Spell:{5440u,5442u,5444u,5439u,5446u,5522u})
    {
        FString Name,Details;uint32 Icon=0,Flags=0,Type=0;bool Projectile=false;
        TestTrue(TEXT("Acid spell name and icon"),Dat->TryGetSpellInfo(Spell,Name,Icon));
        TestTrue(TEXT("Acid spell has a real icon"),Icon!=0);
        TestTrue(TEXT("Acid spell inspection"),Dat->TryGetSpellExamination(Spell,Details));
        TestTrue(TEXT("Acid spell targeting"),Dat->TryGetRetailSpellTargeting(Spell,Flags,Type,Projectile));
        TestTrue(TEXT("Acid spell targets creatures"),(Type&ACEItemType::Creature)!=0);
    }

    FACESession S;S.State=EACESessionState::InWorld;S.PlayerGuid=0x50000001;
    S.PlayerVitals.HeritageGroup=12;
    FACEWorldObject Self;Self.Guid=S.PlayerGuid;Self.bIsSelf=true;Self.bIsPlayer=true;Self.ItemType=ACEItemType::Creature;
    Self.ObjectDescriptionFlags=ACEObjectDescFlag::Attackable|ACEObjectDescFlag::PlayerKiller;
    S.WorldObjects.Add(Self.Guid,Self);
    FACEWorldObject Other=Self;Other.Guid=0x50000002;Other.bIsSelf=false;
    S.WorldObjects.Add(Other.Guid,Other);
    TestTrue(TEXT("PK Olthoi targets PK human"),S.IsCombatTarget(Other.Guid));
    S.WorldObjects[Other.Guid].ObjectDescriptionFlags=ACEObjectDescFlag::Attackable;
    TestFalse(TEXT("PK Olthoi cannot target protected NPK player"),S.IsCombatTarget(Other.Guid));
    S.WorldObjects[Other.Guid].ObjectDescriptionFlags|=ACEObjectDescFlag::FreePkStatus;
    TestTrue(TEXT("Free-PK target does not require matching PK flag"),S.IsCombatTarget(Other.Guid));
    S.WorldObjects[Other.Guid].ObjectDescriptionFlags=0;
    S.WorldObjects[Self.Guid].ObjectDescriptionFlags=ACEObjectDescFlag::FreePkStatus;
    TestTrue(TEXT("Free-PK local character may target other players"),S.IsCombatTarget(Other.Guid));
    TestFalse(TEXT("Never auto-target self"),S.IsCombatTarget(Self.Guid));
    S.WorldObjects[Self.Guid].ObjectDescriptionFlags=ACEObjectDescFlag::PkLiteStatus;
    S.WorldObjects[Other.Guid].ObjectDescriptionFlags=ACEObjectDescFlag::PlayerKiller;
    TestFalse(TEXT("PK and PKLite remain separate"),S.IsCombatTarget(Other.Guid));
    S.WorldObjects[Other.Guid].ObjectDescriptionFlags=ACEObjectDescFlag::PkLiteStatus;
    TestTrue(TEXT("PKLite pair is targetable"),S.IsCombatTarget(Other.Guid));
    Other.bIsPlayer=false;Other.PetOwnerId=55;Other.ObjectDescriptionFlags=ACEObjectDescFlag::Attackable;
    TestFalse(TEXT("Summons are not ordinary monster targets"),ACECombatTargeting::CanAttack(Other,&Self));

    FString HeardText,HeardSender;int Heard=0,Tells=0;
    S.OnChatMessage.AddLambda([&](const FString& Text,const FString& Sender,int32){HeardText=Text;HeardSender=Sender;++Heard;});
    S.OnPlayerTell.AddLambda([&](const FString&,const FString&,int32){++Tells;});
    auto Tell=[&](const TCHAR* Name)
    {
        FACEBinaryWriter W;W.WriteString16L(TEXT("secret"));W.WriteString16L(Name);
        W.WriteInt32(0x50000002);W.WriteInt32(S.PlayerGuid);W.WriteInt32(ACEChatMessageType::Tell);W.WriteUInt32(0);
        FACEBinaryReader R(W.GetData());S.HandleTell(R);
    };
    Tell(TEXT("Hive Ally&"));
    TestEqual(TEXT("Olthoi understand each other"),HeardText,FString(TEXT("secret")));
    TestEqual(TEXT("Reply name excludes language marker"),S.LastTellSenderName,FString(TEXT("Hive Ally")));
    Tell(TEXT("Human"));TestNotEqual(TEXT("Human speech scrambled for Olthoi"),HeardText,FString(TEXT("secret")));
    TestEqual(TEXT("Foreign-language tell does not run buff commands"),Tells,1);
    Tell(TEXT("Admin^"));TestEqual(TEXT("Admin language bypass"),HeardText,FString(TEXT("secret")));
    S.PlayerVitals.HeritageGroup=1;Tell(TEXT("Hive Ally&"));
    TestNotEqual(TEXT("Olthoi speech scrambled for humans"),HeardText,FString(TEXT("secret")));
    S.PlayerVitals.QualityBools.Add(129,true);Tell(TEXT("Hive Ally&"));
    TestEqual(TEXT("NoOlthoiTalk listener override"),HeardText,FString(TEXT("secret")));
    for(int32 Heritage:{12,13})
    {
        TestTrue(TEXT("Olthoi chat available"),ACEOlthoi::CanUseChatDestination(Heritage,13));
        TestTrue(TEXT("Local speech available"),ACEOlthoi::CanUseChatDestination(Heritage,0));
        TestFalse(TEXT("Human global channel unavailable"),ACEOlthoi::CanUseChatDestination(Heritage,7));
        TestFalse(TEXT("Olthoi cannot choose fellowship chat"),ACEOlthoi::CanUseChatDestination(Heritage,1));
    }

    // Verify standard reliable GameAction packets with a loopback receiver.
    auto* Sockets=ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    auto* Receiver=Sockets->CreateSocket(NAME_DGram,TEXT("Olthoi protocol receiver"),false);
    if(!TestNotNull(TEXT("Protocol receiver"),Receiver))return false;
    ON_SCOPE_EXIT { Receiver->Close();Sockets->DestroySocket(Receiver); };
    auto Address=Sockets->CreateInternetAddr();bool Valid=false;Address->SetIp(TEXT("127.0.0.1"),Valid);Address->SetPort(0);
    if(!TestTrue(TEXT("Bind loopback"),Receiver->Bind(*Address)))return false;
    Receiver->GetAddress(*Address);Receiver->SetNonBlocking(true);S.ServerC2SAddr=Address;
    S.SocketC2S=Sockets->CreateSocket(NAME_DGram,TEXT("Olthoi protocol sender"),false);
    const uint8 Seed[]={1,2,3,4};S.IssacClient=MakeUnique<FACEIsaac>(Seed);
    auto Expect=[&](uint32 Action,const TArray<uint8>& Payload)
    {
        uint8 Buffer[4096];int32 Read=0;const double Deadline=FPlatformTime::Seconds()+1;
        while(!Receiver->Recv(Buffer,sizeof(Buffer),Read)&&FPlatformTime::Seconds()<Deadline)FPlatformProcess::Sleep(.001f);
        if(!TestTrue(TEXT("Action reaches wire"),Read>44))return;
        FACEBinaryReader R(Buffer,Read);R.Skip(34);TestEqual(TEXT("Weenie action queue"),R.ReadUInt16(),ACEQueue::WeenieQueue);
        TestEqual(TEXT("GameAction wrapper"),R.ReadUInt32(),0xF7B1u);R.ReadUInt32();
        TestEqual(TEXT("Retail action opcode"),R.ReadUInt32(),Action);
        TestTrue(TEXT("Retail payload unchanged"),R.ReadBytes(R.Remaining())==Payload);
    };
    FACEBinaryWriter Mode;Mode.WriteUInt32(ACECombatMode::Melee);S.SendChangeCombatMode(ACECombatMode::Melee);Expect(ACEGameAction::ChangeCombatMode,Mode.GetData());
    TestEqual(TEXT("Ripper fights unarmed"),S.GetCurrentStance(),ACEMotion::StanceHandCombat);
    FACEBinaryWriter Attack;Attack.WriteInt32(Other.Guid);Attack.WriteUInt32(ACEAttackHeight::High);Attack.WriteFloat(.5f);
    S.SendTargetedMeleeAttack(Other.Guid,ACEAttackHeight::High,.5f);Expect(ACEGameAction::TargetedMeleeAttack,Attack.GetData());
    FACEWorldObject Goo;Goo.Guid=55;Goo.WielderId=S.PlayerGuid;Goo.CurrentWieldedLocation=ACEEquipMask::Held;Goo.ItemType=ACEItemType::Caster;
    S.WorldObjects.Add(Goo.Guid,Goo);
    TestEqual(TEXT("Salivatory Goo selects standard magic combat"),ACECombatStance::ResolveEquippedMode(S),ACECombatMode::Magic);
    FACEBinaryWriter Magic;Magic.WriteUInt32(ACECombatMode::Magic);S.SendChangeCombatMode(ACECombatMode::Magic);Expect(ACEGameAction::ChangeCombatMode,Magic.GetData());
    for(int32 Spell:{5440,5442,5444,5439,5446,5522})
    {
        FACEBinaryWriter Cast;Cast.WriteInt32(Other.Guid);Cast.WriteInt32(Spell);
        S.SendCastSpell(Spell,Other.Guid);Expect(ACEGameAction::CastTargetedSpell,Cast.GetData());
    }
    FACEBinaryWriter Use;Use.WriteInt32(100);S.SendUseItem(100);Expect(ACEGameAction::Use,Use.GetData());
    FACEWorldObject Corpse;Corpse.Guid=100;Corpse.ItemType=ACEItemType::Container;
    Corpse.ObjectDescriptionFlags=ACEObjectDescFlag::Corpse|ACEObjectDescFlag::Openable;
    S.WorldObjects.Add(100,Corpse);
    FACEWorldObject Slag;Slag.Guid=101;Slag.Name=TEXT("Monster Slag");Slag.ItemType=ACEItemType::Misc;
    S.WorldObjects.Add(101,Slag);
    FACEBinaryWriter Contents;Contents.WriteUInt32(100);Contents.WriteUInt32(1);Contents.WriteUInt32(101);Contents.WriteUInt32(0);
    FACEBinaryReader ContentsReader(Contents.GetData());S.HandleViewContents(ContentsReader);
    TestEqual(TEXT("Server corpse contents opens normal loot interface"),S.GetOpenExternalContainerGuid(),100);
    TestEqual(TEXT("Slag belongs to the server corpse"),S.WorldObjects[101].ContainerId,100);
    FACEBinaryWriter Loot;Loot.WriteInt32(101);Loot.WriteInt32(S.PlayerGuid);Loot.WriteInt32(0);
    S.SendPutItemInContainer(101,S.PlayerGuid,0);Expect(ACEGameAction::PutItemInContainer,Loot.GetData());
    FACEBinaryWriter Contain;Contain.WriteInt32(101);Contain.WriteInt32(S.PlayerGuid);Contain.WriteInt32(0);Contain.WriteUInt32(0);
    FACEBinaryReader ContainReader(Contain.GetData());S.HandleInventoryPutObjInContainer(ContainReader);
    TestEqual(TEXT("Server acknowledgement puts slag into Olthoi inventory"),S.WorldObjects[101].ContainerId,S.PlayerGuid);
    TestEqual(TEXT("Looting does not force Olthoi into peace"),S.PlayerVitals.CombatMode,int32(ACECombatMode::Magic));
    FACEBinaryWriter Give;Give.WriteInt32(102);Give.WriteInt32(101);Give.WriteInt32(1);
    S.SendGiveObjectRequest(102,101,1);Expect(ACEGameAction::GiveObjectRequest,Give.GetData());
    return !HasAnyErrors();
}
#endif
