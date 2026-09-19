#pragma once
#include "ACELoginProfile.h"

namespace ACELoginSettings
{
	FString GetPath();
	/** Entire payload protected by Windows DPAPI or Android Keystore AES-GCM. */
	bool Load(FACELoginSettings& Settings, const FString& Path = GetPath());
	bool Save(const FACELoginSettings& Settings, const FString& Path = GetPath());
}
