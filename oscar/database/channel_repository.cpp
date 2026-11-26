/* Channel Repository Implementation
 *
 * Database access for channel display settings (schema v20): the global
 * channel_defaults table, the sparse per-profile profile_channel_overrides table, and
 * channels_legacy, which holds pre-v20 per-profile full copies until they are converted.
 *
 * Copyright (c) 2025-2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "channel_repository.h"
#include "database_manager.h"
#include <QHash>
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QDebug>

namespace {

/*! \brief A text value to bind for an override field: NULL when absent, otherwise the
    string, with a null QString turned into '' (Qt binds a null QString as SQL NULL, which
    would lose an override to the empty string). */
QVariant optionalText(const std::optional<QString> &v)
{
    if (!v) {
        return QVariant();
    }
    return v->isNull() ? QVariant(QStringLiteral("")) : QVariant(*v);
}

//! \brief Value to bind for an optional flag: NULL when absent, else 0 or 1.
QVariant optionalBool(const std::optional<bool> &v)
{
    return v ? QVariant(*v ? 1 : 0) : QVariant();
}

//! \brief Value to bind for an optional threshold: NULL when absent.
QVariant optionalDouble(const std::optional<double> &v)
{
    return v ? QVariant(*v) : QVariant();
}

//! \brief Value to bind for an optional colour: NULL when absent or invalid, else #RRGGBB.
QVariant optionalColor(const std::optional<QColor> &v)
{
    if (!v || !v->isValid()) {
        return QVariant();
    }
    return QVariant(ChannelRepository::colorToString(*v));
}

/*! \brief True when \p a and \p b would be stored identically in channel_defaults.
    Colours compare as #RRGGBB strings; thresholds exactly, since a REAL column returns the
    double it was given. */
bool sameDefaults(const ChannelDefaultRow &a, const ChannelDefaultRow &b)
{
    return a.channelCode == b.channelCode
        && a.type == b.type
        && a.enabled == b.enabled
        && ChannelRepository::colorToString(a.defaultColor) == ChannelRepository::colorToString(b.defaultColor)
        && a.fullname == b.fullname
        && a.label == b.label
        && a.description == b.description
        && a.lowerThreshold == b.lowerThreshold
        && ChannelRepository::colorToString(a.lowerThresholdColor) == ChannelRepository::colorToString(b.lowerThresholdColor)
        && a.upperThreshold == b.upperThreshold
        && ChannelRepository::colorToString(a.upperThresholdColor) == ChannelRepository::colorToString(b.upperThresholdColor)
        && a.showInOverview == b.showInOverview;
}

//! \brief Binds the value columns of \p row in channel_defaults column order.
void bindDefaultValues(QSqlQuery &query, const ChannelDefaultRow &row)
{
    query.addBindValue(row.channelCode);
    query.addBindValue(row.type);
    query.addBindValue(row.enabled ? 1 : 0);
    query.addBindValue(ChannelRepository::colorToString(row.defaultColor));
    query.addBindValue(row.fullname);
    query.addBindValue(row.label);
    query.addBindValue(row.description);
    query.addBindValue(row.lowerThreshold);
    query.addBindValue(ChannelRepository::colorToString(row.lowerThresholdColor));
    query.addBindValue(row.upperThreshold);
    query.addBindValue(ChannelRepository::colorToString(row.upperThresholdColor));
    query.addBindValue(row.showInOverview ? 1 : 0);
}

} // namespace

ChannelRepository::ChannelRepository()
    : db(DatabaseManager::instance().database())
{
}

ChannelRepository::ChannelRepository(const QSqlDatabase& database)
    : db(database)
{
}

