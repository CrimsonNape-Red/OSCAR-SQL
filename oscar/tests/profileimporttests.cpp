/* Profile Import Unit Tests
 *
 * Tests ProfileImporter::matchMachineFolder(): which device of the new profile a device
 * folder of a file-based (OSCAR 1.7.1) profile belongs to.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "profileimporttests.h"

#include <QTemporaryDir>

#include "SleepLib/machine.h"
#include "SleepLib/profiles.h"
#include "profileimporter.h"

namespace {

/*! \brief Give \a mach the identity the importer matches on
    \param mach   device to set up
    \param type   device type
    \param loader loader name, the first part of the folder name
    \param serial serial number; empty for a device that has none */
void identify(Machine & mach, MachineType type, const QString & loader, const QString & serial)
{
    mach.setInfo(MachineInfo(type, 0, loader, loader, QString(), QString(), serial, QString(),
                             QDateTime(), 1));
}

/*! \brief A profile with one CPAP device, and a position sensor and an oximeter that have no
    serial number, in the order the importer has them (by brand) */
struct ThreeDevices
{
    ThreeDevices()
        : profile(dir.path(), false), cpap(&profile, 0x1001),
          position(&profile, 0x3fa9), oximeter(&profile, 0x30e8)
    {
        identify(cpap, MT_CPAP, "ResMed", "11111111111");
        identify(position, MT_POSITION, "Somnopose", QString());
        identify(oximeter, MT_OXIMETER, "Viatom", QString());
        machines = { &cpap, &position, &oximeter };
    }

    QTemporaryDir dir;
    Profile profile;
    CPAP cpap;
    PositionSensor position;
    Oximeter oximeter;
    QList<Machine *> machines;
};

} // namespace

// GitLab #440: a device without a serial number has a folder named by its id in hex. Each
// such folder belongs to its own device; the oximeter's used to go to the position sensor,
// the first device in the list with an empty serial number.
void ProfileImportTests::testDevicesWithoutSerialKeepTheirOwnFolders()
{
    ThreeDevices d;
    QCOMPARE(ProfileImporter::matchMachineFolder(d.machines, "Viatom_000030e8"), &d.oximeter);
    QCOMPARE(ProfileImporter::matchMachineFolder(d.machines, "Somnopose_00003fa9"), &d.position);

    // Whichever of them comes first in the list.
    const QList<Machine *> reversed = { &d.oximeter, &d.position, &d.cpap };
    QCOMPARE(ProfileImporter::matchMachineFolder(reversed, "Viatom_000030e8"), &d.oximeter);
    QCOMPARE(ProfileImporter::matchMachineFolder(reversed, "Somnopose_00003fa9"), &d.position);

    // Two oximeters without serial numbers.
    Oximeter second(&d.profile, 0x1a2b);
    identify(second, MT_OXIMETER, "CMS50", QString());
    const QList<Machine *> oximeters = { &second, &d.oximeter, &d.cpap };
    QCOMPARE(ProfileImporter::matchMachineFolder(oximeters, "Viatom_000030e8"), &d.oximeter);
    QCOMPARE(ProfileImporter::matchMachineFolder(oximeters, "CMS50_00001a2b"), &second);
}

// A device with a serial number has a folder named by it, underscores and all.
void ProfileImportTests::testFolderOfDeviceWithSerial()
{
    ThreeDevices d;
    QCOMPARE(ProfileImporter::matchMachineFolder(d.machines, "ResMed_11111111111"), &d.cpap);

    CPAP underscored(&d.profile, 0x2002);
    identify(underscored, MT_CPAP, "PRS1", "AB_12");
    const QList<Machine *> machines = { &d.cpap, &underscored, &d.position, &d.oximeter };
    QCOMPARE(ProfileImporter::matchMachineFolder(machines, "PRS1_AB_12"), &underscored);
}

// Folders that do not have the usual name are still matched by serial number or by id,
// and never through an empty serial number.
void ProfileImportTests::testFoldersNamedAnotherWay()
{
    ThreeDevices d;
    CPAP other(&d.profile, 0x2002);
    identify(other, MT_CPAP, "PRS1", "22222222222");
    const QList<Machine *> machines = { &d.cpap, &other, &d.position, &d.oximeter };

    // The id alone, and the id under another prefix.
    QCOMPARE(ProfileImporter::matchMachineFolder(machines, "000030e8"), &d.oximeter);
    QCOMPARE(ProfileImporter::matchMachineFolder(machines, "Oximeter_000030E8"), &d.oximeter);
    // The serial number under another prefix, and with something after it.
    QCOMPARE(ProfileImporter::matchMachineFolder(machines, "CPAP_22222222222"), &other);
    QCOMPARE(ProfileImporter::matchMachineFolder(machines, "PRS1_22222222222 (old)"), &other);
}

// A folder that belongs to none of the devices is not handed to one without a serial number.
void ProfileImportTests::testNoMatch()
{
    ThreeDevices d;
    CPAP other(&d.profile, 0x2002);
    identify(other, MT_CPAP, "PRS1", "22222222222");
    const QList<Machine *> machines = { &d.cpap, &other, &d.position, &d.oximeter };

    QCOMPARE(ProfileImporter::matchMachineFolder(machines, "Zeo_0000beef"), nullptr);
    QCOMPARE(ProfileImporter::matchMachineFolder(machines, "Unknown_"), nullptr);
}
