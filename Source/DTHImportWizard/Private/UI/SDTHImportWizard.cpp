#include "UI/SDTHImportWizard.h"

#include "DesktopPlatformModule.h"
#include "DTHImportLog.h"
#include "Engine/SkeletalMesh.h"
#include "Export/DTHExport.h"
#include "Framework/Application/SlateApplication.h"
#include "IDesktopPlatform.h"
#include "IDetailsView.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "Settings/DTHImportSettings.h"
#include "Steps/DTHImportRun.h"
#include "Steps/DTHMergeCheck.h"
#include "Steps/DTHSkeleton.h"
#include "Styling/AppStyle.h"
#include "Styling/StyleColors.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSegmentedControl.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "SDTHImportWizard"

namespace
{
	const FName ColumnImport(TEXT("Import"));
	const FName ColumnFile(TEXT("File"));
	const FName ColumnName(TEXT("Name"));
	const FName ColumnSlots(TEXT("Slots"));
}

void SDTHImportWizard::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("Brushes.Panel"))
		.Padding(0.f)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot().AutoHeight()
			[
				MakeExportBar()
			]

			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SSeparator).Thickness(1.f)
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(8.f, 8.f, 8.f, 4.f)
			[
				MakeStepBar()
			]

			+ SVerticalBox::Slot().FillHeight(1.f).Padding(8.f, 4.f)
			[
				SAssignNew(Pages, SWidgetSwitcher)
				.WidgetIndex_Lambda([this]() { return static_cast<int32>(CurrentPage); })

				+ SWidgetSwitcher::Slot()[MakeSettingsPage()]
				+ SWidgetSwitcher::Slot()[MakeMeshesPage()]
				+ SWidgetSwitcher::Slot()
				[
					MakePlaceholderPage(LOCTEXT("MaterialsTitle", "Materials & Textures"),
						LOCTEXT("MaterialsBody", "One material instance per slot, parented to the DazToHue materials, with the export's textures."))
				]
				+ SWidgetSwitcher::Slot()
				[
					MakePlaceholderPage(LOCTEXT("GroomsTitle", "Grooms"),
						LOCTEXT("GroomsBody", "Groom alembics from the DazToHue Groom Export node: import, binding, hair material and occlusion."))
				]
				+ SWidgetSwitcher::Slot()[MakeImportPage()]
			]

			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SSeparator).Thickness(1.f)
			]

			+ SVerticalBox::Slot().AutoHeight()
			[
				MakeActionBar()
			]
		]
	];
}

TSharedRef<SWidget> SDTHImportWizard::MakeExportBar()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("Brushes.Header"))
		.Padding(FMargin(8.f, 6.f))
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
			[
				SNew(STextBlock).Text(LOCTEXT("ExportLabel", "Export Folder"))
			]

			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(SEditableTextBox)
				.Text(this, &SDTHImportWizard::GetExportFolderText)
				.HintText(LOCTEXT("ExportHint", "Folder holding the DazToHue export (DTH_<Character>.dth)"))
				.OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type CommitType)
				{
					if (CommitType == ETextCommit::OnEnter && !Text.IsEmpty())
					{
						LoadExport(Text.ToString());
					}
				})
			]

			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.f, 0.f, 0.f, 0.f)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ToolTipText(LOCTEXT("BrowseTip", "Browse for an export folder"))
				.OnClicked(this, &SDTHImportWizard::OnBrowseExport)
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush("Icons.FolderOpen"))
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
			]
		];
}

TSharedRef<SWidget> SDTHImportWizard::MakeStepBar()
{
	TSharedRef<SSegmentedControl<int32>> Bar = SNew(SSegmentedControl<int32>)
		.Value_Lambda([this]() { return static_cast<int32>(CurrentPage); })
		.OnValueChanged_Lambda([this](int32 NewValue) { CurrentPage = static_cast<EPage>(NewValue); });

	const FText Labels[] = {
		LOCTEXT("StepSettings", "Project Settings"),
		LOCTEXT("StepMeshes", "Meshes & Rig"),
		LOCTEXT("StepMaterials", "Materials"),
		LOCTEXT("StepGrooms", "Grooms"),
		LOCTEXT("StepImport", "Import"),
	};
	static_assert(UE_ARRAY_COUNT(Labels) == static_cast<int32>(EPage::Count), "One label per page");

	for (int32 Index = 0; Index < static_cast<int32>(EPage::Count); ++Index)
	{
		Bar->AddSlot(Index).Text(Labels[Index]);
	}
	return Bar;
}

