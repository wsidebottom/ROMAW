/*+===================================================================
  File:      dcache.h

  Summary:   Declares dcache support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(_RSYS_DCACHE_H_)
#define _RSYS_DCACHE_H_

typedef uint32 (*write_callback_funcp_t)(uint32 fd, const void* buf, uint32 offset, uint32 count);

typedef uint32 (*read_callback_funcp_t)(uint32 fd, void* buf, uint32 offset, uint32 count);

extern boolean_t dcache_set_enabled(boolean_t enabled_p);

extern uint32 dcache_read(uint32 fd, void* buf, uint32 offset, uint32 count, read_callback_funcp_t read_callback);

extern uint32 dcache_write(uint32 fd, const void* buf, uint32 offset, uint32 count,
                           write_callback_funcp_t write_callback);

extern boolean_t dcache_invalidate(uint32 fd, boolean_t flush_p);
extern boolean_t dcache_flush(uint32 fd);
extern boolean_t dcache_invalidate_all(boolean_t flush_p);

#endif /* !_RSYS_DCACHE_H_ */
