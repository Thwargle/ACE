#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Engine/Texture2D.h"
#include "GameFramework/PlayerInput.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Components/CanvasPanelSlot.h"
#include "ACEClientSubsystem.h"
#include "ACEDatSubsystem.h"
#include "ACEMouseCursorWidget.h"
#include "ACEHoverTooltipWidget.h"
#include "ACEPlayerController.h"
#include "ACESession.h"
#include "ACEOpcodes.h"
#include "UI/ACEUIResourceResolver.h"
#include "UI/ACEUICanvasWidget.h"
#include "UI/ACEUIGameplayBinder.h"
#include "Protocol/ACEBinaryReader.h"
#include "Protocol/ACEBinaryWriter.h"
#include "Protocol/ACEIsaac.h"
#include "SocketSubsystem.h"
#include "Sockets.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACERetailCursorTest,"ACE.RetailParity.MouseCursors",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FACERetailCursorTest::RunTest(const FString&)
{
    auto* W=UWorld::CreateWorld(EWorldType::Game,false);
    auto& Context=GEngine->CreateNewWorldContext(EWorldType::Game);
    auto* GI=NewObject<UGameInstance>(GEngine);W->SetGameInstance(GI);
    Context.OwningGameInstance=GI;Context.SetCurrentWorld(W);GI->OnWorldChanged(nullptr,W);GI->Init();
    ON_SCOPE_EXIT {GI->Shutdown();GEngine->DestroyWorldContext(W);W->DestroyWorld(false);};
    auto* C=GI->GetSubsystem<UACEClientSubsystem>();
    C->Session=MakeShared<FACESession>();auto& S=*C->Session;
    S.State=EACESessionState::InWorld;S.PlayerGuid=1234;S.PlayerVitals.CombatMode=1;
    auto* Dat=GI->GetSubsystem<UACEDatSubsystem>();
    if (!TestTrue(TEXT("Retail DAT opens"),Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call")))) return false;
    auto* PC=W->SpawnActor<AACEPlayerController>();PC->Client=C;PC->PlayerInput=NewObject<UPlayerInput>(PC);
    AddExpectedMessagePlain(TEXT("does not have a valid PlayerInput object"),ELogVerbosity::Warning);
    auto* LP=NewObject<ULocalPlayer>(GEngine);LP->PlayerController=PC;
    GI->AddLocalPlayer(LP,FPlatformUserId::CreateFromInternalId(0));PC->SetPlayer(LP);W->AddController(PC);
    auto* Canvas=NewObject<UACEUICanvasWidget>();Canvas->Initialize();
    auto* Binder=NewObject<UACEUIGameplayBinder>();Binder->Client=C;Binder->Canvas=Canvas;Binder->PlayerController=PC;
    PC->DatGameplayBinder=Binder;
    ON_SCOPE_EXIT {PC->DatGameplayBinder=nullptr;Binder->Client=nullptr;PC->Client=nullptr;};
    PC->EnsureRetailMouseCursor();
    if (!TestNotNull(TEXT("Real retail cursor is installed"),PC->MouseCursorWidget.Get())) return false;
    auto CursorSlate=PC->MouseCursorWidget->TakeWidget();
    auto* Art=Cast<UImage>(PC->MouseCursorWidget->WidgetTree->FindWidget(TEXT("CursorImage")));
    if (!TestNotNull(TEXT("Software cursor has an image"),Art)) return false;
    PC->MouseCursorWidget->SetCursorScale(1.f);
    auto* Resources=C->GetUIResourceResolver();
    auto CheckArt=[&](const TCHAR* Label,int32 Enum,int32 Hotspot=0)
    {
        auto* Expected=Resources->ResolveTexture(Resources->ResolveCursorId(Enum));
        TestNotNull(FString(Label)+TEXT(" DAT surface"),Expected);
        TestTrue(Label,Expected && Art->GetBrush().GetResourceObject()==Expected);
        const auto* Slot=Cast<UCanvasPanelSlot>(Art->Slot);
        TestTrue(FString(Label)+TEXT(" hotspot"),Slot && (Slot->GetPosition()-FVector2D(Art->GetBrush().ImageSize)).Equals(FVector2D(-Hotspot,-Hotspot),.01));
    };
    // Reference: ClientUISystem::UpdateCursorState, mapper 2500000F.
    PC->ApplyRetailMouseCursor(false);CheckArt(TEXT("Peace cursor"),1);
    PC->ApplyRetailMouseCursor(true);CheckArt(TEXT("Peace hover cursor"),2);
    S.PlayerVitals.CombatMode=2;PC->ApplyRetailMouseCursor(false);CheckArt(TEXT("Melee cursor"),3);
    PC->ApplyRetailMouseCursor(true);CheckArt(TEXT("Melee hover cursor"),4);
    S.PlayerVitals.CombatMode=4;PC->ApplyRetailMouseCursor(false);CheckArt(TEXT("Missile cursor"),3);
    S.PlayerVitals.CombatMode=8;PC->ApplyRetailMouseCursor(false);CheckArt(TEXT("Magic cursor"),5);
    PC->ApplyRetailMouseCursor(true);CheckArt(TEXT("Magic hover cursor"),6);
    PC->SetObjectCursorTargeting(1);CheckArt(TEXT("Use cursor overrides magic"),12,14);
    PC->ApplyRetailMouseCursor(true);CheckArt(TEXT("Use hover"),13,14);
    PC->SetObjectCursorTargeting(2);CheckArt(TEXT("Examine cursor"),10);
    PC->ApplyRetailMouseCursor(true);CheckArt(TEXT("Examine hover"),11);
    PC->SetPendingUseTargeting(true);CheckArt(TEXT("Use-with cursor"),39,14);
    PC->ApplyRetailMouseCursor(true,true);CheckArt(TEXT("Compatible target"),40,14);
    PC->ApplyRetailMouseCursor(true,false);CheckArt(TEXT("Incompatible target"),41,14);
    PC->SetPendingUseTargeting(false);S.PlayerVitals.CombatMode=1;
    // The fixture has no game viewport; keep its tooltip local too.
    PC->HoverTooltipWidget=CreateWidget<UACEHoverTooltipWidget>(GI,UACEHoverTooltipWidget::StaticClass());

    auto* Sockets=ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    auto* Receiver=Sockets->CreateSocket(NAME_DGram,TEXT("Cursor loopback receiver"),false);
    if (!TestNotNull(TEXT("Loopback receiver"),Receiver)) return false;
    ON_SCOPE_EXIT {S.State=EACESessionState::Disconnected;Receiver->Close();Sockets->DestroySocket(Receiver);};
    const auto Address=Sockets->CreateInternetAddr();bool Valid=false;
    Address->SetIp(TEXT("127.0.0.1"),Valid);Address->SetPort(0);
    if (!TestTrue(TEXT("Bind loopback"),Receiver->Bind(*Address))) return false;
    Receiver->GetAddress(*Address);
    S.SocketC2S=Sockets->CreateSocket(NAME_DGram,TEXT("Cursor loopback sender"),false);
    S.ServerC2SAddr=Address;S.IssacClient=MakeUnique<FACEIsaac>(123u);
    auto Done=[&](uint32 Error=0){FACEBinaryWriter B;B.WriteUInt32(Error);FACEBinaryReader R(B.GetData());S.HandleUseDone(R);};
    auto Appraise=[&](uint32 Guid){FACEBinaryWriter B;B.WriteUInt32(Guid);B.WriteUInt32(0);B.WriteUInt32(0);FACEBinaryReader R(B.GetData());S.HandleIdentifyObjectResponse(R);};
    auto Event=[&](uint32 Type){FACEBinaryWriter B;B.WriteUInt32(S.PlayerGuid);B.WriteUInt32(1);B.WriteUInt32(Type);B.WriteUInt32(0);FACEBinaryReader R(B.GetData());S.HandleGameEvent(R);};

    S.SendUseItem(200);TestTrue(TEXT("Use awaits server completion"),S.IsCursorBusy());
    PC->PollObjectHover();CheckArt(TEXT("Stationary pointer picks up use hourglass"),14);
    PC->ApplyRetailMouseCursor(true);CheckArt(TEXT("Busy hover uses retail alternate hourglass"),15);
    S.SendIdentifyObject(300);Done();TestTrue(TEXT("UseDone cannot finish an overlapping appraisal"),S.IsCursorBusy());
    S.SendIdentifyObject(400,true);Appraise(400);TestTrue(TEXT("Background appraisal cannot finish the user's appraisal"),S.IsCursorBusy());
    Appraise(300);PC->PollObjectHover();CheckArt(TEXT("Stationary pointer restores arrow after all replies"),1);
    TestFalse(TEXT("Failed appraisal also ends busy state"),S.IsCursorBusy());
    Done();TestFalse(TEXT("Unsolicited UseDone cannot underflow busy state"),S.IsCursorBusy());
    S.SendIdentifyObject(400,true);TestFalse(TEXT("Background-only appraisals do not flash an hourglass"),S.IsCursorBusy());
    S.SendIdentifyObject(300);S.SendIdentifyObject(301);Appraise(300);
    TestTrue(TEXT("Replaced manual appraisal waits for the new object"),S.IsCursorBusy());
    Appraise(301);TestFalse(TEXT("Latest appraisal completes once"),S.IsCursorBusy());
    S.SendIdentifyObject(300);S.CancelCursorAppraisal();TestFalse(TEXT("Examining a spell cancels the object appraisal wait"),S.IsCursorBusy());

    S.SendCastSpell(1,200);TestTrue(TEXT("Targeted cast shows busy"),S.IsCursorBusy());
    TestFalse(TEXT("Cursor tracking does not change gameplay use gate"),S.IsUseBusy());
    PC->SetPendingUseTargeting(true);CheckArt(TEXT("Busy takes priority over targeting"),14);
    Done();PC->ApplyRetailMouseCursor(false);CheckArt(TEXT("Target cursor restored after cast completion"),39,14);
    PC->SetPendingUseTargeting(false);
    S.SendCastSpell(1);TestTrue(TEXT("Untargeted cast shows busy"),S.IsCursorBusy());Done();
    S.SendUseWithTarget(200,300);TestTrue(TEXT("Item on target shows busy"),S.IsCursorBusy());Done();
    for (uint32 Action:{ACEGameAction::Buy,ACEGameAction::Sell})
    {
        S.SendGameAction(Action,{},ACEQueue::WeenieQueue);
        TestTrue(TEXT("Vendor transaction awaits UseDone"),S.IsCursorBusy());Done();
        TestFalse(TEXT("Vendor completion clears hourglass"),S.IsCursorBusy());
    }
    Event(ACEGameEvent::CombatCommenceAttack);TestTrue(TEXT("Server attack commencement shows hourglass"),S.IsCursorBusy());
    S.SendUseItem(200);Event(ACEGameEvent::AttackDone);
    TestTrue(TEXT("AttackDone does not clear a pending item use"),S.IsCursorBusy());Done();
    TestFalse(TEXT("Attack and use completion restore normal cursor"),S.IsCursorBusy());
    S.SendAllegianceUpdateRequest(true);TestTrue(TEXT("Allegiance panel waits for update"),S.IsCursorBusy());
    { FACEBinaryWriter B;B.WriteUInt32(0);B.WriteUInt32(0);B.WriteUInt32(0);FACEBinaryReader R(B.GetData());S.HandleAllegianceUpdate(R); }
    TestFalse(TEXT("Allegiance update ends hourglass"),S.IsCursorBusy());
    S.SendAllegianceUpdateRequest(true);S.SendAllegianceUpdateRequest(false);
    TestFalse(TEXT("Closing allegiance panel ends its wait"),S.IsCursorBusy());

    S.SendUseItem(200);S.CancelPendingUse(TEXT("cursor regression"));
    TestFalse(TEXT("Cancelled item use restores cursor"),S.IsCursorBusy());
    S.SendCastSpell(1);S.CursorUseWaits[0].Value=FPlatformTime::Seconds()-1.;
    TestFalse(TEXT("Missing reply cannot leave a permanent hourglass"),S.IsCursorBusy());S.ResetCursorWaits();
    S.SendUseItem(200);S.SendIdentifyObject(300);S.ClearWorldState();
    TestFalse(TEXT("Leaving world clears every pending cursor operation"),S.IsCursorBusy());
    S.State=EACESessionState::InWorld;S.PlayerGuid=1234;

    C->SelectObject(0);Binder->UseSelectedObject();CheckArt(TEXT("Use with no selection enters retail targeting"),12,14);
    Binder->HandleEscape();CheckArt(TEXT("Escape cancels targeting"),1);
    Binder->ExamineSelectedObject();CheckArt(TEXT("Examine with no selection enters retail targeting"),10);
    TestTrue(TEXT("Target click completes examine mode"),Binder->TryCompletePendingUseWithTarget(300));
    TestEqual(TEXT("Examine targets the clicked object"),S.CursorAppraisalGuid,300);
    PC->PollObjectHover();CheckArt(TEXT("Examine target click starts hourglass"),14);
    Appraise(300);PC->PollObjectHover();CheckArt(TEXT("Appraisal response restores ordinary pointer"),1);
    PC->SetObjectCursorTargeting(1);S.State=EACESessionState::Disconnected;PC->PollObjectHover();
    TestEqual(TEXT("Disconnect clears stale target mode"),PC->GetObjectCursorTargeting(),0);
    return true;
}
#endif
