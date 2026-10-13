#include "Mods/ACELedger.h"
#include "Mods/ACEPluginSubsystem.h"
#include "Mods/ACEPluginStyle.h"
#include "ACEClientSubsystem.h"
#include "ACESession.h"
#include "Engine/GameInstance.h"
#include "Misc/SecureHash.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Images/SImage.h"
#include "Styling/CoreStyle.h"

bool UACEPluginSubsystem::UpdateLedgerCharacter()
{
    auto* C=GetGameInstance()->GetSubsystem<UACEClientSubsystem>();
    if(!C)return false;
    const auto State=C->GetSessionState();
    if(State!=EACESessionState::InWorld&&State!=EACESessionState::EnteringWorld)
    {LedgerCharacter.Empty();return false;}
    const int32 Player=C->GetPlayerGuid();if(!Player)return false;
    const FString Key=FMD5::HashAnsiString(*C->GetServerName())+FString::Printf(TEXT(".%08X"),uint32(Player));
    if(Key==LedgerCharacter)return true;
    LedgerCharacter=Key;LedgerSupplies.Reset();++SupplyRevision;SupplyInventoryRevision=MAX_uint64;
    const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;
    if(Settings&&Settings->TryGetArrayField(TEXT("supplies.")+Key,Rows))for(const auto& Row:*Rows)
    {
        const auto O=Row->AsObject();if(!O)continue;FACESupplyEntry Entry;
        O->TryGetNumberField(TEXT("class"),Entry.ClassId);O->TryGetNumberField(TEXT("icon"),Entry.Icon);O->TryGetStringField(TEXT("name"),Entry.Name);
        O->TryGetNumberField(TEXT("overlay"),Entry.Overlay);O->TryGetNumberField(TEXT("underlay"),Entry.Underlay);
        O->TryGetNumberField(TEXT("effects"),Entry.Effects);O->TryGetNumberField(TEXT("item_type"),Entry.ItemType);
        if(!Entry.Name.IsEmpty())LedgerSupplies.Add(Entry);
    }
    ResetHuntLedger();return true;
}
void UACEPluginSubsystem::ResetHuntLedger()
{
    HuntLedger.Reset(FPlatformTime::Seconds());
    if(auto* C=GetGameInstance()->GetSubsystem<UACEClientSubsystem>())HuntLedger.Observe(C->GetPlayerVitalsView(),FPlatformTime::Seconds());
}
void UACEPluginSubsystem::ObserveLedgerVitals(const FACEPlayerVitals& V)
{
    if(UpdateLedgerCharacter())HuntLedger.Observe(V,FPlatformTime::Seconds());
}
void UACEPluginSubsystem::UpdateLedgers()
{
    if(!UpdateLedgerCharacter())return;
    auto* C=GetGameInstance()->GetSubsystem<UACEClientSubsystem>();const auto S=C->GetSession();if(!S)return;
    if(SupplyInventoryRevision==S->GetInventoryDataRevision())return;
    SupplyInventoryRevision=S->GetInventoryDataRevision();
    ACELedger::Count(LedgerSupplies,C->GetPlayerGuid(),S->GetWorldObjects());
}
void UACEPluginSubsystem::SaveSupplies()
{
    if(LedgerCharacter.IsEmpty())return;
    if(!Settings)Settings=MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> Rows;
    for(const auto& E:LedgerSupplies)
    {
        auto O=MakeShared<FJsonObject>();O->SetNumberField(TEXT("class"),E.ClassId);O->SetNumberField(TEXT("icon"),E.Icon);O->SetStringField(TEXT("name"),E.Name);
        O->SetNumberField(TEXT("overlay"),E.Overlay);O->SetNumberField(TEXT("underlay"),E.Underlay);
        O->SetNumberField(TEXT("effects"),E.Effects);O->SetNumberField(TEXT("item_type"),E.ItemType);
        Rows.Add(MakeShared<FJsonValueObject>(O));
    }
    Settings->SetArrayField(TEXT("supplies.")+LedgerCharacter,Rows);SaveSettings();++SupplyRevision;SupplyInventoryRevision=MAX_uint64;
    UpdateLedgers();
}
void UACEPluginSubsystem::EditSupplySelection(bool Add)
{
    if(!UpdateLedgerCharacter())return;
    auto* C=GetGameInstance()->GetSubsystem<UACEClientSubsystem>();FACEWorldObject Item;
    if(!C->GetWorldObject(C->GetSelectedObject().Guid,Item)||(!C->GetSession()||!ACELedger::IsOwned(Item,C->GetPlayerGuid(),C->GetSession()->GetWorldObjects())))
    {SupplyNotice=TEXT("Select an item in your inventory first.");return;}
    const int32 Index=LedgerSupplies.IndexOfByPredicate([&](const auto& E){return E.Matches(Item);});
    if(Add&&Index==INDEX_NONE){FACESupplyEntry E;E.ClassId=Item.WeenieClassId;E.CopyAppearance(Item);E.Name=Item.Name;LedgerSupplies.Add(E);}
    else if(!Add&&Index!=INDEX_NONE)LedgerSupplies.RemoveAt(Index);
    else return;
    SupplyNotice.Empty();SaveSupplies();
}
void UACEPluginSubsystem::RemoveSupply(int32 Index)
{if(LedgerSupplies.IsValidIndex(Index)){LedgerSupplies.RemoveAt(Index);SaveSupplies();}}
bool UACEPluginSubsystem::LedgerOption(const FString& Id,const FString& Key,bool Default) const
{bool V;return Settings&&Settings->TryGetBoolField(Id+TEXT(".")+Key,V)?V:Default;}
void UACEPluginSubsystem::SetLedgerOption(const FString& Id,const FString& Key,bool Value)
{if(!Settings)Settings=MakeShared<FJsonObject>();Settings->SetBoolField(Id+TEXT(".")+Key,Value);SaveSettings();}
FVector2D UACEPluginSubsystem::LedgerSize(const FString& Id) const
{
    const bool Locked=!IsPluginBarUnlocked();
    return Id==TEXT("supplyledger")?FVector2D(200,14+FMath::Clamp(LedgerSupplies.Num(),1,15)*26+(Locked?0:68+(SupplyNotice.IsEmpty()?0:28)))
        :FVector2D(290,Locked?104:154);
}

