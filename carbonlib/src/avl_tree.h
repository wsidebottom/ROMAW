/*+===================================================================
  File:      avl_tree.h

  Summary:   Implements the classic AVL Tree Manager when System dispatch is unavailable.

  Classes:   None.

  Functions: ROMlib_carbon_avl_dispatch.

  Origin:    Universal Interfaces 3.4.2 AVLTree.h and CarbonLib 1.4 selector wrappers.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMAW_CARBON_AVL_TREE_H
#define ROMAW_CARBON_AVL_TREE_H

enum {
    avl_signature = 0x61766c74,
    avl_duplicate = -960,
    avl_invalid = -961,
    avl_missing = -962,
    avl_insert_during_walk = -963,
    avl_locked = -964,
    avl_corrupt = -965,
};

typedef struct {
    uint32_t child[2], data, length, height, count;
} avl_node;

typedef struct {
    uint32_t signature, root, zone, flags, compare, size, dispose, refcon;
    uint32_t busy, walking;
} avl_tree;

static avl_node* avl_at(uint32_t address)
{
    return address ? ROMlib_carbon_try_guest_memory(address, sizeof(avl_node), 1) : NULL;
}

static uint32_t avl_height(uint32_t address)
{
    const avl_node* node = avl_at(address);
    return node ? node->height : 0;
}

static uint32_t avl_count(uint32_t address)
{
    const avl_node* node = avl_at(address);
    return node ? node->count : 0;
}

static void avl_update(avl_node* node)
{
    const uint32_t left = avl_height(node->child[0]), right = avl_height(node->child[1]);
    node->height = 1 + (left > right ? left : right);
    node->count = 1 + avl_count(node->child[0]) + avl_count(node->child[1]);
}

static int avl_balance(const avl_node* node)
{
    return (int)avl_height(node->child[1]) - (int)avl_height(node->child[0]);
}

static uint32_t avl_rotate(uint32_t root, unsigned toward)
{
    avl_node* old = avl_at(root);
    const uint32_t top = old->child[!toward];
    avl_node* new_root = avl_at(top);
    old->child[!toward] = new_root->child[toward];
    new_root->child[toward] = root;
    avl_update(old);
    avl_update(new_root);
    return top;
}

static uint32_t avl_rebalance(uint32_t root)
{
    avl_node* node = avl_at(root);
    avl_update(node);
    const int balance = avl_balance(node);
    if (balance >= -1 && balance <= 1)
        return root;
    const unsigned heavy = balance > 0;
    const int child_balance = avl_balance(avl_at(node->child[heavy]));
    if ((heavy && child_balance < 0) || (!heavy && child_balance > 0))
        node->child[heavy] = avl_rotate(node->child[heavy], heavy);
    return avl_rotate(root, !heavy);
}

static uint32_t avl_data(const avl_tree* tree, const avl_node* node)
{
    return tree->flags & 1 ? cfp_read(node->data) : node->data;
}

static uint32_t avl_node_type(const avl_node* node)
{
    return node->child[0] ? (node->child[1] ? 0 : 1) : (node->child[1] ? 2 : 3);
}

static int avl_callback(const ROMlib_carbon_ppc_frame* call, uint32_t upp, uint32_t info, const uint32_t* args,
                        unsigned count, uint32_t* value)
{
    ROMlib_carbon_ppc_frame arguments = *call;
    memcpy(arguments.words, args, count * sizeof(*args));
    return ROMlib_carbon_invoke_pascal(upp, info, &arguments, value);
}

static OSErr avl_search(avl_tree* tree, uint32_t key, const ROMlib_carbon_ppc_frame* call,
                        uint32_t** path, unsigned* depth)
{
    uint32_t* link = &tree->root;
    *depth = 0;
    while (*link) {
        if (*depth == 64)
            return avl_corrupt;
        avl_node* node = avl_at(*link);
        if (!node)
            return avl_corrupt;
        path[(*depth)++] = link;
        Handle data = tree->flags & 1 ? (Handle)SYN68K_TO_US(node->data) : NULL;
        const SignedByte state = data ? HGetState(data) : 0;
        if (data)
            HLock(data);
        const uint32_t args[] = {US_TO_SYN68K(tree), key, avl_data(tree, node), avl_node_type(node)};
        uint32_t order = 0;
        const int ok = avl_callback(call, tree->compare, 0x2ff0, args, NELEM(args), &order);
        if (data)
            HSetState(data, state);
        if (!ok)
            return paramErr;
        if (!order)
            return noErr;
        const unsigned right = (int32_t)order > 0;
        link = &node->child[right];
    }
    path[(*depth)++] = link;
    return avl_missing;
}

static OSErr avl_insert(avl_tree* tree, uint32_t key, const ROMlib_carbon_ppc_frame* call, uint32_t** path,
                        unsigned depth)
{
    const uint32_t args[] = {US_TO_SYN68K(tree), key};
    uint32_t length = 0;
    if (!avl_callback(call, tree->size, 0x3f0, args, NELEM(args), &length) || length > INT32_MAX)
        return paramErr;
    THz saved = GetZone();
    SetZone((THz)SYN68K_TO_US(tree->zone));
    avl_node* node = (avl_node*)NewPtrClear(sizeof(*node));
    Handle handle = node && (tree->flags & 1) ? NewHandle(length) : NULL;
    Ptr data = NULL;
    if (handle) {
        HLock(handle);
        data = STARH(handle);
    }
    else if (node && !(tree->flags & 1))
        data = NewPtr(length ? length : 1);
    SetZone(saved);
    const void* source = length ? ROMlib_carbon_try_guest_memory(key, length, 0) : NULL;
    if (!node || !data || (length && !source)) {
        if (handle)
            DisposHandle(handle);
        else if (data)
            DisposPtr(data);
        if (node)
            DisposPtr((Ptr)node);
        return length && !source ? paramErr : memFullErr;
    }
    if (length)
        memcpy(data, source, length);
    if (handle)
        HUnlock(handle);
    *node = (avl_node){.data = handle ? US_TO_SYN68K(handle) : US_TO_SYN68K(data),
                       .length = length, .height = 1, .count = 1};
    *path[--depth] = US_TO_SYN68K(node);
    while (depth) {
        --depth;
        *path[depth] = avl_rebalance(*path[depth]);
    }
    return noErr;
}

static OSErr avl_copy(const avl_tree* tree, const avl_node* node, uint32_t output, uint32_t size)
{
    void* destination = output && node->length ? ROMlib_carbon_try_guest_memory(output, node->length, 1) : NULL;
    void* length = size ? ROMlib_carbon_try_guest_memory(size, 4, 1) : NULL;
    if ((output && node->length && !destination) || (size && !length))
        return paramErr;
    if (destination)
        memmove(destination, SYN68K_TO_US(avl_data(tree, node)), node->length);
    if (length) {
        const uint32_t encoded = CL(node->length);
        memcpy(length, &encoded, sizeof encoded);
    }
    return noErr;
}

static void avl_discard(avl_tree* tree, avl_node* node, const ROMlib_carbon_ppc_frame* call)
{
    Handle handle = tree->flags & 1 ? (Handle)SYN68K_TO_US(node->data) : NULL;
    if (handle)
        HLock(handle);
    if (tree->dispose) {
        const uint32_t args[] = {US_TO_SYN68K(tree), avl_data(tree, node)};
        uint32_t ignored;
        avl_callback(call, tree->dispose, 0x3c0, args, NELEM(args), &ignored);
    }
    if (handle)
        DisposHandle(handle);
    else
        DisposPtr((Ptr)SYN68K_TO_US(node->data));
    DisposPtr((Ptr)node);
}

static uint32_t avl_extract_min(uint32_t root, uint32_t* minimum)
{
    avl_node* node = avl_at(root);
    if (!node->child[0]) {
        *minimum = root;
        return node->child[1];
    }
    node->child[0] = avl_extract_min(node->child[0], minimum);
    return avl_rebalance(root);
}

static void avl_remove(avl_tree* tree, uint32_t** path, unsigned depth, const ROMlib_carbon_ppc_frame* call)
{
    uint32_t* link = path[--depth];
    avl_node* node = avl_at(*link);
    if (node->child[0] && node->child[1]) {
        uint32_t successor;
        const uint32_t right = avl_extract_min(node->child[1], &successor);
        avl_node* replacement = avl_at(successor);
        replacement->child[0] = node->child[0];
        replacement->child[1] = right;
        *link = avl_rebalance(successor);
    }
    else
        *link = node->child[0] ? node->child[0] : node->child[1];
    while (depth) {
        --depth;
        *path[depth] = avl_rebalance(*path[depth]);
    }
    avl_discard(tree, node, call);
}

static void avl_destroy_nodes(avl_tree* tree, uint32_t root, unsigned reverse,
                              const ROMlib_carbon_ppc_frame* call)
{
    avl_node* node = avl_at(root);
    if (!node)
        return;
    const uint32_t second = node->child[!reverse];
    avl_destroy_nodes(tree, node->child[reverse], reverse, call);
    avl_discard(tree, node, call);
    avl_destroy_nodes(tree, second, reverse, call);
}

static OSErr avl_visit(avl_tree* tree, uint32_t root, uint32_t upp, uint32_t refcon, unsigned reverse,
                       unsigned level, const ROMlib_carbon_ppc_frame* call)
{
    avl_node* node = avl_at(root);
    if (!node)
        return noErr;
    Handle data = tree->flags & 1 ? (Handle)SYN68K_TO_US(node->data) : NULL;
    const SignedByte state = data ? HGetState(data) : 0;
    if (data)
        HLock(data);
    uint32_t args[] = {US_TO_SYN68K(tree), avl_data(tree, node), 0, avl_node_type(node), level,
                       (uint32_t)avl_balance(node), refcon};
    uint32_t value = 0;
    OSErr status = noErr;
    for (unsigned stage = 0; stage < 3 && !status; ++stage) {
        args[2] = stage;
        status = avl_callback(call, upp, 0xfebe0, args, NELEM(args), &value) ? (OSErr)value : paramErr;
        if (!status && stage < 2) {
            const unsigned branch = stage ^ reverse;
            status = avl_visit(tree, node->child[branch], upp, refcon, reverse, level + 1, call);
        }
    }
    if (data)
        HSetState(data, state);
    return status;
}

static OSErr avl_new(const uint32_t* args)
{
    if (!args[5] || !cfp_write(args[5], 0) || !args[1] || !args[2] || (args[0] & ~1u))
        return paramErr;
    avl_tree* tree = (avl_tree*)NewPtrClear(sizeof(*tree));
    if (!tree)
        return memFullErr;
    *tree = (avl_tree){.signature = avl_signature, .zone = US_TO_SYN68K(GetZone()), .flags = args[0],
                       .compare = args[1], .size = args[2], .dispose = args[3], .refcon = args[4]};
    if (!cfp_write(args[5], US_TO_SYN68K(tree))) {
        DisposPtr((Ptr)tree);
        return paramErr;
    }
    return noErr;
}

int ROMlib_carbon_avl_dispatch(uint32_t selector, const ROMlib_carbon_ppc_frame* call, uint32_t* result)
{
    const uint32_t* args = call->words;
    if (selector == 0xc01) {
        *result = (int32_t)avl_new(args);
        return 1;
    }
    const bool disposing = selector == 0x302;
    const uint32_t address = disposing && args[0] ? cfp_read(args[0]) : args[0];
    avl_tree* tree = address ? ROMlib_carbon_try_guest_memory(address, sizeof(avl_tree), 1) : NULL;
    OSErr status = avl_invalid;
    if (tree && tree->signature == avl_signature) {
        if (selector == 0x409)
            status = args[1] && cfp_write(args[1], tree->refcon) ? noErr : paramErr;
        else if (selector == 0x804)
            status = args[1] && cfp_write(args[1], avl_count(tree->root)) ? noErr : paramErr;
        else if (tree->busy)
            status = tree->walking && selector == 0x406 ? avl_insert_during_walk : avl_locked;
        else {
            tree->busy = 1;
            switch (selector) {
            case 0x302:
                if ((uint16_t)args[1] > 1 || !cfp_write(args[0], 0))
                    status = paramErr;
                else {
                    const uint32_t root = tree->root;
                    tree->root = 0;
                    avl_destroy_nodes(tree, root, (uint16_t)args[1], call);
                    tree->signature = 0;
                    DisposPtr((Ptr)tree);
                    tree = NULL;
                    status = noErr;
                }
                break;
            case 0x703:
                if (!args[1] || (uint16_t)args[2] > 1)
                    status = paramErr;
                else {
                    tree->walking = 1;
                    status = avl_visit(tree, tree->root, args[1], args[3], (uint16_t)args[2], 1, call);
                    tree->walking = 0;
                }
                break;
            case 0x805: {
                uint32_t index = args[1];
                status = avl_missing;
                for (avl_node* node = avl_at(tree->root); index && node;) {
                    const uint32_t position = avl_count(node->child[0]) + 1;
                    if (index == position) {
                        status = avl_copy(tree, node, args[2], args[3]);
                        break;
                    }
                    const bool right = index > position;
                    node = avl_at(node->child[right]);
                    if (right)
                        index -= position;
                }
                break;
            }
            case 0x406:
            case 0x807:
            case 0x808: {
                uint32_t* path[65];
                unsigned depth;
                status = args[1] ? avl_search(tree, args[1], call, path, &depth) : paramErr;
                if (selector == 0x406) {
                    if (!status)
                        status = avl_duplicate;
                    else if (status == avl_missing)
                        status = avl_insert(tree, args[1], call, path, depth);
                }
                else if (!status) {
                    status = avl_copy(tree, avl_at(*path[depth - 1]), args[2], args[3]);
                    if (!status && selector == 0x807)
                        avl_remove(tree, path, depth, call);
                }
                break;
            }
            default:
                status = paramErr;
                break;
            }
            if (tree)
                tree->busy = 0;
        }
    }
    *result = (int32_t)status;
    return 1;
}

#endif
