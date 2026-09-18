#include "ACELoginSettings.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#include <dpapi.h>
#elif PLATFORM_ANDROID
#include "Android/AndroidApplication.h"
#include "Android/AndroidJNI.h"
#endif

namespace ACELoginSettings
{
#if PLATFORM_ANDROID
	static bool AndroidCrypt(const uint8* Data, int32 Size, bool Encrypt, TArray<uint8>& Output)
	{
		JNIEnv* Env = FAndroidApplication::GetJavaEnv();
		if (!Env || Size < 0 || Size > 131072 || !FJavaWrapper::GameActivityThis) return false;
		static jmethodID Method = FJavaWrapper::FindMethod(Env, FJavaWrapper::GameActivityClassID,
			"AndroidThunkJava_ACELoginCrypt", "([BZ)[B", false);
		if (!Method) return false;
		jbyteArray Input = Env->NewByteArray(Size);
		if (!Input) return false;
		Env->SetByteArrayRegion(Input, 0, Size, reinterpret_cast<const jbyte*>(Data));
		auto Result = static_cast<jbyteArray>(Env->CallObjectMethod(FJavaWrapper::GameActivityThis, Method, Input, jboolean(Encrypt)));
		Env->DeleteLocalRef(Input);
		if (Env->ExceptionCheck()) { Env->ExceptionClear(); return false; }
		if (!Result) return false;
		const int32 Length = Env->GetArrayLength(Result);
		const bool Valid = Length > 0 && Length <= 131072;
		if (Valid) { Output.SetNumUninitialized(Length); Env->GetByteArrayRegion(Result, 0, Length, reinterpret_cast<jbyte*>(Output.GetData())); }
		Env->DeleteLocalRef(Result);
		return Valid;
	}
#endif
	FString GetPath()
	{
		return FPaths::ProjectSavedDir() / TEXT("Login/LastLogin.dat");
	}

	bool Save(const FACELoginSettings& Settings, const FString& Path)
	{
		auto Json = MakeShared<FJsonObject>();
		Json->SetStringField(TEXT("host"), Settings.Host);
		Json->SetStringField(TEXT("port"), Settings.Port);
		Json->SetStringField(TEXT("account"), Settings.Account);
		Json->SetStringField(TEXT("password"), Settings.Password);
		FString Text;
		if (!FJsonSerializer::Serialize(Json, TJsonWriterFactory<>::Create(&Text))) return false;
		const FTCHARToUTF8 Utf8(*Text);
		if (Utf8.Length() > 65536) return false;
		TArray<uint8> Bytes;
#if PLATFORM_WINDOWS
		DATA_BLOB Plain{static_cast<DWORD>(Utf8.Length()), reinterpret_cast<BYTE*>(const_cast<ANSICHAR*>(Utf8.Get()))};
		DATA_BLOB Protected{};
		if (!CryptProtectData(&Plain, L"ACE login", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &Protected)) return false;
		Bytes.Append(Protected.pbData, Protected.cbData);
		LocalFree(Protected.pbData);
#elif PLATFORM_ANDROID
		if (!AndroidCrypt(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length(), true, Bytes)) return false;
#else
		return false;
#endif
		const FString Directory = FPaths::GetPath(Path);
		if (!IFileManager::Get().MakeDirectory(*Directory, true)) return false;
		const FString TemporaryPath = Path + TEXT(".tmp");
		const bool Saved = FFileHelper::SaveArrayToFile(Bytes, *TemporaryPath)
			&& IFileManager::Get().Move(*Path, *TemporaryPath, true, false);
		if (!Saved) IFileManager::Get().Delete(*TemporaryPath, false, true);
		return Saved;
	}

	bool Load(FACELoginSettings& Settings, const FString& Path)
	{
		const int64 Size = IFileManager::Get().FileSize(*Path);
		if (Size <= 0 || Size > 131072) return false;
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *Path)) return false;
		FString Text;
#if PLATFORM_WINDOWS
		DATA_BLOB Protected{static_cast<DWORD>(Bytes.Num()), Bytes.GetData()};
		DATA_BLOB Plain{};
		if (!CryptUnprotectData(&Protected, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &Plain)) return false;
		if (Plain.cbData <= 65536)
		{
			const FUTF8ToTCHAR Utf8(reinterpret_cast<const ANSICHAR*>(Plain.pbData), Plain.cbData);
			Text = FString(Utf8.Length(), Utf8.Get());
		}
		SecureZeroMemory(Plain.pbData, Plain.cbData);
		LocalFree(Plain.pbData);
#elif PLATFORM_ANDROID
		TArray<uint8> Plain;
		if (!AndroidCrypt(Bytes.GetData(), Bytes.Num(), false, Plain)) return false;
		if (Plain.Num() <= 65536)
		{
			const FUTF8ToTCHAR Utf8(reinterpret_cast<const ANSICHAR*>(Plain.GetData()), Plain.Num());
			Text = FString(Utf8.Length(), Utf8.Get());
		}
		FMemory::Memzero(Plain.GetData(), Plain.Num());
#else
		return false;
#endif
		TSharedPtr<FJsonObject> Json;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) || !Json) return false;
		FACELoginSettings Loaded;
		if (!Json->TryGetStringField(TEXT("host"), Loaded.Host)
			|| !Json->TryGetStringField(TEXT("port"), Loaded.Port)
			|| !Json->TryGetStringField(TEXT("account"), Loaded.Account)
			|| !Json->TryGetStringField(TEXT("password"), Loaded.Password)) return false;
		Settings = MoveTemp(Loaded);
		return true;
	}
}
