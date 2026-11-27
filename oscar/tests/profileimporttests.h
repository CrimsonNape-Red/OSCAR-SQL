/* Profile Import Unit Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef PROFILEIMPORTTESTS_H
#define PROFILEIMPORTTESTS_H

#include "tests/AutoTest.h"

/*! \class ProfileImportTests
    \brief How the importer of file-based (OSCAR 1.7.1) profiles matches a device folder to
    a device (ProfileImporter::matchMachineFolder()) */
class ProfileImportTests : public QObject
{
    Q_OBJECT
private slots:
    void testDevicesWithoutSerialKeepTheirOwnFolders();
    void testFolderOfDeviceWithSerial();
    void testFoldersNamedAnotherWay();
    void testNoMatch();
};
DECLARE_TEST(ProfileImportTests)

#endif // PROFILEIMPORTTESTS_H
