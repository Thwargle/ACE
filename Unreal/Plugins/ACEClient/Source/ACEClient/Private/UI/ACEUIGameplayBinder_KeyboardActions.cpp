#include "UI/ACEUIGameplayBinder.h"
#include "UI/ACEUICanvasWidget.h"
#include "UI/ACEUIElementManager.h"
#include "ACEInputBindings.h"
#include "ACEClientSubsystem.h"
#include "ACEPlayerController.h"
#include "Blueprint/WidgetTree.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "HighResScreenshot.h"
#include "Styling/CoreStyle.h"

void UACEUIGameplayBinder::CycleKeyboardSelection(const FString& Kind,int32 Direction)
{
 if(!Client)return;
 const auto Origin=Client->GetPlayerPosition().ToUnrealLocation(1.f);
 const auto Fellow=Client->GetFellowship();
 TArray<FACEWorldObject> Objects=Client->GetWorldObjects();
 Objects.RemoveAll([&](const FACEWorldObject& O)
 {
  if(!O.IsSelectableWorldObject() || !Client->IsWorldObjectVisible(O))return true;
  const auto Delta=O.Position.ToUnrealLocation(1.f)-Origin;
  if(Kind==TEXT("Fellow"))return !Fellow.Members.ContainsByPredicate([&](const FACEFellowshipMember& M){return M.Guid==O.Guid;});
  if(Delta.SizeSquared2D()>FMath::Square(60.f))return true;
  if(O.Guid==Client->GetPlayerGuid())return true;
  if(Kind==TEXT("Player"))return !O.bIsPlayer || !ShouldShowOnRadar(O,Client->GetPlayerGuid());
  if(Kind==TEXT("Corpse"))return !O.IsCorpse() || KeyboardOpenedCorpses.Contains(O.Guid);
  return !ShouldShowOnRadar(O,Client->GetPlayerGuid());
 });
 // CPlayerSystem::SelectNext uses planar distance plus 1.2 * absolute height.
 Objects.Sort([&](const FACEWorldObject& A,const FACEWorldObject& B)
 {
  // Fellowship traversal follows the member list, including self, not distance.
  if(Kind==TEXT("Fellow"))return Fellow.Members.IndexOfByPredicate([&](const auto& M){return M.Guid==A.Guid;})
   <Fellow.Members.IndexOfByPredicate([&](const auto& M){return M.Guid==B.Guid;});
  const FVector DA=A.Position.ToUnrealLocation(1.f)-Origin,DB=B.Position.ToUnrealLocation(1.f)-Origin;
  const double AScore=DA.Size2D()+1.2*FMath::Abs(DA.Z),BScore=DB.Size2D()+1.2*FMath::Abs(DB.Z);
  return AScore==BScore ? uint32(A.Guid)<uint32(B.Guid) : AScore<BScore;
 });
 if(Objects.IsEmpty())return;
 const int32 Current=Objects.IndexOfByPredicate([&](const FACEWorldObject& O){return O.Guid==Client->GetSelectedObject().Guid;});
 const int32 Next=Current==INDEX_NONE||Direction==0?0:(Current+Direction+Objects.Num())%Objects.Num();
 const auto& O=Objects[Next];
 Client->SelectObject(O.Guid);
 if(Kind==TEXT("Corpse") && PlayerController)PlayerController->InteractWithObject(O.Guid);
}

void UACEUIGameplayBinder::HandleStackAmountCommitted(const FText& Text,ETextCommit::Type Method)
{
 if(Method!=ETextCommit::OnCleared && StackAmountEntryGuid==LastSelection.Guid)
 {
  const FString Value=Text.ToString().TrimStartAndEnd();
  if(Value.IsNumeric())SelectedStackAmount=int32(FMath::Clamp<int64>(FCString::Atoi64(*Value),1,FMath::Max(1,SelectedStackMax)));
 }
 if(StackAmountEntry)StackAmountEntry->SetText(FText::AsNumber(SelectedStackAmount,&FNumberFormattingOptions::DefaultNoGrouping()));
}

