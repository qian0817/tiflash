// Copyright 2022 PingCAP, Ltd.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <Common/Checksum.h>
#include <Storages/Page/V3/LogFile/LogFilename.h>
#include <Storages/Page/V3/LogFile/LogFormat.h>
#include <Storages/Page/V3/LogFile/LogWriter.h>
#include <Storages/Page/V3/PageEntriesEdit.h>
#include <common/types.h>

#include <memory>

namespace DB
{
class FileProvider;
using FileProviderPtr = std::shared_ptr<FileProvider>;
class WriteLimiter;
using WriteLimiterPtr = std::shared_ptr<WriteLimiter>;
class PSDiskDelegator;
using PSDiskDelegatorPtr = std::shared_ptr<PSDiskDelegator>;
namespace PS::V3
{
enum class WALRecoveryMode : UInt8
{
    // Original levelDB recovery
    //
    // We tolerate the last record in any log to be incomplete due to a crash
    // while writing it. Zeroed bytes from preallocation are also tolerated in the
    // trailing data of any log.
    //
    // Use case: Applications for which updates, once applied, must not be rolled
    // back even after a crash-recovery. In this recovery mode, RocksDB guarantees
    // this as long as `WritableFile::Append()` writes are durable. In case the
    // user needs the guarantee in more situations (e.g., when
    // `WritableFile::Append()` writes to page cache, but the user desires this
    // guarantee in face of power-loss crash-recovery), RocksDB offers various
    // mechanisms to additionally invoke `WritableFile::Sync()` in order to
    // strengthen the guarantee.
    //
    // This differs from `kPointInTimeRecovery` in that, in case a corruption is
    // detected during recovery, this mode will refuse to open the DB. Whereas,
    // `kPointInTimeRecovery` will stop recovery just before the corruption since
    // that is a valid point-in-time to which to recover.
    TolerateCorruptedTailRecords = 0x00,
    // Recover from clean shutdown
    // We don't expect to find any corruption in the WAL
    // Use case : This is ideal for unit tests and rare applications that
    // can require high consistency guarantee
    AbsoluteConsistency = 0x01,
    // Recover to point-in-time consistency (default)
    // We stop the WAL playback on discovering WAL inconsistency
    // Use case : Ideal for systems that have disk controller cache like
    // hard disk, SSD without super capacitor that store related data
    PointInTimeRecovery = 0x02,
    // Recovery after a disaster
    // We ignore any corruption in the WAL and try to salvage as much data as
    // possible
    // Use case : Ideal for last ditch effort to recover data or systems that
    // operate with low grade unrelated data
    SkipAnyCorruptedRecords = 0x03,
};

class WALStore;
using WALStorePtr = std::unique_ptr<WALStore>;

class WALStoreReader;
using WALStoreReaderPtr = std::shared_ptr<WALStoreReader>;

class WALStore
{
public:
    constexpr static const char * wal_folder_prefix = "/wal";

    static std::pair<WALStorePtr, WALStoreReaderPtr>
    create(
        String storage_name,
        FileProviderPtr & provider,
        PSDiskDelegatorPtr & delegator);

    void apply(PageEntriesEdit & edit, const PageVersionType & version, const WriteLimiterPtr & write_limiter = nullptr);
    void apply(const PageEntriesEdit & edit, const WriteLimiterPtr & write_limiter = nullptr);

    struct FilesSnapshot
    {
        Format::LogNumberType current_writting_log_num;
        LogFilenameSet persisted_log_files;

        bool needSave() const
        {
            // TODO: Make it configurable and check the reasonable of this number
            return persisted_log_files.size() > 4;
        }
    };

    FilesSnapshot getFilesSnapshot() const;

    bool saveSnapshot(
        FilesSnapshot && files_snap,
        PageEntriesEdit && directory_snap,
        const WriteLimiterPtr & write_limiter = nullptr);

private:
    WALStore(
        String storage_name,
        const PSDiskDelegatorPtr & delegator_,
        const FileProviderPtr & provider_,
        Format::LogNumberType last_log_num_);

    std::tuple<std::unique_ptr<LogWriter>, LogFilename>
    createLogWriter(
        const std::pair<Format::LogNumberType, Format::LogNumberType> & new_log_lvl,
        bool manual_flush);

    PSDiskDelegatorPtr delegator;
    FileProviderPtr provider;
    mutable std::mutex log_file_mutex;
    Format::LogNumberType last_log_num;
    // select next path for creating new logfile
    UInt32 wal_paths_index;
    std::unique_ptr<LogWriter> log_file;

    LoggerPtr logger;
};

} // namespace PS::V3
} // namespace DB
