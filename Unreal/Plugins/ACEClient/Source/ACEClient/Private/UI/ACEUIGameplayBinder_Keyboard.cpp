#include "UI/ACEUIGameplayBinder.h"
#include "UI/ACEUICanvasWidget.h"
#include "UI/ACEUIElementManager.h"
#include "UI/ACEUIElement.h"
#include "UI/ACEUILayoutResolver.h"
#include "UI/ACERetailTextBlock.h"
#include "UI/ACERetailKeySelector.h"
#include "UI/ACERetailComboBoxString.h"
#include "UI/ACEUIResourceResolver.h"
#include "Engine/Texture2D.h"
#include "ACEInputBindings.h"
#include "ACEDatSubsystem.h"
#include "Engine/GameInstance.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "ACEClientSubsystem.h"
#include "ACEPlayerController.h"
#include "ACEOpcodes.h"
#include "Blueprint/WidgetTree.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/EditableTextBox.h"
#include "Styling/CoreStyle.h"
void UACEUIGameplayBinder::RefreshKeyboardOverlays()
{
 if(!Manager||!Canvas||!Canvas->WidgetTree)return;
 const auto Root=Manager->FindElementByName(TEXT("RootGameplay_Keyboard_Field"));
 const bool Open=Root && Root->bVisible;
 auto HideRows=[&]()
 {
  for(UWidget* R:KeyboardRows)if(R)R->SetVisibility(ESlateVisibility::Collapsed);
  for(UTextBlock* L:KeyboardRowLabels)if(L)L->SetVisibility(ESlateVisibility::Collapsed);
  for(UTextBlock* L:KeyboardKeyLabels)if(L)L->SetVisibility(ESlateVisibility::Collapsed);
  for(auto E:KeyboardEntryElements)if(E)E->bVisible=false;
  if(KeyboardGroupElement)KeyboardGroupElement->bVisible=false;
  if(KeyboardGroupLabel)KeyboardGroupLabel->SetVisibility(ESlateVisibility::Collapsed);
 };
 if(!Open)
 {
  HideRows();
  bKeymapImportOpen=false;RefreshKeymapDialog();
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
  if(KeyboardLabels.Num()<=LabelIndex)KeyboardLabels.Add(Canvas->WidgetTree->ConstructWidget<UACERetailTextBlock>());
  UTextBlock* L=KeyboardLabels[LabelIndex++];if(!El){L->SetVisibility(ESlateVisibility::Collapsed);return;}
  L->SetText(FText::FromString(Text));L->SetJustification(ETextJustify::Center);L->SetVisibility(ESlateVisibility::HitTestInvisible);
  Canvas->PlaceWidgetAtElement(L,El,100100+LabelIndex);
 };
 for(const TCHAR* P:Pages)Caption(Manager->FindElementUnder(TEXT("KeyboardMappingPages"),FString(P)+TEXT("Tab")),P);
 const TCHAR* Names[]={TEXT("KeyboardLoadKeymapButton"),TEXT("KeyboardSaveKeymapAsButton"),TEXT("KeyboardDefaultsButton"),TEXT("KeyboardRevertButton"),TEXT("KeyboardOKButton"),TEXT("KeyboardCancelButton"),TEXT("KeyboardCurrentKeymapLabel")};
 const FString Labels[]={TEXT("Load File..."),TEXT("Save As..."),TEXT("Defaults"),TEXT("Revert"),TEXT("OK"),TEXT("Cancel"),KeyboardFileName};
 for(int32 I=0;I<7;++I)Caption(Manager->FindElementUnder(TEXT("KeyboardFrame"),Names[I]),Labels[I]);
 const FString PageName=ActiveKeyboardPage+TEXT("Page");
 const auto List=Manager->FindElementUnder(PageName,TEXT("KeyboardMappingListBox"));
 Caption(Manager->FindElementUnder(PageName,TEXT("KeyboardMappingNameColumnLabel")),TEXT("Command"));
 for(int32 I=1;I<=3;++I)Caption(Manager->FindElementUnder(PageName,FString::Printf(TEXT("KeyboardMapping%dColumnLabel"),I)),FString::Printf(TEXT("Mapping %d"),I));
 RefreshKeymapDialog();
 if(!List)return;
 const auto& Actions=ACEInputBindings::Actions();
 if(KeyboardEntryElements.IsEmpty())for(const auto& A:Actions)
 {
  // gmKeyboardUI::AddActionKeyMap uses this exact DAT row, including three
  // separate strip buttons. Keep their fonts, chrome and column positions.
  KeyboardEntryElements.Add(UACEUILayoutResolver::LoadTemplate(0x21000009,0x1000002f));
  KeyboardRowLabels.Add(Canvas->WidgetTree->ConstructWidget<UACERetailTextBlock>());
  for(int32 S=0;S<3;++S)
  {
   auto* Key=Canvas->WidgetTree->ConstructWidget<UACERetailKeySelector>();Key->InitializeBinding(A.Key,S);KeyboardRows.Add(Key);
   KeyboardKeyLabels.Add(Canvas->WidgetTree->ConstructWidget<UACERetailTextBlock>());
  }
 }
 if(!KeyboardGroupElement)
 {
  KeyboardGroupElement=UACEUILayoutResolver::LoadTemplate(0x21000009,0x1000002e);
  KeyboardGroupLabel=Canvas->WidgetTree->ConstructWidget<UACERetailTextBlock>();
 }
 TArray<int32> PageActions;for(int32 I=0;I<Actions.Num();++I)if(ActiveKeyboardPage==Actions[I].Page)PageActions.Add(I);
 constexpr int32 RowH=40;
 KeyboardVisibleRows=FMath::Max(1,List->Height/RowH);
 KeyboardMaxOffset=FMath::Max(0,PageActions.Num()+1-KeyboardVisibleRows);
 KeyboardScrollOffset=FMath::Clamp(KeyboardScrollOffset,0,KeyboardMaxOffset);
 if(KeyboardGroupElement)KeyboardGroupElement->bVisible=KeyboardScrollOffset==0;
 if(KeyboardGroupLabel && KeyboardScrollOffset!=0)KeyboardGroupLabel->SetVisibility(ESlateVisibility::Collapsed);
 if(KeyboardGroupElement && KeyboardScrollOffset==0)
 {
  if(KeyboardGroupElement->Parent.Pin()!=List)List->AddChild(KeyboardGroupElement);
  KeyboardGroupElement->Y=0;KeyboardGroupElement->bVisible=true;
  KeyboardGroupLabel->SetText(FText::FromString(ActiveKeyboardPage));KeyboardGroupLabel->SetVisibility(ESlateVisibility::HitTestInvisible);
  KeyboardGroupLabel->SetColorAndOpacity(FLinearColor(1.f,.95f,.5f));
  Canvas->PlaceWidgetAtElement(KeyboardGroupLabel,KeyboardGroupElement,100210);
 }
 for(int32 I=0;I<Actions.Num();++I)
 {
  const int32 N=PageActions.Find(I),Line=N+1-KeyboardScrollOffset;
  const auto Entry=KeyboardEntryElements[I];if(!Entry)continue;
  const bool Visible=N!=INDEX_NONE && Line>=0 && Line<KeyboardVisibleRows;
  Entry->bVisible=Visible;
  if(!Visible)
  {
   KeyboardRowLabels[I]->SetVisibility(ESlateVisibility::Collapsed);
   for(int32 S=0;S<3;++S){KeyboardRows[I*3+S]->SetVisibility(ESlateVisibility::Collapsed);KeyboardKeyLabels[I*3+S]->SetVisibility(ESlateVisibility::Collapsed);}
   continue;
  }
  if(Entry->Parent.Pin()!=List)List->AddChild(Entry);
  Entry->Y=Line*RowH;Entry->bVisible=true;
  UTextBlock* Label=KeyboardRowLabels[I];Label->SetText(FText::FromString(Actions[I].Label));Label->SetVisibility(ESlateVisibility::HitTestInvisible);
  Canvas->PlaceWidgetAtElement(Label,Entry,100210,FMargin(0,0,300,0));
  for(int32 S=0;S<3;++S)
  {
   const auto Button=Entry->Children[S];
   auto* Key=CastChecked<UACERetailKeySelector>(KeyboardRows[I*3+S]);Key->SetVisibility(ESlateVisibility::Visible);
   Key->SetIsEnabled(!bKeymapImportOpen);
   Key->SetRetailButton(Canvas,Button,KeyboardKeyLabels[I*3+S]);
  }
 }
 SyncDatScrollbar(Manager->FindElementUnder(PageName,TEXT("KeyboardMappingScrollbar")),KeyboardMaxOffset?float(KeyboardScrollOffset)/KeyboardMaxOffset:0,float(KeyboardVisibleRows)/(PageActions.Num()+1));
}
bool UACEUIGameplayBinder::ScrollKeyboard(float WheelDelta,FVector2D CanvasLocalPos)
{
 if(!Manager||!Canvas||bKeymapImportOpen)return false;
 const auto Root=Manager->FindElementByName(TEXT("RootGameplay_Keyboard_Field"));if(!Root||!Root->bVisible)return false;
 const auto List=Manager->FindElementUnder(ActiveKeyboardPage+TEXT("Page"),TEXT("KeyboardMappingListBox"));
 if(!List||!Canvas->IsElementExposedAt(List,CanvasLocalPos))return false;
 const FVector2D P=Canvas->ViewportToLayout(CanvasLocalPos);const FIntPoint O=List->GetScreenOrigin();
 if(P.X<O.X||P.Y<O.Y||P.X>=O.X+List->Width||P.Y>=O.Y+List->Height)return false;
 KeyboardScrollOffset+=WheelDelta>0?-1:1;RefreshKeyboardOverlays();return true;
}
bool UACEUIGameplayBinder::HandleKeyboardNamedClick(const FString& Name)
{
 if(!Manager)return false;
 const auto Root=Manager->FindElementByName(TEXT("RootGameplay_Keyboard_Field"));
 if(!Root||!Root->bVisible)return false;
 if(Name==TEXT("KeymapFileCancel")){bKeymapImportOpen=false;RefreshKeyboardOverlays();return true;}
 if(Name==TEXT("KeymapFileOK")){HandleKeymapImport(FString());return true;}
 if(bKeymapImportOpen)return true;
 for(const TCHAR* P:{TEXT("Movement"),TEXT("Camera"),TEXT("Combat"),TEXT("UI"),TEXT("CharacterSettings"),TEXT("Emotes")})
  if(Name==FString(P)+TEXT("Tab")){ActiveKeyboardPage=P;KeyboardScrollOffset=0;RefreshKeyboardOverlays();return true;}
 if(Name==TEXT("KeyboardLoadKeymapButton")){bKeymapSave=false;ShowKeymapImport();return true;}
 if(Name==TEXT("KeyboardDefaultsButton"))ACEInputBindings::Defaults();
 else if(Name==TEXT("KeyboardRevertButton"))ACEInputBindings::Revert();
 else if(Name==TEXT("KeyboardSaveKeymapAsButton")){bKeymapSave=true;ShowKeymapImport();return true;}
 else if(Name==TEXT("KeyboardOKButton")||Name==TEXT("KeyboardCancelButton"))
 {
  if(Name==TEXT("KeyboardOKButton"))ACEInputBindings::Commit();else ACEInputBindings::Cancel();
  Root->bVisible=false;
 }
 else return false;
 bKeymapImportOpen=false;RefreshKeyboardOverlays();return true;
}

