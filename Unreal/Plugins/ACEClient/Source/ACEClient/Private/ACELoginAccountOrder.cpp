#include "ACELoginWidget.h"
#include "Components/ScrollBox.h"
#include "Framework/Application/SlateApplication.h"
#include "Input/DragAndDrop.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
    // Only this launcher's account payload can change the order. The drag
    // contains an identity and display label, never account credentials.
    class FAccountOrderDrag : public FDragDropOperation
    {
    public:
        DRAG_DROP_OPERATOR_TYPE(FAccountOrderDrag,FDragDropOperation)
        TWeakObjectPtr<UACELoginWidget> Owner;
        FString Id,Title;
        FVector2D Position;

        static TSharedRef<FAccountOrderDrag> New(UACELoginWidget* Owner,const FString& Id,
            const FString& Title,const FVector2D& Position)
        {
            auto Op=MakeShared<FAccountOrderDrag>();
            Op->Owner=Owner; Op->Id=Id; Op->Title=Title; Op->Position=Position;
            Op->bCreateNewWindow=false; Op->MouseCursor=EMouseCursor::GrabHandClosed;
            Op->Construct(); return Op;
        }
        void OnDragged(const FDragDropEvent& Event) override
        {
            Position=Event.GetScreenSpacePosition(); FDragDropOperation::OnDragged(Event);
        }
        TSharedPtr<SWidget> GetDefaultDecorator() const override
        {
            return SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                .BorderBackgroundColor(FLinearColor(.045f,.036f,.025f)).Padding(8)
                [SNew(STextBlock).Text(FText::FromString(Title)).Font(FCoreStyle::GetDefaultFontStyle("Regular",18))
                    .ColorAndOpacity(FLinearColor(.88f,.85f,.77f))];
        }
    };

    // Six-dot grip works before game data loads, without a font glyph dependency.
    class SAccountGripIcon : public SLeafWidget
    {
    public:
        SLATE_BEGIN_ARGS(SAccountGripIcon) {} SLATE_END_ARGS()
        void Construct(const FArguments&) { SetVisibility(EVisibility::HitTestInvisible); }
        FVector2D ComputeDesiredSize(float) const override { return FVector2D(18,26); }
        int32 OnPaint(const FPaintArgs&,const FGeometry& G,const FSlateRect&,FSlateWindowElementList& Out,
            int32 Layer,const FWidgetStyle& Style,bool Enabled) const override
        {
            const auto Effect=ShouldBeEnabled(Enabled)?ESlateDrawEffect::None:ESlateDrawEffect::DisabledEffect;
            const auto Offset=(G.GetLocalSize()-FVector2D(18,26))*.5;
            for (int32 Y=0;Y<3;++Y) for (int32 X=0;X<2;++X)
                FSlateDrawElement::MakeBox(Out,Layer,G.ToPaintGeometry(FVector2D(3,3),
                    FSlateLayoutTransform(Offset+FVector2D(4+X*7,4+Y*7))),FCoreStyle::Get().GetBrush("WhiteBrush"),
                    Effect,FLinearColor(.88f,.85f,.77f)*Style.GetColorAndOpacityTint());
            return Layer;
        }
    };

    class SAccountOrderRow : public SCompoundWidget
    {
    public:
        SLATE_BEGIN_ARGS(SAccountOrderRow):_Owner(nullptr) {}
            SLATE_ARGUMENT(UACELoginWidget*,Owner)
            SLATE_ARGUMENT(FString,Id)
            SLATE_ARGUMENT(FString,Title)
            SLATE_ARGUMENT(FButtonStyle,GripStyle)
            SLATE_DEFAULT_SLOT(FArguments,Content)
        SLATE_END_ARGS()
        void Construct(const FArguments& Args)
        {
            Owner=Args._Owner; Id=Args._Id; GripStyle=Args._GripStyle;
            SetTag(FName(*(TEXT("AccountOrderRow.")+Id)));
            ChildSlot
            [SNew(SHorizontalBox)
                +SHorizontalBox::Slot().AutoWidth().Padding(0,0,4,0)
                [SNew(SButton).Tag(FName(*(TEXT("AccountOrderGrip.")+Id)))
                    .ButtonStyle(&GripStyle).ContentPadding(FMargin(9,7)).IsFocusable(false)
                    .AllowDragDrop(true).Cursor(EMouseCursor::GrabHand)
                    .ToolTipText(FText::FromString(TEXT("Drag to reorder accounts. Changes are saved automatically.")))
                    .OnSlateButtonDragDetected_Lambda([Host=Owner,AccountId=Id,Title=Args._Title](const FGeometry&,const FPointerEvent& Event)
                    {
                        if (!Host.IsValid() || !Host->CanReorderAccounts()) return FReply::Unhandled();
                        return FReply::Handled().BeginDragDrop(FAccountOrderDrag::New(Host.Get(),AccountId,Title,Event.GetScreenSpacePosition()));
                    })
                    [SNew(SAccountGripIcon)]]
                +SHorizontalBox::Slot().FillWidth(1)[Args._Content.Widget]];
        }
        FReply OnDragOver(const FGeometry& G,const FDragDropEvent& E) override
        {
            Hovered=CanAccept(E); After=IsAfter(G,E); Invalidate(EInvalidateWidgetReason::Paint);
            return Hovered?FReply::Handled():FReply::Unhandled();
        }
        void OnDragLeave(const FDragDropEvent& E) override
        {
            Hovered=false; Invalidate(EInvalidateWidgetReason::Paint); SCompoundWidget::OnDragLeave(E);
        }
        FReply OnDrop(const FGeometry& G,const FDragDropEvent& E) override
        {
            Hovered=false; Invalidate(EInvalidateWidgetReason::Paint);
            if (!CanAccept(E)) return FReply::Unhandled();
            Owner->MoveAccountInOrder(E.GetOperationAs<FAccountOrderDrag>()->Id,Id,IsAfter(G,E));
            return FReply::Handled();
        }
        int32 OnPaint(const FPaintArgs& Args,const FGeometry& G,const FSlateRect& Culling,FSlateWindowElementList& Out,
            int32 Layer,const FWidgetStyle& Style,bool Enabled) const override
        {
            Layer=SCompoundWidget::OnPaint(Args,G,Culling,Out,Layer,Style,Enabled);
            if (Hovered)
            {
                const float Y=After?G.GetLocalSize().Y-1:1;
                FSlateDrawElement::MakeLines(Out,++Layer,G.ToPaintGeometry(),{FVector2D(0,Y),FVector2D(G.GetLocalSize().X,Y)},
                    ESlateDrawEffect::None,FLinearColor(.94f,.68f,.26f)*Style.GetColorAndOpacityTint(),true,2.f);
            }
            return Layer;
        }
    private:
        TWeakObjectPtr<UACELoginWidget> Owner;
        FString Id;
        FButtonStyle GripStyle;
        bool Hovered=false,After=false;
        bool CanAccept(const FDragDropEvent& Event) const
        {
            const auto Op=Event.GetOperationAs<FAccountOrderDrag>();
            return Op && Owner.IsValid() && Op->Owner==Owner && Op->Id!=Id && Owner->CanReorderAccounts();
        }
        static bool IsAfter(const FGeometry& G,const FDragDropEvent& E)
        {
            return G.AbsoluteToLocal(E.GetScreenSpacePosition()).Y>=G.GetLocalSize().Y*.5;
        }
    };
}

