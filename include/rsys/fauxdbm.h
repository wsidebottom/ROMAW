/*+===================================================================
  File:      fauxdbm.h

  Summary:   Declares fauxdbm support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(__RSYS_FAUXDBM_H__)
#define __RSYS_FAUXDBM_H__

enum { DBM_BSIZE = 8192 };

typedef struct {
    FILE* fp;
    char buf[DBM_BSIZE];
} DBM;

enum { DBM_REPLACE };

extern DBM* dbm_open(const char* name, int flags, int mode);
extern datum dbm_firstkey(DBM* file);
extern datum dbm_nextkey(DBM* file);
extern datum dbm_fetch(DBM* file, datum key);
extern int dbm_store(DBM* file, datum key, datum content, int flags);
extern void dbm_close(DBM* file);

#endif
