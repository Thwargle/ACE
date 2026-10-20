#include "Mods/ACEPluginStyle.h"
#include "Mods/ACEPluginSubsystem.h"
#include "Mods/ACELedger.h"
#include "ACEClientSubsystem.h"
#include "ACEDatSubsystem.h"
#include "UI/ACEUIFontStyles.h"
#include "UI/ACEUIResourceResolver.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Brushes/SlateColorBrush.h"
#include "Styling/CoreStyle.h"
#include "Rendering/DrawElements.h"

namespace { TWeakPtr<FACEPluginStyle> CurrentStyle; }
TSharedRef<FACEPluginStyle> FACEPluginStyle::Use(UACEPluginSubsystem* Host,const FString& WindowId)
{
    auto& Style=WindowId.IsEmpty()?Host->RetailPluginStyle:Host->WindowPluginStyles.FindOrAdd(WindowId);
    if(!Style)
    {
        auto* C=Host->GetGameInstance()->GetSubsystem<UACEClientSubsystem>();
        Style=MakeShared<FACEPluginStyle>(C?C->GetUIResourceResolver():nullptr);
    }
    Style->Refresh();
    if(!WindowId.IsEmpty())Style->SetWindowAppearance(Host->GetPluginWindowOpacity(WindowId),Host->IsPluginWindowChromeHidden(WindowId));
    CurrentStyle=Style;return Style.ToSharedRef();
}
TSharedRef<FACEPluginStyle> FACEPluginStyle::Current()
{
    if(auto S=CurrentStyle.Pin())return S.ToSharedRef();
    static TSharedRef<FACEPluginStyle> Fallback=MakeShared<FACEPluginStyle>(nullptr);return Fallback;
}
void FACEPluginStyle::AddReferencedObjects(FReferenceCollector& C)
{C.AddReferencedObject(Resolver);C.AddReferencedObject(Atlas);for(auto& Pair:Fonts)C.AddReferencedObject(Pair.Value->Atlas);}
const FSlateBrush* FACEPluginStyle::Image(uint32 Id)
{
    auto& B=Images.FindOrAdd(Id);if(!B)B=MakeShared<FSlateBrush>();
    if(B->GetResourceObject())return B.Get();
    B->DrawAs=ESlateBrushDrawType::Image;
    if(Resolver)if(auto* T=Resolver->ResolveTexture(Id)){B->SetResourceObject(T);B->ImageSize=FVector2D(T->GetSizeX(),T->GetSizeY());}
    return B.Get();
}
const FSlateBrush* FACEPluginStyle::SpellImage(uint32 SpellId)
{
    auto& B=ItemImages.FindOrAdd(FString::Printf(TEXT("spell.%u"),SpellId));
    if(!B)B=MakeShared<FSlateBrush>();
    if(!B->GetResourceObject()&&Resolver)B->SetResourceObject(Resolver->ResolveSpellIcon(SpellId));
    B->DrawAs=B->GetResourceObject()?ESlateBrushDrawType::Image:ESlateBrushDrawType::NoDrawType;
    B->ImageSize=FVector2D(32,32);
    return B.Get();
}
const FSlateBrush* FACEPluginStyle::ItemImage(const FACESupplyEntry& Item,bool Foreground)
{
    const FString Key=Foreground?FString::Printf(TEXT("fg.%d.%d.%d"),Item.Icon,Item.Overlay,Item.Effects)
        :FString::Printf(TEXT("bg.%d.%d"),Item.ItemType,Item.Underlay);
    auto& B=ItemImages.FindOrAdd(Key);if(!B)B=MakeShared<FSlateBrush>();
    if(!B->GetResourceObject()&&Resolver)
    {
        B->SetResourceObject(Foreground?Resolver->ResolveItemForeground(Item.Icon,Item.Overlay,Item.Effects)
            :Resolver->ResolveItemBackground(Item.ItemType,Item.Underlay));
        B->DrawAs=ESlateBrushDrawType::Image;B->ImageSize=FVector2D(32,32);
    }
    return B.Get();
}
FACEPluginStyle::FACEPluginStyle(UACEUIResourceResolver* InResolver):Resolver(InResolver) { InitializeArtwork(); }
void FACEPluginStyle::Refresh()
{
    // A font atlas is not proof that checkbox/slider images have resolved.
    // The login manager can be constructed before asynchronous DAT loading ends.
    if(!DatArtworkLoaded&&Resolver&&Resolver->GetDatSubsystem()&&Resolver->GetDatSubsystem()->IsDatReady())InitializeArtwork();
}
void FACEPluginStyle::InitializeArtwork()
{
    DatArtworkLoaded=Resolver&&Resolver->GetDatSubsystem()&&Resolver->GetDatSubsystem()->IsDatReady();
    const FLinearColor Gold(.65f,.43f,.15f),Ivory(.94f,.89f,.76f);
    Frame=FSlateRoundedBoxBrush(FLinearColor(.008f,.006f,.003f,.96f),0.f,Gold,2.f);
    const FLinearColor TitleColor(.025f,.012f,.004f,1);
    TitleBackground=FSlateColorBrush(TitleColor);
    SectionFrame=FSlateRoundedBoxBrush(TitleColor,0.f,Gold,2.f);
    SectionHeader=FSlateColorBrush(FLinearColor(Gold.R,Gold.G,Gold.B,.25f));
    Background=*Image(0x06004CC2);
    if(!Background.GetResourceObject())Background=FSlateRoundedBoxBrush(FLinearColor(.005f,.004f,.002f,.96f),0.f);
    Button=FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("Button");
    Button.SetNormal(FSlateRoundedBoxBrush(FLinearColor(.19f,.002f,.001f),0.f,Gold,2.f))
        .SetHovered(FSlateRoundedBoxBrush(FLinearColor(.29f,.006f,.002f),0.f,FLinearColor(1,.75f,.25f),2.f))
        .SetPressed(FSlateRoundedBoxBrush(FLinearColor(.075f,.001f,0),0.f,Gold,2.f))
        .SetDisabled(FSlateRoundedBoxBrush(FLinearColor(.035f,.02f,.01f),0.f,FLinearColor(.25f,.18f,.08f),1.f))
        .SetNormalPadding(FMargin(0)).SetPressedPadding(FMargin(0))
        .SetNormalForeground(Ivory).SetHoveredForeground(FLinearColor(1,.85f,.35f)).SetPressedForeground(Ivory);
    DockButton=Button;
    DockButton.SetNormal(FSlateRoundedBoxBrush(FLinearColor(.008f,.005f,.002f,.9f),0.f,FLinearColor(.4f,.26f,.09f),1.f));
    Tab=Button;
    Tab.SetNormal(FSlateRoundedBoxBrush(FLinearColor(.012f,.01f,.007f),0.f,FLinearColor(.24f,.18f,.1f),1.f))
        .SetHovered(FSlateRoundedBoxBrush(FLinearColor(.055f,.039f,.018f),0.f,FLinearColor(.6f,.43f,.2f),1.f))
        .SetPressed(FSlateRoundedBoxBrush(FLinearColor(.16f,.095f,.025f),0.f,FLinearColor(.9f,.66f,.25f),1.f));
    Check=FCoreStyle::Get().GetWidgetStyle<FCheckBoxStyle>("Checkbox");
    auto Small=[this](uint32 Id){auto B=*Image(Id);B.ImageSize=FVector2D(14,14);return B;};
    // Keep Slate's distinct states until each retail image is available. Empty
    // image brushes paint identical squares for both enabled and disabled states.
    const auto Unchecked=Small(0x0600191F),Checked=Small(0x0600191A);
    if(Unchecked.GetResourceObject())Check.SetUncheckedImage(Unchecked).SetUncheckedHoveredImage(Unchecked).SetUncheckedPressedImage(Unchecked);
    if(Checked.GetResourceObject())Check.SetCheckedImage(Checked).SetCheckedHoveredImage(Checked).SetCheckedPressedImage(Checked);
    auto Handle=*Image(0x06001286);Handle.ImageSize=FVector2D(12,20);
    Slider=FCoreStyle::Get().GetWidgetStyle<FSliderStyle>("Slider");Slider.SetBarThickness(4);
    if(const auto* Bar=Image(0x06001285);Bar->GetResourceObject())Slider.SetNormalBarImage(*Bar).SetHoveredBarImage(*Bar);
    if(Handle.GetResourceObject())Slider.SetNormalThumbImage(Handle).SetHoveredThumbImage(Handle);
    Edit=FCoreStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>("NormalEditableTextBox");
    FSlateRoundedBoxBrush Well(FLinearColor(.012f,.008f,.004f),0.f,Gold,1.f);
    Edit.SetBackgroundImageNormal(Well).SetBackgroundImageHovered(Well).SetBackgroundImageFocused(Well).SetBackgroundImageReadOnly(Well).SetForegroundColor(Ivory);
    Combo=FCoreStyle::Get().GetWidgetStyle<FComboButtonStyle>("ComboButton");Combo.SetButtonStyle(Button).SetMenuBorderBrush(Frame);
    Spin=FCoreStyle::Get().GetWidgetStyle<FSpinBoxStyle>("SpinBox");Spin.SetBackgroundBrush(Well).SetHoveredBackgroundBrush(Well)
        .SetActiveFillBrush(Well).SetInactiveFillBrush(Well).SetForegroundColor(Ivory);
    if(Resolver&&Resolver->ResolveFont(0x40000000,Font))Atlas=Resolver->ResolveFontAtlas(Font.ForegroundSurfaceDataID,TintAtlas);
    OriginalFrame=Frame;OriginalBackground=Background;OriginalTitle=TitleBackground;
    OriginalSectionFrame=SectionFrame;OriginalSectionHeader=SectionHeader;
    const float Previous=BackgroundOpacity;BackgroundOpacity=-1;
    if(Previous>=0)SetWindowAppearance(Previous,HideFrame);
}
bool FACEPluginStyle::SetWindowAppearance(float Opacity,bool Frameless)
{
    Opacity=FMath::Clamp(Opacity,0.f,1.f);
    if(BackgroundOpacity==Opacity&&HideFrame==Frameless)return false;
    BackgroundOpacity=Opacity;HideFrame=Frameless;
    auto Fade=[](FSlateBrush& To,const FSlateBrush& From,float Alpha)
    {
        To=From;auto Color=From.TintColor.GetSpecifiedColor();Color.A*=Alpha;To.TintColor=Color;
        Color=From.OutlineSettings.Color.GetSpecifiedColor();Color.A*=Alpha;To.OutlineSettings.Color=Color;
    };
    // Pinning hides chrome, not the backdrop behind the window's contents.
    Fade(Frame,OriginalFrame,Opacity);
    if(Frameless)
    {
        auto Outline=Frame.OutlineSettings.Color.GetSpecifiedColor();Outline.A=0.f;
        Frame.OutlineSettings.Color=Outline;
    }
    Fade(Background,OriginalBackground,Opacity);
    Fade(TitleBackground,OriginalTitle,Opacity);
    Fade(SectionFrame,OriginalSectionFrame,Opacity);
    Fade(SectionHeader,OriginalSectionHeader,Opacity);
    return true;
}
TSharedPtr<FACEPluginFont> FACEPluginStyle::Typeface(int32 Size)
{
    // Resolve the nearest authored size instead of enlarging the smallest atlas.
    const int32 Pixels=FMath::Clamp(FMath::RoundToInt(Size*1.333333f/2)*2,12,24);
    auto& Face=Fonts.FindOrAdd(Pixels);if(!Face)Face=MakeShared<FACEPluginFont>();
    if(!Face->Atlas&&Resolver&&Resolver->ResolveFont(ACEUIFontStyles::FontIdForStyleName(FString::Printf(TEXT("basefont%d"),Pixels)),Face->Font))
        Face->Atlas=Resolver->ResolveFontAtlas(Face->Font.ForegroundSurfaceDataID,Face->Tint);
    return Face;
}
void SACEPluginTab::Construct(const FArguments& A)
{
    Retail=FACEPluginStyle::Current();Selected=A._Selected;
    TSharedRef<SWidget> Content=A._Content.Widget;
    if(Content==SNullWidget::NullWidget)
        Content=SNew(SACEPluginText).Text(A._Text).Justification(ETextJustify::Center)
            .Font_Lambda([Size=A._FontSize]{return FCoreStyle::GetDefaultFontStyle("Regular",FMath::Clamp(Size.Get(14),10,36));})
            .ColorAndOpacity(FLinearColor(.96f,.91f,.8f));
    SButton::Construct(SButton::FArguments().ButtonStyle(&Retail->Tab).IsFocusable(false)
        .ContentPadding(A._ContentPadding).HAlign(HAlign_Center).VAlign(VAlign_Center).OnClicked(A._OnClicked)[Content]);
    // Slate tracks this attribute and repaints retained tabs when selection
    // changes, without polling, reconstructing controls or shifting their size.
    SetAppearPressed(Selected);
}
int32 SACEPluginTab::OnPaint(const FPaintArgs& A,const FGeometry& G,const FSlateRect& Clip,FSlateWindowElementList& Out,int32 Layer,const FWidgetStyle& WidgetStyle,bool Enabled) const
{
    const int32 Top=SButton::OnPaint(A,G,Clip,Out,Layer,WidgetStyle,Enabled);
    if(!IsSelected())return Top;
    const auto Size=G.GetLocalSize();
    // Keep an unmistakable shape cue even with untinted retail bitmap fonts,
    // a hovered sibling, transparent window backgrounds or hidden window chrome.
    FSlateDrawElement::MakeBox(Out,Top+1,G.ToPaintGeometry(FVector2D(FMath::Max(0.,Size.X-4),3),FSlateLayoutTransform(FVector2D(2,Size.Y-3))),
        FCoreStyle::Get().GetBrush("WhiteBrush"),ShouldBeEnabled(Enabled)?ESlateDrawEffect::None:ESlateDrawEffect::DisabledEffect,
        FLinearColor(1,.78f,.3f)*WidgetStyle.GetColorAndOpacityTint());
    return Top+1;
}
void SACEPluginText::Construct(const FArguments& A)
{
    Retail=FACEPluginStyle::Current();Wrap=A._AutoWrapText;WrapAt=A._WrapTextAt;Justification=A._Justification;
    STextBlock::Construct(A);
    // Match the bitmap path when a label uses characters absent from the DAT.
    // Never turn a narrow plugin title, setting or button into stacked fragments.
    SetWrappingPolicy(ETextWrappingPolicy::DefaultWrapping);
    SetClipping(EWidgetClipping::ClipToBounds);
}
float SACEPluginText::BitmapScale() const
{
    // Match Slate's point-to-pixel conversion, including sizes between DAT fonts.
    return FMath::Max(1.f,float(GetFont().Size))*1.333333f/FMath::Max(1u,Face->Font.MaxCharHeight);
}
void SACEPluginText::Layout(float Available) const
{
    const float Limit=WrapAt.Get(0)>0?WrapAt.Get():Wrap.Get(false)?Available:0;
    const int32 W=Limit>0?FMath::Max(1,FMath::FloorToInt(Limit/BitmapScale())):MAX_int32;
    const FString Text=GetText().ToString();
    if(Text!=CachedText||W!=CachedWidth)
    {
        CachedText=Text;CachedWidth=W;
        // Plugin labels wrap at word boundaries. Oversized words retain their
        // natural width and clip to the widget until the user widens the window.
        Lines=ACEDatText::Layout(Face->Font,Text,W,false,/*bBreakLongWords=*/false);
    }
}
bool SACEPluginText::CanDrawNative() const
{
    // Bound font attributes are evaluated after Construct, and can change while
    // a Lua control keeps its Slate identity. Refresh both glyphs and wrapping.
    const int32 Size=GetFont().Size;
    if(!Face||FaceSize!=Size||!Face->Atlas)
    {Face=Retail->Typeface(Size);FaceSize=Size;CachedWidth=-1;CheckedGlyphs.Reset();NativeGlyphs=true;}
    if(!Face->Atlas)return false;
    const FString Text=GetText().ToString();
    if(Text!=CheckedGlyphs)
    {
        CheckedGlyphs=Text;NativeGlyphs=true;
        for(TCHAR C:Text)if(C>32&&!Face->Font.IndexByUnicode.Contains(C)){NativeGlyphs=false;break;}
    }
    // Keep international plugin labels and symbols readable when absent from the retail atlas.
    return NativeGlyphs;
}
FVector2D SACEPluginText::ComputeDesiredSize(float S) const
{
    if(!CanDrawNative())return STextBlock::ComputeDesiredSize(S);
    const auto M=GetMargin();Layout(FMath::Max(0.f,Width-M.Left-M.Right));int32 W=0;
    for(const auto& L:Lines)W=FMath::Max(W,L.Width);
    return FVector2D(W,Lines.Num()*Face->Font.MaxCharHeight)*BitmapScale()+FVector2D(M.Left+M.Right,M.Top+M.Bottom);
}
int32 SACEPluginText::OnPaint(const FPaintArgs& A,const FGeometry& G,const FSlateRect& Clip,FSlateWindowElementList& E,int32 Layer,const FWidgetStyle& Style,bool Enabled) const
{
    if(!CanDrawNative())return STextBlock::OnPaint(A,G,Clip,E,Layer,Style,Enabled);
    // STextBlock is non-ticking. Use the allotted width to reserve wrapped line height
    // on the next prepass without enabling per-frame work on every label.
    if(Wrap.Get(false)&&!FMath::IsNearlyEqual(Width,float(G.GetLocalSize().X)))
    {Width=G.GetLocalSize().X;const_cast<SACEPluginText*>(this)->Invalidate(EInvalidateWidgetReason::Layout);}
    const auto M=GetMargin();const float Available=G.GetLocalSize().X-M.Left-M.Right;Layout(Available);
    const float Scale=BitmapScale();auto* Atlas=Face->Atlas.Get();const auto& DatFont=Face->Font;
    FSlateBrush B;B.DrawAs=ESlateBrushDrawType::Image;B.SetResourceObject(Atlas);
    const FLinearColor Color=(Face->Tint?GetColorAndOpacity().GetColor(Style):FLinearColor::White)*Style.GetColorAndOpacityTint();
    // Rounding each scaled glyph quad independently can squeeze a thin stem
    // out of its sample footprint (notably 'l' at 16pt). Preserve fractional
    // geometry and let the atlas's bilinear sampler retain its coverage.
    // Native/integer pixel sizes still snap for crisp authored bitmap text.
    const float RasterScale=Scale*G.GetAccumulatedLayoutTransform().GetScale();
    const auto Effects=(ShouldBeEnabled(Enabled)?ESlateDrawEffect::None:ESlateDrawEffect::DisabledEffect)
        | (FMath::IsNearlyEqual(RasterScale,FMath::RoundToFloat(RasterScale),1.e-4f)?ESlateDrawEffect::None:ESlateDrawEffect::NoPixelSnapping);
    E.PushClip(FSlateClippingZone(G.GetLayoutBoundingRect().IntersectionWith(Clip)));
    for(int32 N=0;N<Lines.Num();++N)
    {
        const auto& L=Lines[N];float X=M.Left;const float Spare=FMath::Max(0.f,Available-L.Width*Scale);
        if(Justification.Get()==ETextJustify::Center)X+=Spare*.5f;else if(Justification.Get()==ETextJustify::Right)X+=Spare;
        const float Y=M.Top+N*DatFont.MaxCharHeight*Scale;
        for(int32 I=L.Begin;I<L.End;++I)if(const auto* C=ACEDatText::FindChar(DatFont,CachedText[I]))
        {
            X+=int8(C->HorizontalOffsetBefore)*Scale;
            if(C->Width&&C->Height)
            {
                const FVector2D UV(C->OffsetX,C->OffsetY),Size(C->Width,C->Height),AS(Atlas->GetSizeX(),Atlas->GetSizeY());
                B.SetUVRegion(FBox2f(FVector2f(UV/AS),FVector2f((UV+Size)/AS)));
                FSlateDrawElement::MakeBox(E,Layer,G.ToPaintGeometry(Size*Scale,FSlateLayoutTransform(FVector2D(X,Y+int8(C->VerticalOffsetBefore)*Scale))),&B,Effects,Color);
            }
            X+=(C->Width+int8(C->HorizontalOffsetAfter))*Scale;
        }
    }
    E.PopClip();return Layer;
}