namespace
{
TSharedPtr<FACEUIElement> KeymapChild(TSharedPtr<FACEUIElement> Root,uint32 Id)
{
 if(!Root)return nullptr;if(Root->ElementId==Id)return Root;
 for(const auto& C:Root->Children)if(auto Found=KeymapChild(C,Id))return Found;return nullptr;
}
}
UWidget* UACEUIGameplayBinder::GenerateKeymapFileLabel(FString Item)
{
 auto* Label=Canvas->WidgetTree->ConstructWidget<UACERetailTextBlock>();
 Label->SetText(FText::FromString(Item));Label->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),10));
 Label->SetColorAndOpacity(FLinearColor::White);return Label;
}
void UACEUIGameplayBinder::RefreshKeymapDialog()
{
 if(KeymapDialog)KeymapDialog->bVisible=bKeymapImportOpen;
 if(!bKeymapImportOpen)
 {
  if(KeymapReportScroll)KeymapReportScroll->SetVisibility(ESlateVisibility::Collapsed);
  if(KeymapFileChoice)KeymapFileChoice->SetVisibility(ESlateVisibility::Collapsed);
  if(KeymapImportPath)KeymapImportPath->SetVisibility(ESlateVisibility::Collapsed);
  for(UTextBlock* L:KeymapDialogLabels)if(L)L->SetVisibility(ESlateVisibility::Collapsed);
  return;
 }
 if(!KeymapDialog)return;
 // A dialog is a separate window so its art, labels and inputs all paint above
 // the keyboard window's generated labels, just as retail UIRegion siblings do.
 if(const auto Frame=Manager->FindElementByName(TEXT("KeyboardFrame")))
 {
  const auto Parent=KeymapDialog->Parent.Pin();
  const FIntPoint Origin=Frame->GetScreenOrigin()-(Parent?Parent->GetScreenOrigin():FIntPoint::ZeroValue);
  KeymapDialog->X=Origin.X;KeymapDialog->Y=Origin.Y;
  KeymapDialog->Width=Frame->Width;KeymapDialog->Height=Frame->Height;
  KeymapDialog->EdgeAnchorX=KeymapDialog->EdgeAnchorY=KeymapDialog->UserDragX=KeymapDialog->UserDragY=0;
  KeymapDialog->RecomputeLayoutOffset();
 }
 const auto Box=KeymapChild(KeymapDialog,0x3d),Body=KeymapChild(KeymapDialog,0x3e);
 const auto Choice=KeymapChild(KeymapDialog,0x21),OK=KeymapChild(KeymapDialog,0x22),Cancel=KeymapChild(KeymapDialog,0x23);
 Choice->bVisible=!bKeymapReport;
 const FString Captions[]={KeymapDialogMessage,bKeymapReport?TEXT("Review"):KeymapOverwritePath.IsEmpty()?(bKeymapSave?TEXT("Save"):TEXT("Load")):TEXT("Overwrite"),TEXT("Cancel")};
 const TSharedPtr<FACEUIElement> Elements[]={Body,OK,Cancel};
 for(int32 I=0;I<3;++I)
 {
  if(KeymapDialogLabels.Num()<=I)KeymapDialogLabels.Add(Canvas->WidgetTree->ConstructWidget<UACERetailTextBlock>());
  auto* Label=KeymapDialogLabels[I].Get();Label->SetText(FText::FromString(Captions[I]));Label->SetJustification(ETextJustify::Center);
  Label->SetAutoWrapText(I==0);
  Label->SetVisibility(ESlateVisibility::HitTestInvisible);Canvas->PlaceWidgetAtElement(Label,Elements[I],100501);
  if(I==0 && bKeymapReport)Label->SetVisibility(ESlateVisibility::Collapsed);
 }
 if(bKeymapReport)
 {
  if(!KeymapReportScroll)
  {
   KeymapReportScroll=Canvas->WidgetTree->ConstructWidget<UScrollBox>();
   KeymapReportText=Canvas->WidgetTree->ConstructWidget<UACERetailTextBlock>();
   KeymapReportText->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),10));
   KeymapReportText->SetAutoWrapText(true);KeymapReportScroll->AddChild(KeymapReportText);
  }
  KeymapReportText->SetText(FText::FromString(KeymapDialogMessage));
  KeymapReportScroll->SetVisibility(ESlateVisibility::Visible);Canvas->PlaceWidgetAtElement(KeymapReportScroll,Body,100502);
 }
 else if(KeymapReportScroll)KeymapReportScroll->SetVisibility(ESlateVisibility::Collapsed);
 if(KeymapFileChoice){KeymapFileChoice->SetVisibility(bKeymapSave?ESlateVisibility::Collapsed:ESlateVisibility::Visible);Canvas->PlaceWidgetAtElement(KeymapFileChoice,Choice,100502);}
 if(KeymapImportPath){KeymapImportPath->SetVisibility(bKeymapSave?ESlateVisibility::Visible:ESlateVisibility::Collapsed);Canvas->PlaceWidgetAtElement(KeymapImportPath,Choice,100502);}
 if(bKeymapReport){KeymapFileChoice->SetVisibility(ESlateVisibility::Collapsed);KeymapImportPath->SetVisibility(ESlateVisibility::Collapsed);}
}
void UACEUIGameplayBinder::ShowKeymapImport(const FString& Report)
{
 if(!Canvas||!Canvas->WidgetTree||!Manager)return;
 const auto Frame=Manager->FindElementByName(TEXT("KeyboardFrame"));if(!Frame)return;
 bKeymapImportOpen=true;
 if(!KeymapDialog)
 {
  // Retail gmKeyboardUI::MakeLoadKeymapDialog requests DialogFactory type 7.
  KeymapDialog=UACEUILayoutResolver::LoadTemplate(0x2100003c,0x1f);if(!KeymapDialog)return;
  KeymapDialog->SetElementName(TEXT("RootGameplay_KeymapDialog_Field"));KeymapDialog->ZLevel=10000;
  KeymapDialog->LeftEdge=KeymapDialog->RightEdge=KeymapDialog->TopEdge=KeymapDialog->BottomEdge=0;
  KeymapDialog->Width=Frame->Width;KeymapDialog->Height=Frame->Height;
  Manager->FindElementByName(TEXT("RootGameplay_Field"))->AddChild(KeymapDialog);
  KeymapChild(KeymapDialog,0x22)->SetElementName(TEXT("KeymapFileOK"));
  KeymapChild(KeymapDialog,0x23)->SetElementName(TEXT("KeymapFileCancel"));
  auto Box=KeymapChild(KeymapDialog,0x3d);Box->X=(Frame->Width-Box->Width)/2;Box->Y=(Frame->Height-Box->Height)/2;
  KeymapFileChoice=Canvas->WidgetTree->ConstructWidget<UACERetailComboBoxString>();
  auto Brush=[&](uint32 Id){FSlateBrush B;B.SetResourceObject(Canvas->GetResourceResolver()->ResolveTexture(Id));B.ImageSize=FVector2D(16,18);B.TintColor=FLinearColor::White;return B;};
  FButtonStyle Button;const auto Background=Brush(0x060012b3);Button.SetNormal(Background).SetHovered(Background).SetPressed(Background).SetNormalPadding(FMargin(0)).SetPressedPadding(FMargin(0));
  auto Style=KeymapFileChoice->GetWidgetStyle();Style.ComboButtonStyle.SetButtonStyle(Button).SetDownArrowImage(Brush(0x060012b1)).SetMenuBorderBrush(Background).SetContentPadding(FMargin(0));
  KeymapFileChoice->SetWidgetStyle(Style);KeymapFileChoice->OnGenerateWidgetEvent.BindDynamic(this,&UACEUIGameplayBinder::GenerateKeymapFileLabel);
  KeymapImportPath=Canvas->WidgetTree->ConstructWidget<UEditableTextBox>();
  auto EntryStyle=KeymapImportPath->GetWidgetStyle();EntryStyle.SetPadding(FMargin(2,0));
  EntryStyle.SetBackgroundImageNormal(Background).SetBackgroundImageHovered(Background).SetBackgroundImageFocused(Background);
  EntryStyle.SetForegroundColor(FLinearColor::White).SetBackgroundColor(FLinearColor::White);
  EntryStyle.TextStyle.SetColorAndOpacity(FLinearColor::White);
  EntryStyle.TextStyle.SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),10));KeymapImportPath->SetWidgetStyle(EntryStyle);
 }
 KeymapDialogMessage=Report.IsEmpty()?(bKeymapSave?TEXT("Save keymap as:"):TEXT("Load a keymap file:")):Report;
 if(Report.IsEmpty())
 {
  bKeymapReport=false;
  KeymapOverwritePath.Reset();KeymapFiles.Reset();KeymapFileChoice->ClearOptions();
  TArray<FString> Folders={FPaths::Combine(FPlatformProcess::UserDir(),TEXT("Asheron's Call")),FPaths::ProjectSavedDir()/TEXT("Keymaps")};
  if(auto* GI=Canvas->GetGameInstance())if(auto* Dat=GI->GetSubsystem<UACEDatSubsystem>())Folders.AddUnique(Dat->GetDatDirectory());
  for(const FString& Folder:Folders)
  {
   if(Folder.IsEmpty())continue;TArray<FString> Names;IFileManager::Get().FindFiles(Names,*(Folder/TEXT("*.keymap")),true,false);Names.Sort();
   for(const auto& Name:Names)if(KeymapFiles.Num()<100)
   {
    const FString Full=FPaths::ConvertRelativePathToFull(Folder/Name);if(KeymapFiles.Contains(Full))continue;
    KeymapFiles.Add(Full);KeymapFileChoice->AddOption(KeymapFileChoice->FindOptionIndex(Name)==INDEX_NONE?Name:Name+TEXT(" - ")+FPaths::GetCleanFilename(Folder));
   }
  }
  if(!KeymapFiles.IsEmpty())KeymapFileChoice->SetSelectedIndex(0);
  else if(!bKeymapSave)KeymapDialogMessage=TEXT("No .keymap files found. Copy one to Documents/Asheron's Call, Saved/Keymaps, or beside the DAT files.");
  KeymapImportPath->SetText(FText::FromString(KeyboardFileName));
 }
 // Keep the retail menu/OK/Cancel row, expanding only the message area for errors.
 const auto Box=KeymapChild(KeymapDialog,0x3d),Body=KeymapChild(KeymapDialog,0x3e);
 const int32 H=bKeymapReport?FMath::Max(167,Frame->Height-20):KeymapDialogMessage.Len()>50?167:95,Delta=H-Box->Height;
 for(auto C:Box->Children){if(C->TopEdge==2)C->Y+=Delta;else if(C!=Body&&C->BottomEdge==1)C->Height+=Delta;}
 Body->Height=H-(bKeymapReport?65:77);Box->Height=H;Box->Y=(Frame->Height-H)/2;
 if(bKeymapReport && KeymapReportScroll)KeymapReportScroll->ScrollToStart();
 Manager->BringFloatyToFront(KeymapDialog);
 RefreshKeyboardOverlays();
}
void UACEUIGameplayBinder::HandleKeymapImport(const FString& Path)
{
 if(bKeymapReport && Path.IsEmpty()){bKeymapImportOpen=false;bKeymapReport=false;RefreshKeyboardOverlays();return;}
 if(Path==TEXT("!back")){bKeymapImportOpen=false;RefreshKeyboardOverlays();return;}
 FString File=Path;
 if(bKeymapSave)
 {
  if(File.IsEmpty()&&KeymapImportPath)File=KeymapImportPath->GetText().ToString();
  File=File.TrimStartAndEnd().TrimQuotes();if(File.IsEmpty()){ShowKeymapImport(TEXT("Enter a filename."));return;}
  if(FPaths::IsRelative(File))File=FPaths::ProjectSavedDir()/TEXT("Keymaps")/File;
  if(!File.EndsWith(TEXT(".keymap"),ESearchCase::IgnoreCase))File+=TEXT(".keymap");
  if(IFileManager::Get().FileExists(*File)&&KeymapOverwritePath!=File)
  {KeymapOverwritePath=File;ShowKeymapImport(TEXT("This file exists. Overwrite it?"));return;}
  FString Error;if(!ACEInputBindings::ExportRetailKeymapFile(File,Error)){ShowKeymapImport(Error);return;}
  KeyboardFileName=FPaths::GetCleanFilename(File);bKeymapImportOpen=false;RefreshKeyboardOverlays();return;
 }
 if(File.IsEmpty()&&KeymapFileChoice)
 {
  const int32 Index=KeymapFileChoice->GetSelectedIndex();if(KeymapFiles.IsValidIndex(Index))File=KeymapFiles[Index];
 }
 const auto Result=ACEInputBindings::ImportRetailKeymapFile(File);
 if(!Result.bSuccess){ShowKeymapImport(Result.Error);return;}
 KeyboardFileName=FPaths::GetCleanFilename(File);
 if(!Result.Skipped.IsEmpty() || !Result.UnchangedContexts.IsEmpty())
 {
  bKeymapReport=true;
  FString Report=FString::Printf(TEXT("Loaded %d bindings into the draft. Review returns to Keyboard: OK applies them; Cancel discards them."),Result.BindingCount);
  if(!Result.Skipped.IsEmpty())Report+=TEXT("\n\nNot imported:\n")+FString::Join(Result.Skipped,TEXT("\n"));
  if(!Result.UnchangedContexts.IsEmpty())Report+=TEXT("\n\nSeparate UI/system maps (not remapped):\n")+FString::Join(Result.UnchangedContexts,TEXT("\n"));
  UE_LOG(LogTemp,Display,TEXT("Retail keymap import: %s"),*Report);
  ShowKeymapImport(Report);return;
 }
 bKeymapImportOpen=false;RefreshKeyboardOverlays();
}

