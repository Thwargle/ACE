#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Engine/GameInstance.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Slate/WidgetRenderer.h"
#include "RenderingThread.h"
#include "ImageUtils.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"
#include "ACEDatSubsystem.h"
#include "ACEClientSubsystem.h"
#include "ACESession.h"
#include "ACEOpcodes.h"
#include "UI/ACEUICanvasWidget.h"
#include "UI/ACEUIElementManager.h"
#include "UI/ACEUILayoutResolver.h"
#include "UI/ACEUIResourceResolver.h"
#include "UI/ACEUIGameplayBinder.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEChessTest,"ACE.RetailParity.Chess",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACEChessTest::RunTest(const FString&)
{
    TGuardValue<uint64> FrameGuard(GFrameCounter,GFrameCounter);
    auto* GI=NewObject<UGameInstance>();
    auto* Client=NewObject<UACEClientSubsystem>(GI);Client->Session=MakeShared<FACESession>();
    auto& S=*Client->Session;S.PlayerGuid=1234;S.State=EACESessionState::InWorld;
    auto* Dat=NewObject<UACEDatSubsystem>(GI);
    if(!TestTrue(TEXT("Retail DAT opens"),Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call"))))return false;
    ON_SCOPE_EXIT {Dat->Deinitialize();};
    auto* Resources=NewObject<UACEUIResourceResolver>();Resources->Initialize(Dat);
    auto* Manager=NewObject<UACEUIElementManager>();Manager->Initialize();
    auto* Layout=NewObject<UACEUILayoutResolver>();Layout->Initialize(Dat,Manager);
    if(!TestTrue(TEXT("Retail gameplay layout loads"),Layout->LoadLayout(0x21000005)))return false;
    auto* Canvas=NewObject<UACEUICanvasWidget>();Canvas->Initialize();Canvas->InitializeCanvas(Manager);Canvas->SetResourceResolver(Resources);
    const auto Slate=Canvas->TakeWidget();
    auto* B=NewObject<UACEUIGameplayBinder>();B->Initialize(Client,Manager,Canvas,nullptr);Canvas->SetGameplayBinder(B);
    ON_SCOPE_EXIT {B->Shutdown();Canvas->SetGameplayBinder(nullptr);Manager->Shutdown();};
    S.OnChessEvent.AddLambda([&](const FACEChessEvent& Event){Client->OnChessEvent.Broadcast(Event);});
    S.OnVitalsUpdated.AddLambda([&](const FACEPlayerVitals& V){Client->OnVitalsUpdated.Broadcast(V);});
    FWidgetRenderer Renderer(true,true);
    auto* Target=FWidgetRenderer::CreateTargetFor(FVector2D(1600,900),TF_Bilinear,true);
    const FString Art=FPaths::ProjectSavedDir()/TEXT("Automation/Chess");IFileManager::Get().MakeDirectory(*Art,true);
    auto Draw=[&](const TCHAR* Name)
    {
        for(int I=0;I<4;++I){++GFrameCounter;Canvas->NativeTick(Canvas->GetCachedGeometry(),0);Renderer.DrawWidget(Target,Slate,FVector2D(1600,900),0);FlushRenderingCommands();}
        TArray<FColor> Pixels;Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels);
        TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(1600,900,Pixels,PNG);FFileHelper::SaveArrayToFile(PNG,*(Art/(FString(Name)+TEXT(".png"))));
    };
    Manager->ApplyEdgeAnchoredLayout(1600,900);
    auto* Sockets=ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    auto* Receiver=Sockets->CreateSocket(NAME_DGram,TEXT("Chess receiver"),false);
    if(!TestNotNull(TEXT("Loopback receiver"),Receiver))return false;
    ON_SCOPE_EXIT {Receiver->Close();Sockets->DestroySocket(Receiver);};
    auto Address=Sockets->CreateInternetAddr();bool Valid=false;Address->SetIp(TEXT("127.0.0.1"),Valid);Address->SetPort(0);
    if(!TestTrue(TEXT("Bind loopback"),Receiver->Bind(*Address)))return false;
    Receiver->GetAddress(*Address);Receiver->SetNonBlocking(true);S.ServerC2SAddr=Address;
    S.SocketC2S=Sockets->CreateSocket(NAME_DGram,TEXT("Chess sender"),false);
    if(!TestNotNull(TEXT("Loopback sender"),S.SocketC2S))return false;
    // This subsystem can tick between tests in packaged clients. Real session
    // sockets are nonblocking; an idle blocking fixture freezes PollSockets.
    S.SocketC2S->SetNonBlocking(true);
    ON_SCOPE_EXIT {S.SocketC2S->Close();Sockets->DestroySocket(S.SocketC2S);S.SocketC2S=nullptr;S.ServerC2SAddr.Reset();};
    const uint8 Seed[]={1,2,3,4};S.IssacClient=MakeUnique<FACEIsaac>(Seed);
    auto Expect=[&](uint32 Action,std::initializer_list<int32> Payload)
    {
        uint8 Bytes[4096];int32 Read=0;const double Deadline=FPlatformTime::Seconds()+1;
        while(!Receiver->Recv(Bytes,sizeof(Bytes),Read)&&FPlatformTime::Seconds()<Deadline)FPlatformProcess::Sleep(.001f);
        if(!TestTrue(TEXT("Action reaches wire"),Read>=48))return;
        FACEBinaryReader R(Bytes,Read);R.Skip(34);TestEqual(TEXT("Retail action queue"),R.ReadUInt16(),ACEQueue::WeenieQueue);
        TestEqual(TEXT("GameAction wrapper"),R.ReadUInt32(),0xF7B1u);R.ReadUInt32();
        TestEqual(TEXT("Retail action opcode"),R.ReadUInt32(),Action);
        for(int32 V:Payload)TestEqual(TEXT("Retail action payload"),R.ReadInt32(),V);
        TestEqual(TEXT("No extra action fields"),R.Remaining(),0);
    };
    auto Notice=[&](uint32 Type,std::initializer_list<int32> Fields)
    {
        FACEBinaryWriter W;for(int32 V:Fields)W.WriteInt32(V);
        FACEBinaryReader R(W.GetData());S.HandleChessGameEvent(Type,R);
        TestEqual(TEXT("Complete notice consumed"),R.Remaining(),0);
    };
    constexpr int32 Board=9876;
    FACEWorldObject BoardObject;BoardObject.Guid=Board;BoardObject.ItemType=int32(0x80000000u);
    S.WorldObjects.Add(Board,BoardObject);
    TestTrue(TEXT("World chessboard is interactable"),BoardObject.IsGameBoard());
    S.SendUseItem(Board);Expect(ACEGameAction::ChessJoin,{Board,-1});
    const uint32 JoinSequence=S.NextPacketSequence;
    S.SendUseItem(Board);TestEqual(TEXT("Repeated use does not join twice"),S.NextPacketSequence,JoinSequence);
    Notice(ACEGameEvent::ChessJoinGameResponse,{Board,0});
    TestTrue(TEXT("Join opens the real game panel"),B->ActivePanelPage==TEXT("MiniGamePanel_Field"));
    TestFalse(TEXT("Cannot move before start"),B->SubmitChessMove(4,1,4,3));
    Notice(ACEGameEvent::ChessStartGame,{Board,0});Draw(TEXT("White"));
    int32 Icons=0;for(auto Icon:B->ChessSquareIcons)if(Icon&&Icon->GetVisibility()!=ESlateVisibility::Collapsed)
    {++Icons;TestNotNull(TEXT("Native chess texture"),Icon->Background.GetResourceObject());}
    TestEqual(TEXT("All 32 pieces present"),Icons,32);
    TestFalse(TEXT("Chess has no pass button"),Manager->FindElementUnder(TEXT("MiniGamePanel_Field"),TEXT("MiniGame_Pass"))->bVisible);
    // Click e2/e4 in the rendered retail board, then spam another move before ACK.
    auto Click=[&](int32 X,int32 Y)
    {
        const auto El=Manager->FindElementUnder(TEXT("MiniGamePanel_Field"),TEXT("MiniGame_PieceListBox"));
        const int32 Col=B->ChessMyColor==1?7-X:X,Row=B->ChessMyColor==1?Y:7-Y;
        const FVector2D P=FVector2D(El->GetScreenOrigin())+FVector2D((Col+.5)*El->Width/8,(Row+.5)*El->Height/8);
        return B->TryHandleChessBoardClick(FVector2D(P.X*Canvas->GetLastScaleX(),P.Y*Canvas->GetLastScaleY()));
    };
    Click(4,1);Click(4,3);Expect(ACEGameAction::ChessMove,{4,1,4,3});
    TestEqual(TEXT("World and panel await server animation completion"),int32(B->ChessBoard[12]),1);
    TestFalse(TEXT("Only one move in flight"),B->SubmitChessMove(3,1,3,3));
    Notice(ACEGameEvent::ChessMoveResponse,{Board+1,1});TestEqual(TEXT("Foreign board ACK ignored"),B->ChessPendingFromX,4);
    Notice(ACEGameEvent::ChessMoveResponse,{Board,1});
    TestEqual(TEXT("Confirmed move updates board"),int32(B->ChessBoard[28]),1);
    TestFalse(TEXT("Opponent turn blocks local move"),B->SubmitChessMove(3,1,3,3));
    Notice(ACEGameEvent::ChessMoveResponse,{Board,1});TestEqual(TEXT("Duplicate ACK cannot change turn"),B->ChessTurnColor,1);
    Notice(ACEGameEvent::ChessOpponentTurn,{Board,1,5,5678,4,6,4,4});
    TestEqual(TEXT("Opponent pawn is at e5"),int32(B->ChessBoard[36]),-1);
    TestEqual(TEXT("Turn returns to white"),B->ChessTurnColor,0);
    TestTrue(TEXT("Move offered for server validation"),B->SubmitChessMove(4,3,4,4));Expect(ACEGameAction::ChessMove,{4,3,4,4});
    Notice(ACEGameEvent::ChessMoveResponse,{Board,-100});
    TestEqual(TEXT("Rejected move keeps pawn at e4"),int32(B->ChessBoard[28]),1);
    TestEqual(TEXT("Rejected move unlocks input"),B->ChessPendingFromX,-1);
    // Special moves mirror server results; legality stays server-side.
    FMemory::Memzero(B->ChessBoard);B->ChessBoard[4]=6;B->ChessBoard[7]=4;B->ApplyChessMove(4,0,6,0);
    TestEqual(TEXT("Castled rook"),int32(B->ChessBoard[5]),4);TestEqual(TEXT("Castled king"),int32(B->ChessBoard[6]),6);
    FMemory::Memzero(B->ChessBoard);B->ChessBoard[60]=-6;B->ChessBoard[56]=-4;B->ApplyChessMove(4,7,2,7);
    TestEqual(TEXT("Black queenside rook"),int32(B->ChessBoard[59]),-4);
    FMemory::Memzero(B->ChessBoard);B->ChessBoard[36]=1;B->ChessBoard[35]=-1;B->ApplyChessMove(4,4,3,5);
    TestEqual(TEXT("En passant removes passed pawn"),int32(B->ChessBoard[35]),0);
    B->ChessBoard[48]=1;B->ApplyChessMove(0,6,0,7);TestEqual(TEXT("Promotion is queen"),int32(B->ChessBoard[56]),5);
    B->ChessBoard[9]=-1;B->ApplyChessMove(1,1,1,0);TestEqual(TEXT("Black promotion"),int32(B->ChessBoard[1]),-5);
    B->OnElementActivated(Manager->FindElementByName(TEXT("MiniGame_Stalemate")));Expect(ACEGameAction::ChessStalemate,{1});
    B->OnElementActivated(Manager->FindElementByName(TEXT("MiniGame_Stalemate")));Expect(ACEGameAction::ChessStalemate,{0});
    B->OnElementActivated(Manager->FindElementByName(TEXT("MiniGame_Resign")));
    TestTrue(TEXT("Retail resignation confirmation"),B->bChessQuitConfirm);Draw(TEXT("Resign"));
    B->FinishServerConfirmation(false);TestTrue(TEXT("Cancel keeps match"),B->bChessActive);
    B->OnElementActivated(Manager->FindElementByName(TEXT("MiniGame_Resign")));B->FinishServerConfirmation(true);Expect(ACEGameAction::ChessQuit,{});
    TestTrue(TEXT("Result remains server-authoritative"),B->bChessActive);
    Notice(ACEGameEvent::ChessGameOver,{Board,1});TestFalse(TEXT("Server result ends match"),B->bChessActive);
    TestEqual(TEXT("Server result releases board"),S.ChessBoardGuid,0);
    for(int32 Rank:{1400,1425,1398,0})
    {
        FACEBinaryWriter W;W.WriteUInt8(1);W.WriteUInt32(181);W.WriteInt32(Rank);
        FACEBinaryReader R(W.GetData());S.HandlePrivateUpdatePropertyInt(R);
        TestEqual(TEXT("Server rank replaces previous value, up or down"),S.PlayerVitals.ChessRank,Rank);
        TestEqual(TEXT("Live panel gets rank without relog"),B->LastVitals.ChessRank,Rank);
        TestTrue(TEXT("Character information prints live rank"),B->BuildCharacterInformation().Contains(FText::AsNumber(Rank).ToString()));
    }
    // The same input coordinates work when seated on the opposite side.
    Notice(ACEGameEvent::ChessJoinGameResponse,{Board,1});Notice(ACEGameEvent::ChessStartGame,{Board,0});
    Notice(ACEGameEvent::ChessOpponentTurn,{Board,0,5,5678,4,1,4,3});Draw(TEXT("Black"));
    Click(4,6);Click(4,4);Expect(ACEGameAction::ChessMove,{4,6,4,4});Notice(ACEGameEvent::ChessMoveResponse,{Board,1});
    Notice(ACEGameEvent::ChessGameOver,{Board,-1});TestFalse(TEXT("Draw releases game"),B->bChessActive);
    Notice(ACEGameEvent::ChessJoinGameResponse,{Board,0});Notice(ACEGameEvent::ChessGameOver,{Board,-2});
    TestFalse(TEXT("Cancelled waiting game releases match"),B->bChessActive);
    // Truncated packets must not fabricate victories or advance turns.
    int32 Received=0;S.OnChessEvent.AddLambda([&](const FACEChessEvent&){++Received;});
    for(uint32 Type:{ACEGameEvent::ChessJoinGameResponse,ACEGameEvent::ChessStartGame,ACEGameEvent::ChessMoveResponse,ACEGameEvent::ChessGameOver,ACEGameEvent::ChessOpponentStalemate,ACEGameEvent::ChessOpponentTurn})
    {
        FACEBinaryWriter W;W.WriteInt32(Board);W.WriteInt32(1);
        if(Type==ACEGameEvent::ChessOpponentStalemate)W.WriteInt32(1);
        if(Type==ACEGameEvent::ChessOpponentTurn)for(int32 V:{5,5678,4,6,4,4})W.WriteInt32(V);
        for(int32 Cut=0;Cut<W.GetData().Num();++Cut)
        {auto Bytes=W.GetData();Bytes.SetNum(Cut);FACEBinaryReader R(Bytes);S.HandleChessGameEvent(Type,R);}
    }
    TestEqual(TEXT("No partial chess event published"),Received,0);
    return !HasAnyErrors();
}
#endif
