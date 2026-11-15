/* Session gap Unit Tests
 *
 * Covers SessionGaps::gaps(): the spaces between a night's sessions, after sessions that
 * touch or overlap are merged, with nothing before the first or after the last.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "sessiongaptests.h"
#include "Graphs/gSessionGapLayer.h"

namespace {
bool sameSpans(const QVector<SessionGaps::Span> &a, const QVector<SessionGaps::Span> &b)
{
    if (a.size() != b.size()) return false;
    for (int i = 0; i < a.size(); ++i) {
        if (a[i].start != b[i].start || a[i].end != b[i].end) return false;
    }
    return true;
}
}

void SessionGapTests::testNoGapForOneSession()
{
    QVERIFY(SessionGaps::gaps({{0, 100}}).isEmpty());
}

void SessionGapTests::testGapBetweenTwo()
{
    QVERIFY(sameSpans(SessionGaps::gaps({{0, 100}, {130, 200}}), {{100, 130}}));
    QVERIFY(sameSpans(SessionGaps::gaps({{0, 100}, {130, 200}, {260, 300}}), {{100, 130}, {200, 260}}));
}

void SessionGapTests::testOverlapMerged()
{
    QVERIFY(SessionGaps::gaps({{0, 100}, {90, 200}}).isEmpty());
    QVERIFY(SessionGaps::gaps({{0, 100}, {100, 200}}).isEmpty());          // touching
    QVERIFY(SessionGaps::gaps({{0, 300}, {50, 100}}).isEmpty());           // one inside another
    QVERIFY(sameSpans(SessionGaps::gaps({{0, 100}, {90, 200}, {250, 300}}), {{200, 250}}));
}

void SessionGapTests::testUnsorted()
{
    QVERIFY(sameSpans(SessionGaps::gaps({{130, 200}, {0, 100}}), {{100, 130}}));
}

void SessionGapTests::testEmpty()
{
    QVERIFY(SessionGaps::gaps({}).isEmpty());
}
