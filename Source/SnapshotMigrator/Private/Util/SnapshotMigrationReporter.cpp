
#include "Util/SnapshotMigrationReporter.h"

void SnapshotMigrationData::RecordMigratedEntity()
{
	NumMigratedEntities++;
}

void SnapshotMigrationData::RecordSkippedEntity(const uint32 EntityId, const FString& EntityClass, const FString& SkipReason)
{
	SkippedEntities.Add(EntityId, SkippedEntityInfo{ EntityClass, SkipReason });
}

void SnapshotMigrationData::RecordSkippedComponentFieldUpdate(const uint32 EntityId, const uint32 ComponentId, const FString& FieldName, const FString& SkipReason)
{
	TArray<SkippedComponentFieldInfo>& SkippedComponentFieldUpdatesForEntity = SkippedComponentFieldUpdates.FindOrAdd(EntityId);
	SkippedComponentFieldUpdatesForEntity.Add(SkippedComponentFieldInfo{ ComponentId, FieldName, SkipReason });
}
