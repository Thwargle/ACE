#include "ACEBodySweep.h"
#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<int32> GACEBatchCrowdQueries(TEXT("ace.Collision.BatchCrowdQueries"),1,
    TEXT("Gather escapable creature overlaps and floor-ray obstructions together. 0 retains per-body retries for comparison."));

bool ACEBodySweep::BatchCrowdQueries()
{
    return GACEBatchCrowdQueries.GetValueOnGameThread()!=0;
}
