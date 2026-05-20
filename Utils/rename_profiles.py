"""
THIS IS GENERATED, PLEASE VET THIS FILE BEFORE USAGE.

rename_profiles.py

Renames existing ITGmania local profile directories from the old pure-numeric
format (e.g. "00000000") to the new "number_displayname" format
(e.g. "00000000_CoolPlayer").

The sanitization logic matches ProfileManager.cpp's SanitizeForDirName():
  - Characters illegal in Windows filenames are replaced with '_'
  - Spaces and periods are replaced with '_'
  - Result is capped at 32 characters
  - If the sanitized name is all underscores, no suffix is added

Run from any directory; the profiles path is hard-coded below.
Use --dry-run to preview renames without touching the filesystem.
"""

import argparse
import configparser
import os
import re
import sys

PROFILES_DIR = r"C:\Games\ITGmania\Save\LocalProfiles"
MAX_NAME_LEN = 32
# Matches a directory name that is already in the new format:
# 8 digits, optionally followed by underscore + anything.
ALREADY_NEW_FORMAT = re.compile(r"^\d{8}_.+$")
# Matches the old pure-numeric format (8 zero-padded digits).
OLD_FORMAT = re.compile(r"^\d{8}$")


def sanitize_for_dir_name(name: str) -> str:
    """Mirrors SanitizeForDirName() in ProfileManager.cpp."""
    ILLEGAL = set('\\/:*?"<>|')
    result = []
    for c in name:
        if ord(c) < 32 or c in ILLEGAL or c in (' ', '.'):
            result.append('_')
        else:
            result.append(c)
    result = ''.join(result)[:MAX_NAME_LEN]
    # If the sanitized name is nothing but underscores, treat as empty.
    if all(c == '_' for c in result):
        return ""
    return result


def read_display_name(profile_dir: str) -> str:
    """Read DisplayName from Editable.ini inside a profile directory."""
    editable_ini = os.path.join(profile_dir, "Editable.ini")
    if not os.path.isfile(editable_ini):
        return ""
    parser = configparser.RawConfigParser()
    # ITGmania writes INI files without quotes; RawConfigParser handles that.
    try:
        parser.read(editable_ini, encoding="utf-8")
    except Exception:
        try:
            parser.read(editable_ini, encoding="latin-1")
        except Exception:
            return ""
    # Section name may be capitalised differently; do a case-insensitive search.
    for section in parser.sections():
        if section.lower() == "editable":
            return parser.get(section, "DisplayName", fallback="")
    return ""


def collect_renames(profiles_dir: str):
    """
    Returns a list of (old_path, new_path) tuples for directories that need
    renaming.  Skips directories that are already in the new format or that
    are not in the old pure-numeric format.
    """
    renames = []
    try:
        entries = sorted(os.listdir(profiles_dir))
    except FileNotFoundError:
        print(f"ERROR: Profiles directory not found: {profiles_dir}", file=sys.stderr)
        sys.exit(1)

    for entry in entries:
        old_path = os.path.join(profiles_dir, entry)
        if not os.path.isdir(old_path):
            continue
        if not OLD_FORMAT.match(entry):
            if not ALREADY_NEW_FORMAT.match(entry):
                print(f"  SKIP  {entry!r}  (unrecognised format)")
            else:
                print(f"  SKIP  {entry!r}  (already renamed)")
            continue

        display_name = read_display_name(old_path)
        sanitized = sanitize_for_dir_name(display_name)

        if not sanitized:
            print(f"  SKIP  {entry!r}  (no usable display name: {display_name!r})")
            continue

        new_name = f"{entry}_{sanitized}"
        new_path = os.path.join(profiles_dir, new_name)
        renames.append((old_path, new_path, entry, new_name))

    return renames


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Print what would be renamed without actually renaming anything.",
    )
    parser.add_argument(
        "--profiles-dir",
        default=PROFILES_DIR,
        help=f"Path to LocalProfiles directory (default: {PROFILES_DIR})",
    )
    args = parser.parse_args()

    profiles_dir = args.profiles_dir
    print(f"Profiles directory: {profiles_dir}")
    print(f"Dry run: {args.dry_run}\n")

    renames = collect_renames(profiles_dir)

    if not renames:
        print("Nothing to rename.")
        return

    # Check for collisions before doing anything.
    new_names = [new_name for _, _, _, new_name in renames]
    existing = set(os.listdir(profiles_dir))
    collisions = [
        (old_name, new_name)
        for _, _, old_name, new_name in renames
        if new_name in existing and new_name != old_name
    ]
    if collisions:
        print("ERROR: The following renames would collide with existing directories:")
        for old, new in collisions:
            print(f"  {old!r}  ->  {new!r}")
        sys.exit(1)

    for old_path, new_path, old_name, new_name in renames:
        print(f"  {'WOULD RENAME' if args.dry_run else 'RENAMING'}  {old_name!r}  ->  {new_name!r}")
        if not args.dry_run:
            os.rename(old_path, new_path)

    print(f"\n{'Would rename' if args.dry_run else 'Renamed'} {len(renames)} director{'y' if len(renames)==1 else 'ies'}.")
    if args.dry_run:
        print("Re-run without --dry-run to apply.")


if __name__ == "__main__":
    main()
