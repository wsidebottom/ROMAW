# +===================================================================
#   File:      Makefile.am
#
#   Summary:   Defines build targets and dependencies for syn68k 68K translation and runtime support.
#
#   Classes:   None.
#
#   Functions: No explicitly exported functions.
#
#   Origin:    Existing repository source in syn68k/src/runtime; original attribution and provenance comments are
#              preserved below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              syn68k/COPYING.
# ===================================================================+

SUBDIRS =

lib_LIBRARIES = libsyn68k.a
include_HEADERS = $(srcdir)/../include/syn68k_public.h


SYN68K_CFLAGS=@SYN68K_CFLAGS@
CLEANUP=@assembly_cleanup_script@
OPTIMIZE=@assembly_optimization_script@

# The way we use labels in our assembler is out of spec.  The gcc info
# pages say that you're not allowed to jump between labels of asm.
# Historically, however, we chose not to put everything into a single
# function and then jump about within spec.  IIRC, that was either due
# to the compilation time and memory consumption to be excessive
# and/or due to us running into compiler bugs.  This was all back
# around 1995 though and we have more memory and better compilers.  As
# such, it may make sense to re-evaluate the decision to split all the
# opcodes into separate functions.  However, until we do that, we need
# to deal with the fact that some dead-code-elimination (DCE)
# optimizations will cause our trickery to fail.  As such, we include
# "-fno-dce" in the architectures where we know we're going to have trouble.

dce_fixup=@dce_fixup@

AM_CFLAGS = -DRUNTIME

DIST_SOURCES = 68k.defines.scm 68k.scm alloc.c backpatch.c block.c fpu.c ea.c jit-x64.c jit-memory.c \
               mmu040.c mmu040-memory.c cache040.c mapped-cpu.c native040-code.c native040-mmu.c native040-fpu.c native040-ea.c \
               native040-control.c native040-registers.c native040-flat.c native040-cache.c flat-memory.c \
               blockinfo.c callback.c checksum.c deathqueue.c destroyblock.c \
               fpsp.c fpsp-adapter.S \
	       diagnostics.c hash.c i486-cleanup.pl \
	       i486-optimize.pl init.c interrupt.c opcode_dummy.c \
               profile.c rangetree.c syn68k_header.c \
	       translate.c trap.c \
\
               include/alloc.h include/jit-x64.h include/jit-cache.h include/jit-fpu.h include/jit-mmu.h \
	       include/backpatch.h include/block.h include/blockinfo.h \
	       include/callback.h include/checksum.h \
               include/deathqueue.h include/destroyblock.h \
               include/diagnostics.h include/hash.h include/interrupt.h \
	       include/mapping.h include/profile.h \
	       include/rangetree.h include/translate.h \
	       include/trap.h include/ea.h include/mmu040.h \
               include/mmu040-memory.h include/cache040.h include/mapped-cpu.h include/fpu.h include/integer.h include/native040.h \
               include/native040-ea.h include/native040-memory.h include/fpu_convert.h include/fpu-captured.h \
               include/control040.h include/native040-control.h include/native040-registers.h include/native040-flat.h include/movem040.h


LOCAL_INCLUDES =

LOCAL_INCLUDES += -I../include -I. -I$(srcdir)/include \
  -I$(srcdir)/../include -I$(srcdir) -I../include

LOCAL_CFLAGS = -DRUNTIME -Iinclude

all: libsyn68k.a

.c.o:
	$(CC) $(CPPFLAGS) $(AM_CFLAGS) $(CFLAGS) -c $(LOCAL_INCLUDES) $< -o $@

OBJS =	block.o diagnostics.o hash.o rangetree.o translate.o alloc.o ea.o jit-x64.o jit-memory.o \
        mmu040.o mmu040-memory.o cache040.o mapped-cpu.o native040-code.o \
	blockinfo.o trap.o destroyblock.o callback.o init.o flat-memory.o interrupt.o	\
	profile.o deathqueue.o checksum.o	\
	backpatch.o						\
	mapindex.o mapinfo.o syn68k.o opcode_dummy.o fpu.o softfloat.o fpsp.o

