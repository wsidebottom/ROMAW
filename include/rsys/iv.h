/*+===================================================================
  File:      iv.h

  Summary:   Declares iv support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/*
 * Copyright 1995 by Abacus Research and Development, Inc.
 * All rights reserved.
 *
 * $Id: iv.h 63 2004-12-24 18:19:43Z ctm $
 */

#if !defined(_IMAGE_COMMON_H_)
#define _IMAGE_COMMON_H_

#define PORT (htons(7117))

typedef struct color {
    unsigned short red, green, blue;
} color_t;

typedef struct image_header {
    int width;
    int height;
    int row_bytes;
    color_t image_color_map[256];
} image_header_t;

#endif /* !_IMAGE_COMMON_H_ */
