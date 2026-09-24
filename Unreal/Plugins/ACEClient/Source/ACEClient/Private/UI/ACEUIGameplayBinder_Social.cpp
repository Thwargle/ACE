#include "UI/ACERetailTextBlock.h"
#include "UI/ACEUIGameplayBinder.h"
#include "ACESession.h"
#include "UI/ACEUICanvasWidget.h"
#include "UI/ACEUIElementManager.h"
#include "UI/ACEUILayoutResolver.h"
#include "UI/ACEUIResourceResolver.h"
#include "Dat/ACEDatTextLayout.h"
#include "ACEClientSubsystem.h"
#include "ACEDatSubsystem.h"
#include "ACEOpcodes.h"
#include "ACEPlayerController.h"
#include "ACETypes.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/EditableTextBox.h"
#include "Components/MultiLineEditableText.h"
#include "Components/TextBlock.h"
#include "Blueprint/WidgetTree.h"
#include "Styling/CoreStyle.h"

namespace
{
	static const FLinearColor SocialGold(0.95f, 0.82f, 0.35f, 1.f);
	static const FLinearColor SocialWhite(0.92f, 0.92f, 0.88f, 1.f);
	static const FLinearColor SocialDim(0.65f, 0.65f, 0.6f, 1.f);
	/** DAT paint climbs past 20k — overlays must sit above tab/frame chrome. */
	constexpr int32 SocialOverlayZ = 100000;
}

void UACEUIGameplayBinder::HandleFellowshipChanged()
{
	RefreshFellowshipOverlays();
	RefreshSocialButtonLabels();
}

void UACEUIGameplayBinder::HandleAllegianceChanged()
{
	RefreshAllegianceOverlays();
}

void UACEUIGameplayBinder::HandleFriendsChanged()
{
	RefreshFriendsOverlays();
}

void UACEUIGameplayBinder::HandleContractsChanged()
{
	RefreshQuestOverlays();
}

void UACEUIGameplayBinder::HandleSquelchChanged()
{
	if (Client)
		if (auto S=Client->GetSession();S.IsValid())
			GlobalChatTypeFilter=(GlobalChatTypeFilter&0xffffffff00000000ull)|uint32(~S->GetGlobalSquelchMask());
	RefreshSquelchOverlays();
}

void UACEUIGameplayBinder::HandleSquelchNameCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	if (CommitMethod != ETextCommit::OnEnter)
	{
		return;
	}
	const FString Name = Text.ToString().TrimStartAndEnd();
	if (Name.IsEmpty() || !Client)
	{
		return;
	}
	// Retail: guid 0 + name lets the server resolve offline characters. 1 = AllChannels.
	Client->SendModifyCharacterSquelch(true, 0, Name, 1);
	if (SquelchNameEntry)
	{
		SquelchNameEntry->SetText(FText::GetEmpty());
	}
}

void UACEUIGameplayBinder::EnsureSocialEntryBoxes()
{
	if (!Canvas || !Canvas->WidgetTree)
	{
		return;
	}
	UWidgetTree* Tree = Canvas->WidgetTree;
	auto MakeEntry = [&](TObjectPtr<UEditableTextBox>& Box, const FString& Hint)
	{
		if (Box)
		{
			return;
		}
		Box = Tree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass());
		Box->SetVisibility(ESlateVisibility::Collapsed);
		Box->SetHintText(FText::FromString(Hint));
		// Mirror ChatEntry: replace brushes + set a real font once. Mutating DrawAs only
		// (or round-tripping GetWidgetStyle every refresh) left a bad FSlateFontInfo and
		// crashed Slate Prepass when FellowshipNameEntry became visible.
		FEditableTextBoxStyle Style = Box->GetWidgetStyle();
		FSlateBrush Clear;
		Clear.DrawAs = ESlateBrushDrawType::NoDrawType;
		Style.BackgroundImageNormal = Clear;
		Style.BackgroundImageHovered = Clear;
		Style.BackgroundImageFocused = Clear;
		Style.BackgroundImageReadOnly = Clear;
		Style.Padding = FMargin(2.f, 0.f);
		Style.TextStyle.SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 9));
		Style.TextStyle.ColorAndOpacity = FSlateColor(SocialWhite);
		Box->SetWidgetStyle(Style);
		Box->SetClearKeyboardFocusOnCommit(true);
	};
	MakeEntry(FellowshipNameEntry, TEXT("Fellowship name"));
	MakeEntry(FriendNameEntry, TEXT("Friend name"));
	MakeEntry(SquelchNameEntry, TEXT("Character name"));
	if (FriendNameEntry && !bSocialEntriesBound)
	{
		FellowshipNameEntry->OnTextChanged.AddDynamic(this, &UACEUIGameplayBinder::HandleFellowshipNameChanged);
		FellowshipNameEntry->OnTextCommitted.AddDynamic(this, &UACEUIGameplayBinder::HandleFellowshipNameCommitted);
		FriendNameEntry->OnTextCommitted.AddDynamic(this, &UACEUIGameplayBinder::HandleFriendNameCommitted);
		if (SquelchNameEntry)
		{
			SquelchNameEntry->OnTextCommitted.AddDynamic(this, &UACEUIGameplayBinder::HandleSquelchNameCommitted);
		}
		bSocialEntriesBound = true;
	}
}

bool UACEUIGameplayBinder::ScrollFellowship(float WheelDelta,FVector2D CanvasLocalPos)
{
	if(ActivePanelPage!=TEXT("SocialPanel_Field") || ActiveSocialTab!=TEXT("FellowshipPage") || !Manager || !Canvas)return false;
	const auto List=Manager->FindElementUnder(TEXT("FellowshipPage"),TEXT("FellowsListBox"));
	if(!List || !Canvas->IsElementExposedAt(List,CanvasLocalPos))return false;
	const FVector2D P=Canvas->ViewportToLayout(CanvasLocalPos);const FIntPoint O=List->GetScreenOrigin();
	if(P.X<O.X||P.Y<O.Y||P.X>=O.X+List->Width||P.Y>=O.Y+List->Height)return false;
	FellowScrollOffset+=WheelDelta>0?-1:1;RefreshFellowshipOverlays();return true;
}

void UACEUIGameplayBinder::HandleFellowshipNameChanged(const FText&)
{
	if (Manager) if (const auto Create=Manager->FindElementUnder(TEXT("FellowshipPage"),TEXT("CreateFellowshipButton")))
	{Create->bActivatable=CanActivateFellowshipControl(Create->ElementName);Create->bGhosted=!Create->bActivatable;}
}
void UACEUIGameplayBinder::HandleFellowshipNameCommitted(const FText&, ETextCommit::Type Method)
{
	if(Method==ETextCommit::OnEnter)HandleNamedClick(TEXT("CreateFellowshipButton"));
}
bool UACEUIGameplayBinder::CanActivateFellowshipControl(const FString& Name) const
{
	if(!Client)return false;
	const auto Info=Client->GetFellowship();const int32 Self=Client->GetPlayerGuid();
	if(Name==TEXT("CreateFellowshipButton"))return !Info.bValid && FellowshipNameEntry && !FellowshipNameEntry->GetText().ToString().TrimStartAndEnd().IsEmpty();
	if(!Info.bValid)return false;
	const bool Leader=Info.LeaderGuid==Self;
	if(Name==TEXT("FellowQuitButton"))return true;
	if(Name==TEXT("FellowOpenButton")||Name==TEXT("FellowDisbandButton"))return Leader;
	if(Name==TEXT("FellowLeaderButton")||Name==TEXT("FellowDismissButton"))
		return Leader && SelectedFellowGuid!=Self && Info.Members.ContainsByPredicate([&](const auto& M){return M.Guid==SelectedFellowGuid;});
	if(Name==TEXT("FellowRecruitButton"))
	{
		FACEWorldObject Target;
		return (Leader||Info.bOpen) && Info.Members.Num()<9 && LastSelection.bValid && LastSelection.Guid!=Self
			&& Client->GetWorldObject(LastSelection.Guid,Target) && Target.bIsPlayer
			&& !Info.Members.ContainsByPredicate([&](const auto& M){return M.Guid==Target.Guid;});
	}
	return false;
}

bool UACEUIGameplayBinder::CanActivateAllegianceControl(const FString& Name, int32 TargetGuid) const
{
	if (!Client) return false;
	const auto Info=Client->GetAllegiance();
	if (Name==TEXT("BreakButton")) return Info.PatronGuid!=0 && (!TargetGuid || TargetGuid==Info.PatronGuid);
	if (Name==TEXT("KickButton"))
		return Info.Vassals.ContainsByPredicate([&](const auto& V){return V.Guid==(TargetGuid ? TargetGuid : SelectedVassalGuid);});
	if (Name!=TEXT("SwearButton") || Info.PatronGuid) return false;
	const int32 Target=TargetGuid ? TargetGuid : LastSelection.bValid ? LastSelection.Guid : 0;
	FACEWorldObject Player;
	return Target && Target!=Client->GetPlayerGuid() && Target!=Info.MonarchGuid
		&& Client->GetWorldObject(Target,Player) && Player.bIsPlayer
		&& !Info.Vassals.ContainsByPredicate([&](const auto& V){return V.Guid==Target;});
}

