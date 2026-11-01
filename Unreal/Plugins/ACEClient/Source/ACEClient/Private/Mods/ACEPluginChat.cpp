#include "Mods/ACEPluginSubsystem.h"
#include "ACEClientSubsystem.h"
#include "Engine/GameInstance.h"
#include "ACESession.h"
#include "ACEVTRegex.h"

bool UACEPluginSubsystem::ValidatePluginChatFilters(const TArray<TSharedPtr<FJsonValue>>& Values,
    TArray<FACEPluginChatFilter>& Filters,FString& Error) const
{
    // Rules run before display without invoking Lua. Patterns use the same
    // bounded work/stack matcher as loot profiles.
    if(Values.Num()>32){Error=TEXT("At most 32 chat filters per plugin");return false;}
    TSet<FString> Ids;
    for(const auto& Value:Values)
    {
        if(!Value||Value->Type!=EJson::Object){Error=TEXT("Chat filters must be objects");return false;}
        const auto O=Value->AsObject();FACEPluginChatFilter F;
        for(const auto& Field:O->Values)
        {
            const FString Key(Field.Key);const auto& V=Field.Value;
            if(Key==TEXT("id")||Key==TEXT("contains")||Key==TEXT("prefix")||Key==TEXT("exact")||Key==TEXT("sender")||Key==TEXT("sender_type")||Key==TEXT("pattern")||Key==TEXT("action")||Key==TEXT("color"))
            {
                FString Text;
                if(!V->TryGetString(Text)||Text.Len()>(Key==TEXT("id")?64:256)){Error=TEXT("Chat filter text exceeds its limit or is not a string");return false;}
                if(Key==TEXT("id"))F.Id=Text;
                else if(Key==TEXT("contains"))F.Contains=Text;
                else if(Key==TEXT("prefix"))F.Prefix=Text;
                else if(Key==TEXT("exact"))F.Exact=Text;
                else if(Key==TEXT("sender")){F.Sender=Text;F.HasSender=true;}
                else if(Key==TEXT("sender_type"))
                {if(!TArray<FString>{TEXT("player"),TEXT("npc"),TEXT("vendor"),TEXT("server"),TEXT("unknown")}.Contains(Text)){Error=TEXT("Invalid sender_type");return false;}F.SenderType=Text;}
                else if(Key==TEXT("pattern")){if(!ACEVTRegex::Validate(Text,Error))return false;F.Pattern=Text;}
                else if(Key==TEXT("action")){if(!TArray<FString>{TEXT("hide"),TEXT("show"),TEXT("color")}.Contains(Text)){Error=TEXT("Invalid chat rule action");return false;}F.Action=Text;}
                else
                {
                    static const TMap<FString,FLinearColor> Named={{TEXT("red"),FLinearColor::Red},{TEXT("green"),FLinearColor::Green},{TEXT("blue"),FLinearColor::Blue},{TEXT("yellow"),FLinearColor::Yellow},{TEXT("white"),FLinearColor::White}};
                    if(const auto* Color=Named.Find(Text))F.Color=*Color;
                    else if(Text.StartsWith(TEXT("#"))&&Text.Len()==7){for(int32 I=1;I<Text.Len();++I)if(!FChar::IsHexDigit(Text[I])){Error=TEXT("Invalid hex color");return false;}F.Color=FLinearColor(FColor::FromHex(Text));}
                    else {Error=TEXT("Color must be red, green, blue, yellow, white or #RRGGBB");return false;}
                }
            }
            else if(Key==TEXT("case_sensitive"))
            {
                if(V->Type!=EJson::Boolean){Error=TEXT("case_sensitive must be boolean");return false;}
                F.CaseSensitive=V->AsBool();
            }
            else if(Key==TEXT("window")||Key==TEXT("chat_type")||Key==TEXT("seconds"))
            {
                double Number=0;
                if(V->Type!=EJson::Number||!V->TryGetNumber(Number)||!FMath::IsFinite(Number))
                {Error=TEXT("Invalid chat filter numeric value");return false;}
                if(Key==TEXT("seconds"))
                {
                    if(Number<=0||Number>300){Error=TEXT("Chat filter seconds must be greater than 0 and at most 300");return false;}
                    F.Expires=FPlatformTime::Seconds()+Number;
                }
                else
                {
                    if(Number!=FMath::FloorToDouble(Number)||Number<(Key==TEXT("window")?-1:0)||Number>(Key==TEXT("window")?4:63))
                    {Error=TEXT("Chat filter window must be -1..4; chat_type must be 0..63");return false;}
                    if(Key==TEXT("window"))F.Windows=Number<0?0x1f:uint8(1u<<int32(Number));
                    else F.ChatType=int32(Number);
                }
            }
            else {Error=TEXT("Unknown chat filter field: ")+Key;return false;}
        }
        if(F.Id.IsEmpty()||Ids.Contains(F.Id)||(F.Contains.IsEmpty()&&F.Prefix.IsEmpty()&&F.Exact.IsEmpty()&&F.Pattern.IsEmpty()&&F.SenderType.IsEmpty()&&!F.HasSender&&F.ChatType==INDEX_NONE))
        {Error=TEXT("Chat filters need unique IDs and at least one text, sender or chat_type predicate");return false;}
        if(F.Color.IsSet()&&!O->HasField(TEXT("action")))F.Action=TEXT("color");
        Ids.Add(F.Id);Filters.Add(MoveTemp(F));
    }
    return true;
}

