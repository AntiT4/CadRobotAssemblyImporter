#pragma once

#include "CoreMinimal.h"

namespace CadWorkspaceUtils
{
	bool TryNormalizePath(
		const FString& WorkspaceInput,
		FString& OutTrimmedWorkspace,
		FString& OutNormalizedWorkspace,
		FString& OutError);

	bool TryValidateForApply(
		const FString& WorkspaceInput,
		FString& OutNormalizedWorkspace,
		FString& OutError);

	bool TryValidateForGeneration(
		const FString& WorkspaceInput,
		FString& OutNormalizedWorkspace,
		FString& OutError);
}
