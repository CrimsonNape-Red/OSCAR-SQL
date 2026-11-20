/* BMC Night Archive Implementation
 *
 * Moves what is new on a BMC card into its night archive. See bmc_archive.h.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "bmc_archive.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QObject>

#include <algorithm>
#include <exception>

#include "SleepLib/day.h"
#include "SleepLib/machine.h"
#include "SleepLib/machine_common.h"

BmcCaptureStats BmcCaptureToArchive(BmcDataParser &parser, const QString &sourceDir,
                                    CardArchive &archive, const QString &loaderName,
                                    const std::function<void(int done, int total)> &progress,
                                    const std::function<bool()> &aborted)
{
    BmcCaptureStats stats;

    // A missing or unreadable manifest is not an error: the archive is new, or is rebuilt
    // from its segments just below. One written by a later build must be left alone.
    archive.load();
    if (!archive.isWritable()) {
        qWarning() << "BMC backup: the archive cannot be written by this version of OSCAR; nothing was backed up";
        stats.failed = 1;
        return stats;
    }

    // Pick up segments that have no manifest entry: one left by a crash between writing
    // the segment and the manifest, or all of them if the manifest was lost.
    archive.adoptOrphans(parser.PacketSize(),
                         [&parser](const QByteArray &packets, QDateTime &first, QDateTime &last) {
                             return parser.DescribePackets(packets, first, last);
                         });
    archive.setIdentity(loaderName, parser.BaseName());

    // Index files first: with them in place, a run that is cut short still leaves an
    // archive whose index covers every segment it holds.
    const QStringList indexNames = parser.IndexFileNames();
    QDate indexFirst, indexLast;
    if (parser.IndexDateRange(&indexFirst, &indexLast)) {
        if (!archive.installIndexSet(sourceDir, indexNames, indexFirst, indexLast)) {
            qWarning() << "BMC backup: could not update the index files in the backup";
            ++stats.failed;
        }
    }

    // Everything else on the card except the ring files, which are never copied as files,
    // and an archive's own parts when the source is itself an archive.
    const bool othersCopied = archive.mirrorOtherFiles(sourceDir, [&](const QString &relativePath) {
        const QString top = relativePath.section(QLatin1Char('/'), 0, 0);
        if (top != relativePath) {
            // An archive's own folders, and the folders an operating system keeps on a
            // removable drive, which are not card data and may not be readable.
            return top == CardArchive::segmentDirName() || top == CardArchive::generationDirName()
                   || top == BmcUnconvertedDirName()
                   || top.compare(QLatin1String("System Volume Information"), Qt::CaseInsensitive) == 0
                   || top.compare(QLatin1String("$RECYCLE.BIN"), Qt::CaseInsensitive) == 0;
        }
        return relativePath == CardArchive::manifestName()
               || indexNames.contains(relativePath, Qt::CaseInsensitive)
               || parser.IsRingFileName(relativePath);
    });
    if (!othersCopied) {
        qWarning() << "BMC backup: could not copy all of the card's other files to the backup";
        ++stats.failed;
    }

    QList<BmcArchiveRecord> records = parser.ArchiveRecords();
    std::stable_sort(records.begin(), records.end(), [](const BmcArchiveRecord &a, const BmcArchiveRecord &b) {
        return a.info.date < b.info.date;
    });
    stats.offered = static_cast<int>(records.size());

    // Decide from the index alone what needs reading, so an unchanged card costs nothing.
    QList<int> wanted;
    for (int i = 0; i < records.size(); ++i) {
        if (archive.wants(records.at(i).info)) {
            wanted.append(i);
        } else {
            ++stats.skipped;
        }
    }

    const int total = static_cast<int>(wanted.size());
    int done = 0;
    if (progress) {
        progress(done, total);
    }

    for (const int index : wanted) {
        if (aborted && aborted()) {
            stats.aborted = true;
            break;
        }

        BmcArchiveRecord &record = records[index];
        const QByteArray packets = parser.ReadArchiveRecordBytes(record, aborted);
        if (aborted && aborted()) {
            stats.aborted = true;       // an abandoned read returns nothing; do not take it for an empty night
            break;
        }

        if (record.readFailed) {
            // Not "no waveform": the data is there and could not be read. Counting it as
            // failed is what keeps a conversion from deleting the ring files it came from.
            qWarning() << "BMC backup: could not read the waveform for"
                       << record.info.date.toString(Qt::ISODate) << "from" << sourceDir;
            ++stats.failed;
        } else if (packets.isEmpty()) {
            ++stats.skipped;            // indexed, but the card holds no waveform for it
        } else {
            switch (archive.store(record.info, packets, parser.PacketSize())) {
            case CardArchive::StoreResult::Stored:
                ++stats.stored;
                break;
            case CardArchive::StoreResult::Replaced:
                ++stats.replaced;
                break;
            case CardArchive::StoreResult::KeptExisting:
                ++stats.skipped;
                break;
            case CardArchive::StoreResult::Failed:
                qWarning() << "BMC backup: could not store the waveform for"
                           << record.info.date.toString(Qt::ISODate);
                ++stats.failed;
                break;
            }
        }

        ++done;
        if (progress) {
            progress(done, total);
        }
    }

    qDebug() << "BMC backup:" << stats.offered << "nights on the card -" << stats.stored << "stored,"
             << stats.replaced << "replaced," << stats.skipped << "already held or empty,"
             << stats.failed << "failed" << (stats.aborted ? "(cancelled)" : "");
    return stats;
}

int BmcRemoveRingFiles(const BmcDataParser &parser, const QString &dir)
{
    int removed = 0;
    const QDir folder(dir);
    const QStringList names = folder.entryList(QDir::Files | QDir::Hidden);
    for (const QString &name : names) {
        if (!parser.IsRingFileName(name)) {
            continue;
        }
        QFile file(folder.absoluteFilePath(name));
        // Files copied from a write-protected card keep the read-only attribute.
        file.setPermissions(file.permissions() | QFileDevice::WriteOwner | QFileDevice::WriteUser);
        if (file.remove()) {
            ++removed;
        } else {
            qWarning() << "BMC backup: could not remove the old ring file" << name << "-" << file.errorString();
        }
    }
    return removed;
}

bool BmcHasRingFiles(const BmcDataParser &parser, const QString &dir)
{
    const QStringList names = QDir(dir).entryList(QDir::Files | QDir::Hidden);
    for (const QString &name : names) {
        if (parser.IsRingFileName(name)) {
            return true;
        }
    }
    return false;
}

BmcArchiveReadReport BmcArchiveReadCheck(const QList<BmcDataParser *> &parsers, const CardArchive &archive)
{
    BmcArchiveReadReport report;

    QList<QDate> described;
    for (BmcDataParser *parser : parsers) {
        if (parser == nullptr) {
            continue;
        }
        for (const QDate &night : parser->UnreadableNights()) {
            if (!report.unreadable.contains(night)) {
                report.unreadable.append(night);
            }
        }
        for (const BmcArchiveRecord &record : parser->ArchiveRecords()) {
            described.append(record.info.date);
        }
    }

    for (const CardArchiveEntry &entry : archive.entries()) {
        if (!described.contains(entry.date) && !report.unclaimed.contains(entry.date)) {
            report.unclaimed.append(entry.date);
        }
    }

    std::sort(report.unreadable.begin(), report.unreadable.end());
    std::sort(report.unclaimed.begin(), report.unclaimed.end());
    return report;
}

namespace {

//! A parser on \a dir with ReadData() done, reading waveforms from \a archive if given.
//! Null if the folder cannot be parsed.
std::unique_ptr<BmcDataParser> openParser(const QString &dir, CardArchive *archive,
                                          const BmcParserFactory &factory)
{
    std::unique_ptr<BmcDataParser> parser = factory(dir);
    if (!parser) {
        return nullptr;
    }
    try {
        parser->SetArchive(archive);
        parser->ReadData();
    } catch (const std::exception &e) {
        qWarning() << "BMC: could not read the card data in" << dir << "-" << e.what();
        return nullptr;
    }
    return parser;
}

//! Add \a stats to \a total.
void addStats(BmcCaptureStats &total, const BmcCaptureStats &stats)
{
    total.offered += stats.offered;
    total.skipped += stats.skipped;
    total.stored += stats.stored;
    total.replaced += stats.replaced;
    total.failed += stats.failed;
    total.aborted = total.aborted || stats.aborted;
}

//! The ring files in \a dir: by the parser's own rule when it knows the card's base name,
//! otherwise any file with a three-digit extension (a BMC Backup folder holds no others).
QStringList ringFilesIn(const QString &dir, const BmcDataParser *probe)
{
    const bool knowsBase = (probe != nullptr) && !probe->BaseName().isEmpty();
    QStringList found;
    const QStringList names = QDir(dir).entryList(QDir::Files | QDir::Hidden, QDir::Name);
    for (const QString &name : names) {
        bool ring;
        if (knowsBase) {
            ring = probe->IsRingFileName(name);
        } else {
            const QString suffix = QFileInfo(name).suffix();
            ring = suffix.size() == 3 && suffix.at(0).isDigit() && suffix.at(1).isDigit() && suffix.at(2).isDigit();
        }
        if (ring) {
            found.append(name);
        }
    }
    return found;
}

//! Delete \a names in \a dir. Returns how many went.
int removeFiles(const QString &dir, const QStringList &names)
{
    int removed = 0;
    for (const QString &name : names) {
        QFile file(QDir(dir).absoluteFilePath(name));
        file.setPermissions(file.permissions() | QFileDevice::WriteOwner | QFileDevice::WriteUser);
        if (file.remove()) {
            ++removed;
        }
    }
    return removed;
}

//! Move \a names from \a dir into its BmcUnconvertedDirName() folder. Returns how many moved.
int moveAside(const QString &dir, const QStringList &names)
{
    const QDir folder(dir);
    if (!folder.mkpath(BmcUnconvertedDirName())) {
        return 0;
    }
    int moved = 0;
    for (const QString &name : names) {
        QString target = folder.absoluteFilePath(BmcUnconvertedDirName() + QLatin1Char('/') + name);
        for (int n = 2; QFile::exists(target); ++n) {
            target = folder.absoluteFilePath(BmcUnconvertedDirName() + QLatin1Char('/') + name
                                             + QLatin1Char('.') + QString::number(n));
        }
        if (QFile::rename(folder.absoluteFilePath(name), target)) {
            ++moved;
        }
    }
    return moved;
}

//! Read every segment of \a archive back and check it. A damaged one is set aside, so the
//! next import takes that night from the old copy again. Returns how many did not read.
int verifySegments(CardArchive &archive, const std::function<bool()> &aborted, bool *wasAborted)
{
    int bad = 0;
    const QList<CardArchiveEntry> entries = archive.entries();
    for (const CardArchiveEntry &entry : entries) {
        if (aborted && aborted()) {
            if (wasAborted != nullptr) { *wasAborted = true; }
            return bad;
        }
        bool ok = false;
        CardArchive::ReadStatus status = CardArchive::ReadStatus::Unavailable;
        archive.read(entry, &ok, &status);
        if (!ok) {
            ++bad;
            qWarning() << "BMC backup: segment" << entry.file << "did not read back; keeping the old card copy";
            if (status == CardArchive::ReadStatus::Damaged) {
                archive.quarantine(entry);
            }
        }
    }
    return bad;
}

//! Up to ten of \a dates in the user's short date format, then how many were left out.
QString listDates(const QList<QDate> &dates)
{
    constexpr int kMaxListed = 10;
    QStringList shown;
    for (int i = 0; i < dates.size() && i < kMaxListed; ++i) {
        shown.append(QLocale().toString(dates.at(i), QLocale::ShortFormat));
    }
    QString text = shown.join(QStringLiteral(", "));
    const int left = static_cast<int>(dates.size()) - kMaxListed;
    if (left > 0) {
        text += QStringLiteral(", ") + QObject::tr("and %n more", "", left);
    }
    return text;
}

} // namespace

BmcImportSource BmcOpenImportSource(const QString &dir, bool ownBackup, const BmcParserFactory &factory)
{
    BmcImportSource source;
    source.isArchive = CardArchive::isArchive(dir);

    if (source.isArchive) {
        if (CardArchive::isNewerFormat(dir)) {
            // Written by a later version of OSCAR. Reading it by this build's rules could
            // bring nights in without their waveform and say nothing, so it is not read.
            qWarning() << "BMC: the backup in" << dir << "was written by a later version of OSCAR and cannot be imported by this one";
            return source;
        }
        source.archive = std::make_unique<CardArchive>(dir);
        source.archive->load();
        if (!ownBackup) {
            source.archive->setReadOnly(true);
        }

        const std::unique_ptr<BmcDataParser> probe = factory(dir);
        if (probe && BmcHasRingFiles(*probe, dir) && !source.archive->conversionComplete()) {
            // Ring files and no record that their conversion finished: a full card copy
            // part-way through conversion. Its ring files are still the complete record,
            // so read it as the card it was.
            qDebug() << "BMC:" << dir << "is a card copy part-way through conversion; reading its ring files";
            source.isArchive = false;
            source.archive.reset();
        } else if (probe) {
            // Segments without a manifest entry are still nights: a manifest that was lost
            // or cannot be read is rebuilt from them (in memory only for a read-only
            // archive), so the import does not quietly come in without waveforms.
            const BmcDataParser *describer = probe.get();
            const int adopted = source.archive->adoptOrphans(describer->PacketSize(),
                [describer](const QByteArray &packets, QDateTime &first, QDateTime &last) {
                    return describer->DescribePackets(packets, first, last);
                });
            if (adopted > 0) {
                qWarning() << "BMC: the backup's list of nights was incomplete;" << adopted << "segments were re-listed";
            }
        }
    }

    if (!source.isArchive) {
        std::unique_ptr<BmcDataParser> parser = openParser(dir, nullptr, factory);
        if (parser) {
            source.parsers.push_back(std::move(parser));
            source.dirs.append(dir);
        }
        return source;
    }

    // The current index set, then each retired one: a night described only by an earlier
    // generation is imported from that generation's index.
    QStringList folders;
    folders.append(dir);
    folders.append(source.archive->generationDirs());
    for (const QString &folder : folders) {
        std::unique_ptr<BmcDataParser> parser = openParser(folder, source.archive.get(), factory);
        if (parser) {
            source.parsers.push_back(std::move(parser));
            source.dirs.append(folder);
        } else if (folder == dir) {
            // Without the current index set there is nothing to anchor the import to.
            source.parsers.clear();
            source.dirs.clear();
            return source;
        }
    }
    return source;
}

BmcArchiveReadReport BmcFinishArchiveImport(BmcImportSource &source)
{
    if (!source.isArchive || !source.archive) {
        return BmcArchiveReadReport();
    }

    QList<BmcDataParser *> parsers;
    for (const std::unique_ptr<BmcDataParser> &parser : source.parsers) {
        parsers.append(parser.get());
    }
    const BmcArchiveReadReport report = BmcArchiveReadCheck(parsers, *source.archive);

    if (source.archive->isWritable()) {
        // Take out what is damaged, so a card that still holds the night can supply it again.
        // Another record sharing the date is checked by itself and kept if it reads.
        for (const QDate &night : report.unreadable) {
            const QList<CardArchiveEntry> entries = source.archive->entries();
            for (const CardArchiveEntry &entry : entries) {
                if (entry.date != night) {
                    continue;
                }
                bool ok = false;
                CardArchive::ReadStatus status = CardArchive::ReadStatus::Unavailable;
                source.archive->read(entry, &ok, &status);
                // Only a file that opened and proved wrong is damaged, and even that is
                // set aside, not deleted. One that could not be opened just now (locked,
                // not downloaded) is left alone: this may be the only copy of the night.
                if (!ok && status == CardArchive::ReadStatus::Damaged) {
                    source.archive->quarantine(entry);
                }
            }
        }
    }
    return report;
}

QString BmcArchiveReadWarning(const BmcArchiveReadReport &report)
{
    QStringList parts;
    if (!report.unreadable.isEmpty()) {
        // Importing the card puts the night back in the backup, but an import only
        // re-reads the newest day, so the graphs return with the rebuild after that.
        parts.append(QObject::tr("%n night(s) in the backup could not be read and were restored without detailed graphs. If the SD card still holds them, import the card and then rebuild again to get the graphs back.",
                                 "", static_cast<int>(report.unreadable.size()))
                     + QStringLiteral("\n") + listDates(report.unreadable));
    }
    if (!report.unclaimed.isEmpty()) {
        parts.append(QObject::tr("%n night(s) in the backup have no matching index record and were not imported.",
                                 "", static_cast<int>(report.unclaimed.size()))
                     + QStringLiteral("\n") + listDates(report.unclaimed));
    }
    return parts.join(QStringLiteral("\n\n"));
}

BmcBackupOutcome BmcBackUpImportSource(BmcImportSource &source, const QString &backupDir,
                                       const BmcParserFactory &factory,
                                       const std::function<bool(const QString &)> &isCardFolder,
                                       const QString &loaderName,
                                       const std::function<void(int done, int total)> &progress,
                                       const std::function<bool()> &aborted)
{
    BmcBackupOutcome outcome;
    CardArchive target(backupDir);

    // A Backup folder that holds ring files is a full copy of the card, as earlier
    // versions of OSCAR kept it (with a manifest too, if an earlier conversion was cut
    // short). It may hold nights the card has since overwritten, so it is captured first,
    // in place.
    if (QDir(backupDir).exists()) {
        const std::unique_ptr<BmcDataParser> probe = factory ? factory(backupDir) : nullptr;
        const QStringList ringFiles = ringFilesIn(backupDir, probe.get());
        if (!ringFiles.isEmpty()) {
            target.load();
            std::unique_ptr<BmcDataParser> mirror;
            if (!target.conversionComplete() && isCardFolder && isCardFolder(backupDir)) {
                mirror = openParser(backupDir, nullptr, factory);
            }

            if (target.conversionComplete()) {
                // The copy was captured in full on an earlier import and the deletion of
                // its ring files was cut short. Finish that.
                outcome.ringFilesRemoved = removeFiles(backupDir, ringFiles);
                qDebug() << "BMC backup: removed" << outcome.ringFilesRemoved
                         << "leftover ring files of the old card copy";
            } else if (!mirror) {
                // Ring files that cannot be read as a card: the remains of an old copy that
                // was cut short. They cannot be converted, must not be deleted, and must
                // not stay where a rebuild would take the folder for a card copy.
                outcome.setAside = moveAside(backupDir, ringFiles);
                qWarning() << "BMC backup:" << outcome.setAside << "ring files of an unreadable old copy were moved to"
                           << BmcUnconvertedDirName();
                if (outcome.setAside != ringFiles.size()) {
                    qWarning() << "BMC backup: could not move all of them; the new nights will be backed up once they are out of the way";
                    outcome.capture.aborted = true;
                    return outcome;
                }
            } else {
                qDebug() << "BMC backup: converting the full card copy in" << backupDir << "to a night archive";
                outcome.migrated = true;
                outcome.migration = BmcCaptureToArchive(*mirror, backupDir, target, loaderName, progress, aborted);
                if (outcome.migration.ok()) {
                    // The ring files are about to be the only other copy no longer. Read
                    // every segment back and check it before that.
                    outcome.migration.failed += verifySegments(target, aborted, &outcome.migration.aborted);
                }
                // Record that the copy is captured in full before removing any of it:
                // from here on the folder is an archive, however far the deletion gets.
                if (outcome.migration.ok() && target.markConversionComplete()) {
                    outcome.ringFilesRemoved = BmcRemoveRingFiles(*mirror, backupDir);
                    qDebug() << "BMC backup: removed" << outcome.ringFilesRemoved
                             << "ring files of the old card copy";
                }
                if (!outcome.migration.ok() || !target.conversionComplete()) {
                    // The copy is still the complete record and is what a rebuild would
                    // read. Adding nights it does not hold would make them invisible to
                    // that rebuild, so leave everything as it is until a later import
                    // finishes the conversion.
                    qWarning() << "BMC backup: the old card copy could not be fully converted; the new nights will be backed up once it is";
                    outcome.capture.aborted = true;
                    return outcome;
                }
            }
        }
    }

    // Retired generations first, oldest first, then the current index set: the archive's
    // index guard then files each set where it belongs.
    QList<int> order;
    for (int i = 1; i < static_cast<int>(source.parsers.size()); ++i) {
        order.append(i);
    }
    if (!source.parsers.empty()) {
        order.append(0);
    }
    for (const int i : order) {
        const BmcCaptureStats stats = BmcCaptureToArchive(*source.parsers.at(static_cast<size_t>(i)),
                                                          source.dirs.at(i), target, loaderName,
                                                          progress, aborted);
        addStats(outcome.capture, stats);
        if (stats.aborted) {
            break;
        }
    }
    return outcome;
}

QList<BmcStoredNight> BmcStoredNights(Machine *mach)
{
    QList<BmcStoredNight> nights;
    if (mach == nullptr) {
        return nights;
    }
    for (auto it = mach->day.constBegin(); it != mach->day.constEnd(); ++it) {
        Day *day = it.value();
        // The stored sample count of the flow channel says whether a waveform was
        // imported, without loading any event data. A count above zero is the test:
        // an events-only legacy BMC session still has a flow-channel summary row, with a
        // count of zero, so the row's mere presence (channelHasData) proves nothing; an
        // events-only G3X session has no row at all.
        if (day == nullptr || day->count(CPAP_FlowRate) <= 0) {
            continue;
        }
        BmcStoredNight night;
        night.day = it.key();
        night.first = QDateTime::fromMSecsSinceEpoch(day->first());
        night.last = QDateTime::fromMSecsSinceEpoch(day->last());
        nights.append(night);
    }
    return nights;
}

QList<BmcStoredNight> BmcNightsNotInArchive(const QList<BmcStoredNight> &nights, const CardArchive &archive)
{
    QList<BmcStoredNight> missing;
    for (const BmcStoredNight &night : nights) {
        if (!archive.coversSpan(night.first, night.last)) {
            missing.append(night);
        }
    }
    return missing;
}

QString BmcRebuildLossText(const QList<BmcStoredNight> &missing)
{
    if (missing.isEmpty()) {
        return QString();
    }
    QDate first = missing.first().day;
    QDate last = first;
    for (const BmcStoredNight &night : missing) {
        first = std::min(first, night.day);
        last = std::max(last, night.day);
    }
    return QObject::tr("%n night(s) now have detailed graphs that this backup cannot restore (%1 to %2). After rebuilding they will show events and statistics only.",
                       "", static_cast<int>(missing.size()))
        .arg(QLocale().toString(first, QLocale::ShortFormat), QLocale().toString(last, QLocale::ShortFormat));
}

QString BmcRebuildCardCopyText()
{
    return QObject::tr("This backup holds only the most recent contents of the SD card. Older nights that now have detailed graphs will show events and statistics only after rebuilding.");
}

QString BmcRebuildLossTextFromArchive(const CardArchive &archive)
{
    const QDate indexFirst = archive.indexFirstDate();
    if (!indexFirst.isValid()) {
        return QString();
    }
    const QList<CardArchiveEntry> entries = archive.entries();     // sorted by date
    if (entries.isEmpty()) {
        return QObject::tr("This backup holds no detailed graph data. After rebuilding, every night will show events and statistics only.");
    }
    const QDate firstSegment = entries.first().date;
    if (firstSegment <= indexFirst) {
        return QString();
    }
    return QObject::tr("This backup holds detailed graph data from %1 on. Earlier nights will show events and statistics only after rebuilding.")
        .arg(QLocale().toString(firstSegment, QLocale::ShortFormat));
}

QString BmcUnconvertedDirName()
{
    return QStringLiteral("Unconverted");
}

QString BmcBackupWarning(const BmcBackupOutcome &outcome)
{
    if (outcome.cancelledByUser) {
        return QString();
    }

    QStringList parts;
    if (outcome.setAside > 0) {
        parts.append(QObject::tr("Files left by an earlier, unfinished backup of this device could not be read. They were moved to the \"%1\" folder inside its Backup folder; nothing was deleted.")
                         .arg(BmcUnconvertedDirName()));
    }
    if (outcome.migrated && !outcome.migration.ok()) {
        parts.append(QObject::tr("OSCAR could not finish converting this device's earlier backup to the new format, so the nights from this import have not been backed up yet. The imported data itself is not affected. OSCAR will try again at the next import; if this message keeps appearing, check that the disk holding OSCAR's data has free space and that the backup files can be read."));
    } else if (outcome.capture.failed > 0 || outcome.capture.aborted) {
        parts.append(QObject::tr("OSCAR could not bring this device's backup up to date. The imported data itself is not affected. OSCAR will try again at the next import; if this message keeps appearing, check that the disk holding OSCAR's data has free space."));
    }
    return parts.join(QStringLiteral("\n\n"));
}
