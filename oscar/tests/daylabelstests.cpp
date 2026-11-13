/* Day label Unit Tests
 *
 * Covers the placement of date labels on an Overview date axis: spacing, the bold label of
 * the day under the mouse, the underlined label of a day with a details popup, and that
 * ordinary labels keep their place when the mouse moves or the axis scrolls.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "daylabelstests.h"
#include "Graphs/daylabels.h"

#include <algorithm>

using DayLabels::Label;

namespace {
const QDate kFirst(2026, 10, 11);
const double kBar = 1190.0 / 30, kText = 44, kGap = 8;

QVector<Label> sorted(QVector<Label> labels)
{
    std::sort(labels.begin(), labels.end(), [](const Label &a, const Label &b) { return a.x < b.x; });
    return labels;
}

const Label *find(const QVector<Label> &labels, const QDate &date)
{
    for (const Label &l : labels) {
        if (l.date == date) return &l;
    }
    return nullptr;
}
}

void DayLabelsTests::testLabelsDoNotOverlap()
{
    const QVector<Label> labels = sorted(DayLabels::layout(kFirst, 30, kBar, kText, kGap));
    QVERIFY(labels.size() > 5);
    for (int i = 1; i < labels.size(); ++i) {
        QVERIFY(labels[i].x - labels[i - 1].x >= kText + kGap);
    }
    for (const Label &l : labels) {
        QVERIFY(!l.bold);
        // Centred under its day
        QCOMPARE(l.x + kText / 2, (kFirst.daysTo(l.date) + 0.5) * kBar);
    }
}

void DayLabelsTests::testHoverIsBoldAndNeighboursStayPut()
{
    const QVector<Label> plain = DayLabels::layout(kFirst, 30, kBar, kText, kGap);
    QDate hover(2026, 11, 1);
    if (find(plain, hover)) hover = hover.addDays(1);   // not an ordinary label, so it has neighbours to crowd out
    QVERIFY(!find(plain, hover));
    const QVector<Label> labels = sorted(DayLabels::layout(kFirst, 30, kBar, kText, kGap, hover));

    const Label *h = find(labels, hover);
    QVERIFY(h && h->bold && !h->underlined);
    for (int i = 1; i < labels.size(); ++i) {
        QVERIFY(labels[i].x - labels[i - 1].x >= kText + kGap);
    }
    // Every other label is one of the plain ones, at the same place
    for (const Label &l : labels) {
        if (l.date == hover) continue;
        const Label *p = find(plain, l.date);
        QVERIFY(p);
        QCOMPARE(l.x, p->x);
    }
}

void DayLabelsTests::testPinnedIsUnderlined()
{
    const QDate pin(2026, 10, 28), hover(2026, 11, 3);
    const QVector<Label> labels = DayLabels::layout(kFirst, 30, kBar, kText, kGap, hover, { pin });
    const Label *p = find(labels, pin);
    QVERIFY(p && p->bold && p->underlined);

    // The hovered day wins where the two would overlap
    const QVector<Label> close = DayLabels::layout(kFirst, 30, kBar, kText, kGap, pin.addDays(1), { pin });
    QVERIFY(!find(close, pin));
    QVERIFY(find(close, pin.addDays(1)));

    // Hovering the pinned day keeps its underline
    const QVector<Label> same = DayLabels::layout(kFirst, 30, kBar, kText, kGap, pin, { pin });
    QVERIFY(find(same, pin)->underlined);

    // Days outside the axis are ignored
    QVERIFY(!find(DayLabels::layout(kFirst, 30, kBar, kText, kGap, QDate(), { kFirst.addDays(-3) }), kFirst.addDays(-3)));
}

void DayLabelsTests::testSingleDay()
{
    const QVector<Label> labels = DayLabels::layout(kFirst, 1, 1190, kText, kGap);
    QCOMPARE(labels.size(), 1);
    QCOMPARE(labels[0].date, kFirst);
    QVERIFY(DayLabels::layout(kFirst, 0, 10, kText, kGap).isEmpty());
    QVERIFY(DayLabels::layout(QDate(), 5, 10, kText, kGap).isEmpty());
}

void DayLabelsTests::testGridFollowsDates()
{
    // Scrolled by one day, the same dates are labelled
    const QVector<Label> a = DayLabels::layout(kFirst, 30, kBar, kText, kGap);
    const QVector<Label> b = DayLabels::layout(kFirst.addDays(1), 30, kBar, kText, kGap);
    for (const Label &l : b) {
        if (l.date <= kFirst.addDays(29)) QVERIFY(find(a, l.date));
    }
    // The 2.1 rule counts from the first day shown
    const QVector<Label> old = DayLabels::layout(kFirst.addDays(1), 30, kBar, kText, kGap, QDate(), {}, false);
    QCOMPARE(old.first().date, kFirst.addDays(1));
}
