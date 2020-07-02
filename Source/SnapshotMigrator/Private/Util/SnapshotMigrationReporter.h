#pragma once

#include "CoreMinimal.h"
#include "Misc/FileHelper.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonWriter.h"

#include <functional>

#include "SnapshotMigratorModuleInternal.h"

#include "SpatialConstants.h"

struct SkippedEntityInfo
{
	FString Class;
	FString SkipReason;
};

struct SkippedComponentFieldInfo
{
	uint32 ComponentId;
	FString FieldName;
	FString SkipReason;
};

struct SnapshotMigrationData
{
	SnapshotMigrationData()
		: SnapshotMigrationData(FString{})
	{
	}

	SnapshotMigrationData(const FString& InSnapshotName)
		: SnapshotName(InSnapshotName), Start(FDateTime::Now())
	{}
	
	void RecordMigratedEntity();
	void RecordSkippedEntity(const uint32 EntityId, const FString& EntityClass, const FString& SkipReason);
	void RecordSkippedComponentFieldUpdate(const uint32 EntityId, const uint32 ComponentId, const FString& FieldName, const FString& SkipReason);

	void FinalizeData()
	{
		ElapsedTime = (FDateTime::Now() - Start).GetTotalSeconds();
		NumSkippedEntities = SkippedEntities.Num();
		NumEncounteredEntities = NumMigratedEntities + NumSkippedEntities;

		PercentMigratedEntities = (100.f * NumMigratedEntities) / NumEncounteredEntities;
		PercentSkippedEntities = (100.f * NumSkippedEntities) / NumEncounteredEntities;
	}

	const FString& GetSnapshotName() const { return SnapshotName; }
	float GetElapsedTime() const { return ElapsedTime; }

	int GetNumEncounteredEntities() const { return NumEncounteredEntities; }
	int GetNumMigratedEntities() const { return NumMigratedEntities; }
	float GetPercentMigratedEntities() const { return PercentMigratedEntities; }
	int GetNumSkippedEntities() const { return NumSkippedEntities; }
	float GetPercentSkippedEntities() const { return PercentSkippedEntities; }

	const TMap<uint32, SkippedEntityInfo>& GetSkippedEntities() const { return SkippedEntities; }
	const TMap<uint32, TArray<SkippedComponentFieldInfo>>& GetSkippedComponentFields() const { return SkippedComponentFieldUpdates; }

private:
	FString SnapshotName;
	FDateTime Start;
	float ElapsedTime;

	TMap<uint32, SkippedEntityInfo> SkippedEntities;

	int NumEncounteredEntities = 0;
	int NumMigratedEntities = 0;
	float PercentMigratedEntities = 0.f;
	int NumSkippedEntities = 0;
	float PercentSkippedEntities = 0.f;
	TMap<uint32, TArray<SkippedComponentFieldInfo>> SkippedComponentFieldUpdates;
};

class SnapshotMigrationReporterBase
{
public:
	SnapshotMigrationReporterBase()
		: SnapshotMigrationReporterBase([](const FString&) {})
	{
	}

	SnapshotMigrationReporterBase(std::function<void(const FString&)>&& InWrite)
		: Write(InWrite)
	{
	}

	virtual ~SnapshotMigrationReporterBase() {}
	virtual void WriteToReport(const SnapshotMigrationData& MigrationData) = 0;

protected:
	std::function<void(const FString&)> Write;
};



class SnapshotMigrationFileReporterBase : public SnapshotMigrationReporterBase
{
public:
	SnapshotMigrationFileReporterBase(const FString& InFilepath)
		: SnapshotMigrationReporterBase([this](const FString& Report) { WriteToFile(Report); }), Filepath(InFilepath)
	{
		// Delete existing file
		IFileManager::Get().Delete(*Filepath, false, true);
	}

	virtual ~SnapshotMigrationFileReporterBase() override {}

protected:
	void WriteToFile(const FString& ToWrite)
	{
		FFileHelper::SaveStringToFile(ToWrite, *Filepath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), EFileWrite::FILEWRITE_Append);
	}

private:
	FString Filepath;
};
