/* Graph card Unit Tests
 *
 * Covers the geometry of the redesigned graph frame (the header band and the menu button at
 * its right end), the test that decides whether a click is on the header, and the text the
 * header shows beside the title.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "graphcardtests.h"
#include "Graphs/graphcard.h"


using namespace GraphCard;

void GraphCardTests::testHeaderInsideCard()
{
    QCOMPARE(headerRect(QRect(0, 0, 600, 200)), QRect(10, 6, 580, 26));
    QCOMPARE(headerRect(QRect(5, 300, 600, 200)), QRect(15, 306, 580, 26));
}

void GraphCardTests::testMenuButtonAtTopRight()
{
    const QRect card(0, 0, 600, 200);
    const QRect button = menuButtonRect(card);
    QCOMPARE(button.size(), QSize(22, 22));
    QCOMPARE(button.left() + button.width(), 590);      // flush with the header's right end
    QVERIFY(headerRect(card).contains(button));
}

void GraphCardTests::testInHeader()
{
    const QRect card(0, 0, 600, 200);
    QVERIFY(inHeader(card, QPoint(300, 15)));
    QVERIFY(inHeader(card, QPoint(2, 2)));          // the card's top edge counts, not only the padded band
    QVERIFY(!inHeader(card, QPoint(300, 60)));
    QVERIFY(!inHeader(card, QPoint(300, -1)));
    QVERIFY(!inHeader(card, QPoint(700, 15)));
}

void GraphCardTests::testElideSummary()
{
    // The unit tests have no fonts, so measure every character as 7 pixels wide
    const auto width = [](const QString &s) { return int(s.size()) * 7; };
    const QString text = QStringLiteral("2 low usage, 0 no usage, out of 30 days (93% compliant.)");
    QCOMPARE(elideSummary(text, width, width(text) + 10), text);

    const int narrow = width(text) / 2;
    const QString elided = elideSummary(text, width, narrow);
    QVERIFY(elided.endsWith(QChar(0x2026)));
    QVERIFY(width(elided) <= narrow);
    QVERIFY(width(elided) > narrow - 14);       // and no shorter than it has to be

    QCOMPARE(elideSummary(text, width, 0), QString());
    QCOMPARE(elideSummary(text, width, 10), QString());    // too narrow for anything but the ellipsis
}

void GraphCardTests::testHeaderUnits()
{
    // Overview passes multi-line axis captions; the header shows what the title doesn't already say
    QCOMPARE(headerUnits("Usage", "Usage\n(hours)"), QString("hours"));
    QCOMPARE(headerUnits("Pressure", "Pressure\n(cmH2O)"), QString("cmH2O"));
    QCOMPARE(headerUnits("AHI", "Apnea\nHypopnea\nIndex"), QString("Apnea Hypopnea Index"));
    QCOMPARE(headerUnits("Session Times", "Session Times"), QString());
    QCOMPARE(headerUnits("Weight", ""), QString());
}
