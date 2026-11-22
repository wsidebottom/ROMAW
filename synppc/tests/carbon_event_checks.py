#!/usr/bin/env python3
# +===================================================================
#   File:      carbon_event_checks.py
#
#   Summary:   Emit Carbon Event Manager assertions into the grouped Toolbox regression app.
#
#   Classes:   None.
#
#   Functions: event_names, event_sections, window_property_names, windows_metadata_names
#
#   Origin:    CarbonLib 1.4 Event Manager and Universal Interfaces 3.4.2 CarbonEvents.h.
#
#   Copyright and Legal notices: See synppc/COPYING and carbonlib/COPYING.
# ===================================================================+

import re
import struct


def event_names(root):
    source = (root / "carbonlib/src/carbon_event_entries.h").read_text()
    return re.findall(r"^CARBON_EVENT\((\w+),", source, re.M)


def window_property_names(root):
    source = (root / "carbonlib/src/window_property_entries.h").read_text()
    return re.findall(r"^CARBON_(?:WINDOW_(?:PROPERTY|GROUP|ACTIVATION)|CONTROL_PROPERTY)\((\w+),", source, re.M) + [
        "GetWindowRetainCount",
        "RetainWindow",
        "CloneWindow",
        "ReleaseWindow",
    ]


def windows_metadata_names(root):
    source = (root / "carbonlib/src/window_property_entries.h").read_text()
    return re.findall(r"^CARBON_WINDOWS_COMPAT\((\w+),", source, re.M)


