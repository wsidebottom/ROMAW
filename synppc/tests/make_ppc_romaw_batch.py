#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_romaw_batch.py
#
#   Summary:   Generate one ROMAW application from flat PPC regression fixtures.
#
#   Classes:   None.
#
#   Functions: make_batch, main
#
#   Origin:    Original composition of the repository's PowerPC integration fixtures.
#
#   Copyright and Legal notices: Applicable license: synppc/COPYING.
# ===================================================================+

"""Generate a reporting ROMAW application from flat PPC checks."""
import argparse
import json
import runpy
import sys
from pathlib import Path
from unittest.mock import patch

sys.dont_write_bytecode = True
from make_ppc_system_batch import write_batch
import make_ppc_probe as toolbox


def make_batch(destination, app, cases, *, real_carbon=False):
    fragment_writer = toolbox.write_fragment
    checks, resources, fragments, files = [], {}, {}, {}
    for index, case in enumerate(cases):
        name = case["App"]
        script = Path(__file__).with_name(case["Generator"])
        fixture = destination / "checks" / f"{index + 1:02d}"
        captured = []

        def capture(target, fragment, image, kind, extra_resources=(), **options):
            if kind == 1 and fragment == name:
                assert not options
                captured.append((case["Report"], bytes(image)))
                for res_type, res_id, content in extra_resources:
                    key = (res_type, res_id)
                    if key in resources and resources[key] != content:
                        raise ValueError(f"{name} has a conflicting resource {key}")
                    resources[key] = content
            elif kind != 1 or fragment in case.get("FragmentFiles", ()):
                if real_carbon and target != fixture and target.is_relative_to(fixture):
                    fragment_writer(target, fragment, image, kind, extra_resources=extra_resources, **options)
                    return
                data = (bytes(image), kind, tuple(extra_resources), options)
                if target != fixture or (fragment in fragments and fragments[fragment] != data):
                    raise ValueError(f"{name} requires a conflicting fragment {fragment}")
                fragments[fragment] = data

        if script.name == "make_ppc_probe.py":
            assert not case.get("Arguments")
            with patch.object(toolbox, "write_fragment", capture):
                toolbox.make_probe(fixture)
        else:
            # Generators import the writer by name. Execute each check with a
            # fresh import cache for fixture modules that hold that reference.
            with (
                patch.object(toolbox, "write_fragment", capture),
                patch.dict(sys.modules),
                patch.object(sys, "argv", [str(script), str(fixture), *case.get("Arguments", [])]),
            ):
                fixture.mkdir(parents=True)
                runpy.run_path(str(script), run_name="__main__")
        if len(captured) != 1:
            raise ValueError(f"{script.name} did not generate exactly one {name}")
        checks.extend(captured)
        for folder in case.get("Directories", ()):
            if Path(folder).name != folder or not (fixture / folder).is_dir():
                raise ValueError(f"{name} requires a missing fixture directory {folder}")
            (destination / folder).mkdir(exist_ok=True)
        for filename in case.get("Files", []):
            if Path(filename).name != filename:
                raise ValueError(f"{name} requires a fixture filename in the application directory")
            contents = (fixture / filename).read_bytes()
            if filename in files and files[filename] != contents:
                raise ValueError(f"{name} requires a conflicting fixture file {filename}")
            files[filename] = contents

    for name, (image, kind, extra_resources, options) in fragments.items():
        fragment_writer(destination, name, image, kind, extra_resources=extra_resources, **options)
    for filename, contents in files.items():
        (destination / filename).write_bytes(contents)
    debug_library = "CarbonLib" if real_carbon and (b"SIZE", 0) in resources else "InterfaceLib"
    write_batch(
        destination,
        app,
        app,
        checks,
        extra_resources=[(*key, data) for key, data in resources.items()],
        debug_library=debug_library,
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path)
    parser.add_argument("app")
    parser.add_argument("manifest", type=Path)
    parser.add_argument("--real-carbon", action="store_true")
    args = parser.parse_args()
    make_batch(
        args.destination,
        args.app,
        json.loads(args.manifest.read_text(encoding="utf-8-sig")),
        real_carbon=args.real_carbon,
    )


if __name__ == "__main__":
    main()
