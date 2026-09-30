#pragma once

#include "ACETypes.h"

namespace ACERetailObjectNames
{
#include "Protocol/ACEMaterialTypeNames.inl"

inline FString Name(const FACEWorldObject& Object, bool bPlural = false)
{
    // ACCWeenieObject::GetObjectName: use the supplied plural, otherwise append
    // s/es, then normalize the material prefix already present in some names.
    FString Result = bPlural && !Object.PluralName.IsEmpty() ? Object.PluralName : Object.Name;
    if (bPlural && Object.PluralName.IsEmpty() && !Result.IsEmpty())
        Result += Result.EndsWith(TEXT("s"), ESearchCase::CaseSensitive) ? TEXT("es") : TEXT("s");
    if (Object.MaterialType > 0 && Object.MaterialType <= 77)
    {
        const FString Material = GetMaterialTypeName(Object.MaterialType);
        if (Result.ReplaceInline(*Material, TEXT(""), ESearchCase::CaseSensitive)) Result.TrimStartAndEndInline();
        Result = Material + TEXT(" ") + Result;
    }
    if ((Object.ObjectDescriptionFlags & ACEObjectDescFlag::HiddenAdmin) && Result.StartsWith(TEXT("+")))
        Result.RightChopInline(1);
    return Result;
}
}