TSharedRef<SWidget> SDTHImportWizard::MakeSection(const FText& Title, const TSharedRef<SWidget>& Content)
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("Brushes.Header"))
			.Padding(FMargin(8.f, 4.f))
			[
				SNew(STextBlock)
				.Font(FAppStyle::GetFontStyle("NormalFontBold"))
				.Text(Title)
			]
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("Brushes.Recessed"))
			.Padding(FMargin(8.f, 6.f))
			[
				Content
			]
		];
}

TSharedRef<SWidget> SDTHImportWizard::MakeSettingsPage()
{
	FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs Args;
	Args.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	Args.bAllowSearch = false;
	SettingsView = PropertyEditor.CreateDetailView(Args);
	SettingsView->SetObject(GetMutableDefault<UDTHImportSettings>());
	SettingsView->OnFinishedChangingProperties().AddLambda([this](const FPropertyChangedEvent&)
	{
		GetMutableDefault<UDTHImportSettings>()->SaveConfig();
		if (!ExportFolder.IsEmpty())
		{
			LoadExport(ExportFolder);
		}
	});

	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("Brushes.Recessed"))
		.Padding(4.f)
		[
			SettingsView.ToSharedRef()
		];
}

TSharedRef<SWidget> SDTHImportWizard::MakeMeshesPage()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("Brushes.Recessed"))
		.Padding(8.f)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
			[
				MakeSection(LOCTEXT("ExportSection", "Export"),
					SNew(STextBlock)
					.AutoWrapText(true)
					.Text(this, &SDTHImportWizard::GetExportSummary))
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
			[
				MakeSection(LOCTEXT("DestinationSection", "Destination"),
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
					[
						SNew(STextBlock).Text(LOCTEXT("DestinationLabel", "Content Folder"))
					]
					+ SHorizontalBox::Slot().FillWidth(1.f)
					[
						SNew(SEditableTextBox)
						.Text_Lambda([this]() { return FText::FromString(DestinationFolder); })
						.OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type) { DestinationFolder = Text.ToString(); })
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12.f, 0.f, 0.f, 0.f)
					[
						SNew(SCheckBox)
						.IsChecked_Lambda([this]() { return bReimportExistingMeshes ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
						.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { bReimportExistingMeshes = State == ECheckBoxState::Checked; })
						.ToolTipText(LOCTEXT("ReimportTip", "Replace meshes that already exist in the destination. Otherwise they are reused and only the setup steps run."))
						[
							SNew(STextBlock).Text(LOCTEXT("Reimport", "Reimport existing meshes"))
						]
					])
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
			[
				MakeSection(LOCTEXT("MergeSection", "Material Merges"),
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth()
						[
							SNew(SButton)
							.Text(LOCTEXT("CheckMerges", "Check Material Merges"))
							.ToolTipText(LOCTEXT("CheckMergesTip", "Compares each exported material slot with the Daz surfaces it was made from (Daz's own export in houdini/daz-export) and lists slots that merge surfaces with different textures. Baked slots are skipped. Also runs at the start of every import."))
							.IsEnabled_Lambda([this]() { return Export.IsValid(); })
							.OnClicked(this, &SDTHImportWizard::OnCheckMerges)
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
					[
						SNew(STextBlock)
						.AutoWrapText(true)
						.Visibility_Lambda([this]() { return MergeCheckText.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
						.Text_Lambda([this]() { return MergeCheckText; })
					])
			]

			+ SVerticalBox::Slot().FillHeight(1.f)
			[
				SAssignNew(MeshList, SListView<TSharedPtr<FMeshRow>>)
				.ListItemsSource(&MeshRows)
				.SelectionMode(ESelectionMode::None)
				.OnGenerateRow(this, &SDTHImportWizard::MakeMeshRow)
				.HeaderRow
				(
					SNew(SHeaderRow)
					+ SHeaderRow::Column(ColumnImport).DefaultLabel(LOCTEXT("ColImport", "Import")).FixedWidth(56.f).HAlignCell(HAlign_Center)
					+ SHeaderRow::Column(ColumnFile).DefaultLabel(LOCTEXT("ColFile", "Skeletal Mesh")).FillWidth(0.45f)
					+ SHeaderRow::Column(ColumnName).DefaultLabel(LOCTEXT("ColName", "Manifest Name")).FillWidth(0.35f)
					+ SHeaderRow::Column(ColumnSlots).DefaultLabel(LOCTEXT("ColSlots", "Material Slots")).FillWidth(0.2f)
				)
			]
		];
}

TSharedRef<ITableRow> SDTHImportWizard::MakeMeshRow(TSharedPtr<FMeshRow> Row, const TSharedRef<STableViewBase>& Owner)
{
	class SMeshRow : public SMultiColumnTableRow<TSharedPtr<FMeshRow>>
	{
	public:
		SLATE_BEGIN_ARGS(SMeshRow) {}
		SLATE_END_ARGS()

		void Construct(const FArguments&, const TSharedRef<STableViewBase>& InOwner, TSharedPtr<FMeshRow> InRow, TSharedPtr<FDTHExport> InExport)
		{
			Row = InRow;
			Export = InExport;
			SMultiColumnTableRow::Construct(FSuperRowType::FArguments().Padding(FMargin(0.f, 2.f)), InOwner);
		}

		virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& Column) override
		{
			const FDTHMeshEntry& Mesh = Export->Meshes[Row->MeshIndex];
			if (Column == ColumnImport)
			{
				return SNew(SCheckBox)
					.IsChecked_Lambda([this]() { return Row->bImport ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
					.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { Row->bImport = State == ECheckBoxState::Checked; });
			}
			FText Text;
			if (Column == ColumnFile)
			{
				Text = FText::FromString(FPaths::GetBaseFilename(Mesh.File));
			}
			else if (Column == ColumnName)
			{
				Text = Mesh.bInManifest
					? FText::FromString(Mesh.IsBody() ? Mesh.Name + TEXT("  (body)") : Mesh.Name)
					: LOCTEXT("NotInManifest", "not in manifest");
			}
			else if (Column == ColumnSlots)
			{
				Text = FText::AsNumber(Mesh.Materials.Num());
			}
			return SNew(SBox).VAlign(VAlign_Center).Padding(FMargin(4.f, 0.f))
				[
					SNew(STextBlock)
					.Text(Text)
					.ColorAndOpacity(Mesh.bInManifest ? FSlateColor::UseForeground() : FSlateColor::UseSubduedForeground())
				];
		}

	private:
		TSharedPtr<FMeshRow> Row;
		TSharedPtr<FDTHExport> Export;
	};

	return SNew(SMeshRow, Owner, Row, Export);
}

TSharedRef<SWidget> SDTHImportWizard::MakePlaceholderPage(const FText& Title, const FText& Body)
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("Brushes.Recessed"))
		.Padding(12.f)
		[
			SNew(SScrollBox)

			+ SScrollBox::Slot().Padding(0.f, 0.f, 0.f, 8.f)
			[
				SNew(STextBlock)
				.Font(FAppStyle::GetFontStyle("HeadingExtraSmall"))
				.Text(Title)
			]

			+ SScrollBox::Slot()
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.ColorAndOpacity(FStyleColors::Foreground)
				.Text(Body)
			]
		];
}

