/*+===================================================================
  File:      menu_access.h

  Summary:   Implements Carbon Menu Manager operations over ROMAW menus.

  Classes:   None.

  Functions: Private Menu Manager frame adapters for the export registry.

  Origin:    CarbonLib 1.4, System 9.2.1 MenusLib, and Universal Interfaces
             3.4.2 Menus.h and MacErrors.h.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMAW_CARBON_MENU_ACCESS_H
#define ROMAW_CARBON_MENU_ACCESS_H

#include "rsys/menu.h"
#include "cf_batch.h"
#include "collection_entries.h"
#include "toolbox_vectors.h"

static int menu_reference_matches(const ROMlib_carbon_reference* ref, uint32 offset)
{
    return ref->code_offset == offset && ref->code_size == 0xb8fb4 && ref->toc && ref->toc_offset == 0x2f78 &&
           ref->toc_size == 0x10f0c;
}

static int carbon_CheckMenuItem(ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference ref;
    if (!ROMlib_carbon_reference_function("CarbonLib", "CheckMenuItem", &ref))
        return 0;
    if (!menu_reference_matches(&ref, 0x7560c))
        return ROMlib_carbon_invoke_frame(ref.function, call);

    const uint32_t menu = call->words[0], item = call->words[1];
    const uint32_t mark[] = {menu, (uint32_t)(int32_t)(int16_t)item, (uint8_t)call->words[2]};
    const uint32_t invalidate[] = {menu, (uint16_t)item, 1};
    return cfp_private(&ref, 0xb60f0, mark, 3, NULL) && cfp_private(&ref, 0xb39c0, invalidate, 3, NULL);
}

static BOOLEAN carbon_IsMenuBarVisible(void)
{
    return CW(MBarHeight) > 0;
}

static int carbon_HideMenuBar(ROMlib_carbon_ppc_frame* call)
{
    ROMlib_menu_hide_bar();
    call->words[0] = 0;
    return 1;
}

static int carbon_ShowMenuBar(ROMlib_carbon_ppc_frame* call)
{
    ROMlib_menu_show_bar();
    call->words[0] = 0;
    return 1;
}

static bool alternate_menu_profile(const char* name, uint32_t offset, ROMlib_carbon_reference* reference)
{
    return ROMlib_carbon_probe_reference("MenusLib", name, reference) &&
           (reference->code_offset != offset || reference->code_size != 0x162a4);
}

static int carbon_ValidateMenuBar(ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference reference;
    if (alternate_menu_profile("ValidateMenuBar", 0xaf0, &reference))
        return ROMlib_carbon_invoke_frame(reference.function, call);
    ROMlib_menu_validate_bar();
    return 1;
}

static int carbon_InvalidateAllMenuSizes(ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference reference;
    if (alternate_menu_profile("InvalidateAllMenuSizes", 0x1351c, &reference))
        return ROMlib_carbon_invoke_frame(reference.function, call);
    ROMlib_menu_invalidate_sizes();
    return 1;
}

static int carbon_SetMenuBarHeight(ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference reference;
    if (alternate_menu_profile("SetMenuBarHeight", 0x1d60, &reference))
        return ROMlib_carbon_invoke_frame(reference.function, call);
    ROMlib_menu_set_bar_height((int16_t)call->words[0]);
    return 1;
}

enum menu_clock_action { clock_is_enabled, clock_enable, clock_disable, clock_get, clock_set, clock_refresh };

enum {
    menu_clock_state_offset = 0x1750,
    menu_clock_state_size = 0x132,
    menu_clock_config_size = 22,
    menu_clock_flags = 4,
    menu_clock_enabled = 0x40,
    menu_clock_refresh_flags = 0x18,
    menu_clock_font_cache = 0x1a,
    menu_clock_text_cache = 0x1e,
    menu_clock_width = 0x12a,
    menu_clock_icon = 0x12e,
    menu_clock_already_enabled = -5621,
    menu_clock_already_disabled = -5620,
};

static void menu_clock_request_redraw(Byte* clock)
{
    clock[menu_clock_refresh_flags] &= ~3;
}

static int menu_result(ROMlib_carbon_ppc_frame* call, uint32 status)
{
    call->words[0] = status;
    return 1;
}

static bool menu_live_reference(const char* name, uint32 offset, ROMlib_carbon_reference* original)
{
    return ROMlib_carbon_probe_reference("MenusLib", name, original) && original->code_offset == offset &&
           original->code_size == 0x162a4 && original->toc && original->toc_offset == 0 && original->toc_size == 0x2104;
}

static int menu_clock_change(const ROMlib_carbon_reference* original, Byte* clock, enum menu_clock_action action,
                             ROMlib_carbon_ppc_frame* call)
{
    const uint32 address = US_TO_SYN68K(clock);
    const bool enabling = action == clock_enable;
    if (enabling) {
        if (clock[menu_clock_flags] & menu_clock_enabled)
            return menu_result(call, menu_clock_already_enabled);
        Handle font = NewHandleSys(32);
        if (!font)
            return menu_result(call, MemError());
        Handle text = NewHandleSys(332);
        if (!text) {
            const OSErr status = MemError();
            DisposHandle(font);
            return menu_result(call, status);
        }
        const uint32 font_address = CL(US_TO_SYN68K(font)), text_address = CL(US_TO_SYN68K(text));
        memcpy(clock + menu_clock_font_cache, &font_address, 4);
        memcpy(clock + menu_clock_text_cache, &text_address, 4);
        clock[menu_clock_flags] |= menu_clock_enabled;
    }
    else {
        if (!(clock[menu_clock_flags] & menu_clock_enabled))
            return menu_result(call, menu_clock_already_disabled);
        uint32 icon;
        if (!cfp_get(address + menu_clock_icon, &icon) || (icon && !cfp_private(original, 0x13e54, &icon, 1, NULL)))
            return 0;
        memset(clock + menu_clock_icon, 0, 4);
        for (unsigned slot = menu_clock_font_cache; slot <= menu_clock_text_cache; slot += 4) {
            uint32 cache;
            if (!cfp_get(address + slot, &cache))
                return 0;
            memset(clock + slot, 0, 4);
            if (cache)
                DisposHandle((Handle)SYN68K_TO_US(cache));
        }
        clock[menu_clock_flags] &= ~menu_clock_enabled;
    }
    uint32 status;
    if (!cfp_private(original, 0xb428, &address, 1, &status))
        return 0;
    if (enabling) {
        menu_clock_request_redraw(clock);
        memset(clock + menu_clock_width, 0, 2);
    }
    return carbon_vector_InvalMenuBar(call) && menu_result(call, (int16)status);
}

static int menu_clock(const char* name, uint32 offset, enum menu_clock_action action, ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference original;
    if (!menu_live_reference(name, offset, &original))
        return original.function ? ROMlib_carbon_invoke_frame(original.function, call) : 0;

    const uint32 address = US_TO_SYN68K(original.toc) + menu_clock_state_offset;
    Byte* clock = ROMlib_carbon_try_guest_memory(address, menu_clock_state_size,
                                                 action != clock_is_enabled && action != clock_get);
    if (!clock)
        return ROMlib_carbon_invoke_frame(original.function, call);
    if (action == clock_is_enabled)
        return menu_result(call, (clock[menu_clock_flags] & menu_clock_enabled) != 0);
    if (action == clock_enable || action == clock_disable)
        return menu_clock_change(&original, clock, action, call);
    if (action == clock_refresh) {
        menu_clock_request_redraw(clock);
        return menu_result(call, noErr);
    }

    Byte* config = call->words[0]
                       ? ROMlib_carbon_try_guest_memory(call->words[0], menu_clock_config_size, action == clock_get)
                       : NULL;
    if (!config || config[0] || config[1])
        return menu_result(call, paramErr);
    if (action == clock_get) {
        memmove(config + 2, clock + menu_clock_flags, menu_clock_config_size - 2);
        return menu_result(call, noErr);
    }
    memmove(clock + menu_clock_flags, config + 2, menu_clock_config_size - 2);
    menu_clock_request_redraw(clock);
    uint32 status;
    if (!cfp_private(&original, 0xb428, &address, 1, &status))
        return 0;
    return carbon_vector_DrawMenuBar(call) && menu_result(call, (int16)status);
}

#define CARBON_MENU_CLOCK(name, offset, action)                                                       \
    static int carbon_##name(ROMlib_carbon_ppc_frame* call)                                            \
    {                                                                                                 \
        return menu_clock(#name, offset, action, call);                                                \
    }
CARBON_MENU_CLOCK(IsMenuBarClockEnabled, 0xa000, clock_is_enabled)
CARBON_MENU_CLOCK(EnableMenuBarClock, 0xa01c, clock_enable)
CARBON_MENU_CLOCK(DisableMenuBarClock, 0xa13c, clock_disable)
CARBON_MENU_CLOCK(GetMenuBarClockConfig, 0xa200, clock_get)
CARBON_MENU_CLOCK(SetMenuBarClockConfig, 0xa274, clock_set)
CARBON_MENU_CLOCK(RefreshMenuBarClock, 0xa330, clock_refresh)
#undef CARBON_MENU_CLOCK

enum carbon_menu_enable_operation {
    menu_enable,
    menu_disable,
    menu_enabled,
    menu_enable_all,
    menu_disable_all,
    menu_has_enabled
};

static MenuHandle carbon_menu_handle(uint32_t address)
{
    const uint32_t* master = address ? ROMlib_carbon_guest_memory(address, 4, 0) : NULL;
    if (!master || !*master || !ROMlib_carbon_guest_memory(CL(*master), offsetof(MenuInfo, menuData) + 2, 0))
        return NULL;
    return (MenuHandle)master;
}

static int menu_forget_saved_pixels(const ROMlib_carbon_reference* reference, uint32 menu_record)
{
    uint32 cache, snapshot;
    if (!cfp_get(menu_record + 0x10, &cache))
        return 0;
    if (!cache)
        return 1;
    _Alignas(16) uint32 owner = CL(cache);
    const uint32 output = US_TO_SYN68K(&owner);
    return cfp_get(cache, &snapshot) && cfp_write(menu_record + 0x10, 0) &&
           cfp_private(reference, 0x13824, &snapshot, 1, NULL) &&
           cfp_private(reference, 0x1383c, &output, 1, NULL);
}

static int carbon_InvalidateCachedMenuPixels(ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference reference;
    if (!menu_live_reference("InvalidateCachedMenuPixels", 0x1114, &reference))
        return reference.function ? ROMlib_carbon_invoke_frame(reference.function, call) : 0;
    if (!carbon_menu_handle(call->words[0]))
        return menu_result(call, noErr);
    uint32 record, list, cache;
    if (!cfp_private(&reference, 0x500, call->words, 1, &record))
        return 0;
    if (!record)
        return menu_result(call, noErr);
    if (!cfp_get(record + 0x10, &cache) || !cfp_get(US_TO_SYN68K(reference.toc) + 0x608, &list))
        return 0;
    if (!cache || !list)
        return menu_result(call, noErr);

    _Alignas(16) uint32 index = CLC(1);
    const uint32 search[] = {list, record, US_TO_SYN68K(&index)};
    uint32 found;
    if (!cfp_private(&reference, 0x137f4, search, NELEM(search), &found))
        return 0;
    if ((uint8)found) {
        const uint32 remove[] = {list, CL(index)};
        if (!cfp_private(&reference, 0x1380c, remove, NELEM(remove), NULL) ||
            !menu_forget_saved_pixels(&reference, record))
            return 0;
    }
    return menu_result(call, noErr);
}

static int carbon_InvalidateAllCachedMenuPixels(ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference reference;
    if (!menu_live_reference("InvalidateAllCachedMenuPixels", 0x11b8, &reference))
        return reference.function ? ROMlib_carbon_invoke_frame(reference.function, call) : 0;
    uint32 list;
    if (!cfp_get(US_TO_SYN68K(reference.toc) + 0x608, &list))
        return 0;
    if (!list)
        return 1;
    uint32 count;
    if (!cfp_private(&reference, 0x13944, &list, 1, &count) || count > 9)
        return 0;
    for (uint32 index = 1; index <= count; ++index) {
        _Alignas(16) uint32 record = 0;
        const uint32 element[] = {list, index, US_TO_SYN68K(&record)};
        if (!cfp_private(&reference, 0x1395c, element, NELEM(element), NULL) ||
            (record && !menu_forget_saved_pixels(&reference, CL(record))))
            return 0;
    }
    return !count || cfp_private(&reference, 0x13974, &list, 1, NULL);
}

static int menu_uses_saved_edit(uint32_t slot, uint32_t code_offset)
{
    ROMlib_carbon_reference menus;
    return ROMlib_carbon_probe_reference("MenusLib", "HideMenuBar", &menus) && menus.code_offset == 0x1c80 &&
           menus.code_size == 0x162a4 && ROMlib_carbon_toolbox_vector_matches(slot, &menus, code_offset);
}

static int carbon_InsertMenuItem(ROMlib_carbon_ppc_frame* call)
{
    if (!menu_uses_saved_edit(0x0e98, 0x5c84))
        return carbon_vector_InsertMenuItem(call);
    MenuHandle menu = carbon_menu_handle(call->words[0]);
    const int count = ROMlib_menu_item_count(menu);
    const int16_t after = call->words[2];
    if (count < 0 || after < 0 || after >= count)
        return carbon_vector_InsertMenuItem(call);
    const uint32_t flags = MI_ENABLE_FLAGS(menu);
    if (!carbon_vector_InsertMenuItem(call))
        return 0;
    const int inserted = ROMlib_menu_item_count(menu) - count;
    if (inserted > 0)
        ROMlib_menu_sync_saved_insert(menu, after, inserted, count, flags);
    return 1;
}

static int carbon_DeleteMenuItem(ROMlib_carbon_ppc_frame* call)
{
    if (!menu_uses_saved_edit(0x1348, 0x5e6c))
        return carbon_vector_DeleteMenuItem(call);
    MenuHandle menu = carbon_menu_handle(call->words[0]);
    const int count = ROMlib_menu_item_count(menu);
    const int16_t item = call->words[1];
    if (item <= 0 || item > count)
        return carbon_vector_DeleteMenuItem(call);
    const boolean_t first_extended_enabled = ROMlib_menu_item_enabled(menu, 32);
    if (!carbon_vector_DeleteMenuItem(call))
        return 0;
    if (ROMlib_menu_item_count(menu) == count - 1)
        ROMlib_menu_sync_item_removal(menu, item, first_extended_enabled);
    return 1;
}

enum { menu_invalid = -5623, menu_attributes_mask = 0xfffff, menu_attributes_with_side_effects = 0x0d };

static int menu_registry(const ROMlib_carbon_reference* reference, uint32* collection)
{
    const uint32 slot = cfp_toc(reference, (uint32)-0x504);
    if (!slot || !cfp_get(slot, collection))
        return 0;
    if (*collection)
        return 1;
    return cfp_private(reference, 0x56530, NULL, 0, collection) && cfp_write(slot, *collection);
}

static int menu_read_record(uint32 collection, uint32 menu, uint32 tag, uint32* value, int32* status)
{
    _Alignas(16) uint32 size = CLC(4), bytes = CL(*value);
    ROMlib_carbon_ppc_frame args = {.words = {collection, menu, tag, US_TO_SYN68K(&size), US_TO_SYN68K(&bytes)}};
    if (!carbon_GetCollectionItem(&args))
        return 0;
    *status = args.words[0];
    if (!*status)
        *value = CL(bytes);
    return 1;
}

static int menu_write_record(uint32 collection, uint32 menu, uint32 tag, uint32 value, int32* status)
{
    _Alignas(16) uint32 bytes = CL(value);
    ROMlib_carbon_ppc_frame args = {.words = {collection, menu, tag, 4, US_TO_SYN68K(&bytes)}};
    if (!carbon_AddCollectionItem(&args))
        return 0;
    *status = args.words[0];
    return 1;
}

enum menu_owner_operation { menu_owner_count, menu_owner_retain, menu_owner_release, menu_owner_dispose };

static int menu_owners(ROMlib_carbon_ppc_frame* call, enum menu_owner_operation operation)
{
    static const char* const names[] = {"GetMenuRetainCount", "RetainMenu", "ReleaseMenu", "DisposeMenu"};
    static const uint32 offsets[] = {0x6efcc, 0x6f078, 0x6f160, 0x5acc4};
    const uint32 menu = call->words[0];
    if (!menu)
        return menu_result(call, operation == menu_owner_count || operation == menu_owner_dispose ? 0 : menu_invalid);

    ROMlib_carbon_reference reference;
    if (!ROMlib_carbon_load_reference("CarbonLib", names[operation], &reference)) {
        if (operation == menu_owner_dispose)
            return carbon_vector_DisposeMenu(call);
        return menu_result(call, operation == menu_owner_count ? 0 : -4);
    }
    if (!menu_reference_matches(&reference, offsets[operation]))
        return ROMlib_carbon_invoke_frame(reference.function, call);

    const uint32 tag = TICK("rcnt");
    uint32 collection, count = 1;
    int32 status = memFullErr;
    if (!menu_registry(&reference, &collection))
        return 0;
    if (collection && !menu_read_record(collection, menu, tag, &count, &status))
        return 0;
    if (operation == menu_owner_count)
        return menu_result(call, collection ? count : 0);
    if (operation == menu_owner_retain) {
        if (status)
            return menu_result(call, collection ? paramErr : memFullErr);
        return menu_write_record(collection, menu, tag, count + 1, &status) && menu_result(call, status);
    }
    if (!status && count > 1)
        return menu_write_record(collection, menu, tag, count - 1, &status) && menu_result(call, status);
    if (status)
        return operation == menu_owner_dispose ? carbon_vector_DisposeMenu(call) : menu_result(call, noErr);

    /* The installed toolbox patch clears the per-menu collection records before disposing the handle. */
    return carbon_vector_DisposeMenu(call) && menu_result(call, noErr);
}

