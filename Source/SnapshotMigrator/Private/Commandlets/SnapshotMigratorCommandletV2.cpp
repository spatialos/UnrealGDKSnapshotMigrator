// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#include "SnapshotMigratorCommandletV2.h"
#include "SnapshotMigratorModuleInternal.h"

#include <fstream>
#include <iostream>

#include "Util/SnapshotHelperLibrary.h"
#include "Util/SnapshotMigrationJsonReporter.h"
#include "Util/SnapshotMigrationLogReporter.h"

#include "Engine.h"
#include "FileHelpers.h"
#include "Misc/ScopeExit.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#include "SpatialGDKServicesModule.h"

#include "EngineClasses/SpatialActorChannel.h"
#include "EngineClasses/SpatialGameInstance.h"
#include "Interop/GlobalStateManager.h"
#include "Schema/RPCPayload.h"
#include "SpatialGDKServicesConstants.h"
#include "Utils/EntityFactory.h"
#include "Utils/InterestFactory.h"

USnapshotMigratorCommandlet::USnapshotMigratorCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 USnapshotMigratorCommandlet::Main(const FString& Params)
{
	UE_LOG(LogSnapshotMigrator, Display, TEXT("Starting Snapshot Migrator Commandlet"));

	if (!Setup())
	{
		UE_LOG(LogSnapshotMigrator, Error, TEXT("Failed to initialise commandlet!"));
		return 1;
	}

	for (const Snapshot& Snapshot : Snapshots)
	{
		if (!ConfigureNetDriver())
		{
			UE_LOG(LogSnapshotMigrator, Error, TEXT("Failed to initialize NetDriver in order to migrate %s!"), *Snapshot.Name);
			return 1;
		}

		MigrationData = SnapshotMigrationData{ Snapshot.Name };
		if (!MigrateSnapshot(Snapshot.SourcePath, Snapshot.TargetPath))
		{
			UE_LOG(LogSnapshotMigrator, Warning, TEXT("Failed to migrate %s!"), *Snapshot.Name);
		}
		MigrationData.FinalizeData();

		for (TUniquePtr<SnapshotMigrationReporterBase>& Reporter : Reporters)
		{
			Reporter->WriteToReport(MigrationData);
		}
	}

	return 0;
}

bool USnapshotMigratorCommandlet::LoadJsonSchemaBundleAtPath(const FString& SchemaBundlePath, TSharedPtr<FJsonObject>& OutJsonObject)
{
	FString SchemaBundleJson{};
	if (!FFileHelper::LoadFileToString(SchemaBundleJson, *SchemaBundlePath))
	{
		return false;
	}

	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(SchemaBundleJson);
	return FJsonSerializer::Deserialize(Reader, OutJsonObject) && OutJsonObject.IsValid();
}

