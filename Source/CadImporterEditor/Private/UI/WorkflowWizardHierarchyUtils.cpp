#include "UI/WorkflowWizardHierarchyUtils.h"

#include "CadMasterActor.h"
#include "CadImportStringUtils.h"
#include "Editor/ActorHierarchyUtils.h"
#include "Misc/Paths.h"

namespace CadWorkflowWizardHierarchyUtils
{
	FString ToWorkflowNodeTypeLabel(const ECadMasterChildActorType ActorType)
	{
		switch (ActorType)
		{
		case ECadMasterChildActorType::Movable:
			return TEXT("movable");
		case ECadMasterChildActorType::Background:
			return TEXT("background");
		case ECadMasterChildActorType::None:
			return TEXT("none");
		case ECadMasterChildActorType::Static:
		default:
			return TEXT("static");
		}
	}

	bool TryParseWorkflowNodeTypeLabel(const FString& SelectedType, ECadMasterChildActorType& OutType)
	{
		if (SelectedType.Equals(TEXT("robot"), ESearchCase::IgnoreCase))
		{
			OutType = ECadMasterChildActorType::Movable;
			return true;
		}

		return CadImportStringUtils::TryParseMasterChildActorTypeString(SelectedType, OutType, true);
	}

	bool IsHierarchyNodePathFlattenable(const FCadMasterHierarchyNode& Node)
	{
		AActor* NodeActor = CadActorHierarchyUtils::FindByPath(Node.ActorPath);
		return CadActorHierarchyUtils::CanActorFlattenOneLevel(NodeActor);
	}

	void BuildEditableHierarchyNodeRecursive(
		const FCadMasterHierarchyNode& SourceNode,
		const TSet<FString>& ForcedMasterPaths,
		const TSet<FString>& BranchPathsTreatedAsNone,
		const TMap<FString, ECadMasterChildActorType>& ExistingLeafTypesByPath,
		SCadWorkflowWizard::FEditableHierarchyNode& OutNode)
	{
		OutNode = SCadWorkflowWizard::FEditableHierarchyNode();
		OutNode.ActorName = SourceNode.ActorName;
		OutNode.ActorPath = SourceNode.ActorPath;
		OutNode.RelativeTransform = SourceNode.RelativeTransform;
		OutNode.bCanPromoteToMaster = IsHierarchyNodePathFlattenable(SourceNode);

		AActor* SourceActor = CadActorHierarchyUtils::FindByPath(SourceNode.ActorPath);
		const bool bForcedMaster = ForcedMasterPaths.Contains(SourceNode.ActorPath);
		const bool bIsExplicitMasterActor = SourceActor && SourceActor->IsA<ACadMasterActor>();
		OutNode.bIsBranchNode = bForcedMaster || bIsExplicitMasterActor;
		OutNode.bTreatAsMaster = OutNode.bIsBranchNode && !BranchPathsTreatedAsNone.Contains(SourceNode.ActorPath);
		OutNode.bIncluded = true;
		OutNode.LeafType = ECadMasterChildActorType::Static;

		if (const ECadMasterChildActorType* ExistingLeafType = ExistingLeafTypesByPath.Find(SourceNode.ActorPath))
		{
			OutNode.LeafType = *ExistingLeafType;
		}

		if (!OutNode.bIsBranchNode)
		{
			return;
		}

		for (const FCadMasterHierarchyNode& ChildNode : SourceNode.Children)
		{
			SCadWorkflowWizard::FEditableHierarchyNode EditableChildNode;
			BuildEditableHierarchyNodeRecursive(
				ChildNode,
				ForcedMasterPaths,
				BranchPathsTreatedAsNone,
				ExistingLeafTypesByPath,
				EditableChildNode);
			OutNode.Children.Add(MoveTemp(EditableChildNode));
		}
	}

