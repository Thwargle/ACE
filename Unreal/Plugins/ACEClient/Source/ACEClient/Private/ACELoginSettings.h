#pragma once
#include "CoreMinimal.h"

/** Last entries on this installation. Nothing is supplied on a fresh install. */
struct FACELoginSettings
{
	FString Host;
	FString Port = TEXT("9000");
	FString Account;
	FString Password;
};

namespace ACELoginSettings
{
	FString GetPath();
	/** Entire payload protected by Windows DPAPI or Android Keystore AES-GCM. */
	bool Load(FACELoginSettings& Settings, const FString& Path = GetPath());
	bool Save(const FACELoginSettings& Settings, const FString& Path = GetPath());
}
