#pragma once

#include "Widgets/SCompoundWidget.h"

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

	TSharedRef<SWidget> MakeExportBar();
	TSharedRef<SWidget> MakeStepBar();
	TSharedRef<SWidget> MakePage(const FText& Title, const FText& Body);
	TSharedRef<SWidget> MakeActionBar();

	EPage CurrentPage = EPage::ProjectSettings;
	TSharedPtr<SWidgetSwitcher> Pages;
};
