#include "Import/MeshImporter.h"

#include "Import/AssetImportUtils.h"
#include "AssetImportTask.h"
#include "AssetToolsModule.h"
#include "CadImporterEditor.h"
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "Factories/FbxFactory.h"
#include "Factories/FbxImportUI.h"
#include "Factories/FbxStaticMeshImportData.h"
#include "Modules/ModuleManager.h"
#include "PhysicsEngine/BodySetup.h"
#include "StaticMeshEditorSubsystem.h"

bool FCadImportAssetImporter::ImportMeshes(const FCadImportModel& Model, const FCadImportPaths& Paths, const FCadFbxImportOptions& Options, FCadImportResult& OutResult, FString& OutError)
{
	OutResult.MeshAssetsByLink.Reset();
	OutResult.ImportedMeshAssetPaths.Reset();

	if (!ConfigureFbxImportOnce(Options, OutError))
	{
		return false;
	}

	const bool bGenerateSimpleCollision = ShouldGenerateSimpleCollisionForModel(Model);
	if (CachedFbxImportUI && CachedFbxImportUI->StaticMeshImportData)
	{
		CachedFbxImportUI->StaticMeshImportData->bAutoGenerateCollision =
			Options.bAutoGenerateCollision && bGenerateSimpleCollision;
	}
	UE_LOG(LogCadImporter, Display, TEXT("Simple collision generation policy: %s"), bGenerateSimpleCollision ? TEXT("enabled") : TEXT("disabled"));

	TMap<FString, FString> ImportedMeshBySource;
	for (const FCadImportLink& Link : Model.Links)
	{
		if (!ImportMeshForLink(Model, Paths, Link, bGenerateSimpleCollision, ImportedMeshBySource, OutResult, OutError))
		{
			return false;
		}
	}

	return true;
}

bool FCadImportAssetImporter::ConfigureFbxImportOnce(const FCadFbxImportOptions& Options, FString& OutError)
{
	if (CachedFbxImportUI)
	{
		return true;
	}

	UFbxFactory* FbxFactory = NewObject<UFbxFactory>();
	if (!FbxFactory)
	{
		OutError = TEXT("Failed to allocate FBX factory.");
		return false;
	}

	if (Options.bShowDialog)
	{
		if (!FbxFactory->ConfigureProperties())
		{
			OutError = TEXT("FBX import options dialog was canceled.");
			return false;
		}
	}

	if (!FbxFactory->ImportUI)
	{
		FbxFactory->ImportUI = NewObject<UFbxImportUI>(FbxFactory);
	}

	if (!FbxFactory->ImportUI)
	{
		OutError = TEXT("Failed to initialize FBX import options.");
		return false;
	}

	CachedFbxImportUI = DuplicateObject<UFbxImportUI>(FbxFactory->ImportUI, GetTransientPackage());
	if (!CachedFbxImportUI)
	{
		OutError = TEXT("Failed to cache FBX import options.");
		return false;
	}

	CachedFbxImportUI->MeshTypeToImport = FBXIT_StaticMesh;
	CachedFbxImportUI->OriginalImportType = FBXIT_StaticMesh;
	CachedFbxImportUI->bAutomatedImportShouldDetectType = false;
	CachedFbxImportUI->bImportAsSkeletal = false;
	CachedFbxImportUI->bImportAnimations = false;
	CachedFbxImportUI->bImportMaterials = false;
	CachedFbxImportUI->bImportTextures = false;

	if (CachedFbxImportUI->StaticMeshImportData)
	{
		CachedFbxImportUI->StaticMeshImportData->bCombineMeshes = Options.bCombineMeshes;
		CachedFbxImportUI->StaticMeshImportData->bAutoGenerateCollision = Options.bAutoGenerateCollision;
		CachedFbxImportUI->StaticMeshImportData->bBuildNanite = Options.bBuildNanite;
		CachedFbxImportUI->StaticMeshImportData->ImportUniformScale = Options.ImportUniformScale;
		CachedFbxImportUI->StaticMeshImportData->ImportRotation = Options.ImportRotation;
		CachedFbxImportUI->StaticMeshImportData->ImportTranslation = Options.ImportTranslation;
		CachedFbxImportUI->StaticMeshImportData->bConvertScene = Options.bConvertScene;
		CachedFbxImportUI->StaticMeshImportData->bForceFrontXAxis = Options.bForceFrontXAxis;
		CachedFbxImportUI->StaticMeshImportData->bConvertSceneUnit = Options.bConvertSceneUnit;
	}

	return true;
}

