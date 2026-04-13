#pragma once

#include "CoreMinimal.h"
#include "ImportModelTypes.h"
#include "WorkflowTypes.h"

struct FCadLevelReplaceResult;

class ICadImportServiceNotifier
{
public:
	virtual ~ICadImportServiceNotifier() = default;
	virtual void ShowError(const FString& Title, const FString& Error) const = 0;
};

class FCadImportService
{
public:
	explicit FCadImportService(TSharedPtr<ICadImportServiceNotifier> InNotifier = nullptr);

	bool BuildFromWorkflow(
		const FCadWorkflowBuildInput& BuildInput,
		FCadLevelReplaceResult* OutReplaceResult = nullptr) const;

private:
	bool ReportFailure(const TCHAR* LogPrefix, const FString& DialogTitle, const FString& Error) const;

	TSharedPtr<ICadImportServiceNotifier> Notifier;
};
