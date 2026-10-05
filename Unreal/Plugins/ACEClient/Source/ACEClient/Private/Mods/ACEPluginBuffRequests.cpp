#include "Mods/ACEPluginSubsystem.h"
#include "ACEClientSubsystem.h"
#include "Engine/GameInstance.h"

TArray<TSharedPtr<FJsonValue>> UACEPluginSubsystem::BuffCommands() const
{
    const TArray<TSharedPtr<FJsonValue>>* Commands=nullptr;
    if(auto P=Find(TEXT("ucm"));P&&P->Profile->TryGetArrayField(TEXT("buff_commands"),Commands))return *Commands;
    TArray<TSharedPtr<FJsonValue>> Defaults;
    for(const TCHAR* Role:{TEXT("mage"),TEXT("heavy"),TEXT("missile"),TEXT("light"),TEXT("finesse")})
    {
        auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("keyword"),Role);O->SetStringField(TEXT("role"),Role);
        Defaults.Add(MakeShared<FJsonValueObject>(O));
    }
    return Defaults;
}
void UACEPluginSubsystem::ClearBuffRequests()
{BuffRequests.Empty();LastBuffRequest.Empty();BuffRequestStatus.Empty();}

void UACEPluginSubsystem::ObservePlayerTell(const FString& Text,const FString& Sender,int32 SenderId)
{
    auto P=Find(TEXT("ucm"));auto* C=GetGameInstance()->GetSubsystem<UACEClientSubsystem>();
    bool Enabled=false,Buffing=true;
    if(!P||!P->Enabled||!P->Running||C->GetSessionState()!=EACESessionState::InWorld)return;
    P->Profile->TryGetBoolField(TEXT("buff_others"),Enabled);P->Profile->TryGetBoolField(TEXT("buffing"),Buffing);
    if(!Enabled||!Buffing||SenderId==C->GetPlayerGuid()||uint32(SenderId)<0x50000001u||uint32(SenderId)>0x6fffffffu)return;
    const FString Keyword=Text.TrimStartAndEnd().ToLower();FString Role;
    for(const auto& V:BuffCommands())
    {
        const auto O=V->AsObject();FString K,R;
        if(O&&O->TryGetStringField(TEXT("keyword"),K)&&O->TryGetStringField(TEXT("role"),R)&&K.TrimStartAndEnd().ToLower()==Keyword)
        {Role=R;break;}
    }
    if(!TArray<FString>{TEXT("mage"),TEXT("heavy"),TEXT("missile"),TEXT("light"),TEXT("finesse")}.Contains(Role))return;
    const double Now=FPlatformTime::Seconds();
    for(auto It=LastBuffRequest.CreateIterator();It;++It)if(Now-It.Value()>300)It.RemoveCurrent();
    if(BuffRequests.Num()>=8||LastBuffRequest.Num()>=256)return;
    if(const auto* Last=LastBuffRequest.Find(SenderId);Last&&Now-*Last<120)return;
    for(const auto& V:BuffRequests)if(uint32(V->AsObject()->GetNumberField(TEXT("player")))==uint32(SenderId))return;
    auto O=MakeShared<FJsonObject>();O->SetNumberField(TEXT("id"),++BuffRequestSerial);
    O->SetNumberField(TEXT("player"),uint32(SenderId));O->SetStringField(TEXT("name"),Sender.Left(128));O->SetStringField(TEXT("role"),Role);
    O->SetNumberField(TEXT("expires"),Now+900);BuffRequests.Add(MakeShared<FJsonValueObject>(O));LastBuffRequest.Add(SenderId,Now);
    BuffRequestStatus=FString::Printf(TEXT("Queued %s buffs for %s (%d waiting)"),*Role,*Sender,BuffRequests.Num());
}
