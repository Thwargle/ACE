#pragma once

#include "UnrealEd.h"
#include "Engine.h"
#include "ParticleDefinitions.h"
#include "SoundDefinitions.h"
#include "Net/UnrealNetwork.h"
#include "ACEWorldBakeClasses.h"
#include "ACEWorldBakeEditorClasses.h"

ACEWORLDBAKEEDITOR_API DECLARE_LOG_CATEGORY_EXTERN(LogACEWorldBakeEd, All, All);

#define ACEWORLDBAKEED_LOG_PREFIX TEXT("ACEWorldBakeEd: ")
#define UE_LOG_ACEWORLDBAKEED(Verbosity, Format, ...) \
{ \
	UE_LOG(LogACEWorldBakeEd, Verbosity, TEXT("%s%s"), ACEWORLDBAKEED_LOG_PREFIX, *FString::Printf(Format, ##__VA_ARGS__)); \
}
