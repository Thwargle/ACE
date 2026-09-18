#include "VR/ACEVRPlatformTextEntry.h"
#include "VR/ACEVRKeyboardSubmitInput.h"
#include "Framework/Application/SlateApplication.h"
#if PLATFORM_ANDROID
#include "Android/AndroidApplication.h"
#include "Android/AndroidJNI.h"
#include "Android/AndroidJavaEnv.h"
#endif

FACEVRPlatformTextEntry::~FACEVRPlatformTextEntry()
{
    if (SubmitInput && FSlateApplication::IsInitialized())
        FSlateApplication::Get().UnregisterInputPreProcessor(SubmitInput);
}

void FACEVRPlatformTextEntry::EnableNativeSubmit()
{
#if PLATFORM_ANDROID
    if (SubmitInput || !FSlateApplication::IsInitialized()) return;
    SubmitInput = MakeShared<FACEVRKeyboardSubmitInput>([]
    {
        // Read the final IME buffer on Android's UI thread. GetText() here can
        // still be missing the final characters waiting in UE's change timer.
        if (JNIEnv* Env = FAndroidApplication::GetJavaEnv())
        {
            static jmethodID Method = FJavaWrapper::FindMethod(Env,FJavaWrapper::GameActivityClassID,
                "AndroidThunkJava_ACEAcceptKeyboardInput","()V",false);
            FJavaWrapper::CallVoidMethod(Env,FJavaWrapper::GameActivityThis,Method);
        }
    });
    FSlateApplication::Get().RegisterInputPreProcessor(SubmitInput, 0);
#endif
}

void ACEVRUpdateNativeKeyboardText(const FString& Expected, const FString& Replacement)
{
#if PLATFORM_ANDROID
    if (JNIEnv* Env = FAndroidApplication::GetJavaEnv())
    {
        static jmethodID Method = FJavaWrapper::FindMethod(Env,FJavaWrapper::GameActivityClassID,
            "AndroidThunkJava_ACEUpdateKeyboardText","(Ljava/lang/String;Ljava/lang/String;)V",false);
        const auto Before = FJavaHelper::ToJavaString(Env, Expected);
        const auto After = FJavaHelper::ToJavaString(Env, Replacement);
        FJavaWrapper::CallVoidMethod(Env,FJavaWrapper::GameActivityThis,Method,*Before,*After);
    }
#endif
}
