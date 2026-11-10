#include "ACESession.h"

namespace
{
bool IsBlankBookPage(const FString& Text)
{
	// gmBookUI::PageTextBlank treats spaces and newlines as blank, not tabs.
	for (TCHAR C : Text) if (C != TEXT(' ') && C != TEXT('\n') && C != TEXT('\r') && C != 0) return false;
	return true;
}
}

void FACESession::ResetBookEditing()
{
	BookWriteAction = 0;
	BookWritePage = BookNextPage = INDEX_NONE;
	BookWriteRequestedAt = 0.;
	BookDraft.Reset(); BookWriteText.Reset();
	bBookDraft = bBookRefreshRequired = false;
}

bool FACESession::CanEditBookPage() const
{
	if (State != EACESessionState::InWorld || !PlayerGuid || !Book.bOpen || BookWriteAction || bBookRefreshRequired
		|| !Book.HasPageText(Book.CurrentPage) || !Book.PageAuthors.IsValidIndex(Book.CurrentPage)) return false;
	const auto& Author = Book.PageAuthors[Book.CurrentPage];
	return Author.Guid == PlayerGuid || Author.bIgnoreAuthor;
}

FString FACESession::GetBookPageText() const
{
	return bBookDraft ? BookDraft : Book.HasPageText(Book.CurrentPage) ? Book.Pages[Book.CurrentPage] : FString();
}

bool FACESession::SetBookPageText(const FString& Text)
{
	if (!CanEditBookPage() || Text.Len() > FMath::Min(Book.MaxCharsPerPage, 65535)) return false;
	BookDraft = Text.Replace(TEXT("\r\n"), TEXT("\n"));
	bBookDraft = true;
	return true;
}

bool FACESession::SaveBookPage(int32 NextPage)
{
	if (!CanEditBookPage()) return false;
	const FString Text = GetBookPageText();
	const bool Delete = Book.PageAuthors[Book.CurrentPage].Guid == PlayerGuid && IsBlankBookPage(Text);
	if (!Delete && Text == Book.Pages[Book.CurrentPage]) return false;
	BookWriteAction = Delete ? ACEGameAction::BookDeletePage : ACEGameAction::BookModifyPage;
	BookWritePage = Book.CurrentPage; BookNextPage = NextPage;
	BookWriteText = Text; BookWriteRequestedAt = FPlatformTime::Seconds();
	FACEBinaryWriter W;
	W.WriteInt32(Book.BookGuid); W.WriteInt32(BookWritePage);
	if (!Delete) W.WriteString16L(Text);
	SendGameAction(BookWriteAction, W.GetData(), ACEQueue::WeenieQueue);
	return true;
}

void FACESession::TurnBookPage(int32 PageIndex)
{
	CheckBookWriteTimeout();
	if (State != EACESessionState::InWorld || !Book.bOpen || BookWriteAction || bBookRefreshRequired
		|| PageIndex < 0 || PageIndex >= Book.MaxPages || PageIndex > Book.NumPages
		|| (PageIndex == Book.CurrentPage && Book.Pages.IsValidIndex(PageIndex))) return;
	if (Book.CurrentPage == Book.NumPages-1 && PageIndex > Book.CurrentPage && CanEditBookPage()
		&& Book.PageAuthors[Book.CurrentPage].Guid == PlayerGuid && IsBlankBookPage(GetBookPageText()))
	{
		EmitChatMessage(TEXT("This book is already open to a blank page."), FString(), ACEChatMessageType::ChatError);
		return;
	}
	if (PendingBookPage != INDEX_NONE && FPlatformTime::Seconds()-BookPageRequestedAt < 5.0) return;
	if (!SaveBookPage(PageIndex)) OpenBookPage(PageIndex);
	OnBookChanged.Broadcast();
}

