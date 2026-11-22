/*+===================================================================
  File:      guest_code.r

  Summary:   Guest ABI veneers for the native Finder resource procedures.

  Classes:   None.

  Functions: No exported functions; resource declarations only.

  Origin:    Finder 7.5.5 resource identities with ROMAW native dispatch veneers.

  Copyright and Legal notices: Applicable repository license: COPYING.
===================================================================+*/

data 'CDEF' (128, purgeable) { $$finderCode() };
data 'WDEF' (128) { $$finderCode() };
data 'gest' (128, "Main", sysheap, locked) { $$finderCode() };
data 'hcsl' (0, "Main", sysheap, locked) { $$finderCode() };
