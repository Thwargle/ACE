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
#include "UI/ACEUICanvasWidget.h"
#include "UI/ACEUIElementManager.h"
#include "UI/ACEUILayoutResolver.h"
#include "UI/ACEUIResourceResolver.h"
#include "UI/ACEUIGameplayBinder.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEHousingTest,"ACE.RetailParity.Housing",
	EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACEHousingTest::RunTest(const FString&)
{
	TGuardValue<uint64> FrameGuard(GFrameCounter,GFrameCounter);
	auto* GI=NewObject<UGameInstance>();
	auto* Client=NewObject<UACEClientSubsystem>(GI); Client->Session=MakeShared<FACESession>();
	auto& S=*Client->Session; S.PlayerGuid=1234; S.State=EACESessionState::InWorld;
	FACEWorldObject Self; Self.Guid=1234; Self.Name=TEXT("Housing regression"); S.WorldObjects.Add(1234,Self);
	// Independent fixture from HouseProfile::Pack / CM_House::RecvNotice_HouseProfile.
	auto Profile=[&](int32 Owner,int32 Type=1)
	{
		FACEBinaryWriter W;
		for(uint32 V:{4321u,77u,uint32(Owner),0u,1u,0xFFFFFFFFu,0xFFFFFFFFu,0xFFFFFFFFu,0u,uint32(Type)}) W.WriteUInt32(V);
		W.WriteString16L(Owner?TEXT("House owner"):TEXT(""));
		W.WriteUInt32(2);
		W.WriteInt32(1000); W.WriteInt32(0); W.WriteUInt32(273); W.WriteString16L(TEXT("Pyreal")); W.WriteString16L(TEXT("Pyreals"));
		W.WriteInt32(2); W.WriteInt32(0); W.WriteUInt32(111); W.WriteString16L(TEXT("Writ")); W.WriteString16L(TEXT("Writs"));
		W.WriteUInt32(1);
		W.WriteInt32(1000); W.WriteInt32(100); W.WriteUInt32(273); W.WriteString16L(TEXT("Pyreal")); W.WriteString16L(TEXT("Pyreals"));
		return W.GetData();
	};
	auto Bytes=Profile(0);
	FACEBinaryReader First(Bytes); S.HandleHouseProfile(First);
	TestEqual(TEXT("Profile fully consumed"),First.Remaining(),0);
	TestEqual(TEXT("Signed no-maximum restriction retained"),S.HouseOffer.MaxLevel,-1);
	TestEqual(TEXT("Both server purchase requirements retained"),S.HouseOffer.Buy.Num(),2);
	const uint64 Revision=S.HouseOfferRevision;
	for(int32 Cut=0;Cut<Bytes.Num();++Cut)
	{
		TArray<uint8> Truncated=Bytes; Truncated.SetNum(Cut);
		FACEBinaryReader R(Truncated); S.HandleHouseProfile(R);
		TestEqual(TEXT("Truncation cannot publish a partial offer"),S.HouseOfferRevision,Revision);
	}
	auto Load=[&](int32 Owner,int32 Type=1){auto B=Profile(Owner,Type);FACEBinaryReader R(B);S.HandleHouseProfile(R);};
	auto Add=[&](int32 Guid,int32 Wcid,int32 Count,int32 Container=1234)
	{
		FACEWorldObject I; I.Guid=Guid; I.WeenieClassId=Wcid; I.StackSize=Count; I.ContainerId=Container;
		I.Name=Wcid==273?TEXT("Pyreal"):TEXT("Writ"); I.ItemType=ACEItemType::Misc; I.IconId=0x060011CB;
		S.WorldObjects.Add(Guid,I);
		FACEContainerItemRef Ref; Ref.ItemGuid=Guid; S.ContainerContents.FindOrAdd(Container).Add(Ref);
	};
	Add(200,273,1000); Add(201,111,2); Add(202,111,2,9000); Add(203,273,1000);
	S.WorldObjects[203].WielderId=1234;
	TestFalse(TEXT("Foreign payment rejected"),S.StageHousePayment(202,false));
	TestFalse(TEXT("Equipped payment rejected"),S.StageHousePayment(203,false));
	TestFalse(TEXT("Empty purchase disabled"),S.CanPayHouse(false));
	TestTrue(TEXT("Currency accepted"),S.StageHousePayment(200,false));
	TestFalse(TEXT("Partial purchase disabled"),S.CanPayHouse(false));
	TestTrue(TEXT("Second requirement accepted"),S.StageHousePayment(201,false));
	TestTrue(TEXT("Complete purchase enabled"),S.CanPayHouse(false));
	TestFalse(TEXT("Duplicate offer ignored"),S.StageHousePayment(201,false));
	S.TradeSelfItems.Add(201);
	TestFalse(TEXT("An item offered in trade cannot also purchase a house"),S.CanPayHouse(false));
	S.TradeSelfItems.Reset();
	Load(0); TestTrue(TEXT("Main pack expands accepted contents"),S.StageHousePayment(1234,false));
	TestEqual(TEXT("Only qualifying owned unequipped items staged"),S.HouseBuyItems.Num(),2);
	Load(1234); S.WorldObjects[200].StackSize=10;
	TestFalse(TEXT("Owned house cannot be bought again"),S.StageHousePayment(200,false));
	TestTrue(TEXT("Partial rent accepted"),S.StageHousePayment(200,true));
	TestTrue(TEXT("Partial maintenance can be submitted"),S.CanPayHouse(true));
	Load(0); Add(204,2621,1); S.WorldObjects[204].ItemType=ACEItemType::PromissoryNote; S.WorldObjects[204].Value=10000;
	TestTrue(TEXT("Trade note accepted as pyreals"),S.StageHousePayment(204,false));
	TestEqual(TEXT("Trade note uses face value"),S.GetHousePaymentAmount(273,false),int64(10000));
	S.StageHousePayment(201,false);

	auto* Dat=NewObject<UACEDatSubsystem>(GI);
	if(!TestTrue(TEXT("Retail DAT opens"),Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call")))) return false;
	ON_SCOPE_EXIT {Dat->Deinitialize();};
	auto* Resources=NewObject<UACEUIResourceResolver>(); Resources->Initialize(Dat);
	auto* Manager=NewObject<UACEUIElementManager>(); Manager->Initialize();
	auto* Layout=NewObject<UACEUILayoutResolver>(); Layout->Initialize(Dat,Manager);
	if(!TestTrue(TEXT("Retail gameplay layout loads"),Layout->LoadLayout(0x21000005))) return false;
	auto* Canvas=NewObject<UACEUICanvasWidget>(); Canvas->Initialize(); Canvas->InitializeCanvas(Manager); Canvas->SetResourceResolver(Resources);
	const auto Slate=Canvas->TakeWidget();
	auto* Binder=NewObject<UACEUIGameplayBinder>(); Binder->Initialize(Client,Manager,Canvas,nullptr); Canvas->SetGameplayBinder(Binder);
	ON_SCOPE_EXIT {Binder->Shutdown();Canvas->SetGameplayBinder(nullptr);Manager->Shutdown();};
	FWidgetRenderer Renderer(true,true);
	auto* Target=FWidgetRenderer::CreateTargetFor(FVector2D(1600,900),TF_Bilinear,true);
	const FString Art=FPaths::ProjectSavedDir()/TEXT("Automation/Housing"); IFileManager::Get().MakeDirectory(*Art,true);
	auto Draw=[&](const TCHAR* Name)
	{
		for(int I=0;I<4;++I){++GFrameCounter;Canvas->NativeTick(Canvas->GetCachedGeometry(),0);Renderer.DrawWidget(Target,Slate,FVector2D(1600,900),0);FlushRenderingCommands();}
		TArray<FColor> Pixels;Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels);
		TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(1600,900,Pixels,PNG);FFileHelper::SaveArrayToFile(PNG,*(Art/(FString(Name)+TEXT(".png"))));
	};
	Manager->ApplyEdgeAnchoredLayout(1600,900); Binder->RefreshHouseOffer(); Draw(TEXT("Buy"));
	TestEqual(TEXT("Profile opens the house lord"),Binder->OpenHouseLord,4321);
	TestTrue(TEXT("Actual Slumlord DAT subtree shown"),bool(Manager->FindElementByName(TEXT("Slumlord"))->bVisible));
	TestFalse(TEXT("Vendor chrome stays hidden"),bool(Manager->FindElementByName(TEXT("Vendor"))->bVisible));
	TestTrue(TEXT("Buy tab visible"),bool(Manager->FindElementByName(TEXT("HouseBuyPage"))->bVisible));
	TestFalse(TEXT("Maintenance tab hidden"),bool(Manager->FindElementByName(TEXT("HouseMaintenancePage"))->bVisible));
	TestTrue(TEXT("Payment icons use actual item list"),Binder->HouseOfferSlots.Num()>0);
	TestTrue(TEXT("Complete requirements enable Buy"),Manager->FindElementByName(TEXT("HouseBuy_Button"))->bActivatable);
	Binder->OnElementActivated(Manager->FindElementByName(TEXT("HouseBuy_Button")));
	TestTrue(TEXT("Landscape purchase requires retail confirmation"),Binder->bHousePaymentConfirm);
	Binder->FinishServerConfirmation(false);
	TestFalse(TEXT("Declining clears confirmation"),Binder->bHousePaymentConfirm);
	TestEqual(TEXT("Declining retains staged payments"),S.HouseBuyItems.Num(),2);

	auto* Sockets=ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	auto Address=Sockets->CreateInternetAddr();bool Valid=false;Address->SetIp(TEXT("127.0.0.1"),Valid);Address->SetPort(0);
	auto* Receiver=Sockets->CreateSocket(NAME_DGram,TEXT("House receiver"),false);
	if(!Receiver)return false;
	ON_SCOPE_EXIT {Receiver->Close();Sockets->DestroySocket(Receiver);};
	if(!TestTrue(TEXT("Loopback binds"),Valid && Receiver->Bind(*Address)))return false;
	Receiver->GetAddress(*Address);
	S.SocketC2S=Sockets->CreateSocket(NAME_DGram,TEXT("House sender"),false);
	if(!S.SocketC2S)return false;
	ON_SCOPE_EXIT {S.SocketC2S->Close();Sockets->DestroySocket(S.SocketC2S);S.SocketC2S=nullptr;};
	S.ServerC2SAddr=Address; S.IssacClient=MakeUnique<FACEIsaac>(123);
	Binder->OnElementActivated(Manager->FindElementByName(TEXT("HouseBuy_Button"))); Binder->FinishServerConfirmation(true);
	const auto* Packet=S.CachedC2SPackets.Find(S.NextPacketSequence-1);
	if(TestNotNull(TEXT("Confirmed purchase sends action"),Packet))
	{
		FACEBinaryReader R(Packet->Payload); R.Skip(14); TestEqual(TEXT("Retail weenie queue"),R.ReadUInt16(),uint16(ACEQueue::WeenieQueue));
		R.Skip(8); TestEqual(TEXT("BuyHouse opcode"),R.ReadUInt32(),0x21Cu);
		TestEqual(TEXT("Lord GUID"),R.ReadUInt32(),4321u); TestEqual(TEXT("Two payment IDs"),R.ReadUInt32(),2u);
		TestEqual(TEXT("Trade note ID"),R.ReadUInt32(),204u);TestEqual(TEXT("Writ ID"),R.ReadUInt32(),201u);
		TestEqual(TEXT("No invented payment fields"),R.Remaining(),0);
	}
	TestTrue(TEXT("Only local staged cart clears"),S.HouseBuyItems.IsEmpty());
	TestTrue(TEXT("Server remains authoritative over items"),S.WorldObjects.Contains(204));
	Load(1234);Binder->RefreshHouseOffer();S.StageHousePayment(200,true);Draw(TEXT("Maintenance"));
	TestTrue(TEXT("Owned house opens maintenance"),Binder->bHouseRentTab);
	TestTrue(TEXT("Partial maintenance shows paid and staged count"),Binder->HouseOfferLabels[3]->GetText().ToString().Contains(TEXT("110/1000 Pyreals")));
	Binder->OnElementActivated(Manager->FindElementByName(TEXT("HouseMaintenance_Button")));
	TestFalse(TEXT("Owner needs no proxy confirmation"),Binder->bHousePaymentConfirm);
	const auto* Rent=S.CachedC2SPackets.Find(S.NextPacketSequence-1);
	if(TestNotNull(TEXT("Rent sends packet"),Rent)){FACEBinaryReader R(Rent->Payload);R.Skip(24);TestEqual(TEXT("RentHouse opcode"),R.ReadUInt32(),0x221u);}
	// Splits must arrive from the server before their new object IDs can be paid.
	Binder->StageHouseItem(200,4);
	TestEqual(TEXT("Partial payment tracks a pending split"),Binder->HousePaymentSplit.SourceGuid,200);
	TestTrue(TEXT("Original full stack is never staged during split"),S.HouseRentItems.IsEmpty());
	Binder->RefreshHouseOffer();TestTrue(TEXT("Awaiting split acknowledgement keeps cart empty"),S.HouseRentItems.IsEmpty());
	constexpr int32 SplitGuid=int32(0x80000205u);
	S.WorldObjects[200].StackSize=6;Add(SplitGuid,273,4);
	Binder->RefreshHouseOffer();
	TestTrue(TEXT("Confirmed dynamic high-bit stack becomes payment"),S.HouseRentItems.Contains(SplitGuid));
	TestEqual(TEXT("Split tracking completes"),Binder->HousePaymentSplit.SourceGuid,0);
	Draw(TEXT("SplitMaintenance"));
	const auto List=Manager->FindElementByName(TEXT("HouseMaintenanceItemsList"));
	const FVector2D In=Canvas->LayoutToViewport(FVector2D(List->GetScreenOrigin())+FVector2D(16,16));
	TestTrue(TEXT("Staged item is clickable at its visible position"),Binder->TryBeginInventoryDrag(In));
	Binder->bInvDragActive=true;Binder->TryFinishInventoryDrag(FVector2D(0,0));
	TestTrue(TEXT("Dragging out removes only the payment offer"),S.HouseRentItems.IsEmpty());
	TestTrue(TEXT("Dragging out preserves physical item"),S.WorldObjects.Contains(SplitGuid));
	// Dropping a main-pack reference stages matching contents instead of moving the player.
	Binder->InvDragGuid=1234;Binder->bInvDragPending=true;Binder->bInvDragActive=true;
	Binder->TryFinishInventoryDrag(In);
	TestTrue(TEXT("Main pack drag stages payments"),S.HouseRentItems.Contains(200));
	Load(9999);Binder->RefreshHouseOffer();S.StageHousePayment(200,true);Binder->RefreshHouseOffer();
	Binder->OnElementActivated(Manager->FindElementByName(TEXT("HouseMaintenance_Button")));
	TestTrue(TEXT("Proxy maintenance requires confirmation"),Binder->bHousePaymentConfirm);Binder->FinishServerConfirmation(false);
	FACEBinaryWriter Failed;Failed.WriteUInt32(1234);Failed.WriteUInt32(1);Failed.WriteUInt32(0x259);Failed.WriteUInt32(2);
	FACEBinaryReader Failure(Failed.GetData());S.HandleGameEvent(Failure);
	const auto* Query=S.CachedC2SPackets.Find(S.NextPacketSequence-1);
	if(TestNotNull(TEXT("Failed transaction refreshes the offer"),Query))
	{FACEBinaryReader R(Query->Payload);R.Skip(24);TestEqual(TEXT("QueryHouse action"),R.ReadUInt32(),0x258u);TestEqual(TEXT("Queries active lord"),R.ReadUInt32(),4321u);}
	Load(0,4);Binder->RefreshHouseOffer();S.StageHousePayment(204,false);S.StageHousePayment(201,false);Binder->RefreshHouseOffer();
	Binder->OnElementActivated(Manager->FindElementByName(TEXT("HouseBuy_Button")));
	TestFalse(TEXT("Apartment purchase bypasses landscape cooldown prompt"),Binder->bHousePaymentConfirm);
	TestTrue(TEXT("Apartment payment was submitted"),S.HouseBuyItems.IsEmpty());
	Binder->ShowVendorPanel(9876);Binder->RefreshHouseOffer();
	TestEqual(TEXT("Opening another environment panel closes house context"),Binder->OpenHouseLord,0);
	Binder->CloseHouseOffer();
	TestFalse(TEXT("Closing collapses native housing panel"),bool(Manager->FindElementByName(TEXT("Slumlord"))->bVisible));
	TestEqual(TEXT("Closing clears server offer context"),S.GetHouseProfile().LordGuid,0);
	return !HasAnyErrors();
}
#endif
