#include "UI/ACERetailUILayout.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

namespace ACERetailUILayout
{
    bool Parse(const FString& Text, TArray<FRect>& Rects, FString& Error)
    {
        Rects.Reset(); Error.Reset();
        TArray<FString> Lines; Text.ParseIntoArrayLines(Lines);
        const FRegexPattern Pattern(TEXT("^<([A-Z0-9]+)>\\s+X:\\s*(-?[0-9]+)\\s+Y:\\s*(-?[0-9]+)\\s+W:\\s*([0-9]+)\\s+H:\\s*([0-9]+)\\s*$"));
        TSet<FString> Seen;
        for (int32 I = 0; I < Lines.Num(); ++I)
        {
            const FString Line = Lines[I].TrimStartAndEnd();
            if (Line.IsEmpty() || Line.StartsWith(TEXT("#"))) continue;
            // Retail skips unrecognized tags. Validate every recognized record
            // before changing any window, so a bad file cannot half-apply.
            bool Known = false;
            for (const auto& Window : Windows)
                Known |= Line.StartsWith(FString::Printf(TEXT("<%s>"), Window.Tag));
            if (!Known) continue;
            FRegexMatcher Match(Pattern, Line);
            FRect Rect; int64 Values[4] = {};
            bool Valid = Match.FindNext();
            if (Valid)
            {
                Rect.Tag = Match.GetCaptureGroup(1);
                for (int32 J = 0; J < 4; ++J)
                    Valid &= LexTryParseString(Values[J], *Match.GetCaptureGroup(J + 2))
                        && Values[J] >= -1000000 && Values[J] <= 1000000;
                Valid &= Values[2] > 0 && Values[3] > 0 && !Seen.Contains(Rect.Tag);
            }
            if (!Valid)
            {
                Error = FString::Printf(TEXT("Invalid UI layout at line %d. No windows were changed."), I + 1);
                Rects.Reset(); return false;
            }
            Rect.X = int32(Values[0]); Rect.Y = int32(Values[1]);
            Rect.W = int32(Values[2]); Rect.H = int32(Values[3]);
            Seen.Add(Rect.Tag); Rects.Add(Rect);
        }
        if (Rects.IsEmpty()) { Error = TEXT("This file contains no retail UI window records."); return false; }
        return true;
    }

    bool NamedFile(const FString& Arguments, FString& Filename, FString& Error)
    {
        FString Name = Arguments.TrimStartAndEnd(); Error.Reset(); Filename.Reset();
        if (Name.IsEmpty()) { Filename = TEXT("UI-Default.txt"); return true; }
        const bool Quoted = Name.StartsWith(TEXT("\"")) && Name.EndsWith(TEXT("\"")) && Name.Len() >= 2;
        if (Quoted) Name = Name.Mid(1, Name.Len() - 2);
        if (Name.EndsWith(TEXT(".txt"), ESearchCase::IgnoreCase)) Name.LeftChopInline(4);
        if (Name.IsEmpty() || Name.Len() > 16 || Name.TrimStartAndEnd() != Name
            || (!Quoted && Name.Contains(TEXT(" "))))
        { Error = TEXT("Use a layout name of 1-16 characters. Quote names containing spaces."); return false; }
        for (const TCHAR C : Name)
            if (C < 32 || FString(TEXT("<>:\"/\\|?*.")).Contains(FString::Chr(C)))
            { Error = TEXT("Use a layout name, not a path or filename containing reserved characters."); return false; }
        const FString Upper = Name.ToUpper();
        if (Upper == TEXT("CON") || Upper == TEXT("PRN") || Upper == TEXT("AUX") || Upper == TEXT("NUL")
            || (Upper.Len() == 4 && (Upper.StartsWith(TEXT("COM")) || Upper.StartsWith(TEXT("LPT"))) && FChar::IsDigit(Upper[3])))
        { Error = TEXT("That layout name is reserved by the operating system."); return false; }
        Filename = Name + TEXT(".txt"); return true;
    }

    FString AutoFile(const FString& Server, const FString& Character, FIntPoint Size)
    {
        auto Escape = [](const FString& Value)
        {
            FString Result;
            for (const TCHAR C : Value)
                if (FChar::IsAlnum(C) || C == TEXT('_')) Result.AppendChar(C);
                else Result += FString::Printf(TEXT("%%%04X"), uint32(C));
            return Result;
        };
        return FString::Printf(TEXT("UI-%s-%s-%d-%d.txt"), *Escape(Server), *Escape(Character), Size.X, Size.Y);
    }

    bool Save(const FString& Path, const FString& Text, FString& Error)
    {
        Error.Reset();
        IFileManager& Files = IFileManager::Get();
        const FString Temp = Path + TEXT(".") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".tmp");
        const bool Success = Files.MakeDirectory(*FPaths::GetPath(Path), true)
            && FFileHelper::SaveStringToFile(Text, *Temp, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
            && Files.Move(*Path, *Temp, true, true);
        if (!Success)
        {
            Files.Delete(*Temp);
            Error = FString::Printf(TEXT("Could not save UI layout: %s"), *Path);
        }
        return Success;
    }

    bool Load(const FString& Path, FString& Text, FString& Error)
    {
        Error.Reset(); Text.Reset();
        const int64 Size = IFileManager::Get().FileSize(*Path);
        if (Size < 0) { Error = FString::Printf(TEXT("UI layout not found: %s"), *Path); return false; }
        if (Size > 65536) { Error = TEXT("UI layout file is too large (maximum 64 KiB)."); return false; }
        if (!FFileHelper::LoadFileToString(Text, *Path))
        { Error = FString::Printf(TEXT("Could not read UI layout: %s"), *Path); return false; }
        return true;
    }
}
