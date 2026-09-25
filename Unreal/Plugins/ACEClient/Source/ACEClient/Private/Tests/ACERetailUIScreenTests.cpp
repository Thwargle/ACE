#include "UI/ACERetailTextEntry.h"
#include "Widgets/SVirtualWindow.h"
#include "Widgets/Layout/SDPIScaler.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ACEDatSubsystem.h"
#include "ACEClientSubsystem.h"
#include "ACESession.h"
#include "Protocol/ACECharacterTitleNames.inl"
#include "ACEInventoryRules.h"
#include "ACEInputBindings.h"
#include "RetailCustomKeymap.inl"
#include "GameFramework/PlayerInput.h"
#include "ACECameraSettings.h"
#include "ACERuntimeOptions.h"
#include "UI/ACEFrameRateWidget.h"
#include "Components/CheckBox.h"
#include "UI/ACEVideoSettingsWidget.h"
#include "Components/ComboBoxString.h"
#include "Components/Slider.h"
#include "Components/EditableTextBox.h"
#include "Components/MultiLineEditableText.h"
#include "Engine/UserInterfaceSettings.h"
#include "UI/ACEUIFontStyles.h"
#include "Dat/ACEDatTextLayout.h"
#include "Components/Button.h"
#include "Misc/ConfigCacheIni.h"
#include "ACEPlayerController.h"
#include "ACEMouseCursorWidget.h"
#include "ACECharacterAppearanceComponent.h"
#include "ACEWorldEntityActor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Components/Image.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/MeshComponent.h"
#include "ACERetailPaperDoll.h"
#include "ProceduralMeshComponent.h"
#include "ShaderCompiler.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"
#include "Components/ScrollBox.h"
#include "Components/ProgressBar.h"
#include "Components/Border.h"
#include "UI/ACEUICanvasWidget.h"
#include "UI/ACEUICharSelectBinder.h"
#include "UI/ACEUIGameplayBinder.h"
#include "UI/ACEUIElementManager.h"
#include "UI/ACEUILayoutResolver.h"
#include "UI/ACEUIResourceResolver.h"
#include "UI/ACERetailTextBlock.h"
#include "UI/ACERetailKeySelector.h"
#include "Engine/GameInstance.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Slate/WidgetRenderer.h"
#include "Input/HittestGrid.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/CanvasPanel.h"
#include "ImageUtils.h"
#include "RenderingThread.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/GarbageCollection.h"
#include "Framework/Application/SlateApplication.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACERetailScreenTest,"ACE.RetailParity.UIScreens",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACERetailScreenTest::RunTest(const FString& Parameters)
{
    TGuardValue<FString> ScreenSettingsPath(GGameUserSettingsIni,FPaths::ProjectSavedDir()/TEXT("Automation/ScreenPreferencesFixture.ini"));
    FConfigFile ScreenPreferences;ScreenPreferences.NoSave=true;GConfig->SetFile(GGameUserSettingsIni,&ScreenPreferences);
    TestEqual(TEXT("Gameplay uses physical pixels at 1440p"),GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(FIntPoint(2560,1440)),1.f);
    TestEqual(TEXT("Retail base font is distinct from sans"),ACEUIFontStyles::FontIdForStyleName(TEXT("basefont16")),0x40000000u);
    TestEqual(TEXT("Retail sans font retains its own atlas"),ACEUIFontStyles::FontIdForStyleName(TEXT("sansfont16")),0x40000009u);
    TestEqual(TEXT("Named medium field font resolves without numeric suffix"),ACEUIFontStyles::FontIdForStyleName(TEXT("fieldvaluemedium")),0x40000000u);
    auto* GI=NewObject<UGameInstance>();
    GI->Init();
    auto* Dat=GI->GetSubsystem<UACEDatSubsystem>();
    if (!Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call"))) return false;
    auto* Resources=NewObject<UACEUIResourceResolver>(); Resources->Initialize(Dat);
    FACEDatFont BaseFont; Resources->ResolveFont(0x40000000u,BaseFont);
    bool bGlyphTint=false; Resources->ResolveFontAtlas(BaseFont.ForegroundSurfaceDataID,bGlyphTint);
    TestTrue(TEXT("Retail glyph atlases accept buff and debuff colors"),bGlyphTint);
    for (int32 Heritage : {1,12,13})
        TestTrue(TEXT("Retail paperdoll animation resolves from enum group 7"),(Resources->ResolvePaperDollAnimation(Heritage)>>24)==3);
    TestEqual(TEXT("Human paperdoll uses the independent DAT mapping"),Resources->ResolvePaperDollAnimation(1),0x030003C0u);
    TSet<uint32> BodyMasks;
    for (int32 Y=0; Y<256; ++Y) for (int32 X=0; X<256; ++X)
        if (const uint32 Mask = Resources->ResolvePaperDollSelectionMask(FIntPoint(X,Y))) BodyMasks.Add(Mask);
    TestEqual(TEXT("Authored paperdoll click map contains all nine body regions"), BodyMasks.Num(), 9);
    TestEqual(TEXT("Click outside the paperdoll map selects nothing"), Resources->ResolvePaperDollSelectionMask(FIntPoint(-1,0)), 0u);
    TestEqual(TEXT("Creature names resolve through retail enum mapper"), Resources->ResolveEnumString(0x10000005, 30), FString(TEXT("Skeleton")));
    TestEqual(TEXT("Creature enum preserves authored separators"), Resources->ResolveEnumString(0x10000005, 38), FString(TEXT("Fire_Elemental")));
    TestTrue(TEXT("Unrecognized creature enum stays absent"), Resources->ResolveEnumString(0x10000005, 9999).IsEmpty());
    TestEqual(TEXT("Weapon item background from retail DidMapper"),Resources->ResolveItemBackgroundId(1),0x060011CBu);
    TestEqual(TEXT("Armor background differs from weapon background"),Resources->ResolveItemBackgroundId(2),0x060011CFu);
    TestEqual(TEXT("Multi-type item uses lowest set type bit"),Resources->ResolveItemBackgroundId(3),0x060011CBu);
    TestEqual(TEXT("Unknown item background uses enum 33"),Resources->ResolveItemBackgroundId(0),0x060011D4u);
    TestNotNull(TEXT("Item background composite decodes"),Resources->ResolveItemBackground(1,0));
    auto* Manager=NewObject<UACEUIElementManager>(); Manager->Initialize();
    auto* Layout=NewObject<UACEUILayoutResolver>(); Layout->Initialize(Dat,Manager);
    if (!Layout->LoadLayout(0x21000004)) return false;
    TestEqual(TEXT("Character selection remains unscaled on larger screens"),Manager->GetCanvasScale(FVector2D(2560,1440)),FVector2D(1,1));
    const auto Enter=Manager->FindElementByName(TEXT("EnterGameButton"));
    if (!TestTrue(TEXT("Character-select Enter element exists"),Enter.IsValid())) return false;
    TestEqual(TEXT("Enter pressed state retains retail smaller font"),Enter->States[3].FontId,0x40000011u);
    auto* Canvas=NewObject<UACEUICanvasWidget>(); Canvas->Initialize();
    Canvas->InitializeCanvas(Manager); Canvas->SetResourceResolver(Resources);
    auto Slate=Canvas->TakeWidget();
    auto* Binder=NewObject<UACEUICharSelectBinder>();
    FACECharacterInfo First;First.CharacterId=1;First.Name=TEXT("Adventurer");
    FACECharacterInfo Second;Second.CharacterId=2;Second.Name=TEXT("Recreated");
    Binder->Initialize(nullptr,Manager,Canvas,nullptr,{First,Second},TEXT("Retail test"));
    Canvas->SetCharSelectBinder(Binder);
    const auto* OriginalLayer=Canvas->GetElementLayer();
    Canvas->RebuildWidget();
    TestTrue(TEXT("Viewport-to-VR reconstruction preserves the label layer"),OriginalLayer==Canvas->GetElementLayer());
    for (const auto& Name : Binder->CharNameImages)
        TestTrue(TEXT("Character names stay attached after Slate reconstruction"),Name && Name->GetParent()==Canvas->GetElementLayer());
    if (FApp::CanEverRender())
    {
        FWidgetRenderer Renderer(true,true);
        for (const FIntPoint Size : {FIntPoint(800,600),FIntPoint(1280,960),FIntPoint(1600,900)})
        {
            auto* Target=FWidgetRenderer::CreateTargetFor(FVector2D(Size),TF_Bilinear,true);
            for (int32 Pass=0; Pass<4; ++Pass)
            {
                Renderer.DrawWidget(Target,Slate,FVector2D(Size),0.f); FlushRenderingCommands();
                Canvas->NativeTick(Canvas->GetCachedGeometry(),0.f);
            }
            TArray<FColor> Pixels;
            Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels);
            if(Size.X>800) TestTrue(TEXT("Unscaled character screen has an opaque black surround"),
                !Pixels.IsEmpty() && Pixels[0].R==0 && Pixels[0].G==0 && Pixels[0].B==0 && Pixels[0].A==255);
            TArray64<uint8> PNG; FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,PNG);
            FFileHelper::SaveArrayToFile(PNG,*(FPaths::ProjectSavedDir()/FString::Printf(
                TEXT("Automation/RetailParity/CharSelect_%dx%d.png"),Size.X,Size.Y)));
            bool bFoundLabel=false;
            Canvas->WidgetTree->ForEachWidget([&](UWidget* Widget)
            {
                auto* Label=Cast<UACERetailTextBlock>(Widget);
                if (!Label || Label->GetText().ToString()!=TEXT("ENTER")) return;
                bFoundLabel=true;
                const auto* Slot=Cast<UCanvasPanelSlot>(Label->Slot);
                const FVector2D Expected=FVector2D(Enter->GetScreenOrigin())*Canvas->GetLastScale2D();
                TestTrue(TEXT("Enter text uses exactly the button origin at each resolution"),Slot && Slot->GetPosition().Equals(Expected,.01));
                TestTrue(TEXT("Enter text uses exactly the button extent"),Slot && Slot->GetSize().Equals(
                    FVector2D(Enter->Width,Enter->Height)*Canvas->GetLastScale2D(),.01));
            });
            TestTrue(TEXT("Character-select uses DAT text widget"),bFoundLabel);
        }
    }
    const auto List=Manager->FindElementByName(TEXT("CharacterListBox"));
    if(List.IsValid())
    {
        const FVector2D Origin(List->GetScreenOrigin());
        Binder->TryHandleOverlayClick(Origin+FVector2D(20,40));
        TestEqual(TEXT("Second readable character row selects its own character"),Binder->SelectedCharacterId,2);
    }
    if (!Binder->CharNameCacheTex.IsEmpty() && Binder->CharNameCacheTex[0])
    {
        const auto* Texture=Binder->CharNameCacheTex[0].Get();
        auto& Mip=Texture->GetPlatformData()->Mips[0];
        const auto* Pixels=static_cast<const FColor*>(Mip.BulkData.LockReadOnly());
        int32 Soft=0,Dimmed=0;
        for (int32 I=0; I<Mip.SizeX*Mip.SizeY; ++I)
            if(Pixels[I].A>=32 && Pixels[I].A<224) { ++Soft; if(Pixels[I].R<220) ++Dimmed; }
        Mip.BulkData.Unlock();
        TestTrue(TEXT("DAT character glyphs retain antialiased coverage"),Soft>5);
        TestEqual(TEXT("Character glyph RGB is not darkened by alpha twice"),Dimmed,0);
    }
    if (List.IsValid())
    {
        for (int32 I=2; I<20; ++I)
        {
            FACECharacterInfo Character; Character.CharacterId=I+1;
            Character.Name=FString::Printf(TEXT("Character %d"),I+1);
            Binder->Characters.Add(Character);
        }
        Binder->TickRefresh();
        const float RowHeight=Binder->GetCharacterRowHeight();
        TestTrue(TEXT("Twenty-character roster fits the list"),20*RowHeight<=List->Height);
        Binder->TryHandleOverlayClick(FVector2D(List->GetScreenOrigin())+FVector2D(20,19.5f*RowHeight));
        TestEqual(TEXT("Last character remains selectable on a full roster"),Binder->SelectedCharacterId,20);
    }
    Binder->Shutdown(); Canvas->SetCharSelectBinder(nullptr);
    Layout->LoadLayout(0x21000005);
    auto* Client=NewObject<UACEClientSubsystem>(GI);
    Client->Session=MakeShared<FACESession>(); Client->Session->State=EACESessionState::InWorld;
    Client->Session->PlayerGuid=1234;
    FACEWorldObject Player; Player.Guid=1234; Player.Name=TEXT("Retail test"); Player.ItemsCapacity=102;
    Client->Session->WorldObjects.Add(Player.Guid,Player);
    FACEWorldObject Item; Item.Guid=2345; Item.Name=TEXT("Test inventory wand"); Item.ContainerId=1234;
    Item.ItemType=ACEItemType::Caster; Item.IconId=0x060010F9; Item.PlacementPosition=0;
    Client->Session->WorldObjects.Add(Item.Guid,Item);
    Client->Session->ShortcutObjects.SetNumZeroed(18); Client->Session->ShortcutObjects[2]=Item.Guid;
    auto* Gameplay=NewObject<UACEUIGameplayBinder>(); Gameplay->Initialize(Client,Manager,Canvas,nullptr);
    Canvas->SetGameplayBinder(Gameplay);
    Gameplay->TickRefresh();
    const auto Tab=Manager->FindElementByName(TEXT("Spellcast_Tab2"));
    if (TestTrue(TEXT("Casting tab exists"),Tab.IsValid()))
    {
        TestTrue(TEXT("Casting text tab is an activatable hit target"),Tab->IsInteractiveHitTarget());
        for (auto P=Tab; P; P=P->Parent.Pin()) P->bVisible=true;
        Manager->ApplyEdgeAnchoredLayout(1600,900);
        const auto Origin=Tab->GetScreenOrigin();
        const FVector2D Pos(Origin.X+Tab->Width/2,Origin.Y+Tab->Height/2);
        const auto Hit=Manager->HitTestCanvas(Pos.X,Pos.Y);
        AddInfo(FString::Printf(TEXT("Casting tab at %s hit=%s scale=%s"),*Pos.ToString(),
            Hit ? *Hit->ElementName : TEXT("none"),*Manager->GetCanvasScale(FVector2D(1600,900)).ToString()));
        TSharedPtr<FACEUIElement> Activated;
        const auto ClickHandle=Manager->OnElementActivated.AddLambda([&](TSharedPtr<FACEUIElement> E){Activated=E;});
        Manager->NotifyMouseDown(Pos,FVector2D(1600,900),EKeys::LeftMouseButton);
        Manager->NotifyMouseUp(Pos,FVector2D(1600,900),EKeys::LeftMouseButton);
        TestTrue(TEXT("Mouse press and release activate the correct casting tab"),Activated==Tab);
        Manager->OnElementActivated.Remove(ClickHandle);
    }
    Gameplay->SyncCombatModeFromServer(2);
    Gameplay->TickRefresh(); Manager->ApplyEdgeAnchoredLayout(1600,900);
    auto Click=[&](const TCHAR* Name)
    {
        const auto Element=Manager->FindElementByName(Name);
        if (!TestTrue(Name,Element.IsValid())) return;
        const auto Origin=Element->GetScreenOrigin();
        const FVector2D Pos(Origin.X+Element->Width/2,Origin.Y+Element->Height/2);
        Manager->NotifyMouseDown(Pos,FVector2D(1600,900),EKeys::LeftMouseButton);
        Manager->NotifyMouseUp(Pos,FVector2D(1600,900),EKeys::LeftMouseButton);
        Gameplay->TickRefresh();
    };
    const bool RepeatBefore=Gameplay->bCombatAutoRepeat;
    Click(TEXT("AutoRepeatAttack"));
    TestTrue(TEXT("Combat repeat checkbox click changes its value"),Gameplay->bCombatAutoRepeat!=RepeatBefore);
    const auto Repeat=Manager->FindElementByName(TEXT("AutoRepeatAttack"));
    TestTrue(TEXT("DAT composite checkbox carries boolean button behavior"),Repeat && Repeat->bBooleanButton);
    if (Repeat)
    {
        Repeat->ResolvePaintState(false,false,false);
        TestEqual(TEXT("Checkbox paints its authored on/off state"),Repeat->PaintState,Gameplay->bCombatAutoRepeat ? 6u : 1u);
    }
    Click(TEXT("LowAttack"));
    TestEqual(TEXT("Combat height click selects low attack"),Gameplay->CombatAttackHeight,ACEAttackHeight::Low);
    const auto Slider=Manager->FindElementByName(TEXT("PowerSlider"));
    if (Slider)
    {
        const auto Origin=Slider->GetScreenOrigin();
        const FVector2D Position(Origin.X+Slider->Width*0.75,Origin.Y+Slider->Height/2);
        TestTrue(TEXT("Power track accepts drag"),Gameplay->TryBeginCombatPowerDrag(Position));
        Gameplay->TryFinishCombatPowerDrag(Position); Gameplay->TickRefresh();
        TestTrue(TEXT("Power drag updates requested attack power"),Gameplay->RequestedAttackPower>0.70f && Gameplay->RequestedAttackPower<0.8f);
        const float Requested=Gameplay->RequestedAttackPower;
        Click(TEXT("HighAttack"));
        TestEqual(TEXT("Height button preserves the selected slider power"),Gameplay->RequestedAttackPower,Requested);
        TestTrue(TEXT("Height activation queues one charged attack"),Gameplay->bCombatAttackRequestPending);
    }
    const bool RepeatSaved=Gameplay->bCombatAutoRepeat;
    Gameplay->ApplyCombatMode(static_cast<int32>(ACECombatMode::NonCombat));
    Gameplay->ApplyCombatMode(static_cast<int32>(ACECombatMode::Melee));
    TestEqual(TEXT("Combat mode changes preserve the repeat preference"),Gameplay->bCombatAutoRepeat,RepeatSaved);
    TestFalse(TEXT("Entering melee does not attack just because repeat is enabled"),Gameplay->bCombatAttackRequestPending);
    FIntPoint ScreenSize(1600,900);
    auto CaptureScreen=[&](const FString& Name,float DPIScale=1.f) -> TArray<FColor>
    {
        if (!FApp::CanEverRender()) return {};
        FWidgetRenderer Renderer(true,true);
        const TSharedRef<SWidget> Scaled=SNew(SDPIScaler).DPIScale(DPIScale)[Slate];
        auto* Target=FWidgetRenderer::CreateTargetFor(FVector2D(ScreenSize),TF_Bilinear,true);
        for (int32 Pass=0; Pass<4; ++Pass)
        {
            Canvas->NativeTick(Canvas->GetCachedGeometry(),0.f);
            Renderer.DrawWidget(Target,Scaled,FVector2D(ScreenSize),0.f); FlushRenderingCommands();
        }
        TArray<FColor> Pixels; Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels);
        TArray64<uint8> PNG; FImageUtils::PNGCompressImageArray(ScreenSize.X,ScreenSize.Y,Pixels,PNG);
        FFileHelper::SaveArrayToFile(PNG,*(FPaths::ProjectSavedDir()/TEXT("Automation/RetailParity")/(Name+TEXT(".png"))));
        return Pixels;
    };
    CaptureScreen(TEXT("GameplayCombat"));
    {
        const auto SavedPose=Client->Session->GetPlayerPosition();
        const auto SavedSelection=Client->Session->SelectedObject;
        FACEPosition Pose;Pose.CellId=0xDA55001D;Pose.Location=FVector(100,100,20);
        Client->Session->SetLocalPosition(Pose);
        FACEWorldObject RadarNPC;RadarNPC.Guid=24680;RadarNPC.ItemType=ACEItemType::Creature;
        RadarNPC.bHasPosition=true;RadarNPC.Position=Pose;RadarNPC.Position.Location.Y+=20;
        Client->Session->WorldObjects.Add(RadarNPC.Guid,RadarNPC);
        Gameplay->RefreshRadarOverlays();CaptureScreen(TEXT("GameplayRetailRadar"));
        const int32 Index=Gameplay->RadarBlipGuids.IndexOfByKey(RadarNPC.Guid);
        if(TestTrue(TEXT("Nearby creature appears on the minimap"),Index!=INDEX_NONE))
        {
            auto* Blip=Gameplay->RadarBlips[Index].Get();
            const auto& Geo=Blip->GetCachedGeometry();
            // Just outside the small sprite, but inside retail's six-pixel tolerance.
            const FVector2D Absolute=Geo.LocalToAbsolute(Geo.GetLocalSize()*.5+FVector2D(5,0));
            TestTrue(TEXT("Radar click accepts the retail pick tolerance"),Gameplay->TryHandleOverlayClick(
                Canvas->GetElementLayer()->GetCachedGeometry().AbsoluteToLocal(Absolute),false));
            TestEqual(TEXT("Radar click selects the nearby creature"),Client->GetSelectedObject().Guid,RadarNPC.Guid);
            Gameplay->RefreshRadarOverlays();CaptureScreen(TEXT("GameplayRetailRadarSelected"));
            TestTrue(TEXT("Selected radar marker uses the retail square mask"),
                Blip->Background.GetResourceObject()==Resources->ResolveRadarBlip(4,true));
        }
        Client->Session->WorldObjects.Remove(RadarNPC.Guid);
        Client->Session->SetLocalPosition(SavedPose);Client->Session->SelectedObject=SavedSelection;
        Gameplay->HandleSelectionChanged(SavedSelection);Gameplay->RefreshRadarOverlays();
    }
    const FString LongChat = TEXT("A long incoming tell must wrap all of its words into several visible lines instead of hiding the rest of the message beneath the next row. This final sentence must also remain readable.");
    for (int32 W : {0, 1})
    {
        if (W) Gameplay->SetFloatyVisible(TEXT("RootGameplay_FloatyChat1_Field"), true);
        Gameplay->AppendChatLineToLog(W, LongChat, FLinearColor::White, TEXT("Sender"));
    }
    CaptureScreen(TEXT("GameplayWrappedChat"));
    for (int32 W : {0, 1})
    {
        auto* Log = Gameplay->GetChatLogWidget(W);
        auto* Row = CastChecked<UACERetailTextBlock>(Log->GetChildAt(Log->GetChildrenCount()-1));
        TestTrue(TEXT("Incoming chat reserves height for every wrapped line"),
            Row->GetCachedGeometry().GetLocalSize().Y > BaseFont.MaxCharHeight * 2);
        TestTrue(TEXT("Chat layout and allocated row agree after wrapping"),
            FMath::IsNearlyEqual(Row->GetDesiredSize().Y,Row->GetCachedGeometry().GetLocalSize().Y,1.f));
    }
    Gameplay->SetFloatyVisible(TEXT("RootGameplay_FloatyChat1_Field"), false);
    {
        // Exercise real DAT resize grips, reflow and scroll routing in all five windows.
        TGuardValue<FString> SettingsPath(GGameUserSettingsIni,FPaths::ProjectSavedDir()/TEXT("Automation/ChatResizeFixture.ini"));
        FConfigFile Config; Config.NoSave=false; Config.bCanSaveAllSections=true;
        GConfig->SetFile(GGameUserSettingsIni,&Config);
        const bool Locked=Manager->IsUiLocked(); Manager->SetUiLocked(false);
        for (int32 W=0; W<=4; ++W)
        {
            const FString Name=W ? FString::Printf(TEXT("RootGameplay_FloatyChat%d_Field"),W) : TEXT("RootGameplay_FloatyMainChat_Field");
            auto Root=Manager->FindElementByName(Name);
            const int32 SavedW=Root->UserResizeW,SavedH=Root->UserResizeH;
            const FIntPoint SavedDrag(Root->UserDragX,Root->UserDragY);
            Gameplay->SetFloatyVisible(Name,true); Manager->BringFloatyToFront(Root);
            Root->UserDragX+=600-Root->GetScreenOrigin().X;
            Root->UserDragY+=100-Root->GetScreenOrigin().Y; Root->RecomputeLayoutOffset();
            Gameplay->ClearChatLog(W);
            for (int32 I=0; I<15; ++I) Gameplay->AppendChatLineToLog(W,LongChat,FLinearColor::White,FString());
            CaptureScreen(FString::Printf(TEXT("Chat%dBeforeResize"),W));
            auto* Log=Gameplay->GetChatLogWidget(W);
            const float NarrowRowH=Log->GetChildAt(0)->GetDesiredSize().Y;
            const int32 Width=Root->Width,Height=Root->Height;
            auto Drag=[&](const TCHAR* GripName,FVector2D Delta)
            {
                const auto Grip=Manager->FindElementUnder(Name,GripName);
                const FVector2D Start=Canvas->LayoutToViewport(FVector2D(Grip->GetScreenOrigin())+FVector2D(2,2));
                Manager->NotifyMouseDown(Start,FVector2D(ScreenSize),EKeys::LeftMouseButton);
                Manager->NotifyMouseMove(Start+Delta,FVector2D(ScreenSize));
                Manager->NotifyMouseUp(Start+Delta,FVector2D(ScreenSize),EKeys::LeftMouseButton);
            };
            Drag(W ? TEXT("RightBorder") : TEXT("MainChatRightBorder"),FVector2D(180,0));
            TestEqual(TEXT("Side grip widens chat"),Root->Width,Width+180);
            TestEqual(TEXT("Side grip preserves height"),Root->Height,Height);
            Drag(W ? TEXT("BottomBorder") : TEXT("MainChatBottomBorder"),FVector2D(0,80));
            TestEqual(TEXT("Bottom grip resizes height"),Root->Height,Height+80);
            CaptureScreen(FString::Printf(TEXT("Chat%dWidened"),W));
            auto LogEl=Manager->FindElementUnder(Name,TEXT("ChatLog"));
            auto Bar=Manager->FindElementUnder(Name,TEXT("ChatLogScrollbar"));
            TestTrue(TEXT("Chat rows rewrap to the wider window"),Log->GetChildAt(0)->GetDesiredSize().Y<NarrowRowH);
            TestTrue(TEXT("Scroll region follows content height"),FMath::Abs(LogEl->Height-Bar->Height)<=1);
            TestEqual(TEXT("Scrollbar remains inset from right border"),Bar->GetScreenOrigin().X+Bar->Width,Root->GetScreenOrigin().X+Root->Width-5);
            const auto Up=Manager->FindElementUnder(Name,TEXT("ScrollBar_Up"));
            const auto Down=Manager->FindElementUnder(Name,TEXT("ScrollBar_Down"));
            TestTrue(TEXT("Both arrow controls remain visible"),Up->bVisible && Down->bVisible);
            TestEqual(TEXT("Top arrow has no duplicated reflow offset"),Up->GetScreenOrigin().Y,Bar->GetScreenOrigin().Y);
            TestEqual(TEXT("Bottom arrow remains within bar"),Down->GetScreenOrigin().Y+Down->Height,Bar->GetScreenOrigin().Y+Bar->Height);
            const auto Mid=Manager->FindElementUnder(Name,TEXT("widget_mid_field"));
            TestTrue(TEXT("Blue thumb has drawable area within the scrollbar"),Mid && Mid->Parent.Pin()->bVisible && Mid->Height>0 &&
                Mid->GetScreenOrigin().Y>=Bar->GetScreenOrigin().Y+16 && Mid->GetScreenOrigin().Y+Mid->Height<=Bar->GetScreenOrigin().Y+Bar->Height-16);
            const FVector2D LogPoint=Canvas->LayoutToViewport(FVector2D(LogEl->GetScreenOrigin())+FVector2D(12,12));
            TestEqual(TEXT("Wheel routing finds the correct chat"),Gameplay->ChatWindowAtPointer(LogPoint),W);
            const float Before=Log->GetScrollOffset(); Gameplay->ScrollChatLog(-28.f,W);
            TestTrue(TEXT("Chat scrolls independently upward"),Log->GetScrollOffset()<Before);
            const FVector2D BarPoint=Canvas->LayoutToViewport(FVector2D(Bar->GetScreenOrigin())+FVector2D(8,18));
            TestTrue(TEXT("Every chat thumb accepts dragging"),Gameplay->TryBeginScrollbarDrag(BarPoint));
            Gameplay->TryFinishScrollbarDrag();
            TestTrue(TEXT("Dragging reaches older messages"),Log->GetScrollOffset()<Log->GetScrollOffsetOfEnd()*.2f);
            const float Browsing=Log->GetScrollOffset(); Gameplay->AppendChatLineToLog(W,LongChat,FLinearColor::White,FString());
            CaptureScreen(FString::Printf(TEXT("Chat%dScrolled"),W));
            TestTrue(TEXT("New messages preserve the browsed position"),FMath::Abs(Log->GetScrollOffset()-Browsing)<1.f);
            Manager->SaveFloatyLayout();
            Root->UserResizeW=0; Root->UserResizeH=0; UACEUIElementManager::ApplyFloatyResizeLayout(Root);
            Manager->LoadFloatyLayout();
            TestEqual(TEXT("Chat width survives save/reload"),Root->Width,Width+180);
            TestEqual(TEXT("Chat height survives save/reload"),Root->Height,Height+80);
            Drag(W ? TEXT("RightBorder") : TEXT("MainChatRightBorder"),FVector2D(-180,0));
            CaptureScreen(FString::Printf(TEXT("Chat%dNarrowed"),W));
            TestEqual(TEXT("Narrowing restores original width"),Root->Width,Width);
            TestTrue(TEXT("Rows wrap again when narrowed"),Log->GetChildAt(0)->GetDesiredSize().Y>=NarrowRowH);
            Root->UserResizeW=SavedW; Root->UserResizeH=SavedH;
            Root->UserDragX=SavedDrag.X; Root->UserDragY=SavedDrag.Y;
            Root->RecomputeLayoutOffset(); UACEUIElementManager::ApplyFloatyResizeLayout(Root);
            Gameplay->ClearChatLog(W); Gameplay->AppendChatLineToLog(W,LongChat,FLinearColor::White,FString());
            if (W) Gameplay->SetFloatyVisible(Name,false);
        }
        Manager->SetUiLocked(Locked);
    }
    {
        TGuardValue<FACEPlayerVitals> SavedVitals(Client->Session->PlayerVitals, Client->Session->PlayerVitals);
        TGuardValue<uint32> SavedOptions(Client->Session->CharacterOptions1, Client->Session->CharacterOptions1);
        TGuardValue<bool> SavedNumbers(Gameplay->bShowVitalNumbers, Gameplay->bShowVitalNumbers);
        auto& V = Client->Session->PlayerVitals;
        V.bValid=true; V.Health=50; V.MaxHealth=100; V.Stamina=60; V.MaxStamina=100; V.Mana=70; V.MaxMana=100;
        for (int32 Percent : {0,50,100}) for (bool Side : {false,true}) for (bool Numbers : {false,true})
        {
            V.Health=V.Stamina=V.Mana=Percent;
            if (Side) Client->Session->CharacterOptions1 |= 0x00200000u;
            else Client->Session->CharacterOptions1 &= ~0x00200000u;
            Gameplay->bShowVitalNumbers=Numbers; Gameplay->RefreshVitalsOverlays();
            CaptureScreen(FString::Printf(TEXT("Vitals_%s_%s_%d"),Side?TEXT("Side"):TEXT("Stacked"),Numbers?TEXT("Numbers"):TEXT("Textures"),Percent));
            const TCHAR* Root=Side?TEXT("RootGameplay_FloatySideVitals_Field"):TEXT("RootGameplay_FloatyVitals_Field");
            for (const TCHAR* Name : {TEXT("HealthMeter"),TEXT("StaminaMeter"),TEXT("ManaMeter")})
            {
                const auto Meter=Manager->FindElementUnder(Root,Name);
                TestEqual(TEXT("Vital meter selects the authored detail state"),Meter->PaintState,Numbers?0x10000006u:0x10000007u);
                int32 Details=0;
                TArray<TSharedPtr<FACEUIElement>> Nodes{Meter};
                while (!Nodes.IsEmpty())
                {
                    const auto Node=Nodes.Pop(EAllowShrinking::No); Nodes.Append(Node->Children);
                    if(const auto* Paint=Canvas->PaintStates.Find(Node->InstanceId))
                    {
                        const FIntPoint Origin=Meter->GetScreenOrigin();const auto Scale=Canvas->GetLastScale2D();
                        TestTrue(TEXT("Vital artwork remains inside its meter vertically without stretching"),
                            Paint->Y>=FMath::RoundToInt(Origin.Y*Scale.Y)
                            && Paint->Y+Paint->H<=FMath::RoundToInt((Origin.Y+Meter->Height)*Scale.Y));
                        if(Percent==0)
                        {
                            auto Layer=Node;while(Layer && Layer->Parent.Pin()!=Meter)Layer=Layer->Parent.Pin();
                            TestFalse(TEXT("A zero vital paints no residual fill pixel"),Layer&&Layer->ElementName!=TEXT("meter_background")&&Layer->Type!=ACEUI::ElementType::Text);
                        }
                    }
                    if (Node->ElementName!=TEXT("detail")) continue;
                    ++Details;
                    const auto* State=Node->States.Find(Node->PaintState);
                    TestTrue(TEXT("Both background and fill detail select textured media only without numbers"),
                        State && (Numbers ? State->ImageFileId==0 : State->ImageFileId!=0 && Resources->ResolveTexture(State->ImageFileId,State->AlphaFileId)));
                }
                TestEqual(TEXT("Each vital retains both background and fill texture layers"),Details,2);
            }
            TestEqual(TEXT("Vital numeric overlay follows display preference"),Gameplay->HealthLabel->GetText().IsEmpty(),!Numbers);
        }
        // Save a readable, directly rendered close-up at 3x UI scale. No source
        // texture resize is involved: this also checks the actual scaled result.
        Client->Session->CharacterOptions1&=~0x00200000u;Gameplay->bShowVitalNumbers=false;
        for(float Scale:{1.f,3.f})
        {
            const auto Pixels=CaptureScreen(FString::Printf(TEXT("Vitals_Scale%d"),int32(Scale)),Scale);
            const auto Root=Manager->FindElementByName(TEXT("RootGameplay_FloatyVitals_Field"));
            const FIntPoint O=Root->GetScreenOrigin();const int32 W=Root->Width*Scale,H=Root->Height*Scale;
            if(Pixels.Num()==ScreenSize.X*ScreenSize.Y)
            {
                TArray<FColor> Crop;Crop.Reserve(W*H);
                for(int32 Y=0;Y<H;++Y)for(int32 X=0;X<W;++X)Crop.Add(Pixels[(int32(O.Y*Scale)+Y)*ScreenSize.X+int32(O.X*Scale)+X]);
                TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(W,H,Crop,PNG);
                FFileHelper::SaveArrayToFile(PNG,*(FPaths::ProjectSavedDir()/FString::Printf(TEXT("Automation/RetailParity/Vitals_Closeup%d.png"),int32(Scale))));
            }
        }
    }
    Gameplay->RefreshVitalsOverlays();
    CaptureScreen(TEXT("Vitals_ScaleRestored"));
    Gameplay->SetFloatyVisible(TEXT("OptionsPanel_Field"),false);
    FSlateApplication::Get().ClearKeyboardFocus(EFocusCause::Cleared);
    const FKeyEvent Escape(EKeys::Escape,FModifierKeysState(),0,false,0,0);
    Canvas->NativeOnPreviewKeyDown(Canvas->GetCachedGeometry(),Escape);
    TestTrue(TEXT("Escape opens the options menu"),Manager->FindElementByName(TEXT("OptionsPanel_Field"))->bVisible);
    Canvas->NativeOnPreviewKeyDown(Canvas->GetCachedGeometry(),Escape);
    TestFalse(TEXT("A second Escape closes the options menu"),Manager->FindElementByName(TEXT("OptionsPanel_Field"))->bVisible);
    auto NativeClick=[&](const TCHAR* Name)
    {
        const auto Element=Manager->FindElementByName(Name);
        if (!TestTrue(Name,Element.IsValid())) return;
        const FVector2D Local=Canvas->LayoutToViewport(FVector2D(Element->GetScreenOrigin())+FVector2D(Element->Width,Element->Height)*.5);
        const auto& Geometry=Canvas->GetCachedGeometry();
        const FVector2D Absolute=Geometry.LocalToAbsolute(Local);
        const auto Hit=Manager->HitTestCanvas(Element->GetScreenOrigin().X+Element->Width/2,Element->GetScreenOrigin().Y+Element->Height/2);
        AddInfo(FString::Printf(TEXT("Native click %s hit=%s activatable=%d type=%08X"),Name,Hit?*Hit->ElementName:TEXT("none"),Element->bActivatable,Element->Type));
        FPointerEvent Down(0,Absolute,Absolute,TSet<FKey>{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState());
        FPointerEvent Up(0,Absolute,Absolute,TSet<FKey>{},EKeys::LeftMouseButton,0,FModifierKeysState());
        Canvas->NativeOnMouseButtonDown(Geometry,Down);
        Canvas->NativeOnMouseButtonUp(Geometry,Up);
        Gameplay->TickRefresh();
    };
    NativeClick(TEXT("ChatTarget"));
    TestTrue(TEXT("Actual canvas Say press/release opens the chat destinations"),Gameplay->bChatTargetPopupOpen);
    Gameplay->CloseChatTargetPopup();
    {
    TGuardValue<FACEPlayerVitals> SavedScaleVitals(Client->Session->PlayerVitals,Client->Session->PlayerVitals);
    auto& V=Client->Session->PlayerVitals;
    V.bValid=true;V.Health=50;V.MaxHealth=100;V.Stamina=60;V.MaxStamina=100;V.Mana=70;V.MaxMana=100;
    for(float Scale:{1.25f,2.f,3.f})
    {
        const FIntPoint OriginalSize=ScreenSize;ScreenSize=FIntPoint(3840,2160);
        CaptureScreen(FString::Printf(TEXT("DesktopUI_%dpercent"),FMath::RoundToInt(Scale*100)),Scale);
        TestTrue(TEXT("Scaled viewport keeps the entire layout in bounds"),Canvas->GetCachedGeometry().GetLocalSize().Equals(FVector2D(ScreenSize)/Scale,1.f));
        NativeClick(TEXT("ChatTarget"));
        TestTrue(TEXT("Physical mouse coordinates still activate scaled UI controls"),Gameplay->bChatTargetPopupOpen);
        Gameplay->CloseChatTargetPopup();ScreenSize=OriginalSize;
    }
    }
    CaptureScreen(TEXT("DesktopUI_Restored"));
    Gameplay->ShowVendorPanel(9876);
    CaptureScreen(TEXT("GameplayVendorBefore"));
    NativeClick(TEXT("VendorBuyTab"));
    TestEqual(TEXT("Actual canvas vendor tab switches to buy cart"),Gameplay->ActiveVendorPage,1);
    NativeClick(TEXT("VendorItemsTab"));
    NativeClick(TEXT("Menu_Vendor_SelectionWidget"));
    TestTrue(TEXT("Actual canvas vendor filter opens"),Gameplay->bVendorFilterDropdownOpen);
    Gameplay->HideVendorPanel();
    Gameplay->ShowExternalContainer(123);
    Gameplay->TickRefresh();
    const auto Loot=Manager->FindElementByName(TEXT("ExternalContainer"));
    TestEqual(TEXT("Loot content keeps resolved retail width"),Loot->Width,600);
    const auto Area=Manager->FindElementUnder(TEXT("ExternalContainer"),TEXT("ItemsArea"));
    const auto Edge=Manager->FindElementUnder(TEXT("ExternalContainer"),TEXT("RightEdge"));
    TestTrue(TEXT("Loot bevel follows the item area width"),Area && Edge && Edge->X+Edge->Width==Area->Width);
    CaptureScreen(TEXT("GameplayLoot"));
    Gameplay->HideExternalContainer(false);
    Gameplay->LastAppraisal.Name=TEXT("Inspection layout regression");
    Gameplay->LastAppraisal.bSuccess=true;
    Gameplay->LastAppraisal.ObjectGuid=456;
    Gameplay->LastAppraisal.Value=150; Gameplay->LastAppraisal.bHasValue=true;
    Gameplay->LastAppraisal.Burden=200; Gameplay->LastAppraisal.bHasBurden=true;
    Gameplay->LastAppraisal.Summary=TEXT("Spells (1)\n  Spell 1\nDamage 20  Speed 30  Offense 1.00\n");
    Gameplay->LastAppraisal.bHasWeaponProfile=true; Gameplay->LastAppraisal.ItemType=ACEItemType::MeleeWeapon;
    Gameplay->LastAppraisal.Damage=20; Gameplay->LastAppraisal.DamageVariance=.2f;
    Gameplay->LastAppraisal.DamageType=1; Gameplay->LastAppraisal.WeaponTime=30;
    Gameplay->LastAppraisal.SpellIds={1};
    for (int32 I=0; I<40; ++I) Gameplay->LastAppraisal.Summary+=TEXT("Authored bitmap text wraps and scrolls within the body.\n");
    Gameplay->LastAppraisal.Inscription=TEXT("Inscription stays in the paper strip.");
    Gameplay->ShowExamination(true); Gameplay->RefreshExaminationOverlay();
    const auto ItemText=Manager->FindElementUnder(TEXT("ItemExamineUI"),TEXT("ItemDisplayText"));
    TestEqual(TEXT("Inspection body keeps DAT y=0, without guessed icon band"),ItemText->Y,0);
    TestEqual(TEXT("Inspection body keeps DAT x=4"),ItemText->X,4);
    TestFalse(TEXT("Inspection does not graft obsolete item icon"),Manager->FindElementUnder(TEXT("ItemExamineUI"),TEXT("ItemIcon")).IsValid());
    CaptureScreen(TEXT("GameplayInspection"));
    TestTrue(TEXT("Long appraisal is scrollable instead of overflowing"),Gameplay->ExamScroll && Gameplay->ExamScroll->GetScrollOffsetOfEnd()>0.f);
    const auto InscriptionBar=Manager->FindElementUnder(TEXT("ItemExamineUI"),TEXT("ItemInscriptionScrollbar"));
    TestTrue(TEXT("Short inscriptions hide their unused arrow controls"),InscriptionBar && !InscriptionBar->bVisible);
    TestTrue(TEXT("Retail layout without value controls includes value and burden in body"),
        Gameplay->ExamBody->GetText().ToString().StartsWith(TEXT("Value: 150\nBurden: 200\n")));
    TestTrue(TEXT("Resolving spell names preserves formatted weapon info"),
        Gameplay->ExamBody->GetText().ToString().Contains(TEXT("Damage: 16 - 20, Slashing")));
    TestFalse(TEXT("Inspection removes the obsolete diagnostic weapon summary"),Gameplay->ExamBody->GetText().ToString().Contains(TEXT("Offense 1.00")));
    auto* ExistingBodySlot = Gameplay->ExamBody->Slot.Get();
    Gameplay->ExamScroll->SetScrollOffset(80.f);
    Gameplay->RefreshExaminationOverlay();
    TestTrue(TEXT("Refreshing appraisal retains the existing scroll child and slot"), Gameplay->ExamBody->Slot == ExistingBodySlot);
    TestEqual(TEXT("Refreshing same appraisal preserves scroll position"), Gameplay->ExamScroll->GetScrollOffset(), 80.f);
    Gameplay->ExamScroll->SetScrollOffset(Gameplay->ExamScroll->GetScrollOffsetOfEnd());
    const auto ScrolledPixels = CaptureScreen(TEXT("GameplayInspectionScrolled"));
    if (ScrolledPixels.Num() == 1600*900)
    {
        const auto Origin = ItemText->GetScreenOrigin();
        int32 WhitePixels = 0;
        for (int32 Y=Origin.Y+ItemText->Height-100; Y<Origin.Y+ItemText->Height-20; ++Y)
            for (int32 X=Origin.X+8; X<Origin.X+ItemText->Width-24; ++X)
                if (X>=0 && X<1600 && Y>=0 && Y<900)
                {
                    const FColor P=ScrolledPixels[Y*1600+X];
                    WhitePixels += P.R>200 && P.G>200 && P.B>200;
                }
        TestTrue(TEXT("Bottom of a scrolled appraisal still paints visible glyphs"), WhitePixels>100);
    }
    const FString ShortInscription=Gameplay->LastAppraisal.Inscription;
    for(int I=0;I<20;++I)Gameplay->LastAppraisal.Inscription+=TEXT("\nA long inscription remains readable on its paper strip.");
    CaptureScreen(TEXT("GameplayLongInscription"));
    TestTrue(TEXT("Long inscription activates the retail scrollbar"),InscriptionBar->bVisible && Gameplay->ExamInscriptionScroll->GetScrollOffsetOfEnd()>0);
    Gameplay->OnElementActivated(Manager->FindElementUnder(TEXT("ItemInscriptionScrollbar"),TEXT("ScrollBar_Down")));
    TestTrue(TEXT("Inscription down arrow scrolls its own text"),Gameplay->ExamInscriptionScroll->GetScrollOffset()>0);
    Gameplay->LastAppraisal.Inscription=ShortInscription;
    Gameplay->LastAppraisal.ObjectGuid=457; Gameplay->RefreshExaminationOverlay();
    TestEqual(TEXT("New appraisal starts at top as retail is_new requires"), Gameplay->ExamScroll->GetScrollOffset(), 0.f);
    const auto ItemAppraisal = Gameplay->LastAppraisal;
    Gameplay->LastAppraisal = FACEAppraisalInfo();
    Gameplay->LastAppraisal.ObjectGuid=459; Gameplay->LastAppraisal.Name=TEXT("Bandana of Mana Conversion");
    Gameplay->LastAppraisal.bSuccess=true; Gameplay->LastAppraisal.bHasValue=true; Gameplay->LastAppraisal.Value=3880;
    Gameplay->LastAppraisal.bHasBurden=true; Gameplay->LastAppraisal.Burden=23;
    Gameplay->LastAppraisal.IntProperties={{28,120},{105,7},{171,2},{106,100},{107,300},{108,400},{109,85},{158,7},{160,50}};
    Gameplay->LastAppraisal.FloatProperties={{144,.31},{29,1.15}};
    Gameplay->LastAppraisal.ArmorResistances={1.6f,1.2f,1.f,.4f,.8f,.5f,0.f,2.f};
    Gameplay->LastAppraisal.Summary=TEXT("Spells (1)\n  Spell 1\n"); Gameplay->LastAppraisal.SpellIds={1};
    Gameplay->RefreshExaminationOverlay();
    const FString ItemDetails=Gameplay->ExamBody->GetText().ToString();
    TestTrue(TEXT("Item appraisal includes exact armor quality and mana"),ItemDetails.Contains(TEXT("Slashing: Excellent (1.60)")) && ItemDetails.Contains(TEXT("Mana: 300 / 400.")));
    TestTrue(TEXT("Mana conversion is a zero-based bonus; defense is a multiplier"),ItemDetails.Contains(TEXT("Mana Conversion: +31%.")) && ItemDetails.Contains(TEXT("Melee Defense: +15.0%.")));
    TestTrue(TEXT("Item appraisal displays wield requirements and workmanship"),ItemDetails.Contains(TEXT("Wield requires Level 50")) && ItemDetails.Contains(TEXT("Workmanship: Flawless (7)")));
    CaptureScreen(TEXT("GameplayDetailedItemInspection"));
    Gameplay->LastAppraisal=FACEAppraisalInfo();
    auto& Melee=Gameplay->LastAppraisal;
    Melee.ObjectGuid=462; Melee.Name=TEXT("Cleave Sword"); Melee.bSuccess=true; Melee.bHasWeaponProfile=true;
    Melee.ItemType=ACEItemType::MeleeWeapon; Melee.Damage=40; Melee.DamageVariance=.3f; Melee.DamageType=3;
    Melee.WeaponSkill=44; Melee.WeaponTime=30; Melee.WeaponOffense=1.15f;
    Melee.IntProperties={{353,2},{292,3},{158,2},{159,44},{160,350}}; Melee.FloatProperties={{29,1.2}}; Melee.SpellIds={1};
    Gameplay->RefreshExaminationOverlay();
    const FString MeleeText=Gameplay->ExamBody->GetText().ToString();
    TestTrue(TEXT("Melee inspection shows skill, type, range and speed"),MeleeText.Contains(TEXT("Heavy Weapons (Sword)")) && MeleeText.Contains(TEXT("Damage: 28 - 40, Slashing/Piercing")) && MeleeText.Contains(TEXT("Speed: Fast (30)")));
    TestTrue(TEXT("Melee inspection shows attack, defense and exact cleave count"),MeleeText.Contains(TEXT("Attack Skill: +15%")) && MeleeText.Contains(TEXT("Melee Defense: +20.0%")) && MeleeText.Contains(TEXT("Cleave: 3 enemies")));
    TestTrue(TEXT("Damage is visible before long spell descriptions"),MeleeText.Find(TEXT("Damage:")) < MeleeText.Find(TEXT("Spells:")));
    CaptureScreen(TEXT("GameplayMeleeInspection"));
    Melee.bSuccess=false; Melee.bHasWeaponProfile=false; Melee.SpellIds.Reset(); Gameplay->RefreshExaminationOverlay();
    TestTrue(TEXT("Failed weapon identification displays unknown damage"),Gameplay->ExamBody->GetText().ToString().Contains(TEXT("Damage: Unknown")));
    Gameplay->LastAppraisal=FACEAppraisalInfo();
    auto& CasterInfo=Gameplay->LastAppraisal;
    CasterInfo.ObjectGuid=460; CasterInfo.Name=TEXT("Ivory Slashing Baton"); CasterInfo.bSuccess=true;
    CasterInfo.bHasValue=CasterInfo.bHasBurden=true; CasterInfo.Value=20613; CasterInfo.Burden=50;
    CasterInfo.IntProperties={{105,7},{171,10},{106,370},{107,4084},{108,4084},{109,389},{158,2},{159,34},{160,375},{179,8},{45,1}};
    CasterInfo.FloatProperties={{29,1.15},{144,.07},{152,1.25},{5,-1.0/15.0}};
    CasterInfo.StringProperties={{39,TEXT("Tinker Holdem")},{40,TEXT("Tinker Holdem")}};
    CasterInfo.SpellIds={1,2,3,4,5,6}; CasterInfo.BoolProperties.Add(22,true);
    Gameplay->RefreshExaminationOverlay();
    const FString CasterDetails=Gameplay->ExamBody->GetText().ToString();
    TestTrue(TEXT("Caster describes imbue and elemental bonuses"),CasterDetails.Contains(TEXT("Slash Rending")) && CasterDetails.Contains(TEXT("vs. Monsters: +25.0%")) && CasterDetails.Contains(TEXT("vs. Players: +12.5%")));
    TestTrue(TEXT("Caster mana cost comes from regeneration rate"),CasterDetails.Contains(TEXT("Mana Cost: 1 point per 15 seconds.")));
    FString Description; TestTrue(TEXT("Spell DAT supplies description"),Dat->TryGetSpellDescription(1,Description));
    TestTrue(TEXT("Inspect includes the actual DAT spell description"),!Description.IsEmpty() && CasterDetails.Contains(Description));
    CaptureScreen(TEXT("GameplayCasterInspection"));
    TestTrue(TEXT("Real caster details overflow into scrollable content"),Gameplay->ExamScroll->GetScrollOffsetOfEnd()>0.f);

    Gameplay->LastAppraisal = FACEAppraisalInfo();
    Gameplay->LastAppraisal.ObjectGuid=458; Gameplay->LastAppraisal.Name=TEXT("Creature appraisal");
    Gameplay->LastAppraisal.bIsCreature=true; Gameplay->LastAppraisal.bSuccess=true;
    Gameplay->LastAppraisal.CreatureType=38;
    Gameplay->LastAppraisal.Level=25; Gameplay->LastAppraisal.Strength=200;
    Gameplay->LastAppraisal.Coordination=140; Gameplay->LastAppraisal.Quickness=75;
    Gameplay->LastAppraisal.Health=150; Gameplay->LastAppraisal.MaxHealth=200;
    Gameplay->LastAppraisal.AttributeHighlights=1; Gameplay->LastAppraisal.AttributeColors=1;
    Gameplay->RefreshExaminationOverlay();
    TestEqual(TEXT("Creature uses nine authored attribute rows"), Gameplay->ExamAttributeRows.Num(), 9);
    TestEqual(TEXT("Creature type heading uses retail display name"), Gameplay->ExamCreatureHeadings[2]->GetText().ToString(), FString(TEXT("Fire Elemental")));
    TestEqual(TEXT("Level heading uses authored label"), Gameplay->ExamCreatureHeadings[1]->GetText().ToString(), FString(TEXT("Level")));
    TestEqual(TEXT("Creature panel separates coordination and quickness in retail order"), Gameplay->ExamAttributeValues[2]->GetText().ToString(), FString(TEXT("140")));
    TestEqual(TEXT("Creature health includes retail percent format"), Gameplay->ExamAttributeValues[6]->GetText().ToString(), FString(TEXT("150/200 (75 %)")));
    TestEqual(TEXT("Creature row uses the retail glyph font"), Gameplay->ExamAttributeRows[0]->Children[0]->FontId, uint32(0x40000001));
    TestEqual(TEXT("Creature pane suppresses summary text slab"), Gameplay->ExamBody->GetVisibility(), ESlateVisibility::Collapsed);
    CaptureScreen(TEXT("GameplayCreatureInspection"));
    const auto CreatureWindow=Manager->FindElementByName(TEXT("RootGameplay_FloatyExamination_Field"));
    const int32 PreviousCreatureResize=CreatureWindow->UserResizeH;
    CreatureWindow->UserResizeH=400-CreatureWindow->AuthoredHeight;
    Gameplay->LastAppraisal.ObjectGuid=461;
    Gameplay->LastAppraisal.StringProperties = {{5,TEXT("War Mage")},{21,TEXT("Thwargnaught")},{35,TEXT("Patron Name")},{47,TEXT("The Allegiance")},{43,TEXT("29 March 2019")}};
    Gameplay->LastAppraisal.IntProperties = {{113,1},{188,1},{30,6},{35,91},{125,1481800},{43,6},{134,1},{307,9},{262,150},{350,3},{351,7},{281,1},{287,450}};
    Gameplay->LastAppraisal.ArmorLevels = {730,675,692,661,664,674,713,704,706};
    Gameplay->LastAppraisal.DamageRating = 9; Gameplay->LastAppraisal.CritDamageRating = 8;
    Gameplay->RefreshExaminationOverlay();
    auto DetailValue = [&](const TCHAR* Label) -> FString
    {
        for (int32 I=0;I<Gameplay->ExamMiscLabels.Num();++I)
            if (Gameplay->ExamMiscLabels[I]->GetVisibility()!=ESlateVisibility::Collapsed && Gameplay->ExamMiscLabels[I]->GetText().ToString()==Label)
                return Gameplay->ExamMiscValues[I]->GetText().ToString();
        return FString();
    };
    TestEqual(TEXT("Player inspection uses numeric gender and heritage"),Gameplay->ExamCreatureHeadings[3]->GetText().ToString(),FString(TEXT("Male Aluvian")));
    TestEqual(TEXT("Inspected character name includes its allegiance rank"),Gameplay->ExamTitle->GetText().ToString(),FString(TEXT("Ealdor Creature appraisal")));
    Gameplay->LastAppraisal.IntProperties.Add(113,2);Gameplay->LastAppraisal.IntProperties.Add(30,7);Gameplay->RefreshExaminationOverlay();
    TestEqual(TEXT("Inspection rank title respects gender"),Gameplay->ExamTitle->GetText().ToString(),FString(TEXT("Duchess Creature appraisal")));
    Gameplay->LastAppraisal.IntProperties.Add(30,11);Gameplay->RefreshExaminationOverlay();
    TestEqual(TEXT("Unrecognized rank leaves the subject name intact"),Gameplay->ExamTitle->GetText().ToString(),Gameplay->LastAppraisal.Name);
    Gameplay->LastAppraisal.IntProperties.Add(113,1);Gameplay->LastAppraisal.IntProperties.Add(30,6);Gameplay->RefreshExaminationOverlay();
    TestEqual(TEXT("Player profession has its own authored line"),Gameplay->ExamCreatureHeadings[4]->GetText().ToString(),FString(TEXT("War Mage")));
    TestEqual(TEXT("Creature heading is hidden for a character"),Gameplay->ExamCreatureHeadings[2]->GetVisibility(),ESlateVisibility::Collapsed);
    TestEqual(TEXT("Monarch name uses appraisal property 21"),DetailValue(TEXT("Monarch:")),FString(TEXT("Thwargnaught")));
    TestEqual(TEXT("Patron name uses appraisal property 35"),DetailValue(TEXT("Patron:")),FString(TEXT("Patron Name")));
    TestEqual(TEXT("Player examination includes body armor"),DetailValue(TEXT("Head/Chest/Groin")),FString(TEXT("AL: 730/675/692")));
    TestEqual(TEXT("Player examination includes deaths"),DetailValue(TEXT("Deaths:")),FString(TEXT("6")));
    TestEqual(TEXT("Society rating uses retail rank ranges"),DetailValue(TEXT("Society:")),FString(TEXT("Celestial Hand ~ Knight")));
    Gameplay->LastAppraisal.IntProperties.Add(134,ACEPlayerKillerStatus::PK);Gameplay->RefreshExaminationOverlay();
    TestEqual(TEXT("PK status uses the actual PK flag"),Gameplay->ExamCreatureHeadings[5]->GetText().ToString(),FString(TEXT("Player Killer")));
    Gameplay->LastAppraisal.IntProperties.Add(134,ACEPlayerKillerStatus::PKLite);Gameplay->RefreshExaminationOverlay();
    TestEqual(TEXT("PK Lite status uses the actual PK Lite flag"),Gameplay->ExamCreatureHeadings[5]->GetText().ToString(),FString(TEXT("Player Killer Lite")));
    {
        FACEWorldObject Subject;Subject.Guid=461;Subject.bIsPlayer=true;
        Subject.ObjectDescriptionFlags=ACEObjectDescFlag::Attackable;
        Client->Session->WorldObjects.Add(Subject.Guid,Subject);
        Gameplay->LastAppraisal.IntProperties.Add(134,1); // earlier NPK appraisal remains open
        FACEBinaryWriter PKL;PKL.WriteUInt8(1);PKL.WriteUInt32(461);PKL.WriteUInt32(134);PKL.WriteInt32(0x40);
        FACEBinaryReader PKLR(PKL.GetData());Client->Session->HandlePublicUpdatePropertyInt(PKLR);
        Gameplay->RefreshExaminationOverlay();
        TestEqual(TEXT("Open inspection follows PKL broadcast without another Identify"),Gameplay->ExamCreatureHeadings[5]->GetText().ToString(),FString(TEXT("Player Killer Lite")));
        TestEqual(TEXT("PKL selection and radar use retail pink"),Gameplay->ResolveRadarColor(Client->Session->WorldObjects[461]),ACERadarColor::PKLite);
        Client->Session->WorldObjects.Remove(461);
        Gameplay->LastAppraisal.IntProperties.Add(134,ACEPlayerKillerStatus::PKLite);
    }
    CaptureScreen(TEXT("GameplayPlayerInspection"));
    const auto ExtraInfo=Manager->FindElementUnder(TEXT("BasicCreatureExamineUI"),TEXT("BasicCreatureExam_ExtraInfo"));
    const auto ExtraSlot=CastChecked<UCanvasPanelSlot>(Gameplay->ExamCreatureDetailsScroll->Slot);
    TestTrue(TEXT("Character details use the authored list bounds"), ExtraSlot->GetPosition().Equals(FVector2D(ExtraInfo->GetScreenOrigin())*Canvas->GetLastScale2D(),.01));
    TestTrue(TEXT("Complete character information can scroll"),Gameplay->ExamCreatureDetailsScroll->GetScrollOffsetOfEnd()>0.f);
    Gameplay->ExamCreatureDetailsScroll->SetScrollOffset(80.f);Gameplay->RefreshExaminationOverlay();
    TestEqual(TEXT("Repeated appraisal preserves the details scroll position"),Gameplay->ExamCreatureDetailsScroll->GetScrollOffset(),80.f);
    CaptureScreen(TEXT("GameplayPlayerInspectionScrolled"));
    // Exercise an explicit expansion, independent of the user's saved editor layout.
    CreatureWindow->UserResizeH=600-CreatureWindow->AuthoredHeight;Gameplay->RefreshExaminationOverlay();
    CaptureScreen(TEXT("GameplayPlayerInspectionExpanded"));
    TestTrue(TEXT("Extra information expands with the inspection frame"),ExtraInfo->Height>87);
    CreatureWindow->UserResizeH=PreviousCreatureResize;Gameplay->RefreshExaminationOverlay();
    Gameplay->LastAppraisal.IntProperties.Add(261,1);Gameplay->LastAppraisal.StringProperties.Remove(5);
    Gameplay->RefreshExaminationOverlay();
    TestEqual(TEXT("Title-only character selects character examination"),Gameplay->ExamCreatureHeadings[4]->GetText().ToString(),FString(TEXT("Adventurer")));
    Gameplay->LastAppraisal.IntProperties.Add(261,999999);Gameplay->LastAppraisal.StringProperties.Add(5,TEXT("War Mage"));
    Gameplay->RefreshExaminationOverlay();
    TestEqual(TEXT("Unknown display title falls back to the profession"),Gameplay->ExamCreatureHeadings[4]->GetText().ToString(),FString(TEXT("War Mage")));
    Gameplay->LastAppraisal.IntProperties.Add(261,1);Gameplay->LastAppraisal.StringProperties.Remove(5);
    Gameplay->LastAppraisal.IntProperties.Add(43,0);Gameplay->LastAppraisal.ArmorLevels[0]=9999+123;
    Gameplay->RefreshExaminationOverlay();
    TestEqual(TEXT("Unenchantable armor strips the wire sentinel"),DetailValue(TEXT("Head/Chest/Groin")),FString(TEXT("AL: *123/675/692")));
    TestEqual(TEXT("Retail zero-death wording"),DetailValue(TEXT("Deaths:")),FString(TEXT("Has never died")));
    // An NPC's gender/heritage alone do not make it a character appraisal.
    Gameplay->LastAppraisal.ObjectGuid=462;Gameplay->LastAppraisal.IntProperties.Remove(261);
    Gameplay->LastAppraisal.StringProperties.Add(3,TEXT("Male"));Gameplay->LastAppraisal.StringProperties.Add(4,TEXT("Aluvian"));
    Gameplay->LastAppraisal.CreatureType=31;Gameplay->RefreshExaminationOverlay();
    TestFalse(TEXT("NPC switches off the character header"),bool(Manager->FindElementUnder(TEXT("BasicCreatureExamineUI"),TEXT("CharacterExam_Attributes"))->bVisible));
    TestEqual(TEXT("Creature name omits character rank prefixes"),Gameplay->ExamTitle->GetText().ToString(),Gameplay->LastAppraisal.Name);
    TestEqual(TEXT("Character details do not leak into creature examination"),DetailValue(TEXT("Deaths:")),FString());
    TestEqual(TEXT("Switching subjects resets creature details scroll"),Gameplay->ExamCreatureDetailsScroll->GetScrollOffset(),0.f);
    TestEqual(TEXT("Creature resistance details remain available"),DetailValue(TEXT("DoT/Life:")),FString(TEXT("%Resist: 3/7")));
    CaptureScreen(TEXT("GameplayNPCInspection"));
    Gameplay->LastAppraisal.bSuccess=false; Gameplay->LastAppraisal.Strength=0;
    Gameplay->RefreshExaminationOverlay();
    TestEqual(TEXT("Failed appraisal retains only health percent"), Gameplay->ExamAttributeValues[6]->GetText().ToString(), FString(TEXT("75 %")));
    TestEqual(TEXT("Unknown attribute is not displayed as zero"), Gameplay->ExamAttributeValues[0]->GetText().ToString(), FString(TEXT("???")));
    Gameplay->LastAppraisal = ItemAppraisal;
    Gameplay->ShowExamination(false); Gameplay->RefreshExaminationOverlay();
    Gameplay->ShowPanelPage(TEXT("InventoryPanel_Field")); Gameplay->RefreshInventoryOverlays();
    CaptureScreen(TEXT("GameplayInventory"));
    TestTrue(TEXT("Inventory draws bound shortcut number overlay"),Gameplay->InventorySlotOverlays.Num()>0
        && Gameplay->InventorySlotOverlays[0]->GetVisibility()!=ESlateVisibility::Collapsed);
    auto* ShortcutArt=Cast<UTexture2D>(Gameplay->InventorySlotOverlays[0]->Background.GetResourceObject());
    TestTrue(TEXT("Inventory shortcut uses third DAT numeral"),ShortcutArt==Resources->ResolveIconTexture(0x060010A0));
    // A raised retail floaty moves both its DAT chrome and cached generated contents.
    // An opaque front window must also prevent dragging an inventory slot through it.
    const auto InventoryWindow=Manager->FindElementByName(TEXT("RootGameplay_FloatyPanel_Field"));
    const auto ExaminationWindow=Manager->FindElementByName(TEXT("RootGameplay_FloatyExamination_Field"));
    auto* ItemBorder=Gameplay->InventorySlots[0].Get();
    const auto ItemGeometry=ItemBorder->GetCachedGeometry();
    const FVector2D ItemAbsolute=ItemGeometry.LocalToAbsolute(ItemGeometry.GetLocalSize()*.5);
    const FVector2D ItemLocal=Canvas->GetCachedGeometry().AbsoluteToLocal(ItemAbsolute);
    const FIntPoint PreviousExaminationDrag(ExaminationWindow->UserDragX,ExaminationWindow->UserDragY);
    const auto ExaminationOrigin=ExaminationWindow->GetScreenOrigin();
    ExaminationWindow->UserDragX+=FMath::RoundToInt(ItemLocal.X)-ExaminationOrigin.X-40;
    ExaminationWindow->UserDragY+=FMath::RoundToInt(ItemLocal.Y)-ExaminationOrigin.Y-80;
    Gameplay->ShowExamination(true); Manager->BringFloatyToFront(ExaminationWindow);
    CaptureScreen(TEXT("GameplayInspectionOverInventory"));
    TestFalse(TEXT("Inventory slot behind appraisal is not exposed"),Canvas->IsWidgetExposedAt(ItemBorder,ItemAbsolute));
    TestFalse(TEXT("Covered inventory grid cannot start a drag through its coordinate fallback"),Gameplay->TryBeginInventoryDrag(ItemLocal));
    TestTrue(TEXT("Front examination owns the covered point"),Manager->FindWindowAtCanvas(ItemLocal.X,ItemLocal.Y)==ExaminationWindow);
    Manager->BringFloatyToFront(InventoryWindow);
    CaptureScreen(TEXT("GameplayInventoryOverInspection"));
    TestTrue(TEXT("Raising inventory restores item hit testing without changing contents"),Canvas->IsWidgetExposedAt(ItemBorder,ItemAbsolute));
    TestTrue(TEXT("Raising inventory also raises cached item art over the appraisal body"),
        Cast<UCanvasPanelSlot>(ItemBorder->Slot)->GetZOrder()>Cast<UCanvasPanelSlot>(Gameplay->ExamScroll->Slot)->GetZOrder());
    TestTrue(TEXT("Visible inventory item starts a drag"),Gameplay->TryBeginInventoryDrag(ItemLocal));
    Gameplay->UpdateInventoryDrag(ItemLocal+FVector2D(24,0));
    Gameplay->TryFinishInventoryDrag(ItemLocal);
    TestEqual(TEXT("Drag back to its source slot cannot arm double-click use"),Gameplay->LastInvClickGuid,0);
    ExaminationWindow->UserDragX=PreviousExaminationDrag.X; ExaminationWindow->UserDragY=PreviousExaminationDrag.Y;
    Gameplay->ShowExamination(false);
    Gameplay->bInvDragPending=true; Gameplay->bInvDragActive=true; Gameplay->InvDragGuid=Item.Guid;
    Gameplay->InvDragSourcePack=Player.Guid; Gameplay->LastInvClickGuid=Item.Guid;
    const auto Title=Manager->FindElementByName(TEXT("InvTitleText"));
    if (Title) Gameplay->TryFinishInventoryDrag(FVector2D(Title->GetScreenOrigin())+FVector2D(8,8));
    TestEqual(TEXT("Cancelled item drag cannot arm double-click use"),Gameplay->LastInvClickGuid,0);
    if (FApp::CanEverRender())
    {
        // Exercise Slate's actual input path, not only our model hit-test helpers.
        // Visible native chat text used to win through HitTestInvisible DAT art.
        const auto ChatWindow=Manager->FindElementByName(TEXT("RootGameplay_FloatyMainChat_Field"));
        const auto CombatWindow=Manager->FindElementByName(TEXT("RootGameplay_FloatyCombatPanel_Field"));
        const auto VendorWindow=Manager->FindElementByName(TEXT("RootGameplay_FloatyEnvPanel_Field"));
        const auto PreviousMode=Gameplay->CombatMode;
        Gameplay->ApplyCombatMode(static_cast<int32>(ACECombatMode::Magic));
        Gameplay->ShowVendorPanel(9876);
        FWidgetRenderer Renderer(true,true); FHittestGrid Grid;
        const auto Window=SNew(SVirtualWindow).Size(FVector2D(ScreenSize)); Window->SetContent(Slate);
        auto* Target=FWidgetRenderer::CreateTargetFor(FVector2D(ScreenSize),TF_Bilinear,true);
        auto Draw=[&]()
        {
            for(int32 Pass=0;Pass<3;++Pass)
            {
                Canvas->NativeTick(Canvas->GetCachedGeometry(),0.f);
                Renderer.DrawWindow(Target,Grid,Window,1.f,FVector2D(ScreenSize),0.f);
                FlushRenderingCommands();
            }
        };
        Manager->BringFloatyToFront(ChatWindow); Draw();
        for (UWidget* Chat : {static_cast<UWidget*>(Gameplay->ChatEntry.Get()),static_cast<UWidget*>(Gameplay->ChatLog.Get())})
        {
            const auto Geometry=Chat->GetCachedGeometry();
            FVector2D Point=Geometry.LocalToAbsolute(Chat==Gameplay->ChatEntry
                ? Geometry.GetLocalSize()*.5 : FVector2D(40,Geometry.GetLocalSize().Y-8));
            auto HitsChat=[&]()
            {
                for(const auto& Hit:Grid.GetBubblePath(Point,0,false))
                    if(Hit.Widget==Chat->TakeWidget()) return true;
                return false;
            };
            // Empty space in the log intentionally bubbles to DAT chrome. Find
            // an actual visible text row rather than assuming the last line fills it.
            if(Chat==Gameplay->ChatLog && !HitsChat())
                for(float Y=2;Y<Geometry.GetLocalSize().Y;++Y)
                {
                    Point=Geometry.LocalToAbsolute(FVector2D(40,Y));
                    if(HitsChat())break;
                }
            TestTrue(TEXT("Uncovered chat retains native editing/selection hit path"),HitsChat());
            for(const auto& Front:{CombatWindow,VendorWindow})
            {
                const FIntPoint Drag(Front->UserDragX,Front->UserDragY);
                const FIntPoint Origin=Front->GetScreenOrigin();
                const FVector2D Local=Canvas->ViewportToLayout(Canvas->GetCachedGeometry().AbsoluteToLocal(Point));
                Front->UserDragX+=FMath::RoundToInt(Local.X)-Origin.X-Front->Width/2;
                Front->UserDragY+=FMath::RoundToInt(Local.Y)-Origin.Y-Front->Height/2;
                Manager->BringFloatyToFront(Front); Draw();
                TestFalse(TEXT("Front spell/vendor window blocks native chat focus through its art"),HitsChat());
                TestTrue(TEXT("Front window owns the overlapping mouse point"),Manager->FindWindowAtCanvas(Local.X,Local.Y)==Front);
                Manager->BringFloatyToFront(ChatWindow); Draw();
                TestTrue(TEXT("Raising chat restores native input without rebuilding its text"),HitsChat());
                Front->UserDragX=Drag.X; Front->UserDragY=Drag.Y; Draw();
            }
        }
        Gameplay->HideVendorPanel(); Gameplay->ApplyCombatMode(PreviousMode);
    }
    Gameplay->bChatTargetPopupOpen=true; Gameplay->RefreshChatTargetPopup();
    CaptureScreen(TEXT("GameplayChatMenu"));
    TestTrue(TEXT("Say menu background is below its channel options"),
        Cast<UCanvasPanelSlot>(Gameplay->ChatTargetPopupBg->Slot)->GetZOrder()
        <Cast<UCanvasPanelSlot>(Gameplay->ChatTargetPopupRows[0]->Slot)->GetZOrder());
    TestEqual(TEXT("Chat menu uses all fourteen retail entries"),Gameplay->ChatTargetPopupRows.Num(),14);
    TestTrue(TEXT("Chat menu text uses the authored DAT font"),CastChecked<UACERetailTextBlock>(Gameplay->ChatTargetPopupRows[0])->GetBitmapFont()!=nullptr);
    {
        const auto Saved=Client->Session->SelectedObject;
        FACEWorldObject NPC;NPC.Guid=0x123455;NPC.Name=TEXT("Eiichi");NPC.ItemType=ACEItemType::Creature;
        Client->Session->WorldObjects.Add(NPC.Guid,NPC);
        FACESelectedObject Selected;Selected.Guid=NPC.Guid;Selected.bValid=true;
        Client->Session->SelectedObject=Selected;Gameplay->RefreshChatTargetPopup();
        TestEqual(TEXT("Retail Tell selection includes NPC names"),Gameplay->ChatTargetPopupRows[2]->GetText().ToString(),FString(TEXT("Tell to Eiichi")));
        TestTrue(TEXT("NPC Tell option is enabled"),Gameplay->ChatTargetPopupElements[2]->PaintState!=13);
        NPC.ItemType=ACEItemType::Container;Client->Session->WorldObjects[NPC.Guid]=NPC;Gameplay->RefreshChatTargetPopup();
        TestEqual(TEXT("Objects cannot receive selected tells"),Gameplay->ChatTargetPopupElements[2]->PaintState,13u);
        Client->Session->WorldObjects.Remove(NPC.Guid);Client->Session->SelectedObject=Saved;Gameplay->RefreshChatTargetPopup();
    }
    const auto Row=Gameplay->ChatTargetPopupRows[8];
    const FVector2D RowCenter=Row->GetCachedGeometry().LocalToAbsolute(Row->GetCachedGeometry().GetLocalSize()*.5);
    TestTrue(TEXT("Say menu accepts channel click"),Gameplay->TryHandleModalPopupClick(RowCenter));
    TestEqual(TEXT("Say menu selects General"),Gameplay->ChatSendChannel,7);
    for (int32 I=1; I<=4; ++I)
    {
        const FString Name=FString::Printf(TEXT("RootGameplay_FloatyChat%d_Field"),I);
        Gameplay->SetFloatyVisible(Name,true);
        if (const auto Root=Manager->FindElementByName(Name)) { Root->UserDragX=I*260; Root->UserDragY=100; }
    }
    Gameplay->PlaceFloatyChatOverlays(); CaptureScreen(TEXT("GameplayFloatyChats"));
    {
        TGuardValue<FString> SettingsPath(GGameUserSettingsIni,FPaths::ProjectSavedDir()/TEXT("Automation/ChatRoutingFixture.ini"));
        FConfigFile FixtureConfig; FixtureConfig.NoSave=false; FixtureConfig.bCanSaveAllSections=true;
        GConfig->SetFile(GGameUserSettingsIni,&FixtureConfig);
        Gameplay->LoadFloatyChatSettings();
        TestEqual(TEXT("First auxiliary window defaults to speech and tells"),Gameplay->FloatyChatFilters[0],4124ull);
        TestEqual(TEXT("Fourth auxiliary window defaults to public channels"),Gameplay->FloatyChatFilters[3],2013265920ull);
        Gameplay->ShowPanelPage(TEXT("OptionsPanel_Field"));
        Gameplay->SyncOptionsPanelTab(TEXT("ChatPage"));
        TestTrue(TEXT("Chat options include all five routing sections"),Gameplay->GetOptionsRowCount()>70);
        TestEqual(TEXT("Both chat opacity sliders are present"),Gameplay->ChatOpacitySliders.Num(),2);
        for (USlider* OpacityControl : Gameplay->ChatOpacitySliders)
            TestTrue(TEXT("Chat opacity is visible at the top of its page"),OpacityControl && OpacityControl->GetVisibility()!=ESlateVisibility::Collapsed);

        TestEqual(TEXT("Both retail opacity controls are present"),Gameplay->ChatOpacitySliders.Num(),2);
        Gameplay->ChangeInactiveChatOpacity(.7f);
        Gameplay->ChangeActiveChatOpacity(.3f);
        Gameplay->LoadFloatyChatSettings();
        TestEqual(TEXT("Active opacity survives reload"),Gameplay->ChatActiveOpacity,.3f);
        TestEqual(TEXT("Retail opacity link keeps inactive below active"),Gameplay->ChatInactiveOpacity,.3f);
        Gameplay->RefreshChatOpacity();
        for (int32 W=1;W<=4;++W)
            TestEqual(TEXT("Auxiliary window background applies saved opacity"),Manager->FindElementByName(
                FString::Printf(TEXT("RootGameplay_FloatyChat%d_Field"),W))->ImageOpacity,.3f);
        constexpr int32 MainGeneral=1000+7, WindowTwoGeneral=1000+2*13+7;
        Gameplay->ToggleCharacterOption(MainGeneral);
        Gameplay->ToggleCharacterOption(WindowTwoGeneral);
        Gameplay->LoadFloatyChatSettings();
        TestTrue(TEXT("Main window routing survives reload"),(Gameplay->MainChatTypeFilter&(1ull<<27))==0);
        TestTrue(TEXT("Auxiliary routing survives reload"),(Gameplay->FloatyChatFilters[1]&(1ull<<27))!=0);
        Gameplay->OptionsScrollOffset=1000; Gameplay->RefreshOptionsOverlays();
        CaptureScreen(TEXT("GameplayChatWindowFourOptions"));
        TestTrue(TEXT("Routing list can reach the fourth auxiliary window"),Gameplay->OptionRowOptions.Contains(1000+4*13+7));
        Gameplay->HandleOptionsNamedClick(TEXT("ResetButton"));
        TestEqual(TEXT("Reset restores inactive opacity"),Gameplay->ChatInactiveOpacity,.5f);
        TestEqual(TEXT("Reset restores active opacity"),Gameplay->ChatActiveOpacity,1.f);
        TestTrue(TEXT("Reset restores the main routing snapshot"),(Gameplay->MainChatTypeFilter&(1ull<<27))!=0);
        TestTrue(TEXT("Reset restores the auxiliary routing snapshot"),(Gameplay->FloatyChatFilters[1]&(1ull<<27))==0);
    }
    FSlateApplication::Get().ClearKeyboardFocus();
    const FKeyEvent ChatEnterKey(EKeys::Enter,FModifierKeysState(),0,false,0,0);
    TestTrue(TEXT("Enter reaches chat before a focused option button can consume it"),
        Canvas->NativeOnPreviewKeyDown(Canvas->GetCachedGeometry(),ChatEnterKey).IsEventHandled());
    const bool MouseBefore=Client->IsCharacterOptionSet(0x31);
    Gameplay->ShowPanelPage(TEXT("OptionsPanel_Field"));
    Gameplay->HandleOptionsNamedClick(TEXT("GameplayOptions_MouseTurning_Button"));
    TestTrue(TEXT("Mouse turning option updates live client preference"),Client->IsCharacterOptionSet(0x31)!=MouseBefore);
    TestTrue(TEXT("Opening options highlights its toolbar icon"),Manager->FindElementByName(TEXT("PanelButton_OptionsButton"))->bHighlighted);
    Gameplay->SyncOptionsPanelTab(TEXT("CharacterSettingsPage"));
    Gameplay->RefreshOptionsOverlays();
    CaptureScreen(TEXT("GameplayCharacterOptions"));
    TestTrue(TEXT("Character settings page has live option rows"),
        Gameplay->OptionRowOptions.ContainsByPredicate([](int32 Option){return Option!=INDEX_NONE;}));
    {
        TGuardValue<FString> SettingsPath(GGameUserSettingsIni,FPaths::ProjectSavedDir()/TEXT("Automation/CameraUIFixture.ini"));
        FConfigFile FixtureConfig; FixtureConfig.NoSave=false; FixtureConfig.bCanSaveAllSections=true;
        GConfig->SetFile(GGameUserSettingsIni,&FixtureConfig);
        Gameplay->SyncOptionsPanelTab(TEXT("ConfigPage")); Gameplay->RefreshOptionsOverlays();
        CaptureScreen(TEXT("GameplayVideoOptions"));
        auto* Video=Cast<UACEVideoSettingsWidget>(Gameplay->VideoSettings);
        if (TestNotNull(TEXT("Config contains engine and camera controls"),Video))
        {
            int32 Collapses=0;
            const auto VisibilityHandle=Video->OnNativeVisibilityChanged.AddLambda([&](ESlateVisibility V)
            { if (V==ESlateVisibility::Collapsed || V==ESlateVisibility::Hidden) ++Collapses; });
            for(int32 Refresh=0;Refresh<5;++Refresh) Gameplay->RefreshOptionsOverlays();
            Video->OnNativeVisibilityChanged.Remove(VisibilityHandle);
            TestEqual(TEXT("Config refresh never hides the active dropdown owner"),Collapses,0);
            TestTrue(TEXT("Full configuration extends past the viewport"),Video->GetScrollEnd()>100.f);
            Gameplay->ScrollOptionsList(-3.f);
            TestTrue(TEXT("The shared config scrollbar scrolls actual controls"),Video->GetScrollOffset()>0.f);
            CaptureScreen(TEXT("GameplayVideoOptionsScrolled"));
            Video->SetScrollOffset(0.f);

            auto* Speed=Cast<USlider>(Video->WidgetTree->FindWidget(TEXT("MouseTurnSpeed")));
            auto* InvertX=Cast<UCheckBox>(Video->WidgetTree->FindWidget(TEXT("InvertMouseX")));
            auto* InvertY=Cast<UCheckBox>(Video->WidgetTree->FindWidget(TEXT("InvertMouseY")));
            auto* UIScale=Cast<UComboBoxString>(Video->WidgetTree->FindWidget(TEXT("DesktopUIScale")));
            if (TestNotNull(TEXT("Horizontal mouse inversion control"),InvertX)
                && TestNotNull(TEXT("Vertical mouse inversion control"),InvertY)
                && TestNotNull(TEXT("Desktop UI scale control"),UIScale))
            {
                TestEqual(TEXT("UI scale offers nine clear quarter steps"),UIScale->GetOptionCount(),9);
                InvertX->SetIsChecked(true);InvertY->SetIsChecked(false);UIScale->SetSelectedIndex(4);
                Video->ApplyInterfaceOptions();
                TestTrue(TEXT("Apply saves independent mouse axes"),ACECameraSettings::GetInvertMouseX() && !ACECameraSettings::GetInvertMouseY());
                FConfigFile Saved;Saved.Read(GGameUserSettingsIni);float SavedScale=0;
                Saved.GetFloat(TEXT("ACE.Presentation"),TEXT("DesktopUIScale"),SavedScale);
                TestEqual(TEXT("UI scale persists to disk"),SavedScale,2.f);
                TestEqual(TEXT("4K supports crisp double-sized UI"),ACERuntimeOptions::DesktopUIScale(FIntPoint(3840,2160),false),2.f);
                TestEqual(TEXT("1080p scale is limited to keep the UI accessible"),ACERuntimeOptions::DesktopUIScale(FIntPoint(1920,1080),false),1.75f);
                TestEqual(TEXT("Small windows keep the native layout accessible"),ACERuntimeOptions::DesktopUIScale(FIntPoint(800,600),false),1.f);
                TestEqual(TEXT("VR surfaces retain their own scale"),ACERuntimeOptions::DesktopUIScale(FIntPoint(3840,2160),true),1.f);
                {
                    TGuardValue<float> RestoreScale(GetMutableDefault<UUserInterfaceSettings>()->ApplicationScale,1.f);
                    ACERuntimeOptions::ApplyDesktopUIScale(FIntPoint(3840,2160),false);
                    TestEqual(TEXT("The actual viewport DPI doubles all child widgets"),GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(FIntPoint(3840,2160)),2.f);
                    ACERuntimeOptions::ApplyDesktopUIScale(FIntPoint(3840,2160),true);
                    TestEqual(TEXT("Entering VR removes desktop DPI scaling"),GetDefault<UUserInterfaceSettings>()->ApplicationScale,1.f);
                }
                auto* Reopened=NewObject<UACEVideoSettingsWidget>();Reopened->Initialize();auto ReopenedSlate=Reopened->TakeWidget();
                auto* ReloadedScale=Cast<UComboBoxString>(Reopened->WidgetTree->FindWidget(TEXT("DesktopUIScale")));
                TestTrue(TEXT("Reopening configuration preserves scale"),ReloadedScale && ReloadedScale->GetSelectedOption()==TEXT("200%"));
                // A player-sized settings window can be taller than the logical
                // viewport after DPI changes, despite the 800x600 global limit.
                const auto Floaty=Manager->FindElementByName(TEXT("RootGameplay_FloatyPanel_Field"));
                const auto Apply=Manager->FindElementUnder(TEXT("ConfigPage"),TEXT("ApplyButton"));
                if(TestTrue(TEXT("Settings frame and Apply button exist"),Floaty && Apply))
                {
                    const FIntPoint OriginalScreen=ScreenSize;
                    const int32 OriginalHeight=Floaty->GetLayoutHeight();
                    const int32 OriginalDragX=Floaty->UserDragX,OriginalDragY=Floaty->UserDragY;
                    // Viewport clamping also moves the other visible windows.
                    // Restore those positions before subsequent shortcut tests;
                    // otherwise the resized inventory obscures the shortcut bar.
                    TArray<TPair<TSharedPtr<FACEUIElement>,FIntPoint>> WindowPositions;
                    if(const auto Root=Floaty->Parent.Pin())for(const auto& Window:Root->Children)
                        if(Window)WindowPositions.Emplace(Window,FIntPoint(Window->UserDragX,Window->UserDragY));
                    // Isolate the interface setting from ApplyVideo's OS display
                    // changes; retain the actual canvas hit testing and activation.
                    Manager->OnElementActivated.Remove(Gameplay->ActivatedHandle);
                    UACEUIElementManager::ApplyFloatyResizeLayout(Floaty);
                    for(const FIntPoint Size:{FIntPoint(1920,1080),FIntPoint(2560,1440),FIntPoint(3840,2160),FIntPoint(800,600)})
                    {
                        ScreenSize=Size;
                        CaptureScreen(FString::Printf(TEXT("OptionsWindowResize_%d"),Size.X));
                        // The existing resize handle permits 320 extra pixels.
                        Floaty->UserResizeH=320;
                        UACEUIElementManager::ApplyFloatyResizeLayout(Floaty);
                        CaptureScreen(FString::Printf(TEXT("OptionsBeforeScale_%d"),Size.X));
                        ACERuntimeOptions::Set(TEXT("DesktopUIScale"),3.f);
                        const float Scale=ACERuntimeOptions::DesktopUIScale(Size,false);
                        CaptureScreen(FString::Printf(TEXT("OptionsAfterScale_%d"),Size.X),Scale);
                        const FVector2D LogicalSize=FVector2D(Size)/Scale;
                        const FIntPoint Origin=Apply->GetScreenOrigin();
                        AddInfo(FString::Printf(TEXT("Scaled options %dx%d at %.2f: frame=%s/%d Apply=%s/%d"),
                            Size.X,Size.Y,Scale,*Floaty->GetScreenOrigin().ToString(),Floaty->Height,*Origin.ToString(),Apply->Height));
                        TestTrue(TEXT("Scaled settings frame fits the visible viewport"),
                            Floaty->GetScreenOrigin().Y>=0 && Floaty->GetScreenOrigin().Y+Floaty->Height<=LogicalSize.Y);
                        TestTrue(TEXT("Apply remains entirely onscreen after enlarging the interface"),
                            Origin.X>=0 && Origin.Y>=0 && Origin.X+Apply->Width<=LogicalSize.X && Origin.Y+Apply->Height<=LogicalSize.Y);
                        const FVector2D ScaleOrigin=Canvas->GetCachedGeometry().AbsoluteToLocal(UIScale->GetCachedGeometry().GetAbsolutePosition());
                        const FVector2D ScaleEnd=Canvas->GetCachedGeometry().AbsoluteToLocal(
                            UIScale->GetCachedGeometry().LocalToAbsolute(UIScale->GetCachedGeometry().GetLocalSize()));
                        TestTrue(TEXT("Scale selector remains visible for reducing the UI size"),
                            ScaleOrigin.X>=0 && ScaleOrigin.Y>=0 && ScaleEnd.X<=LogicalSize.X && ScaleEnd.Y<=LogicalSize.Y);
                        const FVector2D Center(Origin.X+Apply->Width/2,Origin.Y+Apply->Height/2);
                        TestTrue(TEXT("Apply remains the pointer hit target after scaling"),Manager->HitTestCanvas(Center.X,Center.Y)==Apply);
                        UIScale->SetSelectedIndex(0);
                        bool Clicked=false;
                        const auto ClickHandle=Manager->OnElementActivated.AddLambda([&](TSharedPtr<FACEUIElement> E){Clicked|=E==Apply;});
                        const FGeometry& Geometry=Canvas->GetCachedGeometry();
                        const FVector2D Absolute=Geometry.LocalToAbsolute(Center);
                        const FPointerEvent Down(0,Absolute,Absolute,TSet<FKey>{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0.f,FModifierKeysState());
                        const FPointerEvent Up(0,Absolute,Absolute,TSet<FKey>{},EKeys::LeftMouseButton,0.f,FModifierKeysState());
                        // Exercise the scaled canvas input route without applying
                        // OS resolution changes in this automated render fixture.
                        const auto Handler=Manager->OnElementActivated.AddLambda([&](TSharedPtr<FACEUIElement> E)
                        { if(E==Apply)Video->ApplyInterfaceOptions(); });
                        Canvas->NativeOnMouseButtonDown(Geometry,Down);
                        Canvas->NativeOnMouseButtonUp(Geometry,Up);
                        Manager->OnElementActivated.Remove(Handler);
                        Manager->OnElementActivated.Remove(ClickHandle);
                        TestTrue(TEXT("Physical click on scaled Apply reaches the control"),Clicked);
                        TestEqual(TEXT("Player can return to 100% using the visible Apply button"),ACERuntimeOptions::Get(TEXT("DesktopUIScale")),1.f);
                    }
                    ScreenSize=OriginalScreen;
                    // Refresh cached Slate geometry before restoring sizes. A
                    // tick with the old 800x600 geometry would clamp them again.
                    CaptureScreen(TEXT("OptionsViewportRestored"));
                    Floaty->UserResizeH=OriginalHeight-Floaty->AuthoredHeight;
                    Floaty->UserDragX=OriginalDragX;Floaty->UserDragY=OriginalDragY;
                    Floaty->RecomputeLayoutOffset();UACEUIElementManager::ApplyFloatyResizeLayout(Floaty);
                    for(const auto& SavedPosition:WindowPositions) {
                        SavedPosition.Key->UserDragX=SavedPosition.Value.X;
                        SavedPosition.Key->UserDragY=SavedPosition.Value.Y;
                        SavedPosition.Key->RecomputeLayoutOffset();
                    }
                    CaptureScreen(TEXT("OptionsScaleRestored"));
                    Gameplay->ActivatedHandle=Manager->OnElementActivated.AddUObject(Gameplay,&UACEUIGameplayBinder::OnElementActivated);
                }
                Video->DefaultsVideo();Video->ApplyInterfaceOptions();
                TestFalse(TEXT("Defaults restore normal mouse X"),ACECameraSettings::GetInvertMouseX());
                TestFalse(TEXT("Defaults restore normal mouse Y"),ACECameraSettings::GetInvertMouseY());
                TestEqual(TEXT("Defaults restore native UI scale"),ACERuntimeOptions::Get(TEXT("DesktopUIScale")),1.f);
                auto* FPS=Cast<UCheckBox>(Video->WidgetTree->FindWidget(TEXT("ShowFrameRate")));
                if(TestNotNull(TEXT("FPS overlay is available in configuration"),FPS))
                {
                    FPS->SetIsChecked(true);Video->ApplyInterfaceOptions();
                    TestEqual(TEXT("FPS toggle saves"),ACERuntimeOptions::Get(TEXT("ShowFrameRate")),1.f);
                    auto* Counter=NewObject<UACEFrameRateWidget>();Counter->Initialize();auto CounterSlate=Counter->TakeWidget();
                    Counter->Sample(0.);for(int32 I=1;I<=45;++I)Counter->Sample(I/90.);
                    TestEqual(TEXT("FPS measures actual frames over elapsed time"),Counter->GetCounterText(),FString(TEXT("90 FPS  |  11.1 ms")));
                    Counter->Sample(1.5);
                    TestEqual(TEXT("A stall is included rather than using clamped simulation delta"),Counter->GetCounterText(),FString(TEXT("1 FPS  |  1000.0 ms")));
                    FPS->SetIsChecked(false);Video->ApplyInterfaceOptions();
                }
            }
            if (TestNotNull(TEXT("Config exposes mouse turn speed"),Speed))
            {
                TestEqual(TEXT("Config displays default speed"),Speed->GetValue(),1.f);
                Speed->SetValue(2.f); Speed->OnValueChanged.Broadcast(2.f);
                TestEqual(TEXT("Selecting a speed applies immediately"),ACECameraSettings::GetMouseTurnSpeed(),2.f);
                FConfigFile Saved; Saved.Read(GGameUserSettingsIni);
                GConfig->SetFile(GGameUserSettingsIni,&Saved);
                auto* Reopened=NewObject<UACEVideoSettingsWidget>(); Reopened->Initialize();
                auto ReopenedSlate=Reopened->TakeWidget();
                auto* ReloadedSpeed=Cast<USlider>(Reopened->WidgetTree->FindWidget(TEXT("MouseTurnSpeed")));
                TestTrue(TEXT("Recreated Config UI reads the persisted speed"),ReloadedSpeed && ReloadedSpeed->GetValue()==2.f);
                TestTrue(TEXT("Slider uses the retail thumb texture"),Speed->GetWidgetStyle().NormalThumbImage.GetResourceObject()==Resources->ResolveTexture(0x06001286));
                Gameplay->HandleOptionsNamedClick(TEXT("DefaultButton"));
                TestEqual(TEXT("Default speed restores the saved preference"),ACECameraSettings::GetMouseTurnSpeed(),1.f);
                TestEqual(TEXT("Default speed updates the visible selector"),Speed->GetValue(),1.f);
            }
        }
    }
    // Footer captions are flattened overlays, so exercise returning to the first
    // page after every list page, as well as a repeated frame refresh.
    for (const TCHAR* PageName : {TEXT("CharacterSettingsPage"),TEXT("GameplayOptionsPage"),
        TEXT("ChatPage"),TEXT("GameplayOptionsPage"),TEXT("ConfigPage"),TEXT("GameplayOptionsPage")})
    {
        Gameplay->SyncOptionsPanelTab(PageName);
        Gameplay->RefreshOptionsOverlays();Gameplay->RefreshOptionsOverlays();
        const bool GameplayPage=FString(PageName)==TEXT("GameplayOptionsPage");
        int32 VisibleLabels=0;
        for (int32 Index=0;Index<Gameplay->OptionsButtonLabels.Num();++Index)
        {
            auto* Label=Cast<UACERetailTextBlock>(Gameplay->OptionsButtonLabels[Index]);
            if (!Label) continue;
            const bool ShouldShow=GameplayPage?Index<7:Index>=7;
            TestEqual(TEXT("Options captions belong only to the active tab"),Label->GetVisibility()!=ESlateVisibility::Collapsed,ShouldShow);
            if (!ShouldShow) continue;
            ++VisibleLabels;
            const auto Button=Label->GetRetailElement();
            const auto* Slot=Cast<UCanvasPanelSlot>(Label->Slot);
            if (TestTrue(TEXT("Options caption is attached to its authored button"),Button && Slot))
            {
                const FVector2D Scale=Canvas->GetLastScale2D();
                TestTrue(TEXT("Caption retains the full button rectangle for retail centering"),
                    Slot->GetPosition().Equals(FVector2D(Button->GetScreenOrigin())*Scale,.01)
                    && Slot->GetSize().Equals(FVector2D(Button->Width,Button->Height)*Scale,.01));
                TestTrue(TEXT("Caption uses the authored font with enough room for all glyphs"),
                    Label->GetBitmapFont() && Label->GetBitmapFont()->MaxCharHeight*Scale.Y<=Slot->GetSize().Y);

            }
        }
        TestEqual(TEXT("Only the active page's button captions are visible"),VisibleLabels,GameplayPage?7:3);
        if (GameplayPage) CaptureScreen(TEXT("GameplayOptionsReturn"));
        else if (FString(PageName)==TEXT("ChatPage")) CaptureScreen(TEXT("GameplayChatOptions"));
    }
    {
        // Isolate persistence from the player's real key mappings.
        TGuardValue<FString> SettingsPath(GGameUserSettingsIni,FPaths::ProjectSavedDir()/TEXT("Automation/InputFixture.ini"));
        FConfigFile FixtureConfig; FixtureConfig.NoSave=false; FixtureConfig.bCanSaveAllSections=true;
        GConfig->SetFile(GGameUserSettingsIni,&FixtureConfig);
        ACEInputBindings::Reload(); ACEInputBindings::BeginEdit(); ACEInputBindings::Defaults(); ACEInputBindings::Commit();
        Gameplay->ToggleKeyboardMappingUI(); Gameplay->RefreshKeyboardOverlays();
        CaptureScreen(TEXT("GameplayKeyboardMovement"));
        Gameplay->HandleKeyboardNamedClick(TEXT("KeyboardLoadKeymapButton"));
        TestTrue(TEXT("Retail keymap importer opens inside the VR-compatible panel"),Gameplay->bKeymapImportOpen && Gameplay->KeymapImportPath);
        CaptureScreen(TEXT("GameplayKeyboardImport"));
        {
            const auto Frame=Manager->FindElementByName(TEXT("KeyboardFrame"));
            TestEqual(TEXT("Modal keeps the keyboard frame's origin after window reflow"),Gameplay->KeymapDialog->GetScreenOrigin(),Frame->GetScreenOrigin());
            TestEqual(TEXT("Modal retains the keyboard frame's width instead of stretching again"),Gameplay->KeymapDialog->Width,Frame->Width);
            int32 DialogChromeMin=MAX_int32;
            TArray<TSharedPtr<FACEUIElement>> Nodes{Gameplay->KeymapDialog};
            while(!Nodes.IsEmpty())
            {
                auto Node=Nodes.Pop(EAllowShrinking::No);Nodes.Append(Node->Children);
                if(const auto* Paint=Canvas->PaintStates.Find(Node->InstanceId))DialogChromeMin=FMath::Min(DialogChromeMin,Paint->Z);
            }
            for(auto* Labels:{&Gameplay->KeyboardLabels,&Gameplay->KeyboardRowLabels,&Gameplay->KeyboardKeyLabels})
                for(UTextBlock* Label:*Labels)if(Label&&Label->IsVisible())
                    TestTrue(TEXT("File-dialog chrome covers all underlying keyboard labels"),CastChecked<UCanvasPanelSlot>(Label->Slot)->GetZOrder()<DialogChromeMin);
            TestFalse(TEXT("Underlying mapping buttons cannot capture input through the file dialog"),Gameplay->KeyboardRows[0]->GetIsEnabled());
            const auto Page=Gameplay->ActiveKeyboardPage;Gameplay->HandleKeyboardNamedClick(TEXT("CameraTab"));
            TestEqual(TEXT("Modal file dialog blocks underlying tabs"),Gameplay->ActiveKeyboardPage,Page);
        }
        NativeClick(TEXT("KeymapFileCancel"));
        TestFalse(TEXT("Actual dialog Cancel closes the modal"),Gameplay->bKeymapImportOpen);
        const FString CustomPath=FPaths::ProjectSavedDir()/TEXT("Automation/CustomFixture.keymap");
        FFileHelper::SaveStringToFile(RetailCustomKeymap,*CustomPath);
        Gameplay->HandleKeymapImport(CustomPath);
        CaptureScreen(TEXT("GameplayKeyboardImportReport"));
        TestTrue(TEXT("Custom import exposes a scrollable report"),Gameplay->bKeymapReport && Gameplay->KeymapReportScroll->IsVisible());
        TestTrue(TEXT("Report distinguishes unchanged native contexts"),Gameplay->KeymapReportText->GetText().ToString().Contains(TEXT("Separate UI/system maps")));
        const auto ReportGeometry=Gameplay->KeymapReportScroll->GetCachedGeometry();
        const auto ReviewGeometry=Gameplay->KeymapDialogLabels[1]->GetCachedGeometry();
        TestTrue(TEXT("Report scroll area ends above the dialog buttons"),ReportGeometry.LocalToAbsolute(ReportGeometry.GetLocalSize()).Y<=ReviewGeometry.GetAbsolutePosition().Y);
        NativeClick(TEXT("KeymapFileOK"));
        TestFalse(TEXT("Review returns to the mapping draft"),Gameplay->bKeymapImportOpen);
        ACEInputBindings::Revert();Gameplay->RefreshKeyboardOverlays();
        TestEqual(TEXT("Key editor exposes three buttons for every keyboard action"),Gameplay->KeyboardRows.Num(),ACEInputBindings::Actions().Num()*3);
        TestEqual(TEXT("Mapping rows use the retail DAT template"),Gameplay->KeyboardEntryElements[0]->ElementId,0x1000002fu);
        TestTrue(TEXT("Load File retains the retail caption"),Gameplay->KeyboardLabels.ContainsByPredicate([](const UTextBlock* T){return T&&T->GetText().ToString()==TEXT("Load File...");}));
        for(int32 K=0;K<3;++K)
        {
            const auto Button=Gameplay->KeyboardEntryElements[0]->Children[K];
            TestEqual(TEXT("Mapping columns retain retail positions"),Button->X,270+K*100);
            TestEqual(TEXT("Mapping button includes left, middle and right chrome"),Button->Children.Num(),3);
            TestTrue(TEXT("Mapping button has a usable input target"),Gameplay->KeyboardRows[K]->IsVisible());
        }
        Gameplay->KeyboardScrollOffset=999;Gameplay->RefreshKeyboardOverlays();
        TestEqual(TEXT("Native scrollbar clamps to the last mapping row"),Gameplay->KeyboardScrollOffset,Gameplay->KeyboardMaxOffset);
        Gameplay->KeyboardScrollOffset=0;Gameplay->RefreshKeyboardOverlays();
        {
            const auto MappingList=Manager->FindElementUnder(TEXT("MovementPage"),TEXT("KeyboardMappingListBox"));
            const auto Point=Canvas->LayoutToViewport(FVector2D(MappingList->GetScreenOrigin())+FVector2D(20,80));
            TestTrue(TEXT("Mouse wheel recognizes the keyboard window over underlying panels"),Gameplay->ScrollKeyboard(-1,Point));
            TestEqual(TEXT("Wheel advances one retail mapping row"),Gameplay->KeyboardScrollOffset,1);
            Gameplay->KeyboardScrollOffset=0;Gameplay->RefreshKeyboardOverlays();
        }
        {
            auto* Key=CastChecked<UACERetailKeySelector>(Gameplay->KeyboardRows[0]);
            const auto CaptureSlate=Key->TakeWidget();
            const auto SlateButton=CaptureSlate->GetChildren()->GetChildAt(0);
            TestTrue(TEXT("Only the retail label paints; the capture control text is collapsed"),SlateButton->GetChildren()->GetChildAt(0)->GetVisibility()==EVisibility::Collapsed);
            const FKeyEvent EnterKey(EKeys::Enter,FModifierKeysState(),0,false,0,0);
            CaptureSlate->OnKeyDown(Key->GetCachedGeometry(),EnterKey);CaptureSlate->OnKeyUp(Key->GetCachedGeometry(),EnterKey);
            TestTrue(TEXT("Activating a mapping button starts key capture"),Key->GetIsSelectingKey());
            CaptureSlate->OnKeyUp(Key->GetCachedGeometry(),FKeyEvent(EKeys::F7,FModifierKeysState(),0,false,0,0));Key->RefreshBinding();
            TestEqual(TEXT("Captured key updates the draft"),ACEInputBindings::Get(EKeys::W,0).Key,EKeys::F7);
            TestEqual(TEXT("Retail label shows the captured binding"),Key->RetailLabel->GetText().ToString(),FString(TEXT("F7")));
            ACEInputBindings::Revert();Gameplay->RefreshKeyboardOverlays();
        }
        Gameplay->HandleKeyboardNamedClick(TEXT("KeyboardSaveKeymapAsButton"));
        TestTrue(TEXT("Save As opens the retail file dialog"),Gameplay->bKeymapSave&&Gameplay->bKeymapImportOpen&&Gameplay->KeymapDialog->ElementId==0x1fu);
        CaptureScreen(TEXT("GameplayKeyboardSave"));
        Gameplay->HandleKeyboardNamedClick(TEXT("KeymapFileCancel"));
        ACEInputBindings::Set(EKeys::W,0,FInputChord(EKeys::F10));
        Gameplay->HandleKeyboardNamedClick(TEXT("KeyboardCancelButton"));
        TestEqual(TEXT("Cancel does not change the active movement key"),ACEInputBindings::Get(EKeys::W,0).Key,EKeys::W);
        Gameplay->ToggleKeyboardMappingUI();
        Gameplay->HandleKeyboardNamedClick(TEXT("CharacterSettingsTab"));
        CaptureScreen(TEXT("GameplayKeyboardCharacter"));
        ACEInputBindings::Set(EKeys::W,0,FInputChord(EKeys::F10));
        Gameplay->HandleKeyboardNamedClick(TEXT("KeyboardOKButton"));
        FConfigFile SavedBindings; SavedBindings.Read(GGameUserSettingsIni);
        FString SavedForward; SavedBindings.GetString(TEXT("ACE.InputBindings"),TEXT("W.0"),SavedForward);
        TestTrue(TEXT("Key mapping is written to disk"),SavedForward.StartsWith(TEXT("F10|")));
        GConfig->SetFile(GGameUserSettingsIni,&SavedBindings);
        ACEInputBindings::Reload();
        TestEqual(TEXT("Saved movement key survives a settings reload"),ACEInputBindings::Get(EKeys::W,0).Key,EKeys::F10);
        TestFalse(TEXT("Closing key editor releases game input"),ACEInputBindings::IsEditing());
    }
    ACEInputBindings::Reload();
    if (auto Toolbar=Manager->FindElementByName(TEXT("PanelButton_SkillManagementButton")))
    {
        const auto* Down=Toolbar->ResolvePaintState(true,true,false);
        TestTrue(TEXT("Pressed toolbar icon retains visible artwork"),Down && Down->ImageFileId!=0);
        Toolbar->ResolvePaintState(false,false,false);
    }

    // Item use is exercised through the live binder and real packet construction.
    // Loopback remains isolated from the user's account/server.
    auto* Sockets=ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    FSocket* Receiver=Sockets->CreateSocket(NAME_DGram,TEXT("Inventory wire fixture"),false);
    const auto Address=Sockets->CreateInternetAddr(); bool ValidAddress=false;
    Address->SetIp(TEXT("127.0.0.1"),ValidAddress); Address->SetPort(0);
    if (Receiver && Receiver->Bind(*Address))
    {
        Receiver->GetAddress(*Address);
        auto& Session=*Client->Session;
        Session.SocketC2S=Sockets->CreateSocket(NAME_DGram,TEXT("Inventory fixture sender"),false);
        Session.ServerC2SAddr=Address; Session.IssacClient=MakeUnique<FACEIsaac>(123u);
        Gameplay->CancelPendingUseWith(); Gameplay->ShowExamination(false);
        auto Activate=[&](FACEWorldObject Object)
        {
            Session.bUseBusy=false;
            Gameplay->CancelPendingUseWith();
            Session.WorldObjects.Add(Object.Guid,Object);
            Session.CachedC2SPackets.Reset();
            Gameplay->UseInventoryItem(Object.Guid);
            TArray<uint32> Actions;
            TArray<uint32> Sequences; Session.CachedC2SPackets.GetKeys(Sequences); Sequences.Sort();
            for (uint32 Sequence:Sequences)
            {
                FACEBinaryReader Wire(Session.CachedC2SPackets[Sequence].Payload);
                Wire.Skip(16); // fragment header
                if (Wire.ReadUInt32()==ACEOpcode::GameAction)
                {
                    Wire.ReadUInt32(); // action sequence
                    Actions.Add(Wire.ReadUInt32());
                }
            }
            // Isolated activation cases do not run a server; retire their pending transaction.
            Session.CancelEquipmentSwap();
            return Actions;
        };
        // F / hand sorts the selected item: server-confirmed merges, then front of main pack.
        auto LastWire=[&](uint32 Expected, TArray<uint32> Values)
        {
            uint32 Last=0; for(const auto& Pair:Session.CachedC2SPackets) Last=FMath::Max(Last,Pair.Key);
            if (!TestTrue(TEXT("Sort emits a reliable action"),Last!=0)) return;
            FACEBinaryReader Wire(Session.CachedC2SPackets[Last].Payload); Wire.Skip(16);
            TestEqual(TEXT("Sort uses GameAction envelope"),Wire.ReadUInt32(),ACEOpcode::GameAction); Wire.ReadUInt32();
            TestEqual(TEXT("Sort emits requested action"),Wire.ReadUInt32(),Expected);
            for(uint32 Value:Values) TestEqual(TEXT("Sort sends exact server quantity/container"),Wire.ReadUInt32(),Value);
        };
        FACEWorldObject Stack; Stack.Guid=9101; Stack.ContainerId=Player.Guid; Stack.WeenieClassId=999901;
        Stack.StackSize=80; Stack.MaxStackSize=100; Stack.PlacementPosition=10;
        FACEWorldObject Match=Stack; Match.Guid=9102; Match.StackSize=50; Match.PlacementPosition=11;
        FACEWorldObject Match2=Stack; Match2.Guid=9103; Match2.StackSize=90; Match2.PlacementPosition=12;
        for(const auto& O:{Stack,Match,Match2}) Session.WorldObjects.Add(O.Guid,O);
        Session.CachedC2SPackets.Reset();
        TestTrue(TEXT("Owned selection begins sorting"),Client->SortInventoryItem(Stack.Guid));
        LastWire(ACEGameAction::StackableMerge,{uint32(Stack.Guid),uint32(Match.Guid),50});
        const int32 PendingCount=Session.CachedC2SPackets.Num();
        Client->SortInventoryItem(Stack.Guid); Client->TickInventorySort(.1f);
        TestEqual(TEXT("Repeated F cannot merge the same stale quantities twice"),Session.CachedC2SPackets.Num(),PendingCount);
        Session.WorldObjects[Stack.Guid].StackSize=30; Client->TickInventorySort(.1f);
        TestEqual(TEXT("Source-only update cannot advance the merge"),Session.CachedC2SPackets.Num(),PendingCount);
        Session.WorldObjects[Match.Guid].StackSize=100; Client->TickInventorySort(.1f);
        LastWire(ACEGameAction::StackableMerge,{uint32(Stack.Guid),uint32(Match2.Guid),10});
        Session.WorldObjects[Stack.Guid].StackSize=20; Session.WorldObjects[Match2.Guid].StackSize=100;
        Client->TickInventorySort(.1f);
        LastWire(ACEGameAction::PutItemInContainer,{uint32(Stack.Guid),uint32(Player.Guid),0});
        TestEqual(TEXT("Completed sort clears pending source"),Client->SortSourceGuid,0);
        Session.WorldObjects[Match.Guid].StackSize=50;
        Client->SortInventoryItem(Stack.Guid);
        LastWire(ACEGameAction::StackableMerge,{uint32(Stack.Guid),uint32(Match.Guid),20});
        Session.WorldObjects[Match.Guid].StackSize=70; Session.WorldObjects.Remove(Stack.Guid);
        Client->TickInventorySort(.1f);
        LastWire(ACEGameAction::PutItemInContainer,{uint32(Match.Guid),uint32(Player.Guid),0});
        TestEqual(TEXT("Consumed stack moves the surviving match to the front"),Client->SortSourceGuid,0);
        Session.WorldObjects.Add(Stack.Guid,Stack); Client->SortInventoryItem(Stack.Guid);
        Session.CachedC2SPackets.Reset(); Client->TickInventorySort(9.f);
        TestEqual(TEXT("Missing server confirmation times out without more inventory actions"),Session.CachedC2SPackets.Num(),0);
        for(int32 Guid:{Stack.Guid,Match.Guid,Match2.Guid}) Session.WorldObjects.Remove(Guid);
        FACEWorldObject Device; Device.Guid=3345; Device.Name=TEXT("Charged device");
        Device.ContainerId=Player.Guid; Device.ItemType=ACEItemType::Misc;
        Device.MaxStructure=50; Device.Structure=50; Device.ItemUseable=8;
        auto Actions=Activate(Device);
        TestTrue(TEXT("Non-targeted charged device sends Use"),Actions.Contains(ACEGameAction::Use));
        TestEqual(TEXT("Non-targeted charged device does not arm a target cursor"),Gameplay->PendingUseWithSourceGuid,0);
        Device.ItemUseable=0x080008;
        Actions=Activate(Device);
        TestFalse(TEXT("Charges cannot bypass advertised targeted use"),Actions.Contains(ACEGameAction::Use));
        TestEqual(TEXT("Charged targeted item waits for a target"),Gameplay->PendingUseWithSourceGuid,Device.Guid);
        Device.ItemUseable=9; // No | Contained: the No bit takes precedence.
        Actions=Activate(Device);
        TestFalse(TEXT("Explicitly non-usable item sends no Use"),Actions.Contains(ACEGameAction::Use));
        Device.ItemUseable=4;
        Actions=Activate(Device);
        TestFalse(TEXT("Wield-only device cannot be used from its pack"),Actions.Contains(ACEGameAction::Use));
        Device.ItemUseable=0x080004;
        Actions=Activate(Device);
        TestEqual(TEXT("Wield-only targeted device does not arm use from its pack"),Gameplay->PendingUseWithSourceGuid,0);
        Device.ItemUseable=8;
        Device.ObjectDescriptionFlags=ACEObjectDescFlag::WieldOnUse|ACEObjectDescFlag::WieldLeft;
        Device.ValidLocations=ACEEquipMask::MeleeWeapon|ACEEquipMask::Shield;
        Actions=Activate(Device);
        TestTrue(TEXT("WieldOnUse applies even to charged Misc devices"),Actions.Contains(ACEGameAction::GetAndWieldItem));
        TestFalse(TEXT("WieldOnUse takes precedence over Use"),Actions.Contains(ACEGameAction::Use));
        TestTrue(TEXT("WieldLeft advertised flag selects the left equipment branch"),
            ACEInventoryRules::DetermineOwnedUse(Device,Player.Guid)==EACEOwnedItemUse::WieldLeft);
        Device.ObjectDescriptionFlags=0; Device.ValidLocations=0;
        Device.ItemType=ACEItemType::Misc; Device.ItemsCapacity=24;
        Actions=Activate(Device);
        TestEqual(TEXT("Owned container activation opens its contents"),Gameplay->SelectedPackGuid,Device.Guid);
        TestFalse(TEXT("Owned container navigation does not send Use"),Actions.Contains(ACEGameAction::Use));
        Device.ItemsCapacity=0; Device.ItemType=ACEItemType::Clothing;
        Device.ValidLocations=ACEEquipMask::HandWear; Device.ItemUseable=1;
        TestTrue(TEXT("Equipment location wins over non-usable item flag"),
            ACEInventoryRules::DetermineOwnedUse(Device,Player.Guid)==EACEOwnedItemUse::AutoWear);
        Device.ItemType=0x20000000; Device.ValidLocations=0; Device.WeenieClassId=99999;
        TestTrue(TEXT("Salvage tool comes from ItemType, not a hardcoded WCID"),
            ACEInventoryRules::DetermineOwnedUse(Device,Player.Guid)==EACEOwnedItemUse::Salvage);
        TestEqual(TEXT("Source permissions choose least restrictive retail mode"),ACEInventoryRules::LeastLimitedSourceUse(4|8),8u);
        Device.ItemType=ACEItemType::Caster; Device.ContainerId=0; Device.WielderId=Player.Guid;
        Device.CurrentWieldedLocation=ACEEquipMask::Held;
        TestTrue(TEXT("Ordinary use of an equipped item remains PlaceInBackpack"),
            ACEInventoryRules::DetermineOwnedUse(Device,Player.Guid)==EACEOwnedItemUse::Backpack);

        for (int32 I=1; I<=4; ++I) Gameplay->SetFloatyVisible(FString::Printf(TEXT("RootGameplay_FloatyChat%d_Field"),I),false);
        Device=FACEWorldObject(); Device.Guid=3345; Device.ContainerId=Player.Guid;
        Device.Name=TEXT("Mana stone"); Device.ItemType=ACEItemType::ManaStone;
        Device.ItemUseable=0x080008; Device.UiEffects=1; Device.IconId=0x060010F9; Device.PlacementPosition=1;
        Session.WorldObjects.Add(Device.Guid,Device); Session.bUseBusy=false;
        Gameplay->SelectedPackGuid=Player.Guid;
        Gameplay->ShowPanelPage(TEXT("InventoryPanel_Field")); Gameplay->RefreshInventoryOverlays();
        CaptureScreen(TEXT("GameplayManaStone"));
        auto NativeItemClick=[&](int32 Guid, bool bDouble)
        {
            const int32 Index=Gameplay->InventorySlotGuids.IndexOfByKey(Guid);
            if (!TestTrue(TEXT("Native item slot is visible"),Gameplay->InventorySlots.IsValidIndex(Index))) return;
            const auto& Geo=Gameplay->InventorySlots[Index]->GetCachedGeometry();
            const FVector2D Abs=Geo.LocalToAbsolute(Geo.GetLocalSize()*.5);
            const auto& CanvasGeo=Canvas->GetCachedGeometry();
            FPointerEvent Down(0,Abs,Abs,TSet<FKey>{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState());
            FPointerEvent Up(0,Abs,Abs,TSet<FKey>{},EKeys::LeftMouseButton,0,FModifierKeysState());
            Canvas->NativeOnMouseButtonDown(CanvasGeo,Down); Canvas->NativeOnMouseButtonUp(CanvasGeo,Up);
            if (bDouble) { Canvas->NativeOnMouseButtonDoubleClick(CanvasGeo,Down); Canvas->NativeOnMouseButtonUp(CanvasGeo,Up); }
        };
        Session.CachedC2SPackets.Reset();
        NativeItemClick(Device.Guid,true);
        TestEqual(TEXT("Actual Slate double-click arms mana-stone targeting"),Gameplay->PendingUseWithSourceGuid,Device.Guid);
        for (const auto& Pair:Session.CachedC2SPackets)
        {
            FACEBinaryReader Wire(Pair.Value.Payload); Wire.Skip(16);
            if (Wire.ReadUInt32()!=ACEOpcode::GameAction) continue;
            Wire.ReadUInt32();
            TestNotEqual(TEXT("Double-click cannot discharge stone before selecting a target"),Wire.ReadUInt32(),ACEGameAction::UseWithTarget);
        }
        NativeItemClick(Item.Guid,false);
        TestEqual(TEXT("Clicking inventory target completes charged mana use"),Gameplay->PendingUseWithSourceGuid,0);
        auto HasAction=[&](uint32 Expected)
        {
            for (const auto& Pair:Session.CachedC2SPackets)
            {
                FACEBinaryReader Wire(Pair.Value.Payload); Wire.Skip(16);
                if (Wire.ReadUInt32()!=ACEOpcode::GameAction) continue;
                Wire.ReadUInt32(); if (Wire.ReadUInt32()==Expected) return true;
            }
            return false;
        };
        TestTrue(TEXT("Target click emits retail UseWithTarget packet"),HasAction(ACEGameAction::UseWithTarget));
        Session.bUseBusy=false; Session.CachedC2SPackets.Reset();
        Device.UiEffects=0; Session.WorldObjects.Add(Device.Guid,Device);
        Gameplay->UseInventoryItem(Device.Guid); Gameplay->TryCompletePendingUseWithTarget(Item.Guid);
        TestEqual(TEXT("Empty stone waits for destruction confirmation"),Gameplay->ManaStoneConfirmTarget,Item.Guid);
        TestFalse(TEXT("Empty stone sends nothing before confirmation"),HasAction(ACEGameAction::UseWithTarget));
        const auto ConfirmationPixels=CaptureScreen(TEXT("GameplayManaStoneConfirmation"));
        int32 ConfirmationInk=0;
        const auto Body=Manager->FindElementByName(TEXT("ManaStoneConfirmationBody"));
        const FIntPoint BodyOrigin=Body->GetScreenOrigin();
        for (int32 Y=BodyOrigin.Y; Y<BodyOrigin.Y+Body->Height; ++Y)
            for (int32 X=BodyOrigin.X; X<BodyOrigin.X+Body->Width; ++X)
                if (ConfirmationPixels.IsValidIndex(Y*1600+X))
                {
                    const auto P=ConfirmationPixels[Y*1600+X]; ConfirmationInk+=P.R>200 && P.G>200 && P.B>200;
                }
        TestTrue(TEXT("Destructive confirmation prompt is visibly rendered"),ConfirmationInk>100);
        NativeClick(TEXT("ManaStoneConfirmationNo"));
        TestEqual(TEXT("No dismisses destructive use"),Gameplay->ManaStoneConfirmSource,0);
        TestFalse(TEXT("No does not send UseWithTarget"),HasAction(ACEGameAction::UseWithTarget));
        Gameplay->UseInventoryItem(Device.Guid); Gameplay->TryCompletePendingUseWithTarget(Item.Guid);
        CaptureScreen(TEXT("GameplayManaStoneConfirmationYes")); NativeClick(TEXT("ManaStoneConfirmationYes"));
        TestTrue(TEXT("Yes sends confirmed UseWithTarget"),HasAction(ACEGameAction::UseWithTarget));
        Session.bUseBusy=false; Session.CachedC2SPackets.Reset();
        Item.ObjectDescriptionFlags=ACEObjectDescFlag::Retained; Session.WorldObjects.Add(Item.Guid,Item);
        Gameplay->UseInventoryItem(Device.Guid); Gameplay->TryCompletePendingUseWithTarget(Item.Guid);
        TestEqual(TEXT("Retained item does not open drain confirmation"),Gameplay->ManaStoneConfirmSource,0);
        TestFalse(TEXT("Retained item cannot be drained"),HasAction(ACEGameAction::UseWithTarget));
        Item.ObjectDescriptionFlags=0;
        Item.SpellDID=1; Item.ContainerId=0; Item.WielderId=Player.Guid;
        Item.CurrentWieldedLocation=Item.ValidLocations=ACEEquipMask::Held;
        Session.WorldObjects.Add(Item.Guid,Item);
        Gameplay->ApplyCombatMode(static_cast<int32>(ACECombatMode::Magic));
        Gameplay->RefreshSpellHotbarOverlays(); Gameplay->RefreshInventoryOverlays();
        CaptureScreen(TEXT("GameplayInnateSpell"));
        TestEqual(TEXT("Held caster supplies innate spell"),Gameplay->BuiltInSpellId,1);
        TestEqual(TEXT("Innate spell retains the caster use source"),Gameplay->BuiltInCasterGuid,Item.Guid);
        TestTrue(TEXT("Innate spell paints its caster icon"),Gameplay->BuiltInSpellIconBorders.Num()==2
            && Gameplay->BuiltInSpellIconBorders[1]->GetVisibility()!=ESlateVisibility::Collapsed);
        bool HasEquippedBadge=false;
        for (const auto& Pair:Gameplay->PaperDollShortcutIcons)
            HasEquippedBadge |= Pair.Value && Pair.Value->GetVisibility()!=ESlateVisibility::Collapsed;
        TestTrue(TEXT("Equipped shortcut shows its inventory numeral"),HasEquippedBadge);
        // Scroll the real DAT arrow and cast: scrolling must not change selection.
        Session.SpellBars.SetNum(8); Session.ActiveSpellBar=0;
        auto& Favorites=Session.SpellBars[0]; Favorites.Reset();
        Gameplay->SelectedCombatSpellSlot=0; Gameplay->SpellHotbarScrollOffset=0;
        Gameplay->RefreshSpellHotbarOverlays();
        CaptureScreen(TEXT("GameplayEmptyNumberedSpellBar"));
        for (int32 I=0; I<Gameplay->SpellBarSpellIds.Num(); ++I)
        {
            const int32 ExpectedDid=I<9 ? 0x060010FA+I : 0x06001A97;
            UTexture2D* ExpectedTexture=Resources->ResolveIconTexture(ExpectedDid);
            TestNotNull(TEXT("Empty spell slot artwork resolves from DAT"),ExpectedTexture);
            TestTrue(TEXT("Empty spell bar has blue 1–9 followed by brown tiles"),
                Gameplay->SpellBarSlotBgs[I]->Background.GetResourceObject()==ExpectedTexture);
            TestTrue(TEXT("Empty numbered tiles have no occupied-spell badge"),
                Gameplay->SpellBarSlotNumIcons[I]->GetVisibility()==ESlateVisibility::Collapsed);
        }
        Favorites={1,2,3}; Gameplay->RefreshSpellHotbarOverlays();
		TestEqual(TEXT("Spell artwork reads its formula's power component"), Dat->GetSpellIconPowerLevel(1), 1u);
		for (int32 I = 0; I < 3; ++I)
		{
			FString Name; uint32 IconId = 0; Dat->TryGetSpellInfo(Favorites[I], Name, IconId);
			auto* Raw = Resources->ResolveIconTexture(IconId);
			auto* Composed = Resources->ResolveSpellIcon(Favorites[I]);
			TestTrue(TEXT("Hotbar uses the retail-composed spell icon"), Composed && Composed != Raw && Gameplay->SpellBarIcons[I]->Background.GetResourceObject() == Composed);
			if (Raw && Composed)
			{
				auto& RawMip = Raw->GetPlatformData()->Mips[0]; auto& FinalMip = Composed->GetPlatformData()->Mips[0];
				const auto* RawPixels = static_cast<const FColor*>(RawMip.BulkData.LockReadOnly());
				const auto* FinalPixels = static_cast<const FColor*>(FinalMip.BulkData.LockReadOnly());
				int32 Mask = 0, Changed = 0;
				for (int32 P = 0; P < 1024; ++P) if (RawPixels[P] == FColor::White) { ++Mask; Changed += FinalPixels[P] != FColor::White; }
				RawMip.BulkData.Unlock(); FinalMip.BulkData.Unlock();
				TestTrue(TEXT("The raw spell's white surround is replaced by authored spell artwork"), Mask > 20 && Changed == Mask);
			}
		}
        TestTrue(TEXT("Occupied spell keeps its separate key badge"),
            Gameplay->SpellBarSlotNumIcons[1]->Background.GetResourceObject()==Resources->ResolveIconTexture(0x060019EE)
            && Gameplay->SpellBarSlotNumIcons[1]->GetVisibility()!=ESlateVisibility::Collapsed);
        TestTrue(TEXT("First empty tile after three favorites is blue 4"),
            Gameplay->SpellBarSlotBgs[3]->Background.GetResourceObject()==Resources->ResolveIconTexture(0x060010FD));
        const auto LeftArrow=Manager->FindElementUnder(TEXT("Spellcasting_Bank1"),TEXT("ScrollBar_Up"));
        const auto RightArrow=Manager->FindElementUnder(TEXT("Spellcasting_Bank1"),TEXT("ScrollBar_Down"));
        TestTrue(TEXT("Short favorite list hides both scroll arrows"),LeftArrow && RightArrow && !LeftArrow->bVisible && !RightArrow->bVisible);
        TestEqual(TEXT("Short favorite list fills the retail viewport"),Gameplay->SpellBarSpellIds.Num(),13);
        TestEqual(TEXT("Insertion cell is empty"),Gameplay->SpellBarSpellIds.Last(),0);
        CaptureScreen(TEXT("GameplayShortSpellBar"));
        auto DropSpellAtEnd=[&](int32 Id)
        {
            Gameplay->SpellDragId=Id; Gameplay->SpellDragSourceBarSlot=INDEX_NONE;
            Gameplay->bSpellDragPending=Gameplay->bSpellDragActive=true;
            const int32 Last=Gameplay->SpellBarSpellIds.Num()-1;
            const auto& G=Gameplay->SpellBarSlotBgs[Last]->GetCachedGeometry();
            Gameplay->TryFinishSpellDrag(Canvas->GetCachedGeometry().AbsoluteToLocal(G.LocalToAbsolute(G.GetLocalSize()*.5)));
        };
        Session.CachedC2SPackets.Reset(); DropSpellAtEnd(4);
        TestTrue(TEXT("Dropping into end cell sends favorite action"),HasAction(ACEGameAction::AddToSpellBar));
        TestEqual(TEXT("Dropping on a blank tile appends without creating gaps"),Favorites.Num(),4);
        TestEqual(TEXT("Filling end cell retains the empty viewport tiles"),Gameplay->SpellBarSpellIds.Num(),13);
        TestEqual(TEXT("Fresh insertion cell follows the new favorite"),Gameplay->SpellBarSpellIds[4],0);
        TestEqual(TEXT("New end cell remains empty"),Gameplay->SpellBarSpellIds.Last(),0);
        Favorites.Reset(); for(int32 I=1;I<=15;++I) Favorites.Add(I);
        Gameplay->RefreshSpellHotbarOverlays(); CaptureScreen(TEXT("GameplayOverflowSpellBar"));
        TestTrue(TEXT("Overflowing favorite list shows scroll arrows"),LeftArrow->bVisible && RightArrow->bVisible);
        const FVector2D ArrowLayout=FVector2D(RightArrow->GetScreenOrigin())+FVector2D(RightArrow->Width,RightArrow->Height)*.5;
        const auto& G=Canvas->GetCachedGeometry(); const FVector2D ArrowAbsolute=G.LocalToAbsolute(Canvas->LayoutToViewport(ArrowLayout));
        Canvas->NativeOnMouseButtonDown(G,FPointerEvent(0,ArrowAbsolute,ArrowAbsolute,{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState()));
        Canvas->NativeOnMouseButtonUp(G,FPointerEvent(0,ArrowAbsolute,ArrowAbsolute,{},EKeys::LeftMouseButton,0,FModifierKeysState()));
        TestEqual(TEXT("Right arrow scrolls one entry"),Gameplay->SpellHotbarScrollOffset,1);
        TestEqual(TEXT("Scrolling preserves absolute selected favorite"),Gameplay->SelectedCombatSpellSlot,0);
        FACEWorldObject SpellRecipient; SpellRecipient.Guid=1235; SpellRecipient.bIsPlayer=true; SpellRecipient.ItemType=ACEItemType::Creature;
        Session.WorldObjects.Add(SpellRecipient.Guid,SpellRecipient);
        Session.SelectedObject.bValid=true; Session.SelectedObject.Guid=SpellRecipient.Guid;
        Session.CachedC2SPackets.Reset(); Gameplay->CastSelectedHotbarSpell();
        bool CastOriginal=false;
        for(const auto& P:Session.CachedC2SPackets)
        {
            FACEBinaryReader Wire(P.Value.Payload); Wire.Skip(16);
            if(Wire.ReadUInt32()!=ACEOpcode::GameAction)continue;
            Wire.ReadUInt32(); const uint32 Action=Wire.ReadUInt32();
            if(Action==ACEGameAction::CastTargetedSpell) { Wire.ReadUInt32(); CastOriginal |= Wire.ReadUInt32()==1; }
            if(Action==ACEGameAction::CastUntargetedSpell) CastOriginal |= Wire.ReadUInt32()==1;
        }
        TestTrue(TEXT("Cast still sends the favorite selected before scrolling"),CastOriginal);
        Session.CachedC2SPackets.Reset(); Gameplay->ActivateHotbarSlot(1);
        TestEqual(TEXT("Key 2 remains the second absolute favorite after scrolling"),Gameplay->SelectedCombatSpellSlot,1);
        Gameplay->SpellHotbarScrollOffset=5; Gameplay->RefreshSpellHotbarOverlays();
        Gameplay->ActivateHotbarSlot(1);
        TestEqual(TEXT("Repeated scrolling never remaps the first ten bindings"),Gameplay->SelectedCombatSpellSlot,1);

        Gameplay->SpellHotbarScrollOffset=3; Gameplay->RefreshSpellHotbarOverlays(); CaptureScreen(TEXT("GameplaySpellBarEnd"));
        TestEqual(TEXT("Scroll reaches the empty end cell"),Gameplay->SpellBarSpellIds.Last(),0);
        DropSpellAtEnd(16);
        TestEqual(TEXT("End drop grows the favorite list"),Session.SpellBars[0].Num(),16);
        if (Session.SpellBars[0].IsValidIndex(15))
            TestEqual(TEXT("End drop appends after hidden earlier favorites"),Session.SpellBars[0][15],16);
        Gameplay->SelectedCombatSpellSlot=-1; Gameplay->RefreshSpellHotbarOverlays();
        Session.CachedC2SPackets.Reset(); Gameplay->CastSelectedHotbarSpell();
        TestTrue(TEXT("Innate selection casts through item UseWithTarget"),HasAction(ACEGameAction::UseWithTarget));


        // The actual stat widgets must consume recomputed server values and retain
        // base/current details when a selected skill is buffed or debuffed.
        auto ClickStatRow=[&](UBorder* Row, float Fraction)
        {
            const auto& RowGeometry=Row->GetCachedGeometry();
            const FVector2D Absolute=RowGeometry.LocalToAbsolute(RowGeometry.GetLocalSize()*FVector2D(Fraction,.5));
            const auto& Geometry=Canvas->GetCachedGeometry();
            Canvas->NativeOnMouseButtonDown(Geometry,FPointerEvent(0,Absolute,Absolute,{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState()));
            Canvas->NativeOnMouseButtonUp(Geometry,FPointerEvent(0,Absolute,Absolute,{},EKeys::LeftMouseButton,0,FModifierKeysState()));
        };
        auto CheckStatHighlight=[&](UBorder* Row, bool Selected)
        {
            TestEqual(TEXT("Row brush matches its current selection"),Row->Background.DrawAs.GetValue(),
                Selected?ESlateBrushDrawType::Image:ESlateBrushDrawType::NoDrawType);
            if (Selected) TestTrue(TEXT("Reselection restores actual retail artwork"),
                Row->Background.GetResourceObject()==Resources->ResolveIconTexture(0x06000F93));
        };
        Session.StatResolver = [Dat](auto& V, const auto& E) { Dat->RecomputePlayerStats(V,E); };
        auto& Stats = Session.PlayerVitals;
        Stats.bValid=true; Stats.Coordination=120; Stats.Focus=180;
        FACESkillInfo Cooking; Cooking.SkillId=39; Cooking.AdvancementClass=2; Cooking.InitLevel=5; Cooking.Ranks=10;
        Stats.Skills={Cooking}; Session.NotifyVitalsChanged();
        Gameplay->ShowPanelPage(TEXT("SkillManagementPanel_Field")); Gameplay->SyncSkillPanelTab(TEXT("SkillPage"));
        Gameplay->SelectedSkillId=39;
        FACEActiveEnchantment CookBuff; CookBuff.SpellId=3; CookBuff.SpellCategory=102;
        CookBuff.StatModType=0x8010; CookBuff.StatModKey=39; CookBuff.StatModValue=40;
        Session.ActiveEnchantments={CookBuff}; Session.NotifyVitalsChanged();
        Client->OnVitalsUpdated.Broadcast(Stats); CaptureScreen(TEXT("GameplayCookingBuffed"));
        TestEqual(TEXT("Selected skill footer shows total and bonus"),Gameplay->AttrFooterTitle->GetText().ToString(),FString(TEXT("Cooking: 155 (+40)")));
        const int32 CookingRow = Gameplay->SkillRowIds.IndexOfByKey(39);
        if (TestTrue(TEXT("Cooking has a rendered skill row"),CookingRow!=INDEX_NONE))
        {
            TestTrue(TEXT("Cooking displays its authored skill icon"), Gameplay->SkillRowIcons[CookingRow]->GetVisibility() != ESlateVisibility::Collapsed);
            auto* Value=Gameplay->SkillRowValues[CookingRow].Get();
            TestEqual(TEXT("Skill tab shows full buffed Cooking value"),Value->GetText().ToString(),FString(TEXT("155")));
            TestEqual(TEXT("Skill tab keeps base and current available"),Value->GetToolTipText().ToString(),FString(TEXT("Base: 115\nCurrent: 155")));
            const FLinearColor BuffColor=Value->GetColorAndOpacity().GetSpecifiedColor();
            TestTrue(TEXT("Selected buffed skill stays green"),BuffColor.G>BuffColor.R);
            auto* Highlight=Gameplay->SkillRowHighlights[CookingRow].Get();
            for (float Fraction : {.04f,.5f,.95f})
            {
                ClickStatRow(Highlight,Fraction);
                TestEqual(TEXT("Clicking selected skill immediately deselects it"),Gameplay->SelectedSkillId,0);
                CheckStatHighlight(Highlight,false);
                TestTrue(TEXT("Deselecting skill immediately clears footer"),Gameplay->AttrFooterTitle->GetText().ToString().StartsWith(TEXT("Select")));
                Client->OnVitalsUpdated.Broadcast(Stats);
                TestEqual(TEXT("Stat updates preserve deselection"),Gameplay->SelectedSkillId,0);
                ClickStatRow(Highlight,Fraction);
                TestEqual(TEXT("Clicking skill again immediately selects it"),Gameplay->SelectedSkillId,39);
                CheckStatHighlight(Highlight,true);
                TestEqual(TEXT("Reselection immediately restores skill footer"),Gameplay->AttrFooterTitle->GetText().ToString(),FString(TEXT("Cooking: 155 (+40)")));
            }
            Session.ActiveEnchantments[0].StatModValue=-20; Session.NotifyVitalsChanged(); Client->OnVitalsUpdated.Broadcast(Stats);
            TestEqual(TEXT("Debuff update refreshes the existing row"),Value->GetText().ToString(),FString(TEXT("95")));
            const FLinearColor DebuffColor=Value->GetColorAndOpacity().GetSpecifiedColor();
            TestTrue(TEXT("Selected debuffed skill becomes red"),DebuffColor.R>DebuffColor.G);
        }
        Session.ActiveEnchantments.Reset(); Session.NotifyVitalsChanged(); Client->OnVitalsUpdated.Broadcast(Stats);
        Gameplay->SyncSkillPanelTab(TEXT("AttributePage"));
        FACEActiveEnchantment FocusBuff=CookBuff; FocusBuff.StatModType=0x8001; FocusBuff.StatModKey=5; FocusBuff.StatModValue=60;
        Session.ActiveEnchantments={FocusBuff}; Session.NotifyVitalsChanged(); Client->OnVitalsUpdated.Broadcast(Stats);
        Gameplay->SelectedAttributeRow=4; Gameplay->RefreshAttributeOverlays();
        CaptureScreen(TEXT("GameplayAttributesBuffed"));
        TestEqual(TEXT("Selected attribute footer shows total and bonus"),Gameplay->AttrFooterTitle->GetText().ToString(),FString(TEXT("Focus: 240 (+60)")));
        TestEqual(TEXT("Coordination is the third retail attribute row"),Gameplay->AttributeRows[2]->GetText().ToString(),FString(TEXT("Coordination")));
        TestEqual(TEXT("Attribute tab shows current Focus"),Gameplay->AttributeRowValues[4]->GetText().ToString(),FString(TEXT("240")));
        TestEqual(TEXT("Attribute tab retains raw Focus"),Gameplay->AttributeRowValues[4]->GetToolTipText().ToString(),FString(TEXT("Base: 180\nCurrent: 240")));
        auto* FocusHighlight=Gameplay->AttributeRowHighlights[4].Get();
        for (float Fraction : {.04f,.5f,.95f})
        {
            ClickStatRow(FocusHighlight,Fraction);
            TestEqual(TEXT("Clicking selected attribute deselects it"),Gameplay->SelectedAttributeRow,INDEX_NONE);
            CheckStatHighlight(FocusHighlight,false);
            TestEqual(TEXT("Deselecting attribute immediately clears footer"),Gameplay->AttrFooterTitle->GetText().ToString(),FString(TEXT("Select an Attribute to Improve")));
            Client->OnVitalsUpdated.Broadcast(Stats);
            TestEqual(TEXT("Refreshing does not force Strength selection"),Gameplay->SelectedAttributeRow,INDEX_NONE);
            CheckStatHighlight(Gameplay->AttributeRowHighlights[0],false);
            ClickStatRow(FocusHighlight,Fraction);
            TestEqual(TEXT("Attribute reselection survives refresh"),Gameplay->SelectedAttributeRow,4);
            CheckStatHighlight(FocusHighlight,true);
        }
        ClickStatRow(Gameplay->AttributeRowHighlights[2],.5f);
        TestEqual(TEXT("Selecting a different attribute changes the selected row"),Gameplay->SelectedAttributeRow,2);
        CheckStatHighlight(FocusHighlight,false);
        CheckStatHighlight(Gameplay->AttributeRowHighlights[2],true);
        ClickStatRow(FocusHighlight,.5f);
        CheckStatHighlight(FocusHighlight,true);
        CheckStatHighlight(Gameplay->AttributeRowHighlights[2],false);
        Session.ActiveEnchantments.Reset(); Session.NotifyVitalsChanged(); Client->OnVitalsUpdated.Broadcast(Stats);
        TestEqual(TEXT("Attribute expiry refreshes the tab"),Gameplay->AttributeRowValues[4]->GetText().ToString(),FString(TEXT("180")));

        // A capped skill retains a meaningful value in the footer.
        Gameplay->LastVitals.Skills[0].XpSpent=MAX_uint32;
        Gameplay->SelectedSkillId=39;
        Gameplay->SyncSkillPanelTab(TEXT("SkillPage")); Gameplay->RefreshSkillOverlays();
        TestEqual(TEXT("Maximum trained skill reads Infinite"),Gameplay->AttrFooterLine1Value->GetText().ToString(),FString(TEXT("Infinite")));

        // Titles: exercise the real sorted row hit regions and SetTitle wire action.
        FACEBinaryWriter Titles;
        Titles.WriteUInt32(1); Titles.WriteUInt32(1); Titles.WriteUInt32(60);
        for (uint32 Id=60; Id>0; --Id) Titles.WriteUInt32(Id);
        FACEBinaryReader TitlesReader(Titles.GetData()); Session.HandleCharacterTitle(TitlesReader);
        Gameplay->ShowPanelPage(TEXT("SkillManagementPanel_Field"));
        Gameplay->SyncSkillPanelTab(TEXT("CharacterTitlePage"));
        CaptureScreen(TEXT("GameplayCharacterTitles"));
        const auto TitleList=Manager->FindElementUnder(TEXT("CharacterTitlePage"),TEXT("CharacterTitle_ListBox"));
        const auto SetTitleButton=Manager->FindElementUnder(TEXT("CharacterTitlePage"),TEXT("CharacterTitle_SetAsDisplayButton"));
        TestTrue(TEXT("Title list leaves room for current title and footer"), TitleList && SetTitleButton
            && TitleList->Y==90 && SetTitleButton->Y>=TitleList->Y+TitleList->Height);
        TestTrue(TEXT("Title list is sorted by retail title names"),Gameplay->SortedTitleIds.Num()==60
            && FCString::Strcmp(GetCharacterTitleName(Gameplay->SortedTitleIds[0]),GetCharacterTitleName(Gameplay->SortedTitleIds[1]))<=0);
        for (int32 I=0; I<3; ++I)
        {
            auto* TitleRowWidget=Gameplay->TitleRows[I].Get();
            const auto* RowSlot=Cast<UCanvasPanelSlot>(TitleRowWidget->Slot);
            TestTrue(TEXT("Title hit region uses the retail 24-pixel template"), RowSlot && FMath::IsNearlyEqual(RowSlot->GetSize().Y,24*Canvas->GetLastScaleY(),.1));
            TestNotNull(TEXT("Title rows use retail bitmap glyphs"),Cast<UACERetailTextBlock>(TitleRowWidget)->GetBitmapFont());
            const auto& Geo=TitleRowWidget->GetCachedGeometry();
            TestTrue(TEXT("Title row click is handled"),Gameplay->TryHandleOverlayClick(Canvas->GetElementLayer()->GetCachedGeometry().AbsoluteToLocal(Geo.LocalToAbsolute(Geo.GetLocalSize()*.5)),false));
            TestEqual(TEXT("Click selects the displayed title, not server ordering"),Gameplay->SelectedTitleId,Gameplay->SortedTitleIds[I]);
        }
        Gameplay->SelectedTitleId=Gameplay->SortedTitleIds[0];
        if (Gameplay->SelectedTitleId==Session.DisplayTitleId) Gameplay->SelectedTitleId=Gameplay->SortedTitleIds[1];
        Gameplay->RefreshTitleOverlays(); CaptureScreen(TEXT("GameplayCharacterTitleSelected"));
        Session.CachedC2SPackets.Reset(); NativeClick(TEXT("CharacterTitle_SetAsDisplayButton"));
        TestTrue(TEXT("Set title sends the retail network action"),HasAction(ACEGameAction::TitleSet));
        FACEBinaryWriter TitleUpdate; TitleUpdate.WriteUInt32(Gameplay->SelectedTitleId); TitleUpdate.WriteUInt32(1);
        FACEBinaryReader TitleUpdateReader(TitleUpdate.GetData()); Session.HandleUpdateTitle(TitleUpdateReader);
        Gameplay->RefreshTitleOverlays();
        TestEqual(TEXT("Server display-title response refreshes the panel"),Gameplay->TitleCurrentValue->GetText().ToString(),FString(GetCharacterTitleName(Gameplay->SelectedTitleId)));
        TestTrue(TEXT("Current display title cannot be set again"),SetTitleButton->bGhosted);
        Gameplay->ScrollStatList(-1);
        TestTrue(TEXT("Title scrolling advances visible rows"),Gameplay->TitleListScrollOffset>0);

        Gameplay->ShowPanelPage(TEXT("InventoryPanel_Field"));
        Gameplay->RefreshInventoryOverlays();

        FACEWorldObject ShortcutUse;
        ShortcutUse.Guid=98765; ShortcutUse.Name=TEXT("Shortcut use fixture");
        ShortcutUse.ContainerId=Session.PlayerGuid; ShortcutUse.ItemType=ACEItemType::Food;
        ShortcutUse.ItemUseable=8;
        Session.WorldObjects.Add(ShortcutUse.Guid,ShortcutUse); Session.ShortcutObjects[0]=ShortcutUse.Guid;
        Gameplay->RefreshShortcutOverlays(); CaptureScreen(TEXT("GameplayItemShortcuts"));
        Session.bUseBusy=false; Session.CachedC2SPackets.Reset();
        Gameplay->LastInvClickGuid=0;
        NativeClick(TEXT("ShortcutBar_Shortcut1Button"));
        TestEqual(TEXT("Single shortcut click selects the actual item"),Client->GetSelectedObject().Guid,ShortcutUse.Guid);
        TestFalse(TEXT("Single shortcut click only selects"),HasAction(ACEGameAction::Use));
        NativeClick(TEXT("ShortcutBar_Shortcut1Button"));
        TestTrue(TEXT("Double shortcut click uses the item"),HasAction(ACEGameAction::Use));
        Session.bUseBusy=false; Session.CachedC2SPackets.Reset();
        Gameplay->CombatMode=static_cast<int32>(ACECombatMode::NonCombat);
        Gameplay->ActivateHotbarSlot(0);
        TestTrue(TEXT("Number key still uses an item immediately"),HasAction(ACEGameAction::Use));

        const auto Toolbar=Manager->FindElementByName(TEXT("RootGameplay_FloatyToolbar_Field"));
        TestEqual(TEXT("Full retail toolbar is tall enough for both rows"),Toolbar->Height,132);
        FACEWorldObject SecondShortcut=ShortcutUse; SecondShortcut.Guid=98766; SecondShortcut.Name=TEXT("Second-row use fixture");
        Session.WorldObjects.Add(SecondShortcut.Guid,SecondShortcut);
        Session.ShortcutObjects[17]=SecondShortcut.Guid;
        for (float UIScale : {1.f,2.f})
        {
            CaptureScreen(FString::Printf(TEXT("RetailTwoShortcutRows_%d"),int32(UIScale)),UIScale);
            for (int32 Index=0;Index<18;++Index)
            {
                const auto El=Manager->FindElementByName(FString::Printf(TEXT("ShortcutBar%s_Shortcut%dButton"),Index<9?TEXT(""):TEXT("2"),Index%9+1));
                const FIntPoint O=El->GetScreenOrigin();
                const FVector2D Local=Canvas->LayoutToViewport(FVector2D(O)+FVector2D(16,16));
                TestEqual(TEXT("Every displayed shortcut has an independent hit target"),Gameplay->HitTestShortcutSlot(Local),Index);
                TestTrue(TEXT("Both rows lie within toolbar clipping bounds"),O.Y+El->Height<=Toolbar->GetScreenOrigin().Y+Toolbar->Height-5);
                TestTrue(TEXT("Both rows paint icons/backgrounds"),Gameplay->ShortcutIcons[Index]->GetVisibility()!=ESlateVisibility::Collapsed);
            }
            Session.bUseBusy=false; Session.CachedC2SPackets.Reset(); Gameplay->LastInvClickGuid=0;
            NativeClick(TEXT("ShortcutBar2_Shortcut9Button"));
            TestEqual(TEXT("Second row selects its item"),Client->GetSelectedObject().Guid,SecondShortcut.Guid);
            TestFalse(TEXT("Second row single click does not activate"),HasAction(ACEGameAction::Use));
            NativeClick(TEXT("ShortcutBar2_Shortcut9Button"));
            TestTrue(TEXT("Second row double click sends use"),HasAction(ACEGameAction::Use));
        }
        CaptureScreen(TEXT("RetailShortcutDrag"));
        {
            TGuardValue<FString> SettingsPath(GGameUserSettingsIni,FPaths::ProjectSavedDir()/TEXT("Automation/ToolbarResizeFixture.ini"));
            FConfigFile Config; Config.NoSave=false; Config.bCanSaveAllSections=true;
            GConfig->SetFile(GGameUserSettingsIni,&Config);
            const bool Locked=Manager->IsUiLocked(); Manager->SetUiLocked(false);
            const FIntPoint SavedDrag(Toolbar->UserDragX,Toolbar->UserDragY);
            TestEqual(TEXT("Retail toolbar minimum fits one row"),Toolbar->MinHeight,100);
            TestEqual(TEXT("Retail toolbar maximum fits two rows"),Toolbar->MaxHeight,132);
            for (uint32 Did : {UACEMouseCursorWidget::MoveCursorDid,UACEMouseCursorWidget::ResizeVerticalCursorDid,
                UACEMouseCursorWidget::ResizeHorizontalCursorDid,UACEMouseCursorWidget::ResizeNWSECursorDid,
                UACEMouseCursorWidget::ResizeNESWCursorDid})
                TestNotNull(TEXT("Retail window cursor artwork resolves from DAT"),Resources->ResolveIconTexture(Did));
            for (float UIScale : {1.f,2.f})
            {
                Manager->BringFloatyToFront(Toolbar);
                CaptureScreen(TEXT("ToolbarResizeSetup"),UIScale);
                Toolbar->UserDragX+=400-Toolbar->GetScreenOrigin().X;
                Toolbar->UserDragY+=220-Toolbar->GetScreenOrigin().Y; Toolbar->RecomputeLayoutOffset();
                CaptureScreen(TEXT("ToolbarResizeSetup"),UIScale);
                auto GripPoint=[&](const TCHAR* Name)
                {
                    const auto El=Manager->FindElementByName(Name);
                    return Canvas->LayoutToViewport(FVector2D(El->GetScreenOrigin())+FVector2D(El->Width,El->Height)*.5);
                };
                auto Resize=[&](int32 Delta)
                {
                    const FVector2D Start=GripPoint(TEXT("ToolbarBottomBorder"));
                    const FVector2D Size=Canvas->GetCachedGeometry().GetLocalSize();
                    TestTrue(TEXT("Toolbar bottom border advertises vertical resizing"),Manager->GetWindowCursor(Start,Size)==EMouseCursor::ResizeUpDown);
                    Manager->NotifyMouseDown(Start,Size,EKeys::LeftMouseButton);
                    TestTrue(TEXT("Resize cursor survives capture outside the viewport"),Manager->GetWindowCursor(FVector2D(-10,-10),Size)==EMouseCursor::ResizeUpDown);
                    const FVector2D End=Start+FVector2D(0,Delta*Canvas->GetLastScale2D().Y);
                    Manager->NotifyMouseMove(End,Size);
                    Manager->NotifyMouseUp(End,Size,EKeys::LeftMouseButton);
                };
                for (const TCHAR* Corner : {TEXT("ToolbarBottomLeftCorner"),TEXT("ToolbarBottomRightCorner")})
                    TestTrue(TEXT("Toolbar bottom corners also advertise vertical resizing"),Manager->GetWindowCursor(GripPoint(Corner),Canvas->GetCachedGeometry().GetLocalSize())==EMouseCursor::ResizeUpDown);
                const int32 Top=Toolbar->GetScreenOrigin().Y;
                Resize(-32);
                CaptureScreen(FString::Printf(TEXT("RetailOneShortcutRow_%d"),int32(UIScale)),UIScale);
                TestEqual(TEXT("Dragging up hides second shortcut row"),Toolbar->Height,100);
                TestEqual(TEXT("Resizing bottom keeps toolbar top fixed"),Toolbar->GetScreenOrigin().Y,Top);
                for (int32 Index=0;Index<18;++Index)
                {
                    const auto El=Manager->FindElementByName(FString::Printf(TEXT("ShortcutBar%s_Shortcut%dButton"),Index<9?TEXT(""):TEXT("2"),Index%9+1));
                    const FVector2D Point=Canvas->LayoutToViewport(FVector2D(El->GetScreenOrigin())+FVector2D(16,16));
                    TestEqual(TEXT("Collapsed row cannot select items through the world"),Gameplay->HitTestShortcutSlot(Point),Index<9?Index:INDEX_NONE);
                    TestEqual(TEXT("Only the visible row paints shortcut overlays"),Gameplay->ShortcutIcons[Index]->GetVisibility()==ESlateVisibility::Collapsed,Index>=9);
                }
                TestEqual(TEXT("Collapsed border stays at the bottom after reflow"),Manager->FindElementByName(TEXT("ToolbarBottomBorder"))->GetScreenOrigin().Y,Top+95);
                TestEqual(TEXT("Hidden row does not intercept bottom resize grip"),Gameplay->HitTestShortcutSlot(GripPoint(TEXT("ToolbarBottomBorder"))),INDEX_NONE);
                Manager->SetUiLocked(true); Manager->SaveFloatyLayout();
                TestTrue(TEXT("Locked toolbar has no resize cursor"),Manager->GetWindowCursor(GripPoint(TEXT("ToolbarBottomBorder")),Canvas->GetCachedGeometry().GetLocalSize())==EMouseCursor::Default);
                // Simulate a fresh manager's defaults before restoring the saved layout.
                Manager->SetUiLocked(false); GConfig->SetBool(TEXT("ACEClient.DatHUD"),TEXT("UiLocked"),true,GGameUserSettingsIni);
                Toolbar->UserResizeH=0; UACEUIElementManager::ApplyFloatyResizeLayout(Toolbar);
                Manager->LoadFloatyLayout();
                TestTrue(TEXT("Layout reload restores the saved lock state"),Manager->IsUiLocked());
                TestEqual(TEXT("Collapsed toolbar survives layout reload"),Toolbar->Height,100);
                Manager->SetUiLocked(false); CaptureScreen(TEXT("ToolbarRestored"),UIScale);
                Resize(16); CaptureScreen(FString::Printf(TEXT("RetailPartialShortcutRow_%d"),int32(UIScale)),UIScale);
                TestEqual(TEXT("Retail toolbar can reveal part of second row"),Toolbar->Height,116);
                TestTrue(TEXT("Shortcut art clips instead of stretching when partly revealed"),Gameplay->ShortcutClipPanel->GetClipping()==EWidgetClipping::ClipToBoundsAlways);
                TestEqual(TEXT("Partial row keeps the original icon height"),Cast<UCanvasPanelSlot>(Gameplay->ShortcutIcons[17]->Slot)->GetSize().Y,32.*Canvas->GetLastScale2D().Y);
                Resize(80); CaptureScreen(TEXT("ToolbarExpanded"),UIScale);
                TestEqual(TEXT("Toolbar stops at exactly two rows"),Toolbar->Height,132);
                TestEqual(TEXT("Toolbar retains the retail fixed width"),Toolbar->Width,310);
                TestTrue(TEXT("Cursor outside viewport resets after release"),Manager->GetWindowCursor(FVector2D(-10,-10),Canvas->GetCachedGeometry().GetLocalSize())==EMouseCursor::Default);
            }
            Toolbar->UserDragX=SavedDrag.X; Toolbar->UserDragY=SavedDrag.Y; Toolbar->RecomputeLayoutOffset();
            Manager->SetUiLocked(Locked); CaptureScreen(TEXT("RetailShortcutDrag"));
        }
        auto ShortcutPoint=[&](const TCHAR* Name)
        {
            const auto El=Manager->FindElementByName(Name);
            return Canvas->LayoutToViewport(FVector2D(El->GetScreenOrigin())+FVector2D(16,16));
        };
        const FVector2D FirstPoint=ShortcutPoint(TEXT("ShortcutBar_Shortcut1Button"));
        const FVector2D LastPoint=ShortcutPoint(TEXT("ShortcutBar2_Shortcut9Button"));
        TestTrue(TEXT("Shortcut can be dragged from row one"),Gameplay->TryBeginInventoryDrag(FirstPoint));
        Gameplay->UpdateInventoryDrag(LastPoint); Gameplay->TryFinishInventoryDrag(LastPoint);
        TestEqual(TEXT("Shortcut drag moves into row two"),Client->GetShortcutObject(17),ShortcutUse.Guid);
        TestEqual(TEXT("Occupied shortcut swaps back to source slot"),Client->GetShortcutObject(0),SecondShortcut.Guid);
        Session.CachedC2SPackets.Reset();
        TestTrue(TEXT("Second-row shortcut can be dragged out"),Gameplay->TryBeginInventoryDrag(LastPoint));
        Gameplay->UpdateInventoryDrag(FVector2D(700,300)); Gameplay->TryFinishInventoryDrag(FVector2D(700,300));
        TestEqual(TEXT("Dragging out removes the shortcut"),Client->GetShortcutObject(17),0);
        TestTrue(TEXT("Removing a shortcut retains the owned item"),Session.WorldObjects.Contains(ShortcutUse.Guid));
        TestTrue(TEXT("Shortcut removal is sent to server"),HasAction(ACEGameAction::RemoveShortCut));
        TestFalse(TEXT("Wheel no longer remaps shortcut bindings"),Gameplay->ScrollShortcutBar(-1));
        Session.ShortcutObjects[17]=0; Session.WorldObjects.Remove(SecondShortcut.Guid);

        // The whole title and all retail Dragbar edges move an unlocked window.
        const bool WasLocked=Manager->IsUiLocked(); Manager->SetUiLocked(false);
        const auto Panel=Manager->FindElementByName(TEXT("RootGameplay_FloatyPanel_Field"));
        const FIntPoint SavedPanelDrag(Panel->UserDragX,Panel->UserDragY);
        for (const TCHAR* Handle : {TEXT("InvTitleText"),TEXT("PanelTopBorder"),TEXT("PanelLeftBorder"),TEXT("PanelRightBorder")})
        {
            Manager->BringFloatyToFront(Panel); CaptureScreen(TEXT("RetailWindowDrag"));
            const auto El=Manager->FindElementByName(Handle);
            const FVector2D Start=Canvas->LayoutToViewport(FVector2D(El->GetScreenOrigin())+FVector2D(El->Width,El->Height)*.5);
            const int32 OldX=Panel->UserDragX,OldY=Panel->UserDragY;
            TestTrue(TEXT("Movable title and borders display the retail move cursor"),Manager->GetWindowCursor(Start,Canvas->GetCachedGeometry().GetLocalSize())==EMouseCursor::CardinalCross);
            Manager->NotifyMouseDown(Start,Canvas->GetCachedGeometry().GetLocalSize(),EKeys::LeftMouseButton);
            TestTrue(TEXT("Move cursor persists while dragging away from the handle"),Manager->GetWindowCursor(FVector2D(1,1),Canvas->GetCachedGeometry().GetLocalSize())==EMouseCursor::CardinalCross);
            Manager->NotifyMouseMove(Start+FVector2D(-20,12),Canvas->GetCachedGeometry().GetLocalSize());
            Manager->NotifyMouseUp(Start+FVector2D(-20,12),Canvas->GetCachedGeometry().GetLocalSize(),EKeys::LeftMouseButton);
            TestEqual(TEXT("Window follows the grabbed title or frame horizontally"),Panel->UserDragX,OldX-20);
            TestEqual(TEXT("Window follows the grabbed title or frame vertically"),Panel->UserDragY,OldY+12);
        }
        Manager->SetUiLocked(true); CaptureScreen(TEXT("RetailWindowLocked"));
        const auto DragTitle=Manager->FindElementByName(TEXT("InvTitleText"));
        const FVector2D TitlePoint=Canvas->LayoutToViewport(FVector2D(DragTitle->GetScreenOrigin())+FVector2D(80,12));
        const int32 LockedX=Panel->UserDragX;
        TestTrue(TEXT("Locked title does not advertise movement"),Manager->GetWindowCursor(TitlePoint,Canvas->GetCachedGeometry().GetLocalSize())==EMouseCursor::Default);
        Manager->NotifyMouseDown(TitlePoint,Canvas->GetCachedGeometry().GetLocalSize(),EKeys::LeftMouseButton);
        Manager->NotifyMouseMove(TitlePoint+FVector2D(50,0),Canvas->GetCachedGeometry().GetLocalSize());
        Manager->NotifyMouseUp(TitlePoint+FVector2D(50,0),Canvas->GetCachedGeometry().GetLocalSize(),EKeys::LeftMouseButton);
        TestEqual(TEXT("UI lock prevents title dragging"),Panel->UserDragX,LockedX);
        Panel->UserDragX=SavedPanelDrag.X; Panel->UserDragY=SavedPanelDrag.Y; Panel->RecomputeLayoutOffset(); Manager->SetUiLocked(WasLocked);

        // A server close while nearby must retire the panel and pending drag.
        Gameplay->ShowExternalContainer(700);
        Gameplay->InvDragSourcePack=700; Gameplay->InvDragGuid=703; Gameplay->bInvDragPending=true;
        Gameplay->HandleExternalContainerClosed(699);
        TestEqual(TEXT("Unrelated container close preserves current loot panel"),Gameplay->OpenLootContainerGuid,700);
        Gameplay->HandleExternalContainerClosed(700);
        TestEqual(TEXT("Server close immediately retires nearby loot panel"),Gameplay->OpenLootContainerGuid,0);
        TestFalse(TEXT("Server close cancels a drag of inaccessible loot"),Gameplay->bInvDragPending);
        Session.bUseBusy=false; Session.ShortcutObjects[0]=0; Session.WorldObjects.Remove(ShortcutUse.Guid);
        Gameplay->ShowExamination(true);
        Gameplay->HandleEscape();
        TestFalse(TEXT("Escape closes inspection first"),Manager->FindElementByName(TEXT("RootGameplay_FloatyExamination_Field"))->bVisible);
        TestEqual(TEXT("Closing inspection retains equipment panel"),Gameplay->ActivePanelPage,FString(TEXT("InventoryPanel_Field")));
        Gameplay->HandleEscape();
        TestTrue(TEXT("Next Escape closes equipment before opening options"),Gameplay->ActivePanelPage.IsEmpty());
        Gameplay->ActiveOptionsTab=TEXT("ConfigPage");
        Gameplay->HandleEscape();
        TestEqual(TEXT("Escape opens options on Game Play regardless of the last tab"),Gameplay->ActiveOptionsTab,FString(TEXT("GameplayOptionsPage")));
        TestEqual(TEXT("Escape reaches the game menu after dismissing panels"),Gameplay->ActivePanelPage,FString(TEXT("OptionsPanel_Field")));
        Gameplay->HandleEscape();
        TestTrue(TEXT("Escape toggles the game menu closed"),Gameplay->ActivePanelPage.IsEmpty());
        Gameplay->ShowPanelPage(TEXT("InventoryPanel_Field"));

        FACEWorldObject Partner; Partner.Guid=4567; Partner.Name=TEXT("Trade partner"); Session.WorldObjects.Add(Partner.Guid,Partner);
        Session.TradePartnerGuid=Partner.Guid;
        Session.TradeSelfItems={Device.Guid}; Session.TradePartnerItems={Item.Guid};
        Gameplay->ShowTradePanel(Partner.Guid); CaptureScreen(TEXT("GameplayTrade"));
        TestEqual(TEXT("Trade paints partner name"),Gameplay->TradeLabels[0]->GetText().ToString(),Partner.Name);
        TestTrue(TEXT("Trade paints item background"),Gameplay->TradeOtherBackgrounds[0]->GetVisibility()!=ESlateVisibility::Collapsed);
        TestTrue(TEXT("Trade paints all default blank slots"),Gameplay->TradeOtherBackgrounds.Last()->GetVisibility()!=ESlateVisibility::Collapsed);
        TestEqual(TEXT("Trade button has its label"),Gameplay->TradeLabels[4]->GetText().ToString(),FString(TEXT("Trade")));
        TestEqual(TEXT("Clear All button has its label"),Gameplay->TradeLabels[5]->GetText().ToString(),FString(TEXT("Clear All")));
		auto ClickOverlay=[&](UWidget* Widget, FKey Button)
		{
			const FVector2D Point=Widget->GetCachedGeometry().GetAbsolutePosition()+Widget->GetCachedGeometry().GetAbsoluteSize()*.5;
			Canvas->NativeOnMouseButtonDown(Canvas->GetCachedGeometry(),FPointerEvent(0,Point,Point,{Button},Button,0,FModifierKeysState()));
			Canvas->NativeOnMouseButtonUp(Canvas->GetCachedGeometry(),FPointerEvent(0,Point,Point,{},Button,0,FModifierKeysState()));
		};
		for (bool Self : {true,false})
		{
			Session.CachedC2SPackets.Reset();
			ClickOverlay(Self ? Gameplay->TradeSelfSlots[0] : Gameplay->TradeOtherSlots[0],EKeys::LeftMouseButton);
			TestEqual(TEXT("Trade item click selects the offered object"),Client->GetSelectedObject().Guid,Self ? Device.Guid : Item.Guid);
			TestTrue(TEXT("Selected trade item has the retail highlight"),(Self ? Gameplay->TradeSelfSelections[0] : Gameplay->TradeOtherSelections[0])->GetVisibility()!=ESlateVisibility::Collapsed);
			ClickOverlay(Self ? Gameplay->TradeSelfSlots[0] : Gameplay->TradeOtherSlots[0],EKeys::RightMouseButton);
			TestTrue(TEXT("Trade item right-click sends IdentifyObject"),HasAction(ACEGameAction::IdentifyObject));
		}
        Session.CachedC2SPackets.Reset(); NativeClick(TEXT("Trade_ClearAllButon"));
        TestTrue(TEXT("Authored Clear All button sends ResetTrade"),HasAction(ACEGameAction::ResetTrade));
        Session.CachedC2SPackets.Reset(); NativeClick(TEXT("TradeSelfTradeButton"));
        TestTrue(TEXT("Authored Trade button sends AcceptTrade"),HasAction(ACEGameAction::AcceptTrade));
        Session.TradeAcceptedByGuid=Player.Guid; Gameplay->RefreshTradeOverlays();
        Session.CachedC2SPackets.Reset(); NativeClick(TEXT("TradeSelfTradeButton"));
        TestTrue(TEXT("Accepted trade button cancels confirmation"),HasAction(ACEGameAction::DeclineTrade));
        Gameplay->HandleTradeStateChanged(ACEGameEvent::DeclineTrade);
        TestTrue(TEXT("Declining confirmation keeps offers open"),Gameplay->bTradeOpen);
        Session.TradeAcceptedByGuid=Partner.Guid; Gameplay->RefreshTradeOverlays();
        TestTrue(TEXT("Partner acceptance lights authored indicator"),Manager->FindElementByName(TEXT("TradeOtherTradeIndicator"))->bVisible);
        Session.TradeSelfItems.Init(Device.Guid,24); Gameplay->RefreshTradeOverlays();
        CaptureScreen(TEXT("GameplayTradeOverflow"));
        const auto TradeBar=Manager->FindElementByName(TEXT("TradeSelf_ItemListScroll"));
        const FVector2D TradeScrollPoint=FVector2D(TradeBar->GetScreenOrigin())+FVector2D(TradeBar->Width-24,8);
        TestTrue(TEXT("Trade overflow scrollbar responds to input"),Gameplay->TryBeginScrollbarDrag(TradeScrollPoint));
        Gameplay->TryFinishScrollbarDrag(); TestTrue(TEXT("Trade scrollbar changes offered-item viewport"),Gameplay->TradeSelfOffset>0);
        Session.CachedC2SPackets.Reset(); NativeClick(TEXT("CloseSecureTradeButton"));
        TestTrue(TEXT("Authored trade close sends CloseTrade"),HasAction(ACEGameAction::CloseTradeNegotiations));
        TestFalse(TEXT("Closing trade hides the panel"),Gameplay->bTradeOpen);
        TestTrue(TEXT("Closing trade also hides cached backgrounds"),Gameplay->TradeOtherBackgrounds[0]->GetVisibility()==ESlateVisibility::Collapsed);
        Device.ItemsCapacity=24; Device.ItemType=ACEItemType::Container;
        Session.WorldObjects.Add(Device.Guid,Device); Session.ShortcutObjects[1]=Device.Guid;
        Gameplay->RefreshInventoryOverlays(); CaptureScreen(TEXT("GameplayPackShortcut"));
        TestTrue(TEXT("Side-pack shortcut paints its bound number"),!Gameplay->PackShortcutIcons.IsEmpty()
            && Gameplay->PackShortcutIcons[0]->GetVisibility()!=ESlateVisibility::Collapsed);

        TestTrue(TEXT("Empty side pack hides capacity fill"),!Gameplay->PackCapacityMeters.IsEmpty() && Gameplay->PackCapacityMeters[0]->GetVisibility()==ESlateVisibility::Collapsed);
        FACEWorldObject Occupant; Occupant.Guid=40001; Occupant.Name=TEXT("Packed item"); Occupant.ContainerId=Device.Guid; Occupant.ItemType=ACEItemType::Misc;
        Occupant.PlacementPosition=0; Occupant.IconId=0x060010F9;
        for(int32 I=0;I<12;++I) { Occupant.Guid=40001+I; Occupant.PlacementPosition=I; Session.UpsertWorldObject(Occupant); }
        Gameplay->SelectedPackGuid=Device.Guid;
        Gameplay->RefreshInventoryOverlays(); CaptureScreen(TEXT("GameplayPackCapacity"));
        TestTrue(TEXT("Nonempty side pack displays capacity meter"),Gameplay->PackCapacityMeters[0]->GetVisibility()!=ESlateVisibility::Collapsed);
        TestTrue(TEXT("Capacity is occupied item slots over pack capacity"),FMath::IsNearlyEqual(Gameplay->PackCapacityMeters[0]->GetPercent(),.5f));
        TestTrue(TEXT("Open pack uses the authored open-container marker"),Gameplay->PackSlotSelected[1]->Background.GetResourceObject()==Resources->ResolveIconTexture(0x06005D9C));
		Session.CachedC2SPackets.Reset();
		ClickOverlay(Gameplay->PackSlots[1],EKeys::RightMouseButton);
		TestEqual(TEXT("Pack right-click selects the pack for examination"),Client->GetSelectedObject().Guid,Device.Guid);
		TestTrue(TEXT("Pack right-click sends IdentifyObject"),HasAction(ACEGameAction::IdentifyObject));
		FACEAppraisalInfo Inscribed; Inscribed.ObjectGuid=Device.Guid; Inscribed.Name=Device.Name;
		Inscribed.bSuccess=true; Inscribed.BoolProperties.Add(22,true);
		Gameplay->HandleAppraisal(Inscribed); CaptureScreen(TEXT("GameplayInscriptionPrompt"));
		TestTrue(TEXT("Owned pack exposes inscription editing"),Gameplay->CanEditInscription());
		NativeClick(TEXT("ItemInscriptionText"));
		TestEqual(TEXT("Clicking inscription paper opens its editor"),Gameplay->EditingInscriptionGuid,Device.Guid);
		if (Gameplay->ExamInscriptionEditor)
		{
			Session.CachedC2SPackets.Reset();
			const FString Inscription=TEXT("Supplies\nFor the next adventure.");
			Gameplay->ExamInscriptionEditor->SetText(FText::FromString(Inscription));
			Gameplay->ExamInscriptionEditor->OnTextCommitted.Broadcast(FText::FromString(Inscription),ETextCommit::OnUserMovedFocus);
			bool Written=false;
			for (const auto& Pair:Session.CachedC2SPackets)
			{
				FACEBinaryReader Wire(Pair.Value.Payload);Wire.Skip(16);
				if (Wire.ReadUInt32()!=ACEOpcode::GameAction) continue;
				Wire.ReadUInt32();if (Wire.ReadUInt32()!=0xBF) continue;
				Written=true;TestEqual(TEXT("Inscription packet identifies the pack"),Wire.ReadUInt32(),uint32(Device.Guid));
				TestEqual(TEXT("Inscription packet preserves multiline content"),Wire.ReadString16L(),Inscription);
			}
			TestTrue(TEXT("Editing emits the retail writing action"),Written);
			Gameplay->BeginInscriptionEdit();Session.CachedC2SPackets.Reset();
			Gameplay->HandleInscriptionCommitted(FText::FromString(TEXT("discard")),ETextCommit::OnCleared);
			TestFalse(TEXT("Cancelling inscription edits sends nothing"),HasAction(0xBF));
			Gameplay->BeginInscriptionEdit();
			Gameplay->HandleInscriptionCommitted(FText::GetEmpty(),ETextCommit::OnUserMovedFocus);
			TestTrue(TEXT("An inscription can be erased"),Gameplay->LastAppraisal.Inscription.IsEmpty() && HasAction(0xBF));
		}
		Inscribed.StringProperties.Add(8,TEXT("Someone else"));Gameplay->HandleAppraisal(Inscribed);
		TestFalse(TEXT("Another player's inscription stays read-only"),Gameplay->CanEditInscription());
		Gameplay->ShowExamination(false);CaptureScreen(TEXT("GameplayPackAfterID"));
        TestEqual(TEXT("Paperdoll checkbox uses retail Slots label"),Gameplay->PaperdollSlotsCheckboxLabel->GetText().ToString(),FString(TEXT("Slots")));
        Session.PlayerVitals.AetheriaUnlocked=0; Gameplay->RefreshInventoryOverlays();
        for(const auto& SlotName:{TEXT("Inv_SigilOneSlot"),TEXT("Inv_SigilTwoSlot"),TEXT("Inv_SigilThreeSlot")})
        {
            const auto E=Manager->FindElementByName(SlotName);
            TestTrue(TEXT("Aetheria slot resolves"),E.IsValid());
            if(E) TestFalse(TEXT("Locked aetheria slot is hidden"),E->bVisible);
        }
        FACEBinaryWriter Unlock; Unlock.WriteUInt8(1); Unlock.WriteUInt32(322); Unlock.WriteInt32(5);
        FACEBinaryReader UnlockReader(Unlock.GetData()); Session.HandlePrivateUpdatePropertyInt(UnlockReader);
        Gameplay->RefreshInventoryOverlays();
        TestTrue(TEXT("Network bitfield unlocks blue and red only"),Manager->FindElementByName(TEXT("Inv_SigilOneSlot"))->bVisible
            && !Manager->FindElementByName(TEXT("Inv_SigilTwoSlot"))->bVisible && Manager->FindElementByName(TEXT("Inv_SigilThreeSlot"))->bVisible);
        for(int32 I=0;I<12;++I) Session.WorldObjects.Remove(40001+I);
        Gameplay->SelectedPackGuid=Player.Guid;
        Gameplay->RefreshInventoryOverlays();
        Gameplay->SetJumpChargeFraction(.25f); CaptureScreen(TEXT("GameplayJumpCharge"));
        const auto JumpRoot=Manager->FindElementByName(TEXT("RootGameplay_PowerBar_Field"));
        const auto JumpMeter=Manager->FindElementUnder(TEXT("RootGameplay_PowerBar_Field"),TEXT("Powerbar"));
        TestTrue(TEXT("Jump uses the authored PBM_JUMP display state"),JumpRoot && JumpRoot->PaintState==0x10000042);
        TestTrue(TEXT("Jump meter tracks charge"),JumpMeter && FMath::IsNearlyEqual(JumpMeter->MeterFillFraction,.25f));
        TestTrue(TEXT("Jump bar honors the DAT maximum width"),JumpRoot && JumpRoot->Width<=810);
        TestTrue(TEXT("Jump bar includes the Height label"),Gameplay->JumpChargeLabel && Gameplay->JumpChargeLabel->GetText().ToString()==TEXT("Height"));
        Gameplay->SetJumpChargeFraction(0);
        ScreenSize=FIntPoint(2560,1354); CaptureScreen(TEXT("GameplayLargeInventory"));
        auto DragWidgetToShortcut = [&](UWidget* Source, int32 Guid, int32 Destination)
        {
            const auto Target = Manager->FindElementByName(FString::Printf(TEXT("ShortcutBar_Shortcut%dButton"),Destination+1));
            const FVector2D From=Source->GetCachedGeometry().GetAbsolutePosition()+Source->GetCachedGeometry().GetAbsoluteSize()*0.5;
            const FVector2D To=Canvas->GetCachedGeometry().LocalToAbsolute(
                (FVector2D(Target->GetScreenOrigin())+FVector2D(16,16))*Canvas->GetLastScale2D());
            Session.CachedC2SPackets.Reset();
            Canvas->NativeOnMouseButtonDown(Canvas->GetCachedGeometry(),FPointerEvent(0,From,From,{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState()));
            TestEqual(TEXT("Native backpack/item press arms the correct drag"),Gameplay->InvDragGuid,Guid);
            Canvas->NativeOnMouseMove(Canvas->GetCachedGeometry(),FPointerEvent(0,To,From,{EKeys::LeftMouseButton},EKeys::Invalid,0,FModifierKeysState()));
            TestTrue(TEXT("Native backpack/item motion becomes a drag"),Gameplay->bInvDragActive);
            CaptureScreen(TEXT("GameplayDraggingItem")); // Tick/layout may refresh between press and release.
            Canvas->NativeOnMouseButtonUp(Canvas->GetCachedGeometry(),FPointerEvent(0,To,To,{},EKeys::LeftMouseButton,0,FModifierKeysState()));
            TestTrue(TEXT("Native backpack/item drop sends AddShortCut"),HasAction(ACEGameAction::AddShortCut));
            TestEqual(TEXT("Native backpack/item drop immediately updates binding"),Session.GetShortcutObject(Destination),Guid);
        };
        if (Gameplay->PackSlots.Num()>1) DragWidgetToShortcut(Gameplay->PackSlots[1],Device.Guid,4);
        TestEqual(TEXT("Moving backpack shortcut removes the old binding"),Session.GetShortcutObject(1),0);
        Item.WielderId=Item.ParentGuid=0; Item.ContainerId=Device.Guid; Item.CurrentWieldedLocation=0; Item.PlacementPosition=0;
        Session.WorldObjects.Add(Item.Guid,Item); Gameplay->SelectedPackGuid=Device.Guid;
        Gameplay->RefreshInventoryOverlays(); CaptureScreen(TEXT("GameplayBackpackItemDrag"));
        if (!Gameplay->InventorySlots.IsEmpty()) DragWidgetToShortcut(Gameplay->InventorySlots[0],Item.Guid,5);
        Gameplay->RefreshInventoryOverlays(); CaptureScreen(TEXT("GameplayBoundShortcut"));
        TestTrue(TEXT("Occupied hotbar slot paints the same numeral as inventory"),Gameplay->ShortcutNumberIcons.Num()>5
            && Gameplay->ShortcutNumberIcons[5]->Background.GetResourceObject()==Resources->ResolveIconTexture(0x060010A3));
        TestEqual(TEXT("Moving item shortcut removes the previous inventory badge"),Session.GetShortcutObject(2),0);
        TestTrue(TEXT("Inventory badge follows item to sixth slot"),Gameplay->InventorySlotOverlays[0]->Background.GetResourceObject()==Resources->ResolveIconTexture(0x060010A3));
        if (Gameplay->PackSlots.Num()>1) DragWidgetToShortcut(Gameplay->PackSlots[1],Device.Guid,5);
        TestEqual(TEXT("Occupied shortcut rehomes the displaced item to the right"),Session.GetShortcutObject(6),Item.Guid);
        TestEqual(TEXT("Occupied shortcut removes backpack's prior binding"),Session.GetShortcutObject(4),0);
        CaptureScreen(TEXT("GameplayMainPackDrag"));
        DragWidgetToShortcut(Gameplay->PackSlots[0],Player.Guid,7);
        TestTrue(TEXT("Main backpack also carries its shortcut numeral"),Gameplay->MainPackShortcutIcon
            && Gameplay->MainPackShortcutIcon->Background.GetResourceObject()==Resources->ResolveIconTexture(0x060010A5));
        TestTrue(TEXT("Main backpack shortcut uses bag art instead of the player's creature icon"),
            Gameplay->ShortcutIcons[7]->Background.GetResourceObject()==Resources->ResolveItemForeground(0x06004CF7,0,0));
        Gameplay->SelectedPackGuid=Device.Guid; Session.CachedC2SPackets.Reset(); Gameplay->UseShortcutSlot(8);
        TestEqual(TEXT("Main backpack shortcut opens the player's inventory"),Gameplay->SelectedPackGuid,Player.Guid);
        TestFalse(TEXT("Main backpack shortcut never sends Use on the player"),HasAction(ACEGameAction::Use));
        // gmToolbarUI::UseShortcut: using a kit and acquiring self are not
        // selection changes. Exercise keyboard activation and native pointer
        // press/release on the backpack (the latter also arms inventory drags).
        {
            const auto SavedSelection=Session.SelectedObject;
            const auto SavedShortcuts=Session.ShortcutObjects;
            const int32 SavedCombatMode=Gameplay->CombatMode;
            FACEWorldObject Kit; Kit.Guid=98801; Kit.Name=TEXT("Healing Kit");
            Kit.ContainerId=Player.Guid; Kit.ItemType=ACEItemType::Misc;
            Kit.ItemUseable=0x400008; // Contained source, creature target.
            FACEWorldObject Monster; Monster.Guid=98802; Monster.Name=TEXT("Combat target");
            Monster.ItemType=ACEItemType::Creature; Monster.ObjectDescriptionFlags=ACEObjectDescFlag::Attackable;
            Monster.Position=Player.Position;
            Session.WorldObjects.Add(Kit.Guid,Kit); Session.WorldObjects.Add(Monster.Guid,Monster);
            Session.ShortcutObjects[8]=Kit.Guid; Session.ShortcutObjects[7]=Player.Guid;
            for (int32 Mode : {int32(ACECombatMode::Melee),int32(ACECombatMode::Magic)})
            for (int32 Input=0; Input<3; ++Input)
            {
                Gameplay->CombatMode=Mode; Session.bUseBusy=false;
                Gameplay->CancelPendingUseWith(); Session.CachedC2SPackets.Reset();
                Gameplay->LastInvClickGuid=0; Gameplay->LastInvClickTime=0;
                Session.SelectedObject.bValid=true; Session.SelectedObject.Guid=Monster.Guid;
                const auto Serial=Session.SelectedObject.SelectionSerial;
                if (Input==0) Gameplay->UseShortcutSlot(9);
                else if (Input==1)
                {
                    NativeClick(TEXT("ShortcutBar_Shortcut9Button"));
                    TestEqual(TEXT("First kit click keeps the monster selected"),Client->GetSelectedObject().Guid,Monster.Guid);
                    NativeClick(TEXT("ShortcutBar_Shortcut9Button"));
                }
                else
                {
                    Gameplay->HandleNamedClick(TEXT("ShortcutBar_Shortcut9Button"));
                    Gameplay->HandleNamedClick(TEXT("ShortcutBar_Shortcut9Button"));
                }
                TestEqual(TEXT("Kit activation retains combat target"),Client->GetSelectedObject().Guid,Monster.Guid);
                TestEqual(TEXT("Kit arms targeted use"),Gameplay->PendingUseWithSourceGuid,Kit.Guid);
                if (Input==0) Gameplay->UseShortcutSlot(8);
                else if (Input==1) NativeClick(TEXT("ShortcutBar_Shortcut8Button"));
                else Gameplay->HandleNamedClick(TEXT("ShortcutBar_Shortcut8Button"));
                TestEqual(TEXT("Self healing retains combat target"),Client->GetSelectedObject().Guid,Monster.Guid);
                TestEqual(TEXT("No transient selection change during self healing"),Session.SelectedObject.SelectionSerial,Serial);
                TestEqual(TEXT("Self healing does not change stance"),Gameplay->CombatMode,Mode);
                TestEqual(TEXT("Self healing retires use cursor"),Gameplay->PendingUseWithSourceGuid,0);
                TestTrue(TEXT("Self healing emits UseWithTarget"),HasAction(ACEGameAction::UseWithTarget));
                TestFalse(TEXT("Self healing never sends Use on the backpack/player"),HasAction(ACEGameAction::Use));
                LastWire(ACEGameAction::UseWithTarget,{uint32(Kit.Guid),uint32(Player.Guid)});
            }
            Session.bUseBusy=false; Session.ShortcutObjects=SavedShortcuts;
            Session.WorldObjects.Remove(Kit.Guid); Session.WorldObjects.Remove(Monster.Guid);
            Session.SelectedObject=SavedSelection; Gameplay->CombatMode=SavedCombatMode;
        }
        // Native inventory drags and server acknowledgements exercise the cached UI.
        const auto SavedObjects=Session.WorldObjects;
        const auto SavedContents=Session.ContainerContents;
        Session.WorldObjects.Reset(); Session.ContainerContents.Reset(); Session.WorldObjects.Add(Player.Guid,Player);
        for(int32 I=0;I<3;++I)
        {
            FACEWorldObject O; O.Guid=60001+I; O.ContainerId=Player.Guid; O.PlacementPosition=I;
            O.ItemType=ACEItemType::Misc; O.IconId=0x060010F9; O.Name=FString::Printf(TEXT("Item %d"),I+1); Session.UpsertWorldObject(O);
            O.Guid=61001+I; O.ItemType=ACEItemType::Container; O.ItemsCapacity=24;
            O.IconId=0x06004CF7; O.Name=FString::Printf(TEXT("Pack %d"),I+1); Session.UpsertWorldObject(O);
        }
        Gameplay->SelectedPackGuid=Player.Guid; Gameplay->RefreshInventoryOverlays(); CaptureScreen(TEXT("GameplayReorderBefore"));
        TestTrue(TEXT("Main backpack capacity excludes side packs"),FMath::IsNearlyEqual(Gameplay->MainPackCapacityMeter->GetPercent(),3.f/102.f));
        TestTrue(TEXT("Main pack uses the container background rather than the player's item type"),
            Gameplay->PackSlotBgs[0]->Background.GetResourceObject()==Resources->ResolveItemBackground(ACEItemType::Container,0));
        int32 EmptyPacks=0;
        for (int32 I=1;I<Gameplay->PackSlots.Num();++I)
        {
            if (Gameplay->PackSlots[I]->GetVisibility()==ESlateVisibility::Collapsed) continue;
            if (Gameplay->PackSlotGuids[I])
            {
                TestTrue(TEXT("Occupied pack paints its retail container background"),
                    Gameplay->PackSlotBgs[I]->GetVisibility()!=ESlateVisibility::Collapsed
                    && Gameplay->PackSlotBgs[I]->Background.GetResourceObject()==Resources->ResolveItemBackground(ACEItemType::Container,0));
                continue;
            }
            ++EmptyPacks;
            // Independent DAT: 21000037 / ItemSlot_Backpack / ItemSlot_Icon / ItemSlot_Empty.
            TestTrue(TEXT("Empty pack uses the authored 06000F6E image at full opacity"),
                Gameplay->PackSlots[I]->Background.GetResourceObject()==Resources->ResolveIconTexture(0x06000F6E)
                && Gameplay->PackSlots[I]->GetRenderOpacity()==1.f);
            TestTrue(TEXT("Empty pack cannot retain a previous occupant's background"),
                Gameplay->PackSlotBgs[I]->GetVisibility()==ESlateVisibility::Collapsed);
        }
        TestEqual(TEXT("Three packs leave four visible placeholders in the seven default slots"),EmptyPacks,4);
        {
            const FACEWorldObject Original=Session.WorldObjects[61003];
            FACEWorldObject Modified=Original; Modified.ItemType=ACEItemType::Misc; Modified.IconUnderlayId=0x060010F9;
            Session.UpsertWorldObject(Modified); Gameplay->RefreshInventoryOverlays();
            TestTrue(TEXT("Pack type and custom underlay changes invalidate the cached background"),
                Gameplay->PackSlotBgs[3]->Background.GetResourceObject()==Resources->ResolveItemBackground(Modified.ItemType,Modified.IconUnderlayId));
            Session.WorldObjects.Remove(Modified.Guid); Gameplay->RefreshInventoryOverlays();
            TestEqual(TEXT("Removing a pack clears its slot's guid"),Gameplay->PackSlotGuids[3],0);
            TestTrue(TEXT("Removing a pack restores the empty artwork and hides the old underlay"),
                Gameplay->PackSlots[3]->Background.GetResourceObject()==Resources->ResolveIconTexture(0x06000F6E)
                && Gameplay->PackSlotBgs[3]->GetVisibility()==ESlateVisibility::Collapsed);
            Session.UpsertWorldObject(Original); Gameplay->RefreshInventoryOverlays();
            Session.WorldObjects[Player.Guid].ContainersCapacity=24;
            Gameplay->PackScrollOffset=24; Gameplay->RefreshInventoryOverlays();
            TestTrue(TEXT("Scrolling augmented pack slots replaces the occupied artwork with placeholders"),
                Gameplay->PackScrollOffset>0 && Gameplay->PackSlotGuids[1]==0
                && Gameplay->PackSlots[1]->Background.GetResourceObject()==Resources->ResolveIconTexture(0x06000F6E)
                && Gameplay->PackSlotBgs[1]->GetVisibility()==ESlateVisibility::Collapsed);
            CaptureScreen(TEXT("GameplayEmptyPackScroll"));
            Session.WorldObjects[Player.Guid].ContainersCapacity=Player.ContainersCapacity;
            Gameplay->PackScrollOffset=0; Gameplay->RefreshInventoryOverlays();
            Gameplay->ShowPanelPage(TEXT("SkillManagementPanel_Field"));
            TestTrue(TEXT("Closing inventory hides pack background overlays"),Gameplay->PackSlotBgs[0]->GetVisibility()==ESlateVisibility::Collapsed);
            Gameplay->ShowPanelPage(TEXT("InventoryPanel_Field"));
            CaptureScreen(TEXT("GameplayPackBackgrounds"));
            TestTrue(TEXT("Reopening inventory restores the occupied pack background"),Gameplay->PackSlotBgs[3]->GetVisibility()!=ESlateVisibility::Collapsed);
        }
        auto DragInventoryWidget=[&](UWidget* Source,UWidget* Target,int32 Guid,int32 ExpectedPosition)
        {
            const auto& Geo=Canvas->GetCachedGeometry();
            const auto From=Source->GetCachedGeometry().LocalToAbsolute(Source->GetCachedGeometry().GetLocalSize()*.5);
            const auto To=Target->GetCachedGeometry().LocalToAbsolute(Target->GetCachedGeometry().GetLocalSize()*.5);
            Session.CachedC2SPackets.Reset();
            Canvas->NativeOnMouseButtonDown(Geo,FPointerEvent(0,From,From,{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState()));
            TestEqual(TEXT("Native reorder press selects expected item"),Gameplay->InvDragGuid,Guid);
            Canvas->NativeOnMouseMove(Geo,FPointerEvent(0,To,From,{EKeys::LeftMouseButton},EKeys::Invalid,0,FModifierKeysState()));
            Canvas->NativeOnMouseButtonUp(Geo,FPointerEvent(0,To,To,{},EKeys::LeftMouseButton,0,FModifierKeysState()));
            bool Moved=false;
            for(const auto& P:Session.CachedC2SPackets)
            {
                FACEBinaryReader Wire(P.Value.Payload); Wire.Skip(16);
                if(Wire.ReadUInt32()!=ACEOpcode::GameAction)continue;
                Wire.ReadUInt32(); if(Wire.ReadUInt32()!=ACEGameAction::PutItemInContainer)continue;
                const auto Id=Wire.ReadUInt32(),Container=Wire.ReadUInt32(); const int32 Position=Wire.ReadInt32();
                Moved |= Id==uint32(Guid) && Container==uint32(Player.Guid) && Position==ExpectedPosition;
            }
            TestTrue(TEXT("Native reorder sends authoritative container and insertion index"),Moved);
        };
        auto AcknowledgeReorder=[&](int32 Guid,int32 Position,bool Pack)
        {
            FACEBinaryWriter Prop; Prop.WriteUInt8(1); Prop.WriteUInt32(Guid); Prop.WriteUInt32(2); Prop.WriteUInt32(Player.Guid);
            FACEBinaryReader PR(Prop.GetData()); Session.HandlePublicUpdateInstanceId(PR);
            FACEBinaryWriter Ack; Ack.WriteUInt32(Guid); Ack.WriteUInt32(Player.Guid); Ack.WriteInt32(Position); Ack.WriteInt32(Pack ? 1 : 0);
            FACEBinaryReader AR(Ack.GetData()); Session.HandleInventoryPutObjInContainer(AR);
            Gameplay->RefreshInventoryOverlays(); // no forced cache invalidation
        };
        DragInventoryWidget(Gameplay->InventorySlots[2],Gameplay->InventorySlots[0],60003,0);
        AcknowledgeReorder(60003,0,false);
        TestEqual(TEXT("Item acknowledgement updates visible inventory order"),Gameplay->InventorySlotGuids[0],60003);
        CaptureScreen(TEXT("GameplayReorderItems"));
        DragInventoryWidget(Gameplay->PackSlots[3],Gameplay->PackSlots[1],61003,0);
        AcknowledgeReorder(61003,0,true);
        TestEqual(TEXT("Pack acknowledgement invalidates the inventory cache"),Gameplay->PackSlotGuids[1],61003);
        CaptureScreen(TEXT("GameplayReorderPacks"));
        // Forward insertion is adjusted for removing the source first, as in UIElement_ItemList.
        DragInventoryWidget(Gameplay->PackSlots[1],Gameplay->PackSlots[3],61003,1);
        AcknowledgeReorder(61003,1,true);
        TestEqual(TEXT("Forward pack move has retail insertion order"),Gameplay->PackSlotGuids[2],61003);
        {
            FACEWorldObject Chest; Chest.Guid=62000; Chest.ItemType=ACEItemType::Container;
            Chest.ItemsCapacity=24; Chest.IconId=0x06004CF7; Chest.Name=TEXT("Loot background fixture");
            Session.UpsertWorldObject(Chest);
            FACEWorldObject NestedPack=Chest; NestedPack.Guid=62001; NestedPack.ContainerId=Chest.Guid;
            Session.UpsertWorldObject(NestedPack);
            Gameplay->ShowExternalContainer(Chest.Guid); CaptureScreen(TEXT("GameplayLootPackBackground"));
            TestTrue(TEXT("Packs in an external container retain their own retail background"),
                !Gameplay->ExtPackSlotBgs.IsEmpty() && Gameplay->ExtPackSlotBgs[0]->GetVisibility()!=ESlateVisibility::Collapsed
                && Gameplay->ExtPackSlotBgs[0]->Background.GetResourceObject()==Resources->ResolveItemBackground(ACEItemType::Container,0));
            Gameplay->OpenLootSelectedPackGuid=NestedPack.Guid;
            Gameplay->RefreshExternalContainerOverlays();
            NestedPack.IconUnderlayId=0x060010F9; Session.UpsertWorldObject(NestedPack);
            Gameplay->RefreshExternalContainerOverlays();
            TestTrue(TEXT("Nested pack underlay updates even while viewing its contents"),
                !Gameplay->ExtPackSlotBgs.IsEmpty()
                && Gameplay->ExtPackSlotBgs[0]->Background.GetResourceObject()==Resources->ResolveItemBackground(ACEItemType::Container,NestedPack.IconUnderlayId));
            Gameplay->HideExternalContainer(false);
            TestTrue(TEXT("Closing loot hides its pack background"),
                Gameplay->ExtPackSlotBgs.IsEmpty() || Gameplay->ExtPackSlotBgs[0]->GetVisibility()==ESlateVisibility::Collapsed);
        }
        Session.WorldObjects=SavedObjects; Session.ContainerContents=SavedContents;
        Gameplay->SelectedPackGuid=Player.Guid; Gameplay->RefreshInventoryOverlays();

        Session.SocketC2S->Close(); Sockets->DestroySocket(Session.SocketC2S); Session.SocketC2S=nullptr;
    }
    else AddError(TEXT("Could not bind inventory loopback fixture"));
    if (Receiver) { Receiver->Close(); Sockets->DestroySocket(Receiver); }

    // Exercise actual software cursor geometry and the inventory's live capture rig.
    {
        const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false)
            .RequiresHitProxies(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
        auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        auto* PreviewGI=NewObject<UGameInstance>(GEngine); World->SetGameInstance(PreviewGI); PreviewGI->Init();
        auto* PreviewDat=PreviewGI->GetSubsystem<UACEDatSubsystem>();
        TestTrue(TEXT("Preview world uses the real DAT subsystem"),PreviewDat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call")));
        auto* Controller=World->SpawnActor<AACEPlayerController>();
        Controller->Client=Client; Gameplay->PlayerController=Controller;
        {
            auto& Session=*Client->Session;
            const auto SavedObjects=Session.WorldObjects; const auto SavedSelection=Session.SelectedObject;
            const auto SavedFellowship=Session.Fellowship; const auto SavedPosition=Session.PlayerPosition;
            Session.WorldObjects.Reset();
            FACEPosition Position;Position.CellId=0xDA55001D;Position.Location=FVector(100,100,20);Session.PlayerPosition=Position;
            FACEWorldObject Self;Self.Guid=Session.PlayerGuid;Self.Name=TEXT("Self");Self.ItemType=ACEItemType::Creature;Self.bIsPlayer=true;Self.bHasPosition=true;Self.Position=Position;
            Session.WorldObjects.Add(Self.Guid,Self);
            auto AddObject=[&](int32 Guid,float Distance,bool Player)
            {
                auto Object=Self;Object.Guid=Guid;Object.Position.Location.X+=Distance;Object.bIsPlayer=Player;
                Session.WorldObjects.Add(Guid,Object);
            };
            AddObject(80001,10,true);AddObject(80002,20,true);AddObject(80003,5,false);AddObject(80004,80,true);
            Gameplay->CycleKeyboardSelection(TEXT("Player"),0);
            TestEqual(TEXT("Closest player excludes nearer NPC and self"),Client->GetSelectedObject().Guid,80001);
            Gameplay->CycleKeyboardSelection(TEXT("Player"),1);
            TestEqual(TEXT("Next player follows retail distance ordering"),Client->GetSelectedObject().Guid,80002);
            Gameplay->CycleKeyboardSelection(TEXT("Player"),1);
            TestEqual(TEXT("Next player wraps and excludes distant objects"),Client->GetSelectedObject().Guid,80001);
            Gameplay->CycleKeyboardSelection(TEXT("CompassItem"),0);
            TestEqual(TEXT("Radar selection can choose the nearer NPC"),Client->GetSelectedObject().Guid,80003);
            Session.Fellowship.Members.Reset();
            for(int32 Guid:{80002,Self.Guid,80001}){FACEFellowshipMember Member;Member.Guid=Guid;Session.Fellowship.Members.Add(Member);}
            Client->SelectObject(80002);Gameplay->CycleKeyboardSelection(TEXT("Fellow"),1);
            TestEqual(TEXT("Fellowship selection follows member order and includes self"),Client->GetSelectedObject().Guid,Self.Guid);
            FACEWorldObject Stack;Stack.Guid=81000;Stack.Name=TEXT("Stack");Stack.ItemType=ACEItemType::MissileWeapon;Stack.ContainerId=Self.Guid;Stack.StackSize=50;Stack.MaxStackSize=100;
            Session.WorldObjects.Add(Stack.Guid,Stack);Client->SelectObject(Stack.Guid);Gameplay->HandleSelectionChanged(Client->GetSelectedObject());
            Gameplay->ShowPanelPage(TEXT("InventoryPanel_Field"));Gameplay->RefreshSelectionOverlay();
            TestTrue(TEXT("Selected stacks expose an editable quantity"),Gameplay->StackAmountEntry && Gameplay->StackAmountEntry->IsVisible());
            Gameplay->HandleStackAmountCommitted(FText::FromString(TEXT("999")),ETextCommit::OnEnter);
            TestEqual(TEXT("Stack quantity cannot exceed the actual stack"),Gameplay->SelectedStackAmount,50);
            Gameplay->HandleStackAmountCommitted(FText::FromString(TEXT("7")),ETextCommit::OnEnter);
            TestEqual(TEXT("Retail split-stack quantity is retained for transfer"),Gameplay->SelectedStackAmount,7);
            CaptureScreen(TEXT("GameplayKeyboardStackQuantity"));
            ACEInputBindings::Reload();ACEInputBindings::BeginEdit();ACEInputBindings::Defaults();
            ACEInputBindings::Set(ACEInputBindings::Action(TEXT("ToggleFriendsPanel")),0,FInputChord(EKeys::F3));ACEInputBindings::Commit();
            Controller->PlayerInput=NewObject<UPlayerInput>(Controller);
            auto Press=[&](FKey Key)
            {
                Controller->PlayerInput->FlushPressedKeys();
                Controller->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(Key,IE_Pressed,1.f));
                Controller->PlayerInput->ProcessInputStack({},.016f,false);
                Gameplay->PollAdditionalKeyboardActions(Controller);
            };
            Gameplay->ShowPanelPage(TEXT("SocialPanel_Field"));Gameplay->SyncSocialPanelTab(TEXT("AllegiancePage"));
            Press(EKeys::F3);
            TestEqual(TEXT("Friends shortcut changes social subtab instead of closing the panel"),Gameplay->ActiveSocialTab,FString(TEXT("FriendsPage")));
            TestEqual(TEXT("Friends shortcut keeps the social panel open"),Gameplay->ActivePanelPage,FString(TEXT("SocialPanel_Field")));
            Controller->PlayerInput->FlushPressedKeys();ACEInputBindings::Reload();
            Session.WorldObjects=SavedObjects;Session.SelectedObject=SavedSelection;Session.Fellowship=SavedFellowship;Session.PlayerPosition=SavedPosition;
            Gameplay->HandleSelectionChanged(SavedSelection);Gameplay->RefreshSelectionOverlay();
        }
		{
			TGuardValue<TObjectPtr<UACEUIElementManager>> KeepManager(Client->UIElementManager,Manager);
			TGuardValue<TObjectPtr<UACEUILayoutResolver>> KeepLayout(Client->UILayoutResolver,Layout);
			TGuardValue<TObjectPtr<UACEUIResourceResolver>> KeepResources(Client->UIResourceResolver,Resources);
			Controller->DatCanvasWidget=Canvas;Controller->DatGameplayBinder=Gameplay;
			Gameplay->ShowPanelPage(TEXT("InventoryPanel_Field"));
			const auto Frame=Manager->FindElementByName(TEXT("InventoryPanel_Field"));
			Controller->bGameplayUiAssetsReady=false;
			TestFalse(TEXT("Portal prefetch yields after building the texture queue"),Controller->TryPrefetchGameplayHudAssets());
			for (int32 Tick=0; Tick<1000 && !Controller->bGameplayUiAssetsReady; ++Tick)
				Controller->TryPrefetchGameplayHudAssets();
			TestTrue(TEXT("Portal prefetch resolves existing HUD resources across ticks"),Controller->bGameplayUiAssetsReady);
			TestTrue(TEXT("Portal prefetch preserves inventory frame identity and visibility"),Manager->FindElementByName(TEXT("InventoryPanel_Field"))==Frame && Frame->bVisible);
			Controller->DatCanvasWidget=nullptr;Controller->DatGameplayBinder=nullptr;
		}
        FACEWorldObject Clicked;Clicked.Guid=456;Clicked.SetupId=0x02000001;Clicked.ItemType=ACEItemType::Creature;
        auto* ClickActor=World->SpawnActor<AACEWorldEntityActor>();ClickActor->InitializeFromObject(Clicked,100,true);
        FACESelectedObject FlashSelection;FlashSelection.Guid=456;FlashSelection.bValid=true;FlashSelection.SelectionSerial=10;
        Client->Session->SelectedObject=FlashSelection;
        Gameplay->HandleSelectionChanged(FlashSelection);
        TestTrue(TEXT("Selecting an actual creature creates brightness flash materials"),!Gameplay->FlashMaterials.IsEmpty());
        if (!Gameplay->FlashMaterials.IsEmpty())
        {
            auto* Flash=Gameplay->FlashMaterials[0].Get();
            TestFalse(TEXT("Selection flash has a renderer-compatible parent"),Flash->Parent->IsA<UMaterialInstanceDynamic>());
            UTexture* SourceTexture=nullptr;Gameplay->FlashOriginalMaterials[0]->GetTextureParameterValue(TEXT("Texture"),SourceTexture);
            TestTrue(TEXT("Selection flash preserves the character palette texture"),Flash->K2_GetTextureParameterValue(TEXT("Texture"))==SourceTexture);
        }
        const double FlashDeadline=Gameplay->SelectionFlashUntil;
        FlashSelection.HealthFraction=.5f;Client->Session->SelectedObject=FlashSelection;Gameplay->HandleSelectionChanged(FlashSelection);
        TestEqual(TEXT("Health updates do not retrigger the click brightness flash"),Gameplay->SelectionFlashUntil,FlashDeadline);
        Gameplay->SelectionFlashUntil=0;
        ++FlashSelection.SelectionSerial;Client->Session->SelectedObject=FlashSelection;Gameplay->HandleSelectionChanged(FlashSelection);
        TestTrue(TEXT("Clicking the same target again starts a fresh flash"),Gameplay->SelectionFlashUntil>0);
        Gameplay->SelectionFlashUntil=0;Gameplay->TickSelectionFlash();ClickActor->Destroy();
        Client->Session->Contracts.Reset();
        for (int32 Id=1;Id<1000 && Client->Session->Contracts.Num()<2;++Id)
        {
            if (!PreviewDat->GetContractInfo(Id)) continue;
            FACEContractEntry Entry;Entry.ContractId=Id;Entry.Stage=2;
            Entry.TimeWhenDone=3600;Entry.ReceivedAt=FPlatformTime::Seconds()-60;
            Client->Session->Contracts.Add(Entry);
        }
        Gameplay->ShowPanelPage(TEXT("QuestManagementPanel_Field"));
        Gameplay->RefreshQuestOverlays();CaptureScreen(TEXT("GameplayContracts"));
        TestFalse(TEXT("Personal notes do not overlap the contracts page"),bool(Manager->FindElementByName(TEXT("JournalPage"))->bVisible));
        TestEqual(TEXT("Journal displays received contracts"),Client->Session->Contracts.Num(),2);
        if (Gameplay->QuestRows.Num()>1)
        {
            TestEqual(TEXT("Journal uses the real contract name"),Gameplay->QuestRows[0]->GetText().ToString(),
                PreviewDat->GetContractInfo(Gameplay->QuestRowIds[0])->Name);
            TestEqual(TEXT("Journal text lets the list receive clicks"),Gameplay->QuestRows[0]->GetVisibility(),ESlateVisibility::HitTestInvisible);
            TestEqual(TEXT("Journal shows the server stage in the status column"),Gameplay->QuestStatusRows[0]->GetText().ToString(),FString(TEXT("In progress")));
            TestTrue(TEXT("Journal countdown advances after the server snapshot"),Gameplay->QuestDetailLabels.ContainsByPredicate([](UTextBlock* Label)
                {return Label && Label->GetText().ToString().Contains(TEXT("00:59:"));}));
            const auto& RowGeometry=Gameplay->QuestStatusRows[1]->GetCachedGeometry();
            const FVector2D RowPoint=Canvas->GetCachedGeometry().AbsoluteToLocal(RowGeometry.LocalToAbsolute(RowGeometry.GetLocalSize()*.5));
            TestTrue(TEXT("Clicking journal status selects its contract"),Gameplay->TryHandleOverlayClick(RowPoint));
            TestEqual(TEXT("Journal detail follows the clicked second row"),Gameplay->SelectedContractId,Gameplay->QuestRowIds[1]);
        }
        Gameplay->JournalStorageRoot=FPaths::ProjectSavedDir()/TEXT("Automation/JournalFixture");
        Gameplay->HandleNamedClick(TEXT("JournalTab")); CaptureScreen(TEXT("GameplayJournal"));
        TestEqual(TEXT("Journal tab remains selected across refresh"),Gameplay->ActiveQuestTab,FString(TEXT("JournalPage")));
        TestTrue(TEXT("All three quest tab captions are visible"),Gameplay->QuestTabLabels.Num()==3 && Gameplay->QuestTabLabels[2]->GetVisibility()!=ESlateVisibility::Collapsed);
        Gameplay->JournalEntries[1]->SetText(FText::FromString(TEXT("Roof survey")));
        Gameplay->JournalNotes->SetText(FText::FromString(TEXT("Remember the Yaraq roofs.")));
        Gameplay->HandleJournalEdited(FText::GetEmpty());
        Gameplay->JournalFile.Reset(); Gameplay->RefreshQuestOverlays();
        TestEqual(TEXT("Journal notes survive a disk reload"),Gameplay->JournalNotes->GetText().ToString(),FString(TEXT("Remember the Yaraq roofs.")));
        Gameplay->HandleNamedClick(TEXT("PageListTab")); CaptureScreen(TEXT("GameplayJournalPages"));
        TestTrue(TEXT("Page list shows saved titles"),Gameplay->QuestRows[0]->GetText().ToString().Contains(TEXT("Roof survey")));
        for (int32 Page=1;Page<40;++Page) { auto& Note=Gameplay->JournalPages.AddDefaulted_GetRef(); Note.Title=FString::Printf(TEXT("Entry %d"),Page); }
        Gameplay->RefreshQuestOverlays(); Gameplay->ScrollQuestList(-1.f);
        TestTrue(TEXT("Page list scrolls to later saved pages"),Gameplay->JournalScrollOffset==3 && Gameplay->QuestRowIds[0]==-4);
        Gameplay->JournalSearch=TEXT("Entry 39"); Gameplay->RefreshQuestOverlays();
        TestTrue(TEXT("Journal search reaches pages beyond the visible rows"),Gameplay->JournalFilteredCount==1 && Gameplay->QuestRowIds[0]==-40);
        Gameplay->JournalSearch.Reset(); Gameplay->JournalPages.SetNum(1); Gameplay->JournalScrollOffset=0;
        Gameplay->HandleNamedClick(TEXT("ContractsTab")); Gameplay->RefreshQuestOverlays();
        TestEqual(TEXT("Returning to contracts restores its page"),Gameplay->ActiveQuestTab,FString(TEXT("ContractsPage")));
        Gameplay->ShowPanelPage(TEXT("SpellManagementPanel_Field"));
        Client->Session->KnownSpells={1,2,3,4,5,6,7,8,9};
        Gameplay->HandleNamedClick(TEXT("SpellbookTab"));Gameplay->TickRefresh();
        CaptureScreen(TEXT("GameplaySpellbook"));
        if (!Gameplay->SpellbookRowIds.IsEmpty())
        {
            const auto* SpellRowWidget=Gameplay->SpellbookRowBackgrounds[0].Get();
            const auto G=SpellRowWidget->GetCachedGeometry();
            Gameplay->TryBeginSpellDrag(Canvas->GetCachedGeometry().AbsoluteToLocal(G.LocalToAbsolute(G.GetLocalSize()*.5)));
            TestEqual(TEXT("Clicking a spellbook row selects its spell"),Gameplay->SelectedSpellbookId,Gameplay->SpellbookRowIds[0]);
            const auto Template=UACEUILayoutResolver::LoadTemplate(0x21000037,0x10000343);
            const auto* Selected=Template->Children.FindByPredicate([](const auto& C){return C->ElementName==TEXT("ItemSlot_Icon_Selected");});
            TestTrue(TEXT("Selected spellbook row uses authored highlight"),Selected && Gameplay->SpellbookRowSelections[0]->Background.GetResourceObject()==Resources->ResolveIconTexture((*Selected)->ImageFileId));
            Gameplay->CancelSpellDrag();
            CaptureScreen(TEXT("GameplaySpellbookSelected"));
        }

        Gameplay->bComponentsShowCarriedOnly=false; // Stress the scrollbar with the full DAT catalog.
        Gameplay->HandleNamedClick(TEXT("SpellComponentTab"));Gameplay->TickRefresh();
        CaptureScreen(TEXT("GameplayComponents"));
        TestTrue(TEXT("Component window populates from the DAT component table"),Gameplay->ComponentRowWcids.ContainsByPredicate([](int32 Wcid){return Wcid>0;}));
        if (const auto CompBar=Manager->FindElementUnder(TEXT("SpellComponentPage"),TEXT("SpellComponents_ComponentList_Scrollbar")))
        {
            const FVector2D Grab=FVector2D(CompBar->GetScreenOrigin())+FVector2D(8,24);
            TestTrue(TEXT("Components scrollbar accepts a drag"),Gameplay->TryBeginScrollbarDrag(Grab));
            Gameplay->UpdateScrollbarDrag(Grab+FVector2D(0,180));
            const float DragFraction=Gameplay->ScrollDragFraction;
            TestTrue(TEXT("Dragging components reaches later entries"),Gameplay->ComponentScrollOffset>0);
            Gameplay->RefreshComponentOverlays(); Gameplay->UpdateScrollbarDrag(Grab+FVector2D(0,180));
            TestEqual(TEXT("Content refresh does not make the scrollbar jump"),Gameplay->ScrollDragFraction,DragFraction);
            Gameplay->TryFinishScrollbarDrag();
            CaptureScreen(TEXT("GameplayComponentsScrolled"));
        }
        // September 11 follow-up: real editable DAT glyphs, refill accounting and command packets.
        auto& Session=*Client->Session;
        // A fresh loopback sender keeps these commands isolated from game servers.
        FSocket* ComponentReceiver=Sockets->CreateSocket(NAME_DGram,TEXT("Component fixture receiver"),false);
        auto ComponentAddress=Sockets->CreateInternetAddr();ComponentAddress->SetIp(TEXT("127.0.0.1"),ValidAddress);ComponentAddress->SetPort(0);
        if (!ComponentReceiver || !ComponentReceiver->Bind(*ComponentAddress)) return false;
        ComponentReceiver->GetAddress(*ComponentAddress);
        Session.SocketC2S=Sockets->CreateSocket(NAME_DGram,TEXT("Component fixture sender"),false);
        Session.ServerC2SAddr=ComponentAddress;
        auto HasAction=[&](uint32 Expected)
        {
            for (const auto& Packet:Session.CachedC2SPackets)
            {
                FACEBinaryReader Wire(Packet.Value.Payload); Wire.Skip(16);
                if (Wire.ReadUInt32()!=ACEOpcode::GameAction) continue;
                Wire.ReadUInt32(); if (Wire.ReadUInt32()==Expected) return true;
            }
            return false;
        };
        Gameplay->bComponentsShowCarriedOnly=true;
        const auto& Component=Dat->GetSpellComponents()[0];
        const auto SavedDesired=Session.DesiredComponents;
        Session.DesiredComponents.Reset(); Session.DesiredComponents.Add(Component.Wcid,100);
        FACEWorldObject Comp;Comp.Guid=99120;Comp.WeenieClassId=Component.Wcid;Comp.IconId=Component.IconDid;
        Comp.Name=Component.Name;Comp.ItemType=0x1000;Comp.StackSize=40;Comp.MaxStackSize=1000;Comp.ContainerId=Player.Guid;
        Session.WorldObjects.Add(Comp.Guid,Comp);
        Gameplay->ComponentScrollOffset=0; Gameplay->RefreshComponentOverlays(); CaptureScreen(TEXT("GameplayComponentsRetail"));
        auto EntryPtr=Gameplay->ComponentDesiredEntries.FindByPredicate([&](const auto& E){return E && E->ContextId==int32(Component.Wcid) && E->GetVisibility()!=ESlateVisibility::Collapsed;});
        if (TestNotNull(TEXT("Component target is an editable field"),EntryPtr))
        {
            auto* Entry=EntryPtr->Get();
            auto InputWindow=SNew(SVirtualWindow).Size(FVector2D(ScreenSize));
            InputWindow->SetIsFocusable(true);InputWindow->SetContent(Slate);
            FSlateApplication::Get().RegisterVirtualWindow(InputWindow);
            {
                TGuardValue<uint32> Options(Session.CharacterOptions1,Session.CharacterOptions1 & ~0x00000800u);
                TGuardValue<TObjectPtr<UACEUIGameplayBinder>> ChatBinder(Controller->DatGameplayBinder,Gameplay);
                TGuardValue<TObjectPtr<UACEUICanvasWidget>> ChatCanvas(Controller->DatCanvasWidget,Canvas);
                auto& App=FSlateApplication::Get();
                Gameplay->ChatEntry->SetText(FText::GetEmpty());
                for (int32 Cycle=0; Cycle<4; ++Cycle)
                {
                    App.ClearKeyboardFocus();
                    TestTrue(TEXT("Every fresh Enter press opens chat without a camera tick or key-up"),
                        Controller->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::Enter,IE_Pressed,1.f)));
                    TestTrue(TEXT("Enter focuses the editable chat field"),Gameplay->IsChatEntryFocused());
                    for (TCHAR C:FString(TEXT("focus regression")))
                        App.ProcessKeyCharEvent(FCharacterEvent(C,FModifierKeysState(),0,false));
                    App.ProcessKeyDownEvent(FKeyEvent(EKeys::Enter,FModifierKeysState(),0,false,0,0));
                    TestTrue(TEXT("Enter sends and clears the current chat draft"),Gameplay->ChatEntry->GetText().IsEmpty());
                    TestTrue(TEXT("Sending schedules restoration of gameplay keyboard focus"),Gameplay->bPendingChatRefocus);
                    TestEqual(TEXT("Default chat mode returns to gameplay"),Gameplay->PendingChatRefocusWindow,INDEX_NONE);
                    Gameplay->bPendingChatRefocus=false;
                }
                App.ClearKeyboardFocus();
                TestTrue(TEXT("Slash opens chat from gameplay"),Controller->InputKey(
                    FInputKeyEventArgs::CreateSimulated(EKeys::Slash,IE_Pressed,1.f)));
                for (TCHAR C:FString(TEXT("/help")))
                    App.ProcessKeyCharEvent(FCharacterEvent(C,FModifierKeysState(),0,false));
                TestEqual(TEXT("Slash command contains exactly one prefix"),Gameplay->ChatEntry->GetText().ToString(),FString(TEXT("/help")));
                Gameplay->ChatEntry->SetText(FText::GetEmpty()); App.ClearKeyboardFocus();
                TestTrue(TEXT("Slash also opens chat from focused UI chrome"),Canvas->NativeOnPreviewKeyDown(
                    Canvas->GetCachedGeometry(),FKeyEvent(EKeys::Slash,FModifierKeysState(),0,false,0,0)).IsEventHandled());
                App.ClearKeyboardFocus();
            }
            Session.CachedC2SPackets.Reset(); Entry->SetText(FText::GetEmpty());
            FSlateApplication::Get().SetKeyboardFocus(Entry->TakeWidget());
            for (TCHAR C:FString(TEXT("175"))) FSlateApplication::Get().ProcessKeyCharEvent(FCharacterEvent(C,FModifierKeysState(),0,false));
            TestEqual(TEXT("Component field accepts routed keyboard characters"),Entry->GetText().ToString(),FString(TEXT("175")));
            TestTrue(TEXT("Component focus suppresses world keyboard actions"),Gameplay->IsChatEntryFocused());
            CaptureScreen(TEXT("GameplayComponentEditing"));
            FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(EKeys::Enter,FModifierKeysState(),0,false,0,0));
            TestEqual(TEXT("Typed target saves exact quantity"),Session.DesiredComponents.FindRef(Component.Wcid),175);
            TestTrue(TEXT("Typed component target sends retail action"),HasAction(ACEGameAction::SetDesiredComponentLevel));
            FSlateApplication::Get().SetKeyboardFocus(Entry->TakeWidget());
            FSlateApplication::Get().ProcessKeyCharEvent(FCharacterEvent('9',FModifierKeysState(),0,false));
            FSlateApplication::Get().ProcessKeyDownEvent(Escape);
            TestEqual(TEXT("Escape cancels the edit without closing Components"),Gameplay->ActivePanelPage,FString(TEXT("SpellManagementPanel_Field")));
            TestEqual(TEXT("Escape restores the original quantity text"),Entry->GetText().ToString(),FString(TEXT("175")));
            FSlateApplication::Get().UnregisterVirtualWindow(InputWindow);
            Entry->SetText(FText::FromString(TEXT("99999")));Entry->Commit(ETextCommit::OnEnter);
            TestEqual(TEXT("Retail target limit is 5000"),Session.DesiredComponents.FindRef(Component.Wcid),5000);
            Entry->SetText(FText::FromString(TEXT("2")));Entry->Commit(ETextCommit::OnCleared);
            TestEqual(TEXT("Cancel does not alter the saved target"),Session.DesiredComponents.FindRef(Component.Wcid),5000);
        }
        Session.DesiredComponents[Component.Wcid]=100;
        const auto SavedStock=Session.VendorMerchandise;
        const int32 SavedVendor=Gameplay->OpenVendorGuid;
        const auto SavedCart=Gameplay->VendorBuyCart;
        const float SavedRate=Session.VendorSellRate;
        FACEWorldObject Stock=Comp;Stock.Guid=99121;Stock.ContainerId=99122;Stock.StackSize=1;Stock.Value=10;
        Stock.VendorQuantityAvailable=-1;Session.VendorMerchandise={Stock};Session.WorldObjects.Add(Stock.Guid,Stock);
        Session.VendorSellRate=1;Gameplay->OpenVendorGuid=99122;Gameplay->VendorBuyCart={{10,Stock.Guid}};
        {
            const auto SavedSelection=Gameplay->LastSelection;
            const auto SavedClientSelection=Session.SelectedObject;
            const int32 SavedSelectedVendor=Gameplay->VendorSelectedGuid;
            const int32 SavedSessionVendor=Session.OpenVendorGuid;
            Session.OpenVendorGuid=99122;
            FACEWorldObject Helmet=Stock;Helmet.Guid=99123;Helmet.ItemType=ACEItemType::Armor;Helmet.MaxStackSize=0;
            Session.VendorMerchandise.Add(Helmet);Session.WorldObjects.Add(Helmet.Guid,Helmet);
            FACESelectedObject Pick;Pick.bValid=true;Pick.Guid=Helmet.Guid;
            Session.SelectedObject=Pick;Gameplay->VendorSelectedGuid=Helmet.Guid;Gameplay->HandleSelectionChanged(Pick);
            TestEqual(TEXT("Unlimited helmets have no stack quantity slider"),Gameplay->SelectedStackMax,1);
            Gameplay->SelectedStackAmount=100;Gameplay->VendorBuyCart.Reset();Gameplay->AddSelectedVendorItemToBuyCart();
            TestEqual(TEXT("A helmet can only be added as one item"),Gameplay->VendorBuyCart[0].Key,1);
            Gameplay->AddSelectedVendorItemToBuyCart();
            TestEqual(TEXT("Repeated add does not turn helmets into a stack"),Gameplay->VendorBuyCart[0].Key,1);
            Session.VendorMerchandise[0].MaxStackSize=250;Session.VendorMerchandise[0].VendorQuantityAvailable=17;
            Pick.Guid=Stock.Guid;Session.SelectedObject=Pick;Gameplay->VendorSelectedGuid=Stock.Guid;Gameplay->HandleSelectionChanged(Pick);
            TestEqual(TEXT("Quantity follows current vendor stock, not the stale object cache"),Gameplay->SelectedStackMax,17);
            TestEqual(TEXT("Selecting vendor stock defaults to the whole available stack"),Gameplay->SelectedStackAmount,17);
            Gameplay->VendorBuyCart.Reset();Gameplay->AddSelectedVendorItemToBuyCart();
            TestEqual(TEXT("Vendor Add transfers the full selected stack"),Gameplay->VendorBuyCart[0].Key,17);
            Gameplay->SelectedStackAmount=5;Gameplay->HandleSelectionChanged(Pick);
            TestEqual(TEXT("Selection refresh preserves an explicitly split quantity"),Gameplay->SelectedStackAmount,5);
            Gameplay->VendorBuyCart.Reset();Gameplay->AddSelectedVendorItemToBuyCart();
            TestEqual(TEXT("Vendor Add respects the user's quantity adjustment"),Gameplay->VendorBuyCart[0].Key,5);
            Session.VendorMerchandise[0].VendorQuantityAvailable=-1;Gameplay->HandleVendorOpened(99122);
            TestEqual(TEXT("Unlimited ammunition is limited to its real stack size"),Gameplay->SelectedStackMax,250);
            Session.VendorMerchandise[0].VendorQuantityAvailable=0;Gameplay->HandleVendorOpened(99122);
            TestEqual(TEXT("Sold-out stack hides quantity control"),Gameplay->SelectedStackMax,0);
            Session.CachedC2SPackets.Reset();Gameplay->BuySelectedVendorItem();
            Session.SendBuyItems(99122,{{100,Stock.Guid}});
            TestFalse(TEXT("Stale cart cannot buy exhausted vendor stock"),HasAction(ACEGameAction::Buy));
            Session.VendorMerchandise={Stock};Session.WorldObjects.Remove(Helmet.Guid);
            Gameplay->VendorBuyCart={{10,Stock.Guid}};Gameplay->VendorSelectedGuid=SavedSelectedVendor;
            Session.SelectedObject=SavedClientSelection;Gameplay->HandleSelectionChanged(SavedSelection);Session.OpenVendorGuid=SavedSessionVendor;
        }
        {
            FACEWorldObject Pack;Pack.Guid=99130;Pack.ContainerId=Player.Guid;Pack.ItemType=ACEItemType::Container;Pack.ItemsCapacity=24;Pack.Attuned=1;
            FACEWorldObject Bread;Bread.Guid=99131;Bread.ContainerId=Pack.Guid;Bread.ItemType=ACEItemType::Food;Bread.StackSize=12;Bread.Value=120;
            FACEWorldObject Retained=Bread;Retained.Guid=99132;Retained.ObjectDescriptionFlags=ACEObjectDescFlag::Retained;
            FACEWorldObject Bound=Bread;Bound.Guid=99133;Bound.Attuned=1;
            for(const auto& Object:{Pack,Bread,Retained,Bound})Session.WorldObjects.Add(Object.Guid,Object);
            Gameplay->VendorSellCart={{2,Bread.Guid}};
            Session.CachedC2SPackets.Reset();Gameplay->AddInventoryGuidToVendorSellCart(Pack.Guid);
            TestEqual(TEXT("Pack drop adds only sellable contents, never the pack"),Gameplay->VendorSellCart.Num(),1);
            TestEqual(TEXT("Pack drop restores full stack quantity"),Gameplay->VendorSellCart[0].Key,12);
            TestEqual(TEXT("Pack contents open the Sell tab"),Gameplay->ActiveVendorPage,2);
            TestFalse(TEXT("Pack drop stages contents without selling them"),HasAction(ACEGameAction::Sell));
            Gameplay->AddInventoryGuidToVendorSellCart(Pack.Guid);
            TestEqual(TEXT("Repeated pack drops do not duplicate sale entries"),Gameplay->VendorSellCart.Num(),1);
            Gameplay->ShowVendorPanel(99122);CaptureScreen(TEXT("GameplayBulkVendor"));
            const auto SellList=Manager->FindElementUnder(TEXT("VendorSellPage"),TEXT("VendorSellList"));
            if(TestTrue(TEXT("Vendor sell list is a real drop target"),SellList.IsValid()))
            {
                auto DropPack=[&](int32 Guid)
                {
                    Gameplay->InvDragGuid=Guid;Gameplay->InvDragSourcePack=Player.Guid;
                    Gameplay->bInvDragPending=true;Gameplay->bInvDragActive=true;
                    Gameplay->TryFinishInventoryDrag(FVector2D(SellList->GetScreenOrigin())+FVector2D(15,15));
                };
                Gameplay->VendorSellCart.Reset();DropPack(Pack.Guid);
                TestTrue(TEXT("Native pack drop reaches the sell cart"),Gameplay->VendorSellCart.ContainsByPredicate([&](auto& P){return P.Value==Bread.Guid;}));
                Session.WorldObjects[Bread.Guid].ContainerId=Player.Guid;
                Gameplay->VendorSellCart.Reset();DropPack(Player.Guid);
                TestTrue(TEXT("Main backpack also supports bulk vendor drops"),Gameplay->VendorSellCart.ContainsByPredicate([&](auto& P){return P.Value==Bread.Guid;}));
                Session.WorldObjects[Bread.Guid].ContainerId=Pack.Guid;
            }
            const uint32 SavedTypes=Session.VendorItemTypes;
            Session.VendorItemTypes=ACEItemType::Armor;Gameplay->VendorSellCart.Reset();
            Gameplay->AddInventoryGuidToVendorSellCart(Pack.Guid);
            TestTrue(TEXT("Bulk sale skips item types the vendor does not buy"),Gameplay->VendorSellCart.IsEmpty());
            Session.VendorItemTypes=SavedTypes;
            Session.VendorMaxValue=9;Gameplay->AddInventoryGuidToVendorSellCart(Pack.Guid);
            TestTrue(TEXT("Bulk sale respects vendor per-unit value limits"),Gameplay->VendorSellCart.IsEmpty());
            Session.VendorMaxValue=-1;
            Gameplay->HandleEscape();
            TestEqual(TEXT("Escape dismisses an open vendor"),Gameplay->OpenVendorGuid,0);
            for(const auto& Object:{Pack,Bread,Retained,Bound})Session.WorldObjects.Remove(Object.Guid);
            Gameplay->OpenVendorGuid=99122;Gameplay->VendorBuyCart={{10,Stock.Guid}};
        }
        Session.CachedC2SPackets.Reset();Gameplay->TryDispatchChatCommand(TEXT("/fillcomps"));
        TestEqual(TEXT("Refill subtracts carried and already-cart quantities"),Gameplay->VendorBuyCart[0].Key,60);
        TestFalse(TEXT("Refill never purchases without the Buy button"),HasAction(ACEGameAction::Buy));
        Gameplay->TryDispatchChatCommand(TEXT("@fillcomps"));
        TestEqual(TEXT("Repeated refill does not overbuy"),Gameplay->VendorBuyCart[0].Key,60);
        Gameplay->VendorBuyCart.Reset();Gameplay->TryDispatchChatCommand(TEXT("/fillcomps 599"));
        TestTrue(TEXT("Price cap preserves whole-component refill limit"),Gameplay->VendorBuyCart.IsEmpty());
        Session.VendorMerchandise[0].VendorQuantityAvailable=20;Gameplay->TryDispatchChatCommand(TEXT("/fillcomps"));
        TestEqual(TEXT("Finite vendor supply caps cart quantity"),Gameplay->VendorBuyCart[0].Key,20);
        Gameplay->TryDispatchChatCommand(TEXT("/fillcomps clear"));
        TestTrue(TEXT("Clear removes saved refill quantities"),Session.DesiredComponents.IsEmpty());
        Session.DesiredComponents=SavedDesired;Session.VendorMerchandise=SavedStock;Session.VendorSellRate=SavedRate;
        Gameplay->OpenVendorGuid=SavedVendor;Gameplay->VendorBuyCart=SavedCart;
        Session.WorldObjects.Remove(Comp.Guid);Session.WorldObjects.Remove(Stock.Guid);
        for (const auto& Command : TArray<TPair<FString,uint32>>{{TEXT("/age"),ACEGameAction::QueryAge},{TEXT("@birth"),ACEGameAction::QueryBirth},{TEXT("/pkl"),ACEGameAction::EnterPkLite},{TEXT("/pklite"),ACEGameAction::EnterPkLite}})
        {
            Session.CachedC2SPackets.Reset();Gameplay->TryDispatchChatCommand(Command.Key);
            TestTrue(TEXT("Misc command sends its dedicated protocol action"),HasAction(Command.Value));
            TestFalse(TEXT("Misc command does not leak into public chat"),HasAction(ACEGameAction::Talk));
        }
        FString AgeReply;
        const auto AgeHandle=Session.OnChatMessage.AddLambda([&](const FString& T,const FString&,int32){AgeReply=T;});
        FACEBinaryWriter Age;Age.WriteUInt32(Player.Guid);Age.WriteUInt32(1);Age.WriteUInt32(ACEGameEvent::QueryAgeResponse);
        Age.WriteString16L(TEXT(""));Age.WriteString16L(TEXT("5d 3h 13m 1s"));FACEBinaryReader AgeReader(Age.GetData());Session.HandleGameEvent(AgeReader);
        TestTrue(TEXT("Server age response reaches displayed chat"),AgeReply.Contains(TEXT("5d 3h 13m 1s")));Session.OnChatMessage.Remove(AgeHandle);
        const FString CommandPath=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("Automation/Misc command fixture.txt"));
        const FString MirrorPath=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("Automation/Misc command output.txt"));
        const bool SavedLogging=Gameplay->bChatMirrorToFile;
        const FString SavedMirror=Gameplay->ChatMirrorFilePath;
        FFileHelper::SaveStringToFile(TEXT("/help %DATE%\n/version\n"),*CommandPath);
        FFileHelper::SaveStringToFile(TEXT("existing log line\n"),*MirrorPath);
        Gameplay->TryDispatchChatCommand(TEXT("/log \"")+MirrorPath+TEXT("\""));
        Session.CachedC2SPackets.Reset();
        Gameplay->TryDispatchChatCommand(TEXT("/loadfile \"")+CommandPath+TEXT("\""));
        Gameplay->TryDispatchChatCommand(TEXT("/help fillcomps"));
        Gameplay->TryDispatchChatCommand(TEXT("/log"));
        TestFalse(TEXT("Log without a name stops file output"),Gameplay->bChatMirrorToFile);
        FString Mirrored;FFileHelper::LoadFileToString(Mirrored,*MirrorPath);
        TestTrue(TEXT("Log appends instead of replacing an existing file"),Mirrored.StartsWith(TEXT("existing log line")));
        TestTrue(TEXT("Command files expand the retail date and weekday"),Mirrored.Contains(FDateTime::Now().ToFormattedString(TEXT("%Y-%m-%d-%a"))));
        TestTrue(TEXT("Command files execute local commands"),Mirrored.Contains(PLATFORM_ANDROID ? TEXT("AC:VR") : TEXT("AC:Unreal")));
        TestTrue(TEXT("Refill help explains the vendor buy list"),Mirrored.Contains(TEXT("choose Buy All")));
        TestFalse(TEXT("Local command files and help do not send public chat"),HasAction(ACEGameAction::Talk));
        Gameplay->bChatMirrorToFile=SavedLogging;Gameplay->ChatMirrorFilePath=SavedMirror;
        const auto SavedVitals=Session.PlayerVitals;
        Session.PlayerVitals.bValid=true;Session.PlayerVitals.AgeSeconds=443581;Session.PlayerVitals.NumDeaths=6;
        Session.PlayerVitals.StatQualityInts.Add(98,1698189042);Session.PlayerVitals.StatQualityInts.Add(354,6);
        Session.PlayerVitals.StatQualityInts.Add(355,8);Session.PlayerVitals.StatQualityInts.Add(362,1);
        Session.PlayerVitals.StatQualityInts.Add(294,1);Session.PlayerVitals.StatQualityInts.Add(326,1);
        Gameplay->ShowPanelPage(TEXT("InventoryPanel_Field"));Gameplay->HandleNamedClick(TEXT("BurdenIndicator"));Gameplay->TickRefresh();
        TestEqual(TEXT("Small pack opens Character Information"),Gameplay->ActivePanelPage,FString(TEXT("CharacterInfoPanel_Field")));
        TestEqual(TEXT("Character Information title is visible"),Gameplay->CharacterInfoTitle->GetText().ToString(),FString(TEXT("Character Information")));
        TestEqual(TEXT("Character Information fills the available panel height"),Manager->FindElementUnder(TEXT("CharacterInfoPanel_Field"),TEXT("CharacterInfoText"))->Height,
            Manager->FindElementByName(TEXT("CharacterInfoPanel_Field"))->Height-25);
        const auto Info=Gameplay->PanelBodyText->GetText().ToString();
        for (const TCHAR* Part:{TEXT("You were born"),TEXT("13 minutes 1 second"),TEXT("Natural Resistances"),TEXT("Innate Self"),TEXT("Daggers"),TEXT("Bows"),TEXT("Naturalist"),TEXT("Infused Creature"),TEXT("Jack of All Trades")})
            TestTrue(Part,Info.Contains(Part));
        CaptureScreen(TEXT("GameplayCharacterInformation"));
        if (Gameplay->CharacterInfoScroll) { Gameplay->CharacterInfoScroll->SetScrollOffset(1000);CaptureScreen(TEXT("GameplayCharacterInformationScrolled")); }
        Session.PlayerVitals=SavedVitals;
        Gameplay->ShowPanelPage(TEXT("MiniGamePanel_Field"));Gameplay->bChessActive=true;Gameplay->ChessMyColor=0;Gameplay->ResetChessBoard();Gameplay->RefreshMiniGameOverlays();
        CaptureScreen(TEXT("GameplayChessPieces"));
        int32 PieceCount=0;for (const auto& Icon:Gameplay->ChessSquareIcons) if (Icon && Icon->GetVisibility()!=ESlateVisibility::Collapsed) {++PieceCount;TestNotNull(TEXT("Chess piece resolves to a DAT texture"),Icon->Background.GetResourceObject());}
        TestEqual(TEXT("Starting chess game paints all 32 pieces"),PieceCount,32);
        TestFalse(TEXT("Chess hides the unrelated Pass action"),Manager->FindElementUnder(TEXT("MiniGamePanel_Field"),TEXT("MiniGame_Pass"))->bVisible);
        Gameplay->bChessActive=false;
        Gameplay->RefreshShortcutOverlays();
        for(int32 I=0;I<9;++I) if(!Session.GetShortcutObject(I))
            TestTrue(TEXT("Empty first-nine shortcuts keep the blue numbered DAT background"),Gameplay->ShortcutSlotBgs[I]->Background.GetResourceObject()==Resources->ResolveIconTexture(0x060010FA+I));
        Gameplay->ShowPanelPage(TEXT("WorldPanel_Field"));
        Gameplay->SyncWorldPanelTab(TEXT("MapPage")); Gameplay->RefreshWorldOverlays();
        CaptureScreen(TEXT("GameplayMapHover"));
        if (const auto MapImage=Manager->FindElementUnder(TEXT("MapPage"),TEXT("Map")))
        {
            // Arwic's authored map hover rectangle is 190,88 to 199,96.
            const FVector2D NativePoint=FVector2D(MapImage->GetScreenOrigin())
                +FVector2D(194.f*MapImage->Width/257.f,92.f*MapImage->Height/267.f);
            FString Town;
            const FVector2D Absolute=Canvas->GetCachedGeometry().LocalToAbsolute(NativePoint*Canvas->GetLastScale2D());
            TestTrue(TEXT("Map exposes a town hover label"),Gameplay->GetMapTooltipAt(Absolute,Town));
            TestEqual(TEXT("Map hover resolves Arwic"),Town,FString(TEXT("Arwic")));
        }
        Client->Session->House.bQueried=true; Client->Session->House.bOwned=true; Client->Session->House.HouseType=1;
        Client->Session->House.BuyTime=1750000000; Client->Session->House.RentTime=1750000000;
        FACEHousePayment RentFixture; RentFixture.Name=TEXT("Pyreal"); RentFixture.PluralName=TEXT("Pyreals");
        RentFixture.Required=1000; RentFixture.Paid=1000; Client->Session->House.Rent={RentFixture};
        Gameplay->SyncWorldPanelTab(TEXT("HousePage")); Gameplay->RefreshWorldOverlays();
        CaptureScreen(TEXT("GameplayHousing"));
        TestTrue(TEXT("Housing uses the retail Bought label"),Gameplay->HouseTextRows.ContainsByPredicate(
            [](UTextBlock* Label){return Label && Label->GetText().ToString().StartsWith(TEXT("Bought:"));}));
        Client->Session->House=FACEHouseInfo();

        Gameplay->ShowPanelPage(TEXT("SocialPanel_Field"));
		{
			const auto SavedEnchantments=Session.ActiveEnchantments;
			Session.ActiveEnchantments.Reset();
			for (int32 Id : {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24})
			{
				FACEActiveEnchantment E; E.SpellId=Id; E.SpellCategory=Id; E.bBeneficial=true;
				E.Duration=Id==1 ? -1 : 3661.9f; E.PowerLevel=100; Session.ActiveEnchantments.Add(E);
			}
			FACEActiveEnchantment Time; Time.Duration=3661.9f;
			TestEqual(TEXT("Retail effect timer includes hours, minutes and seconds, truncating fractions"),Gameplay->FormatEnchantmentRemaining(Time),FString(TEXT("1:01:01")));
			Time.Duration=61.9f; TestEqual(TEXT("Short timer uses m:ss"),Gameplay->FormatEnchantmentRemaining(Time),FString(TEXT("1:01")));
			Time.ReceivedAt=FPlatformTime::Seconds()-10;
			TestEqual(TEXT("Effect timer advances between server updates"),Gameplay->FormatEnchantmentRemaining(Time),FString(TEXT("0:51")));
			Time.Duration=-1; TestTrue(TEXT("Permanent effect has no duration label"),Gameplay->FormatEnchantmentRemaining(Time).IsEmpty());
			Gameplay->ShowPanelPage(TEXT("PositiveEffectsPanel_Field")); Gameplay->TickRefresh();
			TestEqual(TEXT("Effect rows use the authored 32px template"),Gameplay->EffectsRowElements[0]->Height,32);
			TestEqual(TEXT("Opening effects does not invent a selected spell"),Gameplay->SelectedEffectsSpellId,0);
			Gameplay->SelectedEffectsSpellId=1;
			for (int32 SpellId:Gameplay->EffectsListSpellIds)
			{
				FString ValidDescription;
				if (Dat->TryGetSpellDescription(SpellId,ValidDescription) && !ValidDescription.IsEmpty())
				{Gameplay->SelectedEffectsSpellId=SpellId;break;}
			}
			Gameplay->RefreshEffectsOverlays(true);
			FString EffectDescription; Dat->TryGetSpellDescription(Gameplay->SelectedEffectsSpellId,EffectDescription);
			TestTrue(TEXT("Selected effect shows the actual spell description"),!EffectDescription.IsEmpty() && Gameplay->EffectsInfoLabel->GetText().ToString().Contains(EffectDescription));
			for (int32 EffectRow=0;EffectRow<Gameplay->EffectsListSpellIds.Num();++EffectRow)
			{
				auto* NameSlot=Cast<UCanvasPanelSlot>(Gameplay->EffectsListRows[EffectRow]->Slot);
				auto* TimeSlot=Cast<UCanvasPanelSlot>(Gameplay->EffectsListDurations[EffectRow]->Slot);
				TestTrue(TEXT("Effect name cannot overlap its separate duration column"),NameSlot && TimeSlot && NameSlot->GetPosition().X+NameSlot->GetSize().X<=TimeSlot->GetPosition().X+.01);
				TestFalse(TEXT("Duration is not appended to the spell name"),Gameplay->EffectsListRows[EffectRow]->GetText().ToString().Contains(TEXT(":")));
			}
			CaptureScreen(TEXT("GameplayEffectsRetailColumns"));
			Gameplay->EffectsScrollOffset=999; Gameplay->RefreshEffectsOverlays(true); CaptureScreen(TEXT("GameplayEffectsRetailScrolled"));
			auto Stronger=Session.ActiveEnchantments[0]; Stronger.SpellId=30; Stronger.PowerLevel=200; Session.ActiveEnchantments.Add(Stronger);
			Gameplay->RefreshEffectsOverlays(true); TestEqual(TEXT("Superseded enchantment is not a second active row"),Gameplay->EffectsContentCount,24);
			for (auto& E:Session.ActiveEnchantments) E.bBeneficial=false;
			Gameplay->ShowPanelPage(TEXT("NegativeEffectsPanel_Field")); Gameplay->TickRefresh(); CaptureScreen(TEXT("GameplayHarmfulEffectsRetailColumns"));
			Session.ActiveEnchantments=SavedEnchantments;
			Gameplay->ShowPanelPage(TEXT("SocialPanel_Field"));
		}
        for (const TCHAR* AuditTab:{TEXT("AllegiancePage"),TEXT("FellowshipPage"),TEXT("FriendsPage"),TEXT("SquelchPage")})
        {
            Gameplay->SyncSocialPanelTab(AuditTab);Gameplay->TickRefresh();CaptureScreen(FString(TEXT("GameplayAudit"))+AuditTab);
        }
        {
            const auto SavedFellowship=Session.Fellowship;
            const uint32 SavedOptions=Session.CharacterOptions1;
            Session.Fellowship=FACEFellowshipInfo();
            Gameplay->SyncSocialPanelTab(TEXT("FellowshipPage"));Gameplay->TickRefresh();
            const TPair<const TCHAR*,int32> Checks[]={{TEXT("IgnoreFellowshipRequests"),2},{TEXT("FellowshipAutoAcceptRequests"),0x12},{TEXT("FellowshipShareXP"),0x0f},{TEXT("FellowshipShareLoot"),0x11}};
            for(const auto& Check:Checks)
            {
                const auto El=Manager->FindElementUnder(TEXT("FellowshipPage"),Check.Key);
                if(!TestTrue(TEXT("Retail fellowship checkbox exists"),El.IsValid()))continue;
                const FIntPoint Origin=El->GetScreenOrigin();
                const auto Hit=Manager->HitTestCanvas(Origin.X+El->Width/2,Origin.Y+El->Height/2);
                TestTrue(FString::Printf(TEXT("%s label is a clickable retail checkbox"),Check.Key),Hit==El);
                const bool Before=Client->IsCharacterOptionSet(Check.Value);
                Session.CachedC2SPackets.Reset();
                const FVector2D Point=FVector2D(Origin)+FVector2D(El->Width/2,El->Height/2);
                Manager->NotifyMouseDown(Point,Canvas->GetCachedGeometry().GetLocalSize(),EKeys::LeftMouseButton);
                Manager->NotifyMouseUp(Point,Canvas->GetCachedGeometry().GetLocalSize(),EKeys::LeftMouseButton,true);
                TestEqual(TEXT("Actual pointer click toggles the server-backed option"),Client->IsCharacterOptionSet(Check.Value),!Before);
                TestTrue(TEXT("Checkbox emits character option action"),HasAction(ACEGameAction::SetSingleCharacterOption));
            }
            Gameplay->FellowshipNameEntry->SetText(FText::GetEmpty());
            TestFalse(TEXT("Empty name disables Create like retail"),Gameplay->CanActivateFellowshipControl(TEXT("CreateFellowshipButton")));
            Session.CachedC2SPackets.Reset();Gameplay->HandleNamedClick(TEXT("CreateFellowshipButton"));
            TestFalse(TEXT("Empty name cannot silently create a generic fellowship"),HasAction(ACEGameAction::FellowshipCreate));
            Client->SendSetSingleCharacterOption(0x0f,false);
            Gameplay->FellowshipNameEntry->SetText(FText::FromString(TEXT("Retail Test")));
            Session.CachedC2SPackets.Reset();Gameplay->HandleNamedClick(TEXT("CreateFellowshipButton"));
            TestTrue(TEXT("Named fellowship can be created"),HasAction(ACEGameAction::FellowshipCreate));
            for(const auto& P:Session.CachedC2SPackets)
            {
                FACEBinaryReader R(P.Value.Payload);R.Skip(24);if(R.ReadUInt32()!=ACEGameAction::FellowshipCreate)continue;
                TestEqual(TEXT("Create uses typed name"),R.ReadString16L(),FString(TEXT("Retail Test")));
                TestEqual(TEXT("Create uses displayed XP preference rather than stale default"),R.ReadUInt32(),0u);
            }
            FACEFellowshipMember Self;Self.Guid=Player.Guid;Self.Name=TEXT("Leader");Self.Level=100;Self.HealthCur=250;Self.HealthMax=500;Self.StaminaCur=300;Self.StaminaMax=400;Self.ManaCur=50;Self.ManaMax=200;
            FACEFellowshipMember Other=Self;Other.Guid=991122;Other.Name=TEXT("Fellow");
            auto& F=Session.Fellowship;F.bValid=true;F.Name=TEXT("Retail Test");F.LeaderGuid=Self.Guid;F.Members={Self,Other};
            Gameplay->SelectedFellowGuid=Other.Guid;Gameplay->RefreshFellowshipOverlays();Gameplay->RefreshSocialButtonLabels();
            TestEqual(TEXT("Fellowship member uses the retail two-line height"),Gameplay->FellowRowElements[0]->Height,32);
            const auto HealthMeter=Manager->FindElementUnder(TEXT("FellowsListBox"),TEXT("FellowHealth"));
            TestTrue(TEXT("Fellowship health is a live meter"),HealthMeter&&FMath::IsNearlyEqual(HealthMeter->MeterFillFraction,.5f));
            for(int32 I=2;I<9;++I){Other.Guid=991122+I;Other.Name=FString::Printf(TEXT("Fellow %d"),I+1);F.Members.Add(Other);}
            Gameplay->FellowScrollOffset=999;Gameplay->RefreshFellowshipOverlays();
            TestEqual(TEXT("Nine-member fellowship scrolls to its final member"),Gameplay->FellowRowGuids[Gameplay->FellowVisibleRows-1],F.Members.Last().Guid);
            Gameplay->FellowScrollOffset=0;Gameplay->RefreshFellowshipOverlays();
            TestTrue(TEXT("Leader can dismiss another member"),Gameplay->CanActivateFellowshipControl(TEXT("FellowDismissButton")));
            TestTrue(TEXT("Leader can transfer leadership"),Gameplay->CanActivateFellowshipControl(TEXT("FellowLeaderButton")));
            Gameplay->SelectedFellowGuid=Self.Guid;
            TestFalse(TEXT("Leader cannot dismiss self"),Gameplay->CanActivateFellowshipControl(TEXT("FellowDismissButton")));
            F.bOpen=true;Gameplay->RefreshSocialButtonLabels();
            TestTrue(TEXT("Open fellowship button now says Close"),Gameplay->SocialButtonLabels.ContainsByPredicate([](const UTextBlock* T){return T&&T->GetText().ToString()==TEXT("Close");}));
            F.LeaderGuid=Other.Guid;Gameplay->RefreshFellowshipOverlays();
            for(const TCHAR* Name:{TEXT("FellowDismissButton"),TEXT("FellowLeaderButton"),TEXT("FellowOpenButton"),TEXT("FellowDisbandButton")})
            {
                Session.CachedC2SPackets.Reset();Gameplay->HandleNamedClick(Name);
                TestTrue(TEXT("Leader-only controls cannot emit packets for ordinary members"),Session.CachedC2SPackets.IsEmpty());
            }
            CaptureScreen(TEXT("GameplayFellowshipMember"));
            Session.Fellowship=SavedFellowship;Session.CharacterOptions1=SavedOptions;
            Gameplay->FellowshipNameEntry->SetText(FText::GetEmpty());
            Gameplay->RefreshFellowshipOverlays();Gameplay->RefreshSocialButtonLabels();
        }
        Session.SocketC2S->Close();Sockets->DestroySocket(Session.SocketC2S);Session.SocketC2S=nullptr;
        ComponentReceiver->Close();Sockets->DestroySocket(ComponentReceiver);
        for (const TCHAR* Page:{TEXT("WorldPanel_Field"),TEXT("CharacterInfoPanel_Field"),TEXT("PositiveEffectsPanel_Field"),
            TEXT("NegativeEffectsPanel_Field"),TEXT("LinkStatusPanel_Field"),TEXT("VitaePanel_Field"),
            TEXT("BookPanel_Field"),TEXT("MiniGamePanel_Field"),TEXT("AbusePanel_Field"),TEXT("UrgentAssistancePanel_Field")})
        {
            Gameplay->ShowPanelPage(Page);Gameplay->TickRefresh();CaptureScreen(FString(TEXT("GameplayAudit"))+Page);
        }
        Gameplay->ShowPanelPage(TEXT("InventoryPanel_Field"));
        {
            const auto InventoryTitleElement=Manager->FindElementUnder(TEXT("InventoryPanel_Field"),TEXT("InvTitleText"));
            TestTrue(TEXT("Inventory retains its retail title strip"),InventoryTitleElement && InventoryTitleElement->ImageFileId==0x06004CFA && InventoryTitleElement->Height==25);
            if (InventoryTitleElement)
            {
                TestEqual(TEXT("Inventory retains the retail 18px font"),InventoryTitleElement->FontId,0x40000001u);
                FACEDatFont TitleFont; Resources->ResolveFont(InventoryTitleElement->FontId,TitleFont);
                const FString LongTitle=TEXT("Inventory of An Adventurer With A Very Long Character Name");
                const FString Fitted=ACEDatText::Ellipsize(TitleFont,LongTitle,InventoryTitleElement->Width-10);
                TestTrue(TEXT("Long inventory title ends with an ellipsis"),Fitted.EndsWith(TEXT("...")) && Fitted.Len()<LongTitle.Len());
                TestTrue(TEXT("Inventory ellipsis fits inside native margins"),ACEDatText::Layout(TitleFont,Fitted,InventoryTitleElement->Width-10,true)[0].Width<=InventoryTitleElement->Width-10);
            }
        }
        Controller->MouseCursorWidget=NewObject<UACEMouseCursorWidget>(); Controller->MouseCursorWidget->Initialize();
        auto CursorSlate=Controller->MouseCursorWidget->TakeWidget();
        Controller->RetailCursorDefaultTex=Resources->ResolveIconTexture(0x06004D68);
        Controller->RetailCursorTargetTex=Resources->ResolveIconTexture(0x06004D73);
        Controller->RetailCursorTargetValidTex=Resources->ResolveIconTexture(0x06005E6B);
        Controller->RetailCursorTargetInvalidTex=Resources->ResolveIconTexture(0x06005E6A);
        Controller->bRetailCursorInstalled=true;
        UImage* CursorArt=nullptr;
        Controller->MouseCursorWidget->WidgetTree->ForEachWidget([&](UWidget* W){ if (auto* I=Cast<UImage>(W)) CursorArt=I; });
        Controller->SetPendingUseTargeting(true); CursorSlate->SlatePrepass(1.f);
        if (TestNotNull(TEXT("Target cursor contains DAT image widget"),CursorArt))
        {
            TestTrue(TEXT("Arming an item selects retail untargeted crosshair"),CursorArt->GetBrush().GetResourceObject()==Controller->RetailCursorTargetTex);
            const FVector2D Size=CursorArt->GetBrush().ImageSize;
            TestTrue(TEXT("Software cursor has explicit centered desired size"),CursorSlate->GetDesiredSize().Equals(FVector2f(Size*2),.01f));
            const auto* Slot=Cast<UCanvasPanelSlot>(CursorArt->Slot);
            TestTrue(TEXT("Retail target hotspot remains at the pointer"),Slot && (Slot->GetPosition()-Size).Equals(FVector2D(-14,-14),.01));
            Controller->ApplyRetailMouseCursor(true,true);
            TestTrue(TEXT("Compatible target selects green retail target art"),CursorArt->GetBrush().GetResourceObject()==Controller->RetailCursorTargetValidTex);
            Controller->ApplyRetailMouseCursor(true,false);
            TestTrue(TEXT("Incompatible target selects red retail target art"),CursorArt->GetBrush().GetResourceObject()==Controller->RetailCursorTargetInvalidTex);
            Controller->SetPendingUseTargeting(false);
            TestTrue(TEXT("Completed use restores retail arrow"),CursorArt->GetBrush().GetResourceObject()==Controller->RetailCursorDefaultTex);
        }
        FACEWorldObject Stone; Stone.Guid=9876; Stone.ItemType=ACEItemType::ManaStone;
        Stone.ContainerId=Player.Guid; Stone.ItemUseable=0x000A0008; Stone.TargetType=ACEItemType::Caster|ACEItemType::Creature;
        Client->Session->WorldObjects.Add(Stone.Guid,Stone);
        Gameplay->PendingUseWithSourceGuid=Stone.Guid;
        TestTrue(TEXT("Owned mana target passes retail cursor rules"),Gameplay->IsPendingUseTargetCompatible(Item.Guid));
        FACEWorldObject Enemy; Enemy.Guid=9877; Enemy.ItemType=ACEItemType::Creature;
        Client->Session->WorldObjects.Add(Enemy.Guid,Enemy);
        TestFalse(TEXT("Contained target rules reject an unowned creature"),Gameplay->IsPendingUseTargetCompatible(Enemy.Guid));
        Player.ItemType=ACEItemType::Creature; Client->Session->WorldObjects.Add(Player.Guid,Player);
        TestTrue(TEXT("Mana targeting permits the player-wide target"),Gameplay->IsPendingUseTargetCompatible(Player.Guid));
        Client->Session->TradeSelfItems.Add(Item.Guid);
        TestFalse(TEXT("Offered trade item uses incompatible targeting cursor"),Gameplay->IsPendingUseTargetCompatible(Item.Guid));
        Client->Session->TradeSelfItems.Reset(); Gameplay->PendingUseWithSourceGuid=0;
        Player.SetupId=0x02000001; Player.MotionTableId=0x09000001;
        Client->Session->WorldObjects.Add(Player.Guid,Player); Gameplay->LastVitals.HeritageGroup=1;
        const auto BeforeModelAppraisal=Gameplay->LastAppraisal;
        Gameplay->LastAppraisal=FACEAppraisalInfo();
        Gameplay->LastAppraisal.ObjectGuid=Player.Guid;Gameplay->LastAppraisal.Name=TEXT("Character inspection preview");
        Gameplay->LastAppraisal.bIsCreature=true;Gameplay->LastAppraisal.bSuccess=true;
        Gameplay->LastAppraisal.StringProperties.Add(5,TEXT("War Mage"));
        Gameplay->LastAppraisal.IntProperties={{113,1},{188,1}};
        Gameplay->LastAppraisal.Level=275;Gameplay->LastAppraisal.Strength=245;
        Gameplay->LastAppraisal.Health=378;Gameplay->LastAppraisal.MaxHealth=378;
        Gameplay->ShowExamination(true);Gameplay->RefreshExaminationOverlay();
        if (TestNotNull(TEXT("Inspection creates a creature model capture"),Gameplay->ExamPaperDollCapture.Get()))
        {
            auto* Capture=Gameplay->ExamPaperDollCapture.Get();
            auto* Actor=Gameplay->ExamPaperDollPreviewActor.Get();
            TestEqual(TEXT("Inspection uses retail model heading"),Actor->FindComponentByClass<UACECharacterAppearanceComponent>()->MeshFacingYawDegrees,ACERetailPaperDoll::HeadingDegrees);
            FBox Bounds(ForceInit);TArray<UMeshComponent*> Meshes;Actor->GetComponents(Meshes);
            for (auto* Mesh:Meshes) if (Mesh->IsVisible())
            {
                Bounds+=Mesh->Bounds.GetBox();
            }
            TestTrue(TEXT("Inspection model has visible geometry"),bool(Bounds.IsValid));
            const float Aspect=float(Gameplay->ExamPaperDollRenderTarget->SizeX)/Gameplay->ExamPaperDollRenderTarget->SizeY;
            const FVector Size=Bounds.GetSize();
            const float ExpectedDistance=FMath::Max(Size.Z,Size.X/Aspect)*1.2071068f+Size.Y*.5f;
            TestTrue(TEXT("Inspection camera frames actual bounds instead of a fixed human height"),Capture->GetComponentLocation().Equals(Bounds.GetCenter()-FVector(0,ExpectedDistance,0),.1));
            TestTrue(TEXT("Inspection preview is isolated from the playfield"),Actor->GetActorLocation().Equals(FVector(-50000,-48000,-50000),.1));
            if (GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();
            for (int32 I=0;I<3;++I) { Capture->CaptureScene();FlushRenderingCommands(); }
            TArray<FFloat16Color> Pixels;Gameplay->ExamPaperDollRenderTarget->GameThread_GetRenderTargetResource()->ReadFloat16Pixels(Pixels);
            int32 Covered=0;for(const auto& Pixel:Pixels) Covered+=Pixel.A.GetFloat()<.5f;
            AddInfo(FString::Printf(TEXT("Inspection capture covers %d/%d pixels"),Covered,Pixels.Num()));
            TestTrue(TEXT("Rendered inspection model occupies a useful share of the viewport"),Covered>Pixels.Num()/20 && Covered<Pixels.Num()*3/4);
            CaptureScreen(TEXT("GameplayCharacterInspectionWithModel"));
            auto* Appearance = Actor->FindComponentByClass<UACECharacterAppearanceComponent>();
            TestTrue(TEXT("Inspection uses its own view instead of world distance culling"),Appearance->bPreviewCapture);
            float HighestHead = 0.f;
            for (int32 Frame=0; Frame<90; ++Frame)
            {
                Appearance->TickComponent(1.f/30.f,LEVELTICK_All,nullptr);
                TArray<UPrimitiveComponent*> Parts; Actor->GetComponents(Parts);
                for (auto* Part : Parts) if (Part->GetName()==TEXT("ACEPart_16"))
                {
                    Part->UpdateBounds();
                    const FVector HeadTop=Part->Bounds.Origin+FVector(0,0,Part->Bounds.BoxExtent.Z);
                    const FVector CameraSpace=Capture->GetComponentTransform().InverseTransformPosition(HeadTop);
                    HighestHead=FMath::Max(HighestHead,float(CameraSpace.Z/(CameraSpace.X*FMath::Tan(FMath::DegreesToRadians(22.5f)))));
                }
            }
            AddInfo(FString::Printf(TEXT("Animated inspection head top in vertical clip space: %.3f"),HighestHead));
            TestTrue(TEXT("Animated inspection keeps the entire head inside the camera"), HighestHead < 1.f);
            Capture->CaptureScene();FlushRenderingCommands();CaptureScreen(TEXT("GameplayAnimatedInspection"));
            for (uint32 Setup : {0x02001A12u,0x02001A16u,0x0200196Du,0x020005C4u,0x02001A18u})
            {
                FACEWorldObject Subject=Player; Subject.SetupId=Setup; Subject.MotionTableId=Setup==0x020005C4 ? 0 : 0x09000001;
                // Some Undead setups intentionally contain a null head. The
                // server supplies it through ObjDesc; exercise that replacement.
                const auto* HumanSetup=Dat->GetOrBuildSetupMesh(0x02000001,100.f);
                if (Setup!=0x020005C4 && HumanSetup && HumanSetup->Parts.IsValidIndex(16))
                {
                    FACEObjDescAnimPartChange Head;
                    Head.PartIndex=16;Head.PartId=HumanSetup->Parts[16].GfxObjId;
                    Subject.Appearance.AnimPartChanges.Add(Head);
                }
                TestTrue(TEXT("Marketplace subject appearance is available"),Gameplay->ApplyPreviewCaptureAppearance(Actor,Capture,Subject,100.f));
                for (UProceduralMeshComponent* Part : Appearance->PartMeshes) if (Part && Part->GetName()==TEXT("ACEPart_16"))
                {
                    AddInfo(FString::Printf(TEXT("Inspection head %08X replacement=%08X visible=%d hidden=%d sections=%d shown=%d pos=%s"),
                        Setup,Subject.Appearance.AnimPartChanges.IsEmpty()?0:Subject.Appearance.AnimPartChanges.Last().PartId,
                        Part->IsVisible(),Part->bHiddenInGame,Part->GetNumSections(),Capture->ShowOnlyComponents.Contains(Part),*Part->GetComponentLocation().ToString()));
                    TestTrue(TEXT("Replaced inspection head is visible to the capture"),Setup==0x020005C4 ||
                        (Part->IsVisible() && !Part->bHiddenInGame && Part->GetNumSections()>0 && Capture->ShowOnlyComponents.Contains(Part)));
                }
                float HeadTopClip=0;
                for (int32 Frame=0;Frame<90;++Frame)
                {
                    Appearance->TickComponent(1.f/30.f,LEVELTICK_All,nullptr);
                    TArray<UPrimitiveComponent*> Parts;Actor->GetComponents(Parts);
                    for(auto* Part:Parts) if(Part->GetName()==TEXT("ACEPart_16"))
                    {
                        Part->UpdateBounds();
                        const FVector Top=Part->Bounds.Origin+FVector(0,0,Part->Bounds.BoxExtent.Z);
                        const FVector P=Capture->GetComponentTransform().InverseTransformPosition(Top);
                        HeadTopClip=FMath::Max(HeadTopClip,float(P.Z/(P.X*FMath::Tan(FMath::DegreesToRadians(22.5f)))));
                    }
                }
                AddInfo(FString::Printf(TEXT("Inspection setup %08X animated head top %.3f"),Setup,HeadTopClip));
                TestTrue(TEXT("Marketplace character head stays inside inspection frame"),HeadTopClip<1.f);
                Capture->CaptureScene();FlushRenderingCommands();CaptureScreen(FString::Printf(TEXT("GameplayInspection_%08X"),Setup));
            }
            Player.Scale=.5f;Client->Session->WorldObjects.Add(Player.Guid,Player);Gameplay->RefreshExaminationOverlay();
            TestTrue(TEXT("Inspection reframes an unchanged setup after scale changes"),FMath::IsNearlyEqual(Actor->GetActorScale3D().X,.5f,.001f));
            Player.Scale=1.f;Client->Session->WorldObjects.Add(Player.Guid,Player);
        }
        Gameplay->ShowExamination(false);Gameplay->RefreshExaminationOverlay();
        TestEqual(TEXT("Closing inspection hides its model"),Gameplay->ExamPaperDollModelImage->GetVisibility(),ESlateVisibility::Collapsed);
        TestFalse(TEXT("Closed inspection stops spending time on invisible animation"),
            Gameplay->ExamPaperDollPreviewActor->FindComponentByClass<UACECharacterAppearanceComponent>()->IsComponentTickEnabled());
        Gameplay->ReleaseExamPaperDollPreview();Gameplay->LastAppraisal=BeforeModelAppraisal;
        Gameplay->bShowPaperdollSlots=false; Gameplay->RefreshPaperDollPreview();
        if (TestNotNull(TEXT("Inventory constructs a renderable equipped-player preview"),Gameplay->PaperDollCapture.Get()))
        {
            TestTrue(TEXT("Human portrait uses retail camera location"),(Gameplay->PaperDollCapture->GetComponentLocation()-Gameplay->PaperDollPreviewActor->GetActorLocation()).Equals(FVector(-12,-240,88),.01));
            TestTrue(TEXT("Portrait camera looks along retail positive Y without tilt"),Gameplay->PaperDollCapture->GetRelativeRotation().Equals(FRotator(0,90,0),.01));
            TestEqual(TEXT("Retail inventory heading is applied to geometry, not the camera actor"),
                Gameplay->PaperDollPreviewActor->FindComponentByClass<UACECharacterAppearanceComponent>()->MeshFacingYawDegrees,191.3679f);
            if (FApp::CanEverRender())
            {
                if (GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();
                Gameplay->PaperDollCapture->CaptureScene(); FlushRenderingCommands();
                TArray<FColor> Pixels; Gameplay->PaperDollRenderTarget->GameThread_GetRenderTargetResource()->ReadPixels(Pixels);
                const int32 W=Gameplay->PaperDollRenderTarget->SizeX,H=Gameplay->PaperDollRenderTarget->SizeY;
                TArray64<uint8> PNG; FImageUtils::PNGCompressImageArray(W,H,Pixels,PNG);
                FFileHelper::SaveArrayToFile(PNG,*(FPaths::ProjectSavedDir()/TEXT("Automation/RetailParity/PaperDollHuman.png")));
                int32 Lit=0; for (const FColor& P:Pixels) if (FMath::Max3(P.R,P.G,P.B)>80) ++Lit;
                TestTrue(TEXT("Retail portrait actually draws the player's geometry"),Lit>500);
                // Inverse opacity is 1 in empty capture pixels and 0 on opaque geometry.
                TArray<FFloat16Color> LinearPixels;
                Gameplay->PaperDollRenderTarget->GameThread_GetRenderTargetResource()->ReadFloat16Pixels(LinearPixels);
                TestTrue(TEXT("Portrait background stays transparent for the DAT panel"),
                    LinearPixels.Num()==W*H && LinearPixels[0].A.GetFloat()>.99f);
                int32 Opaque=0; for (const auto& Pixel:LinearPixels) if (Pixel.A.GetFloat()<.01f) ++Opaque;
                TestTrue(TEXT("Portrait alpha contains visible character geometry"),Opaque>500);
                FWidgetRenderer DollRenderer(true,true);
                auto* DollTarget=FWidgetRenderer::CreateTargetFor(FVector2D(W,H),TF_Bilinear,true);
                DollRenderer.DrawWidget(DollTarget,Gameplay->PaperDollModelImage->TakeWidget(),FVector2D(W,H),0.f);
                FlushRenderingCommands();
                TArray<FColor> DollPixels; DollTarget->GameThread_GetRenderTargetResource()->ReadPixels(DollPixels);
                int32 DollOpaque=0;for(const auto& Pixel:DollPixels) if(Pixel.A>200) ++DollOpaque;
                TArray64<uint8> DollPNG;FImageUtils::PNGCompressImageArray(W,H,DollPixels,DollPNG);
                FFileHelper::SaveArrayToFile(DollPNG,*(FPaths::ProjectSavedDir()/TEXT("Automation/RetailParity/PaperDollComposite.png")));
                TestTrue(TEXT("Portrait widget preserves visible foreground pixels"),DollOpaque>500);
                TestTrue(TEXT("Portrait widget clears the capture background"),DollPixels.Num()==W*H && DollPixels[0].A<3);
                const auto Composite=CaptureScreen(TEXT("GameplayRetailPaperDoll"));
                Gameplay->PaperDollModelImage->SetColorAndOpacity(FLinearColor::Transparent);
                const auto Background=CaptureScreen(TEXT("GameplayPaperDollBackground"));
                const auto El=Manager->FindElementUnder(TEXT("InventoryPanel_Field"),TEXT("PaperDoll"));
                const auto& LayerGeometry=Canvas->GetElementLayer()->GetCachedGeometry();
                int32 Compared=0,Changed=0,ModelChanged=0;
                if (El && Composite.Num()==ScreenSize.X*ScreenSize.Y && Background.Num()==Composite.Num())
                    for (int32 Y=8;Y<El->Height-8;Y+=4) for (int32 X=8;X<El->Width-8;X+=4)
                    {
                        const int32 CaptureX=X*W/El->Width, CaptureY=Y*H/El->Height;
                        const float InverseOpacity=LinearPixels[CaptureY*W+CaptureX].A.GetFloat();
                        const FVector2D Screen=Canvas->GetCachedGeometry().AbsoluteToLocal(
                            LayerGeometry.LocalToAbsolute(FVector2D(El->GetScreenOrigin())+FVector2D(X,Y)));
                        const int32 SX=FMath::RoundToInt(Screen.X),SY=FMath::RoundToInt(Screen.Y);
                        if (SX<0 || SY<0 || SX>=ScreenSize.X || SY>=ScreenSize.Y) continue;
                        const auto C=Composite[SY*ScreenSize.X+SX],B=Background[SY*ScreenSize.X+SX];
                        const bool Differs=FMath::Abs(int32(C.R)-B.R)>2 || FMath::Abs(int32(C.G)-B.G)>2 || FMath::Abs(int32(C.B)-B.B)>2;
                        if (InverseOpacity>.999f) { ++Compared; if (Differs) ++Changed; }
                        else if (InverseOpacity<.01f && Differs) ++ModelChanged;
                    }
                TestTrue(TEXT("The composited widget visibly draws the character over the background"),ModelChanged>50);
                AddInfo(FString::Printf(TEXT("Portrait composite: %d background samples, %d changed, %d visible model samples"),Compared,Changed,ModelChanged));
                TestTrue(TEXT("Portrait composites over the actual panel artwork without a filled rectangle"),Compared>100 && Changed<Compared/20);
                Gameplay->PaperDollModelImage->SetColorAndOpacity(FLinearColor::White);
            }
            FACESelectedObject DollSelection; DollSelection.bValid=true; DollSelection.Guid=Player.Guid;
            DollSelection.SelectionSerial=Gameplay->LastSelection.SelectionSerial+1;
            Gameplay->HandleSelectionChanged(DollSelection);
            TestTrue(TEXT("Selecting the player flashes the equipment model"),Gameplay->FlashMeshes.ContainsByPredicate([&](const auto& Mesh){return Mesh.IsValid() && Mesh->GetOwner()==Gameplay->PaperDollPreviewActor;}));
            Gameplay->SelectionFlashUntil=0; Gameplay->TickSelectionFlash();
            TestTrue(TEXT("Selection flash restores all original materials"),Gameplay->FlashMeshes.IsEmpty());
            Player.Scale=.7f; Client->Session->WorldObjects.Add(Player.Guid,Player); Gameplay->RefreshPaperDollPreview();
            TestTrue(TEXT("Short characters retain the fixed retail camera distance"),
                (Gameplay->PaperDollCapture->GetComponentLocation()-Gameplay->PaperDollPreviewActor->GetActorLocation()).Equals(FVector(-12,-240,88),.01));
            TestTrue(TEXT("Paperdoll geometry retains server character scale"),Gameplay->PaperDollPreviewActor->GetActorScale3D().Equals(FVector(.7f),.001));
            Player.Scale=1.f; Client->Session->WorldObjects.Add(Player.Guid,Player);
            Gameplay->LastVitals.HeritageGroup=8; Gameplay->RefreshPaperDollPreview();
            TestTrue(TEXT("Heritage change updates portrait framing without an equipment change"),(Gameplay->PaperDollCapture->GetComponentLocation()-Gameplay->PaperDollPreviewActor->GetActorLocation()).Equals(FVector(-12,-340,100),.01));
        }
        // Exercise actual DAT mask coordinates through drag/drop and the network writer.
		FSocket* EquipmentReceiver = Sockets->CreateSocket(NAME_DGram, TEXT("Equipment fixture receiver"), false);
		auto EquipmentAddress = Sockets->CreateInternetAddr(); EquipmentAddress->SetIp(TEXT("127.0.0.1"), ValidAddress); EquipmentAddress->SetPort(0);
		if (!EquipmentReceiver || !EquipmentReceiver->Bind(*EquipmentAddress)) return false;
		EquipmentReceiver->GetAddress(*EquipmentAddress);
		Session.SocketC2S = Sockets->CreateSocket(NAME_DGram, TEXT("Equipment fixture sender"), false);
		Session.ServerC2SAddr = EquipmentAddress;
        // Offline command dispatch exercises the same writer as the chat entry.
        auto Dispatch=[&](const TCHAR* Command,uint32 Action)
        {
            Session.CachedC2SPackets.Reset();Gameplay->TryDispatchChatCommand(Command,nullptr,nullptr);
            TArray<uint8> Body;bool FoundAction=false;
            for (const auto& Pair:Session.CachedC2SPackets)
            {
                FACEBinaryReader Wire(Pair.Value.Payload);Wire.Skip(16);
                if (Wire.ReadUInt32()!=ACEOpcode::GameAction) continue;
                Wire.ReadUInt32();if (Wire.ReadUInt32()!=Action) continue;FoundAction=true;
                Body.Append(Pair.Value.Payload.GetData()+28,Pair.Value.Payload.Num()-28);
            }
            TestTrue(*FString::Printf(TEXT("%s sends action %04X"),Command,Action),FoundAction);
            return Body;
        };
        Session.LastTellSenderGuid=0x50000002;Session.LastTellSenderName=TEXT("Remote Friend");
        auto ReplyBytes=Dispatch(TEXT("/r hello"),ACEGameAction::Tell);
        FACEBinaryReader ReplyWire(ReplyBytes);
        TestEqual(TEXT("Reply uses name-based tells across landblocks"),ReplyWire.ReadString16L(),FString(TEXT("hello")));
        TestEqual(TEXT("Reply names the last player teller"),ReplyWire.ReadString16L(),FString(TEXT("Remote Friend")));
        auto SquelchBytes=Dispatch(TEXT("/squelch Remote Friend"),ACEGameAction::ModifyCharacterSquelch);
        FACEBinaryReader SquelchWire(SquelchBytes);
        TestEqual(TEXT("Squelch enable flag"),SquelchWire.ReadUInt32(),1u);SquelchWire.ReadUInt32();
        TestEqual(TEXT("Squelch preserves a multiword name"),SquelchWire.ReadString16L(),FString(TEXT("Remote Friend")));
        TestEqual(TEXT("Squelch defaults to retail AllChannels, not Broadcast"),SquelchWire.ReadUInt32(),1u);
        auto UnsquelchBytes=Dispatch(TEXT("/unsquelch -reply -tell"),ACEGameAction::ModifyCharacterSquelch);
        FACEBinaryReader UnsquelchWire(UnsquelchBytes);TestEqual(TEXT("Unsquelch disable flag"),UnsquelchWire.ReadUInt32(),0u);
        UnsquelchWire.ReadUInt32();UnsquelchWire.ReadString16L();TestEqual(TEXT("Squelch category reaches server"),UnsquelchWire.ReadUInt32(),3u);
        auto NoTellBytes=Dispatch(TEXT("/notell on"),ACEGameAction::ModifyGlobalSquelch);
        FACEBinaryReader NoTellWire(NoTellBytes);TestEqual(TEXT("Notell on suppresses tells"),NoTellWire.ReadUInt32(),1u);
        TestEqual(TEXT("Notell sends Tell category"),NoTellWire.ReadUInt32(),3u);
        auto ChannelBytes=Dispatch(TEXT("/f hello"),ACEGameAction::ChatChannel);
        FACEBinaryReader ChannelWire(ChannelBytes);TestEqual(TEXT("Fellow shortcut sends the real channel id"),ChannelWire.ReadUInt32(),ACEChatChannel::Fellow);
        auto OnBytes=Dispatch(TEXT("/on help"),ACEGameAction::AddChannel);
        FACEBinaryReader OnWire(OnBytes);TestEqual(TEXT("Channel membership uses Help id"),OnWire.ReadUInt32(),1024u);
        Dispatch(TEXT("/index"),ACEGameAction::IndexChannels);
        Dispatch(TEXT("/clist help"),ACEGameAction::ListChannels);
        Dispatch(TEXT("/off help"),ACEGameAction::RemoveChannel);
        Dispatch(TEXT("/motd"),0x0255);
        Dispatch(TEXT("/motd set Fixture welcome"),0x0254);
        Dispatch(TEXT("/allegiance name set Fixture guild"),0x0033);
        Dispatch(TEXT("/allegiance ban list"),0x02A3);
        Dispatch(TEXT("/allegiance ban add Remote Friend"),0x02A1);
        Dispatch(TEXT("/allegiance ban remove Remote Friend"),0x02A2);
        Dispatch(TEXT("/allegiance chat gag Remote Friend"),0x0041);
        Dispatch(TEXT("/allegiance chat kick Remote Friend, Fixture reason"),0x02A0);
        Dispatch(TEXT("/allegiance lock bypass Remote Friend"),0x0040);
        Dispatch(TEXT("/allegiance lock check"),0x003F);
        Dispatch(TEXT("/allegiance house guest open"),0x0042);
        auto OfficerBytes=Dispatch(TEXT("/allegiance officer add 2 Remote Friend"),0x003B);
        FACEBinaryReader OfficerWire(OfficerBytes);TestEqual(TEXT("Officer action puts name before level"),OfficerWire.ReadString16L(),FString(TEXT("Remote Friend")));
        TestEqual(TEXT("Officer level is serialized"),OfficerWire.ReadUInt32(),2u);
        auto TitleBytes=Dispatch(TEXT("/allegiance title set 3 Steward"),0x003C);
        FACEBinaryReader TitleWire(TitleBytes);TestEqual(TEXT("Officer title action puts level before title"),TitleWire.ReadUInt32(),3u);
        TestEqual(TEXT("Officer title is serialized"),TitleWire.ReadString16L(),FString(TEXT("Steward")));
        // Use the actual General dropdown destination and entry dispatch, not
        // only the /cg shortcut. The server supplies the room id at login.
        const uint32 SavedOptions=Session.CharacterOptions2,SavedGeneral=Session.TurbineGeneralChannel;
        Session.CharacterOptions2=~0u;Session.TurbineGeneralChannel=0x4321;
        Gameplay->ChatSendChannel=7;Session.CachedC2SPackets.Reset();
        const FString GeneralText=TEXT("hey");
        TestTrue(TEXT("General dropdown sends from the chat entry"),Gameplay->TrySendChatFromEntry(&GeneralText,0));
        bool GeneralPacket=false,LocalSay=false;
        for (const auto& Pair:Session.CachedC2SPackets)
        {
            FACEBinaryReader Wire(Pair.Value.Payload);Wire.Skip(16);const uint32 Opcode=Wire.ReadUInt32();
            LocalSay |= Opcode==ACEOpcode::GameAction;
            if (Opcode!=ACEOpcode::TurbineChat) continue;
            GeneralPacket=true;Wire.Skip(48);
            TestEqual(TEXT("General uses the server-provided Turbine room"),Wire.ReadUInt32(),0x4321u);
            TestEqual(TEXT("General sends raw message text without local-speech markup"),Wire.ReadPackedUnicode(),GeneralText);
        }
        TestTrue(TEXT("General is serialized as F7DE"),GeneralPacket);
        TestFalse(TEXT("General does not send a local speech action"),LocalSay);
        Session.CharacterOptions2=SavedOptions;Session.TurbineGeneralChannel=SavedGeneral;
        Gameplay->GlobalChatTypeFilter=~0ull;Gameplay->ChatFilterMode=0;
        Gameplay->HandleChatMessage(TEXT("[General] Remote Friend says, \"hey\""),TEXT("Remote Friend"),ACEChatMessageType::General);
        FString LastDisplayed;
        TFunction<void(UWidget*)> ReadText=[&](UWidget* Widget)
        {
            if (auto* Label=Cast<UTextBlock>(Widget)) LastDisplayed+=Label->GetText().ToString();
            if (auto* Panel=Cast<UPanelWidget>(Widget)) for (auto* Child:Panel->GetAllChildren()) ReadText(Child);
        };
        if (Gameplay->ChatLog && Gameplay->ChatLog->GetChildrenCount()) ReadText(Gameplay->ChatLog->GetChildAt(Gameplay->ChatLog->GetChildrenCount()-1));
        TestTrue(TEXT("General displays one formatted server message"),LastDisplayed.Contains(TEXT("[General] Remote Friend says, \"hey\"")));
        TestFalse(TEXT("General is not nested in a local say message"),LastDisplayed.Contains(TEXT("says, \"[General]")));
        Gameplay->CancelPendingUseWith(); Gameplay->ShowExamination(false);
        Gameplay->ShowPanelPage(TEXT("InventoryPanel_Field")); Gameplay->bShowPaperdollSlots=false;
        FACEWorldObject Shirt; Shirt.Guid=9901; Shirt.Name=TEXT("Undershirt"); Shirt.ItemType=ACEItemType::Clothing;
        Shirt.WielderId=Player.Guid; Shirt.CurrentWieldedLocation=2; Shirt.ValidLocations=2; Shirt.IconId=0x060011CF;
        FACEWorldObject Armor=Shirt; Armor.Guid=9902; Armor.Name=TEXT("Chest armor"); Armor.ItemType=ACEItemType::Armor;
        Armor.CurrentWieldedLocation=0x200; Armor.ValidLocations=0x200;
        Session.WorldObjects.Add(Shirt.Guid,Shirt); Session.WorldObjects.Add(Armor.Guid,Armor);
        Gameplay->SelectInventoryGuid(Armor.Guid);
        Gameplay->RefreshSelectionOverlay();
        Gameplay->bShowPaperdollSlots=true; Gameplay->RefreshInventoryOverlays();
        TestTrue(TEXT("Equipment checkbox draws the retail checked glyph above the model"),Gameplay->PaperdollSlotsCheckboxIcon && Gameplay->PaperdollSlotsCheckboxIcon->GetVisibility()!=ESlateVisibility::Collapsed && Gameplay->PaperdollSlotsCheckboxIcon->Background.GetResourceObject()==Resources->ResolveIconTexture(0x06004D17));
        CaptureScreen(TEXT("GameplayEquipmentSelected"));
        bool bSelectedArtwork=false;
        for (const auto& Pair:Gameplay->PaperDollSelectedIcons)
            if (Pair.Value && Pair.Value->GetVisibility()!=ESlateVisibility::Collapsed)
                bSelectedArtwork |= Pair.Value->Background.GetResourceObject()==Resources->ResolveIconTexture(0x06004D09);
        TestTrue(TEXT("Selected equipment uses the authored selection frame"),bSelectedArtwork);
        Gameplay->bShowPaperdollSlots=false;
        Gameplay->RefreshInventoryOverlays(); CaptureScreen(TEXT("GameplayEquipmentModelTargets"));
        TestTrue(TEXT("Model mode draws the retail unchecked glyph"),Gameplay->PaperdollSlotsCheckboxIcon && Gameplay->PaperdollSlotsCheckboxIcon->Background.GetResourceObject()==Resources->ResolveIconTexture(0x06004D15));
        TestEqual(TEXT("Model chest selects armor above the undershirt"),Gameplay->FindUpperEquippedItem(514),Armor.Guid);
        auto MaskElement=Manager->FindElementUnder(TEXT("InventoryPanel_Field"),TEXT("PaperDollDragMask"));
        if (TestTrue(TEXT("Authored body silhouette target exists"),MaskElement.IsValid()))
        {
            FIntPoint Pixel(-1,-1);
            for (int32 Y=0;Y<MaskElement->Height && Pixel.X<0;++Y)
                for (int32 X=0;X<MaskElement->Width;++X)
                    if (Resources->ResolvePaperDollSelectionMask(FIntPoint(X,Y))==514) { Pixel=FIntPoint(X,Y);break; }
            TestTrue(TEXT("The authored click map has a chest target"),Pixel.X>=0);
            const FVector2D Absolute=Canvas->GetElementLayer()->GetCachedGeometry().LocalToAbsolute(FVector2D(MaskElement->GetScreenOrigin()+Pixel)+FVector2D(.5,.5));
            FString SlotName; int64 Mask=0;
            TestTrue(TEXT("Live model hit testing reaches the DAT silhouette"),Gameplay->HitTestDollSlot(Absolute,SlotName,Mask));
            TestEqual(TEXT("Hidden slot boxes cannot intercept model selection"),SlotName,FString(TEXT("PaperDoll")));
            TestEqual(TEXT("Body click selects both chest layers"),Mask,int64(514));
            const FVector2D Local=Canvas->GetCachedGeometry().AbsoluteToLocal(Absolute);
            Gameplay->CancelInventoryDrag();
            TestTrue(TEXT("Chest armor can be dragged from the 3D model"),Gameplay->TryBeginInventoryDrag(Local));
            TestEqual(TEXT("Drag chooses the outermost equipped item"),Gameplay->InvDragGuid,Armor.Guid);
            Gameplay->CancelInventoryDrag();
            Armor.WielderId=0; Armor.ContainerId=Player.Guid; Armor.CurrentWieldedLocation=0;
            Session.WorldObjects.Add(Armor.Guid,Armor); Session.CachedC2SPackets.Reset();
            Gameplay->InvDragGuid=Armor.Guid; Gameplay->bInvDragPending=true; Gameplay->bInvDragActive=true;
            Gameplay->TryFinishInventoryDrag(Local);
			bool bSentWield = false;
			for (const auto& Pair : Session.CachedC2SPackets)
			{
				FACEBinaryReader Wire(Pair.Value.Payload); Wire.Skip(16);
				if (Wire.ReadUInt32() != ACEOpcode::GameAction) continue;
				Wire.ReadUInt32();
				if (Wire.ReadUInt32() == ACEGameAction::GetAndWieldItem)
					bSentWield |= Wire.ReadUInt32() == uint32(Armor.Guid) && Wire.ReadUInt32() == 0x200u;
			}
			TestTrue(TEXT("Dropping armor on the model sends the item and full wield mask"), bSentWield);
        }
		Session.SocketC2S->Close(); Sockets->DestroySocket(Session.SocketC2S); Session.SocketC2S = nullptr;
		EquipmentReceiver->Close(); Sockets->DestroySocket(EquipmentReceiver);
        Gameplay->ReleasePaperDollPreview(); Gameplay->PlayerController=nullptr; Controller->Client=nullptr;
        PreviewGI->Shutdown(); GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
    }

    // Match the user's magic -> peace -> inventory/critter death -> GC sequence.
    // Unplaced widgets are not owned by WidgetTree simply because it is their Outer.
    {
        TArray<TStrongObjectPtr<UObject>> Roots;
        for (UObject* Object : TArray<UObject*>{GI,Dat,Resources,Manager,Layout,Canvas,Binder,Client,Gameplay}) Roots.Emplace(Object);
        TArray<TWeakObjectPtr<UBorder>> Icons;
        for (UBorder* Icon : Gameplay->BuiltInSpellIconBorders) { Icon->RemoveFromParent(); Icons.Add(Icon); }
        Gameplay->CombatMode=1;
        auto* Video=Cast<UACEVideoSettingsWidget>(Gameplay->VideoSettings);
        UWidget* Generated=Video->GenerateOption(TEXT("High"));
        TWeakObjectPtr<UWidget> OptionLifetime=Generated;
        auto OptionSlate=Generated->TakeWidget();
        CollectGarbage(RF_NoFlags);
        TestTrue(TEXT("Dropdown text retained by an open Slate menu survives GC"),OptionLifetime.IsValid());
        if (auto* OptionRow=Cast<UACERetailOptionWidget>(OptionLifetime.Get()))
            TestEqual(TEXT("Dropdown keeps the chosen value after GC"),Cast<UTextBlock>(OptionRow->WidgetTree->RootWidget)->GetText().ToString(),FString(TEXT("High")));
        bool Alive=true;
        for (const auto& Icon : Icons) Alive &= Icon.IsValid();
        TestTrue(TEXT("Innate spell widget cache survives garbage collection when unplaced"),Alive);
        if (Alive) Gameplay->RefreshSpellHotbarOverlays();
    }
    {
        constexpr int32 Frames=120;
        const double Start=FPlatformTime::Seconds();
        for (int32 I=0; I<Frames; ++I) Gameplay->TickRefresh();
        const double BindEnd=FPlatformTime::Seconds();
        for (int32 I=0; I<Frames; ++I) Canvas->NativeTick(Canvas->GetCachedGeometry(),0.f);
        AddInfo(FString::Printf(TEXT("HUD CPU fixture: binder %.3f ms/frame; total HUD tick %.3f ms/frame"),
            (BindEnd-Start)*1000/Frames,(FPlatformTime::Seconds()-BindEnd)*1000/Frames));
    }
    Gameplay->Shutdown(); Layout->Shutdown(); Manager->Shutdown();
    return true;
}
#endif

