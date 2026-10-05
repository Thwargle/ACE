#include "ACEAppraisalFormatting.h"
#include "UI/ACEUIResourceResolver.h"
namespace ACEAppraisalFormatting::Titles
{
#include "Protocol/ACECharacterTitleNames.inl"
}

ACEAppraisalFormatting::FCreatureHeadings ACEAppraisalFormatting::CreatureHeadings(
    const FACEAppraisalInfo& Info, UACEUIResourceResolver* Resources, const FACEWorldObject* Subject)
{
    FCreatureHeadings Result;
    if (Info.CreatureType && Resources) Result.Type = Resources->ResolveEnumString(0x10000005, Info.CreatureType);
    Result.Type.ReplaceInline(TEXT("_"), TEXT(" "));
    if (!UsesCharacterExamination(Info)) return Result;
    FString Gender = Resources ? Resources->ResolveEnumString(0x10000001, Info.IntProperties.FindRef(113)) : FString();
    FString Heritage = Resources ? Resources->ResolveEnumString(0x10000002, Info.IntProperties.FindRef(188)) : FString();
    if (!Info.IntProperties.FindRef(113)) Gender.Reset();
    if (!Info.IntProperties.FindRef(188)) Heritage = Result.Type;
    Gender.ReplaceInline(TEXT("_"), TEXT(" ")); Heritage.ReplaceInline(TEXT("_"), TEXT(" "));
    Result.Heritage = (Gender + TEXT(" ") + Heritage).TrimStartAndEnd();
    Result.Profession = Info.StringProperties.FindRef(5);
    if (const int32* Title = Info.IntProperties.Find(261))
    {
        const FString Name = Titles::GetCharacterTitleName(*Title);
        if (Name != TEXT("(Unknown Title)")) Result.Profession = Name;
    }
    const int32 PK = Info.IntProperties.FindRef(134);
    const bool bPK = Subject ? (Subject->ObjectDescriptionFlags & ACEObjectDescFlag::PlayerKiller) != 0 : (PK & ACEPlayerKillerStatus::PK) != 0;
    const bool bPKLite = Subject ? (Subject->ObjectDescriptionFlags & ACEObjectDescFlag::PkLiteStatus) != 0 : (PK & ACEPlayerKillerStatus::PKLite) != 0;
    Result.PlayerKiller = bPK ? TEXT("Player Killer") : bPKLite ? TEXT("Player Killer Lite") : TEXT("Non-Player Killer");
    if (Info.IntProperties.FindRef(30) > 0) Result.Allegiance = Info.StringProperties.FindRef(47);
    return Result;
}

