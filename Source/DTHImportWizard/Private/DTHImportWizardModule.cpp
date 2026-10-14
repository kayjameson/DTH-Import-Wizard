#include "DTHImportWizardModule.h"

#include "DTHImportLog.h"

#include "Framework/Docking/TabManager.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "UI/SDTHImportWizard.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "DTHImportWizard"

DEFINE_LOG_CATEGORY(LogDTHImport);

const FName FDTHImportWizardModule::WizardTabName(TEXT("DTHImportWizard"));

void FDTHImportWizardModule::StartupModule()
{
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(WizardTabName,
		FOnSpawnTab::CreateRaw(this, &FDTHImportWizardModule::SpawnWizardTab))
		.SetDisplayName(LOCTEXT("TabTitle", "DTH Import Wizard"))
		.SetTooltipText(LOCTEXT("TabTooltip", "Import a DazToHue character export in one pass."))
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "ClassIcon.SkeletalMesh"))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory());

	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FDTHImportWizardModule::RegisterMenus));
}

void FDTHImportWizardModule::ShutdownModule()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
	if (FSlateApplication::IsInitialized())
	{
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(WizardTabName);
	}
}

void FDTHImportWizardModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);
	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
	FToolMenuSection& Section = Menu->FindOrAddSection("DTH", LOCTEXT("MenuSection", "DazToHue"));
	Section.AddMenuEntry(
		"OpenDTHImportWizard",
		LOCTEXT("MenuEntry", "DTH Import Wizard"),
		LOCTEXT("MenuEntryTooltip", "Import a DazToHue character export in one pass."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "ClassIcon.SkeletalMesh"),
		FUIAction(FExecuteAction::CreateLambda([]()
		{
			FGlobalTabmanager::Get()->TryInvokeTab(FDTHImportWizardModule::WizardTabName);
		})));
}

TSharedRef<SDockTab> FDTHImportWizardModule::SpawnWizardTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SDTHImportWizard)
		];
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FDTHImportWizardModule, DTHImportWizard)