void UACEUIGameplayBinder::ShowAllegianceConfirmation(const FString& Action)
{
	if (!CanActivateAllegianceControl(Action)) return;
	const auto Info=Client->GetAllegiance();
	PendingAllegianceAction=Action;
	if (Action==TEXT("SwearButton"))
	{
		PendingAllegianceGuid=LastSelection.Guid;
		FACEWorldObject Player; Client->GetWorldObject(PendingAllegianceGuid,Player);
		PendingAllegiancePrompt=FString::Printf(TEXT("Are you sure you want to swear allegiance to %s?"),*Player.Name);
	}
	else if (Action==TEXT("BreakButton"))
	{
		PendingAllegianceGuid=Info.PatronGuid;
		PendingAllegiancePrompt=FString::Printf(TEXT("Are you sure you want to break your allegiance to %s?"),*Info.PatronName);
	}
	else
	{
		PendingAllegianceGuid=SelectedVassalGuid;
		const auto* V=Info.Vassals.FindByPredicate([&](const auto& Member){return Member.Guid==PendingAllegianceGuid;});
		PendingAllegiancePrompt=FString::Printf(TEXT("Are you sure you want to break %s's allegiance to you?"),*V->Name);
	}
	RefreshServerConfirmation();
}

bool UACEUIGameplayBinder::ScrollAllegiance(float WheelDelta,FVector2D CanvasLocalPos)
{
	if (ActivePanelPage!=TEXT("SocialPanel_Field") || ActiveSocialTab!=TEXT("AllegiancePage") || !Manager || !Canvas) return false;
	const auto List=Manager->FindElementUnder(TEXT("AllegiancePage"),TEXT("VassalsListBox"));
	if (!Canvas->IsElementExposedAt(List,CanvasLocalPos)) return false;
	VassalScrollOffset+=WheelDelta>0 ? -1 : 1;
	RefreshAllegianceOverlays(); return true;
}

void UACEUIGameplayBinder::HandleFriendNameCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	if (CommitMethod != ETextCommit::OnEnter && CommitMethod != ETextCommit::OnUserMovedFocus)
	{
		return;
	}
	const FString Name = Text.ToString().TrimStartAndEnd();
	if (Name.IsEmpty() || !Client)
	{
		return;
	}
	Client->SendAddFriend(Name);
	if (FriendNameEntry)
	{
		FriendNameEntry->SetText(FText::GetEmpty());
	}
}


void UACEUIGameplayBinder::SyncSocialPanelTab(const FString& PageName)
{
	if (!Manager)
	{
		return;
	}
	if (Client && ActiveSocialTab!=PageName)
	{
		if (ActiveSocialTab==TEXT("AllegiancePage")) Client->SendAllegianceUpdateRequest(false);
		if (ActiveSocialTab==TEXT("FellowshipPage")) Client->SendFellowshipUpdateRequest(false);
	}
	ActiveSocialTab = PageName;
	static const TCHAR* Pages[] = {
		TEXT("AllegiancePage"), TEXT("FellowshipPage"), TEXT("FriendsPage"), TEXT("SquelchPage")
	};
	for (const TCHAR* Page : Pages)
	{
		Manager->SetElementVisibleByName(Page, PageName == Page);
	}
	ApplyPanelTabChrome(TEXT("AllegianceTab"), PageName == TEXT("AllegiancePage"));
	ApplyPanelTabChrome(TEXT("FellowshipTab"), PageName == TEXT("FellowshipPage"));
	ApplyPanelTabChrome(TEXT("FriendsTab"), PageName == TEXT("FriendsPage"));
	ApplyPanelTabChrome(TEXT("SquelchTab"), PageName == TEXT("SquelchPage"));

	if (Client)
	{
		if (PageName == TEXT("AllegiancePage"))
		{
			Client->SendAllegianceUpdateRequest(true);
		}
		else if (PageName == TEXT("FellowshipPage"))
		{
			Client->SendFellowshipUpdateRequest(true);
		}
	}
	RefreshSocialOverlays();
}

void UACEUIGameplayBinder::RefreshSocialOverlays()
{
	if (ActivePanelPage != TEXT("SocialPanel_Field"))
	{
		for (UTextBlock* T : SocialTabLabels)
		{
			if (T) { T->SetVisibility(ESlateVisibility::Collapsed); }
		}
		RefreshAllegianceOverlays();
		RefreshFellowshipOverlays();
		RefreshFriendsOverlays();
		RefreshSquelchOverlays();
		RefreshSocialButtonLabels();
		return;
	}
	EnsureSocialEntryBoxes();
	static const TPair<const TCHAR*, const TCHAR*> Tabs[] = {
		{ TEXT("AllegianceTab"), TEXT("Allegiance") },
		{ TEXT("FellowshipTab"), TEXT("Fellowship") },
		{ TEXT("FriendsTab"), TEXT("Friends") },
		{ TEXT("SquelchTab"), TEXT("Squelch") },
	};
	for (int32 i = 0; i < UE_ARRAY_COUNT(Tabs); ++i)
	{
		UTextBlock* Label = nullptr;
		if (SocialTabLabels.IsValidIndex(i))
		{
			Label = SocialTabLabels[i];
		}
		if (!Label && Canvas && Canvas->WidgetTree)
		{
			Label = Canvas->WidgetTree->ConstructWidget<UTextBlock>(UACERetailTextBlock::StaticClass());
			Label->SetJustification(ETextJustify::Center);
			if (SocialTabLabels.Num() <= i)
			{
				SocialTabLabels.SetNum(i + 1);
			}
			SocialTabLabels[i] = Label;
		}
		if (!Label)
		{
			continue;
		}
		TSharedPtr<FACEUIElement> El = Manager->FindElementUnder(
			TEXT("SocialPanel_Field"), Tabs[i].Key);
		if (!El.IsValid())
		{
			Label->SetVisibility(ESlateVisibility::Collapsed);
			continue;
		}
		const bool bActive = (i == 0 && ActiveSocialTab == TEXT("AllegiancePage"))
			|| (i == 1 && ActiveSocialTab == TEXT("FellowshipPage"))
			|| (i == 2 && ActiveSocialTab == TEXT("FriendsPage"))
			|| (i == 3 && ActiveSocialTab == TEXT("SquelchPage"));
		Label->SetText(FText::FromString(Tabs[i].Value));
		Label->SetColorAndOpacity(FSlateColor(bActive ? SocialGold : SocialWhite));
		Label->SetFont(FCoreStyle::GetDefaultFontStyle(bActive ? TEXT("Bold") : TEXT("Regular"), 9));
		Label->SetVisibility(ESlateVisibility::HitTestInvisible);
		// Inset like SkillManagement tabs so captions sit in the tab face, not over chrome edges.
		Canvas->PlaceWidgetAtElement(Label, El, SocialOverlayZ, FMargin(10.f, 4.f, 8.f, 4.f));
	}
	RefreshAllegianceOverlays();
	RefreshFellowshipOverlays();
	RefreshFriendsOverlays();
	RefreshSquelchOverlays();
	RefreshSocialButtonLabels();
}

