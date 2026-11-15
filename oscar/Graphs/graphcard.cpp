/* Graph card
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "graphcard.h"

namespace GraphCard {

QRect headerRect(const QRect &card)
{
    return QRect(card.left() + Padding, card.top() + TopPadding, card.width() - 2 * Padding, HeaderHeight);
}

QRect menuButtonRect(const QRect &card)
{
    const QRect header = headerRect(card);
    return QRect(header.left() + header.width() - MenuButton, header.top() + (HeaderHeight - MenuButton) / 2,
                 MenuButton, MenuButton);
}

bool inHeader(const QRect &card, const QPoint &p)
{
    return QRect(card.left(), card.top(), card.width(), TopPadding + HeaderHeight).contains(p);
}

QString elideSummary(const QString &text, const std::function<int(const QString &)> &width, int available)
{
    if (available <= 0 || text.isEmpty()) return QString();
    if (width(text) <= available) return text;
    const QChar ellipsis(0x2026);
    // Longest prefix that fits with the ellipsis after it
    int lo = 0, hi = text.size() - 1;
    while (lo < hi) {
        const int mid = (lo + hi + 1) / 2;
        if (width(text.left(mid).trimmed() + ellipsis) <= available) lo = mid; else hi = mid - 1;
    }
    return lo > 0 ? text.left(lo).trimmed() + ellipsis : QString();
}

QString headerUnits(const QString &title, const QString &units)
{
    // Daily's captions put a description before the unit, which is in brackets on its own last
    // line ("Flow Rate\nBreathing flow rate waveform\n(L/min)"): the header shows the unit only
    const QString lastLine = units.section('\n', -1).trimmed();
    if (units.contains('\n') && lastLine.startsWith('(') && lastLine.endsWith(')')) {
        return lastLine.mid(1, lastLine.size() - 2).trimmed();
    }
    QString text = units.simplified();
    if (text.startsWith(title, Qt::CaseInsensitive)) text = text.mid(title.size()).trimmed();
    if (text.startsWith('(') && text.endsWith(')')) text = text.mid(1, text.size() - 2).trimmed();
    return text;
}

HeaderSplit splitHeader(const QRect &headerText, int legendWidth, int minText)
{
    if (headerText.isEmpty()) return {};
    const int legend = qBound(0, legendWidth, qMax(0, headerText.width() - minText));
    HeaderSplit split;
    split.text = QRect(headerText.left(), headerText.top(), headerText.width() - legend, headerText.height());
    split.legend = QRect(headerText.left() + split.text.width(), headerText.top(), legend, headerText.height());
    return split;
}

} // namespace GraphCard
