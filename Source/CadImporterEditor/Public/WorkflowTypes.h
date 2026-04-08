#pragma once

#include "CoreMinimal.h"
#include "ImportModelTypes.h"

enum class ECadMasterChildActorType : uint8
{
	None,
	Static,
	Background,
	Movable
};

inline bool CadMasterChildActorTypeShouldGenerateJson(const ECadMasterChildActorType ActorType)
{
	return ActorType != ECadMasterChildActorType::None;
}

enum class ECadMasterNodeType : uint8
{
	Static,
	Background,
	Robot,
	Master
};

inline ECadMasterNodeType CadMasterNodeTypeFromChildActorType(const ECadMasterChildActorType ActorType)
{
	switch (ActorType)
	{
	case ECadMasterChildActorType::Background:
		return ECadMasterNodeType::Background;
	case ECadMasterChildActorType::Movable:
		return ECadMasterNodeType::Robot;
	case ECadMasterChildActorType::None:
	case ECadMasterChildActorType::Static:
	default:
		return ECadMasterNodeType::Static;
	}
}

inline ECadMasterChildActorType CadMasterChildActorTypeFromNodeType(const ECadMasterNodeType NodeType)
{
	switch (NodeType)
	{
	case ECadMasterNodeType::Background:
		return ECadMasterChildActorType::Background;
	case ECadMasterNodeType::Robot:
		return ECadMasterChildActorType::Movable;
	case ECadMasterNodeType::Master:
		return ECadMasterChildActorType::None;
	case ECadMasterNodeType::Static:
	default:
		return ECadMasterChildActorType::Static;
	}
}

inline bool CadMasterNodeTypeUsesChildJson(const ECadMasterNodeType NodeType)
{
	return NodeType != ECadMasterNodeType::Master;
}

inline bool CadMasterNodeTypeAllowsChildren(const ECadMasterNodeType NodeType)
{
	return NodeType == ECadMasterNodeType::Master;
}

struct FCadChildEntry
{
	FString ActorName;
	FString ActorPath;
	FTransform RelativeTransform = FTransform::Identity;
	ECadMasterChildActorType ActorType = ECadMasterChildActorType::Static;
	FString ChildJsonFileName;
};

struct FCadMasterHierarchyNode
{
	FString ActorName;
	FString ActorPath;
	FTransform RelativeTransform = FTransform::Identity;
	ECadMasterNodeType NodeType = ECadMasterNodeType::Static;
	FString ChildJsonFileName;
	FString MasterJsonFileName;
	FString ChildJsonFolderName;
	TArray<FCadMasterHierarchyNode> Children;
};

struct FCadMasterDoc
{
	FString MasterName;
	FString MasterActorPath;
	FTransform MasterWorldTransform = FTransform::Identity;
	FString WorkspaceFolder;
	FString ChildJsonFolderName;
	FString ContentRootPath;
	TArray<FCadMasterHierarchyNode> HierarchyChildren;
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
