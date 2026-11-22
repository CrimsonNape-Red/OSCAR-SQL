"""Build the old-path -> new-path map for the Notes reorganization.

Usage: python make_map.py <repo-root> <out.tsv>
Reads the tracked files under Notes/ from git, applies the design's rules, and writes a
tab-separated file with columns: action, old path, new path.  Prints a summary to stdout.
"""
import subprocess
import sys
from collections import Counter

DEV = 'Notes/Developer Notes/'

# Explicit destinations for the 60 files in Notes/Developer Notes.
DEV_NOTES = {
    'ARCHITECTURE.md': 'Notes/Guides/',
    'NEW_DEVELOPER_GUIDE.md': 'Notes/Guides/',
    'DATABASE_INTEGRATION_GUIDE.md': 'Notes/Guides/',
    'SIGNALS_AND_SLOTS_GUIDE.md': 'Notes/Guides/',
    'PERFORMANCE_INSTRUMENTATION_GUIDE.md': 'Notes/Guides/',
    'MULTIPLE_INSTANCES.md': 'Notes/Guides/',
    'MULTIPLE_INSTANCES.mw': 'Notes/Guides/',
    'STEADY_BREATHING_ALGORITHM.md': 'Notes/Guides/',
    'SessionChannelData_Calculation_Explanation.md': 'Notes/Guides/',
    'ResMed_IE_Time_Trace.md': 'Notes/Guides/',
    'time differences labels.md': 'Notes/Guides/',
    '.001 Events File Format Documentation.md': 'Notes/Guides/OSCAR 1.x Files/',
    '.001 Events File Format Documentation-revised.md': 'Notes/Guides/OSCAR 1.x Files/',
    'analyze_001_diagnostic_notes.md': 'Notes/Guides/OSCAR 1.x Files/',
    'rxchanges.cache file use.md': 'Notes/Guides/OSCAR 1.x Files/',
    'sessions.info file use.md': 'Notes/Guides/OSCAR 1.x Files/',
    'shg file use.md': 'Notes/Guides/OSCAR 1.x Files/',
    'WEINMANN_LOADER.md': 'Notes/Formats/Lowenstein/',
    'Loaders sampled for learning to date.md': 'Notes/Formats/_General/',
    'DARK_MODE_DESIGN.md': 'Notes/Design/UI/',
    'REPORT_TREE_UI_REDESIGN.md': 'Notes/Design/UI/',
    'DATABASE_MENU_DESIGN.md': 'Notes/Design/',
    'DATABASE_MENU_DESIGN_v1.md': 'Notes/Design/Archive/',
    'PORTABLE_MODE_DESIGN.md': 'Notes/Design/',
    'SHARING_DESIGN.md': 'Notes/Design/',
    'URI_SCHEME_RESTORE.md': 'Notes/Design/',
    'PERCENTILE_INTERPOLATION_DESIGN.md': 'Notes/Design/',
    'JOURNAL_DATABASE_MIGRATION_DESIGN.md': 'Notes/Design/',
    'StructuredJournal.txt': 'Notes/Design/',
    'daily_summaries_per_machine_redesign.md': 'Notes/Design/',
    'BACKUP_RESTORE_IMPLEMENTATION_PLAN.md': 'Notes/Design/',
    'BACKUP_RESTORE_README.md': 'Notes/Design/',
    'Clock Drift Plan.md': 'Notes/Design/',
    'AUTOMATIC_PROFILE_MIGRATION_IMPLEMENTATION.md': 'Notes/Design/',
    'DATABASE_MIGRATION_STRATEGY.md': 'Notes/Design/',
    'DATABASE_MIGRATION_PROGRESS.md': 'Notes/Design/',
    'DATABASE_MIGRATION_COMPLETE.md': 'Notes/Design/',
    'DATABASE_LOADER_COMPATIBILITY.md': 'Notes/Design/',
    'Using wikimedia for help.md': 'Notes/Design/',
    'Reducing log noise.md': 'Notes/Design/',
    'Eliminate DST zone.md': 'Notes/Design/',
    'Code Review — Commit b5adca94.md': 'Notes/Investigations/',
    'LEAK_PERCENTILE_METHOD_REVIEW_2026-04-16.md': 'Notes/Investigations/',
    'RESMED_LEAK95_INVESTIGATION_SUMMARY_2026-04-16.md': 'Notes/Investigations/',
    'LOADER_CALL_ANALYSIS.md': 'Notes/Investigations/',
    'OPENPROFILE_MEMORY_PERFORMANCE_REVIEW.md': 'Notes/Investigations/',
    'OPENPROFILE_MEMORY_PERFORMANCE_REVIEW-codex.md': 'Notes/Investigations/',
    'PROFILE_OPEN_TIMING_1.7.1_VS_2.2.md': 'Notes/Investigations/',
    'profile_open_timing.ps1': 'Notes/Investigations/',
    'TIME_ALIGNMENT_CODE_REVIEW.md': 'Notes/Investigations/',
    'TIME_ALIGNMENT_CODE_REVIEW_CODEX.md': 'Notes/Investigations/',
    'percentile_findiings.md': 'Notes/Investigations/',
    'profile_import_assessment.md': 'Notes/Investigations/',
    'Data Migration Process.md': 'Notes/Investigations/',
    'OSCAR_DATE_FIX.md': 'Notes/Investigations/',
    'QCOLOR_RGB_CONSTRUCTOR_CHANGES.md': 'Notes/Investigations/',
    'QT_COMBOBOX_SIGNAL_CHANGE.md': 'Notes/Investigations/',
    'QUERY_PULSE_P95.sql': 'Notes/Database/',
    'BUG_FIXES.md': 'Notes/Process/',
}

