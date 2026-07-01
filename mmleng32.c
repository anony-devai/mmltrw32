/* ============================================================
 * mmleng32.c  (32-bit MML Transposer Engine)
 *
 * This is the 32-bit implementation of the Common MML Transposer Engine,
 * shared by:
 *      - mmltrw32  (Win32 GUI front-end)
 *      - mmltrc32  (Win32 console front-end)
 *
 * Fully synchronized with the 16-bit engine design:
 *      - Same public API (mml_process)
 *      - Same mode bit layout (FMT / REL / ABS + D-channel extension)
 *      - Same error code definitions
 *      - Same MAX_CHANNELS = 28
 *
 * Note:
 *   The 32-bit engine cannot be used in 16-bit environments.
 *   Only the 16-bit engine is cross-version compatible
 *   (DOS / Win16 / Win32 callers).
 *
 * C89 compliant / OpenWatcom V2 & BCC compatible.
 * ============================================================ */

#include "mmleng32.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Must match mmleng32.h (MAX_CHANNELS = 28) */
#define MAX_LINES       4096

/* Note names for 12 semitones */
static const char* NOTE_NAMES[12] = {
    "c","c+","d","d+","e","f","f+","g","g+","a","a+","b"
};

/* Internal function prototypes (C89 style) */
static int  mml_parse(const char* text, Token* tokens, int max_tokens, int* p_line_count);
static int  mml_transpose(Token* tokens, int count, int shift, int mode, MMLErrorInfo* err_info);
static int  mml_render_common(Token* tokens, int count, char* outbuf, int outsize, int mode);

static void mml_remove_blank_lines(char* buf);
static void mml_trim_line_head_spaces(char* buf);
static void mml_insert_section_breaks(char* buf);
static void mml_compress_spaces(char* buf);

static int  note_to_num(const char* s, int* consumed);
static char get_normalized_char(const char** pp);
static void append_comment_char(Token* tokens, int* p_count, char ch);

static void mml_mark_comment_lines(const char* buf, int* comment_flags);
static void init_comment_flags(int* flags, int size);
static int  is_line_head_channel(Token* tokens, int index, int count, int* p_ch_index, char* p_ch_char);

/* Public API */
int mml_process(const char* in_text, int shift, int mode,
                char* outbuf, int outsize, MMLErrorInfo* err_info)
{
    static Token tokens[MAX_TOKENS]; /* Not thread-safe by design */
    int count;
    int outlen;
    int trans_res;
    int dummy_line_count = 0;
    int base_mode;

    memset(tokens, 0, sizeof(tokens));
    if (err_info) memset(err_info, 0, sizeof(MMLErrorInfo));

    /* Basic safety guards */
    if (!in_text || !outbuf) {
        if (err_info) err_info->error_code = MML_ERR_NULL_INPUT;
        return MML_ERR_NULL_INPUT;
    }
    if (in_text[0] == '\0') {
        if (err_info) err_info->error_code = MML_ERR_EMPTY_INPUT;
        return MML_ERR_EMPTY_INPUT;
    }
    if (outsize <= 0) {
        if (err_info) err_info->error_code = MML_ERR_OUTBUF;
        return MML_ERR_OUTBUF;
    }
    if (shift < -12 || shift > 12) {
        if (err_info) err_info->error_code = MML_ERR_BAD_SHIFT;
        return MML_ERR_BAD_SHIFT;
    }

    /* Mode check (lower 3 bits) */
    base_mode = mode & 7;
    if (base_mode < 0 || base_mode > 6) {
        if (err_info) err_info->error_code = MML_ERR_BAD_MODE;
        return MML_ERR_BAD_MODE;
    }

    /* 1. Parse */
    count = mml_parse(in_text, tokens, MAX_TOKENS, &dummy_line_count);

    /* 2. Transpose + Smart Rewrite (2-pass) */
    trans_res = mml_transpose(tokens, count, shift, mode, err_info);
    if (trans_res != 0) return trans_res;

    /* 3. Render */
    outlen = mml_render_common(tokens, count, outbuf, outsize, mode);
    return outlen;
}

