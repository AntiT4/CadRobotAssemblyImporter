#include "CadRobotActor.h"

ACadRobotActor::ACadRobotActor()
{
	PrimaryActorTick.bCanEverTick = false;
}

bool ACadRobotActor::ApplyCommandInput(const FCadRobotCommand& InCommand)
{
	static_cast<void>(InCommand);
	UE_LOG(LogTemp, Error, TEXT("NOT IMPLEMENTED: ACadRobotActor::ApplyCommandInput"));
	// TODO(cad-robot-io): accept external command I/O and validate/queue command frames.
	return false;
}

bool ACadRobotActor::RunCommandController(float DeltaSeconds)
{
	static_cast<void>(DeltaSeconds);
	UE_LOG(LogTemp, Error, TEXT("NOT IMPLEMENTED: ACadRobotActor::RunCommandController"));
	// TODO(cad-robot-io): consume Command and apply control output to imported joint drives.
	return false;
}

bool ACadRobotActor::UpdateStatusSensor(float DeltaSeconds)
{
	static_cast<void>(DeltaSeconds);
	UE_LOG(LogTemp, Error, TEXT("NOT IMPLEMENTED: ACadRobotActor::UpdateStatusSensor"));
	// TODO(cad-robot-io): compute and publish joint/status sensor output for motion verification.
	return false;
}
