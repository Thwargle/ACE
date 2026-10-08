#include "Mods/ACEPluginSubsystem.h"
#include "ACEVTProfile.h"
#include "ACEUCMLootPresentation.h"
#include "ACEClientSubsystem.h"
#include "ACEDatSubsystem.h"
#include "UI/ACEUIResourceResolver.h"
#include "UI/ACERetailObjectNames.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Images/SImage.h"
#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Styling/CoreStyle.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Framework/Application/SlateApplication.h"

namespace ACEUCMPanelPrivate
{
    const FLinearColor Bg(.012f,.018f,.03f),Card(.024f,.035f,.052f),Ink(.9f,.94f,1),Muted(.48f,.58f,.7f),Accent(.12f,.65f,.85f);
    const FSlateBrush* White(){return FCoreStyle::Get().GetBrush("WhiteBrush");}
    const FButtonStyle& Style()
    {
        static const FButtonStyle B=FButtonStyle()
            .SetNormal(FSlateRoundedBoxBrush(FLinearColor(.04f,.075f,.11f),6.f))
            .SetHovered(FSlateRoundedBoxBrush(FLinearColor(.08f,.18f,.24f),6.f,Accent,1.f))
            .SetPressed(FSlateRoundedBoxBrush(FLinearColor(.03f,.3f,.42f),6.f));
        return B;
    }
    const FSliderStyle& SliderStyle()
    {
        static const FSliderStyle S=FSliderStyle().SetBarThickness(5.f)
            .SetNormalBarImage(FSlateRoundedBoxBrush(FLinearColor::White,3.f))
            .SetHoveredBarImage(FSlateRoundedBoxBrush(FLinearColor::White,3.f))
            .SetNormalThumbImage(FSlateRoundedBoxBrush(FLinearColor::White,7.f,FVector2f(16,24)))
            .SetHoveredThumbImage(FSlateRoundedBoxBrush(FLinearColor::White,7.f,FVector2f(18,26)));
        return S;
    }
    TSharedRef<STextBlock> Text(const FString& S,int Size=16,FLinearColor Color=Ink)
    {return SNew(STextBlock).Text(FText::FromString(S)).Font(FCoreStyle::GetDefaultFontStyle("Regular",Size)).ColorAndOpacity(Color).AutoWrapText(true);}
    TSharedRef<SWidget> Button(const FString& Caption,TFunction<void()> Action)
    {return SNew(SButton).ButtonStyle(&Style()).IsFocusable(false).ContentPadding(FMargin(12,9)).OnClicked_Lambda([Action](){Action();return FReply::Handled();})[Text(Caption)];}
    double Num(const TSharedPtr<FJsonObject>& P,const TCHAR* K,double D=0){double V;return P->TryGetNumberField(K,V)?V:D;}
    FString Str(const TSharedPtr<FJsonObject>& P,const TCHAR* K,const FString& D=FString()){FString V;return P->TryGetStringField(K,V)?V:D;}
    bool Bool(const TSharedPtr<FJsonObject>& P,const TCHAR* K,bool D=false){bool V;return P->TryGetBoolField(K,V)?V:D;}

