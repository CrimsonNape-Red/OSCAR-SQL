/* Card Archive Unit Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "tests/AutoTest.h"

//! \brief Tests for CardArchive: the per-night segment store used for backups of
//!        ring-buffer data cards (GitLab #380).
class CardArchiveTests : public QObject
{
    Q_OBJECT
private slots:
    void testStoreAndReadBack();
    void testBinaryContentRoundTrip();
    void testGzipChecksumOfHighBytes();
    void testReloadKeepsEntries();
    void testFinalCompleteIsSkipped();
    void testLongerReplacesShorter();
    void testNeverShortened();
    void testIdenticalContentNotRewritten();
    void testTailThenFull();
    void testSameDateNoOverlap();
    void testRejectsBadInput();
    void testCorruptSegment();
    void testReadStatus();
    void testQuarantine();
    void testReplaceKeepsOldSegmentUntilManifestSaved();
    void testInstallRetiresWhenRangeUnknown();
    void testNewerVersionReadOnly();
    void testSetReadOnly();
    void testNewerRuleRecaptured();
    void testNewerRuleTailKept();
    void testCoversSpan();
    void testLoadDropsMissingFile();
    void testUnknownKeysPreserved();
    void testNoTmpLeftBehind();
    void testIndexActionTable();
    void testInstallReplace();
    void testInstallKeep();
    void testInstallRetire();
    void testInstallFromArchiveFolderItself();
    void testAdoptOrphans();
    void testAdoptAfterGarbageManifest();
    void testArchiveRecognisedWithoutManifest();
    void testConversionCompleteFlag();
    void testOrphanBadGzipIgnored();
    void testMirrorOtherFiles();
};
DECLARE_TEST(CardArchiveTests)