void UACEUIGameplayBinder::RefreshAllegianceOverlays()
{
	auto Hide = [](UTextBlock* T)
	{
		if (T) { T->SetVisibility(ESlateVisibility::Collapsed); }
	};
	if (ActivePanelPage != TEXT("SocialPanel_Field") || ActiveSocialTab != TEXT("AllegiancePage")
		|| !Client || !Manager || !Canvas)
	{
		Hide(AllegianceNameLabel);
		Hide(MonarchNameLabel);
		Hide(PatronNameLabel);
		Hide(AllegianceXPLabel);
		Hide(AllegiancePatronXPLabel);
		for (UTextBlock* L : AllegianceCaptionLabels) { Hide(L); }
		for (UTextBlock* R : VassalRows) { Hide(R); }
		for (UTextBlock* R : VassalXPRows) { Hide(R); }
		return;
	}
	EnsureOverlays();
	constexpr int32 AllegianceZ = 100000;
	const FACEAllegianceInfo Info = Client->GetAllegiance();
	if (!Info.Vassals.ContainsByPredicate([&](const auto& V){return V.Guid==SelectedVassalGuid;})) SelectedVassalGuid=0;
	for (const TCHAR* Name:{TEXT("SwearButton"),TEXT("BreakButton"),TEXT("KickButton")})
		if (auto Button=Manager->FindElementUnder(TEXT("AllegiancePage"),Name))
		{ Button->bActivatable=CanActivateAllegianceControl(Name); Button->bGhosted=!Button->bActivatable; }
	auto Place = [&](TObjectPtr<UTextBlock>& Label, const FString& ElementName, const FString& Text, int32 Z)
	{
		if (!Label && Canvas->WidgetTree)
		{
			Label = Canvas->WidgetTree->ConstructWidget<UTextBlock>(UACERetailTextBlock::StaticClass());
		}
		if (!Label)
		{
			return;
		}
		// Prefer AllegiancePage scope so fonts resolve from the DAT BaseElement.
		TSharedPtr<FACEUIElement> El = Manager->FindElementUnder(TEXT("AllegiancePage"), ElementName);
		if (!El.IsValid())
		{
			El = Manager->FindElementByName(ElementName);
		}
		PlaceTextOnElement(Label, El, Text, 9, SocialWhite, AllegianceZ + Z);
	};
	Place(AllegianceNameLabel, TEXT("AllegianceNameText"),
		Info.bValid ? Info.AllegianceName : TEXT("No Allegiance"), 0);
	Place(MonarchNameLabel, TEXT("MonarchName"),
		Info.MonarchName.IsEmpty() ? TEXT("-") : Info.MonarchName, 1);
	Place(PatronNameLabel, TEXT("PatronName"),
		Info.PatronName.IsEmpty() ? TEXT("-") : Info.PatronName, 2);

	TSharedPtr<FACEUIElement> ListEl = Manager->FindElementUnder(TEXT("AllegiancePage"), TEXT("VassalsListBox"));
	if (!ListEl.IsValid())
	{
		ListEl = Manager->FindElementByName(TEXT("VassalsListBox"));
	}
	constexpr int32 RowH=18;
	VassalVisibleRows=ListEl ? FMath::Max(1,ListEl->Height/RowH) : 1;
	const int32 MaxOffset=FMath::Max(0,Info.Vassals.Num()-VassalVisibleRows);
	VassalScrollOffset=FMath::Clamp(VassalScrollOffset,0,MaxOffset);
	SyncDatScrollbar(Manager->FindElementUnder(TEXT("AllegiancePage"),TEXT("VassalsListBoxScrollbar")),
		MaxOffset ? float(VassalScrollOffset)/MaxOffset : 0.f, Info.Vassals.IsEmpty() ? 1.f : FMath::Min(1.f,float(VassalVisibleRows)/Info.Vassals.Num()));
	const int32 MaxRows=FMath::Min(VassalVisibleRows,Info.Vassals.Num());
	while (VassalRows.Num() < MaxRows && Canvas->WidgetTree)
	{
		UTextBlock* Row = Canvas->WidgetTree->ConstructWidget<UTextBlock>(UACERetailTextBlock::StaticClass());
		VassalRows.Add(Row);
		VassalXPRows.Add(Canvas->WidgetTree->ConstructWidget<UTextBlock>(UACERetailTextBlock::StaticClass()));
	}
	VassalRowGuids.SetNum(VassalRows.Num());
	for (int32 i = 0; i < VassalRows.Num(); ++i)
	{
		UTextBlock* Row = VassalRows[i];
		if (!Row)
		{
			continue;
		}
		if (!ListEl.IsValid() || !Info.Vassals.IsValidIndex(i+VassalScrollOffset) || i>=VassalVisibleRows)
		{
			Row->SetVisibility(ESlateVisibility::Collapsed);
			VassalRowGuids[i] = 0;
			VassalXPRows[i]->SetVisibility(ESlateVisibility::Collapsed);
			continue;
		}
		const FACEAllegianceMember& V = Info.Vassals[i+VassalScrollOffset];
		VassalRowGuids[i] = V.Guid;
		const bool bSel = (V.Guid != 0 && V.Guid == SelectedVassalGuid);
		FString Line=FString::Printf(TEXT("%s (L%d)"),*V.Name,V.Level);
		FACEDatFont Font;
		if (Canvas->GetResourceResolver()->ResolveFont(0x40000000,Font)) Line=ACEDatText::Ellipsize(Font,Line,171);
		const FLinearColor Color=bSel ? SocialGold : V.bOnline ? SocialWhite : SocialDim;
		PlaceTextOnElement(Row,ListEl,Line,9,Color,AllegianceZ+10+i);
		Canvas->PlaceWidgetAtElement(Row,ListEl,AllegianceZ+10+i,FMargin(4,i*RowH,ListEl->Width-175,ListEl->Height-(i+1)*RowH));
		UTextBlock* XP=VassalXPRows[i];
		PlaceTextOnElement(XP,ListEl,FText::AsNumber(V.CPTithed).ToString(),9,Color,AllegianceZ+10+i);
		XP->SetJustification(ETextJustify::Right);
		Canvas->PlaceWidgetAtElement(XP,ListEl,AllegianceZ+10+i,FMargin(179,i*RowH,4,ListEl->Height-(i+1)*RowH));

	}

	if (!AllegianceXPLabel && Canvas->WidgetTree)
	{
		AllegianceXPLabel = Canvas->WidgetTree->ConstructWidget<UTextBlock>(UACERetailTextBlock::StaticClass());
	}
	if (AllegianceXPLabel)
	{
		PlaceTextOnElement(AllegianceXPLabel, Manager->FindElementUnder(TEXT("MonarchField"),
			TEXT("XPProduced")), FText::AsNumber(Info.SelfCPTithed).ToString(), 9, SocialWhite,
			AllegianceZ + 3);
	}
	if (!AllegiancePatronXPLabel && Canvas->WidgetTree)
	{
		AllegiancePatronXPLabel =
			Canvas->WidgetTree->ConstructWidget<UTextBlock>(UACERetailTextBlock::StaticClass());
	}
	if (AllegiancePatronXPLabel)
	{
		PlaceTextOnElement(AllegiancePatronXPLabel, Manager->FindElementUnder(TEXT("PatronField"),
			TEXT("XPProduced")), FText::AsNumber(Info.SelfCPTithed).ToString(), 9, SocialWhite,
			AllegianceZ + 4);
	}

	// Player header + list captions. Retail fills these from code, not from the DAT.
	struct FAllegianceCaption { const TCHAR* Element; FString Text; };
	const int32 SelfLevel = Client->GetPlayerVitals().Level;
	FACEWorldObject SelfObj;
	const FString SelfName = Client->GetWorldObject(Client->GetPlayerGuid(), SelfObj)
		? SelfObj.Name : FString(TEXT("You"));
	const FAllegianceCaption Captions[] = {
		{ TEXT("PlayerName"), FString::Printf(TEXT("%s (L%d)"), *SelfName, SelfLevel) },
		{ TEXT("PlayerRank"), FString::Printf(TEXT("Rank %d"), Info.Rank) },
		{ TEXT("PlayerFollowers"), FString::Printf(TEXT("%d followers"),
			FMath::Max(0, Info.TotalMembers - 1)) },
		{ TEXT("MonarchFollowers"), FString::Printf(TEXT("%d in allegiance"),
			FMath::Max(0, Info.TotalMembers)) },
		{ TEXT("VassalsListBoxLabel"), FString::Printf(TEXT("Vassals (%d)"), Info.Vassals.Num()) },
		{ TEXT("VassalsXPProducedLabel"), FString(TEXT("XP Produced")) },
	};
	while (AllegianceCaptionLabels.Num() < UE_ARRAY_COUNT(Captions) && Canvas->WidgetTree)
	{
		AllegianceCaptionLabels.Add(
			Canvas->WidgetTree->ConstructWidget<UTextBlock>(UACERetailTextBlock::StaticClass()));
	}
	for (int32 i = 0; i < UE_ARRAY_COUNT(Captions); ++i)
	{
		if (UTextBlock* L = AllegianceCaptionLabels[i])
		{
			TSharedPtr<FACEUIElement> CapEl = Manager->FindElementUnder(TEXT("AllegiancePage"),
				Captions[i].Element);
			if (!CapEl.IsValid())
			{
				CapEl = Manager->FindElementByName(Captions[i].Element);
			}
			PlaceTextOnElement(L, CapEl, Captions[i].Text, 9, SocialWhite, AllegianceZ + 5 + i);
		}
	}
}

