#include "ACECharacterOptions.h"

namespace
{
	// Option index / flag pairs come from ACE.Entity CharacterOption + CharacterOptions1/2.
	// Page assignment follows the retail Options panel tabs.
	const FACECharacterOptionDesc GTable[] = {
		// Character
		{ TEXT("Auto-repeat attacks"),                 0x00, false, 0x00000002u, ACECharacterOptions::PageCharacter },
		{ TEXT("Auto-target"),                         0x0D, false, 0x00002000u, ACECharacterOptions::PageCharacter },
		{ TEXT("Keep combat targets in view"),          0x07, false, 0x00000080u, ACECharacterOptions::PageCharacter },
		{ TEXT("Vivid targeting indicator"),            0x0E, false, 0x00008000u, ACECharacterOptions::PageCharacter },
		{ TEXT("Use charge attack"),                    0x19, false, 0x10000000u, ACECharacterOptions::PageCharacter },
		{ TEXT("Lead missile targets"),                 0x2A, true,  0x00008000u, ACECharacterOptions::PageCharacter },
		{ TEXT("Use fast missiles"),                    0x2B, true,  0x00010000u, ACECharacterOptions::PageCharacter },
		{ TEXT("Run as default movement"),              0x0A, false, 0x00000400u, ACECharacterOptions::PageCharacter },
		{ TEXT("Let other players give you items"),     0x06, false, 0x00000040u, ACECharacterOptions::PageCharacter },
		{ TEXT("Drag item to player opens trade"),      0x17, false, 0x04000000u, ACECharacterOptions::PageCharacter },
		{ TEXT("Ignore all trade requests"),            0x03, false, 0x00020000u, ACECharacterOptions::PageCharacter },
		{ TEXT("Ignore allegiance requests"),           0x01, false, 0x00000004u, ACECharacterOptions::PageCharacter },
		{ TEXT("Ignore fellowship requests"),           0x02, false, 0x00000008u, ACECharacterOptions::PageCharacter },
		{ TEXT("Auto-accept fellowship requests"),      0x12, false, 0x20000000u, ACECharacterOptions::PageCharacter },
		{ TEXT("Share fellowship XP and luminance"),    0x0F, false, 0x00040000u, ACECharacterOptions::PageCharacter },
		{ TEXT("Share fellowship loot"),                0x11, false, 0x00100000u, ACECharacterOptions::PageCharacter },
		{ TEXT("Accept corpse looting permissions"),    0x10, false, 0x00080000u, ACECharacterOptions::PageCharacter },
		{ TEXT("Attempt to deceive other players"),     0x09, false, 0x00000200u, ACECharacterOptions::PageCharacter },
		{ TEXT("Salvage multiple materials at once"),   0x22, true,  0x00000080u, ACECharacterOptions::PageCharacter },
		{ TEXT("Use crafting success dialog"),          0x1A, false, 0x80000000u, ACECharacterOptions::PageCharacter },
		{ TEXT("Confirm use of rare gems"),             0x2D, true,  0x00040000u, ACECharacterOptions::PageCharacter },
		{ TEXT("Pick up items into main pack"),         0x29, true,  0x00004000u, ACECharacterOptions::PageCharacter },
		{ TEXT("Appear offline"),                       0x27, true,  0x00001000u, ACECharacterOptions::PageCharacter },
		{ TEXT("Others may see your date of birth"),    0x1C, true,  0x00000002u, ACECharacterOptions::PageCharacter },
		{ TEXT("Others may see your age"),              0x1D, true,  0x00000020u, ACECharacterOptions::PageCharacter },
		{ TEXT("Others may see your chess rank"),       0x1E, true,  0x00000004u, ACECharacterOptions::PageCharacter },
		{ TEXT("Others may see your fishing skill"),    0x1F, true,  0x00000008u, ACECharacterOptions::PageCharacter },
		{ TEXT("Others may see your deaths"),           0x20, true,  0x00000010u, ACECharacterOptions::PageCharacter },
		{ TEXT("Others may see your titles"),           0x28, true,  0x00002000u, ACECharacterOptions::PageCharacter },

		// Chat
		{ TEXT("Listen to allegiance chat"),            0x1B, false, 0x40000000u, ACECharacterOptions::PageChat },
		{ TEXT("Listen to general chat"),               0x23, true,  0x00000100u, ACECharacterOptions::PageChat },
		{ TEXT("Listen to trade chat"),                 0x24, true,  0x00000200u, ACECharacterOptions::PageChat },
		{ TEXT("Listen to LFG chat"),                   0x25, true,  0x00000400u, ACECharacterOptions::PageChat },
		{ TEXT("Listen to roleplay chat"),              0x26, true,  0x00000800u, ACECharacterOptions::PageChat },
		{ TEXT("Listen to society chat"),               0x2E, true,  0x00080000u, ACECharacterOptions::PageChat },
		{ TEXT("Listen to PK death messages"),          0x34, true,  0x02000000u, ACECharacterOptions::PageChat },
		{ TEXT("Display timestamps"),                   0x21, true,  0x00000040u, ACECharacterOptions::PageChat },
		{ TEXT("Filter language"),                      0x2C, true,  0x00020000u, ACECharacterOptions::PageChat },
		{ TEXT("Stay in chat mode after sending"),      0x0B, false, 0x00000800u, ACECharacterOptions::PageChat },
		{ TEXT("Show allegiance logons"),               0x18, false, 0x08000000u, ACECharacterOptions::PageChat },

		// Config (interface / display)
		{ TEXT("Display 3D tooltips"),                  0x08, false, 0x00000100u, ACECharacterOptions::PageConfig },
		{ TEXT("Side by side vitals"),                  0x13, false, 0x00200000u, ACECharacterOptions::PageConfig },
		{ TEXT("Show coordinates by the radar"),        0x14, false, 0x00400000u, ACECharacterOptions::PageConfig },
		{ TEXT("Display spell durations"),              0x15, false, 0x00800000u, ACECharacterOptions::PageConfig },
		{ TEXT("Advanced combat interface"),            0x0C, false, 0x00001000u, ACECharacterOptions::PageConfig },
		{ TEXT("Lock UI"),                              0x33, true,  0x01000000u, ACECharacterOptions::PageConfig },
		{ TEXT("Use mouse turning"),                    0x31, true,  0x00400000u, ACECharacterOptions::PageConfig },
		{ TEXT("Disable most weather effects"),         0x04, false, 0x00010000u, ACECharacterOptions::PageConfig },
		{ TEXT("Always daylight outdoors"),             0x05, true,  0x00000001u, ACECharacterOptions::PageConfig },
		{ TEXT("Disable distance fog"),                 0x30, true,  0x00200000u, ACECharacterOptions::PageConfig },
		{ TEXT("Disable house restriction effects"),    0x16, false, 0x02000000u, ACECharacterOptions::PageConfig },
		{ TEXT("Show your helm or head gear"),          0x2F, true,  0x00100000u, ACECharacterOptions::PageConfig },
		{ TEXT("Show your cloak"),                      0x32, true,  0x00800000u, ACECharacterOptions::PageConfig },
	};
}

namespace ACECharacterOptions
{
	TArrayView<const FACECharacterOptionDesc> GetTable()
	{
		return TArrayView<const FACECharacterOptionDesc>(GTable, UE_ARRAY_COUNT(GTable));
	}

	void GetPageOptions(uint8 Page, TArray<const FACECharacterOptionDesc*>& Out)
	{
		Out.Reset();
		for (const FACECharacterOptionDesc& Desc : GTable)
		{
			if (Desc.Page == Page)
			{
				Out.Add(&Desc);
			}
		}
	}

	const FACECharacterOptionDesc* Find(int32 Option)
	{
		for (const FACECharacterOptionDesc& Desc : GTable)
		{
			if (Desc.Option == Option)
			{
				return &Desc;
			}
		}
		return nullptr;
	}
}
