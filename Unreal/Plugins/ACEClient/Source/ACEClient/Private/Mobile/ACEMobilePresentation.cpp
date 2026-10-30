#include "Mobile/ACEMobileWidget.h"
#include "ACEPlayerController.h"
#include "ACEClientSubsystem.h"
#include "ACEDatSubsystem.h"
#include "ACESession.h"
#include "UI/ACEUIGameplayBinder.h"
#include "UI/ACEUIResourceResolver.h"
#include "Mods/ACEPluginIcon.h"
#include "Algo/AnyOf.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Rendering/DrawElements.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
const FLinearColor MobileControlInk(.018f,.029f,.043f),White(.95f,.96f,.96f),Muted(.65f,.74f,.79f),Accent(.26f,.83f,.76f),MobileControlEdge(.45f,.56f,.60f,.65f);
uint32 ModeIcon(int32 Mode)
{return Mode==2?0x06004CEE:Mode==4?0x06004CF0:Mode==8?0x06004CF2:0x06004CEC;}
// End-of-retail classic_gameplay / RootGameplay_FloatyVitals_Field:
// meter_background and meter_foreground left/middle/right pieces (16px high).
constexpr uint32 VitalArt[3][6]={
    {0x0600747E,0x0600747F,0x06007480,0x06007481,0x06007482,0x06007483},
    {0x06007484,0x06007485,0x06007486,0x06007487,0x06007488,0x06007489},
    {0x0600748A,0x0600748B,0x0600748C,0x0600748D,0x0600748E,0x0600748F}};
}

