#ifndef OPENMW_COMPONENTS_ARCHIVEKINDS_ARCHIVEKINDS_H
#define OPENMW_COMPONENTS_ARCHIVEKINDS_ARCHIVEKINDS_H

#include <components/vfs/archivefactory.hpp>

// The Bethesda archive kinds as VFS archive factories (task 0083 stage 3, design doc game-packs.md 5.3).
//
// components/vfs knows no archive format; components/bsa knows no VFS type. This directory is the one place that
// includes both. A program that opens archives calls registerBethesdaArchiveKinds() once while it starts, before
// VFS::registerArchives(); the Eden pack reader stays built into VFS::makeArchive.
//
// Each factory decides by the file's magic bytes (Bsa::BSAFile::detectVersion), never by its extension, and returns
// nullptr for a file that is not of its kind, as every ArchiveFactory does. The factories are independent: a file is of
// at most one kind.

namespace VFS
{
    /// Version 0x100 (Morrowind), uncompressed.
    ArchiveFactory tes3ArchiveFactory();

    /// The "BSA" magic with a zero byte (Oblivion to Skyrim Special Edition). The compressed BSA reader then checks the version
    /// itself: a version it does not know throws an error naming the file.
    ArchiveFactory tes4ArchiveFactory();

    /// "BTDX" with "GNRL" or "DX10" (Fallout 4 and later).
    ArchiveFactory ba2ArchiveFactory();

    /// Adds the three factories above to the process-wide list. Safe to call more than once (a second call adds
    /// nothing); not thread-safe, like addArchiveFactory.
    void registerBethesdaArchiveKinds();
}

#endif