bool USnapshotMigratorCommandlet::Setup()
{
	check(GConfig);
	FString FinalIniPath;
	if (!FConfigCacheIni::LoadGlobalIniFile(FinalIniPath, TEXT("SnapshotClasspathWhitelistPatterns")))
	{
		UE_LOG(LogSnapshotMigrator, Error, TEXT("Failed to load classpath whitelist config file!"));
		return false;
	}

	TArray<FString> WhitelistedClasspathPatterns;
	GConfig->GetArray(TEXT("ClasspathPatterns"), TEXT("ClasspathPatterns"), WhitelistedClasspathPatterns, FinalIniPath);

	for (const FString& Pattern : WhitelistedClasspathPatterns)
	{
		EntityActorClassFilters.Add(FRegexPattern{ Pattern });
	}

	World = UEditorLoadingAndSavingUtils::LoadMap(FString{ TEXT("/Game/NWX/Tests/Base/FTEST_Base") });
	check(World);

	const FString& DefaultSpatialRootDir = SpatialGDKServicesConstants::SpatialOSDirectory;
	const FString& DefaultOldDeploymentArtifactsDir = FPaths::Combine(DefaultSpatialRootDir, FString{ TEXT("tmp/artifacts") });
	const FString& SchemaBundleFilename = FString{ TEXT("schema.sb.json") };

	const FString& DefaultCompiledSchemaDir = FPaths::Combine(DefaultSpatialRootDir, FString{ TEXT("build/assembly/schema") });

	TArray<FString> Tokens;
	TArray<FString> Switches;
	FCommandLine::Parse(FCommandLine::Get(), Tokens, Switches);

	FString OldArtifactsDir = DefaultOldDeploymentArtifactsDir;
	FString CompiledSchemaDir = DefaultCompiledSchemaDir;

	// Split won't update target strings if it fails, so we can just ignore the output. It'll be default if it fails or the CL-provided value if it succeeds.
	for (const FString& CLSwitch : Switches)
	{
		FString SwitchName;
		if (CLSwitch.StartsWith(FString{ TEXT("OldArtifactsDir") }))
		{
			CLSwitch.Split(FString{ TEXT("=") }, &SwitchName, &OldArtifactsDir);
		}
		else if (CLSwitch.StartsWith(FString{ TEXT("CompiledSchemaDir") }))
		{
			CLSwitch.Split(FString{ TEXT("=") }, &SwitchName, &CompiledSchemaDir);
		}
		else if (CLSwitch.StartsWith(FString{ TEXT("LogJSON") }))
		{
			FString JsonLogFile;
			if (!CLSwitch.Split(FString{ TEXT("=") }, &SwitchName, &JsonLogFile))
			{
				UE_LOG(LogSnapshotMigrator, Warning, TEXT("LogJSON must be provided with an absolute filepath to the target log file!"));
				return false;
			}

			Reporters.Add(MakeUnique<SnapshotMigrationJsonReporter>(JsonLogFile));
		}
	}

	Reporters.Add(MakeUnique<SnapshotMigrationLogReporter>());

	const FString& OldSchemaBundlePath = FPaths::Combine(OldArtifactsDir, SchemaBundleFilename);
	const FString& NewSchemaBundlePath = FPaths::Combine(CompiledSchemaDir, SchemaBundleFilename);

	TSharedPtr<FJsonObject> OldSchemaBundleJsonObject;
	TSharedPtr<FJsonObject> NewSchemaBundleJsonObject;

	if (!LoadJsonSchemaBundleAtPath(OldSchemaBundlePath, OldSchemaBundleJsonObject) || !LoadJsonSchemaBundleAtPath(NewSchemaBundlePath, NewSchemaBundleJsonObject))
	{
		UE_LOG(LogSnapshotMigrator, Warning, TEXT("Failed to load both schema bundles -- ensure that there are bundles present at both '%s' and '%s'."), *OldSchemaBundlePath, *NewSchemaBundlePath);
		return false;
	}

	OldSchemaBundleDefinitions = SchemaBundleDefinitions{ OldSchemaBundleJsonObject };
	NewSchemaBundleDefinitions = SchemaBundleDefinitions{ NewSchemaBundleJsonObject };

	const FString& TargetSnapshotDir = FPaths::Combine(DefaultSpatialRootDir, FString{ TEXT("snapshots") });

	TArray<FString> ExistingSnapshots;
	IFileManager::Get().FindFiles(ExistingSnapshots, *DefaultOldDeploymentArtifactsDir, TEXT("snapshot"));
	for (const FString& ExistingSnapshot : ExistingSnapshots)
	{
		const FString& SourcePath = FPaths::Combine(DefaultOldDeploymentArtifactsDir, ExistingSnapshot);
		const FString& TargetPath = FPaths::Combine(TargetSnapshotDir, ExistingSnapshot);
		Snapshots.Add(Snapshot{ ExistingSnapshot, SourcePath, TargetPath });
	}

	return true;
}

