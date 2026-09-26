#include "vmsys.h"
#include "vmio.h"
#include "vmgraph.h"
#include "vmeditor.h"
#include "vmchset.h"
#include "vmstdlib.h"
#include "stdint.h"
#include <string.h>
#include <time.h>

#define VM_CHSET_DEST_BYTES 4096                      /* vm_chset_convert target max bytes (<=4096 per spec) */
#define UCS2_CHUNK_WORDS    (VM_CHSET_DEST_BYTES / 2) /* 2048 words */

static VMINT layer_hdl[1];
static VMWCHAR left_label[32];
static VMWCHAR right_label[32];
static VMWCHAR center_label[32];
static VMWCHAR center_label1[32];
VMINT s_width = 0;
VMINT s_height = 0;

vm_editor_font_attribute my_font = {0, 0, 0, 16}; //16 18

static vm_input_mode_enum my_input_modes_lower_first[] = {VM_INPUT_MODE_MULTITAP_FIRST_UPPERCASE_ABC, VM_INPUT_MODE_123, VM_INPUT_MODE_123_SYMBOLS, VM_INPUT_MODE_NONE};

static VMINT32 history_editor = 0;
static VMINT32 input_editor   = 0;
static VMUWSTR history_buf = NULL;
static VMINT history_buf_bytes = 4096;     // 32 KB 32768
static VMUWSTR input_buf = NULL;
static VMINT input_buf_bytes = 512; //2048
static VMINT history_h;
static VMINT input_h = 64;
VMWCHAR outfile[100] = {0};

void handle_sysevt(VMINT message, VMINT param);
void allocate_buffers(void);
VMUINT32 ime_cb(VMINT32 h, vm_editor_message_struct_p m);
void append_history(VMWSTR msg);
void send_message(void);
void clear_history(void);
void close_and_exit(void);
void activate_input(void);
void activate_history(void);
void create_auto_filename(VMWSTR text);
static VMINT save_history_to_utf8_chunked(VMWSTR outfile_path);
void save_history(void);

void vm_main(void)
{
    layer_hdl[0] = -1;

    vm_reg_sysevt_callback(handle_sysevt);

    allocate_buffers();

if(!history_buf || !input_buf)
{
    vm_exit_app();
    return;
}

    s_width = vm_graphic_get_screen_width();
    s_height = vm_graphic_get_screen_height();

    history_h = s_height - input_h - vm_editor_get_softkey_height();

    vm_ascii_to_ucs2(left_label, sizeof(left_label), (VMSTR)"Swich");
    vm_ascii_to_ucs2(center_label, sizeof(center_label), (VMSTR)"Send");
    vm_ascii_to_ucs2(center_label1, sizeof(center_label1), (VMSTR)"Save");
    vm_ascii_to_ucs2(right_label, sizeof(right_label), (VMSTR)"Clear");

}