generated/fpsp-image.h: $(srcdir)/../../tools/build-fpsp.py \
                      $(srcdir)/fpsp-adapter.S \
                      $(wildcard $(srcdir)/../../third_party/fpsp040/*.S) \
                      $(srcdir)/../../third_party/fpsp040/fpsp.h
	mkdir -p generated
	python3 $(srcdir)/../../tools/build-fpsp.py generated

# Recompute the identity cheaply each build; the script preserves the header's
# timestamp when inputs are unchanged. No stale helper ABI may restore code.
.PHONY: jit-build-id-force
jit-build-id-force:

generated/jit-build-id.h: jit-build-id-force
	python3 $(srcdir)/../../tools/jit-build-id.py $@ "$(CC)" "$(CPPFLAGS) $(AM_CFLAGS) $(CFLAGS) $(SYN68K_CFLAGS) $(LOCAL_INCLUDES)"

jit-x64.o: generated/jit-build-id.h

fpsp.o: generated/fpsp-image.h

softfloat.o: $(srcdir)/../../third_party/softfloat/softfloat.c \
             $(wildcard $(srcdir)/../../third_party/softfloat/softfloat*)
	$(CC) $(CPPFLAGS) $(AM_CFLAGS) $(CFLAGS) -O2 -fwrapv \
	  -c $< -o $@

fpu.o: $(wildcard $(srcdir)/../../third_party/softfloat/softfloat*)

jit-memory.o: $(srcdir)/jit-memory.c
	$(CC) $(CPPFLAGS) $(AM_CFLAGS) $(CFLAGS) -O2 -c $(LOCAL_INCLUDES) $< -o $@

ea.o: $(srcdir)/ea.c
	$(CC) $(CPPFLAGS) $(AM_CFLAGS) $(CFLAGS) -O2 -c $(LOCAL_INCLUDES) $< -o $@

# These objects use hand-written rules, so Automake does not track their
# headers.  Address conversion changes must reach every runtime object.
$(OBJS): Makefile $(srcdir)/../include/syn68k_public.h \
         ../include/syn68k_private.h \
         $(wildcard $(srcdir)/include/*.h)

libsyn68k.a: $(OBJS)
	$(RM) libsyn68k.a
	$(AR) cq $@ $(OBJS)
	$(RANLIB) $@

# Syn68k uses inline assembly that confuses various gcc stack optimizations.
# Under gcc 2.x -fno-defer-pop is sufficient to get gcc to do the right thing
# with our code.  Under gcc 3.x we need to also add -maccumulate-outgoing-args.
# Since we don't know what gcc we have until we try to execute CC, we
# do a test right here to figure out if the c-compiler understands
# -maccumulate-outgoing-args.  If it does, we use it.
#
# A better solution would be to put something into our asm to form a
# barrier that gcc won't try to do stack optimizations across.  I haven't yet
# scoured the gcc 3 documentation or source code to see if there's something
# obvious that can be used.

syn68k.o: syn68k.c $(srcdir)/i486-cleanup.pl $(srcdir)/i486-optimize.pl
	outgoing=; reserved=;\
	case "$(host_os)" in mingw*) reserved=-ffixed-ebp;; esac; \
	$(CC) -maccumulate-outgoing-args -c -x c /dev/null 2> /dev/null \
            && outgoing=-maccumulate-outgoing-args; \
	$(CC) $(CPPFLAGS) -S $(SYN68K_CFLAGS) $(dce_fixup) -Wall -static -fno-defer-pop -Wno-unused\
		$(LOCAL_INCLUDES) $$outgoing $$reserved syn68k.c -o ./syn68k.s
	if [ x"$(CLEANUP)" != x ] ; then \
		$(PERL) $(srcdir)/$(CLEANUP) < syn68k.s > syn68k.s.new && \
                mv syn68k.s.new syn68k.s ; fi
	if [ x"$(OPTIMIZE)" != x ] ; then \
		$(PERL) $(srcdir)/$(OPTIMIZE) < syn68k.s > syn68k.s.new && \
                mv syn68k.s.new syn68k.s ; fi
	$(CC) -xassembler-with-cpp -c ./syn68k.s -o syn68k.o
	$(RM) ./syn68k.s

syn68k.c mapindex.c mapinfo.c:	../syngen/syngen \
                                $(srcdir)/syn68k_header.c \
                                $(srcdir)/68k.scm $(srcdir)/68k.defines.scm \
				../include/syn68k_private.h    \
				../runtime/include/hash.h      \
				../runtime/include/interrupt.h \
				../runtime/include/trap.h
	if [ "$(srcdir)" != "." ]; then \
	  cp -p $(srcdir)/68k.scm $(srcdir)/syn68k_header.c $(srcdir)/68k.defines.scm . ; \
	fi
	../syngen/syngen -v 68k.scm

clean-local:
	-rm -rf generated
	-rm -f mapinfo.c syn68k.c mapindex.c
	if [ "$(srcdir)" != "." ]; then \
	  rm -f 68k.scm syn68k_header.c 68k.defines.scm; \
	fi

# Arithmetic/MMU byte images are compiled independently of runtime instrumentation.
generated/native040.h: $(srcdir)/../../tools/build-native040.py \
                      $(srcdir)/../../../tools/native_code_image.py \
                      $(srcdir)/native040-fpu.c $(srcdir)/native040-mmu.c \
                      $(srcdir)/native040-ea.c $(srcdir)/include/movem040.h $(srcdir)/ea.c $(srcdir)/blockinfo.c \
                      $(srcdir)/native040-control.c $(srcdir)/include/native040-control.h $(srcdir)/include/control040.h \
                      $(srcdir)/native040-registers.c $(srcdir)/include/native040-registers.h \
                      $(srcdir)/native040-flat.c $(srcdir)/include/native040-flat.h \
                      $(srcdir)/native040-cache.c $(srcdir)/cache040.c $(srcdir)/include/cache040.h \
                      $(srcdir)/include/trap.h \
                      $(srcdir)/mmu040.c $(srcdir)/mmu040-memory.c \
                      $(srcdir)/include/native040.h $(srcdir)/include/mmu040.h \
                      $(srcdir)/include/native040-ea.h $(srcdir)/include/native040-memory.h \
                      $(srcdir)/include/ea.h $(srcdir)/include/blockinfo.h \
                      ../include/syn68k_private.h $(srcdir)/../include/syn68k_public.h \
                      $(srcdir)/include/mmu040-memory.h \
                      $(srcdir)/include/fpu_arithmetic.h $(srcdir)/include/fpu_exception.h $(srcdir)/include/fpu-captured.h \
                      $(srcdir)/include/fpu_convert.h \
                      $(wildcard $(srcdir)/../../third_party/softfloat/softfloat*)
	mkdir -p generated
	python3 $(srcdir)/../../tools/build-native040.py $@ "$(CC)" ../include

native040-code.o: generated/native040.h
