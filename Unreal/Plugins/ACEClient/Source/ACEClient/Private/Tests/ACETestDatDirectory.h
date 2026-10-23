#pragma once

#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

// Packaged Linux fixtures must honor the same explicit DAT path as Windows.
inline FString ACETestDatDirectory()
{
    FString Directory=TEXT("C:/Turbine/Asheron's Call");
    FParse::Value(FCommandLine::Get(),TEXT("RetailDatDir="),Directory);
    return Directory;
}
