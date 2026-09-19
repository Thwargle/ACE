#include "ACELoginWidget.h"
#include "ACELoginSettings.h"
#include "ACEClientSubsystem.h"
#include "ACEDatSubsystem.h"
#include "ACESession.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/ScaleBox.h"
#include "Components/ScaleBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetSwitcher.h"
#include "Components/WrapBox.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Engine/GameInstance.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SWidget.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#include <shobjidl.h>
#endif

namespace
{
	const FLinearColor Ink(.018f, .024f, .035f, 1);
	const FLinearColor Surface(.036f, .047f, .065f, 1);
	const FLinearColor Tile(.065f, .083f, .11f, 1);
	const FLinearColor Gold(.78f, .57f, .25f, 1);
	const FLinearColor Muted(.49f, .57f, .67f, 1);
	const FLinearColor LobbyTextColor(.88f, .9f, .92f, 1);
	void ButtonStyle(UButton* Button, bool Accent)
	{
		FButtonStyle Style;
		Style.SetNormal(FSlateRoundedBoxBrush(Accent ? Gold : Tile, 7.f));
		Style.SetHovered(FSlateRoundedBoxBrush(Accent ? FLinearColor(.96f,.73f,.36f,1) : FLinearColor(.11f,.16f,.21f,1), 7.f));
		Style.SetPressed(FSlateRoundedBoxBrush(Accent ? Gold * .8f : Surface, 7.f));
		Style.SetDisabled(FSlateRoundedBoxBrush(Surface, 7.f));
		Style.SetNormalPadding(FMargin(14,10)); Style.SetPressedPadding(FMargin(14,10));
		Button->SetStyle(Style);
		if (auto* Size=Cast<USizeBox>(Button->GetContent()))
			if (auto* Caption=Cast<UTextBlock>(Size->GetContent())) Caption->SetColorAndOpacity(Accent?Ink:LobbyTextColor);
	}
	void AddLine(UVerticalBox* Box, UWidget* Child, float Bottom = 12)
	{
		Box->AddChildToVerticalBox(Child)->SetPadding(FMargin(0,0,0,Bottom));
	}
}

void UACELoginActionButton::HandleAction()
{
	// The callback can rebuild its list, including this button.
	const FString Command = Action, Argument = Value;
	if (Owner.IsValid()) Owner->RunAction(Command, Argument);
}

UTextBlock* UACELoginWidget::Label(const FString& String, int32 Size, bool bMuted)
{
	auto* T = WidgetTree->ConstructWidget<UTextBlock>();
	T->SetText(FText::FromString(String));
	T->SetFont(FCoreStyle::GetDefaultFontStyle(Size >= 26 ? TEXT("Bold") : TEXT("Regular"), Size));
	T->SetColorAndOpacity(bMuted ? Muted : LobbyTextColor);
	T->SetAutoWrapText(true);
	T->SetVisibility(ESlateVisibility::HitTestInvisible);
	return T;
}

UACELoginActionButton* UACELoginWidget::ActionButton(const FString& Title, const FString& Action, const FString& Value, bool bAccent)
{
	auto* B = WidgetTree->ConstructWidget<UACELoginActionButton>(UACELoginActionButton::StaticClass(),Action==TEXT("launch")?FName(TEXT("LoginButton")):NAME_None);
	B->Action = Action; B->Value = Value; B->Owner = this;
	B->OnClicked.AddDynamic(B, &UACELoginActionButton::HandleAction);
	ButtonStyle(B, bAccent);
	auto* T = Label(Title, 18); T->SetAutoWrapText(false); T->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
	T->SetJustification(ETextJustify::Center); T->SetColorAndOpacity(bAccent ? Ink : LobbyTextColor);
	auto* Size = WidgetTree->ConstructWidget<USizeBox>(); Size->SetMinDesiredHeight(26);
	Size->SetContent(T); Cast<USizeBoxSlot>(T->Slot)->SetVerticalAlignment(VAlign_Center);
	B->SetContent(Size);
	return B;
}

UEditableTextBox* UACELoginWidget::Field(UVerticalBox* Parent, const FString& Title, const FName Name, bool bSecret)
{
	AddLine(Parent, Label(Title, 16, true), 5);
	auto* Box = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), Name);
	FEditableTextBoxStyle Style = Box->GetWidgetStyle();
	Style.SetBackgroundImageNormal(FSlateRoundedBoxBrush(Ink, 6.f));
	Style.SetBackgroundImageHovered(FSlateRoundedBoxBrush(Tile, 6.f));
	Style.SetBackgroundImageFocused(FSlateRoundedBoxBrush(Ink, 6.f, Gold, 1.5f));
	Style.SetForegroundColor(LobbyTextColor); Style.SetPadding(FMargin(12,10));
	Style.SetTextStyle(FTextBlockStyle().SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),20)).SetColorAndOpacity(LobbyTextColor));
	Box->SetWidgetStyle(Style); Box->SetIsPassword(bSecret); Box->SetClearKeyboardFocusOnCommit(true);
	Box->SetSelectAllTextWhenFocused(true);
	AddLine(Parent, Box, 12);
	return Box;
}

TSharedRef<SWidget> UACELoginWidget::RebuildWidget()
{
	EnsureDefaultLayout(); // Build before Super captures the root Slate widget.
	return Super::RebuildWidget();
}

