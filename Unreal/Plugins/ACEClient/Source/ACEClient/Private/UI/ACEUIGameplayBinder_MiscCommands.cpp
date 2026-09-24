#include "UI/ACEUIGameplayBinder.h"
#include "UI/ACEUICanvasWidget.h"
#include "UI/ACEUIResourceResolver.h"
#include "ACEClientSubsystem.h"
#include "ACEDatSubsystem.h"
#include "ACEPlayerController.h"
#include "ACESession.h"
#include "ACEClientBuild.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

bool UACEUIGameplayBinder::TryDispatchMiscCommand(const FString& Cmd, const FString& Args)
{
    if (!Client) return false;
    if (TryDispatchUILayoutCommand(Cmd, Args)) return true;
    auto Error=[this](const FString& Text) { AppendLocalChatLine(Text,ACEChatMessageType::ChatError); };
    auto Print=[this](const FString& Text) { AppendLocalChatLine(Text,ACEChatMessageType::System); };
    if (Cmd==TEXT("fillcomps")) { FillVendorComponents(Args); return true; }
    if (Cmd==TEXT("age") || Cmd==TEXT("birth") || Cmd==TEXT("pkl") || Cmd==TEXT("pklite"))
    {
        if (!Args.IsEmpty()) Error(FString::Printf(TEXT("Usage: /%s"),*Cmd));
        else if (auto S=Client->GetSession(); S.IsValid())
        {
            if (Cmd==TEXT("age") || Cmd==TEXT("birth"))
                S->SendGameActionString(Cmd==TEXT("age") ? ACEGameAction::QueryAge : ACEGameAction::QueryBirth,FString());
            else S->SendSimpleGameAction(ACEGameAction::EnterPkLite);
        }
        return true;
    }
    if (Cmd==TEXT("day"))
    {
        Client->SendSetSingleCharacterOption(0x05,!Client->IsCharacterOptionSet(0x05));
        Print(Client->IsCharacterOptionSet(0x05) ? TEXT("Always daylight outdoors enabled.") : TEXT("Always daylight outdoors disabled."));
        return true;
    }
    if (Cmd==TEXT("framerate"))
    {
        if (PlayerController) PlayerController->ConsoleCommand(TEXT("stat FPS"),true);
        return true;
    }
    if (Cmd==TEXT("version"))
    {
        Print(FString::Printf(TEXT("%s %s (%s, Unreal Engine %s)"),
            ACEClientBuild::ProductName(PlayerController && PlayerController->IsVRActive()), ACEClientBuild::Version,
            PLATFORM_ANDROID ? TEXT("Quest") : TEXT("Windows"), *FEngineVersion::Current().ToString()));
        return true;
    }
    if (Cmd==TEXT("loc"))
    {
        const FACEPosition P=Client->GetPlayerPosition();
        Print(FString::Printf(TEXT("Your location is: 0x%08X [%.6f %.6f %.6f] %.6f %.6f %.6f %.6f"),
            uint32(P.CellId),P.Location.X,P.Location.Y,P.Location.Z,P.RotationW,P.RotationXYZ.X,P.RotationXYZ.Y,P.RotationXYZ.Z));
        return true;
    }
    if (Cmd==TEXT("endurance"))
    {
        Print(TEXT("Strength and Endurance improve health regeneration (up to about 110% in addition to regeneration spells). Endurance reduces attacking stamina cost by up to 50%, with a minimum cost of one.\n"
            "With the corresponding Melee or Missile Defense skill trained or specialized, Endurance gives up to a 75% chance to avoid the stamina cost of a successful evasion.\n"
            "Strength and Endurance also provide up to 50% resistance to drain/harm and natural resistance to the seven damage types. Natural resistance and Life protections do not stack; the stronger protection applies.\n"
            "Character Information shows your resistance and regeneration categories: Poor, Mediocre, Hardy, Resilient, and Indomitable."));
        return true;
    }
    if (Cmd==TEXT("loadfile"))
    {
        if (Args.IsEmpty()) { Error(TEXT("You must provide a file name.")); return true; }
        FString Path=Args; Path.TrimQuotesInline();
        if (FPaths::IsRelative(Path)) Path=FPaths::Combine(FPaths::ProjectDir(),Path);
        Path=FPaths::ConvertRelativePathToFull(Path); FPaths::CollapseRelativeDirectories(Path);
        if (ActiveChatFiles.Contains(Path)) { Error(TEXT("A command file cannot load itself recursively.")); return true; }
        TArray<FString> Lines;
        if (!FFileHelper::LoadFileToStringArray(Lines,*Path)) { Error(FString::Printf(TEXT("Cannot open file %s"),*Path)); return true; }
        if (ActiveChatFiles.Num()>=16) { Error(TEXT("Command file nesting limit reached.")); return true; }
        ActiveChatFiles.Add(Path);
        const int32 Source=ChatSourceWindow;
        for (FString Line:Lines)
        {
            Line.ReplaceInline(TEXT("%DATE%"),*FDateTime::Now().ToFormattedString(TEXT("%Y-%m-%d-%a")));
            if (Line.TrimStartAndEnd().IsEmpty()) continue;
            Print(Line);
            TrySendChatFromEntry(&Line,Source);
        }
        ActiveChatFiles.Remove(Path);
        return true;
    }
    return false;
}

