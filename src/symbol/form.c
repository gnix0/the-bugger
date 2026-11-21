#include "symbol/form.h"

typedef enum {
    FORM_FIXED,
    FORM_STRING,
    FORM_LEB,
    FORM_LENGTH_FIXED,
    FORM_LENGTH_LEB,
    FORM_INDIRECT,
    FORM_BLOCK
} FormPhase_e;

static bool room(TbSymbolForm_t *form, uint64_t bytes)
{
    if (bytes > form->request.available - form->view.bytes) {
        form->status = TB_SYMBOL_FORM_TRUNCATED;
        return false;
    }
    return true;
}


static void select_form(TbSymbolForm_t *form, uint64_t code, bool indirect)
{
    form->view.form = code;
    form->view.indirect = indirect;
    form->view.payload_offset = form->view.bytes;
    form->fixed = 0;
    form->accumulator = 0;
    form->phase = FORM_FIXED;
    if ((form->request.version == 4 && code >= TB_SYMBOL_FORM_STRX && code != TB_SYMBOL_FORM_REF_SIG8) ||
        !code || code == 2 || code > TB_SYMBOL_FORM_ADDRX4) {
        form->status = TB_SYMBOL_FORM_UNSUPPORTED;
        return;
    }
    switch (code) {
    case TB_SYMBOL_FORM_ADDR:
        form->fixed = form->request.address_bytes;
        break;
    case TB_SYMBOL_FORM_BLOCK1:
    case TB_SYMBOL_FORM_BLOCK2:
    case TB_SYMBOL_FORM_BLOCK4:
        form->fixed = code == TB_SYMBOL_FORM_BLOCK1 ? 1 : code == TB_SYMBOL_FORM_BLOCK2 ? 2 : 4;
        form->phase = FORM_LENGTH_FIXED;
        break;
    case TB_SYMBOL_FORM_DATA1:
    case TB_SYMBOL_FORM_REF1:
    case TB_SYMBOL_FORM_FLAG:
    case TB_SYMBOL_FORM_STRX1:
    case TB_SYMBOL_FORM_ADDRX1:
        form->fixed = 1;
        break;
    case TB_SYMBOL_FORM_DATA2:
    case TB_SYMBOL_FORM_REF2:
    case TB_SYMBOL_FORM_STRX2:
    case TB_SYMBOL_FORM_ADDRX2:
        form->fixed = 2;
        break;
    case TB_SYMBOL_FORM_STRX3:
    case TB_SYMBOL_FORM_ADDRX3:
        form->fixed = 3;
        break;
    case TB_SYMBOL_FORM_DATA4:
    case TB_SYMBOL_FORM_REF4:
    case TB_SYMBOL_FORM_REF_SUP4:
    case TB_SYMBOL_FORM_STRX4:
    case TB_SYMBOL_FORM_ADDRX4:
        form->fixed = 4;
        break;
    case TB_SYMBOL_FORM_DATA8:
    case TB_SYMBOL_FORM_REF8:
    case TB_SYMBOL_FORM_REF_SIG8:
    case TB_SYMBOL_FORM_REF_SUP8:
        form->fixed = 8;
        break;
    case TB_SYMBOL_FORM_DATA16:
        form->fixed = 16;
        break;
    case TB_SYMBOL_FORM_STRP:
    case TB_SYMBOL_FORM_REF_ADDR:
    case TB_SYMBOL_FORM_SEC_OFFSET:
    case TB_SYMBOL_FORM_STRP_SUP:
    case TB_SYMBOL_FORM_LINE_STRP:
        form->fixed = form->request.offset_bytes;
        break;
    case TB_SYMBOL_FORM_STRING:
        form->phase = FORM_STRING;
        break;
    case TB_SYMBOL_FORM_BLOCK:
    case TB_SYMBOL_FORM_EXPRLOC:
        form->phase = FORM_LENGTH_LEB;
        break;
    case TB_SYMBOL_FORM_UDATA:
    case TB_SYMBOL_FORM_REF_UDATA:
    case TB_SYMBOL_FORM_SDATA:
    case TB_SYMBOL_FORM_STRX:
    case TB_SYMBOL_FORM_ADDRX:
    case TB_SYMBOL_FORM_LOCLISTX:
    case TB_SYMBOL_FORM_RNGLISTX:
        form->phase = FORM_LEB;
        break;
    case TB_SYMBOL_FORM_INDIRECT:
        form->phase = FORM_INDIRECT;
        break;
    case TB_SYMBOL_FORM_FLAG_PRESENT:
        form->view.numeric = true;
        form->view.bits = 1;
        form->status = TB_SYMBOL_FORM_COMPLETE;
        return;
    case TB_SYMBOL_FORM_IMPLICIT_CONST:
        if (!indirect) {
            form->status = TB_SYMBOL_FORM_COMPLETE;
            return;
        }
        form->phase = FORM_LEB;
        break;
    default:
        form->status = TB_SYMBOL_FORM_UNSUPPORTED;
        return;
    }
    form->remaining = form->fixed;
    form->view.is_signed = code == TB_SYMBOL_FORM_SDATA || code == TB_SYMBOL_FORM_IMPLICIT_CONST;
    (void)tb_symbol_leb_init(&form->leb, form->view.is_signed);
    if (form->fixed)
        (void)room(form, form->fixed);
}


