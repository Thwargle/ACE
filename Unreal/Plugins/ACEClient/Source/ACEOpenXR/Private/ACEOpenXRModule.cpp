#include "Modules/ModuleManager.h"
#include "IOpenXRExtensionPlugin.h"
#include "IOpenXRHMDModule.h"

// Loaded before OpenXR creates its instance. The game module loads too late to
// negotiate optional extensions. SteamVR/desktop keeps its runtime's settings.
class FACEOpenXRModule final : public IModuleInterface, public IOpenXRExtensionPlugin
{
public:
    void StartupModule() override { RegisterOpenXRExtensionModularFeature(); }
    void ShutdownModule() override { UnregisterOpenXRExtensionModularFeature(); }
    FString GetDisplayName() override { return TEXT("ACE Quest frame pacing"); }
    bool GetOptionalExtensions(TArray<const ANSICHAR*>& Out) override
    {
        Out.Add(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME);
        return true;
    }
    bool InsertOpenXRAPILayer(PFN_xrGetInstanceProcAddr& InOut) override
    {
        GetProc = InOut; // Observe the engine loader; do not replace or wrap it.
        return false;
    }
    const void* OnCreateInstance(IOpenXRHMDModule* Module, const void* Next) override
    {
        HMDModule = Module;
        return Next;
    }
    void PostCreateInstance(XrInstance Instance) override
    {
        Enumerate = nullptr; Request = nullptr; GetRate = nullptr;
        if (!GetProc || !HMDModule || !HMDModule->IsExtensionEnabled(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME))
        {
            UE_LOG(LogTemp, Log, TEXT("ACE XR: refresh extension unavailable; retaining runtime rate"));
            return;
        }
        GetProc(Instance, "xrEnumerateDisplayRefreshRatesFB", reinterpret_cast<PFN_xrVoidFunction*>(&Enumerate));
        GetProc(Instance, "xrRequestDisplayRefreshRateFB", reinterpret_cast<PFN_xrVoidFunction*>(&Request));
        GetProc(Instance, "xrGetDisplayRefreshRateFB", reinterpret_cast<PFN_xrVoidFunction*>(&GetRate));
    }
    const void* OnBeginSession(XrSession Session, const void* Next) override
    {
        bPending = true;
        return Next;
    }
    void OnDestroySession(XrSession Session) override { bPending = false; }
    void* OnWaitFrame(XrSession Session, void* Next) override
    {
        if (!bPending) return Next;
        bPending = false;
        if (!Enumerate || !Request || !GetRate) return Next;
        uint32 Count = 0;
        if (XR_FAILED(Enumerate(Session, 0, &Count, nullptr)) || Count == 0 || Count > 64) return Next;
        TArray<float> Rates; Rates.SetNumUninitialized(Count);
        if (XR_FAILED(Enumerate(Session, Count, &Count, Rates.GetData()))) return Next;
        bool Supports90 = false;
        for (uint32 I = 0; I < Count; ++I) Supports90 |= FMath::IsNearlyEqual(Rates[I], 90.f, .1f);
        if (Supports90)
        {
            const XrResult Result = Request(Session, 90.f);
            UE_LOG(LogTemp, Log, TEXT("ACE XR: requested supported 90 Hz, result=%d (frame budget 11.111 ms)"), int32(Result));
        }
        float Current = 0.f;
        if (XR_SUCCEEDED(GetRate(Session, &Current)))
            UE_LOG(LogTemp, Log, TEXT("ACE XR: current display %.1f Hz; refresh request may complete asynchronously"), Current);
        return Next;
    }
    void OnEvent(XrSession Session, const XrEventDataBaseHeader* Header) override
    {
        if (Header && Header->type == XR_TYPE_EVENT_DATA_DISPLAY_REFRESH_RATE_CHANGED_FB)
        {
            const auto* Event = reinterpret_cast<const XrEventDataDisplayRefreshRateChangedFB*>(Header);
            UE_LOG(LogTemp, Log, TEXT("ACE XR: display refresh confirmed %.1f -> %.1f Hz"), Event->fromDisplayRefreshRate, Event->toDisplayRefreshRate);
        }
    }
private:
    IOpenXRHMDModule* HMDModule = nullptr;
    PFN_xrGetInstanceProcAddr GetProc = nullptr;
    PFN_xrEnumerateDisplayRefreshRatesFB Enumerate = nullptr;
    PFN_xrRequestDisplayRefreshRateFB Request = nullptr;
    PFN_xrGetDisplayRefreshRateFB GetRate = nullptr;
    bool bPending = false;
};
IMPLEMENT_MODULE(FACEOpenXRModule, ACEOpenXR)
