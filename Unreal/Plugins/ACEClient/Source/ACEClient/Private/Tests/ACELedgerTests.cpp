#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Mods/ACELedger.h"
#include "ACEClientSubsystem.h"
#include "ACEDatSubsystem.h"
#include "ACESession.h"
#include "Mods/ACEPluginSubsystem.h"
#include "Mods/ACEPluginDesktop.h"
#include "Mods/ACEPluginStyle.h"
#include "Mods/ACEPluginIcon.h"
#include "UI/ACEUIResourceResolver.h"
#include "UI/ACEUIElementManager.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Engine/GameInstance.h"
#include "Slate/WidgetRenderer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ImageUtils.h"
#include "RenderingThread.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACELedgerTest,"ACE.Plugins.Ledgers",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACELedgerTest::RunTest(const FString&)
{
    FACEHuntLedger L;L.Reset(1000);
    FACEPlayerVitals V;V.bValid=true;V.QualityInt64s.Add(1,1000000);V.QualityInt64s.Add(6,500);
    V.TotalExperience=1000000;V.AvailableLuminance=500;L.Observe(V,1000);
    TestEqual(TEXT("Login totals establish a baseline"),L.XP,int64(0));TestEqual(TEXT("Existing luminance is not income"),L.Lum,int64(0));
    TestEqual(TEXT("No XP gain cannot predict a level"),L.SecondsToLevel(1200,1060),-1.);
    V.TotalExperience+=600;V.AvailableLuminance+=100;L.Observe(V,1060);
    TestEqual(TEXT("Hourly rate uses elapsed run time"),L.PerHour(L.XP,1060),int64(36000));
    TestEqual(TEXT("600 XP in a minute estimates two minutes for 1200 XP"),L.SecondsToLevel(1200,1060),120.);
    TestEqual(TEXT("Estimate updates as remaining XP changes"),L.SecondsToLevel(300,1060),30.);
    TestEqual(TEXT("Idle time reduces the run rate"),L.SecondsToLevel(1200,1120),240.);
    TestEqual(TEXT("A completed level has no remaining time"),L.SecondsToLevel(0,1060),0.);
    V.AvailableExperience=0;V.AvailableLuminance-=500;L.Observe(V,1061);
    V.AvailableLuminance+=20;L.Observe(V,1062);
    TestEqual(TEXT("Spending does not remove earned luminance"),L.Lum,int64(120));
    TestEqual(TEXT("XP spending does not alter income"),L.XP,int64(600));
    TestEqual(TEXT("Recent XP expires after five minutes"),L.LastFiveMinutes(1360).XP,int64(0));
    TestEqual(TEXT("Recent luminance counts each gain independently"),L.LastFiveMinutes(1360).Lum,int64(20));
    TestEqual(TEXT("All recent gains expire"),L.LastFiveMinutes(1362).Lum,int64(0));
    TestEqual(TEXT("Lifetime run total survives rolling expiry"),L.XP,int64(600));
    L.Reset(1400);L.Observe(V,1400);TestEqual(TEXT("Reset does not recount historical XP"),L.XP,int64(0));
    TestEqual(TEXT("Reset waits for new XP before estimating"),L.SecondsToLevel(1200,1460),-1.);
    for(int32 I=1;I<=100000;++I){++V.TotalExperience;L.Observe(V,1400+I);}
    TestEqual(TEXT("Long runs have bounded rolling history"),L.LastFiveMinutes(101400).XP,int64(300));
    TestEqual(TEXT("Long run totals retain all gains"),L.XP,int64(100000));

    TMap<int32,FACEWorldObject> Objects;
    // ACE's Prismatic Taper (20631): use its real item icon, not toolbar artwork.
    auto Item=[&](int32 Id,int32 Parent,int32 Count,int32 Class=20631){FACEWorldObject O;O.Guid=Id;O.ContainerId=Parent;O.WeenieClassId=Class;
        O.Name=TEXT("Prismatic Taper");O.IconId=0x0600262A;O.ItemType=4096;O.StackSize=Count;Objects.Add(Id,O);};
    Item(2,1,1,90);Item(3,2,1,91);Item(10,1,20);Item(11,2,30);Item(12,3,40);Item(13,888,99);
    Item(14,0,5);Objects[14].WielderId=1;Item(15,1,0);Item(16,17,7);Item(17,16,7);
    TArray<FACESupplyEntry> Entries;FACESupplyEntry E;E.ClassId=20631;E.Name=TEXT("Prismatic Taper");E.CopyAppearance(Objects[10]);Entries.Add(E);
    ACELedger::Count(Entries,1,Objects);TestEqual(TEXT("All owned packs and equipped items count; corpse items and cycles do not"),Entries[0].Count,int64(96));
    Objects.Remove(10);Objects[11].StackSize=3;ACELedger::Count(Entries,1,Objects);TestEqual(TEXT("Consumed stacks and transfers update counts"),Entries[0].Count,int64(49));
    ACELedger::Count(Entries,100,Objects);TestEqual(TEXT("Other character does not inherit counts"),Entries[0].Count,int64(0));
    TestEqual(TEXT("Out-of-stock item remains tracked"),Entries.Num(),1);

    auto* GI=NewObject<UGameInstance>();GI->Init();
    TestTrue(TEXT("Retail DAT loads for visual verification"),GI->GetSubsystem<UACEDatSubsystem>()->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call")));
    auto* H=GI->GetSubsystem<UACEPluginSubsystem>();
    auto* Manager=GI->GetSubsystem<UACEClientSubsystem>()->GetUIElementManager();Manager->SetUiLocked(false);
    H->StorageRoot=FPaths::ProjectSavedDir()/TEXT("Automation/Ledgers");H->Settings=MakeShared<FJsonObject>();
    const auto Session=GI->GetSubsystem<UACEClientSubsystem>()->GetSession();
    Session->State=EACESessionState::InWorld;Session->PlayerGuid=1;Session->PlayerVitals=V;
    H->UpdateLedgers();const FString FirstCharacter=H->LedgerCharacter;
    Session->WorldObjects=Objects;++Session->InventoryDataRevision;H->UpdateLedgers();
    GI->GetSubsystem<UACEClientSubsystem>()->SelectObject(11);H->EditSupplySelection(true);
    TestEqual(TEXT("Adding the selected item retains its actual inventory icon"),H->GetSupplies()[0].Icon,0x0600262A);
    TestEqual(TEXT("Native ledger refreshes from inventory revisions"),H->GetSupplies()[0].Count,int64(49));
    Session->PlayerGuid=2;H->UpdateLedgers();TestEqual(TEXT("Second character has a separate list"),H->GetSupplies().Num(),0);
    Session->PlayerGuid=1;H->UpdateLedgers();TestEqual(TEXT("Returning to a character restores its list"),H->GetSupplies().Num(),1);
    FString Saved;TestTrue(TEXT("Supply selection persists to disk"),FFileHelper::LoadFileToString(Saved,*(H->StorageRoot/TEXT("settings.json"))));
    TestTrue(TEXT("Supplies are keyed by character"),Saved.Contains(TEXT("supplies.")+FirstCharacter)&&Saved.Contains(TEXT("Prismatic Taper")));
    TestEqual(TEXT("Item artwork survives character reload"),H->GetSupplies()[0].Icon,0x0600262A);
    TestEqual(TEXT("Item background type survives character reload"),H->GetSupplies()[0].ItemType,4096);
    H->SetWaypointOption(TEXT("arrow"),false);H->SetWaypointOption(TEXT("dungeon_overlay"),false);
    for(const TCHAR* Id:{TEXT("supplyledger"),TEXT("huntledger")})
    {
        auto P=H->Find(Id);if(!TestTrue(TEXT("HUD is discoverable"),P.IsValid()))continue;P->Enabled=true;
        H->SetLedgerOption(Id,TEXT("visible"),true);H->SavePluginWindowPosition(Id,FVector2D(80,80));
    }
    auto Dock=SNew(SACEPluginDesktop).Host(H);Dock->Tick(FGeometry::MakeRoot(FVector2D(1400,900),FSlateLayoutTransform()),1,.016f);
    TestTrue(TEXT("Saved HUD visibility restores without UCM running"),Dock->IsOpen(TEXT("supplyledger"))&&Dock->IsOpen(TEXT("huntledger")));
    TestEqual(TEXT("Supply HUD defaults to a compact width"),Dock->DisplaySize(Dock->Windows[TEXT("supplyledger")]).X,200.);
    const auto InitialSize=Dock->DisplaySize(Dock->Windows[TEXT("supplyledger")]);
    Dock->Resize(TEXT("supplyledger"),FVector2D(-35,90),true);
    const auto Resized=Dock->DisplaySize(Dock->Windows[TEXT("supplyledger")]);
    TestEqual(TEXT("Supply HUD can be made narrower"),Resized.X,165.);
    TestEqual(TEXT("Supply HUD can be made taller"),Resized.Y,InitialSize.Y+90.);
    Dock->Move(TEXT("supplyledger"),FVector2D(10,20),true);const auto At=Dock->Windows[TEXT("supplyledger")].Position;
    Manager->SetUiLocked(true);Dock->Move(TEXT("supplyledger"),FVector2D(50,50),true);
    TestEqual(TEXT("Pinned HUD cannot move"),Dock->Windows[TEXT("supplyledger")].Position,At);
    auto Reloaded=SNew(SACEPluginDesktop).Host(H);TestEqual(TEXT("HUD restores saved position"),Reloaded->Windows[TEXT("supplyledger")].Position,At);
    TestTrue(TEXT("Pin state survives window recreation"),Manager->IsUiLocked());
    TestEqual(TEXT("Supply width persists through recreation and pinning"),Reloaded->DisplaySize(Reloaded->Windows[TEXT("supplyledger")]).X,Resized.X);
    TestEqual(TEXT("Pinning removes only editor chrome from a resized ledger"),Reloaded->DisplaySize(Reloaded->Windows[TEXT("supplyledger")]).Y,Resized.Y-68.);
    const auto PinnedSize=Dock->Windows[TEXT("supplyledger")].Size;
    Dock->Resize(TEXT("supplyledger"),FVector2D(50,50),true);
    TestEqual(TEXT("Pinned ledger cannot be resized"),Dock->Windows[TEXT("supplyledger")].Size,PinnedSize);
    Dock->Toggle(TEXT("huntledger"));TestFalse(TEXT("Closing HUD persists visibility"),H->LedgerOption(TEXT("huntledger"),TEXT("visible")));
    auto Style=FACEPluginStyle::Use(H);TestTrue(TEXT("Retail glyph atlas resolves"),Style->Atlas!=nullptr);
    TestTrue(TEXT("Retail checkbox artwork resolves"),Style->Check.CheckedImage.GetResourceObject()!=nullptr);
    auto* Resources=GI->GetSubsystem<UACEClientSubsystem>()->GetUIResourceResolver();
    TestTrue(TEXT("Supply artwork resolves"),Style->ItemImage(E,true)->GetResourceObject()!=nullptr);
    TestTrue(TEXT("Supply foreground uses exactly the inventory renderer"),Style->ItemImage(E,true)->GetResourceObject()==Resources->ResolveItemForeground(E.Icon,E.Overlay,E.Effects));
    TestTrue(TEXT("Supply background uses exactly the inventory renderer"),Style->ItemImage(E,false)->GetResourceObject()==Resources->ResolveItemBackground(E.ItemType,E.Underlay));
    const auto Wide=SACEPluginIcon::Fit(FVector2D(90,30));const auto Tall=SACEPluginIcon::Fit(FVector2D(30,90));
    TestTrue(TEXT("Wide toolbar cells preserve square icon proportions"),FMath::IsNearlyEqual(Wide.GetScale(),30.f/32.f)&&FVector2D(Wide.GetTranslation()).Equals(FVector2D(30,0)));
    TestTrue(TEXT("Tall toolbar cells center instead of stretching icons"),FMath::IsNearlyEqual(Tall.GetScale(),30.f/32.f)&&FVector2D(Tall.GetTranslation()).Equals(FVector2D(0,30)));
    if(FApp::CanEverRender())
    {
        H->LedgerSupplies=Entries;H->LedgerSupplies[0].Count=56;++H->SupplyRevision;
        H->HuntLedger=L;H->HuntLedger.Started=FPlatformTime::Seconds()-3600;
        int64 NextThreshold=0;GI->GetSubsystem<UACEDatSubsystem>()->TryGetXpToNextLevel(0,100,NextThreshold);
        Session->PlayerVitals.Level=100;Session->PlayerVitals.TotalExperience=NextThreshold-100000;
        Manager->SetUiLocked(false);H->Find(TEXT("ucm"))->Enabled=true;H->SavePluginWindowPosition(TEXT("ucm"),FVector2D(430,40));Dock->Toggle(TEXT("ucm"));
        H->SavePluginWindowPosition(TEXT("huntledger"),FVector2D(80,380));Dock->Windows[TEXT("huntledger")].Position=FVector2D(80,380);Dock->Toggle(TEXT("huntledger"));
        Dock->Refresh();
        const FVector2D Size(1280,800);FWidgetRenderer Renderer(false,true);auto* Target=FWidgetRenderer::CreateTargetFor(Size,TF_Bilinear,true);
        for(int32 Pass=0;Pass<6;++Pass){++GFrameCounter;Renderer.DrawWidget(Target,Dock,Size,0);FlushRenderingCommands();}
        TFunction<void(TSharedRef<SWidget>)> InspectText=[&](TSharedRef<SWidget> W)
        {
            if(W->GetType()==FName(TEXT("SACEPluginText")))
            {
                const auto T=StaticCastSharedRef<SACEPluginText>(W);
                if(T->GetText().ToString().Contains(TEXT("Choose activities")))
                    TestTrue(TEXT("Wrapped plugin help reserves multiple lines"),T->GetDesiredSize().Y>25);
            }
            auto Children=W->GetChildren();for(int32 I=0;I<Children->Num();++I)InspectText(Children->GetChildAt(I));
        };InspectText(Dock);
        TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(Target->SizeX,Target->SizeY,[&]{TArray<FColor> Pixels;Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels);return Pixels;}(),PNG);
        FFileHelper::SaveArrayToFile(PNG,*(H->StorageRoot/TEXT("plugin-windows.png")));
        TFunction<int32(TSharedRef<SWidget>,FName)> CountControls=[&](TSharedRef<SWidget> W,FName Tag)
        {if(!W->GetVisibility().IsVisible())return 0;int32 N=W->GetTag()==Tag?1:0;auto Children=W->GetChildren();for(int32 I=0;I<Children->Num();++I)N+=CountControls(Children->GetChildAt(I),Tag);return N;};
        TFunction<TSharedPtr<SWidget>(TSharedRef<SWidget>,FName)> FindTagged=[&](TSharedRef<SWidget> W,FName Tag)->TSharedPtr<SWidget>
        {if(W->GetTag()==Tag)return W;auto Children=W->GetChildren();for(int32 I=0;I<Children->Num();++I)if(auto Found=FindTagged(Children->GetChildAt(I),Tag))return Found;return nullptr;};
        const auto SupplyContent=FindTagged(Dock->Windows[TEXT("supplyledger")].Widget.ToSharedRef(),TEXT("LedgerContent"));
        const auto HuntContent=FindTagged(Dock->Windows[TEXT("huntledger")].Widget.ToSharedRef(),TEXT("LedgerContent"));
        if(!TestTrue(TEXT("Both ledger frames expose their content"),SupplyContent.IsValid()&&HuntContent.IsValid()))return false;
        const auto SupplyAnchor=SupplyContent->GetCachedGeometry().GetAbsolutePosition();
        const auto HuntAnchor=HuntContent->GetCachedGeometry().GetAbsolutePosition();
        auto Estimate=StaticCastSharedPtr<SACEPluginText>(FindTagged(Dock,TEXT("HuntNextLevel")));
        if(!TestTrue(TEXT("Hunt ledger shows an estimate below totals"),Estimate.IsValid()))return false;
        TestTrue(TEXT("Live estimate uses remaining level XP and earned run rate"),Estimate->GetText().ToString().StartsWith(TEXT("1h")));
        Session->PlayerVitals.Level=100000;
        ++GFrameCounter;Renderer.DrawWidget(Target,Dock,Size,0);FlushRenderingCommands();
        TestEqual(TEXT("Level cap is reported instead of a fabricated time"),Estimate->GetText().ToString(),FString(TEXT("Max level")));
        Session->PlayerVitals.Level=100;
        const auto BeforeReset=H->HuntLedger;H->ResetHuntLedger();
        ++GFrameCounter;Renderer.DrawWidget(Target,Dock,Size,0);FlushRenderingCommands();
        TestEqual(TEXT("UI waits for fresh gains after Reset run"),Estimate->GetText().ToString(),FString(TEXT("Waiting for XP")));
        H->HuntLedger=BeforeReset;
        TestTrue(TEXT("Unlocked ledger offers add/remove"),CountControls(Dock,TEXT("LedgerEdit"))>0);
        TestEqual(TEXT("Only one add control plus one remove per supply row"),CountControls(Dock,TEXT("LedgerEdit")),H->GetSupplies().Num()+1);
        TestEqual(TEXT("Unlocked ledgers show both title bars"),CountControls(Dock,TEXT("LedgerHeader")),2);
        Manager->SetUiLocked(true);
        for(int32 Pass=0;Pass<3;++Pass){++GFrameCounter;Renderer.DrawWidget(Target,Dock,Size,0);FlushRenderingCommands();}
        TestEqual(TEXT("Pinned ledger hides add/remove controls"),CountControls(Dock,TEXT("LedgerEdit")),0);
        TestEqual(TEXT("Pinned hunt ledger hides reset"),CountControls(Dock,TEXT("LedgerReset")),0);
        TestEqual(TEXT("Pinned ledgers hide both title bars"),CountControls(Dock,TEXT("LedgerHeader")),0);
        TestTrue(TEXT("Pinning supply ledger keeps rows at the same screen position"),SupplyContent->GetCachedGeometry().GetAbsolutePosition().Equals(SupplyAnchor,.01f));
        TestTrue(TEXT("Pinning hunt ledger keeps totals at the same screen position"),HuntContent->GetCachedGeometry().GetAbsolutePosition().Equals(HuntAnchor,.01f));
        TestEqual(TEXT("The level estimate remains visible when pinned"),CountControls(Dock,TEXT("HuntNextLevel")),1);
        PNG.Reset();FImageUtils::PNGCompressImageArray(Target->SizeX,Target->SizeY,[&]{TArray<FColor> Pixels;Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels);return Pixels;}(),PNG);
        FFileHelper::SaveArrayToFile(PNG,*(H->StorageRoot/TEXT("plugin-windows-pinned.png")));
        Manager->SetQuickShowUi(true);
        for(int32 Pass=0;Pass<3;++Pass){++GFrameCounter;Renderer.DrawWidget(Target,Dock,Size,0);FlushRenderingCommands();}
        TestEqual(TEXT("Quick-show restores both title bars"),CountControls(Dock,TEXT("LedgerHeader")),2);
        TestTrue(TEXT("Quick-show restores inventory controls"),CountControls(Dock,TEXT("LedgerEdit"))>0);
        TestFalse(TEXT("Quick-show temporarily unlocks shared UI"),Manager->IsUiLocked());
        TestTrue(TEXT("Quick-show does not shift supply rows"),SupplyContent->GetCachedGeometry().GetAbsolutePosition().Equals(SupplyAnchor,.01f));
        TestTrue(TEXT("Quick-show does not shift hunt totals"),HuntContent->GetCachedGeometry().GetAbsolutePosition().Equals(HuntAnchor,.01f));
        Dock->Move(TEXT("supplyledger"),FVector2D(10,20),true);
        const auto Moved=Dock->Windows[TEXT("supplyledger")].Position;
        TestFalse(TEXT("Quick-show permits moving a pinned HUD"),Moved.Equals(At));
        Manager->SetQuickShowUi(false);
        for(int32 Pass=0;Pass<3;++Pass){++GFrameCounter;Renderer.DrawWidget(Target,Dock,Size,0);FlushRenderingCommands();}
        TestTrue(TEXT("Releasing quick-show restores permanent lock"),Manager->IsUiLocked());
        TestEqual(TEXT("Releasing quick-show hides header again"),CountControls(Dock,TEXT("LedgerHeader")),0);
        TestEqual(TEXT("Releasing quick-show preserves new position"),Dock->Windows[TEXT("supplyledger")].Position,Moved);
        Manager->ToggleUiLocked();
        for(int32 Pass=0;Pass<3;++Pass){++GFrameCounter;Renderer.DrawWidget(Target,Dock,Size,0);FlushRenderingCommands();}
        TestEqual(TEXT("Radar lock toggle unlocks both ledgers"),CountControls(Dock,TEXT("LedgerHeader")),2);
        Manager->SetQuickShowUi(true);Manager->SetQuickShowUi(false);
        TestFalse(TEXT("Quick-show release does not lock permanently unlocked UI"),Manager->IsUiLocked());
        for(const TCHAR* Id:{TEXT("supplyledger"),TEXT("huntledger")})Dock->Windows[Id].Position.Y=790;
        for(int32 Pass=0;Pass<3;++Pass){++GFrameCounter;Renderer.DrawWidget(Target,Dock,Size,0);FlushRenderingCommands();}
        const auto BottomSupply=SupplyContent->GetCachedGeometry().GetAbsolutePosition(),BottomHunt=HuntContent->GetCachedGeometry().GetAbsolutePosition();
        Manager->SetUiLocked(true);
        for(int32 Pass=0;Pass<3;++Pass){++GFrameCounter;Renderer.DrawWidget(Target,Dock,Size,0);FlushRenderingCommands();}
        TestTrue(TEXT("Bottom-edge clamping keeps supply content fixed when pinning"),SupplyContent->GetCachedGeometry().GetAbsolutePosition().Equals(BottomSupply,.01f));
        TestTrue(TEXT("Bottom-edge clamping keeps hunt content fixed when pinning"),HuntContent->GetCachedGeometry().GetAbsolutePosition().Equals(BottomHunt,.01f));
        auto Icons=SNew(SHorizontalBox);
        for(const TCHAR* Id:{TEXT("ucm"),TEXT("ucmmicro"),TEXT("looteditor"),TEXT("itemtools"),TEXT("waypoint"),TEXT("waypoint.map"),TEXT("ucm.log"),TEXT("supplyledger"),TEXT("huntledger"),TEXT("transmog"),TEXT("custom")})
            Icons->AddSlot().AutoWidth()[SNew(SBox).WidthOverride(70).HeightOverride(50)[SNew(SACEPluginIcon).Id(Id)]];
        Renderer.DrawWidget(Target,Icons,FVector2D(770,50),0);FlushRenderingCommands();
        PNG.Reset();FImageUtils::PNGCompressImageArray(Target->SizeX,Target->SizeY,[&]{TArray<FColor> Pixels;Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels);return Pixels;}(),PNG);
        FFileHelper::SaveArrayToFile(PNG,*(H->StorageRoot/TEXT("plugin-icons.png")));
        auto Labels=SNew(SVerticalBox);
        for(int32 FontSize:{10,12,14,16,18,24})
        {
            auto Label=SNew(SACEPluginText).Text(FText::FromString(TEXT("Pin map to interface - illi iii Inventory"))).Font(FCoreStyle::GetDefaultFontStyle("Regular",FontSize));
            Labels->AddSlot().AutoHeight()[Label];
            TestTrue(TEXT("Bitmap labels never compress native one-pixel strokes"),Label->ComputeDesiredSize(1).Y>=Style->Typeface(FontSize)->Font.MaxCharHeight);
        }
        Renderer.DrawWidget(Target,Labels,FVector2D(770,220),0);FlushRenderingCommands();
        PNG.Reset();FImageUtils::PNGCompressImageArray(Target->SizeX,Target->SizeY,[&]{TArray<FColor> Pixels;Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels);return Pixels;}(),PNG);
        FFileHelper::SaveArrayToFile(PNG,*(H->StorageRoot/TEXT("plugin-text.png")));
        Target->ReleaseResource();
    }
    Session->State=EACESessionState::Disconnected;GI->Shutdown();return true;
}
#endif
