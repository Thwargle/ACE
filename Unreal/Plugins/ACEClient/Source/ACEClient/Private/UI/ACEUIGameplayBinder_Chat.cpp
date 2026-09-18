#include "ACERetailChat.h"
#include "UI/ACERetailTextBlock.h"
#include "UI/ACEUIGameplayBinder.h"
#include "UI/ACEChatEntry.h"
#include "UI/ACEUICanvasWidget.h"
#include "Components/CanvasPanel.h"
#include "UI/ACEUIElementManager.h"
#include "UI/ACEUILayoutResolver.h"
#include "UI/ACEUIResourceResolver.h"
#include "ACEClientSubsystem.h"
#include "ACEOpcodes.h"
#include "Components/Border.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/EditableTextBox.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Blueprint/WidgetTree.h"
#include "Styling/CoreStyle.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "ACEDatSubsystem.h"
#include "ACEPlayerController.h"
#include "VR/ACEVRComponent.h"
#include "ACESession.h"
#include "Framework/Application/SlateApplication.h"

namespace
{
	constexpr int32 ChatDestCount = 14;
	const TCHAR* ChatDestLabels[ChatDestCount] = {
		TEXT("Say"), TEXT("Fellow"), TEXT("Allegiance"),
		TEXT("Vassals"), TEXT("Patron"), TEXT("Monarch"), TEXT("Co-Vassals"),
		TEXT("General"), TEXT("Trade"), TEXT("LFG"), TEXT("Roleplay"), TEXT("Society"), TEXT("Selected"), TEXT("Olthoi")
	};
	// gmMainChatUI::InitTalkFocusMenu insertion order. -1 is the squelch action.
	constexpr int32 ChatMenuChannels[] = {-1,5,12,4,0,3,1,2,7,8,9,10,11,13};
	const TCHAR* ChatMenuLabels[] = {TEXT("Squelch selected"),TEXT("Tell to monarch"),TEXT("Tell to selected"),
		TEXT("Tell to patron"),TEXT("Say"),TEXT("Tell to vassals"),TEXT("Tell to fellows"),TEXT("Tell to allegiance"),
		TEXT("General"),TEXT("Trade"),TEXT("LFG"),TEXT("Roleplay"),TEXT("Society"),TEXT("Olthoi")};
	constexpr int32 ChatMenuCount = UE_ARRAY_COUNT(ChatMenuChannels);
	const FLinearColor ChatGold(0.95f, 0.82f, 0.35f, 1.f);
	const FLinearColor ChatWhite(0.92f, 0.92f, 0.88f, 1.f);

	bool SplitCommand(const FString& Raw, FString& OutCmd, FString& OutArgs)
	{
		FString Msg = Raw;
		Msg.ReplaceInline(TEXT("\uFEFF"), TEXT(""));
		Msg.ReplaceInline(TEXT("\u200B"), TEXT(""));
		Msg.TrimStartAndEndInline();
		if (Msg.IsEmpty())
		{
			return false;
		}
		// Retail uses @; accept / and fullwidth ＠ as aliases.
		if (Msg.StartsWith(TEXT("/")) || Msg.StartsWith(TEXT("@")) || Msg.StartsWith(TEXT("\uFF20")))
		{
			Msg = Msg.Mid(1).TrimStartAndEnd();
		}
		else if (Msg.StartsWith(TEXT(":")) || Msg.StartsWith(TEXT(";")))
		{
			OutCmd = TEXT("e");
			OutArgs = Msg.Mid(1).TrimStartAndEnd();
			return true;
		}
		else
		{
			return false;
		}
		int32 Boundary = 0;
		while (Boundary < Msg.Len() && !FChar::IsWhitespace(Msg[Boundary])) ++Boundary;
		OutCmd = Msg.Left(Boundary).ToLower();
		OutArgs = Msg.Mid(Boundary).TrimStartAndEnd();
		return !OutCmd.IsEmpty();
	}
}

FString UACEUIGameplayBinder::ChatSendChannelLabel(int32 ChannelIndex)
{
	const int32 i = FMath::Clamp(ChannelIndex, 0, ChatDestCount - 1);
	return ChatDestLabels[i];
}

uint32 UACEUIGameplayBinder::ChatSendChannelId(int32 ChannelIndex)
{
	switch (FMath::Clamp(ChannelIndex, 0, ChatDestCount - 1))
	{
	case 1: return ACEChatChannel::Fellow;
	case 2: return ACEChatChannel::AllegianceBroadcast;
	case 3: return ACEChatChannel::Vassals;
	case 4: return ACEChatChannel::Patron;
	case 5: return ACEChatChannel::Monarch;
	case 6: return ACEChatChannel::CoVassals;
	default: return 0;
	}
}

bool UACEUIGameplayBinder::ChatSendIsTurbine(int32 ChannelIndex)
{
	return (ChannelIndex >= 7 && ChannelIndex <= 11) || ChannelIndex == 13;
}

uint32 UACEUIGameplayBinder::ChatSendTurbineChatType(int32 ChannelIndex)
{
	switch (ChannelIndex)
	{
	case 7: return ACETurbineChat::General;
	case 8: return ACETurbineChat::Trade;
	case 9: return ACETurbineChat::LFG;
	case 10: return ACETurbineChat::Roleplay;
	case 11: return ACETurbineChat::Society;
	case 13: return ACETurbineChat::Olthoi;
	default: return ACETurbineChat::General;
	}
}

namespace
{
	int32 TurbineListenOption(uint32 ChatType)
	{
		switch (ChatType)
		{
		case ACETurbineChat::Allegiance: return 0x1B;
		case ACETurbineChat::General: return 0x23;
		case ACETurbineChat::Trade: return 0x24;
		case ACETurbineChat::LFG: return 0x25;
		case ACETurbineChat::Roleplay: return 0x26;
		case ACETurbineChat::Society:
		case ACETurbineChat::SocietyCelestialHand:
		case ACETurbineChat::SocietyEldrytchWeb:
		case ACETurbineChat::SocietyRadiantBlood:
			return 0x2E;
		default: return INDEX_NONE;
		}
	}

	const TCHAR* TurbineChannelName(uint32 ChatType)
	{
		switch (ChatType)
		{
		case ACETurbineChat::Allegiance: return TEXT("Allegiance");
		case ACETurbineChat::Trade: return TEXT("Trade");
		case ACETurbineChat::LFG: return TEXT("LFG");
		case ACETurbineChat::Roleplay: return TEXT("Roleplay");
		case ACETurbineChat::Society:
		case ACETurbineChat::SocietyCelestialHand:
		case ACETurbineChat::SocietyEldrytchWeb:
		case ACETurbineChat::SocietyRadiantBlood:
			return TEXT("Society");
		case ACETurbineChat::Olthoi: return TEXT("Olthoi");
		default: return TEXT("General");
		}
	}
}

void UACEUIGameplayBinder::SetChatSendChannel(int32 ChannelIndex)
{
	ChatSendChannel = FMath::Clamp(ChannelIndex, 0, ChatDestCount - 1);
	CloseChatTargetPopup();
	RefreshChatChromeOverlays();
}

void UACEUIGameplayBinder::CycleChatFilterMode()
{
	ChatFilterMode = (ChatFilterMode + 1) % 4;
	static const TCHAR* Names[] = { TEXT("All"), TEXT("Speech"), TEXT("Combat"), TEXT("System") };
	AppendLocalChatLine(FString::Printf(TEXT("Chat filter: %s"), Names[ChatFilterMode]),
		ACEChatMessageType::System);
}

bool UACEUIGameplayBinder::PassesChatFilter(int32 ChatType) const
{
	switch (ChatFilterMode)
	{
	case 1: // Speech
		return ChatType == ACEChatMessageType::Speech
			|| ChatType == ACEChatMessageType::Tell
			|| ChatType == ACEChatMessageType::OutgoingTell
			|| ChatType == ACEChatMessageType::Channel
			|| ACEChatMessageType::IsGlobalChannel(ChatType);
	case 2: // Combat
		return ChatType == ACEChatMessageType::Combat
			|| ChatType == ACEChatMessageType::CombatSelf
			|| ChatType == ACEChatMessageType::CombatEnemy
			|| ChatType == ACEChatMessageType::Magic;
	case 3: // System
		return ChatType == ACEChatMessageType::System
			|| ChatType == ACEChatMessageType::Broadcast;
	default:
		return true;
	}
}

void UACEUIGameplayBinder::AppendLocalChatLine(const FString& Line, int32 ChatType)
{
	HandleChatMessage(Line, FString(), ChatType);
}

UEditableTextBox* UACEUIGameplayBinder::GetChatEntryWidget(int32 Window) const
{
	if (Window <= 0)
	{
		return ChatEntry;
	}
	return FloatyChatEntries.IsValidIndex(Window - 1) ? FloatyChatEntries[Window - 1].Get() : nullptr;
}

UScrollBox* UACEUIGameplayBinder::GetChatLogWidget(int32 Window) const
{
	if (Window <= 0)
	{
		return ChatLog;
	}
	return FloatyChatLogs.IsValidIndex(Window - 1) ? FloatyChatLogs[Window - 1].Get() : nullptr;
}

void UACEUIGameplayBinder::FocusChatEntryWindow(int32 Window)
{
	if (UEditableTextBox* Entry = GetChatEntryWidget(Window))
	{
		if (PlayerController && PlayerController->IsVRActive()) PlayerController->GetVRComponent()->FocusTextEntry(Entry);
		else Entry->SetKeyboardFocus();
	}
}

void UACEUIGameplayBinder::ClearChatLog(int32 Window)
{
	if (UScrollBox* Log = GetChatLogWidget(Window))
	{
		Log->ClearChildren();
	}
	if (Window <= 0)
	{
		ChatLineCount = 0;
		bChatStickToBottom = true;
		ChatStickEndOffset = -1.f;
		SyncChatScrollbar();
		SyncChatJumpIndicator();
	}
	else if (FloatyChatLineCounts.IsValidIndex(Window - 1))
	{
		FloatyChatLineCounts[Window - 1] = 0;
	}
	ChatRowSenders.RemoveAll([Window](const FChatRowSender& E)
	{
		return E.Window == Window || !E.Row.IsValid();
	});
}

void UACEUIGameplayBinder::NavigateChatHistory(bool bPrevious)
{
	// History recall applies to whichever entry line has focus (main or floaty).
	UEditableTextBox* Entry = nullptr;
	for (int32 W = 0; W <= NumFloatyChats; ++W)
	{
		UEditableTextBox* Candidate = GetChatEntryWidget(W);
		const auto Slate = Candidate ? Candidate->GetCachedWidget() : nullptr;
		if (Slate && (Slate->HasAnyUserFocus() || Slate->HasFocusedDescendants()))
		{
			Entry = Candidate;
			break;
		}
	}
	if (!Entry)
	{
		Entry = ChatEntry;
	}
	if (auto* Chat = Cast<UACEChatEntry>(Entry)) Chat->NavigateHistory(bPrevious);
}