void UACELoginWidget::EnsureDefaultLayout()
{
	if (HostBox || !WidgetTree) return;
	SetIsFocusable(true); SetVisibility(ESlateVisibility::Visible);
	ResponsiveScale=WidgetTree->ConstructWidget<UScaleBox>(); ResponsiveScale->SetStretch(EStretch::UserSpecified);
	WidgetTree->RootWidget=ResponsiveScale;
	auto* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(); ResponsiveScale->SetContent(Canvas);
	// UScaleBoxSlot defaults to Center, which would allocate only the canvas's
	// tiny desired size instead of the viewport and clip all anchored controls.
	Cast<UScaleBoxSlot>(Canvas->Slot)->SetHorizontalAlignment(HAlign_Fill);
	Cast<UScaleBoxSlot>(Canvas->Slot)->SetVerticalAlignment(VAlign_Fill);
	BackdropBorder = WidgetTree->ConstructWidget<UBorder>();
	BackdropBorder->SetBrush(FSlateRoundedBoxBrush(Ink,0.f)); BackdropBorder->SetPadding(FMargin(0));
	auto* BackSlot = Canvas->AddChildToCanvas(BackdropBorder); BackSlot->SetAnchors(FAnchors(0,0,1,1)); BackSlot->SetOffsets(FMargin(0));
	SetUseDatIntroBackdrop(bUseDatIntroBackdrop);
	auto* Scroll = WidgetTree->ConstructWidget<UScrollBox>();
	Scroll->SetConsumeMouseWheel(EConsumeMouseWheel::WhenScrollingPossible);
	Scroll->SetScrollbarThickness(FVector2D(9,9));
	auto* ScrollSlot = Canvas->AddChildToCanvas(Scroll); ScrollSlot->SetAnchors(FAnchors(0,0,1,1)); ScrollSlot->SetOffsets(FMargin(24,24,24,82));
	DashboardSize = WidgetTree->ConstructWidget<USizeBox>(); DashboardSize->SetWidthOverride(1052);
	auto* DashboardSlot = Cast<UScrollBoxSlot>(Scroll->AddChild(DashboardSize)); DashboardSlot->SetHorizontalAlignment(HAlign_Center);
	auto* Root = WidgetTree->ConstructWidget<UVerticalBox>(); DashboardSize->SetContent(Root);
	auto* Header = WidgetTree->ConstructWidget<UWrapBox>(); Header->SetInnerSlotPadding(FVector2D(10,10)); AddLine(Root, Header, 24);
#if PLATFORM_ANDROID
	auto* Brand = Label(TEXT("AC:VR"), 34);
#else
	auto* Brand = Label(TEXT("AC:Unreal"), 34);
#endif
	auto* BrandSize=WidgetTree->ConstructWidget<USizeBox>(); BrandSize->SetWidthOverride(245); BrandSize->SetContent(Brand); Header->AddChildToWrapBox(BrandSize);
	for (const auto& Item : TArray<TPair<FString,FString>>{{TEXT("Play"),TEXT("play")},{TEXT("Browse servers"),TEXT("browser")},{TEXT("Game files"),TEXT("files")}})
		Header->AddChildToWrapBox(ActionButton(Item.Key,Item.Value));
	Pages = WidgetTree->ConstructWidget<UWidgetSwitcher>(); AddLine(Root, Pages, 16);
	auto Card = [&](UVerticalBox*& Content) -> UBorder*
	{
		auto* B = WidgetTree->ConstructWidget<UBorder>(); B->SetBrush(FSlateRoundedBoxBrush(Surface,12.f)); B->SetPadding(FMargin(24));
		Content = WidgetTree->ConstructWidget<UVerticalBox>(); B->SetContent(Content); return B;
	};
	auto Row = [&](UVerticalBox* Parent, const TArray<TPair<FString,FString>>& Buttons)
	{
		auto* R = WidgetTree->ConstructWidget<UWrapBox>(); R->SetInnerSlotPadding(FVector2D(8,8)); AddLine(Parent,R);
		for (const auto& Item : Buttons) R->AddChildToWrapBox(ActionButton(Item.Key, Item.Value));
	};
	// Play: two cards on desktop/headset; a stacked layout in narrow windows.
	auto* Play = WidgetTree->ConstructWidget<UWrapBox>(); PlayLayout=Play; Play->SetExplicitWrapSize(true); Play->SetWrapSize(1028); Play->SetInnerSlotPadding(FVector2D(20,20)); Pages->AddChild(Play);
	SidebarSize = WidgetTree->ConstructWidget<USizeBox>(); SidebarSize->SetWidthOverride(280); Play->AddChildToWrapBox(SidebarSize);
	UVerticalBox* Sidebar; SidebarSize->SetContent(Card(Sidebar));
	AddLine(Sidebar,Label(TEXT("YOUR SERVERS"),16,true),16);
	ServerList = WidgetTree->ConstructWidget<UScrollBox>(); ServerList->SetScrollbarThickness(FVector2D(8,8));
	auto* ServerHeight = WidgetTree->ConstructWidget<USizeBox>(); ServerHeight->SetHeightOverride(338); ServerHeight->SetContent(ServerList); AddLine(Sidebar,ServerHeight,16);
	AddLine(Sidebar,ActionButton(TEXT("+ Custom server"),TEXT("newserver")),8);
	AddLine(Sidebar,ActionButton(TEXT("Browse directory"),TEXT("browser")),0);
	DetailSize = WidgetTree->ConstructWidget<USizeBox>(); DetailSize->SetWidthOverride(752); Play->AddChildToWrapBox(DetailSize);
	UVerticalBox* Details; DetailSize->SetContent(Card(Details));
	ServerTitle=Label(TEXT("Choose your world"),30); AddLine(Details,ServerTitle,5);
	ServerDetail=Label(TEXT("Save a server or discover one in the directory."),16,true); AddLine(Details,ServerDetail,14);
	ServerDescription=Label(TEXT(""),18); AddLine(Details,ServerDescription,14);
	auto* Links=WidgetTree->ConstructWidget<UWrapBox>(); Links->SetInnerSlotPadding(FVector2D(8,8)); AddLine(Details,Links,16);
	WebsiteButton=ActionButton(TEXT("Website"),TEXT("website")); Links->AddChildToWrapBox(WebsiteButton);
	DiscordButton=ActionButton(TEXT("Discord"),TEXT("discord")); Links->AddChildToWrapBox(DiscordButton);
	Links->AddChildToWrapBox(ActionButton(TEXT("Edit server"),TEXT("editserver")));
	Links->AddChildToWrapBox(ActionButton(TEXT("Remove"),TEXT("removeserver")));
	AddLine(Details,Label(TEXT("Accounts"),16,true),8);
	AccountList=WidgetTree->ConstructWidget<UScrollBox>(); AccountList->SetScrollbarThickness(FVector2D(8,8));
	auto* AccountHeight=WidgetTree->ConstructWidget<USizeBox>(); AccountHeight->SetHeightOverride(122); AccountHeight->SetContent(AccountList); AddLine(Details,AccountHeight);
	auto* Credentials=WidgetTree->ConstructWidget<UHorizontalBox>(); AddLine(Details,Credentials,0);
	auto* UserColumn=WidgetTree->ConstructWidget<UVerticalBox>(); auto* UserSlot=Credentials->AddChildToHorizontalBox(UserColumn);
	UserSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); UserSlot->SetPadding(FMargin(0,0,16,0));
	auto* PasswordColumn=WidgetTree->ConstructWidget<UVerticalBox>(); Credentials->AddChildToHorizontalBox(PasswordColumn)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	AccountBox=Field(UserColumn,TEXT("Username"),TEXT("AccountBox")); PasswordBox=Field(PasswordColumn,TEXT("Password"),TEXT("PasswordBox"),true);
	Row(Details,{{TEXT("Save account"),TEXT("saveaccount")},{TEXT("New account"),TEXT("newaccount")},{TEXT("Remove account"),TEXT("removeaccount")}});
	LoginButton=ActionButton(TEXT("Launch"),TEXT("launch"),FString(),true); AddLine(Details,LoginButton,8);
	AddLine(Details,Label(TEXT("Accounts are saved securely on this device."),14,true),0);
	// Server editor: explicit save/cancel avoids sending credentials to an edited endpoint by accident.
	UVerticalBox* Edit; Pages->AddChild(Card(Edit));
	AddLine(Edit,Label(TEXT("Server details"),30),18);
	NameBox=Field(Edit,TEXT("Server name"),TEXT("ServerName"));
	auto* Address=WidgetTree->ConstructWidget<UHorizontalBox>(); AddLine(Edit,Address,0);
	auto* HostCol=WidgetTree->ConstructWidget<UVerticalBox>(); Address->AddChildToHorizontalBox(HostCol)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	HostBox=Field(HostCol,TEXT("Hostname or IP address"),TEXT("HostBox"));
	auto* PortWidth=WidgetTree->ConstructWidget<USizeBox>(); PortWidth->SetWidthOverride(140); Address->AddChildToHorizontalBox(PortWidth)->SetPadding(FMargin(16,0,0,0));
	auto* PortCol=WidgetTree->ConstructWidget<UVerticalBox>(); PortWidth->SetContent(PortCol); PortBox=Field(PortCol,TEXT("Port"),TEXT("PortBox"));
	AddLine(Edit,Label(TEXT("Server type"),16,true),5);
	auto* Types=WidgetTree->ConstructWidget<UHorizontalBox>(); AddLine(Edit,Types,12);
	ACETypeButton=ActionButton(TEXT("ACE"),TEXT("ace")); GDLETypeButton=ActionButton(TEXT("GDLE"),TEXT("gdle"));
	Types->AddChildToHorizontalBox(ACETypeButton)->SetPadding(FMargin(0,0,8,0)); Types->AddChildToHorizontalBox(GDLETypeButton);
	DescriptionBox=Field(Edit,TEXT("Description (optional)"),TEXT("ServerDescription"));
	WebsiteBox=Field(Edit,TEXT("Website (optional)"),TEXT("ServerWebsite")); DiscordBox=Field(Edit,TEXT("Discord invite (optional)"),TEXT("ServerDiscord"));
	Row(Edit,{{TEXT("Save server"),TEXT("saveserver")},{TEXT("Cancel"),TEXT("play")}});
	// Directory stays inside the same widget surface: no OS dropdowns in stereo.
	UVerticalBox* Browser; Pages->AddChild(Card(Browser));
	AddLine(Browser,Label(TEXT("Find your next world"),30),6);
	AddLine(Browser,Label(TEXT("Community server directory  /  ACE + GDLE"),18,true),18);
	SearchBox=Field(Browser,TEXT("Search names, descriptions or addresses"),TEXT("ServerSearch"));
	SearchBox->OnTextChanged.AddDynamic(this,&UACELoginWidget::OnSearchChanged);
	BrowserList=WidgetTree->ConstructWidget<UScrollBox>(); BrowserList->SetScrollbarThickness(FVector2D(10,10));
	auto* BrowserHeight=WidgetTree->ConstructWidget<USizeBox>(); BrowserHeight->SetHeightOverride(235); BrowserHeight->SetContent(BrowserList); AddLine(Browser,BrowserHeight,14);
	BrowserDetail=Label(TEXT("Refresh to load the public server directory."),18); AddLine(Browser,BrowserDetail,16);
	Row(Browser,{{TEXT("Add to my servers"),TEXT("import")},{TEXT("Refresh"),TEXT("refresh")},{TEXT("Website"),TEXT("browserwebsite")},{TEXT("Discord"),TEXT("browserdiscord")}});
	AddLine(Browser,Label(TEXT("Directory maintained by acresources / Servers.xml. Adding a server does not connect to it."),14,true),0);
	UVerticalBox* Files; Pages->AddChild(Card(Files));
	AddLine(Files,Label(TEXT("Your game files"),30),12);
	AddLine(Files,Label(TEXT("Choose the folder containing your Asheron's Call .dat files. The client reads these files directly."),20),24);
	DatBox=Field(Files,TEXT("DAT folder"),TEXT("DatDirectory")); DatBox->OnTextChanged.AddDynamic(this,&UACELoginWidget::OnDatChanged);
