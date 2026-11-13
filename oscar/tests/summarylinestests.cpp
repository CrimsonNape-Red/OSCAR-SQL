/* Summary chart dotted lines Unit Tests
 *
 * Covers the text form in which the user's choice of dotted lines is kept in the profile
 * preferences.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "summarylinestests.h"
#include "Graphs/summarylines.h"

void SummaryLinesTests::testEncodeDecodeRoundTrip()
{
    QHash<QString, quint32> masks;
    masks.insert("AHIBreakdown", 5);
    masks.insert("Usage", 16);
    const QString text = SummaryLines::encode(masks);
    QCOMPARE(text, QString("AHIBreakdown=5;Usage=16"));     // alphabetical, so the text is stable
    QCOMPARE(SummaryLines::decode(text), masks);
    QVERIFY(SummaryLines::decode(QString()).isEmpty());
    QCOMPARE(SummaryLines::encode({}), QString());
}

void SummaryLinesTests::testDecodeSkipsMalformed()
{
    const QHash<QString, quint32> masks = SummaryLines::decode("Usage=16;broken;=3;Leak=abc;Pressure=2;");
    QCOMPARE(masks.size(), 2);
    QCOMPARE(masks.value("Usage"), quint32(16));
    QCOMPARE(masks.value("Pressure"), quint32(2));
}
