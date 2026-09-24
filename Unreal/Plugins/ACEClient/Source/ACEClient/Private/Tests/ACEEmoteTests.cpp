#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "ACEDatSubsystem.h"
#include "ACEClientSubsystem.h"
#include "ACEPlayerController.h"
#include "ACESession.h"
#include "ACEMotionCommandNames.generated.h"
#include "UI/ACEUIGameplayBinder.h"
#include "UI/ACEUICanvasWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/EditableTextBox.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEEmoteTest,"ACE.RetailParity.TargetedEmotes",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACEEmoteTest::RunTest(const FString&)
{
 const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
  .CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
 auto& Context=GEngine->CreateNewWorldContext(EWorldType::Game);
 auto* GI=NewObject<UGameInstance>(GEngine);World->SetGameInstance(GI);
 Context.OwningGameInstance=GI;Context.SetCurrentWorld(World);GI->Init();
 ON_SCOPE_EXIT {GI->Shutdown();GEngine->DestroyWorldContext(World);World->DestroyWorld(false);};
 auto* Dat=GI->GetSubsystem<UACEDatSubsystem>();
 if(!TestTrue(TEXT("Retail pose table opens"),Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call"))))return false;
 auto* Client=GI->GetSubsystem<UACEClientSubsystem>();auto S=Client->GetSession();
 S->State=EACESessionState::InWorld;S->PlayerGuid=0x50000100;
 auto* Sockets=ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
 auto* Receiver=Sockets->CreateSocket(NAME_DGram,TEXT("Emote test receiver"),false);
 auto Addr=Sockets->CreateInternetAddr();bool Valid=false;Addr->SetIp(TEXT("127.0.0.1"),Valid);Addr->SetPort(0);
 ON_SCOPE_EXIT {if(Receiver){Receiver->Close();Sockets->DestroySocket(Receiver);} S->Disconnect();};
 if(!TestTrue(TEXT("Isolated emote receiver binds"),Receiver && Receiver->Bind(*Addr)))return false;
 Receiver->GetAddress(*Addr);
 S->SocketC2S=Sockets->CreateSocket(NAME_DGram,TEXT("Emote test sender"),false);
 S->ServerC2SAddr=Addr;S->IssacClient=MakeUnique<FACEIsaac>(123u);
 FACEWorldObject Self;Self.Guid=S->PlayerGuid;Self.Name=TEXT("Thwargle");Self.bIsPlayer=true;
 FACEWorldObject Target;Target.Guid=0x50000200;Target.Name=TEXT("Other Name");Target.bIsPlayer=true;
 S->WorldObjects.Add(Self.Guid,Self);S->WorldObjects.Add(Target.Guid,Target);
 auto* PC=World->SpawnActor<AACEPlayerController>();
 auto* Binder=NewObject<UACEUIGameplayBinder>();Binder->Client=Client;Binder->PlayerController=PC;
 Binder->ChatEntry=NewObject<UEditableTextBox>();Binder->Canvas=NewObject<UACEUICanvasWidget>(GI);
 Binder->Canvas->WidgetTree=NewObject<UWidgetTree>(Binder->Canvas);Binder->ChatLog=NewObject<UScrollBox>();
 Binder->ChatRowScales[0]=FVector2D(1,1);Binder->ChatRowWidths[0]=500;
 Binder->LastVitals.bValid=true;Binder->LastVitals.Gender=2;
 auto LocalLine=[&]() {auto* Row=Cast<UTextBlock>(Binder->ChatLog->GetChildAt(Binder->ChatLog->GetChildrenCount()-1));return Row?Row->GetText().ToString():FString();};
 auto PacketTexts=[&]()
 {
  TArray<FString> Texts;
  for(const auto& Pair:S->CachedC2SPackets)
  {FACEBinaryReader R(Pair.Value.Payload);if(!R.CanRead(28))continue;R.Skip(24);
   if(R.ReadUInt32()==0x1e1u)Texts.Add(R.ReadString16L());}
  return Texts;
 };
 auto Submit=[&](const FString& Text){S->CachedC2SPackets.Reset();Binder->TrySendChatFromEntry(&Text);};
 S->SelectObject(Target.Guid);
 Submit(TEXT("*wave*"));
 auto Texts=PacketTexts();TestEqual(TEXT("A pose sends exactly one chat fragment"),Texts.Num(),1);
 if(Texts.Num())TestEqual(TEXT("Observers receive the target in normal SoulEmote text"),Texts[0],FString(TEXT("waves at Other Name.")));
 TestEqual(TEXT("Local first-person text addresses the same target"),LocalLine(),FString(TEXT("You wave at Other Name.")));
 Binder->HandleChatMessage(TEXT("waves at Other Name."),TEXT("Thwargle"),ACEChatMessageType::Emote);
 TestEqual(TEXT("Retail observer formatting prefixes the sender once"),LocalLine(),FString(TEXT("Thwargle waves at Other Name.")));
 struct FCase {const TCHAR* Command;const TCHAR* Join;};
 const FCase Cases[]={{TEXT("Wave"),TEXT(" at ")},{TEXT("WaveHigh"),TEXT(" at ")},
  {TEXT("WaveLow"),TEXT(" at ")},{TEXT("WaveState"),TEXT(" at ")},{TEXT("BowDeepState"),TEXT(" to ")},
  {TEXT("SaluteState"),TEXT(" ")},{TEXT("CurtseyState"),TEXT(" to ")},{TEXT("Nod"),TEXT(" at ")},
  {TEXT("ShakeHead"),TEXT(" at ")},{TEXT("ShakeFist"),TEXT(" at ")},{TEXT("ShakeFistState"),TEXT(" at ")},
  {TEXT("Shrug"),TEXT(" at ")},{TEXT("BlowKiss"),TEXT(" to ")},{TEXT("PointState"),TEXT(" at ")},
  {TEXT("PointLeft"),TEXT(" at ")},{TEXT("PointRight"),TEXT(" at ")},{TEXT("PointDown"),TEXT(" at ")},
  {TEXT("Mock"),TEXT(" at ")},{TEXT("Shoo"),TEXT(" at ")},{TEXT("Beckon"),TEXT(" to ")},
  {TEXT("BeSeeingYou"),TEXT(" at ")}};
 TArray<FString> Keys;Dat->GetChatPoseKeys(Keys);
 for(const auto& Case:Cases)
 {
  FString Alias,Command,My,Other;
  for(const auto& Key:Keys){Dat->TryGetChatPose(Key,Command,My,Other);if(Command.Equals(Case.Command,ESearchCase::IgnoreCase)){Alias=Key;break;}}
  if(!TestFalse(FString::Printf(TEXT("Retail has aliases for %s"),Case.Command),Alias.IsEmpty()))continue;
  Submit(TEXT("*")+Alias+TEXT("*"));Texts=PacketTexts();
  TestEqual(TEXT("Each social gesture transmits once"),Texts.Num(),1);
  if(Texts.Num())TestTrue(TEXT("Directed gesture includes a grammatically joined target"),Texts[0].Contains(FString(Case.Join)+Target.Name));
  const auto Typed=LocalLine();uint32 Motion=0;ACEMotionName::TryResolve(Command.ToLower(),Motion);
  S->CachedC2SPackets.Reset();Binder->PlayEmoteHotkey(Motion,true);
  TestEqual(TEXT("Keybind and typed aliases have the same first-person text"),LocalLine(),Typed);
  TestTrue(TEXT("Keybind and typed aliases have the same wire text"),PacketTexts()==Texts);
 }
 S->CachedC2SPackets.Reset();Binder->PlayEmoteHotkey(0x1300007du,false);
 TestEqual(TEXT("Actual retail BowDeep keybind uses the held gesture's DAT text"),LocalLine(),FString(TEXT("You bow deeply to Other Name.")));
 TestEqual(TEXT("One-shot bow keybind still sends exactly one message"),PacketTexts().Num(),1);
 S->SelectObject(0);Submit(TEXT("*wave*"));TestEqual(TEXT("No selection preserves retail wording"),LocalLine(),FString(TEXT("You wave.")));
 S->SelectObject(Self.Guid);Submit(TEXT("*wave*"));TestEqual(TEXT("Self selection does not generate waves at yourself"),LocalLine(),FString(TEXT("You wave.")));
 S->SelectObject(Target.Guid);S->WorldObjects.Remove(Target.Guid);Submit(TEXT("*wave*"));
 TestEqual(TEXT("Unloaded selection cannot leak an old target name"),LocalLine(),FString(TEXT("You wave.")));
 S->WorldObjects.Add(Target.Guid,Target);S->SelectObject(Target.Guid);Submit(TEXT("*sit*"));
 TestFalse(TEXT("Noninteractive poses never acquire a target suffix"),LocalLine().Contains(Target.Name));
 Submit(TEXT("@sm waves at a friend."));TestEqual(TEXT("Explicit soul emote has a local message despite suppressed echo"),LocalLine(),FString(TEXT("Thwargle waves at a friend.")));
 Submit(TEXT("@e looks toward the mountains"));TestEqual(TEXT("Custom text emotes keep the standard Emote protocol"),PacketTexts().Num(),0);
 // No live server or account is contacted by these packet tests.
 PC->Destroy();return !HasAnyErrors();
}
#endif
