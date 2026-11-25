/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Closed physical cache roots. Normal RAM is accessed in the emitted body. */
#include "native040.h"

typedef struct {
    const Syn68kCache040Bus* original;
    const Syn68kNative040RAM* ram;
    Syn68kCache040Bus bus;
    unsigned push_ram;
} NativeCache;

static unsigned char* ram_bytes(const NativeCache* binding, uint32_t address, unsigned size)
{
    const Syn68kNative040RAM* ram = binding->ram;
    if (!ram->bytes || !ram->size || ram->size > UINT64_C(0x100000000) - ram->address || address < ram->address)
        return 0;
    uint64_t delta = (uint64_t)address - ram->address;
    return delta <= ram->size && size <= ram->size - delta ? ram->bytes + delta : 0;
}

static int read_physical(void* opaque, uint32_t address, unsigned char* value, unsigned size, uint32_t attributes,
                         unsigned fc, unsigned burst, unsigned beat, unsigned way, unsigned* inhibit)
{
    NativeCache* binding = opaque;
    const unsigned char* bytes = fc == 1 || fc == 2 || fc == 5 || fc == 6 ? ram_bytes(binding, address, size) : 0;
    if (bytes) {
        for (unsigned i = 0; i < size; ++i)
            value[i] = bytes[i];
        *inhibit = 0;
        return 1;
    }
    const Syn68kCache040Bus* bus = binding->original;
    return bus && bus->read && bus->read(bus->opaque, address, value, size, attributes, fc, burst, beat, way, inhibit);
}

static int write_physical(void* opaque, uint32_t address, const unsigned char* value, unsigned size,
                          uint32_t attributes, unsigned fc, unsigned burst, unsigned beat, unsigned way)
{
    NativeCache* binding = opaque;
    unsigned char* bytes =
        fc == 1 || fc == 2 || fc == 5 || fc == 6 || (!fc && binding->push_ram) ? ram_bytes(binding, address, size) : 0;
    if (bytes) {
        for (unsigned i = 0; i < size; ++i)
            bytes[i] = value[i];
        return 1;
    }
    const Syn68kCache040Bus* bus = binding->original;
    return bus && bus->write && bus->write(bus->opaque, address, value, size, attributes, fc, burst, beat, way);
}

static const Syn68kCache040Bus* bind_bus(Syn68kNative040Cache* packet, NativeCache* binding, unsigned push_ram)
{
    if (!packet->ram.bytes || !packet->ram.size || packet->ram.size > UINT64_C(0x100000000) - packet->ram.address)
        return packet->bus;
    binding->original = packet->bus;
    binding->ram = &packet->ram;
    binding->push_ram = push_ram;
    binding->bus = (Syn68kCache040Bus){binding, read_physical, write_physical};
    return &binding->bus;
}

__attribute__((used, noinline)) void native040_cache_access(Syn68kNative040Cache* packet)
{
    NativeCache binding;
    unsigned ordinary = packet->fc == 1 || packet->fc == 2 || packet->fc == 5 || packet->fc == 6;
    packet->access = syn68k_cache040_access(packet->cache, bind_bus(packet, &binding, ordinary), packet->address,
                                            packet->fc, packet->size, packet->writing, packet->value,
                                            packet->attributes, packet->cacr, packet->no_allocate);
}

__attribute__((used, noinline)) void native040_cache_maintain(Syn68kNative040Cache* packet)
{
    NativeCache binding;
    packet->access = syn68k_cache040_maintain(packet->cache, bind_bus(packet, &binding, 1), packet->caches,
                                              packet->scope, packet->address, packet->page_shift, packet->push);
}
