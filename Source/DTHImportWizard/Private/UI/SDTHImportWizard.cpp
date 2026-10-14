#include "UI/SDTHImportWizard.h"

#include "Styling/AppStyle.h"
#include "Styling/StyleColors.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSegmentedControl.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SDTHImportWizard"

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

				+ SWidgetSwitcher::Slot()
				[
					MakePage(LOCTEXT("SettingsTitle", "Project Settings"),
						LOCTEXT("SettingsBody", "Where the DazToHue content lives in this project, where new characters go, and the skeleton for UE5 Mannequin exports. Importing stays locked until these validate."))
				]
				+ SWidgetSwitcher::Slot()
				[
					MakePage(LOCTEXT("MeshesTitle", "Meshes, Skeleton & Rig"),
						LOCTEXT("MeshesBody", "The skeletal meshes in the export, the skeleton they import onto, the post-process setup and the control rig."))
				]
				+ SWidgetSwitcher::Slot()
				[
					MakePage(LOCTEXT("MaterialsTitle", "Materials & Textures"),
						LOCTEXT("MaterialsBody", "One material instance per slot, parented to the DazToHue materials, with the export's textures."))
				]
				+ SWidgetSwitcher::Slot()
				[
					MakePage(LOCTEXT("GroomsTitle", "Grooms"),
						LOCTEXT("GroomsBody", "Groom alembics from the DazToHue Groom Export node: import, binding, hair material and occlusion."))
				]
				+ SWidgetSwitcher::Slot()
				[
					MakePage(LOCTEXT("ImportTitle", "Import"),
						LOCTEXT("ImportBody", "Runs every step in one pass and lists any manual steps that remain."))
				]
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
				SNew(STextBlock)
				.Text(LOCTEXT("ExportLabel", "Export Folder"))
			]

			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(SEditableTextBox)
				.HintText(LOCTEXT("ExportHint", "Folder holding the DazToHue export (DTH_<Character>.dth)"))
				.IsEnabled(false)
			]

			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.f, 0.f, 0.f, 0.f)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ToolTipText(LOCTEXT("BrowseTip", "Browse for an export folder"))
				.IsEnabled(false)
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

TSharedRef<SWidget> SDTHImportWizard::MakePage(const FText& Title, const FText& Body)
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
				.Text(LOCTEXT("Status", "Pick a DazToHue export folder to begin."))
			]

			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "PrimaryButton")
				.IsEnabled(false)
				.ToolTipText(LOCTEXT("ImportTip", "Run every step of the import"))
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

#undef LOCTEXT_NAMESPACE
