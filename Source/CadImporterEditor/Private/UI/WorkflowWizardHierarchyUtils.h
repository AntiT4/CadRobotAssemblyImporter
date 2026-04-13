#pragma once

#include "CoreMinimal.h"
#include "UI/WorkflowWizard.h"

namespace CadWorkflowWizardHierarchyUtils
{
	FString ToWorkflowNodeTypeLabel(ECadMasterChildActorType ActorType);
	bool TryParseWorkflowNodeTypeLabel(const FString& SelectedType, ECadMasterChildActorType& OutType);
	bool IsHierarchyNodePathFlattenable(const FCadMasterHierarchyNode& Node);

	void BuildEditableHierarchyNodeRecursive(
		const FCadMasterHierarchyNode& SourceNode,
		const TSet<FString>& ForcedMasterPaths,
		const TSet<FString>& BranchPathsTreatedAsNone,
		const TMap<FString, ECadMasterChildActorType>& ExistingLeafTypesByPath,
		SCadWorkflowWizard::FEditableHierarchyNode& OutNode);

	void FlattenEditableHierarchyNodeRecursive(
		const SCadWorkflowWizard::FEditableHierarchyNode& Node,
		const FTransform& ParentAccumulatedTransform,
		TArray<FCadChildEntry>& OutChildEntries);

	void AppendSelectionHierarchyNodesRecursive(
		const SCadWorkflowWizard::FEditableHierarchyNode& EditableNode,
		const FTransform& ParentAccumulatedTransform,
		TArray<FCadMasterHierarchyNode>& OutNodes);

	bool SetEditableNodeLeafTypeRecursive(
		TArray<SCadWorkflowWizard::FEditableHierarchyNode>& Nodes,
		const FString& ActorPath,
		ECadMasterChildActorType LeafType);
}
