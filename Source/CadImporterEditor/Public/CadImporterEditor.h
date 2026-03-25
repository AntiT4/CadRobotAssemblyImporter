// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Hyoseung

#pragma once

#include "Containers/Ticker.h"
#include "Logging/LogMacros.h"
#include "Modules/ModuleManager.h"

class FCadImportService;
class SDockTab;
class FSpawnTabArgs;
class FSlateStyleSet;
struct FSlateBrush;

class FCadImporterEditorModule : public IModuleInterface
{
public:
	static FName GetStyleSetName();
	static const FSlateBrush* GetWorkflowStepBrush();
	static const FSlateBrush* GetWorkflowTabIconBrush();

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	
private:
	void RegisterStyle();
	void UnregisterStyle();
	void CleanupStaleDryRunFolders() const;
	bool TickDryRunGarbageCollector(float DeltaTime);
	void RegisterMenus();
	void OpenWorkflowTab();
	TSharedRef<SDockTab> SpawnWorkflowTab(const FSpawnTabArgs& Args);

private:
	TSharedPtr<FCadImportService> ImportService;
	TSharedPtr<FSlateStyleSet> StyleSet;
	FTSTicker::FDelegateHandle DryRunCleanupTickerHandle;
};

DECLARE_LOG_CATEGORY_EXTERN(LogCadImporter, Log, All);