	void FlattenEditableHierarchyNodeRecursive(
		const SCadWorkflowWizard::FEditableHierarchyNode& Node,
		const FTransform& ParentAccumulatedTransform,
		TArray<FCadChildEntry>& OutChildEntries)
	{
		const FTransform AccumulatedTransform = Node.RelativeTransform * ParentAccumulatedTransform;
		if (Node.bIsBranchNode)
		{
			for (const SCadWorkflowWizard::FEditableHierarchyNode& ChildNode : Node.Children)
			{
				FlattenEditableHierarchyNodeRecursive(ChildNode, AccumulatedTransform, OutChildEntries);
			}
			return;
		}

		if (!Node.bIncluded || !CadMasterChildActorTypeShouldGenerateJson(Node.LeafType))
		{
			return;
		}

		FCadChildEntry ChildEntry;
		ChildEntry.ActorName = Node.ActorName;
		ChildEntry.ActorPath = Node.ActorPath;
		ChildEntry.RelativeTransform = AccumulatedTransform;
		ChildEntry.ActorType = Node.LeafType;
		const FString SafeName = FPaths::MakeValidFileName(Node.ActorName);
		ChildEntry.ChildJsonFileName = FString::Printf(TEXT("%s.json"), SafeName.IsEmpty() ? TEXT("Child") : *SafeName);
		OutChildEntries.Add(MoveTemp(ChildEntry));
	}

	void AppendSelectionHierarchyNodesRecursive(
		const SCadWorkflowWizard::FEditableHierarchyNode& EditableNode,
		const FTransform& ParentAccumulatedTransform,
		TArray<FCadMasterHierarchyNode>& OutNodes)
	{
		const FTransform AccumulatedTransform = EditableNode.RelativeTransform * ParentAccumulatedTransform;
		if (EditableNode.bIsBranchNode && EditableNode.bTreatAsMaster)
		{
			FCadMasterHierarchyNode OutNode;
			OutNode.ActorName = EditableNode.ActorName;
			OutNode.ActorPath = EditableNode.ActorPath;
			OutNode.RelativeTransform = AccumulatedTransform;
			OutNode.NodeType = ECadMasterNodeType::Master;
			for (const SCadWorkflowWizard::FEditableHierarchyNode& EditableChildNode : EditableNode.Children)
			{
				AppendSelectionHierarchyNodesRecursive(EditableChildNode, FTransform::Identity, OutNode.Children);
			}
			OutNodes.Add(MoveTemp(OutNode));
			return;
		}

		if (EditableNode.bIsBranchNode)
		{
			for (const SCadWorkflowWizard::FEditableHierarchyNode& EditableChildNode : EditableNode.Children)
			{
				AppendSelectionHierarchyNodesRecursive(EditableChildNode, AccumulatedTransform, OutNodes);
			}
			return;
		}

		if (!EditableNode.bIncluded || !CadMasterChildActorTypeShouldGenerateJson(EditableNode.LeafType))
		{
			return;
		}

		FCadMasterHierarchyNode OutNode;
		OutNode.ActorName = EditableNode.ActorName;
		OutNode.ActorPath = EditableNode.ActorPath;
		OutNode.RelativeTransform = AccumulatedTransform;
		OutNode.NodeType = CadMasterNodeTypeFromChildActorType(EditableNode.LeafType);
		const FString SafeName = FPaths::MakeValidFileName(EditableNode.ActorName);
		OutNode.ChildJsonFileName = FString::Printf(TEXT("%s.json"), SafeName.IsEmpty() ? TEXT("Child") : *SafeName);
		OutNodes.Add(MoveTemp(OutNode));
	}

	bool SetEditableNodeLeafTypeRecursive(
		TArray<SCadWorkflowWizard::FEditableHierarchyNode>& Nodes,
		const FString& ActorPath,
		const ECadMasterChildActorType LeafType)
	{
		for (SCadWorkflowWizard::FEditableHierarchyNode& Node : Nodes)
		{
			if (Node.ActorPath == ActorPath && !Node.bIsBranchNode)
			{
				Node.LeafType = LeafType;
				Node.bIncluded = CadMasterChildActorTypeShouldGenerateJson(LeafType);
				return true;
			}

			if (SetEditableNodeLeafTypeRecursive(Node.Children, ActorPath, LeafType))
			{
				return true;
			}
		}

		return false;
	}
}
