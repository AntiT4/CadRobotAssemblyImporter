#pragma once

#include "CoreMinimal.h"
#include "ImportOptions.h"
#include "ImportModelTypes.h"
#include "WorkflowTypes.h"

class FCadImportService
{
public:
	bool RunImport(const FString& JsonPath, const FCadFbxImportOptions& ImportOptions) const;
	bool BuildFromWorkflow(const FCadMasterWorkflowBuildInput& BuildInput, const FCadFbxImportOptions& ImportOptions) const;
	bool SelectJsonFile(FString& OutJsonPath) const;
	bool SelectJsonSavePath(FString& OutJsonPath) const;
};