TSharedRef<SWidget> UACELoginActionButton::RebuildWidget()
{
    auto Button=Super::RebuildWidget();
    if (!bAccountDragHandle) return Button;
    return SNew(SAccountOrderRow).Owner(Owner.Get()).Id(Value).Title(GetToolTipText().ToString())
        .GripStyle(GetStyle())[Button];
}

void UACELoginWidget::TickAccountDrag(float DeltaTime)
{
    if (!CanReorderAccounts() || !ManagedAccountList || !FSlateApplication::IsInitialized()) return;
    const auto Op=FSlateApplication::Get().GetDragDroppingContent();
    if (!Op || !Op->IsOfType<FAccountOrderDrag>()) return;
    const auto Drag=StaticCastSharedPtr<FAccountOrderDrag>(Op);
    if (Drag->Owner.Get()!=this) return;
    const auto& G=ManagedAccountList->GetCachedGeometry();
    const auto P=G.AbsoluteToLocal(Drag->Position),Size=G.GetLocalSize();
    if (P.X<0 || P.X>Size.X || P.Y<0 || P.Y>Size.Y || Size.Y<=0) return;
    const float Edge=FMath::Min(32.f,float(Size.Y*.25));
    const float Speed=P.Y<Edge?-(1.f-P.Y/Edge):P.Y>Size.Y-Edge?1.f-(Size.Y-P.Y)/Edge:0.f;
    if (Speed!=0) ManagedAccountList->SetScrollOffset(FMath::Clamp(
        ManagedAccountList->GetScrollOffset()+Speed*360.f*FMath::Min(DeltaTime,.1f),0.f,ManagedAccountList->GetScrollOffsetOfEnd()));
}
