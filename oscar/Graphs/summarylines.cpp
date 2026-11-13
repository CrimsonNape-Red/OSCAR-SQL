/* Summary chart dotted lines
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "summarylines.h"

#include <QStringList>

namespace SummaryLines {

QString encode(const QHash<QString, quint32> &masks)
{
    QStringList names = masks.keys();
    names.sort();
    QStringList parts;
    for (const QString &name : names) {
        parts.append(QString("%1=%2").arg(name).arg(masks.value(name)));
    }
    return parts.join(';');
}

QHash<QString, quint32> decode(const QString &text)
{
    QHash<QString, quint32> masks;
    const QStringList parts = text.split(';', Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        const int eq = part.lastIndexOf('=');
        if (eq <= 0) continue;
        bool ok = false;
        const quint32 mask = part.mid(eq + 1).toUInt(&ok);
        if (ok) masks.insert(part.left(eq), mask);
    }
    return masks;
}

} // namespace SummaryLines
