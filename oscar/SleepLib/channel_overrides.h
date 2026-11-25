/* Channel overrides
 *
 * Rules for which channel fields a profile may override, and the helpers that compare a
 * channel with its code defaults (see schema::ChannelDefaults) to find the overrides.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef CHANNEL_OVERRIDES_H
#define CHANNEL_OVERRIDES_H

#include <QColor>
#include <QString>
#include <functional>
#include <optional>

#include "SleepLib/schema.h"

struct ChannelData;

/*! \brief Which display fields of a channel a profile may override, and how to find them.
    An "override" is a field whose value differs from the code default. */
namespace ChannelOverrides {

//! \brief The channel fields that can be overridden.
enum Field { Enabled, Color, Fullname, Label, Description,
             LowerThreshold, UpperThreshold, ShowInOverview };

/*! \brief A sparse set of override values; an empty optional means "not overridden".
    An empty string is a valid override. */
struct Values {
    std::optional<bool> enabled;
    std::optional<QColor> color;
    std::optional<QString> fullname, label, description;
    std::optional<double> lowerThreshold, upperThreshold;
    std::optional<bool> showInOverview;

    //! \brief True when no field is set.
    bool isEmpty() const;
};

//! \brief Answers whether a name is one that OSCAR itself once shipped (so not a user edit).
using ShippedLookup = std::function<bool(const QString &)>;

/*! \brief Whether \p field of channel \p id of type \p type may be overridden.
    DATA and SETTING channels have none; WAVEFORM channels all; other types all except the
    thresholds. Leak's upper threshold, the pulse thresholds and SpO2's lower threshold are
    driven by preferences and never overridable. */
bool overridable(ChannelID id, schema::ChanType type, Field field);

/*! \brief The overridable fields of \p current that differ from \p defaults.
    Colours compare by name(); thresholds are equal within 1e-4. */
Values diffFrom(ChannelID id, schema::ChanType type,
                const schema::ChannelDefaults &defaults, const Values &current);

//! \brief The overrides implied by \p chan's live values against chan.defaults().
Values diff(const schema::Channel &chan);

//! \brief Sets the fields present in \p v, skipping any that are not overridable for \p chan.
void apply(schema::Channel &chan, const Values &v);

/*! \brief Classifies an old full-copy stored row as overrides.
    Uses chan.type(), never legacy.type. A name differing from the default is an override
    only when \p wasShipped says it was not an old shipped name (an empty \p wasShipped
    treats no name as shipped); other fields are overrides when they differ from the
    default, except an invalid stored colour, which is never an override. */
Values classifyLegacy(const schema::Channel &chan, const ChannelData &legacy,
                      const ShippedLookup &wasShipped);

} // namespace ChannelOverrides

#endif // CHANNEL_OVERRIDES_H
