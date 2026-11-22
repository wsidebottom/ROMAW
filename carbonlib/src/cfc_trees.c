/*+===================================================================
  File:      cfc_trees.c

  Summary:   Native tree creation, traversal, context access and ownership changes preserving the versioned guest ABI.

  Classes:   None.

  Functions: ROMlib_cfc_CFTreeAppendChild, ROMlib_cfc_CFTreeApplyFunctionToChildren, ROMlib_cfc_CFTreeCreate,
             ROMlib_cfc_CFTreeFindRoot, ROMlib_cfc_CFTreeGetChildAtIndex, ROMlib_cfc_CFTreeGetChildCount,
             ROMlib_cfc_CFTreeGetChildren, ROMlib_cfc_CFTreeGetContext, ROMlib_cfc_CFTreeGetFirstChild,
             ROMlib_cfc_CFTreeGetNextSibling, ROMlib_cfc_CFTreeGetParent, ROMlib_cfc_CFTreeGetTypeID,
             ROMlib_cfc_CFTreeInsertSibling, ROMlib_cfc_CFTreePrependChild, ROMlib_cfc_CFTreeRemove,
             ROMlib_cfc_CFTreeRemoveAllChildren, ROMlib_cfc_CFTreeSetContext, ROMlib_cfc_CFTreeSortChildren

  Origin:    C reconstruction of CoreFoundationLib 1.4 offsets 0x22d74..0x232d0 and Universal Interfaces 3.4.2 CFTree
             declarations.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#include "cfc_cohort.h"
#include <stdlib.h>

static uint32 tree_callbacks(const ROMlib_carbon_reference* reference, uint32 tree)
{
    switch (cfp_read(tree + 4) & 3) {
    case 0:
        return US_TO_SYN68K(reference->toc) + 0x3434;
    case 1:
        return US_TO_SYN68K(reference->toc) + 0x3428;
    default:
        return tree + 24;
    }
}

uint32 ROMlib_cfc_CFTreeGetTypeID(void)
{
    ROMlib_carbon_reference reference;
    uint32 result = 0;
    if (cfp_reference("CFTreeGetTypeID", 0x22d74, NULL, 0, &reference, &result))
        result = cfp_toc(&reference, 0x8d4);
    return result;
}

uint32 ROMlib_cfc_CFTreeCreate(uint32 allocator, uint32 context)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {allocator, context}, result = 0;
    if (!cfp_reference("CFTreeCreate", 0x22d7c, arguments, 2, &reference, &result))
        return result;
    uint32 retain = 0, release = 0, describe = 0, info = 0;
    if (context && (!cfp_get(context + 8, &retain) || !cfp_get(context + 12, &release) ||
                    !cfp_get(context + 16, &describe) || !cfp_get(context + 4, &info)))
        return 0;
    uint32 kind = 0;
    if (retain || release || describe)
        kind = retain == cfp_toc(&reference, 0x29c) && release == cfp_toc(&reference, 0x298) &&
                       describe == cfp_toc(&reference, 0x294)
                   ? 1
                   : 3;
    /* Reserve room for custom callbacks even if the initial context uses
       one of the two compact callback kinds. */
    uint32 tree = cfc_allocate_instance(&reference, allocator, cfp_toc(&reference, 0x8d4), 28);
    if (!tree)
        return 0;
    if (!cfp_write(tree + 8, 0) || !cfp_write(tree + 12, 0) || !cfp_write(tree + 16, 0) ||
        !cfp_write(tree + 4, (cfp_read(tree + 4) & ~3U) | kind))
        return 0;
    if (retain && !cfp_callback(retain, &info, 1, &info))
        return 0;
    if (!cfp_write(tree + 20, info))
        return 0;
    if (kind == 3 &&
        (!cfp_write(tree + 24, retain) || !cfp_write(tree + 28, release) || !cfp_write(tree + 32, describe)))
        return 0;
    return tree;
}

uint32 ROMlib_cfc_CFTreeGetChildCount(uint32 tree)
{
    ROMlib_carbon_reference reference;
    uint32 result = 0;
    if (cfp_reference("CFTreeGetChildCount", 0x22f58, &tree, 1, &reference, &result)) {
        uint32 child;
        if (!cfp_get(tree + 16, &child))
            return 0;
        while (child) {
            ++result;
            if (!cfp_get(child + 12, &child))
                return 0;
        }
    }
    return result;
}

uint32 ROMlib_cfc_CFTreeGetParent(uint32 tree)
{
    ROMlib_carbon_reference reference;
    uint32 result = 0;
    if (cfp_reference("CFTreeGetParent", 0x22f7c, &tree, 1, &reference, &result))
        cfp_get(tree + 8, &result);
    return result;
}

uint32 ROMlib_cfc_CFTreeGetNextSibling(uint32 tree)
{
    ROMlib_carbon_reference reference;
    uint32 result = 0;
    if (cfp_reference("CFTreeGetNextSibling", 0x22f84, &tree, 1, &reference, &result))
        cfp_get(tree + 12, &result);
    return result;
}

