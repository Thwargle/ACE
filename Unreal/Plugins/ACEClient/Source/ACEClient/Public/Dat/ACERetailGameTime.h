#pragma once
#include "CoreMinimal.h"
#include "Dat/ACEDatFileTypes.h"

namespace ACERetailGameTime
{
	/** GameTime::CalcDayBegin / CalcTimeOfDay use the same positive epoch offset. */
	inline double DayIndex(double Ticks, const FACEDatRegionSky& Sky)
	{
		return FMath::Floor((Ticks + Sky.ZeroTimeOfYear) / FMath::Max(1.f, Sky.DayLengthSeconds));
	}
	inline float DayFraction(double Ticks, const FACEDatRegionSky& Sky)
	{
		const double Days = (Ticks + Sky.ZeroTimeOfYear) / FMath::Max(1.f, Sky.DayLengthSeconds);
		return static_cast<float>(Days - FMath::Floor(Days));
	}
	inline uint32 DaySeed(double Ticks, const FACEDatRegionSky& Sky)
	{
		// ACE.Common.DerethDateTime.Ticks is relative to PY 10; DAT ZeroYear is
		// required to reproduce retail GameTime.current_year, including on ACE servers.
		return static_cast<uint32>(static_cast<int64>(DayIndex(Ticks, Sky))) + Sky.DaysPerYear * Sky.ZeroYear;
	}
}
