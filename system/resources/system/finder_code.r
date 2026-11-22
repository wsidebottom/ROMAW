/*+===================================================================
  File:      finder_code.r

  Summary:   Built-in fallback resources for the native Finder guest procedures.

  Classes:   None.

  Functions: No exported functions; resource declarations only.

  Origin:    Finder resource identities; payloads share the checked ROMAW encoder.

  Copyright and Legal notices: Applicable repository license: COPYING.
===================================================================+*/

data 'CDEF' (128, purgeable) { $$finderCode() };
data 'WDEF' (128) { $$finderCode() };
data 'gest' (128, "Main", sysheap, locked) { $$finderCode() };
data 'hcsl' (0, "Main", sysheap, locked) { $$finderCode() };