bool USnapshotMigratorCommandlet::ConfigureNetDriver()
{
	// Pretty much all of this is taken wholesale from USpatialNetDriver::InitBase. However, InitBase also initiates a connection to a running Spatial
	// deployment, which we don't have when running in this commandlet.
	// Would it be worth standing up a local deployment when running this commandlet? Do we have the tooling in place to do so during, e.g., CI ops?
	// Can some of the initialization logic be decoupled from actually needing to connect to a Spatial deployment so this is less cargo-culty?

	// Make absolutely sure that the actor channel that we are using is our Spatial actor channel
	// Copied from what the Engine does with UActorChannel
	FChannelDefinition SpatialChannelDefinition{};
	SpatialChannelDefinition.ChannelName = NAME_Actor;
	SpatialChannelDefinition.ClassName = FName(*USpatialActorChannel::StaticClass()->GetPathName());
	SpatialChannelDefinition.ChannelClass = USpatialActorChannel::StaticClass();
	SpatialChannelDefinition.bServerOpen = true;

	NetDriver = NewObject<USpatialNetDriver>();
	NetConnection = NewObject<USpatialNetConnection>();
	PackageMap = NewObject<USpatialPackageMapClient>();

	ClassInfoManager = NewObject<USpatialClassInfoManager>();
	SpatialSender = NewObject<USpatialSender>();
	SpatialReceiver = NewObject<USpatialReceiver>();
	StaticComponentView = NewObject<USpatialStaticComponentView>();
	WorkerConnection = NewObject<USpatialWorkerConnection>();

	NetDriver->ChannelDefinitions[CHTYPE_Actor] = SpatialChannelDefinition;
	NetDriver->ChannelDefinitionMap[NAME_Actor] = SpatialChannelDefinition;
	NetDriver->GuidCache = MakeShareable(new FSpatialNetGUIDCache(NetDriver));
	NetDriver->World = World;
	NetDriver->Connection = WorkerConnection;
	
	NetConnection->Driver = NetDriver;
	NetConnection->State = USOCK_Closed;

	NetDriver->AddClientConnection(NetConnection);

	NetDriver->StaticComponentView = StaticComponentView;

	SpatialReceiver->Init(NetDriver, &World->GetTimerManager(), nullptr);	 //UE424_TODO - need a proper RPCService?
	NetDriver->Receiver = SpatialReceiver;

	PackageMap->Initialize(NetConnection, NetDriver->GuidCache);
	PackageMap->Init(NetDriver, &World->GetTimerManager());
	NetDriver->PackageMap = PackageMap;
	NetConnection->PackageMap = PackageMap;

	NetDriver->ClassInfoManager = NewObject<USpatialClassInfoManager>();
	if (!NetDriver->ClassInfoManager->TryInit(NetDriver))
	{
		UE_LOG(LogSnapshotMigrator, Error, TEXT("Failed to initialise the NetDriver's ClassInfoManager!"));
		return false;
	}
	NetDriver->ClassInfoManager = ClassInfoManager;

	SpatialSender->Init(NetDriver, &World->GetTimerManager(), nullptr);	   //UE424_TODO - need a proper RPCService?
	NetDriver->Sender = SpatialSender;

	NetDriver->InterestFactory = MakeUnique<SpatialGDK::InterestFactory>(ClassInfoManager, PackageMap);

	return true;
}