static int carbon_GetMenuRetainCount(ROMlib_carbon_ppc_frame* call)
{
    return menu_owners(call, menu_owner_count);
}

static int carbon_RetainMenu(ROMlib_carbon_ppc_frame* call)
{
    return menu_owners(call, menu_owner_retain);
}

static int carbon_ReleaseMenu(ROMlib_carbon_ppc_frame* call)
{
    return menu_owners(call, menu_owner_release);
}

static int carbon_DisposeMenu(ROMlib_carbon_ppc_frame* call)
{
    return menu_owners(call, menu_owner_dispose);
}

static int32 menu_set_mark_column(uint32 menu, uint32 exclude)
{
    const MenuHandle handle = carbon_menu_handle(menu);
    if (!handle)
        return menu_invalid;
    const uint32 value = CL(exclude);
    const int32 status = ROMlib_change_menu_item_property(handle, 0, TICK("macs"), TICK("mark"),
                                                          ROMLIB_MENU_PROPERTY_SET, sizeof value, 0, &value);
    HxX(handle, menuWidth) = CWC(-1);
    HxX(handle, menuHeight) = CWC(-1);
    return status;
}

static int menu_attributes(ROMlib_carbon_ppc_frame* call, int change)
{
    const uint32 menu = call->words[0], output = call->words[1];
    if (!menu)
        return menu_result(call, menu_invalid);
    const uint32 set = call->words[1] & menu_attributes_mask, clear = call->words[2] & menu_attributes_mask;
    if (change && set == clear)
        return menu_result(call, set ? paramErr : noErr);
    if (!change && (!output || !ROMlib_carbon_try_guest_memory(output, 4, 1)))
        return menu_result(call, paramErr);

    ROMlib_carbon_reference reference;
    const char* name = change ? "ChangeMenuAttributes" : "GetMenuAttributes";
    if (!ROMlib_carbon_load_reference("CarbonLib", name, &reference))
        return menu_result(call, -4);
    if (!menu_reference_matches(&reference, change ? 0x6d9b8 : 0x6d4dc))
        return ROMlib_carbon_invoke_frame(reference.function, call);
    if (change && ((set | clear) & menu_attributes_with_side_effects)) {
        if (!ROMlib_carbon_invoke_frame(reference.function, call))
            return 0;
        if (!call->words[0] && ((set | clear) & 1))
            return menu_result(call, menu_set_mark_column(menu, (set & ~clear) & 1));
        return 1;
    }

    const uint32 tag = TICK("attr");
    uint32 collection, attrs = 0;
    int32 status = memFullErr;
    if (!menu_registry(&reference, &collection))
        return 0;
    if (collection && !menu_read_record(collection, menu, tag, &attrs, &status))
        return 0;
    if (!change)
        return cfp_write(output, attrs & menu_attributes_mask) && menu_result(call, status);
    if (!status && !menu_write_record(collection, menu, tag, (attrs | set) & ~clear, &status))
        return 0;
    return menu_result(call, status);
}

static int carbon_GetMenuAttributes(ROMlib_carbon_ppc_frame* call)
{
    return menu_attributes(call, 0);
}

static int carbon_ChangeMenuAttributes(ROMlib_carbon_ppc_frame* call)
{
    return menu_attributes(call, 1);
}

static int carbon_CreateNewMenu(ROMlib_carbon_ppc_frame* call)
{
    const uint32 output = call->words[2];
    if (!output || !ROMlib_carbon_try_guest_memory(output, sizeof(uint32), 1))
        return menu_result(call, paramErr);
    ROMlib_carbon_reference reference;
    const int have_reference = ROMlib_carbon_load_reference("CarbonLib", "CreateNewMenu", &reference);
    if (!have_reference && call->words[1])
        return menu_result(call, -4);
    if (have_reference && !menu_reference_matches(&reference, 0x6d9d8))
        return ROMlib_carbon_invoke_frame(reference.function, call);
    _Alignas(16) const Byte title[] = {0};
    ROMlib_carbon_ppc_frame create = *call;
    create.words[0] = (int16)call->words[0];
    create.words[1] = US_TO_SYN68K(title);
    if (!carbon_vector_NewMenu(&create) || !cfp_write(output, create.words[0]))
        return 0;
    if (!create.words[0])
        return menu_result(call, memFullErr);
    if (have_reference && call->words[1]) {
        const uint32 attributes[] = {create.words[0], call->words[1], 0};
        uint32 status;
        if (!cfp_private(&reference, 0x6d7c4, attributes, NELEM(attributes), &status))
            return 0;
        if (!status && (call->words[1] & 1))
            menu_set_mark_column(create.words[0], 1);
    }
    return menu_result(call, noErr);
}

static int carbon_InvalidateMenuSize(ROMlib_carbon_ppc_frame* call)
{
    const MenuHandle menu = carbon_menu_handle(call->words[0]);
    if (!menu)
        return menu_result(call, menu_invalid);
    HxX(menu, menuWidth) = CWC(-1);
    HxX(menu, menuHeight) = CWC(-1);
    return menu_result(call, noErr);
}

static int carbon_IsMenuSizeInvalid(ROMlib_carbon_ppc_frame* call)
{
    const MenuHandle menu = carbon_menu_handle(call->words[0]);
    return menu_result(call, menu && (HxX(menu, menuWidth) == CWC(-1) || HxX(menu, menuHeight) == CWC(-1)));
}

static int menu_get_reference(const char* name, uint32 offset, ROMlib_carbon_ppc_frame* call,
                              ROMlib_carbon_reference* reference, int unavailable)
{
    if (!ROMlib_carbon_load_reference("CarbonLib", name, reference)) {
        menu_result(call, unavailable);
        return 2;
    }
    if (!menu_reference_matches(reference, offset))
        return ROMlib_carbon_invoke_frame(reference->function, call) ? 2 : 0;
    return 1;
}

static int carbon_DuplicateMenuBar(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t bar = call->words[0], output = call->words[1];
    if (!bar || !output)
        return menu_result(call, paramErr);
    ROMlib_carbon_reference ref;
    const int available = menu_get_reference("DuplicateMenuBar", 0x5c980, call, &ref, -4);
    if (available != 1)
        return available != 0;
    if (!cfp_write(output, bar))
        return 0;
    uint32_t status, copy;
    if (!cfp_private(&ref, 0xb5940, &output, 1, &status))
        return 0;
    if (status)
        return menu_result(call, status);
    /* The handle copy shares its menus. Give each copied list entry a reference. */
    return cfp_get(output, &copy) && cfp_private(&ref, 0x5c224, &copy, 1, NULL) && menu_result(call, noErr);
}

static int carbon_DisposeMenuBar(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t bar = call->words[0];
    if (!bar)
        return menu_result(call, paramErr);
    ROMlib_carbon_reference ref;
    const int available = menu_get_reference("DisposeMenuBar", 0x5c9f0, call, &ref, -4);
    if (available != 1)
        return available != 0;
    const uint32_t release[] = {bar, 1};
    return cfp_private(&ref, 0x5c508, release, NELEM(release), NULL) && cfp_private(&ref, 0xb20a0, &bar, 1, NULL) &&
           menu_result(call, noErr);
}

static int carbon_RegisterMenuDefinition(ROMlib_carbon_ppc_frame* call)
{
    const int16_t id = (int16_t)call->words[0];
    const uint32_t definition = call->words[1];
    if (!id || id == 63)
        return menu_result(call, paramErr);
    ROMlib_carbon_reference ref;
    const int available = menu_get_reference("RegisterMenuDefinition", 0x6eaec, call, &ref, -4);
    if (available != 1)
        return available != 0;
    const uint32_t slot = cfp_toc(&ref, (uint32_t)-0x4c8);
    uint32_t registry;
    if (!slot || !cfp_get(slot, &registry))
        return 0;
    if (!registry) {
        if (!cfp_private(&ref, 0x56530, NULL, 0, &registry) || !cfp_write(slot, registry))
            return 0;
        if (!registry)
            return menu_result(call, memFullErr);
    }
    if (!definition) {
        const uint32_t purge[] = {registry, (uint32_t)id};
        return cfp_private(&ref, 0xb6828, purge, NELEM(purge), NULL) && menu_result(call, noErr);
    }
    uint32_t kind, procedure, status;
    if (!cfp_get(definition, &kind) || !cfp_get(definition + 4, &procedure))
        return 0;
    if (kind || !procedure)
        return menu_result(call, paramErr);
    const uint32_t add[] = {registry, (uint32_t)id, definition + 4, sizeof procedure};
    return cfp_private(&ref, 0x70f28, add, NELEM(add), &status) && menu_result(call, status);
}

static int carbon_IsValidMenu(ROMlib_carbon_ppc_frame* call)
{
    /* CarbonLib 1.4 checks presence, without inspecting the handle. */
    return menu_result(call, call->words[0] != 0);
}

static int carbon_ScrollMenuImage(ROMlib_carbon_ppc_frame* call)
{
    if (!call->words[0])
        return menu_result(call, menu_invalid);
    ROMlib_carbon_reference ref;
    const int available = menu_get_reference("_ScrollMenuImage", 0x70ee0, call, &ref, -4);
    if (available != 1)
        return available != 0;
    const uint32_t scroll[] = {call->words[1], (int16_t)call->words[2], (int16_t)call->words[3], 0};
    return cfp_private(&ref, 0xb1608, scroll, NELEM(scroll), NULL) && menu_result(call, noErr);
}

static int carbon_CopyMenuItems(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t source = call->words[0], destination = call->words[3], count = call->words[2];
    const unsigned first = (uint16_t)call->words[1], after = (uint16_t)call->words[4];
    MenuHandle source_menu = carbon_menu_handle(source), destination_menu = carbon_menu_handle(destination);
    if (!source_menu || !destination_menu)
        return menu_result(call, menu_invalid);
    const unsigned source_count = ROMlib_menu_item_count(source_menu);
    const unsigned destination_count = ROMlib_menu_item_count(destination_menu);
    if (source == destination || !count || !first || first > source_count || count > source_count - first + 1 ||
        after > destination_count || count > UINT16_MAX - destination_count)
        return menu_result(call, paramErr);

    ROMlib_carbon_reference ref;
    const int available = menu_get_reference("CopyMenuItems", 0x18f48, call, &ref, -4);
    if (available != 1)
        return available != 0;
    _Alignas(4) uint32_t attributes = 0;
    const uint32_t query[] = {destination, US_TO_SYN68K(&attributes)};
    if (!cfp_private(&ref, 0xb2838, query, NELEM(query), NULL))
        return 0;
    const bool suspended = (CL(attributes) & 4) != 0;
    const uint32_t suspend[] = {destination, 0, 4}, resume[] = {destination, 4, 0};
    if (suspended && !cfp_private(&ref, 0xb2850, suspend, NELEM(suspend), NULL))
        return 0;

    /* The packed item record carries both Pascal and CFString text and a
       reusable collection for copied metadata. */
    _Alignas(128) Byte record[0x50] = {0};
    _Alignas(256) Byte text[256] = {0};
    const uint32_t fields = US_TO_SYN68K(record);
    const uint32_t placeholder = cfp_toc(&ref, (uint32_t)-0x1fc);
    int ok = placeholder && cfp_write(fields + 8, US_TO_SYN68K(text));
    uint32_t status = noErr;
    for (uint32_t index = 0; ok && !status && index < count; ++index) {
        const uint32_t target = after + index + 1, item = first + index;
        const uint32_t insert[] = {destination, placeholder + 2, (int16_t)(target - 1)};
        const uint32_t read[] = {source, item, 0, fields}, write[] = {destination, target, 0, fields};
        const uint32_t properties[] = {source, destination, item, target};
        uint32_t string = 0;
        ok = cfp_private(&ref, 0xb2520, insert, NELEM(insert), NULL) && cfp_write(fields, 0) &&
             cfp_write(fields + 4, UINT32_C(0x001fffff)) && cfp_write(fields + 0x42, 0) &&
             cfp_private(&ref, 0xb37c8, read, NELEM(read), &status);
        if (ok && !status)
            ok = cfp_private(&ref, 0xb3990, write, NELEM(write), &status);
        if (ok)
            ok = cfp_get(fields + 0x42, &string) && (!string || cfp_private(&ref, 0xb6330, &string, 1, NULL));
        if (ok && !status)
            ok = cfp_private(&ref, 0x188c4, properties, NELEM(properties), &status);
    }
    uint32_t collection;
    const int cleaned =
        cfp_get(fields + 0x46, &collection) && (!collection || cfp_private(&ref, 0xb6270, &collection, 1, NULL));
    const int restored = !suspended || cfp_private(&ref, 0xb2850, resume, NELEM(resume), NULL);
    return ok && cleaned && restored && menu_result(call, status ? menu_invalid : noErr);
}

