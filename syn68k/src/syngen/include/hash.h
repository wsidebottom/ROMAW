/*+===================================================================
  File:      hash.h

  Summary:   Declares hash support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in syn68k/src/syngen/include; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

#ifndef _hash_h_
#define _hash_h_

#define HASH_BUCKETS 253
#define INITIAL_SYMBOLS 7

typedef union {
    long n;
    void* p;
} SymbolInfo;

typedef struct {
    const char* name;
    SymbolInfo value;
} Symbol;

typedef struct {
    int num_symbols, max_symbols;
    Symbol symbol[INITIAL_SYMBOLS]; /* variable length array. */
} Bucket;

typedef struct {
    Bucket* bucket[HASH_BUCKETS];
} SymbolTable;

typedef enum { HASH_NOERR, HASH_DUPLICATE, HASH_NOTFOUND } HashErr;

extern SymbolTable* make_symbol_table(void);
extern void free_symbol_table(SymbolTable* s);
extern HashErr insert_symbol(SymbolTable* s, const char* name, SymbolInfo val);
extern HashErr lookup_symbol(const SymbolTable* s, const char* name, SymbolInfo* val, const char** original_name_ptr);
extern void dump_symbol_table(const SymbolTable* s);

#endif /* Not hash_h_ */