uint32 ROMlib_cfc_CFTreeGetFirstChild(uint32 tree)
{
    ROMlib_carbon_reference reference;
    uint32 result = 0;
    if (cfp_reference("CFTreeGetFirstChild", 0x22f8c, &tree, 1, &reference, &result))
        cfp_get(tree + 16, &result);
    return result;
}

uint32 ROMlib_cfc_CFTreeFindRoot(uint32 tree)
{
    ROMlib_carbon_reference reference;
    uint32 result = 0;
    if (!cfp_reference("CFTreeFindRoot", 0x22f94, &tree, 1, &reference, &result))
        return result;
    uint32 parent;
    while (cfp_get(tree + 8, &parent)) {
        if (!parent)
            return tree;
        tree = parent;
    }
    return 0;
}

void ROMlib_cfc_CFTreeGetContext(uint32 tree, uint32 output)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {tree, output}, ignored = 0;
    if (!cfp_reference("CFTreeGetContext", 0x22fac, arguments, 2, &reference, &ignored))
        return;
    uint32 callbacks = tree_callbacks(&reference, tree), value;
    if (!cfp_write(output, 0) || !cfp_get(tree + 20, &value) || !cfp_write(output + 4, value))
        return;
    for (unsigned i = 0; i < 3; ++i)
        if (!cfp_get(callbacks + i * 4, &value) || !cfp_write(output + 8 + i * 4, value))
            return;
}

void ROMlib_cfc_CFTreeSetContext(uint32 tree, uint32 context)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {tree, context}, ignored, info, callbacks[3];
    if (!cfp_reference("CFTreeSetContext", 0x23014, arguments, 2, &reference, &ignored) || !context ||
        !cfp_get(context + 4, &info))
        return;
    for (unsigned i = 0; i < NELEM(callbacks); ++i)
        if (!cfp_get(context + 8 + i * 4, &callbacks[i]))
            return;
    uint32 flags, old_info, release;
    if (!cfp_get(tree + 4, &flags) || !cfp_get(tree + 20, &old_info) ||
        !cfp_get(tree_callbacks(&reference, tree) + 4, &release))
        return;
    if ((flags & 3) != 3) {
        /* An unmodified companion version may have created a compact node. */
        ROMlib_carbon_reference create;
        uint32 entry;
        if (!ROMlib_carbon_probe_reference("CoreFoundationLib", "CFTreeCreate", &create) ||
            !cfp_get(US_TO_SYN68K(create.function), &entry) || cfp_read(entry + 0x1c) != 0x38a0001c)
            return;
    }
    if (callbacks[0] && !cfp_callback(callbacks[0], &info, 1, &info))
        return;
    for (unsigned i = 0; i < NELEM(callbacks); ++i)
        if (!cfp_write(tree + 24 + i * 4, callbacks[i]))
            return;
    if (cfp_write(tree + 20, info) && cfp_write(tree + 4, (cfp_read(tree + 4) & ~3U) | 3U) && release)
        cfp_callback(release, &old_info, 1, NULL);
}

uint32 ROMlib_cfc_CFTreeGetChildAtIndex(uint32 tree, uint32 index)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {tree, index}, result = 0;
    if (!cfp_reference("CFTreeGetChildAtIndex", 0x23018, arguments, 2, &reference, &result))
        return result;
    if (!cfp_get(tree + 16, &result))
        return 0;
    while (result && index) {
        if (!cfp_get(result + 12, &result))
            return 0;
        --index;
    }
    return result;
}

void ROMlib_cfc_CFTreeGetChildren(uint32 tree, uint32 output)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {tree, output}, ignored = 0, child;
    if (!cfp_reference("CFTreeGetChildren", 0x23040, arguments, 2, &reference, &ignored) || !cfp_get(tree + 16, &child))
        return;
    while (child) {
        if (!cfp_write(output, child) || !cfp_get(child + 12, &child))
            return;
        output += 4;
    }
}

void ROMlib_cfc_CFTreeApplyFunctionToChildren(uint32 tree, uint32 procedure, uint32 context)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {tree, procedure, context}, ignored = 0, child;
    if (!cfp_reference("CFTreeApplyFunctionToChildren", 0x23060, arguments, 3, &reference, &ignored) ||
        !cfp_get(tree + 16, &child))
        return;
    while (child) {
        uint32 callback[] = {child, context};
        if (!cfp_callback(procedure, callback, 2, NULL) || !cfp_get(child + 12, &child))
            return;
    }
}

void ROMlib_cfc_CFTreePrependChild(uint32 tree, uint32 child)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {tree, child}, ignored = 0, first;
    if (!cfp_reference("CFTreePrependChild", 0x230c4, arguments, 2, &reference, &ignored))
        return;
    if (!cfp_call("CFRetain", &child, 1, NULL) || !cfp_write(child + 8, tree) || !cfp_get(tree + 16, &first) ||
        !cfp_write(child + 12, first))
        return;
    cfp_write(tree + 16, child);
}

