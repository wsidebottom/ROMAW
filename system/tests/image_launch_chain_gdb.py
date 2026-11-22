# +===================================================================
#   File:      image_launch_chain_gdb.py
#
#   Summary:   Inspect every mounted image at native Finder and real guest launch stages.
#
#   Classes:   Stage, Forbidden, Spawn.
#
#   Functions: evaluate, void, text, read_fork, sample, snapshot, application, remember_stop, inspect_child, inspect_stages.
#
#   Origin:    Executor Windows GDB integration fixture.
#
#   Copyright and Legal notices: See COPYING.
# ===================================================================+
import json
import os
import struct
import subprocess
import time
from pathlib import Path

import gdb

work = Path(os.environ["ROMAW_IMAGE_TEST"])
expected = {os.path.normcase(str(Path(p))) for p in json.loads((work / "images.json").read_text())}
seen = set()
run_id = os.environ.setdefault("ROMAW_IMAGE_TEST_RUN", str(time.time_ns()))
stop_signal = None


def remember_stop(event):
    global stop_signal
    stop_signal = event.stop_signal if isinstance(event, gdb.SignalEvent) else None


gdb.events.stop.connect(remember_stop)


def evaluate(expression):
    return int(gdb.parse_and_eval(expression))


def void(expression):
    gdb.execute("call (void)" + expression, to_string=True)


def text(value, encoding="cp1252"):
    raw = bytes(gdb.selected_inferior().read_memory(value.address, value.type.sizeof))
    return raw.split(b"\0", 1)[0].decode(encoding)


def read_fork(reference, path, resource, length, storage):
    inferior = gdb.selected_inferior()
    name = path.encode("mac_roman")
    assert len(name) < 256
    inferior.write_memory(storage, bytes([len(name)]) + name)
    function = "OpenRF" if resource else "FSOpen"
    error = evaluate(f"{function}((unsigned char*){storage},{reference},(short*){storage + 512})")
    assert error == 0, ("open", path, error)
    handle = struct.unpack("<h", inferior.read_memory(storage + 512, 2))[0]
    try:
        inferior.write_memory(storage + 520, struct.pack("<i", min(32, length)))
        error = evaluate(f"FSRead({handle},(int*){storage + 520},(char*){storage + 544})")
        assert error == 0, ("read", path, error)
        count = struct.unpack("<i", inferior.read_memory(storage + 520, 4))[0]
        assert count > 0
        return bytes(inferior.read_memory(storage + 544, count)).hex()
    finally:
        assert evaluate(f"FSClose({handle})") == 0


def sample(reference, directory, prefix, entry, storage, budget):
    children = []
    for index in range(1, 32768):
        budget[0] -= 1
        if budget[0] < 0:
            break
        error = evaluate(f"ROMlib_finder_image_entry({reference},{directory},{index},{int(entry)})")
        if error == -43:
            break
        assert error == 0, ("catalog", reference, directory, error)
        row = entry.dereference()
        path = prefix + ":" + text(row["name"], "mac_roman")
        if int(row["directory"]):
            children.append((int(row["id"]), path))
        else:
            for resource, field in [(0, "data_size"), (1, "resource_size")]:
                size = int(row[field])
                if size:
                    try:
                        return [path, resource, size, read_fork(reference, path, resource, size, storage)]
                    except AssertionError:
                        # Incomplete media may contain unavailable forks. Require
                        # one existing, readable fork; never manufacture missing bytes.
                        pass
    if prefix.count(":") < 6:
        for child, path in children:
            result = sample(reference, child, path, entry, storage, budget)
            if result:
                return result
    return None


