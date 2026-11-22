/* Card Archive Header
 *
 * A backup store for data cards that keep their detailed data in a ring buffer. Instead
 * of mirroring the card's fixed-name ring files, which a later import would overwrite,
 * the archive keeps each record (normally one night) as its own write-once, gzipped
 * "segment" holding the record's packets in recording order, plus a manifest that lists
 * them. Nothing in the archive refers to a card offset, so nothing wraps.
 *
 * The class knows no card format and does not use the profile. A loader decides what a
 * record is and supplies its bytes; this class owns the layout and the rules for when a
 * record is stored, skipped or replaced. Design: Notes/Design/
 * 2026-11-19-ring-buffer-backup-archive-design.md (GitLab #380).
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef CARD_ARCHIVE_H
#define CARD_ARCHIVE_H

#include <QByteArray>
#include <QDate>
#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

/*! \struct CardArchiveRecord
    \brief What a loader offers the archive for one record (one night) on a card.
 */
struct CardArchiveRecord {
    QDate     date;             //!< the card index's date for the record
    QDateTime first;            //!< first packet time; invalid if unknown before reading
    QDateTime last;             //!< last packet time; invalid if unknown before reading
    qint64    bytes = -1;       //!< uncompressed size on offer; -1 if unknown before reading
    bool      final = false;    //!< not the newest night on the card, so it will not grow
    bool      complete = true;  //!< false if known to be only the tail of the night
    int       rule = 0;         //!< the loader's capture-rule version (see CardArchive::wants)
};

/*! \struct CardArchiveEntry
    \brief One segment held by the archive: a row of the manifest.
 */
struct CardArchiveEntry {
    QString    file;             //!< segment file name inside the segment folder
    QDate      date;             //!< the card index's date for the record
    QDateTime  first;            //!< first packet time
    QDateTime  last;             //!< last packet time
    qint64     bytes = 0;        //!< uncompressed size
    int        packets = 0;      //!< number of packets
    bool       final = false;    //!< captured when it was no longer the newest night
    bool       complete = true;  //!< false if captured as only the tail of the night
    int        rule = 0;         //!< capture-rule version the segment was taken under
    QByteArray sha256;           //!< SHA-256 of the uncompressed content (32 raw bytes)
};

/*! \class CardArchive
    \brief Per-record segment store kept in a device's Backup folder.

    Layout under the folder given to the constructor:
      - archive.json            the manifest
      - Segments/<base>_<yyyyMMdd>[_n].seg.gz   one gzipped segment per record
      - the card's index files, verbatim (the small files that describe every record)
      - Generations/<yyyyMMdd>[_n]/   an earlier index set that a later card no longer covers
      - any other files from the card

    Segments are write-once: a stored record is only ever replaced by a longer capture of
    the same record, or by a capture made under a newer capture rule.
 */
class CardArchive
{
public:
    //! \brief Outcome of store().
    enum class StoreResult {
        Stored,        //!< a new segment was written
        Replaced,      //!< an existing segment of the same record was replaced
        KeptExisting,  //!< the archive already holds as much or more; nothing written
        Failed         //!< bad input, a read-only archive, or a write error; nothing changed
    };

    //! \brief Why read() returned nothing.
    enum class ReadStatus {
        Ok,           //!< the content was read and matches its entry
        Unavailable,  //!< the file could not be opened: nothing is known about its content
        Damaged       //!< the file opened, but does not decompress to what the entry describes
    };

    //! \brief Manifest format this build reads and writes.
    static constexpr int kFormatVersion = 1;

    //! \brief File name of the manifest ("archive.json").
    static QString manifestName();
    //! \brief Name of the folder holding the segments ("Segments").
    static QString segmentDirName();
    //! \brief Name of the folder, inside the segment folder, that quarantine() moves
    //!        damaged segments to ("Damaged").
    static QString damagedDirName();
    //! \brief Name of the folder holding retired index sets ("Generations").
    static QString generationDirName();

    //! \brief What installIndexSet() does with a card's index files.
    enum class IndexAction {
        Replace,  //!< the card's set covers the archive's: it takes its place
        Keep,     //!< the card's set lies within the archive's (an older copy): no change
        Retire    //!< neither covers the other: the archive's set is moved aside first
    };

    //! \brief Packet decoder supplied by a loader: the first and last packet times of a
    //!        segment's uncompressed content. Returns false if it is not that loader's data.
    using Describe = std::function<bool(const QByteArray &packets, QDateTime &first, QDateTime &last)>;

