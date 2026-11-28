# +===================================================================
#   File:      Makefile.am
#
#   Summary:   Defines build targets and dependencies for ROMAW build and development support.
#
#   Classes:   None.
#
#   Functions: No explicitly exported functions.
#
#   Origin:    Existing repository source in executor/src; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
# ===================================================================+

.DELETE_ON_ERROR:

bin_PROGRAMS = ROMAW
ROMAW_LDFLAGS =
PYTHON = python3

BUILT_SOURCES = generated/build_version.h
.PHONY: force-build-version
force-build-version:

generated/build_version.h: force-build-version $(srcdir)/tools/build_version.py
	$(PYTHON) $(srcdir)/tools/build_version.py $@

noinst_PROGRAMS = mkseedtables mkultable mksspairtable
mkseedtables_SOURCES = tools/romaw/mkseedtables.c
mkultable_SOURCES = tools/romaw/mkultable.c
mksspairtable_SOURCES = tools/romaw/mksspairtable.c

ctl_bitmap_c_files = generated/wind_scroll_arrow_clicked.inc generated/wind_scroll_arrow_active.inc generated/wind_scroll_arrow_inactive.inc \
                     generated/wind_scroll_thumb.inc

wind_bitmap_c_files = generated/wind_zoom.inc generated/wind_close.inc generated/wind_close_clicked.inc generated/wind_grow.inc

CLEANFILES = generated/build_version.h generated/syserr_table.inc generated/ROMAW.ico generated/system_code.inc generated/system_resources.inc generated/ROMAW.rsrc \
             $(ctl_bitmap_c_files) $(wind_bitmap_c_files)  generated/apple.inc \
             generated/seedtables.inc generated/ultable.inc generated/sspairtable.inc generated/rawpatstubs.inc generated/rawsrcstubs.inc

# Explicit build-directory paths avoid VPATH and quoted-include shadowing
# by generated files left behind by older in-source builds.
shared_icons_dir = $(abspath $(srcdir)/bitmaps)
system_resources_dir = $(abspath $(srcdir)/system/resources/system)
shared_tools_dir = $(abspath $(srcdir)/tools)
shared_icon_inputs = $(shell $(PYTHON) $(srcdir)/tools/romaw/system_resources.py --icon-inputs "$(shared_icons_dir)")
shared_icon_tool_inputs = $(shared_tools_dir)/mac_icons.py $(shared_tools_dir)/build_version.py $(shared_tools_dir)/resource_catalog.py $(shared_tools_dir)/rez_types.py $(shared_tools_dir)/rez_system_types.py $(shared_icons_dir)/mac-palettes.r

bitmap_files = $(ctl_bitmap_c_files) $(wind_bitmap_c_files) generated/apple.inc
$(bitmap_files): generated/%.inc: $(shared_icons_dir)/%.icns $(srcdir)/tools/romaw/icns_to_c.py $(shared_icon_tool_inputs)
	mkdir -p generated
	$(PYTHON) $(srcdir)/tools/romaw/icns_to_c.py $< $(if $(filter generated/wind_scroll_thumb.inc,$@),--scroll-thumb) $(if $(filter generated/wind_scroll_arrow_clicked.inc generated/wind_scroll_arrow_active.inc generated/wind_scroll_arrow_inactive.inc,$@),--scroll-arrow) > $@

packed_bitmap_files = generated/color_picker_normal.inc \
                      generated/color_picker_lower_highlighted.inc generated/color_picker_upper_highlighted.inc \
                      generated/color_wheel_4.inc generated/color_wheel_8.inc
packed_bitmap_inputs = $(patsubst generated/%.inc,$(srcdir)/bitmaps/%.png,$(packed_bitmap_files)) \
                       $(srcdir)/bitmaps/scrollbar_track.png $(srcdir)/bitmaps/alarm.icns \
                       $(srcdir)/bitmaps/3061.icns $(srcdir)/bitmaps/arrow_closed.icns
CLEANFILES += $(packed_bitmap_files) generated/alarm.inc generated/popup_down_arrow.inc \
              generated/menu_right_arrow.inc generated/menu_up_arrow.inc generated/menu_down_arrow.inc
$(packed_bitmap_files): generated/%.inc: $(srcdir)/bitmaps/%.png $(srcdir)/tools/bitmaps.py
	$(PYTHON) $(srcdir)/tools/bitmaps.py $< $@ --expect $(bitmap_geometry)

generated/color_picker_normal.inc generated/color_picker_lower_highlighted.inc generated/color_picker_upper_highlighted.inc: bitmap_geometry = 13 22 1
generated/color_wheel_4.inc: bitmap_geometry = 208 208 4
generated/color_wheel_8.inc: bitmap_geometry = 208 208 8
generated/alarm.inc: $(srcdir)/bitmaps/alarm.icns $(srcdir)/tools/bitmaps.py $(shared_icon_tool_inputs)
	$(PYTHON) $(srcdir)/tools/bitmaps.py $< $@ --expect 16 16 1

generated/popup_down_arrow.inc: $(srcdir)/bitmaps/3061.icns $(srcdir)/tools/bitmaps.py $(shared_icon_tool_inputs)
	$(PYTHON) $(srcdir)/tools/bitmaps.py $< $@ --crop 2 6 13 12 --expect 11 6 1

