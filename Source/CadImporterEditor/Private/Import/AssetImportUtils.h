#pragma once

#include "CoreMinimal.h"
#include "ImportModelTypes.h"

class UAssetImportTask;

namespace CadImportAssetImporterUtils
{
	FString NormalizeMeshSourcePath(const FString& RawMeshPath);
	bool IsFbxMeshSourcePath(const FString& RawMeshPath);
	FString NormalizeExistingAssetPackagePath(const FString& RawAssetPath);
	FString PackagePathToObjectPath(const FString& PackagePath);
	FString ObjectPathToPackagePath(const FString& ObjectPath);
	FString ResolveMeshAbsolutePath(const FCadImportModel& Model, const FString& RawMeshPath);
	FString GetFirstImportedStaticMeshPath(UAssetImportTask* ImportTask);
}
