/* BMC Night Replacement Unit Tests
 *
 * Tests the rule that decides whether a night read from a BMC card may replace what is
 * already stored for the same hours: a night the card holds as events only must not
 * replace a stored night that has waveform (#419).
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef BMCNIGHTREPLACEMENTTESTS_H
#define BMCNIGHTREPLACEMENTTESTS_H

#include "AutoTest.h"

class BmcNightReplacementTests : public QObject
{
    Q_OBJECT

private slots:
    //! \brief A night that arrives with waveform replaces the stored night, as before
    void testNightWithWaveformReplacesStored();
    //! \brief An events-only night does not replace a stored night that has waveform
    void testEventsOnlyNightKeepsStoredWaveform();
    //! \brief An events-only night replaces a stored night that is events-only too
    void testEventsOnlyNightReplacesStoredEventsOnly();
    //! \brief A later events-only session that overlaps nothing stored is still imported
    void testLaterEventsOnlySessionIsImported();
    //! \brief A stored waveform session in a gap between the card's sessions is kept too
    void testStoredWaveformBetweenSessionsIsKept();
};

DECLARE_TEST(BmcNightReplacementTests)

#endif // BMCNIGHTREPLACEMENTTESTS_H
