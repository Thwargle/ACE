#include "ACEWorldBakeEditor.h"
#include "ACEPrepareRuntimeMaterialsCommandlet.h"
#include "Misc/CoreDelegates.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "ModelContextProtocolSettings.h"

DEFINE_LOG_CATEGORY(LogACEWorldBakeEd);

class FACEWorldBakeEditorModule : public FDefaultGameModuleImpl
{
	FDelegateHandle CookHandle;
public:
	virtual void StartupModule() override
	{
		// UE 5.8 starts the MCP listener during post-engine-init, including in
		// commandlets. A cook launched from the editor would contend for its port.
		// Override only this process's settings CDO before that callback runs;
		// never SaveConfig, so the interactive editor keeps its auto-start setting.
		if (IsRunningCommandlet())
		{
			GetMutableDefault<UModelContextProtocolSettings>()->bAutoStartServer = false;
			UE_LOG(LogACEWorldBakeEd, Display, TEXT("MCP auto-start disabled for this commandlet; editor preferences preserved"));
		}

		CookHandle = FCoreDelegates::OnCommandletPreMain.AddLambda([]
		{
			// Run before asset discovery, also for packaging launched from the editor.
			// Multi-process workers consume the assets saved by the cook director.
			if (IsRunningCookCommandlet() && !FParse::Param(FCommandLine::Get(), TEXT("CookWorker")))
			{
				if (!UACEPrepareRuntimeMaterialsCommandlet::PrepareMaterials())
					UE_LOG(LogACEWorldBakeEd, Fatal, TEXT("ACE runtime shader preparation failed; see preceding errors"));
			}
		});
	}
	virtual void ShutdownModule() override
	{
		FCoreDelegates::OnCommandletPreMain.Remove(CookHandle);
	}
};

IMPLEMENT_GAME_MODULE(FACEWorldBakeEditorModule, ACEWorldBakeEditor);