bool USnapshotMigratorCommandlet::MigrateSnapshot(const FString& Source, const FString& Target)
{
	const Worker_ComponentVtable DefaultInputVtable{};
	Worker_SnapshotParameters InputParameters{};
	InputParameters.default_component_vtable = &DefaultInputVtable;

	Worker_SnapshotInputStream* InputStream = Worker_SnapshotInputStream_Create(TCHAR_TO_UTF8(*Source), &InputParameters);

	const Worker_ComponentVtable DefaultOutputVtable{};
	Worker_SnapshotParameters OutputParameters{};
	OutputParameters.default_component_vtable = &DefaultOutputVtable;

	// Write to a temporary snapshot first. There may already be a file with the intended target name in existence and we don't want to mangle it if we end up failing the migration.
	const FString& TmpSnapshotPath = FString::Printf(TEXT("%s.tmp"), *Source);

	Worker_SnapshotOutputStream* OutputStream = Worker_SnapshotOutputStream_Create(TCHAR_TO_UTF8(*TmpSnapshotPath), &OutputParameters);

	// In its own scope to make use of ON_SCOPE_EXIT below
	{
		// Clean up & release resources associated with both streams
		ON_SCOPE_EXIT
		{
			Worker_SnapshotInputStream_Destroy(InputStream);
			Worker_SnapshotOutputStream_Destroy(OutputStream);
		};

		// Wrap input/output stream state validators for easier usage.
		const auto IsInputStreamStateValid = [InputStream](const FString& OpContext) {
			return SnapshotHelperLibrary::IsStreamStateValid(Worker_SnapshotInputStream_GetState, InputStream, OpContext);
		};

		const auto IsOutputStreamStateValid = [OutputStream](const FString& OpContext) {
			return SnapshotHelperLibrary::IsStreamStateValid(Worker_SnapshotOutputStream_GetState, OutputStream, OpContext);
		};

		if (!IsInputStreamStateValid(FString{ TEXT("initialise input stream") }) || !IsOutputStreamStateValid(FString{ TEXT("initialise output stream") }))
		{
			return false;
		}

		while (Worker_SnapshotInputStream_HasNext(InputStream))
		{
			if (!IsInputStreamStateValid(FString{ TEXT("check if snapshot has remaining entities") }))
			{
				return false;
			}

			const Worker_Entity* Entity = Worker_SnapshotInputStream_ReadEntity(InputStream);
			if (!IsInputStreamStateValid(FString{ TEXT("read entity from snapshot") }))
			{
				return false;
			}

			if (MigrateEntity(OutputStream, Entity) && !IsOutputStreamStateValid(FString::Printf(TEXT("write entity with id %lld to snapshot"), Entity->entity_id)))
			{
				return false;
			}
		}
	}

	// If we reach this point, all entities have either been migrated or skipped.
	// However, if we fail to move the file into place then the migration has technically failed, since no migrated snapshot will exist at the target path.
	// This can be communicated by simply returning the value of the move operation.
	return IFileManager::Get().Move(*Target, *TmpSnapshotPath, true, true);
}

