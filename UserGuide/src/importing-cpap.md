---
title: Importing CPAP Data
index: Importing!CPAP data; SD card!importing; SD card!locking; SD card!backups; Shift+F2; Specify (import folder); Untested device warning; Rebuild CPAP Data
---

# Importing CPAP Data

OSCAR reads your therapy data from the SD card (or, on some machines, the USB stick) that your CPAP machine writes to. Each import adds the nights that are new since the last one.

!!! warning "Keep the card in your machine"
    Many machines, ResMed in particular, write their detailed data only to the card. Nights recorded without the card in the machine have no detailed data, and OSCAR cannot show it. Put the card back in your machine after every import.

## Before you insert the card {.noindex}

[[idx:SD card!locking]]
If your computer has no SD slot, any USB SD card reader works.

!!! warning "ResMed S9 users: lock the card"
    Windows and macOS sometimes write hidden files to any card you insert. ResMed **S9** machines see those extra files as damage and reformat the card, erasing your data.

    - **Lock the card** before putting it in your computer: slide the small switch on its side towards the *Lock* position. Unlock it again before you put it back in the S9.
    - Some cheap card readers ignore the lock switch. If yours does, be extra careful with S9 cards.

    This applies only to the S9 series. Other machines do not need the card locked.

## Importing

[[idx:Importing!CPAP data]] [[idx:Shift+F2]] [[idx:SD card!importing]]
1. Open your profile.
2. Insert the card into your computer.
3. Start the import in any of these ways:
    - click **Import** on the left of the main window, or **CPAP Importer** on the Welcome page,
    - choose *File → Import CPAP Card Data*,
    - press <kbd>Shift</kbd>+<kbd>F2</kbd>.
4. OSCAR looks for a card it recognises. When it finds one, it asks whether to import from it. Click **Yes**.
5. A progress bar shows the import. When it finishes, OSCAR shows your data.

The first import of a card can take several minutes, because OSCAR also makes a backup copy of the card (see below). Later imports read only what is new and are much faster.

### Importing from a folder

[[idx:Specify (import folder)]]
If you copied the card to your computer, click **Specify** instead of Yes and choose the folder that holds the copy. Choose the top folder of the copy — the one that contains the folders your machine created — not a folder inside it. OSCAR recognises the machine from that folder structure.

### Warnings you may see {.noindex}

[[idx:Untested device warning]]
- **Untested device.** OSCAR imports data from models its developers have not seen before, but tells you so. The developers would welcome a copy of the card to make sure it is handled correctly; see [Reporting a problem](troubleshooting.md#reporting-a-problem).
- **Unexpected data.** The card contains something OSCAR has not seen before. The import continues; the developers would like to hear about it.
- **Different machine.** The card comes from a different machine than the one already in this profile.

You can turn each of these warnings off on the [Import tab of Preferences](preferences.md#import-tab).

### After the import {.noindex}

- The **Switch Tabs After Import** setting (*Preferences → Appearance*) decides which view OSCAR shows afterwards.
- If you were on the Daily view, it moves to the newest night.
- When you close OSCAR, it can remind you to take the card out and put it back in your machine (*Preferences → General*).
- Eject the card properly before you remove it, the same way you would a USB stick.

## SD card backups

[[idx:SD card!backups]]
While importing, OSCAR keeps a copy of everything on the card in your profile's **Backup** folder. This matters because many machines delete old detailed data from the card after a while (ResMed S9 machines keep only 7 days of high-resolution data and 30 days of graph data). With the backup, OSCAR can rebuild your history even after the card has been erased or lost.

Leave **Create SD Card Backups during Import** turned on (*Preferences → Import*) unless you are very short of disk space.

## Rebuilding CPAP data

[[idx:Rebuild CPAP Data]]
*Data → Rebuild CPAP Data* re-imports a device's data from OSCAR's backup copy. Use it after the developers fix a problem with your machine's data in a new version of OSCAR, or after changing settings that affect import, such as [custom event flagging](preferences.md#custom-cpap-user-event-flagging) or [Steady Breathing](preferences.md#steady-breathing).

## Machines that use other import methods {.noindex}

A few devices are not read from a card in the usual way:

- **Philips Respironics M-Series**: *Data → Import RemStar MSeries Data* (see [Supported CPAP devices](supported-cpap.md#philips-respironics)).
- **Oximeters**: the [Oximetry Wizard](oximetry.md).
- **Sleep trackers and position sensors**: the import items on the *Data* menu (see [Other devices](other-devices.md)).
