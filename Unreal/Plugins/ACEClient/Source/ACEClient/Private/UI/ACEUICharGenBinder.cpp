#include "UI/ACEUICharGenBinder.h"
#include "UI/ACEUICanvasWidget.h"
#include "UI/ACEUIElementManager.h"
#include "UI/ACEUILayoutResolver.h"
#include "UI/ACEUIResourceResolver.h"
#include "UI/ACERetailTextBlock.h"
#include "UI/ACECaptureImage.h"
#include "ACEClientSubsystem.h"
#include "ACEDatSubsystem.h"
#include "ACESession.h"
#include "ACEPlayerController.h"
#include "ACECharacterAppearanceComponent.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "InputCoreTypes.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Dat/ACEDatTextureResolver.h"
#include "Dat/ACEDatTextLayout.h"
#include "Protocol/ACEWindows1252.h"

namespace
{
const TCHAR* CharGenPages[]={TEXT(""),TEXT("CGHeritagePage"),TEXT("CGProfessionPage"),TEXT("CGSkillsPage"),TEXT("CGAppearancePage"),TEXT("CGTownPage"),TEXT("CGSummaryPage")};
const TCHAR* Navigation[]={TEXT(""),TEXT("CGHeritageButton"),TEXT("CGProfessionButton"),TEXT("CGSkillsButton"),TEXT("CGAppearanceButton"),TEXT("CGTownButton"),TEXT("CGSummaryButton")};
const TCHAR* HeritageButtons[]={TEXT(""),TEXT("AluvianRadio"),TEXT("RadioGhu"),TEXT("RadioSho"),TEXT("RadioViamont"),TEXT("RadioShadow"),TEXT("RadioGearKnight"),TEXT("RadioAunTumerok"),TEXT("RadioLugian"),TEXT("RadioEmpyrean"),TEXT("RadioPenumbraen"),TEXT("RadioUndead"),TEXT("RadioOlthoi"),TEXT("RadioOlthoiAcid")};
const TCHAR* HeritageTexts[]={TEXT(""),TEXT("Aluvian"),TEXT("Garu"),TEXT("Sho"),TEXT("Via"),TEXT("Shad"),TEXT("Gear"),TEXT("AunT"),TEXT("Lug"),TEXT("Emp"),TEXT("Shad"),TEXT("Und"),TEXT("Olthoi"),TEXT("OlthoiAcid")};
const TCHAR* ProfButtons[]={TEXT("CustomButton"),TEXT("BowButton"),TEXT("SwashButton"),TEXT("LifeButton"),TEXT("WarButton"),TEXT("WayButton"),TEXT("SoldierButton")};
const TCHAR* AttrNames[]={TEXT("Strength"),TEXT("Endurance"),TEXT("Coordination"),TEXT("Quickness"),TEXT("Focus"),TEXT("Self")};
const TCHAR* SpinNames[]={TEXT("HairSpin"),TEXT("EyesSpin"),TEXT("NoseSpin"),TEXT("MouthSpin"),TEXT("SkinSpin"),TEXT("HeadgearSpin"),TEXT("ShirtSpin"),TEXT("TrousersSpin"),TEXT("FootwearSpin")};
bool Visible(TSharedPtr<FACEUIElement> E){for(;E;E=E->Parent.Pin())if(!E->bVisible)return false;return true;}
bool Contains(TSharedPtr<FACEUIElement> E,FVector2D P){if(!E||!Visible(E))return false;auto O=E->GetScreenOrigin();return P.X>=O.X&&P.Y>=O.Y&&P.X<O.X+E->Width&&P.Y<O.Y+E->Height;}
}
bool UACEUICharGenBinder::Initialize(UACEClientSubsystem* InClient,UACEUICanvasWidget* InCanvas,AACEPlayerController* InController)
{
    Client=InClient;Canvas=InCanvas;Controller=InController;Manager=Client?Client->GetUIElementManager():nullptr;
    if(!Client||!Canvas||!Manager||!Canvas->WidgetTree)return false;
    auto Dat=Client->GetUIResourceResolver()->GetDatSubsystem();FString Error;
    if(!Dat||!Dat->GetPortalDat()||!Model.Load(*Dat->GetPortalDat(),Error)){UE_LOG(LogTemp,Error,TEXT("Character creation: %s"),*Error);return false;}
    Model.LoadStrings(Dat->GetDatDirectory());
    Model.RandomizeCharacter();
    Model.Selection.Slot=Client->GetCharacters().Num();
    ActivatedHandle=Manager->OnElementActivated.AddUObject(this,&UACEUICharGenBinder::Activate);
    if(auto Session=Client->GetSession())CreatedHandle=Session->OnCharacterCreated.AddWeakLambda(this,[this](uint32 Result,const FACECharacterInfo&){
        if(Result==1){bReturn=true;return;}
        const TCHAR* Key=Result==3?TEXT("ID_Character_Err_NameReserved"):Result==4?TEXT("ID_Character_Err_NameBanned"):Result==7?TEXT("ID_Character_Err_NameAdminDenied"):TEXT("ID_Character_Err_NameDBDown");
        Message=Model.Text(Key);if(Message.IsEmpty())Message=FString::Printf(TEXT("Character creation failed (%u). Please try again."),Result);
        ShowDialog(4,Message);bDirty=true;bNameFocus=true;
    });
    SetPage(1);return true;
}
void UACEUICharGenBinder::Shutdown()
{
    MouseUp(); bNameFocus=false;
    if(Manager)Manager->OnElementActivated.Remove(ActivatedHandle);
    if(Client&&Client->GetSession())Client->GetSession()->OnCharacterCreated.Remove(CreatedHandle);
    for(auto& P:Labels)if(P.Value)P.Value->RemoveFromParent();Labels.Reset();
    for(auto& I:ColorImages)if(I.Value)I.Value->RemoveFromParent();ColorImages.Reset();ColorTextures.Reset();
    if(Dialog){Dialog->bVisible=false;Dialog.Reset();}
    if(Tooltip){Tooltip->bVisible=false;Tooltip.Reset();}
    if(PreviewEnvironment)PreviewEnvironment->Destroy();PreviewEnvironment=nullptr;
    if(NameSelectionImage)NameSelectionImage->RemoveFromParent();NameSelectionImage=nullptr;
    if(NameCaretImage)NameCaretImage->RemoveFromParent();NameCaretImage=nullptr;
    if(PreviewImage)PreviewImage->RemoveFromParent();PreviewImage=nullptr;
    if(Preview)Preview->Destroy();Preview=nullptr;Capture=nullptr;Target=nullptr;SkillRows.Reset();SummaryRows.Reset();
    Canvas=nullptr;
}
TSharedPtr<FACEUIElement> UACEUICharGenBinder::Find(const TCHAR* Name) const{return Manager?Manager->FindElementByName(Name):nullptr;}
TSharedPtr<FACEUIElement> UACEUICharGenBinder::Under(const TCHAR* Parent,const TCHAR* Name) const{return Manager?Manager->FindElementUnder(Parent,Name):nullptr;}
TSharedPtr<FACEUIElement> UACEUICharGenBinder::Child(TSharedPtr<FACEUIElement> Parent,uint32 Id) const{if(!Parent)return nullptr;for(auto C:Parent->Children){if(C->ElementId==Id)return C;if(auto R=Child(C,Id))return R;}return nullptr;}
void UACEUICharGenBinder::Show(const TCHAR* Name,bool Value){if(auto E=Find(Name))E->bVisible=Value;}
void UACEUICharGenBinder::Selected(const TCHAR* Name,bool Value){if(auto E=Find(Name)){E->bHighlighted=Value;E->bUseExplicitState=E->States.Contains(0x10000016);E->DefaultState=E->bUseExplicitState?(Value?0x10000017:0x10000016):(Value?6:1);}}
void UACEUICharGenBinder::Label(TSharedPtr<FACEUIElement> E,const FString& Text)
{
    if(!E||!Canvas||!Canvas->WidgetTree||!Visible(E))return;
    auto& W=Labels.FindOrAdd(E->InstanceId);if(!W){W=Canvas->WidgetTree->ConstructWidget<UACERetailTextBlock>();Canvas->GetElementLayer()->AddChild(W);W->SetVisibility(ESlateVisibility::HitTestInvisible);}
    W->SetText(FText::FromString(Text));
    const auto* State=E->States.Find(E->PaintState);
    W->SetColorAndOpacity(State&&State->TextColor.IsSet()?State->TextColor.GetValue():E->TextColor.Get(FLinearColor::White));
    W->SetJustification(E->TextHorizontalJustification==1?ETextJustify::Center:E->TextHorizontalJustification==3?ETextJustify::Right:ETextJustify::Left);
    const FVector2D Scale=Manager->GetCanvasScale(Canvas->GetCachedGeometry().GetLocalSize());
    W->SetMargin(FMargin(E->TextMargins.Left*Scale.X,E->TextMargins.Top*Scale.Y,E->TextMargins.Right*Scale.X,E->TextMargins.Bottom*Scale.Y));
    W->SetAutoWrapText(!E->bTextOneLine);W->SetVisibility(ESlateVisibility::HitTestInvisible);
    int Z=1000;
    for(auto P=E;P;P=P->Parent.Pin())
    {
        if(P==Dialog){Z=2200;break;}
        if(P==Tooltip){Z=3200;break;}
    }
    Canvas->PlaceWidgetAtElement(W,E,Z);
}
void UACEUICharGenBinder::Label(const TCHAR* Name,const FString& Text){Label(Find(Name),Text);}
void UACEUICharGenBinder::StaticLabels(TSharedPtr<FACEUIElement> E)
{
    if(!E||!Visible(E))return;if(E->TextEntryId)Label(E,Model.Strings.FindRef(E->TextEntryId));for(auto C:E->Children)StaticLabels(C);
}
void UACEUICharGenBinder::SetPage(int32 InPage)
{
    if(Client&&Client->GetSession()&&Client->GetSession()->IsCharacterCreationPending())return;
    InPage=FMath::Clamp(InPage,1,6);if(Model.Selection.Heritage>=12&&(InPage==2||InPage==3||InPage==5))InPage=InPage>Page?(InPage==5?6:4):(InPage==5?4:1);
    if(Page==6)Model.Selection.Name=FACECharacterCreation::FormatName(Model.Selection.Name);
    Page=InPage;if(Page==2||Page==6)Model.FitProfession();
    NameCaret=NameAnchor=Model.Selection.Name.Len();NameDisplayStart=0;bNameDrag=false;bPreviewDirty=true;Message.Reset();bConfirmCredits=false;bNameFocus=Page==6;bSelectName=false;bHelp=false;DescriptionScroll=0;bDirty=true;
    for(int I=1;I<=6;++I)Show(CharGenPages[I],I==Page);
    if(auto E=Find(TEXT("CGPage")))E->DefaultState=0x10000024+Page;
}
void UACEUICharGenBinder::Tick(float Delta)
{
    if(bReturn){bReturn=false;if(Controller&&Client&&Client->GetSession()){Controller->ShowCharacterSelectUI(Client->GetCharacters(),Client->GetSession()->GetServerName());}return;}
    // Place labels every frame so they follow viewport resizing and the native state font.
    Refresh();RefreshPreview(Delta);RefreshTooltip(Delta);
}
void UACEUICharGenBinder::Refresh()
{
    if(!Model.Heritage()||!Model.Sex())return;
    for(auto& L:Labels)L.Value->SetVisibility(ESlateVisibility::Collapsed);
    for(auto& I:ColorImages)I.Value->SetVisibility(ESlateVisibility::Collapsed);
    if(NameSelectionImage)NameSelectionImage->SetVisibility(ESlateVisibility::Collapsed);
    if(NameCaretImage)NameCaretImage->SetVisibility(ESlateVisibility::Collapsed);
    const bool Pending=Client->GetSession()&&Client->GetSession()->IsCharacterCreationPending();
    for(int I=1;I<=6;++I){Show(CharGenPages[I],Page==I);Selected(Navigation[I],Page==I);Show(Navigation[I],Model.Selection.Heritage<12||I==1||I==4||I==6);}
    Show(TEXT("CGLeftButton"),Page>1);Show(TEXT("CGRightButton"),Page<6);Show(TEXT("CGFinishButton"),Page==6);
    Show(TEXT("AdminButton"),false);Show(TEXT("EnvoyButton"),false);
    if(auto E=Find(TEXT("CGFinishButton")))E->bActivatable=!Pending;
    StaticLabels(Find(TEXT("RootCharGenMaster")));
    if(Page==1)
    {
        for(int I=1;I<=13;++I)if(auto E=Find(HeritageButtons[I])){E->bUseExplicitState=true;E->DefaultState=I==Model.Selection.Heritage?0x10000017:0x10000016;E->bVisible=Model.Heritages.Contains(I);}
        static const uint32 Portrait[]={0,0x10000021,0x10000022,0x10000023,0x10000024,0x10000058,0x1000005A,0x1000005F,0x10000060,0x1000005C,0x10000059,0x1000005B,0x1000005D,0x1000005E};
        if(auto E=Find(TEXT("HeritageBackImage"))){E->bUseExplicitState=true;E->DefaultState=Portrait[Model.Selection.Heritage];}
        FString Description=Model.Text(FString::Printf(TEXT("ID_CharGen_%sText"),HeritageTexts[Model.Selection.Heritage]));
        Description+=TEXT("\n\n")+Model.Text(TEXT("ID_CharGen_Heritage_StartingSkills_Header"))+TEXT("\n")+Model.Text(TEXT("ID_CharGen_Heritage_StartingSkills"));
        Description+=TEXT("\n\n")+Model.Text(TEXT("ID_CharGen_Heritage_BonusSkills_Trained_Header"))+TEXT("\n")+Model.Text(FString::Printf(TEXT("ID_CharGen_%sText_BonusSkills_Trained"),HeritageTexts[Model.Selection.Heritage]));
        auto E=Find(TEXT("RaceDesc"));if(E){auto T=Child(E,0x100002e6);ScrollText(T?T:E,Description);}
    }
    if(Page==2)
    {
        for(int I=0;I<7;++I)Selected(ProfButtons[I],Model.Selection.Profession==I);
        const auto& T=Model.Heritage()->Professions[Model.Selection.Profession];static const TCHAR* Keys[]={TEXT("Custom"),TEXT("Bow"),TEXT("Swash"),TEXT("Life"),TEXT("War"),TEXT("Way"),TEXT("Soldier")};ScrollText(Find(TEXT("ProfText")),Model.Text(FString::Printf(TEXT("ID_CharGen_%sText"),Keys[Model.Selection.Profession])));
        Label(Under(TEXT("AttribCred"),TEXT("CGBigMeterValue")),FString::FromInt(Model.AttributeCredits()));
        for(int I=0;I<6;++I){FString N=FString(AttrNames[I])+TEXT("Slider");auto Slider=Find(*N);Label(Under(*N,TEXT("AttribValue")),FString::FromInt(Model.Selection.Attributes[I]));Label(Under(*N,TEXT("AttribText")),AttrNames[I]);if(auto L=Under(*N,TEXT("Locker"))){L->bUseExplicitState=true;L->DefaultState=Model.Selection.Locked[I]?0x10000019:0x10000018;}if(auto A=Under(*N,TEXT("AttribSlider"))){A->bUseExplicitState=true;A->DefaultState=Model.Selection.Locked[I]?0x10000019:0x10000018;}if(auto Thumb=Child(Under(*N,TEXT("AttribSlider")),1))Thumb->X=FMath::RoundToInt((Model.Selection.Attributes[I]-10)*247.f/90.f);}
        Label(Under(TEXT("AttribHealth"),TEXT("CGSmallMeterValue")),FString::FromInt(Model.Selection.Attributes[1]/2));Label(Under(TEXT("AttribStamina"),TEXT("CGSmallMeterValue")),FString::FromInt(Model.Selection.Attributes[1]));Label(Under(TEXT("AttribMana"),TEXT("CGSmallMeterValue")),FString::FromInt(Model.Selection.Attributes[5]));
    }
    if(Page==3)RefreshSkills();
    if(Page==4)
    {
        const bool SpecialBody=Model.Selection.Heritage==6||Model.Selection.Heritage>=12;
        if(SpecialBody&&bClothes){bClothes=false;ColorField=0;Zoom=0;bPreviewDirty=true;}
        Selected(TEXT("FemaleButton"),Model.Selection.Sex==2);Selected(TEXT("MaleButton"),Model.Selection.Sex==1);Selected(TEXT("FaceButton"),!bClothes);Selected(TEXT("ClothesButton"),bClothes);
        Show(TEXT("FaceChoices"),!bClothes);Show(TEXT("ClothesChoices"),bClothes);
        Show(TEXT("ClothesButton"),!SpecialBody);Show(TEXT("NoseSpin"),!SpecialBody);Show(TEXT("MouthSpin"),!SpecialBody);
        if(auto Skin=Find(TEXT("SkinSpin")))Skin->Y=SpecialBody?90:180;
        for(uint32 Id:{0x1000030au,0x1000030bu})if(auto Arrow=Child(Find(TEXT("EyesSpin")),Id))Arrow->bVisible=!SpecialBody;
        const FString Prefix=Model.Selection.Heritage==6?TEXT("ID_CharGen_GearText_"):TEXT("ID_CharGen_OlthoiText_");
        Label(TEXT("HairSpin"),Model.Text(SpecialBody?Prefix+TEXT("HairButton"):TEXT("ID_CharGen_HairStyle")));
        Label(TEXT("EyesSpin"),Model.Text(SpecialBody?Prefix+TEXT("EyesButton"):TEXT("ID_CharGen_Eyes")));
        Label(TEXT("SkinSpin"),Model.Text(SpecialBody?Prefix+TEXT("SkinButton"):TEXT("ID_CharGen_Skin")));
        for(int I=0;I<9;++I)Selected(SpinNames[I],ColorField==I);
        TArray<uint32> Colors;if(ColorField==0)Colors=Model.Sex()->HairColors;else if(ColorField==1)Colors=Model.Sex()->EyeColors;else if(ColorField>=2&&ColorField<=4)Colors.Add(0);else if(ColorField>=5)Colors=Model.GearColors(ColorField-5);
        uint32 SelectedColor=ColorField==0?Model.Selection.HairColor:ColorField==1?Model.Selection.EyeColor:0;if(ColorField>=5)SelectedColor=Colors.IndexOfByKey(Model.Selection.GearColor[ColorField-5]);
        for(int I=0;I<9;++I){Show(*FString::Printf(TEXT("ColorSpot%d"),I+1),I<Colors.Num());Show(*FString::Printf(TEXT("ColorArrow%d"),I+1),I<Colors.Num()&&I==SelectedColor);}
        Show(TEXT("GradientScroll"),ColorField!=1&&!Colors.IsEmpty());
        if(auto Disk=Find(TEXT("Disk"))){auto Samples=Model.ColorSamples(ColorField);const bool Plug=ColorField==1||!Samples.IsValidIndex(SelectedColor);Disk->ImageFileId=Model.MappedAsset(0x25000010,Plug?0x10000010:0x1000000e);Disk->ImageTint=Plug?FLinearColor::White:FLinearColor(Samples[SelectedColor]);}
        double Hue=ColorField==0?Model.Selection.HairHue:(ColorField>=2&&ColorField<=4)?Model.Selection.SkinHue:ColorField>=5?Model.Selection.GearHue[ColorField-5]:0;
        if(auto Thumb=Child(Find(TEXT("GradientScroll")),1))Thumb->Y=FMath::RoundToInt(Hue*64.);
        FString Desc=Model.Sex()->Name; if(ColorField>=5){int I=ColorField-5;if(Model.Sex()->Gear[I].IsValidIndex(Model.Selection.GearStyle[I]))Desc=Model.Sex()->Gear[I][Model.Selection.GearStyle[I]].Name;}
        auto TextBox=Find(TEXT("AppearanceTextBox"));ScrollText(TextBox,Model.Strings.FindRef(TextBox->TextEntryId));RefreshColors();
    }
    if(Page==5)
    {
        static const TCHAR* Towns[]={TEXT("Holtburg"),TEXT("Shoushi"),TEXT("Yaraq"),TEXT("Sanamar")};
        for(int I=0;I<4;++I){bool Allowed=Model.Heritage()->PrimaryStarts.Contains(I)||Model.Heritage()->SecondaryStarts.Contains(I);Show(Towns[I],Allowed);Selected(Towns[I],Model.Selection.StartArea==I);}
        Label(TEXT("TownName"),Model.StartAreas.IsValidIndex(Model.Selection.StartArea)?Model.StartAreas[Model.Selection.StartArea]:FString());
        ScrollText(Find(TEXT("TownText")),Model.Text(FString::Printf(TEXT("ID_CharGen_%sText"),Model.Selection.StartArea==0?TEXT("Holt"):*Model.StartAreas[Model.Selection.StartArea])));
    }
    if(Page==6)
    {
        RefreshSummary();RefreshName();
        if(!Pending&&Message.IsEmpty()){
            static const TCHAR* NameKeys[]={TEXT(""),TEXT("Alu"),TEXT("Gharu"),TEXT("Sho"),TEXT("Via"),TEXT("Shad"),TEXT("Gear"),TEXT("AunT"),TEXT("Lug"),TEXT("Emp"),TEXT("Shad"),TEXT("Und"),TEXT("Olthoi"),TEXT("Olthoi")};
            ScrollText(Find(TEXT("HowToBox")),Model.Text(TEXT("ID_CharGen_SummaryHowTo"))+TEXT("\n\n")+Model.Text(FString::Printf(TEXT("ID_CharGen_%s%sNames"),NameKeys[Model.Selection.Heritage],Model.Selection.Sex==2?TEXT("Female"):TEXT("Male")))+TEXT("\n\n")+Model.Text(TEXT("ID_CharGen_SummaryHowToEnd")));
        }
        if(Pending)Label(TEXT("HowToBox"),TEXT("Creating character. Waiting for the server..."));else if(!Message.IsEmpty())Label(TEXT("HowToBox"),Message);
    }
    if(DialogAction){Label(Child(Dialog,0x3e),DialogText);Label(Child(Dialog,0x17),DialogAction==4?TEXT("OK"):TEXT("Yes"));Label(Child(Dialog,0x19),TEXT("No"));}
    bDirty=false;
}
void UACEUICharGenBinder::RefreshSkills()
{
    auto List=Find(TEXT("SkillsListBox"));if(!List)return;
    if(bDirty)
    {
        DisplaySkills.Reset();TArray<uint32> Ids;Model.Skills.GetKeys(Ids);Ids.Sort([&](uint32 A,uint32 B){return Model.Skills[A].Name.Compare(Model.Skills[B].Name,ESearchCase::IgnoreCase)<0;});
        for(int Group=3;Group>=0;--Group){DisplaySkills.Add(-Group-1);for(uint32 Id:Ids){auto& S=Model.Skills[Id];if(!S.Chargen)continue;uint32 Level=Model.Selection.Skills[Id];int G=Level>=2?Level:(S.MinLevel<=1?1:0);if(G==Group)DisplaySkills.Add(Id);}}
    }
    SkillScroll=FMath::Clamp(SkillScroll,0,FMath::Max(0,DisplaySkills.Num()-11));
    for(auto E:SkillRows)if(E)E->bVisible=false;
    for(int I=0;I<11&&SkillScroll+I<DisplaySkills.Num();++I)
    {
        int Id=DisplaySkills[SkillScroll+I];uint32 Template=Id<0?0x100002f4:0x100002ff;
        int Index=I*2+(Id<0?0:1);while(SkillRows.Num()<=Index)SkillRows.Add(nullptr);
        auto& Row=SkillRows[Index];if(!Row){Row=UACEUILayoutResolver::LoadTemplate(0x2100004c,Template);if(!Row)continue;List->AddChild(Row);}Row->Y=I*26;Row->bVisible=true;Row->SetElementName(FString::Printf(TEXT("CreationSkill%d"),Id));
        if(Id<0){const TCHAR* Names[]={TEXT("Unusable Untrained"),TEXT("Usable Untrained"),TEXT("Trained"),TEXT("Specialized")};Label(Child(Row,0x100002f6),Names[-Id-1]);Label(Child(Row,0x100002f7),TEXT("Level"));continue;}
        auto& S=Model.Skills[Id];uint32 Level=Model.Selection.Skills[Id];auto Cost=Model.SkillCost(Id);int Up=Level==1?Cost.X:Cost.Y-Cost.X;
        Label(Child(Row,0x10000301),S.Name);Label(Child(Row,0x10000302),FString::FromInt(Model.SkillValue(Id)));Label(Child(Row,0x10000303),Level==3?TEXT("0"):Up>=0&&Up<999?FString::FromInt(Up):TEXT(""));Label(Child(Row,0x10000306),Level>=2?FString::FromInt(Level==3?Cost.Y-Cost.X:Cost.X):TEXT("0"));
        if(auto E=Child(Row,0x10000300))E->ImageFileId=S.Icon;
        if(auto E=Child(Row,0x10000304)){E->bActivatable=Level<3&&Up>=0&&Up<=Model.SkillCredits();E->bUseExplicitState=true;E->DefaultState=E->bActivatable?0x1000001b:0x1000001a;}
        if(auto E=Child(Row,0x10000305)){E->bActivatable=Level==3||(Level==2&&Cost.X>0);E->bUseExplicitState=true;E->DefaultState=E->bActivatable?0x1000001b:0x1000001a;}
        Row->DefaultState=SelectedSkill==Id?6:1;
    }
    Label(Under(TEXT("RemainingSkillCredits"),TEXT("CGSmallMeterValue")),FString::FromInt(Model.SkillCredits()));
    if(auto S=Model.Skills.Find(SelectedSkill))
    {
        Label(TEXT("SkillInfoBoxTitle"),FString::Printf(TEXT("%s (%d)"),*S->Name,Model.SkillValue(SelectedSkill)));
        static const TCHAR* FormulaAttrs[]={TEXT(""),TEXT("Strength"),TEXT("Endurance"),TEXT("Quickness"),TEXT("Coordination"),TEXT("Focus"),TEXT("Self")};
        TArray<FString> Terms;
        for(int I=0;I<2;++I){uint32 Weight=S->Formula[1+I],Attribute=S->Formula[4+I];if(Weight&&Attribute>0&&Attribute<7)Terms.Add(Weight==1?FString(FormulaAttrs[Attribute]):FString::Printf(TEXT("(%u x %s)"),Weight,FormulaAttrs[Attribute]));}
        FString Formula=FString::Join(Terms,TEXT(" + "));if(Terms.Num()>1)Formula=TEXT("(")+Formula+TEXT(")");if(S->Formula[3]>1)Formula+=FString::Printf(TEXT(" / %u"),S->Formula[3]);if(S->Formula[0])Formula+=FString::Printf(TEXT(" + %u"),S->Formula[0]);
        uint32 Level=Model.Selection.Skills[SelectedSkill];FString Bonus=Level==3?TEXT("Specialization Bonus  +10\n"):Level==2?TEXT("Training Bonus  +5\n"):TEXT("");
        ScrollText(Find(TEXT("SkillInfoBoxText")),S->Description+TEXT("\n")+Bonus+TEXT("Formula : ")+Formula);
    }
    if(auto Thumb=Child(Find(TEXT("SkillsScrollbar")),1))Thumb->Y=17+FMath::RoundToInt(float(SkillScroll)*234/FMath::Max(1,DisplaySkills.Num()-11));
}
void UACEUICharGenBinder::RefreshSummary()
{
    auto List=Find(TEXT("SummaryListBox"));
    if(!List)return;
    struct FLine { uint32 Template; FString Name,Value; };
    TArray<FLine> Lines;
    const auto Plain=[&](const FString& Text){Lines.Add({0x100002f8,Text,{}});};
    const auto Title=[&](const FString& Text){Lines.Add({0x100002fa,Text,{}});};
    const auto Value=[&](const FString& Name,int Score){Lines.Add({0x100002fb,Name,FString::FromInt(Score)});};
    Plain(TEXT("Profession: ")+Model.Heritage()->Professions[Model.Selection.Profession].Name);
    Plain(TEXT("Gender: ")+Model.Sex()->Name);
    Plain(TEXT("Heritage: ")+Model.Heritage()->Name);
    Plain(TEXT("Starting Town: ")+Model.StartAreas[Model.Selection.StartArea]);
    Title(TEXT("Attributes"));
    for(int I=0;I<6;++I)Value(AttrNames[I],Model.Selection.Attributes[I]);
    Value(TEXT("Health"),Model.Selection.Attributes[1]/2);
    Value(TEXT("Stamina"),Model.Selection.Attributes[1]);
    Value(TEXT("Mana"),Model.Selection.Attributes[5]);
    TArray<uint32> Ids;
    Model.Skills.GetKeys(Ids);
    Ids.Sort([&](uint32 A,uint32 B){return Model.Skills[A].Name.Compare(Model.Skills[B].Name,ESearchCase::IgnoreCase)<0;});
    for(int Group=3;Group>=0;--Group)
    {
        static const TCHAR* Titles[]={TEXT("Unuseable Untrained Skills"),TEXT("Useable Untrained Skills"),TEXT("Trained Skills"),TEXT("Specialized Skills")};
        Title(Titles[Group]);
        for(uint32 Id:Ids)
        {
            const auto& Skill=Model.Skills[Id];
            if(!Skill.Chargen)continue;
            const uint32 Level=Model.Selection.Skills[Id];
            const int SkillGroup=Level>=2?Level:(Skill.MinLevel<=1?1:0);
            if(SkillGroup==Group)Value(Skill.Name,Model.SkillValue(Id));
        }
    }
    const int VisibleRows=List->Height/20;
    SummaryMaxScroll=FMath::Max(0,Lines.Num()-VisibleRows);
    SummaryScroll=FMath::Clamp(SummaryScroll,0,SummaryMaxScroll);
    for(auto E:SummaryRows)if(E)E->bVisible=false;
    // Keep one instance of each native row style per visible line. Scrolling never
    // retains an unbounded set of widgets or substitutes value rows for headers.
    for(int I=0;I<VisibleRows&&SummaryScroll+I<Lines.Num();++I)
    {
        auto& Line=Lines[SummaryScroll+I];
        int Index=I*3+(Line.Template==0x100002f8?0:Line.Template==0x100002fa?1:2);
        while(SummaryRows.Num()<=Index)SummaryRows.Add(nullptr);
        auto& Row=SummaryRows[Index];
        if(!Row){Row=UACEUILayoutResolver::LoadTemplate(0x2100004c,Line.Template);if(!Row)continue;List->AddChild(Row);}
        Row->Y=I*20;Row->Width=List->Width;Row->bVisible=true;
        if(Line.Template==0x100002fb){Label(Child(Row,0x100002fc),Line.Name);Label(Child(Row,0x100002fd),Line.Value);}
        else {auto Text=Child(Row,Line.Template==0x100002f8?0x100002f9:0x100000fe);if(Text)Text->Width=List->Width;Label(Text,Line.Name);}
    }
    if(auto Bar=Find(TEXT("SummaryScrollbar")))if(auto Thumb=Child(Bar,1))Thumb->Y=17+FMath::RoundToInt(float(SummaryScroll)*(Bar->Height-34-Thumb->Height)/FMath::Max(1,SummaryMaxScroll));
}

