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

#include <QByteArray>
#include <QSet>
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

/*! \brief Parses the bytes of the names list into a set of names.
    The data is UTF-8, one name per line. A line may end in LF or CRLF (a checkout with
    Windows line endings must not add a CR to every name). Within a line, two backslashes
    stand for one backslash and backslash-n for a newline. Empty lines are skipped, so the
    set never holds an empty string.
    \param data The file contents.
    \return The names; empty for empty \p data. */
QSet<QString> parse(const QByteArray &data);

/*! \brief Returns true if the loaded list looks complete and correctly decoded.
    Converting the pre-v20 channel rows is irreversible, and it treats a name missing from the
    list as a user rename. A missing, truncated or wrongly decoded resource would otherwise
    be mistaken for "OSCAR never shipped any of these names" and turn every old default into
    a permanent override. The check is fixed, not measured against the running build's
    names, because the running language may be one whose names the frozen list lacks: the
    list must hold at least 20000 names and four sentinels, English and Chinese, that every
    correct list contains ("Large Leak", "RERA", "Flow Limit.", "气流限制."). */
bool isUsable();

} // namespace ShippedChannelNames

#endif // SHIPPED_CHANNEL_NAMES_H
