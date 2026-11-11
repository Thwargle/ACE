#include "UI/ACEUIGameplayBinder.h"
#include "ACEClientSubsystem.h"
#include "ACESession.h"
#include "ACERetailCommandSyntax.h"
#include "ACERuntimeOptions.h"
#include "String/LexFromString.h"

bool UACEUIGameplayBinder::TryDispatchRetailUtilityCommand(const FString& Cmd,const FString& Args)
{
    if (!Client) return false;
    const auto S=Client->GetSession();
    auto Error=[this](const FString& Text){AppendLocalChatLine(Text,ACEChatMessageType::ChatError);};
    auto Print=[this](const FString& Text){AppendLocalChatLine(Text,ACEChatMessageType::System);};
    if (Cmd==TEXT("consent") || Cmd==TEXT("permit"))
    {
        FString Rest=Args;const FString Verb=ACERetailCommandSyntax::Take(Rest);
        const FString Name=ACERetailCommandSyntax::Name(Rest);
        if (!S) return true;
        if (Cmd==TEXT("consent"))
        {
            if (Verb==TEXT("on") || Verb==TEXT("off"))
            {
                const bool Enabled=Verb==TEXT("on");
                if (Client->IsCharacterOptionSet(0x10)!=Enabled) Client->SendSetSingleCharacterOption(0x10,Enabled);
                Print(Enabled?TEXT("You can now accept corpse looting permissions from other players.")
                    :TEXT("You are no longer accepting corpse looting permissions from other players."));
            }
            else if (Verb==TEXT("who")) S->SendSimpleGameAction(ACEGameAction::DisplayPlayerConsentList);
            else if (Verb==TEXT("clear")) S->SendSimpleGameAction(ACEGameAction::ClearPlayerConsentList);
            else if (Verb==TEXT("remove") && !Name.IsEmpty()) S->SendGameActionString(ACEGameAction::RemoveFromPlayerConsentList,Name);
            else Error(TEXT("Usage: @consent on|off|who|clear|remove <name>"));
        }
        else if ((Verb==TEXT("add") || Verb==TEXT("remove")) && !Name.IsEmpty())
            S->SendGameActionString(Verb==TEXT("add")?ACEGameAction::AddPlayerPermission:ACEGameAction::RemovePlayerPermission,Name);
        else Error(TEXT("Usage: @permit add|remove <name>"));
        return true;
    }
    if (Cmd==TEXT("corpse") || Cmd==TEXT("cor"))
    {
        const auto* P=S?S->GetPlayerPositionQuality(14):nullptr;
        if (P && (uint32(P->CellId)&0xffff)>=1 && (uint32(P->CellId)&0xffff)<=64)
            Print(FString::Printf(TEXT("The last time you died outside, your corpse was located at (%s)."),
                *ACERetailCommandSyntax::CorpseCoordinates(P->CellId)));
        else Print(TEXT("We're sorry, but we have no record of your last outside corpse location."));
        return true;
    }
    if (Cmd==TEXT("render"))
    {
        FString Rest=Args;const FString Option=ACERetailCommandSyntax::Take(Rest);
        const FString Number=ACERetailCommandSyntax::Take(Rest);
        int32 Value=0;
        bool DigitsOnly=!Number.IsEmpty();
        for (const TCHAR C:Number) DigitsOnly &= C>=TEXT('0') && C<=TEXT('9');
        if (Option!=TEXT("fov") && Option!=TEXT("radius"))
            Error(TEXT("Usage: @render fov <10-160> | @render radius <5-25>"));
        else if (!DigitsOnly || Number.Len()>3 || !LexTryParseString(Value,*Number) || !Rest.IsEmpty()
            || Value<(Option==TEXT("fov")?10:5) || Value>(Option==TEXT("fov")?160:25))
            Error(Option==TEXT("fov")?TEXT("Field of view must be between 10 and 160."):TEXT("Radius must be between 5 and 25."));
        else
        {
            ACERuntimeOptions::Set(Option==TEXT("fov")?TEXT("FieldOfViewDegrees"):TEXT("LandscapeRadius"),Value);
            ACERuntimeOptions::Apply();
            Print(Option==TEXT("fov")?TEXT("Field of view set."):
                PLATFORM_ANDROID?TEXT("Landscape radius saved; Android applies its terrain memory limit."):TEXT("Landscape radius set."));
        }
        return true;
    }
    return false;
}