void UACEUIGameplayBinder::RefreshFellowshipOverlays()
{
	if (ActivePanelPage != TEXT("SocialPanel_Field") || ActiveSocialTab != TEXT("FellowshipPage")
		|| !Client || !Manager || !Canvas)
	{
		for (UTextBlock* R : FellowRows) { if (R) R->SetVisibility(ESlateVisibility::Collapsed); }
		for (UTextBlock* R : FellowDetailLabels) { if (R) R->SetVisibility(ESlateVisibility::Collapsed); }
		if (FellowshipNameEntry) { FellowshipNameEntry->SetVisibility(ESlateVisibility::Collapsed); }
		if (FellowshipTitleLabel) { FellowshipTitleLabel->SetVisibility(ESlateVisibility::Collapsed); }
		return;
	}
	EnsureOverlays();
	EnsureSocialEntryBoxes();
	const FACEFellowshipInfo Info = Client->GetFellowship();
	if(!Info.Members.ContainsByPredicate([&](const auto& M){return M.Guid==SelectedFellowGuid;}))SelectedFellowGuid=0;
	for(const TCHAR* Name:{TEXT("CreateFellowshipButton"),TEXT("FellowQuitButton"),TEXT("FellowOpenButton"),TEXT("FellowDisbandButton"),TEXT("FellowLeaderButton"),TEXT("FellowDismissButton"),TEXT("FellowRecruitButton")})
		if(const auto Button=Manager->FindElementUnder(TEXT("FellowshipPage"),Name))
		{Button->bActivatable=CanActivateFellowshipControl(Name);Button->bGhosted=!Button->bActivatable;}

	Manager->SetElementVisibleByName(TEXT("NotInAFellowshipFrame"), !Info.bValid);
	Manager->SetElementVisibleByName(TEXT("FellowshipFrame"), Info.bValid);

	auto PlaceSocialEntry = [&](UEditableTextBox* Box, const FString& ElementName)
	{
		if (!Box)
		{
			return;
		}
		TSharedPtr<FACEUIElement> EntryEl = Manager->FindElementUnder(
			TEXT("FellowshipPage"), ElementName);
		if (!EntryEl.IsValid())
		{
			EntryEl = Manager->FindElementByName(ElementName);
		}
		if (!EntryEl.IsValid())
		{
			Box->SetVisibility(ESlateVisibility::Collapsed);
			return;
		}
		// Style is set once in EnsureSocialEntryBoxes — do not Get/SetWidgetStyle here
		// (corrupts FSlateFontInfo and crashes Slate Prepass on show).
		Box->SetVisibility(ESlateVisibility::Visible);
		Canvas->PlaceWidgetAtElement(Box, EntryEl, SocialOverlayZ + 5, FMargin(3.f, 1.f));
	};

	if (!Info.bValid)
	{
		FellowScrollOffset=0;
		for(auto Entry:FellowRowElements)if(Entry)Entry->bVisible=false;
		for(UTextBlock* Label:FellowDetailLabels)if(Label)Label->SetVisibility(ESlateVisibility::Collapsed);
		PlaceSocialEntry(FellowshipNameEntry, TEXT("FellowshipNameEntryBox"));
		if (!FellowshipTitleLabel && Canvas->WidgetTree)
		{
			FellowshipTitleLabel = Canvas->WidgetTree->ConstructWidget<UTextBlock>(UACERetailTextBlock::StaticClass());
		}
		if (FellowshipTitleLabel)
		{
			PlaceTextOnElement(FellowshipTitleLabel,
				Manager->FindElementUnder(TEXT("FellowshipPage"), TEXT("FellowshipNameLabel")),
				TEXT("Fellowship Name:"), 9, SocialGold, SocialOverlayZ + 4);
		}
		for (UTextBlock* R : FellowRows) { if (R) R->SetVisibility(ESlateVisibility::Collapsed); }
		return;
	}

	if (FellowshipNameEntry)
	{
		FellowshipNameEntry->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (!FellowshipTitleLabel && Canvas->WidgetTree)
	{
		FellowshipTitleLabel = Canvas->WidgetTree->ConstructWidget<UTextBlock>(UACERetailTextBlock::StaticClass());
	}
	if (FellowshipTitleLabel)
	{
		const FString Title = FString::Printf(TEXT("%s%s%s"),
			*Info.Name,
			Info.bOpen ? TEXT("  [Open]") : TEXT("  [Closed]"),
			Info.bShareXP ? TEXT("  XP") : TEXT(""));
		TSharedPtr<FACEUIElement> TitleEl = Manager->FindElementUnder(
			TEXT("FellowshipPage"), TEXT("FellowshipName"));
		PlaceTextOnElement(FellowshipTitleLabel, TitleEl, Title, 10, SocialGold, SocialOverlayZ + 4);
	}

	TSharedPtr<FACEUIElement> ListEl = Manager->FindElementUnder(
		TEXT("FellowshipPage"), TEXT("FellowsListBox"));
	constexpr int32 MaxRows = 9;
	constexpr int32 RowH = 32; // classic_fellowship: name/level, then H/S/M meters
	while (FellowRows.Num() < MaxRows && Canvas->WidgetTree)
	{
		UTextBlock* Row = Canvas->WidgetTree->ConstructWidget<UTextBlock>(UACERetailTextBlock::StaticClass());
		FellowRows.Add(Row);
		FellowRowElements.Add(UACEUILayoutResolver::LoadTemplate(0x21000030,0x10000281));
		for(int32 J=0;J<4;++J) FellowDetailLabels.Add(Canvas->WidgetTree->ConstructWidget<UTextBlock>(UACERetailTextBlock::StaticClass()));
	}
	FellowRowGuids.SetNum(MaxRows);
	FellowVisibleRows=ListEl ? FMath::Max(1,ListEl->Height/RowH) : 1;
	const int32 MaxOffset=FMath::Max(0,Info.Members.Num()-FellowVisibleRows);
	FellowScrollOffset=FMath::Clamp(FellowScrollOffset,0,MaxOffset);
	SyncDatScrollbar(Manager->FindElementUnder(TEXT("FellowshipPage"),TEXT("FellowsListBoxScrollbar")),MaxOffset?float(FellowScrollOffset)/MaxOffset:0, Info.Members.IsEmpty()?1.f:FMath::Min(1.f,float(FellowVisibleRows)/Info.Members.Num()));
	for (int32 i = 0; i < FellowRows.Num(); ++i)
	{
		UTextBlock* Row = FellowRows[i];
		const auto Entry=FellowRowElements[i];
		const int32 Index=i+FellowScrollOffset;
		if (!Row || !Entry) continue;
		if(ListEl && Entry->Parent.Pin()!=ListEl)ListEl->AddChild(Entry);
		Entry->bVisible=ListEl && Info.Members.IsValidIndex(Index) && i<FellowVisibleRows;
		if (!Entry->bVisible)
		{
			Row->SetVisibility(ESlateVisibility::Collapsed);
			for(int32 J=0;J<4;++J)FellowDetailLabels[i*4+J]->SetVisibility(ESlateVisibility::Collapsed);
			FellowRowGuids[i] = 0;
			continue;
		}
		const FACEFellowshipMember& M = Info.Members[Index];
		FellowRowGuids[i] = M.Guid;
		const bool bSel = (M.Guid == SelectedFellowGuid);
		Entry->Y=i*RowH;Entry->Width=ListEl->Width;Entry->DefaultState=bSel?6:1;
		TFunction<TSharedPtr<FACEUIElement>(TSharedPtr<FACEUIElement>,const TCHAR*)> Find;
		Find=[&](TSharedPtr<FACEUIElement> N,const TCHAR* Name)->TSharedPtr<FACEUIElement>
		{if(N->ElementName==Name)return N;for(const auto& C:N->Children)if(auto Found=Find(C,Name))return Found;return nullptr;};
		auto NameEl=Find(Entry,TEXT("FellowName"));auto StatsEl=Find(Entry,TEXT("FellowStats"));
		if(NameEl)NameEl->Width=FMath::Max(1,ListEl->Width-82);
		if(StatsEl)StatsEl->X=FMath::Max(1,ListEl->Width-80);
		PlaceTextOnElement(Row,NameEl,M.Name,10,bSel?SocialGold:SocialWhite,SocialOverlayZ+10);
		Row->SetClipping(EWidgetClipping::ClipToBounds);
		PlaceTextOnElement(FellowDetailLabels[i*4],StatsEl,FString::Printf(TEXT("Level %d"),M.Level),10,SocialWhite,SocialOverlayZ+10);
		const TCHAR* Meters[]={TEXT("FellowHealth"),TEXT("FellowStamina"),TEXT("FellowMana")};
		const TCHAR* Labels[]={TEXT("FellowHealthStats"),TEXT("FellowStaminaStats"),TEXT("FellowManaStats")};
		const int32 Cur[]={M.HealthCur,M.StaminaCur,M.ManaCur},Max[]={M.HealthMax,M.StaminaMax,M.ManaMax};
		for(int32 J=0;J<3;++J)
		{
			auto Meter=Find(Entry,Meters[J]);auto Label=Find(Entry,Labels[J]);
			if(Meter){Meter->X=ListEl->Width*J/3;Meter->Width=ListEl->Width*(J+1)/3-Meter->X;Meter->MeterFillFraction=Max[J]>0?FMath::Clamp(float(Cur[J])/Max[J],0.f,1.f):0.f;for(auto C:Meter->Children)C->Width=Meter->Width;}
			PlaceTextOnElement(FellowDetailLabels[i*4+J+1],Label,FString::Printf(TEXT("%d/%d"),Cur[J],Max[J]),9,SocialWhite,SocialOverlayZ+10);
		}
	}
}

void UACEUIGameplayBinder::RefreshFriendsOverlays()
{
	if (ActivePanelPage != TEXT("SocialPanel_Field") || ActiveSocialTab != TEXT("FriendsPage")
		|| !Client || !Manager || !Canvas)
	{
		for (UTextBlock* R : FriendRows) { if (R) R->SetVisibility(ESlateVisibility::Collapsed); }
		if (FriendNameEntry) { FriendNameEntry->SetVisibility(ESlateVisibility::Collapsed); }
		return;
	}
	EnsureOverlays();
	EnsureSocialEntryBoxes();
	if (FriendNameEntry)
	{
		TSharedPtr<FACEUIElement> EntryEl = Manager->FindElementUnder(
			TEXT("FriendsPage"), TEXT("FriendNameEntryBox"));
		if (!EntryEl.IsValid())
		{
			EntryEl = Manager->FindElementByName(TEXT("FriendNameEntryBox"));
		}
		if (EntryEl.IsValid())
		{
			FriendNameEntry->SetVisibility(ESlateVisibility::Visible);
			Canvas->PlaceWidgetAtElement(FriendNameEntry, EntryEl, SocialOverlayZ + 5, FMargin(3.f, 1.f));
		}
	}
	const TArray<FACEFriendInfo> List = Client->GetFriends();
	TSharedPtr<FACEUIElement> ListEl = Manager->FindElementUnder(
		TEXT("FriendsPage"), TEXT("FriendsListBox"));
	constexpr int32 MaxRows = 20;
	constexpr int32 RowH = 16;
	while (FriendRows.Num() < MaxRows && Canvas->WidgetTree)
	{
		UTextBlock* Row = Canvas->WidgetTree->ConstructWidget<UTextBlock>(UACERetailTextBlock::StaticClass());
		Row->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 8));
		FriendRows.Add(Row);
	}
	FriendRowGuids.SetNum(MaxRows);
	for (int32 i = 0; i < FriendRows.Num(); ++i)
	{
		UTextBlock* Row = FriendRows[i];
		if (!Row)
		{
			continue;
		}
		if (!ListEl.IsValid() || !List.IsValidIndex(i))
		{
			Row->SetVisibility(ESlateVisibility::Collapsed);
			FriendRowGuids[i] = 0;
			continue;
		}
		const FACEFriendInfo& F = List[i];
		FriendRowGuids[i] = F.Guid;
		const bool bSel = (F.Guid == SelectedFriendGuid);
		Row->SetText(FText::FromString(FString::Printf(TEXT("%s  %s"),
			*F.Name, F.bOnline ? TEXT("[online]") : TEXT("[offline]"))));
		Row->SetVisibility(ESlateVisibility::HitTestInvisible);
		Row->SetColorAndOpacity(FSlateColor(bSel ? SocialGold : (F.bOnline ? SocialWhite : SocialDim)));
		const float Top = static_cast<float>(2 + i * RowH);
		Canvas->PlaceWidgetAtElement(Row, ListEl, SocialOverlayZ + 10 + i,
			FMargin(4.f, Top, 4.f, FMath::Max(0.f, static_cast<float>(ListEl->Height) - Top - RowH)));
	}
}