void UACEUIGameplayBinder::RefreshStackAmountEntry(bool bShow)
{
 if(!Canvas || !Manager || !Canvas->WidgetTree)return;
 if(SelectionStackAmountLabel)SelectionStackAmountLabel->SetVisibility(ESlateVisibility::Collapsed);
 if(!bShow){if(StackAmountEntry)StackAmountEntry->SetVisibility(ESlateVisibility::Collapsed);return;}
 if(!StackAmountEntry)
 {
  StackAmountEntry=Canvas->WidgetTree->ConstructWidget<UEditableTextBox>();
  StackAmountEntry->SetJustification(ETextJustify::Center);
  StackAmountEntry->SetSelectAllTextWhenFocused(true);
  StackAmountEntry->SetSelectAllTextOnCommit(true);
  auto Style=StackAmountEntry->GetWidgetStyle();Style.SetPadding(FMargin(1.f,0.f));Style.SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),8));
  Style.SetBackgroundImageNormal(FSlateNoResource());Style.SetBackgroundImageHovered(FSlateNoResource());Style.SetBackgroundImageFocused(FSlateNoResource());
  Style.SetForegroundColor(FLinearColor::White);StackAmountEntry->SetWidgetStyle(Style);
  StackAmountEntry->OnTextCommitted.AddDynamic(this,&UACEUIGameplayBinder::HandleStackAmountCommitted);
 }
 if(!StackAmountEntry->HasKeyboardFocus() || StackAmountEntryGuid!=LastSelection.Guid)
  StackAmountEntry->SetText(FText::AsNumber(SelectedStackAmount,&FNumberFormattingOptions::DefaultNoGrouping()));
 StackAmountEntryGuid=LastSelection.Guid;
 StackAmountEntry->SetVisibility(ESlateVisibility::Visible);
 Canvas->PlaceWidgetAtElement(StackAmountEntry,Manager->FindElementByName(TEXT("StackSizeEntryBox")),526);
}