bool UACEPluginSubsystem::MatchesPluginChat(const FACEPluginChatFilter& F,const FString& Text,const FString& Sender,int32 Type,const FString& SenderType)
{
    if((F.Expires>0&&FPlatformTime::Seconds()>=F.Expires)||(F.ChatType!=INDEX_NONE&&F.ChatType!=Type)
        ||(!F.SenderType.IsEmpty()&&F.SenderType!=SenderType))return false;
    const auto Case=F.CaseSensitive?ESearchCase::CaseSensitive:ESearchCase::IgnoreCase;
    if((F.HasSender&&!Sender.Equals(F.Sender,Case))||(!F.Contains.IsEmpty()&&!Text.Contains(F.Contains,Case))
        ||(!F.Prefix.IsEmpty()&&!Text.StartsWith(F.Prefix,Case))||(!F.Exact.IsEmpty()&&!Text.Equals(F.Exact,Case)))return false;
    FString Error;return F.Pattern.IsEmpty()||ACEVTRegex::Test((F.CaseSensitive?FString():FString(TEXT("(?i)")))+F.Pattern,Text.Left(4096),Error);
}

FString UACEPluginSubsystem::PluginChatSenderType(const FString& Sender) const
{
    if(Sender.IsEmpty())return TEXT("server");
    const auto* C=GetGameInstance()->GetSubsystem<UACEClientSubsystem>();auto Session=C?C->GetSession():nullptr;
    if(Session)
    {
        if(Sender.Equals(TEXT("You"),ESearchCase::IgnoreCase)||Sender.Equals(Session->GetPlayerName(),ESearchCase::IgnoreCase))return TEXT("player");
        for(const auto& Pair:Session->GetWorldObjects())if(Pair.Value.Name.Equals(Sender,ESearchCase::IgnoreCase))
        {if(Pair.Value.bIsPlayer)return TEXT("player");if(Pair.Value.IsVendor())return TEXT("vendor");if(Pair.Value.IsGiveOrCreatureTarget())return TEXT("npc");}
        for(const auto& Fellow:C->GetFellowship().Members)if(Fellow.Name.Equals(Sender,ESearchCase::IgnoreCase))return TEXT("player");
    }
    return TEXT("unknown");
}

FACEPluginChatPresentation UACEPluginSubsystem::PresentPluginChat(const FString& Text,const FString& Sender,int32 Type) const
{
    FACEPluginChatPresentation Result;
    const auto* C=GetGameInstance()->GetSubsystem<UACEClientSubsystem>();
    if(C)
    {
        Result.Forced=C->CurrentChatWindows;
        if(C->CurrentChatColor.IsSet())for(int32 W=0;W<5;++W)Result.Colors.Add(W,C->CurrentChatColor.GetValue());
    }
    if(!C||C->GetSessionState()!=EACESessionState::InWorld)return Result;
    const FString SenderType=PluginChatSenderType(Sender);
    for(const auto& P:Plugins)
    {
        if(!P->Enabled||!P->Running||!P->HasPresentation()||!P->Permissions.Contains(TEXT("chat_filter"))
            ||P->Player!=C->GetPlayerGuid()||P->Server!=C->GetServerName()||P->VMSession.Pin()!=C->GetSession())continue;
        for(const auto& F:P->ChatFilters)
        {
            if(!MatchesPluginChat(F,Text,Sender,Type,SenderType))continue;
            if(F.Action==TEXT("hide"))Result.Hidden|=F.Windows;
            else if(F.Action==TEXT("show"))Result.Forced|=F.Windows;
            if(F.Color.IsSet())for(int32 W=0;W<5;++W)if(F.Windows&(1u<<W))Result.Colors.Add(W,F.Color.GetValue());
        }
    }
    // Hiding wins conflicts between plugins; rules never expose a squelched line.
    Result.Forced&=~Result.Hidden;return Result;
}

uint8 UACEPluginSubsystem::FilterPluginChatWindows(const FString& Text,const FString& Sender,int32 Type) const
{return PresentPluginChat(Text,Sender,Type).Hidden;}
