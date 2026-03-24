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

struct FCadImportModel
{
	ECadImportModelProfile Profile = ECadImportModelProfile::DynamicRobot;
	FString RobotName;
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
