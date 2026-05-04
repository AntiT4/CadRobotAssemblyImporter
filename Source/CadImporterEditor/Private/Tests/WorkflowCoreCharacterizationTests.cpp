#include "Misc/AutomationTest.h"

#include "CadImportStringUtils.h"
#include "ChildDocExporter.h"
#include "ChildDocParser.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Workflow/WorkflowBuildInputResolver.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCadWorkflowBuildInputResolverPrefersExplicitInputs,
	"CadImporterEditor.Workflow.BuildInputResolver.PrefersExplicitInputs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCadWorkflowBuildInputResolverPrefersExplicitInputs::RunTest(const FString& Parameters)
{
	FCadWorkflowBuildInput BuildInput;
	BuildInput.WorkspaceFolder = TEXT("  C:/Workspace/Input  ");
	BuildInput.MasterJsonPath = TEXT(" C:/Input/master.json ");
	BuildInput.ChildJsonFolderPath = TEXT(" C:/Input/Children ");
	BuildInput.ContentRootPath = TEXT(" /Game/InputRoot ");

	FCadMasterDoc MasterDocument;
	MasterDocument.WorkspaceFolder = TEXT("C:/Workspace/Document");
	MasterDocument.ChildJsonFolderName = TEXT("DocChildren");
	MasterDocument.ContentRootPath = TEXT("/Game/DocRoot");

	const FCadWorkflowBuildInput Resolved = CadWorkflowBuildInputResolver::Resolve(BuildInput, MasterDocument);

	TestEqual(TEXT("Master json path uses normalized explicit input"), Resolved.MasterJsonPath, FString(TEXT("C:/Input/master.json")));
	TestEqual(TEXT("Workspace uses explicit input when present"), Resolved.WorkspaceFolder, FString(TEXT("C:/Workspace/Input")));
	TestEqual(TEXT("Child json folder uses explicit input when present"), Resolved.ChildJsonFolderPath, FString(TEXT("C:/Input/Children")));
	TestEqual(TEXT("Content root uses explicit input when present"), Resolved.ContentRootPath, FString(TEXT("/Game/InputRoot")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCadWorkflowBuildInputResolverFallsBackToDocument,
	"CadImporterEditor.Workflow.BuildInputResolver.FallsBackToDocument",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCadWorkflowBuildInputResolverFallsBackToDocument::RunTest(const FString& Parameters)
{
	FCadWorkflowBuildInput BuildInput;
	BuildInput.MasterJsonPath = TEXT("C:/Workspace/master.json");

	FCadMasterDoc MasterDocument;
	MasterDocument.WorkspaceFolder = TEXT("  C:/Workspace/Document  ");
	MasterDocument.ChildJsonFolderName = TEXT("Children");
	MasterDocument.ContentRootPath = TEXT("/Game/DocRoot");

	const FCadWorkflowBuildInput Resolved = CadWorkflowBuildInputResolver::Resolve(BuildInput, MasterDocument);

	TestEqual(TEXT("Workspace falls back to document value"), Resolved.WorkspaceFolder, FString(TEXT("C:/Workspace/Document")));
	TestEqual(TEXT("Child folder combines workspace and document child folder"), Resolved.ChildJsonFolderPath, FString(TEXT("C:/Workspace/Document/Children")));
	TestEqual(TEXT("Content root falls back to document value"), Resolved.ContentRootPath, FString(TEXT("/Game/DocRoot")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCadImportStringUtilsRoundTripAndAliases,
	"CadImporterEditor.Workflow.StringUtils.RoundTripAndAliases",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCadImportStringUtilsRoundTripAndAliases::RunTest(const FString& Parameters)
{
	ECadMasterChildActorType ParsedChildType = ECadMasterChildActorType::None;
	TestTrue(TEXT("Parse child actor type movable"), CadImportStringUtils::TryParseMasterChildActorTypeString(TEXT("movable"), ParsedChildType, false));
	TestEqual(TEXT("Parsed child actor type movable"), ParsedChildType, ECadMasterChildActorType::Movable);
	TestEqual(TEXT("Serialize child actor type movable"), CadImportStringUtils::ToMasterChildActorTypeString(ECadMasterChildActorType::Movable), FString(TEXT("movable")));

	ECadMasterNodeType ParsedNodeType = ECadMasterNodeType::Static;
	TestTrue(TEXT("Parse node type alias movable as robot"), CadImportStringUtils::TryParseMasterNodeTypeString(TEXT("movable"), ParsedNodeType));
	TestEqual(TEXT("Parsed node type alias result"), ParsedNodeType, ECadMasterNodeType::Robot);
	TestEqual(TEXT("Serialize node type robot"), CadImportStringUtils::ToMasterNodeTypeString(ECadMasterNodeType::Robot), FString(TEXT("robot")));

	ECadImportJointType ParsedJointType = ECadImportJointType::Fixed;
	TestTrue(TEXT("Parse joint type revolute"), CadImportStringUtils::TryParseJointTypeString(TEXT("revolute"), ParsedJointType));
	TestEqual(TEXT("Parsed joint type revolute"), ParsedJointType, ECadImportJointType::Revolute);
	TestEqual(TEXT("Serialize joint type revolute"), CadImportStringUtils::ToJointTypeString(ECadImportJointType::Revolute), FString(TEXT("revolute")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCadChildDocSerializeParseRoundTrip,
	"CadImporterEditor.Workflow.ChildDoc.SerializeParseRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCadChildDocSerializeParseRoundTrip::RunTest(const FString& Parameters)
{
	FCadChildDoc SourceDocument;
	SourceDocument.MasterName = TEXT("MasterA");
	SourceDocument.ChildActorName = TEXT("ChildA");
	SourceDocument.SourceActorPath = TEXT("/Root/ChildA");
	SourceDocument.ActorType = ECadMasterChildActorType::Movable;
	SourceDocument.RelativeTransform.SetLocation(FVector(1.0, 2.0, 3.0));
	SourceDocument.Physics.Mass = 5.0f;
	SourceDocument.Physics.bSimulatePhysics = true;

	FCadChildJointDef JointDef;
	JointDef.JointName = TEXT("joint_1");
	JointDef.JointType = ECadImportJointType::Revolute;
	JointDef.ParentActorName = TEXT("base");
	JointDef.ChildActorName = TEXT("link_1");
	JointDef.Axis = FVector(0.0, 0.0, 1.0);
	SourceDocument.Joints.Add(JointDef);

	FString JsonText;
	FString Error;
	TestTrue(TEXT("Serialize child doc succeeds"), CadChildDocExporter::TrySerializeChildDocument(SourceDocument, JsonText, Error));
	TestTrue(TEXT("Serialized json is not empty"), !JsonText.IsEmpty());

	const FString TempDir = FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("CadImporterEditorTests"));
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	PlatformFile.CreateDirectoryTree(*TempDir);
	const FString TempFilePath = FPaths::Combine(TempDir, TEXT("child_round_trip.json"));
	TestTrue(TEXT("Write temp json succeeds"), FFileHelper::SaveStringToFile(JsonText, *TempFilePath));

	FCadChildDoc ParsedDocument;
	TestTrue(TEXT("Parse serialized child doc succeeds"), CadChildDocParser::TryLoadChildDocumentFromJsonPath(TempFilePath, ParsedDocument, Error));
	TestEqual(TEXT("Round-trip child name is preserved"), ParsedDocument.ChildActorName, SourceDocument.ChildActorName);
	TestEqual(TEXT("Round-trip actor type is preserved"), ParsedDocument.ActorType, SourceDocument.ActorType);
	TestEqual(TEXT("Round-trip joint count is preserved"), ParsedDocument.Joints.Num(), SourceDocument.Joints.Num());
	TestEqual(TEXT("Round-trip first joint type is preserved"), ParsedDocument.Joints[0].JointType, SourceDocument.Joints[0].JointType);

	PlatformFile.DeleteFile(*TempFilePath);
	return true;
}

#endif
