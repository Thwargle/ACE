#include "Mods/ACEPluginCosmetics.h"
#include "Serialization/JsonSerializer.h"
#include "Widgets/Images/SImage.h"
#include "Engine/Texture2D.h"
#include "UObject/StrongObjectPtr.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Rendering/DrawElements.h"
#include "Styling/AppStyle.h"

namespace
{
FString Text(const TSharedPtr<FJsonObject>& O,const TCHAR* K,const FString& Default=FString())
{ FString V;return O&&O->TryGetStringField(K,V)?V:Default; }
bool Ready(const TSharedPtr<FJsonObject>& O)
{ bool V=false;return O&&O->TryGetBoolField(TEXT("supported"),V)&&V; }
TSharedRef<SWidget> Label(const FString& S)
{ return SNew(STextBlock).Text(FText::FromString(S)).AutoWrapText(true); }
bool CanTarget(const TSharedPtr<FJsonObject>& O,const FString& Target)
{
    const TArray<TSharedPtr<FJsonValue>>* Values=nullptr;
    if(!O||!O->TryGetArrayField(TEXT("targets"),Values))return false;
    for(const auto& Value:*Values)if(Value->AsString()==Target||(Target==TEXT("offhand")&&Value->AsString()==TEXT("shield")))return true;
    return false;
}
FString SlotName(const FString& Id)
{
    if(Id==TEXT("weapon"))return TEXT("Main hand");
    if(Id==TEXT("offhand"))return TEXT("Off hand / shield");
    FString S=Id;if(!S.IsEmpty())S[0]=FChar::ToUpper(S[0]);return S;
}
// Widget-owned textures remain alive through GC, and disappear with the row.
// Only visible dropdown rows allocate previews; the full catalog holds metadata.
struct FCosmeticIcon
{
    FString Id;
    TStrongObjectPtr<UTexture2D> Texture;
    FSlateBrush Brush;
};
}

TSharedRef<SWidget> UACEPluginCosmetics::MakeCatalogIcon(TFunction<TSharedPtr<FJsonObject>()> Item,float Size)
{
    auto Icon=MakeShared<FCosmeticIcon>();TWeakObjectPtr<UACEPluginCosmetics> Self(this);
    return SNew(SBox).WidthOverride(Size).HeightOverride(Size)
        [SNew(SImage).Image_Lambda([Self,Item,Icon]()->const FSlateBrush*
        {
            const FString Id=Text(Item(),TEXT("cacheId"));
            if(Icon->Id!=Id)
            {
                Icon->Id=Id;Icon->Texture.Reset();Icon->Brush.SetResourceObject(nullptr);
                FGuid Guid;
                if(Self.IsValid()&&FGuid::ParseExact(Id,EGuidFormats::Digits,Guid))
                {
                    const FString Path=FPaths::Combine(Self->CacheRoot,TEXT("cache"),Id,TEXT("thumbnail.png"));
                    TArray<uint8> Png;const int64 Bytes=IFileManager::Get().FileSize(*Path);
                    if(LocalFile(Path)&&Bytes>=33&&Bytes<=256*1024&&FFileHelper::LoadFileToArray(Png,*Path))
                    {
                        const uint8 Signature[]={137,80,78,71,13,10,26,10};
                        auto BE=[&](int32 I){return (uint32(Png[I])<<24)|(uint32(Png[I+1])<<16)|(uint32(Png[I+2])<<8)|Png[I+3];};
                        if(FMemory::Memcmp(Png.GetData(),Signature,8)==0&&FMemory::Memcmp(Png.GetData()+12,"IHDR",4)==0&&BE(16)>0&&BE(20)>0&&BE(16)<=256&&BE(20)<=256)
                        {
                            Icon->Texture.Reset(FImageUtils::ImportBufferAsTexture2D(Png));
                            if(Icon->Texture.IsValid())
                            {
                                Icon->Brush.SetResourceObject(Icon->Texture.Get());Icon->Brush.DrawAs=ESlateBrushDrawType::Image;
                                Icon->Brush.ImageSize=FVector2D(Icon->Texture->GetSizeX(),Icon->Texture->GetSizeY());
                            }
                        }
                    }
                }
            }
            return Icon->Texture.IsValid()?&Icon->Brush:FAppStyle::GetBrush(TEXT("NoBrush"));
        })];
}

