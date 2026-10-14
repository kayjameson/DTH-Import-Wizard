#pragma once

#include "Modules/ModuleInterface.h"

/** Registers the wizard's dockable tab and its entry in the level editor's Tools menu. */
class FDTHImportWizardModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	static const FName WizardTabName;

private:
	void RegisterMenus();
	TSharedRef<class SDockTab> SpawnWizardTab(const class FSpawnTabArgs& Args);
};