bool FCadImportAssetImporter::EnsureSimpleCollision(UStaticMesh* StaticMesh, FString& OutError) const
{
	if (!StaticMesh)
	{
		OutError = TEXT("StaticMesh is null while checking collisions.");
		return false;
	}

	UBodySetup* BodySetup = StaticMesh->GetBodySetup();
	if (BodySetup && BodySetup->AggGeom.GetElementCount() > 0)
	{
		return true;
	}

	if (!BodySetup)
	{
		StaticMesh->CreateBodySetup();
		BodySetup = StaticMesh->GetBodySetup();
	}

	if (!BodySetup)
	{
		OutError = FString::Printf(TEXT("Failed to create a BodySetup for %s."), *StaticMesh->GetPathName());
		return false;
	}

	const FBoxSphereBounds MeshBounds = StaticMesh->GetBounds();
	if (MeshBounds.BoxExtent.IsNearlyZero())
	{
		OutError = FString::Printf(TEXT("Static mesh bounds are invalid for simple collision generation: %s"), *StaticMesh->GetPathName());
		return false;
	}

	UStaticMeshEditorSubsystem* StaticMeshEditorSubsystem = GEditor ? GEditor->GetEditorSubsystem<UStaticMeshEditorSubsystem>() : nullptr;
	if (!StaticMeshEditorSubsystem)
	{
		OutError = TEXT("StaticMeshEditorSubsystem is unavailable for simplified collision generation.");
		return false;
	}

	const int32 CollisionIndex = StaticMeshEditorSubsystem->AddSimpleCollisionsWithNotification(
		StaticMesh,
		EScriptCollisionShapeType::NDOP26,
		true);
	if (CollisionIndex < 0)
	{
		OutError = FString::Printf(TEXT("Failed to generate 26DOP simple collision for %s."), *StaticMesh->GetPathName());
		return false;
	}

	UE_LOG(LogCadImporter, Display, TEXT("Generated 26DOP simple collision for static mesh: %s"), *StaticMesh->GetPathName());
	return true;
}

bool FCadImportAssetImporter::ShouldGenerateSimpleCollisionForModel(const FCadImportModel& Model) const
{
	// Static actor/static assembly imports should not synthesize broad simple collisions.
	// They can easily produce oversized hulls for widely separated meshes.
	if (Model.Profile == ECadImportModelProfile::FixedAssembly)
	{
		return false;
	}

	for (const FCadImportJoint& Joint : Model.Joints)
	{
		if (Joint.Type == ECadImportJointType::Revolute || Joint.Type == ECadImportJointType::Prismatic)
		{
			return true;
		}
	}

	return false;
}

