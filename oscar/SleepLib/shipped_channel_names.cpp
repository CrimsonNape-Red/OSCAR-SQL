/* Shipped channel names
 *
 * Loads the generated list of channel names OSCAR has shipped (see
 * Tools/gen_shipped_channel_names.py) and answers membership queries.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "shipped_channel_names.h"

#include <QFile>
#include <QSet>
#include <QtDebug>

namespace {

//! The Qt resource holding the list: UTF-8, one string per line, backslash written as
//! two backslashes and a newline as backslash-n.
const QString kResourcePath = QStringLiteral(":/docs/shipped_channel_names.txt");

/*! \brief Reverses the generator's escaping of one line.
    \param line A line of the resource, without its line terminator.
    \return The original string: "\\\\" becomes a backslash and "\\n" a newline. */
QString unescapeLine(const QString &line)
{
    QString out;
    out.reserve(line.size());
    for (int i = 0; i < line.size(); ++i) {
        const QChar c = line.at(i);
        if (c == QLatin1Char('\\') && i + 1 < line.size()) {
            const QChar next = line.at(i + 1);
            if (next == QLatin1Char('n')) {
                out += QLatin1Char('\n');
                ++i;
                continue;
            }
            if (next == QLatin1Char('\\')) {
                out += QLatin1Char('\\');
                ++i;
                continue;
            }
        }
        out += c;
    }
    return out;
}

/*! \brief Reads the resource into a set. Done once, on first use.
    A missing resource yields an empty set, so nothing is ever treated as a shipped name,
    and a warning is logged. */
QSet<QString> loadNames()
{
    QSet<QString> names;
    QFile file(kResourcePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "ShippedChannelNames: cannot open" << kResourcePath;
        return names;
    }
    const QList<QByteArray> lines = file.readAll().split('\n');
    names.reserve(lines.size());
    for (const QByteArray &raw : lines) {
        if (raw.isEmpty()) {
            continue;
        }
        names.insert(unescapeLine(QString::fromUtf8(raw)));
    }
    return names;
}

} // namespace

bool ShippedChannelNames::contains(const QString &name)
{
    if (name.isEmpty()) {
        return false;
    }
    // Initialised on first call; the initialisation is thread-safe.
    static const QSet<QString> names = loadNames();
    return names.contains(name);
}
