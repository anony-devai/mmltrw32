// mmltrw32.c - MML Transposer Win32 GUI - C89 / OpenWatcom V2
// This tool supports *.mml files only.

#include <windows.h>
#include <commdlg.h>
#include <commctrl.h>
#include <shellapi.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "resource.h"
#include "mmleng32.h"
#include "msgw32.h"

#define WM_MENU_LOST (WM_USER + 100)

/* Compatibility for older comctl32 */
#ifndef UDM_SETRANGE
#define UDM_SETRANGE (WM_USER + 101)
#endif
#ifndef UDM_SETPOS
#define UDM_SETPOS   (WM_USER + 102)
#endif
#ifndef UDM_GETPOS
#define UDM_GETPOS   (WM_USER + 106)
#endif
#ifndef OPENFILENAME_SIZE_VERSION_400
#define OPENFILENAME_SIZE_VERSION_400 76
#endif

/* ============================================================
 * Globals
 * ============================================================ */
static HINSTANCE g_hInst      = NULL;
static HWND      g_hDlgMain   = NULL;
static HFONT     g_hFontUI    = NULL;
static HWND      g_hTooltip   = NULL;
static HMENU     g_hMenu      = NULL;
static BOOL      g_menuVisible = FALSE;
static BOOL      g_menu_was_selected = FALSE;

static GUILang   g_lang       = LANG_EN;

static char input_path[MAX_PATH] = "";
static int  g_shift    = 0;
static int  g_mode     = MODE_PURE;
static int  g_autosave = 1;

static TOOLINFO ti;

/* ============================================================
 * Prototypes
 * ============================================================ */
BOOL CALLBACK DlgProc(HWND, UINT, WPARAM, LPARAM);
void InitShiftControls(HWND);
BOOL SaveFileDialog(HWND, char*, int);

GUIErrorCode LoadFile(const char*, char**, DWORD*, DWORD*);
GUIErrorCode SaveFile(const char*, const char*, DWORD);

void MakeAutoSaveName(const char*, int, int, char*, int);
static void UpdateModeFromUI(HWND hDlg);
static void ShowError(HWND hDlg, GUIErrorCode code,
                      const MMLErrorInfo* err_info,
                      DWORD aux_filesize);

static const char* ExtractFileName(const char* path);
static void ApplyLanguage(HWND hDlg);
static BOOL CALLBACK SetChildFontProc(HWND hWnd, LPARAM lParam);
static void CreateTooltip(HWND hDlg);
static void UpdateSliderTooltip(HWND hDlg);
static void DoConvertAndSave(HWND hDlg, const char* inpath, BOOL use_autosave);
static void HandleDropFiles(HWND hDlg, HDROP hDrop);
static void ShowHiddenMenu(HWND hDlg, BOOL show);
static void SwitchLanguage(HWND hDlg, GUILang lang);
static void UpdateFilenameAndTooltip(HWND hDlg);

/* ============================================================
 * Extract file name
 * ============================================================ */
static const char* ExtractFileName(const char* path)
{
    const char* p;

    p = strrchr(path, '\\');
    if (p) return p + 1;
    p = strrchr(path, '/');
    if (p) return p + 1;
    return path;
}

/* ============================================================
 * WinMain
 * ============================================================ */
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nShow)
{
    g_hInst = hInst;
    InitCommonControls();
    return DialogBoxParam(hInst, MAKEINTRESOURCE(IDD_MAIN_DIALOG), NULL, DlgProc, 0);
}

/* ============================================================
 * Initialize shift controls
 * ============================================================ */
void InitShiftControls(HWND hDlg)
{
    HWND hSpin = GetDlgItem(hDlg, IDC_SPIN_SHIFT);

    SendMessage(hSpin, UDM_SETRANGE, 0, MAKELPARAM(12, -12));
    SendMessage(hSpin, UDM_SETPOS, 0, MAKELPARAM(g_shift, 0));

    SetDlgItemInt(hDlg, IDC_EDIT_SHIFT, g_shift, TRUE);
}

/* ============================================================
 * Update mode flags
 * ============================================================ */
static void UpdateModeFromUI(HWND hDlg)
{
    int fmt = (IsDlgButtonChecked(hDlg, IDC_CHECK_FORMAT) == BST_CHECKED);
    int rel = (IsDlgButtonChecked(hDlg, IDC_CHECK_REL)    == BST_CHECKED);
    int abs = (IsDlgButtonChecked(hDlg, IDC_CHECK_ABS)    == BST_CHECKED);
    int dch = (IsDlgButtonChecked(hDlg, IDC_CHECK_DSHIFT) == BST_CHECKED);

    if (rel && abs) abs = 0;

    g_mode = (fmt ? 4 : 0) | (rel ? 2 : 0) | (abs ? 1 : 0);

    if (dch) g_mode |= MODE_NOISE_SHIFT;

    if (g_mode == 3 || g_mode == 7)
        g_mode = MODE_PURE;
}

