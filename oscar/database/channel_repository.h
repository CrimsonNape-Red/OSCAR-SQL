/* Channel Repository Header
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

#ifndef CHANNEL_REPOSITORY_H
#define CHANNEL_REPOSITORY_H

#include <QSqlDatabase>
#include <QColor>
#include <QString>
#include <QList>
#include <QSet>
#include "SleepLib/machine_common.h"
#include "SleepLib/channel_overrides.h"

class QSqlQuery;

/**
 * @brief A row of channels_legacy: one profile's full stored copy of one channel, as
 * written by OSCAR before schema v20.
 */
struct ChannelData {
    qint64 id = 0;
    qint64 profileId = 0;
    ChannelID channelId = 0;
    QString channelCode;
    int type = 0;  // Channel type from schema (FLAG, WAVEFORM, DATA, etc.)
    bool enabled = true;

    // Display properties
    QColor defaultColor;
    QString fullname;
    QString label;
    QString description;

    // Threshold settings
    double lowerThreshold = 0.0;
    QColor lowerThresholdColor;
    double upperThreshold = 0.0;
    QColor upperThresholdColor;

    // UI preferences
    bool showInOverview = false;
};

/**
 * @brief A row of channel_defaults: one channel's code defaults in the current language.
 */
struct ChannelDefaultRow {
    ChannelID channelId = 0; QString channelCode; int type = 0;
    bool enabled = true; QColor defaultColor; QString fullname, label, description;
    double lowerThreshold = 0; QColor lowerThresholdColor;
    double upperThreshold = 0; QColor upperThresholdColor; bool showInOverview = false;
};

/**
 * @brief A row of profile_channel_overrides: the fields one profile changed on one
 * channel. An absent value means "use the default" and is stored as NULL.
 */
struct ChannelOverrideRow { ChannelID channelId = 0; QString channelCode; ChannelOverrides::Values values; };

/**
 * @brief Repository for the channel tables of schema v20.
 *
 * OSCAR never writes the channels view; it writes channel_defaults (from code) and
 * profile_channel_overrides (from the user's changes), and reads channels_legacy only
 * to convert it.
 */
class ChannelRepository
{
public:
    //! \brief Uses the application's database connection.
    ChannelRepository();
    //! \brief Uses \p database, e.g. a private connection.
    explicit ChannelRepository(const QSqlDatabase &database);

    /**
     * @brief Makes channel_defaults equal to \p rows in one transaction.
     * Inserts new channels, rewrites rows whose stored values differ, leaves identical
     * rows untouched, and deletes channels not in \p rows.
     */
    bool syncDefaults(const QList<ChannelDefaultRow> &rows);

    //! \brief All override rows of profile \p profileId, ordered by channel id.
    QList<ChannelOverrideRow> findOverrides(qint64 profileId);

    /**
     * @brief Replaces a profile's overrides for the channels the running build knows.
     * In one transaction, deletes the profile's rows whose channel_id is in \p knownIds or
     * whose channel_code is in \p knownCodes, then inserts every row of \p rows whose values
     * are not empty. Rows for channels unknown to this build are left alone.
     */
    bool replaceOverrides(qint64 profileId, const QList<ChannelOverrideRow> &rows,
                          const QSet<ChannelID> &knownIds, const QSet<QString> &knownCodes);

    //! \brief Ids of the profiles that have rows in channels_legacy, ascending.
    QList<qint64> profilesWithLegacyRows();

    //! \brief The channels_legacy rows of profile \p profileId, ordered by channel id.
    QList<ChannelData> findLegacy(qint64 profileId);

    //! \brief Inserts \p rows into channels_legacy for profile \p profileId, in one transaction.
    bool insertLegacy(qint64 profileId, const QList<ChannelData> &rows);

    //! \brief Deletes the channels_legacy rows whose primary keys are \p rowIds.
    bool deleteLegacy(const QList<qint64> &rowIds);

    //! \brief A colour as #RRGGBB, or an empty string for an invalid colour.
    static QString colorToString(const QColor &color);
    //! \brief The colour named by \p colorStr, or an invalid colour for an empty string.
    static QColor stringToColor(const QString &colorStr);

private:
    QSqlDatabase db;

    //! \brief Maps the current row of a channels_legacy query.
    ChannelData mapLegacy(const QSqlQuery &query);
    //! \brief Maps the current row of a profile_channel_overrides query.
    ChannelOverrideRow mapOverride(const QSqlQuery &query);
};

#endif // CHANNEL_REPOSITORY_H
