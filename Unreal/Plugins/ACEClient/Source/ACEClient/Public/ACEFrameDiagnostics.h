#pragma once
#include "CoreMinimal.h"

class UWorld;

/** Limits gameplay hitch reporting; never schedules captures or GPU readbacks. */
struct FACEFrameReportGate
{
 double ReadyAt=0,NextReportAt=0;
 int32 Reports=0;
 bool bReady=false;
 bool Update(double Now,double FrameMs,bool bGameplay)
 {
  if(!bGameplay){bReady=false;return false;}
  // Portal/loading frames are excluded by bGameplay. Keep only a short settle
  // period: first-room streaming hitches can happen seconds after arrival.
  if(!bReady){bReady=true;ReadyAt=Now+1;return false;}
  if(!FMath::IsFinite(FrameMs) || FrameMs<50 || Now<ReadyAt || Now<NextReportAt || Reports>=20)return false;
  ++Reports;NextReportAt=Now+10;return true;
 }
};

namespace ACEFrameDiagnostics
{
 /** Most recent asynchronous engine timings plus a cheap inventory of effect hosts. */
 ACECLIENT_API TArray<FString> Describe(UWorld* World,double FrameMs);
}
