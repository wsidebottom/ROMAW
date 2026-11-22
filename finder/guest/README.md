# Native Finder guest procedures

The four C translations implement the former Finder 68000 resources. They
are linked into ROMAW, not the native desktop archive. `runtime.c` connects
them to real Memory Manager, QuickDraw, Resource Manager and Gestalt services
and implements the guest stack/result conventions.

| Resource | C entry | Dispatch slot | Behavior |
| --- | --- | --- | --- |
| `CDEF 128` | `finder_cdef128` | 11 | Draw a framed control, hit-test its inset interior, or calculate its region. |
| `WDEF 128` | `finder_wdef128` | 12 | Return zero for every message. |
| `gest 128`, Main | `finder_gest128` | 13 | Allocate/cache a system Handle and answer Finder private selectors. |
| `hcsl 0`, Main | `finder_hcsl0_install`, `finder_hcsl0_query` | 14, 15 | Register the hcsl selector and return the installed response. |

Resource declarations stay in `finder/resources/guest_code.r`. Matching
fallback declarations in `system/resources/system/finder_code.r` expose the
procedures through the embedded System map. Original names and attributes
survive. Application resource maps override the fallbacks through the normal
Resource Manager chain. There are no binary inputs.

`$$finderCode()` uses the checked encoder in `tools/finder_guest_resources.py`.
It emits only a 14-byte code-relative dispatch veneer, plus state storage and
the second hcsl entry where required. The veneer loads the resource base in A1
and tail-calls a native callback through ROMAW's existing low-memory 0x58 table.
The builder validates executable payloads against that encoder. All procedure
logic lives in C. Generated artifacts stay in the selected object tree.

## ABI and lifetime

CDEF and WDEF callbacks consume twelve bytes of Pascal arguments and write a
four-byte result. Gestalt handlers consume eight bytes of arguments and write
a two-byte result. The hcsl installer returns without consuming its single
argument, matching its original plain RTS. Callbacks preserve callee-saved
registers across nested Toolbox calls.

Toolbox calls use patch-aware dispatch and arguments stored in reserved guest
stack memory. Register-based Memory Manager and NewGestalt operations execute
real A-line dispatch with their original trap-word flags. Guest trap patches
therefore observe the same calls and valid guest argument addresses.

Writable state belongs to each resource, not a global host map:

* gest retains its cached Handle at offset 0x80. Its private selectors return
  the actual guest entry address (0x666e648c) and Handle (0x666e64f6).
  NewHandleSysClear and GetHandleSize use the real system heap.
* hcsl retains its response at offset 0x16 and its callable handler at 0x1a.
  The installer calls real NewGestalt, including its system-heap location
  check and duplicate-selector error. As in the original, registration
  errors do not undo the stored response.

Separate loaded copies therefore have independent state; reloads obtain the
resource's initial bytes. Gestalt registrations hold actual guest addresses,
so callers must retain the original locked system-heap resources while registered.
Five shared native callbacks are allocated once. There are no per-resource
host callback allocations or fabricated guest addresses.

## Translation details

The original CDEF begins with ORI.B #1,D0 and a function frame at offset 4.
The C body reads the rectangle at control-record offset 8, visibility at 16,
highlight at 17 and palette index at 38 (the low word of contrlRfCon, not
contrlValue). Every message locks/unlocks the ControlHandle. Highlighted drawing
uses a 2-by-2 pen followed by PenNormal. Ordinary drawing erases/frames, insets
by 3, then paints with a saved/restored foreground color when ROM85 bit 14 is
clear, or fills with the alternating AA55 pattern otherwise. Hit tests ignore
highlight state. Region calculation masks its pointer to 24 bits.

gest preserves the original size-left-shift-by-eight and low-word-decrement
search counter. The runtime additionally bounds reads to the real table Handle
allocation. Missing or truncated records return gestaltUndefSelectorErr (-5551)
without changing the response. Malformed tables cannot overrun guest memory
or make the original counter loop indefinitely. This is a bounded robustness
change to the original resource's unsafe search.

## Validation

From WSL at /mnt/d/Source/ROMAW, after building ROMAW:

```sh
bash system/tests/check-ui.sh finder-code
bash system/tests/check-ui.sh finder-guest /absolute/path/to/ROMAW.exe
python3 system/tests/test-resource-catalog.py
python3 system/tests/test-system-resources.py
```

finder-code runs the portable C fixture on Windows. finder-guest builds a
Retro68 application and executes it through the selected Windows ROMAW.
It exercises resource loading/reloading, real QuickDraw drawing and regions,
guest Pascal calls, selector tables, malformed table boundaries, independent
resource-instance state and actual Gestalt registration/failure paths.
Guest-installed HLock, FrameRect and NewGestalt trap hooks verify patch dispatch.
Reports and trace logs stay in build/finder-guest/. This focused suite does
not establish complete classic Finder application compatibility.
