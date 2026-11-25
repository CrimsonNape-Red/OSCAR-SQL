/* Shipped channel names
 *
 * The list of every channel full name, label and description OSCAR has ever shipped, in
 * English and in every translation. Converting the pre-v20 per-profile channel copies uses
 * it to tell a name the user typed from a default an older build wrote.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef SHIPPED_CHANNEL_NAMES_H
#define SHIPPED_CHANNEL_NAMES_H

#include <QString>

//! \brief Lookup in the generated list of channel names OSCAR has shipped.
//!
//! The list is the resource :/docs/shipped_channel_names.txt, produced once by
//! Tools/gen_shipped_channel_names.py from the git history. It is frozen at the schema v20
//! release: only builds before v20 stored channel names per profile, so it never needs
//! regenerating.
namespace ShippedChannelNames {

/*! \brief Returns true if \p name is a channel full name, label or description that some
    OSCAR build shipped, in any language.
    The list is loaded on first use. An empty \p name is never in the list.
    \param name The text to look up, compared exactly (case and spacing matter). */
bool contains(const QString &name);

} // namespace ShippedChannelNames

#endif // SHIPPED_CHANNEL_NAMES_H
