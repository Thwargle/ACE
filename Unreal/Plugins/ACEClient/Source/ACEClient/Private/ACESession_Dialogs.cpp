#include "ACESession.h"

namespace
{
bool ReadDialogText(FACEBinaryReader& R, FString& Text)
{
	if (!R.CanRead(2)) return false;
	const int32 Start = R.Tell(), Length = R.ReadUInt16();
	R.Seek(Start);
	if (!R.CanRead(Align(Length + 2, 4))) return false;
	Text = R.ReadString16L();
	return R.Remaining() == 0 && !Text.IsEmpty();
}
}

void FACESession::HandleServerDialog(uint32 Event, FACEBinaryReader& R)
{
	if (State != EACESessionState::InWorld) return;
	if (Event == ACEGameEvent::PopupString)
	{
		FString Message;
		if (!ReadDialogText(R, Message)) return;
		// Retail PopUpString creates a MessageDialog with no confirmation callback.
		// Present successive messages in order, rather than obscuring earlier text.
		// Preserve overflow in chat while keeping server-controlled UI memory bounded.
		if (ServerPopups.Num() >= 64)
			EmitChatMessage(Message, FString(), ACEChatMessageType::System);
		else
			ServerPopups.Add({++NextServerPopupId, MoveTemp(Message)});
	}
	else if (Event == 0x0274)
	{
		if (!R.CanRead(10)) return;
		FACEConfirmation Request;
		Request.Type = R.ReadUInt32(); Request.Context = R.ReadUInt32();
		if (!ReadDialogText(R, Request.Prompt)) return;
		if (auto* Existing = Confirmations.FindByPredicate([&](const auto& C) { return C.Type == Request.Type && C.Context == Request.Context; }))
			*Existing = MoveTemp(Request);
		else if (Confirmations.Num() < 16)
			Confirmations.Add(MoveTemp(Request));
	}
	else if (Event == 0x0276 && R.Remaining() == 8)
	{
		const uint32 Type = R.ReadUInt32(), Context = R.ReadUInt32();
		Confirmations.RemoveAll([&](const auto& C) { return C.Type == Type && C.Context == Context; });
	}
}

bool FACESession::DismissServerPopup(uint64 Id)
{
	if (State != EACESessionState::InWorld || !Id) return false;
	return ServerPopups.RemoveAll([Id](const auto& P) { return P.Id == Id; }) != 0;
}