def event_sections(probe):
    p = probe.p
    event_class = int.from_bytes(b"ctst", "big")
    name = int.from_bytes(b"test", "big")
    kind = int.from_bytes(b"TEXT", "big")
    wildcard = int.from_bytes(b"****", "big")
    _, scratch = probe.raw(bytes(192))
    _, past = probe.raw(struct.pack(">4d", 17.5, 42.25, 30.0, 0.0))
    _, data = probe.raw(b"ABCDE\x00\x00\x00")
    _, changed = probe.raw(b"second\x00\x00")
    _, spec = probe.raw(struct.pack(">4I", event_class, 1, event_class, 2))
    _, first_callback = probe.raw(bytes(8))
    _, second_callback = probe.raw(bytes(8))
    _, timer_callback = probe.raw(bytes(8))
    wait_vector = probe.raw(bytes(8))
    _, wait_callback = wait_vector
    p.routines.extend(
        [
            (
                first_callback,
                [
                    0x90850008,  # stw r4,8(r5)
                    0x81850000,  # lwz r12,0(r5)
                    0x800C0000,
                    0x804C0004,
                    0x7C0903A6,
                    0x4E800420,
                ],
            ),
            (second_callback, [0x90850004, 0x38600000, 0x4E800020]),
            (timer_callback, [0x90640004, 0x4E800020]),
            (
                wait_callback,
                [
                    0x80E20000 | scratch + 124,  # Read and report live Carbon wait depth.
                    0x80E70000,
                    0x90E20000 | scratch + 136,
                    0x90620000 | scratch + 140,
                    0x90A20000 | scratch + 144,
                    0x90C20000 | scratch + 148,
                    0xB0640000,
                    0x38600001,
                    0x4E800020,
                ],
            ),
        ]
    )

    def require(name, args=(), status=0, side=0):
        probe.call(name, args, side=side)
        p.expect(status, name + " status")

    def save_word(offset):
        p.emit(0x907E0000 | offset)

    def check_word(offset, expected, label):
        p.emit(0x807E0000 | offset)
        p.expect(expected, label)

    def compare_words(first, second, label):
        p.emit(0x807E0000 | first, 0x809E0000 | second)
        p.equal(label)

    def create(slot, event_kind=1, side=0):
        p.emit(0xC83E0000 | past)
        require("CreateEvent", [0, event_class, event_kind, 0, 0, 0, probe.ptr(slot)], side=side)
        p.emit(0x807E0000 | slot)
        p.nonzero("CreateEvent returned a live event")

    def check_time(time_offset, event, side):
        probe.call("GetEventTime", [event], side=side)
        p.emit(0xD83E0000 | scratch + 16)
        compare_words(scratch + 16, time_offset, "event time high word")
        compare_words(scratch + 20, time_offset + 4, "event time low word")

    def check_parameter(event, payload, length, side):
        # Ask for two bytes to verify bounded copies and complete metadata.
        p.emit(0x3C605A5A, 0x60635A5A)
        save_word(scratch + 32)
        require(
            "GetEventParameter",
            [event, name, kind, probe.ptr(scratch + 24), 2, probe.ptr(scratch + 28), probe.ptr(scratch + 32)],
            side=side,
        )
        check_word(scratch + 24, kind, "parameter type")
        check_word(scratch + 28, length, "full size of a truncated parameter")
        check_word(scratch + 32, int.from_bytes(payload[:2] + b"ZZ", "big"), "truncation preserves output guard")
        require(
            "GetEventParameter",
            [event, name, wildcard, probe.ptr(scratch + 24), 0, probe.ptr(scratch + 28), 0],
            side=side,
        )
        check_word(scratch + 28, length, "wildcard parameter size query")

    def records():
        probe.call("GetCurrentEventTime")
        p.emit(0xD83E0000 | scratch + 16, 0x807E0000 | scratch + 16)
        p.nonzero("monotonic event clock provides a time")
        for symbol, floating in (("GetCurrentKeyModifiers", False), ("GetLastUserEventTime", True)):
            for side in (0, 1):
                probe.call(symbol, side=side)
                if floating:
                    p.emit(0xD83E0000 | scratch + 16 + side * 8)
                else:
                    save_word(scratch + 16 + side * 8)
            compare_words(scratch + 16, scratch + 24, symbol + " shares saved state")
            if floating:
                compare_words(scratch + 20, scratch + 28, symbol + " preserves the fraction")
        for side in (0, 1):
            create(scratch, side=side)
            event = probe.slot(scratch)
            for symbol, expected in (
                ("GetEventClass", event_class),
                ("GetEventKind", 1),
                ("GetEventAttributes", 0),
                ("GetEventPriority", 1),
                ("GetEventRetainCount", 1),
            ):
                require(symbol, [event], expected, side=1 - side)
            check_time(past, event, 1 - side)
            p.emit(0xC83E0000 | past + 8)
            require("SetEventTime", [event, 0, 0], side=side)
            check_time(past + 8, event, 1 - side)
            require("GetEventParameter", [event, name, kind, 0, 0, probe.ptr(scratch + 28), 0], -9870, side=side)
            require("SetEventParameter", [event, name, kind, 5, probe.ptr(data)], side=side)
            check_parameter(event, b"ABCDE", 5, 1 - side)
            probe.call("CopyEvent", [event], side=side)
            p.nonzero("copy event with parameter storage")
            save_word(scratch + 4)
            copy = probe.slot(scratch + 4)
            require("SetEventParameter", [event, name, kind, 6, probe.ptr(changed)], side=1 - side)
            check_parameter(copy, b"ABCDE", 5, 1 - side)
            check_parameter(event, b"second", 6, side)
            probe.call("RetainEvent", [copy], side=side)
            p.emit(0x809E0000 | scratch + 4)
            p.equal("retain returns the same saved-compatible event")
            require("GetEventRetainCount", [copy], 2, side=1 - side)
            probe.call("ReleaseEvent", [copy], side=1 - side)
            require("GetEventRetainCount", [copy], 1, side=side)
            probe.call("ReleaseEvent", [copy], side=side)
            probe.call("ReleaseEvent", [event], side=1 - side)
        require("CreateEvent", [0, event_class, 1, 0, 0, 0x100, probe.ptr(scratch)], -50)
        check_word(scratch, 0, "invalid attributes clear output")
        require("SetEventParameter", [0, name, kind, 0, 0], -50)

    def shared(name, offset):
        probe.call(name)
        p.nonzero(name + " is initialized")
        save_word(offset)
        probe.call(name, side=1)
        p.emit(0x809E0000 | offset)
        p.equal(name + " returns the saved object")

    def queues():
        shared("GetMainEventQueue", scratch + 4)
        shared("GetCurrentEventQueue", scratch + 8)
        compare_words(scratch + 4, scratch + 8, "current cooperative queue is the main queue")
        shared("GetMainEventLoop", scratch + 52)
        shared("GetCurrentEventLoop", scratch + 56)
        compare_words(scratch + 52, scratch + 56, "current cooperative loop is the main loop")
        queue = probe.slot(scratch + 4)
        for side in (0, 1):
            create(scratch, side=side)
            event = probe.slot(scratch)
            probe.call("GetNumEventsInQueue", [queue], side=side)
            save_word(scratch + 12)
            require("PostEventToQueue", [queue, event, 3], -50, side=side)
            require("PostEventToQueue", [queue, event, 2], side=side)
            require("IsEventInQueue", [queue, event], 1, side=1 - side)
            require("GetEventPriority", [event], 2, side=1 - side)
            require("GetEventRetainCount", [event], 2, side=1 - side)
            probe.call("GetNumEventsInQueue", [queue], side=1 - side)
            p.emit(0x809E0000 | scratch + 12, 0x38840001)
            p.equal("post adds an event to the shared queue")
            require("RemoveEventFromQueue", [queue, event], side=side)
            require("RemoveEventFromQueue", [queue, event], -9877, side=1 - side)
            require("IsEventInQueue", [queue, event], 0, side=side)
            require("GetEventRetainCount", [event], 1, side=1 - side)
            probe.call("GetNumEventsInQueue", [queue], side=side)
            p.emit(0x809E0000 | scratch + 12)
            p.equal("remove restores the queue count")
            probe.call("ReleaseEvent", [event], side=1 - side)

    def receive_events():
        queue = probe.slot(scratch + 4)
        output = probe.ptr(scratch + 16)
        for side in (0, 1):
            p.emit(0xC83E0000 | past + 24)
            require("ReceiveNextEvent", [1, probe.ptr(spec), 0, 0, 2, output], -50, side)
            require("ReceiveNextEvent", [1, probe.ptr(spec), 0, 0, 1, 0], -50, side)
            create(scratch, side=side)
            event = probe.slot(scratch)
            require("PostEventToQueue", [queue, event, 1], side=side)
            if not side and not probe.original_only:
                p.emit(0xC83E0000 | past + 24)
                require("ReceiveNextEvent", [1, probe.ptr(spec + 8), 0, 0, 0, output], -9875)
                check_word(scratch + 16, 0, "a nonmatching filter clears the output")
            p.emit(0xC83E0000 | past + 24)
            require("ReceiveNextEvent", [1, probe.ptr(spec), 0, 0, 0, output], side=side)
            compare_words(scratch + 16, scratch, "peeking returns the queued event")
            require("GetEventRetainCount", [event], 2, side=1 - side)
            p.emit(0xC83E0000 | past + 24)
            require("ReceiveNextEvent", [1, probe.ptr(spec), 0, 0, 1, output], side=side)
            compare_words(scratch + 16, scratch, "removing returns the queued event")
            require("IsEventInQueue", [queue, event], 0, side=1 - side)
            require("GetEventRetainCount", [event], 2, side=1 - side)
            probe.call("ReleaseEvent", [event], side=1 - side)
            probe.call("ReleaseEvent", [event], side=side)
            require("QuitEventLoop", [0], -50, side)
            require("QuitEventLoop", [probe.slot(scratch + 52)], side=side)

    def mouse_tracking():
        queue = probe.slot(scratch + 4)
        event = probe.slot(scratch)
        location, modifiers, outcome = (probe.ptr(scratch + offset) for offset in (24, 28, 32))
        mouse = int.from_bytes(b"mous", "big")
        key = int.from_bytes(b"keyb", "big")

        def post(event_class, event_kind):
            p.emit(0xC83E0000 | past)
            require("CreateEvent", [0, event_class, event_kind, 0, 0, 0, probe.ptr(scratch)])
            for label, data_type, value in ((b"mloc", b"QDpt", 0x00230045), (b"kmod", b"magn", 0x200)):
                p.emit(0x3C600000 | value >> 16, 0x60630000 | (value & 0xFFFF))
                save_word(scratch + 36)
                require(
                    "SetEventParameter",
                    [event, int.from_bytes(label, "big"), int.from_bytes(data_type, "big"), 4, probe.ptr(scratch + 36)],
                )
            require("PostEventToQueue", [queue, event, 2])

        def check_outcome(expected):
            p.emit(0xA07E0000 | scratch + 32)
            p.expect(expected, "mouse tracking writes the expected outcome")

        for side in (1, 0):
            require("TrackMouseLocation", [-1, 0, outcome], -50, side)
            require("TrackMouseLocationWithOptions", [-1, 0, 0, 0, location, modifiers, 0], -50, side)
            require("TrackMouseRegion", [-1, 0, location, outcome], -50, side)
            for event_class, event_kind, flags, expected in ((mouse, 1, 0, 1), (mouse, 2, 1, 2), (key, 4, 0, 6)):
                post(event_class, event_kind)
                p.emit(0xC83E0000 | past + 24)
                require(
                    "TrackMouseLocationWithOptions",
                    [-1, flags, 0, 0, location, modifiers, outcome],
                    side=side,
                )
                check_outcome(expected)
                if event_class == mouse:
                    check_word(scratch + 24, 0x00230045, "global tracking preserves the queued point")
                check_word(scratch + 28, 0x200, "track modifiers come from the queued event")
                require("IsEventInQueue", [queue, event], flags)
                require("GetEventRetainCount", [event], 1 + flags)
                if flags:
                    require("RemoveEventFromQueue", [queue, event])
                probe.call("ReleaseEvent", [event])
            if not side and not probe.original_only:
                # The saved wait loop can continue polling indefinitely when asked for zero seconds.
                p.emit(0xC83E0000 | past + 24)
                require("TrackMouseLocationWithOptions", [-1, 0, 0, 0, location, 0, outcome])
                check_outcome(8)

            post(mouse, 2)
            require("TrackMouseLocation", [-1, location, outcome], side=side)
            check_outcome(2)
            require("IsEventInQueue", [queue, event], 0)
            require("GetEventRetainCount", [event], 1)
            probe.call("ReleaseEvent", [event])

            probe.call("NewRgn")
            p.nonzero("allocate a tracking region")
            save_word(scratch + 40)
            region = probe.slot(scratch + 40)
            require("TrackMouseRegion", [-1, region, location, outcome], -50, side)
            probe.call("SetRectRgn", [region, -20000, -20000, 20000, 20000])
            p.emit(0x38600000, 0x987E0000 | scratch + 24)
            require("TrackMouseRegion", [-1, region, location, outcome], side=side)
            check_outcome(4)
            p.emit(0x887E0000 | scratch + 24)
            p.expect(1, "region tracking stores the new containment state")
            post(mouse, 1)
            require("TrackMouseRegion", [-1, region, location, outcome], side=side)
            check_outcome(1)
            require("IsEventInQueue", [queue, event], 0)
            require("GetEventRetainCount", [event], 1)
            probe.call("ReleaseEvent", [event])
            probe.call("DisposeRgn", [region])

    def find_and_flush_events():
        _, comparator = probe.raw(bytes(8))
        p.routines.append(
            (
                comparator,
                [
                    0x80840000,  # lwz r4,0(r4): expected event
                    0x7C632278,  # xor r3,r3,r4
                    0x7C630034,  # cntlzw r3,r3
                    0x5463D97E,  # srwi r3,r3,5
                    0x4E800020,
                ],
            )
        )
        queue = probe.slot(scratch + 4)
        events = [probe.slot(scratch + offset) for offset in (0, 16, 24)]
        probe.call("NewEventComparatorUPP", [probe.ptr(comparator)])
        p.nonzero("create the queue comparator")
        save_word(scratch + 28)
        comparator_upp = probe.slot(scratch + 28)
        for side in (0, 1):
            require("FindSpecificEventInQueue", [queue, 0, 0], side=side)
            require("FlushSpecificEventsFromQueue", [queue, 0, 0], -50, side)
            require("FlushEventQueue", [0], -50, side)
            require("FlushEventsMatchingListFromQueue", [queue, 0, probe.ptr(spec)], -50, side)
            require("FlushEventsMatchingListFromQueue", [queue, 1, 0], -50, side)
            require("FlushEventQueue", [queue], side=side)
            for index, offset in enumerate((0, 16, 24)):
                create(scratch + offset, event_kind=2 if index == 1 else 1)
                require("PostEventToQueue", [queue, events[index], index])
            probe.call("FindSpecificEventInQueue", [queue, comparator_upp, probe.ptr(scratch + 16)], side=side)
            p.emit(0x809E0000 | scratch + 16)
            p.equal("find returns the matching event")
            require("GetEventRetainCount", [events[1]], 2)
            require("FlushEventsMatchingListFromQueue", [queue, 1, probe.ptr(spec)], side=side)
            require("GetNumEventsInQueue", [queue], 1)
            require("FindSpecificEventInQueue", [queue, comparator_upp, probe.ptr(scratch)], side=side)
            require("GetEventRetainCount", [events[0]], 1)
            require("GetEventRetainCount", [events[1]], 2)
            require("GetEventRetainCount", [events[2]], 1)
            require("FlushSpecificEventsFromQueue", [queue, comparator_upp, probe.ptr(scratch)], side=side)
            require("GetNumEventsInQueue", [queue], 1)
            require("FlushSpecificEventsFromQueue", [queue, comparator_upp, probe.ptr(scratch + 16)], side=side)
            require("GetNumEventsInQueue", [queue], 0)
            require("GetEventRetainCount", [events[1]], 1)
            for event in events:
                probe.call("ReleaseEvent", [event])
        probe.call("DisposeEventComparatorUPP", [comparator_upp])

    def object_targets():
        bounds = probe.raw(struct.pack(">4h", 60, 40, 130, 180))
        title = probe.raw(b"\x05Event")
        window, control, menu = (probe.slot(scratch + offset) for offset in (96, 100, 104))
        probe.call("NewWindow", [0, bounds, title, 0, 0, -1, 1, 0])
        p.nonzero("create a window for event target checks")
        save_word(scratch + 96)
        probe.call("NewControl", [window, bounds, title, 1, 0, 0, 1, 0, 0])
        p.nonzero("create a button for event target checks")
        save_word(scratch + 100)
        probe.call("NewMenu", [8541, title])
        p.nonzero("create a menu for event target checks")
        save_word(scratch + 104)
        for name, obj in (
            ("GetWindowEventTarget", window),
            ("GetControlEventTarget", control),
            ("GetMenuEventTarget", menu),
        ):
            require(name, [0], 0)
            probe.call(name, [obj])
            p.nonzero(name + " returns a live event target")
            save_word(scratch + 108)
            probe.call(name, [obj], side=1)
            p.emit(0x809E0000 | scratch + 108)
            p.equal(name + " shares the object property with original CarbonLib")
        require("InstallStandardEventHandler", [0], -50)
        probe.call("GetApplicationEventTarget")
        save_word(scratch + 108)
        require("InstallStandardEventHandler", [probe.slot(scratch + 108)])
        probe.call("DisposeMenu", [menu])
        probe.call("DisposeControl", [control])
        probe.call("DisposeWindow", [window])

    def control_drag_tracking():
        bounds = probe.raw(struct.pack(">4h", 50, 40, 100, 140))
        title = probe.raw(b"\x04Drag")
        window, control = probe.slot(scratch + 96), probe.slot(scratch + 100)
        output = probe.ptr(scratch + 120)
        probe.call("NewWindow", [0, bounds, title, 0, 0, -1, 1, 0])
        p.nonzero("allocate a window for the drag-enabled control")
        save_word(scratch + 96)
        probe.call("NewControl", [window, bounds, title, 0, 0, 0, 1, 16 * 184, 0])
        p.nonzero("allocate a control that handles the drag CDEF messages")
        save_word(scratch + 100)

        def boolean_output(symbol, args, expected, side):
            p.emit(0x38605A5A, 0xB07E0000 | (scratch + 120))
            require(symbol, [*args, output], side=side)
            p.emit(0xA07E0000 | (scratch + 120))
            p.expect((expected << 8) | 0x5A, symbol + " only writes its Boolean output")

        for side in (0, 1):
            require("IsControlDragTrackingEnabled", [control, 0], -50, side)
            require("HandleControlDragTracking", [control, 1, 0, output], -50, side)
            boolean_output("IsControlDragTrackingEnabled", [control], 0, side)
            require("HandleControlDragReceive", [control, 0], -1857, side)
            boolean_output("HandleControlDragTracking", [control, 2, 0], 0, side)
            require("SetControlDragTrackingEnabled", [control, 1], side=side)
            boolean_output("IsControlDragTrackingEnabled", [control], 1, 1 - side)
            for message in (2, 3, 4):
                boolean_output("HandleControlDragTracking", [control, message, 0], 1, side)
            require("HandleControlDragReceive", [control, 0], side=side)
            require("SetControlDragTrackingEnabled", [control, 0], side=side)
            boolean_output("IsControlDragTrackingEnabled", [control], 0, 1 - side)

            require("IsAutomaticControlDragTrackingEnabledForWindow", [window, 0], -50, side)
            boolean_output("IsAutomaticControlDragTrackingEnabledForWindow", [window], 0, side)
            require("SetAutomaticControlDragTrackingEnabledForWindow", [window, 1], side=side)
            boolean_output("IsAutomaticControlDragTrackingEnabledForWindow", [window], 1, 1 - side)
            require("SetAutomaticControlDragTrackingEnabledForWindow", [window, 0], side=side)
            boolean_output("IsAutomaticControlDragTrackingEnabledForWindow", [window], 0, 1 - side)
        if not probe.original_only:
            for install, remove in (
                ("InstallTrackingHandler", "RemoveTrackingHandler"),
                ("InstallReceiveHandler", "RemoveReceiveHandler"),
            ):
                require(install, [0, window, 0], -50)
                require(remove, [wait_vector, window], -1856)
                require(install, [wait_vector, window, output])
                require(install, [wait_vector, window, 0], -1855)
                require(install, [wait_vector, 0, 0])
                require(remove, [wait_vector, window])
                require(remove, [wait_vector, window], -1856)
                require(remove, [wait_vector, 0])
            require("InstallTrackingHandler", [wait_vector, window, 0])
        probe.call("DisposeControl", [control])
        probe.call("DisposeWindow", [window])
        if not probe.original_only:
            require("RemoveTrackingHandler", [wait_vector, window], -1856)

    def drag_data_operations():
        _, state = probe.raw(bytes(40))
        content = probe.raw(b"ABCDEFGH")
        bounds = probe.raw(struct.pack(">4h", 13, 27, 72, 99))
        drag, output = probe.slot(state), probe.ptr(state + 4)
        size, data = probe.ptr(state + 8), probe.ptr(state + 16)
        first, second = 0xDEAD0001, 0xAABBCCDD
        text, promise = 0x54455854, 0x70726F6D

        require("NewDrag", [0], -50)
        require("NewDrag", [probe.ptr(state)])
        require("CountDragItems", [drag, output])
        check_word(state + 4, 0, "an empty drag has no items")
        require("AddDragItemFlavor", [drag, first, text, content, 8, 4])
        require("AddDragItemFlavor", [drag, first, text, content, 8, 0], -1853)
        require("AddDragItemFlavor", [drag, first, promise, 0, -1, 0])
        require("AddDragItemFlavor", [drag, second, text, content, 0, 0])
        require("CountDragItems", [drag, output])
        check_word(state + 4, 2 << 16, "the item count is a guest UInt16")
        require("GetDragItemReferenceNumber", [drag, 0, output], -1851)
        require("GetDragItemReferenceNumber", [drag, 2, output])
        check_word(state + 4, second, "items are returned in insertion order")
        require("CountDragItemFlavors", [drag, first, size])
        check_word(state + 8, 2 << 16, "the flavor count is a guest UInt16")
        require("GetFlavorType", [drag, first, 2, output])
        check_word(state + 4, promise, "indexed flavors preserve insertion order")
        require("GetFlavorFlags", [drag, first, text, output])
        check_word(state + 4, 4, "stored flavor flags are returned")
        require("GetFlavorFlags", [drag, first, promise, output])
        check_word(state + 4, 0x200, "a null payload records a promised flavor")
        require("GetFlavorDataSize", [drag, first, text, size])
        check_word(state + 8, 8, "the size reflects the copied payload")
        require("GetFlavorDataSize", [drag, first, promise, size], -1854)
        p.emit(0x38600004, 0x907E0000 | (state + 8), 0x38601234, 0x907E0000 | (state + 20))
        require("GetFlavorData", [drag, first, text, data, size, 2])
        check_word(state + 16, 0x43444546, "the payload read respects the offset and capacity")
        check_word(state + 20, 0x1234, "the read preserves the next word")
        require("SetDragItemFlavorData", [drag, first, text, content, 4, 10])
        require("GetFlavorDataSize", [drag, first, text, size])
        check_word(state + 8, 14, "appending extends the data through the requested offset")
        p.emit(0x38600004, 0x907E0000 | (state + 8))
        require("GetFlavorData", [drag, first, text, data, size, 8])
        check_word(state + 16, 0x00004142, "the gap added by an offset write is zero-filled")
        require("SetDragItemBounds", [drag, second, bounds])
        require("GetDragItemBounds", [drag, second, data])
        compare_words(state + 16, bounds[1], "the item bounds retain the top-left point")
        compare_words(state + 20, bounds[1] + 4, "the item bounds retain the bottom-right point")

        _, procedure = probe.raw(bytes(8))
        p.routines.append(
            (
                procedure,
                [
                    0x7C681B78,  # mr r8,r3
                    0x7CC33378,  # mr r3,r6
                    0x7C862378,  # mr r6,r4
                    0x7CA42B78,  # mr r4,r5
                    0x7D054378,  # mr r5,r8
                    0x81860000,
                    0x80C60004,
                    0x38E00004,
                    0x39000000,
                    0x800C0000,
                    0x804C0004,
                    0x7C0903A6,
                    0x4E800420,
                ],
            )
        )
        save_import("SetDragItemFlavorData", state + 24)
        probe.arguments([content], 0)
        save_word(state + 28)
        probe.call("NewDragSendDataUPP", [probe.ptr(procedure)])
        p.nonzero("allocate the promised-data callback")
        save_word(state + 12)
        require("SetDragSendProc", [drag, probe.slot(state + 12), probe.ptr(state + 24)])
        require("GetFlavorData", [drag, first, promise, data, size, 0])
        check_word(state + 8, 4, "the sender publishes the promised extent")
        check_word(state + 16, 0x41424344, "the sender publishes the promised bytes")
        require("GetFlavorFlags", [drag, first, promise, output])
        check_word(state + 4, 0, "fulfillment clears the promise flag")
        probe.call("DisposeDragSendDataUPP", [probe.slot(state + 12)])

        _, descriptors = probe.raw(bytes(16))
        source, copied = probe.ptr(descriptors), probe.ptr(descriptors + 8)
        require("GetDropLocation", [drag, copied])
        check_word(descriptors + 8, 0x6E756C6C, "a new drag has a null drop location")
        check_word(descriptors + 12, 0, "the null descriptor owns no handle")
        require("AECreateDesc", [text, content, 8, source])
        require("SetDropLocation", [drag, source])
        require("AEDisposeDesc", [source])
        require("GetDropLocation", [drag, copied])
        require("DisposeDrag", [drag])
        require("AEGetDescData", [copied, data, 8])
        check_word(state + 16, 0x41424344, "the returned location survives disposal of the drag")
        check_word(state + 20, 0x45464748, "the full location payload survives disposal")
        require("AEDisposeDesc", [copied])
        require("CountDragItems", [drag, output], -1850)
        require("DisposeDrag", [drag], -1850)

    def drag_tracking_operations():
        _, state = probe.raw(bytes(40))
        _, context = probe.raw(bytes(32))
        _, callback = probe.raw(bytes(8))
        initial = probe.raw(struct.pack(">HIIhhH", 1, 0, 0, 50, 60, 0x100))
        rectangle = probe.raw(struct.pack(">4h", 10, 20, 18, 30))
        offset = probe.raw(struct.pack(">2h", 3, 4))
        drag, source, result, upp = (probe.slot(state + n) for n in (0, 4, 8, 12))
        output = probe.ptr(state + 16)
        require("NewDrag", [probe.ptr(state)])
        for slot in (4, 8):
            probe.call("NewRgn")
            p.nonzero("allocate a region for the drag callback")
            save_word(state + slot)
        probe.call("RectRgn", [source, rectangle])
        require("GetDragRegion", [drag, result, 0], -50)
        require("SetDragRegion", [drag, source, 0], -50)
        require("TrackDrag", [drag, 0, 0], -50)

        for value, index in ((result, 0), (output, 4), (source, 16), (offset, 20)):
            probe.arguments([value], 0)
            save_word(context + index)
        save_import("GetDragRegion", context + 8)
        save_import("SetDragRegion", context + 24)
        p.routines.append(
            (
                callback,
                [
                    0x7C0802A6,  # mflr r0
                    0x90010008,
                    0x93C1FFF8,
                    0x93E1FFFC,
                    0x9421FFB0,
                    0x7CBF2B78,  # r31 is the callback context
                    0x7CDE3378,  # r30 is the drag reference
                    0x7FC3F378,
                    0x809F0010,
                    0x80BF0014,
                    0x819F0018,
                    0x800C0000,
                    0x804C0004,
                    0x7C0903A6,
                    0x4E800421,
                    0x907F001C,
                    0x7FC3F378,
                    0x809F0000,
                    0x80BF0004,
                    0x819F0008,
                    0x800C0000,
                    0x804C0004,
                    0x7C0903A6,
                    0x4E800421,
                    0x907F000C,
                    0x3860F8BF,  # cancel the drag without waiting for host input
                    0x38210050,
                    0x83C1FFF8,
                    0x83E1FFFC,
                    0x80010008,
                    0x7C0803A6,
                    0x4E800020,
                ],
            )
        )
        probe.call("NewDragInputUPP", [probe.ptr(callback)])
        p.nonzero("allocate a Drag Manager input callback")
        save_word(state + 12)
        for index in (12, 28):
            p.emit(0x3860FFFF)
            save_word(context + index)
        require("SetDragInputProc", [drag, upp, probe.ptr(context)])
        require("TrackDrag", [drag, initial, source], -1857)
        check_word(context + 12, 0, "the live callback can copy out the tracked region")
        check_word(context + 28, 0, "the live callback can replace the tracked region")
        compare_words(state + 16, offset[1], "the region offset stays relative to the pinned mouse")
        probe.call("GetRegionBounds", [result, output])
        check_word(state + 16, (53 << 16) | 64, "the copied region is translated to the pinned mouse")
        require("GetDragOrigin", [drag, output])
        check_word(state + 16, (50 << 16) | 60, "the drag origin is the original event point")
        require("SetDragMouse", [drag, (99 << 16) | 120])
        require("GetDragMouse", [drag, output, probe.ptr(state + 20)])
        check_word(state + 16, (50 << 16) | 60, "pinning preserves the physical mouse point")
        check_word(state + 20, (99 << 16) | 120, "the returned pinned point reflects SetDragMouse")
        require("GetDragModifiers", [drag, 0, output, probe.ptr(state + 18)])
        check_word(state + 16, 0x01000000, "cancellation preserves initial modifiers and clears release modifiers")
        require("GetDragAttributes", [drag, output])
        check_word(state + 16, 0, "cancellation before tracking leaves the window attributes empty")
        require("DisposeDrag", [drag])
        probe.call("DisposeDragInputUPP", [upp])
        probe.call("DisposeRgn", [result])
        probe.call("DisposeRgn", [source])

    def drag_feedback_operations():
        _, state = probe.raw(bytes(32))
        _, released_event = probe.raw(bytes(16))
        _, drawing = probe.raw(bytes(8))
        _, input_routine = probe.raw(bytes(8))
        rectangle = probe.raw(struct.pack(">4h", 40, 50, 65, 90))
        target = probe.raw(struct.pack(">4h", 42, 53, 62, 82))
        initial = probe.raw(struct.pack(">HIIhhH", 1, 0, 0, 48, 68, 0))
        drag, region, drawing_upp, input_upp = (probe.slot(state + offset) for offset in (0, 4, 8, 12))
        context = probe.ptr(state + 16)
        p.routines.extend(
            [
                (drawing, [0x81480000, 0x39600001, 0x7D6B1830, 0x7D4A5B78, 0x91480000, 0x38600000, 0x4E800020]),
                (input_routine, [0x80850004, 0x38840001, 0x90850004, 0x3864FFFF, 0x4E800020]),
            ]
        )
        probe.call("PostEvent", [2, 0])
        probe.call("WaitMouseMoved", [(48 << 16) | 68])
        p.expect(0, "a released mouse does not start a drag even if the pointer moved")
        probe.call("GetNextEvent", [1 << 2, probe.ptr(released_event)])
        require("NewDrag", [probe.ptr(state)])
        probe.call("NewRgn")
        p.nonzero("allocate the feedback source region")
        save_word(state + 4)
        probe.call("RectRgn", [region, rectangle])
        require("ZoomRegion", [region, (2 << 16) | 3, 1, 0])
        require("ZoomRects", [rectangle, target, 1, 1])
        probe.call("GetRegionBounds", [region, probe.ptr(state + 24)])
        compare_words(state + 24, rectangle[1], "the zoom animation leaves its source region at its original position")
        probe.call("NewDragDrawingUPP", [probe.ptr(drawing)])
        p.nonzero("allocate the drag drawing callback")
        save_word(state + 8)
        probe.call("NewDragInputUPP", [probe.ptr(input_routine)])
        p.nonzero("allocate the drag cancellation callback")
        save_word(state + 12)
        require("SetDragDrawingProc", [drag, drawing_upp, context])
        require("SetDragInputProc", [drag, input_upp, context])
        require("ChangeDragBehaviors", [drag, 0, 1])
        require("TrackDrag", [drag, initial, region], -1857)
        check_word(state + 16, 0x2E, "drag feedback begins, draws, hides and ends on cancellation")
        check_word(state + 20, 2, "the second input callback cancelled the drag")
        require("DisposeDrag", [drag])
        probe.call("DisposeDragDrawingUPP", [drawing_upp])
        probe.call("DisposeDragInputUPP", [input_upp])
        probe.call("DisposeRgn", [region])

    def drag_highlight_operations(depth):
        _, state = probe.raw(bytes(48))
        bounds = probe.raw(struct.pack(">4h", 0, 0, 32, 32))
        frame = probe.raw(struct.pack(">4h", 8, 8, 24, 24))
        black = probe.raw(bytes(6))
        white = probe.raw(b"\xff" * 6)
        title = probe.raw(b"\x04Drag")
        drag, world, region, window = (probe.slot(state + offset) for offset in (0, 4, 16, 20))
        sample, pixmap = probe.ptr(state + 24), probe.slot(state + 40)
        probe.call("NewWindow", [0, bounds, title, 0, 0, -1, 0, 0])
        p.nonzero("create a window for querying the drag highlight color")
        save_word(state + 20)
        require("GetDragHiliteColor", [window, sample])
        probe.call("GetPortHiliteColor", [window, probe.ptr(state + 32)])
        compare_words(state + 24, state + 32, "drag highlighting uses the window port's highlight color")
        probe.call("DisposeWindow", [window])

        require("NewDrag", [probe.ptr(state)])
        probe.call("GetGWorld", [probe.ptr(state + 8), probe.ptr(state + 12)])
        require("NewGWorld", [probe.ptr(state + 4), depth, bounds, 0, 0, 0])
        probe.call("GetGWorldPixMap", [world])
        p.nonzero("get the drag highlight surface pixel map")
        save_word(state + 40)
        probe.call("LockPixels", [pixmap])
        p.expect(1, "lock the drag highlight surface")
        probe.call("SetGWorld", [world, 0])
        probe.call("ClipRect", [bounds])
        probe.call("HiliteColor", [black])
        probe.call("RGBBackColor", [white])
        probe.call("EraseRect", [bounds])
        probe.call("NewRgn")
        p.nonzero("create a local drag highlight frame")
        save_word(state + 16)
        probe.call("RectRgn", [region, frame])

        def pixel(x, shown, label):
            probe.call("GetCPixel", [x, 16, sample])
            check_word(state + 24, 0 if shown else 0xFFFFFFFF, label)

        require("ShowDragHilite", [drag, region, 1])
        pixel(9, True, "an inside highlight paints the two-pixel inset")
        pixel(8, False, "the inside highlight excludes the frame boundary")
        pixel(16, False, "the interior beyond the border remains white")
        probe.call("EraseRect", [bounds])
        require("UpdateDragHilite", [drag, region])
        pixel(9, True, "the update region repaints a draw-over part of the highlight")
        require("DragPreScroll", [drag, 1, -1])
        pixel(9, False, "pre-scroll removes the highlight before scrolling pixels")
        require("DragPostScroll", [drag])
        pixel(9, True, "post-scroll restores the highlight")
        require("ShowDragHilite", [drag, region, 0])
        pixel(9, False, "replacing the highlight removes the earlier border")
        pixel(6, True, "an outside highlight extends out from the frame")
        require("HideDragHilite", [drag])
        pixel(6, False, "hide restores the original pixels")
        require("DisposeDrag", [drag])
        probe.call("DisposeRgn", [region])
        probe.call("SetGWorld", [probe.slot(state + 8), probe.slot(state + 12)])
        probe.call("UnlockPixels", [pixmap])
        probe.call("DisposeGWorld", [world])

    def drag_images():
        _, state = probe.raw(bytes(76))
        bounds = probe.raw(struct.pack(">4h", 0, 0, 8, 8))
        shorter_bounds = probe.raw(struct.pack(">4h", 0, 0, 5, 8))
        black = probe.raw(bytes(6))
        initial = probe.raw(struct.pack(">HIIhhH", 1, 0, 0, 48, 68, 0))
        screen_patch = probe.raw(struct.pack(">4h", 48, 68, 56, 76))
        white = probe.raw(b"\xff" * 6)
        _, wait_offset = probe.raw(bytes(8))
        pixel_import = next(i for i, (_, symbol, _) in enumerate(p.imports) if symbol == "GetCPixel")
        pixel_import += p.import_base // 4
        address = pixel_import * 4
        load_pixel = (
            [0x81820000 | address]
            if address < 0x8000
            else [0x3D820000 | ((address + 0x8000) >> 16), 0x818C0000 | (address & 0xFFFF)]
        )
        p.routines.append(
            (
                wait_offset,
                [
                    0x9421FFC0,
                    0x7C0802A6,
                    0x90010044,
                    0x90410014,
                    0x38000002,
                    0xB0040000,  # Return mouseUp without depending on host input.
                    0x38600046,
                    0x38800032,
                    0x38A20000 | (state + 56),
                    *load_pixel,
                    0x800C0000,
                    0x804C0004,
                    0x7C0903A6,
                    0x4E800421,
                    0x80410014,
                    0x80010044,
                    0x7C0803A6,
                    0x38210040,
                    0x38600001,
                    0x4E800020,
                ],
            )
        )
        drag, world, alpha, short_mask, image, mask, short_pixmap, region = (
            probe.slot(state + offset) for offset in range(0, 32, 4)
        )
        result = probe.ptr(state + 40)
        require("Gestalt", [int.from_bytes(b"drag", "big"), result])
        check_word(state + 40, 0xD, "the PPC Drag Manager exposes image support")
        probe.call("GetGWorld", [probe.ptr(state + 32), probe.ptr(state + 36)])
        for depth, rectangle, world_slot, pixel_slot in (
            (8, bounds, 4, 16),
            (32, bounds, 8, 20),
            (32, shorter_bounds, 12, 24),
        ):
            surface = probe.slot(state + world_slot)
            require("NewGWorld", [probe.ptr(state + world_slot), depth, rectangle, 0, 0, 0])
            probe.call("GetGWorldPixMap", [surface])
            p.nonzero("get the drag image or alpha mask pixel map")
            save_word(state + pixel_slot)
            probe.call("LockPixels", [probe.slot(state + pixel_slot)])
            p.expect(1, "lock the drag image test surface")
            probe.call("SetGWorld", [surface, 0])
            probe.call("RGBBackColor", [black])
            probe.call("EraseRect", [rectangle])
        probe.call("SetGWorld", [probe.slot(state + 32), probe.slot(state + 36)])

        probe.call("NewRgn")
        p.nonzero("allocate an optional shape mask for the drag image")
        save_word(state + 28)
        probe.call("RectRgn", [region, bounds])
        require("NewDrag", [probe.ptr(state)])
        require("SetDragImage", [drag, 0, 0, 0, 0], -1861)
        require("SetDragImage", [drag, image, 0xFFFFFFFC, 0, 0], -1860)
        require("SetDragImageWithAlpha", [drag, image, 0, 0, 0], -50)
        require("SetDragImageWithAlpha", [drag, image, image, 0, 0], -50)
        require("SetDragImageWithAlpha", [drag, image, short_pixmap, 0, 0], -50)
        require("ChangeDragBehaviors", [drag, 0, 1])
        offset = (48 << 16) | 68
        p.emit(0x80600D2C)  # Select the color Window Manager port.
        save_word(state + 44)
        probe.call("SetPort", [probe.slot(state + 44)])
        probe.call("GetBackColor", [probe.ptr(state + 64)])
        probe.call("RGBBackColor", [white])
        probe.call("EraseRect", [screen_patch])
        probe.call("GetCPixel", [70, 50, result])
        check_word(state + 40, 0xFFFFFFFF, "the drag backdrop starts white")
        p.emit(0x80600F80)  # Save the live WaitNextEvent vector.
        save_word(state + 48)
        probe.call("NewRoutineDescriptor", [probe.ptr(wait_offset), 0x3F90, 1])
        p.nonzero("install a temporary drag image observation callback")
        save_word(state + 52)
        p.emit(0x90600F80)
        for setter, arguments in (
            ("SetDragImage", [drag, image, region, offset, 0x10]),
            ("SetDragImageWithAlpha", [drag, image, mask, offset, 3]),
        ):
            require(setter, arguments)
            p.emit(0x3860FFFF)
            save_word(state + 56)
            require("TrackDrag", [drag, initial, 0], -1857)
            p.emit(0x807E0000 | (state + 56), 0x7C6318F8)
            p.nonzero("the image is visible while the drag waits for its next event")
            probe.call("GetCPixel", [70, 50, result])
            check_word(state + 40, 0xFFFFFFFF, "ending the drag restores the original screen pixels")
        p.emit(0x807E0000 | (state + 48), 0x90600F80)
        probe.call("DisposeRoutineDescriptor", [probe.slot(state + 52)])
        probe.call("RGBBackColor", [probe.ptr(state + 64)])
        probe.call("SetGWorld", [probe.slot(state + 32), probe.slot(state + 36)])
        require("DisposeDrag", [drag])
        probe.call("DisposeRgn", [region])
        for surface, pixmap in ((world, image), (alpha, mask), (short_mask, short_pixmap)):
            probe.call("UnlockPixels", [pixmap])
            probe.call("DisposeGWorld", [surface])

    def drag_textedit_selection():
        _, state = probe.raw(bytes(16))
        dest = probe.raw(struct.pack(">4h", 12, 16, 70, 180))
        view = probe.raw(struct.pack(">4h", 12, 18, 70, 140))
        text = probe.raw(b"Carbon Drag")
        editor, region = probe.slot(state), probe.slot(state + 4)
        probe.call("TENew", [dest, view])
        p.nonzero("create a TextEdit record for a drag selection")
        save_word(state)
        probe.call("TESetText", [text, 11, editor])
        probe.call("NewRgn")
        p.nonzero("allocate a drag selection region")
        save_word(state + 4)
        probe.call("RectRgn", [region, dest])
        require("TEGetHiliteRgn", [region, editor])
        probe.call("EmptyRgn", [region])
        p.expect(1, "a caret replaces the previous region with an empty selection")
        probe.call("TESetSelect", [0, 6, editor])
        require("TEGetHiliteRgn", [region, editor])
        probe.call("PtInRgn", [(13 << 16) | 18, region])
        p.expect(1, "the selection covers the first visible character in local TextEdit coordinates")
        probe.call("PtInRgn", [(13 << 16) | 16, region])
        p.expect(0, "selection highlighting respects the TextEdit view rectangle")
        probe.call("PtInRgn", [(13 << 16) | 120, region])
        p.expect(0, "the selection stops at the last selected character")
        require("TEGetHiliteRgn", [0, editor], -50)
        require("TEGetHiliteRgn", [region, 0], -50)
        probe.call("TEDispose", [editor])
        probe.call("DisposeRgn", [region])

    def window_properties_and_buttons():
        bounds = probe.raw(struct.pack(">4h", 60, 40, 130, 180))
        title = probe.raw(b"\x04Test")
        data = probe.raw(b"Testdata")
        window, control = (probe.slot(scratch + offset) for offset in (96, 100))
        result, output = (probe.ptr(scratch + offset) for offset in (104, 108))
        creator, tag = (int.from_bytes(value, "big") for value in (b"ARDI", b"test"))
        probe.call("NewWindow", [0, bounds, title, 0, 0, -1, 1, 0])
        p.nonzero("create a window for shared properties")
        save_word(scratch + 96)
        probe.call("NewControl", [window, bounds, title, 0, 0, 0, 1, 0, 0])
        p.nonzero("create a button")
        save_word(scratch + 100)

        for side in (0, 1):
            for noun, key in (
                ("WindowProperty", [window, creator, tag]),
                ("WindowSystemProperty", [window, tag]),
                ("ControlProperty", [control, creator, tag]),
                ("ControlSystemProperty", [control, tag]),
            ):
                require("Get" + noun + "Size", [*key, result], -5604, side=side)
                require("Set" + noun, [*key, 8, data], side=side)
                require("Get" + noun, [*key, 0, result, 0], 0 if noun == "WindowProperty" else -50, side=side)
                require("Get" + noun + "Size", [*key, result], side=1 - side)
                check_word(scratch + 104, 8, "window property size")
                require("Get" + noun, [*key, 8, result, output], side=1 - side)
                check_word(scratch + 104, 8, "window property bytes read")
                check_word(scratch + 108, int.from_bytes(b"Test", "big"), "first property word")
                check_word(scratch + 112, int.from_bytes(b"data", "big"), "second property word")
                require("Change" + noun + "Attributes", [*key, 1, 0], side=side)
                require("Get" + noun + "Attributes", [*key, result], side=1 - side)
                check_word(scratch + 104, 1, "window property is persistent")
                require("Change" + noun + "Attributes", [*key, 0, 1], side=1 - side)
                require("Get" + noun + "Attributes", [*key, result], side=side)
                check_word(scratch + 104, 0, "window property is no longer persistent")
                require("Remove" + noun, key, side=1 - side)
                require("Get" + noun + "Size", [*key, result], -5604, side=side)

            for kind in ("Default", "Cancel"):
                getter, setter = f"GetWindow{kind}Button", f"SetWindow{kind}Button"
                require(getter, [window, result], side=side)
                check_word(scratch + 104, 0, "new window has no designated button")
                require(setter, [window, control], side=side)
                require(getter, [window, result], side=1 - side)
                compare_words(scratch + 104, scratch + 100, "designated button is shared")
                require(setter, [window, 0], side=1 - side)
                require(getter, [window, result], side=side)
                check_word(scratch + 104, 0, "clearing the designated button")

        require("GetWindowPropertySize", [window, 0x6D616373, tag, result], -5603)
        require("GetControlPropertySize", [control, 0x6D616373, tag, result], -5603)
        require("GetControlPropertySize", [0, creator, tag, result], -30599)
        require("GetWindowDefaultButton", [0, result], -50)
        check_word(scratch + 104, 0, "invalid button lookup clears output")
        probe.call("DisposeControl", [control])
        probe.call("DisposeWindow", [window])

    def window_titles():
        bounds = probe.raw(struct.pack(">4h", 60, 40, 130, 180))
        classic = probe.raw(b"\x04Test")
        renamed = probe.raw(b"\x04Caf\x8e")
        _, title_bytes = probe.raw(bytes(256))
        window, plain, accent, value = (probe.slot(scratch + offset) for offset in (96, 100, 104, 108))
        output = probe.ptr(scratch + 108)
        probe.call("NewWindow", [0, bounds, classic, 0, 0, -1, 1, 0])
        p.nonzero("create a window for title checks")
        save_word(scratch + 96)
        for source, location in ((classic, 100), (renamed, 104)):
            probe.call("CFStringCreateWithPascalString", [0, source, 0])
            p.nonzero("create expected window title string")
            save_word(scratch + location)

        def copy(name, expected, side=0):
            require(name, [window, output], side=side)
            require("CFEqual", [value, expected], 1)
            probe.call("CFRelease", [value])

        copy("CopyWindowTitleAsCFString", plain)
        require("CopyWindowAlternateTitle", [window, output])
        check_word(scratch + 108, 0, "new window has no alternate title")
        require("SetWindowTitleWithCFString", [window, accent])
        copy("CopyWindowTitleAsCFString", accent, side=1)
        probe.call("GetWTitle", [window, probe.ptr(title_bytes)])
        check_word(title_bytes, 0x04436166, "Classic title begins with the displayed label")
        p.emit(0x887E0000 | title_bytes + 4)
        p.expect(0x8E, "Classic title is stored in MacRoman")

        for side in (0, 1):
            require("SetWindowAlternateTitle", [window, plain], side=side)
            copy("CopyWindowAlternateTitle", plain, side=1 - side)
            copy("CopyWindowTitleAsCFString", accent, side=1 - side)
            require("SetWindowAlternateTitle", [window, 0], side=1 - side)
            require("CopyWindowAlternateTitle", [window, output], side=side)
            check_word(scratch + 108, 0, "alternate title can be cleared")
        probe.call("SetWTitle", [window, classic])
        copy("CopyWindowTitleAsCFString", plain)
        require("SetWindowTitleWithCFString", [window, 0], -50)
        require("CopyWindowTitleAsCFString", [0, output], -5600)
        check_word(scratch + 108, 0, "invalid window clears the title output")
        probe.call("CFRelease", [plain])
        probe.call("CFRelease", [accent])
        probe.call("DisposeWindow", [window])

    def window_metadata():
        bounds = probe.raw(struct.pack(">4h", 60, 40, 130, 180))
        caption = probe.raw(b"\x05Class")
        window = probe.slot(scratch + 96)
        native, saved = (probe.ptr(scratch + offset) for offset in (100, 104))
        probe.call("NewWindow", [0, bounds, caption, 0, 0, -1, 1, 0])
        p.nonzero("create a classic window for class and attribute checks")
        save_word(scratch + 96)

        for name in ("GetWindowClass", "GetWindowAttributes", "GetWindowOwnerCount", "GetWindowActivationScope"):
            require(name, [window, native])
            require(name, [window, saved], side=1)
            compare_words(scratch + 100, scratch + 104, "native and saved " + name + " agree")

        require("GetWindowRetainCount", [0], 0)
        require("ReleaseWindow", [0], -5600)
        for side in (0, 1):
            require("GetWindowRetainCount", [window], 1, side=side)
            require("RetainWindow", [window], side=side)
            require("CloneWindow", [window], side=1 - side)
            require("GetWindowRetainCount", [window], 3, side=1 - side)
            require("GetWindowOwnerCount", [window, native], side=side)
            check_word(scratch + 100, 3, "both ownership APIs observe the same retain count")
            probe.call("DisposeWindow", [window])
            require("GetWindowRetainCount", [window], 2, side=side)
            require("ReleaseWindow", [window], side=1 - side)
            require("GetWindowRetainCount", [window], 1, side=1 - side)

        for window_class in (*range(19), 0xFFFFFFFF):
            for name in ("_IsValidWindowClass", "_GetAvailableWindowAttributes"):
                probe.call(name, [window_class], side=1)
                save_word(scratch + 104)
                probe.call(name, [window_class])
                p.emit(0x809E0000 | scratch + 104)
                p.equal(name + " matches the installed class table")

        require("SetWindowActivationScope", [window, 4], -50)
        require("SetWindowActivationScope", [window, 3])
        require("GetWindowActivationScope", [window, saved], side=1)
        check_word(scratch + 104, 3, "saved getter sees the native activation scope")
        require("SetWindowActivationScope", [window, 1], side=1)
        require("GetWindowActivationScope", [window, native])
        check_word(scratch + 100, 1, "native getter sees the saved activation scope")
        require("GetWindowClass", [window, 0], -50)
        require("GetWindowAttributes", [0, native], -5600)
        require("SetWindowClass", [window, 8], -50)
        require("SetWindowClass", [window, 5])
        require("GetWindowClass", [window, saved], side=1)
        check_word(scratch + 104, 5, "the changed window class is visible to the saved getter")
        require("SetWindowClass", [window, 6])
        require("GetWindowClass", [window, native])
        check_word(scratch + 100, 6, "window can return to the document class")
        require("GetWindowAttributes", [window, native])
        require("GetWindowAttributes", [window, saved], side=1)
        compare_words(scratch + 100, scratch + 104, "class change preserves shared attributes")

        for side in (0, 1):
            require("ChangeWindowAttributes", [0, 0, 0], -5600, side=side)
            require("ChangeWindowAttributes", [window, 0, 0], side=side)
            require("ChangeWindowAttributes", [window, 1, 1], -50, side=side)
            require("ChangeWindowAttributes", [window, 0, 1], -5601, side=side)

        # The saved routine identifies its custom WDEF path by the resource ID
        # in the system property. Leave the installed drawing procedure intact.
        probe.call("GetResource", [int.from_bytes(b"wcls", "big"), 123])
        p.nonzero("load a resource identifying the custom window definition")
        save_word(scratch + 112)
        require("SetWindowSystemProperty", [window, int.from_bytes(b"wdef", "big"), 4, probe.ptr(scratch + 112)])

        for side in (0, 1):
            for close in (0, 1):
                require("ChangeWindowAttributes", [window, close, 1 - close], side=side)
                require("GetWindowAttributes", [window, native], side=1 - side)
                p.emit(0x807E0000 | scratch + 100, 0x70630001)
                p.expect(close, "attribute change is visible across both implementations")
                p.emit(0x807E0000 | scratch + 96, 0x88630070)
                p.expect(close, "attribute change updates the classic window record")

        probe.call("NewWindow", [0, bounds, caption, 1, 0, -1, 0, 0])
        p.nonzero("create a visible front window for class traversal")
        save_word(scratch + 108)
        front = probe.slot(scratch + 108)
        for name, prefix in (("GetFrontWindowOfClass", []), ("GetNextWindowOfClass", [front])):
            for window_class in (6, 0xFFFFFFFE, 0xFFFFFFFF):
                for visible in (0, 1):
                    args = [*prefix, window_class, visible]
                    probe.call(name, args, side=1)
                    save_word(scratch + 104)
                    probe.call(name, args)
                    p.emit(0x809E0000 | scratch + 104)
                    p.equal(name + " matches saved class and visibility traversal")

        require("ReleaseWindow", [front])
        require("GetWindowRetainCount", [front], 0)
        probe.call("DisposeWindow", [window])

    def window_groups():
        bounds = probe.raw(struct.pack(">4h", 65, 45, 125, 165))
        caption = probe.raw(b"\x05Group")
        slots = [probe.slot(scratch + offset) for offset in (96, 100, 104, 108)]
        hidden, front, root, group = slots
        for visible, offset in ((0, 96), (1, 100)):
            probe.call("NewWindow", [0, bounds, caption, visible, 0, -1, 1, 0])
            p.nonzero("create a window for Carbon group traversal")
            save_word(scratch + offset)
        for selector in (0xFFFFFFFF, 1, 3, 6, 8, 0xFFFFFFFF - 1):
            probe.call("GetWindowGroupOfClass", [selector], side=1)
            save_word(scratch + 112)
            probe.call("GetWindowGroupOfClass", [selector])
            if selector == 0xFFFFFFFF:
                save_word(scratch + 104)
            p.emit(0x809E0000 | scratch + 112)
            p.equal("class group matches the saved CarbonLib")
        probe.call("GetWindowGroup", [front], side=1)
        save_word(scratch + 112)
        probe.call("GetWindowGroup", [front])
        p.nonzero("a valid window has a parent group")
        save_word(scratch + 108)
        compare_words(scratch + 108, scratch + 112, "the parent group agrees with the saved implementation")

        for parent in (root, group):
            for window in (hidden, front):
                probe.call("IsWindowContainedInGroup", [window, parent], side=1)
                save_word(scratch + 112)
                probe.call("IsWindowContainedInGroup", [window, parent])
                p.emit(0x809E0000 | scratch + 112)
                p.equal("group membership agrees with the saved implementation")
            for options in range(8):
                probe.call("CountWindowGroupContents", [parent, options], side=1)
                save_word(scratch + 112)
                probe.call("CountWindowGroupContents", [parent, options])
                p.emit(0x809E0000 | scratch + 112)
                p.equal("group contents count filters visibility and nested groups")

        for api, args in (
            ("GetIndexedWindow", [root, 1, 3]),
            ("GetIndexedWindow", [root, 1, 7]),
            ("GetIndexedWindow", [group, 1, 1]),
            ("GetIndexedWindow", [group, 99, 1]),
            ("GetIndexedWindow", [group, 0, 1]),
            ("GetWindowIndex", [front, root, 3]),
            ("GetWindowIndex", [hidden, root, 7]),
            ("GetWindowIndex", [front, group, 1]),
        ):
            for side, offset in ((1, 120), (0, 128)):
                p.emit(0x38000055, 0x901E0000 | scratch + offset)
                probe.call(api, args + [probe.ptr(scratch + offset)], side=side)
                save_word(scratch + offset + 4)
            compare_words(scratch + 120, scratch + 128, api + " writes the expected output")
            compare_words(scratch + 124, scratch + 132, api + " reports the expected status")

        for parent, options, limit in ((root, 0, 4), (group, 1, 2), (group, 5, 2), (root, 3, 2)):
            for side, offset in ((1, 136), (0, 160)):
                for index in range(5):
                    p.emit(0x38000055, 0x901E0000 | scratch + offset + 4 * index)
                probe.call(
                    "GetWindowGroupContents",
                    [parent, options, limit, probe.ptr(scratch + offset), probe.ptr(scratch + offset + 4)],
                    side=side,
                )
                save_word(scratch + offset + 20)
            for index in range(6):
                compare_words(scratch + 136 + 4 * index, scratch + 160 + 4 * index, "group contents match saved output")

        require("SetWindowGroup", [front, 0], -5616)
        require("SetWindowGroup", [front, group], -50)
        probe.call("GetWindowGroupOfClass", [5])
        p.nonzero("the floating window class identifies a built-in group")
        save_word(scratch + 112)
        destination = probe.slot(scratch + 112)
        require("SetWindowGroup", [front, destination], -50)
        probe.call("GetWindowGroup", [front], side=1)
        p.emit(0x809E0000 | scratch + 112)
        p.equal("the saved reader sees the destination after a native group move")
        require("SetWindowGroup", [front, group], -50, side=1)
        probe.call("GetWindowGroup", [front])
        p.emit(0x809E0000 | scratch + 108)
        p.equal("the native reader sees the original group restored by CarbonLib")
        probe.call("DisposeWindow", [front])
        probe.call("DisposeWindow", [hidden])

    def window_activation():
        bounds = probe.raw(struct.pack(">4h", 65, 45, 125, 165))
        caption = probe.raw(b"\x08Activate")
        document, floating, hidden = (probe.slot(scratch + offset) for offset in (96, 100, 104))
        for visible, offset in ((1, 96), (1, 100), (0, 104)):
            probe.call("NewWindow", [0, bounds, caption, visible, 0, -1, 1, 0])
            p.nonzero("create a window for activation and visibility checks")
            save_word(scratch + offset)
        require("SetWindowClass", [floating, 5])
        require("SetWindowActivationScope", [document, 2])
        probe.call("AreFloatingWindowsVisible", side=1)
        save_word(scratch + 108)

        def compare(name, args=()):
            probe.call(name, args, side=1)
            save_word(scratch + 112)
            probe.call(name, args)
            p.emit(0x809E0000 | scratch + 112)
            p.equal(name + " agrees with the saved implementation")

        require("HideFloatingWindows", side=1)
        for side in (1, 0):
            require("ShowFloatingWindows", side=side)
            require("AreFloatingWindowsVisible", status=1, side=1 - side)
            probe.call("IsWindowVisible", [floating])
            if side:
                save_word(scratch + 116)
            else:
                p.emit(0x809E0000 | scratch + 116)
                p.equal("a floating window is shown under the same conditions")
            require("HideFloatingWindows", side=side)
            require("AreFloatingWindowsVisible", status=0, side=1 - side)
            probe.call("IsWindowVisible", [floating])
            if side:
                save_word(scratch + 120)
            else:
                p.emit(0x809E0000 | scratch + 120)
                p.equal("a floating window is hidden under the same conditions")
        require("ShowFloatingWindows")

        for scope in (0, 1, 2, 3):
            require("SetWindowActivationScope", [floating, scope])
            compare("FrontNonFloatingWindow")
            compare("ActiveNonFloatingWindow")
            for window in (document, floating, hidden):
                compare("IsWindowActive", [window])

        for window in (document, floating, hidden):
            for activate in (0, 1):
                require("ActivateWindow", [window, activate])
                compare("IsWindowActive", [window])
                require("ActivateWindow", [window, activate], side=1)
                compare("IsWindowActive", [window])
        compare("GetWindowList")
        for window in (document, floating, hidden, 0):
            compare("GetNextWindow", [window])
        for side, window, offset in ((0, document, 96), (1, hidden, 104)):
            require("SetUserFocusWindow", [window], side=side)
            probe.call("GetUserFocusWindow", side=1 - side)
            p.emit(0x809E0000 | (scratch + offset))
            p.equal("window focus is shared by the native and saved APIs")
            require("SetUserFocusWindow", [0], -50, side)
        require("SetUserFocusWindow", [-1])
        compare("GetUserFocusWindow")
        ideal_user_state = probe.raw(struct.pack(">4h", 80, 60, 150, 180))
        dimensions = probe.raw(struct.pack(">2h", 60, 120))
        for side in (0, 1):
            require("SetWindowIdealUserState", [document, ideal_user_state], side=side)
            require("GetWindowIdealUserState", [document, probe.ptr(scratch + 124)], side=1 - side)
            for offset in (0, 4):
                compare_words(
                    scratch + 124 + offset, ideal_user_state[1] + offset, "ideal user-state bounds are shared"
                )
            require("GetWindowIdealUserState", [document, 0], -50, side)
            require("SetWindowIdealUserState", [0, ideal_user_state], -5600, side)
            require("IsWindowInStandardState", [0, dimensions, 0], 0, side)
            p.emit(0x38605A5A, 0xB07E0000 | (scratch + 136))
            require("GetWindowWidgetHilite", [document, probe.ptr(scratch + 134)], side=side)
            p.emit(0xA07E0000 | (scratch + 136))
            p.expect(0x5A5A, "widget hilite writes one guest word")
            p.emit(0xA07E0000 | (scratch + 134))
            save_word(scratch + 140 + 4 * side)
        compare_words(scratch + 140, scratch + 144, "widget hilite reads the same state")
        compare("IsWindowInStandardState", [document, dimensions, 0])
        compare("IsWindowInStandardState", [document, 0, 0])
        for side in (0, 1):
            require("GetWindowStructureWidths", [0, ideal_user_state], -5600, side)
            require("GetWindowStructureWidths", [document, 0], -50, side)
            output = scratch + 148 + 8 * side
            store_at = probe.ptr(output)
            probe.call("BlockMoveData", [ideal_user_state, store_at, 8])
            probe.call("GetWindowStructureWidths", [document, store_at], side)
            if side:
                p.emit(0x809E0000 | (scratch + 164))
                p.equal("native and saved structure-width queries return the same status")
            else:
                save_word(scratch + 164)
        for offset in (0, 4):
            compare_words(scratch + 148 + offset, scratch + 156 + offset, "matching window frame widths")
        structure_bounds = probe.raw(struct.pack(">4h", 100, 75, 225, 240))
        for side in (0, 1):
            require("SetWindowBounds", [0, 33, ideal_user_state], -5600, side)
            require("SetWindowBounds", [document, 34, ideal_user_state], -50, side)
            require("MoveWindowStructure", [0, 32, 48], -5600, side)
            require("SetWindowBounds", [document, 33, ideal_user_state], side=side)
            require("GetWindowBounds", [document, 33, probe.ptr(scratch + 124)])
            for offset in (0, 4):
                compare_words(scratch + 124 + offset, ideal_user_state[1] + offset, "content bounds follow the resize")
            require("MoveWindowStructure", [document, 57, 54], side=side)
            require("GetWindowBounds", [document, 32, probe.ptr(scratch + 124)])
            check_word(scratch + 124, (54 << 16) | 57, "move uses the structure's top-left corner")
            require("SetWindowBounds", [document, 32, structure_bounds], side=side)
            require("GetWindowBounds", [document, 32, probe.ptr(scratch + 124)])
            for offset in (0, 4):
                compare_words(
                    scratch + 124 + offset, structure_bounds[1] + offset, "structure bounds include the window frame"
                )
        for side, offset in ((0, 148), (1, 160)):
            device, screen = probe.ptr(scratch + offset), probe.ptr(scratch + offset + 4)
            require("GetWindowGreatestAreaDevice", [0, 32, device, screen], -5600, side)
            require("GetWindowGreatestAreaDevice", [document, 32, device, screen], side=side)
            p.emit(0x807E0000 | (scratch + offset))
            p.nonzero("the window intersects an available display")
            require("GetAvailableWindowPositioningBounds", [probe.slot(scratch + offset), 0], -50, side)
            require(
                "GetAvailableWindowPositioningBounds",
                [probe.slot(scratch + offset), probe.ptr(scratch + 176 + 8 * side)],
                side=side,
            )
        for offset in (0, 4, 8):
            compare_words(scratch + 148 + offset, scratch + 160 + offset, "matching window screen and screen bounds")
        for offset in (0, 4):
            compare_words(
                scratch + 176 + offset, scratch + 184 + offset, "matching menu-bar-adjusted positioning bounds"
            )
        require("IsWindowActive", [0], 0)
        require("ActivateWindow", [0, 1], -5600)
        color_output = probe.ptr(scratch + 144)
        for side, rgb in ((0, (0x1234, 0x5678, 0x9ABC)), (1, (0x4567, 0x3210, 0xFEDC))):
            color_input = probe.raw(struct.pack(">3H", *rgb))
            require("GetWindowContentColor", [document, 0], -50, side)
            require("GetWindowContentColor", [0, color_output], -5600, side)
            require("SetWindowContentColor", [0, color_input], -5600, side)
            require("SetWindowContentColor", [document, color_input], side=side)
            p.emit(0x38605678, 0xB07E0000 | (scratch + 150))
            require("GetWindowContentColor", [document, color_output], side=1 - side)
            check_word(scratch + 144, (rgb[0] << 16) | rgb[1], "both APIs see the window's red and green components")
            check_word(scratch + 148, (rgb[2] << 16) | 0x5678, "the color query leaves the trailing guard intact")
        for window in (hidden, floating, document):
            probe.call("DisposeWindow", [window])
        p.emit(0x807E0000 | scratch + 108, 0x2C030000, 0)
        branch = len(p.code) - 1
        require("HideFloatingWindows")
        p.code[branch] = 0x40820000 | (len(p.code) - branch) * 4

    def save_import(name, output):
        if probe.original_only and name in probe.names:
            probe.load_oracle_vector(probe.names.index(name), 3)
            save_word(output)
            return
        index = p.imports.index(("CarbonLib", name, 2))
        at = p.import_base + 4 * index
        if at >= 0x8000:
            p.emit(0x3D9E0000 | ((at + 0x8000) >> 16), 0x806C0000 | (at & 0xFFFF))
        else:
            p.emit(0x807E0000 | at)
        save_word(output)

    def window_compatibility():
        bounds = probe.raw(struct.pack(">4h", 60, 40, 130, 180))
        caption = probe.raw(b"\x04Test")
        native = probe.ptr(scratch + 100)

        require("IsWindowModified", [0], 0)
        require("SetWindowModified", [0, 1], -5600)
        require("GetWindowVisualClass", [0, native], -5600)
        require("GetWindowGadgetDropThreshold", [0, native], -5600)
        require("SetWindowGadgetDropThreshold", [0, 17], -5600)
        probe.call("GetWindowProcess", [0, native])
        check_word(scratch + 100, 0, "unknown window process ID high word")
        check_word(scratch + 104, 0, "unknown window process ID low word")

        probe.call("GetWindowDoubleClickAction")
        save_word(scratch + 108)
        for action in (2, 1):
            probe.call("SetWindowDoubleClickAction", [action])
            require("GetWindowDoubleClickAction", status=action)
            probe.call("SetWindowDoubleClickAction", [4])
            require("GetWindowDoubleClickAction", status=action)
        probe.call("SetWindowDoubleClickAction", [probe.slot(scratch + 108)])

        for offset in (112, 124):
            probe.call("NewWindow", [0, bounds, caption, 0, 0, -1, 1, 0])
            p.nonzero("create a window for metadata checks")
            save_word(scratch + offset)
        window, other = (probe.slot(scratch + at) for at in (112, 124))
        require("GetWindowVisualClass", [window, native])
        check_word(scratch + 100, 6, "classic document visual class")
        require("IsWindowModified", [window], 0)
        for modified, threshold in ((1, 157), (0, 22)):
            require("SetWindowModified", [window, modified])
            require("IsWindowModified", [window], modified)
            require("IsWindowModified", [other], 0)
            require("SetWindowGadgetDropThreshold", [window, threshold])
            require("GetWindowGadgetDropThreshold", [window, native])
            p.emit(0x887E0000 | (scratch + 100))
            p.expect(threshold, "gadget drop threshold is written as a byte")
            require("GetWindowGadgetDropThreshold", [other, native])
            p.emit(0x887E0000 | (scratch + 100))
            p.expect(0, "the other window keeps its own threshold")
        require("GetCurrentProcess", [probe.ptr(scratch + 116)])
        probe.call("GetWindowProcess", [window, native])
        compare_words(scratch + 100, scratch + 116, "window process high word")
        compare_words(scratch + 104, scratch + 120, "window process low word")
        probe.call("DisposeWindow", [other])
        probe.call("DisposeWindow", [window])

    def classic_wait():
        event_offset = scratch + 152
        event = probe.ptr(event_offset)
        for side in (0, 1):
            require("WaitNextEvent", [0, event, 0, 0], side=side)
            p.emit(0xA07E0000 | event_offset)
            p.expect(0, "zero-mask wait returns a null event")

        probe.load_oracle_vector(probe.names.index("WaitNextEvent"), 12)
        p.emit(0x818C0004, 0x816CF148, 0x818CF150)
        p.emit(0x917E0000 | scratch + 124, 0x919E0000 | scratch + 128, 0x806C0000)
        save_word(scratch + 132)
        p.emit(0x806B0000)
        p.expect(0, "completed waits leave no active Carbon wait")

        probe.arguments([wait_vector], 0)
        p.emit(0x819E0000 | scratch + 128, 0x906C0000)
        for side in (0, 1):
            p.emit(0x819E0000 | scratch + 124, 0x38600001, 0x906C0000)
            require("WaitNextEvent", [0x12345678, event, 0x11223344, 0x55667788], 1, side=side)
            check_word(scratch + 136, 2, "Carbon delegate sees the incremented wait depth")
            check_word(scratch + 140, 0x5678, "wait narrows the event mask to 16 bits")
            check_word(scratch + 144, 0x11223344, "wait preserves the sleep value")
            check_word(scratch + 148, 0x55667788, "wait preserves the mouse region")
            p.emit(0xA07E0000 | event_offset)
            p.expect(0x5678, "wait forwards the caller's EventRecord")
            p.emit(0x819E0000 | scratch + 124, 0x806C0000)
            p.expect(1, "Carbon restores the original wait depth after the delegate")
        p.emit(0x819E0000 | scratch + 124, 0x38600000, 0x906C0000)
        p.emit(0x819E0000 | scratch + 128, 0x807E0000 | scratch + 132, 0x906C0000)

    def classic_conversion():
        record_offset = scratch + 152
        record = probe.ptr(record_offset)
        event = probe.slot(scratch)
        button = probe.raw(struct.pack(">3H", 1, 2, 3))
        key_parameters = probe.raw(struct.pack(">5I4B", 0, 0x1000, 0x1100, 0x35, 0x2F, 0, 0x1B, 0x2E, 0))

        def make_event(group, event_kind, slot=scratch):
            p.emit(0xC83E0000 | past)
            require("CreateEvent", [0, int.from_bytes(group, "big"), event_kind, 0, 0, 0, probe.ptr(slot)])

        for group, event_kind, mask in (
            (b"keyb", 1, 8),
            (b"keyb", 2, 32),
            (b"keyb", 3, 16),
            (b"wind", 1, 64),
            (b"wind", 5, 256),
            (b"wind", 40, 0x8000),
            (b"appl", 2, 0x8000),
            (b"eppc", 1, 1024),
            (b"vol ", 0x6469736B, 128),
        ):
            make_event(group, event_kind)
            for side in (0, 1):
                require("IsEventInMask", [event, mask], 1, side)
                require("IsEventInMask", [event, mask ^ 0xFFFF], 0, side)
            probe.call("ReleaseEvent", [event])

        make_event(b"mous", 1)
        for side in (0, 1):
            require("IsShowContextualMenuEvent", [event], 0, side)
        for index, matches in enumerate((1, 1, 0)):
            require("SetEventParameter", [event, 0x6B6D6F64, 0x6D61676E, 4, key_parameters])
            require("SetEventParameter", [event, 0x6D62746E, 0x6D62746E, 2, probe.ptr(button[1] + 2 * index)])
            for side in (0, 1):
                require("IsEventInMask", [event, 2], matches, side)
                require("IsShowContextualMenuEvent", [event], int(index == 1), side)
        require("SetEventParameter", [event, 0x6D62746E, 0x6D62746E, 2, button])
        for offset, matches in ((4, 1), (8, 0)):
            require("SetEventParameter", [event, 0x6B6D6F64, 0x6D61676E, 4, probe.ptr(key_parameters[1] + offset)])
            for side in (0, 1):
                require("IsShowContextualMenuEvent", [event], matches, side)
        probe.call("ReleaseEvent", [event])

        make_event(b"keyb", 1)
        for side in (0, 1):
            require("IsUserCancelEventRef", [event], 0, side)
        require("SetEventParameter", [event, 0x6B707276, 0x626F6F6C, 1, probe.ptr(key_parameters[1] + 20)])
        for key_code, key_char, expected in ((12, 21, 1), (16, 22, 0)):
            require("SetEventParameter", [event, 0x6B636F64, 0x6D61676E, 4, probe.ptr(key_parameters[1] + key_code)])
            require("SetEventParameter", [event, 0x6B636872, 0x54455854, 1, probe.ptr(key_parameters[1] + key_char)])
            require("SetEventParameter", [event, 0x6B6D6F64, 0x6D61676E, 4, key_parameters])
            for side in (0, 1):
                require("IsUserCancelEventRef", [event], expected, side)
        probe.call("CouldBeMenuKeyEvent", [0, event], side=1)
        save_word(scratch + 176)
        probe.call("CouldBeMenuKeyEvent", [0, event])
        p.emit(0x809E0000 | scratch + 176)
        p.equal("menu key eligibility uses the saved modifier policy")
        probe.call("ReleaseEvent", [event])
        make_event(b"keyb", 3)
        for side in (0, 1):
            require("CouldBeMenuKeyEvent", [0, event], 0, side)
        probe.call("ReleaseEvent", [event])

        make_event(b"ctst", 1)
        for side in (0, 1):
            p.emit(0x38600003, 0xB07E0000 | record_offset)
            require("ConvertEventRefToEventRecord", [0, record], 1, side)
            require("ConvertEventRefToEventRecord", [event, record], 0, side)
            p.emit(0xA07E0000 | record_offset)
            p.expect(0, "an unmapped Carbon class produces a null classic event")
            check_word(record_offset + 2, 0, "reset the previous classic message")
            p.emit(0x807E0000 | record_offset + 6)
            save_word(scratch + 176 + 4 * side)
        compare_words(scratch + 176, scratch + 180, "classic tick time matches the saved conversion")
        probe.call("ReleaseEvent", [event])

        shared("GetMainEventQueue", scratch + 4)
        queue = probe.slot(scratch + 4)
        for side in (0, 1):
            make_event(b"keyb", 1)
            require("PostEventToQueue", [queue, event, 2])
            probe.call("__FlushEvents", [8, 8], side)
            require("IsEventInQueue", [queue, event], 1)
            probe.call("__FlushEvents", [8, 0], side)
            require("IsEventInQueue", [queue, event], 0)
            probe.call("ReleaseEvent", [event])

        delegate = probe.raw(bytes(8))
        p.routines.append((delegate[1], [0x38600102, 0x4E800020]))
        for symbol, slot in (("__WaitMouseUp", 0xF134), ("__StillDown", 0xF138)):
            probe.load_oracle_vector(probe.names.index(symbol), 12)
            p.emit(0x818C0004, 0x816C0000 | slot, 0x818CF154, 0x806C0000)
            save_word(scratch + 176)
            p.emit(0x917E0000 | scratch + 180, 0x919E0000 | scratch + 184, 0x806B0000)
            save_word(scratch + 188)
            probe.arguments([delegate], 0)
            p.emit(0x817E0000 | scratch + 180, 0x906B0000)
            for side in (0, 1):
                require(symbol, [], 2, side)
                p.emit(0x809E0000 | scratch + 184, 0x80640000, 0x809E0000 | scratch + 176)
                p.equal("classic mouse wait restores the nested dispatch depth")
            p.emit(0x817E0000 | scratch + 180, 0x807E0000 | scratch + 188, 0x906B0000)

        poll_delegate = probe.raw(bytes(8))
        p.routines.append((poll_delegate[1], [0xB0640000, 0x38600001, 0x4E800020]))
        for symbol, slot in (("__GetOSEvent", 0xF13C), ("__OSEventAvail", 0xF144), ("__EventAvail", 0xF140)):
            probe.load_oracle_vector(probe.names.index(symbol), 12)
            p.emit(0x818C0004, 0x816C0000 | slot, 0x806B0000)
            save_word(scratch + 176)
            p.emit(0x917E0000 | scratch + 180)
            probe.arguments([poll_delegate], 0)
            p.emit(0x817E0000 | scratch + 180, 0x906B0000)
            for side in (0, 1):
                p.emit(0x38600003, 0xB07E0000 | record_offset)
                require(symbol, [0x12340000, record], 1, side)
                p.emit(0xA07E0000 | record_offset)
                p.expect(0, "classic availability forwards the narrowed mask and output record")
            p.emit(0x817E0000 | scratch + 180, 0x807E0000 | scratch + 176, 0x906B0000)

    def handlers():
        for symbol, offset in (
            ("GetEventDispatcherTarget", 4),
            ("GetUserFocusEventTarget", 8),
            ("GetApplicationEventTarget", 12),
        ):
            shared(symbol, scratch + offset)
        target = probe.slot(scratch + 12)
        context = scratch + 64
        procedure = "CallNextEventHandler"
        save_import(procedure, context)
        probe.used.add(procedure)
        for side in (0, 1):
            create(scratch, side=side)
            create(scratch + 16, 2, side=1 - side)
            event = probe.slot(scratch)
            other = probe.slot(scratch + 16)
            for routine, handler, upp in ((second_callback, 20, 24), (first_callback, 28, 32)):
                probe.call("NewEventHandlerUPP", [probe.ptr(routine)])
                p.nonzero("created callback UPP")
                save_word(scratch + upp)
                require(
                    "InstallEventHandler",
                    [
                        target,
                        probe.slot(scratch + upp),
                        1,
                        probe.ptr(spec),
                        probe.ptr(context),
                        probe.ptr(scratch + handler),
                    ],
                    side=side,
                )
            require("SendEventToEventTarget", [event, target], side=1 - side)
            compare_words(context + 4, scratch, "later handler sees the original event")
            compare_words(context + 8, scratch, "first handler saw the original event before CallNextEventHandler")
            require("AddEventTypesToHandler", [probe.slot(scratch + 20), 1, probe.ptr(spec + 8)], side=side)
            require("SendEventToEventTarget", [other, target], side=side)
            compare_words(context + 4, scratch + 16, "new event type reaches the installed handler")
            require("RemoveEventTypesFromHandler", [probe.slot(scratch + 20), 1, probe.ptr(spec + 8)], side=1 - side)
            require("SendEventToEventTarget", [other, target], -9874, side=side)
            for handler, upp in ((28, 32), (20, 24)):
                require("RemoveEventHandler", [probe.slot(scratch + handler)], side=1 - side)
                probe.call("DisposeEventHandlerUPP", [probe.slot(scratch + upp)])
            require("SendEventToEventTarget", [event, target], -9874, side=side)
            probe.call("ReleaseEvent", [event], side=side)
            probe.call("ReleaseEvent", [other], side=1 - side)

    def application_loops():
        if probe.original_only:
            return
        context = probe.raw(bytes(16))
        procedure = probe.raw(bytes(8))
        timeout = probe.raw(struct.pack(">d", 0.05))
        p.routines.append(
            (procedure[1], [0x38600001, 0x90640004, 0x81840000, 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800420])
        )
        shared("GetMainEventLoop", context[1] + 8)
        loop = probe.slot(context[1] + 8)
        probe.call("NewEventLoopTimerUPP", [procedure])
        p.nonzero("created the application loop quit timer UPP")
        save_word(scratch + 84)
        upp = probe.slot(scratch + 84)
        # The saved loop's wait can spin when the host repeatedly wakes it for
        # work that leaves the queue ready. Exercise both quit APIs through the
        # native loop, which checks deadlines and services timers before waiting.
        for duration in (past + 24, timeout[1]):
            p.emit(0xC83E0000 | duration)
            require("RunCurrentEventLoop", [0, 0], -9875)
        for side in (0, 1):
            if side:
                probe.load_oracle_vector(probe.names.index("QuitApplicationEventLoop"), 3)
                save_word(context[1])
            else:
                save_import("QuitApplicationEventLoop", context[1])
                probe.used.add("QuitApplicationEventLoop")
            p.emit(0x38600000, 0x907E0000 | (context[1] + 4))
            p.emit(0xC83E0000 | (past + 24 if side else timeout[1]), 0xC85E0000 | past + 24)
            require("InstallEventLoopTimer", [loop, 0, 0, 0, 0, upp, context, probe.ptr(context[1] + 12)])
            probe.call("RunApplicationEventLoop")
            check_word(context[1] + 4, 1, "application loop dispatches its quit timer before returning")
            require("RemoveEventLoopTimer", [probe.slot(context[1] + 12)])
        probe.call("DisposeEventLoopTimerUPP", [upp])

    def run_due_timers(loop):
        # CarbonLib 1.4's timer service is also called by the saved event loop.
        # Invoke it directly so the test does not wait for host input events.
        probe.arguments([loop], 0)
        probe.load_oracle_vector(probe.names.index("InstallEventLoopTimer"), 12)
        p.emit(
            0x816C0000,
            0x396B0000 | ((0x41298 - 0x418D4) & 0xFFFF),
            0x804C0004,
            0x7D6903A6,
            0x4E800421,
            0x7FC2F378,
        )

    def timers():
        loop = probe.slot(scratch + 52)
        probe.call("NewEventLoopTimerUPP", [probe.ptr(timer_callback)])
        p.nonzero("created timer callback")
        save_word(scratch + 84)
        for side in (0, 1):
            p.emit(0xC83E0000 | past + 16, 0xC85E0000 | past + 24)
            require(
                "InstallEventLoopTimer",
                [loop, 0, 0, 0, 0, probe.slot(scratch + 84), probe.ptr(scratch + 88), probe.ptr(scratch + 80)],
                side=side,
            )
            p.emit(0x807E0000 | scratch + 80)
            p.nonzero("timer installation publishes shared timer")
            timer = probe.slot(scratch + 80)
            p.emit(0x38600000)
            save_word(scratch + 92)
            run_due_timers(loop)
            check_word(scratch + 92, 0, "a future timer does not fire prematurely")
            p.emit(0xC83E0000 | past + 24)
            require("SetEventLoopTimerNextFireTime", [timer, 0, 0], side=1 - side)
            run_due_timers(loop)
            compare_words(scratch + 92, scratch + 80, "saved event loop fires the rescheduled timer")
            require("RemoveEventLoopTimer", [timer], side=1 - side)
        probe.call("DisposeEventLoopTimerUPP", [probe.slot(scratch + 84)])

    def modal_windows():
        _, context = probe.raw(bytes(12))
        _, callback = probe.raw(bytes(8))
        p.routines.append(
            (
                callback,
                [
                    0x7C0802A6,  # mflr r0
                    0x90010008,  # stw r0,8(r1)
                    0x93E1FFFC,  # stw r31,-4(r1)
                    0x9421FFC0,  # stwu r1,-64(r1)
                    0x7CBF2B78,  # mr r31,r5
                    0x819F0000,  # lwz r12,0(r31) - QuitAppModalLoopForWindow
                    0x807F0004,  # lwz r3,4(r31)
                    0x800C0000,
                    0x804C0004,
                    0x7C0903A6,
                    0x4E800421,
                    0x907F0008,  # record the quit status
                    0x38210040,
                    0x83E1FFFC,
                    0x80010008,
                    0x7C0803A6,
                    0x4E800020,
                ],
            )
        )
        bounds = probe.raw(struct.pack(">4h", 60, 40, 130, 180))
        title = probe.raw(b"\x05Modal")
        window = probe.slot(context + 4)
        mode, unavailable = (probe.ptr(scratch + offset) for offset in (100, 104))
        for side in (0, 1):
            require("RunAppModalLoopForWindow", [0], -50, side)
            require("BeginAppModalStateForWindow", [0], -50, side)
            require("EndAppModalStateForWindow", [0], side=side)
            require("QuitAppModalLoopForWindow", [0], side=side)
            require("GetWindowModality", [0, mode, unavailable], -5600, side)
            require("SetWindowModality", [0, 1, 0], -5600, side)
        require("_RunAppModalLoopForWindow", [0], -50)
        require("_QuitAppModalLoopForWindow", [0])
        require("_BeginAppModalStateForWindow", [0], -50)
        require("_EndAppModalStateForWindow", [0])
        probe.call("NewWindow", [0, bounds, title, 0, 0, -1, 0, 0])
        p.nonzero("create a hidden window for modality checks")
        save_word(context + 4)
        require("BeginAppModalStateForWindow", [window], -5615)
        require("BeginAppModalStateForWindow", [window], -5615, side=1)
        for side in (0, 1):
            require("GetWindowModality", [window, 0, unavailable], -50, side)
            require("SetWindowModality", [window, 4, 0], -50, side)
            require("SetWindowModality", [window, 3, 0], -50, side)
            for kind, other in ((1, 0), (2, 0), (3, window), (0, 0)):
                require("SetWindowModality", [window, kind, other], side=side)
                require("GetWindowModality", [window, mode, unavailable], side=1 - side)
                check_word(scratch + 100, kind, "modality is visible across implementations")
                if other:
                    compare_words(scratch + 104, context + 4, "window-modal property preserves the other window")
                else:
                    check_word(scratch + 104, 0, "no unavailable window")
                p.emit(0x807E0000 | context + 4, 0x88630071, 0x70630080)
                p.expect(0x80 if kind == 1 else 0, "system-modal flag tracks the property")
            require("GetWindowModality", [window, mode, 0], side=side)
            check_word(scratch + 100, 0, "modality lookup accepts an omitted unavailable-window output")
        probe.call("ShowWindow", [window])
        probe.call("GetEventDispatcherTarget")
        save_word(scratch + 52)
        probe.call("GetMainEventQueue")
        save_word(scratch + 56)
        create(scratch + 88)
        probe.call("NewEventHandlerUPP", [probe.ptr(callback)])
        p.nonzero("create a modal-loop event handler")
        save_word(scratch + 84)
        require(
            "InstallEventHandler",
            [
                probe.slot(scratch + 52),
                probe.slot(scratch + 84),
                1,
                probe.ptr(spec),
                probe.ptr(context),
                probe.ptr(scratch + 80),
            ],
        )
        for side in (0, 1):
            quit_name = ("QuitAppModalLoopForWindow", "_QuitAppModalLoopForWindow")[side]
            require(quit_name, [window], side=side)
            save_import(quit_name, context)
            if side:
                require("_BeginAppModalStateForWindow", [window])
                require("BeginAppModalStateForWindow", [window], 10000, side=1)
            p.emit(0x3860FFFF)
            save_word(context + 8)
            require("PostEventToQueue", [probe.slot(scratch + 56), probe.slot(scratch + 88), 1])
            require(("RunAppModalLoopForWindow", "_RunAppModalLoopForWindow")[side], [window], side=side)
            check_word(context + 8, 0, "modal event handler quit the loop")
            require("GetWindowOwnerCount", [window, mode], side=1 - side)
            check_word(scratch + 100, 1, "leaving the modal loop preserves the caller's window ownership")
            if side:
                require("BeginAppModalStateForWindow", [window], 10000)
                require("_EndAppModalStateForWindow", [window])
            else:
                require("BeginAppModalStateForWindow", [window], side=1)
                require("EndAppModalStateForWindow", [window])
        require("RemoveEventHandler", [probe.slot(scratch + 80)])
        probe.call("DisposeEventHandlerUPP", [probe.slot(scratch + 84)])
        probe.call("ReleaseEvent", [probe.slot(scratch + 88)])
        if not probe.original_only:
            _, timer_callback = probe.raw(bytes(8))
            handler_code = p.routines[-1][1]
            p.routines.append((timer_callback, [*handler_code[:4], 0x7C9F2378, *handler_code[5:]]))
            probe.call("NewEventLoopTimerUPP", [probe.ptr(timer_callback)])
            p.nonzero("create a timer callback that exits the modal loop")
            save_word(scratch + 84)
            probe.call("GetCurrentEventLoop")
            save_word(scratch + 52)
            save_import("QuitAppModalLoopForWindow", context)
            _, delayed = probe.raw(struct.pack(">d", 0.05))
            for delay in (past + 24, delayed):
                p.emit(0x3860FFFF)
                save_word(context + 8)
                p.emit(0xC83E0000 | delay, 0xC85E0000 | past + 24)
                require(
                    "InstallEventLoopTimer",
                    [
                        probe.slot(scratch + 52),
                        0,
                        0,
                        0,
                        0,
                        probe.slot(scratch + 84),
                        probe.ptr(context),
                        probe.ptr(scratch + 80),
                    ],
                )
                require("RunAppModalLoopForWindow", [window])
                check_word(context + 8, 0, "timer exits the modal loop without posting an event")
                require("RemoveEventLoopTimer", [probe.slot(scratch + 80)])
            probe.call("DisposeEventLoopTimerUPP", [probe.slot(scratch + 84)])
        probe.call("DisposeWindow", [window])

    probe.section("Carbon event records and parameters", records)
    probe.section("Carbon event queues and loops", queues)
    probe.section("Carbon event retrieval and loop quit", receive_events)
    probe.section("Carbon mouse tracking and queued event ownership", mouse_tracking)
    probe.section("Carbon event queue search and flush", find_and_flush_events)
    probe.section("Carbon object event targets", object_targets)
    probe.section("Carbon control drag dispatch and window handler registration", control_drag_tracking)
    if not probe.original_only:
        probe.section("Drag Manager items and promised flavors", drag_data_operations)
        probe.section("Drag Manager input callbacks and region ownership", drag_tracking_operations)
        probe.section("Drag Manager feedback and zoom animations", drag_feedback_operations)
        for depth in (16, 32):
            probe.section(
                f"Drag Manager {depth}-bit highlight drawing and scrolling", lambda: drag_highlight_operations(depth)
            )
        probe.section("Drag Manager TextEdit selection region", drag_textedit_selection)
        probe.section("Drag Manager indexed drag imagery and alpha masks", drag_images)
    probe.section("Carbon window and control properties and designated buttons", window_properties_and_buttons)
    probe.section("Carbon regular and alternate window titles", window_titles)
    probe.section("Carbon window classes, attributes and activation scope", window_metadata)
    probe.section("Carbon window groups and z-order traversal", window_groups)
    probe.section("Carbon floating window visibility and activation", window_activation)
    if not probe.original_only:
        probe.section("Classic window metadata and title preferences", window_compatibility)
    probe.section("Carbon classic event wait and delegate bookkeeping", classic_wait)
    probe.section("Carbon event conversion, masks and classic dispatch", classic_conversion)
    probe.section("Carbon event targets and handler chains", handlers)
    probe.section("Carbon event loop timers", timers)
    probe.section("Carbon application event-loop dispatch and exit", application_loops)
    probe.section("Carbon application-modal loops and window modality", modal_windows)
