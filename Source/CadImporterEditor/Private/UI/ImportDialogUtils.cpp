#include "UI/ImportDialogUtils.h"

#include "Import/AssetImportUtils.h"
#include "CadImporterEditor.h"
#include "Editor.h"

namespace CadImportDialogUtils
{
	FString ProfileToString(ECadImportModelProfile Profile)
	{
		switch (Profile)
		{
		case ECadImportModelProfile::FixedAssembly:
			return TEXT("fixed_assembly");
		case ECadImportModelProfile::DynamicRobot:
		default:
			return TEXT("dynamic_robot");
		}
	}

	FString JointDriveModeToString(ECadImportJointDriveMode Mode)
	{
		switch (Mode)
		{
		case ECadImportJointDriveMode::None:
			return TEXT("none");
		case ECadImportJointDriveMode::Velocity:
			return TEXT("velocity");
		case ECadImportJointDriveMode::Position:
		default:
			return TEXT("position");
		}
	}

	FString JointTypeToString(ECadImportJointType Type)
	{
		switch (Type)
		{
		case ECadImportJointType::Fixed:
			return TEXT("fixed");
		case ECadImportJointType::Revolute:
			return TEXT("revolute");
		case ECadImportJointType::Prismatic:
			return TEXT("prismatic");
		default:
			return TEXT("unknown");
		}
	}

	FString FormatVector(const FVector& Value)
	{
		return FString::Printf(TEXT("(%.3f, %.3f, %.3f)"), Value.X, Value.Y, Value.Z);
	}

	FString FormatRotator(const FRotator& Value)
	{
		return FString::Printf(TEXT("(Pitch=%.3f, Yaw=%.3f, Roll=%.3f)"), Value.Pitch, Value.Yaw, Value.Roll);
	}

	FString FormatColor(const FLinearColor& Value)
	{
		return FString::Printf(TEXT("(%.3f, %.3f, %.3f, %.3f)"), Value.R, Value.G, Value.B, Value.A);
	}

	void SyncImportedAssetsInContentBrowser(const FCadImportResult& ImportResult)
	{
		if (!GEditor)
		{
			return;
		}

		TSet<FString> UniqueImportedPaths;
		for (const FString& ImportedPath : ImportResult.ImportedMeshAssetPaths)
		{
			if (!ImportedPath.IsEmpty())
			{
				UniqueImportedPaths.Add(ImportedPath);
			}
		}

		if (UniqueImportedPaths.Num() == 0)
		{
			for (const TPair<FString, TArray<FString>>& Pair : ImportResult.MeshAssetsByLink)
			{
				for (const FString& MeshPath : Pair.Value)
				{
					if (!MeshPath.IsEmpty())
					{
						UniqueImportedPaths.Add(MeshPath);
					}
				}
			}
		}

		TArray<UObject*> ImportedAssets;
		ImportedAssets.Reserve(UniqueImportedPaths.Num());
		for (const FString& PackagePath : UniqueImportedPaths)
		{
			const FString ObjectPath = CadAssetImportUtils::PackagePathToObjectPath(PackagePath);
			if (UObject* ImportedAsset = LoadObject<UObject>(nullptr, *ObjectPath))
			{
				ImportedAssets.Add(ImportedAsset);
			}
		}

		if (!ImportResult.BlueprintAssetPath.IsEmpty())
		{
			if (UObject* BlueprintAsset = LoadObject<UObject>(nullptr, *ImportResult.BlueprintAssetPath))
			{
				ImportedAssets.AddUnique(BlueprintAsset);
			}
		}

		if (ImportedAssets.Num() > 0)
		{
			GEditor->SyncBrowserToObjects(ImportedAssets);
		}
	}

	void LogModel(const FCadImportModel& Model, const FString& JsonPath)
	{
		UE_LOG(LogCadImporter, Display, TEXT("CAD JSON: %s"), *JsonPath);
		UE_LOG(LogCadImporter, Display, TEXT("Robot=%s Profile=%s Root=%s Units(length=%s, angle=%s) Links=%d Joints=%d"),
			*Model.RobotName,
			*ProfileToString(Model.Profile),
			*Model.RootLinkName,
			*Model.Units.Length,
			*Model.Units.Angle,
			Model.Links.Num(),
			Model.Joints.Num());
		if (Model.RootPlacement.bHasWorldTransform || !Model.RootPlacement.ParentActorName.IsEmpty())
		{
			UE_LOG(LogCadImporter, Display, TEXT("RootPlacement has_world=%s world_location=%s world_rotation=%s world_scale=%s parent_actor=%s"),
				Model.RootPlacement.bHasWorldTransform ? TEXT("true") : TEXT("false"),
				*FormatVector(Model.RootPlacement.WorldTransform.GetLocation()),
				*FormatRotator(Model.RootPlacement.WorldTransform.Rotator()),
				*FormatVector(Model.RootPlacement.WorldTransform.GetScale3D()),
				*Model.RootPlacement.ParentActorName);
		}

		for (const FCadImportLink& Link : Model.Links)
		{
			const FVector Location = Link.Transform.GetLocation();
			const FRotator Rotation = Link.Transform.GetRotation().Rotator();
			UE_LOG(LogCadImporter, Display, TEXT("Link %s visuals=%d mass=%.3f simulate=%s location=%s rotation=%s"),
				*Link.Name,
				Link.Visuals.Num(),
				Link.Physics.Mass,
				Link.Physics.bSimulatePhysics ? TEXT("true") : TEXT("false"),
				*FormatVector(Location),
				*FormatRotator(Rotation));

			for (const FCadImportVisual& Visual : Link.Visuals)
			{
				const FString ColorText = Visual.bHasColor ? FormatColor(Visual.Color) : TEXT("none");
				UE_LOG(LogCadImporter, Display, TEXT("  Visual mesh=%s material_path=%s material_name=%s location=%s rotation=%s color=%s"),
					*Visual.MeshPath,
					*Visual.MaterialPath,
					*Visual.MaterialName,
					*FormatVector(Visual.Transform.GetLocation()),
					*FormatRotator(Visual.Transform.GetRotation().Rotator()),
					*ColorText);
			}
		}

		for (const FCadImportJoint& Joint : Model.Joints)
		{
			const FVector Location = Joint.Transform.GetLocation();
			const FRotator Rotation = Joint.Transform.GetRotation().Rotator();
			const FString DriveSourceText = Joint.Drive.bHasDrive ? TEXT("json") : TEXT("profile_default");
			UE_LOG(LogCadImporter, Display, TEXT("Joint %s parent=%s child=%s type=%s axis=%s drive_enabled=%s drive_mode=%s drive_source=%s location=%s rotation=%s"),
				*Joint.Name,
				*Joint.Parent,
				*Joint.Child,
				*JointTypeToString(Joint.Type),
				*FormatVector(Joint.Axis),
				Joint.Drive.bEnabled ? TEXT("true") : TEXT("false"),
				*JointDriveModeToString(Joint.Drive.Mode),
				*DriveSourceText,
				*FormatVector(Location),
				*FormatRotator(Rotation));

			if (Joint.Limit.bHasLimit)
			{
				UE_LOG(LogCadImporter, Display, TEXT("  Limit lower=%.3f upper=%.3f effort=%.3f velocity=%.3f"),
					Joint.Limit.Lower,
					Joint.Limit.Upper,
					Joint.Limit.Effort,
					Joint.Limit.Velocity);
			}
		}
	}
}
