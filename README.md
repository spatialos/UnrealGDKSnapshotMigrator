# UnrealGDK Snapshot Migrator

**Note that this plugin is marked as beta and should be considered to be an experimental work in progress.**

This plugin (for use with Unreal Engine 4 and the SpatialOS GDK for Unreal) provides a means to migrate the contents of a set of snapshots between one version of schema and another.

## Requirements

* Unreal Engine 4.24 (or greater)
* SpatialOS GDK for Unreal 0.9 (or greater)

## Installation

1. Download this plugin to either plugin folder (`Engine/Plugins` or `Game/Plugins`)
1. Add the `SPATIALGDK_API` tag to `USpatialReceiver`'s class definition (in `SpatialGDK/Source/SpatialGDK/Public/Interop/SpatialReceiver.h`)
    * This is currently required in order to allow instantiation of the `SpatialReceiver` in the Migrator; this is hopefully temporary
1. In `DefaultSnapshotClasspathWhitelistPatterns.ini`, replace `{ProjectName}` with your project's name.
    * This config file is used to gate which entities are migrated in  a snapshot; if an entity represents an Unreal Actor it will only be migrated if its classpath matches one of the patterns in this file.
1. Move `DefaultSnapshotClasspathWhitelistPatterns.ini` to your project's config folder (`Game/Config`)

## Usage

The migrator requires three pieces of data to work:
1. The snapshots you want to migrate; these are your **source snapshots**
1. A schema json bundle representing the schema used when the snapshots you want to migrate were generated; this is the **source bundle**.
1. A schema json bundle representing your current schema; this is the **target bundle**.

Schema json bundles can be obtained by invoking the Schema Compiler with the `--bundle_json_out` argument; if you are using the `CookAndGenerateSchema` commandlet provided by the GDK, you can pass this argument through to the compiler by invoking the commandlet with `-AdditionalSchemaCompilerArgs="--bundle_json_out=\"{path/to/bundle.sb.json}\""`.

The Snapshot Migrator is currently implemented as a commandlet and can be invoked as any other UE4 commandlet can. E.g., from the commandline: `path/to/UE4/Engine/Binaries/Win64/UE4Editor-Cmd.exe path/to/UE4/{Project}/Game/{Project}.uproject -Run=SnapshotMigrator`

By default, the migrator will expect to find the **source snapshots** and the **source bundle** at `{project spatial dir}/tmp/artifacts` and the **target bundle** at `{project spatial dir}/build/assembly/schema`. This can be overridden by passing `-OldArtifactsDir` or `-CompiledSchemaDir`, respectively.