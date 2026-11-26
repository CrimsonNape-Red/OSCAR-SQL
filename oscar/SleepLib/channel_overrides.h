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
             LowerThreshold, UpperThreshold, ShowInOverview,
             Type    //!< The Flag Type of an event channel (Flag, Minor Flag or Span)
           };

/*! \brief A sparse set of override values; an empty optional means "not overridden".
    An empty string is a valid override. */
struct Values {
    std::optional<bool> enabled;
    std::optional<QColor> color;
    std::optional<QString> fullname, label, description;
    std::optional<double> lowerThreshold, upperThreshold;
    std::optional<bool> showInOverview;
    std::optional<schema::ChanType> type;   //!< Flag Type; only FLAG, MINOR_FLAG or SPAN is used

    //! \brief True when no field is set.
    bool isEmpty() const;
};

//! \brief Answers whether a name is one that OSCAR itself once shipped (so not a user edit).
using ShippedLookup = std::function<bool(const QString &)>;

/*! \brief True for the three types the Flag Type column of Preferences can choose between:
    FLAG, MINOR_FLAG and SPAN. */
bool isFlagKind(schema::ChanType type);

/*! \brief The type whose rules apply to \p chan: the type the code defines (the recorded
    default), not a Flag Type the profile chose; the live type when no defaults are recorded. */
schema::ChanType codeType(const schema::Channel &chan);

/*! \brief Whether \p field of channel \p id may be overridden, \p type being the type the
    code defines for it (see codeType()).
    DATA and SETTING channels have none; WAVEFORM channels all but the type; other types all
    except the thresholds. The type itself is overridable only on FLAG, MINOR_FLAG and SPAN
    channels, and only to another of those three. Leak's upper threshold, the pulse
    thresholds and SpO2's lower threshold are driven by preferences and never overridable. */
bool overridable(ChannelID id, schema::ChanType type, Field field);

/*! \brief The overridable fields of \p current that differ from \p defaults.
    Colours compare by name(); thresholds are equal within 1e-4; a type that is not a flag
    kind is never reported. */
Values diffFrom(ChannelID id, schema::ChanType type,
                const schema::ChannelDefaults &defaults, const Values &current);

//! \brief The overrides implied by \p chan's live values against chan.defaults().
Values diff(const schema::Channel &chan);

/*! \brief Sets the fields present in \p v, skipping any that are not overridable for \p chan
    and a type that is not a flag kind. */
void apply(schema::Channel &chan, const Values &v);

/*! \brief Classifies an old full-copy stored row as overrides.
    The rules are those of the code's type; legacy.type is never used and never becomes a
    type override, because the builds that stored it never read it back, so it was not in
    effect. A name differing from the default is an override
    only when \p wasShipped says it was not an old shipped name (an empty \p wasShipped
    treats no name as shipped); other fields are overrides when they differ from the
    default, except an invalid stored colour, which is never an override. */
Values classifyLegacy(const schema::Channel &chan, const ChannelData &legacy,
                      const ShippedLookup &wasShipped);

} // namespace ChannelOverrides

#endif // CHANNEL_OVERRIDES_H