def snapshot(role):
    inferior = gdb.selected_inferior()
    volume = gdb.parse_and_eval("(FinderImageVolume*)calloc(1,sizeof(FinderImageVolume))")
    entry = gdb.parse_and_eval("(FinderImageEntry*)calloc(1,sizeof(FinderImageEntry))")
    storage = evaluate("_NewPtr_flags(4096,1,1)")
    assert int(volume) and int(entry) and storage
    rows, sources = {}, set()
    baseline_path = work / "baseline.json"
    baseline = json.loads(baseline_path.read_text()) if baseline_path.exists() else None
    try:
        for index in range(128):
            if not evaluate(f"ROMlib_finder_image_volume({index},{int(volume)})"):
                break
            row = volume.dereference()
            source = os.path.normcase(str(Path(text(row["source"]))))
            sources.add(source)
            key = source + "#" + str(int(row["partition"]))
            reference = int(row["reference"])
            if baseline is None:
                data = sample(reference, 2, "", entry, storage, [500])
                assert data is not None, ("no readable fork", source)
            else:
                assert key in baseline, ("unexpected volume", key)
                data = baseline[key]
                assert read_fork(reference, *data[:3], storage) == data[3], ("fork bytes changed", key)
            rows[key] = data
        assert sources == expected, ("missing images", sorted(expected - sources), sorted(sources - expected))
        if baseline is not None:
            assert rows.keys() == baseline.keys(), "A partition was lost during launch"
        # Modern volumes must retain source-backed direct readers. Their virtual
        # namespace roots must never become physical placeholders or caches.
        mount = gdb.parse_and_eval("'diskimages.c'::mounted")
        modern = 0
        while int(mount):
            image = mount["set"].dereference()
            assert bool(image["direct"]), "Non-direct image reader"
            pathname = image["root"]["_M_pathname"]
            pointer = int(pathname["_M_dataplus"]["_M_p"])
            length = int(pathname["_M_string_length"])
            root = bytes(inferior.read_memory(pointer, length * 2)).decode("utf-16-le")
            assert not Path(root).exists(), ("Materialized image root", root)
            modern += 1
            mount = mount["next"]
        assert modern > 0
        cache = work / "profile/.romaw/cache"
        assert not [p for p in cache.rglob("*") if p.name != "directorymap-public-le"], "Image cache created"
        if baseline is None:
            baseline_path.write_text(json.dumps(rows, indent=2))
        result = {
            "role": role,
            "run_id": run_id,
            "images": len(sources),
            "volumes": len(rows),
            "modern_direct_sets": modern,
            "pid": inferior.pid,
            "forks_verified": len(rows),
        }
        (work / (role + ".json")).write_text(json.dumps(result, indent=2))
        print("IMAGE_CHAIN_STAGE_OK|" + json.dumps(result), flush=True)
    finally:
        void(f"DisposPtr((char*){storage})")
        void(f"free((void*){int(volume)})")
        void(f"free((void*){int(entry)})")


def application():
    storage = evaluate("_NewPtr_flags(512,1,1)")
    assert storage
    try:
        void(f"C_GetAppParms((unsigned char*){storage},(short*){storage + 256},(void*){storage + 264})")
        inferior = gdb.selected_inferior()
        length = bytes(inferior.read_memory(storage, 1))[0]
        return bytes(inferior.read_memory(storage + 1, length)).decode("mac_roman")
    finally:
        void(f"DisposPtr((char*){storage})")


class Stage(gdb.Breakpoint):
    def stop(self):
        return True


class Forbidden(gdb.Breakpoint):
    def stop(self):
        message = "Forbidden image caching/extraction call: " + self.location
        (work / "failure.txt").write_text(message)
        print(message, flush=True)
        return True


for function in [
    "romaw_images_prepare",
    "romaw_images_prepare_utf8",
    "romaw_images_open_readonly_utf8",
    "romaw_images_read_item_utf8",
    "romaw_images_export_utf8",
    "romaw_images_export_progress_utf8",
    "romaw_images_fork_utf8",
]:
    Forbidden(function, internal=True)
stages = [Stage(function, internal=True) for function in ["romaw_finder_main", "C_WaitNextEvent", "C_GetNextEvent"]]


class Spawn(gdb.Breakpoint):
    def stop(self):
        return True


spawns = [Spawn("*" + function, internal=True) for function in ["CreateProcessW", "CreateProcessA"]]


