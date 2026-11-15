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
    // Daily captions: name, description, then the unit in brackets on the last line
    QCOMPARE(headerUnits("Flow Rate", "Flow Rate\nBreathing flow rate waveform\n(L/min)"), QString("L/min"));
    QCOMPARE(headerUnits("Snore", "Snore\nSnore count\n()"), QString());
}

void GraphCardTests::testSplitHeaderFits()
{
    // The legend takes its width from the right; the text keeps the rest
    const HeaderSplit split = splitHeader(QRect(100, 6, 400, 26), 150, 80);
    QCOMPARE(split.text.left(), 100);
    QCOMPARE(split.text.right(), 349);
    QCOMPARE(split.legend.left(), 350);
    QCOMPARE(split.legend.right(), 499);
    QCOMPARE(split.text.top(), 6);
    QCOMPARE(split.legend.height(), 26);
}

void GraphCardTests::testSplitHeaderNarrow()
{
    // A legend wider than the room left over is narrowed so the text keeps its minimum
    const HeaderSplit split = splitHeader(QRect(100, 6, 400, 26), 380, 80);
    QCOMPARE(split.text.width(), 80);
    QCOMPARE(split.legend.width(), 320);
    QCOMPARE(split.legend.left(), 180);

    // Less room than the text's minimum: no legend at all, never a negative width
    const HeaderSplit tiny = splitHeader(QRect(100, 6, 60, 26), 50, 80);
    QCOMPARE(tiny.text.width(), 60);
    QCOMPARE(tiny.legend.width(), 0);
}

void GraphCardTests::testSplitHeaderEmpty()
{
    const HeaderSplit split = splitHeader(QRect(), 150, 80);
    QVERIFY(split.text.isEmpty());
    QVERIFY(split.legend.isEmpty());
}