TSharedRef<SWidget> SDTHImportWizard::MakeImportPage()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("Brushes.Recessed"))
		.Padding(4.f)
		[
			SAssignNew(MessageList, SListView<TSharedPtr<FMessage>>)
			.ListItemsSource(&Messages)
			.SelectionMode(ESelectionMode::Multi)
			.OnGenerateRow(this, &SDTHImportWizard::MakeMessageRow)
		];
}

TSharedRef<ITableRow> SDTHImportWizard::MakeMessageRow(TSharedPtr<FMessage> Message, const TSharedRef<STableViewBase>& Owner)
{
	const TCHAR* IconName = TEXT("Icons.Info");
	if (Message->Severity == FMessage::ESeverity::Warning)
	{
		IconName = TEXT("Icons.WarningWithColor");
	}
	else if (Message->Severity == FMessage::ESeverity::Error)
	{
		IconName = TEXT("Icons.ErrorWithColor");
	}
	return SNew(STableRow<TSharedPtr<FMessage>>, Owner)
		.Padding(FMargin(4.f, 2.f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(0.f, 1.f, 6.f, 0.f)
			[
				SNew(SImage).Image(FAppStyle::GetBrush(IconName))
			]
			+ SHorizontalBox::Slot().FillWidth(1.f)
			[
				SNew(STextBlock).AutoWrapText(true).Text(Message->Text)
			]
		];
}

TSharedRef<SWidget> SDTHImportWizard::MakeActionBar()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("Brushes.Header"))
		.Padding(FMargin(8.f, 6.f))
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.ColorAndOpacity(FStyleColors::Foreground)
				.Text(this, &SDTHImportWizard::GetStatusText)
			]

			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "PrimaryButton")
				.IsEnabled(this, &SDTHImportWizard::CanImport)
				.OnClicked(this, &SDTHImportWizard::OnImport)
				.ToolTipText(LOCTEXT("ImportTip", "Import the ticked meshes and set up the character"))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 4.f, 0.f)
					[
						SNew(SImage)
						.Image(FAppStyle::GetBrush("Icons.Import"))
						.ColorAndOpacity(FSlateColor::UseForeground())
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.TextStyle(FAppStyle::Get(), "SmallButtonText")
						.Text(LOCTEXT("ImportButton", "Import"))
					]
				]
			]
		];
}

