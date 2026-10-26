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
#include "Steps/DTHWardrobeBuild.h"
#include "Steps/DTHMergeCheck.h"
#include "Steps/DTHSkeleton.h"
#include "Styling/AppStyle.h"
#include "Styling/StyleColors.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSegmentedControl.h"
#include "Widgets/Input/SSpinBox.h"
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

TWeakPtr<SDTHImportWizard> SDTHImportWizard::Instance;

bool SDTHImportWizard::ShowPage(const FString& Name)
{
	static const TPair<const TCHAR*, EPage> PageNames[] = {
		{ TEXT("Settings"), EPage::ProjectSettings }, { TEXT("Meshes"), EPage::MeshesAndRig }, { TEXT("Materials"), EPage::Materials },
		{ TEXT("Grooms"), EPage::Grooms }, { TEXT("Import"), EPage::Import } };
	for (const auto& Page : PageNames)
	{
		if (Name.Equals(Page.Key, ESearchCase::IgnoreCase))
		{
			CurrentPage = Page.Value;
			return true;
		}
	}
	return false;
}

void SDTHImportWizard::Construct(const FArguments& InArgs)
{
	Instance = SharedThis(this);
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
				+ SWidgetSwitcher::Slot()[MakeMaterialsPage()]
				+ SWidgetSwitcher::Slot()[MakeGroomsPage()]
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
						.ToolTipText(LOCTEXT("ReimportTip", "Replace meshes and grooms that already exist in the destination. Otherwise they are reused and only the setup steps run."))
						[
							SNew(STextBlock).Text(LOCTEXT("Reimport", "Reimport existing meshes and grooms"))
						]
					])
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
			[
				MakeSection(LOCTEXT("WardrobeSection", "Wardrobe"),
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
						[
							SNew(STextBlock).Text(LOCTEXT("OutfitLabel", "Outfit"))
						]
						+ SHorizontalBox::Slot().FillWidth(1.f)
						[
							SNew(SEditableTextBox)
							.Text_Lambda([this]() { return FText::FromString(OutfitName); })
							.OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type) { OutfitName = Text.ToString().TrimStartAndEnd(); })
							.ToolTipText(LOCTEXT("OutfitTip", "The outfit this export's garments (and head hair) become in the character's wardrobe. Key it in Sequencer as the character's Wardrobe State."))
						]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12.f, 0.f, 0.f, 0.f)
						[
							SNew(SCheckBox)
							.IsChecked_Lambda([this]() { return bAddToCharacter ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
							.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { bAddToCharacter = State == ECheckBoxState::Checked; })
							.ToolTipText(LOCTEXT("AddOnTip", "This export is another outfit or hair for the character already in the Content Folder, made from its own Houdini file. Its body is checked against the character's and not imported; its garments and grooms go on the character's body and into its wardrobe. Retargeting, pose assets and the control rig are left as they are."))
							[
								SNew(STextBlock).Text(LOCTEXT("AddOn", "Add to an existing character"))
							]
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
					[
						SNew(STextBlock)
						.AutoWrapText(true)
						.ColorAndOpacity(FSlateColor::UseSubduedForeground())
						.Text(LOCTEXT("WardrobeHelp", "The import makes BP_<Character>: the body with every garment and groom of its wardrobe, one outfit shown at a time. Import further outfit or hair exports into the same folder with \"Add to an existing character\" ticked; they become more outfits on the same blueprint, and their grooms are bound to the same body."))
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

TSharedRef<SWidget> SDTHImportWizard::MakeMaterialsPage()
{
	for (const FString& Parent : DTHImport::MaterialParents())
	{
		ParentOptions.Add(MakeShared<FString>(Parent));
	}
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("Brushes.Recessed"))
		.Padding(8.f)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
			[
				MakeSection(LOCTEXT("MaterialsSection", "Materials & Textures"),
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock)
						.AutoWrapText(true)
						.Text(LOCTEXT("MaterialsHelp", "One material instance per slot, under a per-character copy of its DazToHue parent. Defaults come from the export: the manifest's textures, matching maps found next to them (SSS, specular...), and clothing DTH types as Skin set to Cloth. Change a slot's parent, or browse for a texture, and the import uses what is shown here."))
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth()
						[
							SNew(SButton)
							.Text(LOCTEXT("ResetMaterials", "Reset from Export"))
							.ToolTipText(LOCTEXT("ResetMaterialsTip", "Throw away changes made here and rebuild every slot's material from the export."))
							.IsEnabled_Lambda([this]() { return Export.IsValid(); })
							.OnClicked_Lambda([this]() { ResetMaterialPlan(); return FReply::Handled(); })
						]
						+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(12.f, 0.f, 0.f, 0.f)
						[
							SNew(STextBlock)
							.Text_Lambda([this]()
							{
								int32 Problems = 0;
								for (const FDTHMaterialPlan& Plan : MaterialPlans)
								{
									for (const FDTHTextureRow& Row : Plan.Textures) Problems += Row.bProblem ? 1 : 0;
								}
								return Export.IsValid()
									? FText::Format(LOCTEXT("MaterialsSummary", "{0} slots, {1} textures missing"), MaterialPlans.Num(), Problems)
									: LOCTEXT("MaterialsNoExport", "Open an export to see its materials.");
							})
						]
					])
			]

			+ SVerticalBox::Slot().FillHeight(1.f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					SAssignNew(MaterialsBox, SVerticalBox)
				]
			]
		];
}

