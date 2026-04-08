#pragma once

#include "CoreMinimal.h"
#include "ImportModelTypes.h"

class FCadPathBuilder
{
public:
	FCadImportPaths Build(const FCadImportModel& Model) const;

private:
	FString MakeLinkFolderPath(const FString& RootFolderPath, const FString& RobotName, const FString& LinkName, bool bUseRobotSubfolder) const;
	FString MakeBlueprintPath(const FString& RootFolderPath, const FString& RobotName) const;
};
