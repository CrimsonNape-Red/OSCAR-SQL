/* BMC Night Archive Unit Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef BMCARCHIVETESTS_H
#define BMCARCHIVETESTS_H

#include "AutoTest.h"

//! \brief Tests that the BMC parsers can list a card's nights, hand over a night's raw
//!        packets, and read a night back from a CardArchive in place of the card's ring
//!        files (GitLab #380). The cards are synthetic and built in a temporary folder.
class BmcArchiveTests : public QObject
{
    Q_OBJECT

private slots:
    void testLegacyRecordsListed();
    void testLegacyBytesMatchImport();
    void testLegacyShortCardReadOnce();
    void testLegacyDescribePackets();
    void testLegacyArchiveRoundTrip();
    void testLegacyMissingSegment();
    void testLegacyCorruptSegment();
    void testLegacyRecordRule();
    void testLegacyFileClasses();
    void testG3xRecordsListed();
    void testG3xBytesMatchImport();
    void testG3xWrappedNight();
    void testG3xTailOnly();
    void testG3xArchiveRoundTrip();
    void testG3xMissingAndCorruptSegment();
    void testG3xRecordRule();
    void testG3xNoRingAccessInArchiveMode();
    void testG3xFileClasses();
    void testCaptureFresh();
    void testCaptureSkipsSystemFolders();
    void testCaptureAgainReadsNothing();
    void testCaptureNewNight();
    void testUnionEitherOrder();
    void testMigrationSource();
    void testArchiveToArchive();
    void testAbortMidway();
    void testUnwritableArchive();
    void testWriteFailure();
    void testRuleBumpRecaptures();
    void testReadCheck();
    void testOpenImportSourceCard();
    void testOpenImportSourceArchiveWithGenerations();
    void testBackUpMigratesThenCaptures();
    void testBackUpKeepsMirrorOnFailure();
    void testBackUpKeepsMirrorOnAbort();
    void testBackUpFromForeignArchive();
    void testFinishArchiveImportDropsUnreadable();
    void testReadWarningText();
    void testNightsNotInArchive();
    void testNightsNotInArchiveEmpty();
    void testRebuildLossText();
    void testRebuildLossTextFromArchive();
    void testCaptureCountsUnreadableRingFileAsFailure();
    void testCaptureCountsShortReadAsFailure();
    void testConversionKeepsRingFilesWhenUnreadable();
    void testG3xTruncatedRingNotComplete();
    void testFutureDatedRecordDoesNotCloseNewestNight();
    void testLegacyGarbageCrumbDoesNotHideTail();
    void testInterruptedRingDeletionStillArchive();
    void testOpenImportSourceLostManifest();
    void testOpenImportSourceNewerManifest();
    void testFinishArchiveImportKeepsUnavailableSegment();
    void testConversionVerifiesSegmentsBeforeDeleting();
    void testUnparseableOldCopyIsSetAside();
    void testBackupWarningText();
};

DECLARE_TEST(BmcArchiveTests)

#endif // BMCARCHIVETESTS_H
