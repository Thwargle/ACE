#include "UI/ACEUIGameplayBinder.h"
#include "UI/ACEUICanvasWidget.h"
#include "UI/ACEUIElementManager.h"
#include "UI/ACEUIElement.h"
#include "UI/ACERetailTextBlock.h"
#include "UI/ACERetailKeySelector.h"
#include "ACEInputBindings.h"
#include "Blueprint/WidgetTree.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Styling/CoreStyle.h"
void UACEUIGameplayBinder::RefreshKeyboardOverlays()
{
 if(!Manager||!Canvas||!Canvas->WidgetTree)return;
 const auto Root=Manager->FindElementByName(TEXT("RootGameplay_Keyboard_Field"));
 if(!Root||!Root->bVisible)
 {
  for(UWidget* R:KeyboardRows)if(R)R->SetVisibility(ESlateVisibility::Collapsed);
  for(UTextBlock* L:KeyboardLabels)if(L)L->SetVisibility(ESlateVisibility::Collapsed);
  if(ACEInputBindings::IsEditing())ACEInputBindings::Cancel();return;
 }
 if(!ACEInputBindings::IsEditing())ACEInputBindings::BeginEdit();
 static const TCHAR* Pages[]={TEXT("Movement"),TEXT("Camera"),TEXT("Combat"),TEXT("UI"),TEXT("CharacterSettings"),TEXT("Emotes")};
 for(const TCHAR* P:Pages)
 {
  if(auto Page=Manager->FindElementUnder(TEXT("KeyboardMappingPages"),FString(P)+TEXT("Page")))Page->bVisible=ActiveKeyboardPage==P;
  if(auto Tab=Manager->FindElementUnder(TEXT("KeyboardMappingPages"),FString(P)+TEXT("Tab"))){Tab->DefaultState=ActiveKeyboardPage==P?12:11;Tab->bUseExplicitState=true;}
 }
 int32 LabelIndex=0;
 auto Caption=[&](const TSharedPtr<FACEUIElement>& El,const FString& Text)
 {
  if(KeyboardLabels.Num()<=LabelIndex)KeyboardLabels.Add(Canvas->WidgetTree->ConstructWidget<UTextBlock>(UACERetailTextBlock::StaticClass()));
  UTextBlock* L=KeyboardLabels[LabelIndex++];if(!El){L->SetVisibility(ESlateVisibility::Collapsed);return;}
  L->SetText(FText::FromString(Text));L->SetJustification(ETextJustify::Center);L->SetVisibility(ESlateVisibility::HitTestInvisible);
  Canvas->PlaceWidgetAtElement(L,El,100100+LabelIndex,FMargin(2,4,2,0));
 };
 for(const TCHAR* P:Pages)Caption(Manager->FindElementUnder(TEXT("KeyboardMappingPages"),FString(P)+TEXT("Tab")),FString(P)==TEXT("CharacterSettings")?TEXT("Character"):P);
 const TCHAR* Names[]={TEXT("KeyboardLoadKeymapButton"),TEXT("KeyboardSaveKeymapAsButton"),TEXT("KeyboardDefaultsButton"),TEXT("KeyboardRevertButton"),TEXT("KeyboardOKButton"),TEXT("KeyboardCancelButton"),TEXT("KeyboardCurrentKeymapLabel")};
 const TCHAR* Labels[]={TEXT("Load saved"),TEXT("Save"),TEXT("Defaults"),TEXT("Revert"),TEXT("OK"),TEXT("Cancel"),TEXT("Custom keymap")};
 for(int32 I=0;I<7;++I)Caption(Manager->FindElementUnder(TEXT("KeyboardFrame"),Names[I]),Labels[I]);
 const FString PageName=ActiveKeyboardPage+TEXT("Page");
 const auto List=Manager->FindElementUnder(PageName,TEXT("KeyboardMappingListBox"));
 Caption(Manager->FindElementUnder(PageName,TEXT("KeyboardMappingNameColumnLabel")),TEXT("Action"));
 for(int32 I=1;I<=3;++I)Caption(Manager->FindElementUnder(PageName,FString::Printf(TEXT("KeyboardMapping%dColumnLabel"),I)),FString::Printf(TEXT("Key %d"),I));
 const auto& Actions=ACEInputBindings::Actions();
 if(KeyboardRows.IsEmpty())for(const auto& A:Actions)
 {
  auto* Row=Canvas->WidgetTree->ConstructWidget<UHorizontalBox>();
  auto* Label=Canvas->WidgetTree->ConstructWidget<UACERetailTextBlock>();Label->SetText(FText::FromString(A.Label));
  Label->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),9));
  Label->SetRetailElement(Canvas->GetResourceResolver(),List,FVector2D(1,1),135,false);
  Row->AddChildToHorizontalBox(Label)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
  for(int32 S=0;S<3;++S){auto* Key=Canvas->WidgetTree->ConstructWidget<UACERetailKeySelector>();Key->InitializeBinding(A.Key,S);Row->AddChildToHorizontalBox(Key)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));}
  KeyboardRows.Add(Row);
 }
 int32 Y=0;
 for(int32 I=0;I<KeyboardRows.Num();++I)
 {
  auto* Row=Cast<UHorizontalBox>(KeyboardRows[I]);
  if(!List||ActiveKeyboardPage!=Actions[I].Page){Row->SetVisibility(ESlateVisibility::Collapsed);continue;}
  Row->SetVisibility(ESlateVisibility::Visible);
  for(int32 S=1;S<4;++S)CastChecked<UACERetailKeySelector>(Row->GetChildAt(S))->RefreshBinding();
  Canvas->PlaceWidgetAtElement(Row,List,100200+I,FMargin(4,Y,4,FMath::Max(0,List->Height-Y-28)));Y+=32;
 }
}
bool UACEUIGameplayBinder::HandleKeyboardNamedClick(const FString& Name)
{
 if(!Manager)return false;
 const auto Root=Manager->FindElementByName(TEXT("RootGameplay_Keyboard_Field"));
 if(!Root||!Root->bVisible)return false;
 for(const TCHAR* P:{TEXT("Movement"),TEXT("Camera"),TEXT("Combat"),TEXT("UI"),TEXT("CharacterSettings"),TEXT("Emotes")})
  if(Name==FString(P)+TEXT("Tab")){ActiveKeyboardPage=P;RefreshKeyboardOverlays();return true;}
 if(Name==TEXT("KeyboardDefaultsButton"))ACEInputBindings::Defaults();
 else if(Name==TEXT("KeyboardRevertButton")||Name==TEXT("KeyboardLoadKeymapButton"))ACEInputBindings::Revert();
 else if(Name==TEXT("KeyboardSaveKeymapAsButton")){ACEInputBindings::Commit();ACEInputBindings::BeginEdit();}
 else if(Name==TEXT("KeyboardOKButton")||Name==TEXT("KeyboardCancelButton"))
 {
  if(Name==TEXT("KeyboardOKButton"))ACEInputBindings::Commit();else ACEInputBindings::Cancel();
  Root->bVisible=false;
 }
 else return false;
 RefreshKeyboardOverlays();return true;
}