void SDTHImportWizard::ResetMaterialPlan()
{
	MaterialPlans.Reset();
	if (Export.IsValid())
	{
		TArray<const FDTHMeshEntry*> Meshes;
		for (const FDTHMeshEntry& Mesh : Export->Meshes)
		{
			if (Mesh.bInManifest) Meshes.Add(&Mesh);
		}
		MaterialPlans = DTHImport::BuildMaterialPlan(*Export, Meshes);
	}
	RefreshMaterialsPage();
}

void SDTHImportWizard::RefreshMaterialsPage()
{
	if (bMaterialsRefreshPending) return;
	bMaterialsRefreshPending = true;
	// Next tick: this is often called from inside one of the widgets about to be replaced.
	RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateLambda([this](double, float)
	{
		bMaterialsRefreshPending = false;
		if (MaterialsBox)
		{
			MaterialsBox->ClearChildren();
			FString LastMesh;
			for (int32 Index = 0; Index < MaterialPlans.Num(); ++Index)
			{
				if (MaterialPlans[Index].Mesh != LastMesh)
				{
					LastMesh = MaterialPlans[Index].Mesh;
					MaterialsBox->AddSlot().AutoHeight().Padding(0.f, Index == 0 ? 0.f : 10.f, 0.f, 4.f)
					[
						SNew(STextBlock).Font(FAppStyle::GetFontStyle("BoldFont")).Text(FText::FromString(LastMesh))
					];
				}
				MaterialsBox->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)[MakeMaterialSlot(Index)];
			}
		}
		return EActiveTimerReturnType::Stop;
	}));
}

