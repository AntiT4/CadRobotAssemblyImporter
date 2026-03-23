// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Logging/LogMacros.h"
#include "Modules/ModuleManager.h"

class FCadImporterRunner;

class FCadImporterEditorModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	
	/** This function will be bound to Command. */
	void PluginButtonClicked();
	
private:
	void RegisterMenus();
	void OpenMasterWorkflowWizardPopup();

private:
	TSharedPtr<FCadImporterRunner> WizardRunner;
};

DECLARE_LOG_CATEGORY_EXTERN(LogCadImporter, Log, All);