static int menu_backend(const ROMlib_carbon_reference* ref, int flags_offset, unsigned library_name,
                        unsigned symbol_name, uint32_t* delegate)
{
    const uint32_t flag = cfp_toc(ref, (uint32_t)flags_offset), slot = cfp_toc(ref, (uint32_t)(flags_offset + 4));
    Byte* initialized = flag ? ROMlib_carbon_guest_memory(flag, 1, 1) : NULL;
    if (!initialized || !slot)
        return 0;
    if (*initialized)
        return cfp_get(slot, delegate);
    const uint32_t strings = cfp_toc(ref, (uint32_t)-0x148);
    const uint32_t lookup[] = {strings + library_name, strings + symbol_name};
    if (!strings || !cfp_private(ref, 0x55c5c, lookup, NELEM(lookup), delegate) || !cfp_write(slot, *delegate))
        return 0;
    *initialized = 1;
    return 1;
}

static int carbon_AppendResMenu(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t menu = call->words[0];
    if (!menu)
        return 1;
    ROMlib_carbon_reference ref;
    const int available = menu_get_reference("AppendResMenu", 0x5a9e8, call, &ref, 0);
    if (available != 1)
        return available != 0;
    uint32_t backend;
    if (!menu_backend(&ref, -0x93c, 3, 0x11, &backend))
        return 0;
    if (!backend)
        return 1;
    uint32_t master, definition;
    if (!cfp_get(menu, &master) || !master || !cfp_get(master + offsetof(MenuInfo, menuProc), &definition))
        return 0;
    const uint32_t args[] = {menu, call->words[1]};
    return cfp_callback(backend, args, NELEM(args), NULL) && cfp_private(&ref, 0x6d734, &menu, 1, NULL) &&
           cfp_get(menu, &master) && master && cfp_write(master + offsetof(MenuInfo, menuProc), definition);
}

static int insert_script_menu_resources(ROMlib_carbon_ppc_frame* call, bool fonts)
{
    const uint32_t menu = call->words[0];
    if (!menu)
        return 1;
    ROMlib_carbon_reference ref;
    const int available =
        menu_get_reference(fonts ? "InsertFontResMenu" : "InsertIntlResMenu", fonts ? 0x5e7b8 : 0x5e84c, call, &ref, 0);
    if (available != 1)
        return available != 0;
    uint32_t backend;
    if (!menu_backend(&ref, fonts ? -0x7c4 : -0x7bc, 3, fonts ? 0x1ac : 0x1bf, &backend))
        return 0;
    const uint32_t args[] = {menu, fonts ? (uint32_t)(int16_t)call->words[1] : call->words[1], (int16_t)call->words[2],
                             (int16_t)call->words[3]};
    return !backend || (cfp_callback(backend, args, fonts ? 3 : 4, NULL) && cfp_private(&ref, 0x6d734, &menu, 1, NULL));
}

static int carbon_InsertFontResMenu(ROMlib_carbon_ppc_frame* call)
{
    return insert_script_menu_resources(call, true);
}

static int carbon_InsertIntlResMenu(ROMlib_carbon_ppc_frame* call)
{
    return insert_script_menu_resources(call, false);
}

static int carbon_CallMenuDef(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t menu = call->words[0];
    if (!menu)
        return 1;
    ROMlib_carbon_reference ref;
    const int available = menu_get_reference("CallMenuDef", 0x5bb60, call, &ref, 0);
    if (available != 1)
        return available != 0;
    uint32_t backend;
    if (!menu_backend(&ref, -0x878, 0x5a, 0x64, &backend))
        return 0;
    const uint32_t args[] = {menu, (int16_t)call->words[1], call->words[2], call->words[3], call->words[4]};
    if (backend)
        return cfp_callback(backend, args, NELEM(args), NULL);

    uint32_t master, definition, code, flags;
    if (!cfp_get(menu, &master) || !master || !cfp_get(master + offsetof(MenuInfo, menuProc), &definition))
        return 0;
    if (!definition)
        return 1;
    if (!cfp_get(definition, &code))
        return 0;
    if (!code && (!cfp_private(&ref, 0xb1fc8, &definition, 1, NULL) || !cfp_get(definition, &code)))
        return 0;
    if (!code) {
        const uint32_t menu_not_found = 84;
        return cfp_private(&ref, 0xb6cd8, &menu_not_found, 1, NULL);
    }
    if (!cfp_private(&ref, 0xb2220, &definition, 1, &flags) || !cfp_private(&ref, 0xb2130, &definition, 1, NULL))
        return 0;
    if (!cfp_get(definition, &code))
        return 0;
    const uint32_t invoke[] = {args[1], menu, args[2], args[3], args[4], code};
    const int called = cfp_private(&ref, 0x5bb18, invoke, NELEM(invoke), NULL);
    const uint32_t restore[] = {definition, flags};
    return cfp_private(&ref, 0xb2238, restore, NELEM(restore), NULL) && called;
}

static int menu_read_submenu(const ROMlib_carbon_reference* ref, uint32_t menu, uint16_t item, uint32_t* child,
                             uint32_t* status)
{
    _Alignas(8) uint32_t stored = 0;
    const uint32_t property[] = {menu, item, TICK("subm"), sizeof stored, 0, US_TO_SYN68K(&stored)};
    if (!cfp_private(ref, 0xb4608, property, NELEM(property), status))
        return 0;
    *child = CL(stored);
    return 1;
}

static int menu_hierarchy_delegate(const ROMlib_carbon_reference* ref, int set, const uint32_t* args, uint32_t* status)
{
    const uint32_t flags = cfp_toc(ref, (uint32_t)(set ? -0x7f8 : -0x800));
    const uint32_t slot = cfp_toc(ref, (uint32_t)(set ? -0x7f4 : -0x7fc));
    Byte* initialized = flags ? ROMlib_carbon_guest_memory(flags, 1, 1) : NULL;
    uint32_t delegate;
    if (!initialized || !slot)
        return 0;
    if (!*initialized) {
        const uint32_t strings = cfp_toc(ref, (uint32_t)-0x148);
        const uint32_t names[] = {strings + 0x115, strings + (set ? 0x13f : 0x124)};
        if (!strings || !cfp_private(ref, 0x55c5c, names, NELEM(names), &delegate) || !cfp_write(slot, delegate))
            return 0;
        *initialized = 1;
    }
    if (!cfp_get(slot, &delegate))
        return 0;
    *status = noErr;
    return !delegate || cfp_callback(delegate, args, 3, status);
}

static int carbon_GetMenuItemHierarchicalID(ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference ref;
    const int available = menu_get_reference("GetMenuItemHierarchicalID", 0x5de7c, call, &ref, -4);
    if (available != 1)
        return available != 0;
    const uint32_t menu = call->words[0], output = call->words[2];
    const int16_t item = call->words[1];
    uint16_t* identifier = output ? ROMlib_carbon_try_guest_memory(output, sizeof *identifier, 1) : NULL;
    if (!identifier)
        return menu_result(call, paramErr);
    uint32_t child, status;
    if (!menu_read_submenu(&ref, menu, (uint16_t)item, &child, &status))
        return 0;
    if (!status) {
        uint32_t id;
        if (!cfp_private(&ref, 0xb2940, &child, 1, &id))
            return 0;
        *identifier = CW((uint16_t)id);
        return menu_result(call, noErr);
    }

    const uint32_t args[] = {menu, (int32_t)item, output};
    return menu_hierarchy_delegate(&ref, 0, args, &status) && menu_result(call, status);
}

static int update_submenu_count(const ROMlib_carbon_reference* ref, uint32_t menu, int change)
{
    uint32_t registry, status;
    if (!cfp_private(ref, 0x6d3e0, NULL, 0, &registry))
        return 0;
    if (!registry)
        return 1;
    _Alignas(8) uint16_t count = 0;
    const uint32_t get[] = {registry, menu, TICK("scnt"), 0, US_TO_SYN68K(&count)};
    if (!cfp_private(ref, 0xb6048, get, NELEM(get), &status))
        return 0;
    if (status)
        return 1;
    count = CW(CW(count) + change);
    const uint32_t save[] = {registry, menu, TICK("scnt"), sizeof count, US_TO_SYN68K(&count)};
    return cfp_private(ref, 0xb6060, save, NELEM(save), NULL);
}

static int remove_root_submenu(const ROMlib_carbon_reference* ref, uint16_t item, uint32_t child)
{
    const uint32_t placeholder_slot = cfp_toc(ref, (uint32_t)-0x940);
    uint32_t placeholder, list, record, first;
    if (!placeholder_slot || !cfp_get(placeholder_slot, &placeholder) || !cfp_get(0xa1c, &list))
        return 0;
    if (!placeholder) {
        const uint32_t create[] = {-0x3fff, cfp_toc(ref, (uint32_t)-0x148)};
        if (!cfp_private(ref, 0xb6090, create, NELEM(create), &placeholder) ||
            !cfp_write(placeholder_slot, placeholder))
            return 0;
    }
    if (!list || !cfp_get(list, &record) || !record || !cfp_get(record + 6, &first))
        return 0;
    const uint32_t remove_entry[] = {list, 6U * (item + (first == placeholder)), 0, 6, UINT32_MAX, 0};
    uint32_t position;
    if (!cfp_private(ref, 0xb60c0, remove_entry, NELEM(remove_entry), &position) || (int32_t)position < 0 ||
        !cfp_get(list, &record))
        return 0;
    uint16_t* bytes = ROMlib_carbon_guest_memory(record, sizeof *bytes, 1);
    if (!bytes)
        return 0;
    *bytes = CW(CW(*bytes) - 6);
    const uint32_t refresh[] = {2, 0, 0};
    return cfp_private(ref, 0xb24a8, &child, 1, NULL) && cfp_private(ref, 0xb5040, refresh, NELEM(refresh), NULL) &&
           cfp_private(ref, 0xb60d8, NULL, 0, NULL);
}

static int carbon_SetMenuItemHierarchicalID(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t menu = call->words[0];
    const uint16_t item = call->words[1];
    const int16_t id = call->words[2];
    const MenuHandle handle = carbon_menu_handle(menu);
    if (!handle)
        return menu_result(call, menu_invalid);
    if (!item || item > ROMlib_menu_item_count(handle))
        return menu_result(call, paramErr);
    ROMlib_carbon_reference ref;
    const int available = menu_get_reference("SetMenuItemHierarchicalID", 0x5dff8, call, &ref, -4);
    if (available != 1)
        return available != 0;
    _Alignas(8) uint32_t stored = 0;
    const uint32_t lookup[] = {menu, item, US_TO_SYN68K(&stored)};
    if (!cfp_private(&ref, 0xb38d0, lookup, NELEM(lookup), NULL))
        return 0;
    const uint32_t child = CL(stored);
    const uint32_t root_slot = cfp_toc(&ref, (uint32_t)-0x4c4);
    uint32_t root, child_id = 0;
    if (!root_slot || !cfp_get(root_slot, &root) || (child && !cfp_private(&ref, 0xb2940, &child, 1, &child_id)))
        return 0;
    if (!!child != !!id && !update_submenu_count(&ref, menu, child ? -1 : 1))
        return 0;
    if (child && menu == root && !remove_root_submenu(&ref, item, child))
        return 0;

    uint32_t status;
    const uint32_t legacy[] = {menu, (int16_t)item, id};
    if (!menu_hierarchy_delegate(&ref, 1, legacy, &status))
        return 0;
    if (status)
        return menu_result(call, status);
    const uint32_t invalidate[] = {menu, item, 1, 1};
    if (child && !cfp_private(&ref, 0x705c8, invalidate, NELEM(invalidate), NULL))
        return 0;
    return ((!id && !child) || (child && (int16_t)child_id == id) ||
            (cfp_private(&ref, 0xb39c0, invalidate, 3, NULL) &&
             cfp_private(&ref, 0xb4728, invalidate, NELEM(invalidate), NULL))) &&
           menu_result(call, noErr);
}

static int carbon_GetMenuItemHierarchicalMenu(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t menu = call->words[0], output = call->words[2];
    const uint16_t item = call->words[1];
    const MenuHandle handle = carbon_menu_handle(menu);
    if (!handle)
        return menu_result(call, menu_invalid);
    if (!output || !item || item > ROMlib_menu_item_count(handle))
        return menu_result(call, paramErr);
    ROMlib_carbon_reference ref;
    const int available = menu_get_reference("GetMenuItemHierarchicalMenu", 0x6fd44, call, &ref, -4);
    if (available != 1)
        return available != 0;

    uint32_t child, status;
    if (!menu_read_submenu(&ref, menu, item, &child, &status))
        return 0;
    if (status) {
        _Alignas(8) uint16_t id = 0;
        const uint32_t legacy[] = {menu, (int16_t)item, US_TO_SYN68K(&id)};
        if (!cfp_private(&ref, 0xb2700, legacy, NELEM(legacy), &status))
            return 0;
        child = 0;
        const uint32_t legacy_id = (int16_t)CW(id);
        if (!status && legacy_id && !cfp_private(&ref, 0xb57c0, &legacy_id, 1, &child))
            return 0;
    }
    return cfp_write(output, child) && menu_result(call, noErr);
}

static int prepare_root_submenu(const ROMlib_carbon_reference* ref, uint32_t child)
{
    uint32_t identifier, found = 0;
    if (!cfp_private(ref, 0xb2940, &child, 1, &identifier) ||
        (identifier && !cfp_private(ref, 0xb57c0, &identifier, 1, &found)))
        return 0;
    if (identifier && (!found || found == child))
        return 1;
    const uint32_t reassign[] = {child, 0, 0};
    _Alignas(8) uint16_t original = CW((uint16_t)identifier);
    const uint32_t save[] = {child, 0, TICK("mnid"), sizeof original, US_TO_SYN68K(&original)};
    return cfp_private(ref, 0x5ce54, reassign, NELEM(reassign), NULL) &&
           cfp_private(ref, 0xb4650, save, NELEM(save), NULL);
}

static int insert_root_submenu(const ROMlib_carbon_reference* ref, uint16_t item, uint32_t child, uint32_t* status)
{
    uint32_t list;
    if (!prepare_root_submenu(ref, child) || !cfp_get(0xa1c, &list))
        return 0;
    const uint32_t insert[] = {list, item, child};
    return cfp_private(ref, 0x6f3d4, insert, NELEM(insert), status);
}

