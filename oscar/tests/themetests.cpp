/* Theme module Unit Tests
 *
 * Checks that the light theme carries the mockup's colour values and that every colour
 * role is defined, so no widget ever paints with an invalid colour.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "themetests.h"
#include "uikit/theme.h"

using Theme::Role;

void ThemeTests::testLightValues()
{
    QCOMPARE(Theme::color(Role::Page).name(), QString("#f2f5f8"));
    QCOMPARE(Theme::color(Role::Accent).name(), QString("#007acc"));
    QCOMPARE(Theme::color(Role::Good).name(), QString("#17844a"));
    QCOMPARE(Theme::color(Role::Bad).name(), QString("#c4372c"));
    QCOMPARE(Theme::color(Role::GroupUsageLine).name(), QString("#e9d8b3"));
}

void ThemeTests::testEveryRoleDefined()
{
    for (int r = 0; r < int(Role::Count); ++r) {
        QVERIFY2(Theme::color(Role(r)).isValid(), qPrintable(QString("role %1").arg(r)));
    }
}

void ThemeTests::testCss()
{
    QCOMPARE(Theme::css(Role::Accent), QString("#007acc"));
    QCOMPARE(Theme::css(Role::Shadow), QString("rgba(19, 33, 48, 18)"));
}
