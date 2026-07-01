// msgw32.h
/* ============================================================
 * MML Transposer Win32 Application - International Version
 * ------------------------------------------------------------
 * This header centralizes all user-visible strings and language-
 * dependent settings used by the Win32 edition of the MML
 * Transposer.
 *
 * The program currently supports two languages:
 *   - English
 *   - Japanese
 *
 * Additional languages can be added by extending the tables
 * in this header and rebuilding the program.
 *
 * Source code is written in ANSI C (C89) and built with
 * OpenWatcom V2.
 * ============================================================ */

#ifndef MSGW32_H
#define MSGW32_H

/* ------------------------------------------------------------
 * Language IDs
 * ------------------------------------------------------------ */
typedef enum {
    LANG_EN = 0,
    LANG_JP,
    LANG_MAX
} GUILang;

/* ------------------------------------------------------------
 * GUI Error Codes
 * ------------------------------------------------------------ */
typedef enum {
    GUI_ERR_OK = 0,

    /* LoadFile errors */
    GUI_ERR_LF_OPEN,
    GUI_ERR_LF_SIZE,
    GUI_ERR_LF_EMPTY,
    GUI_ERR_LF_TOO_LARGE,
    GUI_ERR_LF_READ,

    /* MML processing errors */
    GUI_ERR_MML_OCTAVE,
    GUI_ERR_MML_NULL,
    GUI_ERR_MML_EMPTY,
    GUI_ERR_MML_OUTBUF,
    GUI_ERR_MML_BAD_SHIFT,
    GUI_ERR_MML_BAD_MODE,
    GUI_ERR_MML_UNKNOWN,

    /* SaveFile errors */
    GUI_ERR_SF_OPEN,
    GUI_ERR_SF_WRITE,

    GUI_ERR_MAX
} GUIErrorCode;

/* ------------------------------------------------------------
 * Tooltip IDs (FULL VERSION)
 * ------------------------------------------------------------ */
typedef enum {
    TIP_FILENAME_LABEL = 0,
    TIP_FILENAME_TOOLTIP,
    TIP_SLIDER,
    TIP_KEY_LABEL,
    TIP_KEY_EDIT,
    TIP_KEY_SPIN,
    TIP_SAVE_AUTO,
    TIP_SAVE_AS,
    TIP_OPTION_LABEL,
    TIP_REL,
    TIP_ABS,
    TIP_FORMAT,
    TIP_DSHIFT,
    TIP_AUTOSAVE,
    TIP_INVALID_TYPE,
    TIP_DROP_ERROR,

    TIP_MAX
} GUITooltipID;

/* ------------------------------------------------------------
 * Language Table Structure
 * ------------------------------------------------------------ */
typedef struct {
    const char* font_name;
    int         font_size;
    BYTE        charset;

    const char* gui_msg[GUI_ERR_MAX];
    const char* tip[TIP_MAX];

    const char* ui_label_key;
    const char* ui_label_option;
    const char* ui_label_quick;
    const char* ui_label_save;
    const char* ui_label_auto;

} LangTable;

/* ------------------------------------------------------------
 * Language Table (English / Japanese)
 * ------------------------------------------------------------ */
static const LangTable g_lang_table[LANG_MAX] = {

/* ============================================================
 * ENGLISH
 * ============================================================ */
{
    "MS Shell Dlg 2",
    9,
    ANSI_CHARSET,

    /* GUI error messages */
    {
        NULL,
        "Cannot open input file.",
        "Failed to get file size.",
        "Input file is empty.",
        "Input file is too large (%lu bytes).\nMaximum allowed is %d bytes.",
        "Failed to read file.",
        "Octave limit exceeded:\nchannel '%c', line %d (value: o%d).",
        "Input text is NULL.",
        "Input text is empty.",
        "Output buffer is too small.",
        "Shift amount is out of range (-12 to +12).",
        "Invalid mode.",
        "Unknown MML error (code: %d).",
        "Cannot open output file.",
        "Failed to write file."
    },

    /* Tooltip messages */
    {
        "No file selected.",                 /* TIP_FILENAME_LABEL */
        "Please select a file.",             /* TIP_FILENAME_TOOLTIP*/
        "Shift: %s",                         /* TIP_SLIDER */
        "Key height",                        /* TIP_KEY_LABEL */
        "Current key height",                /* TIP_KEY_EDIT */
        "Adjust key height",                 /* TIP_KEY_SPIN */
        "Save now (auto name)",              /* TIP_SAVE_AUTO */
        "Save as...",                        /* TIP_SAVE_AS */
        "Output options",                    /* TIP_OPTION_LABEL */
        "Relative octave (< >)",             /* TIP_REL */
        "Absolute octave (oX)",              /* TIP_ABS */
        "Format output",                     /* TIP_FORMAT */
        "D-channel noise shift",             /* TIP_DSHIFT */
        "Auto-save on drop",                 /* TIP_AUTOSAVE */
        "ERROR: Cannot OPEN. Invalid File Type.", /* TIP_INVALID_TYPE */
        "ERROR: Cannot OPEN dropped file."   /* TIP_DROP_ERROR */
    },

    /* UI labels */
    "Key:",
    "Opt:",
    "Quick",
    "Save",
    "Auto"
},

/* ============================================================
 * JAPANESE
 * ============================================================ */
{
    "ＭＳ Ｐゴシック",
    9,
    SHIFTJIS_CHARSET,

    /* GUI error messages */
    {
        NULL,
        "入力ファイルを開けません。",
        "ファイルサイズを取得できません。",
        "入力ファイルが空です。",
        "入力ファイルが大きすぎます (%lu バイト)。\n最大 %d バイトです。",
        "ファイル読み込みに失敗しました。",
        "移調によりオクターブ限界を突破しました:\nチャンネル '%c', %d 行目 (計算値: o%d)。",
        "入力テキストが NULL です。",
        "入力テキストが空です。",
        "出力バッファサイズが不足しています。",
        "移調量が範囲外です（-12～+12）。",
        "モードが無効です。",
        "不明な MML エラー (コード: %d)。",
        "保存ファイルを開けません。",
        "ファイル書き込みに失敗しました。"
    },

    /* Tooltip messages */
    {
        "ファイルが選択されていません。",     /* TIP_FILENAME_LABEL */
        "ファイルを選択してください。",       /* TIP_FILENAME_TOOLTIP */
        "移調量: %s",                         /* TIP_SLIDER */
        "キーの高さ",                         /* TIP_KEY_LABEL */
        "現在のキーの高さ",                   /* TIP_KEY_EDIT */
        "キーの高さを調整します",             /* TIP_KEY_SPIN */
        "すぐ保存（自動ファイル名）",         /* TIP_SAVE_AUTO */
        "名前を付けて保存",                   /* TIP_SAVE_AS */
        "出力オプション",                     /* TIP_OPTION_LABEL */
        "相対オクターブ（< >）",              /* TIP_REL */
        "絶対オクターブ（oX）",               /* TIP_ABS */
        "整形して出力",                       /* TIP_FORMAT */
        "D-ch ノイズシフト",                  /* TIP_DSHIFT */
        "ドロップ時に自動保存",               /* TIP_AUTOSAVE */
        "エラー: ファイル形式が無効です。",   /* TIP_INVALID_TYPE */
        "エラー: ドロップファイル異常あり。"  /* TIP_DROP_ERROR */
    },

    /* UI labels */
    "キー:",
    "設定:",
    "クイック",
    "保存",
    "自動"
}

};

#endif /* MSGW32_H */
