/* SleepLib Device Loader Class Implementation
 *
 * Copyright (c) 2011-2018 Mark Watkins 
 * Copyright (c) 2019-2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "serialoximeter.h"

// Possibly need to replan this to include oximetry

QList<SerialOximeter *> GetOxiLoaders()
{
    QList<SerialOximeter *> oxiloaders;

    QList<MachineLoader *> loaders = GetLoaders(MT_OXIMETER);

    Q_FOREACH(MachineLoader * loader, loaders) {
        SerialOximeter * oxi = qobject_cast<SerialOximeter *>(loader);
        oxiloaders.push_back(oxi);
    }

    return oxiloaders;
}
void SerialOximeter::trashRecords()
{
    QMap<QDateTime, QVector<OxiRecord> *>::iterator it;
    for (it = oxisessions.begin(); it != oxisessions.end(); ++it) {
        delete it.value();
    }
    oxisessions.clear();
    oxirec = nullptr;
}
