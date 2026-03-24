// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Hyoseung

#pragma once

#include "Logging/LogMacros.h"
#include "Modules/ModuleManager.h"

class FCadImportService;

class FCadImporterEditorModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	
private:
	void RegisterMenus();
	void OpenWorkflowWindow();

private:
	TSharedPtr<FCadImportService> ImportService;
};

DECLARE_LOG_CATEGORY_EXTERN(LogCadImporter, Log, All);