bool ChannelRepository::syncDefaults(const QList<ChannelDefaultRow> &rows)
{
    if (!db.transaction()) {
        qWarning() << "ChannelRepository::syncDefaults failed to start transaction:" << db.lastError().text();
        return false;
    }

    // Read what is stored, so unchanged rows are not rewritten at every start.
    QHash<ChannelID, ChannelDefaultRow> stored;
    QSqlQuery query(db);
    if (!query.exec("SELECT channel_id, channel_code, type, enabled, default_color, fullname, label,"
                    " description, lower_threshold, lower_threshold_color, upper_threshold,"
                    " upper_threshold_color, show_in_overview FROM channel_defaults")) {
        DatabaseManager::instance().checkQueryError("ChannelRepository::syncDefaults (read)", query);
        db.rollback();
        return false;
    }
    while (query.next()) {
        ChannelDefaultRow r;
        r.channelId = query.value(0).toUInt();
        r.channelCode = query.value(1).toString();
        r.type = query.value(2).toInt();
        r.enabled = query.value(3).toBool();
        r.defaultColor = stringToColor(query.value(4).toString());
        r.fullname = query.value(5).toString();
        r.label = query.value(6).toString();
        r.description = query.value(7).toString();
        r.lowerThreshold = query.value(8).toDouble();
        r.lowerThresholdColor = stringToColor(query.value(9).toString());
        r.upperThreshold = query.value(10).toDouble();
        r.upperThresholdColor = stringToColor(query.value(11).toString());
        r.showInOverview = query.value(12).toBool();
        stored.insert(r.channelId, r);
    }

    QSqlQuery insert(db);
    insert.prepare("INSERT INTO channel_defaults (channel_code, type, enabled, default_color,"
                   " fullname, label, description, lower_threshold, lower_threshold_color,"
                   " upper_threshold, upper_threshold_color, show_in_overview, channel_id)"
                   " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    QSqlQuery update(db);
    update.prepare("UPDATE channel_defaults SET channel_code = ?, type = ?, enabled = ?,"
                   " default_color = ?, fullname = ?, label = ?, description = ?,"
                   " lower_threshold = ?, lower_threshold_color = ?, upper_threshold = ?,"
                   " upper_threshold_color = ?, show_in_overview = ?,"
                   " updated_at = CURRENT_TIMESTAMP WHERE channel_id = ?");

    int written = 0;
    QSet<ChannelID> present;
    for (const ChannelDefaultRow &row : rows) {
        present.insert(row.channelId);
        auto it = stored.constFind(row.channelId);
        if (it != stored.constEnd() && sameDefaults(*it, row)) {
            continue;
        }
        QSqlQuery &q = (it == stored.constEnd()) ? insert : update;
        bindDefaultValues(q, row);
        q.addBindValue(row.channelId);
        if (!q.exec()) {
            qWarning() << "ChannelRepository::syncDefaults failed for channel" << row.channelId
                       << ":" << q.lastError().text();
            DatabaseManager::instance().checkQueryError("ChannelRepository::syncDefaults", q);
            db.rollback();
            return false;
        }
        ++written;
    }

    // Channels no longer defined by the running build.
    QSqlQuery remove(db);
    remove.prepare("DELETE FROM channel_defaults WHERE channel_id = ?");
    int removed = 0;
    for (auto it = stored.constBegin(); it != stored.constEnd(); ++it) {
        if (present.contains(it.key())) {
            continue;
        }
        remove.addBindValue(it.key());
        if (!remove.exec()) {
            qWarning() << "ChannelRepository::syncDefaults failed to delete channel" << it.key()
                       << ":" << remove.lastError().text();
            DatabaseManager::instance().checkQueryError("ChannelRepository::syncDefaults (delete)", remove);
            db.rollback();
            return false;
        }
        ++removed;
    }

    if (!db.commit()) {
        qWarning() << "ChannelRepository::syncDefaults failed to commit transaction:" << db.lastError().text();
        db.rollback();
        return false;
    }
    if (written > 0 || removed > 0) {
        qDebug() << "ChannelRepository::syncDefaults: wrote" << written << "removed" << removed << "channel defaults";
    }
    return true;
}

QList<ChannelOverrideRow> ChannelRepository::findOverrides(qint64 profileId)
{
    QList<ChannelOverrideRow> rows;
    QSqlQuery query(db);
    query.prepare("SELECT channel_id, channel_code, enabled, default_color, fullname, label,"
                  " description, lower_threshold, upper_threshold, show_in_overview"
                  " FROM profile_channel_overrides WHERE profile_id = ? ORDER BY channel_id");
    query.addBindValue(profileId);
    if (!query.exec()) {
        qWarning() << "ChannelRepository::findOverrides failed:" << query.lastError().text();
        DatabaseManager::instance().checkQueryError("ChannelRepository::findOverrides", query);
        return rows;
    }
    while (query.next()) {
        rows.append(mapOverride(query));
    }
    return rows;
}

bool ChannelRepository::replaceOverrides(qint64 profileId, const QList<ChannelOverrideRow> &rows,
                                         const QSet<ChannelID> &knownIds, const QSet<QString> &knownCodes)
{
    if (!db.transaction()) {
        qWarning() << "ChannelRepository::replaceOverrides failed to start transaction:" << db.lastError().text();
        return false;
    }

    // The profile's rows are few, so find the ones to replace here rather than issuing one
    // DELETE per known channel.
    QList<qint64> doomed;
    QSqlQuery query(db);
    query.prepare("SELECT id, channel_id, channel_code FROM profile_channel_overrides WHERE profile_id = ?");
    query.addBindValue(profileId);
    if (!query.exec()) {
        qWarning() << "ChannelRepository::replaceOverrides failed to read rows:" << query.lastError().text();
        DatabaseManager::instance().checkQueryError("ChannelRepository::replaceOverrides (read)", query);
        db.rollback();
        return false;
    }
    while (query.next()) {
        if (knownIds.contains(query.value(1).toUInt()) || knownCodes.contains(query.value(2).toString())) {
            doomed.append(query.value(0).toLongLong());
        }
    }

    QSqlQuery remove(db);
    remove.prepare("DELETE FROM profile_channel_overrides WHERE id = ?");
    for (qint64 id : doomed) {
        remove.addBindValue(id);
        if (!remove.exec()) {
            qWarning() << "ChannelRepository::replaceOverrides failed to delete:" << remove.lastError().text();
            DatabaseManager::instance().checkQueryError("ChannelRepository::replaceOverrides (delete)", remove);
            db.rollback();
            return false;
        }
    }

    if (!insertOverrideRows(profileId, rows)) {
        db.rollback();
        return false;
    }

    if (!db.commit()) {
        qWarning() << "ChannelRepository::replaceOverrides failed to commit transaction:" << db.lastError().text();
        db.rollback();
        return false;
    }
    return true;
}

bool ChannelRepository::insertOverrideRows(qint64 profileId, const QList<ChannelOverrideRow> &rows)
{
    QSqlQuery insert(db);
    insert.prepare("INSERT INTO profile_channel_overrides (profile_id, channel_id, channel_code,"
                   " enabled, default_color, fullname, label, description, lower_threshold,"
                   " upper_threshold, show_in_overview) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    for (const ChannelOverrideRow &row : rows) {
        if (row.values.isEmpty()) {
            continue;
        }
        const ChannelOverrides::Values &v = row.values;
        insert.addBindValue(profileId);
        insert.addBindValue(row.channelId);
        insert.addBindValue(row.channelCode);
        insert.addBindValue(optionalBool(v.enabled));
        insert.addBindValue(optionalColor(v.color));
        insert.addBindValue(optionalText(v.fullname));
        insert.addBindValue(optionalText(v.label));
        insert.addBindValue(optionalText(v.description));
        insert.addBindValue(optionalDouble(v.lowerThreshold));
        insert.addBindValue(optionalDouble(v.upperThreshold));
        insert.addBindValue(optionalBool(v.showInOverview));
        if (!insert.exec()) {
            qWarning() << "ChannelRepository: inserting the override for channel" << row.channelId
                       << "failed:" << insert.lastError().text();
            DatabaseManager::instance().checkQueryError("ChannelRepository::insertOverrideRows", insert);
            return false;
        }
    }
    return true;
}

bool ChannelRepository::writeConversion(qint64 profileId, const QList<ChannelOverrideRow> &added,
                                        const QList<qint64> &convertedIds)
{
    return insertOverrideRows(profileId, added) && deleteLegacyRows(convertedIds);
}

QList<qint64> ChannelRepository::profilesWithLegacyRows()
{
    QList<qint64> ids;
    QSqlQuery query(db);
    if (!query.exec("SELECT DISTINCT profile_id FROM channels_legacy ORDER BY profile_id")) {
        qWarning() << "ChannelRepository::profilesWithLegacyRows failed:" << query.lastError().text();
        DatabaseManager::instance().checkQueryError("ChannelRepository::profilesWithLegacyRows", query);
        return ids;
    }
    while (query.next()) {
        ids.append(query.value(0).toLongLong());
    }
    return ids;
}

QList<ChannelData> ChannelRepository::findLegacy(qint64 profileId)
{
    QList<ChannelData> rows;
    QSqlQuery query(db);
    query.prepare("SELECT * FROM channels_legacy WHERE profile_id = ? ORDER BY channel_id");
    query.addBindValue(profileId);
    if (!query.exec()) {
        qWarning() << "ChannelRepository::findLegacy failed:" << query.lastError().text();
        DatabaseManager::instance().checkQueryError("ChannelRepository::findLegacy", query);
        return rows;
    }
    while (query.next()) {
        rows.append(mapLegacy(query));
    }
    return rows;
}

bool ChannelRepository::insertLegacy(qint64 profileId, const QList<ChannelData> &rows)
{
    if (!db.transaction()) {
        qWarning() << "ChannelRepository::insertLegacy failed to start transaction:" << db.lastError().text();
        return false;
    }

    QSqlQuery query(db);
    // OR REPLACE: a row still parked for the same profile and channel id (for example
    // after a failed conversion of a channels.dat import) is replaced, not a reason to fail.
    query.prepare("INSERT OR REPLACE INTO channels_legacy (profile_id, channel_id, channel_code, type, enabled,"
                  " default_color, fullname, label, description, lower_threshold,"
                  " lower_threshold_color, upper_threshold, upper_threshold_color, show_in_overview)"
                  " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    for (const ChannelData &data : rows) {
        query.addBindValue(profileId);
        query.addBindValue(data.channelId);
        query.addBindValue(data.channelCode);
        query.addBindValue(data.type);
        query.addBindValue(data.enabled ? 1 : 0);
        query.addBindValue(colorToString(data.defaultColor));
        query.addBindValue(data.fullname);
        query.addBindValue(data.label);
        query.addBindValue(data.description);
        query.addBindValue(data.lowerThreshold);
        query.addBindValue(colorToString(data.lowerThresholdColor));
        query.addBindValue(data.upperThreshold);
        query.addBindValue(colorToString(data.upperThresholdColor));
        query.addBindValue(data.showInOverview ? 1 : 0);
        if (!query.exec()) {
            qWarning() << "ChannelRepository::insertLegacy failed for channel" << data.channelId
                       << ":" << query.lastError().text();
            DatabaseManager::instance().checkQueryError("ChannelRepository::insertLegacy", query);
            db.rollback();
            return false;
        }
    }

    if (!db.commit()) {
        qWarning() << "ChannelRepository::insertLegacy failed to commit transaction:" << db.lastError().text();
        db.rollback();
        return false;
    }
    return true;
}

bool ChannelRepository::deleteLegacy(const QList<qint64> &rowIds)
{
    if (rowIds.isEmpty()) {
        return true;
    }
    if (!db.transaction()) {
        qWarning() << "ChannelRepository::deleteLegacy failed to start transaction:" << db.lastError().text();
        return false;
    }

    if (!deleteLegacyRows(rowIds)) {
        db.rollback();
        return false;
    }

    if (!db.commit()) {
        qWarning() << "ChannelRepository::deleteLegacy failed to commit transaction:" << db.lastError().text();
        db.rollback();
        return false;
    }
    return true;
}

bool ChannelRepository::deleteLegacyRows(const QList<qint64> &rowIds)
{
    QSqlQuery query(db);
    query.prepare("DELETE FROM channels_legacy WHERE id = ?");
    for (qint64 id : rowIds) {
        query.addBindValue(id);
        if (!query.exec()) {
            qWarning() << "ChannelRepository::deleteLegacyRows failed:" << query.lastError().text();
            DatabaseManager::instance().checkQueryError("ChannelRepository::deleteLegacyRows", query);
            return false;
        }
    }
    return true;
}

QString ChannelRepository::colorToString(const QColor& color)
{
    if (!color.isValid()) {
        return QString();
    }
    return color.name(); // Returns #RRGGBB format
}

QColor ChannelRepository::stringToColor(const QString& colorStr)
{
    if (colorStr.isEmpty()) {
        return QColor();
    }
    return QColor(colorStr);
}

ChannelData ChannelRepository::mapLegacy(const QSqlQuery& query)
{
    ChannelData data;

    data.id = query.value("id").toLongLong();
    data.profileId = query.value("profile_id").toLongLong();
    data.channelId = query.value("channel_id").toUInt();
    data.channelCode = query.value("channel_code").toString();
    data.type = query.value("type").toInt();
    data.enabled = query.value("enabled").toBool();

    data.defaultColor = stringToColor(query.value("default_color").toString());
    data.fullname = query.value("fullname").toString();
    data.label = query.value("label").toString();
    data.description = query.value("description").toString();

    data.lowerThreshold = query.value("lower_threshold").toDouble();
    data.lowerThresholdColor = stringToColor(query.value("lower_threshold_color").toString());
    data.upperThreshold = query.value("upper_threshold").toDouble();
    data.upperThresholdColor = stringToColor(query.value("upper_threshold_color").toString());

    data.showInOverview = query.value("show_in_overview").toBool();

    return data;
}

ChannelOverrideRow ChannelRepository::mapOverride(const QSqlQuery &query)
{
    // Column order is that of the SELECT in findOverrides(). A NULL column is "not
    // overridden"; a non-NULL empty string is an override to the empty string.
    ChannelOverrideRow row;
    row.channelId = query.value(0).toUInt();
    row.channelCode = query.value(1).toString();
    ChannelOverrides::Values &v = row.values;

    auto text = [&query](int col) -> std::optional<QString> {
        const QVariant value = query.value(col);
        if (value.isNull()) {
            return std::nullopt;
        }
        const QString s = value.toString();
        return s.isNull() ? QStringLiteral("") : s;
    };
    if (!query.value(2).isNull()) v.enabled = query.value(2).toBool();
    if (!query.value(3).isNull()) {
        const QColor color = stringToColor(query.value(3).toString());
        if (color.isValid()) v.color = color;
    }
    v.fullname = text(4);
    v.label = text(5);
    v.description = text(6);
    if (!query.value(7).isNull()) v.lowerThreshold = query.value(7).toDouble();
    if (!query.value(8).isNull()) v.upperThreshold = query.value(8).toDouble();
    if (!query.value(9).isNull()) v.showInOverview = query.value(9).toBool();
    return row;
}