# Files that change name as well as folder: old path -> new path.
RENAMES = {
    DEV + 'Getting updates from Brian.md':
        'Notes/Process/Git/Getting updates from a contributor fork.md',
}

# Explicit single files outside Developer Notes: old path -> new folder.
SINGLE = {
    'Notes/Getting Started with OSCAR 2.0.md': 'UserResources/Getting Started/',
    'Notes/Database/oscar_details_csv.py': 'UserResources/Data Access/',
    'Notes/Maintenance/import_bugs_to_gitlab.py': 'Tools/',
    'Notes/Maintenance/COPYRIGHT_UPDATE_2026.md': 'Notes/Process/',
    'Notes/G3X/BMC_G3X_DAT_FORMAT.md': 'Notes/Formats/BMC/G3X/',
    'Notes/G3X/2026-06-10-g3x-evt-only-import.md': 'Notes/Design/',
    'Notes/loaders/AEONMED_AS100_CARD_ANALYSIS.md': 'Notes/Formats/Aeonmed/',
    'Notes/loaders/CONTEC_BLE.md': 'Notes/Formats/Oximeters/',
    'Notes/loaders/CSV_OXIMETRY.md': 'Notes/Formats/Oximeters/',
    'Notes/loaders/EMAY_EMO80_BLE.md': 'Notes/Formats/Oximeters/',
    'Notes/loaders/VIATOM_BLE_TESTING.md': 'Notes/Formats/Oximeters/',
    'Notes/loaders/VIATOM_USB.md': 'Notes/Formats/Oximeters/',
    'Notes/loaders/LEPU_VENTILATOR_FILE_FORMAT.md': 'Notes/Formats/Lepu/',
    'Notes/loaders/LOADER_DETECTION_PATTERNS.md': 'Notes/Formats/_General/',
    'Notes/loaders/SD_CARD_FINGERPRINTS.md': 'Notes/Formats/_General/',
    'Notes/loaders/PRISMA_VENT_CARD_ANALYSIS.md': 'Notes/Formats/Lowenstein/',
    'Notes/loaders/SEFAM_ANALYZE_VS_OSCAR.md': 'Notes/Formats/Sefam/',
    'Notes/loaders/SEFAM_LOADER_DESIGN.md': 'Notes/Formats/Sefam/',
    'Notes/loaders/SEFAM_LOADER_PLAN.md': 'Notes/Formats/Sefam/',
    'Notes/loaders/SEFAM_REVE_CARD_ANALYSIS.md': 'Notes/Formats/Sefam/',
    'Notes/loaders/SEFAM_SBOX_CARD_ANALYSIS.md': 'Notes/Formats/Sefam/',
}

