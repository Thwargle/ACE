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
#include "UI/ACEUICanvasWidget.h"
#include "UI/ACEUIElementManager.h"
#include "UI/ACEUILayoutResolver.h"
#include "UI/ACEUIResourceResolver.h"
#include "UI/ACEUIGameplayBinder.h"
#include "UI/ACERetailTextBlock.h"
#include "Widgets/Layout/SDPIScaler.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEServerDialogsTest,"ACE.Network.ServerDialogs",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)

bool FACEServerDialogsTest::RunTest(const FString&)
{
    TGuardValue<uint64> FrameGuard(GFrameCounter,GFrameCounter);
    auto* GI=NewObject<UGameInstance>();
    auto* Client=NewObject<UACEClientSubsystem>(GI);Client->Session=MakeShared<FACESession>();
    auto& S=*Client->Session;S.PlayerGuid=100;S.State=EACESessionState::InWorld;
    auto* Sockets=ISocketSubsystem::Get();
    auto* Receiver=FUdpSocketBuilder(TEXT("Dialog parity receiver")).AsNonBlocking().BoundToPort(0).Build();
    ON_SCOPE_EXIT {S.State=EACESessionState::Disconnected;if(Receiver){Receiver->Close();Sockets->DestroySocket(Receiver);}};
    if(!TestNotNull(TEXT("Loopback receiver"),Receiver))return false;
    S.SocketC2S=FUdpSocketBuilder(TEXT("Dialog parity sender")).AsNonBlocking().BoundToPort(0).Build();
    if(!TestNotNull(TEXT("Loopback sender"),S.SocketC2S))return false;
    S.ServerC2SAddr=Sockets->CreateInternetAddr();S.ServerC2SAddr->SetIp(0x7f000001);S.ServerC2SAddr->SetPort(Receiver->GetPortNo());
    S.IssacClient=MakeUnique<FACEIsaac>(123u);
    auto Event=[&](uint32 Opcode,const FACEBinaryWriter& Body,int32 Length=INDEX_NONE)
    {
        FACEBinaryWriter W;W.WriteUInt32(100);W.WriteUInt32(1);W.WriteUInt32(Opcode);
        W.WriteBytes(Body.GetData().GetData(),Length==INDEX_NONE?Body.Num():Length);
        FACEBinaryReader R(W.GetData());S.HandleGameEvent(R);return R.Remaining();
    };
    // CM_Communication::DispatchUI_PopUpString and CM_Character confirmation fixtures.
    const FString Message=TEXT("Quest instructions\nReturn the sleeping pets to their cages. Speak to the keeper when all three wings are clear.");
    FACEBinaryWriter Popup;Popup.WriteString16L(Message);
    FACEBinaryWriter Confirm;Confirm.WriteUInt32(1);Confirm.WriteUInt32(456);Confirm.WriteString16L(TEXT("Join this fellowship?"));
    for(int32 Cut=0;Cut<Popup.Num();++Cut)
    {
        Event(0x0004,Popup,Cut);
        if(!TestTrue(TEXT("Every truncated popup, including missing padding, is rejected"),S.GetServerPopups().IsEmpty()))return false;
    }
    for(int32 Cut=0;Cut<Confirm.Num();++Cut)
    {
        Event(0x0274,Confirm,Cut);
        if(!TestTrue(TEXT("Truncated confirmation cannot create a dialog"),S.GetConfirmations().IsEmpty()))return false;
    }
    TestEqual(TEXT("PopupString fully consumed"),Event(0x0004,Popup),0);
    Event(0x0004,Popup);
    if(!TestEqual(TEXT("Identical instructions remain separate occurrences"),S.GetServerPopups().Num(),2))return false;
    const uint64 FirstId=S.GetServerPopups()[0].Id,SecondId=S.GetServerPopups()[1].Id;
    TestTrue(TEXT("Popups have distinct local identities"),FirstId!=0 && SecondId>FirstId);
    TestEqual(TEXT("Text retains line breaks"),S.GetServerPopups()[0].Message,Message);
    Event(0x0274,Confirm);
    for(int32 Cut=0;Cut<Confirm.Num();++Cut)Event(0x0274,Confirm,Cut);
    TestTrue(TEXT("Bad replacements preserve the active prompt"),S.GetConfirmations().Num()==1 && S.GetConfirmations()[0].Prompt==TEXT("Join this fellowship?"));
    FACEBinaryWriter Replacement;Replacement.WriteUInt32(1);Replacement.WriteUInt32(456);Replacement.WriteString16L(TEXT("Join the new fellowship?"));
    Event(0x0274,Replacement);
    TestTrue(TEXT("Matching context replaces rather than duplicates a confirmation"),S.GetConfirmations().Num()==1 && S.GetConfirmations()[0].Prompt==TEXT("Join the new fellowship?"));
    FACEBinaryWriter Done;Done.WriteUInt32(1);Done.WriteUInt32(456);
    for(int32 Cut=0;Cut<8;++Cut)Event(0x0276,Done,Cut);
    TestEqual(TEXT("Partial withdrawal does not remove the prompt"),S.GetConfirmations().Num(),1);
    FACEBinaryWriter OtherDone;OtherDone.WriteUInt32(2);OtherDone.WriteUInt32(456);Event(0x0276,OtherDone);
    TestEqual(TEXT("Withdrawal matches both type and context"),S.GetConfirmations().Num(),1);
    Event(0x0276,Done);
    TestTrue(TEXT("Complete withdrawal removes confirmation only"),S.GetConfirmations().IsEmpty() && S.GetServerPopups().Num()==2);
    TestTrue(TEXT("First OK dismisses locally"),S.DismissServerPopup(FirstId));
    TestFalse(TEXT("Duplicate or stale OK cannot dismiss the next message"),S.DismissServerPopup(FirstId));
    TestTrue(TEXT("Second identical message remains and no reply is sent"),S.GetServerPopups().Num()==1
        && S.GetServerPopups()[0].Id==SecondId && S.CachedC2SPackets.IsEmpty());
    Event(0x0274,Confirm);TestTrue(TEXT("Confirmation still sends a real reply"),S.RespondToConfirmation(1,456,true));
    bool ReplyFound=false;
    for(const auto& P:S.CachedC2SPackets)
    {
        FACEBinaryReader R(P.Value.Payload);if(!R.CanRead(40))continue;
        R.Skip(14);const uint16 Queue=R.ReadUInt16();
        if(R.ReadUInt32()!=ACEOpcode::GameAction)continue;R.ReadUInt32();
        if(R.ReadUInt32()==0x0275)ReplyFound=Queue==ACEQueue::UIQueue && R.ReadUInt32()==1 && R.ReadUInt32()==456 && R.ReadUInt32()==1 && R.Remaining()==0;
    }
    TestTrue(TEXT("Reply encodes type, context, uint32 acceptance on UI queue"),ReplyFound);
    const uint32 ActionSequence=S.NextGameActionSequence;

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
    FWidgetRenderer Renderer(true,true);
    auto* Target=FWidgetRenderer::CreateTargetFor(FVector2D(1280,720),TF_Bilinear,true);
    ON_SCOPE_EXIT {Target->ReleaseResource();};
    const FString Art=FPaths::ProjectSavedDir()/TEXT("Automation/ServerDialogs");IFileManager::Get().MakeDirectory(*Art,true);
    auto Draw=[&](float Scale)
    {
        const TSharedRef<SWidget> Scaled=SNew(SDPIScaler).DPIScale(Scale)[Slate];
        for(int32 I=0;I<4;++I){++GFrameCounter;Canvas->NativeTick(Canvas->GetCachedGeometry(),0);Renderer.DrawWidget(Target,Scaled,FVector2D(1280,720),0);FlushRenderingCommands();}
        TArray<FColor> Pixels;Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels);
        TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(1280,720,Pixels,PNG);
        FFileHelper::SaveArrayToFile(PNG,*(Art/FString::Printf(TEXT("Message-%.1f.png"),Scale)));
    };
    Manager->ApplyEdgeAnchoredLayout(1280,720);Binder->RefreshServerPopup();Draw(1.f);
    if(!TestTrue(TEXT("Actual retail MessageDialog template shown"),Binder->ServerPopupRoot && Binder->ServerPopupRoot->bVisible
        && Binder->ServerPopupRoot->ElementId==0x24))return false;
    const auto OK=Manager->FindElementByName(TEXT("ServerMessageOK"));
    const auto Body=Manager->FindElementByName(TEXT("ServerMessageBody"));
    if(!TestTrue(TEXT("Message body and retail OK control exist"),OK && Body))return false;
    TestEqual(TEXT("Message text displayed in DAT body"),Binder->ServerPopupLabels[0]->GetText().ToString(),Message);
    TestTrue(TEXT("DAT glyphs used"),Cast<UACERetailTextBlock>(Binder->ServerPopupLabels[0])->GetBitmapFont()!=nullptr);
    TestTrue(TEXT("Wrapped message ends above OK"),Body->GetScreenOrigin().Y+Body->Height<=OK->GetScreenOrigin().Y);
    Event(0x0274,Confirm);Binder->RefreshServerConfirmation();Binder->RefreshServerPopup();
    TestTrue(TEXT("Action confirmation defers, not deletes, informational text"),!Binder->ServerPopupRoot->bVisible && S.GetServerPopups().Num()==1
        && Binder->ServerConfirmRoot->bVisible);
    Event(0x0276,Done);Binder->RefreshServerConfirmation();Binder->RefreshServerPopup();
    TestTrue(TEXT("Information returns after server withdrawal"),Binder->ServerPopupRoot->bVisible && Binder->ServerPopupId==SecondId);
    for(float Scale:{.75f,1.f,1.5f})
    {
        Draw(Scale);
        const FVector2D Outside=Canvas->GetCachedGeometry().LocalToAbsolute(FVector2D(1,1));
        TestTrue(TEXT("Outside click cannot leak to gameplay"),Binder->TryHandleModalPopupClick(Outside));
        TestEqual(TEXT("Outside click keeps message"),S.GetServerPopups().Num(),1);
    }
    const FVector2D Center=Canvas->LayoutToViewport(FVector2D(OK->GetScreenOrigin())+FVector2D(OK->Width,OK->Height)*.5);
    TestTrue(TEXT("Scaled OK hit is handled"),Binder->TryHandleModalPopupClick(Canvas->GetCachedGeometry().LocalToAbsolute(Center)));
    TestTrue(TEXT("Desktop OK closes without a reply"),S.GetServerPopups().IsEmpty() && S.NextGameActionSequence==ActionSequence);
    Event(0x0004,Popup);Event(0x0274,Confirm);Binder->RefreshServerConfirmation();
    S.State=EACESessionState::CharacterSelect;Binder->RefreshServerConfirmation();Binder->RefreshServerPopup();
    TestTrue(TEXT("Leaving world immediately hides all server dialogs"),!Binder->ServerConfirmRoot->bVisible && !Binder->ServerPopupRoot->bVisible);
    S.ClearWorldState();
    TestTrue(TEXT("Returning to character selection removes both queues"),S.GetConfirmations().IsEmpty() && S.GetServerPopups().IsEmpty());
    Event(0x0004,Popup);Event(0x0274,Confirm);
    TestTrue(TEXT("Late dialogs outside world are ignored"),S.GetConfirmations().IsEmpty() && S.GetServerPopups().IsEmpty());
    S.State=EACESessionState::InWorld;Event(0x0004,Popup);
    TestTrue(TEXT("A new character cannot reuse an old popup identity"),S.GetServerPopups()[0].Id>SecondId && !S.DismissServerPopup(SecondId));
    S.ClearWorldState();
    TArray<FString> Overflow;
    auto Handle=S.OnChatMessage.AddLambda([&](const FString& Text,const FString&,int32){Overflow.Add(Text);});
    for(int32 I=0;I<65;++I)Event(0x0004,Popup);
    S.OnChatMessage.Remove(Handle);
    TestTrue(TEXT("Excess popup instructions go to chat instead of disappearing or growing UI unbounded"),S.GetServerPopups().Num()==64 && Overflow.Num()==1 && Overflow[0]==Message);
    S.Disconnect();TestTrue(TEXT("Disconnect clears all pending instructions"),S.GetServerPopups().IsEmpty());
    return true;
}
#endif
