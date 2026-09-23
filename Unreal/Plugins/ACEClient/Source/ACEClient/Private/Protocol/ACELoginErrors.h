#pragma once

#include "CoreMinimal.h"

namespace ACELoginErrors
{
    // CharError / ACE.Server.Network.Enum.CharacterError. Preserve the code for
    // reports; never infer an incorrect password from an unknown rejection.
    inline FString Character(uint32 Code)
    {
        const TCHAR* Reason = nullptr;
        switch (Code)
        {
        case 0x01: Reason = TEXT("This account is already logged in on this server. Log out of the other client, or wait for its disconnected session to expire, then try again."); break;
        case 0x03: Reason = TEXT("The server could not load your account information. Try again in a few minutes."); break;
        case 0x04:
        case 0x08: Reason = TEXT("The server disconnected. It may be restarting or unavailable. Try again in a few minutes."); break;
        case 0x05: Reason = TEXT("The server could not log out your character. Wait a moment, then try again."); break;
        case 0x06: Reason = TEXT("The server could not delete this character. Try again; if it continues, contact the server administrator."); break;
        case 0x09: Reason = TEXT("The server rejected the account name or login method. Check your username and the server's ACE/GDLE setting."); break;
        case 0x0A: Reason = TEXT("This account does not exist on this server. Check the username and selected server, or follow that server's account-registration instructions."); break;
        case 0x0B:
        case 0x11: Reason = TEXT("The server could not enter the world with this character. Try again; if it continues, contact the server administrator."); break;
        case 0x0C: Reason = TEXT("The server does not allow this test character to enter the world. Choose another character or contact the server administrator."); break;
        case 0x0D:
        case 0x10: Reason = TEXT("A character on this account is still in the world. Log out of the other client, or wait for the server to finish logging that character out."); break;
        case 0x0E: Reason = TEXT("The server could not find the account for this character. Try logging in again; if it continues, contact the server administrator."); break;
        case 0x0F: Reason = TEXT("This character does not belong to the logged-in account. Log in with the account that owns it."); break;
        case 0x12: Reason = TEXT("The server could not read this character's saved data. Contact the server administrator for help recovering it."); break;
        case 0x13: Reason = TEXT("The server for this character's starting area is unavailable. Try again in a few minutes."); break;
        case 0x14: Reason = TEXT("The server could not place this character in the world. Try again in a few minutes; if it continues, contact the server administrator."); break;
        case 0x15: Reason = TEXT("This server is full. Wait for a player slot to become available, then try again."); break;
        case 0x17: Reason = TEXT("The server is still saving this character. Wait a moment, then try again."); break;
        case 0x18: Reason = TEXT("The server reports that this account's subscription or access has expired. Contact the server administrator."); break;
        default: Reason = TEXT("The server rejected the request without a recognized explanation. If it continues, report this error code to the server administrator."); break;
        }
        return FString::Printf(TEXT("%s (error %u)"), Reason, Code);
    }

    // Retail StringInfo table 8, also sent by GDLE NetworkDefs.h. These hashes
    // are a separate namespace from the numeric CharacterError codes above.
    inline const TCHAR* Network(uint32 StringId, uint32 TableId)
    {
        if (TableId == 8)
        {
            switch (StringId)
            {
            case 0x0C559B1E: return TEXT("This account is already logged in on this server. Log out of the other client, or wait for its disconnected session to expire, then try again.");
            case 0x00F9982C: return TEXT("This server is full. Wait for a player slot to become available, then try again.");
            case 0x00A7E948: return TEXT("The server does not accept this client version. Check for a client update and the server's supported version.");
            case 0x082E3779: return TEXT("The server does not accept this login method. Check the server's ACE/GDLE setting and its login requirements.");
            case 0x04DF9C54: return TEXT("The server rejected the login. Check your username, password, and selected server. This server did not provide a more specific reason.");
            default: break;
            }
        }
        return TEXT("The server reported an unrecognized connection error. If it continues, report the error code and table to the server administrator.");
    }
}
