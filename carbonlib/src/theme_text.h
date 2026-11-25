/*+===================================================================
  File:      theme_text.h

  Summary:   Measure, draw and truncate Appearance text with QuickDraw.

  Classes:   None.

  Functions: Private Appearance text adapters for the Carbon export registry.

  Origin:    CarbonLib 1.4 Appearance text exports and Universal Interfaces 3.4.2 Appearance.h.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMAW_CARBON_THEME_TEXT_H
#define ROMAW_CARBON_THEME_TEXT_H

#include "TextEdit.h"
#include "cf_batch.h"
#include "theme_font.h"

enum { theme_text_conversion_error = -4960 };

typedef struct {
    Ptr bytes;
    int16_t length;
} theme_text;

typedef struct {
    int16_t family, size, mode;
    Byte face;
} theme_text_port;

static int theme_text_fallback(const char* name, ROMlib_carbon_ppc_frame* call, int32_t error)
{
    ROMlib_carbon_reference reference;
    if (ROMlib_carbon_load_reference("CarbonLib", name, &reference))
        return ROMlib_carbon_invoke_frame(reference.function, call);
    return theme_font_result(call, error);
}

static OSErr theme_text_prepare(uint32_t address, uint16_t role, bool mutable, theme_text* text)
{
    void* string = address ? ROMlib_carbon_try_guest_memory(address, 8, 0) : NULL;
    uint32_t type, count;
    if (!string || theme_font_role(role) < 0 || !cfp_call("CFStringGetTypeID", NULL, 0, &type) ||
        ROMlib_carbon_cf_type_id(string) != type || (mutable && !(cfp_read(address + 4) & 1)) ||
        !cfp_call("CFStringGetLength", &address, 1, &count))
        return paramErr;
    if (count > INT16_MAX)
        return theme_text_conversion_error;
    if (!count)
        return noErr;
    Ptr data = NewPtr(count);
    if (!data)
        return memFullErr;
    _Alignas(16) LONGINT used = 0;
    const LONGINT units = ROMlib_carbon_cf_string_bytes(string, 0, count, 0, 0, false, data, count, &used);
    if (units != (LONGINT)count || CL(used) != count) {
        DisposPtr(data);
        return theme_text_conversion_error;
    }
    *text = (theme_text){data, (int16_t)count};
    return noErr;
}

static theme_text_port theme_text_save_port(void)
{
    return (theme_text_port){PORT_TX_FONT(thePort), PORT_TX_SIZE(thePort), PORT_TX_MODE(thePort), PORT_TX_FACE(thePort)};
}

static OSErr theme_text_set_font(uint16_t role)
{
    if (role != 200) {
        ROMlib_carbon_ppc_frame args = {0};
        args.words[0] = role;
        if (!carbon_UseThemeFont(&args))
            return paramErr;
        if (args.words[0])
            return (OSErr)args.words[0];
    }
    return C_FontScript() == 0 ? noErr : theme_text_conversion_error;
}

static void theme_text_restore_port(theme_text_port saved, bool mode)
{
    C_TextFont(saved.family);
    C_TextSize(saved.size);
    C_TextFace(saved.face);
    if (mode)
        C_TextMode(saved.mode);
}

static TEHandle theme_text_layout(const theme_text* text, Rect* bounds)
{
    TEHandle layout = C_TENew(bounds, bounds);
    if (layout) {
        C_TESetText(text->bytes, text->length, layout);
        if (TE_LENGTH(layout) != text->length) {
            C_TEDispose(layout);
            return NULL;
        }
    }
    return layout;
}

static void theme_text_draw_line(const theme_text* text, int16_t start, int16_t length, Rect area, int baseline,
                                 int16_t justification)
{
    const int left = CW(area.left), right = CW(area.right);
    int horizontal = left + 1;
    if (justification == teJustRight)
        horizontal = right - C_TextWidth(text->bytes, start, length);
    else if (justification == teJustCenter)
        horizontal += (right - left - C_TextWidth(text->bytes, start, length)) / 2;
    C_MoveTo(horizontal, baseline);
    C_DrawText(text->bytes, start, length);
}

static OSErr theme_text_draw(const theme_text* text, Rect* area, int16_t justification, bool wrap)
{
    RgnHandle prior = C_NewRgn();
    RgnHandle clip = prior ? C_NewRgn() : NULL;
    if (!clip) {
        if (prior)
            C_DisposeRgn(prior);
        return memFullErr;
    }
    C_GetClip(prior);
    C_RectRgn(clip, area);
    C_SectRgn(prior, clip, clip);
    C_SetClip(clip);
    if (PORT_TX_MODE(thePort) != 0x31)
        C_TextMode(srcOr);
    if (justification == teJustLeft)
        justification = CW(TESysJust) ? teJustRight : teForceLeft;

    FontInfo metrics;
    C_GetFontInfo(&metrics);
    const int ascent = CW(metrics.ascent);
    const int height = CW(metrics.ascent) + CW(metrics.descent) + CW(metrics.leading);
    int baseline = (int16_t)CW(area->top) + ascent;
    OSErr status = noErr;
    if (wrap) {
        TEHandle layout = theme_text_layout(text, area);
        if (!layout)
            status = memFullErr;
        else {
            const int lines = TE_N_LINES(layout);
            for (int i = 0; i < lines && baseline - ascent < (int16_t)CW(area->bottom); ++i, baseline += height) {
                const int start = LINE_START(TE_LINE_STARTS(layout), i);
                int end = LINE_START(TE_LINE_STARTS(layout), i + 1);
                if (start < 0 || start > end || end > text->length)
                    break;
                if (end > start && text->bytes[end - 1] == '\r')
                    --end;
                theme_text_draw_line(text, start, end - start, *area, baseline, justification);
            }
            C_TEDispose(layout);
        }
    }
    else
        theme_text_draw_line(text, 0, text->length, *area, baseline, justification);
    C_SetClip(prior);
    C_DisposeRgn(clip);
    C_DisposeRgn(prior);
    return status;
}

static int carbon_DrawThemeTextBox(ROMlib_carbon_ppc_frame* call)
{
    const Rect* bounds = call->words[4] ? ROMlib_carbon_try_guest_memory(call->words[4], sizeof(Rect), 0) : NULL;
    if (!bounds || !thePort)
        return theme_font_result(call, paramErr);
    Rect area = *bounds;
    theme_text text = {0};
    const uint16_t role = call->words[1];
    OSErr status = theme_text_prepare(call->words[0], role, false, &text);
    if (!status && text.length) {
        const theme_text_port saved = theme_text_save_port();
        status = theme_text_set_font(role);
        if (!status && !C_EmptyRect(&area))
            status = theme_text_draw(&text, &area, (int16_t)call->words[5], (Byte)call->words[3] != 0);
        theme_text_restore_port(saved, true);
        DisposPtr(text.bytes);
    }
    if (status == theme_text_conversion_error)
        return theme_text_fallback("DrawThemeTextBox", call, status);
    return theme_font_result(call, status);
}

static int carbon_GetThemeTextDimensions(ROMlib_carbon_ppc_frame* call)
{
    Point* bounds = call->words[4] ? ROMlib_carbon_try_guest_memory(call->words[4], sizeof(Point), 1) : NULL;
    void* baseline = call->words[5] ? ROMlib_carbon_try_guest_memory(call->words[5], 2, 1) : NULL;
    if (!bounds || !baseline || !thePort)
        return theme_font_result(call, paramErr);
    theme_text text = {0};
    const uint16_t role = call->words[1];
    OSErr status = theme_text_prepare(call->words[0], role, false, &text);
    const bool wrap = (Byte)call->words[3] != 0;
    Point result = {0, wrap ? bounds->h : 0};
    int16_t offset = 0;
    if (!status && text.length) {
        const theme_text_port saved = theme_text_save_port();
        status = theme_text_set_font(role);
        if (!status) {
            FontInfo metrics;
            C_GetFontInfo(&metrics);
            const int ascent = CW(metrics.ascent), descent = CW(metrics.descent), leading = CW(metrics.leading);
            int height = ascent + descent;
            if (wrap) {
                Rect area = {0, 0, CWC(20), result.h};
                TEHandle layout = theme_text_layout(&text, &area);
                if (layout) {
                    height = TE_N_LINES(layout) * (height + leading) - leading;
                    C_TEDispose(layout);
                }
                else
                    status = memFullErr;
            }
            else
                result.h = CW(C_TextWidth(text.bytes, 0, text.length) + 1);
            result.v = CW(height);
            offset = CW(ascent - height);
        }
        theme_text_restore_port(saved, false);
        DisposPtr(text.bytes);
    }
    if (status == theme_text_conversion_error)
        return theme_text_fallback("GetThemeTextDimensions", call, status);
    if (!status) {
        *bounds = result;
        memcpy(baseline, &offset, sizeof offset);
    }
    return theme_font_result(call, status);
}

static int carbon_TruncateThemeText(ROMlib_carbon_ppc_frame* call)
{
    Byte* output = call->words[5] ? ROMlib_carbon_try_guest_memory(call->words[5], 1, 1) : NULL;
    if ((!output && call->words[5]) || !thePort)
        return theme_font_result(call, paramErr);
    theme_text text = {0};
    const uint16_t role = call->words[1];
    OSErr status = theme_text_prepare(call->words[0], role, true, &text);
    bool truncated = false;
    if (!status && text.length) {
        const theme_text_port saved = theme_text_save_port();
        status = theme_text_set_font(role);
        if (!status) {
            _Alignas(16) INTEGER length = CW(text.length);
            const INTEGER width = (int16_t)((int16_t)call->words[3] - 1);
            truncated = C_TruncText(width, text.bytes, &length, (int16_t)call->words[4]) != NotTruncated;
            if (truncated) {
                uint32_t replacement = ROMlib_cfstring_bytes(0, US_TO_SYN68K(text.bytes), CW(length), 0, false);
                if (!replacement)
                    status = theme_text_conversion_error;
                else {
                    ROMlib_cfstring_replace_all(call->words[0], replacement);
                    ROMlib_carbon_cf_release(SYN68K_TO_US_CHECK0(replacement));
                }
            }
        }
        theme_text_restore_port(saved, false);
        DisposPtr(text.bytes);
    }
    if (status == theme_text_conversion_error)
        return theme_text_fallback("TruncateThemeText", call, status);
    if (!status && output)
        *output = truncated;
    return theme_font_result(call, status);
}

static int carbon_GetThemeTextShadowOutset(ROMlib_carbon_ppc_frame* call)
{
    Rect* outset = call->words[2] ? ROMlib_carbon_try_guest_memory(call->words[2], sizeof(Rect), 1) : NULL;
    if (!outset || theme_font_role((uint16_t)call->words[0]) < 0)
        return theme_font_result(call, paramErr);
    /* The Classic Appearance wrapper supplies zero outset for all font roles. */
    *outset = (Rect){0};
    return theme_font_result(call, noErr);
}

#endif
