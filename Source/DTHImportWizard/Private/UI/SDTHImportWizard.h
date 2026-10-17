#pragma once

#include "Steps/DTHImportReport.h"
#include "Steps/DTHMaterialPlan.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class FDTHExport;
class IDetailsView;
class ITableRow;
class STableViewBase;
class SVerticalBox;
class SWidgetSwitcher;

/** The wizard's tab content: export picker, step bar, the page for each step and the action bar. */
class SDTHImportWizard : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDTHImportWizard) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** The open wizard, if its tab exists (for scripting). */
	static TSharedPtr<SDTHImportWizard> Get() { return Instance.Pin(); }
	/** Loads an export as the Browse button does. */
	void OpenExport(const FString& Folder) { LoadExport(Folder); }
	/** Shows a page by name: Settings, Meshes, Materials, Grooms or Import. False for an unknown name. */
	bool ShowPage(const FString& Name);

private:
	enum class EPage : int32
	{
		ProjectSettings,
		MeshesAndRig,
		Materials,
		Grooms,
		Import,
		Count
	};

	/** A mesh row on the Meshes page. */
	struct FMeshRow
	{
		int32 MeshIndex = INDEX_NONE;
		bool bImport = false;
	};

	using FMessage = FDTHImportMessage;

	// Layout
	TSharedRef<SWidget> MakeExportBar();
	TSharedRef<SWidget> MakeStepBar();
	TSharedRef<SWidget> MakeSettingsPage();
	TSharedRef<SWidget> MakeMeshesPage();
	TSharedRef<SWidget> MakePlaceholderPage(const FText& Title, const FText& Body);
	TSharedRef<SWidget> MakeMaterialsPage();
	/** Rebuilds the Materials page's slot list from MaterialPlans (on the next tick, safe from inside its own widgets). */
	void RefreshMaterialsPage();
	TSharedRef<SWidget> MakeMaterialSlot(int32 PlanIndex);
	TSharedRef<SWidget> MakeImportPage();
	TSharedRef<SWidget> MakeActionBar();
	TSharedRef<SWidget> MakeSection(const FText& Title, const TSharedRef<SWidget>& Content);

	TSharedRef<ITableRow> MakeMeshRow(TSharedPtr<FMeshRow> Row, const TSharedRef<STableViewBase>& Owner);
	TSharedRef<ITableRow> MakeMessageRow(TSharedPtr<FMessage> Message, const TSharedRef<STableViewBase>& Owner);

	// Actions
	FReply OnBrowseExport();
	void LoadExport(const FString& Folder);
	FReply OnImport();
	bool CanImport() const;
	FReply OnCheckMerges();
	void ResetMaterialPlan();
	FReply OnBrowseTexture(int32 PlanIndex, int32 RowIndex);
	void AddMessage(FMessage::ESeverity Severity, const FText& Text);

	FText GetExportFolderText() const;
	FText GetStatusText() const;
	FText GetExportSummary() const;

	EPage CurrentPage = EPage::ProjectSettings;
	TSharedPtr<SWidgetSwitcher> Pages;
	TSharedPtr<IDetailsView> SettingsView;

	TSharedPtr<FDTHExport> Export;
	FString ExportFolder;
	FString ExportError;
	FString DestinationFolder;
	FString SkeletonPath;
	bool bReimportExistingMeshes = false;

	/** The last material merge check, as shown on the Meshes page (empty until run). */
	FText MergeCheckText;

	/** The material plan the Materials page edits and the import uses (every manifest slot). */
	TArray<FDTHMaterialPlan> MaterialPlans;
	TSharedPtr<SVerticalBox> MaterialsBox;
	TArray<TSharedPtr<FString>> ParentOptions;
	bool bMaterialsRefreshPending = false;

	static TWeakPtr<SDTHImportWizard> Instance;

	TArray<TSharedPtr<FMeshRow>> MeshRows;
	TSharedPtr<SListView<TSharedPtr<FMeshRow>>> MeshList;

	TArray<TSharedPtr<FMessage>> Messages;
	TSharedPtr<SListView<TSharedPtr<FMessage>>> MessageList;
};
