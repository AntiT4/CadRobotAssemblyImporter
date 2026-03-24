#include "CadRobotActor.h"

ACadRobotActor::ACadRobotActor()
{
	PrimaryActorTick.bCanEverTick = false;
}

bool ACadRobotActor::ApplyCommand(const FCadRobotCommand& InCommand)
{
	static_cast<void>(InCommand);
	UE_LOG(LogTemp, Error, TEXT("NOT IMPLEMENTED: ACadRobotActor::ApplyCommand"));
	// TODO(cad-robot-io): accept external command I/O and validate/queue command frames.
	return false;
}

bool ACadRobotActor::RunController(float DeltaSeconds)
{
	static_cast<void>(DeltaSeconds);
	UE_LOG(LogTemp, Error, TEXT("NOT IMPLEMENTED: ACadRobotActor::RunController"));
	// TODO(cad-robot-io): consume Command and apply control output to imported joint drives.
	return false;
}

bool ACadRobotActor::UpdateStatus(float DeltaSeconds)
{
	static_cast<void>(DeltaSeconds);
	UE_LOG(LogTemp, Error, TEXT("NOT IMPLEMENTED: ACadRobotActor::UpdateStatus"));
	// TODO(cad-robot-io): compute and publish joint/status sensor output for motion verification.
	return false;
}
