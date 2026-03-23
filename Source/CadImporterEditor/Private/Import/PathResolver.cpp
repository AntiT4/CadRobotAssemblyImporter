#include "Import/PathResolver.h"

#include "Misc/Paths.h"

FCadImportPaths FCadImportPathResolver::BuildPaths(const FCadImportModel& Model) const
{
	FCadImportPaths Paths;
	FString RootFolderPath = Model.OutputRootPath.TrimStartAndEnd();
	const bool bHasExplicitRootFolder = !RootFolderPath.IsEmpty();
	if (!bHasExplicitRootFolder)
	{
		RootFolderPath = FString::Printf(TEXT("/Game/%s"), *Model.RobotName);
	}

	Paths.BlueprintPath = MakeBlueprintPath(RootFolderPath, Model.RobotName);

	for (const FCadImportLink& Link : Model.Links)
	{
		Paths.LinkFolders.Add(Link.Name, MakeLinkFolderPath(RootFolderPath, Model.RobotName, Link.Name, bHasExplicitRootFolder));
	}

	return Paths;
}

FString FCadImportPathResolver::MakeLinkFolderPath(
	const FString& RootFolderPath,
	const FString& RobotName,
	const FString& LinkName,
	const bool bUseRobotSubfolder) const
{
	if (bUseRobotSubfolder)
	{
		return FString::Printf(TEXT("%s/%s/%s"), *RootFolderPath, *RobotName, *LinkName);
	}

	return FString::Printf(TEXT("%s/%s"), *RootFolderPath, *LinkName);
}

FString FCadImportPathResolver::MakeBlueprintPath(const FString& RootFolderPath, const FString& RobotName) const
{
	return FString::Printf(TEXT("%s/BP_%s"), *RootFolderPath, *RobotName);
}
