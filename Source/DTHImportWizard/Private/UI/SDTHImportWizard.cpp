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
#include "Widgets/Input/SComboBox.h"
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
	MergeCheckText = FText::GetEmpty();
	ResetMaterialPlan();
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
	Request.MaterialPlans = MaterialPlans;
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