TSharedRef<SWidget> SDTHImportWizard::MakeMaterialSlot(int32 PlanIndex)
{
	const FDTHMaterialPlan& Plan = MaterialPlans[PlanIndex];
	TSharedPtr<FString> Current;
	for (const TSharedPtr<FString>& Option : ParentOptions)
	{
		if (*Option == Plan.Parent) Current = Option;
	}

	TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
	for (int32 RowIndex = 0; RowIndex < Plan.Textures.Num(); ++RowIndex)
	{
		const FDTHTextureRow& Row = Plan.Textures[RowIndex];
		const FText File = Row.Source.IsEmpty()
			? (Row.bProblem ? FText::FromString(Row.Why) : LOCTEXT("NoTexture", "none"))
			: FText::FromString(FPaths::GetCleanFilename(Row.Source));
		const FText Tip = FText::FromString(Row.Source.IsEmpty() ? Row.Why
			: FString::Printf(TEXT("%s\n\nAsset: %s%s\nWhy: %s"), *Row.Source, *Row.AssetName, Row.bFlipGreen ? TEXT(" (green flipped: OpenGL normal map)") : TEXT(""), *Row.Why));
		Rows->AddSlot().AutoHeight().Padding(16.f, 1.f, 0.f, 1.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(230.f)
				[
					SNew(STextBlock).Text(FText::FromString(Row.Parameter))
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(File)
				.ToolTipText(Tip)
				.ColorAndOpacity(Row.bProblem ? FSlateColor(FStyleColors::Warning) : Row.Source.IsEmpty() ? FSlateColor::UseSubduedForeground() : FSlateColor::UseForeground())
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4.f, 0.f, 0.f, 0.f)
			[
				SNew(SButton)
				.Text(LOCTEXT("BrowseTexture", "Browse..."))
				.ToolTipText(LOCTEXT("BrowseTextureTip", "Choose the texture file for this parameter."))
				.OnClicked(this, &SDTHImportWizard::OnBrowseTexture, PlanIndex, RowIndex)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4.f, 0.f, 0.f, 0.f)
			[
				SNew(SButton)
				.Text(LOCTEXT("ClearTexture", "Clear"))
				.ToolTipText(LOCTEXT("ClearTextureTip", "Leave this parameter at the parent material's default."))
				.IsEnabled(!Row.Source.IsEmpty() || Row.bProblem)
				.OnClicked_Lambda([this, PlanIndex, RowIndex]()
				{
					DTHImport::SetSlotTexture(MaterialPlans, PlanIndex, RowIndex, FString(), *Export);
					RefreshMaterialsPage();
					return FReply::Handled();
				})
			]
		];
	}

	const bool bProblem = Plan.Textures.ContainsByPredicate([](const FDTHTextureRow& Row) { return Row.bProblem; });
	FString Short = Plan.Slot;
	if (Export.IsValid()) Short.RemoveFromStart(TEXT("MI_") + Export->Character);
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("Brushes.Panel"))
		.Padding(6.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 3.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 6.f, 0.f)
				[
					SNew(SImage)
					.Visibility(bProblem ? EVisibility::Visible : EVisibility::Collapsed)
					.Image(FAppStyle::GetBrush("Icons.WarningWithColor"))
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(STextBlock).Font(FAppStyle::GetFontStyle("BoldFont")).Text(FText::FromString(Short)).ToolTipText(FText::FromString(Plan.Slot))
				]
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(8.f, 0.f, 0.f, 0.f)
				[
					SNew(STextBlock)
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.Text(FText::Format(LOCTEXT("ManifestType", "manifest type: {0}"), FText::FromString(Plan.Type.IsEmpty() ? TEXT("-") : Plan.Type)))
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 6.f, 0.f)
				[
					SNew(STextBlock).Text(LOCTEXT("ParentLabel", "Parent"))
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SComboBox<TSharedPtr<FString>>)
					.OptionsSource(&ParentOptions)
					.InitiallySelectedItem(Current)
					.OnGenerateWidget_Lambda([](TSharedPtr<FString> Option) { return SNew(STextBlock).Text(FText::FromString(*Option)); })
					.OnSelectionChanged_Lambda([this, PlanIndex](TSharedPtr<FString> Option, ESelectInfo::Type)
					{
						if (Option && MaterialPlans.IsValidIndex(PlanIndex) && *Option != MaterialPlans[PlanIndex].Parent)
						{
							DTHImport::SetSlotParent(MaterialPlans, PlanIndex, *Option, *Export);
							RefreshMaterialsPage();
						}
					})
					[
						SNew(STextBlock).Text(FText::FromString(Plan.Parent))
					]
				]
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				Rows
			]
		];
}

