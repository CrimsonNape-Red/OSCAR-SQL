/* BMC Night Archive Header
 *
 * Drives a BMC card parser into a CardArchive: the part of the night-archive backup that
 * is the same for the legacy BMC and BMC G3X loaders (GitLab #380). The parsers know the
 * card formats; CardArchive knows the storage rules; the functions here walk the card's
 * records and move what is new from one to the other.
 *
 * Design: Notes/Design/2026-11-19-ring-buffer-backup-archive-design.md
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef BMC_ARCHIVE_H
#define BMC_ARCHIVE_H

#include <QDate>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>
#include <vector>

#include "SleepLib/card_archive.h"
#include "SleepLib/loader_plugins/bmcDataParsing.h"

class Machine;

/*! \struct BmcCaptureStats
    \brief What one BmcCaptureToArchive() run did.
 */
struct BmcCaptureStats {
    int offered = 0;    //!< records the parser listed
    int skipped = 0;    //!< already held, nothing on the card, or no better than what is held
                        //!< (a record that could not be read counts as failed, not skipped)
    int stored = 0;     //!< new segments written
    int replaced = 0;   //!< segments replaced by a longer or newer-rule capture
    int failed = 0;     //!< things that could not be written (segments, index set, other files)
    bool aborted = false;   //!< stopped early because the abort callback said so

    //! \brief True when everything the card offered is now safely in the archive.
    bool ok() const { return failed == 0 && !aborted; }
};

/*! \brief Bring \a archive up to date with everything \a parser describes in \a sourceDir.

    Installs the card's index files (as CardArchive::installIndexSet() decides), copies its
    other files, then offers every record to the archive and reads from the card only
    those the archive wants. Segments left without a manifest entry by an interrupted run
    are adopted first.

    Works the same when \a parser is itself reading an archive (copying one archive into
    another) and when \a sourceDir is the archive's own folder (an old full-card mirror
    being converted in place).

    \param parser     a parser on \a sourceDir on which ReadData() has been called
    \param sourceDir  the folder the parser reads
    \param archive    the archive to update; it is (re)loaded here
    \param loaderName recorded in a new archive's manifest
    \param progress   called with (records done, records to read); may be empty. An
                      unchanged card reports a total of at most two.
    \param aborted    polled between and during reads; may be empty
    \return counts of what happened; nothing is written when the archive is not writable
 */
BmcCaptureStats BmcCaptureToArchive(BmcDataParser &parser, const QString &sourceDir,
                                    CardArchive &archive, const QString &loaderName,
                                    const std::function<void(int done, int total)> &progress,
                                    const std::function<bool()> &aborted);

/*! \brief Delete the ring files of an old full-card mirror in \a dir.

    Only call this after the mirror's nights have been captured into the archive.

    \param parser a parser for the card, used to tell which files are ring files
    \return the number of files removed
 */
int BmcRemoveRingFiles(const BmcDataParser &parser, const QString &dir);

/*! \brief True if \a dir holds any of the card's ring files.

    In a Backup folder that means a full copy of the card from an earlier version of
    OSCAR whose conversion to a night archive has not finished, whether or not a manifest
    is already there.
 */
bool BmcHasRingFiles(const BmcDataParser &parser, const QString &dir);

/*! \struct BmcArchiveReadReport
    \brief What an archive held that an import from it did not bring in in full.
 */
struct BmcArchiveReadReport {
    QList<QDate> unreadable;   //!< segment listed in the manifest but damaged or missing
    QList<QDate> unclaimed;    //!< segment for a date that no index describes

    //! \brief True when every segment was imported.
    bool clean() const { return unreadable.isEmpty() && unclaimed.isEmpty(); }
};

/*! \brief After an import from an archive: which of its nights were not imported in full.

    \param parsers the parser used for the archive's index set and one for each retired
                   generation, all reading \a archive, after the import has read its nights
    \param archive the archive that was imported from
 */
BmcArchiveReadReport BmcArchiveReadCheck(const QList<BmcDataParser *> &parsers,
                                         const CardArchive &archive);

// ---------------------------------------------------------------------------------
// The steps of a loader's Open() that both BMC loaders share
// ---------------------------------------------------------------------------------

//! \brief Makes the right parser (legacy or G3X) for a folder.
using BmcParserFactory = std::function<std::unique_ptr<BmcDataParser>(const QString &dir)>;

/*! \struct BmcImportSource
    \brief The folder an import reads, opened: its parsers, and its archive if it is one.

    A card has one parser. An archive has one for its current index set and one for each
    retired generation, all reading waveforms from the same archive; between them they
    describe every night the archive holds.
 */
struct BmcImportSource {
    bool isArchive = false;                               //!< the folder is read as a night archive
    std::unique_ptr<CardArchive> archive;                 //!< that archive; null for a card
    std::vector<std::unique_ptr<BmcDataParser>> parsers;  //!< current index first, then generations oldest first
    QStringList dirs;                                     //!< the folder each parser reads, in the same order
};

/*! \brief Open the folder an import was pointed at, ready to list and read its nights.

    A folder that still holds ring files is read as a card even if it also has a manifest:
    that is a Backup folder whose conversion from a full card copy was interrupted. Until
    the conversion completes no card is captured into it (see BmcBackUpImportSource()), so
    its ring files still hold every night its segments do.

    \param dir       the card folder, or a folder holding a night archive
    \param ownBackup true if \a dir is the device's own Backup folder. Any other archive
                     is opened read-only, so nothing the import does can change it.
    \param factory   makes a parser for a folder
    \return the opened source; parsers is empty if \a dir could not be parsed. ReadData()
            has been called on every parser returned.
 */