TSharedRef<SWidget> UACEPluginCosmetics::MakeCatalogRow(TSharedPtr<FJsonObject> Item)
{
    return SNew(SHorizontalBox)
        +SHorizontalBox::Slot().AutoWidth().Padding(4)[MakeCatalogIcon([Item](){return Item;},48)]
        +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(8,2)[Label(Text(Item,TEXT("name")))];
}

void UACEPluginCosmetics::UpdateCatalog(const FString& Json)
{
    TSharedPtr<FJsonObject> Root;const TArray<TSharedPtr<FJsonValue>>* Items=nullptr;
    if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root)||!Root||!Root->TryGetArrayField(TEXT("items"),Items))
    {Tell(TEXT("Invalid item catalog response."));return;}
    const auto Previous=SlotSelections;Catalog.Empty();SlotSelections.Empty();int32 Available=0;
    for(const auto& V:*Items)
    {
        auto O=V->AsObject();if(!O||Text(O,TEXT("name")).Len()>256||Text(O,TEXT("reason")).Len()>4096)continue;
        FGuid Id;if(!Ready(O)||!FGuid::ParseExact(Text(O,TEXT("cacheId")),EGuidFormats::Digits,Id))continue;
        Catalog.Add(O);Available+=Ready(O)?1:0;
    }
    for(const auto& Pair:Previous)
    {
        if(!Ready(Pair.Value))continue;
        SlotSelections.Add(Pair.Key,Pair.Value);
        for(const auto& Item:Catalog)
            if((Text(Item,TEXT("id"))==Text(Pair.Value,TEXT("id"))||(!Text(Pair.Value,TEXT("cacheId")).IsEmpty()&&Text(Item,TEXT("cacheId"))==Text(Pair.Value,TEXT("cacheId"))))&&CanTarget(Item,Pair.Key))
            {SlotSelections.Add(Pair.Key,Item);break;}
    }
    int32 Remaining=0;Root->TryGetNumberField(TEXT("remaining"),Remaining);
    CatalogStatus=FString::Printf(TEXT("%d ready appearances · %d candidates left to scan"),Available,Remaining);
    SelectSlot(SelectedSlot);Tell(TEXT("Local item catalog loaded. Select a paperdoll slot, then an appearance."));
}

void UACEPluginCosmetics::SelectSlot(const FString& Id)
{
    SelectedSlot=Id;SlotOptions.Empty();
    for(const auto& Item:Catalog)if(Ready(Item)&&CanTarget(Item,Id)&&(CatalogFilter.IsEmpty()||Text(Item,TEXT("name")).Contains(CatalogFilter)))SlotOptions.Add(Item);
    SlotOptions.Sort([](const auto& A,const auto& B){if(Ready(A)!=Ready(B))return Ready(A);return Text(A,TEXT("name"))<Text(B,TEXT("name"));});
    if(ComboOptions)*ComboOptions=SlotOptions;
    if(auto Combo=ItemCombo.Pin())
    {
        Combo->RefreshOptions();
        if(auto* Selected=SlotSelections.Find(Id))Combo->SetSelectedItem(*Selected);else Combo->ClearSelection();
    }
    if(auto* Selected=SlotSelections.Find(Id))SelectCatalogItem(*Selected);
}

void UACEPluginCosmetics::SelectCatalogItem(const TSharedPtr<FJsonObject>& Item)
{
    if(!Ready(Item)||!CanTarget(Item,SelectedSlot))return;
    SlotSelections.Add(SelectedSlot,Item);
    const TSharedPtr<FJsonObject>* Values=nullptr;
    if(Item->TryGetObjectField(TEXT("values"),Values))
        for(auto& Pair:Inputs)if((*Values)->HasField(Pair.Key))Pair.Value=Text(*Values,*Pair.Key);
    Inputs.Add(TEXT("slot"),SelectedSlot==TEXT("offhand")&&Text(Item,TEXT("sourceSlot"))==TEXT("shield")?TEXT("shield"):SelectedSlot);
}