void UACEUIGameplayBinder::RefreshSquelchOverlays()
{
	if (ActivePanelPage != TEXT("SocialPanel_Field") || ActiveSocialTab != TEXT("SquelchPage")
		|| !Client || !Manager || !Canvas)
	{
		for (UTextBlock* R : SquelchRows) { if (R) { R->SetVisibility(ESlateVisibility::Collapsed); } }
		if (SquelchNameEntry) { SquelchNameEntry->SetVisibility(ESlateVisibility::Collapsed); }
		return;
	}
	EnsureOverlays();
	EnsureSocialEntryBoxes();
	if (SquelchNameEntry)
	{
		TSharedPtr<FACEUIElement> EntryEl = Manager->FindElementUnder(
			TEXT("SquelchPage"), TEXT("SquelchNameEntryBox"));
		if (!EntryEl.IsValid())
		{
			EntryEl = Manager->FindElementByName(TEXT("SquelchNameEntryBox"));
		}
		if (EntryEl.IsValid())
		{
			SquelchNameEntry->SetVisibility(ESlateVisibility::Visible);
			Canvas->PlaceWidgetAtElement(SquelchNameEntry, EntryEl, SocialOverlayZ + 5, FMargin(3.f, 1.f));
		}
	}
	const TArray<FACESquelchEntry> List = Client->GetSquelches();
	TSharedPtr<FACEUIElement> ListEl = Manager->FindElementUnder(
		TEXT("SquelchPage"), TEXT("SquelchListBox"));
	constexpr int32 MaxRows = 24;
	constexpr int32 RowH = 16;
	while (SquelchRows.Num() < MaxRows && Canvas->WidgetTree)
	{
		UTextBlock* Row = Canvas->WidgetTree->ConstructWidget<UTextBlock>(UACERetailTextBlock::StaticClass());
		Row->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 8));
		SquelchRows.Add(Row);
	}
	SquelchRowGuids.SetNum(MaxRows);
	SquelchRowNames.SetNum(MaxRows);
	for (int32 i = 0; i < SquelchRows.Num(); ++i)
	{
		UTextBlock* Row = SquelchRows[i];
		if (!Row)
		{
			continue;
		}
		if (!ListEl.IsValid() || !List.IsValidIndex(i))
		{
			Row->SetVisibility(ESlateVisibility::Collapsed);
			SquelchRowGuids[i] = 0;
			SquelchRowNames[i].Reset();
			continue;
		}
		const FACESquelchEntry& E = List[i];
		SquelchRowGuids[i] = E.Guid;
		SquelchRowNames[i] = E.Name;
		const bool bSel = (E.Guid != 0 && E.Guid == SelectedSquelchGuid);
		Row->SetText(FText::FromString(FString::Printf(TEXT("%s%s"),
			*E.Name, E.bAccount ? TEXT("  [account]") : TEXT(""))));
		Row->SetVisibility(ESlateVisibility::HitTestInvisible);
		Row->SetColorAndOpacity(FSlateColor(bSel ? SocialGold : SocialWhite));
		const float Top = static_cast<float>(2 + i * RowH);
		Canvas->PlaceWidgetAtElement(Row, ListEl, SocialOverlayZ + 10 + i,
			FMargin(4.f, Top, 4.f, FMath::Max(0.f, static_cast<float>(ListEl->Height) - Top - RowH)));
	}
	if (List.Num() == 0 && ListEl.IsValid() && SquelchRows.Num() > 0 && SquelchRows[0])
	{
		SquelchRows[0]->SetText(FText::FromString(TEXT("No squelched characters.")));
		SquelchRows[0]->SetColorAndOpacity(FSlateColor(SocialDim));
		SquelchRows[0]->SetVisibility(ESlateVisibility::HitTestInvisible);
		Canvas->PlaceWidgetAtElement(SquelchRows[0], ListEl, SocialOverlayZ + 10, FMargin(4.f, 2.f, 4.f, 2.f));
	}
}