void UACEUICharGenBinder::RefreshName()
{
    auto E=Find(TEXT("NameTextBox"));
    if(!E)return;
    FACEDatFont Font;
    if(!Client->GetUIResourceResolver()->ResolveFont(E->FontId,Font)){Label(E,Model.Selection.Name);return;}
    const FString& Name=Model.Selection.Name;
    NameCaret=FMath::Clamp(NameCaret,0,Name.Len());
    NameAnchor=FMath::Clamp(NameAnchor,0,Name.Len());
    NameDisplayStart=FMath::Clamp(NameDisplayStart,0,NameCaret);
    const auto Width=[&](int A,int B){int Result=0;for(int I=A;I<B;++I)Result+=ACEDatText::Advance(Font,Name[I]);return Result;};
    const int Available=FMath::Max(1,int(E->Width-E->TextMargins.Left-E->TextMargins.Right)-2);
    while(NameDisplayStart<NameCaret&&Width(NameDisplayStart,NameCaret)>Available)++NameDisplayStart;
    if(!bNameFocus)NameDisplayStart=0;
    int Last=NameDisplayStart;
    while(Last<Name.Len()&&Width(NameDisplayStart,Last+1)<=Available)++Last;
    FString Prompt=Model.Text(TEXT("ID_CharGen_NamePrompt"));Prompt.ReplaceInline(TEXT("\\["),TEXT("["));Prompt.ReplaceInline(TEXT("\\]"),TEXT("]"));
    Label(E,Name.IsEmpty()?Prompt:Name.Mid(NameDisplayStart,Last-NameDisplayStart));
    const auto Bar=[&](TObjectPtr<UImage>& Image,float X,float W,FLinearColor Color,int Z)
    {
        if(!Image){Image=Canvas->WidgetTree->ConstructWidget<UImage>();Canvas->GetElementLayer()->AddChild(Image);FSlateBrush Brush;Brush.DrawAs=ESlateBrushDrawType::Image;Image->SetBrush(Brush);}
        Image->SetColorAndOpacity(Color);Image->SetVisibility(ESlateVisibility::HitTestInvisible);
        Canvas->PlaceWidgetAtElement(Image,E,Z);
        const FVector2D Scale=Canvas->GetLastScale2D();
        auto Slot=Cast<UCanvasPanelSlot>(Image->Slot);
        Slot->SetPosition(Slot->GetPosition()+FVector2D((E->TextMargins.Left+X)*Scale.X,(E->Height-Font.MaxCharHeight)*.5f*Scale.Y));
        Slot->SetSize(FVector2D(W*Scale.X,Font.MaxCharHeight*Scale.Y));
    };
    if(bNameFocus&&NameCaret!=NameAnchor)
    {
        const int First=FMath::Clamp(FMath::Min(NameCaret,NameAnchor),NameDisplayStart,Last);
        const int End=FMath::Clamp(FMath::Max(NameCaret,NameAnchor),NameDisplayStart,Last);
        if(End>First)Bar(NameSelectionImage,Width(NameDisplayStart,First),Width(First,End),FLinearColor(.02f,.06f,.35f,1),990);
    }
    if(bNameFocus&&Name.IsEmpty())
    {
        int PromptWidth=0;for(TCHAR C:Prompt)PromptWidth+=ACEDatText::Advance(Font,C);
        Bar(NameSelectionImage,0,FMath::Min(PromptWidth,Available),FLinearColor(.02f,.06f,.35f,1),990);
    }
    if(bNameFocus&&FMath::Fmod(FPlatformTime::Seconds(),1.)<.5)Bar(NameCaretImage,Width(NameDisplayStart,NameCaret),1,FLinearColor::White,1100);
}
void UACEUICharGenBinder::Spin(int32 Field,int32 Direction)
{
    auto S=Model.Sex();if(!S)return;ColorField=Field;
    auto Step=[&](uint32& V,int N){V=N?(int32(V)+Direction+N)%N:0;};
    switch(Field){case 0:Step(Model.Selection.HairStyle,S->Hair.Num());break;case 1:Step(Model.Selection.Eyes,S->Eyes.Num());break;case 2:Step(Model.Selection.Nose,S->Nose.Num());break;case 3:Step(Model.Selection.Mouth,S->Mouth.Num());break;case 4:break;default:if(Field>=5&&Field<=8){int I=Field-5;if(I==0){uint32 Choice=Model.Selection.GearStyle[I]+1;Step(Choice,S->Gear[I].Num()+1);Model.Selection.GearStyle[I]=Choice-1;}else Step(Model.Selection.GearStyle[I],S->Gear[I].Num());auto Colors=Model.GearColors(I);if(!Colors.Contains(Model.Selection.GearColor[I]))Model.Selection.GearColor[I]=Colors.IsEmpty()?0:Colors[0];}break;}
    bPreviewDirty=bDirty=true;
}
void UACEUICharGenBinder::Submit()
{
    Model.Selection.Name=Model.Selection.Name.TrimStartAndEnd();
    while(!Model.Selection.Name.IsEmpty()&&FString(TEXT("[] ")).Contains(Model.Selection.Name.Left(1)))Model.Selection.Name.RightChopInline(1);
    while(!Model.Selection.Name.IsEmpty()&&FString(TEXT("[] ")).Contains(Model.Selection.Name.Right(1)))Model.Selection.Name.LeftChopInline(1);
    Model.Selection.Name=FACECharacterCreation::FormatName(Model.Selection.Name);
    NameCaret=NameAnchor=Model.Selection.Name.Len();
    if(Model.Selection.Name.IsEmpty()){ShowDialog(4,Model.Text(TEXT("ID_CharGen_NoNameWarning")));return;}
    if(!Model.Validate(Message)){ShowDialog(4,Message);bDirty=true;return;}
    if(Model.AttributeCredits()>0&&!bConfirmCredits){ShowDialog(1,Model.Text(TEXT("ID_CharGen_CreditWarning")));return;}
    if(!Client||!Client->GetSession()||!Client->GetSession()->CreateCharacter(Model.Selection))
        ShowDialog(4,TEXT("Unable to create a character: the account is disconnected or has no empty slots."));
    bDirty=true;
}
void UACEUICharGenBinder::Activate(TSharedPtr<FACEUIElement> E)
{
    if(!E)return;bool Pending=Client&&Client->GetSession()&&Client->GetSession()->IsCharacterCreationPending();if(Pending)return;
    if(DialogAction){if(E==Child(Dialog,0x17)){CloseDialog(true);return;}if(E==Child(Dialog,0x19)){CloseDialog(false);return;}for(auto P=E;P;P=P->Parent.Pin()){if(P==Child(Dialog,0x17)){CloseDialog(true);return;}if(P==Child(Dialog,0x19)){CloseDialog(false);return;}}return;}
    FString Name=E->ElementName;
    for(auto P=E;P;P=P->Parent.Pin())
    {
        Name=P->ElementName;
        for(int I=1;I<=6;++I)if(Name==Navigation[I]){SetPage(I);return;}
        if(Name==TEXT("CGLeftButton")){SetPage(Page-1);return;}if(Name==TEXT("CGRightButton")){SetPage(Page+1);return;}
        if(Name==TEXT("CGFinishButton")){Submit();return;}if(Name==TEXT("CGExitButton")){ShowDialog(2,Model.Text(TEXT("ID_CharGen_ExitWarning")));return;}
        if(Name==TEXT("CGHelpButton")){ShowDialog(4,Model.Text(TEXT("ID_CharGen_SummaryHowTo"))+TEXT("\n\n")+Model.Text(TEXT("ID_CharGen_SummaryHowToEnd")));return;}
        if(Name==TEXT("CGRandomButton")){if(Page==6)ShowDialog(3,Model.Text(TEXT("ID_CharGen_RandomizeWarning")));else RandomizePage();return;}
        for(int I=1;I<=13;++I)if(Name==HeritageButtons[I]){Model.SelectHeritage(I);bDirty=bPreviewDirty=true;return;}
        for(int I=0;I<7;++I)if(Name==ProfButtons[I]){Model.SelectProfession(I);bDirty=true;return;}
        if(Name==TEXT("FemaleButton")||Name==TEXT("MaleButton")){Model.SelectSex(Name==TEXT("FemaleButton")?2:1);bDirty=bPreviewDirty=true;return;}
        if(Name==TEXT("FaceButton")||Name==TEXT("ClothesButton")){bClothes=Name==TEXT("ClothesButton");Zoom=bClothes?1:0;ColorField=bClothes?5:0;bPreviewDirty=bDirty=true;return;}
        for(int I=0;I<9;++I)if(Name==SpinNames[I]){Spin(I,E->ElementName.Contains(TEXT("LeftArrow"))?-1:E->ElementName.Contains(TEXT("RightArrow"))?1:0);return;}
        if(Name.StartsWith(TEXT("ColorSpot"))){int Index=FCString::Atoi(*Name.Mid(9))-1;if(Index<0)return;if(ColorField==0&&Model.Sex()->HairColors.IsValidIndex(Index))Model.Selection.HairColor=Index;if(ColorField==1&&Model.Sex()->EyeColors.IsValidIndex(Index))Model.Selection.EyeColor=Index;if(ColorField>=5){auto Colors=Model.GearColors(ColorField-5);if(Colors.IsValidIndex(Index))Model.Selection.GearColor[ColorField-5]=Colors[Index];}bDirty=bPreviewDirty=true;return;}
        if(Name==TEXT("RotateClockwise")||Name==TEXT("RotateCounterClockwise")){PreviewYaw+=Name==TEXT("RotateClockwise")?15:-15;return;}
        if(Name==TEXT("ZoomIn")||Name==TEXT("ZoomOut")){Zoom=Name==TEXT("ZoomIn")?0:1;return;}
        for(int I=0;I<4;++I)if(Name==Model.StartAreas[I]){Model.Selection.StartArea=I;bDirty=true;return;}
        if(Name.StartsWith(TEXT("CreationSkill"))){int Id=FCString::Atoi(*Name.Mid(13));if(Id<0)return;if(SelectedSkill!=Id)DescriptionScroll=0;SelectedSkill=Id;if(E->bActivatable&&E->ElementId==0x10000304)Model.SetSkill(Id,Model.Selection.Skills[Id]+1);if(E->bActivatable&&E->ElementId==0x10000305)Model.SetSkill(Id,Model.Selection.Skills[Id]-1);bDirty=true;return;}
        if(Name==TEXT("ScrollBar_Up")||Name==TEXT("ScrollBar_Down")){int D=Name==TEXT("ScrollBar_Up")?-1:1;auto Bar=P->Parent.Pin();if(Bar==Find(TEXT("SkillsScrollbar")))SkillScroll+=D;else if(Bar==Find(TEXT("SummaryScrollbar")))SummaryScroll+=D;else DescriptionScroll=FMath::Max(0,DescriptionScroll+D);bDirty=true;return;}
    }
}
FVector2D UACEUICharGenBinder::Relative(FVector2D P) const{auto E=Find(TEXT("RootCharGenMaster"));return E?P-FVector2D(E->GetScreenOrigin()):P;}
bool UACEUICharGenBinder::MouseDown(FVector2D P)
{
    TooltipClock=0;if(Tooltip)Tooltip->bVisible=false;
    if(Client&&Client->GetSession()&&Client->GetSession()->IsCharacterCreationPending())return true;
    if(DialogAction)return false;
    LastMouse=P;
    if(Page==4){if(Contains(Find(TEXT("RotateClockwise")),P)){RotateDirection=1;return true;}if(Contains(Find(TEXT("RotateCounterClockwise")),P)){RotateDirection=-1;return true;}}
    if(auto Hit=Manager->HitTestCanvas(P.X,P.Y))for(auto E=Hit;E;E=E->Parent.Pin())if(E->Type==ACEUI::ElementType::Scrollbar&&E->ElementName!=TEXT("GradientScroll")&&E->ElementName!=TEXT("AttribSlider")&&Hit->ElementName!=TEXT("ScrollBar_Up")&&Hit->ElementName!=TEXT("ScrollBar_Down")){DragScrollbar=E;MouseMove(P);return true;}
    if(Page==2)for(int I=0;I<6;++I){FString N=FString(AttrNames[I])+TEXT("Slider");if(Contains(Under(*N,TEXT("Locker")),P)){Model.Selection.Locked[I]=!Model.Selection.Locked[I];bDirty=true;return true;}if(Contains(Under(*N,TEXT("AttribSlider")),P)){DragAttribute=I;MouseMove(P);return true;}}
    if(Page==4&&Contains(Find(TEXT("GradientScroll")),P)){bGradientDrag=true;MouseMove(P);return true;}
    if((Page==4&&Contains(Find(TEXT("3DViewport")),P))||(Page==6&&Contains(Find(TEXT("Summary3DViewport")),P))){bPreviewDrag=true;return true;}
    if(Page==6){bNameFocus=Contains(Find(TEXT("NameTextBox")),P);if(bNameFocus){NameCaret=NameAnchor=NamePosition(P);bNameDrag=true;bSelectName=false;return true;}}
    return false;
}
void UACEUICharGenBinder::MouseMove(FVector2D P)
{
    if(!P.Equals(LastMouse,.1f)){TooltipClock=0;if(Tooltip)Tooltip->bVisible=false;}
    if(DragAttribute>=0){FString N=FString(AttrNames[DragAttribute])+TEXT("Slider");auto E=Under(*N,TEXT("AttribSlider"));if(E)Model.SetAttribute(DragAttribute,10+FMath::RoundToInt((P.X-E->GetScreenOrigin().X-18.f)*90/247));bDirty=true;}
    if(DragScrollbar){auto Thumb=Child(DragScrollbar,1);const float Travel=FMath::Max(1,DragScrollbar->Height-34-(Thumb?Thumb->Height:39));float Fraction=FMath::Clamp((P.Y-DragScrollbar->GetScreenOrigin().Y-17-(Thumb?Thumb->Height:39)*.5f)/Travel,0.f,1.f);if(DragScrollbar==Find(TEXT("SkillsScrollbar")))SkillScroll=FMath::RoundToInt(Fraction*FMath::Max(0,DisplaySkills.Num()-11));else if(DragScrollbar==Find(TEXT("SummaryScrollbar")))SummaryScroll=FMath::RoundToInt(Fraction*SummaryMaxScroll);else DescriptionScroll=FMath::RoundToInt(Fraction*DescriptionMaxScroll);bDirty=true;}
    if(bGradientDrag){auto E=Find(TEXT("GradientScroll"));if(E){double H=FMath::Clamp((P.Y-E->GetScreenOrigin().Y-10.)/64.,0.,1.);if(ColorField==0)Model.Selection.HairHue=H;else if(ColorField>=2&&ColorField<=4)Model.Selection.SkinHue=H;else if(ColorField>=5)Model.Selection.GearHue[ColorField-5]=H;bDirty=bPreviewDirty=true;}}
    if(bPreviewDrag)PreviewYaw+=(P.X-LastMouse.X);LastMouse=P;
    if(bNameDrag)NameCaret=NamePosition(P);
}
void UACEUICharGenBinder::MouseUp(){DragAttribute=-1;bGradientDrag=false;bPreviewDrag=false;bNameDrag=false;RotateDirection=0;DragScrollbar.Reset();}
int32 UACEUICharGenBinder::NamePosition(FVector2D P) const
{
    auto E=Find(TEXT("NameTextBox"));if(!E)return 0;FACEDatFont Font;int Position=NameDisplayStart;
    float X=P.X-E->GetScreenOrigin().X-E->TextMargins.Left;
    if(Client->GetUIResourceResolver()->ResolveFont(E->FontId,Font))
        for(TCHAR C:Model.Selection.Name.Mid(NameDisplayStart)){const int W=ACEDatText::Advance(Font,C);if(X<W*.5f)break;X-=W;++Position;}
    else Position=Model.Selection.Name.Len();
    return Position;
}
bool UACEUICharGenBinder::MouseWheel(FVector2D P,float D)
{
    if(DialogAction)return true;
    if(Page==3&&!Contains(Find(TEXT("SkillInfoBoxText")),P))SkillScroll-=FMath::RoundToInt(D*3);else if(Page==6&&Contains(Find(TEXT("SummaryListBox")),P))SummaryScroll-=FMath::RoundToInt(D*3);else if(Page==4&&!Contains(Find(TEXT("AppearanceTextBox")),P))Zoom=FMath::Clamp(Zoom-D*.1f,0.f,1.f);else DescriptionScroll=FMath::Max(0,DescriptionScroll-FMath::RoundToInt(D));bDirty=true;return true;
}
void UACEUICharGenBinder::InsertName(const FString& Text)
{
    int Begin=FMath::Min(NameCaret,NameAnchor),End=FMath::Max(NameCaret,NameAnchor);
    Model.Selection.Name.RemoveAt(Begin,End-Begin);NameCaret=NameAnchor=Begin;
    bool TooLong=false;
    for(TCHAR C:Text)if(FACECharacterCreation::IsNameCharacter(C)){if(Model.Selection.Name.Len()>=32){TooLong=true;break;}Model.Selection.Name.InsertAt(NameCaret,C);++NameCaret;}
    if(TooLong)ShowDialog(4,Model.Text(TEXT("ID_CharGen_NameTooLong")));
    NameAnchor=NameCaret;bConfirmCredits=false;bDirty=true;
}
bool UACEUICharGenBinder::KeyDown(const FKeyEvent& E)
{
    // Motion-controller buttons belong to the VR input bindings, even if the
    // canvas has keyboard focus. Only text/navigation keys belong here.
    if(E.GetKey().IsGamepadKey())return false;
    if(Client&&Client->GetSession()&&Client->GetSession()->IsCharacterCreationPending())return true;
    if(DialogAction){if(E.GetKey()==EKeys::Escape)CloseDialog(false);else if(E.GetKey()==EKeys::Enter)CloseDialog(true);return true;}
    if(E.GetKey()==EKeys::Escape){ShowDialog(2,Model.Text(TEXT("ID_CharGen_ExitWarning")));return true;}
    if(E.GetKey()==EKeys::Enter){if(Page==6)Submit();else SetPage(Page+1);return true;}
    if(Page!=6)return true;
    if(E.GetKey()==EKeys::Tab){bNameFocus=true;return true;}if(!bNameFocus)return true;
    if(E.IsControlDown()&&E.GetKey()==EKeys::A){NameAnchor=0;NameCaret=Model.Selection.Name.Len();return true;}
    if(E.IsControlDown()&&(E.GetKey()==EKeys::C||E.GetKey()==EKeys::X)){FPlatformApplicationMisc::ClipboardCopy(*Model.Selection.Name.Mid(FMath::Min(NameCaret,NameAnchor),FMath::Abs(NameCaret-NameAnchor)));if(E.GetKey()==EKeys::X)InsertName(TEXT(""));return true;}
    if(E.IsControlDown()&&E.GetKey()==EKeys::V){FString S;FPlatformApplicationMisc::ClipboardPaste(S);InsertName(S);return true;}
    if(E.GetKey()==EKeys::BackSpace||E.GetKey()==EKeys::Delete){if(NameCaret==NameAnchor){if(E.GetKey()==EKeys::BackSpace)NameAnchor=FMath::Max(0,NameCaret-1);else NameAnchor=FMath::Min(Model.Selection.Name.Len(),NameCaret+1);}InsertName(TEXT(""));return true;}
    int Pos=NameCaret;if(E.GetKey()==EKeys::Home)Pos=0;else if(E.GetKey()==EKeys::End)Pos=Model.Selection.Name.Len();else if(E.GetKey()==EKeys::Left)Pos=FMath::Max(0,NameCaret-1);else if(E.GetKey()==EKeys::Right)Pos=FMath::Min(Model.Selection.Name.Len(),NameCaret+1);else return true;
    NameCaret=Pos;if(!E.IsShiftDown())NameAnchor=Pos;return true;
}
bool UACEUICharGenBinder::KeyChar(const FCharacterEvent& E)
{
    if(!DialogAction&&Page==6&&bNameFocus&&!(Client&&Client->GetSession()&&Client->GetSession()->IsCharacterCreationPending())&&!E.IsControlDown()&&FACECharacterCreation::IsNameCharacter(E.GetCharacter()))InsertName(FString::Chr(E.GetCharacter()));return true;
}
void UACEUICharGenBinder::RefreshPreview(float Delta)
{
    bool ShowPreview=Page==4||Page==6;if(PreviewImage)PreviewImage->SetVisibility(ShowPreview?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    if(Preview)if(auto* App=Preview->FindComponentByClass<UACECharacterAppearanceComponent>())App->SetComponentTickEnabled(ShowPreview&&(Page==6||bClothes));
    if(!ShowPreview||!Controller)return;
    UWorld* World=Controller->GetWorld();if(!World)return;
    if(!Preview)
    {
        FActorSpawnParameters Params;Params.ObjectFlags|=RF_Transient;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        Preview=World->SpawnActor<AActor>(AActor::StaticClass(),FVector(-60000,-60000,-60000),FRotator::ZeroRotator,Params);if(!Preview)return;
        auto Root=NewObject<USceneComponent>(Preview);Preview->SetRootComponent(Root);Root->RegisterComponent();
        // A bare AActor has no root during SpawnActor, so its spawn transform is
        // discarded. Place the examination stage after installing the root.
        Preview->SetActorLocation(FVector(-60000,-60000,-60000));
        auto App=NewObject<UACECharacterAppearanceComponent>(Preview);App->RegisterComponent();
        // Examination materials reproduce CreatureMode's fixed vertex lighting.
        // World sun, fog and adaptive exposure do not belong in this viewport.
        // Capture scene color and composite its inverse-opacity alpha explicitly,
        // as in the inventory paperdoll. FinalColorLDR does not provide usable
        // opacity on mobile LDR or when Slate draws into the VR panel texture.
        Target=NewObject<UTextureRenderTarget2D>(this);
        Target->ClearColor=FLinearColor(0,0,0,1);
        Target->InitCustomFormat(490,742,PF_FloatRGBA,true);
        Target->UpdateResourceImmediate(true);
        Capture=NewObject<USceneCaptureComponent2D>(Preview);Capture->SetupAttachment(Root);Capture->RegisterComponent();Capture->TextureTarget=Target;Capture->CaptureSource=ESceneCaptureSource::SCS_SceneColorHDR;Capture->bCaptureEveryFrame=false;Capture->bCaptureOnMovement=false;Capture->ShowFlags.SetAtmosphere(false);Capture->ShowFlags.SetFog(false);Capture->PrimitiveRenderMode=ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
        Capture->ShowFlags.SetMotionBlur(false);
        Capture->ShowFlags.SetAntiAliasing(false);
        Capture->ShowFlags.SetEyeAdaptation(false);
        Capture->ShowFlags.SetBloom(false);
        Capture->PostProcessSettings.bOverride_AutoExposureMethod=true;Capture->PostProcessSettings.AutoExposureMethod=AEM_Manual;
        Capture->bAlwaysPersistRenderingState=true;
        Capture->PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure=true;Capture->PostProcessSettings.AutoExposureApplyPhysicalCameraExposure=false;
        Capture->PostProcessSettings.bOverride_ToneCurveAmount=true;Capture->PostProcessSettings.ToneCurveAmount=0.f;
        Capture->PostProcessSettings.bOverride_BlueCorrection=true;Capture->PostProcessSettings.BlueCorrection=0.f;
        Capture->PostProcessSettings.bOverride_ExpandGamut=true;Capture->PostProcessSettings.ExpandGamut=0.f;
        PreviewImage=Canvas->WidgetTree->ConstructWidget<UACECaptureImage>();Canvas->GetElementLayer()->AddChild(PreviewImage);PreviewImage->SetBrushResourceObject(Target);bPreviewDirty=true;
    }
    auto App=Preview->FindComponentByClass<UACECharacterAppearanceComponent>();
    if(bPreviewDirty&&App)
    {
        const bool Animate=Page==6||bClothes;
        FACEWorldObject Object;
        if(Model.BuildAppearance(Object,Page==4&&!bClothes))
        {
            const uint32 Animation=Model.MappedAsset(0x25000010,Model.Selection.Heritage==12?0x10000011:Model.Selection.Heritage==13?0x10000013:Animate?0x10000006:0x10000005);
            // Retain the generated mesh and animation phase when the selected swatch/shade
            // has not changed. Mouse motion and page text refreshes are not mesh changes.
            const bool Changed=App->GetAppliedAppearanceHash()!=Object.Appearance.GetContentHash()||App->GetSetupId()!=Object.SetupId;
            App->ApplyWorldObject(Object,100,false);
            if(Changed||PreviewAnimationId!=Animation||bPreviewWasAnimated!=Animate)
                App->SetPreviewAnimation(Animation,Animate);
            PreviewAnimationId=Animation;bPreviewWasAnimated=Animate;
        }
        if(PreviewEnvironmentId!=Model.Heritage()->Environment)
        {
            if(PreviewEnvironment)PreviewEnvironment->Destroy();PreviewEnvironment=nullptr;
            PreviewEnvironmentId=Model.Heritage()->Environment;
            if(PreviewEnvironmentId){FActorSpawnParameters Spawn;Spawn.ObjectFlags|=RF_Transient;PreviewEnvironment=World->SpawnActor<AActor>(AActor::StaticClass(),Preview->GetActorLocation(),FRotator::ZeroRotator,Spawn);if(PreviewEnvironment){auto Root=NewObject<USceneComponent>(PreviewEnvironment);PreviewEnvironment->SetRootComponent(Root);Root->RegisterComponent();PreviewEnvironment->SetActorLocation(Preview->GetActorLocation());auto Environment=NewObject<UACECharacterAppearanceComponent>(PreviewEnvironment);Environment->RegisterComponent();FACEWorldObject Backdrop;Backdrop.SetupId=PreviewEnvironmentId;Environment->ApplyWorldObject(Backdrop,100,false);}}
        }
        Capture->ClearShowOnlyComponents();TArray<UPrimitiveComponent*> Prims;Preview->GetComponents(Prims);if(PreviewEnvironment){TArray<UPrimitiveComponent*> Background;PreviewEnvironment->GetComponents(Background);Prims.Append(Background);}
        auto* Dat=Client->GetUIResourceResolver()->GetDatSubsystem();
        for(auto Prim:Prims)
        {
            Prim->SetCollisionEnabled(ECollisionEnabled::NoCollision);Prim->SetLightingChannels(false,false,true);Prim->SetVisibleInSceneCaptureOnly(true);
            for(int I=0;I<Prim->GetNumMaterials();++I)Prim->SetMaterial(I,Dat->CreateExaminationMaterial(Prim->GetMaterial(I),Preview));
            Capture->ShowOnlyComponent(Prim);
        }
        bPreviewDirty=false;
    }
    auto El=Find(Page==4?TEXT("3DViewport"):TEXT("Summary3DViewport"));if(!El)return;
    Canvas->PlaceWidgetAtElement(PreviewImage,El,900);PreviewImage->SetVisibility(ESlateVisibility::HitTestInvisible);
    PreviewYaw+=RotateDirection*120.f*Delta;
    if(App&&App->GetMeshRoot())App->GetMeshRoot()->SetRelativeRotation(FRotator(0,PreviewYaw,0));
    const uint32 Heritage=Model.Selection.Heritage;
    FVector Close(0,-55,165),Far(0,-250,95);
    if(Heritage==7)Close=FVector(0,-85,165);
    if(Heritage==12){Close=FVector(0,-185,185);Far=FVector(0,-380,115);}
    if(Heritage==13){Close=FVector(0,-305,275);Far=FVector(0,-570,165);}
    const FVector Desired=FMath::Lerp(Close,Far,Page==6?1.f:Zoom);
    // gmCGAppearancePage::DoZoomAnimation linearly traverses the selected camera
    // endpoints in 0.6 seconds, independent of heritage size or current zoom.
    if(!Desired.Equals(PreviewCameraTarget)){PreviewCameraStart=PreviewCamera;PreviewCameraTarget=Desired;PreviewCameraTime=0;}
    PreviewCameraTime=FMath::Min(.6f,PreviewCameraTime+Delta);
    PreviewCamera=FMath::Lerp(PreviewCameraStart,PreviewCameraTarget,PreviewCameraTime/.6f);
    Capture->SetRelativeLocation(PreviewCamera/Preview->GetActorScale3D());Capture->SetRelativeRotation(FRotator(0,90,0));Capture->FOVAngle=45.f;
    PreviewClock+=Delta;if(PreviewClock>=1.f/60){PreviewClock=0;Capture->CaptureScene();}
}

void UACEUICharGenBinder::ScrollText(TSharedPtr<FACEUIElement> E,const FString& Text)
{
    if(!E)return;FACEDatFont Font;FString Shown=Text;
    if(Client->GetUIResourceResolver()->ResolveFont(E->FontId,Font))
    {
        auto Lines=ACEDatText::Layout(Font,Text,FMath::Max(1,int(E->Width-E->TextMargins.Left-E->TextMargins.Right)),false);
        int VisibleLines=FMath::Max(1,int((E->Height-E->TextMargins.Top-E->TextMargins.Bottom)/FMath::Max(1u,Font.MaxCharHeight)));
        DescriptionMaxScroll=FMath::Max(0,Lines.Num()-VisibleLines);DescriptionScroll=FMath::Clamp(DescriptionScroll,0,DescriptionMaxScroll);
        if(Lines.IsValidIndex(DescriptionScroll))Shown=Text.Mid(Lines[DescriptionScroll].Begin);
        if(auto Bar=Child(E,0x100002e7))if(auto Thumb=Child(Bar,1))Thumb->Y=17+FMath::RoundToInt(float(DescriptionScroll)*(Bar->Height-34-Thumb->Height)/FMath::Max(1,DescriptionMaxScroll));
    }
    Label(E,Shown);
}
void UACEUICharGenBinder::RefreshColors()
{
    auto Resolver=Client->GetUIResourceResolver()->GetDatSubsystem()->GetTextureResolver();if(!Resolver)return;
    const TArray<FColor> Samples=Model.ColorSamples(ColorField);
    for(int I=0;I<9;++I)
    {
        auto E=Find(*FString::Printf(TEXT("ColorSpot%d"),I+1));if(!E)continue;E->bVisible=true;E->bActivatable=I<Samples.Num();
        const bool Filled=Samples.IsValidIndex(I);FColor Color=Filled?Samples[I]:FColor::Black;
        uint32 Did=Model.MappedAsset(0x25000010,Filled?0x1000000d:0x1000000f);uint64 Key=(uint64(Did)<<32)|Color.ToPackedARGB();
        auto& Texture=ColorTextures.FindOrAdd(Key);if(!Texture){FACEDatTexture Data;FACEDatDecodedSurface Surface;if(Resolver->LoadTextureForUi(Did,Data)&&Resolver->DecodeTextureForUi(Data,Surface)){for(auto& P:Surface.Pixels)if(Filled&&P.R==0&&P.G==0&&P.B==0){uint8 A=P.A;P=Color;P.A=A;}Texture=FACEDatTextureResolver::CreateTransientRgbaUi(Surface.Width,Surface.Height,Surface.Pixels);}}
        auto& Image=ColorImages.FindOrAdd(E->InstanceId);if(!Image){Image=Canvas->WidgetTree->ConstructWidget<UImage>();Canvas->GetElementLayer()->AddChild(Image);}Image->SetBrushResourceObject(Texture);Image->SetVisibility(ESlateVisibility::HitTestInvisible);Canvas->PlaceWidgetAtElement(Image,E,900);
    }
}
bool UACEUICharGenBinder::CanEditName() const
{
    return Canvas && Page==6 && !DialogAction && !bReturn
        && !(Client && Client->GetSession() && Client->GetSession()->IsCharacterCreationPending());
}
bool UACEUICharGenBinder::IsNameEntryAt(FVector2D Position) const
{
    return CanEditName() && Contains(Find(TEXT("NameTextBox")),Position);
}
void UACEUICharGenBinder::FocusNameEntry()
{
    if(!CanEditName())return;
    MouseUp(); bNameFocus=true;
}
void UACEUICharGenBinder::SetNameFromKeyboard(const FString& Text)
{
    if(!CanEditName())return;
    FString Filtered;
    for(TCHAR C:Text)
        if(FACECharacterCreation::IsNameCharacter(C) && Filtered.Len()<32)Filtered.AppendChar(C);
    Model.Selection.Name=MoveTemp(Filtered);
    NameCaret=NameAnchor=Model.Selection.Name.Len();
    bConfirmCredits=false;bDirty=true;
}
void UACEUICharGenBinder::CommitNameFromKeyboard()
{
    if(CanEditName())Submit();
}

void UACEUICharGenBinder::RefreshTooltip(float Delta)
{
    if(Tooltip)Tooltip->bVisible=false;
    if(DialogAction||DragAttribute>=0||DragScrollbar||bGradientDrag||bPreviewDrag||RotateDirection||
        (Client->GetSession()&&Client->GetSession()->IsCharacterCreationPending()))return;
    auto Hit=Manager->HitTestCanvas(LastMouse.X,LastMouse.Y);
    while(Hit&&!Hit->TooltipEntryId)Hit=Hit->Parent.Pin();
    const uint32 Source=Hit?Hit->InstanceId:0;
    if(Source!=TooltipSource){TooltipSource=Source;TooltipClock=0;}
    TooltipClock+=Delta;
    // UIElementManager defaults: quarter-second hover delay, ten-second lifetime.
    if(!Hit||TooltipClock<.25f||TooltipClock>=10.25f)return;
    const FString Text=Model.Strings.FindRef(Hit->TooltipEntryId);
    if(Text.IsEmpty())return;
    if(!Tooltip)
    {
        Tooltip=UACEUILayoutResolver::LoadTemplate(0x21000041,0x10000487);
        if(!Tooltip)return;
        Tooltip->SetElementName(TEXT("RootCharGenTooltip"));Tooltip->ZLevel=2000;
        Tooltip->bActivatable=false;
        for(auto C:Tooltip->Children)C->bActivatable=false;
        Find(TEXT("RootCharGenMaster"))->AddChild(Tooltip);
    }
    auto Body=Child(Tooltip,0x10000396);FACEDatFont Font;
    if(!Body||!Client->GetUIResourceResolver()->ResolveFont(Body->FontId,Font))return;
    auto Lines=ACEDatText::Layout(Font,Text,252,false);
    int Width=1;for(const auto& Line:Lines)Width=FMath::Max(Width,Line.Width);
    Tooltip->Width=FMath::Min(260,Width+8);Tooltip->Height=FMath::Min(516,int(Lines.Num()*Font.MaxCharHeight)+8);
    Body->Width=Tooltip->Width-4;Body->Height=Tooltip->Height-4;
    if(auto Top=Child(Tooltip,0x100002b5))Top->Width=Tooltip->Width;
    if(auto Left=Child(Tooltip,0x10000209))Left->Height=Tooltip->Height;
    if(auto Right=Child(Tooltip,0x1000020a)){Right->X=Tooltip->Width-1;Right->Height=Tooltip->Height;}
    if(auto Bottom=Child(Tooltip,0x1000020b)){Bottom->Y=Tooltip->Height-1;Bottom->Width=Tooltip->Width;}
    const auto Pos=Relative(LastMouse);
    Tooltip->X=FMath::Clamp(FMath::RoundToInt(Pos.X+32),0,800-Tooltip->Width);
    Tooltip->Y=FMath::Clamp(FMath::RoundToInt(Pos.Y+32),0,600-Tooltip->Height);
    Tooltip->bVisible=true;Label(Body,Text);
}

void UACEUICharGenBinder::ShowDialog(int32 Action,const FString& Text)
{
    if(!Dialog){Dialog=UACEUILayoutResolver::LoadTemplate(0x2100003c,0x15);if(!Dialog)return;Dialog->SetElementName(TEXT("RootCharGenDialog"));Dialog->ZLevel=1000;Find(TEXT("RootCharGenMaster"))->AddChild(Dialog);}
    DialogAction=Action;DialogText=Text;Dialog->bVisible=true;
    auto Box=Child(Dialog,0x3d),Body=Child(Dialog,0x3e);if(!Box||!Body)return;
    FACEDatFont Font;int H=160;if(Client->GetUIResourceResolver()->ResolveFont(Body->FontId,Font))H=FMath::Clamp(int(ACEDatText::Layout(Font,Text,370,false).Num()*Font.MaxCharHeight+78),95,480);
    int Delta=H-Box->Height;Box->Height=H;Box->X=200;Box->Y=(600-H)/2;
    for(auto C:Box->Children){if(C->Y>=Body->Y+Body->Height)C->Y+=Delta;else if(C->Height>40)C->Height+=Delta;}
    Body->Height=H-77;Child(Dialog,0x19)->bVisible=Action!=4;auto Yes=Child(Dialog,0x17);Yes->X=Action==4?160:80;
    bDirty=true;
}
void UACEUICharGenBinder::CloseDialog(bool Accept)
{
    int Action=DialogAction;DialogAction=0;if(Dialog)Dialog->bVisible=false;
    if(Accept){if(Action==1){bConfirmCredits=true;Submit();}else if(Action==2)bReturn=true;else if(Action==3)RandomizePage();}bDirty=true;
}
void UACEUICharGenBinder::RandomizePage()
{
    if(Page==1)Model.SelectHeritage(FMath::RandRange(1,4));
    if(Page==2)Model.RandomizeProfession();
    if(Page==3)Model.RandomizeSkills();
    if(Page==4){const auto Before=Model.Selection;Model.RandomizeAppearance();if(bClothes){auto Styles=Model.Selection;Model.Selection=Before;for(int I=0;I<4;++I){Model.Selection.GearStyle[I]=Styles.GearStyle[I];Model.Selection.GearColor[I]=Styles.GearColor[I];Model.Selection.GearHue[I]=Styles.GearHue[I];}}else for(int I=0;I<4;++I){Model.Selection.GearStyle[I]=Before.GearStyle[I];Model.Selection.GearColor[I]=Before.GearColor[I];Model.Selection.GearHue[I]=Before.GearHue[I];}}
    if(Page==6){Model.RandomizeCharacter();NameCaret=NameAnchor=NameDisplayStart=0;}
    if(Page==5){auto A=Model.Heritage()->PrimaryStarts;A.Append(Model.Heritage()->SecondaryStarts);if(!A.IsEmpty())Model.Selection.StartArea=A[FMath::RandRange(0,A.Num()-1)];}
    bDirty=bPreviewDirty=true;
}