    //! \brief Attach to the archive in \a dir. Nothing is read or created until load() or store().
    explicit CardArchive(const QString &dir);

    //! \brief True if \a dir holds an archive: a manifest, or segments whose manifest was
    //!        lost (it can be rebuilt from them, see adoptOrphans()).
    static bool isArchive(const QString &dir);

    //! \brief True if \a dir holds a manifest written by a later format than this build
    //!        knows. Such an archive must be left alone and not imported from.
    static bool isNewerFormat(const QString &dir);

    //! \brief Name the archive. Only takes effect while the archive has no base name,
    //!        that is for a new archive; a loaded manifest keeps its own identity.
    //! \param loaderName the loader that owns the archive
    //! \param baseName   the card's file base name, used to name segments
    void setIdentity(const QString &loaderName, const QString &baseName);

    //! \brief The card file base name segments are named after.
    QString baseName() const;

    //! \brief Read the manifest. Entries whose segment file is missing are dropped.
    //! \return false if the manifest is missing, unreadable, or written by a newer build.
    //!         Only the last case makes the archive read-only (see isWritable()).
    bool load();

    //! \brief False after loading a manifest of a newer format, or after setReadOnly(true).
    bool isWritable() const;

    //! \brief Forbid every change. Use for an archive that is not the device's own Backup folder.
    void setReadOnly(bool on);

    //! \brief Write the manifest atomically, keeping keys this build does not know.
    //! \return false if the archive is not writable or the write failed.
    bool save();

    //! \brief All segments, sorted by date and then by first packet time.
    QList<CardArchiveEntry> entries() const;

    //! \brief The entry holding \a rec, or nullptr.
    //!
    //! An entry matches when its date equals rec.date and either rec.first is invalid or
    //! the two [first, last] spans overlap. If several match, the one with the most bytes
    //! is returned. The pointer is valid until the archive is next modified.
    const CardArchiveEntry *find(const CardArchiveRecord &rec) const;

    //! \brief True if any entry's [first, last] overlaps [\a from, \a to].
    bool coversSpan(const QDateTime &from, const QDateTime &to) const;

    /*! \brief Whether a loader should read \a rec from the card and offer it to store().

        True when the archive has no entry for the record, the entry was taken under an
        older capture rule, or the card is known to offer more bytes than the entry holds
        ("complete" records what the capturing card showed, and that card may have been a
        truncated copy). Otherwise false when the entry is final and complete; and for the
        rest, when the size on offer is known, true only if it equals the entry's and the
        entry is not yet final; when it is unknown, true.
     */
    bool wants(const CardArchiveRecord &rec) const;

    /*! \brief Store \a packets as the segment for \a rec, applying the replace rules.

        \param rec        the record; date, first and last must be valid
        \param packets    the record's packets, in order; a non-zero multiple of \a packetSize
        \param packetSize size of one packet in bytes

        An existing entry for the record is kept when it is longer, or equally long and
        final. Exception: when it was taken under an older capture rule, a complete new
        capture replaces it whatever its size, and an incomplete one only if longer.

        When the archive already holds exactly these bytes nothing is written and
        KeptExisting is returned; the entry only gains standing (final, complete, and the
        newer capture rule), which is saved.
     */
    StoreResult store(const CardArchiveRecord &rec, const QByteArray &packets, int packetSize);

    //! \brief The uncompressed content of \a entry's segment.
    //! \param ok if non-null, set to false (and an empty array returned) when the file
    //!           cannot be opened, does not decompress, or disagrees with the entry's size or hash
    //! \param status if non-null, says which: a file that cannot be opened (locked by another
    //!           program, not downloaded from a synced folder) is Unavailable, not Damaged,
    //!           and nothing may be concluded about it
    QByteArray read(const CardArchiveEntry &entry, bool *ok = nullptr, ReadStatus *status = nullptr) const;

    //! \brief Remove \a entry and its segment file, then save. No-op when not writable.
    //!        For a segment that read() found Damaged use quarantine(), which keeps the file.
    void drop(const CardArchiveEntry &entry);

    /*! \brief Take a damaged segment out of the archive without destroying it.

        The file is moved to Segments/Damaged/ and its entry removed, so that a card which
        still holds the night can supply it again, while whatever the file contains stays
        on disk. Use it only for a segment that read() reported as Damaged.

        \return false if the archive is not writable or the file could not be moved; the
                entry is then left as it was
     */
    bool quarantine(const CardArchiveEntry &entry);