#if PLATFORM_WINDOWS
	if (!UHeadMountedDisplayFunctionLibrary::IsHeadMountedDisplayEnabled()) AddLine(Files,ActionButton(TEXT("Choose folder"),TEXT("browsefolder")));
	Row(Files,{{TEXT("Save location"),TEXT("savedat")},{TEXT("Use default"),TEXT("defaultdat")}});
#else
	Row(Files,{{TEXT("Save location"),TEXT("savedat")},{TEXT("Use app DAT folder"),TEXT("defaultdat")}});
	AddLine(Files,Label(TEXT("On Quest, the folder must be accessible to this app. The installer uses the app's DAT folder."),18,true),20);
#endif
	FileStatus=Label(TEXT(""),20); AddLine(Files,FileStatus,24);
	AddLine(Files,Label(TEXT("Changing an already-loaded installation takes effect after restarting. No game files are moved or copied."),18,true));
	// Inline destructive-action confirmation is also fully usable with VR pointers.
	FooterSize=WidgetTree->ConstructWidget<USizeBox>(); FooterSize->SetWidthOverride(1028);
	auto* FooterSlot=Canvas->AddChildToCanvas(FooterSize); FooterSlot->SetAnchors(FAnchors(.5f,1)); FooterSlot->SetAlignment(FVector2D(.5f,1)); FooterSlot->SetPosition(FVector2D(0,-18)); FooterSlot->SetAutoSize(true);
	auto* Footer=WidgetTree->ConstructWidget<UVerticalBox>(); FooterSize->SetContent(Footer);
	UVerticalBox* Confirmation; ConfirmPanel=Card(Confirmation); AddLine(Footer,ConfirmPanel);
	ConfirmText=Label(TEXT(""),18); AddLine(Confirmation,ConfirmText);
	Row(Confirmation,{{TEXT("Remove"),TEXT("confirmremove")},{TEXT("Keep"),TEXT("cancelremove")}}); ConfirmPanel->SetVisibility(ESlateVisibility::Collapsed);
	auto* StatusBorder=WidgetTree->ConstructWidget<UBorder>(); StatusBorder->SetBrush(FSlateRoundedBoxBrush(Ink,7.f)); StatusBorder->SetPadding(FMargin(12,10));
	StatusText=Label(TEXT("Ready"),18); StatusBorder->SetContent(StatusText); AddLine(Footer,StatusBorder,0);
	// Retained for Blueprint/test compatibility; retail DAT owns character selection.
	EnterWorldButton=ActionButton(TEXT("Enter world"),TEXT("enter")); EnterWorldButton->SetVisibility(ESlateVisibility::Collapsed);
	CharacterListLabel=Label(TEXT("")); CharacterListBox=WidgetTree->ConstructWidget<UScrollBox>();
}