# Whole folders: old prefix -> new prefix (None = delete, same prefix = keep).
FOLDERS = [
    ('Notes/Waveform Demo/', None),
    ('Notes/images/', 'UserResources/Getting Started/images/'),
    ('Notes/OSCAR Usage/', 'UserResources/Usage/'),
    ('Notes/Accessing OSCAR Data/', 'UserResources/Data Access/'),
    ('Notes/Cloud Services/', 'UserResources/Sharing Data/'),
    ('Notes/loaders/Apex/', 'Notes/Formats/Apex/'),
    ('Notes/loaders/G3X/', 'Notes/Formats/BMC/G3X/'),
    ('Notes/loaders/Luna/', 'Notes/Formats/BMC/Luna/'),
    ('Notes/specs/', 'Notes/Design/'),
    ('Notes/plans/', 'Notes/Design/'),
    ('Notes/UI Redesign/', 'Notes/Design/UI/'),
    ('Notes/Git/', 'Notes/Process/Git/'),
    ('Notes/AI Aids/', 'Notes/Process/AI Aids/'),
    ('Notes/Database/', 'Notes/Database/'),
    ('Notes/Wiki/', 'Notes/Wiki/'),
]


def destination(old):
    """Return (action, new path) for one tracked file."""
    if old in RENAMES:
        return 'move', RENAMES[old]
    name = old.rsplit('/', 1)[-1]
    if old in SINGLE:
        return 'move', SINGLE[old] + name
    if old.startswith(DEV):
        rest = old[len(DEV):]
        if rest not in DEV_NOTES:
            raise SystemExit('No rule for Developer Notes file: ' + rest)
        return 'move', DEV_NOTES[rest] + rest
    for prefix, target in FOLDERS:
        if old.startswith(prefix):
            if target is None:
                return 'delete', ''
            if target == prefix:
                return 'keep', old
            return 'move', target + old[len(prefix):]
    raise SystemExit('No rule for: ' + old)


def main():
    repo, out = sys.argv[1], sys.argv[2]
    raw = subprocess.run(['git', '-C', repo, 'ls-files', '-z', 'Notes'],
                         capture_output=True, check=True).stdout
    files = [p for p in raw.decode('utf-8').split('\0') if p]
    rows = [(old,) + destination(old) for old in files]
    targets = Counter(new for _, action, new in rows if action != 'delete')
    clashes = [t for t, n in targets.items() if n > 1]
    if clashes:
        raise SystemExit('Two files map to the same path: ' + ', '.join(clashes))
    unused = [k for k in DEV_NOTES if DEV + k not in files]
    unused += [k for k in list(SINGLE) + list(RENAMES) if k not in files]
    if unused:
        raise SystemExit('Rules that match no tracked file: ' + ', '.join(unused))
    with open(out, 'w', encoding='utf-8', newline='\n') as fh:
        for old, action, new in rows:
            fh.write('\t'.join((action, old, new)) + '\n')
    print('files:', len(rows), dict(Counter(a for _, a, _ in rows)))
    by_folder = Counter(new.rsplit('/', 1)[0] for _, a, new in rows if a == 'move')
    for folder, n in sorted(by_folder.items()):
        print('%4d  %s' % (n, folder))


if __name__ == '__main__':
    main()