static int carbon_SetMenuItemHierarchicalMenu(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t menu = call->words[0], child = call->words[2];
    const uint16_t item = call->words[1];
    const MenuHandle handle = carbon_menu_handle(menu);
    if (!handle || (child && !carbon_menu_handle(child)))
        return menu_result(call, menu_invalid);
    if (!item || item > ROMlib_menu_item_count(handle))
        return menu_result(call, paramErr);
    ROMlib_carbon_reference ref;
    const int available = menu_get_reference("SetMenuItemHierarchicalMenu", 0x6f4b0, call, &ref, -4);
    if (available != 1)
        return available != 0;

    uint32_t legacy[] = {menu, (int16_t)item, 0};
    if (!cfp_private(&ref, 0xb2460, legacy, NELEM(legacy), NULL))
        return 0;
    if (!child)
        return menu_result(call, noErr);
    const uint32_t update = cfp_toc(&ref, (uint32_t)-0x4c0);
    Byte* dirty = ROMlib_carbon_guest_memory(update, 1, 1);
    const uint32_t root_slot = cfp_toc(&ref, (uint32_t)-0x4c4);
    uint32_t root;
    if (!dirty || !root_slot || !cfp_get(root_slot, &root))
        return 0;
    *dirty = 1;
    if (!cfp_private(&ref, 0xb3798, &child, 1, NULL))
        return 0;
    uint32_t status = noErr;
    if (menu == root && !insert_root_submenu(&ref, item, child, &status))
        return 0;
    if (!status) {
        if (!cfp_private(&ref, 0xb2940, &child, 1, &legacy[2]))
            return 0;
        if (!legacy[2])
            legacy[2] = 1;
        _Alignas(8) uint32_t stored = CL(child);
        const uint32_t property[] = {menu, item, TICK("subm"), sizeof stored, US_TO_SYN68K(&stored)};
        if (!cfp_private(&ref, 0xb2460, legacy, NELEM(legacy), NULL) ||
            !cfp_private(&ref, 0xb4650, property, NELEM(property), &status))
            return 0;
        if (!status) {
            const uint32_t invalidate[] = {menu, 1};
            return cfp_private(&ref, 0x5db54, invalidate, NELEM(invalidate), NULL) && menu_result(call, noErr);
        }
        legacy[2] = 0;
        if (!cfp_private(&ref, 0xb2460, legacy, NELEM(legacy), NULL))
            return 0;
    }
    return cfp_private(&ref, 0xb24a8, &child, 1, NULL) && menu_result(call, status);
}

static int carbon_GetMenuTitleIcon(ROMlib_carbon_ppc_frame* call)
{
    const MenuHandle menu = carbon_menu_handle(call->words[0]);
    if (!menu)
        return menu_result(call, menu_invalid);
    const Byte* title = HxX(menu, menuData);
    const uint32_t type_output = call->words[1], icon_output = call->words[2];
    const int suite = title[0] == 5 && title[1] == 1;
    uint32_t icon = 0;
    if (suite && icon_output && !cfp_get(US_TO_SYN68K(title) + 2, &icon))
        return 0;
    return (!type_output || cfp_write(type_output, suite ? 5 : 0)) && (!icon_output || cfp_write(icon_output, icon)) &&
           menu_result(call, noErr);
}

static int carbon_SetMenuTitleIcon(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t menu = call->words[0], type = call->words[1], icon = call->words[2];
    if (!carbon_menu_handle(menu))
        return menu_result(call, menu_invalid);
    if ((type != 0 && type != 5 && type != 6) || (!type != !icon))
        return menu_result(call, paramErr);
    ROMlib_carbon_reference ref;
    const int available = menu_get_reference("SetMenuTitleIcon", 0x7085c, call, &ref, -4);
    if (available != 1)
        return available != 0;

    _Alignas(16) Byte title[6] = {0};
    uint32_t suite = icon;
    if (type == 6) {
        _Alignas(8) uint32_t family_handle = 0, suite_handle = 0;
        const uint32_t all_icons = 0xff00;
        const uint32_t to_family[] = {icon, all_icons, US_TO_SYN68K(&family_handle)};
        uint32_t status;
        if (!cfp_private(&ref, 0xb6708, to_family, NELEM(to_family), &status))
            return 0;
        if (status)
            return menu_result(call, status);
        const uint32_t family = CL(family_handle);
        const uint32_t to_suite[] = {family, all_icons, US_TO_SYN68K(&suite_handle)};
        const int converted = cfp_private(&ref, 0xb6720, to_suite, NELEM(to_suite), &status);
        if (!cfp_private(&ref, 0xb20a0, &family, 1, NULL) || !converted)
            return 0;
        if (status)
            return menu_result(call, status);
        suite = CL(suite_handle);
    }
    if (type) {
        title[0] = 5;
        title[1] = 1;
        const uint32_t encoded = CL(suite);
        memcpy(title + 2, &encoded, sizeof encoded);
    }
    const uint32_t set_title[] = {menu, US_TO_SYN68K(title)};
    const uint32_t attributes[] = {menu, 0x04000000, 0};
    return cfp_private(&ref, 0xb29e8, set_title, NELEM(set_title), NULL) &&
           cfp_private(&ref, 0x6d7c4, attributes, NELEM(attributes), NULL) && menu_result(call, noErr);
}

static int carbon_GetMenuDefinition(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t menu = call->words[0], output = call->words[1];
    if (!carbon_menu_handle(menu))
        return menu_result(call, menu_invalid);
    if (!output || !ROMlib_carbon_try_guest_memory(output, 8, 1))
        return menu_result(call, paramErr);
    ROMlib_carbon_reference ref;
    const int available = menu_get_reference("GetMenuDefinition", 0x6eb9c, call, &ref, -4);
    if (available != 1)
        return available != 0;
    const uint32_t args[] = {menu, output + 4};
    uint32_t custom;
    if (!cfp_private(&ref, 0xb0fd8, args, NELEM(args), &custom) || !cfp_write(output, 0) ||
        (!custom && !cfp_write(output + 4, 0)))
        return 0;
    return menu_result(call, custom ? noErr : -5621);
}

static int set_menu_definition(const ROMlib_carbon_reference* ref, uint32_t menu, uint32_t specification,
                               uint32_t* status)
{
    MenuHandle handle = carbon_menu_handle(menu);
    if (!handle) {
        *status = menu_invalid;
        return 1;
    }
    *status = paramErr;
    uint32_t kind, callback;
    if (!specification)
        return 1;
    if (!cfp_get(specification, &kind) || !cfp_get(specification + 4, &callback))
        return 0;
    if (kind || !callback)
        return 1;

    _Alignas(32) Byte message_record[8] = {0};
    _Alignas(8) uint32_t previous = 0;
    _Alignas(8) uint16_t selection = 0;
    const uint32_t rectangle = cfp_toc(ref, (uint32_t)-0x4f0);
    const uint32_t point = cfp_toc(ref, (uint32_t)-0x4ec);
    uint32_t packed_point, has_definition;
    const uint32_t original[] = {menu, US_TO_SYN68K(&previous)};
    if (!rectangle || !point || !cfp_move(US_TO_SYN68K(message_record), rectangle, sizeof message_record) ||
        !cfp_get(point, &packed_point) || !cfp_private(ref, 0xb0fd8, original, NELEM(original), &has_definition))
        return 0;
    uint32_t message[] = {menu, 9, US_TO_SYN68K(message_record), packed_point, US_TO_SYN68K(&selection)};
    if (has_definition) {
        if (!cfp_private(ref, 0xb4638, message, NELEM(message), NULL))
            return 0;
    }
    else {
        const uint32_t resource[] = {TICK("MDEF"), 127};
        uint32_t definition;
        if (!cfp_private(ref, 0xb1fb0, resource, NELEM(resource), &definition))
            return 0;
        if (!definition) {
            *status = memFullErr;
            return 1;
        }
        const uint32_t record = cfp_read(menu);
        if (!record || !cfp_write(record + offsetof(MenuInfo, menuProc), definition))
            return 0;
    }
    const uint32_t install[] = {callback, 0, 0};
    selection = 0;
    message[1] = 8;
    if (!cfp_private(ref, 0x55f14, install, NELEM(install), NULL) ||
        !cfp_private(ref, 0xb4638, message, NELEM(message), NULL))
        return 0;
    *status = selection ? memFullErr : noErr;
    return *status || cfp_private(ref, 0xb3750, &menu, 1, NULL);
}

static int carbon_SetMenuDefinition(ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference ref;
    const int available = menu_get_reference("SetMenuDefinition", 0x6dab4, call, &ref, -4);
    if (available != 1)
        return available != 0;
    uint32_t status;
    return set_menu_definition(&ref, call->words[0], call->words[1], &status) && menu_result(call, status);
}

static int carbon_CreateCustomMenu(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t specification = call->words[0], output = call->words[3];
    if (!output)
        return menu_result(call, paramErr);
    if (!cfp_write(output, 0))
        return 0;
    uint32_t callback;
    if (!specification)
        return menu_result(call, paramErr);
    if (!cfp_get(specification + 4, &callback))
        return 0;
    if (!callback)
        return menu_result(call, paramErr);
    ROMlib_carbon_reference ref;
    const int available = menu_get_reference("CreateCustomMenu", 0x6dc4c, call, &ref, -4);
    if (available != 1)
        return available != 0;
    const uint32_t create[] = {(int16_t)call->words[1], call->words[2], output};
    uint32_t status, menu;
    if (!cfp_private(&ref, 0xb2418, create, NELEM(create), &status))
        return 0;
    if (status)
        return menu_result(call, status);
    if (!cfp_get(output, &menu) || !set_menu_definition(&ref, menu, specification, &status))
        return 0;
    return (!status || (cfp_private(&ref, 0xb24a8, &menu, 1, NULL) && cfp_write(output, 0))) &&
           menu_result(call, status);
}

static int carbon_AcquireRootMenu(ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference ref;
    const int available = menu_get_reference("AcquireRootMenu", 0x6fb40, call, &ref, 0);
    if (available != 1)
        return available != 0;
    const uint32_t slot = cfp_toc(&ref, (uint32_t)-0x4c4);
    uint32_t root;
    if (!slot || !cfp_get(slot, &root))
        return 0;
    if (root) {
        if (!cfp_private(&ref, 0xb3798, &root, 1, NULL))
            return 0;
        uint32_t collection, attributes = 0;
        int32_t status = -4;
        if (!menu_registry(&ref, &collection) ||
            (collection && !menu_read_record(collection, root, TICK("attr"), &attributes, &status)))
            return 0;
        if (!status && (attributes & 0x10000000))
            return menu_result(call, root);
    }
    else {
        uint32_t previous_zone, application_zone;
        if (!cfp_private(&ref, 0xb53b8, NULL, 0, &previous_zone) ||
            !cfp_private(&ref, 0xb57f0, NULL, 0, &application_zone) ||
            !cfp_private(&ref, 0xb5448, &application_zone, 1, NULL))
            return 0;
        _Alignas(8) uint32_t created = 0;
        const uint32_t args[] = {0, 0, US_TO_SYN68K(&created)};
        uint32_t status;
        const int ok = cfp_private(&ref, 0xb2418, args, NELEM(args), &status);
        if (!cfp_private(&ref, 0xb5448, &previous_zone, 1, NULL) || !ok)
            return 0;
        if (status)
            return menu_result(call, 0);
        root = CL(created);
    }
    uint32_t status;
    if (!cfp_private(&ref, 0x6f9e0, &root, 1, &status))
        return 0;
    if (!status)
        return menu_result(call, root);
    return cfp_private(&ref, 0xb24a8, &root, 1, NULL) && menu_result(call, 0);
}

static int copy_root_hierarchy(const ROMlib_carbon_reference* ref, uint32_t menu)
{
    _Alignas(8) uint32_t data = 0;
    const uint32_t property[] = {menu, 0, TICK("hmnu"), sizeof data, 0, US_TO_SYN68K(&data)};
    uint32_t status;
    if (!cfp_private(ref, 0xb4608, property, NELEM(property), &status))
        return 0;
    if (status)
        return 1;
    const uint32_t hierarchy = CL(data);
    uint32_t list;
    if (!hierarchy || !cfp_get(0xa1c, &list) || !list || !cfp_private(ref, 0x5c224, &hierarchy, 1, NULL) ||
        !cfp_private(ref, 0xb2130, &hierarchy, 1, NULL))
        return 0;
    uint32_t list_bytes, bytes, address;
    int ok = cfp_private(ref, 0xb58c8, &list, 1, &list_bytes) && list_bytes >= 6 && list_bytes <= INT32_MAX;
    if (ok) {
        const uint32_t resize[] = {list, list_bytes - 6};
        ok = cfp_private(ref, 0xb2118, resize, NELEM(resize), NULL) &&
             cfp_private(ref, 0xb58c8, &hierarchy, 1, &bytes) && bytes <= INT32_MAX && cfp_get(hierarchy, &address);
    }
    if (ok) {
        const uint32_t append[] = {address, list, bytes};
        ok = cfp_private(ref, 0xb2148, append, NELEM(append), NULL);
    }
    return cfp_private(ref, 0xb20d0, &hierarchy, 1, NULL) && ok;
}

static int carbon_SetRootMenu(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t menu = call->words[0];
    const MenuHandle handle = carbon_menu_handle(menu);
    if (!handle)
        return menu_result(call, menu_invalid);
    ROMlib_carbon_reference ref;
    const int available = menu_get_reference("SetRootMenu", 0x6fe38, call, &ref, -4);
    if (available != 1)
        return available != 0;
    const uint32_t slot = cfp_toc(&ref, (uint32_t)-0x4c4);
    uint32_t previous;
    if (!slot || !cfp_get(slot, &previous))
        return 0;
    if (previous == menu)
        return menu_result(call, noErr);
    if ((previous && (!cfp_private(&ref, 0xb24a8, &previous, 1, NULL) || !cfp_write(slot, 0))) ||
        !cfp_private(&ref, 0xb25f8, NULL, 0, NULL))
        return 0;

    const int count = ROMlib_menu_item_count(handle);
    for (int item = 1; item <= count; ++item) {
        _Alignas(8) uint32_t stored = 0;
        const uint32_t read[] = {menu, (uint16_t)item, US_TO_SYN68K(&stored)};
        uint32_t status;
        if (!cfp_private(&ref, 0xb38d0, read, NELEM(read), &status))
            return 0;
        if (status)
            return menu_result(call, paramErr);
        const uint32_t child = CL(stored);
        const uint32_t insert[] = {child, 0};
        if (!prepare_root_submenu(&ref, child) || !cfp_private(&ref, 0xb25c8, insert, NELEM(insert), NULL))
            return 0;
    }
    const uint32_t attributes[] = {menu, 0, 0x10000000};
    if (!copy_root_hierarchy(&ref, menu) || !cfp_private(&ref, 0x6d7c4, attributes, NELEM(attributes), NULL) ||
        !cfp_get(slot, &previous))
        return 0;
    if (previous != menu && ((previous && !cfp_private(&ref, 0xb24a8, &previous, 1, NULL)) ||
                             !cfp_private(&ref, 0xb3798, &menu, 1, NULL) || !cfp_write(slot, menu)))
        return 0;
    return menu_result(call, noErr);
}

