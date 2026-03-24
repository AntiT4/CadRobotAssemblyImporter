#pragma once

#include "CoreMinimal.h"
#include "MasterJsonActorCollector.h"
#include "MasterChildJsonExtractor.h"
#include "MasterJsonGenerator.h"
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
	EActiveTimerReturnType PollSelection(double CurrentTime, float DeltaTime);
	void RefreshSelectionPreview();
	void RebuildChildRows();
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
	FReply GenerateWorkflowJson();
	FReply BuildAssembly();
	FReply RestartWorkflow();

	void SetStatus(const FString& InMessage);
	void SetStep(const int32 StepIndex);

	TSharedPtr<FCadImportService> Runner;
	TSharedPtr<SEditableTextBox> WorkspaceTextBox;
	TSharedPtr<SVerticalBox> ChildTypeRowsBox;

	FString WorkspaceFolder;
	FString StatusMessage;
	FString SelectionPreviewText;
	FString SelectionKey;
	int32 StepIndex = 0;
	TArray<TSharedPtr<FString>> ChildTypeItems;
	FCadMasterSelection ConfirmedSelection;
	TArray<FCadChildEntry> ChildEntries;
	TMap<FString, bool> SavedVisibility;
	int32 IsolatedIndex = INDEX_NONE;

	FCadMasterJsonGenerationResult MasterJsonResult;
	FCadChildJsonResult ChildJsonResult;
	FCadWorkflowBuildInput BuildInput;
	FCadFbxImportOptions ImportOptions;
};