void UACEMobileWidget::RefreshPresentation()
{
    auto* R=Client?Client->GetUIResourceResolver():nullptr;
    if(!R)return;
    for(const auto& BarArt:VitalArt)for(uint32 Id:BarArt)
        if(!VitalTextures.FindRef(Id))if(auto* Texture=R->ResolveTexture(Id))VitalTextures.Add(Id,Texture);
    auto* Dat=R->GetDatSubsystem();auto* Binder=PC?PC->GetActionDeckBinder():nullptr;
    const auto& Bar=Client->GetSpellBar(Client->GetActiveSpellBar());
    // Read the same selection CastSelectedHotbarSpell consumes. The touch wheel
    // is only one way to change it; desktop hotbars and plugins can also select.
    const int32 Selected=Binder?(Binder->SelectedCombatSpellSlot<0?Binder->BuiltInSpellId:
        (Bar.IsValidIndex(Binder->SelectedCombatSpellSlot)?Bar[Binder->SelectedCombatSpellSlot]:0)):0;
    SelectedSpell=Selected;
    const int32 Mode=Binder?Binder->CombatMode:1;
    const int32 AttackMode=Mode==1 && Binder?Binder->ResolveEquippedCombatMode():Mode;
    auto Resource=[&](FACEMobileActionVisual& V,uint32 Id){V.ResourceId=Id;V.Icon=R->ResolveTexture(Id);};
    auto Spell=[&](FACEMobileActionVisual& V,int32 Id)
    {
        V.SpellId=Id;V.Available=Id!=0;V.Detail=TEXT("Choose a spell");
        if(Id){uint32 Did=0;if(Dat)Dat->TryGetSpellInfo(Id,V.Detail,Did);V.ResourceId=Did;V.Icon=R->ResolveSpellIcon(Id);}
    };
    auto Item=[&](FACEMobileActionVisual& V,int32 Id)
    {
        V.ObjectId=Id;FACEWorldObject Object;V.Available=Client->GetWorldObject(Id,Object);
        if(Id!=0 && Id==Client->GetPlayerGuid()){Resource(V,0x06004CF7);V.Detail=TEXT("Target self");V.Available=true;return;}
        if(!V.Available){V.Detail=TEXT("Empty shortcut");return;}
        V.Detail=Object.Name;V.ResourceId=Object.IconId;
        V.Background=R->ResolveItemBackground(Object.ItemType,Object.IconUnderlayId);
        V.Icon=R->ResolveItemForeground(Object.IconId,Object.IconOverlayId,Object.UiEffects,false,Object.Structure,Object.MaxStructure);
        if(Object.StackSize>1)V.Badge=FString::FromInt(Object.StackSize);
    };
    TSet<FName> Names={"Attack","Cast","Combat","Inventory","Inspect","Chat","Plugins","Menu","Spell wheel","Equipment","Spellbook","Hotbars","Character","Fellowship","Allegiance","Loot","Vendor","Trade","Salvage","Options","Touch settings","Video","Combat settings"};
    for(const auto& C:Layout)if(C.bVisible)Names.Add(C.Action);
    for(FName Name:Names)
    {
        FACEMobileActionVisual V;V.Label=Name.ToString();V.Detail=V.Label;
        const FString Action=Name.ToString();
        if(Name=="Combat"){Resource(V,ModeIcon(Mode));V.Label=Mode==1?TEXT("Peace"):Mode==2?TEXT("Melee"):Mode==4?TEXT("Missile"):TEXT("Magic");V.Detail=TEXT("Change combat mode");}
        else if(Name=="Cast" || Name=="Attack")
        {
            if(Name=="Cast" || AttackMode==8){Spell(V,Selected);V.Label=TEXT("Cast");}
            else {Resource(V,ModeIcon(AttackMode));V.Detail=AttackMode==4?TEXT("Missile attack"):TEXT("Melee attack");}
        }
        else if(Action.StartsWith(TEXT("Shortcut ")))
        {const int32 SlotIndex=FCString::Atoi(*Action.Mid(9))-1;Item(V,Client->GetShortcutObject(SlotIndex));V.Label=FString::Printf(TEXT("Item %d"),SlotIndex+1);}
        else if(Action.StartsWith(TEXT("Spell ")) && Name!="Spell wheel")
        {const int32 SlotIndex=FCString::Atoi(*Action.Mid(6))-1;Spell(V,Bar.IsValidIndex(SlotIndex)?Bar[SlotIndex]:0);V.Label=FString::Printf(TEXT("Spell %d"),SlotIndex+1);}
        else if(Name=="Use" || Name=="Pick up")
        {FACEWorldObject Object;if(Client->GetWorldObject(Client->GetSelectedObject().Guid,Object) && Object.IconId)Item(V,Object.Guid);else Resource(V,0x06001129);}
        else if(Name=="Inspect")Resource(V,0x06001127);
        else if(Name=="Inventory" || Name=="Loot" || Name=="Self"){Resource(V,0x06004CF7);if(Name=="Inventory")V.Label=TEXT("Bag");}
        else if(Name=="Spell wheel" || Name=="Spellbook" || Name=="Hotbars")
        {Resource(V,0x06001119);if(Name=="Spell wheel")V.Label=TEXT("Spells");}
        else if(Name=="Chat" || Name=="Fellowship" || Name=="Allegiance" || Name=="Trade")Resource(V,0x0600111F);
        else if(Name=="Character" || Name=="Equipment" || Name=="Salvage")Resource(V,0x06001122);
        else if(Name=="Combat settings")Resource(V,ModeIcon(2));
        else if(Name=="Next target" || Name=="Previous target"){V.Label=Name=="Next target"?TEXT("Next"):TEXT("Prev");}
        else if(Name=="Jump" || Name=="Walk")
        {FString SkillName;uint32 Did=0;if(Dat && Dat->TryGetSkillInfo(Name=="Jump"?22:24,SkillName,Did))Resource(V,Did);}
        else if(Name!="Move" && Name!="Turn" && Name!="Cancel")Resource(V,0x0600111C);
        ActionVisuals.Add(Name,MoveTemp(V));
    }
    // Only visible bindings and the fixed menu set retain textures.
    for(auto It=ActionVisuals.CreateIterator();It;++It)if(!Names.Contains(It.Key()))It.RemoveCurrent();
}

TSharedRef<SWidget> UACEMobileWidget::MenuTile(FName Page,TFunction<void()> Click)
{
    static const FButtonStyle Style=FButtonStyle()
        .SetNormal(FSlateRoundedBoxBrush(MobileControlInk,12,MobileControlEdge,1))
        .SetHovered(FSlateRoundedBoxBrush(FLinearColor(.035f,.10f,.13f),12,Accent,1))
        .SetPressed(FSlateRoundedBoxBrush(FLinearColor(.06f,.20f,.22f),12,Accent,2));
    TSharedPtr<FSlateBrush> Brush=MakeShared<FSlateBrush>();Brush->ImageSize=FVector2D(32,32);
    if(const auto* Visual=ActionVisuals.Find(Page))if(Visual->Icon)
    {Brush->SetResourceObject(Visual->Icon);Brush->ImageSize=FVector2D(Visual->Icon->GetSizeX(),Visual->Icon->GetSizeY());}
    TSharedRef<SWidget> Icon=Page=="Plugins"?StaticCastSharedRef<SWidget>(SNew(SACEPluginIcon).Id(TEXT("mobile.plugins"))):
        StaticCastSharedRef<SWidget>(SNew(SScaleBox).Stretch(EStretch::ScaleToFit)[SNew(SImage).Image_Lambda([Brush]()->const FSlateBrush*{return Brush.Get();})]);
    return SNew(SBox).MinDesiredHeight(68)[SNew(SButton).Tag(Page).ButtonStyle(&Style).IsFocusable(false)
        .TouchMethod(EButtonTouchMethod::PreciseTap).ContentPadding(10).OnClicked_Lambda([Click=MoveTemp(Click)](){Click();return FReply::Handled();})
        [SNew(SHorizontalBox)
            +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,10,0)[SNew(SBox).WidthOverride(32).HeightOverride(32)[Icon]]
            +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[SNew(STextBlock).Text(FText::FromName(Page))
                .Font(FCoreStyle::GetDefaultFontStyle("Regular",20)).ColorAndOpacity(White).AutoWrapText(true)]]];
}

