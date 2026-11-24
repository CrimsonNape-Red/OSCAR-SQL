/* Machine Properties Unit Tests
 *
 * Tests that a device's properties (firmware version, model codes and similar values a
 * loader reads from the card) reach the database when the row is created and whenever
 * they change later, without losing stored keys the loader does not supply (GitLab #420).
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef MACHINEPROPERTIESTESTS_H
#define MACHINEPROPERTIESTESTS_H

#include <QJsonObject>

#include "AutoTest.h"

class Machine;
class QCoreApplication;
class QTemporaryDir;

//! \brief Tests for storing Machine::info.properties in the machines table, against a
//! temporary database.
class MachinePropertiesTests : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void testNewRowStoresLoaderProperties();
    void testSetInfoStoresChangedProperties();
    void testStoreMachinesStoresDirectWrites();
    void testUnchangedNumericValueKeepsItsType();
    void cleanupTestCase();

private:
    //! \brief The device's stored properties, parsed from its machines row.
    QJsonObject storedProperties() const;

    QCoreApplication *m_app = nullptr;
    QTemporaryDir *m_tempDir = nullptr;
    QString m_previousAppData;
    Machine *m_machine = nullptr;   //!< A device with a row in the temporary database
};

DECLARE_TEST(MachinePropertiesTests)

#endif // MACHINEPROPERTIESTESTS_H
