#pragma once

#include "CoreMinimal.h"

namespace CadAssetImportUtils
{
	FString NormalizeMeshSourcePath(const FString& RawMeshPath);
	FString NormalizeExistingAssetPackagePath(const FString& RawAssetPath);
	FString PackagePathToObjectPath(const FString& PackagePath);
	FString ObjectPathToPackagePath(const FString& ObjectPath);
}
