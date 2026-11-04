---
title: Troubleshooting
index: Troubleshooting; Reporting a problem; Logs; Debug pane; System Information; Database!integrity check; Database!compress; Zip!of SD card; Zip!of logs; Zip!of OSCAR data; Performance information; Graphs!drawing problems
---

# Troubleshooting

## Common problems

[[idx:Troubleshooting]]

### OSCAR does not find my SD card {.noindex}

- Make sure the card is inserted and your computer shows it as a drive.
- If you copied the card to your computer, click **Specify** and choose the copy's top folder (see [Importing from a folder](importing-cpap.md#importing-from-a-folder)).
- Check that your machine is [supported](supported-cpap.md). Some machines keep no usable data on the card.
- On Chrome OS, the card must be shared with Linux before OSCAR can see it.

### A night is missing or has no detailed graphs {.noindex}

- Was the card in the machine that night? Without it, many machines record only summary data, or nothing.
- Check *Ignore Short Sessions* and the [session-splitting settings](preferences.md#session-splitting-settings): a short or early-morning session may be hidden or counted on another day.
- On a machine with a wrong clock, the night may be on the wrong day. See [Time corrections](data-tools.md#time-corrections).

### Oximetry does not line up with CPAP data {.noindex}

- Use **Match to CPAP** in the [Oximetry Wizard](oximetry.md#step-4-match-to-cpap) to sync the recording to a CPAP session.
- Let OSCAR set the oximeter's clock when it connects (Viatom/Wellue and newer Contec models).
- If the CPAP machine's clock is the one that is wrong, use [Time corrections](data-tools.md#time-corrections).

### The oximeter will not connect {.noindex}

- **Bluetooth:** close the maker's phone app (ViHealth, EMAY's app), because only one program can connect at a time. Wake the oximeter; Viatom/Wellue oximeters switch off after about two minutes without a connection. Check Bluetooth is on, and on macOS that OSCAR is allowed to use it (*System Settings → Privacy & Security → Bluetooth*).
- **USB cable:** close O2 Insight Pro or Contec's software. On Linux, use the **Allow OSCAR to use the cable...** button if the wizard shows it.

### Graphs draw incorrectly {.noindex}

[[idx:Graphs!drawing problems]]
Change the [Graphics Engine](preferences.md#graphics-engine-requires-restart) to **Software Engine** and restart OSCAR.

### OSCAR is slow {.noindex}

- Turn off *Compress Session Data* if disk space allows (Preferences → Import).
- Showing *Everything* on the Overview with years of data takes time; choose a shorter range.

## The Troubleshooting menu

*Help → Troubleshooting* collects tools for diagnosing problems and gathering what the developers need.

Show Debug Pane
:   [[idx:Debug pane]] Shows OSCAR's running log inside the window.

Show Performance Information
:   [[idx:Performance information]] Shows timing information, for diagnosing slowness.

Check database integrity
:   [[idx:Database!integrity check]] Checks the database for damage.

Compress database (rarely needed)
:   [[idx:Database!compress]] Reclaims unused space in the database.

Create zip of CPAP data card
:   [[idx:Zip!of SD card]] Makes a zip of your SD card's contents, for sending to the developers when your machine's data is not read correctly.

Create zip of OSCAR diagnostic logs
:   [[idx:Zip!of logs]] [[idx:Logs]] Makes a zip of OSCAR's log files.

Create zip of OSCAR database
:   [[idx:Zip!of OSCAR data]] Makes a zip of your OSCAR data.

Report an Issue
:   Opens the place to report a problem to the OSCAR team.

System Information
:   [[idx:System Information]] Shows OSCAR's version, your operating system, and where your data folder is. Include it in any problem report.

## Reporting a problem

[[idx:Reporting a problem]]
To help the developers fix a problem:

1. Note what you did, what you expected, and what happened instead.
2. Copy the details from *Help → Troubleshooting → System Information*.
3. If the problem is with a machine's data, make a zip with *Create zip of CPAP data card*. For other problems, make a zip of the logs.
4. Use *Help → Troubleshooting → Report an Issue*, or post on the [Apnea Board](https://www.apneaboard.com) forum.

!!! note "Privacy"
    The SD card zip and the database zip contain your health data. Share them only with people you trust, such as the OSCAR developers.