void UACEUIGameplayBinder::RefreshSocialButtonLabels()
{
	if (ActivePanelPage != TEXT("SocialPanel_Field") || !Manager || !Canvas || !Canvas->WidgetTree)
	{
		for (UTextBlock* T : SocialButtonLabels) { if (T) { T->SetVisibility(ESlateVisibility::Collapsed); } }
		for (UBorder* B : SocialCheckboxIcons) { if (B) { B->SetVisibility(ESlateVisibility::Collapsed); } }
		return;
	}
	// Element name → caption. Only elements on the visible page resolve; the rest collapse.
	// bCentered separates DAT buttons (centered caption) from static text labels.
	struct FSocialCaption { const TCHAR* Element; const TCHAR* Text; bool bCentered; };
	if (!bLoadedSocialStrings && Canvas->GetResourceResolver())
		if (const auto* Dat=Canvas->GetResourceResolver()->GetDatSubsystem())
		{
			bLoadedSocialStrings = true;
			SocialStrings.LoadStrings(Dat->GetDatDirectory(), 0x23000001);
		}
	static const FSocialCaption Labels[] = {
		{ TEXT("SwearButton"), TEXT("Swear Allegiance"), true },
		{ TEXT("BreakButton"), TEXT("Break Allegiance"), true },
		{ TEXT("KickButton"), TEXT("Boot Vassal"), true },
		{ TEXT("FellowLeaderButton"), TEXT("Make Leader"), true },
		{ TEXT("FellowQuitButton"), TEXT("Quit"), true },
		{ TEXT("FellowOpenButton"), TEXT("Open"), true },
		{ TEXT("FellowRecruitButton"), TEXT("Recruit"), true },
		{ TEXT("FellowDismissButton"), TEXT("Dismiss"), true },
		{ TEXT("FellowDisbandButton"), TEXT("Disband"), true },
		{ TEXT("CreateFellowshipButton"), TEXT("Create Fellowship"), true },
		{ TEXT("TellButton"), TEXT("Tell"), true },
		{ TEXT("RemoveButton"), TEXT("Remove"), true },
		{ TEXT("AddButton"), TEXT("Add"), true },
		{ TEXT("SquelchRemoveButton"), TEXT("Remove"), true },
		{ TEXT("SquelchCharacterButton"), TEXT("Squelch Character"), true },
		{ TEXT("SquelchAccountButton"), TEXT("Squelch Account"), true },
		{ TEXT("SquelchLabel"), TEXT("Squelched Characters"), false },
		{ TEXT("SquelchNameLabel"), TEXT("Name:"), false },
		{ TEXT("FellowNameLabel"), TEXT("Name"), false },
		{ TEXT("FellowStatsLabel"), TEXT("Stats"), false },
		{ TEXT("MonarchLabel"), TEXT("Monarch:"), false },
		{ TEXT("PatronLabel"), TEXT("Patron:"), false },
	};
	while (SocialButtonLabels.Num() < UE_ARRAY_COUNT(Labels))
	{
		SocialButtonLabels.Add(
			Canvas->WidgetTree->ConstructWidget<UTextBlock>(UACERetailTextBlock::StaticClass()));
	}
	for (int32 i = 0; i < UE_ARRAY_COUNT(Labels); ++i)
	{
		if (UTextBlock* L = SocialButtonLabels[i])
		{
			L->SetJustification(Labels[i].bCentered ? ETextJustify::Center : ETextJustify::Left);
			TSharedPtr<FACEUIElement> El = Manager->FindElementUnder(
				TEXT("SocialPanel_Field"), Labels[i].Element);
			if (!El.IsValid())
			{
				El = Manager->FindElementByName(Labels[i].Element);
			}
			FString Caption=El ? SocialStrings.Strings.FindRef(El->TextEntryId) : FString();
			if (Caption.IsEmpty()) Caption=Labels[i].Text;
			if (FString(Labels[i].Element)==TEXT("FellowOpenButton") && Client && Client->GetFellowship().bOpen) Caption=TEXT("Close");
			PlaceTextOnElement(L, El, Caption, 8,
				Labels[i].bCentered ? SocialWhite : SocialGold,
				SocialOverlayZ + 20, Labels[i].bCentered);
		}
	}

	// Retail checkbox rows on these pages are character options — draw glyph + text.
	struct FSocialCheck { const TCHAR* Element; const TCHAR* Text; int32 Option; };
	static const FSocialCheck Checks[] = {
		{ TEXT("IgnoreAllegianceRequests"), TEXT("Ignore allegiance requests"), 0x01 },
		{ TEXT("IgnoreFellowshipRequests"), TEXT("Ignore fellowship requests"), 0x02 },
		{ TEXT("FellowshipAutoAcceptRequests"), TEXT("Auto-accept fellowship requests"), 0x12 },
		{ TEXT("FellowshipShareXP"), TEXT("Share experience"), 0x0F },
		{ TEXT("FellowshipShareLoot"), TEXT("Share loot"), 0x11 },
		{ TEXT("AppearOffline_Checkbox"), TEXT("Appear offline"), 0x27 },
	};
	constexpr int32 DidSocialCheckOff = 0x06004D15;
	constexpr int32 DidSocialCheckOn = 0x06004D18;
	const int32 LabelBase = UE_ARRAY_COUNT(Labels);
	while (SocialButtonLabels.Num() < LabelBase + UE_ARRAY_COUNT(Checks))
	{
		UTextBlock* L = Canvas->WidgetTree->ConstructWidget<UTextBlock>(UACERetailTextBlock::StaticClass());
		L->SetJustification(ETextJustify::Left);
		SocialButtonLabels.Add(L);
	}
	for (int32 i = 0; i < UE_ARRAY_COUNT(Checks); ++i)
	{
		TSharedPtr<FACEUIElement> El = Manager->FindElementUnder(
			TEXT("SocialPanel_Field"), Checks[i].Element);
		if (!El.IsValid())
		{
			El = Manager->FindElementByName(Checks[i].Element);
		}
		bool bVisible = El.IsValid() && El->bVisible;
		for (TSharedPtr<FACEUIElement> P = El.IsValid() ? El->Parent.Pin() : nullptr;
			bVisible && P.IsValid(); P = P->Parent.Pin())
		{
			if (!P->bVisible) { bVisible = false; }
		}
		UBorder* Icon = EnsureIconBorder(SocialCheckboxIcons, i);
		UTextBlock* Text = SocialButtonLabels.IsValidIndex(LabelBase + i)
			? SocialButtonLabels[LabelBase + i] : nullptr;
		if (!bVisible || !Icon)
		{
			if (Icon) { Icon->SetVisibility(ESlateVisibility::Collapsed); }
			if (Text) { Text->SetVisibility(ESlateVisibility::Collapsed); }
			continue;
		}
		const bool bOn = Client && Client->IsCharacterOptionSet(Checks[i].Option);
		SetIconDid(Icon, bOn ? DidSocialCheckOn : DidSocialCheckOff);
		Icon->SetVisibility(ESlateVisibility::HitTestInvisible);
		if (Icon->GetParent() != Canvas->GetElementLayer())
		{
			Canvas->GetElementLayer()->AddChild(Icon);
		}
		const FIntPoint O = El->GetScreenOrigin();
		if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Icon->Slot))
		{
			Slot->SetAnchors(FAnchors(0.f, 0.f));
			Slot->SetPosition(Canvas->LayoutToViewport(FVector2D(O.X, O.Y + 1)));
			Slot->SetSize(FVector2D(13.f, 13.f)*Canvas->GetLastScale2D());
			Canvas->SetOverlayOrder(Icon, Manager->FindElementByName(TEXT("RootGameplay_FloatyPanel_Field")), SocialOverlayZ + 21);
		}
		if (Text)
		{
			Text->SetText(FText::FromString(Checks[i].Text));
			Text->SetColorAndOpacity(FSlateColor(bOn ? SocialGold : SocialWhite));
			Text->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 8));
			Text->SetVisibility(ESlateVisibility::HitTestInvisible);
			Canvas->PlaceWidgetAtElement(Text, El, SocialOverlayZ + 22, FMargin(16.f, 0.f, 2.f, 0.f));
		}
	}
}

