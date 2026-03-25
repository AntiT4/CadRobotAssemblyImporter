#include "Import/AssetImportUtils.h"

#include "Misc/PackageName.h"

namespace CadAssetImportUtils
{
	FString NormalizeMeshSourcePath(const FString& RawMeshPath)
	{
		FString NormalizedPath = RawMeshPath;
		NormalizedPath.TrimStartAndEndInline();
		NormalizedPath.ReplaceInline(TEXT("\\"), TEXT("/"));
		return NormalizedPath;
	}

	FString NormalizeExistingAssetPackagePath(const FString& RawAssetPath)
	{
		FString AssetPath = NormalizeMeshSourcePath(RawAssetPath);

		if (AssetPath.StartsWith(TEXT("Content/"), ESearchCase::IgnoreCase))
		{
			AssetPath = FString::Printf(TEXT("/Game/%s"), *AssetPath.RightChop(8));
		}
		else if (AssetPath.StartsWith(TEXT("/Content/"), ESearchCase::IgnoreCase))
		{
			AssetPath = FString::Printf(TEXT("/Game/%s"), *AssetPath.RightChop(9));
		}

		if (AssetPath.Contains(TEXT(".")))
		{
			AssetPath = FPackageName::ObjectPathToPackageName(AssetPath);
		}

		return AssetPath;
	}

	FString PackagePathToObjectPath(const FString& PackagePath)
	{
		const FString NormalizedPackagePath = NormalizeExistingAssetPackagePath(PackagePath);
		if (NormalizedPackagePath.IsEmpty() || NormalizedPackagePath.Contains(TEXT(".")))
		{
			return NormalizedPackagePath;
		}

		const FString AssetName = FPackageName::GetLongPackageAssetName(NormalizedPackagePath);
		return AssetName.IsEmpty()
			? NormalizedPackagePath
			: FString::Printf(TEXT("%s.%s"), *NormalizedPackagePath, *AssetName);
	}

	FString ObjectPathToPackagePath(const FString& ObjectPath)
	{
		return NormalizeExistingAssetPackagePath(ObjectPath);
	}
}