void UACELoginWidget::NativeConstruct()
{
	EnsureDefaultLayout(); Super::NativeConstruct();
	if (auto* GI=GetGameInstance())
	{
		Client=GI->GetSubsystem<UACEClientSubsystem>();
		if (Client)
		{
			Client->OnSessionStateChanged.AddUniqueDynamic(this,&UACELoginWidget::HandleState);
			Client->OnCharacterList.AddUniqueDynamic(this,&UACELoginWidget::HandleCharacters);
			Client->OnLogMessage.AddUniqueDynamic(this,&UACELoginWidget::HandleLog);
		}
	}
	if (!bLoginSettingsLoaded)
	{
		Profile.Host=DefaultHost; Profile.Port=FString::FromInt(DefaultPort); Profile.Account=DefaultAccount; Profile.Password=DefaultPassword;
		ACELoginSettings::Load(Profile, ProfilePath.IsEmpty()?ACELoginSettings::GetPath():ProfilePath); Profile.MigrateLegacy(); bLoginSettingsLoaded=true;
		SelectServer(Profile.SelectedServerId);
		if (Profile.DatDirectory.IsEmpty())
			if (auto* GI=GetGameInstance()) if (auto* Dat=GI->GetSubsystem<UACEDatSubsystem>()) Profile.DatDirectory=Dat->GetDatDirectory();
		DatBox->SetText(FText::FromString(Profile.DatDirectory));
		FString Cached, Error;
		if (IFileManager::Get().FileSize(*ACELoginProfile::DirectoryCachePath()) <= 2*1024*1024
			&& FFileHelper::LoadFileToString(Cached,*ACELoginProfile::DirectoryCachePath())) ACELoginProfile::ParseDirectory(Cached,Directory,Error);
		SetStatus(TEXT("Select a server and account, then launch."));
	}
	ForceLayoutPrepass();
}