bool USnapshotMigratorCommandlet::MigrateEntity(Worker_SnapshotOutputStream* OutStream, const Worker_Entity* Entity)
{
	const Worker_ComponentData* UnrealMetadataComponentPtr = SnapshotHelperLibrary::GetComponentFromEntityById(Entity, SpatialConstants::UNREAL_METADATA_COMPONENT_ID);

	TArray<Worker_ComponentData> MigratedComponents;

	const uint32 EntityId = Entity->entity_id;

	if (UnrealMetadataComponentPtr == nullptr)
	{
		MigratedComponents = TArray<Worker_ComponentData>(Entity->components, Entity->component_count);
	}
	else
	{
		const SpatialGDK::UnrealMetadata UnrealMetadata{ *UnrealMetadataComponentPtr };

		const bool bIsStartupActor = UnrealMetadata.bNetStartup.IsSet() && UnrealMetadata.bNetStartup.GetValue();

		if (!DoesEntityPassClassFilter(UnrealMetadata.ClassPath))
		{
			MigrationData.RecordSkippedEntity(EntityId, UnrealMetadata.ClassPath, FString{ TEXT("Class does not match any patterns provided in the whitelist.") });
			return false;
		}

		UClass* EntityActorClass = LoadObject<UClass>(NULL, *UnrealMetadata.ClassPath);
		if (EntityActorClass == nullptr)
		{
			MigrationData.RecordSkippedEntity(EntityId, UnrealMetadata.ClassPath, FString{ TEXT("Could not locate class. This is expected if the class in question has been deleted.") });
			return false;
		}

		// We may designate some classes as not persistent (e.g., most things related to players).
		// If we encounter an entity in the snapshot whose class has since been marked as not-persistent, we should skip over it.
		if (EntityActorClass->HasAnySpatialClassFlags(SPATIALCLASS_NotPersistent))
		{
			MigrationData.RecordSkippedEntity(EntityId, UnrealMetadata.ClassPath, FString{ TEXT("Class is marked 'Not Persistent'.") });
			return false;
		}

		AActor* EntityActor = World->SpawnActor(EntityActorClass);
		if (EntityActor == nullptr)
		{
			MigrationData.RecordSkippedEntity(EntityId, UnrealMetadata.ClassPath, FString{ TEXT("Failed to spawn actor.") });
			return false;
		}

		EntityActor->bNetStartup = bIsStartupActor;

		NetDriver->PackageMap->ResolveEntityActor(EntityActor, Entity->entity_id);

		USpatialActorChannel* Channel = Cast<USpatialActorChannel>(NetConnection->CreateChannelByName(NAME_Actor, EChannelCreateFlags::OpenedLocally));

		Channel->SetChannelActor(EntityActor, ESetChannelActorFlags::None);
		Channel->SetEntityId(Entity->entity_id);
		Channel->bCreatingNewEntity = true;

		ON_SCOPE_EXIT
		{
			Channel->Close(EChannelCloseReason::Destroyed);
			NetDriver->RemoveActorChannel(Entity->entity_id, *Channel);
			World->DestroyActor(EntityActor);
		};

		// The code in the following scope is taken from USpatialActorChannel::ReplicateActor (minus the Reporter line).
		// We do this in order to build the Actor's "skeleton", which we can then boil down to the list of components that would be sent if we were actually creating this entity.
		{
			// Create an outgoing bunch (to satisfy some of the functions below)
			FOutBunch Bunch(PackageMap);
			if (Bunch.IsError())
			{
				MigrationData.RecordSkippedEntity(EntityId, UnrealMetadata.ClassPath, FString{ TEXT("Failed to create initial bunch for simulated replication.") });
				return false;
			}

			FReplicationFlags RepFlags;
			RepFlags.bNetInitial = true;
			Bunch.bClose = EntityActor->bNetTemporary;
			Bunch.bReliable = true;
			RepFlags.bNetOwner = true;

			Channel->PreReceiveSpatialUpdate(EntityActor);
			EntityActor->OnSerializeNewActor(Bunch);
			EntityActor->ReplicateSubobjects(Channel, &Bunch, &RepFlags);
		}

		TArray<Worker_ComponentData> EntityComponents;

		TMap<Worker_ComponentId, Worker_ComponentData> OldComponentsById;

		for (Worker_ComponentData& Component : TArray<Worker_ComponentData>(Entity->components, Entity->component_count))
		{
			uint32 NewComponentId;
			// If this component still exists in the new schema
			if (SchemaBundleDefinitions::GetCorrespondingComponentId(OldSchemaBundleDefinitions, NewSchemaBundleDefinitions, Component.component_id, NewComponentId))
			{
				// The Tombstone component will never be directly added to a "newly" created entity, so if it exists on the old entity we should create it on the new one as well.
				// We should also pull the sublevel component across.
				if (NewComponentId == SpatialConstants::TOMBSTONE_COMPONENT_ID || NetDriver->ClassInfoManager->IsSublevelComponent(NewComponentId))
				{
					// Add this as an empty component; it'll get picked up and updated with the proper fields during UpdateComponent
					Worker_ComponentData Data;
					Data.component_id = NewComponentId;
					Data.schema_type = Schema_CreateComponentData();
					EntityComponents.Add(Data);
				}

				OldComponentsById.Add(Component.component_id, Component);
			}
		}

		FClassInfo EntityClassInfo = NetDriver->ClassInfoManager->GetOrCreateClassInfoByClass(EntityActorClass);
		TArray<Worker_ComponentId> ActorOrSubobjectIds;

		auto AddIfValidId = [&ActorOrSubobjectIds](Worker_ComponentId Id) { if (Id != SpatialConstants::INVALID_COMPONENT_ID) { ActorOrSubobjectIds.Add(Id); } };

		ForAllSchemaComponentTypes([&](ESchemaComponentType Type) {
			AddIfValidId(EntityClassInfo.SchemaComponents[Type]);
		});

		for (const TPair<uint32, TSharedRef<const FClassInfo>> SubobjectInfo : EntityClassInfo.SubobjectInfo)
		{
			ForAllSchemaComponentTypes([&](ESchemaComponentType Type) {
				AddIfValidId(SubobjectInfo.Value.Get().SchemaComponents[Type]);
			});
		}

		SpatialGDK::EntityFactory EntityFactory(NetDriver, PackageMap, NetDriver->ClassInfoManager, nullptr); //UE424_TODO - need a proper RPCService?
		SpatialGDK::FRPCsOnEntityCreationMap PendingRPCs{};
		uint32 BytesWritten;
		EntityComponents.Append(EntityFactory.CreateEntityComponents(Channel, PendingRPCs, BytesWritten));

		for (Worker_ComponentData& EntityComponent : EntityComponents)
		{
			uint32 OldId;
			const bool FoundOldId = SchemaBundleDefinitions::GetCorrespondingComponentId(NewSchemaBundleDefinitions, OldSchemaBundleDefinitions, EntityComponent.component_id, OldId);
			if (FoundOldId && OldComponentsById.Contains(OldId) && !UpdateComponent(EntityId, OldComponentsById.FindChecked(OldId), EntityComponent))
			{
				MigrationData.RecordSkippedEntity(EntityId, UnrealMetadata.ClassPath, FString{ TEXT("Encountered a problem while trying to update at least one component.") });
				UE_LOG(LogSnapshotMigrator, Display, TEXT("Failed to update component %s on entity %lld!"), *NewSchemaBundleDefinitions.FindComponentChecked(EntityComponent.component_id).GetName(SchemaBundleDefinitionWithFields::NameType::SHORT), Entity->entity_id);
				return false;
			}
		}

		MigratedComponents = MoveTemp(EntityComponents);
	}

	Worker_Entity NewEntity;
	NewEntity.entity_id = Entity->entity_id;
	NewEntity.components = MigratedComponents.GetData();
	NewEntity.component_count = MigratedComponents.Num();

	Worker_SnapshotOutputStream_WriteEntity(OutStream, &NewEntity);
	MigrationData.RecordMigratedEntity();
	return true;
}

