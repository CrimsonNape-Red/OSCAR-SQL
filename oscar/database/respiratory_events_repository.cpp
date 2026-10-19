/* Respiratory Events Repository Implementation
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file manages database operations for individual respiratory events
 * (Obstructive Apnea, Hypopnea, RERA, Clear Airway, etc.)
 */

#include "respiratory_events_repository.h"
#include "database_manager.h"
#include <QSqlDatabase>
#include <QSqlError>
#include <QDebug>
#include <QVariant>

//#define DBDEBUG

RespiratoryEventsRepository::RespiratoryEventsRepository()
{
}

qint64 RespiratoryEventsRepository::create(const RespiratoryEventData& data)
{
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "RespiratoryEventsRepository::create() - Database not open";
        return -1;
    }
    
    QSqlQuery query(db);
    query.prepare(
        "INSERT INTO respiratory_events "
        "(session_id, profile_id, channel_id, event_type, start_time, end_time, duration, desaturation, severity) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)"
    );
    
    query.addBindValue(data.sessionId);
    query.addBindValue(data.profileId);
    query.addBindValue(data.channelId);
    query.addBindValue(data.eventType);
    query.addBindValue(data.startTime);
    query.addBindValue(data.endTime);
    query.addBindValue(data.duration);
    query.addBindValue(data.desaturation > 0.0 ? data.desaturation : QVariant(QVariant::Double));
    query.addBindValue(data.severity > 0 ? data.severity : QVariant(QVariant::Int));
    
    if (!query.exec()) {
        qWarning() << "RespiratoryEventsRepository::create() - Failed to insert:"
                   << query.lastError().text();
        DatabaseManager::instance().checkQueryError("RespiratoryEventsRepository::create", query);
        return -1;
    }
    
    return query.lastInsertId().toLongLong();
}

bool RespiratoryEventsRepository::createBatch(const QList<RespiratoryEventData>& events)
{
    if (events.isEmpty()) {
        return true;  // Nothing to do
    }
    
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "RespiratoryEventsRepository::createBatch() - Database not open";
        return false;
    }
    
    // No transaction management here - rely on calling code's transaction
    // This is called from StoreEventsToDatabase which may already be in a transaction
    
    QSqlQuery query(db);
    query.prepare(
        "INSERT INTO respiratory_events "
        "(session_id, profile_id, channel_id, event_type, start_time, end_time, duration, desaturation, severity) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)"
    );
    
    int inserted = 0;
    for (const RespiratoryEventData& event : events) {
        query.addBindValue(event.sessionId);
        query.addBindValue(event.profileId);
        query.addBindValue(event.channelId);
        query.addBindValue(event.eventType);
        query.addBindValue(event.startTime);
        query.addBindValue(event.endTime);
        query.addBindValue(event.duration);
        query.addBindValue(event.desaturation > 0.0 ? event.desaturation : QVariant(QVariant::Double));
        query.addBindValue(event.severity > 0 ? event.severity : QVariant(QVariant::Int));
        
        if (!query.exec()) {
            qWarning() << "RespiratoryEventsRepository::createBatch() - Failed to insert event:"
                       << query.lastError().text();
            DatabaseManager::instance().checkQueryError("RespiratoryEventsRepository::createBatch", query);
            return false;
        }
        
        inserted++;
    }
    
#ifdef DBDEBUG
    qDebug() << "RespiratoryEventsRepository::createBatch() - Inserted" << inserted << "events";
#endif
    return true;
}

QList<RespiratoryEventData> RespiratoryEventsRepository::findBySession(qint64 sessionId)
{
    QList<RespiratoryEventData> results;
    
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "RespiratoryEventsRepository::findBySession() - Database not open";
        return results;
    }
    
    QSqlQuery query(db);
    query.prepare(
        "SELECT id, session_id, event_type, start_time, end_time, duration, "
        "desaturation, severity "
        "FROM respiratory_events "
        "WHERE session_id = ? "
        "ORDER BY start_time"
    );
    query.addBindValue(sessionId);
    
    if (!query.exec()) {
        qWarning() << "RespiratoryEventsRepository::findBySession() - Query failed:"
                   << query.lastError().text();
        DatabaseManager::instance().checkQueryError("RespiratoryEventsRepository::findBySession", query);
        return results;
    }
    
    while (query.next()) {
        results.append(populateFromQuery(query));
    }
    
    return results;
}

QList<RespiratoryEventData> RespiratoryEventsRepository::findByType(qint64 sessionId, int eventType)
{
    QList<RespiratoryEventData> results;
    
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "RespiratoryEventsRepository::findByType() - Database not open";
        return results;
    }
    
    QSqlQuery query(db);
    query.prepare(
        "SELECT id, session_id, event_type, start_time, end_time, duration, "
        "desaturation, severity "
        "FROM respiratory_events "
        "WHERE session_id = ? AND event_type = ? "
        "ORDER BY start_time"
    );
    query.addBindValue(sessionId);
    query.addBindValue(eventType);
    
    if (!query.exec()) {
        qWarning() << "RespiratoryEventsRepository::findByType() - Query failed:"
                   << query.lastError().text();
        DatabaseManager::instance().checkQueryError("RespiratoryEventsRepository::findByType", query);
        return results;
    }
    
    while (query.next()) {
        results.append(populateFromQuery(query));
    }
    
    return results;
}