BmcImportSource BmcOpenImportSource(const QString &dir, bool ownBackup, const BmcParserFactory &factory);

/*! \brief After the import tasks of an archive import have run: report what the archive
           held that could not be imported in full, and clear out what is damaged.

    In the device's own Backup folder a damaged segment (one that opens but does not
    hold what its entry describes) is moved to Segments/Damaged/, so that the next import
    of a card that still holds that night stores it again while the file itself is kept.
    A segment that merely could not be opened is left exactly where it is: during a
    rebuild the archive may be the only copy. A read-only archive is only reported on.
 */
BmcArchiveReadReport BmcFinishArchiveImport(BmcImportSource &source);

/*! \brief The message to show the user for \a report; empty when it is clean.

    States how many nights could not be read, and how many segments no index describes,
    each followed by up to ten of the dates.
 */
QString BmcArchiveReadWarning(const BmcArchiveReadReport &report);

/*! \struct BmcBackupOutcome
    \brief What BmcBackUpImportSource() did.
 */
struct BmcBackupOutcome {
    int setAside = 0;             //!< ring files of an old copy that cannot be read as a card, moved
                                  //!< to the BmcUnconvertedDirName() folder
    bool cancelledByUser = false; //!< the user cancelled; set by the caller, for BmcBackupWarning()
    bool migrated = false;        //!< the Backup folder held a full card copy and its conversion was run
    BmcCaptureStats migration;    //!< the conversion of that copy
    BmcCaptureStats capture;      //!< the capture of the import source, summed over its parsers;
                                  //!< not attempted (and marked aborted) if the conversion did not complete
    int ringFilesRemoved = 0;     //!< old ring files deleted once the copy's nights were all stored

    //! \brief True when everything that was attempted succeeded.
    bool ok() const { return capture.ok() && (!migrated || migration.ok()); }
};

/*! \brief Bring the device's Backup folder up to date with what was just imported.

    If the Backup folder still holds a full copy of a card, as earlier versions of OSCAR
    left it, its nights are captured into the archive first: it can hold nights the card
    has since overwritten. Once every one of them is stored, and every segment has been
    read back and checked, the copy's ring files are deleted. Only then is the import
    source captured: while ring files remain, nothing
    else is added, so the copy stays a complete source for a rebuild (it is read as a
    card, see BmcOpenImportSource()). An interrupted conversion is resumed by the next
    import. Ring files that cannot be read as a card at all (the remains of an old copy
    that was cut short) cannot be converted; they are moved to BmcUnconvertedDirName() and
    the backup carries on without them.

    \param source       the opened import source (a card, or another archive)
    \param backupDir    the device's Backup folder; must not be the source's own folder
    \param factory      makes a parser for a folder
    \param isCardFolder the loader's Detect(): does a folder hold this loader's card data?
    \param loaderName   recorded in a new archive's manifest
    \param progress     as for BmcCaptureToArchive(); called afresh for each capture
    \param aborted      as for BmcCaptureToArchive()
 */
//! \brief Name of the folder, inside a Backup folder, that holds ring files of an old copy
//!        which could not be read as a card and so could not be converted ("Unconverted").
QString BmcUnconvertedDirName();

/*! \brief What to tell the user about a backup step that did not fully succeed; empty when
           it did, or when the user cancelled it.

    The import itself has succeeded by then, but the card goes on overwriting its oldest
    nights, so a backup that keeps failing must not stay hidden in the log.
 */
QString BmcBackupWarning(const BmcBackupOutcome &outcome);

BmcBackupOutcome BmcBackUpImportSource(BmcImportSource &source, const QString &backupDir,
                                       const BmcParserFactory &factory,
                                       const std::function<bool(const QString &)> &isCardFolder,
                                       const QString &loaderName,
                                       const std::function<void(int done, int total)> &progress,
                                       const std::function<bool()> &aborted);

// ---------------------------------------------------------------------------------
// What a rebuild from backup could not restore
// ---------------------------------------------------------------------------------

/*! \struct BmcStoredNight
    \brief One night's waveform extent as the database holds it.
 */
struct BmcStoredNight {
    QDate day;          //!< the OSCAR day
    QDateTime first;    //!< start of the day's first session
    QDateTime last;     //!< end of the day's last session
};

/*! \brief The days of \a mach that have flow waveform data in the database.

    These are the nights that would come back as events and statistics only if the backup
    did not hold their waveform. Reads the stored summaries (a flow sample count above
    zero); no event data is loaded.
 */
QList<BmcStoredNight> BmcStoredNights(Machine *mach);

/*! \brief The \a nights that no segment in \a archive overlaps.

    A night counts as held if the archive has any of it: the question is whether a
    rebuild would bring its graphs back at all.
 */
QList<BmcStoredNight> BmcNightsNotInArchive(const QList<BmcStoredNight> &nights,
                                            const CardArchive &archive);

/*! \brief The sentence for the rebuild prompts saying that \a missing nights have
           detailed graphs the backup cannot restore; empty if there are none.
 */
QString BmcRebuildLossText(const QList<BmcStoredNight> &missing);

/*! \brief What can be said from the archive alone, for when the device's sessions are not
           loaded (the upgrade prompt runs before they are).

    If the archive's index reaches back before its first segment, the nights in between
    have no waveform in the backup: says from which date on it holds detailed data. Empty
    when the segments reach as far back as the index, or the archive has no index.
 */
QString BmcRebuildLossTextFromArchive(const CardArchive &archive);

/*! \brief The caution for the rebuild prompts when the backup is still a full copy of the
           card, which holds only what was on the card at the last import.
 */
QString BmcRebuildCardCopyText();

#endif // BMC_ARCHIVE_H
