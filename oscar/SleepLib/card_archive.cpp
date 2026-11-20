/* Card Archive Implementation
 *
 * Per-record segment store for backups of ring-buffer data cards. See card_archive.h.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "card_archive.h"

#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>

#include <algorithm>

#include "SleepLib/common.h"

namespace {

const QString kSegmentSuffix = QStringLiteral(".seg.gz");

//! True if the closed spans [aFirst, aLast] and [bFirst, bLast] share any instant.
bool spansOverlap(const QDateTime &aFirst, const QDateTime &aLast,
                  const QDateTime &bFirst, const QDateTime &bLast)
{
    return aFirst <= bLast && bFirst <= aLast;
}

//! Order for the entry list: by date, then by first packet time.
bool entryLess(const CardArchiveEntry &a, const CardArchiveEntry &b)
{
    if (a.date != b.date) { return a.date < b.date; }
    return a.first < b.first;
}

QJsonObject entryToJson(const CardArchiveEntry &e)
{
    QJsonObject o;
    o.insert("file", e.file);
    o.insert("date", e.date.toString(Qt::ISODate));
    o.insert("first", e.first.toString(Qt::ISODateWithMs));
    o.insert("last", e.last.toString(Qt::ISODateWithMs));
    o.insert("bytes", static_cast<double>(e.bytes));
    o.insert("packets", e.packets);
    o.insert("final", e.final);
    o.insert("complete", e.complete);
    o.insert("rule", e.rule);
    o.insert("sha256", QString::fromLatin1(e.sha256.toHex()));
    return o;
}

CardArchiveEntry entryFromJson(const QJsonObject &o)
{
    CardArchiveEntry e;
    e.file = o.value("file").toString();
    e.date = QDate::fromString(o.value("date").toString(), Qt::ISODate);
    e.first = QDateTime::fromString(o.value("first").toString(), Qt::ISODateWithMs);
    e.last = QDateTime::fromString(o.value("last").toString(), Qt::ISODateWithMs);
    e.bytes = static_cast<qint64>(o.value("bytes").toDouble());
    e.packets = o.value("packets").toInt();
    e.final = o.value("final").toBool();
    e.complete = o.value("complete").toBool(true);
    e.rule = o.value("rule").toInt();
    e.sha256 = QByteArray::fromHex(o.value("sha256").toString().toLatin1());
    return e;
}

//! Make sure \a path can be overwritten or removed. Files copied from a write-protected
//! card keep the read-only attribute, and on Windows such a file cannot be deleted.
void makeWritable(const QString &path)
{
    QFile file(path);
    file.setPermissions(file.permissions() | QFileDevice::WriteOwner | QFileDevice::WriteUser);
}

//! Copy \a src to \a tmp, replacing any stale temporary file. The copy is left writable.
bool copyToTemp(const QString &src, const QString &tmp)
{
    if (QFile::exists(tmp)) {
        makeWritable(tmp);
        QFile::remove(tmp);
    }
    if (!QFile::copy(src, tmp)) {
        return false;
    }
    makeWritable(tmp);
    return true;
}

//! Move \a tmp over \a dst, replacing it.
bool moveOver(const QString &tmp, const QString &dst)
{
    if (QFile::exists(dst)) {
        makeWritable(dst);
        if (!QFile::remove(dst)) {
            return false;
        }
    }
    return QFile::rename(tmp, dst);
}

//! Copy \a src over \a dst by way of "<dst>.tmp", so a failed copy leaves \a dst as it was.
bool copyReplacing(const QString &src, const QString &dst)
{
    const QString tmp = dst + QStringLiteral(".tmp");
    if (!copyToTemp(src, tmp)) {
        return false;
    }
    if (!moveOver(tmp, dst)) {
        QFile::remove(tmp);
        return false;
    }
    return true;
}

} // namespace

QString CardArchive::manifestName()
{
    return QStringLiteral("archive.json");
}

QString CardArchive::segmentDirName()
{
    return QStringLiteral("Segments");
}

QString CardArchive::damagedDirName()
{
    return QStringLiteral("Damaged");
}

QString CardArchive::generationDirName()
{
    return QStringLiteral("Generations");
}

CardArchive::CardArchive(const QString &dir)
    : m_dir(QDir(dir).absolutePath())
{
}

bool CardArchive::isArchive(const QString &dir)
{
    const QDir folder(dir);
    if (QFile::exists(folder.absoluteFilePath(manifestName()))) {
        return true;
    }
    // A lost manifest does not make the segments worthless: it is rebuilt from them.
    const QDir segments(folder.absoluteFilePath(segmentDirName()));
    return !segments.entryList(QStringList() << (QLatin1Char('*') + kSegmentSuffix), QDir::Files).isEmpty();
}

bool CardArchive::isNewerFormat(const QString &dir)
{
    QFile file(QDir(dir).absoluteFilePath(manifestName()));
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    return doc.isObject() && doc.object().value("version").toInt() > kFormatVersion;
}

bool CardArchive::conversionComplete() const
{
    return m_converted;
}

bool CardArchive::markConversionComplete()
{
    if (!isWritable()) {
        return false;
    }
    if (m_converted) {
        return true;
    }
    m_converted = true;
    if (!save()) {
        m_converted = false;
        return false;
    }
    return true;
}

void CardArchive::setIdentity(const QString &loaderName, const QString &baseName)
{
    if (!m_base.isEmpty()) { return; }
    m_loader = loaderName;
    m_base = baseName;
}

QString CardArchive::baseName() const
{
    return m_base;
}

bool CardArchive::load()
{
    m_entries.clear();
    m_root = QJsonObject();
    m_newer = false;
    m_converted = false;
    m_indexFirst = QDate();
    m_indexLast = QDate();
    m_indexFiles.clear();

    QFile file(QDir(m_dir).absoluteFilePath(manifestName()));
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "CardArchive: unreadable manifest in" << m_dir << "-" << parseError.errorString();
        return false;
    }

    const QJsonObject root = doc.object();
    const int version = root.value("version").toInt();
    if (version > kFormatVersion) {
        // Written by a later build whose rules this one does not know: leave it alone.
        qWarning() << "CardArchive: manifest in" << m_dir << "has format" << version
                   << "- this build writes format" << kFormatVersion << "- treating as read-only";
        m_newer = true;
        return false;
    }

    m_root = root;
    m_loader = root.value("loader").toString();
    m_base = root.value("base").toString();
    m_converted = root.value("converted").toBool();

    const QJsonObject index = root.value("index").toObject();
    m_indexFirst = QDate::fromString(index.value("first").toString(), Qt::ISODate);
    m_indexLast = QDate::fromString(index.value("last").toString(), Qt::ISODate);
    const QJsonArray indexFiles = index.value("files").toArray();
    for (const QJsonValue &value : indexFiles) {
        m_indexFiles.append(value.toString());
    }
    if (!m_indexFirst.isValid() || !m_indexLast.isValid()) {
        m_indexFirst = QDate();
        m_indexLast = QDate();
    }

    const QJsonArray segments = root.value("segments").toArray();
    for (const QJsonValue &value : segments) {
        const CardArchiveEntry entry = entryFromJson(value.toObject());
        if (entry.file.isEmpty() || !entry.date.isValid()) {
            continue;
        }
        if (!QFile::exists(segmentPath(entry.file))) {
            qWarning() << "CardArchive: segment" << entry.file << "is listed but missing - dropped";
            continue;
        }
        m_entries.append(entry);
    }
    std::sort(m_entries.begin(), m_entries.end(), entryLess);
    return true;
}

bool CardArchive::isWritable() const
{
    return !m_newer && !m_readOnly;
}

void CardArchive::setReadOnly(bool on)
{
    m_readOnly = on;
}

bool CardArchive::save()
{
    if (!isWritable()) {
        return false;
    }
    if (!QDir().mkpath(m_dir)) {
        qWarning() << "CardArchive: could not create" << m_dir;
        return false;
    }

    QJsonObject root = m_root;      // start from what was loaded so unknown keys survive
    root.insert("version", kFormatVersion);
    root.insert("loader", m_loader);
    root.insert("base", m_base);
    QJsonArray segments;
    for (const CardArchiveEntry &entry : m_entries) {
        segments.append(entryToJson(entry));
    }
    root.insert("segments", segments);
    if (m_converted) {
        root.insert("converted", true);
    }
    if (m_indexFirst.isValid()) {
        QJsonObject index = root.value("index").toObject();
        index.insert("first", m_indexFirst.toString(Qt::ISODate));
        index.insert("last", m_indexLast.toString(Qt::ISODate));
        index.insert("files", QJsonArray::fromStringList(m_indexFiles));
        root.insert("index", index);
    }

    QSaveFile file(QDir(m_dir).absoluteFilePath(manifestName()));
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "CardArchive: could not write manifest in" << m_dir << "-" << file.errorString();
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        qWarning() << "CardArchive: could not write manifest in" << m_dir << "-" << file.errorString();
        return false;
    }
    m_root = root;
    return true;
}

QList<CardArchiveEntry> CardArchive::entries() const
{
    return m_entries;
}

const CardArchiveEntry *CardArchive::find(const CardArchiveRecord &rec) const
{
    const bool haveSpan = rec.first.isValid();
    const QDateTime recLast = rec.last.isValid() ? rec.last : rec.first;

    const CardArchiveEntry *best = nullptr;
    for (const CardArchiveEntry &entry : m_entries) {
        if (entry.date != rec.date) {
            continue;
        }
        if (haveSpan && !spansOverlap(entry.first, entry.last, rec.first, recLast)) {
            continue;
        }
        if (best == nullptr || entry.bytes > best->bytes) {
            best = &entry;
        }
    }
    return best;
}

bool CardArchive::coversSpan(const QDateTime &from, const QDateTime &to) const
{
    if (!from.isValid() || !to.isValid()) {
        return false;
    }
    for (const CardArchiveEntry &entry : m_entries) {
        if (spansOverlap(entry.first, entry.last, from, to)) {
            return true;
        }
    }
    return false;
}

bool CardArchive::wants(const CardArchiveRecord &rec) const
{
    const CardArchiveEntry *entry = find(rec);
    if (entry == nullptr) {
        return true;
    }
    if (entry->rule < rec.rule) {
        return true;        // taken under an older capture rule: look again
    }
    if (rec.bytes > entry->bytes) {
        return true;        // the card is known to hold more of it than the archive does
    }
    if (entry->final && entry->complete) {
        return false;       // nothing a card could add
    }
    if (rec.bytes < 0) {
        return true;        // size unknown until read; store() keeps the longer one
    }
    if (rec.bytes < entry->bytes) {
        return false;
    }
    if (rec.bytes == entry->bytes && entry->final) {
        return false;
    }
    return true;
}

CardArchive::StoreResult CardArchive::store(const CardArchiveRecord &rec, const QByteArray &packets,
                                            int packetSize)
{
    if (!isWritable()) {
        return StoreResult::Failed;
    }
    if (packetSize <= 0 || packets.isEmpty() || (packets.size() % packetSize) != 0) {
        qWarning() << "CardArchive: refusing a segment of" << packets.size()
                   << "bytes for packet size" << packetSize;
        return StoreResult::Failed;
    }
    if (!rec.date.isValid() || !rec.first.isValid() || !rec.last.isValid()) {
        qWarning() << "CardArchive: refusing a segment without a valid date and time span";
        return StoreResult::Failed;
    }

    const QByteArray sha256 = QCryptographicHash::hash(packets, QCryptographicHash::Sha256);

    const CardArchiveEntry *existing = find(rec);
    QString fileName;
    QString replacedFile;       // set when an existing segment is being replaced
    if (existing != nullptr && existing->bytes == packets.size() && existing->sha256 == sha256) {
        // The archive already holds exactly these bytes, as it does for the newest night
        // on every import of an unchanged card. There is nothing to write; the entry can
        // only gain standing: final once a later night exists, complete if any card ever
        // showed the whole night, and the newest capture rule that produced these bytes.
        for (CardArchiveEntry &entry : m_entries) {
            if (entry.file != existing->file) {
                continue;
            }
            const CardArchiveEntry before = entry;
            entry.final = entry.final || rec.final;
            entry.complete = entry.complete || rec.complete;
            entry.rule = std::max(entry.rule, rec.rule);
            const bool changed = entry.final != before.final || entry.complete != before.complete
                                 || entry.rule != before.rule;
            if (changed && !save()) {
                entry = before;
                return StoreResult::Failed;
            }
            break;
        }
        return StoreResult::KeptExisting;
    }
    if (existing != nullptr) {
        bool replace;
        if (existing->rule < rec.rule) {
            // The newer rule's full range is the correct one whatever its size; a tail
            // taken under it is only worth having if it holds more than the old capture.
            replace = rec.complete || packets.size() > existing->bytes;
        } else {
            replace = packets.size() > existing->bytes
                      || (packets.size() == existing->bytes && !existing->final);
        }
        if (!replace) {
            return StoreResult::KeptExisting;
        }
        // The replacement goes into a new file beside the old one. The old file is only
        // removed once the manifest describing the new one is safely written, so at every
        // moment the manifest's entry and the file it names agree.
        replacedFile = existing->file;
    }
    fileName = newSegmentName(rec.date);

    const QString dir = QDir(m_dir).absoluteFilePath(segmentDirName());
    if (!QDir().mkpath(dir)) {
        qWarning() << "CardArchive: could not create" << dir;
        return StoreResult::Failed;
    }

    // QSaveFile writes to a temporary file and renames it over the target on commit(), so
    // a failure or a crash part-way leaves either the old segment or none, never half of one.
    QSaveFile file(segmentPath(fileName));
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "CardArchive: could not write" << fileName << "-" << file.errorString();
        return StoreResult::Failed;
    }
    const QByteArray compressed = gCompress(packets);
    if (file.write(compressed) != compressed.size() || !file.commit()) {
        qWarning() << "CardArchive: could not write" << fileName << "-" << file.errorString();
        file.cancelWriting();
        return StoreResult::Failed;
    }

    CardArchiveEntry entry;
    entry.file = fileName;
    entry.date = rec.date;
    entry.first = rec.first;
    entry.last = rec.last;
    entry.bytes = packets.size();
    entry.packets = static_cast<int>(packets.size() / packetSize);
    entry.final = rec.final;
    entry.complete = rec.complete;
    entry.rule = rec.rule;
    entry.sha256 = sha256;

    const bool replaced = !replacedFile.isEmpty();
    const QList<CardArchiveEntry> before = m_entries;
    if (replaced) {
        for (int i = 0; i < m_entries.size(); ++i) {
            if (m_entries.at(i).file == replacedFile) {
                m_entries.removeAt(i);
                break;
            }
        }
    }
    insertSorted(entry);

    if (!save()) {
        // The manifest on disk still describes the archive as it was, and the old segment
        // is still there to match it. Take back the file that could not be recorded.
        m_entries = before;
        QFile::remove(segmentPath(fileName));
        return StoreResult::Failed;
    }
    if (replaced) {
        makeWritable(segmentPath(replacedFile));
        QFile::remove(segmentPath(replacedFile));
    }
    return replaced ? StoreResult::Replaced : StoreResult::Stored;
}

QByteArray CardArchive::read(const CardArchiveEntry &entry, bool *ok, ReadStatus *status) const
{
    if (ok != nullptr) { *ok = false; }
    if (status != nullptr) { *status = ReadStatus::Unavailable; }

    QFile file(segmentPath(entry.file));
    if (!file.open(QIODevice::ReadOnly)) {
        // Locked by another program, not downloaded from a synced folder, or gone.
        // Nothing is known about its content, so it must not be treated as damaged.
        qWarning() << "CardArchive: could not open segment" << entry.file << "-" << file.errorString();
        return QByteArray();
    }
    const QByteArray compressed = file.readAll();
    file.close();

    if (status != nullptr) { *status = ReadStatus::Damaged; }
    const QByteArray data = gUncompress(compressed);
    if (data.size() != entry.bytes
        || QCryptographicHash::hash(data, QCryptographicHash::Sha256) != entry.sha256) {
        qWarning() << "CardArchive: segment" << entry.file << "is damaged (size or checksum mismatch)";
        return QByteArray();
    }
    if (ok != nullptr) { *ok = true; }
    if (status != nullptr) { *status = ReadStatus::Ok; }
    return data;
}

bool CardArchive::quarantine(const CardArchiveEntry &entry)
{
    if (!isWritable()) {
        return false;
    }
    const QString source = segmentPath(entry.file);
    if (QFile::exists(source)) {
        const QString folder = QDir(m_dir).absoluteFilePath(segmentDirName() + QLatin1Char('/') + damagedDirName());
        if (!QDir().mkpath(folder)) {
            qWarning() << "CardArchive: could not create" << folder;
            return false;
        }
        // Never over a file set aside earlier: each keeps its own name.
        QString target = folder + QLatin1Char('/') + entry.file;
        for (int n = 2; QFile::exists(target); ++n) {
            target = folder + QLatin1Char('/') + entry.file + QLatin1Char('.') + QString::number(n);
        }
        makeWritable(source);
        if (!QFile::rename(source, target)) {
            qWarning() << "CardArchive: could not set aside the damaged segment" << entry.file;
            return false;
        }
        qWarning() << "CardArchive: damaged segment" << entry.file << "set aside in" << folder;
    }

    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).file == entry.file) {
            m_entries.removeAt(i);
            break;
        }
    }
    save();
    return true;
}

void CardArchive::drop(const CardArchiveEntry &entry)
{
    if (!isWritable()) {
        return;
    }
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).file == entry.file) {
            m_entries.removeAt(i);
            break;
        }
    }
    QFile::remove(segmentPath(entry.file));
    save();
}

QString CardArchive::segmentPath(const QString &fileName) const
{
    return QDir(m_dir).absoluteFilePath(segmentDirName() + QLatin1Char('/') + fileName);
}

//! \brief A segment file name for a new record dated \a date that no entry or file uses yet.
QString CardArchive::newSegmentName(const QDate &date) const
{
    const QString stem = (m_base.isEmpty() ? QStringLiteral("card") : m_base)
                         + QLatin1Char('_') + date.toString(QStringLiteral("yyyyMMdd"));
    for (int n = 1; ; ++n) {
        const QString name = (n == 1) ? stem + kSegmentSuffix
                                      : stem + QLatin1Char('_') + QString::number(n) + kSegmentSuffix;
        bool used = QFile::exists(segmentPath(name));
        for (const CardArchiveEntry &entry : m_entries) {
            if (entry.file == name) {
                used = true;
                break;
            }
        }
        if (!used) {
            return name;
        }
    }
}

//! \brief Insert \a entry keeping m_entries sorted by date, then first packet time.
void CardArchive::insertSorted(const CardArchiveEntry &entry)
{
    const auto pos = std::upper_bound(m_entries.begin(), m_entries.end(), entry, entryLess);
    m_entries.insert(pos, entry);
}

int CardArchive::adoptOrphans(int packetSize, const Describe &describe)
{
    // A read-only archive is rebuilt in memory, so it can still be read from; one written
    // by a newer format is not second-guessed.
    if (m_newer || packetSize <= 0 || !describe) {
        return 0;
    }

    // <base>_<yyyyMMdd>.seg.gz or <base>_<yyyyMMdd>_<n>.seg.gz
    static const QRegularExpression namePattern(
        QStringLiteral("^(.+)_(\\d{8})(?:_\\d+)?\\.seg\\.gz$"));

    const QDir segments(QDir(m_dir).absoluteFilePath(segmentDirName()));
    const QStringList files = segments.entryList(QStringList() << (QLatin1Char('*') + kSegmentSuffix),
                                                 QDir::Files, QDir::Name);
    int adopted = 0;
    for (const QString &name : files) {
        bool listed = false;
        for (const CardArchiveEntry &entry : m_entries) {
            if (entry.file == name) {
                listed = true;
                break;
            }
        }
        if (listed) {
            continue;
        }

        const QRegularExpressionMatch match = namePattern.match(name);
        const QDate date = match.hasMatch()
                               ? QDate::fromString(match.captured(2), QStringLiteral("yyyyMMdd"))
                               : QDate();
        if (!date.isValid()) {
            qWarning() << "CardArchive: ignoring" << name << "- not named like a segment";
            continue;
        }

        QFile file(segments.absoluteFilePath(name));
        if (!file.open(QIODevice::ReadOnly)) {
            continue;
        }
        const QByteArray data = gUncompress(file.readAll());
        file.close();
        if (data.isEmpty() || (data.size() % packetSize) != 0) {
            qWarning() << "CardArchive: ignoring" << name << "- it does not hold whole packets";
            continue;
        }

        CardArchiveEntry entry;
        if (!describe(data, entry.first, entry.last) || !entry.first.isValid() || !entry.last.isValid()) {
            qWarning() << "CardArchive: ignoring" << name << "- its packets were not recognised";
            continue;
        }
        entry.file = name;
        entry.date = date;
        entry.bytes = data.size();
        entry.packets = static_cast<int>(data.size() / packetSize);
        entry.final = true;         // nothing says it can still grow
        entry.complete = false;     // and nothing says it is the whole night
        entry.rule = 0;             // so any card still holding the night may replace it
        entry.sha256 = QCryptographicHash::hash(data, QCryptographicHash::Sha256);
        insertSorted(entry);
        if (m_base.isEmpty()) {
            m_base = match.captured(1);
        }
        ++adopted;
        qDebug() << "CardArchive: adopted segment" << name << "with no manifest entry";
    }

    if (adopted > 0 && isWritable()) {
        save();
    }
    return adopted;
}

CardArchive::IndexAction CardArchive::indexAction(const QDate &cardFirst, const QDate &cardLast) const
{
    if (!m_indexFirst.isValid() || !m_indexLast.isValid()) {
        return IndexAction::Replace;
    }
    if (cardFirst <= m_indexFirst && cardLast >= m_indexLast) {
        return IndexAction::Replace;
    }
    if (cardFirst >= m_indexFirst && cardLast <= m_indexLast) {
        return IndexAction::Keep;
    }
    return IndexAction::Retire;
}

bool CardArchive::installIndexSet(const QString &srcDir, const QStringList &names,
                                  const QDate &first, const QDate &last)
{
    if (!isWritable() || !first.isValid() || !last.isValid()) {
        return false;
    }
    IndexAction action = indexAction(first, last);
    if (action == IndexAction::Keep) {
        return true;
    }

    const QDir archiveDir(m_dir);
    const QDir source(srcDir);

    // Index files already in the folder whose range is not on record (the manifest was
    // lost, or they were left by something else). What nights they list is unknown, so
    // they are not overwritten: they are retired like a set the card does not cover.
    QStringList unrecorded;
    if (!m_indexFirst.isValid() && !(source == archiveDir)) {
        for (const QString &name : names) {
            if (archiveDir.exists(name)) {
                unrecorded.append(name);
            }
        }
        if (!unrecorded.isEmpty()) {
            action = IndexAction::Retire;
        }
    }

    if (source == archiveDir) {
        // An old card mirror being converted: the files are already here.
        m_indexFiles = names;
        m_indexFirst = first;
        m_indexLast = last;
        return save();
    }

    if (!QDir().mkpath(m_dir)) {
        return false;
    }

    // Copy the whole new set beside its targets before touching the old one.
    QStringList copied;
    for (const QString &name : names) {
        const QString tmp = archiveDir.absoluteFilePath(name + QStringLiteral(".tmp"));
        if (!copyToTemp(source.absoluteFilePath(name), tmp)) {
            qWarning() << "CardArchive: could not copy index file" << name << "from" << srcDir;
            for (const QString &done : copied) {
                QFile::remove(archiveDir.absoluteFilePath(done + QStringLiteral(".tmp")));
            }
            return false;
        }
        copied.append(name);
    }

    if (action == IndexAction::Retire) {
        // Neither set covers the other, so the old one may describe nights the new one
        // does not. Move it aside whole; a rebuild reads every generation.
        const QString stem = generationDirName() + QLatin1Char('/')
                             + (m_indexLast.isValid() ? m_indexLast.toString(QStringLiteral("yyyyMMdd"))
                                                      : QStringLiteral("unknown"));
        QString generation = stem;
        for (int n = 2; archiveDir.exists(generation); ++n) {
            generation = stem + QLatin1Char('_') + QString::number(n);
        }
        bool moved = archiveDir.mkpath(generation);
        const QStringList retiring = m_indexFiles.isEmpty() ? unrecorded : m_indexFiles;
        for (const QString &name : retiring) {
            if (!moved) { break; }
            if (!archiveDir.exists(name)) {
                continue;
            }
            makeWritable(archiveDir.absoluteFilePath(name));
            moved = QFile::rename(archiveDir.absoluteFilePath(name),
                                  archiveDir.absoluteFilePath(generation + QLatin1Char('/') + name));
        }
        if (!moved) {
            qWarning() << "CardArchive: could not move the previous index set to" << generation;
            for (const QString &done : copied) {
                QFile::remove(archiveDir.absoluteFilePath(done + QStringLiteral(".tmp")));
            }
            return false;
        }
        qDebug() << "CardArchive: the card's index no longer covers the archive's; previous set kept in"
                 << generation;
    }

    bool ok = true;
    for (const QString &name : names) {
        if (!moveOver(archiveDir.absoluteFilePath(name + QStringLiteral(".tmp")),
                      archiveDir.absoluteFilePath(name))) {
            qWarning() << "CardArchive: could not replace index file" << name;
            QFile::remove(archiveDir.absoluteFilePath(name + QStringLiteral(".tmp")));
            ok = false;
        }
    }
    if (!ok) {
        return false;
    }

    m_indexFiles = names;
    m_indexFirst = first;
    m_indexLast = last;
    return save();
}

QDate CardArchive::indexFirstDate() const
{
    return m_indexFirst;
}

QDate CardArchive::indexLastDate() const
{
    return m_indexLast;
}

QStringList CardArchive::generationDirs() const
{
    const QDir generations(QDir(m_dir).absoluteFilePath(generationDirName()));
    QStringList result;
    // Folder names are yyyyMMdd[_n], so name order is date order.
    const QStringList names = generations.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &name : names) {
        result.append(generations.absoluteFilePath(name));
    }
    return result;
}

bool CardArchive::mirrorOtherFiles(const QString &srcDir, const std::function<bool(const QString &)> &skip)
{
    if (!isWritable()) {
        return false;
    }
    const QDir source(srcDir);
    const QDir archiveDir(m_dir);
    if (source == archiveDir) {
        return true;
    }

    bool ok = true;
    QDirIterator it(source.absolutePath(), QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QString relative = source.relativeFilePath(path);
        if (skip && skip(relative)) {
            continue;
        }
        const QString target = archiveDir.absoluteFilePath(relative);
        const QFileInfo targetInfo(target);
        if (targetInfo.exists() && targetInfo.size() == QFileInfo(path).size()) {
            continue;
        }
        if (!QDir().mkpath(targetInfo.absolutePath()) || !copyReplacing(path, target)) {
            qWarning() << "CardArchive: could not copy" << relative << "from" << srcDir;
            ok = false;
        }
    }
    return ok;
}