int32 UACEMobileWidget::Paint(const FGeometry& G,FSlateWindowElementList& Out,int32 Layer)const
{
    if(!Client || Client->GetSessionState()!=EACESessionState::InWorld || !Settings)return Layer;
    const float Scale=G.GetLocalSize().Y/ViewSize.Y;
    auto Geo=[&](FVector2D P,FVector2D S){return G.ToPaintGeometry(FVector2f(S*Scale),FSlateLayoutTransform(FVector2f(P*Scale)));};
    auto Round=[&](FVector2D P,FVector2D S,FLinearColor Fill,float Radius,FLinearColor Border=MobileControlEdge,float Thickness=1.f)
    {FSlateRoundedBoxBrush Brush(Fill,Radius*Scale,Border,Thickness*Scale);FSlateDrawElement::MakeBox(Out,Layer,Geo(P,S),&Brush,ESlateDrawEffect::None,Fill);};
    auto ImageInBox=[&](FVector2D P,FVector2D Size,UTexture2D* Texture,float Alpha=1.f)
    {
        if(!Texture)return;
        const FVector2D Native(Texture->GetSizeX(),Texture->GetSizeY());
        const auto R=ACEMobile::FitImage(Native,FSlateRect(P.X,P.Y,P.X+Size.X,P.Y+Size.Y));
        FSlateBrush Brush;Brush.SetResourceObject(Texture);Brush.ImageSize=Native;
        FSlateDrawElement::MakeBox(Out,Layer+1,Geo({R.Left,R.Top},R.GetSize()),&Brush,ESlateDrawEffect::None,FLinearColor(1,1,1,Alpha));
    };
    auto Image=[&](FVector2D P,float Size,UTexture2D* Texture,float Alpha=1.f){ImageInBox(P,{Size,Size},Texture,Alpha);};
    auto Text=[&](FVector2D P,FVector2D Size,const FString& Value,float Font=18,FLinearColor Color=White,bool Center=true,bool Fit=false)
    {
        if(Value.IsEmpty())return;
        auto Measure=FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
        FString Display=Value;auto FontInfo=FCoreStyle::GetDefaultFontStyle("Bold",FMath::Max(1,FMath::RoundToInt(Font*Scale)));
        FVector2D Extent=Measure->Measure(Display,FontInfo)/Scale;
        // Short control labels should survive font pixel rounding at every UI
        // scale. Keep long rebindings readable by limiting shrink before ellipsis.
        const int32 MinimumFont=FMath::Max(1,FMath::FloorToInt(FontInfo.Size*.8f));
        while(Fit && (Extent.X>Size.X || Extent.Y>Size.Y) && FontInfo.Size>MinimumFont)
        {--FontInfo.Size;Extent=Measure->Measure(Display,FontInfo)/Scale;}
        while(Extent.X>Size.X && Display.Len()>3){Display.LeftChopInline(Display.EndsWith(TEXT("..."))?4:1);Display+=TEXT("...");Extent=Measure->Measure(Display,FontInfo)/Scale;}
        const FVector2D Offset(Center?(Size.X-Extent.X)*.5:0,(Size.Y-Extent.Y)*.5);
        // Text remains opaque even when the user makes the control backplates translucent.
        FSlateDrawElement::MakeText(Out,Layer+3,Geo(P+Offset,{1,1}),Display,FontInfo,ESlateDrawEffect::None,Color);
    };
    auto Lines=[&](TArray<FVector2D> Points,FLinearColor Color,float Width=2.f)
    {for(auto& P:Points)P*=Scale;FSlateDrawElement::MakeLines(Out,Layer+2,G.ToPaintGeometry(),Points,ESlateDrawEffect::None,Color,true,Width*Scale);};
    auto Key=[&](FVector2D P,FVector2D S,const FString& Label,bool Active=false)
    {Round(P,S,Active?FLinearColor(.04f,.19f,.20f,.95f):FLinearColor(.018f,.029f,.043f,.92f),12,Active?Accent:MobileControlEdge);Text(P+FVector2D(6,0),S-FVector2D(12,0),Label);};
    auto Visual=[&](FName Name,FVector2D P,FVector2D Size)
    {
        if(const auto* V=ActionVisuals.Find(Name))
        {ImageInBox(P,Size,V->Background);ImageInBox(P,Size,V->Icon,V->Available?1.f:.4f);return V->Icon!=nullptr;}
        return false;
    };
    if(bEditing)
    {
        Key({8,12},{132,64},TEXT("Done"));Key({154,12},{146,64},TEXT("Rebind"));Key({314,12},{146,64},TEXT("Resize"));Key({ViewSize.X-152,12},{144,64},TEXT("Reset"));
        Text({480,12},{ViewSize.X-648,64},Layout.IsValidIndex(SelectedControl)?Layout[SelectedControl].Action.ToString():TEXT("Tap a control"));
    }
    else
    {
        const auto& V=Client->GetPlayerVitalsView();
        const int32 Current[]={V.Health,V.Stamina,V.Mana},Max[]={V.MaxHealth,V.MaxStamina,V.MaxMana};
        for(int32 I=0;I<3;++I)
        {
            const FVector2D P(10+I*146,12);
            auto Pieces=[&](int32 Start)
            {
                const auto* L=VitalTextures.FindRef(VitalArt[I][Start]).Get();const auto* R=VitalTextures.FindRef(VitalArt[I][Start+2]).Get();
                const float Left=L?32.f*L->GetSizeX()/FMath::Max(1,L->GetSizeY()):8.f,Right=R?32.f*R->GetSizeX()/FMath::Max(1,R->GetSizeY()):8.f;
                const float Widths[]={Left,138-Left-Right,Right};float X=0;
                for(int32 Part=0;Part<3;++Part)
                {
                    if(auto* Texture=VitalTextures.FindRef(VitalArt[I][Start+Part]).Get())
                    {
                        FSlateBrush Brush;Brush.SetResourceObject(Texture);
                        Brush.ImageSize=FVector2D(Texture->GetSizeX(),Texture->GetSizeY())*(32.f/FMath::Max(1,Texture->GetSizeY()))*Scale;
                        if(Part==1)Brush.Tiling=ESlateBrushTileType::Horizontal;
                        FSlateDrawElement::MakeBox(Out,Layer+1,Geo(P+FVector2D(X,0),{Widths[Part],32}),&Brush);
                    }
                    X+=Widths[Part];
                }
            };
            Pieces(0);
            const float Width=138*FMath::Clamp(float(Current[I])/FMath::Max(1,Max[I]),0.f,1.f);
            if(Width>0)
            {
                const auto A=G.LocalToAbsolute(P*Scale),B=G.LocalToAbsolute((P+FVector2D(Width,32))*Scale);
                Out.PushClip(FSlateClippingZone(FSlateRect(A.X,A.Y,B.X,B.Y)));Pieces(3);Out.PopClip();
            }
            Text(P,{138,32},FString::Printf(TEXT("%d / %d"),Current[I],Max[I]),16);
        }
        Text({12,48},{ViewSize.X-464,26},Client->GetSelectedObject().Name,18,White,false);
        const auto& History=Client->GetChatHistory();
        if(!History.IsEmpty())Text({12,76},{ViewSize.X-32,20},History.Last().Text.Replace(TEXT("\n"),TEXT(" ")),14,Muted,false);
        const FName Header[]={"Chat","Inventory","Plugins","Menu"};
        for(int32 I=0;I<4;++I)
        {
            const FVector2D P(ViewSize.X-432+108*I,12);const auto Content=ACEMobile::ButtonContent({100,64});
            Round(P,{100,64},FLinearColor(.018f,.029f,.043f,.85f),Content.Radius,MobileControlEdge,Content.Border);
            if(Header[I]=="Plugins")for(int32 X=0;X<2;++X)for(int32 Y=0;Y<2;++Y)Round(P+FVector2D(37+15*X,7+15*Y),{11,11},FLinearColor(.15f,.42f,.42f),3,Accent);
            else if(Header[I]=="Menu")for(int32 Y=0;Y<3;++Y)Lines({P+FVector2D(36,9+10*Y),P+FVector2D(64,9+10*Y)},White,2);
            else Visual(Header[I],P+FVector2D(Content.Icon.Left,Content.Icon.Top),Content.Icon.GetSize());
            Text(P+FVector2D(Content.Label.Left,Content.Label.Top),Content.Label.GetSize(),Header[I]=="Inventory"?TEXT("Bag"):Header[I].ToString(),Content.Font,White,true,true);
        }
    }
    if(bWheel)
    {
        Round({ViewSize.X/2-340,80},{680,ViewSize.Y-88},FLinearColor(.01f,.02f,.03f,.97f),22);
        for(int32 I=0;I<8;++I)Key({ViewSize.X/2-320+I*80,84},{76,54},FString::FromInt(I+1),Client->GetActiveSpellBar()==I);
        const int32 Count=FMath::Clamp(WheelSpells.Num()-WheelPage*8,0,8);
        const FVector2D Center(ViewSize.X/2,FMath::Max(300.,ViewSize.Y*.5));
        Round(Center-FVector2D(157,157),{314,314},FLinearColor(.025f,.055f,.075f,.7f),157,MobileControlEdge);
        for(int32 I=0;I<Count;++I)
        {
            const float Angle=2*PI*I/FMath::Max(1,Count);const FVector2D P=Center+FVector2D(FMath::Sin(Angle),-FMath::Cos(Angle))*118-FVector2D(32,32);
            const int32 Index=WheelPage*8+I;Round(P,{64,64},MobileControlInk,32,WheelSpells[Index]==SelectedSpell?Accent:MobileControlEdge,2);
            if(WheelIcons.IsValidIndex(Index))Image(P+FVector2D(10,10),44,WheelIcons[Index]);
        }
        Text(Center-FVector2D(80,28),{160,28},Count?TEXT("Select spell"):TEXT("Empty tab"),18);
        Text(Center-FVector2D(80,-2),{160,24},FString::Printf(TEXT("Page %d / %d"),WheelPage+1,FMath::Max(1,FMath::DivideAndRoundUp(WheelSpells.Num(),8))),15,Muted);
        Key({ViewSize.X/2-320,ViewSize.Y-62},{200,54},TEXT("Previous"));Key({ViewSize.X/2-100,ViewSize.Y-62},{200,54},TEXT("Cancel"));Key({ViewSize.X/2+120,ViewSize.Y-62},{200,54},TEXT("Next"));
        return Layer+4;
    }
    for(int32 I=0;I<Layout.Num();++I)
    {
        const auto& C=Layout[I];if(!C.bVisible)continue;
        const FVector2D Center=C.Position*ViewSize,P=Center-FVector2D(C.Size/2,C.Size/2);
        const bool Held=Algo::AnyOf(Fingers,[I](const auto& F){return F.Value.Control==I;});
        const FLinearColor Border=bEditing && I==SelectedControl?White:Held?Accent:MobileControlEdge;
        const FLinearColor Fill=Held?FLinearColor(.04f,.17f,.20f,.95f):FLinearColor(.018f,.029f,.043f,Settings->ControlOpacity);
        if(C.Id=="Turn")
        {
            for(int32 Side:{-1,1})
            {
                const bool Active=Held && Turning()*Side>0;
                const auto Rect=ACEMobile::TurnKeyRect(C.Size,Side);const auto Content=ACEMobile::ButtonContent(Rect.GetSize());
                const FVector2D KeyP=Center+FVector2D(Rect.Left,Rect.Top),Aim=KeyP+FVector2D(Content.Icon.GetCenter());
                const float S=Rect.GetSize().Y/64.f;
                Round(KeyP,Rect.GetSize(),Active?FLinearColor(.04f,.17f,.20f,.95f):Fill,Content.Radius,Active?Accent:Border,Content.Border*(Active?1.6f:1.f));
                Lines({Aim+FVector2D(-Side*6,-10)*S,Aim+FVector2D(Side*6,0)*S,Aim+FVector2D(-Side*6,10)*S},White,2.5f*S);
                Text(KeyP+FVector2D(Content.Label.Left,Content.Label.Top),Content.Label.GetSize(),TEXT("Turn"),Content.Font,Muted,true,true);
            }
            continue;
        }
        if(C.Id=="Move")
        {
            const auto Axes=Movement();
            for(const FVector2D D:{FVector2D(0,0),FVector2D(0,-1),FVector2D(-1,0),FVector2D(1,0),FVector2D(0,1)})
            {
                const auto Rect=ACEMobile::DPadKeyRect(C.Size,D);const auto Content=ACEMobile::ButtonContent(Rect.GetSize());
                const bool Active=Held && FVector2D::DotProduct(D,Axes)>0;
                const FVector2D KeyP=Center+FVector2D(Rect.Left,Rect.Top),K=Center+FVector2D(Rect.GetCenter()),Across(-D.Y,D.X);
                const float S=Rect.GetSize().Y/52.f;
                Round(KeyP,Rect.GetSize(),Active?FLinearColor(.04f,.17f,.20f,.95f):Fill,Content.Radius,Active?Accent:Border,Content.Border*(Active?1.6f:1.f));
                if(D.IsNearlyZero())Text(KeyP,Rect.GetSize(),TEXT("Move"),11*S,Muted,true,true);
                else Lines({K+(-D*5+Across*10)*S,K+D*6*S,K+(-D*5-Across*10)*S},White,2.5f*S);
            }
            continue;
        }
        const auto Content=ACEMobile::ButtonContent({C.Size,C.Size});const float S=C.Size/64.f;
        Round(P,{C.Size,C.Size},Fill,Content.Radius,Border,Content.Border*(Held?1.6f:1.f));
        const auto* V=ActionVisuals.Find(C.Action);
        const FVector2D IconP=P+FVector2D(Content.Icon.Left,Content.Icon.Top),Aim=P+FVector2D(Content.Icon.GetCenter());
        if(!Visual(C.Action,IconP,Content.Icon.GetSize()))
        {
            if(C.Action=="Next target" || C.Action=="Previous target")
            {
                Round(Aim-FVector2D(9,9)*S,FVector2D(18,18)*S,FLinearColor::Transparent,9*S,Muted,1.5f*S);
                for(FVector2D D:{FVector2D(1,0),FVector2D(0,1)})Lines({Aim-D*15*S,Aim-D*7*S},Muted,1.5f*S),Lines({Aim+D*7*S,Aim+D*15*S},Muted,1.5f*S);
            }
            else if(C.Action=="Cancel")
            {Lines({Aim-FVector2D(10,10)*S,Aim+FVector2D(10,10)*S},Muted,2*S);Lines({Aim+FVector2D(10,-10)*S,Aim+FVector2D(-10,10)*S},Muted,2*S);}
            else Text(IconP,Content.Icon.GetSize(),TEXT("+"),FMath::RoundToInt(24*S),Muted);
        }
        Text(P+FVector2D(Content.Label.Left,Content.Label.Top),Content.Label.GetSize(),V?V->Label:C.Action.ToString(),Content.Font,V && !V->Available?Muted:White,true,true);
        if(V && !V->Badge.IsEmpty())Text(IconP,{Content.Icon.GetSize().X,13*S},V->Badge,FMath::RoundToInt(10*S),White,false);
    }
    if(bEditing)Text({12,ViewSize.Y-40},{ViewSize.X-24,32},TEXT("Drag to move. Tap a control to rebind or resize. Controls cannot overlap."),16);
    else if(PC && PC->IsChargingJump() && !IsBlockingMovement())
    {
        const float Width=FMath::Min(480.,ViewSize.X-32),Charge=PC->GetJumpChargeFraction();
        const FVector2D P((ViewSize.X-Width)/2,ViewSize.Y-40);
        Round(P,{Width,34},FLinearColor(.018f,.029f,.043f,.98f),12);
        if(Charge>0)Round(P+FVector2D(3,3),{(Width-6)*Charge,28},FLinearColor(.04f,.38f,.30f),9,FLinearColor::Transparent,0);
        Text(P,{Width,34},FString::Printf(TEXT("Jump charge: %d%%"),FMath::RoundToInt(Charge*100)),16);
    }
    else if(const auto* Ability=ActionVisuals.Find("Attack"))
    {
        const float Width=FMath::Min(480.,ViewSize.X-32);const FVector2D P((ViewSize.X-Width)/2,ViewSize.Y-40);
        Round(P,{Width,34},FLinearColor(.018f,.029f,.043f,.9f),12);
        Image(P+FVector2D(8,4),26,Ability->Icon);
        Text(P+FVector2D(44,0),{Width-56,34},Ability->Detail,16,White,false);
    }
    return Layer+4;
}
