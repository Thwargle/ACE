#pragma once
#include "CoreMinimal.h"

namespace ACEAppraisalFormatting
{
// ItemExamineUI::AddItemInfo uses one line within a section and two between
// sections. Normalize line endings, preserving authored breaks inside text.
inline void AppendItemText(FString& Text, FString Part, bool Paragraph = false)
{
    Part.ReplaceInline(TEXT("\r\n"), TEXT("\n"));
    Part.ReplaceInline(TEXT("\r"), TEXT("\n"));
    while (Part.StartsWith(TEXT("\n"))) Part.RightChopInline(1);
    while (Part.EndsWith(TEXT("\n"))) Part.LeftChopInline(1);
    if (Part.IsEmpty()) return;
    while (Text.EndsWith(TEXT("\n"))) Text.LeftChopInline(1);
    if (!Text.IsEmpty()) Text += Paragraph ? TEXT("\n\n") : TEXT("\n");
    Text += Part;
}
}
