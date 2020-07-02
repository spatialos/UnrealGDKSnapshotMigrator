
#include "Util/SnapshotMigrationJsonReporter.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

void SnapshotMigrationJsonReporter::WriteToReport(const SnapshotMigrationData& MigrationData)
{
	TSharedPtr<FJsonObject> Json = MakeShareable(new FJsonObject);

	Json->SetStringField(FString{ TEXT("SnapshotName") }, MigrationData.GetSnapshotName());
	Json->SetNumberField(FString{ TEXT("ElapsedTime") }, MigrationData.GetElapsedTime());
	Json->SetNumberField(FString{ TEXT("NumEncounteredEntities") }, MigrationData.GetNumEncounteredEntities());
	Json->SetNumberField(FString{ TEXT("NumMigratedEntities") }, MigrationData.GetNumMigratedEntities());
	Json->SetNumberField(FString{ TEXT("PercentMigratedEntities") }, MigrationData.GetPercentMigratedEntities());
	Json->SetNumberField(FString{ TEXT("NumSkippedEntities") }, MigrationData.GetNumSkippedEntities());
	Json->SetNumberField(FString{ TEXT("PercentSkippedEntities") }, MigrationData.GetPercentSkippedEntities());

	const TMap<uint32, SkippedEntityInfo>& SkippedEntities = MigrationData.GetSkippedEntities();

	TArray<uint32> SkippedEntityIds;
	SkippedEntities.GenerateKeyArray(SkippedEntityIds);
	SkippedEntityIds.Sort();

	TArray<TSharedPtr<FJsonValue>> SkippedEntitiesJson;

	for (const uint32 EntityId : SkippedEntityIds)
	{
		const SkippedEntityInfo& SkipInfo = SkippedEntities.FindChecked(EntityId);

		TSharedPtr<FJsonObject> SkippedEntityJson = MakeShareable(new FJsonObject);

		SkippedEntityJson->SetNumberField(FString{ TEXT("EntityId") }, EntityId);
		SkippedEntityJson->SetStringField(FString{ TEXT("Class") }, SkipInfo.Class);
		SkippedEntityJson->SetStringField(FString{ TEXT("SkipReason") }, SkipInfo.SkipReason);

		SkippedEntitiesJson.Add(MakeShareable<FJsonValueObject>(new FJsonValueObject(SkippedEntityJson)));
	}

	Json->SetArrayField(FString{ TEXT("SkippedEntities") }, SkippedEntitiesJson);

	const TMap<uint32, TArray<SkippedComponentFieldInfo>>& SkippedComponentFields = MigrationData.GetSkippedComponentFields();

	SkippedEntityIds.Empty();
	SkippedComponentFields.GenerateKeyArray(SkippedEntityIds);
	SkippedEntityIds.Sort();

	TArray<TSharedPtr<FJsonValue>> SkippedComponentFieldsJson;

	for (const uint32 EntityId : SkippedEntityIds)
	{
		const TArray<SkippedComponentFieldInfo>& SkippedComponentFieldsForEntity = SkippedComponentFields.FindChecked(EntityId);

		TSharedPtr<FJsonObject> EntityWithSkippedComponentFieldsJson = MakeShareable(new FJsonObject);

		EntityWithSkippedComponentFieldsJson->SetNumberField(FString{ TEXT("EntityId") }, EntityId);
		EntityWithSkippedComponentFieldsJson->SetNumberField(FString{ TEXT("NumSkippedComponentFields") }, SkippedComponentFieldsForEntity.Num());

		TArray<TSharedPtr<FJsonValue>> SkippedComponentFieldsForEntityJson;

		for (const SkippedComponentFieldInfo& SkippedComponentField : SkippedComponentFieldsForEntity)
		{
			TSharedPtr<FJsonObject> SkippedComponentFieldJson = MakeShareable(new FJsonObject);

			SkippedComponentFieldJson->SetNumberField(FString{ TEXT("ComponentId") }, SkippedComponentField.ComponentId);
			SkippedComponentFieldJson->SetStringField(FString{ TEXT("FieldName") }, SkippedComponentField.FieldName);
			SkippedComponentFieldJson->SetStringField(FString{ TEXT("SkipReason") }, SkippedComponentField.SkipReason);

			SkippedComponentFieldsForEntityJson.Add(MakeShareable<FJsonValueObject>(new FJsonValueObject(SkippedComponentFieldJson)));
		}

		EntityWithSkippedComponentFieldsJson->SetArrayField(FString{ TEXT("SkippedComponentFields") }, SkippedComponentFieldsForEntityJson);
		SkippedComponentFieldsJson.Add(MakeShareable<FJsonValueObject>(new FJsonValueObject(EntityWithSkippedComponentFieldsJson)));
	}

	Json->SetArrayField(FString{ TEXT("SkippedComponentFieldUpdates") }, SkippedComponentFieldsJson);

	FString OutputString;

	using CharType = TCHAR;
	using Policy = TCondensedJsonPrintPolicy<CharType>;

	TSharedRef<TJsonWriter<CharType, Policy>> Writer = TJsonWriterFactory<CharType, Policy>::Create(&OutputString);
	FJsonSerializer::Serialize(Json.ToSharedRef(), Writer);

	Write(OutputString);
}
