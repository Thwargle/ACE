#pragma once
#include "ACETypes.h"

namespace ACEOlthoi
{
    inline bool IsHeritage(int32 Heritage) { return Heritage == 12 || Heritage == 13; }

    // gmMainChatUI::SetTalkFocusEnabled: local speech, selected tell and Olthoi.
    // These are our destination indices, not retail's TalkFocus enumeration.
    inline bool CanUseChatDestination(int32 Heritage, int32 Destination)
    {
        return IsHeritage(Heritage) ? (Destination == 0 || Destination == 12 || Destination == 13)
            : Destination != 13;
    }

    // ACE Player.GetNameWithSuffix / retail ClientCommunicationSystem. Strip
    // the language marker before squelch/reply matching, never from message text.
    inline bool DecodeSpeaker(FString& Name, int32 ListenerHeritage, bool NoOlthoiTalk)
    {
        const bool Bypass = Name.EndsWith(TEXT("^"));
        const bool SpeakerOlthoi = Name.EndsWith(TEXT("&"));
        if (Bypass || SpeakerOlthoi) Name.LeftChopInline(1);
        return !Bypass && !NoOlthoiTalk && SpeakerOlthoi != IsHeritage(ListenerHeritage);
    }

    inline FString ForeignSpeech(bool SpeakerOlthoi)
    {
        // GetRandomOlthoiText / GetRandomHumanText, retail 00681700 / 00681870.
        static const TCHAR* Olthoi[] = {
            TEXT("glares menacingly."), TEXT("clicks its pincers together in anticipation of destruction."),
            TEXT("screeches in a horrible fashion."), TEXT("lets out a maddening series of clicks and hisses."),
            TEXT("surveys the area as acid drips from its mandibles."), TEXT("hisses some kind of threat."),
            TEXT("calls out searching for other Olthoi."), TEXT("cries out to indicate to its kin that prey is near."),
            TEXT("casts about looking for victims."), TEXT("prepares to hunt enemies of the queen.")};
        static const TCHAR* Human[] = {
            TEXT("cowers in fear."), TEXT("calls out for help and prepares to fight you."),
            TEXT("seems startled to see Olthoi."), TEXT("cries out to warn others that Olthoi are present."),
            TEXT("surveys the area with weapons at the ready."), TEXT("regards you warily."),
            TEXT("lets out a battle cry as a challenge."), TEXT("goes into a defensive posture at the sight of you."),
            TEXT("seems to be looking for an escape route."), TEXT("gestures a challenge to you.")};
        const int32 Index = FMath::RandRange(0, 9);
        return SpeakerOlthoi ? Olthoi[Index] : Human[Index];
    }
}