bool USnapshotMigratorCommandlet::DoesEntityPassClassFilter(const FString& EntityActorClasspath)
{
	for (const FRegexPattern& Pattern : EntityActorClassFilters)
	{
		FRegexMatcher Matcher{ Pattern, EntityActorClasspath };
		if (Matcher.FindNext())
		{
			return true;
		}
	}

	return false;
}

bool USnapshotMigratorCommandlet::UpdateComponent(const uint32 EntityId, const Worker_ComponentData& OldComponent, Worker_ComponentData& Component)
{
	const Worker_ComponentUpdate Update = CreateComponentMigration(EntityId, OldComponent, Component.component_id);

	// If the Ids match, there is an update to apply.
	if (Update.component_id == Component.component_id)
	{
		Schema_Object* Obj = Schema_GetComponentUpdateFields(Update.schema_type);

		TArray<Schema_FieldId> AlteredFieldIds;
		AlteredFieldIds.SetNumUninitialized(Schema_GetUniqueFieldIdCount(Obj));
		Schema_GetUniqueFieldIds(Obj, AlteredFieldIds.GetData());

		TArray<Schema_FieldId> ClearedFieldIds;
		ClearedFieldIds.SetNumUninitialized(Schema_GetComponentUpdateClearedFieldCount(Update.schema_type));
		Schema_GetComponentUpdateClearedFieldList(Update.schema_type, ClearedFieldIds.GetData());

		for (const Schema_FieldId FieldId : ClearedFieldIds)
		{
			Schema_ClearField(Schema_GetComponentDataFields(Component.schema_type), FieldId);
		}

		const uint8_t ApplyResult = Schema_ApplyComponentUpdateToData(Update.schema_type, Component.schema_type);
		if (ApplyResult == 0)
		{
			const FString Error{ UTF8_TO_TCHAR(Schema_GetError(Schema_GetComponentDataFields(Component.schema_type))) };
			UE_LOG(LogSnapshotMigrator, Error, TEXT("Failed to migrate data forward onto component %ld: %s"), Component.component_id, *Error);
			return false;
		}
	}

	return true;
}

