/*+===================================================================
  File:      data_browser_control_entries.h

  Summary:   Describe public Data Browser arguments and the CDEF records they populate.

  Classes:   None.

  Functions: Macro definitions for private Data Browser adapters.

  Origin:    CarbonLib 1.4 DataBrowserLib PowerPC exports.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

/* Required arguments are a bit mask for the first two payload words.
   A value of four requires at least one argument. Negative field widths
   put the address of a scalar in the packet. Zero inserts a zero word. */
BROWSER_CONTROL(SetDataBrowserCallbacks, 0x38874, 0x16, 44, browser_set_buffer, 1, 4, 4, 0)
BROWSER_CONTROL(GetDataBrowserCallbacks, 0x388f0, 0x16, 44, browser_get_buffer, 1, 4, 4, 0)
BROWSER_CONTROL(SetDataBrowserCustomCallbacks, 0x389bc, 0x17, 32, browser_set_buffer, 1, 4, 4, 0)
BROWSER_CONTROL(GetDataBrowserCustomCallbacks, 0x38a38, 0x17, 32, browser_get_buffer, 1, 4, 4, 0)
BROWSER_CONTROL(GetDataBrowserSelectionAnchor, 0x38dc0, 0x1d, 8, browser_get_packet, 3, 4, 4, 0)
BROWSER_CONTROL(GetDataBrowserItems, 0x38f7c, 0x1b, 16, browser_get_items, 0, 4, 4, 0)
BROWSER_CONTROL(GetDataBrowserItemState, 0x3906c, 0x19, 8, browser_get_packet, 0, 4, 4, 0)
BROWSER_CONTROL(GetDataBrowserItemCount, 0x390d8, 0x1a, 16, browser_get_item_count, 0, 4, 4, 0)
BROWSER_CONTROL(SetDataBrowserSelectedItems, 0x391ec, 0x1c, 12, browser_set_packet, 0, 4, 4, 0)
BROWSER_CONTROL(SetDataBrowserTarget, 0x39258, 0xf, 4, browser_set_scalar, 0, 4, 4, 0)
BROWSER_CONTROL(GetDataBrowserTarget, 0x392bc, 0xf, 4, browser_get_buffer, 1, 4, 4, 0)
BROWSER_CONTROL(SetDataBrowserEditItem, 0x3933c, 0x10, 8, browser_set_packet, 0, -4, -4, 0)
BROWSER_CONTROL(GetDataBrowserEditItem, 0x393b4, 0x10, 8, browser_get_packet, 3, 4, 4, 0)
BROWSER_CONTROL(SetDataBrowserEditText, 0x3943c, 0x11, 4, browser_set_scalar, 0, 4, 4, 0)
BROWSER_CONTROL(GetDataBrowserEditText, 0x394a0, 0x11, 4, browser_get_packet, 0, 4, 4, 0)
BROWSER_CONTROL(SetDataBrowserSortOrder, 0x39508, 0x1e, 2, browser_set_scalar, 0, 4, 4, 0)
BROWSER_CONTROL(GetDataBrowserSortOrder, 0x3956c, 0x1e, 2, browser_get_buffer, 1, 4, 4, 0)
BROWSER_CONTROL(SetDataBrowserUserState, 0x395ec, 0x12, 4, browser_set_scalar, 1, 4, 4, 0)
BROWSER_CONTROL(GetDataBrowserUserState, 0x39668, 0x12, 4, browser_get_buffer, 1, 4, 4, 0)
BROWSER_CONTROL(SetDataBrowserViewStyle, 0x396e8, 0x13, 8, browser_set_packet, 0, 4, 0, 0)
BROWSER_CONTROL(GetDataBrowserViewStyle, 0x39754, 0x13, 4, browser_get_buffer, 1, 4, 4, 0)
BROWSER_CONTROL(SetDataBrowserActiveItems, 0x397d4, 0x14, 1, browser_set_scalar, 0, 4, 4, 0)
BROWSER_CONTROL(GetDataBrowserActiveItems, 0x39838, 0x14, 1, browser_get_buffer, 1, 4, 4, 0)
BROWSER_CONTROL(SetDataBrowserSortProperty, 0x398b8, 0x1f, 4, browser_set_scalar, 0, 4, 4, 0)
BROWSER_CONTROL(GetDataBrowserSortProperty, 0x3991c, 0x1f, 4, browser_get_buffer, 1, 4, 4, 0)
BROWSER_CONTROL(SetDataBrowserHasScrollBars, 0x3999c, 0x21, 8, browser_set_packet, 0, -1, -1, 0)
BROWSER_CONTROL(GetDataBrowserHasScrollBars, 0x39a14, 0x21, 8, browser_get_packet, 4, 4, 4, 0)
BROWSER_CONTROL(SetDataBrowserScrollPosition, 0x39a9c, 0x23, 8, browser_set_packet, 0, 4, 4, 0)
BROWSER_CONTROL(GetDataBrowserScrollPosition, 0x39b04, 0x23, 8, browser_get_scroll, 4, 4, 4, 0)
BROWSER_CONTROL(SetDataBrowserScrollBarInset, 0x39bcc, 0x22, 8, browser_set_buffer, 1, 4, 4, 0)
BROWSER_CONTROL(GetDataBrowserScrollBarInset, 0x39c48, 0x22, 8, browser_get_buffer, 1, 4, 4, 0)
BROWSER_CONTROL(SetDataBrowserSelectionFlags, 0x39cc8, 0x15, 4, browser_set_scalar, 0, 4, 4, 0)
BROWSER_CONTROL(GetDataBrowserSelectionFlags, 0x39d2c, 0x15, 4, browser_get_buffer, 1, 4, 4, 0)
BROWSER_CONTROL(SetDataBrowserPropertyFlags, 0x39dac, 0x20, 8, browser_set_packet, 0, 4, -4, 0)
BROWSER_CONTROL(GetDataBrowserPropertyFlags, 0x39e1c, 0x20, 8, browser_get_packet, 2, 4, 4, 0)
BROWSER_CONTROL(GetDataBrowserItemPartBounds, 0x39e9c, 0x18, 16, browser_get_packet, 3, 4, 4, 0)
BROWSER_CONTROL(GetDataBrowserTableViewItemID, 0x39f2c, 0x0, 8, browser_get_packet, 2, 4, 4, 0)
BROWSER_CONTROL(SetDataBrowserTableViewItemRow, 0x39fac, 0x1, 8, browser_set_packet, 0, 4, -4, 0)
BROWSER_CONTROL(GetDataBrowserTableViewItemRow, 0x3a01c, 0x1, 8, browser_get_packet, 2, 4, 4, 0)
BROWSER_CONTROL(SetDataBrowserTableViewGeometry, 0x3a09c, 0x2, 8, browser_set_packet, 0, -1, -1, 0)
BROWSER_CONTROL(GetDataBrowserTableViewGeometry, 0x3a114, 0x2, 8, browser_get_packet, 0, 4, 4, 0)
BROWSER_CONTROL(SetDataBrowserTableViewRowHeight, 0x3a180, 0x3, 8, browser_set_packet, 0, 0, -2, 1)
BROWSER_CONTROL(GetDataBrowserTableViewRowHeight, 0x3a1ac, 0x3, 8, browser_get_packet, 2, 0, 4, 1)
BROWSER_CONTROL(SetDataBrowserTableViewHiliteStyle, 0x3a1d8, 0x5, 4, browser_set_scalar, 0, 4, 4, 0)
BROWSER_CONTROL(GetDataBrowserTableViewHiliteStyle, 0x3a23c, 0x5, 4, browser_get_buffer, 0, 4, 4, 0)
BROWSER_CONTROL(GetDataBrowserTableViewColumnCount, 0x3a304, 0x4, 4, browser_get_buffer, 1, 4, 4, 0)
BROWSER_CONTROL(SetDataBrowserTableViewColumnWidth, 0x3a384, 0x8, 8, browser_set_packet, 0, 0, -2, 1)
BROWSER_CONTROL(GetDataBrowserTableViewColumnWidth, 0x3a3b0, 0x8, 8, browser_get_packet, 2, 0, 4, 1)
BROWSER_CONTROL(SetDataBrowserTableViewItemRowHeight, 0x3a3dc, 0x3, 8, browser_set_packet, 0, 4, -2, 0)
BROWSER_CONTROL(GetDataBrowserTableViewItemRowHeight, 0x3a44c, 0x3, 8, browser_get_packet, 2, 4, 4, 0)
BROWSER_CONTROL(SetDataBrowserTableViewColumnPosition, 0x3a4cc, 0x7, 8, browser_set_packet, 0, 4, -4, 0)
BROWSER_CONTROL(GetDataBrowserTableViewColumnPosition, 0x3a53c, 0x7, 8, browser_get_packet, 2, 4, 4, 0)
BROWSER_CONTROL(GetDataBrowserTableViewColumnProperty, 0x3a5bc, 0x6, 8, browser_get_packet, 2, 4, 4, 0)
BROWSER_CONTROL(SetDataBrowserTableViewNamedColumnWidth, 0x3a63c, 0x8, 8, browser_set_packet, 0, 4, -2, 0)
BROWSER_CONTROL(GetDataBrowserTableViewNamedColumnWidth, 0x3a6ac, 0x8, 8, browser_get_packet, 2, 4, 4, 0)
BROWSER_CONTROL(SetDataBrowserListViewUsePlainBackground, 0x3a72c, 0xd, 1, browser_set_scalar, 0, 4, 4, 0)
BROWSER_CONTROL(GetDataBrowserListViewUsePlainBackground, 0x3a790, 0xd, 1, browser_get_buffer, 1, 4, 4, 0)
BROWSER_CONTROL(SetDataBrowserListViewHeaderBtnHeight, 0x3a94c, 0xc, 2, browser_set_scalar, 0, 4, 4, 0)
BROWSER_CONTROL(GetDataBrowserListViewHeaderBtnHeight, 0x3a9b0, 0xc, 2, browser_get_buffer, 1, 4, 4, 0)
BROWSER_CONTROL(SetDataBrowserListViewDisclosureColumn, 0x3aa30, 0xe, 8, browser_set_packet, 0, -4, -1, 0)
BROWSER_CONTROL(GetDataBrowserListViewDisclosureColumn, 0x3aaa8, 0xe, 8, browser_get_packet, 0, 4, 4, 0)
BROWSER_CONTROL(GetDataBrowserColumnViewPath, 0x3ab14, 0xa, 4, browser_get_packet, 1, 4, 4, 0)
BROWSER_CONTROL(SetDataBrowserColumnViewPath, 0x3ab94, 0xa, 8, browser_set_packet, 2, 4, 4, 0)
BROWSER_CONTROL(GetDataBrowserColumnViewPathLength, 0x3ac10, 0x9, 4, browser_get_buffer, 0, 4, 4, 0)
BROWSER_CONTROL(SetDataBrowserColumnViewDisplayType, 0x3ac80, 0xb, 4, browser_set_scalar, 0, 4, 4, 0)
BROWSER_CONTROL(GetDataBrowserColumnViewDisplayType, 0x3ace4, 0xb, 4, browser_get_buffer, 0, 4, 4, 0)
