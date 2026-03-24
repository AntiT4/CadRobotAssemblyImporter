#pragma once

#include "CoreMinimal.h"
#include "ImportOptions.h"
#include "ImportModelTypes.h"
#include "WorkflowTypes.h"

class FCadImporterRunner
{
public:
	bool RunImport(const FString& JsonPath, const FCadFbxImportOptions& ImportOptions) const;
	bool RunMasterWorkflowImport(const FCadMasterWorkflowBuildInput& BuildInput, const FCadFbxImportOptions& ImportOptions) const;
	bool SelectJsonFile(FString& OutJsonPath) const;
	bool SelectOutputJsonFile(FString& OutJsonPath) const;
};
