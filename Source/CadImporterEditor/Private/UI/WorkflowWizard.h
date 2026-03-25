#pragma once

#include "CoreMinimal.h"
#include "LevelReplacer.h"
#include "MasterSelectionCollector.h"
#include "ChildDocExporter.h"
#include "MasterDocExporter.h"
#include "Widgets/SCompoundWidget.h"

class FCadImportService;
class SEditableTextBox;
class SVerticalBox;
class AActor;

class SCadWorkflowWizard : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCadWorkflowWizard)
		: _Runner(nullptr)
	{
	}
		SLATE_ARGUMENT(TSharedPtr<FCadImportService>, Runner)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	struct FEditableJointChildState
	{
		FCadChildEntry ChildEntry;
		FCadChildDoc ChildDocument;
		TArray<TSharedPtr<FString>> ParentLinkItems;
		TArray<TSharedPtr<FString>> ChildLinkItems;
		TArray<bool> JointDebugDrawEnabled;
	};

	struct FJointTestAnimationState
	{
		TWeakObjectPtr<AActor> TargetActor;
		FTransform OriginalRelativeTransform = FTransform::Identity;
		FVector LocalAxis = FVector::UpVector;
		ECadImportJointType JointType = ECadImportJointType::Fixed;
		float Lower = 0.0f;
		float Upper = 0.0f;
		float ElapsedSeconds = 0.0f;
	};

	EActiveTimerReturnType PollSelection(double CurrentTime, float DeltaTime);
	void RefreshSelectionPreview();
	void RebuildChildRows();
	void RebuildJointEditorRows();
	void LoadEditableJointDocuments(const FCadMasterSelection& SelectionForGeneration, const TArray<FCadChildDoc>& InChildDocuments);
	void ResetJointEditorState();
	EActiveTimerReturnType UpdateJointTestAnimation(double CurrentTime, float DeltaTime);
	bool TryBuildSelectionForGeneration(FCadMasterSelection& OutSelection, FString& OutError) const;
	bool TryBuildDryRunChildDocuments(const FCadMasterSelection& SelectionForGeneration, TArray<FCadChildDoc>& OutChildDocuments, FString& OutError);
	bool ValidateEditedJointDefinitions(FString& OutError) const;
	bool TryApplyEditedChildJsonOverrides(FString& OutError) const;
	void RedrawJointPreviewLines();
	void ClearJointPreviewLines();
	void CleanupDryRunPreviewFolder();
	void SetJointDebugDrawEnabled(const int32 ChildDocIndex, const int32 JointIndex, const bool bEnabled);
	void RunJointLimitTest(const int32 ChildDocIndex, const int32 JointIndex);
	void StopJointTestAnimation(const bool bRestoreTransform);
	void SetJointAxisX(const int32 ChildDocIndex, const int32 JointIndex, const float Value);
	void SetJointAxisY(const int32 ChildDocIndex, const int32 JointIndex, const float Value);
	void SetJointAxisZ(const int32 ChildDocIndex, const int32 JointIndex, const float Value);
	void SetJointParentLink(const int32 ChildDocIndex, const int32 JointIndex, const FString& ParentLinkName);
	void SetJointChildLink(const int32 ChildDocIndex, const int32 JointIndex, const FString& ChildLinkName);
	void SetJointType(const int32 ChildDocIndex, const int32 JointIndex, const FString& JointTypeName);
	void SetJointLimitEnabled(const int32 ChildDocIndex, const int32 JointIndex, const bool bEnabled);
	void SetJointLimitLower(const int32 ChildDocIndex, const int32 JointIndex, const float Value);
	void SetJointLimitUpper(const int32 ChildDocIndex, const int32 JointIndex, const float Value);
	void SetJointLimitEffort(const int32 ChildDocIndex, const int32 JointIndex, const float Value);
	void SetJointLimitVelocity(const int32 ChildDocIndex, const int32 JointIndex, const float Value);
	void SetChildType(const int32 ChildIndex, const FString& SelectedType);
	void SaveChildVisibility();
	void IsolateChildVisibility(const int32 ChildIndex);
	void RestoreChildVisibilityState();
	bool IsChildIsolated(const int32 ChildIndex) const;
	FReply ToggleChildVisibility(const int32 ChildIndex);

	FReply HandleBrowseWorkspace();
	FReply HandleApplyWorkspace();
	FReply HandleBack();
	FReply ConfirmMaster();
	FReply ProceedToJointSetup();
	FReply GenerateWorkflowJson();
	FReply ContinueFromJointSetupPreview();
	FReply BuildAssembly();
	FReply RevertLastBuild();
	FReply RestartWorkflow();

	void SetStatus(const FString& InMessage);
	void SetStep(const int32 StepIndex);

	TSharedPtr<FCadImportService> Runner;
	TSharedPtr<SEditableTextBox> WorkspaceTextBox;
	TSharedPtr<SVerticalBox> ChildTypeRowsBox;
	TSharedPtr<SVerticalBox> JointEditorRowsBox;

	FString WorkspaceFolder;
	FString StatusMessage;
	FString SelectionPreviewText;
	FString MovableJointPreviewText;
	FString DryRunPreviewFolderPath;
	FString SelectionKey;
	int32 StepIndex = 0;
	TArray<TSharedPtr<FString>> ChildTypeItems;
	TArray<TSharedPtr<FString>> JointTypeItems;
	FCadMasterSelection ConfirmedSelection;
	TArray<FCadChildEntry> ChildEntries;
	TArray<FEditableJointChildState> EditableJointChildren;
	TMap<FString, bool> SavedVisibility;
	int32 IsolatedIndex = INDEX_NONE;
	TOptional<FJointTestAnimationState> JointTestAnimation;
	FCadLevelReplaceResult LastBuildReplaceResult;
	bool bCanRevertLastBuild = false;

	FCadMasterJsonGenerationResult MasterJsonResult;
	FCadChildJsonResult ChildJsonResult;
	FCadWorkflowBuildInput BuildInput;
	FCadFbxImportOptions ImportOptions;
};
