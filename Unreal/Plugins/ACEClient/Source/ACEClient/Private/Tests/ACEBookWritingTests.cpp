#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "ACETestDatDirectory.h"
#include "ACEClientSubsystem.h"
#include "ACESession.h"
#include "ACEDatSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Slate/WidgetRenderer.h"
#include "RenderingThread.h"
#include "ImageUtils.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "Common/UdpSocketBuilder.h"
#include "Components/ScrollBox.h"
#include "UI/ACEUICanvasWidget.h"
#include "UI/ACEUIElementManager.h"
#include "UI/ACEUILayoutResolver.h"
#include "UI/ACEUIResourceResolver.h"
#include "UI/ACEUIGameplayBinder.h"
#include "UI/ACERetailTextEntry.h"
#include "UI/ACERetailTextBlock.h"
#include "Widgets/Layout/SDPIScaler.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEBookWritingTest,"ACE.Network.BookWriting",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)

bool FACEBookWritingTest::RunTest(const FString&)
{
    TGuardValue<uint64> FrameGuard(GFrameCounter,GFrameCounter);
    auto* GI=NewObject<UGameInstance>();
    auto* Client=NewObject<UACEClientSubsystem>(GI);Client->Session=MakeShared<FACESession>();
    auto& S=*Client->Session;S.PlayerGuid=100;S.State=EACESessionState::InWorld;
    S.OnBookChanged.AddLambda([&](){Client->OnBookChanged.Broadcast();});
    auto* Sockets=ISocketSubsystem::Get();
    auto* Receiver=FUdpSocketBuilder(TEXT("Book writing receiver")).AsNonBlocking().BoundToPort(0).Build();
    ON_SCOPE_EXIT {S.State=EACESessionState::Disconnected;if(Receiver){Receiver->Close();Sockets->DestroySocket(Receiver);}};
    if(!TestNotNull(TEXT("Loopback receiver"),Receiver))return false;
    S.SocketC2S=FUdpSocketBuilder(TEXT("Book writing sender")).AsNonBlocking().BoundToPort(0).Build();
    if(!TestNotNull(TEXT("Loopback sender"),S.SocketC2S))return false;
    S.ServerC2SAddr=Sockets->CreateInternetAddr();S.ServerC2SAddr->SetIp(0x7f000001);S.ServerC2SAddr->SetPort(Receiver->GetPortNo());
    S.IssacClient=MakeUnique<FACEIsaac>(123u);
    auto Event=[&](uint32 Opcode,const FACEBinaryWriter& Body,int32 Length=INDEX_NONE)
    {
        FACEBinaryWriter W;W.WriteUInt32(100);W.WriteUInt32(1);W.WriteUInt32(Opcode);
        W.WriteBytes(Body.GetData().GetData(),Length==INDEX_NONE?Body.Num():Length);
        FACEBinaryReader R(W.GetData());S.HandleGameEvent(R);
    };
    // CM_Writing / PageData wire fixtures; author account is deliberately never stored in UI state.
    auto Page=[](FACEBinaryWriter& W,int32 Author,bool Shared,const FString& Text,bool Included=true,bool Modern=true)
    {
        W.WriteInt32(Author);W.WriteString16L(TEXT("Author"));W.WriteString16L(TEXT("private-account"));
        if(Modern){W.WriteUInt32(0xFFFF0002);W.WriteUInt32(Included);W.WriteUInt32(Shared);}
        else W.WriteUInt32(Included);
        if(Included)W.WriteString16L(Text);
    };
    auto Book=[&](int32 Author,bool Shared,const TArray<FString>& Pages,int32 Capacity=4,bool Modern=true)
    {
        FACEBinaryWriter W;W.WriteInt32(500);W.WriteInt32(Capacity);W.WriteInt32(Capacity);W.WriteInt32(1000);W.WriteInt32(Pages.Num());
        for(const auto& Text:Pages)Page(W,Author,Shared,Text,true,Modern);
        W.WriteString16L(TEXT(""));W.WriteUInt32(0);W.WriteString16L(TEXT(""));return W;
    };
    auto Open=[&](int32 Author,bool Shared,const TArray<FString>& Pages,int32 Capacity=4,bool Modern=true)
    {
        S.ResetBookEditing();S.Book={};Event(0xB4,Book(Author,Shared,Pages,Capacity,Modern));
    };
    auto Reply=[&](int32 Guid,int32 Index,bool Success)
    {FACEBinaryWriter W;W.WriteInt32(Guid);W.WriteInt32(Index);W.WriteUInt32(Success);return W;};
    auto Requests=[&](uint32 Action,int32 Index=INDEX_NONE,const FString& Text=FString())
    {
        int32 Count=0;
        for(const auto& P:S.CachedC2SPackets)
        {
            FACEBinaryReader R(P.Value.Payload);if(!R.CanRead(32))continue;
            R.Skip(14);const uint16 Queue=R.ReadUInt16();if(R.ReadUInt32()!=ACEOpcode::GameAction)continue;R.ReadUInt32();
            if(R.ReadUInt32()!=Action || R.ReadInt32()!=500)continue;
            if(Action!=0xAC && Action!=0xAA){if(!R.CanRead(4)||R.ReadInt32()!=Index)continue;}
            if(Action==0xAB && R.ReadString16L()!=Text)continue;
            TestEqual(TEXT("Writing uses retail WeenieQueue"),Queue,uint16(ACEQueue::WeenieQueue));
            TestEqual(TEXT("Writing packet has exactly the retail fields"),R.Remaining(),0);++Count;
        }
        return Count;
    };
    Open(200,false,{TEXT("Other author"),TEXT("Next")},4,false);
    TestTrue(TEXT("Legacy page author retained without inventing shared permission"),S.Book.PageAuthors[0].Guid==200
        && !S.Book.PageAuthors[0].bIgnoreAuthor && !S.CanEditBookPage());
    TestFalse(TEXT("Foreign page rejects edit"),S.SetBookPageText(TEXT("Overwrite")));
    S.TurnBookPage(1);TestEqual(TEXT("Read-only pages still navigate"),S.Book.CurrentPage,1);
    TestEqual(TEXT("Reading never sends a modification"),Requests(0xAB,0,TEXT("Overwrite")),0);
    S.TurnBookPage(2);TestEqual(TEXT("New page allowed after another author"),Requests(0xAC),1);
    Event(0xB6,Reply(500,2,true));TestTrue(TEXT("Creation waits for authoritative page metadata"),S.Book.NumPages==3 && !S.CanEditBookPage() && Requests(0xAA)==1);
    // A normal snapshot can contain pages by different authors.
    FACEBinaryWriter Mixed;Mixed.WriteInt32(500);Mixed.WriteInt32(4);Mixed.WriteInt32(4);Mixed.WriteInt32(1000);Mixed.WriteInt32(3);
    Page(Mixed,200,false,TEXT("Other author"));Page(Mixed,200,false,TEXT("Next"));Page(Mixed,100,false,TEXT(""));
    Mixed.WriteString16L(TEXT(""));Mixed.WriteUInt32(0);Mixed.WriteString16L(TEXT(""));Event(0xB4,Mixed);
    TestTrue(TEXT("Created page editable after snapshot, with selection preserved"),S.Book.CurrentPage==2 && S.CanEditBookPage());
    S.TurnBookPage(3);TestEqual(TEXT("Cannot append repeated empty last pages"),Requests(0xAC),1);

    Open(100,false,{TEXT("First"),TEXT("Second")});
    TestFalse(TEXT("Page length limit rejects oversized paste"),S.SetBookPageText(FString::ChrN(1001,TEXT('a'))));
    TestTrue(TEXT("Own page accepts multiline writing"),S.SetBookPageText(TEXT("Edited\r\npage")));
    Event(0xB4,Book(100,false,{TEXT("First"),TEXT("Second")}));
    TestEqual(TEXT("Repeated book-open response preserves unsaved text"),S.GetBookPageText(),FString(TEXT("Edited\npage")));
    S.TurnBookPage(1);S.TurnBookPage(1);
    Event(0xB4,Book(100,false,{TEXT("First"),TEXT("Second")}));
    TestTrue(TEXT("Turning saves once and keeps page until acknowledged"),Requests(0xAB,0,TEXT("Edited\npage"))==1
        && S.IsBookWritePending() && S.Book.CurrentPage==0 && S.Book.Pages[0]==TEXT("First") && !S.CanEditBookPage());
    const auto Modified=Reply(500,0,true);
    for(int32 Size=0;Size<Modified.Num();++Size)
    {
        Event(0xB5,Modified,Size);
        if(!TestTrue(TEXT("Partial acknowledgements cannot release pending edit"),S.IsBookWritePending() && S.GetBookPageText()==TEXT("Edited\npage")))return false;
    }
    Event(0xB5,Reply(501,0,true));Event(0xB5,Reply(500,1,true));Event(0xB7,Modified);
    TestTrue(TEXT("Wrong book, page and operation replies ignored"),S.IsBookWritePending());
    Event(0xB5,Reply(500,0,false));
    TestTrue(TEXT("Rejected write keeps original cache and editable draft"),S.CanEditBookPage() && S.Book.Pages[0]==TEXT("First") && S.GetBookPageText()==TEXT("Edited\npage"));
    S.TurnBookPage(1);Event(0xB5,Modified);
    TestTrue(TEXT("Successful retry commits and turns page"),S.Book.CurrentPage==1 && S.Book.Pages[0]==TEXT("Edited\npage") && S.CanEditBookPage());
    Event(0xB5,Modified);TestEqual(TEXT("Duplicate completion cannot move the page"),S.Book.CurrentPage,1);

    Open(100,false,{TEXT("First"),TEXT("Second"),TEXT("Third")});
    S.SetBookPageText(TEXT(" \n "));S.TurnBookPage(1);
    TestEqual(TEXT("Own blank page sends delete"),Requests(0xAD,0),1);
    Event(0xB7,Reply(500,0,true));
    TestTrue(TEXT("Delete shifts text, permissions and next-page index together"),S.Book.NumPages==2 && S.Book.CurrentPage==0
        && S.Book.Pages[0]==TEXT("Second") && S.Book.PageAuthors.Num()==2 && S.Book.PageTextIncluded.Num()==2);
    Open(200,true,{TEXT("Shared"),TEXT("Second")});
    S.SetBookPageText(TEXT(""));S.TurnBookPage(1);
    TestTrue(TEXT("Another author's shared blank page is modified, not deleted"),Requests(0xAB,0,TEXT(""))==1 && Requests(0xAD,0)==1);
    Event(0xB5,Modified);TestEqual(TEXT("Shared writing records new author"),S.Book.PageAuthors[0].Guid,100);

    Open(100,false,{TEXT("Old")});S.SetBookPageText(TEXT("Closed book text"));S.ClearBook();S.ClearBook();
    TestEqual(TEXT("Close saves exactly once"),Requests(0xAB,0,TEXT("Closed book text")),1);
    Event(0xB5,Modified);TestFalse(TEXT("Closed write completion never reopens reader"),S.Book.bOpen);
    Open(100,false,{TEXT("")});S.ClearBook();TestEqual(TEXT("Closing an own blank page deletes it"),Requests(0xAD,0),2);

    Open(100,false,{TEXT("First"),TEXT("Second")});S.SetBookPageText(TEXT("Slow reply"));S.TurnBookPage(1);
    S.BookWriteRequestedAt-=11.;S.CheckBookWriteTimeout();S.TurnBookPage(1);
    TestTrue(TEXT("Timeout retains draft, locks ambiguous page indices, never repeats mutation"),!S.IsBookWritePending()
        && !S.CanEditBookPage() && S.GetBookPageText()==TEXT("Slow reply") && Requests(0xAB,0,TEXT("Slow reply"))==1);
    Event(0xB5,Modified);TestEqual(TEXT("Late timed-out write cannot navigate"),S.Book.CurrentPage,0);
    Event(0xB4,Book(100,false,{TEXT("Slow reply"),TEXT("Second")}));TestTrue(TEXT("Reopened authoritative snapshot unlocks editing"),S.CanEditBookPage());
    Open(100,false,{});TestTrue(TEXT("Opening empty book creates its first page"),S.BookWriteAction==0xAC && S.BookWritePage==0);
    Event(0xB6,Reply(500,1,true));TestTrue(TEXT("Concurrent append index requests refresh, no fabricated pages"),S.Book.Pages.IsEmpty() && S.bBookRefreshRequired);
    Open(100,false,{TEXT("Full")},1);const uint32 BeforeFull=S.NextGameActionSequence;S.TurnBookPage(1);
    TestEqual(TEXT("Capacity prevents append"),S.NextGameActionSequence,BeforeFull);

    // Exercise the actual DAT book panel, not a separate mock editor.
    auto* Dat=NewObject<UACEDatSubsystem>(GI);
    if(!TestTrue(TEXT("Retail DAT opens"),Dat->LoadDatDirectory(ACETestDatDirectory())))return false;
    ON_SCOPE_EXIT {Dat->Deinitialize();};
    auto* Resources=NewObject<UACEUIResourceResolver>();Resources->Initialize(Dat);
    auto* Manager=NewObject<UACEUIElementManager>();Manager->Initialize();
    auto* Layout=NewObject<UACEUILayoutResolver>();Layout->Initialize(Dat,Manager);
    if(!TestTrue(TEXT("Gameplay layout loads"),Layout->LoadLayout(0x21000005)))return false;
    auto* Canvas=NewObject<UACEUICanvasWidget>();Canvas->Initialize();Canvas->InitializeCanvas(Manager);Canvas->SetResourceResolver(Resources);
    const auto Slate=Canvas->TakeWidget();
    auto* Binder=NewObject<UACEUIGameplayBinder>();Binder->Initialize(Client,Manager,Canvas,nullptr);Canvas->SetGameplayBinder(Binder);
    ON_SCOPE_EXIT {Binder->Shutdown();Canvas->SetGameplayBinder(nullptr);Manager->Shutdown();};
    FWidgetRenderer Renderer(true,true);auto* Target=FWidgetRenderer::CreateTargetFor(FVector2D(1280,720),TF_Bilinear,true);
    ON_SCOPE_EXIT {Target->ReleaseResource();};
    const FString Art=FPaths::ProjectSavedDir()/TEXT("Automation/BookWriting");IFileManager::Get().MakeDirectory(*Art,true);
    auto Draw=[&](float Scale)
    {
        const TSharedRef<SWidget> Scaled=SNew(SDPIScaler).DPIScale(Scale)[Slate];
        for(int32 I=0;I<4;++I){++GFrameCounter;Canvas->NativeTick(Canvas->GetCachedGeometry(),0);Renderer.DrawWidget(Target,Scaled,FVector2D(1280,720),0);FlushRenderingCommands();}
        TArray<FColor> Pixels;Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels);
        TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(1280,720,Pixels,PNG);
        FFileHelper::SaveArrayToFile(PNG,*(Art/FString::Printf(TEXT("Book-%.1f.png"),Scale)));
    };
    Open(100,false,{TEXT("A journal of Dereth\n\nWrite on this page, then turn the page or close the book to save."),TEXT("Second page")});
    FACEWorldObject Journal;Journal.Guid=500;Journal.Name=TEXT("Journal of Dereth");S.WorldObjects.Add(500,Journal);
    Binder->HandleBookChanged();Manager->ApplyEdgeAnchoredLayout(1280,720);Binder->RefreshBookOverlays();
    if(!TestTrue(TEXT("Owned page displays editor in retail book panel"),Binder->ActivePanelPage==TEXT("BookPanel_Field")
        && Binder->BookPageEditor && Binder->BookPageEditor->IsVisible() && !Binder->BookScroll->IsVisible()))return false;
    TestEqual(TEXT("Editor receives current page text"),Binder->BookPageEditor->GetText().ToString(),S.Book.Pages[0]);
    for(float Scale:{1.f,1.5f})Draw(Scale);
    TestTrue(TEXT("DAT title is visible and identifies book and page"),Binder->BookTitleLabel->IsVisible()
        && Binder->BookTitleLabel->GetText().ToString()==TEXT("Journal of Dereth  (1 / 2)")
        && Cast<UACERetailTextBlock>(Binder->BookTitleLabel)->GetBitmapFont()!=nullptr);
    FString LongPage;for(int32 I=1;I<=60;++I)LongPage+=FString::Printf(TEXT("Line %d\n"),I);
    Binder->BookPageEditor->SetText(FText::FromString(LongPage));
    Binder->BookPageEditor->OnTextChanged.Broadcast(Binder->BookPageEditor->GetText());Draw(1.f);
    TestTrue(TEXT("Multiline book text scrolls within DAT page bounds"),Binder->BookPageEditor->GetScrollOffsetOfEnd()>0);
    Binder->ScrollDragTarget=UACEUIGameplayBinder::EACEUIScrollTarget::Book;
    Binder->ScrollDragTravel=100;Binder->ScrollDragTrackOrigin=0;Binder->ScrollDragGrabOffset=0;
    Binder->bScrollDragHorizontal=false;Binder->ScrollDragLastOffset=INDEX_NONE;
    Binder->ScrollDragMaxOff=FMath::CeilToInt(Binder->BookPageEditor->GetScrollOffsetOfEnd());
    Binder->UpdateScrollbarDrag(Canvas->LayoutToViewport(FVector2D(0,100)));
    TestTrue(TEXT("Retail scrollbar drags editable page instead of hidden reader"),Binder->BookPageEditor->GetScrollOffset()>0);
    Binder->ScrollDragTarget=UACEUIGameplayBinder::EACEUIScrollTarget::None;
    Binder->BookPageEditor->SetText(FText::FromString(TEXT("Saved through book editor")));
    Binder->BookPageEditor->OnTextChanged.Broadcast(Binder->BookPageEditor->GetText());
    Binder->HandleDialogNamedClick(TEXT("BookNextButton"));
    TestEqual(TEXT("Retail next button saves typed text"),Requests(0xAB,0,TEXT("Saved through book editor")),1);
    TestFalse(TEXT("Editor unavailable during write"),Binder->BookPageEditor->IsVisible());
    Event(0xB5,Modified);TestTrue(TEXT("Acknowledged navigation restores editor on next page"),Binder->BookPageEditor->IsVisible() && S.Book.CurrentPage==1);
    Binder->BookPageEditor->OnTextChanged.Broadcast(FText::FromString(TEXT("Saved on panel switch")));
    Binder->ShowPanelPage(TEXT("InventoryPanel_Field"));
    TestTrue(TEXT("Switching panels saves and closes book"),!S.Book.bOpen && Requests(0xAB,1,TEXT("Saved on panel switch"))==1 && !Binder->BookPageEditor->IsVisible());
    Open(200,false,{TEXT("A read-only quest book")});Binder->HandleBookChanged();
    TestTrue(TEXT("Read-only book uses reader without editable overlay"),Binder->BookScroll->IsVisible() && !Binder->BookPageEditor->IsVisible());
    Open(100,false,{TEXT("Before close")});Binder->HandleBookChanged();Binder->HandleBookTextChanged(FText::FromString(TEXT("Saved on hide")));
    Binder->HidePanel();TestTrue(TEXT("Hiding panel saves editable page"),!S.Book.bOpen && Requests(0xAB,0,TEXT("Saved on hide"))==1);
    Open(100,false,{TEXT("Old")});S.SetBookPageText(TEXT("Discard at world reset"));
    const uint32 BeforeReset=S.NextGameActionSequence;S.ClearWorldState();
    TestTrue(TEXT("World reset discards old-character draft without a write"),!S.Book.bOpen && !S.bBookDraft && S.NextGameActionSequence==BeforeReset);
    return true;
}
#endif
