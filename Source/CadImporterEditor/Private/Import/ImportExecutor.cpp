#include "Import/ImportExecutor.h"

#include "CadImporterEditor.h"
#include "Engine/Blueprint.h"
#include "Engine/StaticMesh.h"
#include "Import/AssetImportUtils.h"
#include "Import/BlueprintBuilder.h"
#include "Import/PathBuilder.h"
#include "Misc/Paths.h"
#include "UI/ImportDialogUtils.h"

namespace
{
	bool TryResolveExistingMeshAssets(
		const FCadImportModel& Model,
		FCadImportResult& OutImportResult,
		FString& OutError)
	{
		OutImportResult.MeshAssetsByLink.Reset();
		OutImportResult.ImportedMeshAssetPaths.Reset();
		OutError.Reset();

		for (const FCadImportLink& Link : Model.Links)
		{
			TArray<FString>& ResolvedMeshPaths = OutImportResult.MeshAssetsByLink.FindOrAdd(Link.Name);
			ResolvedMeshPaths.Reserve(Link.Visuals.Num());

			for (const FCadImportVisual& Visual : Link.Visuals)
			{
				const FString RawMeshPath = Visual.MeshPath.TrimStartAndEnd();
				if (RawMeshPath.IsEmpty())
				{
					ResolvedMeshPaths.Add(FString());
					continue;
				}

				if (FPaths::GetExtension(CadAssetImportUtils::NormalizeMeshSourcePath(RawMeshPath)).Equals(TEXT("fbx"), ESearchCase::IgnoreCase))
				{
					OutError = FString::Printf(
						TEXT("FBX mesh sources are no longer supported. Replace '%s' on link '%s' with an existing static mesh asset path."),
						*RawMeshPath,
						*Link.Name);
					return false;
				}

				const FString MeshPackagePath = CadAssetImportUtils::NormalizeExistingAssetPackagePath(RawMeshPath);
				const FString MeshObjectPath = CadAssetImportUtils::PackagePathToObjectPath(MeshPackagePath);
				if (LoadObject<UStaticMesh>(nullptr, *MeshObjectPath) == nullptr)
				{
					OutError = FString::Printf(
						TEXT("Static mesh asset could not be loaded for link '%s': %s"),
						*Link.Name,
						*RawMeshPath);
					return false;
				}

				ResolvedMeshPaths.Add(MeshPackagePath);
				OutImportResult.ImportedMeshAssetPaths.AddUnique(MeshPackagePath);
			}
		}

		return true;
	}
}

namespace CadImportExecutor
{
	bool TryImportModel(
		const FCadImportModel& Model,
		const FString& SourceLabel,
		UBlueprint** OutBuiltBlueprint,
		FString& OutError)
	{
		OutError.Reset();
		CadImportDialogUtils::LogModel(Model, SourceLabel);

		const FCadPathBuilder PathBuilder;
		const FCadImportPaths Paths = PathBuilder.Build(Model);
		FCadImportResult ImportResult;
		if (!TryResolveExistingMeshAssets(Model, ImportResult, OutError))
		{
			return false;
		}

		FCadBlueprintBuilder BlueprintBuilder;
		UBlueprint* RobotBlueprint = BlueprintBuilder.BuildBlueprint(Model, Paths, ImportResult, OutError);
		if (!RobotBlueprint)
		{
			return false;
		}

		ImportResult.BlueprintAssetPath = RobotBlueprint->GetPathName();
		CadImportDialogUtils::SyncImportedAssetsInContentBrowser(ImportResult);
		UE_LOG(LogCadImporter, Display, TEXT("Resolved %d static mesh assets and built actor blueprint: %s"), ImportResult.ImportedMeshAssetPaths.Num(), *ImportResult.BlueprintAssetPath);
		if (OutBuiltBlueprint)
		{
			*OutBuiltBlueprint = RobotBlueprint;
		}

		return true;
	}
}
