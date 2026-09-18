#include "ACESession.h"

void FACESession::SendSocialCommand(const FString& Command,const FString& Arguments,FString& OutError)
{
    if (State!=EACESessionState::InWorld) return;
    FString Rest=Arguments.TrimStartAndEnd();
    auto Take=[&]()
    {
        FString Word,Tail;
        if (!Rest.Split(TEXT(" "),&Word,&Tail)) Word=Rest;
        Rest=Tail.TrimStartAndEnd();return Word.ToLower();
    };
    const FString Topic=Command==TEXT("motd") ? TEXT("motd") : Take();
    FACEBinaryWriter Payload;uint32 Action=0;
    auto Name=[&]()
    {
        FString Value=Rest;
        if (Value.StartsWith(TEXT("\"")) && Value.EndsWith(TEXT("\""))) Value=Value.Mid(1,Value.Len()-2);
        if (Value.IsEmpty()) OutError=TEXT("Please specify a name or message.");
        Payload.WriteString16L(Value);
    };
    if (Topic==TEXT("hometown") || Topic==TEXT("ho")) Action=ACEGameAction::RecallAllegianceHometown;
    else if (Topic==TEXT("broadcast") || Topic==TEXT("br"))
    {
        if (!Rest.IsEmpty()) SendChatChannel(ACEChatChannel::AllegianceBroadcast,Rest);
        else OutError=TEXT("You must specify the text you wish to broadcast!");
        return;
    }
    else if (Topic==TEXT("info")) {Action=0x027B;Name();}
    else if (Topic==TEXT("boot"))
    {
        const bool Account=Rest.StartsWith(TEXT("-account"),ESearchCase::IgnoreCase);
        if (Account) Take();
        Action=0x0277;Name();Payload.WriteUInt32(Account ? 1u : 0u);
    }
    else if (Topic==TEXT("motd") || Topic==TEXT("name"))
    {
        const FString Verb=Take();
        if (Verb.IsEmpty()) Action=Topic==TEXT("motd") ? 0x0255 : 0x0030;
        else if (Verb==TEXT("clear")) Action=Topic==TEXT("motd") ? 0x0256 : 0x0031;
        else if (Verb==TEXT("set")) {Action=Topic==TEXT("motd") ? 0x0254 : 0x0033;Name();}
    }
    else if (Topic==TEXT("ban"))
    {
        const FString Verb=Take();
        if (Verb==TEXT("list")) Action=0x02A3;
        else if (Verb==TEXT("add") || Verb==TEXT("remove")) {Action=Verb==TEXT("add") ? 0x02A1 : 0x02A2;Name();}
    }
    else if (Topic==TEXT("officer") || Topic==TEXT("title"))
    {
        const bool Officer=Topic==TEXT("officer");const FString Verb=Take();
        if (Verb.IsEmpty() || Verb==TEXT("list")) Action=Officer ? 0x02A6 : 0x003D;
        else if (Verb==TEXT("clear")) Action=Officer ? 0x02A7 : 0x003E;
        else if (Officer && Verb==TEXT("remove")) {Action=0x02A5;Name();}
        else if (Verb==TEXT("set") || (Officer && Verb==TEXT("add")))
        {
            const int32 Level=FCString::Atoi(*Take());
            if (Level<1 || Level>3) OutError=TEXT("Please specify an officer level from 1 to 3.");
            Action=Officer ? 0x003B : 0x003C;
            if (Officer) {Name();Payload.WriteUInt32(Level);}
            else {Payload.WriteUInt32(Level);Name();}
        }
    }
    else if (Topic==TEXT("lock"))
    {
        const FString Verb=Take();uint32 Mode=0;
        if (Verb.IsEmpty() || Verb==TEXT("check")) Mode=4;
        else if (Verb==TEXT("off")) Mode=1;
        else if (Verb==TEXT("on")) Mode=2;
        else if (Verb==TEXT("toggle")) Mode=3;
        else if (Verb==TEXT("bypass"))
        {
            if (Rest.IsEmpty()) Mode=5;
            else if (Rest.Equals(TEXT("clear"),ESearchCase::IgnoreCase)) Mode=6;
            else {Action=0x0040;Name();}
        }
        if (Mode) {Action=0x003F;Payload.WriteUInt32(Mode);}
    }
    else if (Topic==TEXT("house"))
    {
        const FString Verb=Take();uint32 Mode=0;
        if (Verb.IsEmpty()) Mode=1;
        else if (Verb==TEXT("guest")) Mode=Rest.Equals(TEXT("open"),ESearchCase::IgnoreCase) ? 2 : Rest.Equals(TEXT("close"),ESearchCase::IgnoreCase) ? 3 : 0;
        else if (Verb==TEXT("storage")) Mode=Rest.Equals(TEXT("open"),ESearchCase::IgnoreCase) ? 4 : Rest.Equals(TEXT("close"),ESearchCase::IgnoreCase) ? 5 : 0;
        if (Mode) {Action=0x0042;Payload.WriteUInt32(Mode);}
    }
    else if (Topic==TEXT("chat") || Topic==TEXT("ch"))
    {
        const FString Verb=Take();
        if (Verb==TEXT("gag") || Verb==TEXT("ungag")) {Action=0x0041;Name();Payload.WriteUInt32(Verb==TEXT("gag") ? 1u : 0u);}
        else if (Verb==TEXT("kick"))
        {
            FString Target,Reason;
            if (!Rest.Split(TEXT(","),&Target,&Reason)) {Target=Rest;Reason=TEXT("No reason given.");}
            Rest=Target.TrimStartAndEnd();Action=0x02A0;Name();Payload.WriteString16L(Reason.TrimStartAndEnd());
        }
    }
    if (!Action && OutError.IsEmpty()) OutError=TEXT("See @help allegiance for command syntax.");
    if (Action && OutError.IsEmpty()) SendGameAction(Action,Payload.GetData(),ACEQueue::WeenieQueue);
}
