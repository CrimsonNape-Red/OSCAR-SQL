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
using PortableMode::dataFolderSetting;
using PortableMode::resolveDataRoot;
using PortableMode::zipNamedParent;

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

// The marker is read as text: Windows backslashes must survive, comments must not count.
// A backslash is a separator only on Windows; elsewhere it is part of a file name.
void PortableModeTests::testDataFolderSetting()
{
#ifdef Q_OS_WIN
    QCOMPARE(dataFolderSetting("; marker\r\n\r\n[Portable]\r\nDataFolder=..\\OSCAR Portable Data\r\n"),
             QString("../OSCAR Portable Data"));
    QCOMPARE(dataFolderSetting("datafolder = \"E:\\My Data\" \n"), QString("E:/My Data"));
#else
    QCOMPARE(dataFolderSetting("; marker\r\n\r\n[Portable]\r\nDataFolder=../OSCAR Portable Data\r\n"),
             QString("../OSCAR Portable Data"));
    QCOMPARE(dataFolderSetting("datafolder = \"/media/stick/My Data\" \n"),
             QString("/media/stick/My Data"));
    QCOMPARE(dataFolderSetting("DataFolder=a\\b\n"), QString("a\\b"));
#endif
    QCOMPARE(dataFolderSetting(";DataFolder=..\\Commented\n# DataFolder=x\n"), QString());
    QCOMPARE(dataFolderSetting("; nothing here\n"), QString());
    QCOMPARE(dataFolderSetting(QByteArray()), QString());
    QCOMPARE(dataFolderSetting("DataFolder=\n"), QString());
}

// The default puts the data beside the program folder; ".." cannot climb above a root.
void PortableModeTests::testResolveDataRoot()
{
#ifdef Q_OS_WIN
    QCOMPARE(resolveDataRoot(QString(), "E:/OSCAR Portable"), QString("E:/OSCAR Portable Data"));
    QCOMPARE(resolveDataRoot("../OSCAR Portable Data", "E:/Apps/OSCAR Portable"),
             QString("E:/Apps/OSCAR Portable Data"));
    QCOMPARE(resolveDataRoot("Data", "E:/OSCAR Portable"), QString("E:/OSCAR Portable/Data"));
    QCOMPARE(resolveDataRoot("./x/../Data", "E:/OSCAR Portable"), QString("E:/OSCAR Portable/Data"));
    QCOMPARE(resolveDataRoot("F:\\Elsewhere", "E:/OSCAR Portable"), QString("F:/Elsewhere"));
    QCOMPARE(resolveDataRoot(QString(), "E:/"), QString("E:/OSCAR Portable Data"));
    QCOMPARE(resolveDataRoot("../../x", "E:/OSCAR Portable"), QString("E:/x"));
#else
    QCOMPARE(resolveDataRoot(QString(), "/media/stick/OSCAR Portable"),
             QString("/media/stick/OSCAR Portable Data"));
    QCOMPARE(resolveDataRoot("/home/x/Data", "/media/stick/OSCAR Portable"), QString("/home/x/Data"));
    QCOMPARE(resolveDataRoot(QString(), "/"), QString("/OSCAR Portable Data"));
#endif
}

// "Extract All" names the destination after the ZIP, e.g. OSCAR-2.1.0-Win64-Portable.
void PortableModeTests::testZipNamedParent()
{
#ifdef Q_OS_WIN
    QCOMPARE(zipNamedParent("E:/OSCAR-2.1.0-Win64-Portable/OSCAR Portable"),
             QString("E:/OSCAR-2.1.0-Win64-Portable"));
    QCOMPARE(zipNamedParent("E:/OSCAR-2.1.1-Win64-1a2b3c4d-Portable-test (1)/OSCAR Portable"),
             QString("E:/OSCAR-2.1.1-Win64-1a2b3c4d-Portable-test (1)"));
    QCOMPARE(zipNamedParent("E:/OSCAR Portable"), QString());           // parent is the drive
    QCOMPARE(zipNamedParent("E:/"), QString());
    QCOMPARE(zipNamedParent("E:/Apps/OSCAR Portable"), QString());
    QCOMPARE(zipNamedParent("E:/OSCAR-Data/OSCAR Portable"), QString()); // not a ZIP name
#else
    QCOMPARE(zipNamedParent("/media/stick/OSCAR-2.1.0-Win64-Portable/OSCAR Portable"),
             QString("/media/stick/OSCAR-2.1.0-Win64-Portable"));
    QCOMPARE(zipNamedParent("/media/stick/OSCAR Portable"), QString());
#endif
}
