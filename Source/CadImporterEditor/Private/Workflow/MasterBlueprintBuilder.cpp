#include "Workflow/MasterBlueprintBuilder.h"

#include "CadMasterActor.h"
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"

namespace
{
	FString BuildMasterBlueprintPackagePath(const FCadMasterDoc& MasterDocument, const FCadWorkflowBuildInput& BuildInput)
	{
		const FString SafeMasterName = FPaths::MakeValidFileName(MasterDocument.MasterName).IsEmpty()
			? TEXT("CadMaster")
			: FPaths::MakeValidFileName(MasterDocument.MasterName);

		FString ContentRootPath = BuildInput.ContentRootPath.TrimStartAndEnd();
		if (ContentRootPath.IsEmpty())
		{
			ContentRootPath = MasterDocument.ContentRootPath.TrimStartAndEnd();
		}
		if (ContentRootPath.IsEmpty())
		{
			ContentRootPath = FString::Printf(TEXT("/Game/%s"), *SafeMasterName);
		}

		return FString::Printf(TEXT("%s/BP_%s_Master"), *ContentRootPath, *SafeMasterName);
	}

	ECadMasterPlacementNodeType ToMasterBlueprintPlacementNodeType(const ECadMasterNodeType NodeType)
	{
		switch (NodeType)
		{
		case ECadMasterNodeType::Background:
			return ECadMasterPlacementNodeType::Background;
		case ECadMasterNodeType::Robot:
			return ECadMasterPlacementNodeType::Robot;
		case ECadMasterNodeType::Master:
			return ECadMasterPlacementNodeType::Master;
		case ECadMasterNodeType::Static:
		default:
			return ECadMasterPlacementNodeType::Static;
		}
	}

	void AppendMasterBlueprintPlacementNodesRecursive(
		const FCadMasterHierarchyNode& SourceNode,
		const int32 ParentNodeIndex,
		TArray<FCadMasterPlacementNodeRecord>& OutNodes)
	{
		FCadMasterPlacementNodeRecord PlacementNode;
		PlacementNode.NodeName = SourceNode.ActorName;
		PlacementNode.NodeType = ToMasterBlueprintPlacementNodeType(SourceNode.NodeType);
		PlacementNode.ChildJsonFileName = SourceNode.ChildJsonFileName;
		PlacementNode.MasterJsonFileName = SourceNode.MasterJsonFileName;
		PlacementNode.RelativeTransform = SourceNode.RelativeTransform;
		PlacementNode.ParentNodeIndex = ParentNodeIndex;
		const int32 CurrentNodeIndex = OutNodes.Add(MoveTemp(PlacementNode));
		for (const FCadMasterHierarchyNode& ChildNode : SourceNode.Children)
		{
			AppendMasterBlueprintPlacementNodesRecursive(ChildNode, CurrentNodeIndex, OutNodes);
		}
	}

	FCadMasterHierarchyNode BuildMasterBlueprintHierarchyNodeFromChildEntry(const FCadChildEntry& ChildEntry)
	{
		FCadMasterHierarchyNode HierarchyNode;
		HierarchyNode.ActorName = ChildEntry.ActorName;
		HierarchyNode.ActorPath = ChildEntry.ActorPath;
		HierarchyNode.RelativeTransform = ChildEntry.RelativeTransform;
		HierarchyNode.NodeType = CadMasterNodeTypeFromChildActorType(ChildEntry.ActorType);
		HierarchyNode.ChildJsonFileName = ChildEntry.ChildJsonFileName;
		return HierarchyNode;
	}
}