void UACELoginWidget::NativeDestruct()
{
	SaveLoginEntries();
	if (DirectoryRequest) { DirectoryRequest->OnProcessRequestComplete().Unbind(); DirectoryRequest->CancelRequest(); DirectoryRequest.Reset(); bRefreshingDirectory=false; }
	if (Client)
	{
		Client->OnSessionStateChanged.RemoveDynamic(this,&UACELoginWidget::HandleState);
		Client->OnCharacterList.RemoveDynamic(this,&UACELoginWidget::HandleCharacters);
		Client->OnLogMessage.RemoveDynamic(this,&UACELoginWidget::HandleLog);
	}
	Super::NativeDestruct();
}

void UACELoginWidget::NativeTick(const FGeometry& Geometry, float Dt)
{
	Super::NativeTick(Geometry,Dt);
	// Scale text and pointer targets together. The same design fits a 720p window,
	// a high-DPI/fullscreen desktop, and the dedicated 1100x900 headset surface.
	const FVector2D View=Geometry.GetLocalSize();
	const float Scale=FMath::Clamp(float(FMath::Min(View.X/1100.,View.Y/900.)),.7f,2.5f);
	if (ResponsiveScale && !FMath::IsNearlyEqual(ResponsiveScale->GetUserSpecifiedScale(),Scale)) ResponsiveScale->SetUserSpecifiedScale(Scale);
	const float Width=FMath::Clamp(float(View.X)/Scale-72.f,280.f,1160.f);
	if (DashboardSize && !FMath::IsNearlyEqual(Width,LayoutWidth,1.f))
	{
		LayoutWidth=Width; DashboardSize->SetWidthOverride(Width); FooterSize->SetWidthOverride(Width);
		PlayLayout->SetWrapSize(Width);
		const bool Narrow=Width<900.f;
		SidebarSize->SetWidthOverride(Narrow?Width:280.f); DetailSize->SetWidthOverride(Narrow?Width:Width-301.f);
	}
}

void UACELoginWidget::OnLoginEntryChanged(const FText&) { bLoginEntriesDirty=true; }
void UACELoginWidget::OnLoginEntryCommitted(const FText&, ETextCommit::Type) { SaveLoginEntries(); }
void UACELoginWidget::SaveLoginEntries()
{
	if (!bLoginSettingsLoaded || !bLoginEntriesDirty) return;
	if (ACELoginSettings::Save(Profile, ProfilePath.IsEmpty()?ACELoginSettings::GetPath():ProfilePath)) bLoginEntriesDirty=false;
	else SetStatus(TEXT("Could not save settings on this device. Check available storage."));
}
void UACELoginWidget::OnLoginClicked() { DoLogin(); }
void UACELoginWidget::OnEnterWorldClicked() { DoEnterSelectedCharacter(); }

void UACELoginWidget::RefreshServers()
{
	ServerList->ClearChildren();
	if (Profile.Servers.IsEmpty()) ServerList->AddChild(Label(TEXT("No saved servers yet.\n\nBrowse the directory or add your own."),18,true));
	for (const auto& S:Profile.Servers)
	{
		auto* B=ActionButton(S.Name,TEXT("selectserver"),S.Id,S.Id==Profile.SelectedServerId);
		Cast<UScrollBoxSlot>(ServerList->AddChild(B))->SetPadding(FMargin(0,0,0,8));
	}
}

