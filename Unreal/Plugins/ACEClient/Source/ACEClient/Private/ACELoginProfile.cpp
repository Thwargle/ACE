#include "ACELoginProfile.h"
#include "XmlFile.h"
#include "Misc/Paths.h"

void FACELoginSettings::MigrateLegacy()
{
	if (bProfileFormat) return;
	bProfileFormat = true;
	if (!Account.IsEmpty())
	{
		FACELoginAccount Entry{FGuid::NewGuid().ToString(), Account, Password};
		Accounts.Add(Entry); SelectedAccountId = Entry.Id;
	}
	if (Host.IsEmpty()) return;
	FACELoginServer Server;
	Server.Id = FGuid::NewGuid().ToString(); Server.Name = Host; Server.Host = Host;
	Server.Port = Port.IsNumeric() ? FCString::Atoi(*Port) : 9000;
	FString Error;
	if (!ACELoginProfile::ValidateServer(Server, Error)) return;
	Servers.Add(Server); SelectedServerId = Server.Id;
}

const FACELoginServer* FACELoginSettings::SelectedServer() const
{
	return Servers.FindByPredicate([&](const auto& S) { return S.Id == SelectedServerId; });
}

void FACELoginSettings::RemoveServer(const FString& Id)
{
	Servers.RemoveAll([&](const auto& S) { return S.Id == Id; });
	if (SelectedServerId == Id)
	{
		SelectedServerId = Servers.IsEmpty() ? FString() : Servers[0].Id;
		Host.Reset();
	}
}

bool ACELoginProfile::IsWebLink(const FString& Url)
{
	return Url.Len() <= 2048 && !Url.Contains(TEXT("\n")) && !Url.Contains(TEXT("\r"))
		&& (Url.StartsWith(TEXT("https://")) || Url.StartsWith(TEXT("http://")))
		&& Url.Len() > (Url.StartsWith(TEXT("https://")) ? 8 : 7);
}

bool ACELoginProfile::ValidateServer(const FACELoginServer& S, FString& Error)
{
	if (S.Name.TrimStartAndEnd().IsEmpty() || S.Name.Len() > 100) Error = TEXT("Enter a server name (up to 100 characters).");
	else if (S.Host.IsEmpty() || S.Host.Len() > 253 || S.Host.Contains(TEXT(":")) || S.Host.Contains(TEXT("/"))
		|| S.Host.Contains(TEXT("\\")) || S.Host.Contains(TEXT("@")) || S.Host.Contains(TEXT("?"))
		|| S.Host.Contains(TEXT("#")) || S.Host.Contains(TEXT(" ")) || S.Host.Contains(TEXT("\t"))
		|| S.Host.Contains(TEXT("\r")) || S.Host.Contains(TEXT("\n"))) Error = TEXT("Enter a hostname or IPv4 address, without a URL or port.");
	else if (S.Port < 1 || S.Port > 65534) Error = TEXT("Port must be between 1 and 65534 (the connection also uses port + 1).");
	else if ((!S.Website.IsEmpty() && !IsWebLink(S.Website)) || (!S.Discord.IsEmpty() && !IsWebLink(S.Discord)))
		Error = TEXT("Website and Discord links must start with https:// or http://.");
	else { Error.Reset(); return true; }
	return false;
}

FString ACELoginProfile::DirectoryCachePath()
{
	return FPaths::ProjectSavedDir() / TEXT("Login/Servers.xml");
}

bool ACELoginProfile::ParseDirectory(const FString& Text, TArray<FACELoginServer>& Out, FString& Error)
{
	if (Text.Len() > 2 * 1024 * 1024 || Text.Contains(TEXT("<!DOCTYPE")) || Text.Contains(TEXT("<!ENTITY")))
	{ Error = TEXT("The server directory has an unsupported format."); return false; }
	FXmlFile File(Text, EConstructMethod::ConstructFromBuffer);
	const auto* Root = File.GetRootNode();
	if (!File.IsValid() || !Root || Root->GetTag() != TEXT("ArrayOfServerItem"))
	{ Error = TEXT("Could not read the server directory. Your saved servers are unchanged."); return false; }
	TArray<FACELoginServer> Parsed;
	TSet<FString> Endpoints;
	for (const auto* Node : Root->GetChildrenNodes())
	{
		if (Node->GetTag() != TEXT("ServerItem") || Parsed.Num() >= 1000) continue;
		auto Field = [&](const TCHAR* Name)
		{
			const auto* Child = Node->FindChildNode(Name);
			FString Value = Child ? Child->GetContent().TrimStartAndEnd() : FString();
			// FXmlFile preserves entity spelling in node content. Decode once,
			// with ampersand last so escaped literal entities remain literal.
			Value.ReplaceInline(TEXT("&lt;"), TEXT("<")); Value.ReplaceInline(TEXT("&gt;"), TEXT(">"));
			Value.ReplaceInline(TEXT("&quot;"), TEXT("\"")); Value.ReplaceInline(TEXT("&apos;"), TEXT("'"));
			Value.ReplaceInline(TEXT("&amp;"), TEXT("&")); return Value;
		};
		const FString Type = Field(TEXT("emu")).ToUpper();
		if (Type != TEXT("ACE") && Type != TEXT("GDLE") && Type != TEXT("GDL")) continue;
		const FString Visibility = Field(TEXT("visibility"));
		if (!Visibility.IsEmpty() && !Visibility.Equals(TEXT("Visible"), ESearchCase::IgnoreCase)) continue;
		FACELoginServer S;
		S.Id = Field(TEXT("id")); S.Name = Field(TEXT("name")); S.bGDLE = Type != TEXT("ACE");
		S.Host = Field(TEXT("server_host")); FString Port = Field(TEXT("server_port"));
		if (S.Host.IsEmpty()) Field(TEXT("connect_string")).Split(TEXT(":"), &S.Host, &Port, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		S.Port = Port.IsNumeric() ? FCString::Atoi(*Port) : 0;
		S.Description = Field(TEXT("description")).Left(4096);
		S.Website = Field(TEXT("website_url")); S.Discord = Field(TEXT("discord_url"));
		if (S.Discord.IsEmpty()) S.Discord = Field(TEXT("DiscordUrl"));
		if (!IsWebLink(S.Website)) S.Website.Reset();
		if (!IsWebLink(S.Discord)) S.Discord.Reset();
		FString Validation;
		const FString Endpoint = S.Host.ToLower() + TEXT(":") + FString::FromInt(S.Port);
		if (!ValidateServer(S, Validation) || Endpoints.Contains(Endpoint)) continue;
		Endpoints.Add(Endpoint);
		if (S.Id.IsEmpty()) S.Id = Endpoint;
		Parsed.Add(MoveTemp(S));
	}
	if (Parsed.IsEmpty()) { Error = TEXT("No supported ACE or GDLE servers were found."); return false; }
	Parsed.Sort([](const auto& A, const auto& B) { return A.Name.Compare(B.Name, ESearchCase::IgnoreCase) < 0; });
	Out = MoveTemp(Parsed); Error.Reset(); return true;
}
