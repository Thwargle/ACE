#pragma once
#include "CoreMinimal.h"

/** Retail StartBarber / FinishBarber field order; asset IDs, not chargen indices. */
struct ACECLIENT_API FACEBarberProfile
{
	enum EField { BasePalette, Head, HairTexture, DefaultHairTexture, EyesTexture, DefaultEyesTexture,
		NoseTexture, DefaultNoseTexture, MouthTexture, DefaultMouthTexture, SkinPalette, HairPalette,
		EyesPalette, Setup, Option1, Option2, Count };
	uint32 Values[Count]={};
	bool HasSuppressedEffect(uint32 Heritage) const;
	static uint32 EffectSetup(uint32 Heritage,uint32 Gender,uint32 SetupId,bool Suppressed);
};