TbSymbolFormStatus_e tb_symbol_form_init(TbSymbolForm_t *form, TbSymbolFormRequest_t request)
{
    if (!form || (request.version != 4 && request.version != 5) ||
        (request.offset_bytes != 4 && request.offset_bytes != 8) || !request.address_bytes ||
        request.address_bytes > UINT8_MAX ||
        (request.byte_order != TB_SYMBOL_LITTLE_ENDIAN && request.byte_order != TB_SYMBOL_BIG_ENDIAN)) {
        return TB_SYMBOL_FORM_INVALID;
    }
    *form = (TbSymbolForm_t){
        .request     = request,
        .status      = TB_SYMBOL_FORM_PENDING,
        .initialized = true
    };
    select_form(form, request.form, false);
    return form->status;
}


static void block(TbSymbolForm_t *form, uint64_t bytes)
{
    form->view.payload_offset = form->view.bytes;
    form->view.payload_bytes = bytes;
    form->remaining = bytes;
    form->phase = FORM_BLOCK;
    if (room(form, bytes) && !bytes)
        form->status = TB_SYMBOL_FORM_COMPLETE;
}


static void leb_complete(TbSymbolForm_t *form)
{
    TbSymbolLebView_t view;
    if (!tb_symbol_leb_view(&form->leb, &view)) {
        form->status = TB_SYMBOL_FORM_INVALID;
        return;
    }
    if (form->phase == FORM_INDIRECT) {
        if (view.fits)
            select_form(form, view.bits, true);
        else
            form->status = TB_SYMBOL_FORM_UNSUPPORTED;
    } else if (form->phase == FORM_LENGTH_LEB) {
        if (view.fits)
            block(form, view.bits);
        else
            form->status = TB_SYMBOL_FORM_TRUNCATED;
    } else {
        form->view.numeric = view.fits;
        form->view.bits = view.bits;
        form->view.payload_bytes = view.bytes;
        form->status = TB_SYMBOL_FORM_COMPLETE;
    }
}


TbSymbolFormStatus_e tb_symbol_form_feed(TbSymbolForm_t *form, const void *bytes, size_t length,
                                        size_t *consumed)
{
    if (!form || !form->initialized || !consumed || (length && !bytes))
        return TB_SYMBOL_FORM_INVALID;
    *consumed = 0;
    const unsigned char *input = bytes;
    size_t count = length < 256 ? length : 256;
    while (*consumed < count && form->status == TB_SYMBOL_FORM_PENDING) {
        if (!room(form, 1))
            break;
        unsigned byte = input[(*consumed)++];
        ++form->view.bytes;
        if (form->phase == FORM_LEB || form->phase == FORM_LENGTH_LEB || form->phase == FORM_INDIRECT) {
            unsigned char encoded = (unsigned char)byte;
            size_t taken;
            TbSymbolReadStatus_e status = tb_symbol_leb_feed(&form->leb, &encoded, 1, &taken);
            if (status == TB_SYMBOL_READ_OK)
                leb_complete(form);
            else if (status != TB_SYMBOL_READ_LIMIT)
                form->status = TB_SYMBOL_FORM_INVALID;
        } else if (form->phase == FORM_STRING) {
            ++form->view.payload_bytes;
            if (!byte) {
                --form->view.payload_bytes;
                form->status = TB_SYMBOL_FORM_COMPLETE;
            }
        } else {
            if (form->phase != FORM_BLOCK && form->fixed <= 8) {
                unsigned shift = form->request.byte_order == TB_SYMBOL_LITTLE_ENDIAN
                                     ? (unsigned)(form->fixed - form->remaining) * 8
                                     : (unsigned)(form->remaining - 1) * 8;
                form->accumulator |= (uint64_t)byte << shift;
            }
            if (!--form->remaining) {
                if (form->phase == FORM_LENGTH_FIXED) {
                    block(form, form->accumulator);
                } else {
                    if (form->phase == FORM_FIXED) {
                        form->view.payload_bytes = form->fixed;
                        form->view.numeric = form->fixed <= 8;
                        form->view.bits = form->view.numeric ? form->accumulator : 0;
                    }
                    form->status = TB_SYMBOL_FORM_COMPLETE;
                }
            }
        }
    }
    if (form->status == TB_SYMBOL_FORM_PENDING && form->view.bytes == form->request.available)
        form->status = TB_SYMBOL_FORM_TRUNCATED;
    return form->status;
}


TbSymbolFormStatus_e tb_symbol_form_finish(const TbSymbolForm_t *form)
{
    if (!form || !form->initialized)
        return TB_SYMBOL_FORM_INVALID;
    return form->status == TB_SYMBOL_FORM_PENDING ? TB_SYMBOL_FORM_TRUNCATED : form->status;
}


bool tb_symbol_form_view(const TbSymbolForm_t *form, TbSymbolFormView_t *view)
{
    if (!view || tb_symbol_form_finish(form) != TB_SYMBOL_FORM_COMPLETE)
        return false;
    *view = form->view;
    return true;
}