namespace
{
    class SACELedgerPanel : public SCompoundWidget
    {
    public:
        SLATE_BEGIN_ARGS(SACELedgerPanel){} SLATE_ARGUMENT(UACEPluginSubsystem*,Host) SLATE_ARGUMENT(FString,Id) SLATE_END_ARGS()
        void Construct(const FArguments& A)
        {Host=A._Host;Id=A._Id;Style=FACEPluginStyle::Use(A._Host);ChildSlot[SAssignNew(Body,SVerticalBox)];Rebuild();}
        virtual void Tick(const FGeometry& G,double T,float D) override
        {SCompoundWidget::Tick(G,T,D);if(Host.IsValid()&&(Revision!=Host->GetSupplyRevision()||Locked!=!Host->IsPluginBarUnlocked()))Rebuild();}
    private:
        TWeakObjectPtr<UACEPluginSubsystem> Host;FString Id;
        TSharedPtr<FACEPluginStyle> Style;TSharedPtr<SVerticalBox> Body;
        uint32 Revision=0;bool Locked=false;
        TSharedRef<SWidget> Label(const FString& Text)
        {return SNew(SACEPluginText).Text(FText::FromString(Text)).Font(FCoreStyle::GetDefaultFontStyle("Regular",12));}
        TSharedRef<SWidget> Button(const FString& Text,const FString& Tip,TFunction<void()> Action)
        {return SNew(SButton).Tag(Text==TEXT("Reset run")?TEXT("LedgerReset"):TEXT("LedgerEdit")).IsFocusable(false).ButtonStyle(&Style->Button).ContentPadding(FMargin(8,3)).ToolTipText(FText::FromString(Tip)).OnClicked_Lambda([Action]{Action();return FReply::Handled();})[Label(Text)];}
        void Rebuild()
        {
            if(!Host.IsValid())return;FACEPluginStyle::Use(Host.Get());Revision=Host->GetSupplyRevision();Locked=!Host->IsPluginBarUnlocked();Body->ClearChildren();
            if(Id==TEXT("supplyledger"))
            {
                auto Rows=SNew(SVerticalBox);const auto& LedgerSupplies=Host->GetSupplies();
                if(LedgerSupplies.IsEmpty())Rows->AddSlot().AutoHeight()[Label(Locked?TEXT("No tracked supplies"):TEXT("Select an inventory item, then +"))];
                for(int32 I=0;I<LedgerSupplies.Num();++I)
                {
                    auto Row=SNew(SHorizontalBox).ToolTipText(FText::FromString(LedgerSupplies[I].Name+(Locked?TEXT("\nUse the game UI lock or hold Quick-show pinned windows to move and edit this HUD."):TEXT(""))));
                    const auto& Supply=LedgerSupplies[I];
                    Row->AddSlot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(22).HeightOverride(22)
                        [SNew(SScaleBox).Stretch(EStretch::ScaleToFit)[SNew(SOverlay)
                        +SOverlay::Slot()[SNew(SImage).Image(Style->ItemImage(Supply,false))]
                        +SOverlay::Slot()[SNew(SImage).Image(Style->ItemImage(Supply,true))]]]];
                    Row->AddSlot().FillWidth(1).VAlign(VAlign_Center).Padding(4,0)[SNew(SBox).Clipping(EWidgetClipping::ClipToBounds)[Label(LedgerSupplies[I].Name)]];
                    Row->AddSlot().AutoWidth().VAlign(VAlign_Center)[SNew(SACEPluginText).Font(FCoreStyle::GetDefaultFontStyle("Regular",12))
                        .Text_Lambda([H=Host,I]{return FText::AsNumber(H.IsValid()&&H->GetSupplies().IsValidIndex(I)?H->GetSupplies()[I].Count:0);})];
                    if(!Locked)Row->AddSlot().AutoWidth().Padding(4,0)[Button(TEXT("-"),TEXT("Remove this item from the ledger"),[H=Host,I]{if(H.IsValid())H->RemoveSupply(I);})];
                    Rows->AddSlot().AutoHeight().Padding(2,2)[Row];
                }
                Body->AddSlot().FillHeight(1)[SNew(SScrollBox)+SScrollBox::Slot()[Rows]];
                Body->SetToolTipText(FText::FromString(Locked?TEXT("Use the game UI lock or hold Quick-show pinned windows to move and edit this HUD."):TEXT("Track selected items across your inventory.")));
                if(!Locked)
                {
                    Body->AddSlot().AutoHeight().HAlign(HAlign_Left)[Button(TEXT("+"),TEXT("Track the selected inventory item"),[H=Host]{if(H.IsValid())H->EditSupplySelection(true);})];
                    Body->AddSlot().AutoHeight()[SNew(SACEPluginText).AutoWrapText(true).Font(FCoreStyle::GetDefaultFontStyle("Regular",10))
                        .Text_Lambda([H=Host]{return FText::FromString(H.IsValid()?H->SupplyNotice:FString());})];
                }
            }
            else
            {
                auto Row=[&](const FString& Caption,int32 Metric)
                {
                    auto R=SNew(SHorizontalBox);R->AddSlot().FillWidth(1)[Label(Caption)];
                    for(bool Lum:{false,true})R->AddSlot().FillWidth(1).HAlign(HAlign_Right)[SNew(SACEPluginText).Font(FCoreStyle::GetDefaultFontStyle("Regular",12))
                        .Text_Lambda([H=Host,Metric,Lum]
                        {
                            if(Metric<0)return FText::FromString(Lum?TEXT("Luminance"):TEXT("XP"));
                            if(!H.IsValid())return FText::GetEmpty();const auto& L=H->GetHuntLedger();const double Now=FPlatformTime::Seconds();
                            if(Metric==0){const auto Recent=L.LastFiveMinutes(Now);return FText::AsNumber(Lum?Recent.Lum:Recent.XP);}
                            const int64 Total=Lum?L.Lum:L.XP;return FText::AsNumber(Metric==1?L.PerHour(Total,Now):Total);
                        })];
                    Body->AddSlot().AutoHeight().Padding(3,3)[R];
                };
                Row(TEXT(""),-1);Row(TEXT("Last 5 min"),0);Row(TEXT("Per hour"),1);Row(TEXT("Total"),2);
                Body->SetToolTipText(FText::FromString(FString(TEXT("Last 5 min: gains in the rolling five-minute window. Per hour: average since login or Reset. Spending XP/luminance does not subtract earned totals. Closing this window keeps tracking."))
                    +(Locked?TEXT("\nUse the game UI lock or hold Quick-show pinned windows to move and edit this HUD."):TEXT(""))));
                if(!Locked)Body->AddSlot().AutoHeight()[Button(TEXT("Reset run"),TEXT("Clear totals and begin a new estimate now"),[H=Host]{if(H.IsValid())H->ResetHuntLedger();})];
            }
        }
    };
}
TSharedRef<SWidget> UACEPluginSubsystem::MakeLedgerPanel(const FString& Id)
{UpdateLedgers();return SNew(SACELedgerPanel).Host(this).Id(Id);}
