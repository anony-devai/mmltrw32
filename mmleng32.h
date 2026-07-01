/* ============================================================
 * mmleng32.h  (32-bit Common MML Transposer Engine Interface)
 *
 * Header for the 32-bit implementation of the shared MML Transposer Engine,
 * used by:
 *      - mmltrw32  (Win32 GUI front-end)
 *      - mmltrc32  (Win32 console front-end)
 *
 * Provides:
 *      - Buffer limit definitions
 *      - Error code definitions
 *      - Mode bit layout (3-bit base + D-channel extension)
 *      - Token and error information structures
 *      - Public API entry point (mml_process)
 *
 * Fully synchronized with the 16-bit engine design.
 * The 32-bit engine cannot run in 16-bit environments.
 * ============================================================ */

#ifndef MMLENG32_H_INCLUDED
#define MMLENG32_H_INCLUDED

/* ------------------------------------------------------------
 * Buffer limits (MAX_TEXT fully restored)
 * ------------------------------------------------------------ */
#define MAX_TOKENS   16384
#define MAX_RAW_LEN  256    /* Keep existing 256-byte RAW buffer */
#define MAX_OUT      65536
#define MAX_TEXT     65536  /* Required by CUI side, fully restored */

/* Maximum number of channels: 28
 * (Uppercase A-Z = 26) + (lowercase a, b = 2)
 */
#define MAX_CHANNELS 28

/* ------------------------------------------------------------
 * Error codes (shared allocation, kept exactly as before)
 * ------------------------------------------------------------ */
#define MML_ERR_NULL_INPUT             -1  /* Input/output pointer is NULL */
#define MML_ERR_EMPTY_INPUT            -2  /* Input string is empty */
#define MML_ERR_OUTBUF                 -3  /* Output buffer size invalid (<= 0) */
#define MML_ERR_BAD_SHIFT              -4  /* Shift value out of range (-12?`12 only) */
#define MML_ERR_BAD_MODE               -5  /* Mode value out of range */
#define MML_ERR_PARSE                  -6  /* Parse error (used by 32-bit token version) */
#define MML_ERR_OCTAVE_OUT_OF_RANGE   -10  /* Transposition exceeded octave limit of sound source */

/* ------------------------------------------------------------
 * Mode bit layout
 * ------------------------------------------------------------
 * Base 3-bit mode:
 *   bit 2 (4) : FMT  ?c formatting on/off
 *   bit 1 (2) : REL  ?c relative notation (< >)
 *   bit 0 (1) : ABS  ?c absolute notation (oX)
 *
 * 4th bit is used as an independent D-channel (noise) control.
 * ------------------------------------------------------------ */
#define MODE_PURE        0   /* 0000 : Pure (no formatting, reassign oX / <>) */
#define MODE_PURE_ABS    1   /* 0001 : Pure + Abs (no formatting) */
#define MODE_PURE_REL    2   /* 0010 : Pure + Rel (no formatting) */
#define MODE_FMT         4   /* 0100 : FMT (formatting, reassign oX / <>) */
#define MODE_FMT_ABS     5   /* 0101 : FMT + Abs (with formatting) */
#define MODE_FMT_REL     6   /* 0110 : FMT + Rel (with formatting) */

/* Extended flag: D-channel (noise) note shift enable flag (-d option)
 * This flag can be OR-ed with any base mode (0?`6).
 */
#define MODE_NOISE_SHIFT 8   /* 1000 : Shift notes only on D-channel, keep o0 fixed */

/* ------------------------------------------------------------
 * Structures and enums
 * ------------------------------------------------------------ */

/* Token type (kept exactly as in the original enum) */
typedef enum {
    TK_NONE = 0,
    TK_NOTE,
    TK_REST,
    TK_RAW
} MMLTokenType;

/* Detailed error information (for octave limit violations, etc.) */
typedef struct {
    int  error_code;       /* Error code (MML_ERR_xxx) */
    char channel_char;     /* Channel character where error occurred (A-Z, a, b) */
    int  line_number;      /* MML line number where error occurred (1-based) */
    int  calculated_value; /* Invalid octave value calculated when limit was exceeded */
} MMLErrorInfo;

/* Token information (original members preserved, with safety-oriented comments) */
typedef struct {
    MMLTokenType type;
    int  octave;                 /* Absolute octave for NOTE/REST */
    int  note;                   /* 0?`11, or -1 for REST */
    char length[16];             /* Length string ("4", "8.", "16", etc.; 16 bytes reserved) */
    char raw[MAX_RAW_LEN];       /* Raw text (internal RAW) */
    int  is_literal_o0;          /* Whether to keep o0 literally as-is */
    int  is_raw_ox;              /* Whether to treat oX as RAW */
    int  is_comment;             /* Whether this token came from a comment */
} Token;

/* ------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------ */
#ifdef __cplusplus
extern "C" {
#endif

/**
 * Main MML batch conversion entry point.
 *
 * Parameters:
 *   in_text   - Input MML text (null-terminated)
 *   shift     - Semitone shift (-12?`12)
 *   mode      - Mode bitmask:
 *                 base: 0?`6
 *                 plus: +8 when D-channel noise shift is enabled
 *   outbuf    - Output buffer
 *   outsize   - Size of output buffer in bytes
 *   err_info  - Optional pointer to receive detailed error info
 *
 * Returns:
 *   >= 0 : Length of output text (in bytes, excluding terminator)
 *   <  0 : Error code (MML_ERR_xxx)
 */
int mml_process(const char* in_text, int shift, int mode,
                char* outbuf, int outsize, MMLErrorInfo* err_info);

#ifdef __cplusplus
}
#endif

#endif /* MMLENG32_H_INCLUDED */
