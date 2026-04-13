#include "Misc/AutomationTest.h"
#include "LevelReplacer.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCadLevelReplacerBuildPlanFailsWithoutValidMaster,
	"CadImporterEditor.LevelReplacer.BuildPlanFailsWithoutValidMaster",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCadLevelReplacerBuildPlanFailsWithoutValidMaster::RunTest(const FString& Parameters)
{
	FCadMasterDoc MasterDocument;
	MasterDocument.MasterActorPath = TEXT("/Invalid/Actor.Path");
	MasterDocument.MasterName = TEXT("InvalidMaster");
	MasterDocument.WorkspaceFolder = TEXT("C:/InvalidWorkspace");
	MasterDocument.ChildJsonFolderName = TEXT("Children");

	TMap<FString, UBlueprint*> MasterBlueprintsByJsonPath;
	TMap<FString, UBlueprint*> ChildBlueprintsByJsonPath;
	FCadLevelReplacePlan Plan;
	FString Error;

	const bool bBuilt = CadLevelReplacer::TryBuildReplacementPlan(
		MasterDocument,
		MasterBlueprintsByJsonPath,
		ChildBlueprintsByJsonPath,
		Plan,
		Error);

	TestFalse(TEXT("Build plan fails for invalid master actor path"), bBuilt);
	TestTrue(TEXT("Error is populated on failure"), !Error.TrimStartAndEnd().IsEmpty());
	TestTrue(TEXT("Plan remains empty on failure"), Plan.CandidateDeleteActorPaths.Num() == 0);
	TestEqual(TEXT("Delete actor count defaults to zero on failure"), Plan.CandidateDeleteActorCount, 0);
	TestEqual(TEXT("Preserved child count defaults to zero on failure"), Plan.PreservedDirectChildCount, 0);
	TestFalse(TEXT("No destructive change flag on failure"), Plan.bHasDestructiveChanges);

	return true;
}

#endif
