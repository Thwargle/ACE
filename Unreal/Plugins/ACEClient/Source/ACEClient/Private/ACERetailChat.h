#pragma once
#include "CoreMinimal.h"
#include "ACEOpcodes.h"

namespace ACERetailChat
{
    // gmChatOptionsUI::InitOptions / AddCheckboxBitfield64Option.
    inline constexpr uint64 MainWindowDefault = 0xFBFFFFFFull;
    inline constexpr uint64 FloatyDefaults[] = {4124ull, 265216ull, 0x80000ull, 2013265920ull};
    struct FFilterGroup { const TCHAR* Label; uint64 Mask; };
    inline constexpr FFilterGroup FilterGroups[] = {
        {TEXT("Gameplay"),0x83912021ull}, {TEXT("Combat"),0x600040ull},
        {TEXT("Magic"),0x20080ull}, {TEXT("Area speech"),0x1004ull},
        {TEXT("Tells"),0x18ull}, {TEXT("Allegiance"),0x40C00ull},
        {TEXT("Fellowship"),0x80000ull}, {TEXT("General"),0x8000000ull},
        {TEXT("Trade"),0x10000000ull}, {TEXT("LFG"),0x20000000ull},
        {TEXT("Roleplay"),0x40000000ull}, {TEXT("Society"),0x100000000ull},
        {TEXT("Errors"),0x4000000ull}
    };
    struct FChannel { uint32 Id; const TCHAR* Name; const TCHAR* Alias; };
    inline constexpr FChannel Channels[] = {
        {1,TEXT("Abuse"),TEXT("abuse")},{2,TEXT("Admin"),TEXT("ad")},
        {4,TEXT("Audit"),TEXT("au")},{8,TEXT("Av1"),TEXT("advocate")},
        {16,TEXT("Av2"),TEXT("av2")},{32,TEXT("Av3"),TEXT("av3")},
        {64,TEXT("QA1"),TEXT("qa1")},{128,TEXT("QA2"),TEXT("qa2")},
        {256,TEXT("Debug"),TEXT("debug")},{512,TEXT("Sentinel"),TEXT("sent")},
        {1024,TEXT("Help"),TEXT("help")},{2048,TEXT("Fellowship"),TEXT("fellow")},
        {4096,TEXT("Vassals"),TEXT("vassals")},{8192,TEXT("Patron"),TEXT("patron")},
        {16384,TEXT("Monarch"),TEXT("monarch")},{32768,TEXT("Al-Arqas"),TEXT("alarqas")},
        {65536,TEXT("Holtburg"),TEXT("holtburg")},{131072,TEXT("Lytelthorpe"),TEXT("lytelthorpe")},
        {262144,TEXT("Nanto"),TEXT("nanto")},{524288,TEXT("Rithwic"),TEXT("rithwic")},
        {1048576,TEXT("Samsur"),TEXT("samsur")},{2097152,TEXT("Shoushi"),TEXT("shoushi")},
        {4194304,TEXT("Yanshi"),TEXT("yanshi")},{8388608,TEXT("Yaraq"),TEXT("yaraq")},
        {0x01000000,TEXT("Co-Vassals"),TEXT("covassals")},
        {0x02000000,TEXT("Allegiance Broadcast"),TEXT("ab")}
    };
    inline uint32 FindChannel(const FString& Name)
    {
        for (const auto& C : Channels)
            if (Name.Equals(C.Name,ESearchCase::IgnoreCase) || Name.Equals(C.Alias,ESearchCase::IgnoreCase)) return C.Id;
        return 0;
    }
    inline FString ChannelName(uint32 Id)
    {
        for (const auto& C : Channels) if (C.Id==Id) return C.Name;
        return FString::Printf(TEXT("%u"),Id);
    }
    // ClientCommunicationSystem::OnChannelBroadcast: retain the destination's
    // wording and log type, including the empty sender used for a server echo.
    inline FString Format(uint32 Id, const FString& Sender, const FString& Message, int32& Type)
    {
        const bool Self=Sender.IsEmpty();
        const FString Speaker=Self ? TEXT("You say") : Sender+TEXT(" says");
        Type=Self ? ACEChatMessageType::ChannelSend : ACEChatMessageType::Channel;
        FString Prefix;
        if (Id==0x04000000) { Type=ACEChatMessageType::Fellowship; return Message; }
        if (Id==ACEChatChannel::Fellow || Id==ACEChatChannel::CoVassals || Id==ACEChatChannel::AllegianceBroadcast)
        {
            Type=Id==ACEChatChannel::Fellow ? ACEChatMessageType::Fellowship : ACEChatMessageType::Social;
            Prefix=FString::Printf(TEXT("[%s] %s"),*ChannelName(Id),*Speaker);
        }
        else if (Id==ACEChatChannel::Vassals || Id==ACEChatChannel::Patron || Id==ACEChatChannel::Monarch)
        {
            Type=Self ? ACEChatMessageType::SocialSend : ACEChatMessageType::Social;
            const FString Relation=Id==ACEChatChannel::Vassals ? TEXT("patron") : Id==ACEChatChannel::Patron ? TEXT("vassal") : TEXT("follower");
            Prefix=Self ? FString::Printf(TEXT("You say to your %s"),*ChannelName(Id).ToLower())
                : FString::Printf(TEXT("Your %s %s says to you"),*Relation,*Sender);
        }
        else
        {
            if (Id==1) Type=ACEChatMessageType::Abuse;
            if (Id==1024) Type=ACEChatMessageType::Help;
            Prefix=FString::Printf(TEXT("%s on the %s channel"),*Speaker,*ChannelName(Id));
        }
        return FString::Printf(TEXT("%s, \"%s\""),*Prefix,*Message);
    }
}
