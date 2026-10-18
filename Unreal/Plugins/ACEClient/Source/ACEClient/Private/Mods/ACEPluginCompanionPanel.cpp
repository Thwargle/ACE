#include "Mods/ACEPluginPresentation.h"
#include "Mods/ACEPluginStyle.h"
#include "Mods/ACEPluginSubsystem.h"
#include "ACEDatSubsystem.h"
#include "UI/ACEUIResourceResolver.h"
#include "Engine/GameInstance.h"
#include "UObject/StrongObjectPtr.h"
#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "ImageUtils.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/STextBlock.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/CoreStyle.h"

namespace
{
namespace ACEPluginCompanionPanelPrivate
{
    FString S(const TSharedPtr<FJsonObject>& O,const TCHAR* K,const FString& D=FString())
    {FString V;return O->TryGetStringField(K,V)?V:D;}
    double N(const TSharedPtr<FJsonObject>& O,const TCHAR* K,double D=0)
    {double V;return O->TryGetNumberField(K,V)&&FMath::IsFinite(V)?V:D;}
    bool B(const TSharedPtr<FJsonObject>& O,const TCHAR* K,bool D=true)
    {bool V;return O->TryGetBoolField(K,V)?V:D;}
    FLinearColor Color(const FString& Name)
    {
        if(Name==TEXT("green"))return FLinearColor(.2f,1,.4f);
        if(Name==TEXT("red"))return FLinearColor(1,.3f,.2f);
        if(Name==TEXT("yellow"))return FLinearColor(1,.85f,.25f);
        if(Name==TEXT("blue"))return FLinearColor(.25f,.7f,1);
        return FLinearColor(.9f,.94f,1);
    }
    // Stable Lua control IDs are also stable Slate identities. Only structural
    // changes touch slots; ordinary snapshots update the retained row data.
    class SACECompanionRows : public SCompoundWidget
    {
    public:
        SLATE_BEGIN_ARGS(SACECompanionRows){} SLATE_ARGUMENT(UACEPluginSubsystem*,Host) SLATE_ARGUMENT(FString,Id) SLATE_END_ARGS()
        void Construct(const FArguments& Args)
        {Host=Args._Host;Id=Args._Id;FACEPluginStyle::Use(Args._Host,Id);ChildSlot[SAssignNew(Rows,SVerticalBox)];Refresh();}
        void Tick(const FGeometry& G,double Time,float Delta) override {SCompoundWidget::Tick(G,Time,Delta);Refresh();}
    private:
        struct FRow
        {
            FString Type;
            TSharedPtr<FJsonObject> Data;
            TSharedPtr<SWidget> Widget;
            TSharedPtr<SVerticalBox> Vertical;
            TSharedPtr<SHorizontalBox> Horizontal,TabButtons;
            TSharedPtr<SWidgetSwitcher> Switch;
            TArray<TSharedPtr<SWidget>> Children;
            // False means this control is being edited. Retry just this control,
            // never block other rows because the game viewport has mouse capture.
            TFunction<bool()> Sync;
        };
        TWeakObjectPtr<UACEPluginSubsystem> Host;FString Id;uint32 Revision=MAX_uint32;
        TWeakPtr<FACEClientPlugin> Last;TSharedPtr<SVerticalBox> Rows;
        TStrongObjectPtr<UACEUIResourceResolver> Resolver;
        TMap<FString,TSharedPtr<FRow>> Controls;
        TArray<TSharedPtr<SWidget>> RootChildren;
        TSet<FString> Visited;
        TArray<TSharedPtr<FRow>> Pending;
        TMap<FString,bool> Expanded;TMap<FString,FString> Tabs;
        TSharedRef<STextBlock> Label(const TSharedPtr<FJsonObject>& R)
        {
            return SNew(SACEPluginText).Text_Lambda([R](){return FText::FromString(S(R,TEXT("text")));})
                .Font_Lambda([R](){return FCoreStyle::GetDefaultFontStyle(B(R,TEXT("bold"),false)?"Bold":"Regular",FMath::Clamp(int32(N(R,TEXT("size"),16)),10,36));})
                .ColorAndOpacity_Lambda([R](){return FSlateColor(Color(S(R,TEXT("color"))));}).AutoWrapText(true);
        }
        UTexture2D* Image(const FString& Asset)
        {
            auto P=Last.Pin();if(!P)return nullptr;
            if(!P->Images)P->Images=MakeShared<FACEPluginImageCache>();
            return P->Images->Load(P->Directory,Asset);
        }
        void Children(FRow& Node)
        {
            const auto R=Node.Data;const FString Key=S(R,TEXT("id"));
            const TArray<TSharedPtr<FJsonValue>>* Values=nullptr;R->TryGetArrayField(TEXT("children"),Values);
            TArray<TSharedPtr<SWidget>> Next;TArray<FString> Keys;
            if(Values)for(const auto& V:*Values){Next.Add(Build(V->AsObject()));Keys.Add(S(V->AsObject(),TEXT("id")));}
            if(Node.Children!=Next)
            {
                // Reattach existing child widgets in their new order. In particular,
                // a dropdown's anchor, text editor and section survive sibling edits.
                if(Node.Vertical){Node.Vertical->ClearChildren();for(const auto& W:Next)Node.Vertical->AddSlot().AutoHeight().Padding(2)[W.ToSharedRef()];}
                if(Node.Horizontal){Node.Horizontal->ClearChildren();for(const auto& W:Next)Node.Horizontal->AddSlot().FillWidth(1).Padding(3)[W.ToSharedRef()];}
                if(Node.Switch)
                {
                    for(const auto& Old:Node.Children)Node.Switch->RemoveSlot(Old.ToSharedRef());Node.TabButtons->ClearChildren();
                    for(int32 I=0;I<Next.Num();++I)
                    {
                        Node.Switch->AddSlot()[Next[I].ToSharedRef()];const FString Page=Keys[I];
                        Node.TabButtons->AddSlot().FillWidth(1).Padding(2)[SNew(SButton).ButtonStyle(&FACEPluginStyle::Current()->Button).IsFocusable(false)
                            .OnClicked_Lambda([this,Key,Page,Switch=Node.Switch,I](){Tabs.Add(Key,Page);Switch->SetActiveWidgetIndex(I);return FReply::Handled();})[Label(Controls[Page]->Data)]];
                    }
                }
                Node.Children=MoveTemp(Next);
            }
            if(Node.Switch)
            {
                int32 Page=Keys.IndexOfByKey(Tabs.FindRef(Key));if(Page==INDEX_NONE)Page=0;
                if(Keys.IsValidIndex(Page))Tabs.Add(Key,Keys[Page]);
                Node.Switch->SetActiveWidgetIndex(Page);
            }
        }
        TSharedRef<SWidget> Build(const TSharedPtr<FJsonObject>& Source)
        {
            const FString Key=S(Source,TEXT("id")),Type=S(Source,TEXT("type"),S(Source,TEXT("action")).IsEmpty()?TEXT("text"):TEXT("button"));
            Visited.Add(Key);auto Node=Controls.FindRef(Key);
            if(Node&&Node->Type==Type)
            {
                Node->Data->Values=Source->Values;
                if(Node->Vertical||Node->Horizontal||Node->Switch)Children(*Node);
                if(Node->Sync&&!Node->Sync())Pending.Add(Node);
                return Node->Widget.ToSharedRef();
            }
            Node=MakeShared<FRow>();Node->Type=Type;Node->Data=MakeShared<FJsonObject>(*Source);Controls.Add(Key,Node);
            const auto R=Node->Data;
            auto Changed=[H=Host,Plugin=Id,Key,R](const TSharedPtr<FJsonValue>& Value){if(H.IsValid()&&H->ChangePluginControl(Plugin,Key,Value))R->SetField(TEXT("value"),Value);};
            TSharedRef<SWidget> Content=SNullWidget::NullWidget;
            if(Type==TEXT("row")||Type==TEXT("column")||Type==TEXT("section")||Type==TEXT("tabs"))
            {
                if(Type==TEXT("row")){Node->Horizontal=SNew(SHorizontalBox);Content=Node->Horizontal.ToSharedRef();}
                else if(Type==TEXT("tabs"))
                {
                    Node->Switch=SNew(SWidgetSwitcher);Node->TabButtons=SNew(SHorizontalBox);
                    Content=SNew(SVerticalBox)+SVerticalBox::Slot().AutoHeight()[Node->TabButtons.ToSharedRef()]+SVerticalBox::Slot().AutoHeight()[Node->Switch.ToSharedRef()];
                }
                else
                {
                    Node->Vertical=SNew(SVerticalBox);
                    if(Type==TEXT("section"))
                    {
                        if(!Expanded.Contains(Key))Expanded.Add(Key,B(R,TEXT("expanded"),true));
                        const auto Theme=FACEPluginStyle::Current();
                        Content=SNew(SExpandableArea).BorderImage(&Theme->SectionHeader).BodyBorderImage(&Theme->TitleBackground)
                            .InitiallyCollapsed(!Expanded[Key]).OnAreaExpansionChanged_Lambda([this,Key](bool Open){Expanded.Add(Key,Open);}).HeaderContent()[Label(R)].BodyContent()[Node->Vertical.ToSharedRef()];
                    }
                    else Content=Node->Vertical.ToSharedRef();
                }
                Children(*Node);
            }
            else if(Type==TEXT("button"))
                Content=SNew(SButton).ButtonStyle(&FACEPluginStyle::Current()->Button).Tag(FName(*(TEXT("PluginRow.")+Key))).IsFocusable(false).ContentPadding(FMargin(10,7))
                    .OnClicked_Lambda([H=Host,Plugin=Id,Key](){if(H.IsValid())H->ClickPluginRow(Plugin,Key);return FReply::Handled();})[Label(R)];
            else if(Type==TEXT("checkbox"))
                Content=SNew(SCheckBox).Style(&FACEPluginStyle::Current()->Check).IsChecked_Lambda([R](){return B(R,TEXT("value"),false)?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
                    .OnCheckStateChanged_Lambda([Changed](ECheckBoxState Value){Changed(MakeShared<FJsonValueBoolean>(Value==ECheckBoxState::Checked));})[Label(R)];
            else if(Type==TEXT("input"))
            {
                auto Applying=MakeShared<bool>(false);
                auto Input=SNew(SEditableTextBox).Style(&FACEPluginStyle::Current()->Edit).Font(FCoreStyle::GetDefaultFontStyle("Regular",16)).Text(FText::FromString(S(R,TEXT("value"))))
                    .HintText_Lambda([R](){return FText::FromString(S(R,TEXT("placeholder")));}).SelectAllTextWhenFocused(false)
                    .OnTextChanged_Lambda([Changed,R,Applying](const FText& Text){if(!*Applying&&B(R,TEXT("live"),false))Changed(MakeShared<FJsonValueString>(Text.ToString().Left(1024)));})
                    .OnTextCommitted_Lambda([Changed,Applying](const FText& Text,ETextCommit::Type){if(!*Applying)Changed(MakeShared<FJsonValueString>(Text.ToString().Left(1024)));});
                Node->Sync=[R,Input,Applying](){if(Input->HasAnyUserFocusOrFocusedDescendants())return false;
                    const FString Value=S(R,TEXT("value"));if(Input->GetText().ToString()!=Value){TGuardValue<bool> Guard(*Applying,true);Input->SetText(FText::FromString(Value));}return true;};
                Content=SNew(SVerticalBox)+SVerticalBox::Slot().AutoHeight()[Label(R)]+SVerticalBox::Slot().AutoHeight()[Input];
            }
            else if(Type==TEXT("number"))
            {
                auto Input=SNew(SSpinBox<double>).Style(&FACEPluginStyle::Current()->Spin).Font(FCoreStyle::GetDefaultFontStyle("Regular",16)).MinValue(N(R,TEXT("min"))).MaxValue(N(R,TEXT("max"),100)).Delta(N(R,TEXT("step"),1)).Value(N(R,TEXT("value")))
                    .OnValueCommitted_Lambda([Changed](double V,ETextCommit::Type){Changed(MakeShared<FJsonValueNumber>(V));})
                    .OnEndSliderMovement_Lambda([Changed](double V){Changed(MakeShared<FJsonValueNumber>(V));});
                Node->Sync=[R,Input](){if(Input->HasAnyUserFocusOrFocusedDescendants()||Input->HasMouseCapture())return false;
                    Input->SetMinValue(N(R,TEXT("min")));Input->SetMaxValue(N(R,TEXT("max"),100));Input->SetDelta(N(R,TEXT("step"),1));Input->SetValue(N(R,TEXT("value")));return true;};
                Content=SNew(SVerticalBox)+SVerticalBox::Slot().AutoHeight()[Label(R)]+SVerticalBox::Slot().AutoHeight()[Input];
            }
            else if(Type==TEXT("slider"))
            {
                auto Input=SNew(SSlider).MinValue(N(R,TEXT("min"))).MaxValue(N(R,TEXT("max"),100)).StepSize_Lambda([R](){return float(N(R,TEXT("step"),1));}).Value(N(R,TEXT("value")))
                    .OnValueChanged_Lambda([Changed](float V){Changed(MakeShared<FJsonValueNumber>(V));});
                Node->Sync=[R,Input](){if(Input->HasMouseCapture()||Input->HasAnyUserFocusOrFocusedDescendants())return false;
                    Input->SetMinAndMaxValues(N(R,TEXT("min")),N(R,TEXT("max"),100));Input->SetValue(float(N(R,TEXT("value"))));return true;};
                Content=SNew(SVerticalBox)+SVerticalBox::Slot().AutoHeight()[SNew(SHorizontalBox)+SHorizontalBox::Slot().FillWidth(1)[Label(R)]+SHorizontalBox::Slot().AutoWidth()[SNew(SACEPluginText).Font(FCoreStyle::GetDefaultFontStyle("Regular",16)).Text_Lambda([R](){return FText::AsNumber(N(R,TEXT("value")));})]]
                    +SVerticalBox::Slot().AutoHeight()[Input];
            }
            else if(Type==TEXT("select"))
            {
                auto Options=MakeShared<TArray<TSharedPtr<FString>>>();auto Applying=MakeShared<bool>(false);
                auto Combo=SNew(SComboBox<TSharedPtr<FString>>).OptionsSource(&Options.Get())
                    .OnGenerateWidget_Lambda([Options](TSharedPtr<FString> V){return SNew(SACEPluginText).Font(FCoreStyle::GetDefaultFontStyle("Regular",16)).Text(FText::FromString(V?*V:FString()));})
                    .OnSelectionChanged_Lambda([Options,Changed,Applying](TSharedPtr<FString> V,ESelectInfo::Type){if(V&&!*Applying)Changed(MakeShared<FJsonValueString>(*V));})
                    [SNew(SACEPluginText).Font(FCoreStyle::GetDefaultFontStyle("Regular",16)).Text_Lambda([R](){return FText::FromString(S(R,TEXT("value")));})];
                Node->Sync=[R,Options,Combo,Applying]()
                {
                    // Keep the open menu's rows/highlight stable until it closes.
                    if(Combo->IsOpen())return false;
                    TGuardValue<bool> Guard(*Applying,true);const TArray<TSharedPtr<FJsonValue>>* Values=nullptr;R->TryGetArrayField(TEXT("options"),Values);
                    const int32 Count=Values?Values->Num():0;bool Different=Options->Num()!=Count;
                    for(int32 I=0;!Different&&I<Count;++I)Different=*(*Options)[I]!=(*Values)[I]->AsString();
                    if(Different){Options->Reset();if(Values)for(const auto& V:*Values)Options->Add(MakeShared<FString>(V->AsString()));Combo->RefreshOptions();}
                    TSharedPtr<FString> Selected;const FString Value=S(R,TEXT("value"));for(const auto& Option:*Options)if(*Option==Value){Selected=Option;break;}
                    if(Combo->GetSelectedItem()!=Selected)Combo->SetSelectedItem(Selected);
                    return true;
                };
                Content=SNew(SVerticalBox)+SVerticalBox::Slot().AutoHeight()[Label(R)]+SVerticalBox::Slot().AutoHeight()[Combo];
            }
            else if(Type==TEXT("progress"))
                Content=SNew(SVerticalBox)+SVerticalBox::Slot().AutoHeight()[Label(R)]+SVerticalBox::Slot().AutoHeight().Padding(0,4)[SNew(SProgressBar).Percent_Lambda([R](){return TOptional<float>(FMath::Clamp(N(R,TEXT("value")),0.,1.));}).FillColorAndOpacity_Lambda([R](){return Color(S(R,TEXT("color")));})];
            else if(Type==TEXT("item_icon"))
            {
                if(!Resolver.IsValid()&&Host.IsValid()){Resolver.Reset(NewObject<UACEUIResourceResolver>(Host.Get()));Resolver->Initialize(Host->GetGameInstance()->GetSubsystem<UACEDatSubsystem>());}
                auto Background=MakeShared<FSlateBrush>(),Foreground=MakeShared<FSlateBrush>();
                auto Back=SNew(SImage).Image_Lambda([Background]()->const FSlateBrush*{return &Background.Get();});
                auto Front=SNew(SImage).Image_Lambda([Foreground]()->const FSlateBrush*{return &Foreground.Get();});
                Node->Sync=[this,R,Background,Foreground,Back,Front]()
                {
                    if(!Resolver.IsValid()||!Resolver->GetDatSubsystem()||!Resolver->GetDatSubsystem()->IsDatReady())return false;
                    const double Size=FMath::Clamp(N(R,TEXT("size"),32),16.,128.);
                    auto Update=[Size](const auto& Brush,const auto& Widget,UTexture2D* Texture)
                    {
                        if(Brush->GetResourceObject()!=Texture||Brush->ImageSize!=FVector2D(Size,Size))
                        {Brush->SetResourceObject(Texture);Brush->DrawAs=ESlateBrushDrawType::Image;Brush->ImageSize=FVector2D(Size,Size);Widget->Invalidate(EInvalidateWidgetReason::Layout|EInvalidateWidgetReason::Paint);}
                    };
                    // Same compositors as the inventory and Supply Ledger, including
                    // the square type background, underlay, overlay and enchantments.
                    Update(Background,Back,Resolver->ResolveItemBackground(uint32(N(R,TEXT("item_type"))),uint32(N(R,TEXT("underlay")))));
                    Update(Foreground,Front,Resolver->ResolveItemForeground(uint32(N(R,TEXT("icon"))),uint32(N(R,TEXT("overlay"))),uint32(N(R,TEXT("effects")))));
                    return true;
                };
                Content=SNew(SHorizontalBox)+SHorizontalBox::Slot().AutoWidth()[SNew(SBox)
                    .WidthOverride_Lambda([R](){return FOptionalSize(FMath::Clamp(N(R,TEXT("size"),32),16.,128.));})
                    .HeightOverride_Lambda([R](){return FOptionalSize(FMath::Clamp(N(R,TEXT("size"),32),16.,128.));})
                    [SNew(SOverlay)+SOverlay::Slot()[Back]+SOverlay::Slot()[Front]]]
                    +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(6,0)[Label(R)];
            }
            else if(Type==TEXT("icon")||Type==TEXT("image"))
            {
                if(!Resolver.IsValid()&&Host.IsValid()){Resolver.Reset(NewObject<UACEUIResourceResolver>(Host.Get()));Resolver->Initialize(Host->GetGameInstance()->GetSubsystem<UACEDatSubsystem>());}
                auto Brush=MakeShared<FSlateBrush>();auto Icon=SNew(SImage).Image_Lambda([Brush]()->const FSlateBrush*{return &Brush.Get();});
                Node->Sync=[this,R,Type,Brush,Icon]()
                {
                    const double Size=FMath::Clamp(N(R,TEXT("size"),32),16.,128.);
                    auto* Texture=Type==TEXT("image")?Image(S(R,TEXT("asset"))):Resolver.IsValid()?Resolver->ResolveIconTexture(uint32(N(R,TEXT("icon")))):nullptr;
                    if(Brush->GetResourceObject()!=Texture||Brush->ImageSize!=FVector2D(Size,Size)){Brush->SetResourceObject(Texture);Brush->ImageSize=FVector2D(Size,Size);Icon->Invalidate(EInvalidateWidgetReason::Layout|EInvalidateWidgetReason::Paint);}return true;
                };
                Content=SNew(SHorizontalBox)+SHorizontalBox::Slot().AutoWidth()[SNew(SBox)
                    .WidthOverride_Lambda([R](){return FOptionalSize(FMath::Clamp(N(R,TEXT("size"),32),16.,128.));})
                    .HeightOverride_Lambda([R](){return FOptionalSize(FMath::Clamp(N(R,TEXT("size"),32),16.,128.));})[Icon]]
                    +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(6,0)[Label(R)];
            }
            else Content=Label(R);
            Content->SetEnabled(TAttribute<bool>::CreateLambda([R](){return B(R,TEXT("enabled"));}));
            Content->SetToolTipText(TAttribute<FText>::CreateLambda([R](){return FText::FromString(S(R,TEXT("tooltip")));}));
            Node->Widget=SNew(SBox).Tag(FName(*(TEXT("PluginControl.")+Key))).Padding_Lambda([R](){return FMargin(FMath::Clamp(float(N(R,TEXT("padding"),2)),0.f,24.f));})[Content];
            if(Node->Sync&&!Node->Sync())Pending.Add(Node);
            return Node->Widget.ToSharedRef();
        }
        void Refresh()
        {
            if(!Host.IsValid())return;TSharedPtr<FACEClientPlugin> P;for(const auto& Candidate:Host->Plugins)if(Candidate->Id==Id){P=Candidate;break;}
            if(!P)
            {
                Rows->ClearChildren();RootChildren.Reset();Controls.Reset();Pending.Reset();Last.Reset();Revision=MAX_uint32;return;
            }
            if(Last.Pin()==P&&Revision==P->PanelRevision)
            {Pending.RemoveAll([](const auto& Node){return !Node->Sync||Node->Sync();});return;}
            FACEPluginStyle::Use(Host.Get(),Id);
            if(Last.Pin()!=P){Controls.Reset();Resolver.Reset();Expanded.Reset();Tabs.Reset();}
            Last=P;Revision=P->PanelRevision;Visited.Reset();Pending.Reset();
            TArray<TSharedPtr<SWidget>> Next;
            for(const auto& V:P->PanelRows)Next.Add(Build(V->AsObject()));
            if(RootChildren!=Next){Rows->ClearChildren();for(const auto& Widget:Next)Rows->AddSlot().AutoHeight().Padding(2)[Widget.ToSharedRef()];RootChildren=MoveTemp(Next);}
            for(auto It=Controls.CreateIterator();It;++It)if(!Visited.Contains(It.Key())){Expanded.Remove(It.Key());Tabs.Remove(It.Key());It.RemoveCurrent();}
            // Tick runs after the normal prepass. Measure changed/new content now
            // so the current frame never paints an empty, unmeasured replacement.
            SlatePrepass(GetCachedGeometry().Scale);
        }
    };
}
} // anonymous namespace
TSharedRef<SWidget> UACEPluginSubsystem::MakeCompanionPanel(const FString& Id)
{
    using namespace ACEPluginCompanionPanelPrivate;return SNew(SACECompanionRows).Host(this).Id(Id);}
