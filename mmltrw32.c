// mmltrw32.c  (Auto-save 特化版 EXE, C89 対応, 新エンジン対応)

#include <windows.h>
#include <commdlg.h>
#include <commctrl.h>
#include <shellapi.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "resource.h"
#include "mmleng32.h"

// --- BCC55 / comctl32 v5 対策 ---
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
 * GUI 層エラーコード（エンジン層とは完全に独立）
 * ============================================================ */
typedef enum {
    GUI_ERR_OK = 0,

    /* LoadFile 系 */
    GUI_ERR_LF_OPEN,
    GUI_ERR_LF_SIZE,
    GUI_ERR_LF_EMPTY,
    GUI_ERR_LF_TOO_LARGE,   /* 動的メッセージ */
    GUI_ERR_LF_READ,

    /* mml_process 系 */
    GUI_ERR_MML_OCTAVE,     /* 動的メッセージ */
    GUI_ERR_MML_NULL,
    GUI_ERR_MML_EMPTY,
    GUI_ERR_MML_OUTBUF,
    GUI_ERR_MML_BAD_SHIFT,
    GUI_ERR_MML_BAD_MODE,
    GUI_ERR_MML_UNKNOWN,    /* 動的メッセージ */

    /* SaveFile 系 */
    GUI_ERR_SF_OPEN,
    GUI_ERR_SF_WRITE,

    GUI_ERR_MAX
} GUIErrorCode;

/* ============================================================
 * GUI エラーメッセージ配列（動的生成が必要なものは NULL）
 * ============================================================ */
static const char* g_gui_error_messages[GUI_ERR_MAX] = {
    NULL,                                   /* GUI_ERR_OK */

    "入力ファイルを開けません。",
    "ファイルサイズを取得できません。",
    "入力ファイルが空です。",
    NULL,                                   /* GUI_ERR_LF_TOO_LARGE → 動的生成 */
    "ファイル読み込みに失敗しました。",

    NULL,                                   /* GUI_ERR_MML_OCTAVE → 動的生成 */
    "入力テキストが NULL です。",
    "入力テキストが空です。",
    "出力バッファサイズが不足しています。",
    "移調量が範囲外です（-12～+12）。",
    "モードが無効です。",
    NULL,                                   /* GUI_ERR_MML_UNKNOWN → 動的生成 */

    "保存ファイルを開けません。",
    "ファイル書き込みに失敗しました。"
};

/* ============================================================
 * グローバル
 * ============================================================ */
static char input_path[MAX_PATH] = "";
static int  g_shift    = 0;
static int  g_mode     = MODE_PURE;
static int  g_autosave = 1;
static HWND g_hTooltip = NULL;
static HFONT g_hFont   = NULL;
static TOOLINFO ti;

/* ============================================================
 * プロトタイプ
 * ============================================================ */
BOOL CALLBACK DlgProc(HWND, UINT, WPARAM, LPARAM);
void InitShiftControls(HWND);
BOOL SaveFileDialog(HWND, char*, int);

/* GUI 層エラーコード版の Load/Save */
GUIErrorCode LoadFile(const char*, char**, DWORD*, DWORD*);
GUIErrorCode SaveFile(const char*, const char*, DWORD);

void MakeAutoSaveName(const char*, int, int, char*, int);
static void UpdateModeFromUI(HWND hDlg);

/* 唯一の MessageBox 出力関数 */
static void ShowError(HWND hDlg, GUIErrorCode code,
                      const MMLErrorInfo* err_info,
                      DWORD aux_filesize);

/* ============================================================
 * ファイル名抽出
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
    InitCommonControls();
    return DialogBoxParam(hInst, MAKEINTRESOURCE(IDD_MAIN_DIALOG), NULL, DlgProc, 0);
}

/* ============================================================
 * Shift 初期化
 * ============================================================ */
void InitShiftControls(HWND hDlg)
{
    HWND hSpin = GetDlgItem(hDlg, IDC_SPIN_SHIFT);

    SendMessage(hSpin, UDM_SETRANGE, 0, MAKELPARAM(12, -12));
    SendMessage(hSpin, UDM_SETPOS, 0, MAKELPARAM(g_shift, 0));

    SetDlgItemInt(hDlg, IDC_EDIT_SHIFT, g_shift, TRUE);
}

/* ============================================================
 * モード更新（D-ch 対応）
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
 * 唯一のエラーメッセージ表示関数
 * ============================================================ */
