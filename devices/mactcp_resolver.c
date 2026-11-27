/*+===================================================================
  File:      mactcp_resolver.c

  Summary:   Implement all MacTCP DNR selectors, asynchronous results, cache enumeration and optional Hosts files.

  Classes:   None.

  Functions: ROMlib_mactcp_resolver_definition, ROMlib_mactcp_resolver_poll, ROMlib_mactcp_resolver_reset.

  Origin:    ROMAW implementation of the public AddressXlation.h and DNR selector ABI.

  Copyright and Legal notices: See COPYING.
===================================================================+*/
#include "rsys/common.h"
#include "rsys/mactcp.h"
#include "rsys/host_mactcp.h"
#include "rsys/host_mactcp_dns.h"
#include "rsys/glue.h"
#include "MemoryMgr.h"
#include "FileMgr.h"
#include "ToolboxEvent.h"
#include <ctype.h>

enum { resolver_pending_limit = 32, resolver_cache_limit = 32, hosts_record_limit = 1024 };

typedef struct local_record {
    struct local_record* next;
    host_mactcp_dns_record record;
    char cpu[30], os[30];
} local_record;

typedef struct {
    unsigned valid;
    uint16 type;
    uint32 inserted, ttl;
    char query[255];
    host_mactcp_dns_reply* reply;
} cached_result;

typedef struct {
    host_mactcp_dns_request* host;
    uint64 serial;
    uint16 type;
    uint32 output, callback, user, saved_world, address;
    char query[255];
} pending_result;

static unsigned users;
static uint64 generation, next_serial;
static local_record* local_records;
static unsigned local_count;
static char hosts_filename[256];
static cached_result cache[resolver_cache_limit];
static pending_result pending[resolver_pending_limit];

static uint16 read16(const void* p)
{
    uint16 value;
    memcpy(&value, p, 2);
    return CW(value);
}

static uint32 read32(const void* p)
{
    uint32 value;
    memcpy(&value, p, 4);
    return CL(value);
}

static void write16(void* p, uint16 value)
{
    value = CW(value);
    memcpy(p, &value, 2);
}

static void write32(void* p, uint32 value)
{
    value = CL(value);
    memcpy(p, &value, 4);
}

static void copy_string(char* output, size_t size, const char* input)
{
    size_t length = strlen(input);
    if (length >= size)
        length = size - 1;
    memcpy(output, input, length);
    output[length] = 0;
}

/* DNS case folding is ASCII and independent of the host user's locale.
   A trailing root label does not change a fully qualified name's identity. */
static int dns_compare(const char* left, const char* right)
{
    size_t left_length = strlen(left), right_length = strlen(right);
    if (left_length > 1 && left[left_length - 1] == '.')
        --left_length;
    if (right_length > 1 && right[right_length - 1] == '.')
        --right_length;
    for (size_t i = 0; i < MIN(left_length, right_length); ++i) {
        unsigned char a = left[i], b = right[i];
        if (a >= 'A' && a <= 'Z')
            a += 'a' - 'A';
        if (b >= 'A' && b <= 'Z')
            b += 'a' - 'A';
        if (a != b)
            return (int)a - b;
    }
    return left_length < right_length ? -1 : left_length != right_length;
}

static int guest_string(uint32 address, char output[256])
{
    for (unsigned i = 0; i < 256; ++i) {
        const char* byte = ROMlib_mactcp_memory(address + i, 1, FALSE);
        if (!byte)
            return paramErr;
        output[i] = *byte;
        if (!*byte)
            return noErr;
    }
    return -23041;
}

static int ipv4(const char* text, uint32* address)
{
    uint32 result = 0;
    for (unsigned i = 0; i < 4; ++i) {
        unsigned value = 0, digits = 0;
        while (*text >= '0' && *text <= '9') {
            value = value * 10 + *text++ - '0';
            if (++digits > 3 || value > 255)
                return FALSE;
        }
        if (!digits || (i < 3 ? *text++ != '.' : *text != 0))
            return FALSE;
        result = result << 8 | value;
    }
    *address = result;
    return TRUE;
}