void UACEUIGameplayBinder::FillVendorComponents(const FString& Args)
{
    auto Error=[this](const FString& Text) { AppendLocalChatLine(Text,ACEChatMessageType::ChatError); };
    auto Print=[this](const FString& Text) { AppendLocalChatLine(Text,ACEChatMessageType::System); };
    TArray<FString> Words; Args.ToLower().ParseIntoArrayWS(Words);
    if (Words.Num()==1 && Words[0]==TEXT("clear"))
    {
        // Retail's bulk clear marker is rejected by older ACE servers. Individual
        // removals use the same protocol and persist correctly on those servers too.
        const auto Desired=Client->GetDesiredComponents();
        for (const auto& Pair:Desired) Client->SendSetDesiredComponentLevel(Pair.Key,0);
        RefreshComponentOverlays(); Print(TEXT("Component refill quantities cleared.")); return;
    }
    int32 Category=0;
    int64 Budget=0;
    const TCHAR* Categories[]={TEXT(""),TEXT("scarabs"),TEXT("herbs"),TEXT("powders"),TEXT("potions"),TEXT("talismans"),TEXT("tapers"),TEXT("peas")};
    bool bValid=Words.Num()<=2;
    for (const auto& Word:Words)
    {
        if (Word.IsNumeric() && !Budget)
        {
            int64 Value=0;
            if (!LexTryParseString(Value,*Word) || Value<=0 || Value>MAX_int32) bValid=false;
            else Budget=Value;
        }
        else
        {
            int32 Match=0;
            for (int32 I=1;I<UE_ARRAY_COUNT(Categories);++I)
                if (Word==Categories[I] || Word==FString(Categories[I]).LeftChop(1)) Match=I;
            if (!Match || Category) bValid=false;
            Category=Match;
        }
    }
    if (!bValid) { Error(TEXT("Usage: /fillcomps [scarabs|herbs|powders|potions|talismans|tapers|peas] [maximum cost], or /fillcomps clear")); return; }
    if (!OpenVendorGuid) { Error(TEXT("You must be using a vendor to fill your components.")); return; }
    auto* Dat=Canvas && Canvas->GetResourceResolver() ? Canvas->GetResourceResolver()->GetDatSubsystem() : nullptr;
    if (!Dat) { Error(TEXT("Spell component data is unavailable.")); return; }
    TMap<int32,int32> Carried;
    auto Tally=[&](const TArray<FACEWorldObject>& Items) { for (const auto& Item:Items) Carried.FindOrAdd(Item.WeenieClassId)+=FMath::Max(1,Item.StackSize); };
    Tally(Client->GetPackItems(Client->GetPlayerGuid()));
    for (const auto& Pack:Client->GetPlayerPacks()) Tally(Client->GetPackItems(Pack.Guid));
    auto Stock=Client->GetVendorMerchandise();
    if (Stock.IsEmpty()) Stock=Client->GetPackItems(OpenVendorGuid);
    const auto Desired=Client->GetDesiredComponents();
    int64 Cost=0; int32 Added=0;
    TArray<FString> Missing;
    bool bReachedBudget=false;
    for (const auto& Component:Dat->GetSpellComponents())
    {
        if (Category && uint32(Category)!=Component.Type) continue;
        const int32 Want=Desired.FindRef(Component.Wcid);
        int32 Need=FMath::Max(0,Want-Carried.FindRef(Component.Wcid));
        for (const auto& Item:Stock) if (Item.WeenieClassId==int32(Component.Wcid))
            for (const auto& Pair:VendorBuyCart) if (Pair.Value==Item.Guid) Need-=Pair.Key;
        if (Need<=0) continue;
        for (const auto& Item:Stock)
        {
            if (Item.WeenieClassId!=int32(Component.Wcid)) continue;
            auto* Existing=VendorBuyCart.FindByPredicate([&](const auto& Pair) { return Pair.Value==Item.Guid; });
            const int32 Already=Existing ? Existing->Key : 0;
            const int32 Qty=Item.VendorQuantityAvailable<0 ? Need : FMath::Min(Need,FMath::Max(0,Item.VendorQuantityAvailable-Already));
            if (!Qty) continue;
            const int64 Price=FMath::Max<int64>(1,FMath::CeilToInt64(double(Item.Value)*Client->GetVendorSellRate()*Qty/FMath::Max(1,Item.StackSize)-.1));
            // A refill must fit the limit as a whole; preserve the existing cart.
            if (Budget && Cost+Price>Budget) { bReachedBudget=true; break; }
            if (Existing) Existing->Key+=Qty; else VendorBuyCart.Emplace(Qty,Item.Guid);
            Cost+=Price; Added+=Qty; Need-=Qty;
            if (!Need) break;
        }
        if (bReachedBudget) break;
        if (Need>0) Missing.Add(Component.Name);
    }
    ActiveVendorPage=1;
    SyncVendorPageVisibility(); RefreshVendorOverlays(); RefreshVendorTabLabels(); RefreshVendorButtonLabels(); RefreshVendorInfoTexts();
    if (bReachedBudget) Print(TEXT("Buying aborted; max price reached."));
    if (!Missing.IsEmpty()) Print(TEXT("There was not enough: ")+FString::Join(Missing,TEXT(", ")));
    if (Added) Print(FString::Printf(TEXT("Added %d components to the buy list. Review the list and choose Buy All."),Added));
    else if (Missing.IsEmpty() && !bReachedBudget) Print(TEXT("Your carried components and buy list already meet the refill quantities."));
}
