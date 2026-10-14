#pragma once

#include "CoreMinimal.h"

/** One line of an import's progress, shown on the wizard's Import page and in the Output Log. */
struct FDTHImportMessage
{
	enum class ESeverity : uint8 { Info, Warning, Error };

	ESeverity Severity = ESeverity::Info;
	FText Text;
};

/** Something the wizard can't do for the user, listed once at the end of the run. */
struct FDTHManualStep
{
	FText Title;
	TArray<FText> Instructions;
};

/** Everything a run reports back. */
struct FDTHImportReport
{
	TArray<FDTHImportMessage> Messages;
	TArray<FDTHManualStep> ManualSteps;
	bool bSucceeded = true;

	void Info(const FText& Text);
	void Warning(const FText& Text);
	void Error(const FText& Text);
};