/* ============================================================
 * Error message dispatcher
 * ============================================================ */
static void ShowError(HWND hDlg, GUIErrorCode code,
                      const MMLErrorInfo* err_info,
                      DWORD aux_filesize)
{
    char msg[512];
    const char* tmpl;

    if (code <= GUI_ERR_OK || code >= GUI_ERR_MAX) {
        MessageBox(hDlg, "Unknown GUI error.", "Error", MB_ICONERROR);
        return;
    }

    tmpl = g_lang_table[g_lang].gui_msg[code];
    if (!tmpl) {
        MessageBox(hDlg, "Unknown GUI error.", "Error", MB_ICONERROR);
        return;
    }

    switch (code) {
    case GUI_ERR_LF_TOO_LARGE:
        wsprintf(msg, tmpl, (unsigned long)aux_filesize, MAX_TEXT);
        break;

    case GUI_ERR_MML_OCTAVE:
        if (err_info) {
            wsprintf(msg, tmpl,
                     err_info->channel_char,
                     err_info->line_number,
                     err_info->calculated_value);
        } else {
            lstrcpy(msg, tmpl);
        }
        break;

    case GUI_ERR_MML_UNKNOWN:
        if (err_info) {
            wsprintf(msg, tmpl, err_info->error_code);
        } else {
            lstrcpy(msg, tmpl);
        }
        break;

    default:
        lstrcpy(msg, tmpl);
        break;
    }

    MessageBox(hDlg, msg,
               "Error",
               MB_ICONERROR);
}

/* ============================================================
 * AutoSave output name generator
 * ============================================================ */
void MakeAutoSaveName(const char* inpath, int shift, int mode,
                      char* out, int outsize)
{
    char drive[_MAX_DRIVE];
    char dir[_MAX_DIR];
    char fname[_MAX_FNAME];
    char ext[_MAX_EXT];
    char shiftbuf[16];
    char modebuf[32];
    int base_mode = mode & 7;
    int dch_flag  = (mode & MODE_NOISE_SHIFT) ? 1 : 0;

    _splitpath(inpath, drive, dir, fname, ext);

    wsprintf(shiftbuf, shift == 0 ? "0" : "%c%d",
             shift > 0 ? '+' : '-',
             shift > 0 ? shift : -shift);

    switch (base_mode) {
        case MODE_PURE:       lstrcpy(modebuf, "pure");     break;
        case MODE_PURE_ABS:   lstrcpy(modebuf, "pure_abs"); break;
        case MODE_PURE_REL:   lstrcpy(modebuf, "pure_rel"); break;
        case MODE_FMT:        lstrcpy(modebuf, "fmt");      break;
        case MODE_FMT_ABS:    lstrcpy(modebuf, "fmt_abs");  break;
        case MODE_FMT_REL:    lstrcpy(modebuf, "fmt_rel");  break;
        default:              lstrcpy(modebuf, "pure");     break;
    }

    if (dch_flag)
        lstrcat(modebuf, "_d");

    _snprintf(out, outsize, "%s%s%s_%s_%s.mml",
              drive, dir, fname, shiftbuf, modebuf);

    out[outsize - 1] = '\0';
}

/* ============================================================
 * Safe file load (*.mml only)
 * ============================================================ */
