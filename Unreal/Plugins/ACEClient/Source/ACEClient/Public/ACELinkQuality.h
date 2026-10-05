#pragma once
#include "ACETypes.h"

namespace ACELinkQuality
{
enum class EQuality : uint8 { Good, Fair, Poor };
inline EQuality Evaluate(const FACELinkStatus& Status)
{
    // Retain retail's packet-silence thresholds, but a stream of late or
    // retransmitted packets must not make an unusable connection green.
    if (!Status.bConnected || Status.SecondsSinceLastPacket >= 20.f
        || Status.PacketLossPercent >= 10.f || (Status.bHasPing && Status.RoundTripSeconds >= 1.f))
        return EQuality::Poor;
    if (Status.SecondsSinceLastPacket >= 5.f || Status.PacketLossPercent >= 2.f
        || (Status.bHasPing && Status.RoundTripSeconds >= .3f))
        return EQuality::Fair;
    return EQuality::Good;
}
}