void UACEUIGameplayBinder::RefreshQuestOverlays()
{
	for (UTextBlock* L : QuestTabLabels) if (L) L->SetVisibility(ESlateVisibility::Collapsed);
	for (UTextBlock* L : JournalLabels) if (L) L->SetVisibility(ESlateVisibility::Collapsed);
	for (UEditableTextBox* E : JournalEntries) if (E) E->SetVisibility(ESlateVisibility::Collapsed);
	if (JournalNotes) JournalNotes->SetVisibility(ESlateVisibility::Collapsed);
	if (ActivePanelPage != TEXT("QuestManagementPanel_Field") || !Client || !Manager || !Canvas
		|| !Canvas->WidgetTree)
	{
		for (UTextBlock* R : QuestRows) { if (R) R->SetVisibility(ESlateVisibility::Collapsed); }
		for (UTextBlock* R : QuestStatusRows) { if (R) R->SetVisibility(ESlateVisibility::Collapsed); }
		for (UTextBlock* T : QuestDetailLabels) { if (T) T->SetVisibility(ESlateVisibility::Collapsed); }
		QuestRowIds.Reset();
		return;
	}
	EnsureOverlays();
	if (const auto Panel = Manager->FindElementByName(TEXT("QuestManagementPanel_Field")))
	{
		const TCHAR* Tabs[] = {TEXT("Contracts"), TEXT("Journal"), TEXT("PageList")};
		const TCHAR* Captions[] = {TEXT("Contracts"), TEXT("Journal"), TEXT("Page List")};
		while (QuestTabLabels.Num()<3) QuestTabLabels.Add(Canvas->WidgetTree->ConstructWidget<UACERetailTextBlock>());
		for (int32 I=0; I<3; ++I)
		{
			const FString Page=FString(Tabs[I])+TEXT("Page"), Tab=FString(Tabs[I])+TEXT("Tab");
			Manager->SetElementVisibleByName(Page,ActiveQuestTab==Page);
			Manager->SetElementVisibleByName(Tab,true);
			ApplyPanelTabChrome(Tab,ActiveQuestTab==Page);
			PlaceTextOnElement(QuestTabLabels[I],Tab,Captions[I],9,SocialWhite,660,true);
		}
		if (ActiveQuestTab != TEXT("ContractsPage"))
		{
			for (UTextBlock* R:QuestRows) if (R) R->SetVisibility(ESlateVisibility::Collapsed);
			for (UTextBlock* R:QuestStatusRows) if (R) R->SetVisibility(ESlateVisibility::Collapsed);
			for (UTextBlock* R:QuestDetailLabels) if (R) R->SetVisibility(ESlateVisibility::Collapsed);
			RefreshJournalOverlays(); return;
		}

	}
	UACEDatSubsystem* Dat = nullptr;
	if (PlayerController)
	{
		if (UGameInstance* GI = PlayerController->GetGameInstance())
		{
			Dat = GI->GetSubsystem<UACEDatSubsystem>();
		}
	}
	TArray<FACEContractEntry> Contracts = Client->GetContracts();
	for (UTextBlock* R:QuestStatusRows) if (R) R->SetVisibility(ESlateVisibility::Collapsed);
	auto Status=[](const FACEContractEntry& C)->FString
	{
		if(C.Stage==1)return TEXT("Available");
		if(C.Stage==3)return TEXT("Completed");
		if(C.Stage>=4)return FString::Printf(TEXT("Progress: %d"),C.Stage-4);
		return TEXT("In progress");
	};
	auto ContractName = [Dat](int32 ContractId) -> FString
	{
		if (Dat)
		{
			if (const FACEDatContractInfo* Info = Dat->GetContractInfo(static_cast<uint32>(ContractId)))
			{
				if (!Info->Name.IsEmpty())
				{
					return Info->Name;
				}
			}
		}
		return FString::Printf(TEXT("Contract %d"), ContractId);
	};
	// Retail sorts by the header button you last clicked.
	if (bQuestSortByStatus)
	{
		Contracts.Sort([](const FACEContractEntry& A, const FACEContractEntry& B)
		{
			return A.Stage != B.Stage ? A.Stage > B.Stage : A.ContractId < B.ContractId;
		});
	}
	else
	{
		Contracts.Sort([&ContractName](const FACEContractEntry& A, const FACEContractEntry& B)
		{
			return ContractName(A.ContractId) < ContractName(B.ContractId);
		});
	}

	TSharedPtr<FACEUIElement> ListEl = Manager->FindElementByName(TEXT("ContractsBox"));
	constexpr int32 QuestRowHeight = 20;
	const int32 VisibleRows = ListEl.IsValid()
		? FMath::Max(1, ListEl->Height / QuestRowHeight) : 1;
	QuestScrollOffset = FMath::Clamp(QuestScrollOffset, 0,
		FMath::Max(0, Contracts.Num() - VisibleRows));
	while (QuestRows.Num() < VisibleRows)
	{
		UTextBlock* Row = Canvas->WidgetTree->ConstructWidget<UTextBlock>(UACERetailTextBlock::StaticClass());
		Row->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 9));
		QuestRows.Add(Row);
	}
	QuestRowIds.SetNum(QuestRows.Num());
	while (QuestStatusRows.Num()<QuestRows.Num())
		QuestStatusRows.Add(Canvas->WidgetTree->ConstructWidget<UTextBlock>(UACERetailTextBlock::StaticClass()));
	if (!Contracts.ContainsByPredicate([this](const FACEContractEntry& C){return C.ContractId==SelectedContractId;}))
		SelectedContractId=Contracts.IsEmpty()?0:Contracts[0].ContractId;
	if (Contracts.Num() == 0 && ListEl.IsValid() && QuestRows.Num() > 0 && QuestRows[0])
	{
		QuestRows[0]->SetText(FText::FromString(TEXT("No active contracts.")));
		QuestRows[0]->SetColorAndOpacity(FSlateColor(SocialDim));
		QuestRows[0]->SetVisibility(ESlateVisibility::HitTestInvisible);
		Canvas->PlaceWidgetAtElement(QuestRows[0], ListEl, 640, FMargin(6.f, 4.f, 6.f, 4.f));
		QuestRowIds[0] = 0;
	}
	for (int32 i = (Contracts.Num() == 0 ? 1 : 0); i < QuestRows.Num(); ++i)
	{
		UTextBlock* Row = QuestRows[i];
		if (!Row)
		{
			continue;
		}
		const int32 Index = QuestScrollOffset + i;
		if (!ListEl.IsValid() || i >= VisibleRows || !Contracts.IsValidIndex(Index))
		{
			Row->SetVisibility(ESlateVisibility::Collapsed);
			if (QuestRowIds.IsValidIndex(i)) { QuestRowIds[i] = 0; }
			continue;
		}
		const FACEContractEntry& C = Contracts[Index];
		QuestRowIds[i] = C.ContractId;
		const bool bSel = (C.ContractId == SelectedContractId);
		Row->SetText(FText::FromString(ContractName(C.ContractId)));
		Row->SetColorAndOpacity(FSlateColor(bSel ? SocialGold : SocialWhite));
		Row->SetVisibility(ESlateVisibility::HitTestInvisible);
		Canvas->PlaceWidgetAtElement(Row, ListEl, 640 + i,
			FMargin(6.f, static_cast<float>(2 + i * QuestRowHeight), 96.f,
				FMath::Max(0, ListEl->Height - 2 - (i + 1) * QuestRowHeight)));
		UTextBlock* State=QuestStatusRows[i];
		State->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),9));
		State->SetJustification(ETextJustify::Right);
		State->SetText(FText::FromString(Status(C)));
		State->SetColorAndOpacity(FSlateColor(bSel?SocialGold:SocialWhite));
		State->SetVisibility(ESlateVisibility::HitTestInvisible);
		Canvas->PlaceWidgetAtElement(State,ListEl,640+i,FMargin(FMath::Max(0,ListEl->Width-92),2+i*QuestRowHeight,2,
			FMath::Max(0,ListEl->Height-2-(i+1)*QuestRowHeight)));
	}
	if (TSharedPtr<FACEUIElement> Bar = Manager->FindElementByName(TEXT("ContractsBoxScrollbar")))
	{
		const int32 MaxOffset = FMath::Max(0, Contracts.Num() - VisibleRows);
		const float Frac = MaxOffset > 0
			? static_cast<float>(QuestScrollOffset) / static_cast<float>(MaxOffset) : 0.f;
		const float VisibleFrac = Contracts.Num() > 0
			? FMath::Clamp(static_cast<float>(VisibleRows) / static_cast<float>(Contracts.Num()), 0.05f, 1.f)
			: 1.f;
		SyncDatScrollbar(Bar, Frac, VisibleFrac);
	}

	// Detail pane + static captions. Retail fills all of these from the ContractTable.
	const FACEContractEntry* Sel = Contracts.FindByPredicate([this](const FACEContractEntry& C)
	{
		return C.ContractId == SelectedContractId;
	});
	if (!Sel && Contracts.Num() > 0)
	{
		Sel = &Contracts[0];
		SelectedContractId = Sel->ContractId;
	}
	const FACEDatContractInfo* Info = (Sel && Dat)
		? Dat->GetContractInfo(static_cast<uint32>(Sel->ContractId)) : nullptr;
	auto FormatCell = [](uint32 Cell, const FVector3f& Origin) -> FString
	{
		if (Cell == 0)
		{
			return TEXT("-");
		}
		// Landblock coordinates: retail shows N/S, E/W from the global landblock grid.
		const int32 BlockX = static_cast<int32>((Cell >> 24) & 0xFF);
		const int32 BlockY = static_cast<int32>((Cell >> 16) & 0xFF);
		const float GlobalX = BlockX * 192.f + Origin.X;
		const float GlobalY = BlockY * 192.f + Origin.Y;
		const float NS = GlobalY / 240.f - 102.f;
		const float EW = GlobalX / 240.f - 102.f;
		return FString::Printf(TEXT("%.1f%s, %.1f%s"), FMath::Abs(NS), NS >= 0.f ? TEXT("N") : TEXT("S"),
			FMath::Abs(EW), EW >= 0.f ? TEXT("E") : TEXT("W"));
	};
	const FString StatusText = Sel?Status(*Sel):FString(TEXT("-"));
	auto Remaining=[Sel](float Seconds)
	{
		const double Elapsed=Sel&&Sel->ReceivedAt>0?FPlatformTime::Seconds()-Sel->ReceivedAt:0;
		const int32 S=FMath::Max(0,FMath::CeilToInt(Seconds-Elapsed));
		return FString::Printf(TEXT("%dd %02d:%02d:%02d"),S/86400,(S/3600)%24,(S/60)%60,S%60);
	};
	FString TimedText = TEXT("Not timed");
	if (Sel && Sel->TimeWhenDone > 0.f)
	{
		TimedText = FString(TEXT("Time left: "))+Remaining(Sel->TimeWhenDone);
	}
	else if (Sel && Sel->TimeWhenRepeats > 0.f)
	{
		TimedText = FString(TEXT("Available in: "))+Remaining(Sel->TimeWhenRepeats);
	}
	const FString Notes = Info
		? (Sel && (Sel->Stage == 2 || Sel->Stage >= 4) && !Info->DescriptionProgress.IsEmpty()
			? Info->DescriptionProgress : Info->Description)
		: FString();
	struct FQuestCaption { const TCHAR* Element; FString Text; bool bGold; };
	const FQuestCaption Captions[] = {
		{ TEXT("ContractNameSortButton"), FString(TEXT("Contract")), false },
		{ TEXT("ContractStatusSortButton"), FString(TEXT("Status")), false },
		{ TEXT("ContractStatusLabel"), FString(TEXT("Status:")), true },
		{ TEXT("ContractStatusText"), StatusText, false },
		{ TEXT("ContractContactLabel"), FString(TEXT("Contact:")), true },
		{ TEXT("ContractContactText"), Info && !Info->NameNPCStart.IsEmpty()
			? Info->NameNPCStart : FString(TEXT("-")), false },
		{ TEXT("ContractContactLocLabel"), FString(TEXT("Contact location:")), true },
		{ TEXT("ContractContactLocText"), Info
			? FormatCell(Info->CellNPCStart, Info->OriginNPCStart) : FString(TEXT("-")), false },
		{ TEXT("ContractAreaLabel"), FString(TEXT("Quest area:")), true },
		{ TEXT("ContractAreaText"), Info
			? FormatCell(Info->CellQuestArea, Info->OriginQuestArea) : FString(TEXT("-")), false },
		{ TEXT("ContractNotesLabel"), Notes, false },
		{ TEXT("ContractTimedLabel"), FString(TEXT("Timing:")), true },
		{ TEXT("ContractTimedText"), TimedText, false },
		{ TEXT("ContractAbandonButton"), FString(TEXT("Abandon")), false },
	};
	while (QuestDetailLabels.Num() < UE_ARRAY_COUNT(Captions))
	{
		QuestDetailLabels.Add(
			Canvas->WidgetTree->ConstructWidget<UTextBlock>(UACERetailTextBlock::StaticClass()));
	}
	for (int32 i = 0; i < UE_ARRAY_COUNT(Captions); ++i)
	{
		UTextBlock* Label = QuestDetailLabels[i];
		if (!Label)
		{
			continue;
		}
		const bool bButton = FCString::Strstr(Captions[i].Element, TEXT("Button")) != nullptr;
		const bool bWrap = FCString::Strcmp(Captions[i].Element, TEXT("ContractNotesLabel")) == 0;
		Label->SetJustification(bButton ? ETextJustify::Center : ETextJustify::Left);
		Label->SetAutoWrapText(bWrap);
		PlaceTextOnElement(Label, Captions[i].Element, Captions[i].Text, bWrap ? 8 : 9,
			Captions[i].bGold ? SocialGold : SocialWhite, 660 + i, bButton);
	}
}