FString UACEUIGameplayBinder::ExpandChatReply(const FString& Text) const
{
	if (!Client) return Text;
	FString Input = Text.TrimStart();
	if (!Input.StartsWith(TEXT("/")) && !Input.StartsWith(TEXT("@"))) return Text;
	int32 Boundary = 1;
	while (Boundary < Input.Len() && !FChar::IsWhitespace(Input[Boundary])) ++Boundary;
	if (Boundary == Input.Len()) return Text; // Do not expand while typing /reply.
	const FString Cmd = Input.Mid(1, Boundary - 1).ToLower();
	FString Name;
	if (Cmd == TEXT("r") || Cmd == TEXT("rp") || Cmd == TEXT("reply")) Name = Client->GetLastTellSenderName();
	else if (const auto Session = Client->GetSession(); Session)
	{
		if (Cmd == TEXT("mr")) Name = Session->GetLastMonarchTellSenderName();
		else if (Cmd == TEXT("pr")) Name = Session->GetLastPatronTellSenderName();
	}
	// ChatInterface::HandleTextReplacements preserves the message after the
	// shortcut and locks the recipient into an ordinary, editable name tell.
	return Name.IsEmpty() ? Text : TEXT("@tell ") + Name + TEXT(", ") + Input.Mid(Boundary + 1);
}

void UACEUIGameplayBinder::LoadFloatyChatSettings()
{
	FloatyChatLogs.SetNum(NumFloatyChats);
	FloatyChatEntries.SetNum(NumFloatyChats);
	FloatyChatTitleLabels.SetNum(NumFloatyChats);
	FloatyChatLineCounts.SetNum(NumFloatyChats);
	FloatyChatTitles.SetNum(NumFloatyChats);
	FloatyChatFilters.SetNum(NumFloatyChats);
	const TCHAR* Section = TEXT("ACEClient.DatHUD");
	GConfig->GetFloat(Section,TEXT("ChatInactiveOpacity"),ChatInactiveOpacity,GGameUserSettingsIni);
	GConfig->GetFloat(Section,TEXT("ChatActiveOpacity"),ChatActiveOpacity,GGameUserSettingsIni);
	ChatInactiveOpacity=FMath::Clamp(ChatInactiveOpacity,0.f,1.f);
	ChatActiveOpacity=FMath::Clamp(ChatActiveOpacity,ChatInactiveOpacity,1.f);
	FString Value;
	for (int32 W = 0; W < NumFloatyChats; ++W)
	{
		// Retail defaults differ for each auxiliary window.
		FloatyChatTitles[W] = FString::Printf(TEXT("Chat Window %d"), W + 1);
		FloatyChatFilters[W] = ACERetailChat::FloatyDefaults[W];
		if (GConfig->GetString(Section, *FString::Printf(TEXT("Chat%d_Title"), W + 1),
			Value, GGameUserSettingsIni) && !Value.IsEmpty())
		{
			FloatyChatTitles[W] = Value;
		}
		if (GConfig->GetString(Section, *FString::Printf(TEXT("Chat%d_Filter"), W + 1),
			Value, GGameUserSettingsIni) && !Value.IsEmpty())
		{
			FloatyChatFilters[W] = FCString::Strtoui64(*Value, nullptr, 10);
		}
	}
	MainChatTypeFilter = ACERetailChat::MainWindowDefault;
	if (GConfig->GetString(Section, TEXT("MainChatFilter"), Value, GGameUserSettingsIni))
		MainChatTypeFilter = FCString::Strtoui64(*Value, nullptr, 10);
	if (GConfig->GetString(Section, TEXT("GlobalChatFilter"), Value, GGameUserSettingsIni)
		&& !Value.IsEmpty())
	{
		GlobalChatTypeFilter = FCString::Strtoui64(*Value, nullptr, 10);
	}
}

void UACEUIGameplayBinder::SaveFloatyChatSettings()
{
	const TCHAR* Section = TEXT("ACEClient.DatHUD");
	GConfig->SetFloat(Section,TEXT("ChatInactiveOpacity"),ChatInactiveOpacity,GGameUserSettingsIni);
	GConfig->SetFloat(Section,TEXT("ChatActiveOpacity"),ChatActiveOpacity,GGameUserSettingsIni);
	for (int32 W = 0; W < NumFloatyChats; ++W)
	{
		if (!FloatyChatTitles.IsValidIndex(W))
		{
			continue;
		}
		GConfig->SetString(Section, *FString::Printf(TEXT("Chat%d_Title"), W + 1),
			*FloatyChatTitles[W], GGameUserSettingsIni);
		GConfig->SetString(Section, *FString::Printf(TEXT("Chat%d_Filter"), W + 1),
			*FString::Printf(TEXT("%llu"), FloatyChatFilters[W]), GGameUserSettingsIni);
	}
	GConfig->SetString(Section, TEXT("GlobalChatFilter"),
		*FString::Printf(TEXT("%llu"), GlobalChatTypeFilter), GGameUserSettingsIni);
	GConfig->SetString(Section, TEXT("MainChatFilter"),
		*FString::Printf(TEXT("%llu"), MainChatTypeFilter), GGameUserSettingsIni);
	GConfig->Flush(false, GGameUserSettingsIni);
}

void UACEUIGameplayBinder::ChangeInactiveChatOpacity(float Value)
{
	ChatInactiveOpacity=FMath::Clamp(Value,0.f,1.f);
	ChatActiveOpacity=FMath::Max(ChatActiveOpacity,ChatInactiveOpacity);
	SaveFloatyChatSettings(); RefreshChatOpacity();
}

void UACEUIGameplayBinder::ChangeActiveChatOpacity(float Value)
{
	ChatActiveOpacity=FMath::Clamp(Value,0.f,1.f);
	ChatInactiveOpacity=FMath::Min(ChatInactiveOpacity,ChatActiveOpacity);
	SaveFloatyChatSettings(); RefreshChatOpacity();
}

void UACEUIGameplayBinder::RefreshChatOpacity()
{
	if (!Canvas || !Manager || !FSlateApplication::IsInitialized()) return;
	const FVector2D Pointer=Canvas->GetCachedGeometry().AbsoluteToLocal(FSlateApplication::Get().GetCursorPos())/Canvas->GetLastScale2D();
	for (int32 W=0;W<=NumFloatyChats;++W)
	{
		const FString Name=W ? FString::Printf(TEXT("RootGameplay_FloatyChat%d_Field"),W) : FString(TEXT("RootGameplay_FloatyMainChat_Field"));
		const auto Root=Manager->FindElementByName(Name);
		if (!Root) continue;
		const auto Origin=Root->GetScreenOrigin();
		const auto* Entry=GetChatEntryWidget(W);
		const bool bActive=(Entry && Entry->HasKeyboardFocus()) || (Pointer.X>=Origin.X && Pointer.X<Origin.X+Root->Width
			&& Pointer.Y>=Origin.Y && Pointer.Y<Origin.Y+Root->GetLayoutHeight());
		Root->ImageOpacity=bActive ? ChatActiveOpacity : ChatInactiveOpacity;
	}
}

void UACEUIGameplayBinder::SendFloatyChatCommitted(int32 Window, const FText& Text,
	ETextCommit::Type CommitMethod)
{
	if (CommitMethod != ETextCommit::OnEnter)
	{
		return;
	}
	const FString Committed = Text.ToString();
	TrySendChatFromEntry(&Committed, Window);
}

void UACEUIGameplayBinder::HandleFloatyChat1Committed(const FText& Text, ETextCommit::Type CommitMethod)
{
	SendFloatyChatCommitted(1, Text, CommitMethod);
}

void UACEUIGameplayBinder::HandleFloatyChat2Committed(const FText& Text, ETextCommit::Type CommitMethod)
{
	SendFloatyChatCommitted(2, Text, CommitMethod);
}

void UACEUIGameplayBinder::HandleFloatyChat3Committed(const FText& Text, ETextCommit::Type CommitMethod)
{
	SendFloatyChatCommitted(3, Text, CommitMethod);
}

void UACEUIGameplayBinder::HandleFloatyChat4Committed(const FText& Text, ETextCommit::Type CommitMethod)
{
	SendFloatyChatCommitted(4, Text, CommitMethod);
}

namespace
{
	/** Retail LogTextTypeEnumMapper names (underscores dropped for matching). */
	struct FChatTypeName
	{
		const TCHAR* Name;
		int32 Type;
	};
	const FChatTypeName GChatTypeNames[] = {
		{ TEXT("Default"), ACEChatMessageType::Broadcast },
		{ TEXT("AllChannels"), ACEChatMessageType::AllChannels },
		{ TEXT("Speech"), ACEChatMessageType::Speech },
		{ TEXT("Tell"), ACEChatMessageType::Tell },
		{ TEXT("SpeechDirectSend"), ACEChatMessageType::OutgoingTell },
		{ TEXT("System"), ACEChatMessageType::System },
		{ TEXT("Combat"), ACEChatMessageType::Combat },
		{ TEXT("Magic"), ACEChatMessageType::Magic },
		{ TEXT("Channel"), ACEChatMessageType::Channel },
		{ TEXT("ChannelSend"), ACEChatMessageType::ChannelSend },
		{ TEXT("Social"), ACEChatMessageType::Social },
		{ TEXT("SocialSend"), ACEChatMessageType::SocialSend },
		{ TEXT("Emote"), ACEChatMessageType::Emote },
		{ TEXT("Advancement"), ACEChatMessageType::Advancement },
		{ TEXT("Abuse"), ACEChatMessageType::Abuse },
		{ TEXT("Help"), ACEChatMessageType::Help },
		{ TEXT("Appraisal"), ACEChatMessageType::Appraisal },
		{ TEXT("Spellcasting"), ACEChatMessageType::Spellcasting },
		{ TEXT("Allegiance"), ACEChatMessageType::Allegiance },
		{ TEXT("Fellowship"), ACEChatMessageType::Fellowship },
		{ TEXT("WorldBroadcast"), ACEChatMessageType::WorldBroadcast },
		{ TEXT("CombatEnemy"), ACEChatMessageType::CombatEnemy },
		{ TEXT("CombatSelf"), ACEChatMessageType::CombatSelf },
		{ TEXT("Recall"), ACEChatMessageType::Recall },
		{ TEXT("Craft"), ACEChatMessageType::Craft },
		{ TEXT("Salvaging"), ACEChatMessageType::Salvaging },
		{ TEXT("AdminTell"), ACEChatMessageType::AdminTell },
	};

