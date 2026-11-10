/* PRS1 Unit Tests
 *
 * Copyright (c) 2019-2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef PRS1TESTS_H
#define PRS1TESTS_H

#include "AutoTest.h"

class PRS1Tests : public QObject
{
    Q_OBJECT
 
private slots:
    void initTestCase();
    void testMachineSupport();
    void testChunksToYaml();
    void testSessionsToYaml();
    void cleanupTestCase();

private:
    // Members rather than free functions, so that the friend declarations for PRS1Tests
    // in the loader classes cover them.
    static void parseAndEmitSessionYaml(const QString & path);
    static void parseAndEmitChunkYaml(const QString & path);
    static void iterateTestCards(const QString & root, void (*action)(const QString &));
};

DECLARE_TEST(PRS1Tests)

#endif // PRS1TESTS_H

