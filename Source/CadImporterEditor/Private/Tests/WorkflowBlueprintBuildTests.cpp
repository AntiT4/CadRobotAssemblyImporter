#include "Misc/AutomationTest.h"
#include "Workflow/WorkflowBlueprintBuild.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCadWorkflowCollectDirectLeafChildrenUsesHierarchyWhenPresent,
	"CadImporterEditor.Workflow.CollectDirectLeafChildren.UsesHierarchyWhenPresent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCadWorkflowCollectDirectLeafChildrenUsesHierarchyWhenPresent::RunTest(const FString& Parameters)
{
	FCadMasterDoc MasterDocument;

	FCadMasterHierarchyNode StaticNode;
	StaticNode.ActorName = TEXT("Base");
	StaticNode.ActorPath = TEXT("/Root/Base");
	StaticNode.NodeType = ECadMasterNodeType::Static;
	StaticNode.ChildJsonFileName = TEXT("base.json");
	StaticNode.RelativeTransform.SetLocation(FVector(10.0, 20.0, 30.0));

	FCadMasterHierarchyNode BackgroundNode;
	BackgroundNode.ActorName = TEXT("Backdrop");
	BackgroundNode.ActorPath = TEXT("/Root/Backdrop");
	BackgroundNode.NodeType = ECadMasterNodeType::Background;
	BackgroundNode.ChildJsonFileName = TEXT("backdrop.json");

	FCadMasterHierarchyNode MasterNode;
	MasterNode.ActorName = TEXT("Nested");
	MasterNode.NodeType = ECadMasterNodeType::Master;
	MasterNode.ChildJsonFileName = TEXT("should_not_be_used.json");

	MasterDocument.HierarchyChildren = { StaticNode, BackgroundNode, MasterNode };

	TArray<FCadChildEntry> OutChildren;
	CadWorkflowBlueprintBuild::CollectDirectLeafChildrenForBuild(MasterDocument, OutChildren);

	TestEqual(TEXT("Only non-master hierarchy nodes are collected"), OutChildren.Num(), 2);
	TestEqual(TEXT("First child actor name is copied"), OutChildren[0].ActorName, StaticNode.ActorName);
	TestEqual(TEXT("First child actor path is copied"), OutChildren[0].ActorPath, StaticNode.ActorPath);
	TestEqual(TEXT("First child type maps to Static"), OutChildren[0].ActorType, ECadMasterChildActorType::Static);
	TestEqual(TEXT("First child relative transform is copied"), OutChildren[0].RelativeTransform.GetLocation(), StaticNode.RelativeTransform.GetLocation());
	TestEqual(TEXT("Second child type maps to Background"), OutChildren[1].ActorType, ECadMasterChildActorType::Background);
	TestEqual(TEXT("Second child json file is copied"), OutChildren[1].ChildJsonFileName, BackgroundNode.ChildJsonFileName);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCadWorkflowCollectDirectLeafChildrenFallsBackToLegacyChildren,
	"CadImporterEditor.Workflow.CollectDirectLeafChildren.FallsBackToLegacyChildren",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCadWorkflowCollectDirectLeafChildrenFallsBackToLegacyChildren::RunTest(const FString& Parameters)
{
	FCadMasterDoc MasterDocument;

	FCadChildEntry LegacyChild;
	LegacyChild.ActorName = TEXT("LegacyChild");
	LegacyChild.ActorPath = TEXT("/Root/LegacyChild");
	LegacyChild.ActorType = ECadMasterChildActorType::Movable;
	LegacyChild.ChildJsonFileName = TEXT("legacy_child.json");
	MasterDocument.Children.Add(LegacyChild);

	TArray<FCadChildEntry> OutChildren;
	CadWorkflowBlueprintBuild::CollectDirectLeafChildrenForBuild(MasterDocument, OutChildren);

	TestEqual(TEXT("Legacy child list is preserved when hierarchy is empty"), OutChildren.Num(), 1);
	TestEqual(TEXT("Legacy actor name is preserved"), OutChildren[0].ActorName, LegacyChild.ActorName);
	TestEqual(TEXT("Legacy actor type is preserved"), OutChildren[0].ActorType, LegacyChild.ActorType);
	TestEqual(TEXT("Legacy json file is preserved"), OutChildren[0].ChildJsonFileName, LegacyChild.ChildJsonFileName);

	return true;
}

#endif
