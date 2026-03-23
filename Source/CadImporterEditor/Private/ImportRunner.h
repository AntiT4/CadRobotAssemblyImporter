#pragma once

#include "CoreMinimal.h"
#include "ImportTypes.h"

class UBlueprint;

class FCadImporterRunner
{
public:
	bool RunImport(const FString& JsonPath, const FCadFbxImportOptions& ImportOptions) const;
	bool RunMasterWorkflowImport(const FCadMasterWorkflowBuildInput& BuildInput, const FCadFbxImportOptions& ImportOptions) const;
	bool SelectJsonFile(FString& OutJsonPath) const;
	bool SelectOutputJsonFile(FString& OutJsonPath) const;

private:
	bool RunImportModel(
		const FCadImportModel& Model,
		const FString& SourceLabel,
		const FCadFbxImportOptions& ImportOptions,
		UBlueprint** OutBuiltBlueprint = nullptr) const;
};
