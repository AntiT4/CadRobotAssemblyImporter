#pragma once

#include "CoreMinimal.h"
#include "ImportOptions.h"
#include "ImportModelTypes.h"
#include "WorkflowTypes.h"

struct FCadLevelReplaceResult;

class FCadImportService
{
public:
	bool RunImport(const FString& JsonPath, const FCadFbxImportOptions& ImportOptions) const;
	bool BuildFromWorkflow(
		const FCadWorkflowBuildInput& BuildInput,
		const FCadFbxImportOptions& ImportOptions,
		FCadLevelReplaceResult* OutReplaceResult = nullptr) const;
	bool SelectJsonFile(FString& OutJsonPath) const;
	bool SelectJsonSavePath(FString& OutJsonPath) const;
};
