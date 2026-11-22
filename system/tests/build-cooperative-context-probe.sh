#!/usr/bin/env bash
# +===================================================================
#   File:      build-cooperative-context-probe.sh
#
#   Summary:   Implements build cooperative context probe support for regression tests and test fixtures.
#
#   Classes:   None.
#
#   Functions: No explicitly exported functions.
#
#   Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
# ===================================================================+

set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
cd "$root"
printf '.text\n.globl exchange_context\nexchange_context:\n.word 0xabfe,0x4e75\n.section .note.GNU-stack,"",@progbits\n' >build/cooperative-context-abi.s
m68k-linux-gnu-gcc -std=c17 -Os -m68000 -mpcrel -ffreestanding -fno-builtin -fomit-frame-pointer -fno-stack-protector -c system/tests/cooperative_context_m68k.c -o build/cooperative-context-probe.o
m68k-linux-gnu-as -m68000 build/cooperative-context-abi.s -o build/cooperative-context-abi.o
printf 'SECTIONS { . = 0; .text : { *(.text.probe) *(.text*) } /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) } }\n' >build/cooperative-context.ld
m68k-linux-gnu-ld -T build/cooperative-context.ld build/cooperative-context-probe.o build/cooperative-context-abi.o -o build/cooperative-context-probe.elf
m68k-linux-gnu-objcopy -O binary build/cooperative-context-probe.elf build/cooperative-context-probe.bin
m68k-linux-gnu-readelf -r build/cooperative-context-probe.elf
