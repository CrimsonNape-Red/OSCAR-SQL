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

#ifndef CHANNEL_STORE_H
#define CHANNEL_STORE_H

//! \brief Bridges the channel registry (schema::channel) and the channel tables.
namespace ChannelStore {

/*! \brief Writes every channel's code defaults to channel_defaults.
    First snapshots the defaults of any channel that has none yet (schema::snapshotDefaults()),
    then writes each channel's defaults(), never its current values, which may carry an open
    profile's overrides. Rows for channels the build no longer defines are removed.
    \return false if the database is not open or the write failed. */
bool syncDefaults();

} // namespace ChannelStore

#endif // CHANNEL_STORE_H