	FString NormalizeChatTypeToken(const FString& In)
	{
		FString S = In;
		S.ReplaceInline(TEXT("-"), TEXT(""));
		S.ReplaceInline(TEXT("_"), TEXT(""));
		return S.ToLower();
	}
}

bool UACEUIGameplayBinder::ChatTypeFromName(const FString& Name, int32& OutType)
{
	const FString Key = NormalizeChatTypeToken(Name);
	if (Key == TEXT("all")) { OutType = ACEChatMessageType::AllChannels; return true; }
	for (const FChatTypeName& Entry : GChatTypeNames)
	{
		if (Key == NormalizeChatTypeToken(Entry.Name))
		{
			OutType = Entry.Type;
			return true;
		}
	}
	return false;
}

FString UACEUIGameplayBinder::ChatTypeNames()
{
	FString Out;
	for (const FChatTypeName& Entry : GChatTypeNames)
	{
		if (!Out.IsEmpty())
		{
			Out += TEXT(", ");
		}
		Out += Entry.Name;
	}
	return Out;
}

void UACEUIGameplayBinder::RefreshChatRowLayout(int32 Window)
{
	if (!Canvas || !Manager || Window < 0 || Window > NumFloatyChats) return;
	auto* Log = GetChatLogWidget(Window);
	const FString Root = Window ? FString::Printf(TEXT("RootGameplay_FloatyChat%d_Field"), Window)
		: TEXT("RootGameplay_FloatyMainChat_Field");
	const auto LogEl = Manager->FindElementUnder(Root, TEXT("ChatLog"));
	if (!Log || !LogEl) return;
	const float Width = FMath::Max(1.f, float(LogEl->Width) - (Window ? 4.f : 18.f));
	const FVector2D Scale = Canvas->GetLastScale2D();
	if (ChatRowWidths[Window] == Width && ChatRowScales[Window] == Scale) return;
	ChatRowWidths[Window] = Width;
	ChatRowScales[Window] = Scale;
	// Offscreen ScrollBox children do not tick. Update their wrap constraint too,
	// so total content height and the proportional thumb reflect the new width.
	for (auto* Child : Log->GetAllChildren())
		if (auto* Row = Cast<UACERetailTextBlock>(Child))
			Row->SetRetailElement(Canvas->GetResourceResolver(), nullptr, Scale, Width, false);
}

void UACEUIGameplayBinder::AppendChatLineToLog(int32 Window, const FString& Line,
	const FLinearColor& Color, const FString& ClickSender)
{
	UScrollBox* Log = GetChatLogWidget(Window);
	if (!Log || !Canvas || !Canvas->WidgetTree)
	{
		return;
	}
	UTextBlock* Row = Canvas->WidgetTree->ConstructWidget<UTextBlock>(UACERetailTextBlock::StaticClass());
	Row->SetText(FText::FromString(Line));
	Row->SetAutoWrapText(true);
	Row->SetColorAndOpacity(FSlateColor(Color));
	Row->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 9));
	RefreshChatRowLayout(Window);
	CastChecked<UACERetailTextBlock>(Row)->SetRetailElement(Canvas->GetResourceResolver(), nullptr,
		ChatRowScales[Window], ChatRowWidths[Window], false);
	Log->AddChild(Row);
	if (Window <= 0)
	{
		++ChatLineCount;
		while (ChatLineCount > MaxChatLines && Log->GetChildrenCount() > 0)
		{
			Log->RemoveChildAt(0);
			--ChatLineCount;
		}
		if (bChatStickToBottom)
		{
			// Wrapped paragraphs grow after layout — TickRefresh keeps snapping while stick is on.
			Log->ScrollToEnd();
			bPendingChatScrollToEnd = true;
			ChatStickEndOffset = -1.f;
		}
		SyncChatScrollbar();
		SyncChatJumpIndicator();
	}
	else
	{
		int32& Count = FloatyChatLineCounts[Window - 1];
		++Count;
		while (Count > MaxChatLines && Log->GetChildrenCount() > 0)
		{
			Log->RemoveChildAt(0);
			--Count;
		}
		if (bFloatyChatStickToBottom[Window-1]) Log->ScrollToEnd();
	}
	if (!ClickSender.IsEmpty())
	{
		ChatRowSenders.Add({ Row, ClickSender, Window });
	}
	if (ChatRowSenders.Num() > MaxChatLines * (NumFloatyChats + 1))
	{
		ChatRowSenders.RemoveAll([](const FChatRowSender& E) { return !E.Row.IsValid(); });
	}
}

void UACEUIGameplayBinder::PlaceFloatyChatOverlays()
{
	if (!Manager || !Canvas)
	{
		return;
	}
	for (int32 W = 1; W <= NumFloatyChats; ++W)
	{
		const FString RootName = FString::Printf(TEXT("RootGameplay_FloatyChat%d_Field"), W);
		TSharedPtr<FACEUIElement> Root = Manager->FindElementByName(RootName);
		const bool bOn = Root.IsValid() && Root->bVisible;
		UScrollBox* Log = GetChatLogWidget(W);
		UEditableTextBox* Entry = GetChatEntryWidget(W);
		UTextBlock* Title = FloatyChatTitleLabels.IsValidIndex(W - 1)
			? FloatyChatTitleLabels[W - 1].Get() : nullptr;
		TSharedPtr<FACEUIElement> LogEl = bOn
			? Manager->FindElementUnder(RootName, TEXT("ChatLog")) : nullptr;
		TSharedPtr<FACEUIElement> EntryEl = bOn
			? Manager->FindElementUnder(RootName, TEXT("ChatPanelTextEntry")) : nullptr;
		TSharedPtr<FACEUIElement> TitleEl = bOn
			? Manager->FindElementUnder(RootName, TEXT("TitleBar")) : nullptr;
		if (Log)
		{
			if (bOn && LogEl.IsValid())
			{
				Log->SetVisibility(ESlateVisibility::HitTestInvisible);
				Canvas->PlaceWidgetAtElement(Log, LogEl, 100530, FMargin(2.f, 2.f, 2.f, 2.f));
				RefreshChatRowLayout(W);
				if (bFloatyChatStickToBottom[W-1])
				{
					const float End=Log->GetScrollOffsetOfEnd();
					if (FMath::Abs(Log->GetScrollOffset()-End)>.5f) Log->SetScrollOffset(End);
				}
				SyncChatScrollbar(W);
			}
			else
			{
				Log->SetVisibility(ESlateVisibility::Collapsed);
			}
		}
		if (Entry)
		{
			if (bOn && EntryEl.IsValid())
			{
				Entry->SetVisibility(ESlateVisibility::Visible);
				Canvas->PlaceWidgetAtElement(Entry, EntryEl, 100531, FMargin(1.f, 0.f));
			}
			else
			{
				Entry->SetVisibility(ESlateVisibility::Collapsed);
			}
		}
		if (Title)
		{
			if (bOn && TitleEl.IsValid())
			{
				Title->SetText(FText::FromString(
					FloatyChatTitles.IsValidIndex(W - 1) ? FloatyChatTitles[W - 1] : FString()));
				Title->SetVisibility(ESlateVisibility::HitTestInvisible);
				PlaceTextOnElement(Title, TitleEl, Title->GetText().ToString(), 8, ChatWhite, 100531);
			}
			else
			{
				Title->SetVisibility(ESlateVisibility::Collapsed);
			}
		}
	}
}

bool UACEUIGameplayBinder::TryHandleChatNameClick(FVector2D Absolute)
{
	for (int32 i = ChatRowSenders.Num() - 1; i >= 0; --i)
	{
		const FChatRowSender& E = ChatRowSenders[i];
		UTextBlock* Row = E.Row.Get();
		if (!Row || !Row->GetParent() || Row->GetVisibility() == ESlateVisibility::Collapsed)
		{
			continue;
		}
		// Rows keep stale cached geometry when their owning log is collapsed (hidden floaty
		// chat) — phantom hit targets swallowed clicks over panels/inventory. Require the
		// click to land inside the live log bounds too.
		UScrollBox* OwnerLog = GetChatLogWidget(E.Window);
		if (!OwnerLog || OwnerLog->GetVisibility() == ESlateVisibility::Collapsed
			|| !Canvas->IsWidgetExposedAt(OwnerLog, Absolute))
		{
			continue;
		}
		if (!Canvas->IsWidgetExposedAt(Row, Absolute))
		{
			continue;
		}
		// Retail <Tell:IIDString> click → ChatInterface::StartTell: prefill
		// "@tell Name, " and focus the entry line.
		UEditableTextBox* Entry = GetChatEntryWidget(E.Window);
		if (!Entry)
		{
			Entry = ChatEntry;
		}
		if (!Entry)
		{
			return false;
		}
		Entry->SetText(FText::FromString(FString::Printf(TEXT("@tell %s, "), *E.Sender)));
		FocusChatEntryWindow(E.Window);
		return true;
	}
	return false;
}

void UACEUIGameplayBinder::ToggleChatTargetPopup()
{
	bChatTargetPopupOpen = !bChatTargetPopupOpen;
	if (bChatTargetPopupOpen)
	{
		RefreshChatTargetPopup();
	}
	else
	{
		CloseChatTargetPopup();
	}
}