/* Detect channel markers at line head */
static int is_line_head_channel(Token* tokens, int index, int count,
                                int* p_ch_index, char* p_ch_char)
{
    char c;
    int at_line_head;

    if (index < 0 || index >= count) return 0;
    if (tokens[index].type != TK_RAW || strlen(tokens[index].raw) != 1) return 0;

    c = tokens[index].raw[0];

    if (!((c >= 'A' && c <= 'Z') || c == 'a' || c == 'b')) return 0;

    at_line_head = 0;
    if (index == 0) at_line_head = 1;
    else if (tokens[index - 1].type == TK_RAW &&
             strchr(tokens[index - 1].raw, '\n') != NULL)
        at_line_head = 1;

    if (!at_line_head) return 0;

    if (index + 1 < count &&
        tokens[index + 1].type == TK_RAW &&
        strcmp(tokens[index + 1].raw, " ") == 0)
    {
        if (p_ch_index) {
            if (c >= 'A' && c <= 'Z') *p_ch_index = c - 'A';
            else *p_ch_index = c - 'a' + 26;
        }
        if (p_ch_char) *p_ch_char = c;
        return 1;
    }
    return 0;
}

/* Transpose and Smart Rewrite (first pass) */
static int mml_transpose(Token* tokens, int count, int shift,
                         int mode, MMLErrorInfo* err_info)
{
    int i, ch;
    int cur_oct = 4;
    int current_channel = -1;
    char current_ch_char = '?';
    int current_line = 1;

    int last_oct_global = -999;
    int last_oct_ch[MAX_CHANNELS];
    int first_note_ch[MAX_CHANNELS];
    int first_note_global = 0;

    int is_smart_rewrite = ((mode & 3) == 0);
    int is_noise_shift   = ((mode & MODE_NOISE_SHIFT) != 0);

    for (i = 0; i < MAX_CHANNELS; i++) {
        last_oct_ch[i] = -999;
        first_note_ch[i] = 0;
    }

    /* First pass: determine absolute octaves */
    for (i = 0; i < count; i++) {
        Token* tk = &tokens[i];
        int ch_index;

        /* Update line number */
        if (tk->type == TK_RAW) {
            const char* rc = tk->raw;
            while (*rc) {
                if (*rc == '\n') current_line++;
                rc++;
            }
        }

        if (is_line_head_channel(tokens, i, count, &ch_index, &current_ch_char))
            current_channel = ch_index;

        /* RAW token handling */
        if (tk->type == TK_RAW) {

            if (tk->is_comment) continue;

            /* Handle oX commands safely */
            if (tk->raw[0] == 'o') {
                int n = 0, has_digit = 0;
                const char* p2 = tk->raw + 1;

                while (*p2 >= '0' && *p2 <= '9') {
                    n = n * 10 + (*p2 - '0');
                    p2++;
                    has_digit = 1;
                }

                /* D-channel oX */
                if (current_channel == 3) {
                    cur_oct = 0;

                    if (has_digit) {

                        if (first_note_ch[3] == 0) {
                            /* First oX on D-ch ?? treat as o0 regardless of digit */
                            sprintf(tk->raw, "o0");
                            tk->is_literal_o0 = 1;
                            tk->is_raw_ox     = 1;
                            first_note_ch[3]  = 1;

                        } else {
                            /* From the second time on, silently remove all oX */
                            tk->raw[0]        = '\0';
                            tk->is_literal_o0 = 0;
                            tk->is_raw_ox     = 0;
                        }

                    } else {
                        /* 'o' without digit ?? invalid, remove */
                        tk->raw[0]        = '\0';
                        tk->is_literal_o0 = 0;
                        tk->is_raw_ox     = 0;
                    }

                    continue;
                }

                /* Normal channels */
                if (has_digit) {
                    if (n < 0) n = 0;
                    if (n > 8) n = 8;
                    cur_oct = n;
                    tk->is_raw_ox = 1;
                }

                if (!is_smart_rewrite) tk->raw[0] = '\0';
                continue;
            }

            /* Handle < and > octave shifts safely */
            if (tk->raw[0] == '<' || tk->raw[0] == '>') {
                int idx = 0;

                while (tk->raw[idx] == '<') { cur_oct--; idx++; }
                while (tk->raw[idx] == '>') { cur_oct++; idx++; }

                /* Clip cur_oct to safe range */
                if (cur_oct < 0) cur_oct = 0;
                if (cur_oct > 8) cur_oct = 8;

                if (current_channel == 3) {
                    cur_oct = 0;
                    tk->raw[0] = '\0';
                    continue;
                }

                tk->raw[0] = '\0';
                continue;
            }

            continue;
        }

        /* TK_NOTE: determine absolute octave (first pass) */
        if (tk->type == TK_NOTE) {

            /* D-channel (always octave 0) */
            if (current_channel == 3) {
                cur_oct = 0;
                tk->octave = 0;

                if (is_noise_shift) {
                    int num = tk->note + shift;
                    int dst_note = num % 12;
                    if (dst_note < 0) dst_note += 12;
                    tk->note = dst_note;
                }
            }
            else {
                /* Normal channels transpose (safe) */
                int num;

                if (cur_oct < 0) cur_oct = 0;
                if (cur_oct > 8) cur_oct = 8;

                num = cur_oct * 12 + tk->note + shift;

                {
                    int dst_oct = num / 12;
                    int dst_note = num % 12;

                    if (dst_note < 0) {
                        dst_note += 12;
                        dst_oct -= 1;
                    }

                    if (dst_oct < 0 || dst_oct > 8) {
                        if (err_info) {
                            err_info->error_code = MML_ERR_OCTAVE_OUT_OF_RANGE;
                            err_info->channel_char = (current_channel >= 0) ? current_ch_char : ' ';
                            err_info->line_number = current_line;
                            err_info->calculated_value = dst_oct;
                        }
                        return MML_ERR_OCTAVE_OUT_OF_RANGE;
                    }

                    tk->octave = dst_oct;
                    tk->note   = dst_note;
                }
            }

            /* First-note flags (D-channel o0 completion rules) */
            if (current_channel >= 0 && current_channel < MAX_CHANNELS) {

                if (current_channel == 3) {
                    if (tk->is_literal_o0) {
                        first_note_ch[3] = 1;
                    }
                    else if (!first_note_ch[3]) {
                        first_note_ch[3] = 2;
                    }

                    last_oct_ch[3] = 0;
                }
                else {
                    if (!first_note_ch[current_channel]) {
                        first_note_ch[current_channel] = 1;
                        last_oct_ch[current_channel] = tk->octave;
                    }
                }
            }
            else {
                if (!first_note_global) {
                    first_note_global = 1;
                    last_oct_global = tk->octave;
                }
            }
        }
    }

    /* Second pass: Smart Rewrite for < > and oX reconstruction */
    if (is_smart_rewrite) {
        int prev_oct_global = last_oct_global;
        int prev_oct_ch[MAX_CHANNELS];

        for (ch = 0; ch < MAX_CHANNELS; ch++) {
            prev_oct_ch[ch] = last_oct_ch[ch];
            if (prev_oct_ch[ch] == -999) prev_oct_ch[ch] = 4;
        }
        if (prev_oct_global == -999) prev_oct_global = 4;

        current_channel = -1;

        for (i = 0; i < count; i++) {
            Token* tk = &tokens[i];
            int ch_index;
            int* p_prev;

            if (is_line_head_channel(tokens, i, count, &ch_index, NULL))
                current_channel = ch_index;

            p_prev = (current_channel >= 0 && current_channel < MAX_CHANNELS)
                     ? &prev_oct_ch[current_channel]
                     : &prev_oct_global;

            /* Keep comments as-is */
            if (tk->type == TK_RAW && tk->is_comment) continue;

            /* Rebuild raw oX commands (Smart Rewrite) */
            if (tk->type == TK_RAW && tk->is_raw_ox) {
                int base_oct = -999;
                int j;
                int ch_self = current_channel;

                if (current_channel == 3 && !is_noise_shift)
                    continue;

                for (j = i + 1; j < count; j++) {
                    int ch2;
                    if (is_line_head_channel(tokens, j, count, &ch2, NULL)) {
                        if (ch_self != -1 && ch2 != ch_self) break;
                    }
                    if (tokens[j].type == TK_NOTE) {
                        base_oct = tokens[j].octave;
                        break;
                    }
                }

                if (base_oct != -999) {
                    sprintf(tk->raw, "o%d", base_oct);
                    *p_prev = base_oct;
                }
                continue;
            }

            /* Rebuild < and > octave shifts for notes */
            if (tk->type == TK_NOTE) {
                int cur = tk->octave;
                int diff = cur - *p_prev;
                int j2;

                if (current_channel == 3 && !is_noise_shift) {
                    *p_prev = cur;
                    continue;
                }

                tk->raw[0] = '\0';

                if (diff > 0) {
                    if (diff >= MAX_RAW_LEN - 1) diff = MAX_RAW_LEN - 2;
                    for (j2 = 0; j2 < diff; j2++) tk->raw[j2] = '>';
                    tk->raw[diff] = '\0';
                }
                else if (diff < 0) {
                    diff = -diff;
                    if (diff >= MAX_RAW_LEN - 1) diff = MAX_RAW_LEN - 2;
                    for (j2 = 0; j2 < diff; j2++) tk->raw[j2] = '<';
                    tk->raw[diff] = '\0';
                }

                *p_prev = cur;
            }
        }
    }

    return 0;
}