void FACESession::OpenBookPage(int32 PageIndex)
{
	if (PageIndex < 0 || PageIndex >= Book.MaxPages) return;
	if (Book.Pages.IsValidIndex(PageIndex)) { SendBookPageData(Book.BookGuid, PageIndex); return; }
	if (PageIndex != Book.NumPages || State != EACESessionState::InWorld || !Book.bOpen) return;
	// Appending is allowed up to capacity, even after pages by other authors.
	// The server still decides whether the book can be accessed or changed.
	BookWriteAction = ACEGameAction::BookAddPage;
	BookWritePage = BookNextPage = PageIndex; BookWriteRequestedAt = FPlatformTime::Seconds();
	FACEBinaryWriter W; W.WriteInt32(Book.BookGuid);
	SendGameAction(BookWriteAction, W.GetData(), ACEQueue::WeenieQueue);
}

void FACESession::CheckBookWriteTimeout()
{
	if (!BookWriteAction || FPlatformTime::Seconds()-BookWriteRequestedAt < 10.0) return;
	// Never retry a write against a page index whose server state is unknown.
	BookWriteAction = 0; bBookRefreshRequired = true;
	EmitChatMessage(TEXT("The book update was not confirmed. Close and reopen the book before editing again."), FString(), ACEChatMessageType::ChatError);
	OnBookChanged.Broadcast();
}

void FACESession::HandleBookWriteResponse(uint32 Event, FACEBinaryReader& R)
{
	if (!R.CanRead(12)) return;
	const int32 Guid = R.ReadInt32(), Page = R.ReadInt32();
	const bool Success = R.ReadUInt32() != 0;
	const uint32 Action = Event == ACEGameEvent::BookAddPageResponse ? ACEGameAction::BookAddPage
		: Event == ACEGameEvent::BookDeletePageResponse ? ACEGameAction::BookDeletePage : ACEGameAction::BookModifyPage;
	if (!Book.bOpen || Guid != Book.BookGuid || !BookWriteAction || Action != BookWriteAction) return;
	if (Page != BookWritePage)
	{
		if (Action != ACEGameAction::BookAddPage) return;
		// Another writer can change the append index. Refresh rather than inventing intervening pages.
		BookWriteAction = 0; bBookRefreshRequired = true; SendBookData(Guid); OnBookChanged.Broadcast(); return;
	}
	BookWriteAction = 0;
	if (!Success)
	{
		EmitChatMessage(TEXT("The server could not update this book page. Your text has been kept in the editor."), FString(), ACEChatMessageType::ChatError);
		OnBookChanged.Broadcast(); return;
	}
	int32 NextPage = BookNextPage;
	if (Action == ACEGameAction::BookAddPage)
	{
		if (Page != Book.Pages.Num()) return;
		Book.Pages.Add(FString()); Book.PageTextIncluded.Add(true);
		FACEBookPageAuthor Author; Author.Guid = PlayerGuid; Author.Name = GetPlayerName();
		Book.PageAuthors.Add(MoveTemp(Author)); Book.NumPages = Book.Pages.Num(); Book.CurrentPage = Page;
		bBookDraft = false; BookDraft.Reset();
		// Read back actual page permissions (including IgnoreAuthor) after creation.
		bBookRefreshRequired = true; SendBookData(Guid); OnBookChanged.Broadcast(); return;
	}
	if (!Book.Pages.IsValidIndex(Page)) return;
	if (Action == ACEGameAction::BookDeletePage)
	{
		Book.Pages.RemoveAt(Page); Book.PageTextIncluded.RemoveAt(Page); Book.PageAuthors.RemoveAt(Page);
		Book.NumPages = Book.Pages.Num();
		if (NextPage > Page) --NextPage;
		Book.CurrentPage = INDEX_NONE;
	}
	else
	{
		Book.Pages[Page] = BookWriteText;
		// Shared pages become attributed to the player who wrote them on ACE.
		Book.PageAuthors[Page].Guid = PlayerGuid; Book.PageAuthors[Page].Name = GetPlayerName();
	}
	bBookDraft = false; BookDraft.Reset(); BookWriteText.Reset();
	OpenBookPage(NextPage);
	OnBookChanged.Broadcast();
}