generated/menu_right_arrow.inc: $(srcdir)/bitmaps/arrow_closed.icns $(srcdir)/tools/bitmaps.py $(shared_icon_tool_inputs)
	$(PYTHON) $(srcdir)/tools/bitmaps.py $< $@ --crop 5 2 11 13 --expect 6 11 1

generated/menu_down_arrow.inc: $(srcdir)/bitmaps/arrow_closed.icns $(srcdir)/tools/bitmaps.py $(shared_icon_tool_inputs)
	$(PYTHON) $(srcdir)/tools/bitmaps.py $< $@ --crop 5 2 11 13 --rotate clockwise --expect 11 6 1

generated/menu_up_arrow.inc: $(srcdir)/bitmaps/arrow_closed.icns $(srcdir)/tools/bitmaps.py $(shared_icon_tool_inputs)
	$(PYTHON) $(srcdir)/tools/bitmaps.py $< $@ --crop 5 2 11 13 --rotate counterclockwise --expect 11 6 1

menus/stdmdef.$(OBJEXT): generated/menu_right_arrow.inc generated/menu_up_arrow.inc generated/menu_down_arrow.inc
controls/ctlPopup.$(OBJEXT): generated/popup_down_arrow.inc
quickdraw/qColorPicker.$(OBJEXT): generated/color_picker_normal.inc generated/color_picker_lower_highlighted.inc generated/color_picker_upper_highlighted.inc
ui/color_wheel_bits.$(OBJEXT): generated/color_wheel_4.inc generated/color_wheel_8.inc
events/toolevent.$(OBJEXT): generated/alarm.inc

