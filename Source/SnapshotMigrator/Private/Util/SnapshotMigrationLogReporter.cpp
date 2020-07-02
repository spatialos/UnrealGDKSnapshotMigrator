
#include "SnapshotMigrationLogReporter.h"

void SnapshotMigrationLogReporter::WriteToReport(const SnapshotMigrationData& MigrationData)
{
	TArray<FString> ReportLines;

	ReportLines.Add(FString{ TEXT("\n") });
	ReportLines.Add(FString::Printf(TEXT("-- Migration Report for %s (Elapsed Time: %.2f seconds) --"), *MigrationData.GetSnapshotName(), MigrationData.GetElapsedTime()));
	ReportLines.Add(FString::Printf(TEXT("%-25s: %6d "), TEXT("# Encountered"), MigrationData.GetNumEncounteredEntities()));
	ReportLines.Add(FString::Printf(TEXT("%-25s: %6d (%5.2f%% of Encountered)"), TEXT("# Successfully Migrated"), MigrationData.GetNumMigratedEntities(), MigrationData.GetPercentMigratedEntities()));
	ReportLines.Add(FString::Printf(TEXT("%-25s: %6d (%5.2f%% of Encountered)"), TEXT("# Skipped"), MigrationData.GetNumSkippedEntities(), MigrationData.GetPercentSkippedEntities()));
	ReportLines.Add(FString{ TEXT("-- End of Migration Report -- ") });
	ReportLines.Add(FString{});

	Write(FString::Join(ReportLines, TEXT("\n")));
}