static void clear_state(void)
{
    for (unsigned i = 0; i < resolver_pending_limit; ++i) {
        host_mactcp_dns_dispose(pending[i].host);
        memset(&pending[i], 0, sizeof pending[i]);
    }
    for (unsigned i = 0; i < resolver_cache_limit; ++i) {
        free(cache[i].reply);
        memset(&cache[i], 0, sizeof cache[i]);
    }
    while (local_records) {
        local_record* next = local_records->next;
        free(local_records);
        local_records = next;
    }
    local_count = 0;
    hosts_filename[0] = 0;
    ++generation;
}

void ROMlib_mactcp_resolver_reset(void)
{
    clear_state();
    while (users) {
        host_mactcp_resolver_close();
        --users;
    }
}

static int add_local(const char* name, unsigned type, const char* target, uint32 address, unsigned preference,
                     const char* cpu, const char* os)
{
    if (local_count == hosts_record_limit)
        return -23009;
    if (!*name || strlen(name) > 253 || (target && (!*target || strlen(target) > 253)))
        return -23041;
    local_record* item = calloc(1, sizeof *item);
    if (!item)
        return -23048;
    item->record.type = type;
    item->record.address = address;
    item->record.preference = preference;
    copy_string(item->record.name, sizeof item->record.name, name);
    if (target)
        copy_string(item->record.target, sizeof item->record.target, target);
    if (cpu)
        copy_string(item->cpu, sizeof item->cpu, cpu);
    if (os)
        copy_string(item->os, sizeof item->os, os);
    item->next = local_records;
    local_records = item;
    ++local_count;
    return 0;
}

static int parse_hosts(char* contents)
{
    char* line = contents;
    while (*line) {
        char* end = line;
        while (*end && *end != '\n' && *end != '\r')
            ++end;
        char* next = end;
        while (*next == '\n' || *next == '\r')
            ++next;
        if (*end)
            *end = 0;
        if (end - line > 2048)
            return -23041;
        char* words[64];
        unsigned count = 0;
        char* p = line;
        while (*p) {
            while (isspace((unsigned char)*p))
                ++p;
            if (!*p || *p == '#' || *p == ';')
                break;
            if (count == NELEM(words))
                return -23009;
            char quote = *p == '"' ? *p++ : 0;
            words[count++] = p;
            char* output = p;
            while (*p && (quote ? *p != quote : !isspace((unsigned char)*p) && *p != '#' && *p != ';')) {
                if (*p == '\\' && p[1])
                    ++p;
                *output++ = *p++;
            }
            if (quote) {
                if (*p != quote)
                    return -23041;
                ++p;
            }
            else if (*p == '#' || *p == ';') {
                *output = 0;
                break;
            }
            char* after = *p ? p + 1 : p;
            *output = 0;
            p = after;
        }
        if (count) {
            uint32 address;
            int error = 0;
            if (ipv4(words[0], &address)) {
                if (count < 2)
                    return -23041;
                for (unsigned i = 1; i < count && !error; ++i)
                    error = add_local(words[i], 1, words[1], address, 0, NULL, NULL);
            }
            else if (strchr(words[0], ':')) {
                /* IPv6 Hosts entries are outside MacTCP's IPv4 interface. */
            }
            else {
                unsigned at = count > 1 && !dns_compare(words[1], "IN") ? 2 : 1;
                if (count <= at + 1)
                    return -23041;
                if (!dns_compare(words[at], "A")) {
                    if (!ipv4(words[at + 1], &address))
                        return -23041;
                    error = add_local(words[0], 1, words[0], address, 0, NULL, NULL);
                }
                else if (!dns_compare(words[at], "CNAME") || !dns_compare(words[at], "NS") ||
                         !dns_compare(words[at], "PTR")) {
                    unsigned type = !dns_compare(words[at], "CNAME") ? 5 : !dns_compare(words[at], "NS") ? 2 : 12;
                    error = add_local(words[0], type, words[at + 1], 0, 0, NULL, NULL);
                }
                else if (!dns_compare(words[at], "HINFO")) {
                    if (count <= at + 2)
                        return -23041;
                    error = add_local(words[0], 13, NULL, 0, 0, words[at + 1], words[at + 2]);
                }
                else if (!dns_compare(words[at], "MX")) {
                    if (count <= at + 2)
                        return -23041;
                    char* rest;
                    unsigned long preference = strtoul(words[at + 1], &rest, 10);
                    if (*rest || preference > 65535)
                        return -23041;
                    error = add_local(words[0], 15, words[at + 2], 0, preference, NULL, NULL);
                }
            }
            if (error)
                return error;
        }
        line = next;
    }
    return 0;
}