void handle_sysevt(VMINT message, VMINT param)
{
    switch(message)
    {
    case VM_MSG_CREATE:
    case VM_MSG_ACTIVE:

        if(layer_hdl[0] < 0)
        {
            layer_hdl[0] = vm_graphic_create_layer(0, 0, s_width, s_height, -1);
            vm_graphic_set_clip(0, 0, s_width, s_height);
        }

        if(history_editor == 0)
        {
            history_editor = vm_editor_create(VM_EDITOR_MULTILINE, 0, 0, s_width, history_h, history_buf, history_buf_bytes, VM_FALSE, layer_hdl[0]);
            vm_editor_set_bg_border_style(history_editor, VM_EDITOR_SINGLE_BORDER, 0x001F, 0x001F);
            vm_editor_set_multiline_text_font(history_editor, my_font);
            vm_editor_set_IME(history_editor, VM_INPUT_TYPE_SENTENCE, my_input_modes_lower_first, VM_INPUT_MODE_MULTITAP_FIRST_UPPERCASE_ABC, ime_cb);
            vm_editor_set_softkey(history_editor, (VMUWSTR)left_label, VM_LEFT_SOFTKEY, activate_input);
            vm_editor_set_softkey(history_editor, (VMUWSTR)center_label1, VM_CENTER_SOFTKEY, save_history);
            vm_editor_set_softkey(history_editor, (VMUWSTR)right_label, VM_RIGHT_SOFTKEY, clear_history);
            vm_editor_show(history_editor);
        }

        if(input_editor == 0)
        {
            input_editor = vm_editor_create(VM_EDITOR_MULTILINE, 0, history_h, s_width, input_h, input_buf, input_buf_bytes, VM_FALSE, layer_hdl[0]);
            vm_editor_set_bg_border_style(input_editor, VM_EDITOR_DOUBLE_BORDER, 0x07E0, 0x07E0);
            vm_editor_set_multiline_text_font(input_editor, my_font);
            vm_editor_set_IME(input_editor, VM_INPUT_TYPE_SENTENCE, my_input_modes_lower_first, VM_INPUT_MODE_MULTITAP_FIRST_UPPERCASE_ABC, ime_cb);
            vm_editor_set_softkey(input_editor, (VMUWSTR)left_label, VM_LEFT_SOFTKEY, activate_history);
            vm_editor_set_softkey(input_editor, (VMUWSTR)center_label, VM_CENTER_SOFTKEY, send_message);
            vm_editor_set_softkey(input_editor, (VMUWSTR)right_label, VM_RIGHT_SOFTKEY, clear_history);
            vm_editor_activate(input_editor, VM_FALSE);
        }
        vm_switch_power_saving_mode(turn_off_mode);
        break;

    case VM_MSG_PAINT:

        if(history_editor)
            vm_editor_show(history_editor);

        if(input_editor)
            vm_editor_show(input_editor);

        break;

    case VM_MSG_INACTIVE:

        vm_switch_power_saving_mode(turn_on_mode);

        if(input_editor)
            vm_editor_deactivate(input_editor);

        if(history_editor)
        {
            vm_editor_close(history_editor);
            history_editor = 0;
        }

        if(input_editor)
        {
            vm_editor_close(input_editor);
            input_editor = 0;
        }

        if(layer_hdl[0] != -1)
        {
            vm_graphic_delete_layer(layer_hdl[0]);
            layer_hdl[0] = -1;
        }

        break;

    case VM_MSG_QUIT:

        close_and_exit();
        break;
    }
}

void allocate_buffers(void)
{
    history_buf = (VMUWSTR)vm_malloc(history_buf_bytes);
    input_buf = (VMUWSTR)vm_malloc(input_buf_bytes);

    if(history_buf)
        history_buf[0] = 0;

    if(input_buf)
        input_buf[0] = 0;
}

void append_history(VMWSTR msg)
{

    if (!history_buf || !msg || msg[0] == 0)
        return;

    VMWCHAR nl[2];
    VMINT used = vm_wstrlen((VMWSTR)history_buf);
    VMINT add  = vm_wstrlen(msg);

    if ((used + add + 2) * sizeof(VMWCHAR) >= history_buf_bytes)
    {
        return;
    }

    vm_wstrcat((VMWSTR)history_buf, (VMWSTR)msg);

    nl[0] = '\n';
    nl[1] = 0;

    vm_wstrcat((VMWSTR)history_buf, (VMWSTR)nl);

    //if(history_editor)
    //{
       //vm_editor_show(history_editor);
    vm_editor_set_text(history_editor, history_buf, history_buf_bytes);
    vm_editor_show(history_editor);
    vm_editor_show(input_editor);
   //}

}

void send_message(void)
{
    VMWCHAR text[256];

    vm_editor_get_text(input_editor, (VMUWSTR)text, sizeof(text));

    if (vm_wstrlen(text) == 0)
        return;

    input_buf[0] = 0;
    vm_editor_set_text(input_editor, input_buf, input_buf_bytes);

    vm_editor_show(history_editor);

    append_history(text);

}

void clear_history(void){}

void close_and_exit(void)
{

    if(history_editor)
    {
        vm_editor_close(history_editor);
        history_editor = 0;
    }

    if(input_editor)
    {
        vm_editor_close(input_editor);
        input_editor = 0;
    }

    if(history_buf)
    {
        vm_free(history_buf);
        history_buf = NULL;
    }

    if(input_buf)
    {
        vm_free(input_buf);
        input_buf = NULL;
    }

    if(layer_hdl[0] != -1)
    {
       vm_graphic_delete_layer(layer_hdl[0]);
       layer_hdl[0] = -1;
    }

    vm_exit_app();
}

