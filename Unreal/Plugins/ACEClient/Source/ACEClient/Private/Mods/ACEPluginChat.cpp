#include "Mods/ACEPluginSubsystem.h"
#include "ACEClientSubsystem.h"
#include "Engine/GameInstance.h"

bool UACEPluginSubsystem::ValidatePluginChatFilters(const TArray<TSharedPtr<FJsonValue>>& Values,
    TArray<FACEPluginChatFilter>& Filters,FString& Error) const
{
    // Literal matching only on the receive path: no Lua calls or regex backtracking
    // while a large command response is being delivered to the UI.
    if(Values.Num()>32){Error=TEXT("At most 32 chat filters per plugin");return false;}
    TSet<FString> Ids;
    for(const auto& Value:Values)
    {
        if(!Value||Value->Type!=EJson::Object){Error=TEXT("Chat filters must be objects");return false;}
        const auto O=Value->AsObject();FACEPluginChatFilter F;
        for(const auto& Field:O->Values)
        {
            const FString Key(Field.Key);const auto& V=Field.Value;
            if(Key==TEXT("id")||Key==TEXT("contains")||Key==TEXT("prefix")||Key==TEXT("exact")||Key==TEXT("sender"))
            {
                FString Text;
                if(!V->TryGetString(Text)||Text.Len()>(Key==TEXT("id")?64:256)){Error=TEXT("Chat filter text exceeds its limit or is not a string");return false;}
                if(Key==TEXT("id"))F.Id=Text;
                else if(Key==TEXT("contains"))F.Contains=Text;
                else if(Key==TEXT("prefix"))F.Prefix=Text;
                else if(Key==TEXT("exact"))F.Exact=Text;
                else {F.Sender=Text;F.HasSender=true;}
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
        if(F.Id.IsEmpty()||Ids.Contains(F.Id)||(F.Contains.IsEmpty()&&F.Prefix.IsEmpty()&&F.Exact.IsEmpty()))
        {Error=TEXT("Chat filters need unique IDs and at least one nonempty contains, prefix or exact match");return false;}
        Ids.Add(F.Id);Filters.Add(MoveTemp(F));
    }
    return true;
}

uint8 UACEPluginSubsystem::FilterPluginChatWindows(const FString& Text,const FString& Sender,int32 Type) const
{
    const auto* C=GetGameInstance()->GetSubsystem<UACEClientSubsystem>();
    if(!C||C->GetSessionState()!=EACESessionState::InWorld)return 0;
    const double Now=FPlatformTime::Seconds();uint8 Hidden=0;
    for(const auto& P:Plugins)
    {
        if(!P->Enabled||!P->Running||!P->HasPresentation()||!P->Permissions.Contains(TEXT("chat_filter"))
            ||P->Player!=C->GetPlayerGuid()||P->Server!=C->GetServerName()||P->VMSession.Pin()!=C->GetSession())continue;
        for(const auto& F:P->ChatFilters)
        {
            if((F.Expires>0&&Now>=F.Expires)||(F.ChatType!=INDEX_NONE&&F.ChatType!=Type)||(Hidden&F.Windows)==F.Windows)continue;
            const auto Case=F.CaseSensitive?ESearchCase::CaseSensitive:ESearchCase::IgnoreCase;
            if((F.HasSender&&!Sender.Equals(F.Sender,Case))||(!F.Contains.IsEmpty()&&!Text.Contains(F.Contains,Case))
                ||(!F.Prefix.IsEmpty()&&!Text.StartsWith(F.Prefix,Case))||(!F.Exact.IsEmpty()&&!Text.Equals(F.Exact,Case)))continue;
            Hidden|=F.Windows;
            if(Hidden==0x1f)return Hidden;
        }
    }
    return Hidden;
}