static void ShowError(HWND hDlg, GUIErrorCode code,
                      const MMLErrorInfo* err_info,
                      DWORD aux_filesize)
{
    char msg[512];
    const char* fixed;

    if (code <= GUI_ERR_OK || code >= GUI_ERR_MAX) {
        MessageBox(hDlg, "不明なエラーが発生しました。", "エラー", MB_ICONERROR);
        return;
    }

    fixed = g_gui_error_messages[code];

    if (fixed != NULL) {
        lstrcpy(msg, fixed);
    } else {
        /* 動的メッセージ生成 */
        switch (code) {

        case GUI_ERR_LF_TOO_LARGE:
            wsprintf(msg,
                "エラー: 入力ファイルが大きすぎます (%lu バイト)。\n"
                "       最大 %d バイトまでです。",
                (unsigned long)aux_filesize, MAX_TEXT - 1);
            break;

        case GUI_ERR_MML_OCTAVE:
            if (err_info) {
                wsprintf(msg,
                    "エラー: 移調によりオクターブ限界を突破しました。\n"
                    "        チャンネル '%c', %d 行目 (計算値: o%d)",
                    err_info->channel_char,
                    err_info->line_number,
                    err_info->calculated_value);
            } else {
                lstrcpy(msg, "エラー: オクターブ限界を突破しました。");
            }
            break;

        case GUI_ERR_MML_UNKNOWN:
            if (err_info) {
                wsprintf(msg,
                    "MML 処理に失敗しました。(コード %d)",
                    err_info->error_code);
            } else {
                lstrcpy(msg, "MML 処理に失敗しました。（不明なコード）");
            }
            break;

        default:
            lstrcpy(msg, "不明なエラーが発生しました。");
            break;
        }
    }

    MessageBox(hDlg, msg, "エラー", MB_ICONERROR);
}

/* ============================================================
 * 変換＆保存（エラーハンドリング集約版）
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

    /* 入力ファイル読み込み */
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

    /* 新エンジン API */
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
 * フォント適用
 * ============================================================ */
BOOL CALLBACK SetChildFontProc(HWND hWnd, LPARAM lParam)
{
    HFONT hFont = (HFONT)lParam;
    SendMessage(hWnd, WM_SETFONT, (WPARAM)hFont, TRUE);
    return TRUE;
}