static int menu_pascal_insertion_backend(const ROMlib_carbon_reference* ref, int insert, uint32_t* delegate)
{
    const uint32_t flags = cfp_toc(ref, (uint32_t)(insert ? -0x75c : -0x764));
    const uint32_t slot = cfp_toc(ref, (uint32_t)(insert ? -0x760 : -0x768));
    Byte* initialized = flags ? ROMlib_carbon_guest_memory(flags, 1, 1) : NULL;
    if (!initialized || !slot)
        return 0;
    if (!*initialized) {
        const uint32_t strings = cfp_toc(ref, (uint32_t)-0x148);
        const uint32_t names[] = {strings + 0x5a, strings + (insert ? 0x297 : 0x283)};
        if (!strings || !cfp_private(ref, 0x55c5c, names, NELEM(names), delegate) || !cfp_write(slot, *delegate))
            return 0;
        *initialized = 1;
        return 1;
    }
    return cfp_get(slot, delegate);
}

static int menu_insert_pascal_text(ROMlib_carbon_ppc_frame* call, int insert)
{
    const uint32_t menu = call->words[0];
    uint32_t text = call->words[1];
    const MenuHandle handle = carbon_menu_handle(menu);
    if (!handle)
        return menu_result(call, menu_invalid);
    if (!text)
        return menu_result(call, paramErr);
    const Byte* length = ROMlib_carbon_guest_memory(text, 1, 0);
    if (!length || !ROMlib_carbon_guest_memory(text, (unsigned)*length + 1, 0))
        return 0;
    ROMlib_carbon_reference ref;
    const int available = menu_get_reference(insert ? "InsertMenuItemText" : "AppendMenuItemText",
                                             insert ? 0x5f3dc : 0x5f1e0, call, &ref, -4);
    if (available != 1)
        return available != 0;
    if (!*length) {
        text = cfp_toc(&ref, (uint32_t)-0x7e0);
        if (!text)
            return 0;
    }
    uint32_t delegate, status;
    if (!menu_pascal_insertion_backend(&ref, insert, &delegate))
        return 0;
    const uint16_t after = call->words[2];
    if (!delegate) {
        _Alignas(8) const Byte placeholder[] = {1, ' '};
        const uint16_t before = ROMlib_menu_item_count(handle);
        const uint16_t position = insert && after < before ? after : before;
        ROMlib_carbon_ppc_frame add = {.words = {menu, US_TO_SYN68K(placeholder), position}};
        if (!carbon_vector_InsertMenuItem(&add))
            return 0;
        if (ROMlib_menu_item_count(handle) <= before)
            return menu_result(call, memFullErr);
        const uint32_t update[] = {menu, (uint16_t)(position + 1), text};
        return cfp_private(&ref, 0xb2610, update, NELEM(update), NULL) && menu_result(call, noErr);
    }
    const uint32_t args[] = {menu, text, after};
    if (!cfp_callback(delegate, args, insert ? 3 : 2, &status))
        return 0;
    if (status)
        return menu_result(call, status);
    const uint32_t count = ROMlib_menu_item_count(handle);
    const uint32_t index = insert && (uint16_t)(after + 1) < count ? (uint16_t)(after + 1) : count;
    const uint32_t invalidate[] = {menu, index, 1, 2};
    if (!cfp_private(&ref, 0xb4728, invalidate, NELEM(invalidate), NULL) ||
        !cfp_private(&ref, 0x16ad4, &menu, 1, NULL) || !cfp_private(&ref, 0x6d734, &menu, 1, NULL))
        return 0;
    _Alignas(8) uint32_t child = 0;
    const uint32_t lookup[] = {menu, index, US_TO_SYN68K(&child)};
    return cfp_private(&ref, 0xb38d0, lookup, NELEM(lookup), NULL) && (!child || update_submenu_count(&ref, menu, 1)) &&
           menu_result(call, noErr);
}

static int carbon_AppendMenuItemText(ROMlib_carbon_ppc_frame* call)
{
    return menu_insert_pascal_text(call, 0);
}

static int carbon_InsertMenuItemText(ROMlib_carbon_ppc_frame* call)
{
    return menu_insert_pascal_text(call, 1);
}

static int carbon_DeleteMenuItems(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t menu = call->words[0], first = (uint16_t)call->words[1], count = call->words[2];
    const MenuHandle handle = carbon_menu_handle(menu);
    if (!handle)
        return menu_result(call, menu_invalid);
    const uint32_t items = ROMlib_menu_item_count(handle);
    if (!count || !first || first > items || count > items - first + 1)
        return menu_result(call, paramErr);
    ROMlib_carbon_reference ref;
    const int available = menu_get_reference("DeleteMenuItems", 0x19334, call, &ref, -4);
    if (available != 1)
        return available != 0;
    const uint32_t properties[] = {menu, first, count, 0};
    if (!cfp_private(&ref, 0x19160, properties, NELEM(properties), NULL))
        return 0;
    for (uint32_t remaining = count; remaining; --remaining) {
        const uint32_t remove[] = {menu, (uint16_t)(first + remaining - 1)};
        if (!cfp_private(&ref, 0x5bec0, remove, NELEM(remove), NULL))
            return 0;
    }
    const uint32_t attributes[] = {menu, 0x08000000};
    uint32_t refresh;
    if (!cfp_private(&ref, 0x6d734, &menu, 1, NULL) ||
        !cfp_private(&ref, 0x6ddac, attributes, NELEM(attributes), &refresh))
        return 0;
    const uint32_t update[] = {menu, 0};
    return (!refresh || cfp_private(&ref, 0x1bcd4, update, NELEM(update), NULL)) && menu_result(call, noErr);
}

static int carbon_GetMenuType(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t menu = call->words[0], output = call->words[1];
    if (!carbon_menu_handle(menu))
        return menu_result(call, menu_invalid);
    uint16_t* type = output ? ROMlib_carbon_try_guest_memory(output, sizeof *type, 1) : NULL;
    if (!type)
        return menu_result(call, paramErr);
    ROMlib_carbon_reference ref;
    const int available = menu_get_reference("GetMenuType", 0x6ee14, call, &ref, -4);
    if (available != 1)
        return available != 0;

    const uint32_t state = cfp_toc(&ref, (uint32_t)-0x828), constants = cfp_toc(&ref, (uint32_t)-0x118);
    uint32_t high, low, bits;
    if (!state || !constants || !cfp_get(state, &high) || !cfp_get(state + 4, &low) ||
        !cfp_get(constants + 0x6c, &bits))
        return 0;
    const uint64_t timestamp_bits = (uint64_t)high << 32 | low;
    double timestamp;
    float unavailable;
    memcpy(&timestamp, &timestamp_bits, sizeof timestamp);
    memcpy(&unavailable, &bits, sizeof unavailable);
    if (timestamp == unavailable)
        return menu_result(call, paramErr);

    uint32_t hierarchical;
    if (!cfp_private(&ref, 0x6eda0, &menu, 1, &hierarchical))
        return 0;
    unsigned kind = 0;
    if (hierarchical) {
        uint32_t save, record;
        if (!cfp_get(0xb5c, &save) || !save || !cfp_get(save, &record) || !record)
            return 0;
        const uint16_t* count = ROMlib_carbon_guest_memory(record, sizeof *count, 0);
        if (!count)
            return 0;
        kind = 1;
        if (*count) {
            uint32_t first;
            if (!cfp_get(record + 0x2c, &first))
                return 0;
            kind = first == menu ? 1 : 2;
        }
    }
    *type = CW(kind);
    return menu_result(call, noErr);
}

static int carbon_IsMenuBarInvalid(ROMlib_carbon_ppc_frame* call)
{
    const uint32 root = call->words[0];
    if (root) {
        ROMlib_carbon_reference reference;
        int ready = menu_get_reference("IsMenuBarInvalid", 0x6ef70, call, &reference, 0);
        if (ready != 1)
            return ready == 2;
        if (root != cfp_read(cfp_toc(&reference, (uint32)-0x4c4)))
            return menu_result(call, 0);
    }
    const Byte* flags = ROMlib_carbon_try_guest_memory(0xb20, 2, 0);
    return menu_result(call, flags && ((flags[0] | flags[1]) & 0x40));
}

static int carbon_InvalidateMenuKeyCache(ROMlib_carbon_ppc_frame* call)
{
    const uint32 menu = call->words[0], flags = call->words[3];
    ROMlib_carbon_reference reference;
    int ready = menu_get_reference("InvalidateMenuKeyCache", 0x1b808, call, &reference, -4);
    if (ready != 1)
        return ready == 2;
    if (flags & 1) {
        const uint32 cache = cfp_toc(&reference, (uint32)-0x15a8);
        uint32 existing;
        if (!cfp_get(cache, &existing) || !cfp_write(cache, existing & ~UINT32_C(3)))
            return 0;
        const uint32 args[] = {menu, 0, TICK("mmca")};
        if (menu && !cfp_private(&reference, 0xb4668, args, NELEM(args), NULL))
            return 0;
    }
    else if (flags & 6) {
        const uint32 args[] = {menu, (uint16)call->words[1], call->words[2], flags};
        if (!cfp_private(&reference, flags & 2 ? 0x1b4e4 : 0x1b66c, args, NELEM(args), NULL))
            return 0;
    }
    return menu_result(call, noErr);
}

static int carbon_InvalidateMenuEnabling(ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference reference;
    int ready = menu_get_reference("InvalidateMenuEnabling", 0x1d14c, call, &reference, -4);
    if (ready != 1)
        return ready == 2;
    uint32 menu = call->words[0], tracking;
    if (!menu && !cfp_private(&reference, 0x6fcbc, NULL, 0, &menu))
        return 0;
    const uint32 invalid[] = {menu, UINT32_C(0x00200000), 0};
    if (!cfp_private(&reference, 0x6d7c4, invalid, NELEM(invalid), NULL) ||
        !cfp_private(&reference, 0x70140, &menu, 1, &tracking))
        return 0;
    if (!tracking) {
        uint32 has_parent;
        if (!cfp_private(&reference, 0x75348, &menu, 1, &has_parent))
            return 0;
        if (has_parent) {
            uint32 root;
            if (!cfp_private(&reference, 0x6fcbc, NULL, 0, &root))
                return 0;
            const uint32 parent[] = {root, UINT32_C(0x00100000), 0};
            if (!cfp_private(&reference, 0x6d7c4, parent, NELEM(parent), NULL))
                return 0;
        }
    }
    return menu_result(call, noErr);
}

static int carbon_InvalidateMenuItems(ROMlib_carbon_ppc_frame* call)
{
    const uint32 menu = call->words[0];
    if (!menu)
        return menu_result(call, menu_invalid);
    const uint16 first = call->words[1];
    const uint64 last = (uint64)first + call->words[2] - 1;
    const int count = ROMlib_menu_item_count(carbon_menu_handle(menu));
    if (!first || count < 0 || last > (unsigned)count)
        return menu_result(call, paramErr);
    ROMlib_carbon_reference reference;
    int ready = menu_get_reference("InvalidateMenuItems", 0x1c7e4, call, &reference, -4);
    if (ready != 1)
        return ready == 2;
    const uint32 check_tracking[] = {menu, UINT32_C(0x20000000)};
    uint32 tracking;
    if (!cfp_private(&reference, 0x6ddac, check_tracking, NELEM(check_tracking), &tracking))
        return 0;
    for (uint32 item = last; item >= first; --item) {
        const uint32 invalid[] = {menu, item, UINT32_C(0x80000000), 0, UINT32_MAX};
        if (tracking && !cfp_private(&reference, 0x6e1a4, invalid, NELEM(invalid), NULL))
            return 0;
        const uint32 pixels[] = {menu, item, TICK("expi")};
        if (!cfp_private(&reference, 0xb4668, pixels, NELEM(pixels), NULL))
            return 0;
    }
    return cfp_private(&reference, 0xb3750, &menu, 1, NULL) && menu_result(call, noErr);
}

static int menu_saved_item_invalid(const ROMlib_carbon_reference* reference, uint32 menu, uint16 item, uint32* invalid)
{
    _Alignas(16) uint32 attrs = 0;
    const uint32 args[] = {menu, item, UINT32_C(0x80000000), US_TO_SYN68K(&attrs)};
    if (!cfp_private(reference, 0x6e06c, args, NELEM(args), NULL))
        return 0;
    *invalid = CL(attrs) >> 31;
    return 1;
}

static int carbon_IsMenuItemInvalid(ROMlib_carbon_ppc_frame* call)
{
    const uint32 menu = call->words[0];
    const uint16 item = call->words[1];
    if (!item || (int)item > ROMlib_menu_item_count(carbon_menu_handle(menu)))
        return menu_result(call, 0);
    ROMlib_carbon_reference reference;
    int ready = menu_get_reference("IsMenuItemInvalid", 0x1c3e0, call, &reference, 0);
    if (ready != 1)
        return ready == 2;
    uint32 invalid;
    return menu_saved_item_invalid(&reference, menu, item, &invalid) && menu_result(call, invalid);
}

typedef struct PACKED {
    uint32 menu;
    uint16 selected, under_mouse;
    Rect item_rect;
    int32 virtual_top, virtual_bottom;
} CarbonMenuTrackingData;

typedef struct PACKED {
    uint16 first, last;
    uint32 tracking;
} CarbonMenuDrawRange;

_Static_assert(sizeof(CarbonMenuTrackingData) == 24, "Carbon MenuTrackingData size");
_Static_assert(sizeof(CarbonMenuDrawRange) == 8, "Carbon menu draw range size");

static const mbdfentry* menu_tracking_entry(uint32 menu, boolean_t* topmost)
{
    const uint32 handle_address = CL((uint32)(uintptr_t)MBSaveLoc);
    Handle handle = handle_address ? ROMlib_carbon_try_guest_memory(handle_address, 4, 0) : NULL;
    if (!handle)
        return NULL;
    const Size size = GetHandleSize(handle);
    if (size < (Size)(sizeof(mbdfheader) + sizeof(mbdfentry)))
        return NULL;
    const uint32 address = CL((uint32)(uintptr_t)HIDDEN_RAW(*handle));
    const Byte* bytes = address ? ROMlib_carbon_try_guest_memory(address, size, 0) : NULL;
    if (!bytes)
        return NULL;
    const mbdfheader* header = (const mbdfheader*)bytes;
    const unsigned last = CW(header->lastMBSave);
    if (last < sizeof(mbdfentry) || last % sizeof(mbdfentry) || last > size - sizeof(mbdfentry))
        return NULL;
    for (unsigned offset = last; offset >= sizeof(mbdfentry); offset -= sizeof(mbdfentry)) {
        const mbdfentry* entry = (const mbdfentry*)(bytes + offset);
        if (!menu || menu == CL((uint32)(uintptr_t)PP_RAW(entry->mbMLHandle))) {
            *topmost = offset == last;
            return entry;
        }
    }
    return NULL;
}