void UACEUIGameplayBinder::PollKeyboardActions(APlayerController* PC)
{
 ACEInputBindings::SetCombatContext(CombatMode);
 if(!Client||!PC||ACEInputBindings::IsEditing()||IsChatEntryFocused())return;
 auto Pressed=[&](const TCHAR* Name){return ACEInputBindings::Pressed(PC,ACEInputBindings::Action(Name));};
 PollAdditionalKeyboardActions(PC);
 if(ACEInputBindings::IsEditing() || IsChatEntryFocused())return;
 for(int32 I=0;I<18;++I)if(ACEInputBindings::Pressed(PC,ACEInputBindings::Shortcut(I)))
 {
  UseShortcutSlot(I+1);
  break;
 }
 for(int32 I=0;I<9;++I)if(ACEInputBindings::Pressed(PC,ACEInputBindings::SpellSlot(I))){ActivateHotbarSlot(I);break;}
 if(ACEInputBindings::Pressed(PC,EKeys::Zero))
 {
  const int32 Guid=Client->GetSelectedObject().Guid;
  if(Guid)for(int32 I=0;I<18;++I)if(!Client->GetShortcutObject(I)){AssignInventoryShortcut(Guid,I);break;}
 }
 if(Pressed(TEXT("Examine")))Client->SendIdentifyObject(Client->GetSelectedObject().Guid);
 if(ACEInputBindings::Pressed(PC,EKeys::F))UseInventoryItem(Client->GetSelectedObject().Guid);
 if(Pressed(TEXT("Skills"))){ToggleGameplayPanel(TEXT("SkillManagementPanel_Field"));SyncSkillPanelTab(TEXT("SkillPage"));}
 if(Pressed(TEXT("Components"))){ToggleGameplayPanel(TEXT("SpellManagementPanel_Field"));SyncSpellPanelTab(TEXT("SpellComponentPage"));}
 if(Pressed(TEXT("World")))ToggleGameplayPanel(TEXT("WorldPanel_Field"));
 if(Pressed(TEXT("Allegiance"))){ToggleGameplayPanel(TEXT("SocialPanel_Field"));SyncSocialPanelTab(TEXT("AllegiancePage"));}
 if(Pressed(TEXT("Fellowship"))){ToggleGameplayPanel(TEXT("SocialPanel_Field"));SyncSocialPanelTab(TEXT("FellowshipPage"));}
 if(Pressed(TEXT("Ready")))PlayEmoteHotkey(ACEMotion::Ready,true);
 if(Pressed(TEXT("Laugh")))PlayEmoteHotkey(0x13000080u,false);
 if(Pressed(TEXT("Cheer")))PlayEmoteHotkey(0x1300004cu,false);
 if(Pressed(TEXT("Cry")))PlayEmoteHotkey(0x1300007fu,false);
 if(CombatMode==int32(ACECombatMode::Magic))
 {
  const int32 TabDelta=Pressed(TEXT("SpellNextTab"))?1:Pressed(TEXT("SpellPrevTab"))?-1:0;
  const bool FirstTab=Pressed(TEXT("SpellFirstTab")),LastTab=Pressed(TEXT("SpellLastTab"));
  const bool Prev=Pressed(TEXT("SpellPrevious")),Next=Pressed(TEXT("SpellNext")),First=Pressed(TEXT("SpellFirst")),Last=Pressed(TEXT("SpellLast"));
  if(TabDelta||FirstTab||LastTab)
  {
   SetCombatSpellBar(FirstTab?0:LastTab?7:(Client->GetActiveSpellBar()+TabDelta+8)%8);
  }
  if(Prev||Next||First||Last)StepCombatSpellSelection(Next?1:-1,First,Last);
  if(Pressed(TEXT("SpellCast")))CastSelectedHotbarSpell();
  if(TabDelta||FirstTab||LastTab||Prev||Next||First||Last)RefreshSpellHotbarOverlays();
 }
 else if(CombatMode==int32(ACECombatMode::Melee)||CombatMode==int32(ACECombatMode::Missile))
 {
  const bool Missile=CombatMode==int32(ACECombatMode::Missile);
  const int32 Delta=Pressed(Missile?TEXT("MissileIncrease"):TEXT("MeleeIncrease"))?1:Pressed(Missile?TEXT("MissileDecrease"):TEXT("MeleeDecrease"))?-1:0;
  const bool Low=Pressed(Missile?TEXT("MissileLow"):TEXT("MeleeLow")),Medium=Pressed(Missile?TEXT("MissileMedium"):TEXT("MeleeMedium")),High=Pressed(Missile?TEXT("MissileHigh"):TEXT("MeleeHigh"));
  if(Delta){CombatPowerOrAccuracy=FMath::Clamp(CombatPowerOrAccuracy+Delta*.1f,0.f,1.f);RefreshCombatPanelOverlays();}
  if(Low||Medium||High)BeginCombatPowerCharge(Low?ACEAttackHeight::Low:High?ACEAttackHeight::High:ACEAttackHeight::Medium);
 }
}
