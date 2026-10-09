#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "ACEDatSubsystem.h"
#include "ACEClientSubsystem.h"
#include "ACESession.h"
#include "Dat/ACEDatTextureResolver.h"
#include "UI/ACEUIResourceResolver.h"
#include "UI/ACEUIGameplayBinder.h"
#include "UI/ACERetailObjectNames.h"
#include "UI/ACEAppraisalFormatting.h"
#include "UI/ACERadarColors.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEItemPresentationTest, "ACE.RetailParity.ItemPresentation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACEItemPresentationTest::RunTest(const FString&)
{
    auto* GI = NewObject<UGameInstance>();
    auto* Dat = NewObject<UACEDatSubsystem>(GI);
    if (!TestTrue(TEXT("Retail DAT opens"), Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call")))) return false;
    ON_SCOPE_EXIT { Dat->Deinitialize(); };
    auto* Resources = NewObject<UACEUIResourceResolver>(); Resources->Initialize(Dat);
    auto* Client = NewObject<UACEClientSubsystem>(GI); Client->Session = MakeShared<FACESession>();
    auto& Session = *Client->Session; Session.PlayerGuid = 1234;
    FACEWorldObject Item; Item.Guid = 1235; Item.ContainerId = 1234; Item.Name = TEXT("Ring");
    Item.MaterialType = 59; Item.IconId = 0x060011CB;
    TestEqual(TEXT("Equipment includes the material prefix"), ACERetailObjectNames::Name(Item), FString(TEXT("Copper Ring")));
    Item.Name = TEXT("Copper Ring");
    TestEqual(TEXT("An existing material prefix is not repeated"), ACERetailObjectNames::Name(Item), FString(TEXT("Copper Ring")));
    Item.Name = TEXT("Ring"); Item.PluralName = TEXT("Rings");
    TestEqual(TEXT("Explicit plurals retain the material"), ACERetailObjectNames::Name(Item, true), FString(TEXT("Copper Rings")));
    Item.MaterialType = 77;
    TestEqual(TEXT("Material lookup includes the last retail material"), ACERetailObjectNames::Name(Item), FString(TEXT("Teak Ring")));
    Item.MaterialType = 0;
    TestEqual(TEXT("Objects without a material keep their names"), ACERetailObjectNames::Name(Item), FString(TEXT("Ring")));
    Session.WorldObjects.Add(Item.Guid, Item);
    auto* Binder = NewObject<UACEUIGameplayBinder>(); Binder->Client = Client; Binder->SelectedPackGuid = 1234;
    auto UpdateInt = [&](uint32 Property, int32 Value)
    {
        FACEBinaryWriter Packet; Packet.WriteUInt8(1); Packet.WriteUInt32(Item.Guid);
        Packet.WriteUInt32(Property); Packet.WriteInt32(Value);
        FACEBinaryReader Reader(Packet.GetData()); Session.HandlePublicUpdatePropertyInt(Reader);
    };
    {
        FACEWorldObject Target; Target.Guid = Item.Guid;
        const auto Check = [&](const TCHAR* Name, uint8 Expected, const FACEFellowshipInfo* Fellowship = nullptr)
        {
            const uint8 Color = Binder->ResolveRadarColor(Target, Fellowship);
            TestEqual(Name, Color, Expected);
            TestTrue(TEXT("Selection arrows and radar share retail color"),
                Binder->ColorFromSelectionMarker(Color).Equals(ACERadarColors::Tint(Expected)));
        };
        Target.ObjectDescriptionFlags = ACEObjectDescFlag::Door; Check(TEXT("Door is white"), ACERadarColor::White);
        Target.ObjectDescriptionFlags = ACEObjectDescFlag::Stuck; Check(TEXT("Sign is white"), ACERadarColor::White);
        Target.ObjectDescriptionFlags = ACEObjectDescFlag::Openable; Check(TEXT("Chest is white"), ACERadarColor::White);
        Target.ObjectDescriptionFlags = ACEObjectDescFlag::Corpse; Check(TEXT("Corpse is white"), ACERadarColor::White);
        Target.ObjectDescriptionFlags = 0; Check(TEXT("Loose item is white"), ACERadarColor::White);
        Target.ItemType = ACEItemType::Creature; Check(TEXT("Creature prop without an explicit tint stays white"), ACERadarColor::White);
        Target.RadarBlipColor = ACERadarColor::NPC; Check(TEXT("Authored NPC yellow is preserved"), ACERadarColor::Yellow);
        Target.RadarBlipColor = 0; Target.ObjectDescriptionFlags = ACEObjectDescFlag::Attackable;
        Check(TEXT("Attackable creature is gold"), ACERadarColor::Gold);
        Target.ObjectDescriptionFlags |= ACEObjectDescFlag::Vendor; Check(TEXT("Vendor overrides creature gold"), ACERadarColor::Yellow);
        Target.ObjectDescriptionFlags |= ACEObjectDescFlag::Portal; Check(TEXT("Portal overrides vendor yellow"), ACERadarColor::Purple);
        Target.bIsPlayer = true; Target.ObjectDescriptionFlags = 0; Check(TEXT("Ordinary player is white"), ACERadarColor::White);
        Target.ObjectDescriptionFlags = ACEObjectDescFlag::PkLiteStatus; Check(TEXT("PK Lite is pink"), ACERadarColor::Pink);
        Target.ObjectDescriptionFlags |= ACEObjectDescFlag::PlayerKiller; Check(TEXT("PK takes precedence over PK Lite"), ACERadarColor::Red);
        Target.ObjectDescriptionFlags = ACEObjectDescFlag::FreePkStatus; Check(TEXT("Free PK is gold"), ACERadarColor::Gold);
        Target.ObjectDescriptionFlags = ACEObjectDescFlag::Admin; Check(TEXT("Visible admin is cyan"), ACERadarColor::Cyan);
        Target.ObjectDescriptionFlags |= ACEObjectDescFlag::HiddenAdmin; Check(TEXT("Hidden admin does not expose admin color"), ACERadarColor::White);
        FACEFellowshipInfo Fellowship; Fellowship.bValid = true; Fellowship.LeaderGuid = Target.Guid;
        Target.ObjectDescriptionFlags = ACEObjectDescFlag::PlayerKiller;
        Check(TEXT("Fellowship leader is green"), ACERadarColor::BrightGreen, &Fellowship);
        Fellowship.LeaderGuid = 0; FACEFellowshipMember Member; Member.Guid = Target.Guid; Fellowship.Members.Add(Member);
        Check(TEXT("Fellowship member is green"), ACERadarColor::BrightGreen, &Fellowship);
        Target.RadarBlipColor = ACERadarColor::Blue;
        Check(TEXT("Explicit server color precedes player and fellowship status"), ACERadarColor::Blue, &Fellowship);
        Target.ObjectDescriptionFlags |= ACEObjectDescFlag::UiHidden;
        Check(TEXT("UiHidden suppresses explicit tint"), ACERadarColor::White, &Fellowship);
        TestTrue(TEXT("Default palette color is white"), ACERadarColors::Tint(0).Equals(FLinearColor::White));
        TestTrue(TEXT("Unknown palette color falls back to white"), ACERadarColors::Tint(255).Equals(FLinearColor::White));
        TestTrue(TEXT("Retail and ACE bright-green IDs both render green"),
            ACERadarColors::Tint(10).Equals(FLinearColor::Green) && ACERadarColors::Tint(16).Equals(FLinearColor::Green));
        UpdateInt(95, ACERadarColor::Cyan);
        TestEqual(TEXT("Public radar color update is used immediately"), ACERadarColors::Resolve(Session.WorldObjects[Item.Guid]), ACERadarColor::Cyan);
        UpdateInt(95, 0);
        TestEqual(TEXT("Clearing public color restores white item selection"), ACERadarColors::Resolve(Session.WorldObjects[Item.Guid]), ACERadarColor::White);
    }
    const uint64 InitialHash = Binder->HashInventoryOverlayState();
    const uint64 InitialRevision = Session.GetInventoryDataRevision();
    UpdateInt(131, 59);
    TestEqual(TEXT("Public material update changes the rendered name"), ACERetailObjectNames::Name(Session.WorldObjects[Item.Guid]), FString(TEXT("Copper Ring")));
    TestTrue(TEXT("Material update invalidates the desktop inventory"), InitialHash != Binder->HashInventoryOverlayState());
    TestTrue(TEXT("Material update invalidates the VR inventory"), InitialRevision != Session.GetInventoryDataRevision());
    UpdateInt(91, 100); UpdateInt(92, 50);
    const uint64 HalfHash = Binder->HashInventoryOverlayState();
    UpdateInt(92, 25);
    TestTrue(TEXT("Remaining units invalidate an already rendered inventory icon"), HalfHash != Binder->HashInventoryOverlayState());
    TestEqual(TEXT("Units are replicated independently of stack count"), Session.WorldObjects[Item.Guid].Structure, 25);

    FACEAppraisalInfo Appraisal;
    const auto Handle = Session.OnAppraisal.AddLambda([&](const FACEAppraisalInfo& Info) { Appraisal = Info; });
    ON_SCOPE_EXIT { Session.OnAppraisal.Remove(Handle); };
    FACEBinaryWriter Identify; Identify.WriteUInt32(Item.Guid); Identify.WriteUInt32(0); Identify.WriteUInt32(0);
    FACEBinaryReader IdentifyReader(Identify.GetData()); Session.HandleIdentifyObjectResponse(IdentifyReader);
    TestEqual(TEXT("Failed appraisal preserves the known material in its title"), ACEAppraisalFormatting::ExaminationName(Appraisal), FString(TEXT("Copper Ring")));
    {
        auto& Known=Session.WorldObjects[Item.Guid];
        const auto Saved=Known;
        Known.ValidLocations=ACEEquipMask::MissileWeapon;Known.AmmoType=1;Known.ClothingPriority=0x810;
        FACEBinaryWriter Reply;Reply.WriteUInt32(Item.Guid);Reply.WriteUInt32(0);Reply.WriteUInt32(1);
        FACEBinaryReader Reader(Reply.GetData());Session.HandleIdentifyObjectResponse(Reader);
        TestEqual(TEXT("Appraisal inherits public ammo type when the server omits it"),Appraisal.IntProperties.FindRef(50),1);
        TestEqual(TEXT("Appraisal inherits public equipment locations"),Appraisal.IntProperties.FindRef(9),static_cast<int32>(ACEEquipMask::MissileWeapon));
        TestEqual(TEXT("Appraisal inherits clothing coverage"),Appraisal.IntProperties.FindRef(4),0x810);
        FACEBinaryWriter Override;Override.WriteUInt32(Item.Guid);Override.WriteUInt32(1);Override.WriteUInt32(1);
        Override.WriteUInt16(1);Override.WriteUInt16(0);Override.WriteUInt32(50);Override.WriteInt32(2);
        FACEBinaryReader OverrideReader(Override.GetData());Session.HandleIdentifyObjectResponse(OverrideReader);
        TestEqual(TEXT("Explicit server appraisal quality overrides a public descriptor fallback"),Appraisal.IntProperties.FindRef(50),2);
        Known=Saved;
    }
    {
        // Real appraisal packet: one inherent spell and one active enchantment.
        FACEBinaryWriter Reply; Reply.WriteUInt32(Item.Guid); Reply.WriteUInt32(0x10); Reply.WriteUInt32(1);
        Reply.WriteUInt32(2); Reply.WriteUInt32(1); Reply.WriteUInt32(0x80000002u);
        FACEBinaryReader Reader(Reply.GetData()); Session.HandleIdentifyObjectResponse(Reader);
        TestTrue(TEXT("Appraisal preserves raw enchantment flag without changing loot spell IDs"),
            Appraisal.SpellBookEntries==TArray<int32>({1,int32(0x80000002u)}) && Appraisal.SpellIds==TArray<int32>({1,2}));
        FString SpellName,EnchantName,Description; uint32 Icon=0;
        TestTrue(TEXT("Retail spell names resolve for appraisal fixture"),Dat->TryGetSpellInfo(1,SpellName,Icon) && Dat->TryGetSpellInfo(2,EnchantName,Icon)
            && Dat->TryGetSpellDescription(2,Description));
        const FString Text=ACEAppraisalFormatting::ItemExaminationText(Appraisal,Dat,false,false);
        TestTrue(TEXT("Inherent spells retain their short list"),Text.Contains(TEXT("Spells: ")+SpellName));
        TestFalse(TEXT("Active enchantments are not appended to the short spell list"),Text.Contains(TEXT("Spells: ")+SpellName+TEXT(", ")+EnchantName));
        TestTrue(TEXT("Active enchantments have retail's separate description section"),Text.Contains(TEXT("Enchantments:\n\n~ ")+EnchantName+TEXT(": ")+Description));
        Appraisal.bSuccess=false;
        const auto Failed=ACEAppraisalFormatting::ItemExaminationText(Appraisal,Dat,false,false);
        TestTrue(TEXT("Failed spellbook appraisal is unknown"),Failed.Contains(TEXT("Spells: unknown.")));
        TestFalse(TEXT("Failed spellbook appraisal does not reveal descriptions"),Failed.Contains(TEXT("Spell Descriptions:")) || Failed.Contains(TEXT("Enchantments:")));
    }

    FACEDatTexture FrameRaw, FillRaw; FACEDatDecodedSurface Frame, Fill;
    auto* Decoder = Dat->GetTextureResolver();
    if (!TestTrue(TEXT("Retail structure meter frame and fill decode"),
        Decoder->LoadTextureForUi(0x06004D24, FrameRaw) && Decoder->DecodeTextureForUi(FrameRaw, Frame)
        && Decoder->LoadTextureForUi(0x06004D25, FillRaw) && Decoder->DecodeTextureForUi(FillRaw, Fill))) return false;
    if (!TestTrue(TEXT("Retail meter uses its authored dimensions"), Frame.Width == 5 && Frame.Height == 30 && Fill.Width == 5 && Fill.Height == 30)) return false;
    auto Pixels = [](UTexture2D* Texture)
    {
        TArray<FColor> Result;
        if (Texture && Texture->GetPlatformData() && Texture->GetPlatformData()->Mips.Num())
        {
            auto& Mip = Texture->GetPlatformData()->Mips[0];
            const auto* Data = static_cast<const FColor*>(Mip.BulkData.LockReadOnly());
            if (Data) Result.Append(Data, 1024);
            Mip.BulkData.Unlock();
        }
        return Result;
    };
    UTexture2D* Plain = Resources->ResolveItemForeground(Item.IconId, 0, 0);
    UTexture2D* Half = Resources->ResolveItemForeground(Item.IconId, 0, 0, false, 50, 100);
    UTexture2D* Empty = Resources->ResolveItemForeground(Item.IconId, 0, 0, false, 0, 100);
    UTexture2D* Overfull = Resources->ResolveItemForeground(Item.IconId, 0, 0, false, 101, 100);
    TestTrue(TEXT("Full items hide the bar as in retail"), Plain == Resources->ResolveItemForeground(Item.IconId, 0, 0, false, 100, 100));
    TestTrue(TEXT("Unknown capacity does not invent a full or empty bar"), Plain == Resources->ResolveItemForeground(Item.IconId, 0, 0, false, 50, 0));
    TestTrue(TEXT("Equivalent fill ratios reuse the icon texture"), Half == Resources->ResolveItemForeground(Item.IconId, 0, 0, false, 2, 4));
    TestTrue(TEXT("Offered icons retain their own composited meter"), Half != Resources->ResolveItemForeground(Item.IconId, 0, 0, true, 50, 100));
    const auto BasePixels = Pixels(Plain), HalfPixels = Pixels(Half), EmptyPixels = Pixels(Empty), OverfullPixels = Pixels(Overfull);
    if (!TestTrue(TEXT("Composed icons have inspectable RGBA pixels"), BasePixels.Num() == 1024 && HalfPixels.Num() == 1024 && EmptyPixels.Num() == 1024 && OverfullPixels.Num() == 1024)) return false;
    bool bHalfCorrect = true, bEmptyCorrect = true, bClampedCorrect = true, bOutsideUnchanged = true;
    for (int32 Y = 0; Y < 32; ++Y) for (int32 X = 0; X < 32; ++X)
    {
        const int32 P = Y * 32 + X;
        if (X >= 26 && X < 31 && Y >= 1 && Y < 31)
        {
            const int32 M = (Y - 1) * 5 + X - 26;
            bHalfCorrect &= HalfPixels[P] == (Y >= 16 ? Fill.Pixels[M] : Frame.Pixels[M]);
            bEmptyCorrect &= EmptyPixels[P] == Frame.Pixels[M];
            bClampedCorrect &= OverfullPixels[P] == Fill.Pixels[M];
        }
        else bOutsideUnchanged &= HalfPixels[P] == BasePixels[P];
    }
    TestTrue(TEXT("Half-full salvage clips the original DAT fill from the bottom"), bHalfCorrect);
    TestTrue(TEXT("Empty salvage shows the original DAT empty meter"), bEmptyCorrect);
    TestTrue(TEXT("Retail clamps overfull ratios while keeping the bar visible"), bClampedCorrect);
    TestTrue(TEXT("Structure does not recolor or replace the item artwork"), bOutsideUnchanged);
    return !HasAnyErrors();
}
#endif
