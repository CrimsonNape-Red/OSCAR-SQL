---
title: Profiles
index: Profiles; Profiles!creating; Profiles!editing; Profiles!destroying; Profiles!renaming; Personal information; Height!for BMI; Time zone!profile
---

# Profiles

A **profile** holds one person's data: the devices they use, every night imported from them, and their journal. Most people need only one profile. Use separate profiles for separate people, and a separate profile for each make of CPAP machine (see [Several devices in one profile](#several-devices-in-one-profile)).

## The Profiles page

[[idx:Profiles!page]]
When OSCAR starts, it shows the **Profiles** page (unless it is set to [open the last profile automatically](preferences.md#memory-and-startup-options)). You can return to it at any time with the **Profiles** button on the left of the main window.

| Control | What it does |
|---|---|
| **Filter** | Type part of a name to narrow the list. The button next to it clears the filter. |
| **Open Profile** | Opens the selected profile. Double-clicking a profile does the same. |
| **Edit Profile** | Changes the selected profile's details (see below). |
| **New Profile** | Creates a profile. |
| **Destroy Profile** | Permanently deletes the selected profile and all its data. |
| Profile information panel | Shows a summary of the selected profile. |

## Creating a profile

[[idx:Profiles!creating]]
Click **New Profile**. A short wizard opens.

Only the **Profile Name** is required. Every other field is voluntary, but filling them in is recommended, because many of them are printed on OSCAR's reports.

1. **Conditions of use.** Read them and tick **I agree to all the conditions above**.
2. **Profile information.** The **Profile Name** is required and may be any valid file name. You can rename the profile later.

    !!! warning "Don't use your real name as the profile name"
        [[idx:Profiles!name and privacy]] [[idx:Privacy!profile name]]
        For security reasons, it is not recommended to use your real name as the profile name. Choose a nickname or initials instead. The profile name is not kept private inside OSCAR:

        - it appears in the title of OSCAR's window, so it shows in screenshots you post on a forum or send to someone;
        - it names your profile's folder on your computer;
        - it is part of the file name OSCAR suggests when you back up a profile, unless you tick *Replace personal information with blanks*.

        Health information linked to a real name is valuable to the wrong people. Your real name can go in the **First Name** and **Last Name** fields instead: those are shown only on printed reports, and on the Statistics page when you choose to show them.

3. **Locale Settings.** Choose your **Country** and **TimeZone**. OSCAR uses the time zone to place your nights correctly.

4. **Personal Information (for reports).** First and last name, date of birth, gender and height.

    - Your rough **age** and **gender** improve a few calculations; it is fine to leave them blank.
    - **Height** lets OSCAR show your Body Mass Index (BMI) when you record your weight in the [Daily journal](daily.md#notes-tab).
    - **Contact information** (address, email, phone) is printed on reports.
    - Personal information appears only on printed reports, and on the Statistics page when *View → Show Personal Data* is ticked.

5. **CPAP Treatment Information.** Date diagnosed, untreated AHI, CPAP mode, prescribed pressure and notes. These are for your own records and are shown only in this dialog.

6. **Doctors / Clinic Information.** Your doctor, practice, patient ID and contact details, for your records.

Click **Next** through the pages and finish. OSCAR opens the new profile on the Welcome page, ready for you to [import your CPAP data](importing-cpap.md).

## Editing a profile

[[idx:Profiles!editing]] [[idx:Profiles!renaming]] [[idx:Personal information]] [[idx:Height!for BMI]] [[idx:Time zone!profile]]
Choose *File → Edit Profile* (or **Edit Profile** on the Profiles page) to change any of the details above, including the profile's name.

## Destroying a profile

[[idx:Profiles!destroying]]
**Destroy Profile** removes the profile and all its imported data from OSCAR. This cannot be undone. Make a [backup](export-backup-share.md#backing-up-a-profile) first if there is any chance you will want the data again.

## Several devices in one profile

A profile can hold several devices, for example a CPAP machine and an oximeter, or an old CPAP machine and its replacement. OSCAR shows each device's data on the same timeline.

!!! warning "Keep each make of CPAP machine in its own profile"
    Additional CPAP machines in a profile should be the **same brand**. If you switch to a machine from a different manufacturer, it is **highly recommended** that you create a separate profile for it.

    Each manufacturer uses different scale values, or a different reporting format, for parameters that have the same name. Mixing makes in one profile puts those different values side by side in the Overview and Statistics, where they look comparable but are not.

If you import an SD card from a different machine than the one already in the profile, OSCAR warns you, in case you inserted someone else's card.

## Moving profiles between computers

Use *File → Profiles → Backup Profile* on the old computer and *File → Profiles → Restore Profile* on the new one. See [Backing up and restoring](export-backup-share.md#backing-up-a-profile).
