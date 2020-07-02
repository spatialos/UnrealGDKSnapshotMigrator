#pragma once

#include "SnapshotMigrationReporter.h"

class SnapshotMigrationJsonReporter : public SnapshotMigrationFileReporterBase
{
public:
	SnapshotMigrationJsonReporter(const FString& JsonReportFilepath)
		: SnapshotMigrationFileReporterBase(JsonReportFilepath)
	{
	}

	virtual ~SnapshotMigrationJsonReporter() override {}

	virtual void WriteToReport(const SnapshotMigrationData& MigrationData) override;
};
