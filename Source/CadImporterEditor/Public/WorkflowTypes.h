#pragma once

#include "CoreMinimal.h"
#include "ImportModelTypes.h"

enum class ECadMasterChildActorType : uint8
{
	Static,
	Movable
};

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

struct FCadMasterWorkflowBuildInput
{
	FString WorkspaceFolder;
	FString MasterJsonPath;
	FString ChildJsonFolderPath;
	FString ContentRootPath;
};