QList<RespiratoryEventData> RespiratoryEventsRepository::findInTimeRange(
    qint64 sessionId, qint64 startTime, qint64 endTime)
{
    QList<RespiratoryEventData> results;
    
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "RespiratoryEventsRepository::findInTimeRange() - Database not open";
        return results;
    }
    
    QSqlQuery query(db);
    query.prepare(
        "SELECT id, session_id, event_type, start_time, end_time, duration, "
        "desaturation, severity "
        "FROM respiratory_events "
        "WHERE session_id = ? "
        "AND start_time <= ? AND end_time >= ? "
        "ORDER BY start_time"
    );
    query.addBindValue(sessionId);
    query.addBindValue(endTime);
    query.addBindValue(startTime);
    
    if (!query.exec()) {
        qWarning() << "RespiratoryEventsRepository::findInTimeRange() - Query failed:"
                   << query.lastError().text();
        DatabaseManager::instance().checkQueryError("RespiratoryEventsRepository::findInTimeRange", query);
        return results;
    }
    
    while (query.next()) {
        results.append(populateFromQuery(query));
    }
    
    return results;
}

bool RespiratoryEventsRepository::deleteBySession(qint64 sessionId)
{
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "RespiratoryEventsRepository::deleteBySession() - Database not open";
        return false;
    }
    
    QSqlQuery query(db);
    query.prepare("DELETE FROM respiratory_events WHERE session_id = ?");
    query.addBindValue(sessionId);
    
    if (!query.exec()) {
        qWarning() << "RespiratoryEventsRepository::deleteBySession() - Failed:"
                   << query.lastError().text();
        DatabaseManager::instance().checkQueryError("RespiratoryEventsRepository::deleteBySession", query);
        return false;
    }

    return true;
}

QList<qint64> RespiratoryEventsRepository::findSessionsWithExcessRows(qint64 profileId)
{
    QList<qint64> sessionIds;
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "RespiratoryEventsRepository::findSessionsWithExcessRows() - Database not open";
        return sessionIds;
    }

    // event_type 1 = EVL_Event; waveform lists never produce respiratory_events rows.
    // LEFT JOIN: rows for a channel with no event lists left at all (e.g. user flags
    // destroyed by a reindex) are stale too.
    QSqlQuery query(db);
    query.prepare(
        "SELECT DISTINCT r.session_id FROM "
        "  (SELECT session_id, channel_id, COUNT(*) AS n FROM respiratory_events "
        "   WHERE profile_id = ? GROUP BY session_id, channel_id) r "
        "LEFT JOIN "
        "  (SELECT session_id, channel_id, SUM(count) AS n FROM event_lists "
        "   WHERE profile_id = ? AND event_type = 1 GROUP BY session_id, channel_id) e "
        "ON r.session_id = e.session_id AND r.channel_id = e.channel_id "
        "WHERE r.n > COALESCE(e.n, 0)"
    );
    query.addBindValue(profileId);
    query.addBindValue(profileId);

    if (!query.exec()) {
        qWarning() << "RespiratoryEventsRepository::findSessionsWithExcessRows() - Failed:"
                   << query.lastError().text();
        DatabaseManager::instance().checkQueryError("RespiratoryEventsRepository::findSessionsWithExcessRows", query);
        return sessionIds;
    }

    while (query.next()) {
        sessionIds.append(query.value(0).toLongLong());
    }
    return sessionIds;
}

QMap<int, int> RespiratoryEventsRepository::countByType(qint64 sessionId)
{
    QMap<int, int> counts;
    
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) {
        qWarning() << "RespiratoryEventsRepository::countByType() - Database not open";
        return counts;
    }
    
    QSqlQuery query(db);
    query.prepare(
        "SELECT event_type, COUNT(*) as count "
        "FROM respiratory_events "
        "WHERE session_id = ? "
        "GROUP BY event_type"
    );
    query.addBindValue(sessionId);
    
    if (!query.exec()) {
        qWarning() << "RespiratoryEventsRepository::countByType() - Query failed:"
                   << query.lastError().text();
        DatabaseManager::instance().checkQueryError("RespiratoryEventsRepository::countByType", query);
        return counts;
    }
    
    while (query.next()) {
        int eventType = query.value(0).toInt();
        int count = query.value(1).toInt();
        counts[eventType] = count;
    }
    
    return counts;
}

RespiratoryEventData RespiratoryEventsRepository::populateFromQuery(const QSqlQuery& query)
{
    RespiratoryEventData data;
    
    data.id = query.value("id").toLongLong();
    data.sessionId = query.value("session_id").toLongLong();
    data.eventType = query.value("event_type").toInt();
    data.startTime = query.value("start_time").toLongLong();
    data.endTime = query.value("end_time").toLongLong();
    data.duration = query.value("duration").toInt();
    data.desaturation = query.value("desaturation").toDouble();
    data.severity = query.value("severity").toInt();
    
    return data;
}