static int load_hosts(const char* filename)
{
    unsigned char name[256];
    size_t length = strlen(filename);
    if (length > 255)
        return bdNamErr;
    name[0] = length;
    memcpy(name + 1, filename, length);
    ParamBlockRec pb;
    memset(&pb, 0, sizeof pb);
    PP_ASSIGN(pb.ioParam.ioNamePtr, (StringPtr)name);
    pb.ioParam.ioPermssn = fsRdPerm;
    int error = PBOpen(&pb, FALSE);
    if (error)
        return error;
    INTEGER reference = pb.ioParam.ioRefNum;
    char* contents = malloc(1024 * 1024 + 1);
    if (!contents)
        error = -23048;
    else {
        pb.ioParam.ioRefNum = reference;
        PP_ASSIGN(pb.ioParam.ioBuffer, contents);
        pb.ioParam.ioReqCount = CL(1024 * 1024);
        pb.ioParam.ioPosMode = CW(fsFromStart);
        pb.ioParam.ioPosOffset = 0;
        error = PBRead(&pb, FALSE);
        uint32 used = CL(pb.ioParam.ioActCount);
        if (used > 1024 * 1024 || (!error && used == 1024 * 1024))
            error = -23006;
        if (!error || error == eofErr) {
            contents[used] = 0;
            error = memchr(contents, 0, used) ? -23041 : parse_hosts(contents);
        }
        free(contents);
    }
    pb.ioParam.ioRefNum = reference;
    int saved_error = error;
    int close_error = PBClose(&pb, FALSE);
    return saved_error ? saved_error : close_error;
}

static int local_lookup(char query[255], unsigned type, uint32 address, host_mactcp_dns_reply* reply)
{
    memset(reply, 0, sizeof *reply);
    if (type == 12)
        for (local_record* item = local_records; item; item = item->next)
            if (item->record.type == 1 && item->record.address == address) {
                copy_string(reply->canonical, sizeof reply->canonical,
                            item->record.target[0] ? item->record.target : item->record.name);
                reply->addresses[0] = address;
                return 0;
            }
    for (unsigned depth = 0; depth < 16; ++depth) {
        unsigned found = 0, addresses = 0;
        const char* alias = NULL;
        for (local_record* item = local_records; item; item = item->next) {
            if (dns_compare(query, item->record.name))
                continue;
            if (item->record.type == 5)
                alias = item->record.target;
            if (item->record.type != type)
                continue;
            if (type == 1) {
                unsigned i;
                for (i = 0; i < addresses && reply->addresses[i] != item->record.address; ++i)
                    ;
                if (i == addresses && addresses < 4)
                    reply->addresses[addresses++] = item->record.address;
                copy_string(reply->canonical, sizeof reply->canonical, item->record.target);
                found = 1;
            }
            else if (type == 12) {
                copy_string(reply->canonical, sizeof reply->canonical, item->record.target);
                found = 1;
            }
            else if (type == 13) {
                copy_string(reply->canonical, sizeof reply->canonical, query);
                memcpy(reply->cpu, item->cpu, sizeof reply->cpu);
                memcpy(reply->os, item->os, sizeof reply->os);
                found = 1;
            }
            else if (type == 15 && (!found || item->record.preference < reply->preference)) {
                copy_string(reply->canonical, sizeof reply->canonical, query);
                copy_string(reply->exchange, sizeof reply->exchange, item->record.target);
                reply->preference = item->record.preference;
                found = 1;
            }
        }
        if (found)
            return 0;
        if (!alias)
            return -23042;
        copy_string(query, 255, alias);
    }
    return -23041;
}