void UACEUIGameplayBinder::ShowSalvagePanel(int32 ToolGuid)
{
	if (ToolGuid == 0)
	{
		return;
	}
	OpenSalvageToolGuid = ToolGuid;
	SalvageMaterialType = 0;
	SalvageQueueGuids.Reset();
	OpenLootContainerGuid = 0;
	OpenVendorGuid = 0;
	bTradeOpen = false;
	ExpandEnvFloatyForWideContent();
	SyncEnvPanelMode();
	if (Manager)
	{
		if (TSharedPtr<FACEUIElement> Env = Manager->FindElementByName(TEXT("RootGameplay_FloatyEnvPanel_Field")))
		{
			Manager->BringFloatyToFront(Env);
		}
		Manager->SetElementVisibleByName(TEXT("SalvagePanel"), true);
	}
	RefreshSalvageOverlays();
	PostInventorySystemMessage(TEXT("Drag salvageable items onto the Ust panel, then click Salvage."));
}

void UACEUIGameplayBinder::HideSalvagePanel()
{
	OpenSalvageToolGuid = 0;
	SalvageMaterialType = 0;
	SalvageQueueGuids.Reset();
	for (UBorder* B : SalvageItemSlots) { if (B) B->SetVisibility(ESlateVisibility::Collapsed); }
	if (SalvageWarningLabel) { SalvageWarningLabel->SetVisibility(ESlateVisibility::Collapsed); }
	SyncEnvPanelMode();
}

bool UACEUIGameplayBinder::AddItemToSalvageQueue(int32 Guid)
{
	if (!Client || Guid == 0 || OpenSalvageToolGuid == 0 || Guid == OpenSalvageToolGuid)
	{
		return false;
	}
	if (SalvageQueueGuids.Contains(Guid))
	{
		return true;
	}
	FACEWorldObject Obj;
	if (!Client->GetWorldObject(Guid, Obj))
	{
		PostInventorySystemMessage(TEXT("That item cannot be salvaged."));
		return false;
	}
	if (Obj.MaterialType == 0)
	{
		PostInventorySystemMessage(TEXT("That item has no salvageable material."));
		return false;
	}
	if (Obj.Structure >= 100)
	{
		PostInventorySystemMessage(TEXT("That item is already fully salvaged."));
		return false;
	}
	if (SalvageMaterialType != 0 && Obj.MaterialType != SalvageMaterialType)
	{
		PostInventorySystemMessage(TEXT("You can only salvage one material type at a time."));
		return false;
	}
	if (SalvageMaterialType == 0)
	{
		SalvageMaterialType = Obj.MaterialType;
	}
	SalvageQueueGuids.Add(Guid);
	RefreshSalvageOverlays();
	return true;
}

void UACEUIGameplayBinder::RemoveItemFromSalvageQueue(int32 Guid)
{
	SalvageQueueGuids.Remove(Guid);
	if (SalvageQueueGuids.Num() == 0)
	{
		SalvageMaterialType = 0;
	}
	RefreshSalvageOverlays();
}

void UACEUIGameplayBinder::SubmitSalvageQueue()
{
	if (!Client || OpenSalvageToolGuid == 0 || SalvageQueueGuids.Num() == 0)
	{
		return;
	}
	Client->SendCreateTinkeringTool(OpenSalvageToolGuid, SalvageQueueGuids);
	SalvageQueueGuids.Reset();
	SalvageMaterialType = 0;
	RefreshSalvageOverlays();
}

void UACEUIGameplayBinder::RefreshSalvageOverlays()
{
	if (!Client || !Manager || !Canvas || OpenSalvageToolGuid == 0)
	{
		for (UBorder* B : SalvageItemSlots) { if (B) B->SetVisibility(ESlateVisibility::Collapsed); }
		if (SalvageWarningLabel) { SalvageWarningLabel->SetVisibility(ESlateVisibility::Collapsed); }
		return;
	}
	SyncEnvPanelMode();
	EnsureOverlays();
	TSharedPtr<FACEUIElement> ListEl = Manager->FindElementByName(TEXT("SalvageItemsList"));
	if (!ListEl.IsValid())
	{
		return;
	}
	constexpr int32 Cell = 32;
	constexpr int32 OverlayZ = 120000;
	const int32 Cols = FMath::Max(1, ListEl->Width / Cell);
	const int32 Rows = FMath::Max(1, ListEl->Height / Cell);
	const int32 PageSize = Cols * Rows;
	const FIntPoint Origin = ListEl->GetScreenOrigin();
	SalvageItemSlotGuids.SetNum(PageSize);
	for (int32 i = 0; i < PageSize; ++i)
	{
		UBorder* Icon = EnsureIconBorder(SalvageItemSlots, i);
		if (!Icon)
		{
			continue;
		}
		if (!SalvageQueueGuids.IsValidIndex(i))
		{
			Icon->SetVisibility(ESlateVisibility::Collapsed);
			SalvageItemSlotGuids[i] = 0;
			continue;
		}
		const int32 Guid = SalvageQueueGuids[i];
		FACEWorldObject Obj;
		if (!Client->GetWorldObject(Guid, Obj))
		{
			Icon->SetVisibility(ESlateVisibility::Collapsed);
			SalvageItemSlotGuids[i] = 0;
			continue;
		}
		SalvageItemSlotGuids[i] = Guid;
		SetIconDid(Icon, Obj.IconId);
		Icon->SetVisibility(ESlateVisibility::Visible);
		Icon->SetToolTipText(FText::FromString(Obj.Name));
		const int32 Col = i % Cols;
		const int32 Row = i / Cols;
		if (Icon->GetParent() != Canvas->GetElementLayer())
		{
			Canvas->GetElementLayer()->AddChild(Icon);
		}
		if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Icon->Slot))
		{
			Slot->SetAnchors(FAnchors(0.f, 0.f));
			Slot->SetPosition(FVector2D(static_cast<float>(Origin.X + Col * Cell),
				static_cast<float>(Origin.Y + Row * Cell)));
			Slot->SetSize(FVector2D(static_cast<float>(Cell), static_cast<float>(Cell)));
			Canvas->SetOverlayOrder(Icon, Manager->FindElementByName(TEXT("RootGameplay_FloatyEnvPanel_Field")), OverlayZ);
		}
	}
	if (!SalvageWarningLabel && Canvas->WidgetTree)
	{
		SalvageWarningLabel = Canvas->WidgetTree->ConstructWidget<UTextBlock>(UACERetailTextBlock::StaticClass());
		FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 9);
		SalvageWarningLabel->SetFont(Font);
	}
	if (SalvageWarningLabel)
	{
		const FString Warn = SalvageQueueGuids.Num() == 0
			? TEXT("Add items to salvage.")
			: FString::Printf(TEXT("%d item(s) ready"), SalvageQueueGuids.Num());
		SalvageWarningLabel->SetText(FText::FromString(Warn));
		SalvageWarningLabel->SetVisibility(ESlateVisibility::HitTestInvisible);
		SalvageWarningLabel->SetColorAndOpacity(FSlateColor(SocialGold));
		PlaceTextOnElement(SalvageWarningLabel, TEXT("SalvageWarning_Text"), Warn, 9, SocialGold, 10010);
	}
	Manager->SetElementVisibleByName(TEXT("Salvage_Button"), SalvageQueueGuids.Num() > 0);
}
