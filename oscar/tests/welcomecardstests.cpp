/* Welcome page card list Unit Tests
 *
 * Covers the catalog of cards the redesigned Welcome page can show (every card listed once,
 * with a unique storage key) and the text form in which the profile remembers the cards
 * the user has hidden.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "welcomecardstests.h"

#include <QSet>

#include "welcomemetrics.h"

void WelcomeCardsTests::testCatalogCoversEveryCard()
{
    const QVector<CardInfo> catalog = WelcomeMetrics::catalog();
    const QVector<MetricId> expected = { MetricId::TimeUsed, MetricId::Ahi, MetricId::Events,
                                         MetricId::Pressure, MetricId::Leak, MetricId::LargeLeak,
                                         MetricId::FlowLimit, MetricId::Sessions, MetricId::Oximetry };
    QCOMPARE(catalog.size(), expected.size());
    for (int i = 0; i < expected.size(); ++i) {
        QCOMPARE(catalog[i].id, expected[i]);   // display order
    }
}

void WelcomeCardsTests::testCatalogKeysUnique()
{
    QSet<QString> keys;
    for (const CardInfo &c : WelcomeMetrics::catalog()) {
        QVERIFY(!c.key.isEmpty());
        QVERIFY(!c.key.contains(','));          // the separator of the stored list
        keys.insert(c.key);
        QCOMPARE(WelcomeMetrics::cardKey(c.id), c.key);
    }
    QCOMPARE(keys.size(), WelcomeMetrics::catalog().size());
}

void WelcomeCardsTests::testCardKeysRoundTrip()
{
    const QSet<QString> hidden = { "leak", "ahi" };
    const QString text = WelcomeMetrics::joinCardKeys(hidden);
    QCOMPARE(text, QString("ahi,leak"));         // sorted, so the stored value is stable
    QCOMPARE(WelcomeMetrics::parseCardKeys(text), hidden);
    QCOMPARE(WelcomeMetrics::joinCardKeys({}), QString());
    QVERIFY(WelcomeMetrics::parseCardKeys(QString()).isEmpty());
}

void WelcomeCardsTests::testCardKeysTolerateJunk()
{
    QCOMPARE(WelcomeMetrics::parseCardKeys(" ahi, ,leak ,"), QSet<QString>({ "ahi", "leak" }));
}