bool FCadImportAssetImporter::ImportMeshForLink(
	const FCadImportModel& Model,
	const FCadImportPaths& Paths,
	const FCadImportLink& Link,
	bool bGenerateSimpleCollision,
	TMap<FString, FString>& InOutImportedMeshBySource,
	FCadImportResult& OutResult,
	FString& OutError)
{
	if (Link.Visuals.IsEmpty())
	{
		UE_LOG(LogCadImporter, Warning, TEXT("Skipping mesh import for link '%s' because it has no visuals."), *Link.Name);
		return true;
	}

	const FString* DestinationPath = Paths.LinkFolders.Find(Link.Name);
	if (!DestinationPath || DestinationPath->IsEmpty())
	{
		OutError = FString::Printf(TEXT("Missing destination content path for link: %s"), *Link.Name);
		return false;
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");

	for (const FCadImportVisual& Visual : Link.Visuals)
	{
		if (Visual.MeshPath.IsEmpty())
		{
			OutError = FString::Printf(TEXT("Missing mesh path for link visual: %s"), *Link.Name);
			return false;
		}

		if (!CadImportAssetImporterUtils::IsFbxMeshSourcePath(Visual.MeshPath))
		{
			const FString StaticMeshPackagePath = CadImportAssetImporterUtils::NormalizeExistingAssetPackagePath(Visual.MeshPath);
			const FString MeshSourceKey = StaticMeshPackagePath.ToLower();
			if (const FString* ExistingImportedPath = InOutImportedMeshBySource.Find(MeshSourceKey))
			{
				UE_LOG(LogCadImporter, Display, TEXT("Reusing pre-imported static mesh for link %s: %s"), *Link.Name, **ExistingImportedPath);
				OutResult.MeshAssetsByLink.FindOrAdd(Link.Name).Add(*ExistingImportedPath);
				OutResult.ImportedMeshAssetPaths.AddUnique(*ExistingImportedPath);
				continue;
			}

			const FString StaticMeshObjectPath = CadImportAssetImporterUtils::PackagePathToObjectPath(StaticMeshPackagePath);
			UStaticMesh* ExistingMesh = LoadObject<UStaticMesh>(nullptr, *StaticMeshObjectPath);
			if (!ExistingMesh)
			{
				OutError = FString::Printf(
					TEXT("mesh_path is not .fbx, so it was treated as a pre-imported StaticMesh package path, but load failed: raw=%s resolved=%s"),
					*Visual.MeshPath,
					*StaticMeshPackagePath);
				return false;
			}

			const FString ResolvedAssetPackagePath = ExistingMesh->GetOutermost()->GetName();
			if (bGenerateSimpleCollision && !EnsureSimpleCollision(ExistingMesh, OutError))
			{
				return false;
			}
			UE_LOG(
				LogCadImporter,
				Display,
				TEXT("Using pre-imported StaticMesh for link %s (raw=%s -> package=%s)"),
				*Link.Name,
				*Visual.MeshPath,
				*ResolvedAssetPackagePath);

			InOutImportedMeshBySource.Add(MeshSourceKey, ResolvedAssetPackagePath);
			OutResult.MeshAssetsByLink.FindOrAdd(Link.Name).Add(ResolvedAssetPackagePath);
			OutResult.ImportedMeshAssetPaths.AddUnique(ResolvedAssetPackagePath);
			continue;
		}

		const FString AbsolutePath = CadImportAssetImporterUtils::ResolveMeshAbsolutePath(Model, Visual.MeshPath);
		if (!FPaths::FileExists(AbsolutePath))
		{
			OutError = FString::Printf(TEXT("FBX not found: %s"), *AbsolutePath);
			return false;
		}

		UE_LOG(LogCadImporter, Display, TEXT("mesh_path for link %s is .fbx, importing source file: %s"), *Link.Name, *AbsolutePath);

		const FString MeshSourceKey = AbsolutePath.ToLower();
		if (const FString* ExistingImportedPath = InOutImportedMeshBySource.Find(MeshSourceKey))
		{
			OutResult.MeshAssetsByLink.FindOrAdd(Link.Name).Add(*ExistingImportedPath);
			OutResult.ImportedMeshAssetPaths.AddUnique(*ExistingImportedPath);
			continue;
		}

		UFbxFactory* FbxFactory = NewObject<UFbxFactory>();
		if (!FbxFactory)
		{
			OutError = TEXT("Failed to allocate FBX factory.");
			return false;
		}

		UFbxImportUI* PerImportOptions = DuplicateObject<UFbxImportUI>(CachedFbxImportUI.Get(), FbxFactory);
		if (!PerImportOptions)
		{
			OutError = TEXT("Failed to duplicate cached FBX import options.");
			return false;
		}
		FbxFactory->ImportUI = PerImportOptions;

		UAssetImportTask* ImportTask = NewObject<UAssetImportTask>();
		ImportTask->Filename = AbsolutePath;
		ImportTask->DestinationPath = *DestinationPath;
		ImportTask->DestinationName = FPaths::GetBaseFilename(AbsolutePath);
		ImportTask->Factory = FbxFactory;
		ImportTask->Options = PerImportOptions;
		ImportTask->bAutomated = true;
		ImportTask->bReplaceExisting = true;
		ImportTask->bReplaceExistingSettings = true;
		ImportTask->bSave = false;

		TArray<UAssetImportTask*> Tasks;
		Tasks.Add(ImportTask);
		AssetToolsModule.Get().ImportAssetTasks(Tasks);

		const FString ImportedMeshPath = CadImportAssetImporterUtils::GetFirstImportedStaticMeshPath(ImportTask);
		if (ImportedMeshPath.IsEmpty())
		{
			OutError = FString::Printf(TEXT("FBX import did not produce a static mesh: %s"), *AbsolutePath);
			return false;
		}

		const FString ImportedMeshObjectPath = CadImportAssetImporterUtils::PackagePathToObjectPath(ImportedMeshPath);
		UStaticMesh* ImportedMesh = LoadObject<UStaticMesh>(nullptr, *ImportedMeshObjectPath);
		if (!ImportedMesh)
		{
			OutError = FString::Printf(TEXT("Imported static mesh could not be loaded for collision generation: %s"), *ImportedMeshPath);
			return false;
		}

		if (bGenerateSimpleCollision && !EnsureSimpleCollision(ImportedMesh, OutError))
		{
			return false;
		}

		UE_LOG(LogCadImporter, Display, TEXT("Imported FBX %s -> %s"), *AbsolutePath, *ImportedMeshPath);

		InOutImportedMeshBySource.Add(MeshSourceKey, ImportedMeshPath);
		OutResult.MeshAssetsByLink.FindOrAdd(Link.Name).Add(ImportedMeshPath);
		OutResult.ImportedMeshAssetPaths.AddUnique(ImportedMeshPath);
	}

	return true;
}
