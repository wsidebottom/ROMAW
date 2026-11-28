/*+===================================================================
  File:      host_afp.h

  Summary:   Native AFP session interface.

  Classes:   None.

  Functions: Session lifecycle and AFP command declarations.

  Origin:    ROMAW native Apple Filing Protocol client.

  Copyright and Legal notices: See COPYING.
===================================================================+*/
#ifndef RSYS_HOST_AFP_H
#define RSYS_HOST_AFP_H
#include <stdint.h>

#define ROMAW_AFP_TRANSPORT_ERROR INT32_MIN

typedef struct romaw_afp_session romaw_afp_session;
romaw_afp_session* romaw_afp_connect(uint32_t address, uint16_t port, int32_t* error);
int32_t romaw_afp_command(romaw_afp_session* session, const void* request, unsigned size, void* reply,
                          unsigned capacity, unsigned* received);
int32_t romaw_afp_open_volume(romaw_afp_session* session, const char* name, uint16_t* volume);
int32_t romaw_afp_close_volume(romaw_afp_session* session, uint16_t volume);

typedef struct {
    unsigned count;

    struct {
        char name[256];
        uint8_t flags;
    } items[255];
} romaw_afp_volume_list;

int32_t romaw_afp_list_volumes(romaw_afp_session* session, romaw_afp_volume_list* volumes);

typedef struct {
    uint32_t node_id, parent_id;
    uint32_t created, modified, backup;
    uint32_t data_length, resource_length;
    uint16_t children;
    unsigned char finder_info[32];
    int directory;
    char name[256];
} romaw_afp_entry;

int32_t romaw_afp_enumerate(romaw_afp_session* session, uint16_t volume, uint32_t directory, uint32_t index,
                            romaw_afp_entry* entry);
int32_t romaw_afp_lookup(romaw_afp_session* session, uint16_t volume, uint32_t directory, const char* path,
                         unsigned length, romaw_afp_entry* entry);
int32_t romaw_afp_open_read_fork(romaw_afp_session* session, uint16_t volume, uint32_t directory, const char* name,
                                 int resource, uint16_t* fork, uint32_t* fork_length);
int32_t romaw_afp_read_fork(romaw_afp_session* session, uint16_t fork, uint32_t offset, void* data, unsigned length,
                            unsigned* received);
int32_t romaw_afp_close_fork(romaw_afp_session* session, uint16_t fork);
void romaw_afp_close(romaw_afp_session* session);

unsigned ROMlib_afp_chooser_services(char names[][256], unsigned capacity);
int ROMlib_afp_chooser_endpoint(const char* name, int advertised, uint32_t* address, uint16_t* port);
void ROMlib_afp_chooser_remembered(char* endpoint, unsigned capacity);
void ROMlib_afp_classic_chooser(void);
#endif