FReply SDTHImportWizard::OnBrowseTexture(int32 PlanIndex, int32 RowIndex)
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform || !Export.IsValid() || !MaterialPlans.IsValidIndex(PlanIndex) || !MaterialPlans[PlanIndex].Textures.IsValidIndex(RowIndex))
	{
		return FReply::Handled();
	}
	const FDTHTextureRow& Row = MaterialPlans[PlanIndex].Textures[RowIndex];
	TArray<FString> Chosen;
	const void* ParentWindow = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(AsShared());
	const FString StartDir = Row.Source.IsEmpty() ? Export->Folder : FPaths::GetPath(Row.Source);
	if (DesktopPlatform->OpenFileDialog(ParentWindow, FText::Format(LOCTEXT("BrowseTextureTitle", "Texture for {0}"), FText::FromString(Row.Parameter)).ToString(),
		StartDir, FString(), TEXT("Images|*.png;*.jpg;*.jpeg;*.tif;*.tiff;*.tga;*.bmp;*.exr;*.psd|All files|*.*"), EFileDialogFlags::None, Chosen) && Chosen.Num() > 0)
	{
		DTHImport::SetSlotTexture(MaterialPlans, PlanIndex, RowIndex, Chosen[0], *Export);
		RefreshMaterialsPage();
	}
	return FReply::Handled();
}

TSharedRef<SWidget> SDTHImportWizard::MakeGroomsPage()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("Brushes.Recessed"))
		.Padding(8.f)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
			[
				MakeSection(LOCTEXT("GroomsSection", "Grooms"),
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock)
						.AutoWrapText(true)
						.Text(LOCTEXT("GroomsHelp", "Groom alembics from the DazToHue Groom Export node (ABC_<Groom Name>.abc in the export folder), set up as the Groom guide does: imported at default options, bound to the body mesh with the chosen slot as Matching Section, and given a copy of MI_DTH_Hair. Use 100 interpolation points for head hair and around 25 for brows and lashes. Adding the Groom components to your character blueprint is left to you."))
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
					[
						SNew(STextBlock)
						.Text_Lambda([this]()
						{
							if (!Export.IsValid())
							{
								return LOCTEXT("GroomsNoExport", "Open an export to see its grooms.");
							}
							if (GroomPlans.Num() == 0)
							{
								return FText::Format(LOCTEXT("GroomsNone", "No ABC_*.abc grooms in {0}."), FText::FromString(Export->Folder));
							}
							const int32 Ticked = GroomPlans.FilterByPredicate([](const FDTHGroomPlan& Plan) { return Plan.bImport; }).Num();
							return FText::Format(LOCTEXT("GroomsSummary", "{0} grooms, {1} ticked for import"), GroomPlans.Num(), Ticked);
						})
					])
			]

			+ SVerticalBox::Slot().FillHeight(1.f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					SAssignNew(GroomsBox, SVerticalBox)
				]
			]
		];
}

void SDTHImportWizard::RefreshGroomsPage()
{
	BodySlotOptions.Reset();
	if (const FDTHMeshEntry* Body = Export.IsValid() ? Export->FindBody() : nullptr)
	{
		for (const FDTHMaterialSlot& Slot : Body->Materials)
		{
			BodySlotOptions.Add(MakeShared<FString>(Slot.Slot));
		}
	}
	if (GroomsBox)
	{
		GroomsBox->ClearChildren();
		for (int32 Index = 0; Index < GroomPlans.Num(); ++Index)
		{
			GroomsBox->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)[MakeGroomRow(Index)];
		}
	}
}

