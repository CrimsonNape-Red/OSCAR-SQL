/* Portable Mode Unit Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "portablemodetests.h"
#include "SleepLib/portablemode.h"

using PortableMode::encodePath;
using PortableMode::decodePath;

#ifdef Q_OS_WIN
static const QString Root = QStringLiteral("E:/OSCAR Portable");
#else
static const QString Root = QStringLiteral("/media/stick/OSCAR Portable");
#endif

// The default data folder sits beside the executable and must not carry the drive.
void PortableModeTests::testInsideRootIsRelative()
{
    QCOMPARE(encodePath(Root + "/OSCAR20_Data", Root), QString("OSCAR20_Data"));
    QCOMPARE(encodePath(Root + "/OSCAR20_Data/oscar.db", Root), QString("OSCAR20_Data/oscar.db"));
}

void PortableModeTests::testRootItself()
{
    QCOMPARE(encodePath(Root, Root), QString("."));
    QCOMPARE(decodePath(".", Root), Root);
}

// Windows: anything on the same drive moves with it; another drive stays absolute.
// Elsewhere: only paths inside the portable folder become relative.
void PortableModeTests::testOutsideRoot()
{
#ifdef Q_OS_WIN
    QCOMPARE(encodePath("E:/Data/OSCAR20_Data", Root), QString("../Data/OSCAR20_Data"));
    QCOMPARE(encodePath("C:/Users/x/Documents/OSCAR20_Data", Root),
             QString("C:/Users/x/Documents/OSCAR20_Data"));
#else
    QCOMPARE(encodePath("/media/stick/Data", Root), QString("/media/stick/Data"));
    QCOMPARE(encodePath("/home/x/OSCAR20_Data", Root), QString("/home/x/OSCAR20_Data"));
#endif
}

// A stored relative path resolves against whatever the root is on this computer.
void PortableModeTests::testDecode()
{
#ifdef Q_OS_WIN
    QCOMPARE(decodePath("OSCAR20_Data", "F:/OSCAR Portable"), QString("F:/OSCAR Portable/OSCAR20_Data"));
    QCOMPARE(decodePath("../Data", "F:/OSCAR Portable"), QString("F:/Data"));
    QCOMPARE(decodePath("C:/Elsewhere", "F:/OSCAR Portable"), QString("C:/Elsewhere"));
#else
    QCOMPARE(decodePath("OSCAR20_Data", "/media/other/OSCAR Portable"),
             QString("/media/other/OSCAR Portable/OSCAR20_Data"));
    QCOMPARE(decodePath("/home/x", "/media/other/OSCAR Portable"), QString("/home/x"));
#endif
}

void PortableModeTests::testRoundTrip()
{
    const QString data = Root + "/OSCAR20_Data";
    QCOMPARE(decodePath(encodePath(data, Root), Root), data);
}

void PortableModeTests::testEmptyAndRelativeInputs()
{
    QCOMPARE(encodePath(QString(), Root), QString());
    QCOMPARE(decodePath(QString(), Root), QString());
    QCOMPARE(encodePath("already/relative", Root), QString("already/relative"));
    QCOMPARE(encodePath(Root + "/OSCAR20_Data", QString()), Root + "/OSCAR20_Data");
}
