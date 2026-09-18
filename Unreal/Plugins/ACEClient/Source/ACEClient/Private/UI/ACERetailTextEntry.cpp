#include "UI/ACERetailTextEntry.h"
#include "UI/ACERetailTextBlock.h"
#include "Dat/ACEDatTextLayout.h"
#include "Widgets/SLeafWidget.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Engine/Texture2D.h"
#include "InputCoreTypes.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Input/IVirtualKeyboardEntry.h"

class SACERetailTextEntry : public SLeafWidget, public IVirtualKeyboardEntry
{
public:
    SLATE_BEGIN_ARGS(SACERetailTextEntry) {} SLATE_END_ARGS()
    void Construct(const FArguments&, UACERetailTextEntry* InOwner) { Owner=InOwner; SetClipping(EWidgetClipping::ClipToBounds); }
    virtual bool ComputeVolatility() const override { return HasKeyboardFocus(); }
    virtual bool SupportsKeyboardFocus() const override { return true; }
    void ResetSelection() { if (Owner.IsValid()) Cursor=Anchor=Owner->Value.Len(); }
    virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(60,16); }
    virtual FReply OnFocusReceived(const FGeometry&, const FFocusEvent&) override
    {
        bCommitting=false;
        if (Owner.IsValid()) { Original=Owner->Value; Undo.Reset(); bHasUndo=false; }
        return FReply::Handled();
    }
    virtual void OnFocusLost(const FFocusEvent& E) override
    {
        if (bPlatformKeyboardOpen) { bPlatformKeyboardOpen=false; FSlateApplication::Get().ShowVirtualKeyboard(false,E.GetUser()); }
        if (Owner.IsValid() && !bCommitting) Owner->Commit(ETextCommit::OnUserMovedFocus);
    }
    bool bPlatformKeyboardOpen=false;
    uint32 KeyboardUser=0;
    void OpenPlatformKeyboard(uint32 User)
    {
        if (!FPlatformApplicationMisc::RequiresVirtualKeyboard() || bPlatformKeyboardOpen || !Owner.IsValid()) return;
        KeyboardUser=User; Original=Owner->Value; bCommitting=false; bPlatformKeyboardOpen=true;
        FSlateApplication::Get().ShowVirtualKeyboard(true,User,StaticCastSharedRef<SACERetailTextEntry>(AsShared()));
    }
    virtual void SetTextFromVirtualKeyboard(const FText& Text,ETextEntryType Type) override
    {
        // UE's Android application dispatches virtual-keyboard events on the game thread.
        check(IsInGameThread());
        if (!Owner.IsValid() || !bPlatformKeyboardOpen) return;
        Anchor=0; Cursor=Owner->Value.Len(); ReplaceSelection(Text.ToString());
        if (Type!=ETextEntryType::TextEntryUpdated)
        {
            bCommitting=true; bPlatformKeyboardOpen=false;
            if (Type==ETextEntryType::TextEntryCanceled) Owner->Value=Original;
            Owner->Commit(Type==ETextEntryType::TextEntryAccepted ? ETextCommit::OnEnter : ETextCommit::OnCleared);
            FSlateApplication::Get().ClearUserFocus(KeyboardUser,EFocusCause::Cleared);
        }
    }
    virtual void SetSelectionFromVirtualKeyboard(int Start,int End) override
    {
        if (!Owner.IsValid()) return;
        Anchor=FMath::Clamp(Start,0,Owner->Value.Len()); Cursor=FMath::Clamp(End,0,Owner->Value.Len());
        Invalidate(EInvalidateWidgetReason::Paint);
    }
    virtual FText GetText() const override { return Owner.IsValid() ? Owner->GetText() : FText::GetEmpty(); }
    virtual bool GetSelection(int& Start,int& End) override { Start=FMath::Min(Anchor,Cursor); End=FMath::Max(Anchor,Cursor); return true; }
    virtual FText GetHintText() const override { return FText::GetEmpty(); }
    virtual EKeyboardType GetVirtualKeyboardType() const override { return Owner.IsValid() && Owner->bDigitsOnly ? Keyboard_Number : Keyboard_Default; }
    virtual FVirtualKeyboardOptions GetVirtualKeyboardOptions() const override { return {}; }
    virtual bool IsMultilineEntry() const override { return Owner.IsValid() && Owner->bMultiline; }
    TArray<FACEBitmapTextLine> Lines(float Width) const
    {
        if (!Owner.IsValid() || !Owner->FontLabel || !Owner->FontLabel->GetBitmapFont()) return {{0,0,0}};
        auto Result=ACEDatText::Layout(*Owner->FontLabel->GetBitmapFont(),Owner->Value,FMath::Max(1,FMath::FloorToInt(Width-4)),!Owner->bMultiline);
        if (Result.IsEmpty()) Result.Add({0,0,0});
        return Result;
    }
    float Left(const FACEBitmapTextLine& Line, float Width) const
    {
        return Owner->bDigitsOnly ? FMath::Max(2.f,Width-8-Line.Width) : 2.f;
    }
    int32 CursorLine(const TArray<FACEBitmapTextLine>& Rows) const
    {
        for (int32 I=0;I<Rows.Num();++I) if (Cursor<Rows[I].End || I==Rows.Num()-1) return I;
        return 0;
    }
    float Scroll(const TArray<FACEBitmapTextLine>& Rows, float Height, int32 LineHeight) const
    {
        return HasKeyboardFocus() ? FMath::Max(0.f,(CursorLine(Rows)+1)*LineHeight+2-Height) : 0.f;
    }
    int32 Hit(const FGeometry& G, FVector2D Absolute) const
    {
        if (!Owner.IsValid() || !Owner->FontLabel || !Owner->FontLabel->GetBitmapFont()) return 0;
        const auto& Font=*Owner->FontLabel->GetBitmapFont();
        auto Rows=Lines(G.GetLocalSize().X);
        const FVector2D P=G.AbsoluteToLocal(Absolute);
        const float Offset=Scroll(Rows,G.GetLocalSize().Y,Font.MaxCharHeight);
        const int32 R=FMath::Clamp(FMath::FloorToInt((P.Y+Offset-2)/Font.MaxCharHeight),0,Rows.Num()-1);
        float X=Left(Rows[R],G.GetLocalSize().X);
        for (int32 I=Rows[R].Begin;I<Rows[R].End;++I)
        {
            const int32 Advance=ACEDatText::Advance(Font,Owner->Value[I]);
            if (P.X<X+Advance*.5f || Owner->Value[I]=='\n') return I;
            X+=Advance;
        }
        return Rows[R].End;
    }
    virtual FReply OnMouseButtonDown(const FGeometry& G, const FPointerEvent& E) override
    {
        if (E.GetEffectingButton()!=EKeys::LeftMouseButton) return FReply::Unhandled();
        if (!HasKeyboardFocus() && Owner.IsValid() && Owner->bDigitsOnly) { Anchor=0;Cursor=Owner->Value.Len(); }
        else { Cursor=Hit(G,E.GetScreenSpacePosition()); if (!E.IsShiftDown()) Anchor=Cursor; }
        Invalidate(EInvalidateWidgetReason::Paint);
        OpenPlatformKeyboard(E.GetUserIndex());
        return FReply::Handled().SetUserFocus(SharedThis(this)).CaptureMouse(SharedThis(this));
    }
    virtual FReply OnMouseMove(const FGeometry& G, const FPointerEvent& E) override
    {
        if (!HasMouseCapture()) return FReply::Unhandled();
        Cursor=Hit(G,E.GetScreenSpacePosition()); Invalidate(EInvalidateWidgetReason::Paint); return FReply::Handled();
    }
    virtual FReply OnMouseButtonUp(const FGeometry&, const FPointerEvent& E) override
    { return HasMouseCapture() ? FReply::Handled().ReleaseMouseCapture() : FReply::Unhandled(); }
    void ReplaceSelection(FString Insert)
    {
        auto* O=Owner.Get(); if (!O) return;
        Insert.ReplaceInline(TEXT("\r\n"),TEXT("\n")); Insert.ReplaceInline(TEXT("\r"),TEXT("\n"));
        if (!O->bMultiline) Insert.ReplaceInline(TEXT("\n"),TEXT(""));
        if (O->bDigitsOnly) for (TCHAR C:Insert) if (C<'0' || C>'9') return;
        const int32 Begin=FMath::Min(Cursor,Anchor), End=FMath::Max(Cursor,Anchor);
        Insert=Insert.Left(FMath::Max(0,O->MaxLength-(O->Value.Len()-(End-Begin))));
        Undo=O->Value; bHasUndo=true;
        O->Value=O->Value.Left(Begin)+Insert+O->Value.Mid(End);
        Cursor=Anchor=Begin+Insert.Len(); Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
    }
    virtual FReply OnKeyChar(const FGeometry&, const FCharacterEvent& E) override
    {
        if (!E.IsControlDown() && E.GetCharacter()>=32) ReplaceSelection(FString::Chr(E.GetCharacter()));
        return FReply::Handled();
    }
    virtual FReply OnKeyDown(const FGeometry& G, const FKeyEvent& E) override
    {
        auto* O=Owner.Get(); if (!O) return FReply::Unhandled();
        const FKey Key=E.GetKey();
        if (E.IsControlDown() && Key==EKeys::A) { Anchor=0;Cursor=O->Value.Len(); }
        else if (E.IsControlDown() && (Key==EKeys::C || Key==EKeys::X))
        {
            if (Anchor!=Cursor) FPlatformApplicationMisc::ClipboardCopy(*O->Value.Mid(FMath::Min(Cursor,Anchor),FMath::Abs(Cursor-Anchor)));
            if (Key==EKeys::X) ReplaceSelection(FString());
        }
        else if (E.IsControlDown() && Key==EKeys::V) { FString P; FPlatformApplicationMisc::ClipboardPaste(P); ReplaceSelection(P); }
        else if (E.IsControlDown() && Key==EKeys::Z) { if (bHasUndo) { Swap(O->Value,Undo); ResetSelection(); } }
        else if (Key==EKeys::BackSpace || Key==EKeys::Delete)
        {
            if (Cursor==Anchor) { if (Key==EKeys::BackSpace) Anchor=FMath::Max(0,Cursor-1); else Cursor=FMath::Min(O->Value.Len(),Cursor+1); }
            ReplaceSelection(FString());
        }
        else if (Key==EKeys::Escape || Key==EKeys::Tab || (Key==EKeys::Enter && (!O->bMultiline || E.IsControlDown())))
        {
            bCommitting=true;
            if (Key==EKeys::Escape) O->Value=Original;
            O->Commit(Key==EKeys::Escape ? ETextCommit::OnCleared : Key==EKeys::Tab ? ETextCommit::OnUserMovedFocus : ETextCommit::OnEnter);
            return FReply::Handled().ClearUserFocus();
        }
        else if (Key==EKeys::Enter) ReplaceSelection(TEXT("\n"));
        else if (Key==EKeys::Left || Key==EKeys::Right || Key==EKeys::Home || Key==EKeys::End || Key==EKeys::Up || Key==EKeys::Down)
        {
            auto Rows=Lines(G.GetLocalSize().X); const int32 R=CursorLine(Rows);
            if (Key==EKeys::Home) Cursor=E.IsControlDown() ? 0 : Rows[R].Begin;
            else if (Key==EKeys::End) { Cursor=E.IsControlDown() ? O->Value.Len() : Rows[R].End; if (Cursor>0 && Cursor<O->Value.Len() && O->Value[Cursor-1]=='\n') --Cursor; }
            else if (Key==EKeys::Left || Key==EKeys::Right) Cursor=FMath::Clamp(Cursor+(Key==EKeys::Left ? -1 : 1),0,O->Value.Len());
            else { const int32 Next=FMath::Clamp(R+(Key==EKeys::Up ? -1 : 1),0,Rows.Num()-1); Cursor=FMath::Min(Rows[Next].End,Rows[Next].Begin+Cursor-Rows[R].Begin); }
            if (!E.IsShiftDown()) Anchor=Cursor;
        }
        else return FReply::Unhandled();
        Invalidate(EInvalidateWidgetReason::Paint); return FReply::Handled();
    }
    virtual int32 OnPaint(const FPaintArgs&,const FGeometry& G,const FSlateRect&,FSlateWindowElementList& Out,int32 Layer,const FWidgetStyle& Style,bool) const override
    {
        const auto* O=Owner.Get(); if (!O || !O->FontLabel || !O->FontLabel->GetBitmapFont()) return Layer;
        const auto& Font=*O->FontLabel->GetBitmapFont(); auto* Atlas=O->FontLabel->GetGlyphAtlas(false); if (!Atlas) return Layer;
        auto Rows=Lines(G.GetLocalSize().X); const float Offset=Scroll(Rows,G.GetLocalSize().Y,Font.MaxCharHeight);
        FSlateBrush Brush; Brush.DrawAs=ESlateBrushDrawType::Image; Brush.SetResourceObject(Atlas);
        auto Box=[&](FVector2D P,FVector2D Size,FLinearColor Color,int32 Z) { FSlateDrawElement::MakeBox(Out,Z,G.ToPaintGeometry(Size,FSlateLayoutTransform(P)),FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")),ESlateDrawEffect::None,Color); };
        const int32 Begin=FMath::Min(Cursor,Anchor), End=FMath::Max(Cursor,Anchor);
        for (int32 R=0;R<Rows.Num();++R)
        {
            float X=Left(Rows[R],G.GetLocalSize().X), Y=(O->bDigitsOnly ? 0 : 2)+R*Font.MaxCharHeight-Offset;
            for (int32 I=Rows[R].Begin;I<Rows[R].End;++I)
            {
                const int32 Adv=ACEDatText::Advance(Font,O->Value[I]);
                if (HasKeyboardFocus() && I>=Begin && I<End) Box({X,Y},{float(Adv),float(Font.MaxCharHeight)},FLinearColor(.15f,.3f,.6f,.5f),Layer);
                if (HasKeyboardFocus() && I==Cursor && FMath::Fmod(FPlatformTime::Seconds(),1.)<.6) Box({X,Y},{1.f,float(Font.MaxCharHeight)},O->TextColor,Layer+2);
                const auto* C=ACEDatText::FindChar(Font,O->Value[I]);
                if (C && C->Width && C->Height)
                {
                    FVector2D UV(C->OffsetX,C->OffsetY),Size(C->Width,C->Height),AS(Atlas->GetSizeX(),Atlas->GetSizeY());
                    Brush.SetUVRegion(FBox2f(FVector2f(UV/AS),FVector2f((UV+Size)/AS)));
                    FSlateDrawElement::MakeBox(Out,Layer+1,G.ToPaintGeometry(Size,FSlateLayoutTransform(FVector2D(X+int8(C->HorizontalOffsetBefore),Y+int8(C->VerticalOffsetBefore)))),&Brush,ESlateDrawEffect::None,O->FontLabel->ShouldTintAtlas(false) ? O->TextColor*Style.GetColorAndOpacityTint() : FLinearColor::White);
                }
                X+=Adv;
            }
            if (HasKeyboardFocus() && R==Rows.Num()-1 && Cursor==O->Value.Len() && FMath::Fmod(FPlatformTime::Seconds(),1.)<.6) Box({X,Y},{1.f,float(Font.MaxCharHeight)},O->TextColor,Layer+2);
        }
        return Layer+2;
    }
private:
    TWeakObjectPtr<UACERetailTextEntry> Owner;
    int32 Cursor=0,Anchor=0;
    FString Original,Undo;
    bool bCommitting=false, bHasUndo=false;
};

void UACERetailTextEntry::SetText(const FText& Text) { Value=Text.ToString(); if (Editor) Editor->ResetSelection(); InvalidateLayoutAndVolatility(); }
void UACERetailTextEntry::SetRetailElement(UACEUIResourceResolver* Resources,const TSharedPtr<FACEUIElement>& Element)
{
    if (!FontLabel) FontLabel=NewObject<UACERetailTextBlock>(this);
    FontLabel->SetRetailElement(Resources,Element,FVector2D(1,1),0,false);
}
void UACERetailTextEntry::Commit(ETextCommit::Type Method) { OnTextCommitted.Broadcast(GetText(),Method); OnContextCommitted.Broadcast(ContextId,GetText(),Method); }
void UACERetailTextEntry::RequestPlatformKeyboard(uint32 UserIndex) { if (Editor.IsValid()) Editor->OpenPlatformKeyboard(UserIndex); }
TSharedRef<SWidget> UACERetailTextEntry::RebuildWidget() { Editor=SNew(SACERetailTextEntry,this); Editor->ResetSelection(); return Editor.ToSharedRef(); }
void UACERetailTextEntry::ReleaseSlateResources(bool bChildren) { Super::ReleaseSlateResources(bChildren); Editor.Reset(); }