FReply SDTHImportWizard::OnBrowseExport()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform)
	{
		return FReply::Handled();
	}
	FString Chosen;
	const void* ParentWindow = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(AsShared());
	if (DesktopPlatform->OpenDirectoryDialog(ParentWindow, LOCTEXT("BrowseTitle", "DazToHue export folder").ToString(), ExportFolder, Chosen))
	{
		LoadExport(Chosen);
	}
	return FReply::Handled();
}

void SDTHImportWizard::LoadExport(const FString& Folder)
{
	ExportFolder = Folder;
	ExportError.Reset();
	SkeletonPath.Reset();
	MeshRows.Reset();
	Export = FDTHExport::Load(Folder, ExportError);
	if (Export)
	{
		ExportFolder = Export->Folder;
		FString CharacterRoot = GetDefault<UDTHImportSettings>()->CharacterRoot.Path;
		CharacterRoot.RemoveFromEnd(TEXT("/"));
		DestinationFolder = CharacterRoot / Export->Character;

		FString SkeletonError;
		SkeletonPath = DTHImport::SkeletonPathFor(*Export, SkeletonError);
		if (SkeletonPath.IsEmpty())
		{
			ExportError = SkeletonError;
		}
		for (int32 Index = 0; Index < Export->Meshes.Num(); ++Index)
		{
			TSharedPtr<FMeshRow> Row = MakeShared<FMeshRow>();
			Row->MeshIndex = Index;
			Row->bImport = Export->Meshes[Index].bInManifest;
			MeshRows.Add(Row);
		}
		CurrentPage = EPage::MeshesAndRig;
	}
	if (MeshList)
	{
		MeshList->RequestListRefresh();
	}
}

bool SDTHImportWizard::CanImport() const
{
	return Export.IsValid() && ExportError.IsEmpty() && !DestinationFolder.IsEmpty()
		&& MeshRows.ContainsByPredicate([](const TSharedPtr<FMeshRow>& Row) { return Row->bImport; });
}

