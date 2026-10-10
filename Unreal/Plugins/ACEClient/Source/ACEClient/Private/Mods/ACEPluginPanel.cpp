#include "Mods/ACEPluginPanel.h"
#include "Mods/ACEPluginSubsystem.h"
#include "ACEClientSubsystem.h"
#include "ACEDatSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "VR/ACEVRComponent.h"
#include "VR/ACEVRMenu.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/CoreStyle.h"
#include "Brushes/SlateRoundedBoxBrush.h"

namespace
{
    FString Text(const TSharedPtr<FJsonObject>& O, const TCHAR* Key)
    { FString V; if (O) O->TryGetStringField(Key, V); return V; }
    TSharedRef<SWidget> Label(const FString& S, int Size = 20)
    { return SNew(STextBlock).Text(FText::FromString(S)).Font(FCoreStyle::GetDefaultFontStyle("Regular", Size)).AutoWrapText(true); }
    const FButtonStyle& PluginButtonStyle()
    {
        static const FButtonStyle Style=FButtonStyle()
            .SetNormal(FSlateRoundedBoxBrush(FLinearColor(.05f,.07f,.10f,1),5.f,FLinearColor(.50f,.39f,.19f,1),1.f))
            .SetHovered(FSlateRoundedBoxBrush(FLinearColor(.12f,.16f,.22f,1),5.f,FLinearColor(.9f,.7f,.3f,1),2.f))
            .SetPressed(FSlateRoundedBoxBrush(FLinearColor(.2f,.15f,.06f,1),5.f));
        return Style;
    }
    TSharedRef<SWidget> Button(const FString& S, TFunction<void()> Fn)
    { return SNew(SButton).ButtonStyle(&PluginButtonStyle()).IsFocusable(false).ContentPadding(FMargin(12,8)).OnClicked_Lambda([Fn](){ Fn(); return FReply::Handled(); })[Label(S)]; }