FString ACEAppraisalFormatting::EquipmentSetName(int32 Id)
{
    // ItemExamineUI::Appraisal_ShowSet names for replicated EquipmentSetId.
    switch(Id)
    {
    case 4: return TEXT("Carraida's Benediction");
    case 5: return TEXT("Noble Relic");
    case 6: return TEXT("Ancient Relic");
    case 7: return TEXT("Alduressa Relic");
    case 8: return TEXT("Shou-jen");
    case 9: return TEXT("Empyrean Rings");
    case 10: return TEXT("Arm, Mind, Heart");
    case 11: return TEXT("Coat of Perfect Light");
    case 12: return TEXT("Leggings of Perfect Light");
    case 13: return TEXT("Soldier's");
    case 14: return TEXT("Adept's");
    case 15: return TEXT("Archer's");
    case 16: return TEXT("Defender's");
    case 17: return TEXT("Tinker's");
    case 18: return TEXT("Crafter's");
    case 19: return TEXT("Hearty");
    case 20: return TEXT("Dexterous");
    case 21: return TEXT("Wise");
    case 22: return TEXT("Swift");
    case 23: return TEXT("Hardened");
    case 24: return TEXT("Reinforced");
    case 25: return TEXT("Interlocking");
    case 26: return TEXT("Flame Proof");
    case 27: return TEXT("Acid Proof");
    case 28: return TEXT("Cold Proof");
    case 29: return TEXT("Lightning Proof");
    case 30: return TEXT("Dedication");
    case 31: return TEXT("Gladiatorial Clothing");
    case 32: return TEXT("Ceremonial Clothing");
    case 33: return TEXT("Protective Clothing");
    case 35: return TEXT("Sigil of Defense");
    case 36: return TEXT("Sigil of Destruction");
    case 37: return TEXT("Sigil of Fury");
    case 38: return TEXT("Sigil of Growth");
    case 39: return TEXT("Sigil of Vigor");
    case 40: return TEXT("Heroic Protector");
    case 41: return TEXT("Heroic Destroyer");
    case 49: return TEXT("Weave of Alchemy");
    case 50: return TEXT("Weave of Arcane Lore");
    case 51: return TEXT("Weave of Armor Tinkering");
    case 52: return TEXT("Weave of Assess Person");
    case 79: return TEXT("Weave of Light Weapons");
    case 77: return TEXT("Weave of Missile Weapons");
    case 55: return TEXT("Weave of Cooking");
    case 56: return TEXT("Weave of Creature Enchantment");
    case 58: return TEXT("Weave of Finesse Weapons");
    case 59: return TEXT("Weave of Deception");
    case 60: return TEXT("Weave of Fletching");
    case 61: return TEXT("Weave of Healing");
    case 62: return TEXT("Weave of Item Enchantment");
    case 63: return TEXT("Weave of Item Tinkering");
    case 64: return TEXT("Weave of Leadership");
    case 65: return TEXT("Weave of Life Magic");
    case 66: return TEXT("Weave of Loyalty");
    case 68: return TEXT("Weave of Magic Defense");
    case 69: return TEXT("Weave of Magic Item Tinkering");
    case 70: return TEXT("Weave of Mana Conversion");
    case 71: return TEXT("Weave of Melee Defense");
    case 72: return TEXT("Weave of Missile Defense");
    case 73: return TEXT("Weave of Salvaging");
    case 76: return TEXT("Weave of Heavy Weapons");
    case 78: return TEXT("Weave of Two Handed Combat");
    case 80: return TEXT("Weave of Void Magic");
    case 81: return TEXT("Weave of War Magic");
    case 82: return TEXT("Weave of Weapon Tinkering");
    case 83: return TEXT("Weave of Assess Creature");
    case 84: return TEXT("Weave of Dirty Fighting");
    case 85: return TEXT("Weave of Dual Wield");
    case 86: return TEXT("Weave of Recklessness");
    case 87: return TEXT("Weave of Shield");
    case 88: return TEXT("Weave of Sneak Attack");
    case 89: return TEXT("Shou-jen Shozoku");
    case 90: return TEXT("Weave of Summoning");
    case 91: return TEXT("Shrouded Soul");
    case 92: return TEXT("Darkened Mind");
    case 93: return TEXT("Clouded Spirit");
    case 94: return TEXT("Minor Stinging Shrouded Soul");
    case 95: return TEXT("Minor Sparking Shrouded Soul");
    case 96: return TEXT("Minor Smoldering Shrouded Soul");
    case 97: return TEXT("Minor Shivering Shrouded Soul");
    case 98: return TEXT("Minor Stinging Darkened Mind");
    case 99: return TEXT("Minor Sparking Darkened Mind");
    case 100: return TEXT("Minor Smoldering Darkened Mind");
    case 101: return TEXT("Minor Shivering Darkened Mind");
    case 102: return TEXT("Minor Stinging Clouded Spirit");
    case 103: return TEXT("Minor Sparking Clouded Spirit");
    case 104: return TEXT("Minor Smoldering Clouded Spirit");
    case 105: return TEXT("Minor Shivering Clouded Spirit");
    case 106: return TEXT("Major Stinging Shrouded Soul");
    case 107: return TEXT("Major Sparking Shrouded Soul");
    case 108: return TEXT("Major Smoldering Shrouded Soul");
    case 109: return TEXT("Major Shivering Shrouded Soul");
    case 110: return TEXT("Major Stinging Darkened Mind");
    case 111: return TEXT("Major Sparking Darkened Mind");
    case 112: return TEXT("Major Smoldering Darkened Mind");
    case 113: return TEXT("Major Shivering Darkened Mind");
    case 114: return TEXT("Major Stinging Clouded Spirit");
    case 115: return TEXT("Major Sparking Clouded Spirit");
    case 116: return TEXT("Major Smoldering Clouded Spirit");
    case 117: return TEXT("Major Shivering Clouded Spirit");
    case 118: return TEXT("Blackfire Stinging Shrouded Soul");
    case 119: return TEXT("Blackfire Sparking Shrouded Soul");
    case 120: return TEXT("Blackfire Smoldering Shrouded Soul");
    case 121: return TEXT("Blackfire Shivering Shrouded Soul");
    case 122: return TEXT("Blackfire Stinging Darkened Mind");
    case 123: return TEXT("Blackfire Sparking Darkened Mind");
    case 124: return TEXT("Blackfire Smoldering Darkened Mind");
    case 125: return TEXT("Blackfire Shivering Darkened Mind");
    case 126: return TEXT("Blackfire Stinging Clouded Spirit");
    case 127: return TEXT("Blackfire Sparking Clouded Spirit");
    case 128: return TEXT("Blackfire Smoldering Clouded Spirit");
    case 129: return TEXT("Blackfire Shivering Clouded Spirit");
    case 130: return TEXT("Shimmering Shadows");
    default: return FString();
    }
}
