#include "UI/WorkflowWizard.h"

#include "UI/WorkflowWizardHierarchyUtils.h"
#include "UI/WorkflowWizardSharedUtils.h"
#include "Editor/ActorHierarchyUtils.h"

using namespace CadWorkflowWizardHierarchyUtils;

void SCadWorkflowWizard::RefreshFlattenBranchCandidates()
{
	FlattenBranchStats.Reset();
	FlattenBranchSelections.Reset();
	FlattenableChildActorPaths.Reset();

	const AActor* MasterActor = ConfirmedSelection.MasterActor.Get();
	if (!MasterActor)
	{
		FlattenPreviewText = TEXT("Confirm master actor first.");
		RebuildFlattenRows();
		return;
	}

	CadActorHierarchyUtils::AnalyzeDirectChildBranches(const_cast<AActor*>(MasterActor), FlattenBranchStats);
	FlattenBranchSelections.Init(false, FlattenBranchStats.Num());
	for (const FCadHierarchyBranchStats& BranchStat : FlattenBranchStats)
	{
		if (BranchStat.bCanFlattenOneLevel)
		{
			FlattenableChildActorPaths.Add(BranchStat.BranchPath);
		}
	}
	const FString BranchTableText = CadWorkflowWizardShared::BuildBranchDepthTableText(FlattenBranchStats);
	FlattenPreviewText = FString::Printf(
		TEXT("Master:\t%s\nPath:\t%s\n\n%s"),
		*MasterActor->GetActorNameOrLabel(),
		*MasterActor->GetPathName(),
		*BranchTableText);
	RebuildFlattenRows();
}

void SCadWorkflowWizard::RebuildChildEntriesFromFlattenPreview()
{
	ChildEntries.Reset();
	ChildEntryIndexByPath.Reset();
	PreviewPromotedChildActorPaths.Reset();
	FlattenableChildActorPaths.Reset();
	EditableHierarchyRoots.Reset();

	if (!ConfirmedSelection.MasterActor.IsValid())
	{
		return;
	}

	RebuildEditableHierarchyPreview(LeafTypeOverridesByPath);

	TFunction<void(const FEditableHierarchyNode&)> CollectFlattenablePathsRecursive;
	CollectFlattenablePathsRecursive = [this, &CollectFlattenablePathsRecursive](const FEditableHierarchyNode& Node)
	{
		if (Node.bCanPromoteToMaster)
		{
			FlattenableChildActorPaths.Add(Node.ActorPath);
		}

		for (const FEditableHierarchyNode& ChildNode : Node.Children)
		{
			CollectFlattenablePathsRecursive(ChildNode);
		}
	};

	for (const FEditableHierarchyNode& HierarchyRoot : EditableHierarchyRoots)
	{
		CollectFlattenablePathsRecursive(HierarchyRoot);
	}

	for (const FEditableHierarchyNode& HierarchyRoot : EditableHierarchyRoots)
	{
		FlattenEditableHierarchyNodeRecursive(HierarchyRoot, FTransform::Identity, ChildEntries);
	}

	for (int32 ChildIndex = 0; ChildIndex < ChildEntries.Num(); ++ChildIndex)
	{
		const FCadChildEntry& ChildEntry = ChildEntries[ChildIndex];
		ChildEntryIndexByPath.Add(ChildEntry.ActorPath, ChildIndex);
		AActor* ChildActor = CadActorHierarchyUtils::FindByPath(ChildEntry.ActorPath);
		if (CadActorHierarchyUtils::CanActorFlattenOneLevel(ChildActor))
		{
			FlattenableChildActorPaths.Add(ChildEntry.ActorPath);
		}
	}
}

void SCadWorkflowWizard::RebuildEditableHierarchyPreview(const TMap<FString, ECadMasterChildActorType>& ExistingLeafTypesByPath)
{
	EditableHierarchyRoots.Reset();

	for (const FCadMasterHierarchyNode& HierarchyRoot : ConfirmedSelection.HierarchyChildren)
	{
		FEditableHierarchyNode EditableRoot;
		BuildEditableHierarchyNodeRecursive(
			HierarchyRoot,
			VirtualMasterBranchPaths,
			BranchPathsTreatedAsNone,
			ExistingLeafTypesByPath,
			EditableRoot);
		EditableHierarchyRoots.Add(MoveTemp(EditableRoot));
	}
}

void SCadWorkflowWizard::SetChildType(const FString& ActorPath, const FString& SelectedType)
{
	if (ActorPath.TrimStartAndEnd().IsEmpty())
	{
		return;
	}

	ECadMasterChildActorType ParsedType = ECadMasterChildActorType::Static;
	if (!TryParseWorkflowNodeTypeLabel(SelectedType, ParsedType))
	{
		return;
	}

	LeafTypeOverridesByPath.FindOrAdd(ActorPath) = ParsedType;
	SetEditableNodeLeafTypeRecursive(EditableHierarchyRoots, ActorPath, ParsedType);
	RebuildChildEntriesFromFlattenPreview();
	RebuildChildRows();
}

void SCadWorkflowWizard::SetFlattenBranchSelected(const int32 BranchIndex, const bool bSelected)
{
	if (!FlattenBranchSelections.IsValidIndex(BranchIndex))
	{
		return;
	}

	FlattenBranchSelections[BranchIndex] = bSelected;
}
