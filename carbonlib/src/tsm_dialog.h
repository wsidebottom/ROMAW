/*+===================================================================
  File:      tsm_dialog.h

  Summary:   Accesses CarbonLib Text Services Manager dialog attachments.

  Classes:   None.

  Functions: Private registry adapters for TSMTE dialog accessors.

  Origin:    CarbonLib 1.4 and Universal Interfaces 3.4.2 TSMTE.h.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMAW_CARBON_TSM_DIALOG_H
#define ROMAW_CARBON_TSM_DIALOG_H

#include "cf_batch.h"

enum {
    tsm_dialog_document = 0xaa,
    tsm_dialog_text_record = 0xae,
    tsm_dialog_signature = 0xba,
};

static uint32_t text_service_dialog_document(uint32_t dialog)
{
    const unsigned char* record = dialog ? ROMlib_carbon_try_guest_memory(dialog, tsm_dialog_signature + 4, 0) : NULL;
    return record && cfp_read(dialog + tsm_dialog_signature) == TICK("tmTE") ? cfp_read(dialog + tsm_dialog_document)
                                                                             : 0;
}

static int carbon_IsTSMTEDialog(ROMlib_carbon_ppc_frame* call)
{
    call->words[0] = text_service_dialog_document(call->words[0]) != 0;
    return 1;
}

static int carbon_GetTSMTEDialogDocumentID(ROMlib_carbon_ppc_frame* call)
{
    call->words[0] = text_service_dialog_document(call->words[0]);
    return 1;
}

static int carbon_GetTSMTEDialogTSMTERecHandle(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t dialog = call->words[0];
    call->words[0] = text_service_dialog_document(dialog) ? cfp_read(dialog + tsm_dialog_text_record) : 0;
    return 1;
}

static int carbon_SetTSMTEDialogDocumentID(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t dialog = call->words[0];
    return !text_service_dialog_document(dialog) || cfp_write(dialog + tsm_dialog_document, call->words[1]);
}

static int carbon_SetTSMTEDialogTSMTERecHandle(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t dialog = call->words[0];
    return !text_service_dialog_document(dialog) || cfp_write(dialog + tsm_dialog_text_record, call->words[1]);
}

#endif