def inspect_child():
    """Attach to the real child before startup, including memory retries."""
    inferior = gdb.selected_inferior()
    stack = evaluate("$rsp")
    info = struct.unpack("<Q", inferior.read_memory(stack + 80, 8))[0]
    flags = struct.unpack("<I", inferior.read_memory(stack + 48, 4))[0]
    inferior.write_memory(stack + 48, struct.pack("<I", flags | 4))  # CREATE_SUSPENDED
    for breakpoint in spawns:
        breakpoint.enabled = False
    gdb.execute("finish")
    process, thread, pid, tid = struct.unpack("<QQII", inferior.read_memory(info, 24))
    assert process and thread and pid
    print("IMAGE_CHAIN_CHILD|" + str(pid), flush=True)
    ready = work / ("ready-" + str(pid))
    ready.unlink(missing_ok=True)
    commands = work / ("child-" + str(pid) + ".gdb")
    commands.write_text(
        "set pagination off\nset confirm off\nset print thread-events off\n"
        "break host_image_confirm_incomplete\ncommands\nsilent\nreturn 1\ncontinue\nend\n"
        "source D:/Source/ROMAW/system/tests/image_launch_chain_gdb.py\n"
        + "python Path("
        + repr(str(ready))
        + ").write_text('ready')\n"
        + "continue\npython inspect_stages()\nquit\n"
    )
    with (work / ("child-" + str(pid) + ".log")).open("w") as log:
        child = subprocess.Popen(
            [
                "C:/msys64/mingw64/bin/gdb.exe",
                "-q",
                "-batch",
                gdb.current_progspace().filename,
                "-p",
                str(pid),
                "-x",
                str(commands),
            ],
            stdout=log,
            stderr=subprocess.STDOUT,
            creationflags=0x08000000,
        )
        for _ in range(300):
            if ready.exists():
                break
            if child.poll() is not None:
                raise RuntimeError("Child debugger failed before attachment: " + str(pid))
            time.sleep(0.1)
        assert ready.exists(), "Child debugger did not attach"
        assert evaluate("(unsigned long)ResumeThread((void*)" + str(thread) + ")") == 1
        child.wait(timeout=240)
        result = work / "Disk First Aid.json"
        assert child.returncode == 0 and result.exists(), "Child chain did not finish"
        assert json.loads(result.read_text())["run_id"] == run_id, "Stale child-chain result"
    for breakpoint in spawns:
        breakpoint.enabled = True


def inspect_stages():
    """Run inferior calls outside Breakpoint.stop, preserving the guest CPU."""
    while True:
        try:
            if not gdb.selected_inferior().pid:
                return
            if (work / "failure.txt").exists():
                raise RuntimeError((work / "failure.txt").read_text())
            if stop_signal and stop_signal != "SIGTRAP":
                raise RuntimeError("Unexpected child signal: " + stop_signal)
            frame_name = gdb.selected_frame().name() or ""
            if (
                frame_name not in {"romaw_finder_main", "C_WaitNextEvent", "C_GetNextEvent"}
                and "CreateProcess" not in frame_name
            ):
                # A suspended Windows child first stops at the loader's initial
                # breakpoint, before any guest heap or Toolbox globals exist.
                gdb.execute("continue")
                continue
            if "CreateProcess" in frame_name:
                inspect_child()
                return
            cpu = gdb.parse_and_eval("cpu_state")
            saved_cpu = bytes(gdb.selected_inferior().read_memory(cpu.address, cpu.type.sizeof))
            try:
                role = "Finder" if gdb.selected_frame().name() == "romaw_finder_main" else application()
                if role not in seen and role in {"Finder", "ContinueLauncher", "ReplaceLauncher", "Disk First Aid"}:
                    snapshot(role)
                    seen.add(role)
                    if role in {"ContinueLauncher", "ReplaceLauncher"}:
                        assert evaluate("PPostEvent(15,0,0)") == 0
                    if role in {"Finder", "ContinueLauncher"}:
                        for breakpoint in stages:
                            breakpoint.enabled = False
            finally:
                gdb.selected_inferior().write_memory(cpu.address, saved_cpu)
            if role == "Disk First Aid":
                return
            gdb.execute("continue")
        except Exception as error:
            (work / "failure.txt").write_text(str(error))
            raise
