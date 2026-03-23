#include "Import/AssetImportUtils.h"

#include "AssetImportTask.h"
#include "Engine/StaticMesh.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"

namespace CadImportAssetImporterUtils
{
	FString NormalizeMeshSourcePath(const FString& RawMeshPath)
	{
		FString NormalizedPath = RawMeshPath;
		NormalizedPath.TrimStartAndEndInline();
		NormalizedPath.ReplaceInline(TEXT("\\"), TEXT("/"));
		return NormalizedPath;
	}

	bool IsFbxMeshSourcePath(const FString& RawMeshPath)
	{
		const FString NormalizedPath = NormalizeMeshSourcePath(RawMeshPath);
		return FPaths::GetExtension(NormalizedPath).Equals(TEXT("fbx"), ESearchCase::IgnoreCase);
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

	FString ResolveMeshAbsolutePath(const FCadImportModel& Model, const FString& RawMeshPath)
	{
		FString NormalizedPath = NormalizeMeshSourcePath(RawMeshPath);

		if (NormalizedPath.StartsWith(TEXT("/")) && !NormalizedPath.StartsWith(TEXT("//")) && !NormalizedPath.Contains(TEXT(":")))
		{
			NormalizedPath.RightChopInline(1, EAllowShrinking::No);
		}

		if (FPaths::GetExtension(NormalizedPath).IsEmpty())
		{
			NormalizedPath += TEXT(".fbx");
		}

		if (FPaths::IsRelative(NormalizedPath))
		{
			return FPaths::ConvertRelativePathToFull(FPaths::Combine(Model.SourceDirectory, NormalizedPath));
		}

		return FPaths::ConvertRelativePathToFull(NormalizedPath);
	}

	FString GetFirstImportedStaticMeshPath(UAssetImportTask* ImportTask)
	{
		if (!ImportTask)
		{
			return FString();
		}

		for (UObject* ImportedObject : ImportTask->GetObjects())
		{
			if (const UStaticMesh* StaticMesh = Cast<UStaticMesh>(ImportedObject))
			{
				return StaticMesh->GetOutermost()->GetName();
			}
		}

		for (const FString& ImportedPath : ImportTask->ImportedObjectPaths)
		{
			if (LoadObject<UStaticMesh>(nullptr, *ImportedPath) != nullptr)
			{
				return ObjectPathToPackagePath(ImportedPath);
			}
		}

		return FString();
	}
}