/* ============================================================
 * ダイアログプロシージャ
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

        InitShiftControls(hDlg);
        DragAcceptFiles(hDlg, TRUE);

        /* --- フォント設定 --- */
        g_hFont = CreateFont(
            -14, 0, 0, 0, FW_NORMAL,
            FALSE, FALSE, FALSE,
            SHIFTJIS_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY,
            FIXED_PITCH | FF_MODERN,
            "MS Sans Serif"
        );
        if (g_hFont) {
            SendMessage(hDlg, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            EnumChildWindows(hDlg, SetChildFontProc, (LPARAM)g_hFont);
        }

        /* --- ツールチップ作成 --- */
        g_hTooltip = CreateWindowEx(
            0, TOOLTIPS_CLASS, NULL,
            WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
            CW_USEDEFAULT, CW_USEDEFAULT,
            CW_USEDEFAULT, CW_USEDEFAULT,
            hDlg, NULL, GetModuleHandle(NULL), NULL);

        if (g_hTooltip)
        {
            /* ファイル名ラベル */
            ZeroMemory(&ti, sizeof(ti));
            ti.cbSize = sizeof(ti);
            ti.uFlags = TTF_SUBCLASS | TTF_IDISHWND;
            ti.hwnd   = hDlg;
            ti.uId    = (UINT)GetDlgItem(hDlg, IDC_STATIC_FILENAME);
            ti.lpszText = "No file selected";
            SendMessage(g_hTooltip, TTM_ADDTOOL, 0, (LPARAM)&ti);

            /* スライダー */
            ZeroMemory(&ti, sizeof(ti));
            ti.cbSize = sizeof(ti);
            ti.uFlags = TTF_SUBCLASS | TTF_IDISHWND;
            ti.hwnd   = hDlg;
            ti.uId    = (UINT)GetDlgItem(hDlg, IDC_SLIDER_SHIFT);
            ti.lpszText = "Shift: 0";
            SendMessage(g_hTooltip, TTM_ADDTOOL, 0, (LPARAM)&ti);

            /* Key: ラベル */
            ZeroMemory(&ti, sizeof(ti));
            ti.cbSize = sizeof(ti);
            ti.uFlags = TTF_SUBCLASS | TTF_IDISHWND;
            ti.hwnd   = hDlg;
            ti.uId    = (UINT)GetDlgItem(hDlg, IDC_STATIC_KEY);
            ti.lpszText = "Semitone shift";
            SendMessage(g_hTooltip, TTM_ADDTOOL, 0, (LPARAM)&ti);
        }

        /* --- 初期状態 --- */
        CheckDlgButton(hDlg, IDC_CHECK_REL,     BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_CHECK_ABS,     BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_CHECK_FORMAT,  BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_CHECK_DSHIFT,  BST_UNCHECKED);

        CheckDlgButton(hDlg, IDC_CHECK_AUTOSAVE, BST_CHECKED);
        g_autosave = 1;

        EnableWindow(GetDlgItem(hDlg, IDC_BUTTON_SAVE_AUTO), FALSE);
        EnableWindow(GetDlgItem(hDlg, IDC_BUTTON_SAVE_AS),   FALSE);

        /* スピンのバディ設定 */
        hEdit = GetDlgItem(hDlg, IDC_EDIT_SHIFT);
        hSpin = GetDlgItem(hDlg, IDC_SPIN_SHIFT);
        SendMessage(hSpin, UDM_SETBUDDY, (WPARAM)hEdit, 0);

        /* スライダー初期化 */
        {
            HWND hSlider = GetDlgItem(hDlg, IDC_SLIDER_SHIFT);
            SendMessage(hSlider, TBM_SETRANGE, TRUE, MAKELPARAM(0, 24));
            SendMessage(hSlider, TBM_SETPOS, TRUE, g_shift + 12);
        }

        return TRUE;
    }

    case WM_DROPFILES:
    {
        HDROP hDrop = (HDROP)wParam;
        char path[MAX_PATH] = "";
        char* ext;

        if (DragQueryFile(hDrop, 0, path, sizeof(path)))
        {
            ext = strrchr(path, '.');
            if (!ext || lstrcmpi(ext, ".mml") != 0)
            {
                input_path[0] = '\0';

                SetDlgItemText(hDlg, IDC_STATIC_FILENAME,
                    "ERROR: Cannot OPEN.");

                if (g_hTooltip)
                {
                    ZeroMemory(&ti, sizeof(ti));
                    ti.cbSize = sizeof(ti);
                    ti.uFlags = TTF_SUBCLASS | TTF_IDISHWND;
                    ti.hwnd   = hDlg;
                    ti.uId    = (UINT)GetDlgItem(hDlg, IDC_STATIC_FILENAME);
                    ti.lpszText = "ERROR: Cannot OPEN. Invalid File Type.";
                    SendMessage(g_hTooltip, TTM_UPDATETIPTEXT, 0, (LPARAM)&ti);
                }

                DragFinish(hDrop);
                return TRUE;
            }

            lstrcpy(input_path, path);

            SetDlgItemText(hDlg, IDC_STATIC_FILENAME,
                           ExtractFileName(input_path));

            if (g_hTooltip){
                ZeroMemory(&ti, sizeof(ti));
                ti.cbSize = sizeof(ti);
                ti.uFlags = TTF_SUBCLASS | TTF_IDISHWND;
                ti.hwnd   = hDlg;
                ti.uId    = (UINT)GetDlgItem(hDlg, IDC_STATIC_FILENAME);
                ti.lpszText = input_path;
                SendMessage(g_hTooltip, TTM_UPDATETIPTEXT, 0, (LPARAM)&ti);
            }

            if (IsDlgButtonChecked(hDlg, IDC_CHECK_AUTOSAVE) == BST_CHECKED){

                UpdateModeFromUI(hDlg);
                DoConvertAndSave(hDlg, input_path, TRUE);

                EnableWindow(GetDlgItem(hDlg, IDC_BUTTON_SAVE_AUTO), FALSE);
                EnableWindow(GetDlgItem(hDlg, IDC_BUTTON_SAVE_AS),   FALSE);
            }else{
                EnableWindow(GetDlgItem(hDlg, IDC_BUTTON_SAVE_AUTO), TRUE);
                EnableWindow(GetDlgItem(hDlg, IDC_BUTTON_SAVE_AS),   TRUE);
            }
        }

        DragFinish(hDrop);
        return TRUE;
    }

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

                wsprintf(tipbuf, "Shift: %s", buf);
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

            if (g_hTooltip) {
                ZeroMemory(&ti, sizeof(ti));
                ti.cbSize = sizeof(ti);
                ti.uFlags = TTF_SUBCLASS | TTF_IDISHWND;
                ti.hwnd   = hDlg;
                ti.uId    = (UINT)GetDlgItem(hDlg, IDC_SLIDER_SHIFT);

                wsprintf(tipbuf, "Shift: %s", buf);
                ti.lpszText = tipbuf;

                SendMessage(g_hTooltip, TTM_UPDATETIPTEXT, 0, (LPARAM)&ti);
            }
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

        case IDCANCEL:
            EndDialog(hDlg, 0);
            break;
        }
        break;

    case WM_CLOSE:
        EndDialog(hDlg, 0);
        break;
    }

    return FALSE;
}

/* ============================================================
 * 保存ダイアログ
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
    ofn.lpstrFilter  = "MML Files (*.mml)\0*.mml\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile    = outpath;
    ofn.nMaxFile     = outsize;
    ofn.lpstrInitialDir = initdir;
    ofn.lpstrDefExt  = "mml";
    ofn.Flags        = OFN_OVERWRITEPROMPT;

    return GetSaveFileName(&ofn);
}

/* ============================================================
 * ファイル読み込み（GUIErrorCode 版）
 * ============================================================ */
GUIErrorCode LoadFile(const char* path, char** outbuf, DWORD* outsize, DWORD* out_filesize)
{
    HANDLE hFile;
    DWORD size;
    char* buf;
    DWORD read;

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
        read != size || read == 0) {

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
 * ファイル保存（GUIErrorCode 版）
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
 * AutoSave 用ファイル名生成
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

    if (dch_flag) {
        lstrcat(modebuf, "_d");
    }

    _snprintf(out, outsize, "%s%s%s_%s_%s.mml",
              drive, dir, fname, shiftbuf, modebuf);

    out[outsize - 1] = '\0';
}

