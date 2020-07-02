// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#include "SnapshotMigratorModule.h"
#include "SnapshotMigratorModuleInternal.h"

// Temporary; needed since we get link errors otherwise. Should be removed when this is moved into the GDK proper.
#include "Interop/SpatialClassInfoManager.h"
DEFINE_LOG_CATEGORY(LogSpatialClassInfoManager);

DEFINE_LOG_CATEGORY(LogSnapshotMigrator)

void FSnapshotMigratorModule::StartupModule()
{
}

void FSnapshotMigratorModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FSnapshotMigratorModule, SnapshotMigrator)
