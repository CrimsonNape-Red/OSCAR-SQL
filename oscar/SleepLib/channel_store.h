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

#ifndef CHANNEL_STORE_H
#define CHANNEL_STORE_H

#include <QSet>

#include "SleepLib/channel_overrides.h"
#include "SleepLib/shipped_channel_names.h"

//! \brief Bridges the channel registry (schema::channel) and the channel tables.
namespace ChannelStore {

/*! \brief Writes every channel's code defaults to channel_defaults.
    First snapshots the defaults of any channel that has none yet (schema::snapshotDefaults()),
    then writes each channel's defaults(), never its current values, which may carry an open
    profile's overrides. Rows for channels the build no longer defines are removed.
    \return false if the database is not open or the write failed. */
bool syncDefaults();

/*! \brief Converts the channels_legacy rows of every profile into overrides.
    Each row is matched to a channel of the registry by id, or by channel_code when the id is
    unknown or names a channel with a different code, and classified against that channel's
    code defaults (ChannelOverrides::classifyLegacy). The non-empty results are stored as
    override rows under the registry's id and code; an override row the profile already has
    for that channel wins. Matched rows are deleted; a row matching no channel stays and is
    tried again on a later run. Each converted profile gets STR_PREF_ChannelsDatImported, so
    a channels.dat left in its folder is never imported over the converted values.
    One transaction per profile: a profile whose conversion fails is left exactly as it was
    and the others still convert. No schema::Channel value is changed (the defaults snapshot
    is taken first for channels that have none, see schema::snapshotDefaults()).
    \param wasShipped Says whether a name is one OSCAR once shipped (a default, not a rename).
    \param failedProfiles If given, receives the ids of the profiles whose conversion failed
           and was rolled back (their rows are still in channels_legacy).
    \return The number of legacy rows that matched a channel and were removed, whether or not
            they produced an override. */
int convertLegacyRows(const ChannelOverrides::ShippedLookup &wasShipped = &ShippedChannelNames::contains,
                      QSet<qint64> *failedProfiles = nullptr);

} // namespace ChannelStore

#endif // CHANNEL_STORE_H
