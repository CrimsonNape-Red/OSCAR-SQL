---
title: Installing OSCAR 2
index: Installing; Download; OSCAR20_Data!choosing the folder; Migrating OSCAR 1.x data; OSCAR 1.x!migrating; OSCAR 1.x!importing a profile; Updates!checking
---

# Installing OSCAR 2

Installing OSCAR takes three steps:

1. Download the installer for your computer from the official download page, **<https://www.sleepfiles.com/OSCAR/>**.
2. Install it the usual way for your system (see below).
3. Start OSCAR, choose where it keeps its data, and either bring in your OSCAR 1.x data or create a new profile.

## Download and install

[[idx:Download]]
The download page has versions for Windows, macOS and Linux. The source code is linked near the bottom of the page, but you only need it if you want to build OSCAR yourself.

### Windows

[[idx:Windows!installing]]
1. Double-click the downloaded `.exe` installer.
2. If Windows shows *Windows protected your PC*, click **More info**, then **Run anyway**.
3. Follow the installer's steps. You can accept the suggested folder (`OSCAR20` in Program Files) and choose whether to add a desktop icon.
4. Click **Finish**. OSCAR 2 is in the Start menu, and on the desktop if you chose the icon.

To install a new version, run its installer the same way. Your data is kept.

#### Portable version (no installation)

[[idx:Windows!portable version]] [[idx:Portable version]]
The portable version runs from a USB stick or SD card on any Windows PC, without installing anything. It comes as a ZIP file.

