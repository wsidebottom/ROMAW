# SoftFloat Release 2b

John R. Hauser's software IEEE arithmetic, imported from Musashi revision
`313ebf1bd9f4d0d93341eb5ce21fd8a119e9dbdd`, `softfloat/`:
https://github.com/kstenerud/Musashi/tree/313ebf1bd9f4d0d93341eb5ce21fd8a119e9dbdd/softfloat

The copyright, permission and disclaimer notices in the original files are
retained. These files have their own license; do not replace those notices
with Syn68k's license. Upstream formatting is preserved.

This is a derivative integration: `softfloat.c` includes `softfloat-compat.h`
instead of Musashi's CPU header. The new compatibility header supplies fixed
width types and inline/literal macros without depending on another CPU core.
The instruction decoder and Motorola register/exception adapter are maintained
separately in `runtime/fpu.c`. No host floating-point representation is used
for the extended-precision arithmetic.

The optional `SYN68K_SOFTFLOAT_CONTEXT` build suppresses the four global
state declarations and definitions. The closed native 68040 arithmetic adapter binds those
names to its invocation-local rounding, precision, exception and tininess
fields before importing the arithmetic definitions. Ordinary library builds keep
the original globals and behavior. No arithmetic algorithms or legal notices
are changed by this specialization.
