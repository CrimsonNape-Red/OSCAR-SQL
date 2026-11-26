/* Channel overrides
 *
 * Rules for which channel fields a profile may override, and the helpers that compare a
 * channel with its code defaults to find the overrides.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "channel_overrides.h"

#include <cmath>

#include "database/channel_repository.h"

namespace ChannelOverrides {

namespace {

const double kThresholdEpsilon = 1e-4;

bool sameColor(const QColor &a, const QColor &b) { return a.name() == b.name(); }
bool sameThreshold(double a, double b) { return std::fabs(a - b) < kThresholdEpsilon; }

} // namespace

bool Values::isEmpty() const
{
    return !enabled && !color && !fullname && !label && !description
        && !lowerThreshold && !upperThreshold && !showInOverview;
}

bool overridable(ChannelID id, schema::ChanType type, Field field)
{
    if (type == schema::DATA || type == schema::SETTING) {
        return false;
    }
    if (field == LowerThreshold || field == UpperThreshold) {
        if (type != schema::WAVEFORM) {
            return false;
        }
        // Thresholds that preferences drive.
        if (id == CPAP_Leak && field == UpperThreshold) return false;
        if (id == OXI_Pulse) return false;
        if (id == OXI_SPO2 && field == LowerThreshold) return false;
    }
    return true;
}

Values diffFrom(ChannelID id, schema::ChanType type,
                const schema::ChannelDefaults &defaults, const Values &current)
{
    Values out;
    if (current.enabled && overridable(id, type, Enabled)
            && *current.enabled != defaults.enabled) {
        out.enabled = current.enabled;
    }
    if (current.color && overridable(id, type, Color)
            && !sameColor(*current.color, defaults.color)) {
        out.color = current.color;
    }
    if (current.fullname && overridable(id, type, Fullname)
            && *current.fullname != defaults.fullname) {
        out.fullname = current.fullname;
    }
    if (current.label && overridable(id, type, Label)
            && *current.label != defaults.label) {
        out.label = current.label;
    }
    if (current.description && overridable(id, type, Description)
            && *current.description != defaults.description) {
        out.description = current.description;
    }
    if (current.lowerThreshold && overridable(id, type, LowerThreshold)
            && !sameThreshold(*current.lowerThreshold, defaults.lowerThreshold)) {
        out.lowerThreshold = current.lowerThreshold;
    }
    if (current.upperThreshold && overridable(id, type, UpperThreshold)
            && !sameThreshold(*current.upperThreshold, defaults.upperThreshold)) {
        out.upperThreshold = current.upperThreshold;
    }
    if (current.showInOverview && overridable(id, type, ShowInOverview)
            && *current.showInOverview != defaults.showInOverview) {
        out.showInOverview = current.showInOverview;
    }
    return out;
}

Values diff(const schema::Channel &chan)
{
    if (!chan.hasDefaults()) {
        return Values();
    }
    Values cur;
    cur.enabled = chan.enabled();
    cur.color = chan.defaultColor();
    cur.fullname = chan.fullname();
    cur.label = chan.label();
    cur.description = chan.description();
    cur.lowerThreshold = static_cast<double>(chan.lowerThreshold());
    cur.upperThreshold = static_cast<double>(chan.upperThreshold());
    cur.showInOverview = chan.showInOverview();
    return diffFrom(chan.id(), chan.type(), chan.defaults(), cur);
}

void apply(schema::Channel &chan, const Values &v)
{
    const ChannelID id = chan.id();
    const schema::ChanType type = chan.type();
    if (v.enabled && overridable(id, type, Enabled)) chan.setEnabled(*v.enabled);
    if (v.color && overridable(id, type, Color)) chan.setDefaultColor(*v.color);
    if (v.fullname && overridable(id, type, Fullname)) chan.setFullname(*v.fullname);
    if (v.label && overridable(id, type, Label)) chan.setLabel(*v.label);
    if (v.description && overridable(id, type, Description)) chan.setDescription(*v.description);
    if (v.lowerThreshold && overridable(id, type, LowerThreshold)) {
        chan.setLowerThreshold(static_cast<EventDataType>(*v.lowerThreshold));
    }
    if (v.upperThreshold && overridable(id, type, UpperThreshold)) {
        chan.setUpperThreshold(static_cast<EventDataType>(*v.upperThreshold));
    }
    if (v.showInOverview && overridable(id, type, ShowInOverview)) {
        chan.setShowInOverview(*v.showInOverview);
    }
}

Values classifyLegacy(const schema::Channel &chan, const ChannelData &legacy,
                      const ShippedLookup &wasShipped)
{
    if (!chan.hasDefaults()) {
        return Values();
    }
    Values stored;
    stored.enabled = legacy.enabled;
    // A NULL or unparsable stored colour is invalid; it would compare as #000000 and be
    // written back as NULL, so it is not an override.
    if (legacy.defaultColor.isValid()) {
        stored.color = legacy.defaultColor;
    }
    stored.fullname = legacy.fullname;
    stored.label = legacy.label;
    stored.description = legacy.description;
    stored.lowerThreshold = legacy.lowerThreshold;
    stored.upperThreshold = legacy.upperThreshold;
    stored.showInOverview = legacy.showInOverview;

    Values out = diffFrom(chan.id(), chan.type(), chan.defaults(), stored);

    // A name that OSCAR itself once shipped is not a user edit.
    auto dropShipped = [&](std::optional<QString> &name) {
        if (name && wasShipped && wasShipped(*name)) {
            name.reset();
        }
    };
    dropShipped(out.fullname);
    dropShipped(out.label);
    dropShipped(out.description);
    return out;
}

} // namespace ChannelOverrides