1. If Windows warns that the file came from the internet, right-click the ZIP file, choose **Properties**, tick **Unblock** and click **OK**.
2. Right-click the ZIP file, choose **Extract All**, set the destination to the drive itself (for example `D:\`) and click **Extract**. This creates `D:\OSCAR Portable`.
3. Double-click `OSCAR.exe` in the *OSCAR Portable* folder.

The first time it runs, OSCAR creates *OSCAR Portable Data* next to *OSCAR Portable*, holding its settings and your data. The drive must be one you can write to; a folder under Program Files will not work. Always close OSCAR before removing the drive, and back up *OSCAR Portable Data* regularly. To upgrade, close OSCAR, delete the *OSCAR Portable* folder, and put the new one in its place; leave *OSCAR Portable Data* alone.

### macOS

[[idx:macOS!installing]] [[idx:Unidentified developer (macOS)]]
1. Double-click the downloaded `.dmg` file. A window opens with the OSCAR icon and the Applications folder.
2. Drag **OSCAR** onto **Applications**.
3. Eject the disk image.

The first time you open a new version, macOS may refuse with a message that OSCAR is from an *unidentified developer*. To open it:

1. In the Applications folder, hold <kbd>Control</kbd> and click **OSCAR**, then choose **Open**.
2. In the window that appears, click **Open**.

On newer versions of macOS the first window may have no *Open* button. Close it, then go to *System Settings → Privacy & Security*, scroll down to the message about OSCAR and click **Open Anyway**. You need to do this only once for each new version.

### Linux

[[idx:Linux!installing]] [[idx:.deb package]]
OSCAR comes as a `.deb` package for Debian, Ubuntu, Mint and related distributions.
<!-- AppImage: held back until the AppImage is released. Restore by replacing the sentence above with:
[[idx:AppImage]]
OSCAR comes as a `.deb` package for Debian, Ubuntu, Mint and related distributions, and as an AppImage that runs on most other distributions.
-->

**.deb package**

1. Open the downloaded file with your software installer, or in a terminal run:

        sudo apt install ./OSCAR_<version>.deb

    The `./` is needed so `apt` installs the file rather than looking in the distribution's repositories. `gdebi` also works.

2. OSCAR appears in your applications menu, and an icon is placed on your desktop. Some desktops ask you to double-click the icon and choose **Trust** or **Allow Launching** the first time.

To upgrade, install the new `.deb` the same way.

<!-- AppImage: held back until the AppImage is released.
**AppImage**

1. Make the file executable: right-click it, choose **Properties → Permissions** and tick *Allow executing file as program*, or run `chmod +x OSCAR*.AppImage`.
2. Double-click it to run OSCAR.

The AppImage does not need installing. To upgrade, replace the file with the new one.
-->

## Removing OSCAR

[[idx:Uninstalling]]
- **Windows:** *Settings → Apps*, choose **OSCAR 2** and **Uninstall**.
- **macOS:** drag OSCAR from Applications to the Trash.
- **Linux:** see below.

Removing OSCAR leaves your data folder (`OSCAR20_Data`) in place, so a later installation finds your data again. Delete that folder yourself if you want your data gone too.

### Removing OSCAR on Linux

[[idx:Uninstalling!Linux]] [[idx:oscar20 package]]
1. Find the OSCAR packages installed on your computer:

        dpkg -l | grep oscar

    The released version of OSCAR 2 is listed as **oscar20**. Test and other unreleased versions have a suffix, such as `oscar20-test`.

2. Remove the package by the name shown, for example:

        sudo apt purge oscar20

    Repeat for any other OSCAR package listed that you want to remove.
<!-- AppImage: held back until the AppImage is released.
To remove the AppImage, delete the AppImage file.
-->

## Choosing the data folder

[[idx:OSCAR20_Data!choosing the folder]]
When OSCAR 2 starts for the first time, it asks where to keep your data. Click **OK** to accept the recommended folder, `OSCAR20_Data` in your Documents folder, or **No** to choose another place.

!!! warning
    - Do **not** choose your CPAP's SD card. Choose a folder on your computer's own drive.
    - Do **not** choose the `OSCAR_Data` folder OSCAR 1.x uses. OSCAR 2 needs a separate folder.

See [Your data and the database](database.md) for what OSCAR keeps in this folder.

## Coming from OSCAR 1.x

[[idx:Migrating OSCAR 1.x data]]
There are two ways to bring OSCAR 1.x data into OSCAR 2. Both copy the data and leave your OSCAR 1.x folder untouched, so you can keep using OSCAR 1.x.

### Migrating everything (first start only)

[[idx:OSCAR 1.x!migrating]]
On its first start, OSCAR 2 offers to migrate all your OSCAR 1.x profiles, data and settings. This is the easiest way and is recommended for most people.

1. Click **OK** when OSCAR asks whether to copy your OSCAR 1.x data.
2. Select your OSCAR 1.x data folder, usually `OSCAR_Data` in your Documents folder.
3. Wait while a progress bar shows the copy. With several years of data this can take a while.

If you click **No**, OSCAR opens the profile page so you can [create a profile](profiles.md#creating-a-profile) instead. You can migrate everything only at this point; later you can import profiles one at a time.

!!! tip "Can't find your OSCAR 1.x data?"
    In OSCAR 1.x, choose *Help → Troubleshooting → System Information*. The window shows where the data folder is.

### Importing one profile later

[[idx:OSCAR 1.x!importing a profile]]
Choose *File → Profiles → Import from OSCAR...* at any time.

1. Select the folder of the OSCAR 1.x profile you want, inside `OSCAR_Data/Profiles`. OSCAR remembers the last folder you used.
2. If the profile is larger than 2 GB, OSCAR asks you to confirm, because large profiles take longer.
3. Check the name for the new profile. It defaults to the old name; if that name is taken, OSCAR adds a suffix. You can change it.
4. Click **Import**. A message tells you when it has finished.

### Going back to OSCAR 1.x

If you ever need your OSCAR 2 data in OSCAR 1.x, use *Data → Rebuild CPAP Data* in OSCAR 1.x and point it at the **Backup** folder inside your OSCAR 2 profile's device folder.

## Updates

[[idx:Updates!checking]]
OSCAR can tell you when a new version is out. Use *Help → Check For Updates* to check now, or set how often it checks automatically under *Preferences → General → Check For Updates* (see [Preferences](preferences.md#general-tab)). Test versions always check at least once a week.

Next: [Profiles](profiles.md).
