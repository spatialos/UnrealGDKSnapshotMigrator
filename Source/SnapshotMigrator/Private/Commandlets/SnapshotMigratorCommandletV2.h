#pragma once

#include "Commandlets/Commandlet.h"
#include "CoreMinimal.h"
#include "Internationalization/Regex.h"

#include "Util/SchemaBundleWrappers.h"
#include "Util/SnapshotMigrationReporter.h"

#include "EngineClasses/SpatialNetDriver.h"
#include "EngineClasses/SpatialNetConnection.h"
#include "EngineClasses/SpatialPackageMapClient.h"
#include "Interop/SpatialClassInfoManager.h"
#include "Interop/SpatialReceiver.h"
#include "Interop/SpatialSender.h"
#include "Interop/SpatialStaticComponentView.h"
#include "Interop/Connection/SpatialWorkerConnection.h"

#include <WorkerSDK/improbable/c_schema.h>
#include <WorkerSDK/improbable/c_worker.h>

#include "SnapshotMigratorCommandletV2.generated.h"

struct Snapshot
{
	FString Name;
	FString SourcePath;
	FString TargetPath;
};

UCLASS()
class USnapshotMigratorCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	virtual int32 Main(const FString& Params) override;

	USnapshotMigratorCommandlet();

private:
	TArray<TUniquePtr<SnapshotMigrationReporterBase>> Reporters;
	SnapshotMigrationData MigrationData;

	TArray<FRegexPattern> EntityActorClassFilters;

	UPROPERTY()
	USpatialNetDriver* NetDriver;
	UPROPERTY()
	USpatialNetConnection* NetConnection;
	UPROPERTY()
	USpatialPackageMapClient* PackageMap;

	UPROPERTY()
	USpatialClassInfoManager* ClassInfoManager;
	UPROPERTY()
	USpatialSender* SpatialSender;
	UPROPERTY()
	USpatialReceiver* SpatialReceiver;
	UPROPERTY()
	USpatialStaticComponentView* StaticComponentView;
	UPROPERTY()
	USpatialWorkerConnection* WorkerConnection;

	SchemaBundleDefinitions OldSchemaBundleDefinitions;
	SchemaBundleDefinitions NewSchemaBundleDefinitions;
	TArray<Snapshot> Snapshots;

	UWorld* World;

	bool LoadJsonSchemaBundleAtPath(const FString& SchemaBundlePath, TSharedPtr<FJsonObject>& OutJsonObject);

	bool Setup();
	bool ConfigureNetDriver();

	bool MigrateSnapshot(const FString& Source, const FString& Target);

	bool MigrateEntity(Worker_SnapshotOutputStream* OutStream, const Worker_Entity* Entity);
	bool DoesEntityPassClassFilter(const FString& EntityActorClasspath);

	bool UpdateComponent(const uint32 EntityId, const Worker_ComponentData& OldComponent, Worker_ComponentData& Component);

	Worker_ComponentUpdate CreateComponentMigration(const uint32 EntityId, const Worker_ComponentData& OldComponent, const Worker_ComponentId NewComponentId);
};
