/* Channel store
 *
 * Keeps the global channel_defaults table equal to the code defaults of the running
 * build, so the channels view and the per-profile overrides are always measured against
 * the current names, colours and thresholds.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "channel_store.h"

#include <QDebug>

#include "SleepLib/schema.h"
#include "database/channel_repository.h"
#include "database/database_manager.h"

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

} // namespace ChannelStore
