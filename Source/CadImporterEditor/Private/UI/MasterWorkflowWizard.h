#pragma once

#include "CoreMinimal.h"
#include "MasterJsonActorCollector.h"
#include "MasterChildJsonExtractor.h"
#include "MasterJsonGenerator.h"
#include "Widgets/SCompoundWidget.h"

class FCadImporterRunner;
class SEditableTextBox;
class SVerticalBox;
class AActor;

class SCadMasterWorkflowWizard : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCadMasterWorkflowWizard)
		: _Runner(nullptr)
	{
	}
		SLATE_ARGUMENT(TSharedPtr<FCadImporterRunner>, Runner)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	EActiveTimerReturnType HandleSelectionPolling(double CurrentTime, float DeltaTime);
	void RefreshSelectionPreviewIfChanged();
	void RebuildChildTypeRows();
	void HandleChildTypeSelectionChanged(const int32 ChildIndex, const FString& SelectedType);
	AActor* ResolveChildActorByPath(const FCadMasterChildEntry& ChildEntry) const;
	void CaptureChildVisibilitySnapshot();
	void ApplyChildVisibilityIsolation(const int32 ChildIndex);
	void RestoreChildVisibility();
	bool IsChildVisibilityIsolated(const int32 ChildIndex) const;
	FReply HandleToggleChildVisibility(const int32 ChildIndex);

	FReply HandleBrowseWorkspace();
	FReply HandleApplyWorkspace();
	FReply HandleBack();
	FReply HandleConfirmMasterActor();
	FReply HandleGenerateJson();
	FReply HandleBuildActor();
	FReply HandleRestart();

	void SetStatusMessage(const FString& InMessage);
	void MoveToStep(const int32 StepIndex);

	TSharedPtr<FCadImporterRunner> Runner;
	TSharedPtr<SEditableTextBox> WorkspaceTextBox;
	TSharedPtr<SVerticalBox> ChildTypeRowsBox;

	FString WorkspaceFolder;
	FString StatusMessage;
	FString SelectionPreviewText;
	FString LastSelectionSignature;
	int32 ActiveStepIndex = 0;
	TArray<TSharedPtr<FString>> ChildTypeOptions;
	FCadMasterActorSelectionResult ConfirmedSelectionResult;
	TArray<FCadMasterChildEntry> EditableChildren;
	TMap<FString, bool> ChildVisibilitySnapshot;
	int32 IsolatedChildIndex = INDEX_NONE;

	FCadMasterJsonGenerationResult MasterGenerationResult;
	FCadChildJsonExtractionResult ChildExtractionResult;
	FCadMasterWorkflowBuildInput WorkflowBuildInput;
	FCadFbxImportOptions ImportOptions;
};