VMUINT32 ime_cb(VMINT32 h, vm_editor_message_struct_p m) {

    if (!m) return 0;
    switch (m->message_id) {
        case VM_EDITOR_MESSAGE_ACTIVATE:
        case VM_EDITOR_MESSAGE_REDRAW_FLOATING_UI:
        case VM_EDITOR_MESSAGE_REDRAW_IMUI_RECTANGLE:
            //update_center_label_from_buffer();
            //if(history_editor)
            //    vm_editor_show(history_editor);
            //    vm_editor_redraw_ime_screen();
            //}

            //if(input_editor)
            //    vm_editor_show(input_editor);
            //    vm_editor_redraw_ime_screen();
            //}
            break;
        case VM_EDITOR_MESSAGE_DEACTIVATE:
            break;
        default:
            break;
    }
    return 0;
}

void activate_input(void)
{
    vm_editor_activate(input_editor, VM_FALSE);
    vm_editor_set_pos(history_editor, 0, 0);
    vm_editor_set_size(history_editor, s_width, history_h);
    vm_editor_set_pos(input_editor, 0, history_h);
    vm_editor_set_size(input_editor, s_width,input_h);
    vm_editor_show(history_editor);
    vm_editor_show(input_editor);
}

void activate_history(void)
{
    vm_editor_activate(history_editor, VM_FALSE);
    vm_editor_set_pos(history_editor, 0, 0);
    vm_editor_set_size(history_editor, s_width, s_height - vm_editor_get_softkey_height());
    vm_editor_set_pos(input_editor, 0, s_height);
    vm_editor_set_size(input_editor, 0, 0);
    vm_editor_show(history_editor);
}

void create_auto_filename(VMWSTR text) {

    VMINT drv;
    VMCHAR fAutoFileName[100] = {0};
    VMCHAR fData_text[100] = {0};
    struct vm_time_t curr_time;

    vm_get_time(&curr_time);

    if ((drv = vm_get_removable_driver()) < 0) {
       drv = vm_get_system_driver();
    }

    sprintf(fAutoFileName, "%c:\\", drv);
    sprintf(fData_text, "%02d%02d%02d%02d%02d.txt", curr_time.mon, curr_time.day, curr_time.hour, curr_time.min, curr_time.sec);
    strcat(fAutoFileName, fData_text);
    vm_ascii_to_ucs2(text, (strlen(fAutoFileName) + 1) * 2, fAutoFileName);
}

static VMINT save_history_to_utf8_chunked(VMWSTR outfile_path)
{
    if (!outfile_path)
        return -1;

    VMFILE fh = vm_file_open(outfile_path, MODE_CREATE_ALWAYS_WRITE, FALSE);

    if (fh < 0)
        return -2;

    VMWCHAR *u_chunk = (VMWCHAR *)vm_calloc((UCS2_CHUNK_WORDS + 4) * sizeof(VMWCHAR));

    VMCHAR *out_utf8 = (VMCHAR *)vm_calloc(VM_CHSET_DEST_BYTES + 8);

    if (!u_chunk || !out_utf8)
    {
        if (u_chunk)
            vm_free(u_chunk);

        if (out_utf8)
            vm_free(out_utf8);

        vm_file_close(fh);

        return -3;
    }

    VMINT history_len = vm_wstrlen((VMWSTR)history_buf);

    VMINT remaining = history_len;
    VMINT pos = 0;

    while (remaining > 0)
    {
        VMINT take = (remaining > UCS2_CHUNK_WORDS) ? UCS2_CHUNK_WORDS : remaining;

        vm_wstrncpy(u_chunk, (VMWSTR)history_buf + pos, take);

        u_chunk[take] = 0;

        VMINT conv = vm_chset_convert(VM_CHSET_UCS2, VM_CHSET_UTF8, (VMCHAR *)u_chunk, (VMCHAR *)out_utf8, VM_CHSET_DEST_BYTES);

        if (conv != VM_CHSET_CONVERT_SUCCESS &&
            conv != 0)
        {
            vm_free(u_chunk);
            vm_free(out_utf8);
            vm_file_close(fh);

            return -4;
        }

        VMUINT written = 0;

        vm_file_write(fh, out_utf8, (VMUINT)strlen((VMSTR)out_utf8), &written);

        pos += take;
        remaining -= take;
    }

    vm_free(u_chunk);
    vm_free(out_utf8);

    vm_file_close(fh);

    return 0;
}

void save_history(void)
{
    create_auto_filename(outfile);
    save_history_to_utf8_chunked(outfile);
}