namespace CadMasterBlueprintBuilder
{
	bool TryBuildBlueprint(
		const FCadMasterDoc& MasterDocument,
		const FCadWorkflowBuildInput& BuildInput,
		UBlueprint*& OutBlueprint,
		FString& OutError)
	{
		OutBlueprint = nullptr;
		OutError.Reset();

		const FString BlueprintPackagePath = BuildMasterBlueprintPackagePath(MasterDocument, BuildInput);
		const FString BlueprintAssetName = FPackageName::GetLongPackageAssetName(BlueprintPackagePath);
		if (BlueprintAssetName.IsEmpty())
		{
			OutError = FString::Printf(TEXT("Invalid master blueprint package path: %s"), *BlueprintPackagePath);
			return false;
		}

		const FString BlueprintObjectPath = FString::Printf(TEXT("%s.%s"), *BlueprintPackagePath, *BlueprintAssetName);
		UBlueprint* MasterBlueprint = LoadObject<UBlueprint>(nullptr, *BlueprintObjectPath);
		if (!MasterBlueprint)
		{
			UPackage* Package = CreatePackage(*BlueprintPackagePath);
			if (!Package)
			{
				OutError = FString::Printf(TEXT("Failed to create package for master blueprint: %s"), *BlueprintPackagePath);
				return false;
			}

			MasterBlueprint = FKismetEditorUtilities::CreateBlueprint(
				ACadMasterActor::StaticClass(),
				Package,
				FName(*BlueprintAssetName),
				BPTYPE_Normal,
				UBlueprint::StaticClass(),
				UBlueprintGeneratedClass::StaticClass(),
				FName(TEXT("CadImporter")));
			if (!MasterBlueprint)
			{
				OutError = FString::Printf(TEXT("Failed to create master blueprint asset: %s"), *BlueprintPackagePath);
				return false;
			}
		}
		else if (MasterBlueprint->ParentClass != ACadMasterActor::StaticClass())
		{
			MasterBlueprint->ParentClass = ACadMasterActor::StaticClass();
			FBlueprintEditorUtils::RefreshAllNodes(MasterBlueprint);
		}

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(MasterBlueprint);
		FKismetEditorUtilities::CompileBlueprint(MasterBlueprint);
		if (!MasterBlueprint->GeneratedClass)
		{
			OutError = FString::Printf(TEXT("Master blueprint generated class is invalid: %s"), *BlueprintObjectPath);
			return false;
		}

		ACadMasterActor* MasterDefaultObject = Cast<ACadMasterActor>(MasterBlueprint->GeneratedClass->GetDefaultObject());
		if (!MasterDefaultObject)
		{
			OutError = FString::Printf(TEXT("Master blueprint default object cast failed: %s"), *BlueprintObjectPath);
			return false;
		}

		MasterDefaultObject->Metadata.MasterName = MasterDocument.MasterName;
		MasterDefaultObject->Metadata.WorkspaceFolder = BuildInput.WorkspaceFolder;
		MasterDefaultObject->Metadata.Description = FString::Printf(
			TEXT("Generated from master json: %s"),
			*BuildInput.MasterJsonPath);
		MasterDefaultObject->Metadata.SchemaVersion = TEXT("master_json_v2");
		MasterDefaultObject->HierarchyNodes.Reset();
		MasterDefaultObject->ChildPlacements.Reset();

		if (MasterDocument.HierarchyChildren.Num() > 0)
		{
			for (const FCadMasterHierarchyNode& HierarchyNode : MasterDocument.HierarchyChildren)
			{
				AppendMasterBlueprintPlacementNodesRecursive(HierarchyNode, INDEX_NONE, MasterDefaultObject->HierarchyNodes);
			}
		}
		else
		{
			for (const FCadChildEntry& ChildEntry : MasterDocument.Children)
			{
				AppendMasterBlueprintPlacementNodesRecursive(BuildMasterBlueprintHierarchyNodeFromChildEntry(ChildEntry), INDEX_NONE, MasterDefaultObject->HierarchyNodes);
			}
		}

		for (const FCadChildEntry& ChildEntry : MasterDocument.Children)
		{
			FCadMasterChildPlacement Placement;
			Placement.ChildName = ChildEntry.ActorName;
			Placement.ChildJsonFileName = ChildEntry.ChildJsonFileName;
			Placement.RelativeTransform = ChildEntry.RelativeTransform;
			Placement.bMovable = (ChildEntry.ActorType == ECadMasterChildActorType::Movable);
			MasterDefaultObject->ChildPlacements.Add(MoveTemp(Placement));
		}

		MasterBlueprint->MarkPackageDirty();
		OutBlueprint = MasterBlueprint;
		return true;
	}
}
