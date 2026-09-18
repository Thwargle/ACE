#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ACEUILayoutResolver.generated.h"

struct FACEUIElement;
class UACEUIElementManager;
class UACEDatSubsystem;

/**
 * Inflates retail LayoutDesc (0x21) ElementDesc trees into FACEUIElement nodes.
 * Parsing of language DAT is not wired yet — LoadLayout reads pre-resolved JSON from Docs/UI/Resolved.
 */
UCLASS()
class ACECLIENT_API UACEUILayoutResolver : public UObject
{
	GENERATED_BODY()

public:
	void Initialize(UACEDatSubsystem* InDat, UACEUIElementManager* InManager);
	void Shutdown();

	/** True when language DAT + LayoutDesc unpack are available. */
	bool IsReady() const { return bReady; }

	/**
	 * Load resolved layout JSON (Docs/UI/Resolved) and attach root elements under the manager.
	 */
	bool LoadLayout(uint32 LayoutId);
	/** Read a detached retail template for dynamically created list rows. */
	static TSharedPtr<FACEUIElement> LoadTemplate(uint32 LayoutId, uint32 ElementId);

private:
	UPROPERTY()
	TObjectPtr<UACEDatSubsystem> Dat;

	UPROPERTY()
	TObjectPtr<UACEUIElementManager> Manager;

	bool bReady = false;
};
