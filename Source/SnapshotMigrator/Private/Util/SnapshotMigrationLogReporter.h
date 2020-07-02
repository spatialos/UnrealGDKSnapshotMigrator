#pragma once

#include "SnapshotMigrationReporter.h"

class SnapshotMigrationLogReporter : public SnapshotMigrationReporterBase
{
public:
	SnapshotMigrationLogReporter()
		: SnapshotMigrationReporterBase([](const FString& Report) { UE_LOG(LogSnapshotMigrator, Display, TEXT("%s"), *Report); })
	{
	}

	virtual ~SnapshotMigrationLogReporter() override {}
	virtual void WriteToReport(const SnapshotMigrationData& MigrationData) override;
};