static void menu_copy_tracking_data(const mbdfentry* entry, boolean_t topmost, CarbonMenuTrackingData* output)
{
    const uint32 selected = CL(entry->mbReserved);
    const int16 top = topmost ? CW(TopMenuItem) : CW(entry->mbTopScroll);
    const int16 bottom = topmost ? CW(AtMenuBottom) : CW(entry->mbBotScroll);
    *output = (CarbonMenuTrackingData){.menu = (uint32)(uintptr_t)PP_RAW(entry->mbMLHandle),
                                       .selected = CW((uint16)selected),
                                       .under_mouse = CW((uint16)(selected >> 16)),
                                       .virtual_top = CL((int32)top),
                                       .virtual_bottom = CL((int32)bottom)};
}

static int carbon_GetMenuTrackingData(ROMlib_carbon_ppc_frame* call)
{
    boolean_t topmost;
    const mbdfentry* entry = menu_tracking_entry(call->words[0], &topmost);
    if (!entry)
        return menu_result(call, -5620); /* menuNotFoundErr */
    const uint32 address = call->words[1];
    CarbonMenuTrackingData* output = address ? ROMlib_carbon_try_guest_memory(address, sizeof *output, 1) : NULL;
    if (!output)
        return menu_result(call, paramErr);
    menu_copy_tracking_data(entry, topmost, output);
    return menu_result(call, noErr);
}

static int menu_redraw_invalid_ranges(const ROMlib_carbon_reference* reference, uint32 menu, const mbdfentry* entry,
                                      boolean_t topmost)
{
    _Alignas(16) CarbonMenuTrackingData tracking;
    menu_copy_tracking_data(entry, topmost, &tracking);
    _Alignas(16) Rect bounds = entry->mbRectSave;
    _Alignas(16) CarbonMenuDrawRange range = {.tracking = CL(US_TO_SYN68K(&tracking))};
    const uint32 point = cfp_toc(reference, 0x7318);
    const boolean_t invalidate_pixels = cfp_toc(reference, (uint32)-0x2618) != 0;
    uint32 count;
    if (!cfp_private(reference, 0x1bc58, &menu, 1, &count))
        return 0;
    if (count > INT16_MAX)
        return cfp_failure("Invalid Carbon menu item count during redraw");
    for (uint32 item = 1; item <= count; ++item) {
        uint32 invalid;
        if (!menu_saved_item_invalid(reference, menu, item, &invalid))
            return 0;
        if (!invalid)
            continue;
        if (invalidate_pixels && !cfp_private(reference, 0xb6150, &menu, 1, NULL))
            return 0;
        uint32 end = item;
        while (end < count) {
            if (!menu_saved_item_invalid(reference, menu, end + 1, &invalid))
                return 0;
            if (!invalid)
                break;
            ++end;
        }
        range.first = CW((uint16)item);
        range.last = CW((uint16)end);
        const uint32 draw[] = {menu, 12, US_TO_SYN68K(&bounds), point, US_TO_SYN68K(&range)};
        if (!cfp_private(reference, 0xb4638, draw, NELEM(draw), NULL))
            return 0;
        const uint16 drawn = CW(range.last);
        item = drawn >= item && drawn <= count ? drawn : end;
    }
    return 1;
}

static int carbon_UpdateInvalidMenuItems(ROMlib_carbon_ppc_frame* call)
{
    const uint32 menu = call->words[0];
    const MenuHandle handle = carbon_menu_handle(menu);
    if (!handle)
        return menu_result(call, menu_invalid);
    ROMlib_carbon_reference reference;
    int ready = menu_get_reference("UpdateInvalidMenuItems", 0x1c508, call, &reference, -4);
    if (ready != 1)
        return ready == 2;
    if (HxX(handle, menuWidth) != CWC(-1) && HxX(handle, menuHeight) != CWC(-1))
        return menu_result(call, noErr);

    _Alignas(16) uint32 port = 0;
    const uint32 port_output = US_TO_SYN68K(&port);
    if (!cfp_private(&reference, 0xb12f0, &port_output, 1, NULL))
        return 0;
    int success = cfp_private(&reference, 0xb6858, &menu, 1, NULL);
    boolean_t topmost;
    const mbdfentry* entry = success ? menu_tracking_entry(menu, &topmost) : NULL;
    if (entry)
        success = menu_redraw_invalid_ranges(&reference, menu, entry, topmost);
    const uint32 saved_port = CL(port);
    const int restored = cfp_private(&reference, 0xb12d8, &saved_port, 1, NULL);
    return success && restored && menu_result(call, noErr);
}

enum {
    menu_public_item_attrs = 0x3fffffff,
    menu_item_disabled = 1,
    menu_icon_disabled = 2,
    menu_parent_choosable = 4,
};

static void menu_item_attr_overrides(MenuHandle menu, uint16 item, uint32 words[2])
{
    uint32 size, attrs;
    const void* data;
    if (!ROMlib_menu_item_property(menu, item, TICK("macs"), TICK("iatm"), &size, &attrs, &data) && size == 8)
        memcpy(words, data, 8);
}

static int carbon_GetMenuItemAttributes(ROMlib_carbon_ppc_frame* call)
{
    const uint32 menu = call->words[0], output = call->words[2];
    if (!menu)
        return menu_result(call, menu_invalid);
    const MenuHandle handle = carbon_menu_handle(menu);
    const uint16 item = call->words[1];
    if (!item || (int)item > ROMlib_menu_item_count(handle) || !output ||
        !ROMlib_carbon_try_guest_memory(output, sizeof(uint32), 1))
        return menu_result(call, paramErr);
    ROMlib_carbon_reference reference;
    int ready = menu_get_reference("GetMenuItemAttributes", 0x6e0dc, call, &reference, -4);
    if (ready != 1)
        return ready == 2;
    _Alignas(16) uint32 attrs = 0;
    const uint32 args[] = {menu, item, UINT32_MAX, US_TO_SYN68K(&attrs)};
    if (!cfp_private(&reference, 0x6e06c, args, NELEM(args), NULL))
        return 0;
    uint32 overrides[2] = {0, 0};
    menu_item_attr_overrides(handle, item, overrides);
    uint32 value = (CL(attrs) & ~CL(overrides[0])) | CL(overrides[1]);
    value = (value & ~menu_item_disabled) | (ROMlib_menu_item_enabled(handle, item) ? 0 : menu_item_disabled);
    return cfp_write(output, value & menu_public_item_attrs) && menu_result(call, noErr);
}

static int carbon_ChangeMenuItemAttributes(ROMlib_carbon_ppc_frame* call)
{
    const uint32 menu = call->words[0];
    if (!menu)
        return menu_result(call, menu_invalid);
    const MenuHandle handle = carbon_menu_handle(menu);
    const uint16 item = call->words[1];
    if (!item || (int)item > ROMlib_menu_item_count(handle))
        return menu_result(call, paramErr);
    ROMlib_carbon_reference reference;
    int ready = menu_get_reference("ChangeMenuItemAttributes", 0x6e4a0, call, &reference, -4);
    if (ready != 1)
        return ready == 2;
    const uint32 set = call->words[2] & menu_public_item_attrs, clear = call->words[3] & menu_public_item_attrs;
    const uint32 args[] = {menu, item, set, clear, UINT32_MAX};
    uint32 result;
    if (!cfp_private(&reference, 0x6e1a4, args, NELEM(args), &result))
        return 0;
    if (!result && ((set | clear) & ~menu_item_disabled)) {
        uint32 overrides[2] = {0, 0};
        menu_item_attr_overrides(handle, item, overrides);
        overrides[0] = CL((CL(overrides[0]) | set | clear) & ~menu_item_disabled);
        overrides[1] = CL(((CL(overrides[1]) | set) & ~clear) & ~menu_item_disabled);
        result = ROMlib_change_menu_item_property(handle, item, TICK("macs"), TICK("iatm"), ROMLIB_MENU_PROPERTY_SET,
                                                  sizeof overrides, 0, overrides);
    }
    if (!result && ((set | clear) & menu_item_disabled) &&
        !ROMlib_set_menu_item_enabled(handle, item, !(set & menu_item_disabled) || (clear & menu_item_disabled)))
        result = memFullErr;
    return menu_result(call, result);
}

static int carbon_SetSubmenuParentChoosable(ROMlib_carbon_ppc_frame* call)
{
    const int enabled = (uint8)call->words[2] != 0;
    call->words[2] = enabled ? menu_parent_choosable : 0;
    call->words[3] = enabled ? 0 : menu_parent_choosable;
    return carbon_ChangeMenuItemAttributes(call);
}

static int carbon_GetSubmenuParentChoosable(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t address = call->words[2];
    BOOLEAN* output = address ? ROMlib_carbon_try_guest_memory(address, sizeof *output, 1) : NULL;
    const uint32_t menu_address = call->words[0];
    const uint32_t* master = menu_address ? ROMlib_carbon_try_guest_memory(menu_address, sizeof *master, 0) : NULL;
    const uint32_t data = master ? CL(*master) : 0;
    const MenuHandle menu = data && ROMlib_carbon_try_guest_memory(data, offsetof(MenuInfo, menuData) + 2, 0)
                                ? (MenuHandle)master
                                : NULL;
    return menu_result(call, ROMlib_get_submenu_parent_choosable(menu, (INTEGER)call->words[1], output));
}

static int carbon_IsMenuItemIconEnabled(ROMlib_carbon_ppc_frame* call)
{
    _Alignas(16) uint32 attrs = 0;
    ROMlib_carbon_ppc_frame query = *call;
    query.words[2] = US_TO_SYN68K(&attrs);
    if (!carbon_GetMenuItemAttributes(&query))
        return 0;
    return menu_result(call, !query.words[0] && !(CL(attrs) & menu_icon_disabled));
}

static int carbon_EnableMenuItemIcon(ROMlib_carbon_ppc_frame* call)
{
    call->words[2] = 0;
    call->words[3] = menu_icon_disabled;
    return carbon_ChangeMenuItemAttributes(call);
}

static int carbon_DisableMenuItemIcon(ROMlib_carbon_ppc_frame* call)
{
    call->words[2] = menu_icon_disabled;
    call->words[3] = 0;
    return carbon_ChangeMenuItemAttributes(call);
}

static int carbon_GetMenuExcludesMarkColumn(ROMlib_carbon_ppc_frame* call)
{
    _Alignas(16) uint32 attrs = 0;
    ROMlib_carbon_ppc_frame query = *call;
    query.words[1] = US_TO_SYN68K(&attrs);
    if (!carbon_GetMenuAttributes(&query))
        return 0;
    return menu_result(call, !query.words[0] && (CL(attrs) & 1));
}

static int carbon_SetMenuExcludesMarkColumn(ROMlib_carbon_ppc_frame* call)
{
    const int exclude = (uint8)call->words[1] != 0;
    call->words[1] = exclude;
    call->words[2] = !exclude;
    return carbon_ChangeMenuAttributes(call);
}

static int menu_font(ROMlib_carbon_ppc_frame* call, int writing)
{
    const uint32 menu = call->words[0];
    const MenuHandle handle = carbon_menu_handle(menu);
    if (!handle)
        return menu_result(call, menu_invalid);
    const uint32 font_arg = call->words[1], size_arg = call->words[2];
    void* family = NULL;
    void* size = NULL;
    if (!writing) {
        family = font_arg ? ROMlib_carbon_try_guest_memory(font_arg, sizeof(uint16), 1) : NULL;
        size = size_arg ? ROMlib_carbon_try_guest_memory(size_arg, sizeof(uint16), 1) : NULL;
        if (!family || !size)
            return menu_result(call, paramErr);
    }

    ROMlib_carbon_reference reference;
    const char* name = writing ? "SetMenuFont" : "GetMenuFont";
    if (!ROMlib_carbon_load_reference("CarbonLib", name, &reference))
        return menu_result(call, -4);
    if (!menu_reference_matches(&reference, writing ? 0x5fc54 : 0x5fb5c))
        return ROMlib_carbon_invoke_frame(reference.function, call);

    const uint32 initialized = cfp_toc(&reference, (uint32)(writing ? -0x730 : -0x738));
    const uint32 override = cfp_toc(&reference, (uint32)(writing ? -0x72c : -0x734));
    Byte* seen = ROMlib_carbon_try_guest_memory(initialized, 1, 1);
    if (!seen)
        return 0;
    uint32 proc;
    if (!cfp_get(override, &proc))
        return 0;
    if (!*seen) {
        const uint32 strings = cfp_toc(&reference, (uint32)-0x148);
        const uint32 lookup[] = {strings + 0x5a, strings + (writing ? 0x318 : 0x30b)};
        if (!cfp_private(&reference, 0x55c5c, lookup, NELEM(lookup), &proc) || !cfp_write(override, proc))
            return 0;
        *seen = 1;
    }

    const uint32 tag = TICK("fnsz");
    uint32 value = (font_arg << 16) | (uint16)size_arg;
    int32 status = noErr;
    if (proc) {
        const uint32 args[] = {menu, writing ? (uint32)(int16)font_arg : font_arg,
                               writing ? (uint16)size_arg : size_arg};
        uint32 result;
        if (!cfp_callback(proc, args, NELEM(args), &result))
            return 0;
        if (!writing || result)
            return menu_result(call, result);
    }
    else {
        uint32 collection;
        if (!menu_registry(&reference, &collection))
            return 0;
        if (!collection)
            return menu_result(call, memFullErr);
        if (writing) {
            if (!menu_write_record(collection, menu, tag, value, &status))
                return 0;
        }
        else {
            value = (uint32)(uint16)CW(SysFontFam) << 16 | (uint16)CW(SysFontSiz);
            if (!menu_read_record(collection, menu, tag, &value, &status))
                return 0;
            const uint16 family_bytes = CW((uint16)(value >> 16)), size_bytes = CW((uint16)value);
            memcpy(family, &family_bytes, sizeof family_bytes);
            memcpy(size, &size_bytes, sizeof size_bytes);
            return menu_result(call, noErr);
        }
    }
    if (!status) {
        const uint32 bytes = CL(value);
        status = ROMlib_change_menu_item_property(handle, 0, TICK("macs"), tag, ROMLIB_MENU_PROPERTY_SET, sizeof bytes,
                                                  0, &bytes);
        HxX(handle, menuWidth) = CWC(-1);
        HxX(handle, menuHeight) = CWC(-1);
    }
    return menu_result(call, status);
}

static int carbon_GetMenuFont(ROMlib_carbon_ppc_frame* call)
{
    return menu_font(call, 0);
}

static int carbon_SetMenuFont(ROMlib_carbon_ppc_frame* call)
{
    return menu_font(call, 1);
}

enum menu_item_field { menu_indent_get, menu_indent_set, menu_key_get, menu_key_set };

