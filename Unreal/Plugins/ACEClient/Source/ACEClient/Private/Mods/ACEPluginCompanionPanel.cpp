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
    class SACECompanionRows : public SCompoundWidget
    {
    public:
        SLATE_BEGIN_ARGS(SACECompanionRows){} SLATE_ARGUMENT(UACEPluginSubsystem*,Host) SLATE_ARGUMENT(FString,Id) SLATE_END_ARGS()
        void Construct(const FArguments& Args)
        {Host=Args._Host;Id=Args._Id;ChildSlot[SAssignNew(Rows,SVerticalBox)];Refresh();}
        void Tick(const FGeometry&,double,float) override {Refresh();}
    private:
        TWeakObjectPtr<UACEPluginSubsystem> Host;FString Id;uint32 Revision=MAX_uint32;
        TWeakPtr<FACEClientPlugin> Last;TSharedPtr<SVerticalBox> Rows;
        TStrongObjectPtr<UACEUIResourceResolver> Resolver;
        TArray<TSharedPtr<FSlateBrush>> Brushes;
        TMap<FString,TStrongObjectPtr<UTexture2D>> Images;
        int64 ImageBytes=0;
        TMap<FString,bool> Expanded;TMap<FString,int32> Tabs;
        bool Editing() const
        {
            if(!FSlateApplication::IsInitialized())return false;
            const auto Focus=FSlateApplication::Get().GetKeyboardFocusedWidget();
            return Focus.IsValid()&&HasFocusedDescendants()&&(Focus->GetTypeAsString().Contains(TEXT("EditableText"))||Focus->GetTypeAsString().Contains(TEXT("SpinBox")));
        }
        TSharedRef<STextBlock> Label(const TSharedPtr<FJsonObject>& R)
        {return SNew(STextBlock).Text(FText::FromString(S(R,TEXT("text")))).Font(FCoreStyle::GetDefaultFontStyle(B(R,TEXT("bold"),false)?"Bold":"Regular",FMath::Clamp(int32(N(R,TEXT("size"),16)),10,36))).ColorAndOpacity(Color(S(R,TEXT("color")))).AutoWrapText(true);}
        UTexture2D* Image(const FString& Asset)
        {
            if(const auto* Cached=Images.Find(Asset))return Cached->Get();
            auto P=Last.Pin();if(!P||Images.Num()>=32||Asset!=FPaths::GetCleanFilename(Asset)||!Asset.EndsWith(TEXT(".png")))return nullptr;
            // Cache unavailable assets as well, rather than retrying disk IO on every layout update.
            Images.Add(Asset,TStrongObjectPtr<UTexture2D>());
            const FString Path=P->Directory/TEXT("images")/Asset;const int64 Size=IFileManager::Get().FileSize(*Path);
            if(Size<=0||Size>2*1024*1024)return nullptr;TArray<uint8> Bytes;if(!FFileHelper::LoadFileToArray(Bytes,*Path))return nullptr;
            auto& Module=FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));auto Decoder=Module.CreateImageWrapper(EImageFormat::PNG);
            if(!Decoder||!Decoder->SetCompressed(Bytes.GetData(),Bytes.Num())||Decoder->GetWidth()>1024||Decoder->GetHeight()>1024||Decoder->GetWidth()<1||Decoder->GetHeight()<1)return nullptr;
            const int64 Decoded=int64(Decoder->GetWidth())*Decoder->GetHeight()*4;if(ImageBytes+Decoded>16*1024*1024)return nullptr;
            auto* Texture=FImageUtils::ImportBufferAsTexture2D(Bytes);if(!Texture)return nullptr;ImageBytes+=Decoded;Images.Add(Asset,TStrongObjectPtr<UTexture2D>(Texture));return Texture;
        }
        TSharedRef<SWidget> Build(const TSharedPtr<FJsonObject>& R)
        {
            const FString Key=S(R,TEXT("id")),Type=S(R,TEXT("type"),S(R,TEXT("action")).IsEmpty()?TEXT("text"):TEXT("button"));
            auto Changed=[H=Host,Plugin=Id,Key,R](const TSharedPtr<FJsonValue>& Value){if(H.IsValid()&&H->ChangePluginControl(Plugin,Key,Value))R->SetField(TEXT("value"),Value);};
            TSharedRef<SWidget> Content=SNullWidget::NullWidget;
            if(Type==TEXT("row")||Type==TEXT("column")||Type==TEXT("section")||Type==TEXT("tabs"))
            {
                const TArray<TSharedPtr<FJsonValue>>* Children=nullptr;R->TryGetArrayField(TEXT("children"),Children);
                if(Type==TEXT("row"))
                {auto Box=SNew(SHorizontalBox);if(Children)for(const auto& V:*Children)Box->AddSlot().FillWidth(1).Padding(3)[Build(V->AsObject())];Content=Box;}
                else if(Type==TEXT("tabs"))
                {
                    auto Switch=SNew(SWidgetSwitcher);auto Buttons=SNew(SHorizontalBox);int Index=0;
                    if(Children)for(const auto& V:*Children)
                    {
                        const int Page=Index++;Switch->AddSlot()[Build(V->AsObject())];
                        Buttons->AddSlot().AutoWidth().Padding(2)[SNew(SButton).IsFocusable(false).OnClicked_Lambda([this,Key,Switch,Page](){Tabs.Add(Key,Page);Switch->SetActiveWidgetIndex(Page);return FReply::Handled();})[Label(V->AsObject())]];
                    }
                    Switch->SetActiveWidgetIndex(FMath::Clamp(Tabs.FindRef(Key),0,FMath::Max(0,Index-1)));
                    Content=SNew(SVerticalBox)+SVerticalBox::Slot().AutoHeight()[Buttons]+SVerticalBox::Slot().AutoHeight()[Switch];
                }
                else
                {
                    auto Box=SNew(SVerticalBox);if(Children)for(const auto& V:*Children)Box->AddSlot().AutoHeight().Padding(2)[Build(V->AsObject())];
                    if(Type==TEXT("section"))
                    {
                        if(!Expanded.Contains(Key))Expanded.Add(Key,B(R,TEXT("expanded"),true));
                        Content=SNew(SExpandableArea).InitiallyCollapsed(!Expanded[Key]).OnAreaExpansionChanged_Lambda([this,Key](bool Open){Expanded.Add(Key,Open);}).HeaderContent()[Label(R)].BodyContent()[Box];
                    }
                    else Content=Box;
                }
            }
            else if(Type==TEXT("button"))
                Content=SNew(SButton).Tag(FName(*(TEXT("PluginRow.")+Key))).IsFocusable(false).ContentPadding(FMargin(10,7))
                    .OnClicked_Lambda([H=Host,Plugin=Id,Key](){if(H.IsValid())H->ClickPluginRow(Plugin,Key);return FReply::Handled();})[Label(R)];
            else if(Type==TEXT("checkbox"))
                Content=SNew(SCheckBox).IsChecked_Lambda([R](){return B(R,TEXT("value"),false)?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
                    .OnCheckStateChanged_Lambda([Changed](ECheckBoxState Value){Changed(MakeShared<FJsonValueBoolean>(Value==ECheckBoxState::Checked));})[Label(R)];
            else if(Type==TEXT("input"))
                Content=SNew(SVerticalBox)+SVerticalBox::Slot().AutoHeight()[Label(R)]
                    +SVerticalBox::Slot().AutoHeight()[SNew(SEditableTextBox).Font(FCoreStyle::GetDefaultFontStyle("Regular",16)).Text(FText::FromString(S(R,TEXT("value")))).HintText(FText::FromString(S(R,TEXT("placeholder")))).SelectAllTextWhenFocused(false)
                    .OnTextChanged_Lambda([Changed,R](const FText& Text){if(B(R,TEXT("live"),false))Changed(MakeShared<FJsonValueString>(Text.ToString().Left(1024)));})
                    .OnTextCommitted_Lambda([Changed](const FText& Text,ETextCommit::Type){Changed(MakeShared<FJsonValueString>(Text.ToString().Left(1024)));})];
            else if(Type==TEXT("number"))
                Content=SNew(SVerticalBox)+SVerticalBox::Slot().AutoHeight()[Label(R)]
                    +SVerticalBox::Slot().AutoHeight()[SNew(SSpinBox<double>).Font(FCoreStyle::GetDefaultFontStyle("Regular",16)).MinValue(N(R,TEXT("min"))).MaxValue(N(R,TEXT("max"),100)).Delta(N(R,TEXT("step"),1)).Value(N(R,TEXT("value")))
                        .OnValueCommitted_Lambda([Changed](double V,ETextCommit::Type){Changed(MakeShared<FJsonValueNumber>(V));})
                        .OnEndSliderMovement_Lambda([Changed](double V){Changed(MakeShared<FJsonValueNumber>(V));})];
            else if(Type==TEXT("slider"))
                Content=SNew(SVerticalBox)+SVerticalBox::Slot().AutoHeight()[SNew(SHorizontalBox)+SHorizontalBox::Slot().FillWidth(1)[Label(R)]+SHorizontalBox::Slot().AutoWidth()[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular",16)).Text_Lambda([R](){return FText::AsNumber(N(R,TEXT("value")));})]]
                    +SVerticalBox::Slot().AutoHeight()[SNew(SSlider).MinValue(N(R,TEXT("min"))).MaxValue(N(R,TEXT("max"),100)).StepSize(N(R,TEXT("step"),1)).Value(N(R,TEXT("value")))
                        .OnValueChanged_Lambda([Changed](float V){Changed(MakeShared<FJsonValueNumber>(V));})];
            else if(Type==TEXT("select"))
            {
                auto Options=MakeShared<TArray<TSharedPtr<FString>>>();const TArray<TSharedPtr<FJsonValue>>* Values=nullptr;
                if(R->TryGetArrayField(TEXT("options"),Values))for(const auto& V:*Values)Options->Add(MakeShared<FString>(V->AsString()));
                Content=SNew(SVerticalBox)+SVerticalBox::Slot().AutoHeight()[Label(R)]
                    +SVerticalBox::Slot().AutoHeight()[SNew(SComboBox<TSharedPtr<FString>>).OptionsSource(&Options.Get())
                        .OnGenerateWidget_Lambda([Options](TSharedPtr<FString> V){return SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular",16)).Text(FText::FromString(V?*V:FString()));})
                        .OnSelectionChanged_Lambda([Options,Changed](TSharedPtr<FString> V,ESelectInfo::Type){if(V)Changed(MakeShared<FJsonValueString>(*V));})
                        [SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular",16)).Text_Lambda([R](){return FText::FromString(S(R,TEXT("value")));})]];
            }
            else if(Type==TEXT("progress"))
                Content=SNew(SVerticalBox)+SVerticalBox::Slot().AutoHeight()[Label(R)]+SVerticalBox::Slot().AutoHeight().Padding(0,4)[SNew(SProgressBar).Percent(float(FMath::Clamp(N(R,TEXT("value")),0.,1.))).FillColorAndOpacity(Color(S(R,TEXT("color"))))];
            else if(Type==TEXT("icon")||Type==TEXT("image"))
            {
                if(!Resolver.IsValid()&&Host.IsValid()){Resolver.Reset(NewObject<UACEUIResourceResolver>(Host.Get()));Resolver->Initialize(Host->GetGameInstance()->GetSubsystem<UACEDatSubsystem>());}
                auto Brush=MakeShared<FSlateBrush>();const double Size=FMath::Clamp(N(R,TEXT("size"),32),16.,128.);Brush->ImageSize=FVector2D(Size,Size);
                Brush->SetResourceObject(Type==TEXT("image")?Image(S(R,TEXT("asset"))):Resolver.IsValid()?Resolver->ResolveIconTexture(uint32(N(R,TEXT("icon")))):nullptr);Brushes.Add(Brush);
                Content=SNew(SHorizontalBox)+SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(Size).HeightOverride(Size)[SNew(SImage).Image(&Brush.Get())]]+SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(6,0)[Label(R)];
            }
            else Content=Label(R);
            Content->SetEnabled(B(R,TEXT("enabled")));Content->SetToolTipText(FText::FromString(S(R,TEXT("tooltip"))));
            return SNew(SBox).Tag(FName(*(TEXT("PluginControl.")+Key))).Padding(FMath::Clamp(float(N(R,TEXT("padding"),2)),0.f,24.f))[Content];
        }
        void Refresh()
        {
            if(!Host.IsValid())return;TSharedPtr<FACEClientPlugin> P;for(const auto& Candidate:Host->Plugins)if(Candidate->Id==Id){P=Candidate;break;}
            if(!P){Rows->ClearChildren();return;}if(Last.Pin()==P&&Revision==P->PanelRevision)return;
            // Keep the editor/caret and drag capture stable while Lua refreshes other values.
            if(Last.Pin()==P&&P->Running&&(Editing()||(FSlateApplication::IsInitialized()&&FSlateApplication::Get().HasAnyMouseCaptor())))return;
            if(Last.Pin()!=P){Images.Reset();ImageBytes=0;Resolver.Reset();Expanded.Reset();Tabs.Reset();}
            Last=P;Revision=P->PanelRevision;Rows->ClearChildren();Brushes.Reset();
            for(const auto& V:P->PanelRows)Rows->AddSlot().AutoHeight().Padding(2)[Build(V->AsObject())];
        }
    };
}
TSharedRef<SWidget> UACEPluginSubsystem::MakeCompanionPanel(const FString& Id)
{return SNew(SACECompanionRows).Host(this).Id(Id);}