    /*! \brief Add manifest entries for segment files that have none, then save.

        Picks up a segment left by a crash between writing it and writing the manifest,
        and rebuilds a manifest that was lost or unreadable. The date comes from the file
        name and the times from \a describe. A file that is not named like a segment, does
        not decompress, is not a whole number of packets, or that \a describe rejects is
        left on disk untouched. Adopted entries are final, not complete, with rule 0, so
        any card that still holds the night may replace them.

        A read-only archive is rebuilt in memory only, so it can still be imported from.
        Nothing is adopted over a manifest of a newer format.

        \return the number of entries added
     */
    int adoptOrphans(int packetSize, const Describe &describe);

    /*! \brief What to do with a card's index set covering \a cardFirst to \a cardLast.

        Replace when the archive has no index set, or the card's range covers the
        archive's; Keep when the card's range lies within the archive's; otherwise Retire.
     */
    IndexAction indexAction(const QDate &cardFirst, const QDate &cardLast) const;

    /*! \brief Bring the archive's index set up to date from a card, as indexAction() decides.

        Replace copies \a names from \a srcDir over the archive's set. Keep changes
        nothing. Retire first moves the archive's set to Generations/<yyyyMMdd>[_n]/,
        named for the last date it covered, then copies. Index files that are in the
        folder without a recorded range (the manifest was lost) are never overwritten on
        the strength of a guess: they are retired too, to Generations/unknown[_n]/. Files are copied beside their
        target and renamed, so a failed copy leaves the previous set in place. When
        \a srcDir is the archive folder itself (an old card mirror being converted) nothing
        is copied and the files already there become the set.

        \param srcDir the card folder
        \param names  the index-class file names present in \a srcDir
        \param first  earliest date the card's index covers
        \param last   latest date the card's index covers
        \return false if the archive is not writable or a file could not be moved or copied
     */
    bool installIndexSet(const QString &srcDir, const QStringList &names,
                         const QDate &first, const QDate &last);

    //! \brief Absolute paths of the retired index sets, oldest first.
    QStringList generationDirs() const;

    /*! \brief True once a full card copy that this folder used to hold has been captured
               in full (see markConversionComplete()).
     */
    bool conversionComplete() const;

    /*! \brief Record that every night of the full card copy in this folder is now stored.

        Call it before deleting the copy's ring files. From then on the folder is an
        archive whatever ring files are still lying in it, so a deletion that is cut short
        cannot make a rebuild read a card copy with holes in it.

        \return false if the archive is not writable or the manifest could not be saved
     */
    bool markConversionComplete();

    //! \brief Earliest date the held index set covers; invalid if the archive has none.
    QDate indexFirstDate() const;
    //! \brief Latest date the held index set covers; invalid if the archive has none.
    QDate indexLastDate() const;

    /*! \brief Copy the card's remaining files into the archive.

        Every file under \a srcDir, recursively, for which \a skip returns false is copied
        if the archive lacks it or holds a copy of a different size. Nothing is deleted.
        Does nothing when \a srcDir is the archive folder itself.

        \param skip called with each file's path relative to \a srcDir, using forward slashes
        \return false if the archive is not writable or any file could not be copied
     */
    bool mirrorOtherFiles(const QString &srcDir, const std::function<bool(const QString &)> &skip);

private:
    QString segmentPath(const QString &fileName) const;
    QString newSegmentName(const QDate &date) const;
    void insertSorted(const CardArchiveEntry &entry);

    QString m_dir;                    //!< the archive folder, without a trailing separator
    QString m_loader;                 //!< owning loader's name
    QString m_base;                   //!< card file base name
    QList<CardArchiveEntry> m_entries;//!< kept sorted by date, then first
    QDate m_indexFirst;               //!< earliest date the held index set covers; invalid if none
    QDate m_indexLast;                //!< latest date the held index set covers
    QStringList m_indexFiles;         //!< file names of the held index set
    QJsonObject m_root;               //!< manifest as loaded, so unknown keys survive save()
    bool m_newer = false;             //!< manifest was written by a newer format
    bool m_converted = false;         //!< a full card copy once held here is captured in full
    bool m_readOnly = false;          //!< setReadOnly(true)
};

#endif // CARD_ARCHIVE_H