    class SACEPlugins : public SCompoundWidget
    {
    public:
        SLATE_BEGIN_ARGS(SACEPlugins) {} SLATE_ARGUMENT(UACEPluginSubsystem*, Host) SLATE_ARGUMENT(FString, PluginId) SLATE_END_ARGS()
        void Construct(const FArguments& Args)
        {
            Host = Args._Host; Id = Args._PluginId; PluginOnly = !Id.IsEmpty();
            ChildSlot[SNew(SScrollBox) + SScrollBox::Slot()[SAssignNew(Body, SVerticalBox)]];
            Rebuild();
        }
    private:
        TWeakObjectPtr<UACEPluginSubsystem> Host;
        TSharedPtr<SVerticalBox> Body;
        TSharedPtr<SEditableTextBox> Name;
        TSharedPtr<SMultiLineEditableTextBox> Json;
        FString Id, Search;
        bool PluginOnly = false;
        void Add(TSharedRef<SWidget> W) { Body->AddSlot().AutoHeight().Padding(5)[W]; }
        void Save(const TSharedPtr<FACEClientPlugin>& P)
        {
            Host->SaveProfile(P->Id, P->ProfileName, Host->ProfileJson(P->Id));
            if(Json)Json->SetText(FText::FromString(Host->ProfileJson(P->Id)));
        }
        void Rebuild()
        {
            Body->ClearChildren(); if (!Host.IsValid()) return;
            if (!PluginOnly)
            {
                Add(Label(TEXT("Client plugins"), 28));
                Add(Label(TEXT("Enable a plugin to grant its listed actions. Start is manual each session. Manual movement takes priority without stopping plugins. /ucm stop stops UCM; /plugins opens this panel."), 18));
                Add(Button(TEXT("Reload installed plugins"), [this](){ Host->Discover(); Rebuild(); }));
                for (auto P : Host->Plugins)
                {
                    auto Row = SNew(SHorizontalBox);
                    Row->AddSlot().FillWidth(1)[Button(P->Name + TEXT("  ") + P->Version, [this,P](){ Id=P->Id; Rebuild(); })];
                    Row->AddSlot().AutoWidth().Padding(8,0)[SNew(SCheckBox).Padding(FMargin(6,10)).IsChecked_Lambda([P](){return P->Enabled?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
                        .OnCheckStateChanged_Lambda([this,P](ECheckBoxState S){Host->SetEnabled(P->Id,S==ECheckBoxState::Checked);})[Label(TEXT("Enabled"))]];
                    Add(Row);
                    Add(Label(P->Description, 18));
                    Add(Label(TEXT("Allowed actions: ") + FString::Join(P->Permissions, TEXT(", ")),18));
                    Add(SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular",18)).Text_Lambda([P](){return FText::FromString(P->Status);}).AutoWrapText(true));
                }
            }
            if (Id.IsEmpty() && Host->Plugins.Num()) Id=Host->Plugins[0]->Id;
            TSharedPtr<FACEClientPlugin> P;
            for (auto Candidate:Host->Plugins) if (Candidate->Id==Id) P=Candidate;
            if (!P) return;
            Add(SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular",18)).Text_Lambda([H=Host](){return FText::FromString(H.IsValid()?H->Notice:FString());}).AutoWrapText(true));
            if(P->Id==TEXT("waypoint")){Add(SNew(SBox).HeightOverride(650)[Host->MakeWaypointPanel()]);return;}
            if(P->Id==TEXT("looteditor")){Add(SNew(SBox).HeightOverride(650)[Host->MakeUCMPanel(TEXT("Standalone loot"))]);return;}
            if(P->Id==TEXT("ucmmicro")){Add(Host->MakeUCMMicroPanel());return;}
            if(P->Id==TEXT("ucm")){Add(SNew(SBox).HeightOverride(650)[Host->MakeUCMPanel()]);return;}
            Add(Label(P->Name,26));
            Add(SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular",18)).Text_Lambda([P](){return FText::FromString(P->Status);}).AutoWrapText(true));
            auto Actions=SNew(SHorizontalBox);
            Actions->AddSlot().FillWidth(1)[Button(TEXT("Start"),[this,P](){Host->Start(P->Id);})];
            Actions->AddSlot().FillWidth(1).Padding(8,0)[Button(TEXT("Stop"),[this,P](){Host->Stop(P->Id,TEXT("Stopped"),true);})]; Add(Actions);
            Add(Label(TEXT("Profile name")));
            Add(SAssignNew(Name,SEditableTextBox).Font(FCoreStyle::GetDefaultFontStyle("Regular",20)).Text(FText::FromString(P->ProfileName)));
            TArray<FString> SavedProfiles; IFileManager::Get().FindFiles(SavedProfiles, *(Host->UserDirectory()/TEXT("Profiles")/P->Id/TEXT("*.json")),true,false);
            SavedProfiles.Sort();
            for(const FString& File:SavedProfiles)
            {
                const FString Profile=FPaths::GetBaseFilename(File);
                Add(Button(TEXT("Load: ")+Profile,[this,P,Profile](){Host->LoadProfile(P->Id,Profile);Rebuild();}));
            }
            auto Profiles=SNew(SHorizontalBox);
            Profiles->AddSlot().FillWidth(1)[Button(TEXT("Save as"),[this,P](){Host->SaveProfile(P->Id,Name->GetText().ToString(),Host->ProfileJson(P->Id));Rebuild();})];
            Profiles->AddSlot().FillWidth(1).Padding(8,0)[Button(TEXT("Load profile"),[this,P](){if(!Host->LoadProfile(P->Id,Name->GetText().ToString()))Host->Notice=TEXT("Profile not found");Rebuild();})]; Add(Profiles);
            const TArray<TSharedPtr<FJsonValue>>* Fields=nullptr;
            if (P->Manifest->TryGetArrayField(TEXT("settings"),Fields)) for (const auto& Field:*Fields)
            {
                const auto O=Field->AsObject(); if(!O)continue;
                const FString Key=Text(O,TEXT("key")), Kind=Text(O,TEXT("type")), Caption=Text(O,TEXT("label"));
                if(Kind==TEXT("bool"))
                {
                    Add(SNew(SCheckBox).Padding(FMargin(6,10)).IsChecked_Lambda([P,Key](){bool V=false;P->Profile->TryGetBoolField(Key,V);return V?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
                        .OnCheckStateChanged_Lambda([this,P,Key](ECheckBoxState S){P->Profile->SetBoolField(Key,S==ECheckBoxState::Checked);Save(P);})[Label(Caption)]);
                }
                else if(Kind==TEXT("number"))
                {
                    double V=0,Min=0,Max=1000;P->Profile->TryGetNumberField(Key,V);O->TryGetNumberField(TEXT("min"),Min);O->TryGetNumberField(TEXT("max"),Max);
                    Add(Label(Caption));Add(SNew(SSpinBox<double>).Font(FCoreStyle::GetDefaultFontStyle("Regular",20)).MinValue(Min).MaxValue(Max).Value(V)
                        .OnValueCommitted_Lambda([this,P,Key](double N,ETextCommit::Type){P->Profile->SetNumberField(Key,N);Save(P);}));
                }
                else if(Kind==TEXT("choice"))
                {
                    Add(Label(Caption));auto Row=SNew(SHorizontalBox);const TArray<TSharedPtr<FJsonValue>>* Values=nullptr;
                    if(O->TryGetArrayField(TEXT("values"),Values))
                    {
                    for(const auto& Value:*Values)
                    {
                        const FString Choice=Value->AsString();
                        Row->AddSlot().FillWidth(1).Padding(2)[Button((Text(P->Profile,*Key)==Choice?TEXT("[ "):TEXT(""))+Choice+(Text(P->Profile,*Key)==Choice?TEXT(" ]"):TEXT("")),[this,P,Key,Choice](){P->Profile->SetStringField(Key,Choice);Save(P);Rebuild();})];
                    }
                    }
                    Add(Row);
                }
            }
            const TArray<TSharedPtr<FJsonValue>>* Tools=nullptr;
            if(P->Manifest->TryGetArrayField(TEXT("tools"),Tools))for(const auto& Tool:*Tools)
            {
                if(Tool->AsString()==TEXT("route"))
                {
                    const TArray<TSharedPtr<FJsonValue>>* Route=nullptr;P->Profile->TryGetArrayField(TEXT("route"),Route);
                    Add(Label(FString::Printf(TEXT("Route: %d waypoints. Walk the path and record points at corners. Same landblock only; no automatic jumps or portals."),Route?Route->Num():0),18));
                    Add(Button(TEXT("Record current position"),[this,P](){Host->RecordWaypoint(P->Id);Rebuild();}));
                    Add(Button(TEXT("Remove last waypoint"),[this,P](){const TArray<TSharedPtr<FJsonValue>>* R=nullptr;if(P->Profile->TryGetArrayField(TEXT("route"),R)&&R->Num()){auto Copy=*R;Copy.Pop();P->Profile->SetArrayField(TEXT("route"),Copy);Save(P);Rebuild();}}));
                }
                else if(Tool->AsString()==TEXT("spells"))
                {
                    Add(Label(TEXT("Buff families and attack spell"),24));
                    Add(Label(TEXT("Toggle a Self buff to maintain its family. UCM chooses the strongest known version within your magic skill margin. Choose an attack spell for magic combat."),18));
                    Add(SNew(SEditableTextBox).Font(FCoreStyle::GetDefaultFontStyle("Regular",20)).HintText(FText::FromString(TEXT("Filter known spells; press Enter"))).Text(FText::FromString(Search))
                        .OnTextCommitted_Lambda([this](const FText& V,ETextCommit::Type){Search=V.ToString();Rebuild();}));
                    auto List=SNew(SVerticalBox);auto* C=Host->GetGameInstance()->GetSubsystem<UACEClientSubsystem>();auto* D=Host->GetGameInstance()->GetSubsystem<UACEDatSubsystem>();
                    const TArray<TSharedPtr<FJsonValue>>* Buffs=nullptr;P->Profile->TryGetArrayField(TEXT("buffs"),Buffs);
                    TSet<uint32> SelectedCategories; if(Buffs)for(auto V:*Buffs){uint32 S,W,Cat,F;double Dur;if(D->TryGetPluginSpellInfo(V->AsNumber(),S,W,Cat,F,Dur))SelectedCategories.Add(Cat);}
                    TArray<int32> Known=C->GetKnownSpells();Known.Sort();TSet<uint32> Seen;
                    for(int32 Spell:Known)
                    {
                        FString SpellName;uint32 Icon=0,S=0,W=0,Cat=0,F=0;double Dur=0;
                        if(!D->TryGetSpellInfo(Spell,SpellName,Icon)||!D->TryGetPluginSpellInfo(Spell,S,W,Cat,F,Dur)||(!Search.IsEmpty()&&!SpellName.Contains(Search)))continue;
                        const bool Buff=(F&8)&&(F&4)&&Dur>0;
                        if(Buff&&Seen.Contains(Cat))continue;
                        if(Buff)Seen.Add(Cat);else if(F&4)continue;
                        const FString Caption=(Buff?(SelectedCategories.Contains(Cat)?TEXT("[Selected buff] "):TEXT("[Buff] ")):TEXT("[Attack] "))+SpellName;
                        List->AddSlot().AutoHeight().Padding(2)[Button(Caption,[this,P,Spell,Buff,Cat,D](){
                            if(Buff){const TArray<TSharedPtr<FJsonValue>>* B=nullptr;TArray<TSharedPtr<FJsonValue>> Keep;bool Removed=false;
                                if(P->Profile->TryGetArrayField(TEXT("buffs"),B))for(auto V:*B){uint32 S,W,C,F;double Dur;if(D->TryGetPluginSpellInfo(V->AsNumber(),S,W,C,F,Dur)&&C==Cat)Removed=true;else Keep.Add(V);}
                                if(!Removed)Keep.Add(MakeShared<FJsonValueNumber>(Spell));P->Profile->SetArrayField(TEXT("buffs"),Keep);
                            }else P->Profile->SetNumberField(TEXT("attack_spell"),Spell);Save(P);Rebuild();})];
                    }
                    Add(SNew(SBox).HeightOverride(280)[SNew(SScrollBox)+SScrollBox::Slot()[List]]);
                }
            }
            Add(Label(TEXT("Advanced profile and state rules (JSON)"),24));
            Add(Label(TEXT("Edit buffs, waypoints, and states here, then Apply profile. Invalid scripts stop with an error above. Profile changes stop automation."),18));
            Add(SNew(SBox).HeightOverride(240)[SAssignNew(Json,SMultiLineEditableTextBox).Font(FCoreStyle::GetDefaultFontStyle("Regular",18)).Text(FText::FromString(Host->ProfileJson(P->Id)))]);
            Add(Button(TEXT("Apply profile"),[this,P](){if(Host->SaveProfile(P->Id,Name->GetText().ToString(),Json->GetText().ToString()))Rebuild();}));
        }
    };
}

TSharedRef<SWidget> UACEPluginSubsystem::MakePanel(const FString& PluginId) { if(PluginId==TEXT("ucm.log"))return MakeUCMLogPanel();if(PluginId==TEXT("ucmmicro"))return MakeUCMMicroPanel();if(PluginId==TEXT("waypoint") || PluginId==TEXT("waypoint.map"))return MakeWaypointPanel(PluginId.EndsWith(TEXT(".map")));if(PluginId==TEXT("looteditor"))return MakeUCMPanel(TEXT("Standalone loot"));if(PluginId==TEXT("ucm"))return MakeUCMPanel();return SNew(SACEPlugins).Host(this).PluginId(PluginId); }
void UACEPluginSubsystem::TogglePanel()
{
    if(auto* PC=GetGameInstance()->GetFirstLocalPlayerController())if(APawn* Pawn=PC->GetPawn())
        if(auto* VR=Pawn->FindComponentByClass<UACEVRComponent>();VR && VR->IsActive())
        {
            VR->OpenPluginManager(); return;
        }
    auto* Viewport=GetGameInstance()->GetGameViewportClient(); if(!Viewport)return;
    if(DesktopPanel){Viewport->RemoveViewportWidgetContent(DesktopPanel.ToSharedRef());DesktopPanel.Reset();return;}
    DesktopPanel=SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center)
        [SNew(SBox).WidthOverride(840).HeightOverride(650)
        [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(.025f,.035f,.045f,1.f)).Padding(16)
        [SNew(SVerticalBox)+SVerticalBox::Slot().AutoHeight()[Button(TEXT("Close plugins"),[Weak=TWeakObjectPtr<UACEPluginSubsystem>(this)](){if(Weak.IsValid())Weak->TogglePanel();})]
        +SVerticalBox::Slot().FillHeight(1)[MakePanel()]]]];
    Viewport->AddViewportWidgetContent(DesktopPanel.ToSharedRef(),1600);
}
TSharedRef<SWidget> UACEPluginPanel::RebuildWidget()
{
    if(auto* GI=GetWorld()?GetWorld()->GetGameInstance():nullptr) if(auto* Host=GI->GetSubsystem<UACEPluginSubsystem>())return Host->MakePanel();
    return SNew(STextBlock).Text(FText::FromString(TEXT("Plugin manager unavailable")));
}
