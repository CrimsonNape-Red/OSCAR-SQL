OSCAR Portable
==============

This copy of OSCAR runs without being installed, so it can live on a USB stick
or SD card and be used on any Windows PC. It uses two folders side by side:

  OSCAR Portable\          OSCAR itself. Replace this folder to upgrade.
    OSCAR.exe              Start OSCAR by double-clicking this.
    portable.ini           Tells OSCAR to run in portable mode. Do not delete it.
  OSCAR Portable Data\     Everything of yours (created the first time OSCAR runs):
    Settings\              OSCAR's settings.
    OSCAR20_Data\          Your OSCAR database and data.

Test builds name the data folder with a suffix, such as OSCAR20_Data-test.

Nothing is written to the Windows registry. The folders still work if the drive
gets a different letter on another PC.

To keep your data somewhere else, change the DataFolder line in portable.ini
before starting OSCAR for the first time.

Getting started
---------------
1. If Windows warns that the file came from the internet, right-click the ZIP
   file, choose Properties, tick "Unblock" and click OK.
2. Right-click the ZIP file and choose "Extract All". Set the destination to
   the drive itself, for example D:\, and click Extract. This creates
   D:\OSCAR Portable.
   (Or open the ZIP and drag the "OSCAR Portable" folder to the drive.)
3. Double-click OSCAR.exe in "OSCAR Portable".

If "OSCAR Portable" ends up inside a folder named after the ZIP, OSCAR says so
the first time it starts, because your data would then go in that folder too.

The drive must be one you can write to. A folder under Program Files will not
work.

Safety
------
Always close OSCAR before removing the drive. OSCAR keeps its database open
while it runs, and removing the drive then can damage it.

Back up "OSCAR Portable Data" regularly; a USB stick is easy to lose.

If you sign in to Dropbox, Google Drive or OneDrive from OSCAR, that sign-in is
saved in the Settings folder. It is encrypted, but OSCAR has to be able to read
it back without asking you, so someone determined who has the drive could still
use it. Close
OSCAR and delete "OSCAR Portable Data\Settings" before lending or discarding
the drive; your data is kept, and OSCAR finds it again on its next start.

Upgrading
---------
1. Close OSCAR.
2. Delete the "OSCAR Portable" folder. Leave "OSCAR Portable Data" alone.
3. Put the "OSCAR Portable" folder from the new ZIP in the same place, as in
   Getting started.

If you changed DataFolder in portable.ini, make the same change in the new
copy before starting it.

Upgrading from an earlier OSCAR Portable test build, which kept Settings and
OSCAR20_Data inside "OSCAR Portable": close OSCAR, create "OSCAR Portable Data"
next to "OSCAR Portable", and move those two folders into it before upgrading.

Using your existing OSCAR data
------------------------------
To carry on with data from an installed copy of OSCAR, close both copies,
create "OSCAR Portable Data" next to "OSCAR Portable", and copy your existing
OSCAR20_Data folder (usually in Documents) into it before starting OSCAR
Portable for the first time.
