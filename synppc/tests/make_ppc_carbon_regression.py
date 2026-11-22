#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_carbon_regression.py
#
#   Summary:   Combine real CarbonLib regression groups in one reporting app.
#
#   Classes:   None.
#
#   Functions: make_regression, main
#
#   Origin:    Existing CarbonLib test generators and the PowerPC batch linker.
#
#   Copyright and Legal notices: See synppc/COPYING and carbonlib/COPYING.
# ===================================================================+

import argparse
import json
import sys
from pathlib import Path
from unittest.mock import patch

sys.dont_write_bytecode = True
import make_ppc_carbon_cf_collections_probe as collections
import make_ppc_carbon_cf_frameworks_probe as frameworks
import make_ppc_carbon_cf_probe as cf
import make_ppc_carbon_cf_values_probe as values
import make_ppc_carbon_classic_access_probe as classic
import make_ppc_carbon_toolbox_vectors_probe as toolbox
from carbon_print_checks import make_printing_checks
from make_ppc_probe import write_fragment
from make_ppc_system_batch import write_batch


APPLICATION = "PPC Carbon Regression"
GROUPS = ("Values", "Collections", "Frameworks", "ClassicAccess", "Toolbox", "Textension", "ATSUI", "Printing")


def make_regression(destination, groups, *, reference_only=False, math=False):
    generators = {
        "Values": lambda: values.make_probe(destination, "CoreFoundationLib"),
        "Collections": lambda: collections.make_probe(destination, "CoreFoundationLib", reference_only),
        "Frameworks": lambda: frameworks.make_probe(destination, reference_only),
        "ClassicAccess": lambda: classic.make_probe(destination, real=True, reference_only=reference_only),
        "Toolbox": lambda: toolbox.make_probe(destination, reference_only, math_only=math),
        "Textension": lambda: toolbox.make_probe(destination, False, text_only=True),
        "ATSUI": lambda: toolbox.make_atsui_probe(destination),
        "Printing": lambda: make_printing_checks(destination, reference_only),
    }
    checks, coverage, resources = [], [], {}
    images = []

    def capture(target, name, image, kind, extra_resources=(), **options):
        if kind != 1:
            write_fragment(target, name, image, kind, extra_resources, **options)
            return
        if target != destination or options:
            raise ValueError(f"Unexpected application fragment {name}")
        images.append((name, image))
        for resource_type, resource_id, content in extra_resources:
            key = (resource_type, resource_id)
            if key in resources and resources[key] != content:
                raise ValueError(f"Conflicting application resource {key} in {name}")
            resources[key] = content

    destination.mkdir(parents=True, exist_ok=True)
    coverage_path = destination / "coverage.json"
    with patch.object(cf, "write_fragment", capture), patch.object(cf.Probe, "identify_failed_checks", True):
        for group in groups:
            images.clear()
            generators[group]()
            report = json.loads(coverage_path.read_text())
            if len(images) != 1 or images[0][0] != report["application"]:
                raise ValueError(f"Expected one application for {group}")
            checks.append((group, images[0][1]))
            coverage.append({**report, "group": group})
            for name in ("checks.json", "native-symbols.json"):
                path = destination / name
                if path.exists():
                    path.replace(destination / (group + "-" + name))

    # CarbonLib rejects applications which directly import classic InterfaceLib.
    write_batch(
        destination,
        APPLICATION,
        APPLICATION,
        checks,
        extra_resources=[(*key, data) for key, data in resources.items()],
        debug_library="CarbonLib",
        exit_at_end=bool({"Toolbox", "Textension"}.intersection(groups)) and not reference_only and not math,
    )
    report = {"application": APPLICATION, "groups": coverage}
    coverage_path.write_text(json.dumps(report, indent=2) + "\n", newline="\n")


def main():
    parser = argparse.ArgumentParser(description="Generate a single app for real CarbonLib regression checks.")
    parser.add_argument("destination", type=Path)
    parser.add_argument(
        "groups",
        nargs="+",
        choices=GROUPS,
    )
    parser.add_argument("--original-only", action="store_true")
    parser.add_argument("--math", action="store_true")
    parser.add_argument("--atsui", action="store_true")
    args = parser.parse_args()
    groups = list(dict.fromkeys("ATSUI" if args.atsui and group == "Toolbox" else group for group in args.groups))
    if {"Toolbox", "Textension"}.intersection(groups) and "ATSUI" in groups:
        parser.error("Use check-corefoundation.ps1 to run ATSUI with Toolbox or Textension")
    if "ATSUI" in groups and args.original_only:
        parser.error("ATSUI checks require the native archive and the saved implementation")
    if "Textension" in groups and (args.original_only or args.math or "Toolbox" in groups):
        parser.error("Textension is a focused native Toolbox group and requires its saved oracle")
    make_regression(args.destination, groups, reference_only=args.original_only, math=args.math)


if __name__ == "__main__":
    main()