static int cache_alive(const cached_result* item)
{
    return item->valid && (uint32)(TickCount() - item->inserted) / 60 < item->ttl;
}

static void cache_reply(const char* query, unsigned type, const host_mactcp_dns_reply* reply)
{
    if (reply->error || !reply->record_count)
        return;
    uint32 ttl = 86400;
    for (unsigned i = 0; i < reply->record_count; ++i)
        ttl = MIN(ttl, reply->records[i].ttl);
    if (!ttl)
        return;
    cached_result* chosen = &cache[0];
    for (unsigned i = 0; i < resolver_cache_limit; ++i) {
        if (cache[i].valid && cache[i].type == type && !dns_compare(cache[i].query, query)) {
            chosen = &cache[i];
            break;
        }
        if (!cache_alive(&cache[i])) {
            chosen = &cache[i];
            break;
        }
        if ((uint32)(TickCount() - cache[i].inserted) > (uint32)(TickCount() - chosen->inserted))
            chosen = &cache[i];
    }
    if (!chosen->reply)
        chosen->reply = malloc(sizeof *chosen->reply);
    if (!chosen->reply)
        return;
    *chosen->reply = *reply;
    chosen->valid = 1;
    chosen->type = type;
    chosen->inserted = TickCount();
    chosen->ttl = ttl;
    copy_string(chosen->query, sizeof chosen->query, query);
}

static unsigned result_size(unsigned type)
{
    return type == 13 ? 320 : type == 15 ? 518 : 276;
}

static int write_reply(uint32 output, unsigned type, uint32 address, const host_mactcp_dns_reply* reply)
{
    unsigned size = result_size(type);
    unsigned char* bytes = ROMlib_mactcp_memory(output, size, TRUE);
    if (!bytes)
        return paramErr;
    memset(bytes, 0, size);
    write32(bytes, (int32)reply->error);
    memcpy(bytes + 4, reply->canonical, 255);
    if (!reply->error) {
        if (type == 1 || type == 12)
            for (unsigned i = 0; i < 4; ++i)
                write32(bytes + 260 + i * 4, type == 12 && !i ? address : reply->addresses[i]);
        else if (type == 13) {
            memcpy(bytes + 260, reply->cpu, 30);
            memcpy(bytes + 290, reply->os, 30);
        }
        else if (type == 15) {
            write16(bytes + 260, reply->preference);
            memcpy(bytes + 262, reply->exchange, 255);
        }
    }
    return reply->error;
}

void ROMlib_mactcp_resolver_poll(void)
{
    struct {
        unsigned index;
        uint64 serial;
    } snapshot[resolver_pending_limit];

    unsigned count = 0;
    for (unsigned i = 0; i < resolver_pending_limit; ++i)
        if (pending[i].host) {
            snapshot[count].index = i;
            snapshot[count++].serial = pending[i].serial;
        }
    for (unsigned i = 0; i < count; ++i) {
        pending_result* item = &pending[snapshot[i].index];
        if (!item->host || item->serial != snapshot[i].serial)
            continue;
        int error = host_mactcp_dns_result(item->host, NULL);
        if (error == 1)
            continue;
        host_mactcp_dns_reply* reply = calloc(1, sizeof *reply);
        if (!reply)
            error = -23048;
        else if (!error)
            error = host_mactcp_dns_result(item->host, reply);
        pending_result completed = *item;
        memset(item, 0, sizeof *item);
        host_mactcp_dns_dispose(completed.host);
        if (reply) {
            if (error)
                reply->error = error;
            cache_reply(completed.query, completed.type, reply);
            write_reply(completed.output, completed.type, completed.address, reply);
        }
        else {
            unsigned char* result = ROMlib_mactcp_memory(completed.output, 4, TRUE);
            if (result)
                write32(result, (int32)error);
        }
        if (ROMlib_mactcp_memory(completed.output, result_size(completed.type), TRUE)) {
            uint32 saved_world = EM_A5;
            EM_A5 = completed.saved_world;
            CToPascalCall(SYN68K_TO_US(completed.callback), (5 << 3) | (4 << 6), SYN68K_TO_US(completed.output),
                          completed.user);
            EM_A5 = saved_world;
        }
        free(reply);
    }
}

