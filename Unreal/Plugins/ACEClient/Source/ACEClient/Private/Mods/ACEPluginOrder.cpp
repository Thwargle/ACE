#include "Mods/ACEPluginOrder.h"
#include "Mods/ACEPluginSubsystem.h"
#include "Mods/ACEPluginStyle.h"
#include "Rendering/DrawElements.h"
#include "Widgets/Layout/SBorder.h"
#include "Styling/CoreStyle.h"

TArray<FString> UACEPluginSubsystem::PluginDisplayOrder() const
{
    TArray<FString> Known{TEXT("__manager")};
    for(const auto& P:Plugins)Known.AddUnique(P->Id);
    if(Find(TEXT("ucm")))Known.AddUnique(TEXT("ucm.log"));
    if(Find(TEXT("waypoint")))Known.AddUnique(TEXT("waypoint.map"));
    TArray<FString> Ordered;
    const TArray<TSharedPtr<FJsonValue>>* Saved=nullptr;
    if(Settings&&Settings->TryGetArrayField(TEXT("_plugin_order"),Saved))for(const auto& Value:*Saved)
    {FString Id;if(Value&&Value->TryGetString(Id)&&Known.Contains(Id))Ordered.AddUnique(Id);}
    for(const auto& Id:Known)Ordered.AddUnique(Id);
    return Ordered;
}

bool UACEPluginSubsystem::MovePluginInOrder(const FString& Id,const FString& Target,bool After)
{
    auto Order=PluginDisplayOrder();
    if(Id==Target||!Order.Contains(Id)||!Order.Contains(Target))return false;
    const auto Previous=Order;
    Order.Remove(Id);Order.Insert(Id,Order.IndexOfByKey(Target)+(After?1:0));
    if(Order==Previous)return false;
    TArray<TSharedPtr<FJsonValue>> Saved;
    for(const auto& Entry:Order)Saved.Add(MakeShared<FJsonValueString>(Entry));
    if(!Settings)Settings=MakeShared<FJsonObject>();
    Settings->SetArrayField(TEXT("_plugin_order"),Saved);SaveSettings();++PluginOrderRevision;
    // UI observers refresh on their next tick, after the drop callback finishes.
    return true;
}

TSharedRef<FACEPluginOrderDrag> FACEPluginOrderDrag::New(UACEPluginSubsystem* Host,const FString& Id)
{
    auto Op=MakeShared<FACEPluginOrderDrag>();Op->Host=Host;Op->Id=Id;
    Op->Title=Id==TEXT("__manager")?TEXT("Plugin Manager"):Id==TEXT("ucm.log")?TEXT("UCM Log"):Id==TEXT("waypoint.map")?TEXT("Waypoint Map"):Id;
    for(const auto& P:Host->Plugins)if(P->Id==Id)Op->Title=P->DisplayTitle();
    Op->bCreateNewWindow=false;Op->MouseCursor=EMouseCursor::GrabHandClosed;Op->Construct();return Op;
}
TSharedPtr<SWidget> FACEPluginOrderDrag::GetDefaultDecorator() const
{
    return SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(.1f,.06f,.02f)).Padding(6)
        [SNew(SACEPluginText).Text(FText::FromString(Title)).Font(FCoreStyle::GetDefaultFontStyle("Regular",14))];
}
FReply ACEPluginOrder::BeginDrag(UACEPluginSubsystem* Host,const FString& Id,const FString& LockId)
{
    if(!Host||!Host->CanManipulatePluginWindow(LockId))return FReply::Unhandled();
    return FReply::Handled().BeginDragDrop(FACEPluginOrderDrag::New(Host,Id));
}
void SACEPluginOrderTarget::Construct(const FArguments& Args)
{
    Host=Args._Host;Id=Args._Id;LockId=Args._LockId;Horizontal=Args._Horizontal;
    ChildSlot[Args._Content.Widget];
}
bool SACEPluginOrderTarget::CanAccept(const FDragDropEvent& Event) const
{
    const auto Op=Event.GetOperationAs<FACEPluginOrderDrag>();
    return Op&&Host.IsValid()&&Op->Host==Host&&Op->Id!=Id&&Host->CanManipulatePluginWindow(LockId);
}
bool SACEPluginOrderTarget::IsAfter(const FGeometry& G,const FDragDropEvent& E) const
{
    const auto P=G.AbsoluteToLocal(E.GetScreenSpacePosition());
    return Horizontal.Get(false)?P.X>=G.GetLocalSize().X*.5:P.Y>=G.GetLocalSize().Y*.5;
}
FReply SACEPluginOrderTarget::OnDragOver(const FGeometry& G,const FDragDropEvent& E)
{
    Hovered=CanAccept(E);After=IsAfter(G,E);Invalidate(EInvalidateWidgetReason::Paint);
    return Hovered?FReply::Handled():FReply::Unhandled();
}
void SACEPluginOrderTarget::OnDragLeave(const FDragDropEvent& E)
{Hovered=false;Invalidate(EInvalidateWidgetReason::Paint);SCompoundWidget::OnDragLeave(E);}
FReply SACEPluginOrderTarget::OnDrop(const FGeometry& G,const FDragDropEvent& E)
{
    Hovered=false;Invalidate(EInvalidateWidgetReason::Paint);
    if(!CanAccept(E))return FReply::Unhandled();
    Host->MovePluginInOrder(E.GetOperationAs<FACEPluginOrderDrag>()->Id,Id,IsAfter(G,E));return FReply::Handled();
}
int32 SACEPluginOrderTarget::OnPaint(const FPaintArgs& Args,const FGeometry& G,const FSlateRect& Culling,
    FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool Enabled) const
{
    Layer=SCompoundWidget::OnPaint(Args,G,Culling,Elements,Layer,Style,Enabled);
    if(Hovered)
    {
        const auto Size=G.GetLocalSize();TArray<FVector2D> Line;
        if(Horizontal.Get(false)){const double X=After?Size.X-1:1;Line={{X,0},{X,Size.Y}};}
        else{const double Y=After?Size.Y-1:1;Line={{0,Y},{Size.X,Y}};}
        FSlateDrawElement::MakeLines(Elements,++Layer,G.ToPaintGeometry(),Line,ESlateDrawEffect::None,FLinearColor(.94f,.68f,.26f),true,2.f);
    }
    return Layer;
}