FReply SDTHImportWizard::OnCheckMerges()
{
	if (!Export.IsValid())
	{
		return FReply::Handled();
	}
	TArray<const FDTHMeshEntry*> Meshes;
	for (const TSharedPtr<FMeshRow>& Row : MeshRows)
	{
		if (Row->bImport) Meshes.Add(&Export->Meshes[Row->MeshIndex]);
	}
	FScopedSlowTask Task(1.f, LOCTEXT("CheckingMerges", "Comparing material slots with Daz's export..."));
	Task.MakeDialog();
	Task.EnterProgressFrame();
	FDTHImportReport Report;
	DTHImport::ReportMaterialMerges(DTHImport::CheckMaterialMerges(*Export, Meshes), Report);
	TArray<FString> Lines;
	for (const FDTHImportMessage& Message : Report.Messages)
	{
		Lines.Add((Message.Severity == FDTHImportMessage::ESeverity::Warning ? TEXT("! ") : TEXT("")) + Message.Text.ToString());
	}
	MergeCheckText = FText::FromString(FString::Join(Lines, TEXT("\n\n")));
	return FReply::Handled();
}

FReply SDTHImportWizard::OnImport()
{
	FDTHImportRequest Request;
	Request.Export = Export;
	Request.DestinationFolder = DestinationFolder;
	Request.bReimportExistingMeshes = bReimportExistingMeshes;
	for (const TSharedPtr<FMeshRow>& Row : MeshRows)
	{
		if (Row->bImport)
		{
			Request.MeshIndices.Add(Row->MeshIndex);
		}
	}
	CurrentPage = EPage::Import;
	const FDTHImportReport Report = DTHImport::RunImport(Request);
	for (const FDTHImportMessage& Message : Report.Messages)
	{
		AddMessage(Message.Severity, Message.Text);
	}
	for (const FDTHManualStep& Step : Report.ManualSteps)
	{
		AddMessage(FMessage::ESeverity::Warning, FText::Format(LOCTEXT("ManualStep", "Manual step: {0}"), Step.Title));
		for (const FText& Line : Step.Instructions)
		{
			AddMessage(FMessage::ESeverity::Info, Line);
		}
	}
	return FReply::Handled();
}

void SDTHImportWizard::AddMessage(FMessage::ESeverity Severity, const FText& Text)
{
	TSharedPtr<FMessage> Message = MakeShared<FMessage>();
	Message->Severity = Severity;
	Message->Text = Text;
	Messages.Add(Message);
	if (MessageList)
	{
		MessageList->RequestListRefresh();
		MessageList->RequestScrollIntoView(Message);
	}
}

FText SDTHImportWizard::GetExportFolderText() const
{
	return FText::FromString(ExportFolder);
}

FText SDTHImportWizard::GetStatusText() const
{
	if (!ExportError.IsEmpty())
	{
		return FText::FromString(ExportError);
	}
	if (!Export)
	{
		return LOCTEXT("StatusNoExport", "Pick a DazToHue export folder to begin.");
	}
	const int32 Count = MeshRows.FilterByPredicate([](const TSharedPtr<FMeshRow>& Row) { return Row->bImport; }).Num();
	return FText::Format(LOCTEXT("StatusReady", "{0}: {1} skeletal meshes ticked for import into {2}"),
		FText::FromString(Export->Character), Count, FText::FromString(DestinationFolder));
}

FText SDTHImportWizard::GetExportSummary() const
{
	if (!Export)
	{
		return ExportError.IsEmpty() ? LOCTEXT("SummaryNone", "No export loaded.") : FText::FromString(ExportError);
	}
	return FText::Format(LOCTEXT("SummaryFormat", "{0}  (DazToHue {1})\nFigure: {2}    Target skeleton: {3}    Skinning: {4}\nImports onto: {5}"),
		FText::FromString(Export->Character), FText::FromString(Export->DthVersion), FText::FromString(Export->SourceSkeleton),
		FText::FromString(Export->TargetSkeleton), FText::FromString(Export->SkinningMethod),
		FText::FromString(SkeletonPath.IsEmpty() ? ExportError : SkeletonPath));
}

#undef LOCTEXT_NAMESPACE