system_resource_inputs = $(wildcard $(system_resources_dir)/*.r $(system_resources_dir)/keyboards/*.rinc) $(shared_icon_inputs) $(shared_icon_tool_inputs) $(shared_tools_dir)/pictures.py $(shared_tools_dir)/finder_guest_resources.py $(shared_tools_dir)/native_code_resources.py
resources/system_resources.$(OBJEXT): generated/system_resources.inc
resources/resGet.$(OBJEXT): generated/system_code.inc

generated/system_code.inc: generated/system_resources.inc
	@test -f $@ || $(PYTHON) $(srcdir)/tools/romaw/system_resources.py $(srcdir)/system/resources/system generated/system_resources.inc

generated/system_resources.inc: generated/build_version.h $(system_resource_inputs) $(srcdir)/tools/romaw/system_resources.py
	mkdir -p generated
	$(PYTHON) $(srcdir)/tools/romaw/system_resources.py $(srcdir)/system/resources/system $@

diagnostics/syserr.$(OBJEXT): generated/syserr_table.inc

syserr_icon_inputs = $(wildcard $(shared_icons_dir)/syserr/*.icns) $(shared_icons_dir)/stop.icns $(shared_icons_dir)/bomb.icns

generated/syserr_table.inc: $(syserr_icon_inputs) $(system_resources_dir)/syserr.r $(shared_icon_tool_inputs) $(srcdir)/tools/romaw/system_resources.py
	mkdir -p generated
	$(PYTHON) $(srcdir)/tools/romaw/system_resources.py --alert-table $(system_resources_dir)/syserr.r $@

shared_fonts_dir = $(abspath $(srcdir)/fonts)
font_inputs = $(wildcard $(shared_fonts_dir)/*.ttf)
romawassetsdir = $(bindir)
romawassets_DATA = generated/ROMAW.rsrc
outlinefontsdir = $(bindir)/fonts
outlinefonts_DATA = $(font_inputs)

generated/ROMAW.rsrc: generated/build_version.h $(font_inputs) $(srcdir)/tools/romaw/font_suitcase.py $(srcdir)/tools/romaw/system_resources.py
	$(PYTHON) $(srcdir)/tools/romaw/font_suitcase.py "$(shared_fonts_dir)" "$@"

generated/seedtables.inc:	mkseedtables
	mkdir -p generated
	./mkseedtables > $@

generated/ultable.inc:	mkultable
	mkdir -p generated
	./mkultable > $@

generated/sspairtable.inc:	mksspairtable
	mkdir -p generated
	./mksspairtable > $@

generated/rawpatstubs.inc:	$(srcdir)/tools/romaw/makerawblt.pl $(srcdir)/tools/romaw/pat-blitters.tmpl
	mkdir -p generated
	$(PERL) $(srcdir)/tools/romaw/makerawblt.pl < $(srcdir)/tools/romaw/pat-blitters.tmpl > generated/rawpatstubs.inc

generated/rawsrcstubs.inc: $(srcdir)/tools/romaw/makerawblt.pl $(srcdir)/tools/romaw/src-blitters.tmpl
	mkdir -p generated
	$(PERL) $(srcdir)/tools/romaw/makerawblt.pl < $(srcdir)/tools/romaw/src-blitters.tmpl > generated/rawsrcstubs.inc

controls/ctlArrows.$(OBJEXT):	$(ctl_bitmap_c_files)
menus/stdmbdf.$(OBJEXT):	generated/apple.inc
quickdraw/qIMIV.$(OBJEXT):	generated/seedtables.inc
quickdraw/qStdText.$(OBJEXT):	generated/ultable.inc
quickdraw/qIMVI.$(OBJEXT):	generated/sspairtable.inc
quickdraw/rawpatblt.$(OBJEXT):	generated/rawpatstubs.inc
quickdraw/rawsrcblt.$(OBJEXT):	generated/rawsrcstubs.inc
windows/windDocdef.$(OBJEXT):	$(wind_bitmap_c_files)

# Subsystem sources have matching directories under src and obj.

ctl_sources = controls/ctlArrows.c controls/ctlDisplay.c controls/ctlIMIV.c controls/ctlInit.c controls/ctlMisc.c \
              controls/ctlMouse.c controls/ctlSet.c controls/ctlSize.c controls/ctlStddef.c controls/ctlPopup.c

dial_sources = dialogs/dialAlert.c dialogs/dialCreate.c dialogs/dialHandle.c dialogs/dialInit.c dialogs/dialManip.c \
               dialogs/dialDispatch.c dialogs/dialItem.c

file_sources = files/fileAccess.c files/fileCreate.c files/fileDirs.c files/fileDouble.c files/fileInfo.c \
               files/afpCatalog.c files/fileMisc.c files/fileVolumes.c files/fileHighlevel.c files/dcache.c

hfs_sources = hfs/hfsBtree.c hfs/hfsChanging.c hfs/hfsCreate.c hfs/hfsFile.c hfs/hfsHelper.c \
              hfs/hfsHier.c hfs/hfsMisc.c hfs/hfsVolume.c hfs/hfsWorkingdir.c hfs/hfsXbar.c

list_sources = lists/listAccess.c lists/listAddDel.c lists/listCreate.c lists/listDisplay.c \
               lists/listMouse.c lists/listOps.c lists/listStdLDEF.c

menu_sources = menus/menu.c menus/menuColor.c menus/menuV.c menus/stdmdef.c menus/stdmbdf.c

print_sources = printing/PSprint.c printing/PSstrings.c printing/prError.c printing/prInit.c printing/prLowLevel.c \
                printing/prPrinting.c printing/prRecords.c printing/rasterpdf.c

qd_sources = quickdraw/qBit.c quickdraw/qCConv.c quickdraw/qCGrafPort.c quickdraw/qCRegular.c quickdraw/qColor.c quickdraw/qColorMgr.c    \
             quickdraw/qColorutil.c quickdraw/qCursor.c quickdraw/qGrafport.c quickdraw/qIMIV.c quickdraw/qIMV.c quickdraw/qIMVxfer.c     \
             quickdraw/qMisc.c quickdraw/qPaletteMgr.c quickdraw/qPen.c quickdraw/qPicstuff.c quickdraw/qPicture.c              \
             quickdraw/qPixMapConv.c quickdraw/qPoint.c quickdraw/qPoly.c quickdraw/qRect.c quickdraw/qRegion.c quickdraw/qRegular.c      \
             quickdraw/qScale.c quickdraw/qStandard.c quickdraw/qStdArc.c quickdraw/qStdBits.c quickdraw/qStdLine.c quickdraw/qStdOval.c  \
             quickdraw/qStdPic.c quickdraw/qStdPoly.c quickdraw/qStdRRect.c quickdraw/qStdRect.c quickdraw/qStdRgn.c            \
             quickdraw/qStdText.c quickdraw/qText.c quickdraw/qGWorld.c quickdraw/qGDevice.c quickdraw/qIMVI.c quickdraw/qHooks.c         \
             quickdraw/xdata.c quickdraw/xdblt.c quickdraw/rawpatblt.c quickdraw/rawsrcblt.c quickdraw/dirtyrect.c quickdraw/srcblt.c     \
             quickdraw/qColorPicker.c quickdraw/qPict2.c quickdraw/image.c quickdraw/image_inits.c                    \
             quickdraw/default_ctab_values.c quickdraw/dcconvert.c quickdraw/rgbutil.c quickdraw/dcmaketables.c

res_sources = resources/resGet.c resources/resGetinfo.c resources/resGettype.c resources/resIMIV.c resources/resInit.c \
              resources/system_resources.c resources/systemprefs.c resources/resMisc.c resources/resMod.c resources/resOpen.c resources/resSetcur.c resources/resPartial.c

te_sources = textedit/teAccess.c textedit/teDisplay.c textedit/teEdit.c textedit/teIMIV.c textedit/teIMV.c textedit/teInit.c \
             textedit/teInsert.c textedit/teMisc.c textedit/teScrap.c

wind_sources = windows/quickdraw_adapter.c windows/windColor.c windows/windDisplay.c windows/windDocdef.c windows/windInit.c \
  windows/systemwindow.c \
  windows/layers.c windows/windMisc.c \
               windows/windMouse.c windows/windSize.c windows/windUpdate.c

ae_sources = appleevents/AE.c appleevents/AE_desc.c appleevents/AE_hdlr.c appleevents/AE_coercion.c

sound_sources = sound/legacy.c sound/speech.c sound/fileplay.c sound/sounddriver.c sound/sound.c sound/soundIMVI.c sound/soundfake.c sound/snth5.c

num_sources = numerics/bindec.c numerics/float4.c numerics/float5.c numerics/float7.c numerics/floatnext.c numerics/toolmath.c

res_sources += finder/guest/runtime.c finder/guest/runtime.h finder/guest/finder_code.h \
  finder/guest/CDEF-128.c finder/guest/WDEF-128.c finder/guest/gest-128.c finder/guest/hcsl-0.c

runtime_sources = runtime/gestalt.c runtime/globals.c runtime/launch.c runtime/main.c runtime/desktop.c \
  runtime/prefspacing.c runtime/configprefs.c runtime/user_profile.cpp runtime/segment.c runtime/romlib_stubs.c \
  runtime/version.c runtime/shutdown.c runtime/process.c runtime/checkpoint.c runtime/cleanup.c \
  runtime/emutrap.c runtime/emutraptables.c runtime/emustubs.c runtime/romaw.c runtime/cooperative.c runtime/check_structs.c

option_sources = options/option.c options/ecf.c options/parseopt.c options/parsenum.c options/paramline.c options/parse.y
memory_sources = memory/mman.c memory/mmansubr.c memory/tempmem.c
event_sources = events/notify.c events/hle.c events/osevent.c events/toolevent.c events/vbl.c events/syncint.c \
  events/virtualint.c events/keycode.c events/adb.c events/ibm_keycodes.c
service_sources = services/components.c services/systemnotify.c services/osutil.c services/pack.c services/scrap.c \
  services/toolutil.c services/time.c services/edition.c services/crc.c
movie_sources = movies/qt.c
device_sources = devices/mactcp_resolver.c devices/mactcp_stream.c devices/mactcp.c devices/device.c devices/serial.c devices/commtool.c
text_sources = text/font.c text/iu.c text/script.c text/string.c text/fontIMVI.c text/local_charset.c \
  text/intl_resource.c include/rsys/intl_resource.h
host_sources = host/slash.c host/trash.c host/diskimages.c host/macinfo.c host/fauxdbm.c host/application_instance.c
display_sources = display/refresh.c display/autorefresh.c display/redrawscreen.c
ui_sources = ui/afpChooser.c ui/afpClassicChooser.c ui/desk.c ui/deskfiles.c ui/aboutbox.c ui/licensetext.c ui/preferences.c ui/balloon.c \
  ui/color_wheel_bits.c ui/icon.c ui/appearance.c
diagnostic_sources = diagnostics/dump.c diagnostics/trapname.c diagnostics/syserr.c diagnostics/syserr_procedures.c diagnostics/desperate.c \
  diagnostics/error.c diagnostics/system_error.c
file_sources += files/disk.c files/diskinit.c files/stdfile.c files/alias.c files/finder.c files/suffix_maps.c \
  files/filetypemap.c files/filetypedb.cpp files/application_registry.c files/desktop_database.cpp files/lockrange.c files/mkvol/mkvol.c
hfs_sources += hfs/diskimage.c

include_sources = include/rsys/component.h \
  include/rsys/systemnotify.h \
  include/rsys/systemwindow.h \
  include/rsys/layers.h include/hintemplate.h \
                  include/rsys/script_globals.h \
                  include/ADB.h \
                  include/AliasMgr.h \
                  include/AppleEvents.h \
                  include/AppleTalk.h \
                  include/BinaryDecimal.h \
                  include/CommTool.h \
                  include/Components.h \
                  include/ControlMgr.h \
                  include/CQuickDraw.h \
                  include/DeskMgr.h \
                  include/DeviceMgr.h \
                  include/DialogMgr.h \
                  include/Disk.h \
                  include/DiskInit.h \
                  include/EditionMgr.h \
                  include/EventMgr.h \
                  include/FileMgr.h \
                  include/Finder.h \
                  include/FontMgr.h \
                  include/Gestalt.h \
                  include/HelpMgr.h \
                  include/Iconutil.h \
                  include/IntlUtil.h \
                  include/ListMgr.h \
                  include/MacTypes.h \
                  include/MemoryMgr.h \
                  include/MenuMgr.h \
                  include/NotifyMgr.h \
                  include/OSEvent.h \
                  include/OSUtil.h \
                  include/Package.h \
                  include/paramline.h \
                  include/PPC.h \
                  include/PrintMgr.h \
                  include/ProcessMgr.h \
                  include/QuickDraw.h \
                  include/QuickTime.h \
                  include/ResourceMgr.h \
                  include/rsys/aboutbox.h \
                  include/rsys/aboutpanel.h \
                  include/rsys/adb.h \
                  include/rsys/alias.h \
                  include/rsys/appearance.h \
                  include/rsys/apple_events.h \
                  include/rsys/arch.h \
                  include/rsys/arrowkeys.h \
                  include/rsys/autorefresh.h \
                  include/rsys/blockdev.h \
                  include/rsys/blockinterrupts.h \
                  include/rsys/byteswap.h \
                  include/rsys/cfm.h \
                  include/rsys/check_structs.h \
                  include/rsys/checkpoint.h \
                  include/rsys/cleanup.h \
                  include/rsys/color_wheel_bits.h \
                  include/rsys/common.h \
                  include/rsys/commonevt.h \
                  include/rsys/cquick.h \
                  include/rsys/crc.h \
                  include/rsys/cruft.h \
                  include/rsys/ctl.h \
                  include/rsys/hostcatalog.h \
                  include/rsys/button_geometry.h \
                  include/rsys/ctopflags.h \
                  include/rsys/dcache.h \
                  include/rsys/depthconv.h \
                  include/rsys/desk.h \
                  include/rsys/desperate.h \
                  include/rsys/device.h \
                  include/rsys/mactcp.h \
                  include/rsys/afp_catalog.h \
                  include/rsys/host_afp.h \
                  include/rsys/host_mactcp.h \
                  include/rsys/host_mactcp_stream.h \
                  include/rsys/host_mactcp_dns.h \
                  include/rsys/dial.h \
                  include/rsys/dirtyrect.h \
                  include/rsys/drive_flags.h \
                  include/rsys/dump.h \
                  include/rsys/emustubs.h \
                  include/rsys/error.h \
                  include/rsys/everything.h \
                  include/rsys/evil.h \
                  include/rsys/romaw.h \
                  include/rsys/fauxdbm.h \
                  include/rsys/file.h \
                  include/rsys/filedouble.h \
                  include/rsys/mac_storage.h \
                  include/rsys/flags.h \
                  include/rsys/float.h \
                  include/rsys/float_fcw.h \
                  include/rsys/floatconv.h \
                  include/rsys/font.h \
                  include/rsys/ecf.h include/rsys/hosttrash.h include/rsys/host_names.h \
                  include/rsys/winfont.h \
                  include/rsys/gestalt.h \
                  include/rsys/glue.h \
                  include/rsys/gworld.h \
                  include/rsys/hfs.h \
                  include/rsys/hfs_plus.h \
                  include/rsys/hook.h \
                  include/rsys/host.h \
                    include/rsys/icon.h include/rsys/appicon.h \
                  include/rsys/image.h \
                  include/rsys/itm.h \
                  include/rsys/iv.h \
                  include/rsys/jumpvectors.h \
                  include/rsys/keyboard.h \
                  include/rsys/keyboards.h \
                  include/rsys/keycode.h \
                  include/rsys/launch.h \
                  include/rsys/licensetext.h \
                  include/rsys/list.h \
                  include/rsys/local_charset.h \
                  include/rsys/lockunlock.h \
                  include/rsys/lowglobals.h \
                  include/rsys/m68kint.h \
                  include/rsys/macros.h \
                  include/rsys/mace.h include/rsys/sdlpixels.h \
                  include/rsys/mactype.h \
                  include/rsys/memory_layout.h \
                  include/rsys/memsize.h \
                  include/rsys/menu.h \
                  include/rsys/misc.h \
                  include/rsys/mixed_mode.h \
                  include/rsys/mman.h \
                  include/rsys/mman_private.h \
                  include/rsys/mmanstubs.h \
                  include/rsys/new_mmanstubs.h \
                  include/rsys/next.h \
                  include/rsys/nextprint.h \
                  include/rsys/noreturn.h \
                  include/rsys/notmac.h \
                  include/rsys/option.h \
                  include/rsys/options.h \
                  include/rsys/os.h \
                  include/rsys/osevent.h \
                  include/rsys/osutil.h \
                  include/rsys/parse.h \
                  include/rsys/parsenum.h \
                  include/rsys/parseopt.h \
                  include/rsys/partition.h \
                  include/rsys/pictheader.h \
                  include/rsys/picture.h \
                  include/rsys/pragmal.h \
                  include/rsys/pragmar.h \
                  include/rsys/pragmas.h \
                  include/rsys/prefpanel.h \
                  include/rsys/prefs.h \
                  include/rsys/print.h \
                  include/rsys/process.h \
                  include/rsys/PSstrings.h \
                  include/rsys/pstuff.h \
                  include/rsys/ptocflags.h \
                  include/rsys/qcolor.h \
                  include/rsys/quick.h \
                  include/rsys/rawblt.h \
                  include/rsys/redrawscreen.h \
                  include/rsys/refresh.h \
                  include/rsys/region.h \
                  include/rsys/resource.h \
                  include/rsys/rgbutil.h \
                  include/rsys/safe_alloca.h \
                  include/rsys/scrap.h \
                  include/rsys/segment.h \
                  include/rsys/serial.h \
                  include/rsys/slash.h \
                  include/rsys/typeselect.h include/rsys/legacysound.h include/rsys/sounddriver.h \
                  include/rsys/soundfake.h \
                  include/rsys/soundopts.h \
                  include/rsys/srcblt.h \
                  include/rsys/stdbits.h \
                  include/rsys/hostdialog.h include/rsys/stdfile.h \
                  include/rsys/stdio_smashage.h \
                  include/rsys/string.h \
                  include/rsys/stubify.h \
                  include/rsys/suffix_maps.h \
                  include/rsys/syncint.h \
                  include/rsys/syserr.h \
                  include/rsys/system_error.h \
                  include/rsys/tempalloc.h \
                  include/rsys/tesave.h \
                  include/rsys/text.h \
                  include/rsys/time.h \
                  include/rsys/toolevent.h \
                  include/rsys/toolutil.h \
                  include/rsys/trapdecl.h \
                  include/rsys/trapdefines.h \
                  include/rsys/trapglue.h \
                  include/rsys/trapname.h \
                  include/rsys/types.h \
                  include/rsys/vbl.h \
                  include/rsys/vdriver.h \
                  include/rsys/version.h \
                  include/rsys/wind.h \
                  include/classic-ui/classic_ui.h include/classic-ui/host_graphics.h include/rsys/classic_ui_qd.h \
                  include/rsys/x.h \
                  include/rsys/xdata.h \
                  include/rsys/xdblt.h \
                  include/rsys/Xdefs.h \
                  include/SANE.h \
                  include/ScrapMgr.h \
                  include/ScriptMgr.h \
                  include/SegmentLdr.h \
                  include/Serial.h \
                  include/ShutDown.h \
                  include/SoundDvr.h \
                  include/SoundMgr.h \
                  include/StartMgr.h \
                  include/StdFilePkg.h \
                  include/SysErr.h \
                  include/TextEdit.h \
                  include/ThinkC.h \
                  include/TimeMgr.h \
                  include/ToolboxEvent.h \
                  include/ToolboxUtil.h \
                  include/VDriver.h \
                  include/VRetraceMgr.h \
                  include/WindowMgr.h \
                  files/mkvol/mkvol.h \
                  files/mkvol/mkvol_internal.h

# ICNS assets are compiled into obj/generated/*.inc, never in src/.

EXTRA_DIST = tools/build_version.py windows/README.md $(system_resource_inputs) tools/romaw/system_resources.py tools/romaw/font_suitcase.py $(font_inputs) resources/systemprefs.h \
             platform/os/cygwin32/ROMAW.rc platform/os/cygwin32/preferences.rc \
             platform/os/cygwin32/preferences_resource.h platform/os/cygwin32/preferences_winres.h bitmaps/ROMAW.icns \
             tools/romaw/icns_to_c.py \
             COPYING.LGPLv2.1         \
             bitmaps/wind_close_clicked.icns              \
             bitmaps/apple.icns                \
             bitmaps/wind_close.icns              \
             bitmaps/wind_grow.icns                 \
             bitmaps/wind_scroll_arrow_clicked.icns \
             bitmaps/wind_scroll_arrow_active.icns \
             bitmaps/wind_scroll_arrow_inactive.icns \
             bitmaps/wind_scroll_thumb.icns          \
             bitmaps/wind_zoom.icns
EXTRA_DIST += $(packed_bitmap_inputs) tools/bitmaps.py

other_sources = tools/romaw/build-host.h   \
                tools/romaw/globals.pl           \
                tools/romaw/makerawblt.pl        \
                tools/romaw/genctopflags_h.tmpl  \
                tools/romaw/genptocflags_h.tmpl  \
                tools/romaw/genstubify_h.tmpl    \
                tools/romaw/genstubify_s.tmpl    \
                tools/romaw/pat-blitters.tmpl    \
                tools/romaw/src-blitters.tmpl

AM_CPPFLAGS=-I$(srcdir)/disk-images/include -I$(srcdir) -I$(srcdir)/synppc/include -I$(srcdir)/include -I$(srcdir)/frontend-sdl -I$(srcdir)/platform/os/$(host_os) -I$(srcdir)/platform/arch/$(host_cpu)

nodist_ROMAW_SOURCES =

arch_sources = platform/arch/x86_64/x86_64.c platform/arch/x86_64/x86_64.h

front_end_sources = frontend-sdl/SDL_bmp.c        \
                    frontend-sdl/sdlgpu.cpp       \
                    frontend-sdl/sdlgpu.h         \
                    frontend-sdl/sdlpreferences.c \
                    frontend-sdl/sdlevents.c      \
                    frontend-sdl/sdl_mem.c        \
                    frontend-sdl/sdlquit.c        \
                    frontend-sdl/sdlscrap.c       \
                    frontend-sdl/sdlwin.c         \
                    frontend-sdl/sdlwm.c          \
                    frontend-sdl/windowprefs.c    \
                    frontend-sdl/windowprefs.h    \
                    frontend-sdl/syswm_map.c      \
		    frontend-sdl/for_sam.h        \
		    frontend-sdl/host_bltmacros.h \
		    frontend-sdl/host_vdriver.h   \
		    frontend-sdl/map.h            \
		    frontend-sdl/sdl.h            \
		    frontend-sdl/SDL_bmp.h        \
		    frontend-sdl/sdl_mem.h        \
		    frontend-sdl/sdlevents.h      \
		    frontend-sdl/sdlk_to_mkv.h    \
		    frontend-sdl/sdlquit.h        \
		    frontend-sdl/sdlscrap.h       \
		    frontend-sdl/syswm_map.h      \
		    frontend-sdl/syswm_vars.h

image_cmake_flags = -DCMAKE_SYSTEM_NAME=Windows
ROMAW_CMAKE_BUILD_TYPE ?= Debug
# RelWithDebInfo normally defines NDEBUG. Keep the same assertions as the
# debug build, and preserve historical guest-memory aliasing and IEEE math.
romaw_cmake_profile = -DCMAKE_BUILD_TYPE=$(ROMAW_CMAKE_BUILD_TYPE) \
  "-DCMAKE_C_FLAGS_RELWITHDEBINFO=-O2 -g -fno-strict-aliasing" \
  "-DCMAKE_CXX_FLAGS_RELWITHDEBINFO=-O2 -g -fno-strict-aliasing"
image_root = $(srcdir)/disk-images
image_inputs = $(shell find $(image_root)/src $(image_root)/include $(image_root)/third_party -type f) $(image_root)/CMakeLists.txt
images/libromaw-images.a: $(image_inputs)
	cmake -S $(image_root) -B images $(image_cmake_flags) -DCMAKE_C_COMPILER="$(CC)" -DCMAKE_CXX_COMPILER="$(CXX)" $(romaw_cmake_profile)
	cmake --build images --target romaw-images --parallel 10
# Toolbox dispatch tables and initialization entry points must remain present
# even when no ordinary C call refers to them. Preserve the old direct-object
# link's complete contents while giving each subsystem an archive boundary.
shared_ui_root = $(srcdir)/classic-ui
shared_ui_inputs = $(shared_ui_root)/CMakeLists.txt $(shared_ui_root)/src/classic_ui.c \
  $(shared_ui_root)/include/classic-ui/classic_ui.h
classic-ui/libromaw-classic-ui.a: $(shared_ui_inputs)
	cmake -S $(shared_ui_root) -B classic-ui $(image_cmake_flags) -DCMAKE_C_COMPILER="$(CC)" $(romaw_cmake_profile)
	cmake --build classic-ui --target romaw-classic-ui --parallel 10
ROMAW_LDFLAGS += -Wl,--whole-archive $(romaw_core_libraries) classic-ui/libromaw-classic-ui.a -Wl,--no-whole-archive
ROMAW_LDADD = images/libromaw-images.a
ROMAW_DEPENDENCIES = $(romaw_core_libraries) images/libromaw-images.a \
  finder-metadata/libromaw-finder-metadata.a classic-ui/libromaw-classic-ui.a generated/ROMAW-res.o $(EXTRA_ROMAW_DEPENDENCIES)
metadata_root = $(srcdir)/finder-metadata
metadata_inputs = $(metadata_root)/CMakeLists.txt $(shell find $(metadata_root)/src $(metadata_root)/include -type f)
metadata_inputs += $(srcdir)/tools/generate-dsstore-case.py $(srcdir)/tools/generate-folder-icons.py $(shared_icon_tool_inputs)
metadata_inputs += $(srcdir)/host/findermetadata.cpp $(srcdir)/include/rsys/findermetadata.h $(srcdir)/include/rsys/hostcatalog.h
metadata_inputs += $(srcdir)/include/rsys/configprefs.h $(srcdir)/include/rsys/user_profile.h
metadata_inputs += $(shared_icon_inputs)
finder-metadata/libromaw-finder-metadata.a: $(metadata_inputs)
	cmake -S $(metadata_root) -B finder-metadata $(image_cmake_flags) -DCMAKE_C_COMPILER="$(CC)" -DCMAKE_CXX_COMPILER="$(CXX)" $(romaw_cmake_profile)
	cmake --build finder-metadata --target romaw-finder-metadata --parallel 10
ROMAW_LDADD += finder-metadata/libromaw-finder-metadata.a
ROMAW_LDFLAGS += -mwindows -static-libgcc -static-libstdc++
ROMAW_LDADD += generated/ROMAW-res.o -loleaut32 -luuid -lsapi -lcomctl32 -ladvapi32
CLEANFILES += generated/ROMAW-res.o
CLEANFILES += generated/image-file-icons/*.ico generated/image-file-icons/mapping.json generated/image-file-icons/resources.rc generated/image-file-icons/associations.inc
generated/ROMAW.ico: $(shared_icons_dir)/ROMAW.icns $(shared_icon_tool_inputs)
	mkdir -p generated
	$(PYTHON) $(shared_tools_dir)/mac_icons.py $< $@

generated/ROMAW-res.o: $(srcdir)/platform/os/cygwin32/ROMAW.rc \
                      $(srcdir)/platform/os/cygwin32/preferences.rc \
                      $(srcdir)/platform/os/cygwin32/preferences_resource.h \
                      $(srcdir)/platform/os/cygwin32/preferences_winres.h generated/ROMAW.ico \
                      $(shared_icon_inputs) $(shared_icon_tool_inputs) $(srcdir)/tools/generate-folder-icons.py
	mkdir -p generated
	$(PYTHON) $(srcdir)/tools/generate-folder-icons.py --windows-output generated/image-file-icons
	$(WINDRES) -Igenerated -Igenerated/image-file-icons -i $< -o $@ -O coff

os_sources = platform/os/cygwin32/cygwin32.c \
             platform/os/cygwin32/winfs.c             \
             platform/os/cygwin32/win_disk.c          \
             platform/os/cygwin32/win_stat.c          \
             platform/os/cygwin32/win_memory.c        \
             platform/os/cygwin32/win_afp.c \
             platform/os/cygwin32/win_mactcp_dns.c \
             platform/os/cygwin32/win_mactcp_stream.c \
             platform/os/cygwin32/win_mactcp.c        \
             platform/os/cygwin32/win_serial.c        \
             platform/os/cygwin32/win_ntcd.c          \
             platform/os/cygwin32/win_print.c         \
             platform/os/cygwin32/win_beep.c          \
             platform/os/cygwin32/win_font.c          \
             platform/os/cygwin32/win_clip.c          \
             platform/os/cygwin32/win_temp.c          \
             platform/os/cygwin32/win_except.c        \
             platform/os/cygwin32/win_time.c          \
             platform/os/cygwin32/win_speech.c        \
             platform/os/cygwin32/win_dongle.c        \
             platform/os/cygwin32/win_queue.c         \
             platform/os/cygwin32/win_screen.c        \
             platform/os/cygwin32/win_vxdiface.c      \
             platform/os/cygwin32/win_keyboard.c      \
             platform/os/cygwin32/win_preferences.c   \
             platform/os/cygwin32/win_launch.c        \
             platform/os/cygwin32/win_stdfile.c       \
             platform/os/cygwin32/win_names.c         \
	     platform/os/cygwin32/aspi.h              \
             platform/os/cygwin32/cdenable.h          \
	     platform/os/cygwin32/dosdisk.h           \
	     platform/os/cygwin32/main_windows.h      \
	     platform/os/cygwin32/mmsystem.h          \
	     platform/os/cygwin32/ntcd.h              \
	     platform/os/cygwin32/sysdeps.h           \
	     platform/os/cygwin32/vwin32.h            \
	     platform/os/cygwin32/vxdiface.h          \
	     platform/os/cygwin32/win_cdenable.h      \
	     platform/os/cygwin32/win_clip.h          \
	     platform/os/cygwin32/win_dll.h           \
	     platform/os/cygwin32/win_dongle.h        \
	     platform/os/cygwin32/win_except.h        \
	     platform/os/cygwin32/win_hasp.h          \
	     platform/os/cygwin32/win_keyboard.h      \
	     platform/os/cygwin32/win_memory.h        \
	     platform/os/cygwin32/win_ntcd.h          \
	     platform/os/cygwin32/win_ntinc.h         \
	     platform/os/cygwin32/win_print.h         \
	     platform/os/cygwin32/win_print_private.h \
	     platform/os/cygwin32/win_queue.h         \
	     platform/os/cygwin32/win_screen.h        \
	     platform/os/cygwin32/win_sentpro.h       \
	     platform/os/cygwin32/win_serial.h        \
	     platform/os/cygwin32/win_stat.h          \
	     platform/os/cygwin32/win_temp.h          \
	     platform/os/cygwin32/win_win.h           \
	     platform/os/cygwin32/winfs.h

bin_PROGRAMS += exemove
exemove_SOURCES = platform/os/cygwin32/exemove.c options/paramline.c

sound_sources += sound/sdl/sdl-sound.c \
                 sound/sdl/sdl-sound.h
romaw_core_libraries = libraries/libromaw-controls.a libraries/libromaw-dialogs.a \
  libraries/libromaw-files.a libraries/libromaw-hfs.a libraries/libromaw-lists.a \
  libraries/libromaw-menus.a libraries/libromaw-printing.a libraries/libromaw-quickdraw.a \
  libraries/libromaw-resources.a libraries/libromaw-textedit.a libraries/libromaw-windows.a \
  libraries/libromaw-appleevents.a libraries/libromaw-sound.a libraries/libromaw-numerics.a \
  libraries/libromaw-runtime.a libraries/libromaw-options.a libraries/libromaw-memory.a \
  libraries/libromaw-events.a libraries/libromaw-services.a libraries/libromaw-movies.a \
  libraries/libromaw-devices.a libraries/libromaw-text.a libraries/libromaw-host.a \
  libraries/libromaw-display.a libraries/libromaw-ui.a libraries/libromaw-diagnostics.a \
  libraries/libromaw-platform.a libraries/libromaw-sdl.a
noinst_LIBRARIES = $(romaw_core_libraries)
# Membership changes must replace an existing archive even when every member
# object predates it (including migration from an empty archive).
$(romaw_core_libraries): Makefile
libraries_libromaw_controls_a_SOURCES = $(ctl_sources)
libraries_libromaw_dialogs_a_SOURCES = $(dial_sources)
libraries_libromaw_files_a_SOURCES = $(file_sources)
libraries_libromaw_hfs_a_SOURCES = $(hfs_sources)
libraries_libromaw_lists_a_SOURCES = $(list_sources)
libraries_libromaw_menus_a_SOURCES = $(menu_sources)
libraries_libromaw_printing_a_SOURCES = $(print_sources)
libraries_libromaw_quickdraw_a_SOURCES = $(qd_sources)
libraries_libromaw_resources_a_SOURCES = $(res_sources)
libraries_libromaw_textedit_a_SOURCES = $(te_sources)
libraries_libromaw_windows_a_SOURCES = $(wind_sources)
libraries_libromaw_appleevents_a_SOURCES = $(ae_sources)
libraries_libromaw_sound_a_SOURCES = $(sound_sources)
libraries_libromaw_numerics_a_SOURCES = $(num_sources)
libraries_libromaw_runtime_a_SOURCES = $(runtime_sources)
libraries_libromaw_options_a_SOURCES = $(option_sources)
libraries_libromaw_memory_a_SOURCES = $(memory_sources)
libraries_libromaw_events_a_SOURCES = $(event_sources)
libraries_libromaw_services_a_SOURCES = $(service_sources)
libraries_libromaw_movies_a_SOURCES = $(movie_sources)
libraries_libromaw_devices_a_SOURCES = $(device_sources)
libraries_libromaw_text_a_SOURCES = $(text_sources)
libraries_libromaw_host_a_SOURCES = $(host_sources)
libraries_libromaw_display_a_SOURCES = $(display_sources)
libraries_libromaw_ui_a_SOURCES = $(ui_sources)
libraries_libromaw_diagnostics_a_SOURCES = $(diagnostic_sources)
libraries_libromaw_platform_a_SOURCES = $(arch_sources) $(os_sources)
libraries_libromaw_sdl_a_SOURCES = $(front_end_sources)
ROMAW_SOURCES = frontend-sdl/winmain.c $(include_sources) $(other_sources)

.PHONY: library-manifest
library-manifest:
	@$(foreach library,$(romaw_core_libraries),printf '%s\t%s\n' '$(library)' '$($(subst -,_,$(subst /,_,$(subst .,_,$(library))))_SOURCES)';)
	@printf '%s\t%s\n' 'ROMAW.exe' '$(ROMAW_SOURCES)'

pkgdata_DATA =