static int enumerate_cache(uint32 callback, uint32 user)
{
    if (!callback || !ROMlib_mactcp_memory(callback, 2, FALSE))
        return -23043;
    unsigned capacity = hosts_record_limit + resolver_cache_limit * HOST_MACTCP_DNS_RECORDS;
    host_mactcp_dns_record* records = calloc(capacity, sizeof *records);
    if (!records)
        return -23048;
    unsigned count = 0;
    for (local_record* item = local_records; item; item = item->next)
        if (item->record.type == 1 || item->record.type == 2 || item->record.type == 5 || item->record.type == 12)
            records[count++] = item->record;
    for (unsigned i = 0; i < resolver_cache_limit; ++i) {
        if (!cache_alive(&cache[i]))
            continue;
        uint32 age = (uint32)(TickCount() - cache[i].inserted) / 60;
        for (unsigned j = 0; j < cache[i].reply->record_count; ++j) {
            host_mactcp_dns_record record = cache[i].reply->records[j];
            if (age >= record.ttl)
                continue;
            record.ttl -= age;
            unsigned found;
            for (found = 0; found < count; ++found)
                if (records[found].type == record.type && records[found].address == record.address &&
                    !dns_compare(records[found].name, record.name) &&
                    !dns_compare(records[found].target, record.target))
                    break;
            if (found == count)
                records[count++] = record;
        }
    }
    Ptr scratch = NewPtrSysClear(16 + 255 + 255);
    if (!scratch) {
        free(records);
        return -23048;
    }
    uint64 saved_generation = generation;
    for (unsigned i = 0; i < count; ++i) {
        write32(scratch, US_TO_SYN68K(scratch + 16));
        write16(scratch + 4, records[i].type);
        write16(scratch + 6, 1);
        write32(scratch + 8, records[i].ttl);
        write32(scratch + 12, records[i].type == 1 ? records[i].address : US_TO_SYN68K(scratch + 271));
        memcpy(scratch + 16, records[i].name, 255);
        memcpy(scratch + 271, records[i].target, 255);
        CToPascalCall(SYN68K_TO_US(callback), (5 << 3) | (4 << 6), scratch, user);
        if (generation != saved_generation)
            break;
    }
    DisposPtr(scratch);
    free(records);
    return noErr;
}

static int lookup_result(unsigned type, uint32 argument, uint32 output, uint32 callback, uint32 user)
{
    if (!ROMlib_mactcp_memory(output, result_size(type), TRUE))
        return paramErr;
    char query[256], original[255];
    uint32 address = type == 12 ? argument : 0;
    int error;
    if (type == 12) {
        snprintf(query, sizeof query, "%u.%u.%u.%u.in-addr.arpa", address & 255, (address >> 8) & 255,
                 (address >> 16) & 255, address >> 24);
        error = 0;
    }
    else
        error = guest_string(argument, query);
    if (error)
        return error;
    if (!*query || strlen(query) > 253)
        return -23041;
    copy_string(original, sizeof original, query);
    host_mactcp_dns_reply* reply = calloc(1, sizeof *reply);
    if (!reply)
        return -23048;
    error = local_lookup(query, type, address, reply);
    if (error == -23042)
        for (unsigned i = 0; i < resolver_cache_limit; ++i)
            if (cache_alive(&cache[i]) && cache[i].type == type && !dns_compare(cache[i].query, original)) {
                *reply = *cache[i].reply;
                error = 0;
                break;
            }
    if (error == -23042)
        error = host_mactcp_dns_cached(query, type, reply);
    if (error == -23042) {
        pending_result* item = NULL;
        if (!callback || !ROMlib_mactcp_memory(callback, 2, FALSE))
            error = -23043;
        else {
            for (unsigned i = 0; i < resolver_pending_limit; ++i)
                if (!pending[i].host) {
                    item = &pending[i];
                    break;
                }
            if (!item)
                error = -23009;
            else {
                item->host = host_mactcp_dns_start(query, type, &error);
                if (item->host) {
                    item->serial = ++next_serial;
                    item->type = type;
                    item->output = output;
                    item->callback = callback;
                    item->user = user;
                    item->address = address;
                    item->saved_world = EM_A5;
                    copy_string(item->query, sizeof item->query, original);
                    error = -23042;
                }
            }
        }
    }
    else if (!error)
        cache_reply(original, type, reply);
    reply->error = error;
    write_reply(output, type, address, reply);
    free(reply);
    return error;
}