void ROMlib_cfc_CFTreeAppendChild(uint32 tree, uint32 child)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {tree, child}, ignored = 0, parent, last, next;
    if (!cfp_reference("CFTreeAppendChild", 0x23114, arguments, 2, &reference, &ignored) ||
        !cfp_get(child + 8, &parent))
        return;
    if (parent) {
        cfp_failure("Native Carbon tree append requires an unparented child");
        return;
    }
    if (!cfp_call("CFRetain", &child, 1, NULL) || !cfp_write(child + 8, tree) || !cfp_write(child + 12, 0) ||
        !cfp_get(tree + 16, &last))
        return;
    if (!last) {
        cfp_write(tree + 16, child);
        return;
    }
    while (cfp_get(last + 12, &next)) {
        if (!next) {
            cfp_write(last + 12, child);
            return;
        }
        last = next;
    }
}

void ROMlib_cfc_CFTreeInsertSibling(uint32 tree, uint32 sibling)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {tree, sibling}, ignored = 0, next;
    if (!cfp_reference("CFTreeInsertSibling", 0x231a4, arguments, 2, &reference, &ignored))
        return;
    /* 1.4 stores tree itself as the parent, unlike the newer SDK contract. */
    if (!cfp_call("CFRetain", &sibling, 1, NULL) || !cfp_write(sibling + 8, tree) || !cfp_get(tree + 12, &next) ||
        !cfp_write(sibling + 12, next))
        return;
    cfp_write(tree + 12, sibling);
}

void ROMlib_cfc_CFTreeRemove(uint32 tree)
{
    ROMlib_carbon_reference reference;
    uint32 ignored = 0, parent, first, next;
    if (!cfp_reference("CFTreeRemove", 0x231f4, &tree, 1, &reference, &ignored) || !cfp_get(tree + 8, &parent) ||
        !parent || !cfp_get(parent + 16, &first))
        return;
    if (first == tree) {
        if (!cfp_get(tree + 12, &next) || !cfp_write(parent + 16, next))
            return;
    }
    else {
        while (first) {
            if (!cfp_get(first + 12, &next))
                return;
            if (next == tree) {
                if (!cfp_get(tree + 12, &next) || !cfp_write(first + 12, next))
                    return;
                break;
            }
            first = next;
        }
    }
    if (cfp_write(tree + 8, 0) && cfp_write(tree + 12, 0))
        cfp_call("CFRelease", &tree, 1, NULL);
}

void ROMlib_cfc_CFTreeRemoveAllChildren(uint32 tree)
{
    ROMlib_carbon_reference reference;
    uint32 ignored = 0, child, next;
    if (!cfp_reference("CFTreeRemoveAllChildren", 0x23274, &tree, 1, &reference, &ignored) ||
        !cfp_get(tree + 16, &child) || !cfp_write(tree + 16, 0))
        return;
    while (child) {
        if (!cfp_get(child + 12, &next) || !cfp_write(child + 8, 0) || !cfp_write(child + 12, 0) ||
            !cfp_call("CFRelease", &child, 1, NULL))
            return;
        child = next;
    }
}

void ROMlib_cfc_CFTreeSortChildren(uint32 tree, uint32 comparator, uint32 context)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {tree, comparator, context}, ignored;
    if (!cfp_reference("CFTreeSortChildren", 0x232d4, arguments, 3, &reference, &ignored) || !comparator)
        return;
    size_t count = ROMlib_cfc_CFTreeGetChildCount(tree);
    if (count < 2 || count > SIZE_MAX / (3 * sizeof(uint32)))
        return;
    uint32* original = malloc(3 * count * sizeof *original);
    if (!original)
        return;
    uint32* first = original + count;
    uint32* second = first + count;
    uint32 child;
    if (!cfp_get(tree + 16, &child))
        goto done;
    for (size_t i = 0; i < count; ++i) {
        original[i] = first[i] = child;
        if (!child || !cfp_get(child + 12, &child))
            goto done;
    }
    /* Keep the live sibling links intact while guest comparators run. */
    for (size_t width = 1; width < count; width *= 2) {
        for (size_t start = 0; start < count; start += 2 * width) {
            size_t split = MIN(start + width, count), end = MIN(start + 2 * width, count);
            size_t left = start, right = split;
            for (size_t out = start; out < end; ++out) {
                int before = right == end;
                if (left < split && right < end) {
                    uint32 pair[] = {first[left], first[right], context}, order;
                    if (!cfp_callback(comparator, pair, 3, &order))
                        goto done;
                    before = (int32)order <= 0;
                }
                second[out] = first[before ? left++ : right++];
            }
        }
        uint32* swap = first;
        first = second;
        second = swap;
    }
    if (!cfp_get(tree + 16, &child))
        goto done;
    for (size_t i = 0; i < count; ++i)
        if (child != original[i] || !cfp_get(child + 12, &child))
            goto done;
    if (child)
        goto done;
    for (size_t i = 0; i < count; ++i)
        if (!cfp_write(first[i] + 12, i + 1 < count ? first[i + 1] : 0))
            goto done;
    cfp_write(tree + 16, first[0]);
done:
    free(original);
}
