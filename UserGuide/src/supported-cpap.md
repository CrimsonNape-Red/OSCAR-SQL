---
title: Supported CPAP Devices
index: Supported devices!CPAP; ResMed; Philips Respironics; DreamStation; System One; Fisher & Paykel; DeVilbiss IntelliPAP; Löwenstein; Weinmann; Resvent; BMC; React Health; Luna; Yuwell; Sefam; Apex Medical; vREM; Brick machines
---

# Supported CPAP Devices

OSCAR reads data from CPAP, APAP, bilevel (BiPAP/VPAP), ASV and some ventilator models from the makers below. How much OSCAR can show depends on what each machine records:

- **Full data**: usage, settings, events, and detailed waveforms such as flow rate, pressure and leak. Most current machines.
- **Summary only**: usage, settings and nightly totals, but no detailed graphs. Some older or budget machines, and nights recorded without the SD card.

When you import from a model in the same family as one listed here but not itself listed, OSCAR usually reads it but shows an **untested device** warning. Sending the developers a copy of the card helps them confirm it works (see [Reporting a problem](troubleshooting.md#reporting-a-problem)).

!!! note
    The tables list the models OSCAR recognises by name. A model missing from this list may still be supported — import it and see. Machines that store no data on a card cannot be read by OSCAR at all.

## ResMed

[[idx:ResMed]] [[idx:Supported devices!CPAP]]
OSCAR recognises a ResMed card by its `DATALOG` folder and `STR.edf` file.

| | Series | Models |
|---|---|---|
| ![ResMed AirSense 11](icons/airsense11.png){.device} | AirSense 11 / AirCurve 11 | AirSense 11 AutoSet, Elite, CPAP; AirCurve 11 VAuto and other AirCurve 11 models |
| ![ResMed AirSense 10](icons/airsense10.png){.device} | AirSense 10 | AirSense 10 AutoSet, AutoSet for Her, Elite, CPAP |
| ![ResMed AirCurve 10](icons/aircurve.png){.device} | AirCurve 10 | AirCurve 10 S, ST, VAuto, ASV, ASVAuto |
| ![ResMed Lumis 150](icons/lumis150.png){.device} | Lumis | Lumis bilevel and ventilator models |
| ![ResMed S9](icons/rms9.png){.device} | S9 | S9 AutoSet, AutoSet 25, AutoSet CS, AutoSet for Her, Elite, Escape, Escape Auto; VPAP Auto, VPAP S, VPAP ST, VPAP ST 22, VPAP ST-A, VPAP Adapt |
| | S8 (summary data) | S8 AutoSet II, S8 Elite II, S8 Escape, S8 Escape II, S8 Escape II AutoSet |

Notes for ResMed users:

- ResMed machines start a new day at **noon**. OSCAR's [session-splitting settings](preferences.md#session-splitting-settings) are locked to match.
- Some [Preferred Calculation Methods](preferences.md#preferred-calculation-methods) cannot be changed for ResMed, because the machine stores its own summary figures (for example the 95th percentile).
- ResMed records unintentional leak itself, so OSCAR does not need to calculate it.

## Philips Respironics

[[idx:Philips Respironics]] [[idx:DreamStation]] [[idx:System One]]
OSCAR recognises a Philips card by its `P-SERIES` folder.

| | Series | Models |
|---|---|---|
| ![DreamStation 2](icons/prds2.png){.device} | DreamStation 2 | DreamStation 2 CPAP (410X150C), Advanced CPAP (420X150C), Auto CPAP Advanced (520X110C, 520X130C, 520X150C), with P-Flex (521X120C), 521X140C |
| ![DreamStation](icons/dreamstation.png){.device} | DreamStation | CPAP Pro (400X110/120/130/150), Pro with Auto-Trial (401X150), Auto CPAP (500X110/120/130/150/180), Auto with A-Flex (500X140), Auto with P-Flex (501X120); Go (400G110), Go Auto (500G110/120/150, 502G150); BiPAP Pro (600X110/150), Auto BiPAP (700X110/120/130/150); BiPAP autoSV (900X110/120/150); BiPAP S/T 30 (1030X110, 1030X150 with AAM); BiPAP AVAPS 30 (1130X110, 1130X200, 1131X150 AE) |
| ![System One 60 Series](icons/prs1_60s.png){.device} | System One 60 Series | REMstar Pro (460P, 460PBT, 461P, 461CA, 462P), REMstar Auto (560P, 560PBT, 561P, 562P), BiPAP Pro (660P), BiPAP Auto (760P, 761P), BiPAP autoSV Advanced (960P, 961P), autoSV Advanced 30 (960T, 961TCA), BiPAP S/T 30 (1061T), BiPAP AVAPS 30 (1160P) |
| ![System One](icons/prs1.png){.device} | System One (50 Series) | REMstar Pro (450P, 451P, 452P), REMstar Auto (550P, 551P, 552P), BiPAP Pro (650P), BiPAP Auto (750P), BiPAP AutoSV Advanced (950P, 951P) |
| ![BiPAP S/T C Series](icons/prs1vent.png){.device} | C Series | BiPAP S/T (1061401) |
| | M-Series | REMstar M-Series, imported with *Data → Import RemStar MSeries Data* |

[[idx:Brick machines]]
**"Brick" machines.** Some basic models record only compliance data, not therapy data, so OSCAR can show little or nothing: REMstar Plus (251P, 261P, 261CA), Dorma 500 Auto (501V) and the basic DreamStation CPAP (200X110). OSCAR tells you when it finds one.

## Fisher & Paykel

[[idx:Fisher & Paykel]]
OSCAR recognises an F&P card by its `FPHCARE/ICON` folder.

| | Series | Models |
|---|---|---|
| ![Fisher & Paykel ICON](icons/fp_icon.png){.device} | ICON | ICON Auto, ICON Novo, ICON Premo and related ICON models |
| | SleepStyle | SleepStyle Auto and CPAP models |

## DeVilbiss IntelliPAP

[[idx:DeVilbiss IntelliPAP]]
| | Series | Models |
|---|---|---|
| ![IntelliPAP](icons/intellipap.png){.device} | IntelliPAP 1 | DV5x models (recognised by the `SL` folder) |
| ![IntelliPAP 2 DV64](icons/dv64.png){.device} | IntelliPAP 2 | DV6x models such as DV63E, DV64D, DV64E (recognised by the `DV6` folder) |

## Löwenstein and Weinmann

[[idx:Löwenstein]] [[idx:Weinmann]]
| Series | Models |
|---|---|
| prisma | prisma SMART, prisma LINE (such as prisma20A, prisma25S, prisma25ST), prismaCR |
| prisma VENT | prisma VENT30, VENT30-C, VENT40, VENT40-C, VENT50, VENT50-C |
| Weinmann SOMNO | SOMNObalance, SOMNObalance e, SOMNOcomfort, SOMNOcomfort 2, SOMNOcomfort 2e, SOMNOcompact, SOMNOcompact e, SOMNOset, SOMNOsmart, SOMNOsmart 2, SOMNOsoft, SOMNOsoft 2, SOMNOsoft 2e |

## Resvent

[[idx:Resvent]]
![Resvent iBreeze](icons/resvent.png){.device}

iBreeze CPAP and bilevel models (recognised by the `THERAPY` folder on the card).

## BMC and React Health

[[idx:BMC]] [[idx:React Health]] [[idx:Luna]]
| | Series | Models |
|---|---|---|
| ![BMC Luna](icons/bmc.png){.device} | Luna / RESmart (earlier format) | BMC Luna, Luna II and RESmart models |
| | G3 / G3X | React Health (BMC) Luna G3 and G3X CPAP and auto models |

## Yuwell

[[idx:Yuwell]]
![Yuwell](icons/yuwell.png){.device}

YH-350, YH-360, YH-450, YH-480, YH-480C, YH-550, YH-560, YH-580, YH-680, YH-690 (BreathCare III), YH-720, YH-725, YH-730, YH-820, YH-825, YH-830, YH-920 and YH-920E.

## Sefam

[[idx:Sefam]]
| | Models |
|---|---|
| ![Sefam S.Box](icons/sefam-sbox.png){.device} | Sefam S.Box Auto |
| ![Sanrai Rêve](icons/sanrai-reve.png){.device} | Sanrai/Sefam Rêve Auto |
| ![Sefam Néa](icons/sefam-nea.png){.device} | Sefam Néa Auto |

## Apex Medical

[[idx:Apex Medical]]
![Apex XT Auto](icons/apex-xt-auto.png){.device}

Apex XT Auto.

## vREM

[[idx:vREM]]
vREM CPAP models, such as vREM one.

## Machines OSCAR cannot read

Some machines keep their data only inside the machine or send it only to the maker's cloud service, with nothing usable on the card. OSCAR cannot read those. If your machine has an SD card slot but is not listed here, the developers may be able to add support from a copy of the card; ask on the [Apnea Board](https://www.apneaboard.com) forum.
