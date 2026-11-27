/*+===================================================================
  File:      mactcp_driver_guest_probe.r

  Summary:   Original failing .IPP DRVR resource for driver precedence tests.

  Classes:   None.

  Functions: Resource definitions only.

  Origin:    ROMAW original 68K fixture; Open returns ipLoadErr.

  Copyright and Legal notices: See COPYING.
===================================================================+*/
data 'DRVR' (22, ".IPP") {
    $"0000 0000 0000 0000 0018 001E 001E 001E 001E"
    $"042E 4950 5000 303C A625 4E75 4E75"
};
