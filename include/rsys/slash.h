/*+===================================================================
  File:      slash.h

  Summary:   Declares slash support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(_SLASH_H_)
#define _SLASH_H_

#if defined(CYGWIN32)

extern int Uaccess(const char* path, int mode);
extern int Uchdir(const char* path);
extern int Uchmod(const char* path, int mode);
extern FILE* Ufopen(const char* path, const char* type);
extern int Umkdir(const char* path, int mode);
extern int Uopen(const char* path, int flags, int mode);
extern int Uclose(int fd);
extern DIR* Uopendir(const char* path);

extern int Urename(const char* path, const char* new);
extern int Urmdir(const char* path);
extern int Ustat(const char* path, struct stat* buf);
extern int Uunlink(const char* path);
extern int Uutimes(const char* path, struct timeval tvp[2]);

#else

#define Uaccess access
#define Uchdir chdir
#define Uchmod chmod
#define Ufopen fopen
#define Ulstat lstat
#define Umkdir mkdir
#define Uopendir opendir
#define Urename rename
#define Urmdir rmdir
#define Ustat stat
#define Ustatfs statfs
#define Uunlink unlink
#define Uutimes utimes

extern int Uopen(const char* path, int flags, int mode);
extern int Uclose(int fd);

#endif

#endif /* !_SLASH_H_ */