void UACEUIGameplayBinder::CloseChatTargetPopup()
{
	bChatTargetPopupOpen = false;
	if (Manager)
	{
		Manager->SetElementVisibleByName(TEXT("ChatTargetMenuPopup"), false);
	}
	if (ChatTargetPopupBg)
	{
		ChatTargetPopupBg->SetVisibility(ESlateVisibility::Collapsed);
	}
	for (UTextBlock* Row : ChatTargetPopupRows)
	{
		if (Row)
		{
			Row->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
	for (UBorder* Background : ChatTargetPopupRowBackgrounds)
		if (Background) Background->SetVisibility(ESlateVisibility::Collapsed);
}

void UACEUIGameplayBinder::RefreshChatTargetPopup()
{
	if (!bChatTargetPopupOpen || !Manager || !Canvas || !Canvas->WidgetTree)
	{
		CloseChatTargetPopup();
		return;
	}
	static const auto PopupTemplate=UACEUILayoutResolver::LoadTemplate(0x21000006,0x1000001C);
	static const auto EntryTemplate=UACEUILayoutResolver::LoadTemplate(0x21000006,0x1000001E);
	const auto Button=Manager->FindElementByName(TEXT("ChatTarget"));
	auto* Resources=Canvas->GetResourceResolver();
	if (!PopupTemplate || !EntryTemplate || !Button || !Resources) return;
	const int32 RowH=EntryTemplate->Height;
	const int32 ColumnW=EntryTemplate->Width;
	const int32 Columns=FMath::Max(1,PopupTemplate->Width/ColumnW);
	const int32 W=Columns*ColumnW;
	const int32 H=FMath::DivideAndRoundUp(ChatMenuCount,Columns)*RowH+2;
	FIntPoint Origin=Button->GetScreenOrigin(); Origin.Y=FMath::Max(0,Origin.Y-H);
	const FVector2D Scale=Canvas->GetLastScale2D();
	auto Place=[&](UWidget* Widget,int32 X,int32 Y,int32 Width,int32 Height,int32 Layer)
	{
		if (Widget->GetParent()!=Canvas->GetElementLayer()) Canvas->GetElementLayer()->AddChild(Widget);
		Widget->SetVisibility(ESlateVisibility::HitTestInvisible);
		if (auto* Slot=Cast<UCanvasPanelSlot>(Widget->Slot))
		{
			Slot->SetAnchors(FAnchors(0,0)); Slot->SetPosition(FVector2D(X,Y)*Scale);
			Slot->SetSize(FVector2D(Width,Height)*Scale); Canvas->SetOverlayOrder(Widget,nullptr,Layer);
		}
	};
	auto SetArt=[&](UBorder* Border,uint32 ImageId)
	{
		FSlateBrush Brush; auto* Texture=Resources->ResolveIconTexture(ImageId);
		Brush.SetResourceObject(Texture); Brush.DrawAs=ESlateBrushDrawType::Image;
		Brush.Tiling=ESlateBrushTileType::Both;
		if (Texture) Brush.ImageSize=FVector2D(Texture->GetSizeX(),Texture->GetSizeY());
		Border->SetBrush(Brush); Border->SetBrushColor(FLinearColor::White); Border->SetPadding(FMargin(0));
	};
	if (!ChatTargetPopupBg) ChatTargetPopupBg=Canvas->WidgetTree->ConstructWidget<UBorder>();
	SetArt(ChatTargetPopupBg,PopupTemplate->ImageFileId);
	Place(ChatTargetPopupBg,Origin.X,Origin.Y,W,H,200000);
	const auto Selection=Client ? Client->GetSelectedObject() : FACESelectedObject();
	FACEWorldObject Selected;
	const bool bTalkable=Client && Selection.bValid && Client->GetWorldObject(Selection.Guid,Selected)
		&& Selected.bIsPlayer && Selected.Guid!=Client->GetPlayerGuid();
	bool bSquelched=false;
	if (bTalkable) for (const auto& Entry : Client->GetSquelches()) if (Entry.Guid==Selected.Guid) bSquelched=true;
	while (ChatTargetPopupRows.Num()<ChatMenuCount)
	{
		ChatTargetPopupRows.Add(Canvas->WidgetTree->ConstructWidget<UACERetailTextBlock>());
		ChatTargetPopupRowBackgrounds.Add(Canvas->WidgetTree->ConstructWidget<UBorder>());
		ChatTargetPopupElements.Add(MakeShared<FACEUIElement>(*EntryTemplate));
	}
	for (int32 Index=0;Index<ChatMenuCount;++Index)
	{
		auto* Row=CastChecked<UACERetailTextBlock>(ChatTargetPopupRows[Index]);
		auto* Background=ChatTargetPopupRowBackgrounds[Index].Get();
		const auto Element=ChatTargetPopupElements[Index];
		const int32 Channel=ChatMenuChannels[Index];
		const bool bEnabled=(Channel!=-1 && Channel!=12) || bTalkable;
		Element->PaintState=!bEnabled ? 13 : Channel==ChatSendChannel ? 0x10000001 : 1;
		const auto* State=Element->States.Find(Element->PaintState);
		SetArt(Background,State ? State->ImageFileId : EntryTemplate->ImageFileId);
		// UIElement_ListBox horizontal flow fills each row before the next.
		const int32 X=Origin.X+(Index%Columns)*ColumnW;
		const int32 Y=Origin.Y+2+(Index/Columns)*RowH;
		Place(Background,X,Y,ColumnW,RowH,200001);
		Place(Row,X,Y,ColumnW,RowH,200002);
		FString Label=ChatMenuLabels[Index];
		if (Channel==-1) Label=bTalkable ? FString::Printf(TEXT("%s %s"),bSquelched ? TEXT("Unsquelch") : TEXT("Squelch"),*Selected.Name) : TEXT("Squelch (no selection)");
		if (Channel==12) Label=bTalkable ? FString::Printf(TEXT("Tell to %s"),*Selected.Name) : TEXT("Tell to (no selection)");
		Row->SetText(FText::FromString(Label)); Row->SetColorAndOpacity(FLinearColor::White);
		const FMargin M=Element->TextMargins;
		Row->SetMargin(FMargin(M.Left*Scale.X,M.Top*Scale.Y,M.Right*Scale.X,M.Bottom*Scale.Y));
		Row->SetJustification(Element->TextHorizontalJustification==1 ? ETextJustify::Center
			: Element->TextHorizontalJustification==3 ? ETextJustify::Right : ETextJustify::Left);
		Row->SetRetailElement(Resources,Element,Scale,ColumnW);
	}
}

bool UACEUIGameplayBinder::TryHandleModalPopupClick(FVector2D Absolute)
{
	if (ServerConfirmRoot && ServerConfirmRoot->bVisible && Canvas && Manager)
	{
		const FVector2D Pos = Canvas->ViewportToLayout(Canvas->GetCachedGeometry().AbsoluteToLocal(Absolute));
		for (const TCHAR* Name : {TEXT("ServerConfirmationYes"), TEXT("ServerConfirmationNo")})
		{
			const auto Element = Manager->FindElementByName(Name);
			if (!Element) continue;
			const FIntPoint Origin = Element->GetScreenOrigin();
			if (Pos.X >= Origin.X && Pos.Y >= Origin.Y && Pos.X < Origin.X + Element->Width && Pos.Y < Origin.Y + Element->Height)
			{
				FinishServerConfirmation(Element->ElementId == 0x17);
				break;
			}
		}
		return true;
	}
	if (ManaStoneConfirmSource != 0 && Canvas && Manager)
	{
		const FVector2D Pos = Canvas->ViewportToLayout(Canvas->GetCachedGeometry().AbsoluteToLocal(Absolute));
		for (const TCHAR* Name : {TEXT("ManaStoneConfirmationYes"), TEXT("ManaStoneConfirmationNo")})
		{
			const auto Element = Manager->FindElementByName(Name);
			if (!Element) continue;
			const FIntPoint Origin = Element->GetScreenOrigin();
			if (Pos.X >= Origin.X && Pos.Y >= Origin.Y && Pos.X < Origin.X + Element->Width && Pos.Y < Origin.Y + Element->Height)
			{
				FinishManaStoneConfirmation(Element->ElementId == 0x17);
				break;
			}
		}
		return true;
	}
	if (!bChatTargetPopupOpen) return false;
	TryHandleChatTargetPopupClick(Absolute);
	return true; // The dismissing click also belongs to the popup.
}

bool UACEUIGameplayBinder::TryHandleChatTargetPopupClick(FVector2D Absolute)
{
	if (!bChatTargetPopupOpen)
	{
		return false;
	}
	for (int32 i = 0; i < ChatTargetPopupRows.Num(); ++i)
	{
		UTextBlock* Row = ChatTargetPopupRows[i];
		if (!Row || Row->GetVisibility() == ESlateVisibility::Collapsed)
		{
			continue;
		}
		if (Canvas->IsWidgetExposedAt(Row, Absolute))
		{
			if (!ChatTargetPopupElements.IsValidIndex(i) || ChatTargetPopupElements[i]->PaintState==13) return true;
			const int32 Channel=ChatMenuChannels[i];
			if (Channel==-1 && Client)
			{
				const auto Selection=Client->GetSelectedObject(); FACEWorldObject Selected;
				if (Selection.bValid && Client->GetWorldObject(Selection.Guid,Selected) && Selected.bIsPlayer)
				{
					bool bSquelched=false;
					for (const auto& Entry : Client->GetSquelches()) if (Entry.Guid==Selected.Guid) bSquelched=true;
					Client->SendModifyCharacterSquelch(!bSquelched,Selected.Guid,Selected.Name,ACEChatMessageType::AllChannels);
				}
				CloseChatTargetPopup();
			}
			else SetChatSendChannel(Channel);
			return true;
		}
	}
	if (ChatTargetPopupBg && ChatTargetPopupBg->GetVisibility() != ESlateVisibility::Collapsed
		&& Canvas->IsWidgetExposedAt(ChatTargetPopupBg, Absolute))
	{
		return true;
	}
	CloseChatTargetPopup();
	return false;
}

bool UACEUIGameplayBinder::TryDispatchChatCommand(const FString& Message, UEditableTextBox* Entry, bool* bClearEntry)
{
	if (!Client)
	{
		return false;
	}
	FString Cmd, Args;
	if (!SplitCommand(Message, Cmd, Args))
	{
		return false;
	}
	if (TryDispatchMiscCommand(Cmd,Args)) return true;
	if (Cmd == TEXT("friends_add")) { Cmd=TEXT("friends"); Args=TEXT("add ")+Args; }
	if (Cmd == TEXT("friends_remove")) { Cmd=TEXT("friends"); Args=TEXT("remove ")+Args; }
	if (Cmd == TEXT("hou")) Cmd=TEXT("house");
	if (Cmd == TEXT("?")) Cmd=TEXT("help");
	if (Cmd==TEXT("speaker"))
	{
		AppendLocalChatLine(TEXT("This command is no longer in use, please see @allegiance officer."),ACEChatMessageType::System);
		return true;
	}
	if (Cmd==TEXT("allegiance") || Cmd==TEXT("motd"))
	{
		const FString Lower=Args.ToLower();
		if (Cmd==TEXT("allegiance") && (Lower==TEXT("chat on") || Lower==TEXT("ch on") || Lower==TEXT("chat off") || Lower==TEXT("ch off")))
			Client->SendSetSingleCharacterOption(0x1B,Lower.EndsWith(TEXT("on")));
		else if (auto S=Client->GetSession();S.IsValid())
		{
			FString Error;S->SendSocialCommand(Cmd,Args,Error);
			if (!Error.IsEmpty()) AppendLocalChatLine(Error,ACEChatMessageType::ChatError);
		}
		return true;
	}
	if (Cmd == TEXT("chat") || Cmd == TEXT("notell"))
	{
		if (!Args.Equals(TEXT("on"),ESearchCase::IgnoreCase) && !Args.Equals(TEXT("off"),ESearchCase::IgnoreCase))
			AppendLocalChatLine(TEXT("Please specify on or off."),ACEChatMessageType::ChatError);
		else if (auto S=Client->GetSession();S.IsValid())
			S->SendModifyGlobalSquelch(Args.Equals(Cmd==TEXT("chat") ? TEXT("off") : TEXT("on"),ESearchCase::IgnoreCase),
				Cmd==TEXT("chat") ? ACEChatMessageType::Speech : ACEChatMessageType::Tell);
		return true;
	}

	auto NeedArgs = [&](const TCHAR* Usage) -> bool
	{
		if (!Args.IsEmpty())
		{
			return true;
		}
		AppendLocalChatLine(FString::Printf(TEXT("Usage: %s"), Usage), ACEChatMessageType::System);
		return false;
	};

	// Lifestone recall — retail TeleToLifestone (not Talk).
	if (Cmd == TEXT("ls") || Cmd == TEXT("lifestone") || Cmd == TEXT("lif"))
	{
		Client->SendTeleToLifestone();
		AppendLocalChatLine(TEXT("Recalling to your lifestone..."), ACEChatMessageType::System);
		return true;
	}
	// Marketplace — Mag-nus DoMarketplace → TeleToMarketPlace 0x028D.
	if (Cmd == TEXT("mp") || Cmd == TEXT("marketplace") || Cmd == TEXT("mar"))
	{
		Client->SendTeleToMarketplace();
		AppendLocalChatLine(TEXT("Recalling to the Marketplace..."), ACEChatMessageType::System);
		return true;
	}
	// House / mansion recall — Mag-nus DoHouseRecall / DoMansionRecall.
	// Wiki: /house mansion_recall|/house alleg_recall|/house ma|/hoa|/hom
	if (Cmd == TEXT("hr") || Cmd == TEXT("hor"))
	{
		Client->SendTeleToHouse();
		AppendLocalChatLine(TEXT("Recalling to your house..."), ACEChatMessageType::System);
		return true;
	}
	if (Cmd == TEXT("ma") || Cmd == TEXT("mansion") || Cmd == TEXT("allegiancehousing")
		|| Cmd == TEXT("alleg_recall") || Cmd == TEXT("hom") || Cmd == TEXT("hoa"))
	{
		Client->SendTeleToMansion();
		AppendLocalChatLine(TEXT("Recalling to allegiance housing..."), ACEChatMessageType::System);
		return true;
	}
	if (Cmd == TEXT("hslist"))
	{
		TSharedPtr<FACESession> S = Client->GetSession();
		if (!S.IsValid())
		{
			return true;
		}
		uint32 HouseType = 1; // Cottage
		if (Args.Equals(TEXT("villa"), ESearchCase::IgnoreCase)) { HouseType = 2; }
		else if (Args.Equals(TEXT("mansion"), ESearchCase::IgnoreCase)) { HouseType = 3; }
		else if (Args.StartsWith(TEXT("apart"), ESearchCase::IgnoreCase)) { HouseType = 4; }
		else if (Args.Equals(TEXT("cottage"), ESearchCase::IgnoreCase) || Args.IsEmpty()) { HouseType = 1; }
		S->SendGameActionU32(ACEGameAction::ListAvailableHouses, HouseType);
		return true;
	}
	if (Cmd == TEXT("house"))
	{
		TSharedPtr<FACESession> S = Client->GetSession();
		FString Sub, Rest;
		if (!Args.Split(TEXT(" "), &Sub, &Rest))
		{
			Sub = Args;
			Rest.Reset();
		}
		Sub.TrimStartAndEndInline();
		Rest.TrimStartAndEndInline();
		const FString SubL = Sub.ToLower();

		auto NeedName = [&](const TCHAR* Usage) -> bool
		{
			if (!Rest.IsEmpty())
			{
				return true;
			}
			AppendLocalChatLine(FString::Printf(TEXT("Usage: %s"), Usage), ACEChatMessageType::System);
			return false;
		};

		if (SubL.IsEmpty() || SubL == TEXT("recall") || SubL == TEXT("re"))
		{
			Client->SendTeleToHouse();
			AppendLocalChatLine(TEXT("Recalling to your house..."), ACEChatMessageType::System);
			return true;
		}
		if (SubL == TEXT("mansion_recall") || SubL == TEXT("alleg_recall")
			|| SubL == TEXT("mansion") || SubL == TEXT("ma")
			|| SubL == TEXT("hom") || SubL == TEXT("hoa"))
		{
			Client->SendTeleToMansion();
			AppendLocalChatLine(TEXT("Recalling to allegiance housing..."), ACEChatMessageType::System);
			return true;
		}
		if (!S.IsValid())
		{
			return true;
		}
		if (SubL == TEXT("open"))
		{
			S->SendGameActionU32(ACEGameAction::SetOpenHouseStatus, 1);
			return true;
		}
		if (SubL == TEXT("close"))
		{
			S->SendGameActionU32(ACEGameAction::SetOpenHouseStatus, 0);
			return true;
		}
		if (SubL == TEXT("abandon"))
		{
			S->SendSimpleGameAction(ACEGameAction::AbandonHouse);
			return true;
		}
		if (SubL == TEXT("available"))
		{
			uint32 HouseType = 1;
			if (Rest.Equals(TEXT("villa"), ESearchCase::IgnoreCase)) { HouseType = 2; }
			else if (Rest.Equals(TEXT("mansion"), ESearchCase::IgnoreCase)) { HouseType = 3; }
			else if (Rest.StartsWith(TEXT("apart"), ESearchCase::IgnoreCase)) { HouseType = 4; }
			S->SendGameActionU32(ACEGameAction::ListAvailableHouses, HouseType);
			return true;
		}
		if (SubL == TEXT("boot"))
		{
			if (Rest.Equals(TEXT("all"), ESearchCase::IgnoreCase))
			{
				S->SendSimpleGameAction(ACEGameAction::BootEveryone);
				return true;
			}
			if (!NeedName(TEXT("@house boot <name> | @house boot all")))
			{
				return true;
			}
			S->SendGameActionString(ACEGameAction::BootSpecificHouseGuest, Rest);
			return true;
		}
		if (SubL == TEXT("hooks"))
		{
			if (Rest.Equals(TEXT("on"), ESearchCase::IgnoreCase))
			{
				S->SendGameActionU32(ACEGameAction::SetHooksVisibility, 1);
			}
			else if (Rest.Equals(TEXT("off"), ESearchCase::IgnoreCase))
			{
				S->SendGameActionU32(ACEGameAction::SetHooksVisibility, 0);
			}
			else
			{
				AppendLocalChatLine(TEXT("Usage: @house hooks on | @house hooks off"),
					ACEChatMessageType::System);
			}
			return true;
		}
		if (SubL == TEXT("guest") || SubL == TEXT("storage"))
		{
			FString Verb, Name;
			if (!Rest.Split(TEXT(" "), &Verb, &Name))
			{
				Verb = Rest;
				Name.Reset();
			}
			Verb.TrimStartAndEndInline();
			Name.TrimStartAndEndInline();
			const FString VerbL = Verb.ToLower();
			const bool bGuest = SubL == TEXT("guest");
			if (bGuest && (VerbL == TEXT("list") || VerbL.IsEmpty()))
			{
				S->SendSimpleGameAction(ACEGameAction::RequestFullGuestList);
				return true;
			}
			if (VerbL == TEXT("add_allegiance"))
			{
				S->SendGameActionU32(bGuest
					? ACEGameAction::ModifyAllegianceGuestPermission
					: ACEGameAction::ModifyAllegianceStoragePermission, 1);
				return true;
			}
			if (VerbL == TEXT("remove_allegiance"))
			{
				S->SendGameActionU32(bGuest
					? ACEGameAction::ModifyAllegianceGuestPermission
					: ACEGameAction::ModifyAllegianceStoragePermission, 0);
				return true;
			}
			if (VerbL == TEXT("remove_all"))
			{
				S->SendSimpleGameAction(bGuest
					? ACEGameAction::RemoveAllPermanentGuests
					: ACEGameAction::RemoveAllStoragePermission);
				return true;
			}
			if (!bGuest && VerbL == TEXT("add_all"))
			{
				S->SendSimpleGameAction(ACEGameAction::AddAllStoragePermission);
				return true;
			}
			if (VerbL == TEXT("add"))
			{
				if (Name.IsEmpty())
				{
					AppendLocalChatLine(bGuest
						? TEXT("Usage: @house guest add <name>")
						: TEXT("Usage: @house storage add <name>"),
						ACEChatMessageType::System);
					return true;
				}
				if (bGuest)
				{
					S->SendGameActionString(ACEGameAction::AddPermanentGuest, Name);
				}
				else
				{
					S->SendGameActionStringU32(ACEGameAction::ChangeStoragePermission, Name, 1);
				}
				return true;
			}
			if (VerbL == TEXT("remove"))
			{
				if (Name.IsEmpty())
				{
					AppendLocalChatLine(bGuest
						? TEXT("Usage: @house guest remove <name>")
						: TEXT("Usage: @house storage remove <name>"),
						ACEChatMessageType::System);
					return true;
				}
				if (bGuest)
				{
					S->SendGameActionString(ACEGameAction::RemovePermanentGuest, Name);
				}
				else
				{
					S->SendGameActionStringU32(ACEGameAction::ChangeStoragePermission, Name, 0);
				}
				return true;
			}
			AppendLocalChatLine(bGuest
				? TEXT("Usage: @house guest add|remove|list|add_allegiance|remove_allegiance|remove_all")
				: TEXT("Usage: @house storage add|remove|add_all|add_allegiance|remove_allegiance|remove_all"),
				ACEChatMessageType::System);
			return true;
		}
		AppendLocalChatLine(TEXT("Type @house_help for housing commands."), ACEChatMessageType::System);
		return true;
	}
	if (Cmd == TEXT("ah") || Cmd == TEXT("alh") || Cmd == TEXT("hometown")
		|| Cmd == TEXT("allegiancehometown"))
	{
		Client->SendRecallAllegianceHometown();
		AppendLocalChatLine(TEXT("Recalling to your allegiance hometown..."), ACEChatMessageType::System);
		return true;
	}

	// Forced local say.
	if (Cmd == TEXT("s") || Cmd == TEXT("say"))
	{
		if (!NeedArgs(TEXT("@say <text>")))
		{
			return true;
		}
		Client->SendChatMessage(Args);
		HandleChatMessage(Args, TEXT("You"), ACEChatMessageType::Speech);
		return true;
	}

	// Text emotes.
	if (Cmd == TEXT("e") || Cmd == TEXT("em") || Cmd == TEXT("emote") || Cmd == TEXT("me"))
	{
		if (!NeedArgs(TEXT("@e <text>")))
		{
			return true;
		}
		Client->SendEmote(Args);
		return true;
	}
	if (Cmd == TEXT("sm") || Cmd == TEXT("smote") || Cmd == TEXT("soul") || Cmd == TEXT("soulemote"))
	{
		if (!NeedArgs(TEXT("@sm <text>")))
		{
			return true;
		}
		Client->SendSoulEmote(Args);
		return true;
	}

	// Legacy channel membership/list commands are game actions, not local speech.
	if (Cmd==TEXT("index") || Cmd==TEXT("clist") || Cmd==TEXT("on") || Cmd==TEXT("off"))
	{
		if (auto S=Client->GetSession();S.IsValid())
		{
			if (Cmd==TEXT("index")) S->SendSimpleGameAction(ACEGameAction::IndexChannels);
			else if (const uint32 Channel=ACERetailChat::FindChannel(Args))
				S->SendGameActionU32(Cmd==TEXT("clist") ? ACEGameAction::ListChannels :
					Cmd==TEXT("on") ? ACEGameAction::AddChannel : ACEGameAction::RemoveChannel,Channel);
			else AppendLocalChatLine(TEXT("Specify a valid channel name."),ACEChatMessageType::ChatError);
		}
		return true;
	}
	if (const uint32 Channel=ACERetailChat::FindChannel(Cmd); Channel && Channel<ACEChatChannel::Fellow && Cmd!=TEXT("help"))
	{
		if (NeedArgs(TEXT("@<channel> <message>"))) Client->SendChatChannel(Channel,Args);
		return true;
	}

	// Private tells — retail DoTell (@tell/@t/@send/@whisper/@w) requires a comma
	// after the name so multi-word names parse (acclient.c:417964).
	if (Cmd == TEXT("t") || Cmd == TEXT("tell") || Cmd == TEXT("send")
		|| Cmd == TEXT("whisper") || Cmd == TEXT("w"))
	{
		FString Target, Body;
		if (!Args.Split(TEXT(","), &Target, &Body))
		{
			AppendLocalChatLine(TEXT("Use comma after the name for targeted chat."),
				ACEChatMessageType::ChatError);
			return true;
		}
		Target = Target.TrimStartAndEnd();
		Target.TrimQuotesInline();
		Body = Body.TrimStartAndEnd();
		if (Target.IsEmpty() || Body.IsEmpty())
		{
			AppendLocalChatLine(TEXT("Use comma after the name for targeted chat."),
				ACEChatMessageType::ChatError);
			return true;
		}
		Client->SendTell(Target, Body);
		LastOutgoingTellName = Target;
		return true;
	}
	if (Cmd == TEXT("r") || Cmd == TEXT("reply") || Cmd == TEXT("rp") || Cmd == TEXT("mr") || Cmd == TEXT("pr"))
	{
		int32 Guid = Client->GetLastTellSenderGuid();
		FString Name = Client->GetLastTellSenderName();
		if (Cmd == TEXT("mr") || Cmd == TEXT("pr"))
		{
			Guid = 0;
			const auto Session = Client->GetSession();
			Name = !Session ? FString() : Cmd == TEXT("mr") ? Session->GetLastMonarchTellSenderName() : Session->GetLastPatronTellSenderName();
		}
		if (Name.IsEmpty() && Guid != 0)
		{
			FACEWorldObject Obj;
			if (Client->GetWorldObject(Guid, Obj))
			{
				Name = Obj.Name;
			}
		}
		if (Guid == 0 && Name.IsEmpty())
		{
			AppendLocalChatLine(Cmd == TEXT("mr") ? TEXT("Nobody has sent you a monarch tell yet.")
				: Cmd == TEXT("pr") ? TEXT("Nobody has sent you a patron tell yet.") : TEXT("Someone must @tell you first!"), ACEChatMessageType::ChatError);
			return true;
		}
		if (Args.IsEmpty())
		{
			if (Entry)
			{
				Entry->SetText(FText::FromString(FString::Printf(TEXT("@tell %s, "), *Name)));
			}
			if (bClearEntry)
			{
				*bClearEntry = false;
			}
			bPendingChatRefocus = true; PendingChatRefocusWindow = ChatSourceWindow;
			return true;
		}
		if (!Name.IsEmpty())
		{
			// ACE's TalkDirect only resolves nearby objects; name tells reach
			// the last player teller even after either player changes landblock.
			Client->SendTell(Name, Args);
		}
		else
		{
			Client->SendTalkDirect(Guid, Args);
		}
		LastOutgoingTellName = Name;
		return true;
	}
	if (Cmd == TEXT("rt") || Cmd == TEXT("retell"))
	{
		if (!NeedArgs(TEXT("@rt <message>")))
		{
			return true;
		}
		const FString Name = LastOutgoingTellName;
		if (Name.IsEmpty())
		{
			AppendLocalChatLine(TEXT("You must first provide a name using @tell"), ACEChatMessageType::ChatError);
			return true;
		}
		Client->SendTell(Name, Args);
		return true;
	}

	// Channel broadcast shortcuts — retail DoStupidChannelHack: one-off broadcast;
	// empty args is an error (acclient.c:420906). Talk focus is not changed.
	auto ChannelAlias = [&](int32 DestIndex) -> bool
	{
		if (Args.IsEmpty())
		{
			AppendLocalChatLine(TEXT("You must specify the text you wish to broadcast!"),
				ACEChatMessageType::ChatError);
			return true;
		}
		const uint32 Chan = ChatSendChannelId(DestIndex);
		Client->SendChatChannel(static_cast<int32>(Chan), Args);
		return true;
	};
	if (Cmd == TEXT("f") || Cmd == TEXT("fellow") || Cmd == TEXT("fellows")
		|| Cmd == TEXT("fellowship") || Cmd == TEXT("g") || Cmd == TEXT("group")
		|| Cmd == TEXT("party"))
	{
		return ChannelAlias(1);
	}
	if (Cmd == TEXT("ab")) return ChannelAlias(2);
	if (Cmd == TEXT("v") || Cmd == TEXT("vassals") || Cmd == TEXT("vassal"))
	{
		return ChannelAlias(3);
	}
	if (Cmd == TEXT("p") || Cmd == TEXT("patron"))
	{
		return ChannelAlias(4);
	}
	if (Cmd == TEXT("m") || Cmd == TEXT("monarch"))
	{
		return ChannelAlias(5);
	}
	if (Cmd == TEXT("c") || Cmd == TEXT("covassals") || Cmd == TEXT("covassal")
		|| Cmd == TEXT("co-vassals"))
	{
		return ChannelAlias(6);
	}

	// Retail ProcessSquelchArgs: optional account/reply/message-type flags,
	// followed by a character name (which may contain spaces).
	if (Cmd == TEXT("squelch") || Cmd == TEXT("unsquelch"))
	{
		TSharedPtr<FACESession> Session = Client->GetSession();
		if (!Session) return true;
		if (Args.IsEmpty())
		{
			const auto& Entries = Session->GetSquelches();
			AppendLocalChatLine(Entries.IsEmpty() ? TEXT("No squelched characters.")
				: TEXT("Squelched characters:"), ACEChatMessageType::System);
			for (const auto& Squelch : Entries)
				AppendLocalChatLine(Squelch.Name + (Squelch.bAccount ? TEXT(" (account)") : TEXT("")),
					ACEChatMessageType::System);
			return true;
		}
		FString Name = Args;
		bool bAccount = false, bReply = false;
		int32 MessageType = ACEChatMessageType::AllChannels;
		while (Name.StartsWith(TEXT("-")))
		{
			FString Flag, Remaining;
			if (!Name.Split(TEXT(" "), &Flag, &Remaining)) Flag = Name;
			Name = Remaining.TrimStartAndEnd();
			Flag.RightChopInline(1);
			if (Flag.Equals(TEXT("account"), ESearchCase::IgnoreCase)) bAccount = true;
			else if (Flag.Equals(TEXT("reply"), ESearchCase::IgnoreCase)) bReply = true;
			else if (!ChatTypeFromName(Flag, MessageType))
			{
				AppendLocalChatLine(FString::Printf(TEXT("Unknown squelch option: -%s"), *Flag),
					ACEChatMessageType::ChatError);
				return true;
			}
		}
		if (bReply) Name = Client->GetLastTellSenderName();
		if (Name.StartsWith(TEXT("\"")) && Name.EndsWith(TEXT("\""))) Name = Name.Mid(1, Name.Len()-2);
		if (Name.IsEmpty())
		{
			AppendLocalChatLine(bReply ? TEXT("Nobody has told you anything yet.")
				: TEXT("Specify a character name to squelch or unsquelch."), ACEChatMessageType::ChatError);
			return true;
		}
		const bool bSquelch = Cmd == TEXT("squelch");
		if (bAccount) Session->SendModifyAccountSquelch(bSquelch, Name);
		else Session->SendModifyCharacterSquelch(bSquelch, 0, Name, static_cast<uint32>(MessageType));
		return true;
	}

	// Quick local filter cycle (non-retail convenience; retail @filter is below).
	if (Cmd == TEXT("cf"))
	{
		CycleChatFilterMode();
		return true;
	}

	// Retail DoFilter / DoUnFilter (PerformGlobalSquelchMod): globally squelch or
	// restore message types, e.g. "@filter -Combat -Magic".
	if (Cmd == TEXT("filter") || Cmd == TEXT("unfilter"))
	{
		const bool bSquelch = Cmd == TEXT("filter");
		if (Args.IsEmpty())
		{
			FString Filtered;
			for (int32 T = 0; T < 64; ++T)
			{
				if ((GlobalChatTypeFilter & (1ull << T)) != 0)
				{
					continue;
				}
				for (const FChatTypeName& TypeEntry : GChatTypeNames)
				{
					if (TypeEntry.Type == T)
					{
						if (!Filtered.IsEmpty()) { Filtered += TEXT(", "); }
						Filtered += TypeEntry.Name;
					}
				}
			}
			AppendLocalChatLine(Filtered.IsEmpty()
				? FString(TEXT("No message types are filtered."))
				: FString::Printf(TEXT("Filtered message types: %s"), *Filtered),
				ACEChatMessageType::System);
			return true;
		}
		TArray<FString> Tokens;
		Args.ParseIntoArray(Tokens, TEXT(" "), true);
		for (FString Token : Tokens)
		{
			Token.RemoveFromStart(TEXT("-"));
			Token.RemoveFromEnd(TEXT(","));
			int32 Type = 0;
			if (!ChatTypeFromName(Token, Type))
			{
				AppendLocalChatLine(FString::Printf(TEXT("Unknown message type: %s"), *Token),
					ACEChatMessageType::ChatError);
				continue;
			}
			if (bSquelch)
			{
				if (Type==ACEChatMessageType::AllChannels) GlobalChatTypeFilter=0; else GlobalChatTypeFilter &= ~(1ull << Type);
				AppendLocalChatLine(FString::Printf(TEXT("Now filtering %s messages."), *Token),
					ACEChatMessageType::System);
			}
			else
			{
				if (Type==ACEChatMessageType::AllChannels) GlobalChatTypeFilter=~0ull; else GlobalChatTypeFilter |= (1ull << Type);
				AppendLocalChatLine(FString::Printf(TEXT("No longer filtering %s messages."), *Token),
					ACEChatMessageType::System);
			}
			if (auto S=Client->GetSession();S.IsValid()) S->SendModifyGlobalSquelch(bSquelch,Type);
		}
		SaveFloatyChatSettings();
		return true;
	}

	// Retail DoMessageTypes — list filterable message types.
	if (Cmd == TEXT("messagetypes") || Cmd == TEXT("message_types")
		|| Cmd == TEXT("msgtypes") || Cmd == TEXT("msg_types"))
	{
		AppendLocalChatLine(TEXT("Filterable message types:"), ACEChatMessageType::System);
		AppendLocalChatLine(ChatTypeNames(), ACEChatMessageType::System);
		return true;
	}

	// Retail DoClear — clears the source window's buffer; "@clear all" clears all.
	if (Cmd == TEXT("clear"))
	{
		if (Args.Equals(TEXT("all"), ESearchCase::IgnoreCase))
		{
			for (int32 W = 0; W <= NumFloatyChats; ++W)
			{
				ClearChatLog(W);
			}
		}
		else
		{
			ClearChatLog(ChatSourceWindow);
		}
		return true;
	}

	// Retail DoTitle — titles the source floaty window (max 100 chars); main
	// windows refuse (acclient.c:420265, SendNotice_SetChatWindowTitle 5100144).
	if (Cmd == TEXT("title"))
	{
		if (ChatSourceWindow <= 0 || !FloatyChatTitles.IsValidIndex(ChatSourceWindow - 1))
		{
			AppendLocalChatLine(TEXT("You may not add a title to this window."),
				ACEChatMessageType::ChatError);
			return true;
		}
		if (Args.IsEmpty() || Args.Len()>100)
		{
			AppendLocalChatLine(Args.IsEmpty() ? TEXT("You must provide a new title for the window.") : TEXT("Window title length cannot exceed 100 characters."),ACEChatMessageType::ChatError);
			return true;
		}
		FloatyChatTitles[ChatSourceWindow - 1] = Args;
		SaveFloatyChatSettings();
		PlaceFloatyChatOverlays();
		return true;
	}

	// Retail DoSetOutput (@log) — mirror displayed chat lines into a text file.
	if (Cmd == TEXT("log"))
	{
		if (Args.IsEmpty())
		{
			const bool WasLogging=bChatMirrorToFile;
			bChatMirrorToFile=false;
			AppendLocalChatLine(WasLogging ? TEXT("Chat output now directed only to the screen.") : TEXT("Please specify a file to append chat messages to."),ACEChatMessageType::System);
		}
		else
		{
			FString Name=Args; Name.TrimQuotesInline();
			if (FPaths::GetExtension(Name).IsEmpty()) Name+=TEXT(".txt");
			const FString Path=FPaths::IsRelative(Name) ? FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("Logs"),Name) : Name;
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path),true);
			if (FFileHelper::SaveStringToFile(TEXT(""),*Path,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,&IFileManager::Get(),FILEWRITE_Append))
			{
				ChatMirrorFilePath=Path; bChatMirrorToFile=true;
				AppendLocalChatLine(FString::Printf(TEXT("Copying chat to %s. Run /log with no arguments to stop."),*Path),ACEChatMessageType::System);
			}
			else AppendLocalChatLine(FString::Printf(TEXT("Failed to redirect to file %s!"),*Path),ACEChatMessageType::ChatError);
		}
		return true;
	}

	// Retail DoEmoteList (@emotes) — list the standard animated chat poses.
	if (Cmd == TEXT("emotes"))
	{
		UACEDatSubsystem* Dat = nullptr;
		if (PlayerController)
		{
			if (UGameInstance* GI = PlayerController->GetGameInstance())
			{
				Dat = GI->GetSubsystem<UACEDatSubsystem>();
			}
		}
		TArray<FString> Keys;
		if (Dat)
		{
			Dat->GetChatPoseKeys(Keys);
		}
		if (Keys.Num() == 0)
		{
			AppendLocalChatLine(TEXT("No emote poses available."), ACEChatMessageType::System);
			return true;
		}
		Keys.Sort();
		AppendLocalChatLine(FString::Printf(TEXT("Available emotes (*name*): %d"), Keys.Num()),
			ACEChatMessageType::System);
		FString Batch;
		int32 InBatch = 0;
		for (const FString& Key : Keys)
		{
			if (!Batch.IsEmpty()) { Batch += TEXT(", "); }
			Batch += Key;
			if (++InBatch >= 12)
			{
				AppendLocalChatLine(Batch, ACEChatMessageType::System);
				Batch.Reset();
				InBatch = 0;
			}
		}
		if (!Batch.IsEmpty())
		{
			AppendLocalChatLine(Batch, ACEChatMessageType::System);
		}
		return true;
	}

	auto SendTurbine = [&](uint32 ChatType) -> bool
	{
		if (Args.IsEmpty())
		{
			AppendLocalChatLine(TEXT("You must specify the text you wish to broadcast!"),
				ACEChatMessageType::ChatError);
			return true;
		}
		const int32 Opt = TurbineListenOption(ChatType);
		if (Opt != INDEX_NONE && !Client->IsCharacterOptionSet(Opt))
		{
			AppendLocalChatLine(FString::Printf(TEXT("You are not listening to the %s channel!"),
				TurbineChannelName(ChatType)), ACEChatMessageType::ChatError);
			return true;
		}
		TSharedPtr<FACESession> S = Client->GetSession();
		if (!S.IsValid())
		{
			return true;
		}
		S->SendTurbineChat(S->GetTurbineChannelId(ChatType), ChatType, Args);
		return true;
	};
	if (Cmd == TEXT("cg") || Cmd == TEXT("general"))
	{
		return SendTurbine(ACETurbineChat::General);
	}
	if (Cmd == TEXT("ct") || Cmd == TEXT("trade"))
	{
		return SendTurbine(ACETurbineChat::Trade);
	}
	if (Cmd == TEXT("clfg") || Cmd == TEXT("lfg"))
	{
		return SendTurbine(ACETurbineChat::LFG);
	}
	if (Cmd == TEXT("crp") || Cmd == TEXT("roleplay") || Cmd == TEXT("rp"))
	{
		// /rp is also @reply in Mag-nus; empty args already error above for reply.
		// Prefer reply when this is the last-tell shortcut: Mag-nus maps /rp → reply.
		// Retail /crp is Roleplay. Keep /rp as reply (handled earlier).
		return SendTurbine(ACETurbineChat::Roleplay);
	}
	if (Cmd == TEXT("cs") || Cmd == TEXT("society") || Cmd == TEXT("soc"))
	{
		return SendTurbine(ACETurbineChat::Society);
	}
	if (Cmd == TEXT("co") || Cmd == TEXT("olthoi"))
	{
		return SendTurbine(ACETurbineChat::Olthoi);
	}
	if (Cmd == TEXT("ca") || Cmd == TEXT("a") || Cmd == TEXT("guild") || Cmd == TEXT("gu"))
	{
		return SendTurbine(ACETurbineChat::Allegiance);
	}

	auto ResolveTurbineJoin = [](const FString& Token, uint32& OutType) -> bool
	{
		const FString T = Token.ToLower();
		if (T == TEXT("general") || T == TEXT("cg")) { OutType = ACETurbineChat::General; return true; }
		if (T == TEXT("trade") || T == TEXT("ct")) { OutType = ACETurbineChat::Trade; return true; }
		if (T == TEXT("lfg") || T == TEXT("clfg")) { OutType = ACETurbineChat::LFG; return true; }
		if (T == TEXT("roleplay") || T == TEXT("crp") || T == TEXT("rp")) { OutType = ACETurbineChat::Roleplay; return true; }
		if (T == TEXT("society") || T == TEXT("soc") || T == TEXT("cs")) { OutType = ACETurbineChat::Society; return true; }
		if (T == TEXT("allegiance") || T == TEXT("ca")) { OutType = ACETurbineChat::Allegiance; return true; }
		if (T == TEXT("olthoi") || T == TEXT("co")) { OutType = ACETurbineChat::Olthoi; return true; }
		return false;
	};
	if (Cmd == TEXT("join") || Cmd == TEXT("leave"))
	{
		uint32 ChatType = 0;
		if (Args.IsEmpty() || !ResolveTurbineJoin(Args, ChatType))
		{
			AppendLocalChatLine(TEXT("Usage: @join/@leave general|trade|lfg|roleplay|society|allegiance"),
				ACEChatMessageType::System);
			return true;
		}
		const int32 Opt = TurbineListenOption(ChatType);
		if (Opt == INDEX_NONE)
		{
			return true;
		}
		const bool bJoin = Cmd == TEXT("join");
		const bool bOn = Client->IsCharacterOptionSet(Opt);
		if (bJoin != bOn)
		{
			Client->SendSetSingleCharacterOption(Opt, bJoin);
		}
		if (ChatType != ACETurbineChat::Society)
		{
			AppendLocalChatLine(FString::Printf(TEXT("You have %s the %s channel."),
				bJoin ? TEXT("entered") : TEXT("left"), TurbineChannelName(ChatType)),
				ACEChatMessageType::System);
		}
		return true;
	}

	auto PrintHelpLines = [&](const TArray<FString>& Lines)
	{
		for (const FString& Line : Lines)
		{
			AppendLocalChatLine(Line, ACEChatMessageType::Help);
		}
	};
	if (Cmd == TEXT("help") || Cmd == TEXT("house_help") || Cmd == TEXT("allegiance_help")
		|| Cmd == TEXT("fellowship_help"))
	{
		const FString Topic = (Cmd == TEXT("help")) ? Args.ToLower() : Cmd;
		static const TMap<FString,FString> MiscHelp = {
			{TEXT("fillcomps"),TEXT("Set desired counts in Spellbook > Components, then use a component vendor. /fillcomps adds missing components to the buy list; choose Buy All to purchase. Optional arguments: scarabs, herbs, powders, potions, talismans, tapers, or peas, followed by a maximum pyreal cost. /fillcomps clear resets all desired counts to zero.")},
			{TEXT("age"),TEXT("/age displays your character's total time played.")},
			{TEXT("birth"),TEXT("/birth displays when your character was created.")},
			{TEXT("day"),TEXT("/day toggles Always daylight outdoors.")},
			{TEXT("endurance"),TEXT("/endurance explains Endurance and Strength regeneration, stamina and resistance bonuses.")},
			{TEXT("framerate"),TEXT("/framerate toggles the frame rate display.")},
			{TEXT("loc"),TEXT("/loc displays your exact cell, coordinates and facing.")},
			{TEXT("pkl"),TEXT("/pkl or /pklite asks the server to enter Player Killer Lite mode.")},
			{TEXT("pklite"),TEXT("/pklite or /pkl asks the server to enter Player Killer Lite mode.")},
			{TEXT("version"),TEXT("/version displays the client and engine build versions.")},
			{TEXT("loadfile"),TEXT("/loadfile <filename> runs commands and chat lines from a text file. %DATE% expands to the current date. Quote filenames containing spaces.")},
			{TEXT("log"),TEXT("/log <filename> appends displayed chat to a local file. /log with no filename stops logging.")},
			{TEXT("title"),TEXT("Use /title <text> in an auxiliary chat window to set its title (1-100 characters).")},
		};
		if (const auto* Help=MiscHelp.Find(Topic))
		{
			AppendLocalChatLine(*Help,ACEChatMessageType::Help);
			return true;
		}
		if (Topic.IsEmpty() || Topic == TEXT("commands"))
		{
			PrintHelpLines({
				TEXT("Note: You may substitute a forward slash (/) for the at symbol (@)."),
				TEXT("Chat: @s @e @sm @t <name>, <text> @r @rt @f @a @v @p @m @c"),
				TEXT("Global: @cg/@general @ct/@trade @clfg/@lfg @crp/@roleplay @cs/@society @ca @co/@olthoi"),
				TEXT("Channels: @join/@leave <general|trade|lfg|roleplay|society|allegiance>"),
				TEXT("Recall: @ls @mp @house recall @house mansion_recall @ah"),
				TEXT("Miscellaneous: @fillcomps @age @birth @day @endurance @framerate @loc @pklite (@pkl) @version @loadfile @log @title"),
				TEXT("Other: @filter @unfilter @messagetypes @clear @title @log @emotes @afk @die @friends"),
				TEXT("Help: @help commands | @house_help | @allegiance_help | @acehelp (server)"),
			});
			return true;
		}
		if (Topic == TEXT("house") || Topic == TEXT("house_help") || Cmd == TEXT("house_help"))
		{
			PrintHelpLines({
				TEXT("@house recall — teleport to your house."),
				TEXT("@house mansion_recall — teleport to allegiance housing."),
				TEXT("@house open | @house close — open or close your house."),
				TEXT("@house abandon — abandon your house. Items inside will be lost."),
				TEXT("@house available [cottage|villa|mansion|apartment] — list houses for sale."),
				TEXT("@hslist [cottage|villa|mansion|apartment] — same as @house available."),
				TEXT("@house boot <name> | @house boot all"),
				TEXT("@house guest add|remove <name> | list | add_allegiance | remove_allegiance | remove_all"),
				TEXT("@house storage add|remove <name> | add_all | add_allegiance | remove_allegiance | remove_all"),
				TEXT("@house hooks on | @house hooks off"),
			});
			return true;
		}
		if (Topic == TEXT("allegiance") || Topic == TEXT("allegiance_help") || Cmd == TEXT("allegiance_help"))
		{
			PrintHelpLines({
				TEXT("@ab <text> — allegiance broadcast. @a/@ca <text> — allegiance chat."),
				TEXT("@ah / @allegiance hometown — recall to allegiance hometown."),
				TEXT("@v @p @m @c — vassals / patron / monarch / co-vassals."),
				TEXT("@allegiance officer add|set <level 1-3> <name> | remove <name> | list | clear"),
				TEXT("@allegiance title set <level 1-3> <title> | list | clear"),
				TEXT("@allegiance ban add|remove <name> | list; @allegiance boot [-account] <name>"),
				TEXT("@allegiance chat on|off|gag|ungag|kick <name>; @allegiance info <name>"),
				TEXT("@allegiance name [set <name>|clear]; @motd [set <text>|clear]"),
				TEXT("@allegiance lock on|off|toggle|check|bypass [name|clear]"),
				TEXT("@allegiance house [guest|storage open|close]"),
				TEXT("Server extras: @acehelp allegiance"),
			});
			return true;
		}
		if (Topic == TEXT("fellowship") || Topic == TEXT("fellowship_help") || Cmd == TEXT("fellowship_help"))
		{
			PrintHelpLines({
				TEXT("@f / @g / @group <text> — speak to your fellowship."),
				TEXT("Create, recruit, and share options are on the Social panel."),
			});
			return true;
		}
		AppendLocalChatLine(TEXT("Unknown help topic. Type @help commands."), ACEChatMessageType::Help);
		return true;
	}

	if (Cmd == TEXT("afk"))
	{
		TSharedPtr<FACESession> S = Client->GetSession();
		if (!S.IsValid())
		{
			return true;
		}
		FString Sub, Rest;
		if (!Args.Split(TEXT(" "), &Sub, &Rest))
		{
			Sub = Args;
			Rest.Reset();
		}
		Sub.TrimStartAndEndInline();
		if (Sub.Equals(TEXT("msg"), ESearchCase::IgnoreCase))
		{
			S->SendGameActionString(ACEGameAction::SetAfkMessage, Rest);
			AppendLocalChatLine(Rest.IsEmpty()
				? TEXT("AFK message reset to default.")
				: FString::Printf(TEXT("New AFK message set: %s"), *Rest),
				ACEChatMessageType::System);
			return true;
		}
		S->SetAfkLocal(!S->IsAfk());
		S->SendGameActionU32(ACEGameAction::SetAfkMode, S->IsAfk() ? 1u : 0u);
		AppendLocalChatLine(S->IsAfk()
			? TEXT("You are now set to away-from-keyboard.")
			: TEXT("You are no longer set to away-from-keyboard."),
			ACEChatMessageType::System);
		return true;
	}

	if (Cmd == TEXT("die") || Cmd == TEXT("suicide"))
	{
		TSharedPtr<FACESession> S = Client->GetSession();
		if (S.IsValid())
		{
			S->SendSimpleGameAction(ACEGameAction::Suicide);
		}
		return true;
	}

	if (Cmd == TEXT("friends"))
	{
		FString Sub, Rest;
		if (!Args.Split(TEXT(" "), &Sub, &Rest))
		{
			Sub = Args;
			Rest.Reset();
		}
		Sub.TrimStartAndEndInline();
		Rest.TrimStartAndEndInline();
		const FString SubL = Sub.ToLower();
		TSharedPtr<FACESession> S = Client->GetSession();
		const TArray<FACEFriendInfo> Friends = Client->GetFriends();
		if (SubL.IsEmpty() || SubL == TEXT("list") || SubL == TEXT("online"))
		{
			const bool bOnlineOnly = SubL == TEXT("online");
			int32 Shown = 0;
			FString Line = bOnlineOnly ? TEXT("Online friends:") : TEXT("Your friends:");
			AppendLocalChatLine(Line, ACEChatMessageType::System);
			for (const FACEFriendInfo& F : Friends)
			{
				if (bOnlineOnly && !F.bOnline)
				{
					continue;
				}
				AppendLocalChatLine(FString::Printf(TEXT("  %s (%s)"),
					*F.Name, F.bOnline ? TEXT("Online") : TEXT("Offline")),
					ACEChatMessageType::System);
				++Shown;
			}
			if (Shown == 0)
			{
				AppendLocalChatLine(bOnlineOnly
					? TEXT("You have no friends that are online.")
					: TEXT("Your friends list is empty."),
					ACEChatMessageType::System);
			}
			return true;
		}
		if (SubL == TEXT("add"))
		{
			if (Rest.IsEmpty())
			{
				AppendLocalChatLine(TEXT("Usage: @friends add <name>"), ACEChatMessageType::System);
				return true;
			}
			Client->SendAddFriend(Rest);
			return true;
		}
		if (SubL == TEXT("remove"))
		{
			if (Rest.Equals(TEXT("-all"), ESearchCase::IgnoreCase)
				|| Rest.Equals(TEXT("all"), ESearchCase::IgnoreCase))
			{
				if (S.IsValid())
				{
					S->SendSimpleGameAction(ACEGameAction::RemoveAllFriends);
				}
				return true;
			}
			if (Rest.IsEmpty())
			{
				AppendLocalChatLine(TEXT("Usage: @friends remove <name> | @friends remove -all"),
					ACEChatMessageType::System);
				return true;
			}
			for (const FACEFriendInfo& F : Friends)
			{
				if (F.Name.Equals(Rest, ESearchCase::IgnoreCase))
				{
					Client->SendRemoveFriend(F.Guid);
					return true;
				}
			}
			AppendLocalChatLine(FString::Printf(TEXT("%s is not on your friends list."), *Rest),
				ACEChatMessageType::System);
			return true;
		}
		AppendLocalChatLine(TEXT("Usage: @friends [online] | add <name> | remove <name> | remove -all"),
			ACEChatMessageType::System);
		return true;
	}

	// Unknown @command — forward to ACE CommandManager via Talk (admin / server cmds).
	// Mag-nus DoCommand falls back to Talk with the @ intact; never strip the prefix.
	FString Forward = Message;
	Forward.ReplaceInline(TEXT("\uFEFF"), TEXT(""));
	Forward.ReplaceInline(TEXT("\u200B"), TEXT(""));
	Forward.ReplaceInline(TEXT("\u00A0"), TEXT(" "));
	Forward.TrimStartAndEndInline();
	if (Forward.StartsWith(TEXT("/")) || Forward.StartsWith(TEXT("\uFF20")))
	{
		Forward = TEXT("@") + Forward.Mid(1);
	}
	else if (!Forward.StartsWith(TEXT("@")))
	{
		Forward = TEXT("@") + Forward;
	}
	Client->SendChatMessage(Forward);
	return true;
}
