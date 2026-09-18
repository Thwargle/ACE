#pragma once

#include "CoreMinimal.h"

/**
 * Retail Asheron's Call UI constants and ElementDesc.Type IDs.
 * Source: client ForceDisplayResolution(800,600) + UIElement::RegisterElementClass.
 * Do not invent new IDs — extend only from Docs/UI/ElementTypeRegistry.csv.
 */
namespace ACEUI
{
	/** Original client reference resolution. LayoutDesc coords stay in this space; viewport stretches to fill. */
	constexpr int32 ReferenceWidth = 800;
	constexpr int32 ReferenceHeight = 600;

	/** Inclusive rects in retail: x1 = x + width - 1. */
	inline void InclusiveSizeToExclusive(int32 InX, int32 InY, int32 InW, int32 InH,
		int32& OutX0, int32& OutY0, int32& OutX1, int32& OutY1)
	{
		OutX0 = InX;
		OutY0 = InY;
		OutX1 = InX + FMath::Max(InW, 1) - 1;
		OutY1 = InY + FMath::Max(InH, 1) - 1;
	}

	namespace ElementType
	{
		constexpr uint32 Button = 0x00000001u;
		constexpr uint32 Dragbar = 0x00000002u;
		constexpr uint32 Field = 0x00000003u;
		constexpr uint32 ListBox = 0x00000005u;
		constexpr uint32 Menu = 0x00000006u;
		constexpr uint32 Meter = 0x00000007u;
		constexpr uint32 Panel = 0x00000008u;
		constexpr uint32 Resizebar = 0x00000009u;
		constexpr uint32 Scrollbar = 0x0000000Bu;
		constexpr uint32 Text = 0x0000000Cu;
		constexpr uint32 Viewport = 0x0000000Du;
		constexpr uint32 Browser = 0x0000000Eu;
		constexpr uint32 ColorPicker = 0x00000010u;
		constexpr uint32 GroupBox = 0x00000011u;

		constexpr uint32 Toolbar = 0x10000007u;
		constexpr uint32 PanelHost = 0x10000008u;
		constexpr uint32 Vitals = 0x10000009u;
		constexpr uint32 Indicators = 0x1000000Au;
		constexpr uint32 SmartBox = 0x10000014u;
		constexpr uint32 Spellcasting = 0x10000015u;
		constexpr uint32 Inventory = 0x10000023u;
		constexpr uint32 PaperDoll = 0x10000024u;
		constexpr uint32 Spellbook = 0x1000002Eu;
		constexpr uint32 SmartBoxWrapper = 0x10000030u;
		constexpr uint32 MainChat = 0x10000041u;
	}

	namespace LayoutId
	{
		constexpr uint32 Patch = 0x21000000u;
		constexpr uint32 Intro = 0x21000001u;
		constexpr uint32 Disconnected = 0x21000002u;
		constexpr uint32 Credits = 0x21000003u;
		constexpr uint32 CharacterManagement = 0x21000004u;
		constexpr uint32 ClassicGameplay = 0x21000005u;
		constexpr uint32 ClassicChat = 0x21000006u;
	}

	enum class EACEUIFlowMode : uint8
	{
		None = 0,
		Patch,
		Intro,
		CharacterManagement,
		CharacterCreation,
		CharGen,
		Gameplay,
		Disconnected,
		Credits,
		Epilogue
	};
}
