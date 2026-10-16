#pragma once

#include "Steps/DTHImportReport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class FDTHExport;
class IDetailsView;
class ITableRow;
class STableViewBase;
class SWidgetSwitcher;

/** The wizard's tab content: export picker, step bar, the page for each step and the action bar. */
class SDTHImportWizard : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDTHImportWizard) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

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

	TArray<TSharedPtr<FMeshRow>> MeshRows;
	TSharedPtr<SListView<TSharedPtr<FMeshRow>>> MeshList;

	TArray<TSharedPtr<FMessage>> Messages;
	TSharedPtr<SListView<TSharedPtr<FMessage>>> MessageList;
};