void UACELoginWidget::SelectServer(const FString& Id)
{
	const FString AccountId = Profile.SelectedAccountId;
	Profile.SelectedServerId=Id; Profile.SelectedAccountId=AccountId;
	const auto* S=Profile.SelectedServer();
	HostBox->SetText(FText::FromString(S?S->Host:TEXT(""))); PortBox->SetText(FText::FromString(FString::FromInt(S?S->Port:9000)));
	ServerTitle->SetText(FText::FromString(S?S->Name:TEXT("Choose your world")));
	ServerDetail->SetText(FText::FromString(S?FString::Printf(TEXT("%s  /  %s:%d"),S->bGDLE?TEXT("GDLE"):TEXT("ACE"),*S->Host,S->Port):TEXT("Add a server to get started.")));
	ServerDescription->SetText(FText::FromString(S?S->Description.Left(240):TEXT("")));
	ServerDescription->SetVisibility(S && !S->Description.IsEmpty()?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
	WebsiteButton->SetIsEnabled(S && ACELoginProfile::IsWebLink(S->Website)); DiscordButton->SetIsEnabled(S && ACELoginProfile::IsWebLink(S->Discord));
	LoginButton->SetIsEnabled(S!=nullptr);
	const auto* A=Profile.Accounts.FindByPredicate([&](const auto& E){return E.Id==AccountId;});
	if (!A && !Profile.Accounts.IsEmpty()) A=&Profile.Accounts[0];
	Profile.SelectedAccountId=A?A->Id:FString();
	AccountBox->SetText(FText::FromString(A?A->Username:TEXT(""))); PasswordBox->SetText(FText::FromString(A?A->Password:TEXT("")));
	Profile.Host=S?S->Host:FString(); Profile.Port=S?FString::FromInt(S->Port):TEXT("9000");
	Profile.Account=A?A->Username:FString(); Profile.Password=A?A->Password:FString();
	RefreshServers(); RefreshAccounts(); bLoginEntriesDirty=true;
}

void UACELoginWidget::RefreshAccounts()
{
	AccountList->ClearChildren();
	for (const auto& A:Profile.Accounts)
		Cast<UScrollBoxSlot>(AccountList->AddChild(ActionButton(A.Username,TEXT("selectaccount"),A.Id,A.Id==Profile.SelectedAccountId)))->SetPadding(FMargin(0,0,0,6));
	if (!AccountList->GetChildrenCount()) AccountList->AddChild(Label(TEXT("Enter an account below to save your first login."),18,true));
}

bool UACELoginWidget::StoreAccount()
{
	const FString User=AccountBox->GetText().ToString().TrimStartAndEnd(), Secret=PasswordBox->GetText().ToString();
	if (User.IsEmpty() || User.Len()>128 || Secret.IsEmpty() || Secret.Len()>256 || User.Contains(TEXT(":")))
	{ SetStatus(TEXT("Enter a username and password. Usernames cannot contain a colon.")); return false; }
	auto* A=Profile.Accounts.FindByPredicate([&](const auto& E){return E.Id==Profile.SelectedAccountId;});
	if (!A) A=Profile.Accounts.FindByPredicate([&](const auto& E){return E.Username==User && E.Password==Secret;});
	if (!A)
	{
		if (Profile.Accounts.Num()>=512) { SetStatus(TEXT("Remove an unused account before adding another.")); return false; }
		A=&Profile.Accounts.AddDefaulted_GetRef(); A->Id=FGuid::NewGuid().ToString();
	}
	A->Username=User; A->Password=Secret; Profile.SelectedAccountId=A->Id;
	Profile.Account=User; Profile.Password=Secret; bLoginEntriesDirty=true; SaveLoginEntries(); RefreshAccounts(); return !bLoginEntriesDirty;
}

void UACELoginWidget::EditServer(bool bNew)
{
	const auto* S=bNew?nullptr:Profile.SelectedServer();
	if (!bNew && !S) { SetStatus(TEXT("Select a server to edit.")); return; }
	EditingServerId=S?S->Id:FString(); bEditingGDLE=S && S->bGDLE;
	NameBox->SetText(FText::FromString(S?S->Name:TEXT(""))); HostBox->SetText(FText::FromString(S?S->Host:TEXT("")));
	PortBox->SetText(FText::FromString(S?FString::FromInt(S->Port):TEXT("9000")));
	DescriptionBox->SetText(FText::FromString(S?S->Description:TEXT("")));
	WebsiteBox->SetText(FText::FromString(S?S->Website:TEXT(""))); DiscordBox->SetText(FText::FromString(S?S->Discord:TEXT("")));
	ButtonStyle(ACETypeButton,!bEditingGDLE); ButtonStyle(GDLETypeButton,bEditingGDLE); Pages->SetActiveWidgetIndex(1);
	SetStatus(TEXT("Choose the login type used by this server."));
}

void UACELoginWidget::DoLogin()
{
	if (!Profile.SelectedServer()) { SetStatus(TEXT("Choose a server first.")); return; }
	if (!StoreAccount()) return;
	const auto* S=Profile.SelectedServer();
	if (Client && S)
	{
		if (auto* GI=GetGameInstance()) if (auto* Dat=GI->GetSubsystem<UACEDatSubsystem>()) Dat->BeginBackgroundLoad(true);
		SetStatus(TEXT("Connecting..."));
		if (!Client->Login(S->Host,S->Port,Profile.Account,Profile.Password,S->bGDLE)) SetStatus(TEXT("Could not start the connection. Check the server address."));
	}
}

void UACELoginWidget::OnSearchChanged(const FText&) { RefreshBrowser(); }
void UACELoginWidget::OnDatChanged(const FText&) { UpdateDatStatus(); }

void UACELoginWidget::RefreshBrowser()
{
	BrowserList->ClearChildren(); const FString Search=SearchBox->GetText().ToString().TrimStartAndEnd();
	for (const auto& S:Directory)
	{
		if (!Search.IsEmpty() && !S.Name.Contains(Search) && !S.Description.Contains(Search) && !S.Host.Contains(Search)) continue;
		const bool Saved=Profile.Servers.ContainsByPredicate([&](const auto& E){return E.Host.Equals(S.Host,ESearchCase::IgnoreCase) && E.Port==S.Port;});
		const FString Name=FString::Printf(TEXT("%s  /  %s%s"),*S.Name,S.bGDLE?TEXT("GDLE"):TEXT("ACE"),Saved?TEXT("  /  Saved"):TEXT(""));
		Cast<UScrollBoxSlot>(BrowserList->AddChild(ActionButton(Name,TEXT("directoryselect"),S.Id,S.Id==BrowserServerId)))->SetPadding(FMargin(0,0,0,6));
	}
	if (!BrowserList->GetChildrenCount()) BrowserList->AddChild(Label(Directory.IsEmpty()?TEXT("No cached directory. Refresh when connected to the internet."):TEXT("No servers match your search."),18,true));
	const auto* S=Directory.FindByPredicate([&](const auto& E){return E.Id==BrowserServerId;});
	BrowserDetail->SetText(FText::FromString(S?FString::Printf(TEXT("%s:%d\n%s"),*S->Host,S->Port,*S->Description.Left(600)):TEXT("Select a server to see its details and community links.")));
}

void UACELoginWidget::FetchDirectory()
{
	if (bRefreshingDirectory) return;
	bRefreshingDirectory=true; SetStatus(TEXT("Refreshing the community directory..."));
	DirectoryRequest=FHttpModule::Get().CreateRequest(); DirectoryRequest->SetURL(ACELoginProfile::DirectoryUrl);
	DirectoryRequest->SetVerb(TEXT("GET")); DirectoryRequest->SetTimeout(20.f);
	DirectoryRequest->SetHeader(TEXT("Accept"),TEXT("application/xml,text/xml,text/plain"));
	TWeakObjectPtr<UACELoginWidget> Weak(this);
	DirectoryRequest->OnProcessRequestComplete().BindLambda([Weak](FHttpRequestPtr, FHttpResponsePtr Response, bool Success)
	{
		if (!Weak.IsValid()) return; auto* W=Weak.Get(); W->bRefreshingDirectory=false;
		FString Error; TArray<FACELoginServer> Parsed;
		if (!Success || !Response || Response->GetResponseCode()!=200 || Response->GetContent().Num()>2*1024*1024)
			Error=TEXT("Directory unavailable. Your saved servers and cached directory are still available.");
		else if (ACELoginProfile::ParseDirectory(Response->GetContentAsString(),Parsed,Error))
		{
			W->Directory=MoveTemp(Parsed);
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(ACELoginProfile::DirectoryCachePath()),true);
			FFileHelper::SaveStringToFile(Response->GetContentAsString(),*ACELoginProfile::DirectoryCachePath(),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
			W->SetStatus(FString::Printf(TEXT("%d community servers. Select one to add it to your list."),W->Directory.Num()));
		}
		if (!Error.IsEmpty()) W->SetStatus(Error);
		W->RefreshBrowser();
	});
	if (!DirectoryRequest->ProcessRequest()) { bRefreshingDirectory=false; SetStatus(TEXT("Could not refresh the directory. You can still use saved servers.")); }
}

void UACELoginWidget::UpdateDatStatus()
{
	if (!FileStatus || !DatBox) return;
	const FString Dir=DatBox->GetText().ToString().TrimStartAndEnd();
	FString Report;
	for (const TCHAR* File:{TEXT("client_portal.dat"),TEXT("client_cell_1.dat"),TEXT("client_highres.dat")})
		Report+=FString::Printf(TEXT("%s   %s\n"),IFileManager::Get().FileSize(*(Dir/File))>0?TEXT("Found"):TEXT("Missing"),File);
	Report+=TEXT("Portal and cell files are required. High-resolution textures are used when available.");
	FileStatus->SetText(FText::FromString(Report));
}

void UACELoginWidget::RunAction(const FString& Action, const FString& Value)
{
	if (Action==TEXT("launch")) { DoLogin(); return; }
	if (Action==TEXT("enter")) { DoEnterSelectedCharacter(); return; }
	if (Action==TEXT("play")) { Pages->SetActiveWidgetIndex(0); SelectServer(Profile.SelectedServerId); }
	else if (Action==TEXT("selectserver")) { SelectServer(Value); SetStatus(TEXT("Server selected. Choose an account and launch.")); }
	else if (Action==TEXT("selectaccount")) { Profile.SelectedAccountId=Value; SelectServer(Profile.SelectedServerId); }
	else if (Action==TEXT("newserver") || Action==TEXT("editserver")) EditServer(Action==TEXT("newserver"));
	else if (Action==TEXT("ace") || Action==TEXT("gdle"))
	{ bEditingGDLE=Action==TEXT("gdle"); ButtonStyle(ACETypeButton,!bEditingGDLE); ButtonStyle(GDLETypeButton,bEditingGDLE); }
	else if (Action==TEXT("saveserver"))
	{
		FACELoginServer S; S.Id=EditingServerId.IsEmpty()?FGuid::NewGuid().ToString():EditingServerId;
		S.Name=NameBox->GetText().ToString().TrimStartAndEnd(); S.Host=HostBox->GetText().ToString().TrimStartAndEnd();
		const FString Port=PortBox->GetText().ToString().TrimStartAndEnd(); S.Port=Port.IsNumeric()?FCString::Atoi(*Port):0;
		S.bGDLE=bEditingGDLE; S.Description=DescriptionBox->GetText().ToString().Left(4096);
		S.Website=WebsiteBox->GetText().ToString().TrimStartAndEnd(); S.Discord=DiscordBox->GetText().ToString().TrimStartAndEnd();
		FString Error; if (!ACELoginProfile::ValidateServer(S,Error)) { SetStatus(Error); return; }
		if (Profile.Servers.ContainsByPredicate([&](const auto& E){return E.Id!=S.Id && E.Host.Equals(S.Host,ESearchCase::IgnoreCase) && E.Port==S.Port;}))
		{ SetStatus(TEXT("This address and port are already in your server list.")); return; }
		auto* Existing=Profile.Servers.FindByPredicate([&](const auto& E){return E.Id==S.Id;});
		if (Existing) *Existing=S;
		else { if (Profile.Servers.Num()>=128) { SetStatus(TEXT("Remove an unused server before adding another.")); return; } Profile.Servers.Add(S); }
		SelectServer(S.Id); Pages->SetActiveWidgetIndex(0); SetStatus(TEXT("Server saved."));
	}
	else if (Action==TEXT("saveaccount")) { if (StoreAccount()) SetStatus(TEXT("Account saved securely on this device.")); }
	else if (Action==TEXT("newaccount"))
	{
		Profile.SelectedAccountId.Reset(); Profile.Account.Reset(); Profile.Password.Reset();
		AccountBox->SetText(FText::GetEmpty()); PasswordBox->SetText(FText::GetEmpty()); RefreshAccounts(); bLoginEntriesDirty=true;
	}
	else if (Action==TEXT("removeserver") || Action==TEXT("removeaccount"))
	{
		if ((Action==TEXT("removeserver") && !Profile.SelectedServer()) || (Action==TEXT("removeaccount") && Profile.SelectedAccountId.IsEmpty())) return;
		PendingRemoval=Action==TEXT("removeserver")?TEXT("s:")+Profile.SelectedServerId:TEXT("a:")+Profile.SelectedAccountId;
		ConfirmText->SetText(FText::FromString(Action==TEXT("removeserver")?TEXT("Remove this server from your saved list? Your accounts will remain available."):TEXT("Remove this saved account from this device?")));
		ConfirmPanel->SetVisibility(ESlateVisibility::Visible);
	}
	else if (Action==TEXT("confirmremove"))
	{
		if (PendingRemoval.StartsWith(TEXT("s:"))) Profile.RemoveServer(PendingRemoval.Mid(2));
		else if (PendingRemoval.StartsWith(TEXT("a:"))) Profile.Accounts.RemoveAll([&](const auto& A){return A.Id==PendingRemoval.Mid(2);});
		PendingRemoval.Reset(); ConfirmPanel->SetVisibility(ESlateVisibility::Collapsed); SelectServer(Profile.SelectedServerId); SetStatus(TEXT("Removed from this device."));
	}
	else if (Action==TEXT("cancelremove")) { PendingRemoval.Reset(); ConfirmPanel->SetVisibility(ESlateVisibility::Collapsed); }
	else if (Action==TEXT("browser")) { Pages->SetActiveWidgetIndex(2); RefreshBrowser(); if (Directory.IsEmpty()) FetchDirectory(); }
	else if (Action==TEXT("refresh")) FetchDirectory();
	else if (Action==TEXT("directoryselect")) { BrowserServerId=Value; RefreshBrowser(); }
	else if (Action==TEXT("import"))
	{
		const auto* S=Directory.FindByPredicate([&](const auto& E){return E.Id==BrowserServerId;});
		if (!S) { SetStatus(TEXT("Select a server in the directory first.")); return; }
		const auto* Existing=Profile.Servers.FindByPredicate([&](const auto& E){return E.Host.Equals(S->Host,ESearchCase::IgnoreCase) && E.Port==S->Port;});
		if (Existing) SelectServer(Existing->Id);
		else
		{
			if (Profile.Servers.Num()>=128) { SetStatus(TEXT("Remove an unused server before adding another.")); return; }
			FACELoginServer Copy=*S; Copy.Id=FGuid::NewGuid().ToString(); Profile.Servers.Add(Copy); SelectServer(Copy.Id);
		}
		Pages->SetActiveWidgetIndex(0); SetStatus(TEXT("Server added. Choose an account to launch."));
	}
	else if (Action==TEXT("website") || Action==TEXT("discord") || Action==TEXT("browserwebsite") || Action==TEXT("browserdiscord"))
	{
		const auto* S=Action.StartsWith(TEXT("browser"))?Directory.FindByPredicate([&](const auto& E){return E.Id==BrowserServerId;}):Profile.SelectedServer();
		if (!S) return; const FString Url=Action.EndsWith(TEXT("discord"))?S->Discord:S->Website;
		if (ACELoginProfile::IsWebLink(Url)) FPlatformProcess::LaunchURL(*Url,nullptr,nullptr);
		else SetStatus(TEXT("This server has not provided that link."));
	}
	else if (Action==TEXT("files")) { Pages->SetActiveWidgetIndex(3); UpdateDatStatus(); }
	else if (Action==TEXT("defaultdat"))
	{
#if PLATFORM_ANDROID
		DatBox->SetText(FText::FromString(FPaths::ProjectSavedDir()/TEXT("DAT")));
#else
		DatBox->SetText(FText::FromString(TEXT("C:/Turbine/Asheron's Call")));
#endif
	}
	else if (Action==TEXT("savedat"))
	{
		FString Dir=DatBox->GetText().ToString().TrimStartAndEnd(); FPaths::NormalizeDirectoryName(Dir);
		if (Dir.IsEmpty() || IFileManager::Get().FileSize(*(Dir/TEXT("client_portal.dat")))<=0 || IFileManager::Get().FileSize(*(Dir/TEXT("client_cell_1.dat")))<=0)
		{ SetStatus(TEXT("Choose a folder containing client_portal.dat and client_cell_1.dat.")); return; }
		Profile.DatDirectory=Dir; bLoginEntriesDirty=true;
		bool Restart=false;
		if (auto* GI=GetGameInstance()) if (auto* Dat=GI->GetSubsystem<UACEDatSubsystem>())
		{
			Restart=(Dat->IsDatReady() || Dat->IsDatLoading()) && Dat->GetDatDirectory()!=Dir;
			if (!Restart) Dat->SetDatDirectory(Dir);
		}
		SetStatus(Restart?TEXT("Location saved. Restart the client to use these game files."):TEXT("Game-file location saved."));
	}
	else if (Action==TEXT("browsefolder"))
	{
#if PLATFORM_WINDOWS
		// Runtime Windows picker; no dependency on editor-only DesktopPlatform.
		const HRESULT Init=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
		IFileOpenDialog* Dialog=nullptr;
		if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&Dialog))))
		{
			DWORD Options=0; Dialog->GetOptions(&Options); Dialog->SetOptions(Options|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM|FOS_NOCHANGEDIR);
			Dialog->SetTitle(L"Choose your Asheron's Call DAT folder");
			if (SUCCEEDED(Dialog->Show(nullptr)))
			{
				IShellItem* Item=nullptr;
				if (SUCCEEDED(Dialog->GetResult(&Item)))
				{
					PWSTR Path=nullptr; if (SUCCEEDED(Item->GetDisplayName(SIGDN_FILESYSPATH,&Path))) { DatBox->SetText(FText::FromString(Path)); CoTaskMemFree(Path); } Item->Release();
				}
			}
			Dialog->Release();
		}
		if (SUCCEEDED(Init)) CoUninitialize();
#endif
	}
	SaveLoginEntries();
}