TSharedRef<SWidget> UACEPluginCosmetics::MakePaperdollPanel()
{
    TWeakObjectPtr<UACEPluginCosmetics> Self(this);auto Body=SNew(SVerticalBox);
    auto Add=[&](TSharedRef<SWidget> W){Body->AddSlot().AutoHeight().Padding(4)[W];};
    auto Button=[&](const FString& Caption,const FString& Operation)->TSharedRef<SWidget>
    {return SNew(SButton).IsFocusable(false).ContentPadding(8)
        .IsEnabled_Lambda([Self,Operation](){return Self.IsValid()&&(Operation==TEXT("off")||!Self->ToolProcess.IsValid());})
        .OnClicked_Lambda([Self,Operation](){if(Self.IsValid())Self->QueueRequest(Operation);return FReply::Handled();})[Label(Caption)];};
    Add(SNew(STextBlock).Text_Lambda([Self](){return FText::FromString(Self.IsValid()?Self->Status:TEXT("Unavailable"));}).AutoWrapText(true));
    Add(SNew(SHorizontalBox)
        +SHorizontalBox::Slot().FillWidth(1)[Button(TEXT("Find local ")+SourceLabel+TEXT(" installation"),TEXT("scan"))]
        +SHorizontalBox::Slot().FillWidth(1).Padding(4,0)[Button(TEXT("Scan all local items"),TEXT("scan-items"))]
        +SHorizontalBox::Slot().FillWidth(1)[Button(TEXT("Load last catalog"),TEXT("catalog-items"))]);
    auto Installs=SNew(SVerticalBox);InstallationList=Installs;Add(Installs);
    for(const auto& Install:Installations)
        Installs->AddSlot().AutoHeight()[SNew(SButton).OnClicked_Lambda([Self,Install](){if(Self.IsValid())for(const TCHAR* K:{TEXT("root"),TEXT("product")})Self->Inputs[K]=Text(Install,K);return FReply::Handled();})
            [Label(Text(Install,TEXT("product"))+TEXT(" — ")+Text(Install,TEXT("root")))]];
    Add(Label(TEXT("Choose appearances for any slots, then Apply outfit to wear all changes together.")));
    auto Grid=SNew(SGridPanel).FillColumn(0,1).FillColumn(1,1).FillColumn(2,1);
    auto SelectedItem=[Self]()->TSharedPtr<FJsonObject>{return Self.IsValid()?Self->SlotSelections.FindRef(Self->SelectedSlot):nullptr;};
    Grid->AddSlot(1,1).RowSpan(5).HAlign(HAlign_Center).VAlign(VAlign_Center)
        [SNew(SVerticalBox)
            +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[MakeCatalogIcon(SelectedItem,180)]
            +SVerticalBox::Slot().AutoHeight().Padding(4)[SNew(STextBlock).Justification(ETextJustify::Center).AutoWrapText(true)
                .Text_Lambda([SelectedItem](){return FText::FromString(SelectedItem()?Text(SelectedItem(),TEXT("name")):TEXT("Choose an appearance"));})]
            +SVerticalBox::Slot().AutoHeight().Padding(4)[Label(TEXT("Local model preview"))]];
    struct FSlot {const TCHAR* Id;int32 Column,Row;};
    const FSlot Slots[]={{TEXT("head"),1,0},{TEXT("shoulders"),0,1},{TEXT("back"),2,1},{TEXT("arms"),0,2},{TEXT("chest"),2,2},
        {TEXT("hands"),0,3},{TEXT("waist"),2,3},{TEXT("weapon"),0,4},{TEXT("offhand"),2,4},{TEXT("legs"),0,5},{TEXT("feet"),2,5}};
    for(const auto& S:Slots)
    {
        FString Id=S.Id;
        Grid->AddSlot(S.Column,S.Row).Padding(3).VAlign(VAlign_Center)[SNew(SButton).IsFocusable(false).ContentPadding(FMargin(8,10))
            .ButtonColorAndOpacity_Lambda([Self,Id](){return Self.IsValid()&&Self->SelectedSlot==Id?FLinearColor(.12f,.45f,.55f):FLinearColor(.12f,.15f,.19f);})
            .OnClicked_Lambda([Self,Id](){if(Self.IsValid())Self->SelectSlot(Id);return FReply::Handled();})
            [SNew(SHorizontalBox)
                +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[MakeCatalogIcon([Self,Id](){return Self.IsValid()?Self->SlotSelections.FindRef(Id):nullptr;},44)]
                +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(6,0)
                    [SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(SlotName(Id)))]]];
    }
    Add(Grid);
    Add(SNew(STextBlock).Text_Lambda([Self](){return FText::FromString(Self.IsValid()?SlotName(Self->SelectedSlot)+TEXT(" appearance"):FString());}));
    Add(SNew(SEditableTextBox).HintText(FText::FromString(TEXT("Search appearances by model name...")))
        .Text(FText::FromString(CatalogFilter)).OnTextChanged_Lambda([Self](const FText& Value){if(Self.IsValid()){Self->CatalogFilter=Value.ToString();Self->SelectSlot(Self->SelectedSlot);}}));
    TSharedPtr<SComboBox<TSharedPtr<FJsonObject>>> Combo;
    TSharedPtr<TArray<TSharedPtr<FJsonObject>>> Options=MakeShared<TArray<TSharedPtr<FJsonObject>>>();ComboOptions=Options;
    Add(SAssignNew(Combo,SComboBox<TSharedPtr<FJsonObject>>).OptionsSource(Options.Get()).MaxListHeight(300)
        .OnGenerateWidget_Lambda([Self,Options](TSharedPtr<FJsonObject> O)->TSharedRef<SWidget>
        {
            if(!Self.IsValid())return Label(TEXT("Unavailable"));
            return Self->MakeCatalogRow(O);
        })
        .OnSelectionChanged_Lambda([Self](TSharedPtr<FJsonObject> O,ESelectInfo::Type){if(Self.IsValid())Self->SelectCatalogItem(O);})
        [SNew(SHorizontalBox)+SHorizontalBox::Slot().AutoWidth().Padding(4)[MakeCatalogIcon(SelectedItem,48)]
            +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[SNew(STextBlock).AutoWrapText(true).Text_Lambda([Self](){if(!Self.IsValid())return FText();auto* Selected=Self->SlotSelections.Find(Self->SelectedSlot);
            return FText::FromString(Selected?Text(*Selected,TEXT("name")):Self->SlotOptions.IsEmpty()?TEXT("No local candidates for this slot"):TEXT("Choose an appearance..."));})]]);
    ItemCombo=Combo;SelectSlot(SelectedSlot);
    Add(SNew(STextBlock).AutoWrapText(true).Text_Lambda([Self](){if(!Self.IsValid())return FText();auto* Selected=Self->SlotSelections.Find(Self->SelectedSlot);
        if(Selected)return FText::FromString(Self->SourceLabel+TEXT(" ")+Text(*Selected,TEXT("sourceSlot"))+TEXT(" → AC ")+SlotName(Self->SelectedSlot)+TEXT("\n")+Text(*Selected,TEXT("reason")));
        return FText::FromString(Self->SelectedSlot==TEXT("weapon")||Self->SelectedSlot==TEXT("offhand")?TEXT("Weapons, wands and unusual held objects can use an AC hand attachment. Equip the matching AC item; use Fit to adjust orientation."):
            Self->IsBodySlot(Self->SelectedSlot)?TEXT("Armor follows your animated body and replaces covered AC clothing. Use Fit to adjust placement."):TEXT("This slot needs a skinned clothing adapter. Only successfully converted appearances are listed."));}));
    Add(SNew(SButton).ContentPadding(10).IsEnabled_Lambda([Self](){if(!Self.IsValid()||Self->ToolProcess.IsValid())return false;
        return true;})
        .OnClicked_Lambda([Self](){if(Self.IsValid())Self->QueueRequest(TEXT("apply-outfit"));return FReply::Handled();})[Label(TEXT("Apply outfit"))]);
    Add(SNew(SButton).ContentPadding(6).IsEnabled_Lambda([Self](){return Self.IsValid()&&!Self->ToolProcess.IsValid()&&(Self->SelectedSlot==TEXT("weapon")||Self->SelectedSlot==TEXT("offhand")||Self->IsBodySlot(Self->SelectedSlot));})
        .OnClicked_Lambda([Self](){if(Self.IsValid()){Self->SlotSelections.Remove(Self->SelectedSlot);Self->Tell(TEXT("Slot cleared from draft. Apply outfit to save your changes."));}return FReply::Handled();})[Label(TEXT("Clear selected slot from draft"))]);
    auto FitBody=SNew(SVerticalBox);
    FitBody->AddSlot().AutoHeight()[Label(TEXT("Adjust the selected slot only. Changes apply for this session; reloading restores the converter's mount."))];
    const TCHAR* FitNames[]={TEXT("X (cm)"),TEXT("Y (cm)"),TEXT("Z (cm)"),TEXT("Pitch"),TEXT("Yaw"),TEXT("Roll"),TEXT("Scale")};
    for(int32 I=0;I<7;++I)
        FitBody->AddSlot().AutoHeight().Padding(2)[SNew(SHorizontalBox)
            +SHorizontalBox::Slot().FillWidth(1)[Label(FitNames[I])]
            +SHorizontalBox::Slot().FillWidth(1)[SNew(SNumericEntryBox<float>).MinValue(I==6?.01f:-500.f).MaxValue(I==6?10.f:500.f)
                .IsEnabled_Lambda([Self](){return Self.IsValid()&&Self->RendererForSlot(Self->SelectedSlot)!=nullptr;})
                .Value_Lambda([Self,I]()->TOptional<float>{if(!Self.IsValid())return {};auto* R=Self->RendererForSlot(Self->SelectedSlot);if(!R)return {};
                    const FVector P=R->Fit.GetLocation();const FRotator Q=R->Fit.Rotator();return I<3?P[I]:I==3?Q.Pitch:I==4?Q.Yaw:I==5?Q.Roll:R->Fit.GetScale3D().X;})
                .OnValueCommitted_Lambda([Self,I](float V,ETextCommit::Type){if(!Self.IsValid()||!FMath::IsFinite(V))return;auto* R=Self->RendererForSlot(Self->SelectedSlot);if(!R)return;
                    FVector P=R->Fit.GetLocation();FRotator Q=R->Fit.Rotator();float S=R->Fit.GetScale3D().X;V=FMath::Clamp(V,I==6?.01f:-500.f,I==6?10.f:500.f);
                    if(I<3)P[I]=V;else if(I==3)Q.Pitch=V;else if(I==4)Q.Yaw=V;else if(I==5)Q.Roll=V;else S=V;R->SetFit(P,Q,S);})]];
    Add(SNew(SExpandableArea).InitiallyCollapsed(true).HeaderContent()[Label(TEXT("Fit selected appearance"))].BodyContent()[FitBody]);
    Add(SNew(SHorizontalBox)+SHorizontalBox::Slot().FillWidth(1)[Button(TEXT("Discard changes / reload saved outfit"),TEXT("reload"))]
        +SHorizontalBox::Slot().FillWidth(1).Padding(4,0)[Button(TEXT("Restore AC appearance / cancel"),TEXT("off"))]);
    Add(SNew(STextBlock).AutoWrapText(true).Text_Lambda([Self](){return FText::FromString(Self.IsValid()?Self->CatalogStatus:FString());}));
    auto Advanced=SNew(SVerticalBox);
    for(const auto& Field:Fields)
    {
        const FString Key=Text(Field,TEXT("key"));Advanced->AddSlot().AutoHeight().Padding(2)[Label(Text(Field,TEXT("label"),Key))];
        Advanced->AddSlot().AutoHeight().Padding(2)[SNew(SEditableTextBox).Text_Lambda([Self,Key](){return FText::FromString(Self.IsValid()?Self->Inputs.FindRef(Key):FString());})
            .OnTextChanged_Lambda([Self,Key](const FText& V){if(Self.IsValid())Self->Inputs[Key]=V.ToString();})];
    }
    Advanced->AddSlot().AutoHeight().Padding(4)[SNew(SButton).IsEnabled_Lambda([Self](){return Self.IsValid()&&!Self->ToolProcess.IsValid();})
        .OnClicked_Lambda([Self](){if(Self.IsValid()){Self->SlotSelections.Remove(Self->SelectedSlot);Self->QueueRequest(TEXT("convert"));}return FReply::Handled();})[Label(TEXT("Convert manual FileDataIDs"))]];
    Add(SNew(SExpandableArea).InitiallyCollapsed(true).HeaderContent()[Label(TEXT("Advanced local source / manual conversion"))].BodyContent()[Advanced]);
    return Body;
}