Worker_ComponentUpdate USnapshotMigratorCommandlet::CreateComponentMigration(const uint32 EntityId, const Worker_ComponentData& OldComponent, const Worker_ComponentId NewComponentId)
{
	Worker_ComponentUpdate Update;
	Update.component_id = NewComponentId;
	Update.schema_type = Schema_CreateComponentUpdate();

	Schema_Object* OldComponentSchemaObject = Schema_GetComponentDataFields(OldComponent.schema_type);
	Schema_Object* UpdateSchemaObject = Schema_GetComponentUpdateFields(Update.schema_type);

	bool bWroteUpdate = false;

	SnapshotDataMigrator Migrator(OldSchemaBundleDefinitions, NewSchemaBundleDefinitions);

	for (const SchemaBundleFieldDefinition& FieldDefinition : NewSchemaBundleDefinitions.FindComponentChecked(NewComponentId).GetFields())
	{
		// Only migrate fields which:
		//	- Exist in both the new and old component definitions
		//	- Have the same type
		const SchemaBundleFieldDefinition* OldFieldDefinition = OldSchemaBundleDefinitions.FindComponentChecked(OldComponent.component_id).FindField(FieldDefinition.GetName());
		if (OldFieldDefinition == nullptr || !OldFieldDefinition->IsSameTypeAs(FieldDefinition))
		{
			// If we aren't the same type, record this field as skipped
			if (OldFieldDefinition != nullptr)
			{
				MigrationData.RecordSkippedComponentFieldUpdate(EntityId, NewComponentId, FieldDefinition.GetName(), FString{ TEXT("Type mismatch between Old and New field definitions.") });
			}
			continue;
		}

		Schema_FieldId OldFieldId = OldFieldDefinition->GetId();
		Schema_FieldId NewFieldId = FieldDefinition.GetId();

		bool bMigratedSomething = false;

		if (FieldDefinition.IsMap())
		{
			// Right now we only support one map -- Entity ACLs.
			// Support for component interest migration is stubbed in, but non-functional right now.
			// Both maps are keyed with uint32
			if (FieldDefinition.IsPrimitive(SchemaBundleFieldDefinition::TypeIndex::KEY) && FieldDefinition.GetPrimitiveType(SchemaBundleFieldDefinition::TypeIndex::KEY) == SchemaBundleFieldDefinition::SchemaPrimitiveType::Uint32)
			{
				// Entity ACLs have a map value of improbable.WorkerRequirementSet
				if (FieldDefinition.IsType(SchemaBundleFieldDefinition::TypeIndex::VALUE) && FieldDefinition.GetResolvedType(SchemaBundleFieldDefinition::TypeIndex::VALUE).Equals(FString{ TEXT("improbable.WorkerRequirementSet") }))
				{
					bMigratedSomething = Migrator.MigrateObjectField(SchemaBundleFieldDefinition::WRITE_ACL_MAP, OldFieldId, NewFieldId, OldComponentSchemaObject, UpdateSchemaObject);
				}
				else if (FieldDefinition.IsType(SchemaBundleFieldDefinition::TypeIndex::VALUE) && FieldDefinition.GetResolvedType(SchemaBundleFieldDefinition::TypeIndex::VALUE).Equals(FString{ TEXT("improbable.ComponentInterest") }))
				{
					bMigratedSomething = Migrator.MigrateObjectField(SchemaBundleFieldDefinition::COMPONENT_INTEREST_MAP, OldFieldId, NewFieldId, OldComponentSchemaObject, UpdateSchemaObject);
				}
			}
		}
		else
		{
			if (FieldDefinition.IsPrimitive())
			{
				bMigratedSomething = Migrator.MigratePrimitiveField(FieldDefinition.GetPrimitiveType(), OldFieldId, NewFieldId, OldComponentSchemaObject, UpdateSchemaObject);
			}
			else
			{
				bMigratedSomething = Migrator.MigrateObjectField(FieldDefinition.GetResolvedType(), OldFieldId, NewFieldId, OldComponentSchemaObject, UpdateSchemaObject);
			}
		}

		if (!FieldDefinition.IsSingular() && !bMigratedSomething)
		{
			Schema_AddComponentUpdateClearedField(Update.schema_type, NewFieldId);
		}

		// If we migrated something or if we didn't but we're dealing with a collection field (clearing a field counts as an update!)
		bWroteUpdate |= bMigratedSomething || !FieldDefinition.IsSingular();
	}

	if (!bWroteUpdate)
	{
		Schema_DestroyComponentUpdate(Update.schema_type);
		// Set the Update's component id to the Invalid Component Id. We'll use this to check whether or not we have a "valid" update.
		Update.component_id = SpatialConstants::INVALID_COMPONENT_ID;
	}

	return Update;
}
