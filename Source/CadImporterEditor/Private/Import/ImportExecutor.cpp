#include "Import/ImportExecutor.h"

#include "CadImporterEditor.h"
#include "Engine/Blueprint.h"
#include "Import/BlueprintBuilder.h"
#include "Import/MeshImporter.h"
#include "Import/PathBuilder.h"
#include "UI/ImportDialogUtils.h"

namespace CadImportExecutor
{
	bool TryImportModel(
		const FCadImportModel& Model,
		const FString& SourceLabel,
		const FCadFbxImportOptions& ImportOptions,
		UBlueprint** OutBuiltBlueprint,
		FString& OutError)
	{
		OutError.Reset();
		CadImportDialogUtils::LogModel(Model, SourceLabel);

		const FCadPathBuilder PathBuilder;
		const FCadImportPaths Paths = PathBuilder.Build(Model);

		FCadMeshImporter MeshImporter;
		FCadImportResult ImportResult;
		UE_LOG(LogCadImporter, Display, TEXT("FBX Import Effective Options: convert_scene=%s force_front_x_axis=%s convert_scene_unit=%s combine_meshes=%s auto_collision=%s nanite=%s scale=%.4f translation=%s rotation=%s"),
			ImportOptions.bConvertScene ? TEXT("true") : TEXT("false"),
			ImportOptions.bForceFrontXAxis ? TEXT("true") : TEXT("false"),
			ImportOptions.bConvertSceneUnit ? TEXT("true") : TEXT("false"),
			ImportOptions.bCombineMeshes ? TEXT("true") : TEXT("false"),
			ImportOptions.bAutoGenerateCollision ? TEXT("true") : TEXT("false"),
			ImportOptions.bBuildNanite ? TEXT("true") : TEXT("false"),
			ImportOptions.ImportUniformScale,
			*CadImportDialogUtils::FormatVector(ImportOptions.ImportTranslation),
			*CadImportDialogUtils::FormatRotator(ImportOptions.ImportRotation));

		if (!MeshImporter.ImportMeshes(Model, Paths, ImportOptions, ImportResult, OutError))
		{
			return false;
		}

		if (ImportResult.ImportedMeshAssetPaths.Num() == 0)
		{
			OutError = TEXT("Import finished, but no static meshes were created.");
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
		UE_LOG(LogCadImporter, Display, TEXT("Imported %d static mesh assets and built actor blueprint: %s"), ImportResult.ImportedMeshAssetPaths.Num(), *ImportResult.BlueprintAssetPath);
		if (OutBuiltBlueprint)
		{
			*OutBuiltBlueprint = RobotBlueprint;
		}

		return true;
	}
}