GUIErrorCode LoadFile(const char* path, char** outbuf, DWORD* outsize, DWORD* out_filesize)
{
    HANDLE hFile;
    DWORD size;
    DWORD read;
    char* buf;

    *outbuf = NULL;
    *outsize = 0;
    if (out_filesize) *out_filesize = 0;

    hFile = CreateFile(path, GENERIC_READ, FILE_SHARE_READ,
                       NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return GUI_ERR_LF_OPEN;

    size = GetFileSize(hFile, NULL);
    if (size == INVALID_FILE_SIZE) {
        CloseHandle(hFile);
        return GUI_ERR_LF_SIZE;
    }

    if (out_filesize) *out_filesize = size;

    if (size == 0) {
        CloseHandle(hFile);
        return GUI_ERR_LF_EMPTY;
    }

    if (size > MAX_TEXT) {
        CloseHandle(hFile);
        return GUI_ERR_LF_TOO_LARGE;
    }

    buf = (char*)malloc(size + 1);
    if (!buf) {
        CloseHandle(hFile);
        return GUI_ERR_LF_READ;
    }

    read = 0;
    if (!ReadFile(hFile, buf, size, &read, NULL) ||
        read != size || read == 0)
    {
        free(buf);
        CloseHandle(hFile);
        return GUI_ERR_LF_READ;
    }

    buf[size] = '\0';
    CloseHandle(hFile);

    *outbuf  = buf;
    *outsize = size;

    return GUI_ERR_OK;
}

/* ============================================================
 * Save file (*.mml only)
 * ============================================================ */
GUIErrorCode SaveFile(const char* path, const char* data, DWORD size)
{
    FILE* fp;
    size_t written;

    fp = fopen(path, "w");
    if (!fp)
        return GUI_ERR_SF_OPEN;

    written = fwrite(data, 1, size, fp);
    fclose(fp);

    if (written != size)
        return GUI_ERR_SF_WRITE;

    return GUI_ERR_OK;
}

/* ============================================================
 * Save As dialog (*.mml only)
 * ============================================================ */
BOOL SaveFileDialog(HWND hDlg, char* outpath, int outsize)
{
    OPENFILENAME ofn;
    char initdir[MAX_PATH] = "";
    char* p;

    ZeroMemory(&ofn, sizeof(ofn));
    ZeroMemory(initdir, sizeof(initdir));
    outpath[0] = '\0';

    lstrcpy(initdir, input_path);
    p = strrchr(initdir, '\\');
    if (p) *p = '\0';

    ofn.lStructSize  = OPENFILENAME_SIZE_VERSION_400;
    ofn.hwndOwner    = hDlg;

    /* MML only */
    ofn.lpstrFilter  = "MML Files (*.mml)\0*.mml\0";

    ofn.lpstrFile    = outpath;
    ofn.nMaxFile     = outsize;
    ofn.lpstrInitialDir = initdir;
    ofn.lpstrDefExt  = "mml";
    ofn.Flags        = OFN_OVERWRITEPROMPT;

    return GetSaveFileName(&ofn);
}

/* ============================================================
 * Convert and save (centralized error handling)
 * ============================================================ */
static void DoConvertAndSave(HWND hDlg, const char* inpath, BOOL use_autosave)
{
    char* inbuf = NULL;
    DWORD insize = 0;
    DWORD fsize  = 0;
    int outsize;
    char* outbuf;
    int outlen;
    char outpath[MAX_PATH];
    MMLErrorInfo err_info;
    GUIErrorCode gerr;

    gerr = LoadFile(inpath, &inbuf, &insize, &fsize);
    if (gerr != GUI_ERR_OK) {
        ShowError(hDlg, gerr, NULL, fsize);
        return;
    }

    outsize = (int)insize * 2 + 1024;
    outbuf = (char*)malloc(outsize);
    if (!outbuf) {
        free(inbuf);
        ShowError(hDlg, GUI_ERR_MML_OUTBUF, NULL, 0);
        return;
    }

    memset(&err_info, 0, sizeof(err_info));

    outlen = mml_process(inbuf, g_shift, g_mode, outbuf, outsize, &err_info);
    free(inbuf);

    if (outlen < 0) {
        switch (outlen) {
        case MML_ERR_OCTAVE_OUT_OF_RANGE: gerr = GUI_ERR_MML_OCTAVE; break;
        case MML_ERR_NULL_INPUT:          gerr = GUI_ERR_MML_NULL;   break;
        case MML_ERR_EMPTY_INPUT:         gerr = GUI_ERR_MML_EMPTY;  break;
        case MML_ERR_OUTBUF:              gerr = GUI_ERR_MML_OUTBUF; break;
        case MML_ERR_BAD_SHIFT:           gerr = GUI_ERR_MML_BAD_SHIFT; break;
        case MML_ERR_BAD_MODE:            gerr = GUI_ERR_MML_BAD_MODE;  break;
        default:                          gerr = GUI_ERR_MML_UNKNOWN;   break;
        }

        ShowError(hDlg, gerr, &err_info, 0);
        free(outbuf);
        return;
    }

    if (outlen >= outsize) {
        ShowError(hDlg, GUI_ERR_MML_OUTBUF, NULL, 0);
        free(outbuf);
        return;
    }

    outbuf[outlen] = '\0';

    if (use_autosave) {
        MakeAutoSaveName(inpath, g_shift, g_mode, outpath, sizeof(outpath));
    } else {
        if (!SaveFileDialog(hDlg, outpath, sizeof(outpath))) {
            free(outbuf);
            return;
        }
    }

    gerr = SaveFile(outpath, outbuf, (DWORD)outlen);
    if (gerr != GUI_ERR_OK) {
        ShowError(hDlg, gerr, NULL, 0);
    }

    free(outbuf);
}

/* ============================================================
 * Apply font to child controls
 * ============================================================ */
BOOL CALLBACK SetChildFontProc(HWND hWnd, LPARAM lParam)
{
    HFONT hFont = (HFONT)lParam;
    SendMessage(hWnd, WM_SETFONT, (WPARAM)hFont, TRUE);
    return TRUE;
}

/* ============================================================
 * Update filename label + tooltip
 * ============================================================ */
static void UpdateFilenameAndTooltip(HWND hDlg)
{
    HWND hCtrl;

    hCtrl = GetDlgItem(hDlg, IDC_STATIC_FILENAME);
    if (!hCtrl || !g_hTooltip)
        return;

    ZeroMemory(&ti, sizeof(ti));
    ti.cbSize = sizeof(ti);
    ti.uFlags = TTF_SUBCLASS | TTF_IDISHWND;
    ti.hwnd   = hDlg;
    ti.uId    = (UINT)hCtrl;

    if (input_path[0] == '\0') {
        SetDlgItemText(hDlg, IDC_STATIC_FILENAME,
                       g_lang_table[g_lang].tip[TIP_FILENAME_LABEL]);
        ti.lpszText = (LPSTR)g_lang_table[g_lang].tip[TIP_FILENAME_TOOLTIP];
    } else {
        SetDlgItemText(hDlg, IDC_STATIC_FILENAME,
                       ExtractFileName(input_path));
        ti.lpszText = input_path;
    }

    SendMessage(g_hTooltip, TTM_UPDATETIPTEXT, 0, (LPARAM)&ti);
}

/* ============================================================
 * Apply language (font + labels + tooltip)
 * ============================================================ */
static void ApplyLanguage(HWND hDlg)
{
    LOGFONT lf;
    HFONT   hNewFont;

    if (g_hFontUI) {
        DeleteObject(g_hFontUI);
        g_hFontUI = NULL;
    }

    ZeroMemory(&lf, sizeof(lf));
    lf.lfHeight         = (-MulDiv(g_lang_table[g_lang].font_size,
                                  GetDeviceCaps(GetDC(hDlg), LOGPIXELSY), 72)) - 1;
    lf.lfWeight         = FW_NORMAL;
    lf.lfCharSet        = g_lang_table[g_lang].charset;
    lf.lfOutPrecision   = OUT_DEFAULT_PRECIS;
    lf.lfClipPrecision  = CLIP_DEFAULT_PRECIS;
    lf.lfQuality        = DEFAULT_QUALITY;
    lf.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
    lstrcpyn(lf.lfFaceName, g_lang_table[g_lang].font_name,
             sizeof(lf.lfFaceName) / sizeof(TCHAR));

    hNewFont = CreateFontIndirect(&lf);
    if (hNewFont) {
        g_hFontUI = hNewFont;
        SendMessage(hDlg, WM_SETFONT, (WPARAM)g_hFontUI, TRUE);
        EnumChildWindows(hDlg, SetChildFontProc, (LPARAM)g_hFontUI);
    }

    SetWindowText(GetDlgItem(hDlg, IDC_STATIC_KEY),
                  g_lang_table[g_lang].ui_label_key);
    SetWindowText(GetDlgItem(hDlg, IDC_STATIC_OPTION),
                  g_lang_table[g_lang].ui_label_option);
    SetWindowText(GetDlgItem(hDlg, IDC_BUTTON_SAVE_AUTO),
                  g_lang_table[g_lang].ui_label_quick);
    SetWindowText(GetDlgItem(hDlg, IDC_BUTTON_SAVE_AS),
                  g_lang_table[g_lang].ui_label_save);
    SetWindowText(GetDlgItem(hDlg, IDC_CHECK_AUTOSAVE),
                  g_lang_table[g_lang].ui_label_auto);

    CreateTooltip(hDlg);
    UpdateFilenameAndTooltip(hDlg);
    UpdateSliderTooltip(hDlg);
}

/* ============================================================
 * Create tooltip for all controls
 * ============================================================ */
static void CreateTooltip(HWND hDlg)
{
    HWND hCtrl;

    if (g_hTooltip) {
        DestroyWindow(g_hTooltip);
        g_hTooltip = NULL;
    }

    g_hTooltip = CreateWindowEx(
        0, TOOLTIPS_CLASS, NULL,
        WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
        CW_USEDEFAULT, CW_USEDEFAULT,
        CW_USEDEFAULT, CW_USEDEFAULT,
        hDlg, NULL, g_hInst, NULL);

    if (!g_hTooltip)
        return;

    ZeroMemory(&ti, sizeof(ti));
    ti.cbSize = sizeof(ti);
    ti.uFlags = TTF_SUBCLASS | TTF_IDISHWND;
    ti.hwnd   = hDlg;

    /* Filename label (tooltip text updated later) */
    hCtrl = GetDlgItem(hDlg, IDC_STATIC_FILENAME);
    if (hCtrl) {
        ti.uId      = (UINT)hCtrl;
        ti.lpszText = (LPSTR)g_lang_table[g_lang].tip[TIP_FILENAME_TOOLTIP];
        SendMessage(g_hTooltip, TTM_ADDTOOL, 0, (LPARAM)&ti);
    }

    /* Slider (Shift) */
    hCtrl = GetDlgItem(hDlg, IDC_SLIDER_SHIFT);
    if (hCtrl) {
        ti.uId      = (UINT)hCtrl;
        ti.lpszText = (LPSTR)g_lang_table[g_lang].tip[TIP_SLIDER];
        SendMessage(g_hTooltip, TTM_ADDTOOL, 0, (LPARAM)&ti);
    }

    /* Key label */
    hCtrl = GetDlgItem(hDlg, IDC_STATIC_KEY);
    if (hCtrl) {
        ti.uId      = (UINT)hCtrl;
        ti.lpszText = (LPSTR)g_lang_table[g_lang].tip[TIP_KEY_LABEL];
        SendMessage(g_hTooltip, TTM_ADDTOOL, 0, (LPARAM)&ti);
    }

    /* Key edit box */
    hCtrl = GetDlgItem(hDlg, IDC_EDIT_SHIFT);
    if (hCtrl) {
        ti.uId      = (UINT)hCtrl;
        ti.lpszText = (LPSTR)g_lang_table[g_lang].tip[TIP_KEY_EDIT];
        SendMessage(g_hTooltip, TTM_ADDTOOL, 0, (LPARAM)&ti);
    }

    /* Key spin control */
    hCtrl = GetDlgItem(hDlg, IDC_SPIN_SHIFT);
    if (hCtrl) {
        ti.uId      = (UINT)hCtrl;
        ti.lpszText = (LPSTR)g_lang_table[g_lang].tip[TIP_KEY_SPIN];
        SendMessage(g_hTooltip, TTM_ADDTOOL, 0, (LPARAM)&ti);
    }

    /* Quick Save */
    hCtrl = GetDlgItem(hDlg, IDC_BUTTON_SAVE_AUTO);
    if (hCtrl) {
        ti.uId      = (UINT)hCtrl;
        ti.lpszText = (LPSTR)g_lang_table[g_lang].tip[TIP_SAVE_AUTO];
        SendMessage(g_hTooltip, TTM_ADDTOOL, 0, (LPARAM)&ti);
    }

    /* Save As */
    hCtrl = GetDlgItem(hDlg, IDC_BUTTON_SAVE_AS);
    if (hCtrl) {
        ti.uId      = (UINT)hCtrl;
        ti.lpszText = (LPSTR)g_lang_table[g_lang].tip[TIP_SAVE_AS];
        SendMessage(g_hTooltip, TTM_ADDTOOL, 0, (LPARAM)&ti);
    }

    /* Option label */
    hCtrl = GetDlgItem(hDlg, IDC_STATIC_OPTION);
    if (hCtrl) {
        ti.uId      = (UINT)hCtrl;
        ti.lpszText = (LPSTR)g_lang_table[g_lang].tip[TIP_OPTION_LABEL];
        SendMessage(g_hTooltip, TTM_ADDTOOL, 0, (LPARAM)&ti);
    }

    /* REL / ABS / FORMAT / D-SHIFT / AUTOSAVE */
    hCtrl = GetDlgItem(hDlg, IDC_CHECK_REL);
    if (hCtrl) {
        ti.uId      = (UINT)hCtrl;
        ti.lpszText = (LPSTR)g_lang_table[g_lang].tip[TIP_REL];
        SendMessage(g_hTooltip, TTM_ADDTOOL, 0, (LPARAM)&ti);
    }

    hCtrl = GetDlgItem(hDlg, IDC_CHECK_ABS);
    if (hCtrl) {
        ti.uId      = (UINT)hCtrl;
        ti.lpszText = (LPSTR)g_lang_table[g_lang].tip[TIP_ABS];
        SendMessage(g_hTooltip, TTM_ADDTOOL, 0, (LPARAM)&ti);
    }

    hCtrl = GetDlgItem(hDlg, IDC_CHECK_FORMAT);
    if (hCtrl) {
        ti.uId      = (UINT)hCtrl;
        ti.lpszText = (LPSTR)g_lang_table[g_lang].tip[TIP_FORMAT];
        SendMessage(g_hTooltip, TTM_ADDTOOL, 0, (LPARAM)&ti);
    }

    hCtrl = GetDlgItem(hDlg, IDC_CHECK_DSHIFT);
    if (hCtrl) {
        ti.uId      = (UINT)hCtrl;
        ti.lpszText = (LPSTR)g_lang_table[g_lang].tip[TIP_DSHIFT];
        SendMessage(g_hTooltip, TTM_ADDTOOL, 0, (LPARAM)&ti);
    }

    hCtrl = GetDlgItem(hDlg, IDC_CHECK_AUTOSAVE);
    if (hCtrl) {
        ti.uId      = (UINT)hCtrl;
        ti.lpszText = (LPSTR)g_lang_table[g_lang].tip[TIP_AUTOSAVE];
        SendMessage(g_hTooltip, TTM_ADDTOOL, 0, (LPARAM)&ti);
    }

    UpdateSliderTooltip(hDlg);
}

/* ============================================================
 * Update slider tooltip (dynamic shift value)
 * ============================================================ */
static void UpdateSliderTooltip(HWND hDlg)
{
    char buf[32];
    HWND hCtrl;

    if (!g_hTooltip) return;

    hCtrl = GetDlgItem(hDlg, IDC_SLIDER_SHIFT);
    if (!hCtrl) return;

    wsprintf(buf, g_lang_table[g_lang].tip[TIP_SLIDER],
             (g_shift >= 0) ? "+" : "");
    {
        char tmp[16];
        wsprintf(tmp, "%d", g_shift);
        lstrcat(buf, tmp);
    }

    ZeroMemory(&ti, sizeof(ti));
    ti.cbSize   = sizeof(ti);
    ti.uFlags   = TTF_SUBCLASS | TTF_IDISHWND;
    ti.hwnd     = hDlg;
    ti.uId      = (UINT)hCtrl;
    ti.lpszText = buf;

    SendMessage(g_hTooltip, TTM_UPDATETIPTEXT, 0, (LPARAM)&ti);
}

/* ============================================================
 * Adjust dialog height when menu bar is shown/hidden
 * ============================================================ */
void AdjustDialogHeight(HWND hDlg, BOOL visible)
{
    RECT rc;
    int mh;
    int h;

    mh = GetSystemMetrics(SM_CYMENU);
    GetWindowRect(hDlg, &rc);
    h = rc.bottom - rc.top;
    if (visible) h += mh; else h -= mh;
    SetWindowPos(hDlg, NULL, rc.left, rc.top, rc.right - rc.left, h,
                 SWP_NOZORDER | SWP_NOMOVE);
}

/* ============================================================
 * Show/hide hidden menu bar
 * ============================================================ */
static void ShowHiddenMenu(HWND hDlg, BOOL show)
{
    if (show) {
        if (!g_menuVisible && g_hMenu) {
            SetMenu(hDlg, g_hMenu);
            DrawMenuBar(hDlg);
            AdjustDialogHeight(hDlg, TRUE);
            g_menuVisible = TRUE;
        }
    } else {
        if (g_menuVisible) {
            SetMenu(hDlg, NULL);
            DrawMenuBar(hDlg);
            AdjustDialogHeight(hDlg, FALSE);
            g_menuVisible = FALSE;
        }
    }
}

/* ============================================================
 * Handle dropped files (*.mml only)
 * ============================================================ */
static void HandleDropFiles(HWND hDlg, HDROP hDrop)
{
    char path[MAX_PATH] = "";
    char* inbuf = NULL;
    DWORD insize = 0;
    DWORD fsize  = 0;
    GUIErrorCode gerr;

    if (!DragQueryFile(hDrop, 0, path, sizeof(path))) {
        DragFinish(hDrop);
        return;
    }

    /* Extension check (*.mml only) */
    {
        char* ext = strrchr(path, '.');
        if (!ext || lstrcmpi(ext, ".mml") != 0) {

            input_path[0] = '\0';
            SetDlgItemText(hDlg, IDC_STATIC_FILENAME,
                           (LPSTR)g_lang_table[g_lang].tip[TIP_INVALID_TYPE]);

            if (g_hTooltip) {
                ZeroMemory(&ti, sizeof(ti));
                ti.cbSize = sizeof(ti);
                ti.uFlags = TTF_SUBCLASS | TTF_IDISHWND;
                ti.hwnd   = hDlg;
                ti.uId    = (UINT)GetDlgItem(hDlg, IDC_STATIC_FILENAME);
                ti.lpszText = (LPSTR)g_lang_table[g_lang].tip[TIP_INVALID_TYPE];
                SendMessage(g_hTooltip, TTM_UPDATETIPTEXT, 0, (LPARAM)&ti);
            }

            DragFinish(hDrop);
            return;
        }
    }

    /* Try loading file first */
    gerr = LoadFile(path, &inbuf, &insize, &fsize);
    if (gerr != GUI_ERR_OK) {

        input_path[0] = '\0';
        UpdateFilenameAndTooltip(hDlg);

        ShowError(hDlg, gerr, NULL, fsize);
        DragFinish(hDrop);
        return;
    }

    /* Load succeeded: update path and UI */
    lstrcpy(input_path, path);
    UpdateFilenameAndTooltip(hDlg);

    if (IsDlgButtonChecked(hDlg, IDC_CHECK_AUTOSAVE) == BST_CHECKED) {
        UpdateModeFromUI(hDlg);
        DoConvertAndSave(hDlg, input_path, TRUE);
    }

    free(inbuf);
    DragFinish(hDrop);
}

/* ============================================================
 * Switch language (EN/JP)
 * ============================================================ */
static void SwitchLanguage(HWND hDlg, GUILang lang)
{
    if (lang < 0 || lang >= LANG_MAX) return;
    g_lang = lang;
    ApplyLanguage(hDlg);
}

/* ============================================================
 * Dialog procedure
 * ============================================================ */
BOOL CALLBACK DlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    char buf[16];
    static char tipbuf[32];

    switch (msg)
    {
    case WM_INITDIALOG:
    {
        HWND hEdit, hSpin;
        HWND hSlider;

        g_hDlgMain = hDlg;

        InitShiftControls(hDlg);
        DragAcceptFiles(hDlg, TRUE);

        g_hMenu = LoadMenu(g_hInst, MAKEINTRESOURCE(IDR_MAINMENU));
        SetMenu(hDlg, NULL);
        g_menu_was_selected = FALSE;
        AdjustDialogHeight(hDlg, FALSE);

        CheckDlgButton(hDlg, IDC_CHECK_REL,     BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_CHECK_ABS,     BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_CHECK_FORMAT,  BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_CHECK_DSHIFT,  BST_UNCHECKED);

        CheckDlgButton(hDlg, IDC_CHECK_AUTOSAVE, BST_CHECKED);
        g_autosave = 1;

        EnableWindow(GetDlgItem(hDlg, IDC_BUTTON_SAVE_AUTO), FALSE);
        EnableWindow(GetDlgItem(hDlg, IDC_BUTTON_SAVE_AS),   FALSE);

        hEdit = GetDlgItem(hDlg, IDC_EDIT_SHIFT);
        hSpin = GetDlgItem(hDlg, IDC_SPIN_SHIFT);
        SendMessage(hSpin, UDM_SETBUDDY, (WPARAM)hEdit, 0);

        hSlider = GetDlgItem(hDlg, IDC_SLIDER_SHIFT);
        SendMessage(hSlider, TBM_SETRANGE, TRUE, MAKELPARAM(0, 24));
        SendMessage(hSlider, TBM_SETPOS, TRUE, g_shift + 12);

        ApplyLanguage(hDlg);

        return TRUE;
    }

    case WM_DROPFILES:
        HandleDropFiles(hDlg, (HDROP)wParam);
        return TRUE;

    case WM_NOTIFY:
    {
        NMHDR* hdr = (NMHDR*)lParam;

        if (hdr->idFrom == IDC_SPIN_SHIFT && hdr->code == UDN_DELTAPOS)
        {
            NMUPDOWN* ud = (NMUPDOWN*)lParam;

            g_shift += ud->iDelta;
            if (g_shift < -12) g_shift = -12;
            if (g_shift >  12) g_shift =  12;

            wsprintf(buf, (g_shift > 0) ? "+%d" : "%d", g_shift);
            SetDlgItemText(hDlg, IDC_EDIT_SHIFT, buf);

            SendMessage(GetDlgItem(hDlg, IDC_SLIDER_SHIFT),
                        TBM_SETPOS, TRUE, g_shift + 12);

            if (g_hTooltip) {
                ZeroMemory(&ti, sizeof(ti));
                ti.cbSize = sizeof(ti);
                ti.uFlags = TTF_SUBCLASS | TTF_IDISHWND;
                ti.hwnd   = hDlg;
                ti.uId    = (UINT)GetDlgItem(hDlg, IDC_SLIDER_SHIFT);

                wsprintf(tipbuf, g_lang_table[g_lang].tip[TIP_SLIDER], "");
                {
                    char tmp[16];
                    wsprintf(tmp, "%s", buf);
                    lstrcat(tipbuf, tmp);
                }
                ti.lpszText = tipbuf;

                SendMessage(g_hTooltip, TTM_UPDATETIPTEXT, 0, (LPARAM)&ti);
            }

            return TRUE;
        }
        break;
    }

    case WM_HSCROLL:
    {
        HWND hSlider = GetDlgItem(hDlg, IDC_SLIDER_SHIFT);

        if ((HWND)lParam == hSlider)
        {
            int pos = (int)SendMessage(hSlider, TBM_GETPOS, 0, 0);
            g_shift = pos - 12;

            wsprintf(buf, (g_shift > 0) ? "+%d" : "%d", g_shift);
            SetDlgItemText(hDlg, IDC_EDIT_SHIFT, buf);

            SendMessage(GetDlgItem(hDlg, IDC_SPIN_SHIFT),
                        UDM_SETPOS, 0, MAKELPARAM(g_shift, 0));

            UpdateSliderTooltip(hDlg);
        }
        break;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDC_BUTTON_SAVE_AUTO:
        {
            if (IsDlgButtonChecked(hDlg, IDC_CHECK_AUTOSAVE) == BST_CHECKED)
                break;

            if (input_path[0] == '\0') {
                ShowError(hDlg, GUI_ERR_LF_OPEN, NULL, 0);
                break;
            }

            UpdateModeFromUI(hDlg);
            DoConvertAndSave(hDlg, input_path, TRUE);

            UpdateFilenameAndTooltip(hDlg);
            UpdateSliderTooltip(hDlg);
            break;
        }

        case IDC_BUTTON_SAVE_AS:
        {
            if (IsDlgButtonChecked(hDlg, IDC_CHECK_AUTOSAVE) == BST_CHECKED)
                break;

            if (input_path[0] == '\0') {
                ShowError(hDlg, GUI_ERR_LF_OPEN, NULL, 0);
                break;
            }

            UpdateModeFromUI(hDlg);
            DoConvertAndSave(hDlg, input_path, FALSE);

            UpdateFilenameAndTooltip(hDlg);
            UpdateSliderTooltip(hDlg);
            break;
        }

        case IDC_CHECK_AUTOSAVE:
        {
            BOOL auto_on = (IsDlgButtonChecked(hDlg, IDC_CHECK_AUTOSAVE) == BST_CHECKED);
            g_autosave = auto_on ? 1 : 0;

            EnableWindow(GetDlgItem(hDlg, IDC_BUTTON_SAVE_AUTO), !auto_on);
            EnableWindow(GetDlgItem(hDlg, IDC_BUTTON_SAVE_AS),   !auto_on);
            break;
        }

        case IDC_CHECK_REL:
        {
            BOOL rel = (IsDlgButtonChecked(hDlg, IDC_CHECK_REL) == BST_CHECKED);

            if (rel) {
                CheckDlgButton(hDlg, IDC_CHECK_ABS, BST_UNCHECKED);
            }
            break;
        }

        case IDC_CHECK_ABS:
        {
            BOOL abs = (IsDlgButtonChecked(hDlg, IDC_CHECK_ABS) == BST_CHECKED);

            if (abs) {
                CheckDlgButton(hDlg, IDC_CHECK_REL, BST_UNCHECKED);
            }
            break;
        }

        case IDC_CHECK_FORMAT:
            break;

        case IDC_CHECK_DSHIFT:
            break;

        case IDM_FILE_OPEN:
        {
            char path[MAX_PATH] = "";
            OPENFILENAME ofn;
            char* inbuf = NULL;
            DWORD insize = 0;
            DWORD fsize  = 0;
            GUIErrorCode gerr;

            ZeroMemory(&ofn, sizeof(ofn));
            ofn.lStructSize  = OPENFILENAME_SIZE_VERSION_400;
            ofn.hwndOwner    = hDlg;
            ofn.lpstrFilter  = "MML Files (*.mml)\0*.mml\0";
            ofn.lpstrFile    = path;
            ofn.nMaxFile     = sizeof(path);
            ofn.lpstrDefExt  = "mml";
            ofn.Flags        = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;

            if (!GetOpenFileName(&ofn))
                break;

            gerr = LoadFile(path, &inbuf, &insize, &fsize);
            if (gerr != GUI_ERR_OK) {
                input_path[0] = '\0';
                UpdateFilenameAndTooltip(hDlg);
                ShowError(hDlg, gerr, NULL, fsize);
                break;
            }

            lstrcpy(input_path, path);
            UpdateFilenameAndTooltip(hDlg);

            if (IsDlgButtonChecked(hDlg, IDC_CHECK_AUTOSAVE) == BST_CHECKED) {
                UpdateModeFromUI(hDlg);
                DoConvertAndSave(hDlg, input_path, TRUE);
            }

            free(inbuf);
            break;
        }

        case IDM_FILE_EXIT:
            EndDialog(hDlg, 0);
            break;

        case IDM_LANG_EN:
            SwitchLanguage(hDlg, LANG_EN);
            break;

        case IDM_LANG_JP:
            SwitchLanguage(hDlg, LANG_JP);
            break;

        case IDCANCEL:
            EndDialog(hDlg, 0);
            break;
        }
        break;

    case WM_SYSCOMMAND:
        if ((wParam & 0xFFF0) == SC_KEYMENU) {
            ShowHiddenMenu(hDlg, !g_menuVisible);
            return 0;
        }
        break;

    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE) {
            ShowHiddenMenu(hDlg, FALSE);
        }
        break;

    case WM_MENUSELECT:
    {
        BOOL now = !(HIWORD(wParam) == 0xFFFF && lParam == 0);

        if (g_menu_was_selected && !now)
            PostMessage(hDlg, WM_MENU_LOST, 0, 0);

        g_menu_was_selected = now;
    }
    break;

    case WM_MENU_LOST:
        ShowHiddenMenu(hDlg, FALSE);
        break;

    case WM_CLOSE:
        EndDialog(hDlg, 0);
        break;

    case WM_DESTROY:
        DragAcceptFiles(hDlg, FALSE);
        if (g_hFontUI) {
            DeleteObject(g_hFontUI);
            g_hFontUI = NULL;
        }
        if (g_hTooltip) {
            DestroyWindow(g_hTooltip);
            g_hTooltip = NULL;
        }
        if (g_menuVisible) {
            SetMenu(hDlg, NULL);
            g_menuVisible = FALSE;
        }
        g_hDlgMain = NULL;
        break;
    }

    return FALSE;
}