TSharedRef<SWidget> SDTHImportWizard::MakeGroomRow(int32 PlanIndex)
{
	const FDTHGroomPlan& Plan = GroomPlans[PlanIndex];
	TSharedPtr<FString> Current;
	for (const TSharedPtr<FString>& Option : BodySlotOptions)
	{
		if (*Option == Plan.MatchingSlot) Current = Option;
	}
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("Brushes.Panel"))
		.Padding(6.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
			[
				SNew(SCheckBox)
				.ToolTipText(LOCTEXT("GroomImportTip", "Import this groom"))
				.IsChecked_Lambda([this, PlanIndex]() { return GroomPlans.IsValidIndex(PlanIndex) && GroomPlans[PlanIndex].bImport ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this, PlanIndex](ECheckBoxState State)
				{
					if (GroomPlans.IsValidIndex(PlanIndex)) GroomPlans[PlanIndex].bImport = State == ECheckBoxState::Checked;
				})
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Font(FAppStyle::GetFontStyle("BoldFont")).Text(FText::FromString(Plan.Name))
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock)
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.Text(FText::FromString(FPaths::GetCleanFilename(Plan.File)))
					.ToolTipText(FText::FromString(Plan.File))
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12.f, 0.f, 6.f, 0.f)
			[
				SNew(STextBlock).Text(LOCTEXT("GroomPoints", "Interpolation Points"))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(60.f)
				[
					SNew(SSpinBox<int32>)
					.MinValue(1)
					.MaxValue(100)
					.ToolTipText(LOCTEXT("GroomPointsTip", "The binding's Num Interpolation Points: 100 for head hair, around 25 for brows and lashes (Groom guide)."))
					.Value_Lambda([this, PlanIndex]() { return GroomPlans.IsValidIndex(PlanIndex) ? GroomPlans[PlanIndex].InterpolationPoints : 100; })
					.OnValueChanged_Lambda([this, PlanIndex](int32 Value)
					{
						if (GroomPlans.IsValidIndex(PlanIndex)) GroomPlans[PlanIndex].InterpolationPoints = Value;
					})
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12.f, 0.f, 6.f, 0.f)
			[
				SNew(STextBlock).Text(LOCTEXT("GroomSlot", "Matching Section"))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SComboBox<TSharedPtr<FString>>)
				.ToolTipText(LOCTEXT("GroomSlotTip", "The body material slot the groom grows from; its element number is the binding's Matching Section (the head's, in the Groom guide)."))
				.OptionsSource(&BodySlotOptions)
				.InitiallySelectedItem(Current)
				.OnGenerateWidget_Lambda([](TSharedPtr<FString> Option) { return SNew(STextBlock).Text(FText::FromString(*Option)); })
				.OnSelectionChanged_Lambda([this, PlanIndex](TSharedPtr<FString> Option, ESelectInfo::Type)
				{
					if (Option && GroomPlans.IsValidIndex(PlanIndex)) GroomPlans[PlanIndex].MatchingSlot = *Option;
				})
				[
					SNew(STextBlock)
					.Text_Lambda([this, PlanIndex]()
					{
						const FString Slot = GroomPlans.IsValidIndex(PlanIndex) ? GroomPlans[PlanIndex].MatchingSlot : FString();
						return Slot.IsEmpty() ? LOCTEXT("GroomNoSlot", "(choose a slot)") : FText::FromString(Slot);
					})
				]
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
		TArray<int32> Listed;
		for (const TSharedPtr<FMeshRow>& Row : MeshRows)
		{
			if (Row->bImport) Listed.Add(Row->MeshIndex);
		}
		OutfitName = DTHImport::DefaultOutfitName(*Export, Listed);
		bAddToCharacter = DTHImport::FindWardrobe(DestinationFolder) != nullptr;
		CurrentPage = EPage::MeshesAndRig;
	}
	if (MeshList)
	{
		MeshList->RequestListRefresh();
	}
	MergeCheckText = FText::GetEmpty();
	ResetMaterialPlan();
	GroomPlans = Export.IsValid() ? DTHImport::BuildGroomPlans(*Export) : TArray<FDTHGroomPlan>();
	RefreshGroomsPage();
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
	Request.bAddToCharacter = bAddToCharacter;
	Request.OutfitName = OutfitName;
	Request.MaterialPlans = MaterialPlans;
	Request.GroomPlans = GroomPlans;
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
	if (bAddToCharacter)
	{
		return FText::Format(LOCTEXT("StatusAddOn", "{0}: garments and grooms go to the character in {1} as outfit {2} (its body is checked, not imported)"),
			FText::FromString(Export->Character), FText::FromString(DestinationFolder), FText::FromString(OutfitName));
	}
	return FText::Format(LOCTEXT("StatusReady", "{0}: {1} skeletal meshes ticked for import into {2}, outfit {3}"),
		FText::FromString(Export->Character), Count, FText::FromString(DestinationFolder), FText::FromString(OutfitName));
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
