#include "CadMasterActor.h"

#include "Components/SceneComponent.h"

ACadMasterActor::ACadMasterActor()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SceneRoot->SetMobility(EComponentMobility::Static);
	SetRootComponent(SceneRoot);
}