static int menu_item_field(ROMlib_carbon_ppc_frame* call, enum menu_item_field field)
{
    static const char* const names[] = {"GetMenuItemIndent", "SetMenuItemIndent", "GetMenuItemCommandKey",
                                        "SetMenuItemCommandKey"};
    static const uint32 offsets[] = {0x70b0c, 0x70bbc, 0x70cb0, 0x70d84};
    const uint32 menu = call->words[0];
    if (!menu)
        return menu_result(call, menu_invalid);
    const uint16 item = call->words[1];
    const MenuHandle handle = carbon_menu_handle(menu);
    if (!item || (int)item > ROMlib_menu_item_count(handle))
        return menu_result(call, paramErr);

    const int key = field >= menu_key_get;
    const int writing = field == menu_indent_set || field == menu_key_set;
    const uint32 argument = call->words[key ? 3 : 2];
    const unsigned width = key ? 2 : 4;
    void* output = writing || !argument ? NULL : ROMlib_carbon_try_guest_memory(argument, width, 1);
    if (!writing && !output)
        return menu_result(call, paramErr);
    ROMlib_carbon_reference reference;
    if (!ROMlib_carbon_load_reference("CarbonLib", names[field], &reference))
        return menu_result(call, -4);
    if (!menu_reference_matches(&reference, offsets[field]))
        return ROMlib_carbon_invoke_frame(reference.function, call);

    if (key && !(uint8)call->words[2]) {
        const uint32 args[] = {menu, (int16)item, writing ? (uint32)(int16)argument : argument};
        return cfp_private(&reference, writing ? 0xb2640 : 0xb2658, args, NELEM(args), NULL) &&
               menu_result(call, noErr);
    }

    const uint32 tag = key ? TICK("vkey") : TICK("idnt");
    const uint32 creator = TICK("macs");
    if (!writing) {
        const void* data;
        uint32 size, attrs;
        if (!ROMlib_menu_item_property(handle, item, creator, tag, &size, &attrs, &data) && size == width) {
            memcpy(output, data, width);
            return menu_result(call, noErr);
        }
        memset(output, 0, width);
        const uint32 args[] = {menu, item, tag, width, 0, argument};
        return cfp_private(&reference, 0xb4608, args, NELEM(args), NULL) && menu_result(call, noErr);
    }

    _Alignas(16) union {
        uint32 indent;
        uint16 virtual_key;
    } value;

    if (key)
        value.virtual_key = CW((uint16)argument);
    else
        value.indent = CL(argument);
    const uint32 property[] = {menu, item, tag, width, US_TO_SYN68K(&value)};
    uint32 status;
    if (!cfp_private(&reference, 0xb4650, property, NELEM(property), &status))
        return 0;
    if (!status)
        status =
            ROMlib_change_menu_item_property(handle, item, creator, tag, ROMLIB_MENU_PROPERTY_SET, width, 0, &value);
    if (!status) {
        const uint32 invalidate[] = {menu, item, 1, 1};
        if (key) {
            const uint32 virtual_key_attribute[] = {menu, item, 0x400, 0};
            if (!cfp_private(&reference, 0xb2880, virtual_key_attribute, NELEM(virtual_key_attribute), NULL))
                return 0;
        }
        if (!cfp_private(&reference, key ? 0xb4728 : 0xb39c0, invalidate, key ? 4 : 3, NULL))
            return 0;
    }
    return menu_result(call, status);
}

static int carbon_GetMenuItemIndent(ROMlib_carbon_ppc_frame* call)
{
    return menu_item_field(call, menu_indent_get);
}

static int carbon_SetMenuItemIndent(ROMlib_carbon_ppc_frame* call)
{
    return menu_item_field(call, menu_indent_set);
}

static int carbon_GetMenuItemCommandKey(ROMlib_carbon_ppc_frame* call)
{
    return menu_item_field(call, menu_key_get);
}

static int carbon_SetMenuItemCommandKey(ROMlib_carbon_ppc_frame* call)
{
    return menu_item_field(call, menu_key_set);
}

static int carbon_menu_enable(enum carbon_menu_enable_operation operation, ROMlib_carbon_ppc_frame* call)
{
    MenuHandle menu = carbon_menu_handle(call->words[0]);
    const int count = ROMlib_menu_item_count(menu);
    const uint16_t item = call->words[1];
    call->words[0] = 0;
    if (count < 0)
        return 1;
    switch (operation) {
    case menu_enable:
    case menu_disable:
        if (item <= count)
            ROMlib_set_menu_item_enabled(menu, item, operation == menu_enable);
        break;
    case menu_enabled:
        call->words[0] = item <= count && ROMlib_menu_item_enabled(menu, item);
        break;
    case menu_enable_all:
    case menu_disable_all:
        ROMlib_set_menu_items_enabled(menu, count, operation == menu_enable_all);
        break;
    case menu_has_enabled:
        for (int i = 1; i <= count && !call->words[0]; ++i)
            call->words[0] = ROMlib_menu_item_enabled(menu, i);
        break;
    }
    return 1;
}

enum carbon_menu_property_operation {
    menu_get_property,
    menu_property_size,
    menu_set_property,
    menu_remove_property,
    menu_property_attrs,
    menu_change_property_attrs
};

static int32_t carbon_menu_property(enum carbon_menu_property_operation operation, const uint32_t* words, int system)
{
    const uint16_t item = words[1];
    const uint32_t system_creator = UINT32_C(0x6d616373); /* 'macs' */
    const uint32_t creator = system ? system_creator : words[2];
    const uint32_t* args = words + (system ? 2 : 3);
    const uint32_t tag = args[0];
    if (!system && creator == system_creator)
        return -5603; /* menuPropertyInvalidErr */
    const MenuHandle menu = carbon_menu_handle(words[0]);
    if ((int)item > ROMlib_menu_item_count(menu))
        return paramErr;

    switch (operation) {
    case menu_set_property: {
        const void* data = args[1] && args[2] ? ROMlib_carbon_guest_memory(args[2], args[1], 0) : NULL;
        if (args[1] && !data)
            return paramErr;
        return ROMlib_change_menu_item_property(menu, item, creator, tag, ROMLIB_MENU_PROPERTY_SET, args[1], 0, data);
    }
    case menu_remove_property:
        return ROMlib_change_menu_item_property(menu, item, creator, tag, ROMLIB_MENU_PROPERTY_REMOVE, 0, 0, NULL);
    case menu_change_property_attrs:
        return ROMlib_change_menu_item_property(menu, item, creator, tag, ROMLIB_MENU_PROPERTY_ATTRIBUTES, args[1],
                                                args[2], NULL);
    default:
        break;
    }

    const uint32_t output_address = operation == menu_get_property ? args[2] : args[1];
    void* output = output_address ? ROMlib_carbon_guest_memory(output_address, 4, 1) : NULL;
    if (!output && (output_address || operation != menu_get_property))
        return paramErr;
    if (operation == menu_get_property && !args[3])
        return paramErr;
    uint32_t size, attributes;
    const void* data;
    int error = ROMlib_menu_item_property(menu, item, creator, tag, &size, &attributes, &data);
    if (error)
        return error;
    if (operation == menu_get_property) {
        const uint32_t extent = MIN(size, args[1]);
        if (extent) {
            void* buffer = ROMlib_carbon_guest_memory(args[3], extent, 1);
            if (!buffer)
                return paramErr;
            memcpy(buffer, data, extent);
        }
    }
    if (output) {
        const uint32_t value = CL(operation == menu_property_attrs ? attributes : size);
        memcpy(output, &value, 4);
    }
    return noErr;
}

enum carbon_menu_command_operation {
    command_set_id,
    command_count,
    command_find,
    command_enable,
    command_disable,
    command_enabled,
    command_set_mark,
    command_get_mark,
    command_get_property,
    command_property_size,
    command_set_property,
    command_remove_property
};

static int menu_item_command(uint32_t menu_address, MenuHandle menu, int16_t item, uint32_t value, bool write,
                             uint32_t* status)
{
    enum { menu_dispatch_vector = 0x0e94, menu_dispatch_slot = 0x25 };
    uint32_t* output = !write && value ? ROMlib_carbon_guest_memory(value, sizeof *output, 1) : NULL;
    if (!menu || item < 1 || item > ROMlib_menu_item_count(menu) || (!write && !output)) {
        *status = paramErr;
        return 1;
    }

    /* The saved item-data reader and writer use the MenuDispatch command
       selectors. Use the live manager's state for both so copying and
       searching can see command IDs set through either entry point. */
    uint32_t vector;
    if (!cfp_get(menu_dispatch_vector, &vector))
        return 0;
    if (vector && vector != (uint32_t)(uintptr_t)toolstuff[menu_dispatch_slot].orig) {
        const ROMlib_carbon_ppc_frame args = {.words = {menu_address, item, value}};
        uint32_t result;
        if (!ROMlib_carbon_call_toolbox_vector(menu_dispatch_vector, 0x3ba8, write ? 0x0502 : 0x0503, &args,
                                              &result))
            return 0;
        *status = (int16_t)result;
    }
    else {
        *status = write ? ROMlib_set_menu_item_command(menu, item, value)
                        : ROMlib_get_menu_item_command(menu, item, output);
    }
    return 1;
}

static int carbon_GetMenuItemCommandID(ROMlib_carbon_ppc_frame* call)
{
    uint32_t status;
    return menu_item_command(call->words[0], carbon_menu_handle(call->words[0]), (int16_t)call->words[1],
                             call->words[2], false, &status) &&
           menu_result(call, status);
}

static int appearance_SetMenuItemCommandID(ROMlib_carbon_ppc_frame* call)
{
    uint32_t status;
    return menu_item_command(call->words[0], carbon_menu_handle(call->words[0]), (int16_t)call->words[1],
                             call->words[2], true, &status) &&
           menu_result(call, status);
}

typedef struct {
    uint32_t command, index, count, menu, remaining;
    uint16_t item;
} menu_command_search;

typedef struct menu_command_parent {
    const struct menu_command_parent* previous;
    uint32_t menu;
} menu_command_parent;

static int search_menu_commands(const ROMlib_carbon_reference* ref, uint32_t menu, menu_command_search* search,
                                const menu_command_parent* parent, unsigned depth)
{
    if (depth == 256 || !search->remaining)
        return 1;
    /* Preserve separate occurrences of a shared submenu while breaking cycles. */
    for (const menu_command_parent* ancestor = parent; ancestor; ancestor = ancestor->previous)
        if (ancestor->menu == menu)
            return 1;

    const menu_command_parent here = {parent, menu};
    const MenuHandle handle = carbon_menu_handle(menu);
    const int items = ROMlib_menu_item_count(handle);
    for (int item = 1; item <= items && !search->menu && search->remaining; ++item) {
        --search->remaining;
        _Alignas(8) uint32_t id;
        uint32_t status;
        if (!menu_item_command(menu, handle, item, US_TO_SYN68K(&id), false, &status))
            return 0;
        if (!status && CL(id) == search->command && ++search->count == search->index && search->index) {
            search->menu = menu;
            search->item = item;
            return 1;
        }
        _Alignas(8) uint32_t submenu = 0;
        const uint32_t child[] = {menu, item, US_TO_SYN68K(&submenu), 0};
        if (!cfp_private(ref, 0x14eb4, child, 4, NULL))
            return 0;
        uint32_t child_menu = CL(submenu);
        if (!child_menu) {
            const mextp fields = ROMlib_mitemtop(handle, item, NULL);
            if (fields && fields->mkeyeq == 0x1b) {
                const MenuHandle classic = C_GetMHandle(fields->mmarker);
                if (classic)
                    child_menu = US_TO_SYN68K(classic);
            }
        }
        if (child_menu && !search_menu_commands(ref, child_menu, search, &here, depth + 1))
            return 0;
    }
    return 1;
}

static int carbon_menu_command(const char* name, uint32_t offset, enum carbon_menu_command_operation operation,
                               ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference ref;
    if (!ROMlib_carbon_reference_function("CarbonLib", name, &ref))
        return 0;
    if (ref.code_offset != offset || ref.code_size != 0xb8fb4 || !ref.toc || ref.toc_offset != 0x2f78 ||
        ref.toc_size != 0x10f0c)
        return ROMlib_carbon_invoke_frame(ref.function, call);

    const uint32_t* words = call->words;
    if (operation == command_set_id) {
        const uint32_t menu = words[0];
        const MenuHandle handle = carbon_menu_handle(menu);
        const int16_t item = words[1];
        _Alignas(8) uint32_t old;
        uint32_t status;
        if (!menu_item_command(menu, handle, item, US_TO_SYN68K(&old), false, &status))
            return 0;
        if (!status && CL(old) != words[2]) {
            if (!menu_item_command(menu, handle, item, words[2], true, &status))
                return 0;
            if (!status && !cfp_private(&ref, 0x16ad4, &menu, 1, NULL))
                return 0;
        }
        call->words[0] = (uint32_t)status;
        return 1;
    }

    const uint32_t index = operation == command_count ? 0 : operation == command_find ? words[2] : 1;
    const int needs_match = operation == command_find || operation >= command_get_property;
    const int invalid = !words[1] || (operation == command_find && !index);
    menu_command_search search = {.command = words[1], .index = index, .remaining = UINT16_MAX};
    if (!invalid && words[0] && !search_menu_commands(&ref, words[0], &search, NULL, 0))
        return 0;
    if (!invalid && !words[0]) {
        MenuHandle menu;
        for (unsigned i = 0; !search.menu && search.remaining && (menu = ROMlib_menu_bar_item(i)); ++i)
            if (!search_menu_commands(&ref, US_TO_SYN68K(menu), &search, NULL, 0))
                return 0;
    }
    if (operation == command_count) {
        call->words[0] = search.count;
        return 1;
    }
    if (!search.menu) {
        call->words[0] = needs_match ? (uint32_t)(invalid ? paramErr : -5622) : 0;
        return 1;
    }
    const MenuHandle menu = carbon_menu_handle(search.menu);
    int status = noErr;
    switch (operation) {
    case command_find: {
        const uint16_t item = CW(search.item);
        void* output = words[4] ? ROMlib_carbon_guest_memory(words[4], sizeof item, 1) : NULL;
        if ((words[4] && !output) || (words[3] && !cfp_write(words[3], search.menu)))
            return 0;
        if (output)
            memcpy(output, &item, sizeof item);
        break;
    }
    case command_enable:
    case command_disable:
        ROMlib_set_menu_item_enabled(menu, search.item, operation == command_enable);
        break;
    case command_enabled:
        status = ROMlib_menu_item_enabled(menu, search.item);
        break;
    case command_set_mark: {
        const uint32_t args[] = {search.menu, search.item, (uint32_t)(int32_t)(int8_t)words[2]};
        if (!cfp_private(&ref, 0xb6840, args, 3, NULL))
            return 0;
        break;
    }
    case command_get_mark: {
        const uint32_t args[] = {search.menu, search.item, words[2]};
        if (words[2] && !cfp_private(&ref, 0xb2628, args, 3, NULL))
            return 0;
        break;
    }
    default: {
        const enum carbon_menu_property_operation property[] = {menu_get_property, menu_property_size,
                                                                menu_set_property, menu_remove_property};

        const uint32_t args[] = {search.menu, search.item, words[2], words[3], words[4], words[5], words[6]};
        status = carbon_menu_property(property[operation - command_get_property], args, 0);
        break;
    }
    }
    call->words[0] = (uint32_t)status;
    return 1;
}

