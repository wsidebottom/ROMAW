/*+===================================================================
  File:      list.h

  Summary:   Declares list support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in syn68k/src/syngen/include; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

#ifndef _list_h_
#define _list_h_

#include "token.h"

typedef struct ListStruct {
    struct ListStruct *cdr, *car;
    Token token; /* car == NULL, token.type = TOK_LIST -> empty list. */
} List;

#define CDR(c) ((c)->cdr)
#define CAR(c) ((c)->car)
#define CDDR(c) ((c)->cdr->cdr)
#define CDAR(c) ((c)->car->cdr)
#define CADR(c) ((c)->cdr->car)
#define CAAR(c) ((c)->car->car)
#define CDDDR(c) ((c)->cdr->cdr->cdr)
#define CDDAR(c) ((c)->car->cdr->cdr)
#define CDADR(c) ((c)->cdr->car->cdr)
#define CDAAR(c) ((c)->car->car->cdr)
#define CADDR(c) ((c)->cdr->cdr->car)
#define CADAR(c) ((c)->car->cdr->car)
#define CAADR(c) ((c)->cdr->car->car)
#define CAAAR(c) ((c)->car->car->car)
#define CDDDDR(c) ((c)->cdr->cdr->cdr->cdr)
#define CADDAR(c) ((c)->car->cdr->cdr->car)
#define CADDDR(c) ((c)->cdr->cdr->cdr->car)

extern List* alloc_list(void);
extern List* copy_list(const List* ls);
extern void propagate_fileinfo(const Token* original, List* ls);
extern BOOL lists_equal(const List* l1, const List* l2);
extern int list_length(const List* ls);
extern Token* has_token_of_type(List* list, TokenType type);
extern void print_list(FILE* out, const List* ls);
extern void replace_list(List* old, List* new);
extern void replace_tokens_of_type(List* list, TokenType old, TokenType new);
extern void replace_tokens_of_type_with_list(List* list, TokenType old, const List* new);
extern int list_size(const List* ls);
extern BOOL list_overlap(const List* l1, const List* l2);

#endif /* Not _list_h_ */