/* Parser with header protection and full comment preservation */
static int mml_parse(const char* text, Token* tokens, int max_tokens, int* p_line_count)
{
    int count = 0;
    const char* p = text;
    int current_line = 1;

    while (*p != '\0' && count < max_tokens) {

        /* Parse header lines starting with '#' */
        if (*p == '#') {
            Token* tk = &tokens[count];
            int len = 0;
            const char* q = p;

            memset(tk, 0, sizeof(Token));
            tk->type = TK_RAW;

            while (*q != '\0' && len < MAX_RAW_LEN - 1) {
                char ch = get_normalized_char(&q);
                tk->raw[len++] = ch;
                if (ch == '\n') { current_line++; break; }
            }
            tk->raw[len] = '\0';
            p = q;
            count++;
            continue;
        }

        /* Parse block comments safely (no q[-2] access) */
        if (p[0] == '/' && p[1] == '*') {
            const char* q = p;
            char prev = 0, prev2 = 0;

            while (*q != '\0' && count < max_tokens) {
                prev2 = prev;
                prev  = get_normalized_char(&q);

                if (prev == '\n') current_line++;
                append_comment_char(tokens, &count, prev);

                if (prev2 == '*' && prev == '/') break;
            }
            p = q;
            continue;
        }

        /* Parse line comments (';' or '/' like //) */
        if (*p == ';' || *p == '/') {
            const char* q = p;

            while (*q != '\0' && count < max_tokens) {
                char ch = get_normalized_char(&q);
                append_comment_char(tokens, &count, ch);
                if (ch == '\n') { current_line++; break; }
            }
            p = q;
            continue;
        }

        /* Parse whitespace and newlines */
        if (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
            Token* tk = &tokens[count];
            int len = 0;

            memset(tk, 0, sizeof(Token));
            tk->type = TK_RAW;

            while ((*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') &&
                   len < MAX_RAW_LEN - 1)
            {
                if (*p == '\r') {
                    p += (p[1] == '\n') ? 2 : 1;
                    tk->raw[len++] = '\n';
                    current_line++;
                }
                else if (*p == '\n') {
                    p++;
                    tk->raw[len++] = '\n';
                    current_line++;
                }
                else {
                    tk->raw[len++] = *p++;
                }
            }
            tk->raw[len] = '\0';
            count++;
            continue;
        }

        /* Parse octave commands: oX, <, > */
        if (*p == '<' || *p == '>' || *p == 'o') {
            Token* tk = &tokens[count];
            int len = 0, n = 0, has_digit = 0;

            memset(tk, 0, sizeof(Token));
            tk->type = TK_RAW;

            if (*p == 'o') {
                tk->raw[len++] = *p++;
                while (*p >= '0' && *p <= '9' && len < MAX_RAW_LEN - 1) {
                    tk->raw[len++] = *p;
                    n = n * 10 + (*p - '0');
                    p++;
                    has_digit = 1;
                }
                tk->raw[len] = '\0';

                if (has_digit) {
                    if (n == 0) tk->is_literal_o0 = 1;
                    else        tk->is_raw_ox = 1;
                }
            }
            else {
                tk->raw[len++] = *p++;
                tk->raw[len] = '\0';
            }

            count++;
            continue;
        }

        /* Parse tone commands '@' and '@@' */
        if (*p == '@') {
            Token* tk = &tokens[count];
            int len = 0;

            memset(tk, 0, sizeof(Token));
            tk->type = TK_RAW;

            tk->raw[len++] = *p++;
            if (*p == '@' && len < MAX_RAW_LEN - 1)
                tk->raw[len++] = *p++;

            while (((*p >= 'a' && *p <= 'z') ||
                    (*p >= 'A' && *p <= 'Z')) &&
                   len < MAX_RAW_LEN - 1)
            {
                tk->raw[len++] = *p++;
            }

            while ((*p >= '0' && *p <= '9') && len < MAX_RAW_LEN - 1) {
                tk->raw[len++] = *p++;
            }

            tk->raw[len] = '\0';
            count++;
            continue;
        }

        /* Parse bare numbers */
        if (*p >= '0' && *p <= '9') {
            Token* tk = &tokens[count];
            int len = 0;

            memset(tk, 0, sizeof(Token));
            tk->type = TK_RAW;

            while (*p >= '0' && *p <= '9' && len < MAX_RAW_LEN - 1)
                tk->raw[len++] = *p++;

            tk->raw[len] = '\0';
            count++;
            continue;
        }

        /* Parse tie '&' and '^' tokens */
        if (*p == '&' || *p == '^') {
            Token* tk = &tokens[count];

            memset(tk, 0, sizeof(Token));
            tk->type = TK_RAW;
            tk->raw[0] = *p++;
            tk->raw[1] = '\0';

            count++;
            continue;
        }

        /* Parse notes and rests */
        {
            int consumed = 0;
            int num = note_to_num(p, &consumed);

            /* Rest 'r' */
            if (*p == 'r') {
                Token* tk = &tokens[count];
                int len = 0;

                memset(tk, 0, sizeof(Token));
                tk->type = TK_REST;
                tk->note = -1;

                tk->raw[len++] = *p++;
                while (*p >= '0' && *p <= '9' && len < MAX_RAW_LEN - 1)
                    tk->raw[len++] = *p++;

                tk->raw[len] = '\0';
                count++;
                continue;
            }

            /* Note */
            if (num >= 0) {
                Token* tk = &tokens[count];
                int len = 0;

                memset(tk, 0, sizeof(Token));
                tk->type = TK_NOTE;
                tk->note = num;

                p += consumed;

                while (*p >= '0' && *p <= '9' &&
                       len < (int)sizeof(tk->length) - 1)
                {
                    tk->length[len++] = *p++;
                }
                tk->length[len] = '\0';

                count++;
                continue;
            }
        }

        /* Handle undefined characters */
        {
            Token* tk = &tokens[count];

            memset(tk, 0, sizeof(Token));
            tk->type = TK_RAW;
            tk->raw[0] = *p++;
            tk->raw[1] = '\0';

            count++;
        }
    }

    *p_line_count = current_line;
    return count;
}

/* Rendering, comment handling, and formatting utilities */
static int mml_render_common(Token* tokens, int count, char* outbuf, int outsize, int mode)
{
    int i, j, outpos = 0;
    int current_channel = -1;

    int first_note_done[MAX_CHANNELS];
    int last_oct[MAX_CHANNELS];
    int global_first_note_done = 0;
    int global_last_oct = -999;

    int is_fmt = (mode & MODE_FMT) ? 1 : 0;
    int is_rel = (mode & MODE_PURE_REL) ? 1 : 0;
    int is_abs = (mode & MODE_PURE_ABS) ? 1 : 0;

    int is_pure_layout = (!is_fmt && (is_rel || is_abs));
    int is_smart_rewrite = ((mode & 3) == 0);

    memset(outbuf, 0, outsize);

    for (i = 0; i < MAX_CHANNELS; i++) {
        first_note_done[i] = 0;
        last_oct[i] = -999;
    }

    for (i = 0; i < count && outpos < outsize - 1; i++) {
        Token* tk = &tokens[i];
        int ch_index;

        if (is_line_head_channel(tokens, i, count, &ch_index, NULL))
            current_channel = ch_index;

        /* Output NOTE tokens */
        if (tk->type == TK_NOTE) {
            int oct = tk->octave;
            const char* name = NOTE_NAMES[tk->note];
            int need_prefix, ch;
            int* p_first;
            int* p_last;

            ch = current_channel;
            if (ch >= 0 && ch < MAX_CHANNELS) {
                p_first = &first_note_done[ch];
                p_last  = &last_oct[ch];
            } else {
                p_first = &global_first_note_done;
                p_last  = &global_last_oct;
            }

            /* D-channel only: o0 completion logic */
            if (ch == 3) {
                int already_has_o0 = 0;
                int k;

                for (k = 0; k < outpos - 1; k++) {
                    if (outbuf[k] == 'o' && outbuf[k + 1] == '0') {
                        already_has_o0 = 1;
                        break;
                    }
                }

                if (!already_has_o0 && !(*p_first)) {
                    if (outpos > 0 &&
                        outbuf[outpos - 1] != ' ' &&
                        outbuf[outpos - 1] != '\n')
                    {
                        if (outpos < outsize - 1)
                            outbuf[outpos++] = ' ';
                    }
                    if (outpos < outsize - 4) {
                        outbuf[outpos++] = 'o';
                        outbuf[outpos++] = '0';
                        outbuf[outpos++] = ' ';
                    }
                }

                *p_first = 1;
                *p_last  = 0;

                j = 0;
                while (name[j] && outpos < outsize - 1)
                    outbuf[outpos++] = name[j++];

                j = 0;
                while (tk->length[j] && outpos < outsize - 1)
                    outbuf[outpos++] = tk->length[j++];

                continue;
            }

            /* Normal channels (Smart Rewrite) */
            if (is_smart_rewrite) {
                j = 0;
                while (tk->raw[j] && outpos < outsize - 1)
                    outbuf[outpos++] = tk->raw[j++];

                j = 0;
                while (name[j] && outpos < outsize - 1)
                    outbuf[outpos++] = name[j++];

                j = 0;
                while (tk->length[j] && outpos < outsize - 1)
                    outbuf[outpos++] = tk->length[j++];

                continue;
            }

            /* Normal channels (absolute / relative octave) */
            need_prefix = 0;

            if (!(*p_first)) {
                need_prefix = 1;
                *p_first = 1;
                *p_last  = oct;
            }
            else {
                if (is_rel) {
                    int last = *p_last;

                    while (last < oct && outpos < outsize - 1) {
                        outbuf[outpos++] = '>';
                        last++;
                    }
                    while (last > oct && outpos < outsize - 1) {
                        outbuf[outpos++] = '<';
                        last--;
                    }
                    *p_last = oct;
                }
                else {
                    if (*p_last != oct) {
                        need_prefix = 1;
                        *p_last = oct;
                    }
                }
            }

            if (need_prefix) {
                char tmp[16];
                int k2;

                if (outpos > 0 &&
                    outbuf[outpos - 1] != ' ' &&
                    outbuf[outpos - 1] != '\n')
                {
                    if (outpos < outsize - 1)
                        outbuf[outpos++] = ' ';
                }

                if (outpos < outsize - 1)
                    outbuf[outpos++] = 'o';

                sprintf(tmp, "%d", oct);

                k2 = 0;
                while (tmp[k2] && outpos < outsize - 1)
                    outbuf[outpos++] = tmp[k2++];

                if (!is_pure_layout && outpos < outsize - 1)
                    outbuf[outpos++] = ' ';
            }

            j = 0;
            while (name[j] && outpos < outsize - 1)
                outbuf[outpos++] = name[j++];

            j = 0;
            while (tk->length[j] && outpos < outsize - 1)
                outbuf[outpos++] = tk->length[j++];

            continue;
        }

        /* Output REST and RAW tokens */
        if (tk->type == TK_REST || tk->type == TK_RAW) {
            j = 0;
            while (tk->raw[j] && outpos < outsize - 1)
                outbuf[outpos++] = tk->raw[j++];
            continue;
        }
    }

    outbuf[outpos] = '\0';

    /* Formatting mode (FMT) */
    if (is_fmt) {
        mml_remove_blank_lines(outbuf);
        mml_trim_line_head_spaces(outbuf);
        mml_insert_section_breaks(outbuf);
        mml_compress_spaces(outbuf);
    }

    /* Normalize CR to LF */
    for (i = 0; outbuf[i]; i++) {
        if (outbuf[i] == '\r') outbuf[i] = '\n';
    }

    return (int)strlen(outbuf);
}

/* Mark comment lines */
static void mml_mark_comment_lines(const char* buf, int* comment_flags)
{
    int in_block = 0, line = 0;
    const char* p = buf;

    while (*p) {
        if (line >= MAX_LINES) break;

        if (in_block) comment_flags[line] = 1;

        if (!in_block && p[0] == '/' && p[1] == '*') {
            in_block = 1;
            comment_flags[line] = 1;
        }
        if (in_block && p[0] == '*' && p[1] == '/') {
            in_block = 0;
            comment_flags[line] = 1;
        }

        if (!in_block && (p[0] == ';' || p[0] == '/'))
            comment_flags[line] = 1;

        while (*p && *p != '\n') p++;
        if (*p == '\n') { p++; line++; }
    }
}

static void init_comment_flags(int* flags, int size)
{
    int i;
    for (i = 0; i < size; i++) flags[i] = 0;
}

/* Convert note text to numeric value */
static int note_to_num(const char* s, int* consumed)
{
    char c = s[0];
    int base = -1;

    if (c == 'c') base = 0;
    else if (c == 'd') base = 2;
    else if (c == 'e') base = 4;
    else if (c == 'f') base = 5;
    else if (c == 'g') base = 7;
    else if (c == 'a') base = 9;
    else if (c == 'b') base = 11;
    else { *consumed = 0; return -1; }

    if (s[1] == '+' || s[1] == '#') { *consumed = 2; return base + 1; }
    if (s[1] == '-')               { *consumed = 2; return base - 1; }

    *consumed = 1;
    return base;
}

/* Normalize CR/LF sequences */
static char get_normalized_char(const char** pp)
{
    const char* p = *pp;
    char c = *p;

    if (c == '\r') {
        if (p[1] == '\n') { *pp += 2; return '\n'; }
        else              { *pp += 1; return '\n'; }
    }

    *pp += 1;
    return c;
}

/* Append a comment character safely */
static void append_comment_char(Token* tokens, int* p_count, char ch)
{
    Token* tk;
    int count = *p_count;
    int len;

    if (count == 0 ||
        tokens[count - 1].type != TK_RAW ||
        !tokens[count - 1].is_comment ||
        (int)strlen(tokens[count - 1].raw) >= MAX_RAW_LEN - 1)
    {
        if (count >= MAX_TOKENS) return;

        tk = &tokens[count];
        memset(tk, 0, sizeof(Token));
        tk->type = TK_RAW;
        tk->is_comment = 1;

        *p_count = count + 1;
    }

    tk = &tokens[*p_count - 1];
    len = (int)strlen(tk->raw);

    if (len < MAX_RAW_LEN - 1) {
        tk->raw[len] = ch;
        tk->raw[len + 1] = '\0';
    }
}

/* Formatting utilities */

/* Remove redundant blank lines */
static void mml_remove_blank_lines(char* buf)
{
    char* src = buf;
    char* dst = buf;
    int at_line_start = 1, line = 0;
    int after_comment = 0, blank_count = 0, current_section = 0;
    static int comment_flags[MAX_LINES];

    init_comment_flags(comment_flags, MAX_LINES);
    mml_mark_comment_lines(buf, comment_flags);

    while (*src) {
        if (line >= MAX_LINES) break;

        if (at_line_start && comment_flags[line]) {
            while (*src && *src != '\n') *dst++ = *src++;
            if (*src == '\n') { *dst++ = *src++; line++; }
            after_comment = 1;
            at_line_start = 1;
            blank_count = 0;
            continue;
        }

        if (at_line_start) {
            if (line == 0 && *src == '\n') { src++; line++; continue; }

            if (*src == '\n') {
                if (after_comment) {
                    *dst++ = *src++; line++;
                    at_line_start = 1;
                    after_comment = 0;
                    blank_count = 0;
                    continue;
                }

                if (current_section == 1 || current_section == 2) {
                    src++; line++; at_line_start = 1;
                    continue;
                }

                blank_count++;
                if (blank_count > 1) {
                    src++; line++; at_line_start = 1;
                    continue;
                }

                *dst++ = *src++; line++; at_line_start = 1;
                continue;
            }

            {
                const char* p2 = src;
                while (*p2 == ' ' || *p2 == '\t') p2++;

                if (((*p2 >= 'A' && *p2 <= 'Z') || *p2 == 'a' || *p2 == 'b') &&
                    p2[1] == ' ')
                    current_section = 1;
                else if (*p2 == '@')
                    current_section = 2;
                else
                    current_section = 0;
            }

            blank_count = 0;
            after_comment = 0;
        }

        *dst++ = *src++;
        if (dst[-1] == '\n') { at_line_start = 1; line++; }
        else at_line_start = 0;
    }

    *dst = '\0';
}

/* Trim leading spaces */
static void mml_trim_line_head_spaces(char* buf)
{
    static char out[MAX_OUT];
    int i = 0, j = 0, line = 0;
    static int comment_flags[MAX_LINES];

    init_comment_flags(comment_flags, MAX_LINES);
    mml_mark_comment_lines(buf, comment_flags);

    while (buf[i] && j < MAX_OUT - 1) {
        if (line >= MAX_LINES) break;

        if (comment_flags[line]) {
            while (buf[i] && j < MAX_OUT - 1) {
                out[j++] = buf[i++];
                if (out[j - 1] == '\n') { line++; break; }
            }
            continue;
        }

        if (i == 0 || buf[i - 1] == '\n') {
            while (buf[i] == ' ' || buf[i] == '\t' || buf[i] == '\r')
                i++;
        }

        out[j++] = buf[i++];
        if (out[j - 1] == '\n') line++;
    }

    out[j] = '\0';
    strcpy(buf, out);
}

/* Insert section breaks */
static void mml_insert_section_breaks(char* buf)
{
    static char out[MAX_OUT];
    int i = 0, j = 0, line = 0;
    static int comment_flags[MAX_LINES];

    int prev_section = 0, current_section = 0;
    int after_comment = 0;
    char prev_channel = 0, current_channel = 0;

    init_comment_flags(comment_flags, MAX_LINES);
    mml_mark_comment_lines(buf, comment_flags);

    while (buf[i] && j < MAX_OUT - 2) {
        if (line >= MAX_LINES) break;

        if (comment_flags[line]) {
            while (buf[i] != '\n' && buf[i] && j < MAX_OUT - 2)
                out[j++] = buf[i++];
            if (buf[i] == '\n' && j < MAX_OUT - 2)
                out[j++] = buf[i++];
            line++;
            after_comment = 1;
            continue;
        }

        current_section = 0;
        current_channel = 0;

        {
            int k2 = i;
            while (buf[k2] == ' ' || buf[k2] == '\t') k2++;

            if (((buf[k2] >= 'A' && buf[k2] <= 'Z') || buf[k2] == 'a' || buf[k2] == 'b') &&
                buf[k2 + 1] == ' ')
            {
                current_section = 1;
                current_channel = buf[k2];
            }
            else if (buf[k2] == '@') {
                current_section = 2;
            }
        }

        if (!after_comment &&
            prev_section != 0 &&
            current_section != 0)
        {
            if (prev_section != current_section ||
                (current_section == 1 && prev_channel != current_channel))
            {
                out[j++] = '\n';
            }
        }

        while (buf[i] != '\n' && buf[i] && j < MAX_OUT - 2)
            out[j++] = buf[i++];

        if (buf[i] == '\n' && j < MAX_OUT - 2)
            out[j++] = buf[i++];

        prev_section = current_section;
        prev_channel = current_channel;
        after_comment = 0;
        line++;
    }

    out[j] = '\0';
    strcpy(buf, out);
}

/* Compress spaces */
static void mml_compress_spaces(char* buf)
{
    int r = 0, w = 0, space = 0, line = 0;
    static int comment_flags[MAX_LINES];

    init_comment_flags(comment_flags, MAX_LINES);
    mml_mark_comment_lines(buf, comment_flags);

    while (buf[r]) {
        if (line >= MAX_LINES) break;

        if (comment_flags[line]) {
            while (buf[r]) {
                buf[w++] = buf[r++];
                if (buf[r - 1] == '\n') { line++; break; }
            }
            continue;
        }

        if (buf[r] == ' ') {
            if (!space) {
                buf[w++] = ' ';
                space = 1;
            }
        }
        else {
            buf[w++] = buf[r];
            space = 0;
        }

        if (buf[r] == '\n') line++;
        r++;
    }

    buf[w] = '\0';
}
