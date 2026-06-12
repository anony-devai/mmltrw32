/* ============================================================
 * mmleng32.h  (C89/BCC55/DOS 8.3 準拠 完全クリーン版)
 * ============================================================ */

#ifndef MMLENG32_H_INCLUDED
#define MMLENG32_H_INCLUDED

/* ------------------------------------------------------------
 * 各種バッファ制限マクロ (MAX_TEXT を完全に復活)
 * ------------------------------------------------------------ */
#define MAX_TOKENS   16384
#define MAX_RAW_LEN  256    /* 既存の 256 バイト仕様を維持 */
#define MAX_OUT      65536
#define MAX_TEXT     65536  /* CUI側で必要となるため完全復活 */

/* 最大チャンネル数: 28 
 * (大文字 A-Z = 26) + (小文字 a, b = 2) 
 */
#define MAX_CHANNELS 28

/* ------------------------------------------------------------
 * エラーコード定義（共通の割り当てを完全維持）
 * ------------------------------------------------------------ */
#define MML_ERR_NULL_INPUT             -1  /* 入力/出力ポインタがNULL */
#define MML_ERR_EMPTY_INPUT            -2  /* 入力文字列が空 */
#define MML_ERR_OUTBUF                 -3  /* 出力バッファサイズが不正(0以下) */
#define MML_ERR_BAD_SHIFT              -4  /* 移調幅が範囲外 (-12?12 以外) */
#define MML_ERR_BAD_MODE               -5  /* モード指定が範囲外 */
#define MML_ERR_PARSE                  -6  /* パースエラー（32bit Token版で使用） */
#define MML_ERR_OCTAVE_OUT_OF_RANGE  -10  /* 移調により各音源のオクターブ限界を突破した */

/* ------------------------------------------------------------
 * モード体系（基本3bit体系に、Dch独立制御ビットを4ビット目に拡張）
 * ------------------------------------------------------------ */
#define MODE_PURE        0   /* 0000 : Pure（整形なし・oX/<> 振り直し） */
#define MODE_PURE_ABS    1   /* 0001 : Pure + Abs（整形なし） */
#define MODE_PURE_REL    2   /* 0010 : Pure + Rel（整形なし） */
#define MODE_FMT         4   /* 0100 : FMT（整形あり・oX/<> 振り直し） */
#define MODE_FMT_ABS     5   /* 0101 : FMT + Abs（整形あり） */
#define MODE_FMT_REL     6   /* 0110 : FMT + Rel（整形あり） */

/* 【拡張フラグ】Dチャンネル（ノイズ）音符シフト有効化フラグ (-d オプション用) 
 * 既存の各モード（0?6）に対してビット論理和（| MODE_NOISE_SHIFT）で併用可能。
 */
#define MODE_NOISE_SHIFT 8   /* 1000 : Dchのみ音符をシフトし、o0固定とする */

/* ------------------------------------------------------------
 * 構造体および列挙型定義
 * ------------------------------------------------------------ */

/* トークン種別（既存の列挙型を完全維持） */
typedef enum {
    TK_NONE = 0,
    TK_NOTE,
    TK_REST,
    TK_RAW
} MMLTokenType;

/* エラー詳細情報（新設：限界突破時の詳細用） */
typedef struct {
    int  error_code;       /* エラーコード (MML_ERR_xxx) */
    char channel_char;     /* エラーが発生したチャンネル文字 (A-Z, a, b) */
    int  line_number;      /* エラーが発生したMMLの行番号 (1から開始) */
    int  calculated_value; /* 限界突破した際の、計算上の不正なオクターブ値 */
} MMLErrorInfo;

/* トークン情報（既存のメンバを完全維持＋安全性のための拡張） */
typedef struct {
    MMLTokenType type;
    int  octave;            /* NOTE/REST 用の絶対オクターブ */
    int  note;              /* 0?11, REST のときは -1 */
    char length[16];        /* 音長文字列（"4", "8.", "16" など。余裕を見て16面確保） */
    char raw[MAX_RAW_LEN];  /* 生テキスト（内部 RAW） */
    int  is_literal_o0;     /* o0 をそのまま残すかどうかのフラグ */
    int  is_raw_ox;         /* oX を RAW として扱うかどうかのフラグ */
    int  is_comment;        /* コメント由来トークンかどうかのフラグ */
} Token;

/* ------------------------------------------------------------
 * 外部公開関数 API
 * ------------------------------------------------------------ */
#ifdef __cplusplus
extern "C" {
#endif

/**
 * MML一括変換メインエントリー
 * (第3引数 mode に対して、0?6 に加え、オプション時に +8 された値を受け入れます)
 */
int mml_process(const char* in_text, int shift, int mode,
                char* outbuf, int outsize, MMLErrorInfo* err_info);

#ifdef __cplusplus
}
#endif

#endif /* MMLENG32_H_INCLUDED */