void UACEUIGameplayBinder::PollAdditionalKeyboardActions(APlayerController* PC)
{
 auto Pressed=[&](const TCHAR* Name){return ACEInputBindings::Pressed(PC,ACEInputBindings::Action(Name));};
 if(Pressed(TEXT("SelectionSelf")))
 {if(!TryCompletePendingUseWithTarget(Client->GetPlayerGuid()))Client->SelectObject(Client->GetPlayerGuid());}
 for(const TCHAR* Kind:{TEXT("Player"),TEXT("CompassItem"),TEXT("Fellow")})
 {
  const FString Prefix=TEXT("Selection");
  if(Pressed(*(Prefix+TEXT("Closest")+Kind)))CycleKeyboardSelection(Kind,0);
  else if(Pressed(*(Prefix+TEXT("Previous")+Kind)))CycleKeyboardSelection(Kind,-1);
  else if(Pressed(*(Prefix+TEXT("Next")+Kind)))CycleKeyboardSelection(Kind,1);
 }
 if(Pressed(TEXT("SelectionUseClosestUnopenedCorpse")))CycleKeyboardSelection(TEXT("Corpse"),0);
 FACEWorldObject Item;
 const bool Owned=Client->GetWorldObject(Client->GetSelectedObject().Guid,Item) && Client->IsOwnedInventoryItem(Item);
 if(Pressed(TEXT("SelectionSplitStack")) && Item.StackSize>1)
 {
  RefreshSelectionOverlay();
  if(StackAmountEntry)StackAmountEntry->SetKeyboardFocus();
  return;
 }
 if(Pressed(TEXT("SelectionDrop")) && Owned && !Client->IsUseBusy())
 {
  const int32 Amount=FMath::Clamp(SelectedStackAmount,1,FMath::Max(1,Item.StackSize));
  if(!Item.CanDropToWorld())PostInventorySystemMessage(TEXT("You cannot drop that item."));
  else if(Amount<Item.StackSize)Client->SendStackableSplitTo3D(Item.Guid,Amount);
  else Client->SendDropItem(Item.Guid);
 }
 if(Pressed(TEXT("SelectionGive")) && Owned)
 {
  CancelPendingUseWith();PendingUseWithSourceGuid=Item.Guid;bPendingKeyboardGive=true;
  PendingKeyboardGiveAmount=FMath::Clamp(SelectedStackAmount,1,FMath::Max(1,Item.StackSize));
  SyncPendingUseCursor();PostInventorySystemMessage(TEXT("Select who to give this item to."));
 }
 struct FPanel {const TCHAR* Action;const TCHAR* Page;const TCHAR* Tab;};
 static const FPanel Panels[]={
  {TEXT("TogglePositiveEffectsPanel"),TEXT("PositiveEffectsPanel_Field"),nullptr},
  {TEXT("ToggleNegativeEffectsPanel"),TEXT("NegativeEffectsPanel_Field"),nullptr},
  {TEXT("ToggleVitaePanel"),TEXT("VitaePanel_Field"),nullptr},
  {TEXT("ToggleLinkStatusPanel"),TEXT("LinkStatusPanel_Field"),nullptr},
  {TEXT("ToggleConfigOptionsPanel"),TEXT("OptionsPanel_Field"),TEXT("ConfigPage")},
  {TEXT("ToggleFriendsPanel"),TEXT("SocialPanel_Field"),TEXT("FriendsPage")},
  {TEXT("ToggleHousePanel"),TEXT("WorldPanel_Field"),TEXT("HousePage")},
  {TEXT("ToggleJournalPanel"),TEXT("QuestManagementPanel_Field"),TEXT("JournalPage")},
  {TEXT("TogglePageListPanel"),TEXT("QuestManagementPanel_Field"),TEXT("PageListPage")},
  {TEXT("ToggleQuestManagementPanel"),TEXT("QuestManagementPanel_Field"),nullptr}};
 for(const auto& P:Panels)if(Pressed(P.Action))
 {
  const bool SameTab=!P.Tab || (FString(P.Page)==TEXT("SocialPanel_Field")?ActiveSocialTab==P.Tab:
   FString(P.Page)==TEXT("QuestManagementPanel_Field")?ActiveQuestTab==P.Tab:
   FString(P.Page)==TEXT("OptionsPanel_Field")?ActiveOptionsTab==P.Tab:ActiveWorldTab==P.Tab);
  if(ActivePanelPage==P.Page && SameTab)HidePanel();
  else
  {
   ShowPanelPage(P.Page);
   if(FString(P.Page)==TEXT("SocialPanel_Field"))SyncSocialPanelTab(P.Tab);
   else if(FString(P.Page)==TEXT("QuestManagementPanel_Field") && P.Tab){ActiveQuestTab=P.Tab;bJournalFieldsDirty=true;RefreshQuestOverlays();}
   else if(FString(P.Page)==TEXT("OptionsPanel_Field"))SyncOptionsPanelTab(P.Tab);
   else if(FString(P.Page)==TEXT("WorldPanel_Field"))SyncWorldPanelTab(P.Tab);
  }
 }
 for(int32 I=1;I<=4;++I)if(Pressed(*FString::Printf(TEXT("ToggleFloatingChatWindow%d"),I)))HandleNamedClick(FString::Printf(TEXT("FloatingChat%d"),I));
 if(Pressed(TEXT("LOGOUT")))Client->Logout();
 if(Pressed(TEXT("CaptureScreenshot")))FScreenshotRequest::RequestScreenshot(false);
 struct FEmote {const TCHAR* Action;uint32 Motion;bool Hold;};
 static const FEmote Emotes[]={
  {TEXT("AFKState"),0x4300011bu,true},{TEXT("BlowKiss"),0x1300007cu,false},
  {TEXT("BeSeeingYou"),0x1300007bu,false},{TEXT("BowDeep"),0x1300007du,false},
  {TEXT("TapFootState"),0x430000f5u,true},{TEXT("ThinkerState"),0x43000147u,true},
  {TEXT("Winded"),0x1300009au,false},{TEXT("Woah"),0x13000099u,false},{TEXT("YMCA"),0x1200009bu,false}};
 for(const auto& E:Emotes)if(Pressed(E.Action))PlayEmoteHotkey(E.Motion,E.Hold);
 if(Pressed(TEXT("ToggleKeyboardPanel")))ToggleKeyboardMappingUI();
}
