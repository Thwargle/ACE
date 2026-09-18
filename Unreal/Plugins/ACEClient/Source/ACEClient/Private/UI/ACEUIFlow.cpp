#include "UI/ACEUIFlow.h"
#include "UI/ACEUIElementManager.h"
#include "UI/ACEUILayoutResolver.h"

void UACEUIFlow::Initialize(UACEUIElementManager* InManager, UACEUILayoutResolver* InLayoutResolver)
{
	Manager = InManager;
	LayoutResolver = InLayoutResolver;
	Mode = ACEUI::EACEUIFlowMode::None;
}

void UACEUIFlow::Shutdown()
{
	Mode = ACEUI::EACEUIFlowMode::None;
	Manager = nullptr;
	LayoutResolver = nullptr;
}

uint32 UACEUIFlow::GetLayoutIdForMode(ACEUI::EACEUIFlowMode InMode) const
{
	switch (InMode)
	{
	case ACEUI::EACEUIFlowMode::Patch: return ACEUI::LayoutId::Patch;
	case ACEUI::EACEUIFlowMode::Intro: return ACEUI::LayoutId::Intro;
	case ACEUI::EACEUIFlowMode::CharacterCreation: return 0x21000038;
	case ACEUI::EACEUIFlowMode::CharacterManagement: return ACEUI::LayoutId::CharacterManagement;
	case ACEUI::EACEUIFlowMode::Gameplay: return ACEUI::LayoutId::ClassicGameplay;
	case ACEUI::EACEUIFlowMode::Disconnected: return ACEUI::LayoutId::Disconnected;
	case ACEUI::EACEUIFlowMode::Credits: return ACEUI::LayoutId::Credits;
	default: return 0;
	}
}

void UACEUIFlow::SetMode(ACEUI::EACEUIFlowMode NewMode)
{
	if (Mode == NewMode)
	{
		return;
	}
	Mode = NewMode;
	const uint32 LayoutId = GetLayoutIdForMode(Mode);
	if (Manager && LayoutResolver && LayoutResolver->IsReady() && LayoutId != 0)
	{
		Manager->ClearRoots();
		LayoutResolver->LoadLayout(LayoutId);
	}
	else if (Manager)
	{
		Manager->ClearRoots();
	}
	UE_LOG(LogTemp, Log, TEXT("ACE UIFlow mode -> %d layout=0x%08X"),
		static_cast<int32>(Mode), LayoutId);
}
