#pragma once

#include "CoreMinimal.h"

enum class ECadImportJointType : uint8
{
	Fixed,
	Revolute,
	Prismatic
};

enum class ECadImportModelProfile : uint8
{
	DynamicRobot,
	FixedAssembly
};

enum class ECadImportJointDriveMode : uint8
{
	None,
	Velocity,
	Position
};

enum class ECadMasterChildActorType : uint8
{
	Static,
	Movable
};

struct FCadImportUnits
{
	FString Length;
	FString Angle;
	FString UpAxis = TEXT("z");
	FString FrontAxis = TEXT("x");
	FString Handedness = TEXT("left");
	FString EulerOrder = TEXT("xyz");
	float MeshScale = 1.0f;
};

struct FCadImportJointLimit
{
	bool bHasLimit = false;
	float Lower = 0.0f;
	float Upper = 0.0f;
	float Effort = 0.0f;
	float Velocity = 0.0f;
};

struct FCadImportJointDrive
{
	bool bHasDrive = false;
	bool bEnabled = true;
	ECadImportJointDriveMode Mode = ECadImportJointDriveMode::Position;
	float Strength = 1000.0f;
	float Damping = 2000.0f;
	float MaxForce = 0.0f;
};

struct FCadImportRootPlacement
{
	bool bHasWorldTransform = false;
	FTransform WorldTransform = FTransform::Identity;
	FString ParentActorName;
};

struct FCadImportVisual
{
	FString MeshPath;
	FTransform Transform = FTransform::Identity;
	FString MaterialPath;
	FString MaterialName;
	FLinearColor Color = FLinearColor::White;
	bool bHasColor = false;
};

struct FCadImportPhysics
{
	float Mass = 0.0f;
	bool bSimulatePhysics = false;
};

struct FCadImportLink
{
	FString Name;
	FTransform Transform = FTransform::Identity;
	FCadImportPhysics Physics;
	TArray<FCadImportVisual> Visuals;
};

struct FCadImportJoint
{
	// Joint drive schema:
	// - drive.enabled (bool)
	// - drive.mode ("none" | "velocity" | "position")
	// - drive.strength / drive.damping / drive.max_force
	// If drive is omitted in JSON, parser defaults by model profile
	// (dynamic_robot: motor enabled, fixed_assembly: motor disabled).
	FString Name;
	FString Parent;
	FString Child;
	FString ComponentName1;
	FString ComponentName2;
	ECadImportJointType Type = ECadImportJointType::Fixed;
	FVector Axis = FVector::UpVector;
	FTransform Transform = FTransform::Identity;
	FCadImportJointLimit Limit;
	FCadImportJointDrive Drive;
};

struct FCadImportPaths
{
	FString BlueprintPath;
	TMap<FString, FString> LinkFolders;
};

struct FCadFbxImportOptions
{
	bool bShowDialog = true;
	bool bConvertScene = true;
	bool bForceFrontXAxis = false;
	bool bConvertSceneUnit = true;
	float ImportUniformScale = 0.001f;
	FVector ImportTranslation = FVector::ZeroVector;
	FRotator ImportRotation = FRotator(0.0f, -90.0f, 90.0f);
	bool bCombineMeshes = false;
	bool bAutoGenerateCollision = true;
	bool bBuildNanite = false;
};

struct FCadImportModel
{
	// Root-level JSON profile:
	// - dynamic_robot: physics-driven joints
	// - fixed_assembly: static/passive assembly
	// Legacy JSON without profile defaults to dynamic_robot.
	ECadImportModelProfile Profile = ECadImportModelProfile::DynamicRobot;
	FString RobotName;
	// Optional root content path for generated assets (e.g. "/Game/MyMaster").
	// When empty, defaults to "/Game/<RobotName>".
	FString OutputRootPath;
	FString RootLinkName;
	FString SourceDirectory;
	FCadImportRootPlacement RootPlacement;
	FCadImportUnits Units;
	TArray<FCadImportLink> Links;
	TArray<FCadImportJoint> Joints;
};

struct FCadImportResult
{
	FString BlueprintAssetPath;
	TMap<FString, TArray<FString>> MeshAssetsByLink;
	TArray<FString> ImportedMeshAssetPaths;
};

// Master JSON references direct children of a level-placed master actor.
// Type decisions and per-child authoring are split into child JSON files.
struct FCadMasterChildEntry
{
	FString ActorName;
	FString ActorPath;
	FTransform RelativeTransform = FTransform::Identity;
	ECadMasterChildActorType ActorType = ECadMasterChildActorType::Static;
	FString ChildJsonFileName;
};

struct FCadMasterJsonDocument
{
	FString MasterName;
	FString MasterActorPath;
	FTransform MasterWorldTransform = FTransform::Identity;
	FString WorkspaceFolder;
	FString ChildJsonFolderName;
	FString ContentRootPath;
	TArray<FCadMasterChildEntry> Children;
};

// Child JSON captures per-child editable data. Movable entries can define
// one or more joints through this schema.
struct FCadChildVisualEntry
{
	FString MeshPath;
	FTransform RelativeTransform = FTransform::Identity;
	FString MaterialPath;
	FString MaterialName;
};

struct FCadChildJointTemplate
{
	FString JointName;
	ECadImportJointType JointType = ECadImportJointType::Fixed;
	FString ParentActorName;
	FString ChildActorName;
	FVector Axis = FVector::UpVector;
	FCadImportJointLimit Limit;
};

struct FCadChildLinkTemplate
{
	FString LinkName;
	FTransform RelativeTransform = FTransform::Identity;
	TArray<FCadChildVisualEntry> Visuals;
};

struct FCadChildJsonDocument
{
	FString MasterName;
	FString ChildActorName;
	FString SourceActorPath;
	ECadMasterChildActorType ActorType = ECadMasterChildActorType::Static;
	FTransform RelativeTransform = FTransform::Identity;
	FCadImportPhysics Physics;
	TArray<FCadChildLinkTemplate> Links;
	TArray<FCadChildVisualEntry> Visuals;
	TArray<FCadChildJointTemplate> Joints;
};

// Input bundle used by workflow backend when loading and building
// from a master document and its generated child JSON files.
struct FCadMasterWorkflowBuildInput
{
	FString WorkspaceFolder;
	FString MasterJsonPath;
	FString ChildJsonFolderPath;
	FString ContentRootPath;
};