#define CARBON_MENU_COMMAND(name, offset, op)                          \
    static int carbon_##name(ROMlib_carbon_ppc_frame* call)            \
    {                                                                  \
        return carbon_menu_command(#name, offset, command_##op, call); \
    }

CARBON_MENU_COMMAND(SetMenuItemCommandID, 0x5e278, set_id)
CARBON_MENU_COMMAND(CountMenuItemsWithCommandID, 0x15184, count)
CARBON_MENU_COMMAND(GetIndMenuItemWithCommandID, 0x156bc, find)
CARBON_MENU_COMMAND(EnableMenuCommand, 0x157e8, enable)
CARBON_MENU_COMMAND(DisableMenuCommand, 0x15830, disable)
CARBON_MENU_COMMAND(IsMenuCommandEnabled, 0x15878, enabled)
CARBON_MENU_COMMAND(SetMenuCommandMark, 0x158d4, set_mark)
CARBON_MENU_COMMAND(GetMenuCommandMark, 0x15934, get_mark)
CARBON_MENU_COMMAND(GetMenuCommandProperty, 0x15994, get_property)
CARBON_MENU_COMMAND(GetMenuCommandPropertySize, 0x15a0c, property_size)
CARBON_MENU_COMMAND(SetMenuCommandProperty, 0x15a84, set_property)
CARBON_MENU_COMMAND(RemoveMenuCommandProperty, 0x15af4, remove_property)

#undef CARBON_MENU_COMMAND

enum carbon_menu_text_operation {
    text_copy_title,
    text_set_title,
    text_append,
    text_insert,
    text_copy_item,
    text_set_item
};

enum { menu_text_property = 0x63667374, menu_text_conversion_error = -4960 };

static int carbon_GetMenuItemTextEncoding(ROMlib_carbon_ppc_frame* call)
{
    const MenuHandle menu = carbon_menu_handle(call->words[0]);
    uint32_t* output = call->words[2] ? ROMlib_carbon_guest_memory(call->words[2], 4, 1) : NULL;
    call->words[0] = (uint32_t)ROMlib_get_menu_item_encoding(menu, (int16_t)call->words[1], output);
    return 1;
}

static int carbon_SetMenuItemTextEncoding(ROMlib_carbon_ppc_frame* call)
{
    const MenuHandle menu = carbon_menu_handle(call->words[0]);
    const uint32_t item = (uint16_t)call->words[1];
    const int status = ROMlib_set_menu_item_encoding(menu, (int16_t)item, call->words[2]);
    ROMlib_carbon_reference ref;
    if (!status && ROMlib_carbon_probe_reference("CarbonLib", "SetMenuItemTextEncoding", &ref) &&
        ref.code_offset == 0x5f018 && ref.code_size == 0xb8fb4 && ref.toc && ref.toc_offset == 0x2f78 &&
        ref.toc_size == 0x10f0c) {
        const uint32_t invalidate[] = {call->words[0], item, 1};
        if (!cfp_private(&ref, 0xb39c0, invalidate, 3, NULL))
            return 0;
    }
    call->words[0] = status;
    return 1;
}

static int menu_text_cached(const ROMlib_carbon_reference* ref, uint32_t menu, uint32_t item, uint32_t* string)
{
    _Alignas(8) uint32_t stored = 0;
    const uint32_t get[] = {menu, item, menu_text_property, sizeof stored, 0, US_TO_SYN68K(&stored)};
    uint32_t status;
    if (!cfp_private(ref, 0xb4608, get, 6, &status))
        return 0;
    *string = status ? 0 : CL(stored);
    return 1;
}

static int menu_text_store(const ROMlib_carbon_reference* ref, uint32_t menu, uint32_t item, uint32_t string,
                           uint32_t* status)
{
    _Alignas(8) uint32_t stored = CL(string);
    const uint32_t set[] = {menu, item, menu_text_property, sizeof stored, US_TO_SYN68K(&stored)};
    return cfp_private(ref, 0xb4650, set, 5, status);
}

static int menu_text_copy(const ROMlib_carbon_reference* ref, uint32_t menu, uint32_t item, uint32_t output,
                          uint32_t* status)
{
    uint32_t string;
    if (!menu_text_cached(ref, menu, item, &string))
        return 0;
    if (string) {
        if (!cfp_private(ref, 0xb65d0, &string, 1, &string))
            return 0;
    }
    else {
        _Alignas(256) Byte text[256] = {0};
        _Alignas(8) uint32_t item_encoding = 0;
        const uint32_t address = US_TO_SYN68K(text);
        uint32_t encoding = 0xfffffffe;
        if (item) {
            const uint32_t get[] = {menu, item, address};
            const uint32_t script[] = {menu, item, US_TO_SYN68K(&item_encoding)};
            if (!cfp_private(ref, 0xb6198, get, 3, NULL) || !cfp_private(ref, 0xb67e0, script, 3, status))
                return 0;
            if (!*status)
                encoding = CL(item_encoding);
        }
        else {
            const uint32_t get[] = {menu, address};
            if (!cfp_private(ref, 0xb2988, get, 2, NULL))
                return 0;
        }
        if (encoding == 0xfffffffe && !cfp_private(ref, 0xb4368, NULL, 0, &encoding))
            return 0;
        string = ROMlib_cfstring_pascal(0, address, encoding);
        const uint32_t decode[] = {address, encoding, 1};
        if (!string && !cfp_private(ref, 0x334fc, decode, 3, &string))
            return 0;
    }
    *status = string ? noErr : item ? memFullErr : menu_text_conversion_error;
    return cfp_write(output, string);
}

static int menu_text_update(const ROMlib_carbon_reference* ref, enum carbon_menu_text_operation operation,
                            MenuHandle handle, uint32_t item, const uint32_t* words, uint32_t* status)
{
    const uint32_t menu = words[0], input = words[operation == text_set_item ? 2 : 1];
    const uint32_t copy[] = {0, input};
    uint32_t string = 0;
    *status = menu_text_conversion_error;
    if (input && !cfp_private(ref, 0xb6690, copy, 2, &string))
        return 0;
    if (input && !string)
        return 1;

    _Alignas(256) Byte text[256] = {1, ' '};
    uint32_t encoding;
    const uint32_t address = US_TO_SYN68K(text), convert[] = {string, address};
    int ok = cfp_private(ref, 0x3374c, convert, 2, &encoding);
    if (ok && encoding == UINT32_MAX) {
        /* Some installed systems lack the Unicode-to-script run tables.
           A complete conversion to the system encoding still gives the
           classic menu a usable label while keeping the full CFString. */
        void* object = string ? ROMlib_carbon_guest_memory(string, 1, 0) : NULL;
        const uint32_t system_encoding = ROMlib_cfstring_system_encoding();
        if (!string || (object && ROMlib_carbon_cf_string_pascal(object, text, sizeof text, system_encoding))) {
            if (!string) {
                text[0] = 1;
                text[1] = ' ';
            }
            encoding = system_encoding;
        }
    }
    if (!ok || encoding == UINT32_MAX)
        return (!string || cfp_private(ref, 0xb6330, &string, 1, NULL)) && ok;

    uint32_t old = 0, property_status = noErr;
    if (operation == text_set_title) {
        const uint32_t set[] = {menu, address};
        ok = cfp_private(ref, 0xb29e8, set, 2, NULL);
    }
    else if (operation == text_set_item) {
        const uint32_t set[] = {menu, item, address};
        ok = cfp_private(ref, 0xb2610, set, 3, NULL) && menu_text_cached(ref, menu, item, &old);
    }
    else {
        const int before = ROMlib_menu_item_count(handle);
        const int insert = operation == text_insert;
        if (insert) {
            /* Insert a placeholder through the classic trap, then replace its
               label literally. The trap parses separators and shortcuts. */
            Byte placeholder[] = {1, ' '};
            C_InsMenuItem(handle, placeholder, MIN(item, (uint32_t)before));
        }
        else {
            const uint32_t add[] = {menu, address};
            ok = cfp_private(ref, 0xb2550, add, 2, NULL);
        }
        const int after = ok ? ROMlib_menu_item_count(handle) : -1;
        if (after <= before) {
            *status = memFullErr;
            return (!string || cfp_private(ref, 0xb6330, &string, 1, NULL)) && ok;
        }
        item = insert ? MIN(item + 1, (uint32_t)after) : (uint32_t)after;
        if (insert) {
            const uint32_t shift[] = {menu, item, 1, 2};
            const uint32_t set[] = {menu, item, address};
            ok = cfp_private(ref, 0xb4728, shift, 4, NULL) && cfp_private(ref, 0xb2610, set, 3, NULL) &&
                 cfp_private(ref, 0x16ad4, &menu, 1, NULL);
        }
    }
    if (ok && item) {
        *status = ROMlib_set_menu_item_encoding(handle, item, encoding);
        if (*status)
            return !string || cfp_private(ref, 0xb6330, &string, 1, NULL);
    }
    if (ok && string)
        ok = menu_text_store(ref, menu, item, string, &property_status);
    if (string && (!ok || property_status)) {
        *status = property_status;
        return cfp_private(ref, 0xb6330, &string, 1, NULL) && ok;
    }
    if (old && !cfp_private(ref, 0xb6330, &old, 1, NULL))
        return 0;
    if (!ok)
        return 0;

    if (operation == text_set_item) {
        const uint32_t invalidate[] = {menu, item, 1};
        if (!cfp_private(ref, 0xb39c0, invalidate, 3, NULL))
            return 0;
    }
    else if (operation == text_append || operation == text_insert) {
        const uint32_t attributes = words[operation == text_insert ? 3 : 2];
        const uint32_t command = words[operation == text_insert ? 4 : 3];
        const uint32_t change[] = {menu, item, attributes & 0x3fffffff, 0, UINT32_MAX};
        if (attributes && !cfp_private(ref, 0x6e1a4, change, 5, NULL))
            return 0;
        if (command) {
            if (!menu_item_command(menu, handle, item, command, true, status))
                return 0;
            if (*status)
                return 1;
            if (!cfp_private(ref, 0x16ad4, &menu, 1, NULL))
                return 0;
        }
        if (operation == text_append && words[4]) {
            const uint16_t index = CW(item);
            void* output = ROMlib_carbon_guest_memory(words[4], sizeof index, 1);
            if (!output)
                return 0;
            memcpy(output, &index, sizeof index);
        }
    }
    *status = noErr;
    return 1;
}

static int carbon_menu_text(const char* name, uint32_t offset, enum carbon_menu_text_operation operation,
                            ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference ref;
    if (!ROMlib_carbon_reference_function("CarbonLib", name, &ref))
        return 0;
    if (ref.code_offset != offset || ref.code_size != 0xb8fb4 || !ref.toc || ref.toc_offset != 0x2f78 ||
        ref.toc_size != 0x10f0c)
        return ROMlib_carbon_invoke_frame(ref.function, call);
    const uint32_t* words = call->words;
    const MenuHandle handle = carbon_menu_handle(words[0]);
    const uint32_t item = operation == text_insert      ? (uint16_t)words[2]
                          : operation >= text_copy_item ? (uint16_t)words[1]
                                                        : 0;
    uint32_t status;
    if (!handle)
        status = menu_invalid;
    else if (((operation == text_copy_title || operation == text_set_title) && !words[1]) ||
             (operation >= text_copy_item && (!words[2] || !item || (int)item > ROMlib_menu_item_count(handle))))
        status = paramErr;
    else if (operation == text_copy_title || operation == text_copy_item) {
        const uint32_t output = words[item ? 2 : 1];
        if (!ROMlib_carbon_guest_memory(output, 4, 1) || !menu_text_copy(&ref, words[0], item, output, &status))
            return 0;
    }
    else if (!menu_text_update(&ref, operation, handle, item, words, &status))
        return 0;
    call->words[0] = status;
    return 1;
}

#define CARBON_MENU_TEXT(name, offset, operation)                       \
    static int carbon_##name(ROMlib_carbon_ppc_frame* call)             \
    {                                                                   \
        return carbon_menu_text(#name, offset, text_##operation, call); \
    }

CARBON_MENU_TEXT(CopyMenuTitleAsCFString, 0x16b64, copy_title)
CARBON_MENU_TEXT(SetMenuTitleWithCFString, 0x16c5c, set_title)
CARBON_MENU_TEXT(AppendMenuItemTextWithCFString, 0x16d90, append)
CARBON_MENU_TEXT(InsertMenuItemTextWithCFString, 0x16f1c, insert)
CARBON_MENU_TEXT(CopyMenuItemTextAsCFString, 0x170b0, copy_item)
CARBON_MENU_TEXT(SetMenuItemTextWithCFString, 0x171f8, set_item)

#undef CARBON_MENU_TEXT

#define CARBON_MENU_ENABLE(name, operation)                 \
    static int carbon_##name(ROMlib_carbon_ppc_frame* call) \
    {                                                       \
        return carbon_menu_enable(operation, call);         \
    }

CARBON_MENU_ENABLE(EnableMenuItem, menu_enable)
CARBON_MENU_ENABLE(DisableMenuItem, menu_disable)
CARBON_MENU_ENABLE(IsMenuItemEnabled, menu_enabled)
CARBON_MENU_ENABLE(EnableAllMenuItems, menu_enable_all)
CARBON_MENU_ENABLE(DisableAllMenuItems, menu_disable_all)
CARBON_MENU_ENABLE(MenuHasEnabledItems, menu_has_enabled)

#undef CARBON_MENU_ENABLE

#define CARBON_MENU_PROPERTY(name, operation, system)                                    \
    static int carbon_##name(ROMlib_carbon_ppc_frame* call)                              \
    {                                                                                    \
        call->words[0] = (uint32_t)carbon_menu_property(operation, call->words, system); \
        return 1;                                                                        \
    }

CARBON_MENU_PROPERTY(GetMenuItemProperty, menu_get_property, 0)
CARBON_MENU_PROPERTY(GetMenuItemPropertySize, menu_property_size, 0)
CARBON_MENU_PROPERTY(SetMenuItemProperty, menu_set_property, 0)
CARBON_MENU_PROPERTY(RemoveMenuItemProperty, menu_remove_property, 0)
CARBON_MENU_PROPERTY(GetMenuItemPropertyAttributes, menu_property_attrs, 0)
CARBON_MENU_PROPERTY(ChangeMenuItemPropertyAttributes, menu_change_property_attrs, 0)
CARBON_MENU_PROPERTY(GetMenuItemSystemProperty, menu_get_property, 1)
CARBON_MENU_PROPERTY(GetMenuItemSystemPropertySize, menu_property_size, 1)
CARBON_MENU_PROPERTY(SetMenuItemSystemProperty, menu_set_property, 1)
CARBON_MENU_PROPERTY(RemoveMenuItemSystemProperty, menu_remove_property, 1)
CARBON_MENU_PROPERTY(GetMenuItemSystemPropertyAttributes, menu_property_attrs, 1)
CARBON_MENU_PROPERTY(ChangeMenuItemSystemPropertyAttributes, menu_change_property_attrs, 1)

#undef CARBON_MENU_PROPERTY

#endif