static int argument_at(uint32 address, uint32* argument)
{
    const void* bytes = ROMlib_mactcp_memory(address, 4, FALSE);
    if (!bytes)
        return paramErr;
    *argument = read32(bytes);
    return 0;
}

static syn68k_addr_t resolver_callback(syn68k_addr_t pc, void* unused)
{
    (void)pc;
    (void)unused;
    const unsigned char* stack = ROMlib_mactcp_memory(EM_A7, 8, FALSE);
    int error = paramErr;
    if (stack) {
        unsigned selector = read32(stack + 4);
        unsigned argument_offset = 8;
        if (selector < 1 || selector > 8) {
            selector = read16(stack + 4);
            argument_offset = 6;
        }
        uint32 args[4] = {0};
        unsigned count = selector == 2 ? 0 : selector == 1 ? 1 : selector == 4 || selector == 5 ? 2 : 4;
        error = 0;
        for (unsigned i = 0; i < count && !error; ++i)
            error = argument_at(EM_A7 + argument_offset + i * 4, &args[i]);
        if (!error) {
            if (selector == 1) {
                char filename[256] = {0};
                if (args[0])
                    error = guest_string(args[0], filename);
                if (!error && users && filename[0] && strcmp(filename, hosts_filename))
                    error = paramErr;
                if (!error && users >= 65535)
                    error = -23009;
                if (!error)
                    error = host_mactcp_resolver_open();
                if (!error) {
                    if (!users && filename[0])
                        error = load_hosts(filename);
                    if (error) {
                        clear_state();
                        host_mactcp_resolver_close();
                    }
                    else {
                        ++users;
                        if (filename[0])
                            copy_string(hosts_filename, sizeof hosts_filename, filename);
                    }
                }
            }
            else if (selector == 2) {
                if (users) {
                    --users;
                    host_mactcp_resolver_close();
                    if (!users)
                        clear_state();
                }
            }
            else if (selector == 4) {
                char* output = ROMlib_mactcp_memory(args[1], 16, TRUE);
                if (!output)
                    error = paramErr;
                else
                    snprintf(output, 16, "%u.%u.%u.%u", args[0] >> 24, (args[0] >> 16) & 255, (args[0] >> 8) & 255,
                             args[0] & 255);
            }
            else if (!users)
                error = -23047;
            else if (selector == 5)
                error = enumerate_cache(args[0], args[1]);
            else if (selector == 3 || selector == 6 || selector == 7 || selector == 8)
                error = lookup_result(selector == 3   ? 1
                                      : selector == 6 ? 12
                                      : selector == 7 ? 13
                                                      : 15,
                                      args[0], args[1], args[2], args[3]);
            else
                error = paramErr;
        }
    }
    EM_D0 = (int32)error;
    return POPADDR();
}

ProcPtr ROMlib_mactcp_resolver_definition(void)
{
    static syn68k_addr_t entry;
    if (!entry)
        entry = callback_install(resolver_callback, NULL);
    return (ProcPtr)SYN68K_TO_US(entry);
}
