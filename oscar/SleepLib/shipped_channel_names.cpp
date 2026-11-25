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

//! The fewest names a complete list holds (the list as generated has 24,914).
const int kMinimumNames = 20000;

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

/*! \brief Reads the resource into a set.
    A missing resource yields an empty set, so nothing is ever treated as a shipped name,
    and a warning is logged; isUsable() then reports the list as unusable. */
QSet<QString> loadNames()
{
    QFile file(kResourcePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "ShippedChannelNames: cannot open" << kResourcePath;
        return QSet<QString>();
    }
    return ShippedChannelNames::parse(file.readAll());
}

/*! \brief The list, read from the resource on the first call.
    The initialisation of the function-local static is thread-safe. */
const QSet<QString> &loadedNames()
{
    static const QSet<QString> names = loadNames();
    return names;
}

} // namespace

QSet<QString> ShippedChannelNames::parse(const QByteArray &data)
{
    QSet<QString> names;
    const QList<QByteArray> lines = data.split('\n');
    names.reserve(lines.size());
    for (QByteArray raw : lines) {
        // A checkout with Windows line endings leaves a CR before each LF.
        if (raw.endsWith('\r')) {
            raw.chop(1);
        }
        if (raw.isEmpty()) {
            continue;
        }
        names.insert(unescapeLine(QString::fromUtf8(raw)));
    }
    return names;
}

bool ShippedChannelNames::contains(const QString &name)
{
    if (name.isEmpty()) {
        return false;
    }
    return loadedNames().contains(name);
}

bool ShippedChannelNames::isUsable()
{
    const QSet<QString> &names = loadedNames();
    if (names.size() < kMinimumNames) {
        qWarning() << "ShippedChannelNames: the list holds only" << names.size() << "names";
        return false;
    }
    // A full name, a label and a description in English, and the Chinese translation of
    // that description, which only a correct UTF-8 decoding yields. Written with escapes so
    // the check does not depend on the compiler's source character set.
    const QString sentinels[] = {
        QStringLiteral("Large Leak"),
        QStringLiteral("RERA"),
        QStringLiteral("Flow Limit."),
        QStringLiteral("气流限制."),   // "气流限制." (Flow Limit.)
    };
    for (const QString &sentinel : sentinels) {
        if (!names.contains(sentinel)) {
            qWarning() << "ShippedChannelNames: the list lacks" << sentinel;
            return false;
        }
    }
    return true;
}
