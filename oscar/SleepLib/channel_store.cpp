/* Channel store
 *
 * Keeps the global channel_defaults table equal to the code defaults of the running
 * build, so the channels view and the per-profile overrides are always measured against
 * the current names, colours and thresholds; and converts the pre-v20 per-profile channel
 * copies (channels_legacy) into those overrides.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "channel_store.h"

#include <QDebug>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>

#include "SleepLib/common.h"
#include "SleepLib/schema.h"
#include "database/channel_repository.h"
#include "database/database_manager.h"
#include "database/preferences_repository.h"

namespace {

/*! \brief The registry channel a channels_legacy row belongs to, or nullptr if none.
    The stored id is tried first. When the id is unknown, or names a channel whose code is
    not the stored code (the id has since been given to another channel), the stored code
    decides; as the OSCAR 1.x channels.dat reader did, a known code wins over the id. */
schema::Channel *findChannel(const ChannelData &row)
{
    schema::Channel *byId = &schema::channel[row.channelId];
    if (!byId->isNull() && (row.channelCode.isEmpty() || byId->code() == row.channelCode)) {
        return byId;
    }
    if (!row.channelCode.isEmpty()) {
        schema::Channel *byCode = &schema::channel[row.channelCode];
        if (!byCode->isNull()) {
            return byCode;
        }
    }
    return byId->isNull() ? nullptr : byId;
}

/*! \brief Converts the channels_legacy rows of one profile, in one transaction.
    \return The number of legacy rows matched and removed, or -1 if nothing was written. */
int convertProfile(qint64 profileId, const ChannelOverrides::ShippedLookup &wasShipped)
{
    QSqlDatabase db = DatabaseManager::instance().database();
    ChannelRepository repo(db);
    const QList<ChannelData> legacy = repo.findLegacy(profileId);
    if (legacy.isEmpty()) {
        return 0;
    }

    // An override row the profile already has decides that channel; the legacy row is
    // still removed.
    QSet<ChannelID> coveredIds;
    QSet<QString> coveredCodes;
    for (const ChannelOverrideRow &row : repo.findOverrides(profileId)) {
        coveredIds.insert(row.channelId);
        coveredCodes.insert(row.channelCode);
    }

    QList<ChannelOverrideRow> added;
    QList<qint64> converted;
    int unmatched = 0;
    for (const ChannelData &row : legacy) {
        const schema::Channel *chan = findChannel(row);
        if (chan == nullptr) {
            ++unmatched;    // kept for a later build that knows the channel
            continue;
        }
        converted.append(row.id);
        if (coveredIds.contains(chan->id()) || coveredCodes.contains(chan->code())) {
            continue;
        }
        ChannelOverrideRow out;
        out.channelId = chan->id();
        out.channelCode = chan->code();
        out.values = ChannelOverrides::classifyLegacy(*chan, row, wasShipped);
        if (!out.values.isEmpty()) {
            added.append(out);
            coveredIds.insert(out.channelId);
            coveredCodes.insert(out.channelCode);
        }
    }

    if (!db.transaction()) {
        qWarning() << "ChannelStore::convertLegacyRows: cannot start a transaction for profile"
                   << profileId << ":" << db.lastError().text();
        return -1;
    }
    // The mark goes under category "profile", where Profile's own (*this)[key] values are
    // saved and loaded (Profile::saveProfilePreferencesToDatabase()), as a "bool".
    const bool written = repo.writeConversion(profileId, added, converted)
        && PreferencesRepository().savePreference(profileId, QStringLiteral("profile"),
                                                  STR_PREF_ChannelsDatImported, QVariant(true));
    if (!written || !db.commit()) {
        qCritical() << "ChannelStore::convertLegacyRows: converting the stored channels of profile"
                    << profileId << "failed; left unchanged:" << db.lastError().text();
        db.rollback();
        return -1;
    }

    qDebug() << "ChannelStore::convertLegacyRows: profile" << profileId << "-" << converted.size()
             << "stored channels converted," << added.size() << "overrides added,"
             << unmatched << "unknown channels kept";
    return converted.size();
}

} // namespace

namespace ChannelStore {

bool syncDefaults()
{
    if (!DatabaseManager::instance().isOpen()) {
        qWarning() << "ChannelStore::syncDefaults: database is not open";
        return false;
    }

    // The registry is complete by now (schema plus every loader's initChannels()), so this
    // is where the application takes its snapshot of the code defaults.
    schema::snapshotDefaults();

    QList<ChannelDefaultRow> rows;
    rows.reserve(schema::channel.channels.size());
    for (const schema::Channel *chan : std::as_const(schema::channel.channels)) {
        const schema::ChannelDefaults &d = chan->defaults();
        ChannelDefaultRow row;
        row.channelId = chan->id();
        row.channelCode = chan->code();
        row.type = chan->type();
        row.enabled = d.enabled;
        row.defaultColor = d.color;
        row.fullname = d.fullname;
        row.label = d.label;
        row.description = d.description;
        row.lowerThreshold = d.lowerThreshold;
        row.lowerThresholdColor = d.lowerThresholdColor;
        row.upperThreshold = d.upperThreshold;
        row.upperThresholdColor = d.upperThresholdColor;
        row.showInOverview = d.showInOverview;
        rows.append(row);
    }

    if (!ChannelRepository().syncDefaults(rows)) {
        qWarning() << "ChannelStore::syncDefaults: writing channel_defaults failed";
        return false;
    }
    return true;
}

int convertLegacyRows(const ChannelOverrides::ShippedLookup &wasShipped, QSet<qint64> *failedProfiles)
{
    if (!DatabaseManager::instance().isOpen()) {
        qWarning() << "ChannelStore::convertLegacyRows: database is not open";
        return 0;
    }

    // The OSCAR 1.x import at startup runs before Profiles::Scan() has taken the snapshot of
    // the code defaults; classifying needs it. Channels that have one are left alone.
    schema::snapshotDefaults();

    int total = 0;
    const QList<qint64> profileIds = ChannelRepository().profilesWithLegacyRows();
    for (qint64 profileId : profileIds) {
        const int n = convertProfile(profileId, wasShipped);
        if (n > 0) {
            total += n;
        } else if (n < 0 && failedProfiles != nullptr) {
            failedProfiles->insert(profileId);
        }
    }
    return total;
}

} // namespace ChannelStore
