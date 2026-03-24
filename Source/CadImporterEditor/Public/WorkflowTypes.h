#pragma once

#include "CoreMinimal.h"
#include "ImportModelTypes.h"

enum class ECadMasterChildActorType : uint8
{
	Static,
	Movable
};

struct FCadChildEntry
{
	FString ActorName;
	FString ActorPath;
	FTransform RelativeTransform = FTransform::Identity;
	ECadMasterChildActorType ActorType = ECadMasterChildActorType::Static;
	FString ChildJsonFileName;
};

struct FCadMasterDoc
{
	FString MasterName;
	FString MasterActorPath;
	FTransform MasterWorldTransform = FTransform::Identity;
	FString WorkspaceFolder;
	FString ChildJsonFolderName;
	FString ContentRootPath;
	TArray<FCadChildEntry> Children;
};

struct FCadChildVisual
{
	FString MeshPath;
	FTransform RelativeTransform = FTransform::Identity;
	FString MaterialPath;
	FString MaterialName;
};

struct FCadChildJointDef
{
	FString JointName;
	ECadImportJointType JointType = ECadImportJointType::Fixed;
	FString ParentActorName;
	FString ChildActorName;
	FVector Axis = FVector::UpVector;
	FCadImportJointLimit Limit;
};

struct FCadChildLinkDef
{
	FString LinkName;
	FTransform RelativeTransform = FTransform::Identity;
	TArray<FCadChildVisual> Visuals;
};

struct FCadChildDoc
{
	FString MasterName;
	FString ChildActorName;
	FString SourceActorPath;
	ECadMasterChildActorType ActorType = ECadMasterChildActorType::Static;
	FTransform RelativeTransform = FTransform::Identity;
	FCadImportPhysics Physics;
	TArray<FCadChildLinkDef> Links;
	TArray<FCadChildVisual> Visuals;
	TArray<FCadChildJointDef> Joints;
};

struct FCadWorkflowBuildInput
{
	FString WorkspaceFolder;
	FString MasterJsonPath;
	FString ChildJsonFolderPath;
	FString ContentRootPath;
};
