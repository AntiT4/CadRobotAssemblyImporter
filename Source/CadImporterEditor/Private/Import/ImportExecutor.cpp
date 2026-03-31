#include "Import/ImportExecutor.h"

#include "CadImporterEditor.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/StaticMesh.h"
#include "Import/AssetImportUtils.h"
#include "Import/BlueprintBuilder.h"
#include "Import/PathBuilder.h"
#include "Misc/Paths.h"
#include "PhysicsEngine/BodySetup.h"
#include "StaticMeshEditorSubsystem.h"
#include "UI/ImportDialogUtils.h"

namespace
{
	TSet<FString> CollectJointedLinkNames(const FCadImportModel& Model)
	{
		TSet<FString> JointedLinkNames;
		if (Model.Profile != ECadImportModelProfile::DynamicRobot)
		{
			return JointedLinkNames;
		}

		for (const FCadImportJoint& Joint : Model.Joints)
		{
			if (!Joint.Parent.TrimStartAndEnd().IsEmpty())
			{
				JointedLinkNames.Add(Joint.Parent.TrimStartAndEnd());
			}
			if (!Joint.Child.TrimStartAndEnd().IsEmpty())
			{
				JointedLinkNames.Add(Joint.Child.TrimStartAndEnd());
			}
		}
		return JointedLinkNames;
	}

	bool EnsureSimpleCollision(UStaticMesh* StaticMesh, FString& OutError)
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
			OutError = TEXT("StaticMeshEditorSubsystem is unavailable for simple collision generation.");
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

	bool TryResolveExistingMeshAssets(
		const FCadImportModel& Model,
		FCadImportResult& OutImportResult,
		FString& OutError)
	{
		OutImportResult.MeshAssetsByLink.Reset();
		OutImportResult.ImportedMeshAssetPaths.Reset();
		OutError.Reset();
		const TSet<FString> JointedLinkNames = CollectJointedLinkNames(Model);

		for (const FCadImportLink& Link : Model.Links)
		{
			const bool bLinkRequiresCollision = JointedLinkNames.Contains(Link.Name.TrimStartAndEnd());
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
				UStaticMesh* StaticMesh = LoadObject<UStaticMesh>(nullptr, *MeshObjectPath);
				if (!StaticMesh)
				{
					OutError = FString::Printf(
						TEXT("Static mesh asset could not be loaded for link '%s': %s"),
						*Link.Name,
						*RawMeshPath);
					return false;
				}

				if (bLinkRequiresCollision && !EnsureSimpleCollision(StaticMesh, OutError))
				{
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
