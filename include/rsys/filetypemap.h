/*+===================================================================
  File:      filetypemap.h

  Summary:   ifndef RSYS_FILETYPEMAP_H

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#ifndef RSYS_FILETYPEMAP_H
#define RSYS_FILETYPEMAP_H
#include <stdint.h>
#include <stddef.h>
#define FILETYPE_MAP_LINE_CAP 24576
#ifdef __cplusplus
extern "C" {
#endif
typedef struct FileTypeMapping {
    char extension[65], description[1024];
    char application[4096];
    uint32_t type, creator;
    int strip, to_host, to_mac, open_mac;
} FileTypeMapping;

/* HFVExplorer CSV: extension,strip,type,creator,to_host,to_mac,description.
   ROMAW adds optional launch (host/default or mac) and quoted application
   path columns. Association-only rows may have an empty extension.
   Decimal \ddd byte escapes and embedded commas in four-byte codes are supported.
   Short codes in the historical ROMAW starter database are space padded. */
int filetype_map_parse(const char* line, FileTypeMapping* mapping);
int filetype_map_format(const FileTypeMapping* mapping, char* out, size_t capacity);
int filetype_map_record(const char* line, char extension[65], uint32_t* type, uint32_t* creator);
/* Launch preference is independent of copy/import direction flags. */
/* Read the current per-user FTM for DOS-to-Mac metadata, without altering the file. */
int filetype_map_lookup_path(const char* extension, FileTypeMapping* mapping);
/* Kind descriptions are independent of import/export settings. An extension
   must agree with the existing Macintosh identity when one is supplied. */
int filetype_map_description_path(const char* extension, uint32_t type, uint32_t creator, char* out, size_t capacity);
int filetype_map_open_mac(const char* text, const char* extension);
int filetype_map_application(const char* text, uint32_t type, uint32_t creator, char* application, size_t capacity);
/* Persist BNDL/FREF ownership. native_path means host ANSI on Windows;
     otherwise the application path is UTF-8. Returns zero on storage errors. */
int filetype_map_register_application(const char* application, uint32_t creator, const uint32_t* types, size_t count,
                                      int native_path);
int filetype_map_application_path(uint32_t type, uint32_t creator, char* application, size_t capacity, int native_path);
/* Last enabled match wins, independently for each direction. */
int filetype_map_lookup(const char* text, const char* extension, uint32_t type, uint32_t creator, int to_mac,
                        FileTypeMapping* mapping);
/* A zero type requires one unambiguous document type for this creator.
     Suggestions include the dot and never rename the user's file. */
int filetype_map_extension(const char* text, uint32_t type, uint32_t creator, char* extension, size_t capacity);
int filetype_map_extension_path(uint32_t type, uint32_t creator, char* extension, size_t capacity);
/* Document types from an application's FREF resources may match mappings
     owned by other creators. With no types, use this creator's document rows.
     Return unique *.ext patterns; a bounded list ends in ... if truncated. */
int filetype_map_extensions(const char* text, const uint32_t* types, size_t count, uint32_t creator, char* out,
                            size_t capacity);
int filetype_map_extensions_path(const uint32_t* types, size_t count, uint32_t creator, char* out, size_t capacity);
/* Apply raw-copy naming only across a host/HFS boundary. Resource-bearing
   files keep their existing metadata and names. Returns zero on overflow. */
int filetype_map_copy_name(const char* text, char* name, size_t capacity, uint32_t* type, uint32_t* creator,
                           int source_hfs, int destination_hfs, int has_resource_fork);
#ifdef __cplusplus
}
#endif
#endif
