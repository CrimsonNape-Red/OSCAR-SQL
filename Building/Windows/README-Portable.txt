OSCAR Portable
==============

This copy of OSCAR runs without being installed, and keeps everything in this
folder, so it can live on a USB stick or SD card and be used on any Windows PC.

  OSCAR.exe       Start OSCAR by double-clicking this.
  portable.ini    Tells OSCAR to run in portable mode. Do not delete it.
  Settings\       OSCAR's settings (created the first time OSCAR runs).
  OSCAR20_Data\   Your OSCAR database and data (created the first time OSCAR runs).

Test builds name the data folder with a suffix, such as OSCAR20_Data-test.

Nothing is written to the Windows registry. The folder still works if the drive
gets a different letter on another PC.

Getting started
---------------
1. Extract the whole "OSCAR Portable" folder to the drive you want to use.
   If Windows warns that the file came from the internet, right-click the ZIP
   file, choose Properties, tick "Unblock", then extract it again.
2. Double-click OSCAR.exe.

The folder must be somewhere you can write to. A folder under Program Files
will not work.

Safety
------
Always close OSCAR before removing the drive. OSCAR keeps its database open
while it runs, and removing the drive then can damage it.

Back up OSCAR20_Data regularly; a USB stick is easy to lose.

If you sign in to Dropbox, Google Drive or OneDrive from OSCAR, that sign-in is
saved in the Settings folder, and anyone who has the drive could use it. Close
OSCAR and delete the Settings folder before lending or discarding the drive;
your data is kept, and OSCAR finds it again on its next start.

Upgrading
---------
1. Close OSCAR.
2. Delete everything in this folder except Settings, OSCAR20_Data and
   portable.ini.
3. Extract the new ZIP and copy the contents of its "OSCAR Portable" folder
   into this folder.

Extracting the new ZIP over the top of this folder also works, and never
touches your settings or data, but can leave unused files from the old version.

Using your existing OSCAR data
------------------------------
To carry on with data from an installed copy of OSCAR, close both copies and
copy your existing OSCAR20_Data folder (usually in Documents) into this folder
before starting OSCAR Portable for the first time.
