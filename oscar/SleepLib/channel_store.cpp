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
#include <QStringList>

#include <optional>

#include "SleepLib/common.h"
#include "SleepLib/schema.h"
#include "database/channel_repository.h"
#include "database/database_manager.h"
#include "database/preferences_repository.h"

namespace {

//! \brief Counts of the name fields a conversion dropped, for the per-profile log line.
struct DroppedNames {
    int equalToDefault = 0;     //!< Stored name equal to today's default
    int shipped = 0;            //!< Stored name that an older build shipped
    QStringList shippedDetail;  //!< One "code field: text" entry per shipped name dropped
};

/*! \brief Records which name fields of \p row the classification dropped, and why.
    Compares the classification without the shipped-names lookup (names that differ from
    today's default) with \p kept, the real classification: a name absent from both equals
    the default; a name present only in the first was dropped as shipped.
    \param chan The channel the row was matched to.
    \param row The channels_legacy row.
    \param kept The overrides classifyLegacy() returned for the row with the real lookup.
    \param dropped Receives the counts and the dropped texts. */
void countDroppedNames(const schema::Channel &chan, const ChannelData &row,
                       const ChannelOverrides::Values &kept, DroppedNames &dropped)
{
    using ChannelOverrides::Values;
    const Values differing = ChannelOverrides::classifyLegacy(chan, row, ChannelOverrides::ShippedLookup());
    struct NameField {
        ChannelOverrides::Field field;
        std::optional<QString> Values::*member;
        const char *name;
    };
    static const NameField fields[] = {
        { ChannelOverrides::Fullname, &Values::fullname, "fullname" },
        { ChannelOverrides::Label, &Values::label, "label" },
        { ChannelOverrides::Description, &Values::description, "description" },
    };
    for (const NameField &f : fields) {
        if (!ChannelOverrides::overridable(chan.id(), ChannelOverrides::codeType(chan), f.field)) {
            continue;
        }
        const std::optional<QString> &before = differing.*(f.member);
        if (!before) {
            ++dropped.equalToDefault;
        } else if (!(kept.*(f.member))) {
            ++dropped.shipped;
            dropped.shippedDetail.append(QStringLiteral("%1 %2: \"%3\"")
                                         .arg(chan.code(), QLatin1String(f.name), *before));
        }
    }
}

/*! \brief Converts the channels_legacy rows of one profile, in one transaction.
    \param profileId The profile to convert.
    \param wasShipped The shipped-names lookup passed to ChannelOverrides::classifyLegacy().
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
    DroppedNames dropped;
    int unmatched = 0;
    for (const ChannelData &row : legacy) {
        const schema::Channel *chan = ChannelStore::findChannel(row.channelId, row.channelCode);
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
        countDroppedNames(*chan, row, out.values, dropped);
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

    // The legacy rows are gone now: this log is the only record of what was dropped. It
    // holds channel text only, never the profile's or the user's name.
    qInfo().nospace() << "ChannelStore::convertLegacyRows: profile " << profileId << ": "
                      << converted.size() << " stored channels converted, " << added.size()
                      << " overrides written, names dropped: " << dropped.equalToDefault
                      << " equal to today's default, " << dropped.shipped << " shipped before; "
                      << unmatched << " unknown channels kept";
    for (const QString &detail : std::as_const(dropped.shippedDetail)) {
        qDebug().noquote() << "ChannelStore::convertLegacyRows: profile" << profileId
                           << "dropped shipped name" << detail;
    }
    return converted.size();
}

} // namespace

namespace ChannelStore {

schema::Channel *findChannel(ChannelID channelId, const QString &channelCode)
{
    schema::Channel *byId = &schema::channel[channelId];
    if (!byId->isNull() && (channelCode.isEmpty() || byId->code() == channelCode)) {
        return byId;
    }
    if (!channelCode.isEmpty()) {
        schema::Channel *byCode = &schema::channel[channelCode];
        if (!byCode->isNull()) {
            return byCode;
        }
    }
    return byId->isNull() ? nullptr : byId;
}

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
        row.type = d.type;  // the code's type; a profile's Flag Type is an override
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

    const QList<qint64> profileIds = ChannelRepository().profilesWithLegacyRows();
    if (profileIds.isEmpty()) {
        return 0;
    }

    // No lookup given: use the bundled list, but only if it loaded completely. With a
    // missing or broken list every old default would be kept as a "rename" and the evidence
    // deleted, so nothing is converted and the rows wait for a later start.
    ChannelOverrides::ShippedLookup lookup = wasShipped;
    if (!lookup) {
        if (!ShippedChannelNames::isUsable()) {
            qCritical() << "ChannelStore::convertLegacyRows: the list of shipped channel names"
                        << "could not be loaded; no stored channel rows were converted"
                        << "(they are kept for a later start)";
            if (failedProfiles != nullptr) {
                for (qint64 profileId : profileIds) {
                    failedProfiles->insert(profileId);
                }
            }
            return 0;
        }
        lookup = &ShippedChannelNames::contains;
    }

    int total = 0;
    for (qint64 profileId : profileIds) {
        const int n = convertProfile(profileId, lookup);
        if (n > 0) {
            total += n;
        } else if (n < 0 && failedProfiles != nullptr) {
            failedProfiles->insert(profileId);
        }
    }
    return total;
}

} // namespace ChannelStore