    class SUCMPanel : public SCompoundWidget
    {
        friend class FACEUCMLootEditorTest;
    public:
        SLATE_BEGIN_ARGS(SUCMPanel){} SLATE_ARGUMENT(UACEPluginSubsystem*,Host) SLATE_ARGUMENT(FString,InitialPage) SLATE_END_ARGS()
        void Construct(const FArguments& A)
        {
            Host=A._Host;if(!A._InitialPage.IsEmpty())Page=A._InitialPage;for(auto P:Host->Plugins)if(P->Id==TEXT("ucm"))Plugin=P;
            if(!Plugin)return;
            LegacyFolder=Host->GetProfileFolder();RefreshLibrary();
            EditorOnly=Page==TEXT("Standalone loot");if(EditorOnly)Page=TEXT("Loot");
            if(Page==TEXT("Salvage groups")){Page=TEXT("Loot");EditingSalvage=true;}
            if(Page==TEXT("Loot editor")){Page=TEXT("Loot");ShowLootFilters=true;LootDraft=MakeShared<FJsonObject>();LootDraft->SetStringField(TEXT("action"),TEXT("keep"));LootDraft->SetStringField(TEXT("name_mode"),TEXT("prefix"));}
            ChildSlot[SNew(SBorder).BorderImage(White()).BorderBackgroundColor(Bg).Padding(14)
                [SNew(SVerticalBox)
                +SVerticalBox::Slot().AutoHeight()[Header()]
                +SVerticalBox::Slot().AutoHeight().Padding(0,12)[Tabs()]
                +SVerticalBox::Slot().FillHeight(1)[SAssignNew(BodyScroll,SScrollBox)+SScrollBox::Slot()[SAssignNew(Body,SVerticalBox)]]
                +SVerticalBox::Slot().AutoHeight().Padding(0,8)[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular",14)).ColorAndOpacity(Muted)
                    .AutoWrapText(true).Text_Lambda([H=Host](){return FText::FromString(H.IsValid()?H->Notice:FString());})]]];
            Rebuild();
        }
        virtual void Tick(const FGeometry& Geometry,const double Time,const float Delta) override
        {
            SCompoundWidget::Tick(Geometry,Time,Delta);
            const bool Narrow=Geometry.GetLocalSize().X<700;
            if(Narrow!=LootNarrow){LootNarrow=Narrow;if(Page==TEXT("Loot")&&!EditingSalvage)Rebuild();}
        }
    private:
        TWeakObjectPtr<UACEPluginSubsystem> Host;TSharedPtr<FACEClientPlugin> Plugin;
        TSharedPtr<SScrollBox> BodyScroll;
        TSharedPtr<SVerticalBox> Body;TSharedPtr<SEditableTextBox> ProfileName,MonsterName,NewStateName;
        TArray<TSharedPtr<FSlateBrush>> Brushes;
        bool EditorOnly=false,ShowBuffExceptions=false,ShowAdvancedProfile=false;FString BuffSearch;
        FString Page=TEXT("Overview"),Search,MonsterMode=TEXT("auto"),TransitionWhen=TEXT("elapsed"),TransitionTarget;
        double MonsterPriority=0,MonsterRange=0,PauseSeconds=5,TransitionValue=60;float JumpCharge=.5;int32 MonsterElement=0,StateIndex=0,ExpandedMonster=INDEX_NONE;
        TSharedPtr<FJsonObject> P()const{return Plugin->Profile;}
        void Save(bool Stop=false){Host->SaveProfile(Plugin->Id,Plugin->ProfileName,Host->ProfileJson(Plugin->Id),Stop);}
        void Add(TSharedRef<SWidget> W){Body->AddSlot().AutoHeight().Padding(0,0,0,10)[W];}
        void Heading(const FString& Title,const FString& Description)
        {Add(Text(Title,20));if(!Description.IsEmpty())Add(Text(Description,15,Muted));}
        TSharedRef<SWidget> Tile(TSharedRef<SWidget> W)
        {return SNew(SBorder).BorderImage(White()).BorderBackgroundColor(Card).Padding(8)[W];}
        TSharedRef<SWidget> Header()
        {
            if(EditorOnly)return SNew(SVerticalBox)
                +SVerticalBox::Slot().AutoHeight()[Text(TEXT("Loot Profile Editor"),28)]
                +SVerticalBox::Slot().AutoHeight().Padding(0,4,0,0)[Text(TEXT("Edits UCM's active loot profile. Opening or closing this window does not toggle looting."),14,Muted)];
            auto Row=SNew(SHorizontalBox);
            Row->AddSlot().FillWidth(1)[SNew(SVerticalBox)+SVerticalBox::Slot().AutoHeight()[Text(TEXT("UCM"),22)]
                +SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular",14)).ColorAndOpacity(Muted)
                    .Text_Lambda([P=Plugin](){return FText::FromString(TEXT("Setup: ")+P->ProfileName);})]
                +SVerticalBox::Slot().AutoHeight().Padding(0,6)[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular",14)).ColorAndOpacity(Accent).AutoWrapText(true)
                    .Text_Lambda([P=Plugin](){return FText::FromString(P->Status);})]];
            Row->AddSlot().AutoWidth().VAlign(VAlign_Center)[SNew(SButton).ButtonStyle(&Style()).IsFocusable(false).ContentPadding(FMargin(16,12))
                .OnClicked_Lambda([this](){if(Plugin->Running)Host->Stop(Plugin->Id,TEXT("Stopped"),true);else Host->Start(Plugin->Id);return FReply::Handled();})
                [SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Bold",18)).ColorAndOpacity(Ink)
                    .Text_Lambda([P=Plugin](){return FText::FromString(P->Running?TEXT("Stop"):P->CanResumeMeta?TEXT("Resume"):TEXT("Start"));})]];
            return Row;
        }
        TSharedRef<SWidget> Tabs()
        {
            auto Row=SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(4,4));
            if(EditorOnly){Row->AddSlot()[Button(TEXT("Vendors"),[this](){Page=TEXT("Vendors");Rebuild();})];Row->AddSlot()[Button(TEXT("Loot rules"),[this](){Page=TEXT("Loot");Rebuild();})];Row->AddSlot()[Button(TEXT("Import / compatibility"),[this](){Page=TEXT("Import tools");Rebuild();})];return Row;}
            for(const TCHAR* Name:{TEXT("Overview"),TEXT("Buffs"),TEXT("Buff others"),TEXT("Combat"),TEXT("Recovery"),TEXT("Route"),TEXT("Loot"),TEXT("Vendors"),TEXT("Rules"),TEXT("Metas"),TEXT("Profiles")})
                Row->AddSlot()[SNew(SButton).ButtonStyle(&Style()).IsFocusable(false).ContentPadding(FMargin(7,8))
                    .OnClicked_Lambda([this,Name](){Page=Name;BodyScroll->ScrollToStart();Rebuild();return FReply::Handled();})
                    [SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Bold",14)).Text(FText::FromString(Name))
                        .ColorAndOpacity_Lambda([this,Name](){return Page==Name?Accent:Muted;})]];
            return Row;
        }
        TSharedRef<SWidget> CombatToggle()
        {
            return SNew(SCheckBox).IsChecked_Lambda([this](){return Str(P(),TEXT("combat"),TEXT("off"))!=TEXT("off")?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
                .OnCheckStateChanged_Lambda([this](ECheckBoxState State){P()->SetStringField(TEXT("combat"),State==ECheckBoxState::Checked?TEXT("auto"):TEXT("off"));P()->SetBoolField(TEXT("manual_combat"),false);Save();})
                [Text(TEXT("Combat (automatic skill and equipment selection)"),16)];
        }
        TSharedRef<SWidget> Toggle(const TCHAR* Key,const FString& Caption,const FString& Help,bool Default=false)
        {
            return SNew(SCheckBox).IsChecked_Lambda([this,Key,Default](){return Bool(P(),Key,Default)?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
                .OnCheckStateChanged_Lambda([this,Key](ECheckBoxState State){P()->SetBoolField(Key,State==ECheckBoxState::Checked);Save();})
                .ToolTipText(FText::FromString(Help))
                [SNew(SBox).MinDesiredHeight(32).VAlign(VAlign_Center).Padding(8,0)[Text(Caption,16)]];
        }

        void Slider(const TCHAR* Key,const FString& Caption,float Min,float Max,float Default,FLinearColor Color=Accent,const FString& Unit=TEXT("%"))
        {
            auto V=SNew(SVerticalBox);
            V->AddSlot().AutoHeight()[SNew(SHorizontalBox)+SHorizontalBox::Slot().FillWidth(1)[Text(Caption,18)]
                +SHorizontalBox::Slot().AutoWidth()[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Bold",18)).ColorAndOpacity(Color)
                    .Text_Lambda([this,Key,Default,Unit](){return FText::FromString(FString::Printf(TEXT("%.0f%s"),Num(P(),Key,Default),*Unit));})]];
            V->AddSlot().AutoHeight().Padding(0,5)[SNew(SSlider).Style(&SliderStyle()).MinValue(Min).MaxValue(Max).StepSize(1).SliderHandleColor(Color).SliderBarColor(FLinearColor(.1f,.16f,.22f))
                .Value_Lambda([this,Key,Default](){return float(Num(P(),Key,Default));})
                .OnValueChanged_Lambda([this,Key](float N){P()->SetNumberField(Key,N);})
                .OnMouseCaptureEnd_Lambda([this](){Save();}).OnControllerCaptureEnd_Lambda([this](){Save();})];Add(Tile(V));
        }
        void Choices(const TCHAR* Key,const TArray<FString>& Values)
        {
            auto Row=SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(5,5));
            for(const auto& V:Values)
            {
                Row->AddSlot()[SNew(SButton).ButtonStyle(&Style()).IsFocusable(false).ContentPadding(FMargin(12,9))
                .OnClicked_Lambda([this,Key,V](){P()->SetStringField(Key,V);Save();return FReply::Handled();})
                [SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Bold",16)).Text(FText::FromString(V))
                .ColorAndOpacity_Lambda([this,Key,V](){return Str(P(),Key)==V?Accent:Muted;})]];
            }
            Add(Row);
        }
        void ToggleId(const TCHAR* Key,uint32 Id)
        {
            const TArray<TSharedPtr<FJsonValue>>* Old=nullptr;TArray<TSharedPtr<FJsonValue>> List;
            bool Removed=false;
            if(P()->TryGetArrayField(Key,Old))
            {
                for(auto V:*Old)
                {
                    if(uint32(V->AsNumber())==Id)Removed=true;
                    else List.Add(V);
                }
            }
            if(!Removed)List.Add(MakeShared<FJsonValueNumber>(Id));P()->SetArrayField(Key,List);Save();Rebuild();
        }
        bool Has(const TCHAR* Key,uint32 Id)const
        {const TArray<TSharedPtr<FJsonValue>>* Values=nullptr;if(P()->TryGetArrayField(Key,Values))for(auto V:*Values)if(uint32(V->AsNumber())==Id)return true;return false;}
        TSharedRef<SWidget> Icon(uint32 Did)
        {
            auto* R=Host->GetGameInstance()->GetSubsystem<UACEClientSubsystem>()->GetUIResourceResolver();
            TSharedPtr<FSlateBrush> B=MakeShared<FSlateBrush>();B->SetResourceObject(R?R->ResolveIconTexture(Did):nullptr);B->ImageSize=FVector2D(32,32);Brushes.Add(B);
            return SNew(SBox).WidthOverride(36).HeightOverride(36).HAlign(HAlign_Center).VAlign(VAlign_Center)[SNew(SImage).Image(B.Get())];
        }
        void Inventory(bool Consumables)
        {
            auto* C=Host->GetGameInstance()->GetSubsystem<UACEClientSubsystem>();TSet<int32> Seen;
            const TCHAR* Key=Consumables?TEXT("consumables"):TEXT("weapon_items");
            Heading(Consumables?TEXT("Recovery items"):TEXT("Equipment pool"),TEXT("An empty list allows automatic selection. Add individual weapons or supply types to restrict the pool. Refresh updates this list after inventory changes."));
            Add(Button(TEXT("Refresh inventory"),[this](){Rebuild();}));
            for(const auto& O:C->GetWorldObjects())
            {
                if(!Host->IsOwnedPluginItem(O)||(Consumables&&Seen.Contains(O.WeenieClassId)))continue;
                const auto* A=Host->GetPluginAppraisals().Find(O.Guid);
                const bool Pet=A&&A->bSuccess&&A->IntProperties.FindRef(280)==213;
                const bool Weapon=Pet||((O.ItemType&(ACEItemType::MeleeWeapon|ACEItemType::MissileWeapon|ACEItemType::Caster))!=0 && !(O.ValidLocations&0x800000))||((O.ItemType&ACEItemType::Armor)&&(O.ValidLocations&0x200000));
                if(Consumables?Weapon||!ACEInventoryRulesUsable(O):!Weapon)continue;
                Seen.Add(O.WeenieClassId);
                auto Row=SNew(SHorizontalBox);Row->AddSlot().AutoWidth()[Icon(O.IconId)];
                auto Details=SNew(SVerticalBox);Details->AddSlot().AutoHeight()[Text(ACERetailObjectNames::Name(O))];
                if(Pet)Details->AddSlot().AutoHeight().Padding(0,3)[Text(FString::Printf(TEXT("Pet essence  •  %d charges  •  Level %d  •  Summoning %d"),O.Structure,A->IntProperties.FindRef(369),A->IntProperties.FindRef(367)),12,Muted)];
                else if(A&&A->bSuccess&&!Consumables)Details->AddSlot().AutoHeight().Padding(0,3)[Text(FString::Printf(TEXT("Damage %d  •  Modifier %.2f  •  Speed %d%s"),A->Damage,A->WeaponDamageMod,A->WeaponTime,O.WielderId==C->GetPlayerGuid()?TEXT("  •  Equipped"):TEXT("")),12,Muted)];
                Row->AddSlot().FillWidth(1).Padding(10,0).VAlign(VAlign_Center)[Details];
                Row->AddSlot().AutoWidth().VAlign(VAlign_Center)[Text(A&&A->bSuccess?TEXT("Ready"):TEXT("Inspecting"),12,Muted)];
                const uint32 Entry=Consumables?uint32(O.WeenieClassId):uint32(O.Guid);
                Row->AddSlot().AutoWidth().Padding(8,0)[Button(Has(Key,Entry)?TEXT("Selected"):TEXT("Add"),[this,Key,Entry](){ToggleId(Key,Entry);})];Add(Tile(Row));
            }
        }
        bool ACEInventoryRulesUsable(const FACEWorldObject& O)const{return !(O.ItemUseable&1)&&!(O.ItemType&(ACEItemType::Container|ACEItemType::SpellComponents|ACEItemType::Money));}
        void ManaSources()
        {
            Heading(TEXT("Mana source items"),TEXT("Only checked individual items may be consumed to fill empty stones. Equipped and retained items are excluded. An empty selection consumes nothing."));
            Add(Button(TEXT("Refresh inventory"),[this](){Rebuild();}));
            Add(Button(TEXT("Clear mana source selection"),[this](){P()->SetArrayField(TEXT("mana_source_items"),{});Save();Rebuild();}));
            auto* C=Host->GetGameInstance()->GetSubsystem<UACEClientSubsystem>();
            for(const auto& O:C->GetWorldObjects())
            {
                const auto* A=Host->GetPluginAppraisals().Find(O.Guid);
                if(!Host->IsOwnedPluginItem(O)||O.WielderId||O.CurrentWieldedLocation
                    ||(O.ObjectDescriptionFlags&ACEObjectDescFlag::Retained)||(O.ItemType&(ACEItemType::ManaStone|ACEItemType::Container))
                    ||!A||!A->bSuccess||A->IntProperties.FindRef(107)<=0||A->IntProperties.FindRef(108)<=0)continue;
                const uint32 Id=uint32(O.Guid);
                auto Row=SNew(SHorizontalBox);Row->AddSlot().AutoWidth()[Icon(O.IconId)];
                Row->AddSlot().FillWidth(1).Padding(8,0).VAlign(VAlign_Center)[Text(FString::Printf(TEXT("%s - %d mana"),*ACERetailObjectNames::Name(O),A->IntProperties.FindRef(107)))];
                Row->AddSlot().AutoWidth()[Button(Has(TEXT("mana_source_items"),Id)?TEXT("Will consume"):TEXT("Allow consumption"),[this,Id](){ToggleId(TEXT("mana_source_items"),Id);})];
                Add(Tile(Row));
            }
        }
        void MonsterEquipment(const TSharedPtr<FJsonObject>& Rule,bool Offhand,bool DefaultMelee=false)
        {
            const TCHAR* Key=DefaultMelee?(Offhand?TEXT("melee_secondary_equip"):TEXT("melee_weapon")):(Offhand?TEXT("secondary_equip"):TEXT("weapon"));
            const double Selected=Num(Rule,Key,Offhand?0:-1);
            const TArray<FString> Modes=Offhand?TArray<FString>{TEXT("Automatic"),TEXT("Shield"),TEXT("Dual wield"),TEXT("Empty hand")}:DefaultMelee?TArray<FString>{TEXT("Automatic")}:TArray<FString>{TEXT("Automatic"),TEXT("Casting device")};
            FString Caption=TEXT("Missing item — choose another");
            for(int32 I=0;I<Modes.Num();++I)if(Selected==(Offhand?I:I-1))Caption=Modes[I];
            if(Offhand&&!Rule->HasField(Key))Caption=DefaultMelee?TEXT("Left-hand Tether / keep current"):TEXT("Use default hand settings");
            auto* C=Host->GetGameInstance()->GetSubsystem<UACEClientSubsystem>();
            TArray<FACEWorldObject> Items;
            for(const auto& O:C->GetWorldObjects())if(Host->IsOwnedPluginItem(O))
            {
                if(uint32(O.Guid)==Selected)Caption=ACERetailObjectNames::Name(O);
                const bool Melee=(O.ItemType&ACEItemType::MeleeWeapon)&&!(O.ValidLocations&0x2000000);
                const bool Shield=(O.ItemType&ACEItemType::Armor)&&(O.ValidLocations&0x200000);
                const bool Primary=(O.ItemType&(ACEItemType::MeleeWeapon|ACEItemType::MissileWeapon|ACEItemType::Caster))&&!(O.ValidLocations&0x800000);
                if(Offhand?(Melee||Shield):Primary&&(!DefaultMelee||(O.ItemType&ACEItemType::MeleeWeapon))&&!(O.ObjectDescriptionFlags&ACEObjectDescFlag::WieldLeft))Items.Add(O);
            }
            Items.Sort([](const auto& A,const auto& B){return ACERetailObjectNames::Name(A)<ACERetailObjectNames::Name(B);});
            Add(SNew(SComboButton).ButtonContent()[Text((Offhand?TEXT("Offhand: "):DefaultMelee?TEXT("Main hand: "):TEXT("Weapon: "))+Caption)]
                .OnGetMenuContent_Lambda([this,Rule,Key,Offhand,Modes,Items](){
                    auto List=SNew(SVerticalBox);
                    auto Select=[this,Rule,Key](double Value){Rule->SetNumberField(Key,Value);FSlateApplication::Get().DismissAllMenus();Save(true);Rebuild();};
                    if(Offhand)List->AddSlot().AutoHeight()[Button(TEXT("Use default / Left-hand Tether"),[this,Rule,Key](){Rule->RemoveField(Key);FSlateApplication::Get().DismissAllMenus();Save(true);Rebuild();})];
                    for(int32 I=0;I<Modes.Num();++I)List->AddSlot().AutoHeight()[Button(Modes[I],[Select,I,Offhand](){Select(Offhand?I:I-1);})];
                    for(const auto& O:Items)List->AddSlot().AutoHeight()[Button(ACERetailObjectNames::Name(O)+((O.ObjectDescriptionFlags&ACEObjectDescFlag::WieldLeft)?TEXT(" (Left-hand Tether)"):TEXT("")),[Select,Id=uint32(O.Guid)](){Select(Id);})];
                    return SNew(SBox).MaxDesiredHeight(400)[SNew(SScrollBox)+SScrollBox::Slot()[List]];
                }));
        }
        void MonsterDetails(const TSharedPtr<FJsonObject>& Rule)
        {
            MonsterEquipment(Rule,false);MonsterEquipment(Rule,true);
            auto Toggles=SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(4,4));
            for(const auto& Pair:TArray<TPair<FString,FString>>{{TEXT("debuff_yield"),TEXT("Yield")},{TEXT("debuff_imperil"),TEXT("Imperil")},{TEXT("debuff_vuln"),TEXT("Vulnerability")},{TEXT("ring"),TEXT("Ring spells")},{TEXT("debuff_gravity"),TEXT("Gravity Well")},{TEXT("debuff_broadside"),TEXT("Broadside")},{TEXT("debuff_fester"),TEXT("Fester")},{TEXT("debuff_weakening"),TEXT("Weakening Curse")},{TEXT("debuff_festering"),TEXT("Festering Curse")},{TEXT("debuff_corruption"),TEXT("Corruption")},{TEXT("debuff_destructive"),TEXT("Destructive Curse")},{TEXT("debuff_corrosion"),TEXT("Corrosion")}})
            {
                const bool Enabled=Bool(Rule,*Pair.Key,Bool(P(),*Pair.Key));
                Toggles->AddSlot()[Button(Pair.Value+(Enabled?TEXT(" • On"):TEXT(" • Off")),[this,Rule,Key=Pair.Key,Enabled](){Rule->SetBoolField(Key,!Enabled);Save(true);Rebuild();})];
            }
            Add(Toggles);Add(Text(TEXT("Secondary vulnerability"),14,Muted));
            auto Elements=SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(4,4));
            const TCHAR* Names[]={TEXT("None"),TEXT("Slash"),TEXT("Pierce"),TEXT("Blunt"),TEXT("Fire"),TEXT("Cold"),TEXT("Acid"),TEXT("Electric")};
            for(int32 I=0;I<8;++I){const int32 Element=I?1<<(I-1):0;Elements->AddSlot()[Button(FString(Names[I])+(Num(Rule,TEXT("secondary_vuln"))==Element?TEXT(" •"):TEXT("")),[this,Rule,Element](){Rule->SetNumberField(TEXT("secondary_vuln"),Element);Save(true);Rebuild();})];}
            Add(Elements);
        }
        void ForceBuffControls()
        {
            auto Row=SNew(SHorizontalBox);
            Row->AddSlot().AutoWidth()[Button(TEXT("Force Buff"),[this](){Host->RequestForceBuff();})];
            Row->AddSlot().AutoWidth().Padding(8,0)[SNew(SButton).ButtonStyle(&Style()).IsFocusable(false)
                .IsEnabled_Lambda([this](){return Host->IsForceBuffRequested();})
                .OnClicked_Lambda([this](){Host->CancelForceBuff();return FReply::Handled();})[Text(TEXT("Cancel refresh"))]];
            Add(Row);Add(Text(TEXT("Refresh all enabled buff families now, regardless of remaining time. If UCM is stopped, runs only buffs and recovery, then stops."),14,Muted));
        }
        double BuffMargin()const{return Num(P(),TEXT("buff_skill_margin"),Num(P(),TEXT("skill_margin"),30));}
        int32 BuffSchoolSkill(uint32 School)const
        {
            const int32 SkillIds[]={0,34,33,32,31,43};
            if(School>=UE_ARRAY_COUNT(SkillIds))return 0;
            const auto* C=Host->GetGameInstance()->GetSubsystem<UACEClientSubsystem>();
            for(const auto& Skill:C->GetPlayerVitalsView().Skills)
                if(Skill.SkillId==SkillIds[School]&&Skill.AdvancementClass>=2)return Skill.Current;
            return 0;
        }
        void BuffDifficultyControls()
        {
            Add(Text(TEXT("Spell level by school"),18));
            Add(Text(TEXT("Automatic highest chooses the strongest learned, usable buff. A selected level casts that exact tier; unavailable spells are skipped. These choices affect self buffs and buff requests, not vital recovery."),14,Muted));
            for(const auto& School:TArray<TPair<FString,FString>>{{TEXT("creature_buff_level"),TEXT("Creature Magic")},{TEXT("life_buff_level"),TEXT("Life Magic")},{TEXT("item_buff_level"),TEXT("Item Magic")}})
            {
                const FString Key=School.Key;
                const int32 Selected=FMath::Clamp(int32(Num(P(),*Key)),0,8);
                auto Menu=SNew(SVerticalBox);
                for(int32 Level=0;Level<=8;++Level)
                    Menu->AddSlot().AutoHeight()[Button(Level?FString::Printf(TEXT("Level %d"),Level):TEXT("Automatic highest"),[this,Key,Level]()
                    {P()->SetNumberField(Key,Level);Save();FSlateApplication::Get().DismissAllMenus();Rebuild();})];
                Add(SNew(SHorizontalBox)
                    +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[Text(School.Value)]
                    +SHorizontalBox::Slot().AutoWidth()[SNew(SComboButton).ButtonStyle(&Style()).ContentPadding(FMargin(12,8))
                        .ButtonContent()[Text(Selected?FString::Printf(TEXT("Level %d"),Selected):TEXT("Automatic highest"))]
                        .MenuContent()[Menu]]);
            }
            Slider(TEXT("buff_skill_margin"),TEXT("Extra skill required above spell difficulty"),0,150,
                float(Num(P(),TEXT("skill_margin"),30)),Accent,TEXT(" points"));
            Add(Text(TEXT("UCM chooses the strongest learned buff whose difficulty plus this buffer fits your current magic skill. Raising the buffer favors easier casts but can lower the spell level. Lower it to allow stronger buffs. This is a skill difference, not a success percentage."),14,Muted));
            auto Presets=SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(6,4));
            Presets->AddSlot()[Button(TEXT("Highest tiers (0 extra)"),[this](){P()->SetNumberField(TEXT("buff_skill_margin"),0);Save();})];
            Presets->AddSlot()[Button(TEXT("Default (30 extra)"),[this](){P()->SetNumberField(TEXT("buff_skill_margin"),30);Save();})];
            Add(Presets);
            auto* C=Host->GetGameInstance()->GetSubsystem<UACEClientSubsystem>();
            auto* D=Host->GetGameInstance()->GetSubsystem<UACEDatSubsystem>();
            // Cache DAT lookups on panel construction, not during Slate painting.
            TMap<uint32,TMap<uint32,uint32>> Levels;
            for(int32 Id:C->GetKnownSpells())
            {
                uint32 School=0,Power=0,Cat=0,Flags=0,Level=0;double Duration=0;
                if(D->TryGetPluginSpellInfo(Id,School,Power,Cat,Flags,Duration)&&(Flags&4)&&Duration>0
                    &&D->TryGetSpellSchoolAndLevel(Id,School,Level))
                    Levels.FindOrAdd(School).FindOrAdd(Power)=FMath::Max(Levels.FindOrAdd(School).FindRef(Power),Level);
            }
            for(uint32 School:{4u,2u,3u})
            {
                const FString Name=School==4?TEXT("Creature"):School==2?TEXT("Life"):TEXT("Item");
                const auto Tiers=Levels.FindRef(School);
                const FString LevelKey=School==4?TEXT("creature_buff_level"):School==2?TEXT("life_buff_level"):TEXT("item_buff_level");
                Add(Tile(SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular",15)).ColorAndOpacity(Ink).AutoWrapText(true)
                    .Text_Lambda([this,School,Name,Tiers,LevelKey]()
                    {
                        const int32 Skill=BuffSchoolSkill(School),Buffer=FMath::RoundToInt(BuffMargin());
                        const int32 Limit=Skill-Buffer;uint32 Level=0;
                        const uint32 Requested=uint32(Num(P(),*LevelKey));
                        for(const auto& Tier:Tiers)if(int64(Tier.Key)<=Limit&&(!Requested||Tier.Value==Requested))Level=FMath::Max(Level,Tier.Value);
                        const FString Result=Level?FString::Printf(TEXT("up to level %u learned buffs"),Level):TEXT("no learned buff fits");
                        return FText::FromString(FString::Printf(TEXT("%s: skill %d - buffer %d = difficulty %d max\n%s"),*Name,Skill,Buffer,Limit,*Result));
                    })));
            }
            Add(Text(TEXT("Live skill preview, including skill buffs. Individual buff families may have lower learned tiers; components, exclusions and temporary cast failures can also limit the final choice."),14,Muted));
        }
        void Rebuild()
        {
            Body->ClearChildren();Brushes.Empty();
            if(Page==TEXT("Overview"))
            {
                Heading(TEXT("Your hunting setup"),TEXT("Choose activities, then Start. Each activity can be adjusted while running. Manual movement takes priority without stopping UCM. Turn Combat off to fight manually while Loot stays on."));
                Add(SNew(SCheckBox)
                    .IsChecked_Lambda([this](){return Host->IsPluginBarVisible()?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
                    .OnCheckStateChanged_Lambda([this](ECheckBoxState State){Host->SetPluginBarVisible(State==ECheckBoxState::Checked);})
                    .ToolTipText(FText::FromString(TEXT("Hide or show the plugin bar. Plugins keep running. Open Settings > Game Play > Plugins / UCM to restore it. This preference applies to every profile.")))
                    [SNew(SBox).MinDesiredHeight(32).VAlign(VAlign_Center).Padding(8,0)[Text(TEXT("Show plugin bar"),16)]]);
                if(!Plugin->Enabled)Add(Button(TEXT("Enable UCM capabilities"),[this](){Host->SetEnabled(Plugin->Id,true);Rebuild();}));
                Add(Toggle(TEXT("buffing"),TEXT("Buff"),TEXT("Automatically maintains relevant creature, life and item enchantments."),true));
                ForceBuffControls();
                for(bool Bane:{false,true})
                {
                    const FString Key=Bane?TEXT("bane_profile"):TEXT("protection_profile"),Custom=Bane?TEXT("bane_custom"):TEXT("protection_custom");
                    const TArray<FString> Labels=Bane?TArray<FString>{TEXT("Custom"),TEXT("All"),TEXT("None"),TEXT("B"),TEXT("BPS"),TEXT("BPSA"),TEXT("BPSAC"),TEXT("ALFC")}:TArray<FString>{TEXT("Custom"),TEXT("All"),TEXT("None"),TEXT("B"),TEXT("BPS"),TEXT("BPSA"),TEXT("ALFC"),TEXT("BPSAC")};
                    const int32 Index=FMath::Clamp(int32(Num(P(),*Key,2))-1,0,7);
                    Add(Button((Bane?TEXT("Banes: "):TEXT("Protections: "))+Labels[Index],[this,Key,Index](){P()->SetNumberField(Key,(Index+1)%8+1);Save();Rebuild();}));
                    if(Index==0)Add(SNew(SEditableTextBox).Text(FText::FromString(Str(P(),*Custom,TEXT("ALL")))).HintText(FText::FromString(TEXT("A acid, B bludgeon, C cold, L lightning, F fire, P pierce, S slash")))
                        .OnTextCommitted_Lambda([this,Custom](const FText& Value,ETextCommit::Type){FString Letters=Value.ToString().ToUpper();if(Letters.Len()<=7){P()->SetStringField(Custom,Letters);Save();}}));
                }
                Add(CombatToggle());
                Add(Toggle(TEXT("idle_peace"),TEXT("Peace mode when idle"),TEXT("Enter peace mode when no eligible monsters are nearby, including while following a route. Return to the configured combat mode when attacking. Buffing, recovery and looting finish first.")));
                Add(Toggle(TEXT("recovery"),TEXT("Recover vitals"),TEXT("Use eligible spells and supplies at your health, stamina and mana thresholds."),true));
                Add(Toggle(TEXT("navigation"),TEXT("Follow route"),TEXT("Walk recorded points and perform portal, use and jump actions.")));
                Add(Toggle(TEXT("looting"),TEXT("Loot"),TEXT("Use your loot rules and, when enabled, collect learnable unknown spell scrolls.")));
            }
            else if(Page==TEXT("Buffs"))
            {
                Heading(TEXT("Automatic buffing"),TEXT("Uses your highest usable sustained buffs for attributes, trained skills, protections and equipment."));
                Add(Toggle(TEXT("buffing"),TEXT("Buff"),TEXT("Maintains buffs while UCM is running."),true));
                ForceBuffControls();
                Add(Toggle(TEXT("auto_buffs"),TEXT("Choose buffs automatically"),TEXT("Turn off only when using a manually authored buff list."),true));
                BuffDifficultyControls();
                Slider(TEXT("refresh_seconds"),TEXT("Rebuff before expiry"),10,300,60,Accent,TEXT(" sec"));
                Add(Toggle(TEXT("idle_buff_topoff"),TEXT("Refresh buffs while idle"),TEXT("Tops up buffs after combat and looting when no target is nearby.")));
                Slider(TEXT("idle_buff_seconds"),TEXT("Idle buff refresh window"),60,3600,1200,Accent,TEXT(" sec"));
                Add(Button(ShowBuffExceptions?TEXT("Hide buff exceptions"):TEXT("Edit buff exceptions"),[this](){ShowBuffExceptions=!ShowBuffExceptions;Rebuild();}));
                if(!ShowBuffExceptions)return;
                Add(SNew(SEditableTextBox).Text(FText::FromString(BuffSearch)).HintText(FText::FromString(TEXT("Find a buff family; press Enter")))
                    .OnTextCommitted_Lambda([this](const FText& Value,ETextCommit::Type){BuffSearch=Value.ToString();Rebuild();}));
                auto* C=Host->GetGameInstance()->GetSubsystem<UACEClientSubsystem>();auto* D=Host->GetGameInstance()->GetSubsystem<UACEDatSubsystem>();TSet<uint32> Seen;
                auto Known=C->GetKnownSpells();
                // Family labels should represent the strongest learned tier, not
                // the first (usually level I) ID returned by the spellbook.
                TMap<int32,uint32> Powers;
                for(int32 Id:Known){uint32 S=0,Pow=0,Cat=0,F=0;double Dur=0;if(D->TryGetPluginSpellInfo(Id,S,Pow,Cat,F,Dur))Powers.Add(Id,Pow);}
                Known.Sort([&Powers](int32 A,int32 B){const uint32 PA=Powers.FindRef(A),PB=Powers.FindRef(B);return PA==PB?A<B:PA>PB;});
                Add(Text(TEXT("Families show their highest learned sustained buff. Short burst spells require an explicit profile entry. The skill buffer determines the usable tier."),14,Muted));
                for(int32 Id:Known)
                {
                    uint32 School,Power,Cat,Flags,Icon;double Duration;FString Name;
                    if(!D->TryGetPluginSpellInfo(Id,School,Power,Cat,Flags,Duration)||!(Flags&4)||Duration<=0||(Duration<300&&!Has(TEXT("buffs"),Id))||Seen.Contains(Cat))continue;
                    Seen.Add(Cat);D->TryGetSpellInfo(Id,Name,Icon);if(!BuffSearch.IsEmpty()&&!Name.Contains(BuffSearch))continue;
                    Add(Tile(SNew(SHorizontalBox)+SHorizontalBox::Slot().AutoWidth()[IconWidget(Icon)]
                        +SHorizontalBox::Slot().FillWidth(1).Padding(10,0).VAlign(VAlign_Center)[Text(Name)]
                        +SHorizontalBox::Slot().AutoWidth()[Button(Has(TEXT("excluded_buffs"),Cat)?TEXT("Excluded"):TEXT("Automatic"),[this,Cat](){ToggleId(TEXT("excluded_buffs"),Cat);})]));
                }
            }
            else if(Page==TEXT("Vendors")){VendorPanel();}
            else if(Page==TEXT("Buff others"))
            {
                Heading(TEXT("Buff requests by tell"),TEXT("Players can request role-based buffs by tell while UCM and Buff are running."));
                Add(Toggle(TEXT("buff_others"),TEXT("Accept buff requests"),TEXT("One request per player, up to eight queued. Your recovery and self buffs take priority.")));
                Slider(TEXT("buff_other_range"),TEXT("Requester distance"),3,30,20,Accent,TEXT(" m"));
                Add(SNew(STextBlock).AutoWrapText(true).ColorAndOpacity(Accent).Text_Lambda([H=Host](){return FText::FromString(H.IsValid()?FString::Printf(TEXT("%d queued  •  %s"),H->QueuedBuffRequests(),*H->BuffRequestStatus):FString());}));
                Add(Button(TEXT("Clear request queue"),[this](){Host->ClearBuffRequests();}));
                Add(SNew(STextBlock).AutoWrapText(true).ColorAndOpacity(Ink).Text_Lambda([H=Host](){return FText::FromString(H.IsValid()?H->BuffQueueSummary():FString());}));
                Heading(TEXT("Tell keywords"),TEXT("Edit a keyword and press Enter to save. Add another entry to give a role an additional keyword."));
                const auto Commands=Host->BuffCommands();
                for(int32 I=0;I<Commands.Num();++I)
                {
                    const auto Command=Commands[I]->AsObject();if(!Command)continue;
                    auto Row=SNew(SHorizontalBox);
                    Row->AddSlot().FillWidth(1).VAlign(VAlign_Center)[Text(Str(Command,TEXT("role")),17)];
                    Row->AddSlot().FillWidth(2).Padding(8,0)[SNew(SEditableTextBox).Text(FText::FromString(Str(Command,TEXT("keyword"))))
                        .OnTextCommitted_Lambda([this,I](const FText& Text,ETextCommit::Type)
                        {auto V=Host->BuffCommands();const FString Key=Text.ToString().TrimStartAndEnd().ToLower();if(!V.IsValidIndex(I)||Key.IsEmpty()||Key.Len()>64)return;
                         for(int32 J=0;J<V.Num();++J)if(J!=I&&Str(V[J]->AsObject(),TEXT("keyword")).Equals(Key,ESearchCase::IgnoreCase)){Host->Notice=TEXT("That keyword already exists.");return;}
                         V[I]->AsObject()->SetStringField(TEXT("keyword"),Key);P()->SetArrayField(TEXT("buff_commands"),V);Save();})];
                    Row->AddSlot().AutoWidth()[Button(TEXT("Remove"),[this,I](){auto V=Host->BuffCommands();V.RemoveAt(I);P()->SetArrayField(TEXT("buff_commands"),V);Save();Rebuild();})];Add(Tile(Row));
                }
                auto Roles=SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(5,5));
                for(const FString Role:{TEXT("mage"),TEXT("heavy"),TEXT("missile"),TEXT("light"),TEXT("finesse"),TEXT("twohanded"),TEXT("unarmed"),TEXT("melee")})
                    Roles->AddSlot()[Button(TEXT("+ ")+Role,[this,Role](){auto V=Host->BuffCommands();auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("role"),Role);O->SetStringField(TEXT("keyword"),Role+FString::FromInt(V.Num()+1));V.Add(MakeShared<FJsonValueObject>(O));P()->SetArrayField(TEXT("buff_commands"),V);Save();Rebuild();})];
                Add(Roles);
                Add(Text(TEXT("Melee roles include Dual Wield, Dirty Fighting, Recklessness, Sneak Attack and Shield. Unarmed uses Light Weapons in modern retail. Players receive tells with their queue position, turn and completion status."),15,Muted));
                Add(Text(TEXT("Every role includes all six attributes, melee/missile/magic defense, life protections and regeneration, run, jump, healing, lore and mana conversion. Mage adds magic schools; weapon roles add their attack skills. Uses the highest known Other spells within your skill margin, including item buffs on the requester's server-visible equipped gear."),15,Muted));
                Add(Text(TEXT("Keep the intended weapon equipped. Servers may not expose other players' armor as targetable items; UCM reports unavailable buffs instead of claiming those banes succeeded. Self-only spells cannot be cast on someone else."),15,Muted));

            }
            else if(Page==TEXT("Combat"))
            {
                Heading(TEXT("Targeting & equipment"),TEXT("Auto ranks usable equipment and known attack spells against assessed resistances. Unknown resistances use neutral values; choose an element override when needed."));
                Add(CombatToggle());
                Heading(TEXT("Melee hands"),TEXT("Choose a default main hand and offhand. Monster rules can override these choices. Left-hand Tether reserves a weapon for the offhand; two-handed weapons cannot use a second weapon."));
                MonsterEquipment(P(),false,true);MonsterEquipment(P(),true,true);
                Add(Text(TEXT("Automatically chooses your highest usable trained combat skill, then the best available weapon or spell. Monster-rule overrides are retained."),15,Muted));
                Add(Toggle(TEXT("idle_peace"),TEXT("Peace mode when idle"),TEXT("Enter peace mode when no eligible monsters are nearby, including while following a route. Return to the configured combat mode when attacking. Buffing, recovery and looting finish first.")));
                const int32 Selection=FMath::Clamp(int32(Num(P(),TEXT("target_select"),1)),1,3);
                const TCHAR* SelectionNames[]={TEXT(""),TEXT("Distance"),TEXT("Angle"),TEXT("Angle nearby, distance farther away")};
                Add(Button(FString(TEXT("Target preference: "))+SelectionNames[Selection],[this,Selection](){P()->SetNumberField(TEXT("target_select"),Selection%3+1);Save();Rebuild();}));
                Slider(TEXT("target_angle_range"),TEXT("Nearby angle preference range"),1,30,5,Accent,TEXT(" m"));
                Slider(TEXT("minimum_range"),TEXT("Minimum target distance"),0,50,0,Accent,TEXT(" m"));
                Slider(TEXT("radius"),TEXT("Missile / spell attack range"),5,100,30,Accent,TEXT(" m"));Slider(TEXT("approach_range"),TEXT("Target acquisition range"),5,150,60,Accent,TEXT(" m"));
                Slider(TEXT("melee_range"),TEXT("Melee approach distance"),1,5,2,Accent,TEXT(" m"));
                Slider(TEXT("monster_attempts"),TEXT("Environment misses before skipping target"),0,20,4);
                Slider(TEXT("monster_blacklist_seconds"),TEXT("Skip unreachable target for"),0,600,120,Accent,TEXT(" s"));
                Add(Text(TEXT("Acquisition is the maximum search distance. Attack range controls when to stop approaching and fire. Walls and closed doors exclude targets; Follow route continues around them. Override attack range for a monster in Rules."),15,Muted));
                Slider(TEXT("power_percent"),TEXT("Attack power / accuracy"),0,100,50);
                Add(Toggle(TEXT("auto_attack_power"),TEXT("Automatic attack power"),TEXT("Chooses power for the weapon's attack pattern and damage element.")));
                Add(Toggle(TEXT("use_recklessness"),TEXT("Use Recklessness"),TEXT("Keeps automatic power in the Recklessness range when the skill is usable.")));
                Add(Toggle(TEXT("ring"),TEXT("Use ring spells"),TEXT("For nearby groups. Imported monster rules keep their individual ring choices.")));
                Slider(TEXT("ring_range"),TEXT("Ring target distance"),1,15,5,Accent,TEXT(" m"));
                Slider(TEXT("ring_min_targets"),TEXT("Minimum ring targets"),1,20,4);
                const int32 Arcs=FMath::Clamp(int32(Num(P(),TEXT("use_arcs"),1)),1,3);
                const TCHAR* ArcNames[]={TEXT(""),TEXT("Prefer bolts"),TEXT("Prefer arcs at range"),TEXT("Prefer arcs")};
                Add(Button(ArcNames[Arcs],[this,Arcs](){P()->SetNumberField(TEXT("use_arcs"),Arcs%3+1);Save();Rebuild();}));
                Slider(TEXT("arc_range"),TEXT("Arc preference distance"),0,100,5,Accent,TEXT(" m"));
                Add(Toggle(TEXT("fast_cast_buffs"),TEXT("Fast buff movement"),TEXT("Use VT-style backward movement after eligible spell words to shorten buff recoil. Stops on the spell result or manual input."),true));
                Heading(TEXT("Summoned pets"),TEXT("Add essences to the equipment list below. Uses server requirements, remaining charges and cooldowns."));
                Add(Toggle(TEXT("summon_pets"),TEXT("Summon combat pets"),TEXT("Choose an eligible essence for nearby monsters; keep an existing pet active.")));
                Add(Toggle(TEXT("refill_summons"),TEXT("Refill summon essences"),TEXT("Use Encapsulated Spirit from your inventory to refill low-charge essences in the equipment pool. Works independently of automatic summoning.")));
                Slider(TEXT("summon_refill_charges"),TEXT("Refill at or below remaining charges"),0,50,5,Accent,TEXT(" charges"));
                Add(Button(Num(P(),TEXT("pet_range_mode"))==1?TEXT("Pet range: custom"):TEXT("Pet range: attack distance"),[this](){P()->SetNumberField(TEXT("pet_range_mode"),Num(P(),TEXT("pet_range_mode"))==1?0:1);Save();Rebuild();}));
                Slider(TEXT("pet_range"),TEXT("Custom pet distance"),1,240,5,Accent,TEXT(" m"));
                Slider(TEXT("pet_min_targets"),TEXT("Minimum nearby monsters"),1,20,1);
                Add(Toggle(TEXT("debuff_yield"),TEXT("Magic Yield"),TEXT("Lower magic defense before other debuffs and attacks.")));
                Add(Toggle(TEXT("switch_debuff_wand"),TEXT("Use attack wand for debuffs"),TEXT("Use the chosen attack wand for magic loadouts. When off, keep the current usable wand.")));
                Add(Toggle(TEXT("debuff_imperil"),TEXT("Imperil"),TEXT("Lower the target's armor before attacking.")));
                Add(Toggle(TEXT("debuff_vuln"),TEXT("Elemental vulnerability"),TEXT("Match the selected attack element. Imported monster rules keep their choices.")));
                const int32 DebuffMode=FMath::Clamp(int32(Num(P(),TEXT("debuff_each_first"),1)),1,3);
                const TCHAR* DebuffNames[]={TEXT(""),TEXT("Debuff one target"),TEXT("Debuff same-priority targets first"),TEXT("Debuff all targets first")};
                Add(Button(DebuffNames[DebuffMode],[this,DebuffMode](){P()->SetNumberField(TEXT("debuff_each_first"),DebuffMode%3+1);Save();Rebuild();}));
                Add(Toggle(TEXT("debuff_fallback"),TEXT("Allow debuff fallback"),TEXT("Continue attacking if a required debuff is unavailable or fails three times.")));
                Add(Text(TEXT("Attack height: automatic from the target body and elevation."),15,Muted));
                Add(Toggle(TEXT("approach"),TEXT("Approach targets"),TEXT("Move through the normal collision and networking path."),true));
                auto Elements=SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(4,4));
                const TCHAR* Names[]={TEXT("Auto"),TEXT("Slash"),TEXT("Pierce"),TEXT("Blunt"),TEXT("Fire"),TEXT("Cold"),TEXT("Acid"),TEXT("Electric"),TEXT("Nether")};
                for(int I=0;I<9;++I)Elements->AddSlot()[Button(FString(Names[I])+(Num(P(),TEXT("damage_type"))==(I?1<<(I-1):0)?TEXT(" •"):TEXT("")),[this,I](){P()->SetNumberField(TEXT("damage_type"),I?1<<(I-1):0);Save();Rebuild();})];Add(Elements);
                Inventory(false);
            }
            else if(Page==TEXT("Recovery"))
            {
                Add(Toggle(TEXT("mana_charges_when_off"),TEXT("Recharge equipment while UCM is stopped"),TEXT("Uses configured mana supplies only; does not start combat, buffs or navigation.")));
                Heading(TEXT("Spell components"),TEXT("Imported pea recipes travel with the saved setup. Keep a Splitting Tool and the requested peas in inventory."));
                Add(Toggle(TEXT("split_peas"),TEXT("Split component peas"),TEXT("Uses imported All Peas or named component choices and waits for inventory confirmation."),true));
                Slider(TEXT("component_critical"),TEXT("Critical component minimum"),0,100,4);
                Slider(TEXT("component_normal"),TEXT("Normal component minimum"),0,100,20);
                Slider(TEXT("component_idle"),TEXT("Idle component minimum"),0,100,20);
                Heading(TEXT("Maintain your vitals"),TEXT("Recover when a vital falls below its percentage. Prefer the highest eligible Life Magic spell, with supplies as fallback; unknown recovery supplies are inspected automatically. Imported setups retain their ordered recovery methods."));
                Add(Toggle(TEXT("recovery"),TEXT("Automatic recovery"),TEXT("Active while UCM is running."),true));
                Add(Text(FString::Printf(TEXT("Recovery spells use your current magic skill minus the %.0f-point skill buffer. Components are required unless the server grants an exemption."),Num(P(),TEXT("skill_margin"),30)),14,Muted));
                if(P()->HasField(TEXT("recharge_handlers")))Add(Toggle(TEXT("use_imported_recovery_order"),TEXT("Use imported recovery order"),TEXT("On: preserve this profile's ordered recovery handlers. Turn off to use the spell/consumable preference below."),true));
                Add(SNew(SCheckBox)
                    .IsChecked_Lambda([this](){return Bool(P(),TEXT("recovery_supplies_first"))?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
                    .OnCheckStateChanged_Lambda([this](ECheckBoxState State)
                    {
                        P()->SetBoolField(TEXT("recovery_supplies_first"),State==ECheckBoxState::Checked);
                        // An explicit user choice must take effect even on an imported setup.
                        P()->SetBoolField(TEXT("use_imported_recovery_order"),false);Save();
                    })
                    [SNew(SBox).MinDesiredHeight(32).VAlign(VAlign_Center).Padding(8,0)[Text(TEXT("Prefer kits and consumables"))]]);
                Add(SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular",14)).ColorAndOpacity(Muted).AutoWrapText(true)
                    .Text_Lambda([this]()
                    {
                        if(P()->HasField(TEXT("recharge_handlers"))&&Bool(P(),TEXT("use_imported_recovery_order"),true))
                            return FText::FromString(TEXT("Using imported recovery order. Change the preference above to override it."));
                        return FText::FromString(Bool(P(),TEXT("recovery_supplies_first"))
                            ?TEXT("Kits/consumables first, then spells if unavailable or unsuccessful.")
                            :TEXT("Spells first, then kits/consumables if unavailable or unsuccessful."));
                    }));
                Slider(TEXT("health_threshold"),TEXT("Health"),1,95,65,FLinearColor(.95f,.25f,.3f));
                Slider(TEXT("stamina_threshold"),TEXT("Stamina"),1,95,50,FLinearColor(.95f,.7f,.15f));
                Slider(TEXT("mana_threshold"),TEXT("Mana"),1,95,45,FLinearColor(.2f,.55f,1));
                Slider(TEXT("kit_min_success"),TEXT("Minimum kit success chance"),0,100,0,Accent);
                Add(Toggle(TEXT("kits_in_magic"),TEXT("Use kits in magic stance"),TEXT("Allow healing kits while wielding a casting device."),true));
                Add(Toggle(TEXT("kit_peace"),TEXT("Enter peace before using kits"),TEXT("Switch out of combat before using the selected kit.")));
                Heading(TEXT("Remove vulnerabilities"),TEXT("Remove temporary elemental vulnerabilities within the dispel's power limit. Waits for the server's enchantment update."));
                Add(Toggle(TEXT("dispel_self"),TEXT("Cast Life Magic dispel"),TEXT("Requires a learned self dispel, sufficient magic skill and Chorizite.")));
                Add(Toggle(TEXT("dispel_items"),TEXT("Use dispel supplies"),TEXT("Uses eligible runes, gems, chocolate or potions from inventory.")));
                Heading(TEXT("Fellowship recovery"),TEXT("Restore nearby members using fresh server vitals. Set a threshold to zero to disable that vital."));
                Slider(TEXT("helper_health_threshold"),TEXT("Fellow health"),0,100,0,FLinearColor(.95f,.25f,.3f));
                Slider(TEXT("helper_stamina_threshold"),TEXT("Fellow stamina"),0,100,0,FLinearColor(.95f,.7f,.15f));
                Slider(TEXT("helper_mana_threshold"),TEXT("Fellow mana"),0,100,0,FLinearColor(.2f,.55f,1));
                Add(Toggle(TEXT("item_mana"),TEXT("Recharge equipment"),TEXT("Use filled mana stones / charges on the player when equipped item mana is low."),true));
                Slider(TEXT("item_mana_threshold"),TEXT("Equipment mana threshold"),0,99,20,FLinearColor(.2f,.55f,1),TEXT("%"));
                Add(Toggle(TEXT("fill_mana_stones"),TEXT("Automatically fill empty mana stones"),TEXT("Consumes only the individual mana source items selected below. Filled stones can then recharge all equipped gear through the player.")));
                ManaSources();
                Inventory(true);
            }
            else if(Page==TEXT("Route"))
            {
                LibrarySelector(TEXT("nav"),TEXT("Navigation route"));
                Heading(TEXT("Route editor"),TEXT("Record close points along a walkable path. Blue lines show the route in the world; amber points mark jumps. Record the next point after arriving through a portal."));
                Add(Toggle(TEXT("navigation"),TEXT("Follow route"),TEXT("Start joins the nearest waypoint, then follows the route in order. Route edits stop the current run.")));
                Add(Toggle(TEXT("route_preview"),TEXT("Show route in world"),TEXT("Updates as points are added or edited."),true));Add(Toggle(TEXT("loop_route"),TEXT("Loop route"),TEXT("Repeat from the first point when the route finishes.")));Add(Toggle(TEXT("reverse_route"),TEXT("Reverse at route ends"),TEXT("VT Linear mode: walk back along the same points. Takes priority over Loop.")));
                Add(Toggle(TEXT("open_doors"),TEXT("Open doors along route"),TEXT("Use a closed door only when it lies on the current walking segment."),true));
                Add(Toggle(TEXT("follow_corners"),TEXT("Follow around corners"),TEXT("Imported follow routes retain the target player's observed path.")));
                Add(Toggle(TEXT("nav_priority"),TEXT("Prefer navigation over combat"),TEXT("Recovery, buffing and nearby looting still run before navigation.")));
                auto Buttons=SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(5,5));
                Buttons->AddSlot()[Button(TEXT("+ Walk here"),[this](){Host->RecordRouteAction(TEXT("walk"));Rebuild();})];
                Buttons->AddSlot()[Button(TEXT("+ Use selected"),[this](){Host->RecordRouteAction(TEXT("use"));Rebuild();})];
                Buttons->AddSlot()[Button(TEXT("+ Portal selected"),[this](){Host->RecordRouteAction(TEXT("portal"));Rebuild();})];
                Buttons->AddSlot()[Button(TEXT("+ Jump here"),[this](){Host->RecordRouteAction(TEXT("jump"),JumpCharge);Rebuild();})];Add(Buttons);
                Add(Text(TEXT("Jump charge (0 = minimum, 1 = full). Face the jump direction before recording."),14,Muted));
                Add(SNew(SSpinBox<float>).MinValue(0).MaxValue(1).Delta(.1).Value(JumpCharge).OnValueChanged_Lambda([this](float V){JumpCharge=V;}));
                Add(Text(TEXT("Pause duration (seconds)"),14,Muted));Add(SNew(SSpinBox<double>).MinValue(1).MaxValue(3600).Value(PauseSeconds).OnValueChanged_Lambda([this](double V){PauseSeconds=V;}));
                Add(Button(TEXT("+ Pause here"),[this](){RecordExtra(TEXT("pause"),TEXT("seconds"),PauseSeconds);}));
                auto* Client=Host->GetGameInstance()->GetSubsystem<UACEClientSubsystem>();auto* Dat=Host->GetGameInstance()->GetSubsystem<UACEDatSubsystem>();
                auto Recalls=SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(4,4));
                for(int32 Id:Client->GetKnownSpells())
                {
                    uint32 School,Power,Category,Flags,IconId;double Duration;FString Name;
                    if(Dat->TryGetPluginSpellInfo(Id,School,Power,Category,Flags,Duration)&&Category==201)
                    {Dat->TryGetSpellInfo(Id,Name,IconId);Recalls->AddSlot()[Button(TEXT("+ ")+Name,[this,Id](){RecordExtra(TEXT("recall"),TEXT("spell"),Id);})];}
                }
                Add(Recalls);
                const TArray<TSharedPtr<FJsonValue>>* Route=nullptr;
                if(P()->TryGetArrayField(TEXT("route"),Route))for(int I=0;I<Route->Num();++I)
                {
                    auto O=(*Route)[I]->AsObject();const FString Info=FString::Printf(TEXT("%d  %s  •  %.1f, %.1f, %.1f"),I+1,*Str(O,TEXT("kind"),TEXT("walk")),Num(O,TEXT("x")),Num(O,TEXT("y")),Num(O,TEXT("z")));
                    auto Row=SNew(SHorizontalBox);Row->AddSlot().FillWidth(1).VAlign(VAlign_Center)[Text(Info)];
                    Row->AddSlot().AutoWidth()[Button(TEXT("↑"),[this,I](){EditRoute(I,-1);})];Row->AddSlot().AutoWidth().Padding(4,0)[Button(TEXT("↓"),[this,I](){EditRoute(I,1);})];
                    Row->AddSlot().AutoWidth()[Button(TEXT("Remove"),[this,I](){EditRoute(I,0);})];Add(Tile(Row));
                }
            }
            else if(Page==TEXT("Loot"))
            {
                LootPanel();
            }
            else if(Page==TEXT("Rules"))
            {
                Heading(TEXT("Monster rules"),TEXT("First matching name prefix wins. Higher priority targets are chosen first. Set an element when a server does not expose resistances."));
                Add(SAssignNew(MonsterName,SEditableTextBox).Font(FCoreStyle::GetDefaultFontStyle("Regular",16)).HintText(FText::FromString(TEXT("Monster name begins with…"))));
                Add(Text(TEXT("Priority")));Add(SNew(SSpinBox<double>).MinValue(-100).MaxValue(100).Value(MonsterPriority).OnValueChanged_Lambda([this](double V){MonsterPriority=V;}));
                Add(Text(TEXT("Attack range in meters (0 = Combat settings)")));Add(SNew(SSpinBox<double>).MinValue(0).MaxValue(100).Value(MonsterRange).OnValueChanged_Lambda([this](double V){MonsterRange=V;}));
                auto Modes=SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(4,4));
                for(const FString Mode:{TEXT("auto"),TEXT("melee"),TEXT("missile"),TEXT("magic"),TEXT("ignore")})Modes->AddSlot()[SNew(SButton).ButtonStyle(&Style()).IsFocusable(false).OnClicked_Lambda([this,Mode](){MonsterMode=Mode;return FReply::Handled();})[SNew(STextBlock).Text(FText::FromString(Mode)).ColorAndOpacity_Lambda([this,Mode](){return MonsterMode==Mode?Accent:Muted;})]];
                Add(Modes);auto Elements=SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(4,4));
                const TCHAR* Names[]={TEXT("Auto"),TEXT("Slash"),TEXT("Pierce"),TEXT("Blunt"),TEXT("Fire"),TEXT("Cold"),TEXT("Acid"),TEXT("Electric"),TEXT("Nether")};
                for(int Index=0;Index<9;++Index){const int32 Element=Index?1<<(Index-1):0;Elements->AddSlot()[SNew(SButton).ButtonStyle(&Style()).IsFocusable(false).OnClicked_Lambda([this,Element](){MonsterElement=Element;return FReply::Handled();})[SNew(STextBlock).Text(FText::FromString(Names[Index])).ColorAndOpacity_Lambda([this,Element](){return MonsterElement==Element?Accent:Muted;})]];}
                Add(Elements);Add(Button(TEXT("Add monster rule"),[this](){
                    if(MonsterName->GetText().IsEmpty()){Host->Notice=TEXT("Enter a monster name prefix");return;}
                    auto Rule=MakeShared<FJsonObject>();Rule->SetStringField(TEXT("name"),MonsterName->GetText().ToString());Rule->SetStringField(TEXT("combat"),MonsterMode);Rule->SetBoolField(TEXT("ignore"),MonsterMode==TEXT("ignore"));Rule->SetNumberField(TEXT("priority"),MonsterPriority);Rule->SetNumberField(TEXT("damage_type"),MonsterElement);
                    Rule->SetNumberField(TEXT("range"),MonsterRange);
                    const TArray<TSharedPtr<FJsonValue>>* Old=nullptr;TArray<TSharedPtr<FJsonValue>> Copy;if(P()->TryGetArrayField(TEXT("monsters"),Old))Copy=*Old;Copy.Add(MakeShared<FJsonValueObject>(Rule));P()->SetArrayField(TEXT("monsters"),Copy);Save();Rebuild();}));
                const TArray<TSharedPtr<FJsonValue>>* Rules=nullptr;if(P()->TryGetArrayField(TEXT("monsters"),Rules))for(int Index=0;Index<Rules->Num();++Index)
                {auto Rule=(*Rules)[Index]->AsObject();Add(Tile(SNew(SHorizontalBox)+SHorizontalBox::Slot().FillWidth(1)[Text(FString::Printf(TEXT("%s • %s • priority %.0f • range %s"),*Str(Rule,TEXT("name")),*Str(Rule,TEXT("combat")),Num(Rule,TEXT("priority")),Num(Rule,TEXT("range"))>0?*FString::Printf(TEXT("%g m"),Num(Rule,TEXT("range"))):TEXT("default")))]
                    +SHorizontalBox::Slot().AutoWidth()[Button(ExpandedMonster==Index?TEXT("Collapse"):TEXT("Edit"),[this,Index](){ExpandedMonster=ExpandedMonster==Index?INDEX_NONE:Index;Rebuild();})]
                    +SHorizontalBox::Slot().AutoWidth()[Button(TEXT("Remove"),[this,Index](){auto Copy=P()->GetArrayField(TEXT("monsters"));Copy.RemoveAt(Index);P()->SetArrayField(TEXT("monsters"),Copy);ExpandedMonster=INDEX_NONE;Save();Rebuild();})]));if(ExpandedMonster==Index)MonsterDetails(Rule);}
            }
            else if(Page==TEXT("Metas"))
            {
                LibrarySelector(TEXT("met"),TEXT("Meta program"));
                const TArray<TSharedPtr<FJsonValue>>* ImportedRules=nullptr;
                if(P()->TryGetArrayField(TEXT("vt_meta"),ImportedRules))
                {
                    Heading(TEXT("Imported Virindi meta"),TEXT("Ordered rules, calls, timers, variables and embedded routes. Run state and options reset when UCM stops. The import report lists any unsupported dependencies."));
                    Add(Toggle(TEXT("meta_enabled"),TEXT("Run imported meta"),TEXT("Meta rules can change activity settings and replace the active navigation route."),true));
                    Add(SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Bold",18)).Text_Lambda([this](){return FText::FromString(TEXT("Current state: ")+(Plugin->Running?Plugin->MetaState:TEXT("Stopped")));}));
                    TMap<FString,int32> Counts;for(const auto& R:*ImportedRules)++Counts.FindOrAdd(Str(R->AsObject(),TEXT("state")));
                    TArray<FString> Names;Counts.GenerateKeyArray(Names);Names.Sort();
                    for(const auto& Name:Names)Add(Text(FString::Printf(TEXT("%s — %d rules"),*Name,Counts[Name]),16));
                    Add(Button(TEXT("Remove loaded meta"),[this](){Host->ClearProfileFile(TEXT("met"));Rebuild();}));
                    return;
                }
                Heading(TEXT("Activity states"),TEXT("A state can override activities and switch to another state when a condition is met. Conditions run in order; the first match wins. Changes stop the current run."));
                const TArray<TSharedPtr<FJsonValue>>* StateList=nullptr;
                if(!P()->TryGetArrayField(TEXT("states"),StateList)||StateList->IsEmpty())
                {auto Default=MakeShared<FJsonObject>();Default->SetStringField(TEXT("name"),TEXT("Default"));P()->SetArrayField(TEXT("states"),{MakeShared<FJsonValueObject>(Default)});P()->SetStringField(TEXT("initial_state"),TEXT("Default"));P()->TryGetArrayField(TEXT("states"),StateList);}
                StateIndex=FMath::Clamp(StateIndex,0,StateList->Num()-1);
                auto StateTabs=SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(4,4));
                for(int Index=0;Index<StateList->Num();++Index){auto State=(*StateList)[Index]->AsObject();StateTabs->AddSlot()[Button(Str(State,TEXT("name"))+(StateIndex==Index?TEXT(" •"):TEXT("")),[this,Index](){StateIndex=Index;Rebuild();})];}Add(StateTabs);
                Add(SAssignNew(NewStateName,SEditableTextBox).HintText(FText::FromString(TEXT("New state name"))).Font(FCoreStyle::GetDefaultFontStyle("Regular",16)));
                Add(Button(TEXT("Create state"),[this](){const FString Name=NewStateName->GetText().ToString().TrimStartAndEnd();if(Name.IsEmpty())return;
                    auto States=P()->GetArrayField(TEXT("states"));for(auto V:States)if(Str(V->AsObject(),TEXT("name"))==Name){Host->Notice=TEXT("State name already exists");return;}
                    auto State=MakeShared<FJsonObject>();State->SetStringField(TEXT("name"),Name);States.Add(MakeShared<FJsonValueObject>(State));P()->SetArrayField(TEXT("states"),States);StateIndex=States.Num()-1;Save(true);Rebuild();}));
                const auto State=(*StateList)[StateIndex]->AsObject();const FString Name=Str(State,TEXT("name"));
                Heading(Name,Str(P(),TEXT("initial_state"))==Name?TEXT("This is the starting state."):TEXT("This state starts when a transition selects it."));
                Add(Button(TEXT("Use as starting state"),[this,Name](){P()->SetStringField(TEXT("initial_state"),Name);Save(true);Rebuild();}));
                for(const TCHAR* Key:{TEXT("buffing"),TEXT("recovery"),TEXT("navigation"),TEXT("looting")})
                {
                    const FString Label=FString(Key)==TEXT("buffing")?TEXT("Buffs"):FString(Key)==TEXT("recovery")?TEXT("Recovery"):FString(Key)==TEXT("navigation")?TEXT("Navigation"):TEXT("Loot");
                    Add(Text(Label,16));auto Row=SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(4,4));
                    for(const FString Value:{TEXT("inherit"),TEXT("on"),TEXT("off")})Row->AddSlot()[Button(Value+(State->HasField(Key)?(Bool(State,Key)==(Value==TEXT("on"))&&Value!=TEXT("inherit")?TEXT(" •"):TEXT("")):(Value==TEXT("inherit")?TEXT(" •"):TEXT(""))),[this,Key,Value](){auto Current=StateObject();if(Value==TEXT("inherit"))Current->RemoveField(Key);else Current->SetBoolField(Key,Value==TEXT("on"));Save(true);Rebuild();})];Add(Row);
                }
                Add(Text(TEXT("Combat mode"),16));auto Modes=SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(4,4));
                for(const FString Mode:{TEXT("inherit"),TEXT("off"),TEXT("auto"),TEXT("melee"),TEXT("missile"),TEXT("magic")})Modes->AddSlot()[Button(Mode+(Str(State,TEXT("combat"),TEXT("inherit"))==Mode?TEXT(" •"):TEXT("")),[this,Mode](){if(Mode==TEXT("inherit"))StateObject()->RemoveField(TEXT("combat"));else StateObject()->SetStringField(TEXT("combat"),Mode);Save(true);Rebuild();})];Add(Modes);
                Heading(TEXT("Transitions"),TEXT("Health, stamina and mana thresholds are percentages. Elapsed time is seconds since entering this state."));
                auto Conditions=SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(4,4));
                for(const FString When:{TEXT("health_below"),TEXT("stamina_below"),TEXT("mana_below"),TEXT("elapsed"),TEXT("no_targets"),TEXT("target_available"),TEXT("route_complete")})
                {FString Label=When.Replace(TEXT("_"),TEXT(" "));Conditions->AddSlot()[Button(Label+(TransitionWhen==When?TEXT(" •"):TEXT("")),[this,When](){TransitionWhen=When;Rebuild();})];}Add(Conditions);
                Add(SNew(SSpinBox<double>).MinValue(0).MaxValue(86400).Value(TransitionValue).OnValueChanged_Lambda([this](double V){TransitionValue=V;}));
                Add(Text(TEXT("Then switch to")));auto Destinations=SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(4,4));
                for(auto V:*StateList){const FString To=Str(V->AsObject(),TEXT("name"));Destinations->AddSlot()[Button(To+(To==TransitionTarget?TEXT(" •"):TEXT("")),[this,To](){TransitionTarget=To;Rebuild();})];}Add(Destinations);
                Add(Button(TEXT("Add transition"),[this](){if(TransitionTarget.IsEmpty()){Host->Notice=TEXT("Choose a destination state");return;}auto Rule=MakeShared<FJsonObject>();Rule->SetStringField(TEXT("when"),TransitionWhen);Rule->SetNumberField(TEXT("value"),TransitionValue);Rule->SetStringField(TEXT("next"),TransitionTarget);
                    const TArray<TSharedPtr<FJsonValue>>* Old=nullptr;TArray<TSharedPtr<FJsonValue>> Rules;if(StateObject()->TryGetArrayField(TEXT("transitions"),Old))Rules=*Old;Rules.Add(MakeShared<FJsonValueObject>(Rule));StateObject()->SetArrayField(TEXT("transitions"),Rules);Save(true);Rebuild();}));
                const TArray<TSharedPtr<FJsonValue>>* Rules=nullptr;if(State->TryGetArrayField(TEXT("transitions"),Rules))for(int Index=0;Index<Rules->Num();++Index)
                {auto Rule=(*Rules)[Index]->AsObject();Add(Tile(SNew(SHorizontalBox)+SHorizontalBox::Slot().FillWidth(1)[Text(FString::Printf(TEXT("%s %.0f → %s"),*Str(Rule,TEXT("when")).Replace(TEXT("_"),TEXT(" ")),Num(Rule,TEXT("value")),*Str(Rule,TEXT("next"))))]
                    +SHorizontalBox::Slot().AutoWidth()[Button(TEXT("Remove"),[this,Index](){auto Copy=StateObject()->GetArrayField(TEXT("transitions"));Copy.RemoveAt(Index);StateObject()->SetArrayField(TEXT("transitions"),Copy);Save(true);Rebuild();})]));}
            }
            else if(Page==TEXT("Import")||Page==TEXT("Files")){LibraryPanel();Add(Button(TEXT("Advanced import / compatibility report"),[this](){Page=TEXT("Import tools");Rebuild();}));}
            else if(Page==TEXT("Import tools"))ImportPanel();
            else if(Page==TEXT("Legacy loot"))LegacyLootPanel();
            else
            {
                LibraryPanel();
                Heading(TEXT("Saved UCM setups"),TEXT("Save named setups for different characters or hunts. Advanced JSON exposes exclusions, monster ignores and state transitions."));
                Add(SAssignNew(ProfileName,SEditableTextBox).Font(FCoreStyle::GetDefaultFontStyle("Regular",18)).Text(FText::FromString(Plugin->ProfileName)));
                Add(Button(TEXT("Save profile as"),[this](){Host->SaveProfile(Plugin->Id,ProfileName->GetText().ToString(),Host->ProfileJson(Plugin->Id));Rebuild();}));
                TArray<FString> Files;IFileManager::Get().FindFiles(Files,*(Host->UserDirectory()/TEXT("Profiles/ucm/*.json")),true,false);Files.Sort();
                for(auto File:Files){const FString Name=FPaths::GetBaseFilename(File);Add(Button(TEXT("Load ")+Name,[this,Name](){Host->LoadProfile(Plugin->Id,Name);LootDraft.Reset();Rebuild();}));}
                Add(Button(TEXT("Advanced import / compatibility report"),[this](){Page=TEXT("Import tools");Rebuild();}));
                Add(Button(ShowAdvancedProfile?TEXT("Hide advanced JSON"):TEXT("Edit advanced JSON"),[this](){ShowAdvancedProfile=!ShowAdvancedProfile;Rebuild();}));
                if(!ShowAdvancedProfile)return;
                auto Editor=SNew(SMultiLineEditableTextBox).Font(FCoreStyle::GetDefaultFontStyle("Regular",14)).Text(FText::FromString(Host->ProfileJson(Plugin->Id)));
                Add(SNew(SBox).HeightOverride(300)[Editor]);Add(Button(TEXT("Apply advanced profile"),[this,Editor](){Host->SaveProfile(Plugin->Id,Plugin->ProfileName,Editor->GetText().ToString());Rebuild();}));
            }
        }
        TSharedRef<SWidget> IconWidget(uint32 Id){return Icon(Id);}
        TSharedPtr<FJsonObject> StateObject()const{return P()->GetArrayField(TEXT("states"))[StateIndex]->AsObject();}
        void RecordExtra(const FString& Kind,const TCHAR* Key,double Value)
        {const TArray<TSharedPtr<FJsonValue>>* Before=nullptr;const int32 Count=P()->TryGetArrayField(TEXT("route"),Before)?Before->Num():0;
         Host->RecordRouteAction(Kind);const TArray<TSharedPtr<FJsonValue>>* After=nullptr;
         if(P()->TryGetArrayField(TEXT("route"),After)&&After->Num()==Count+1){After->Last()->AsObject()->SetNumberField(Key,Value);Save(true);}Rebuild();}
        #include "ACEUCMVendorPanel.inl"
        #include "ACEUCMLootPanel.inl"
        #include "ACEVTImportPanel.inl"
        #include "ACEVTLegacyLootPanel.inl"
        void EditRoute(int I,int Direction)
        {auto Route=P()->GetArrayField(TEXT("route"));if(!Direction)Route.RemoveAt(I);else if(Route.IsValidIndex(I+Direction))Route.Swap(I,I+Direction);P()->SetArrayField(TEXT("route"),Route);Save(true);Rebuild();}
    };
}
TSharedRef<SWidget> UACEPluginSubsystem::MakeUCMPanel(const FString& InitialPage){return SNew(ACEUCMPanelPrivate::SUCMPanel).Host(this).InitialPage(InitialPage);}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Misc/FileHelper.h"
#include "Slate/WidgetRenderer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ImageUtils.h"
#include "RenderingThread.h"
namespace ACEUCMPanelPrivate
{
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEUCMLootEditorTest,"ACE.Plugins.LootEditor",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FACEUCMLootEditorTest::RunTest(const FString&)
{
    auto* GI=NewObject<UGameInstance>();GI->Init();ON_SCOPE_EXIT{GI->Shutdown();};
    auto* Host=GI->GetSubsystem<UACEPluginSubsystem>();TSharedPtr<FACEClientPlugin> Plugin;
    for(auto P:Host->Plugins)if(P->Id==TEXT("ucm"))Plugin=P;
    if(!Plugin)return false;
    const auto Original=Plugin->Profile;ON_SCOPE_EXIT{Plugin->Profile=Original;};
    auto Parse=[](const TCHAR* Text){TSharedPtr<FJsonObject> O;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),O);return O;};
    Plugin->Profile=Parse(TEXT(R"({"loot_profile":"Imported sample","loot_rules":[{"label":"Legendary armor","action":"keep","keep_up_to":4,"count_by_name":true,"conditions":[{"field":"int","key":265,"op":"ge","value":1,"missing_zero":true},{"field":"float","key":167772169,"op":"ge","value":7.25},{"field":"spells","pattern":"Legendary|Epic","exclude":"Bane","value":2}]}]})"));
    auto Panel=SNew(SUCMPanel).Host(Host).InitialPage(TEXT("Standalone loot"));
    const auto Rule=Plugin->Profile->GetArrayField(TEXT("loot_rules"))[0]->AsObject();
    const auto Conditions=Rule->GetArrayField(TEXT("conditions"));
    TestEqual(TEXT("Opening editor selects first rule"),Panel->LootEditIndex,0);
    TestTrue(TEXT("Unchanged rule can be switched"),Panel->CanLeaveLootRule());
    Panel->SelectLootRequirement(0);
    TestTrue(TEXT("Opening a requirement alone does not dirty it"),Panel->CanLeaveLootRequirement());
    Panel->SelectLootRequirement(0);
    Panel->ConditionDraft->SetNumberField(TEXT("value"),42);
    TestFalse(TEXT("Unsaved requirement prevents changing rule"),Panel->CanLeaveLootRule());
    Panel->DiscardLootRule();
    TestEqual(TEXT("Discard restores requirement value"),Panel->LootDraft->GetArrayField(TEXT("conditions"))[0]->AsObject()->GetNumberField(TEXT("value")),1.);
    Panel->LootDraft->SetStringField(TEXT("label"),TEXT("Unsaved"));
    Panel->NewLootRule();
    TestEqual(TEXT("New cannot silently replace an edited rule"),Panel->LootEditIndex,0);
    Panel->DiscardLootRule();
    Panel->SelectLootRule(0,true);
    TestEqual(TEXT("Clone is a new draft"),Panel->LootEditIndex,INDEX_NONE);
    TestEqual(TEXT("Clone has distinct name"),Panel->LootDraft->GetStringField(TEXT("label")),FString(TEXT("Legendary armor copy")));
    Panel->LootDraft->GetArrayField(TEXT("conditions"))[0]->AsObject()->SetNumberField(TEXT("value"),77);
    TestEqual(TEXT("Cloned requirements do not modify original"),Conditions[0]->AsObject()->GetNumberField(TEXT("value")),1.);
    Panel->DiscardLootRule();
    Panel->NewLootRule();
    Panel->LootDraft->SetStringField(TEXT("action"),TEXT("invalid-test-action"));
    Panel->ApplyLootRule();
    TestEqual(TEXT("Rejected new rule remains a new draft"),Panel->LootEditIndex,INDEX_NONE);
    TestTrue(TEXT("Rejected save retains editable draft"),Panel->LootDraft.IsValid());
    TestEqual(TEXT("Rejected save leaves active rules unchanged"),Panel->CurrentLootRules().Num(),1);
    Panel->DiscardLootRule();
    Host->Notice.Reset();
    TestTrue(TEXT("Classic property names include armor set ID"),ACEUCMLootPresentation::ConditionSummary(Conditions[0]->AsObject()).Contains(TEXT("ArmorSetID (265) >= 1")));
    TestTrue(TEXT("Fractional workmanship appears in summary"),ACEUCMLootPresentation::ConditionSummary(Conditions[1]->AsObject()).Contains(TEXT("7.25")));
    const FString SpellSummary=ACEUCMLootPresentation::ConditionSummary(Conditions[2]->AsObject());
    TestTrue(TEXT("Spell summary includes count and both regexes"),SpellSummary.Contains(TEXT(">= 2"))&&SpellSummary.Contains(TEXT("Legendary|Epic"))&&SpellSummary.Contains(TEXT("Exclude regex: Bane")));
    TestEqual(TEXT("Public workmanship tagged correctly"),ACEUCMLootPresentation::DataRequirement(Conditions[1]->AsObject()),FString(TEXT("Public item data")));
    TestEqual(TEXT("Appraisal property tagged correctly"),ACEUCMLootPresentation::DataRequirement(Conditions[0]->AsObject()),FString(TEXT("Appraisal may be needed")));
    TestTrue(TEXT("Unknown property ID remains visible"),ACEUCMLootPresentation::PropertyLabel(TEXT("int"),999999).Contains(TEXT("999999")));
    for(int Key:{6,7,10,17,1000,1001,1002,1003,1004,2000,2001,2003,2005,2006,2007,2008})
        TestTrue(*FString::Printf(TEXT("Every executable calculated condition has editor fields: %d"),Key),ACEUCMLootPresentation::Calculated().Contains(Key));
    Panel->LootDraft=ACEUCMLootPresentation::Clone(Rule);
    Panel->LootDraft->GetArrayField(TEXT("conditions"))[0]->AsObject()->SetNumberField(TEXT("value"),10);
    TestEqual(TEXT("Cancelling nested draft does not alter active rule"),Conditions[0]->AsObject()->GetNumberField(TEXT("value")),1.);
    Panel->LootDraft.Reset();

    auto Document=Parse(TEXT(R"({"format":"utl","rules":[],"extras":[{"name":"CustomBlock","body":"unchanged\r\n"}]})"));
    TArray<TSharedPtr<FJsonValue>> Rules;
    for(const auto& Schema:Panel->LegacySchemas())
    {
        TArray<FString> Fields;Schema.Value.ParseIntoArray(Fields,TEXT("|"));
        auto Req=MakeShared<FJsonObject>();Req->SetNumberField(TEXT("type"),Schema.Key);FString Body;
        for(int I=1;I<Fields.Num();++I)Body+=(Schema.Key==9999?FString(TEXT("true")):Fields[I].Contains(TEXT("attern"))?FString(TEXT("^Legendary|Epic")):(Schema.Key==15&&I==6)?FString(TEXT("Amuli Coat (Chest)")):FString(TEXT("1")))+TEXT("\r\n");
        Req->SetStringField(TEXT("body"),Body);
        auto R=Parse(TEXT(R"({"label":"Classic requirement","action_id":10,"amount":0,"priority":2147483647,"expression":"custom expression","requirements":[]})"));
        R->SetArrayField(TEXT("requirements"),{MakeShared<FJsonValueObject>(Req)});Rules.Add(MakeShared<FJsonValueObject>(R));
        TestTrue(TEXT("Every Classic field is visible in its summary"),Panel->LegacyRequirementSummary(Req).Contains(Fields[1]));
    }
    TestEqual(TEXT("All original Classic requirement types covered"),Rules.Num(),31);
    Document->SetArrayField(TEXT("rules"),Rules);FString Error;
    const FString Encoded=ACEVTProfile::WriteLoot(Document,Error);TestTrue(TEXT("All Classic requirement layouts save"),!Encoded.IsEmpty()&&Error.IsEmpty());
    const auto Reload=ACEVTProfile::Read(Encoded,TEXT("utl"),Error);if(!TestTrue(TEXT("Original-format document reloads"),Reload.IsValid()))return false;
    TestEqual(TEXT("Expressions remain intact"),Reload->GetArrayField(TEXT("rules"))[0]->AsObject()->GetStringField(TEXT("expression")),FString(TEXT("custom expression")));
    TestEqual(TEXT("Extra blocks remain intact"),Reload->GetArrayField(TEXT("extras"))[0]->AsObject()->GetStringField(TEXT("body")),FString(TEXT("unchanged\r\n")));
    for(int I=0;I<Rules.Num();++I)TestEqual(TEXT("Every requirement body round trips without narrowing"),Reload->GetArrayField(TEXT("rules"))[I]->AsObject()->GetArrayField(TEXT("requirements"))[0]->AsObject()->GetStringField(TEXT("body")),Rules[I]->AsObject()->GetArrayField(TEXT("requirements"))[0]->AsObject()->GetStringField(TEXT("body")));
    auto Req=Parse(TEXT(R"({"type":9,"body":"Epic\r\nBane\r\n3\r\n"})"));
    Panel->SetLegacyValue(Req,1,TEXT("Bane|Ward"));TestEqual(TEXT("Editing one field retains other fields"),Req->GetStringField(TEXT("body")),FString(TEXT("Epic\r\nBane|Ward\r\n3\r\n")));
    int ImportedFiles=0;TSharedPtr<FJsonObject> ImportedProfile;
    for(const FString& Folder:{FPaths::ProjectSavedDir()/TEXT("ClientPlugins/ImportInbox"),FString(TEXT("C:/Games/VirindiPlugins/VirindiTank"))})
    {
        TArray<FString> Files;IFileManager::Get().FindFiles(Files,*(Folder/TEXT("*.utl")),true,false);
        for(const auto& File:Files)
        {
            FString Source;FFileHelper::LoadFileToString(Source,*(Folder/File));const auto OriginalFile=ACEVTProfile::Read(Source,TEXT("utl"),Error);
            if(!TestTrue(*(TEXT("Existing UTL parses: ")+File),OriginalFile.IsValid()))continue;
            const FString SavedCopy=ACEVTProfile::WriteLoot(ACEUCMLootPresentation::Clone(OriginalFile),Error);
            const auto Reopened=ACEVTProfile::Read(SavedCopy,TEXT("utl"),Error);
            TestTrue(*(TEXT("Existing UTL survives draft / save / reload: ")+File),Reopened.IsValid()&&ACEVTProfile::WriteLoot(Reopened,Error)==SavedCopy);
            TArray<FString> Issues;auto Converted=ACEVTProfile::Convert(OriginalFile,Issues);
            if(Converted&&Issues.IsEmpty())for(const auto& Entry:Converted->GetArrayField(TEXT("loot_rules")))
                for(const auto& Condition:Entry->AsObject()->GetArrayField(TEXT("conditions")))TestFalse(TEXT("Imported condition has readable presentation"),ACEUCMLootPresentation::ConditionSummary(Condition->AsObject()).IsEmpty());
            if(Converted&&Issues.IsEmpty()&&File==TEXT("Gardener_LootSnobV4.utl")){ImportedProfile=Converted;ImportedProfile->SetStringField(TEXT("loot_profile"),File);}
            ++ImportedFiles;
        }
    }
    AddInfo(FString::Printf(TEXT("Existing local UTL files checked without modifying originals: %d"),ImportedFiles));
    if(FApp::CanEverRender())
    {
        FWidgetRenderer Renderer(false,true);
        const auto SampleProfile=Plugin->Profile;
        for(int Width:{780,450})for(const FString View:{TEXT("Rules"),TEXT("Condition"),TEXT("Classic"),TEXT("Imported")})
        {
            Plugin->Profile=View==TEXT("Imported")&&ImportedProfile?ImportedProfile:SampleProfile;
            Panel->Page=TEXT("Loot");Panel->LootDraft.Reset();Panel->ConditionDraft.Reset();
            if(View==TEXT("Condition")){Panel->LootDraft=ACEUCMLootPresentation::Clone(Rule);Panel->ConditionDraft=Parse(TEXT(R"({"field":"legacy","key":2008,"args":[70,1.15,1.12]})"));}
            if(View==TEXT("Classic")){Panel->Page=TEXT("Legacy loot");Panel->LegacyDocument=Document;Panel->LegacyRule=Parse(TEXT(R"({"label":"Amuli chest color","action_id":10,"amount":4,"priority":0,"expression":"","requirements":[{"type":15,"body":"255\r\n0\r\n0\r\n10\r\n0.1\r\nAmuli Coat (Chest)\r\n"}]})"));}
            Panel->Rebuild();const FVector2D Size(Width,1100);auto* Target=FWidgetRenderer::CreateTargetFor(Size,TF_Bilinear,true);
            for(int Pass=0;Pass<3;++Pass){Renderer.DrawWidget(Target,Panel,Size,0);FlushRenderingCommands();}
            TArray<FColor> Pixels;FReadSurfaceDataFlags Flags;Flags.SetLinearToGamma(false);Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags);
            TestEqual(TEXT("Loot editor renders at desktop and narrow widths"),Pixels.Num(),Width*1100);
            TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(Width,1100,Pixels,PNG);
            FFileHelper::SaveArrayToFile(PNG,*(FPaths::ProjectSavedDir()/TEXT("Automation")/FString::Printf(TEXT("LootEditor-%s-%d.png"),*View,Width)));Target->ReleaseResource();
        }
    }
    return true;
}
}
#endif
