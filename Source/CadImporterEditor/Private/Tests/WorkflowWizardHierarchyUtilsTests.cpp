#include "Misc/AutomationTest.h"
#include "UI/WorkflowWizardHierarchyUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCadWorkflowWizardNodeTypeLabelRoundTrip,
	"CadImporterEditor.UI.WorkflowWizardHierarchyUtils.NodeTypeLabelRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCadWorkflowWizardNodeTypeLabelRoundTrip::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Movable label"), CadWorkflowWizardHierarchyUtils::ToWorkflowNodeTypeLabel(ECadMasterChildActorType::Movable), FString(TEXT("movable")));
	TestEqual(TEXT("Background label"), CadWorkflowWizardHierarchyUtils::ToWorkflowNodeTypeLabel(ECadMasterChildActorType::Background), FString(TEXT("background")));

	ECadMasterChildActorType ParsedType = ECadMasterChildActorType::None;
	TestTrue(TEXT("Parse robot alias"), CadWorkflowWizardHierarchyUtils::TryParseWorkflowNodeTypeLabel(TEXT("robot"), ParsedType));
	TestEqual(TEXT("Robot alias resolves movable"), ParsedType, ECadMasterChildActorType::Movable);

	TestTrue(TEXT("Parse static"), CadWorkflowWizardHierarchyUtils::TryParseWorkflowNodeTypeLabel(TEXT("static"), ParsedType));
	TestEqual(TEXT("Static parse result"), ParsedType, ECadMasterChildActorType::Static);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCadWorkflowWizardSetLeafTypeRecursive,
	"CadImporterEditor.UI.WorkflowWizardHierarchyUtils.SetLeafTypeRecursive",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCadWorkflowWizardSetLeafTypeRecursive::RunTest(const FString& Parameters)
{
	SCadWorkflowWizard::FEditableHierarchyNode RootNode;
	RootNode.ActorPath = TEXT("/Root");
	RootNode.bIsBranchNode = true;

	SCadWorkflowWizard::FEditableHierarchyNode LeafNode;
	LeafNode.ActorPath = TEXT("/Root/Leaf");
	LeafNode.bIsBranchNode = false;
	LeafNode.LeafType = ECadMasterChildActorType::Static;
	LeafNode.bIncluded = true;
	RootNode.Children.Add(LeafNode);

	TArray<SCadWorkflowWizard::FEditableHierarchyNode> Roots;
	Roots.Add(RootNode);

	const bool bChanged = CadWorkflowWizardHierarchyUtils::SetEditableNodeLeafTypeRecursive(
		Roots,
		TEXT("/Root/Leaf"),
		ECadMasterChildActorType::None);

	TestTrue(TEXT("Leaf type update succeeds"), bChanged);
	TestEqual(TEXT("Leaf type updated to none"), Roots[0].Children[0].LeafType, ECadMasterChildActorType::None);
	TestFalse(TEXT("Leaf node excluded when type is none"), Roots[0].Children[0].bIncluded);

	return true;
}

#endif
