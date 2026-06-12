// mml_trw32.c  (Auto-save 特化版 EXE, C89 対応, 新エンジン対応)

#include <windows.h>
#include <commdlg.h>
#include <commctrl.h>
#include <shellapi.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "resource.h"
#include "mmleng32.h"          // ← 修正：mml_engine_32.h → mmleng32.h

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

// --- グローバル ---
static char input_path[MAX_PATH] = "";
static int  g_shift    = 0;
static int  g_mode     = MODE_PURE;       // GUI 初期値は Pure（000）
static int  g_autosave = 1;               // Auto-save ON がデフォルト
static HWND g_hTooltip = NULL;
static HFONT g_hFont   = NULL;
static TOOLINFO ti;                       // ツールチップ用はこれ 1 個で統一

// --- プロトタイプ ---
BOOL CALLBACK DlgProc(HWND, UINT, WPARAM, LPARAM);
void InitShiftControls(HWND);
BOOL SaveFileDialog(HWND, char*, int);
BOOL LoadFile(const char*, char**, DWORD*);
BOOL SaveFile(const char*, const char*, DWORD);
void MakeAutoSaveName(const char*, int, int, char*, int);

static void UpdateModeFromUI(HWND hDlg);
static void DoConvertAndSave(HWND, const char*, BOOL);

// --- ファイル名だけ抽出 ---
static const char* ExtractFileName(const char* path)
{
    const char* p;

    p = strrchr(path, '\\');
    if (p) return p + 1;
    p = strrchr(path, '/');
    if (p) return p + 1;
    return path;
}

// --- WinMain ---
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nShow)
{
    InitCommonControls();
    return DialogBoxParam(hInst, MAKEINTRESOURCE(IDD_MAIN_DIALOG), NULL, DlgProc, 0);
}

// --- Shift 関連初期化 ---
void InitShiftControls(HWND hDlg)
{
    HWND hSpin = GetDlgItem(hDlg, IDC_SPIN_SHIFT);

    SendMessage(hSpin, UDM_SETRANGE, 0, MAKELPARAM(12, -12));
    SendMessage(hSpin, UDM_SETPOS, 0, MAKELPARAM(g_shift, 0));

    SetDlgItemInt(hDlg, IDC_EDIT_SHIFT, g_shift, TRUE);
}

// --- モード決定（D-ch フラグ対応） ---
static void UpdateModeFromUI(HWND hDlg)
{
    int fmt = (IsDlgButtonChecked(hDlg, IDC_CHECK_FORMAT) == BST_CHECKED);
    int rel = (IsDlgButtonChecked(hDlg, IDC_CHECK_REL)    == BST_CHECKED);
    int abs = (IsDlgButtonChecked(hDlg, IDC_CHECK_ABS)    == BST_CHECKED);
    int dch = (IsDlgButtonChecked(hDlg, IDC_CHECK_DSHIFT) == BST_CHECKED);  // ← 新規

    /* Rel / Abs は排他（両方 ON の場合は Abs を落とす） */
    if (rel && abs) {
        abs = 0;
    }

    /* 3bit 構造：bit2=FMT, bit1=REL, bit0=ABS */
    g_mode = (fmt ? 4 : 0) | (rel ? 2 : 0) | (abs ? 1 : 0);

    /* D-ch ビット（bit8）を追加 */
    if (dch) {
        g_mode |= 8;
    }

    /* 念のため、存在しない 3 や 7 には落ちないようにしておく（GUI からは来ない想定） */
    if (g_mode == 3 || g_mode == 7) {
        g_mode = MODE_PURE;
    }
}

// --- 変換＆保存（エラーハンドリング強化版） ---
static void DoConvertAndSave(HWND hDlg, const char* inpath, BOOL use_autosave)
{
    char* inbuf = NULL;
    DWORD insize = 0;
    int outsize;
    char* outbuf;
    int outlen;
    char outpath[MAX_PATH];
    MMLErrorInfo err_info;      // ← 新規：エラー情報構造体

    if (!LoadFile(inpath, &inbuf, &insize)) {
        MessageBox(hDlg, "入力ファイルを読み込めません。", "エラー", MB_ICONERROR);
        return;
    }

    outsize = (int)insize * 2 + 1024;
    outbuf = (char*)malloc(outsize);
    if (!outbuf) {
        free(inbuf);
        MessageBox(hDlg, "メモリ確保に失敗しました。", "エラー", MB_ICONERROR);
        return;
    }

    memset(&err_info, 0, sizeof(err_info));  // ← 新規：初期化

    /* 新エンジン API：6引数（第6引数に &err_info） */
    outlen = mml_process(inbuf, g_shift, g_mode, outbuf, outsize, &err_info);
    free(inbuf);

    /* エラーハンドリング強化版 */
    if (outlen < 0) {
        char errmsg[512];

        switch (outlen) {
        case MML_ERR_OCTAVE_OUT_OF_RANGE:
            wsprintf(errmsg,
                "移調により各音源のオクターブ限界を突破しました。\n"
                "発生場所: チャンネル '%c', %d 行目\n"
                "計算値: o%d",
                err_info.channel_char, err_info.line_number, err_info.calculated_value);
            break;

        case MML_ERR_NULL_INPUT:
            lstrcpy(errmsg, "入力テキストが NULL です。");
            break;

        case MML_ERR_EMPTY_INPUT:
            lstrcpy(errmsg, "入力テキストが空です。");
            break;

        case MML_ERR_OUTBUF:
            lstrcpy(errmsg, "出力バッファサイズが不足しています。");
            break;

        case MML_ERR_BAD_SHIFT:
            lstrcpy(errmsg, "移調量が範囲外です（-12～+12）。");
            break;

        case MML_ERR_BAD_MODE:
            lstrcpy(errmsg, "モードが無効です。");
            break;

        default:
            wsprintf(errmsg, "MML 処理に失敗しました。(コード %d)", outlen);
            break;
        }

        free(outbuf);
        MessageBox(hDlg, errmsg, "エラー", MB_ICONERROR);
        return;
    }

    if (outlen >= (int)outsize) {
        free(outbuf);
        MessageBox(hDlg, "出力バッファが不足しています。", "エラー", MB_ICONERROR);
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

    if (!SaveFile(outpath, outbuf, (DWORD)outlen)) {
        MessageBox(hDlg, "保存に失敗しました。", "エラー", MB_ICONERROR);
    }

    free(outbuf);
}

// --- フォント適用 ---
BOOL CALLBACK SetChildFontProc(HWND hWnd, LPARAM lParam)
{
    HFONT hFont = (HFONT)lParam;
    SendMessage(hWnd, WM_SETFONT, (WPARAM)hFont, TRUE);
    return TRUE;
}

// --- ダイアログプロシージャ ---
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

        // --- フォント設定 ---
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

        // --- ツールチップ作成 ---
        g_hTooltip = CreateWindowEx(
            0, TOOLTIPS_CLASS, NULL,
            WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
            CW_USEDEFAULT, CW_USEDEFAULT,
            CW_USEDEFAULT, CW_USEDEFAULT,
            hDlg, NULL, GetModuleHandle(NULL), NULL);

        if (g_hTooltip)
        {
            // ファイル名ラベル
            ZeroMemory(&ti, sizeof(ti));
            ti.cbSize = sizeof(ti);
            ti.uFlags = TTF_SUBCLASS | TTF_IDISHWND;
            ti.hwnd   = hDlg;
            ti.uId    = (UINT)GetDlgItem(hDlg, IDC_STATIC_FILENAME);
            ti.lpszText = "No file selected";
            SendMessage(g_hTooltip, TTM_ADDTOOL, 0, (LPARAM)&ti);

            // スライダー
            ZeroMemory(&ti, sizeof(ti));
            ti.cbSize = sizeof(ti);
            ti.uFlags = TTF_SUBCLASS | TTF_IDISHWND;
            ti.hwnd   = hDlg;
            ti.uId    = (UINT)GetDlgItem(hDlg, IDC_SLIDER_SHIFT);
            ti.lpszText = "Shift: 0";
            SendMessage(g_hTooltip, TTM_ADDTOOL, 0, (LPARAM)&ti);

            // Key: ラベル
            ZeroMemory(&ti, sizeof(ti));
            ti.cbSize = sizeof(ti);
            ti.uFlags = TTF_SUBCLASS | TTF_IDISHWND;
            ti.hwnd   = hDlg;
            ti.uId    = (UINT)GetDlgItem(hDlg, IDC_STATIC_KEY);
            ti.lpszText = "Semitone shift";
            SendMessage(g_hTooltip, TTM_ADDTOOL, 0, (LPARAM)&ti);

            // Key 数値（読み取り専用 EDIT）
            ZeroMemory(&ti, sizeof(ti));
            ti.cbSize = sizeof(ti);
            ti.uFlags = TTF_SUBCLASS | TTF_IDISHWND;
            ti.hwnd   = hDlg;
            ti.uId    = (UINT)GetDlgItem(hDlg, IDC_EDIT_SHIFT);
            ti.lpszText = "Current semitone";
            SendMessage(g_hTooltip, TTM_ADDTOOL, 0, (LPARAM)&ti);

            // Key スピン
            ZeroMemory(&ti, sizeof(ti));
            ti.cbSize = sizeof(ti);
            ti.uFlags = TTF_SUBCLASS | TTF_IDISHWND;
            ti.hwnd   = hDlg;
            ti.uId    = (UINT)GetDlgItem(hDlg, IDC_SPIN_SHIFT);
            ti.lpszText = "Adjust semitone";
            SendMessage(g_hTooltip, TTM_ADDTOOL, 0, (LPARAM)&ti);

            // Quick Save
            ZeroMemory(&ti, sizeof(ti));
            ti.cbSize = sizeof(ti);
            ti.uFlags = TTF_SUBCLASS | TTF_IDISHWND;
            ti.hwnd   = hDlg;
            ti.uId    = (UINT)GetDlgItem(hDlg, IDC_BUTTON_SAVE_AUTO);
            ti.lpszText = "Save now (auto name)";
            SendMessage(g_hTooltip, TTM_ADDTOOL, 0, (LPARAM)&ti);

            // Save As
            ZeroMemory(&ti, sizeof(ti));
            ti.cbSize = sizeof(ti);
            ti.uFlags = TTF_SUBCLASS | TTF_IDISHWND;
            ti.hwnd   = hDlg;
            ti.uId    = (UINT)GetDlgItem(hDlg, IDC_BUTTON_SAVE_AS);
            ti.lpszText = "Save as...";
            SendMessage(g_hTooltip, TTM_ADDTOOL, 0, (LPARAM)&ti);

            // Option: ラベル
            ZeroMemory(&ti, sizeof(ti));
            ti.cbSize = sizeof(ti);
            ti.uFlags = TTF_SUBCLASS | TTF_IDISHWND;
            ti.hwnd   = hDlg;
            ti.uId    = (UINT)GetDlgItem(hDlg, IDC_STATIC_OPTION);
            ti.lpszText = "Output options";
            SendMessage(g_hTooltip, TTM_ADDTOOL, 0, (LPARAM)&ti);

            // --- Rel ---
            ZeroMemory(&ti, sizeof(ti));
            ti.cbSize = sizeof(ti);
            ti.uFlags = TTF_SUBCLASS | TTF_IDISHWND;
            ti.hwnd   = hDlg;
            ti.uId    = (UINT)GetDlgItem(hDlg, IDC_CHECK_REL);
            ti.lpszText = "Relative octave (< >)";
            SendMessage(g_hTooltip, TTM_ADDTOOL, 0, (LPARAM)&ti);

            // --- Abs ---
            ZeroMemory(&ti, sizeof(ti));
            ti.cbSize = sizeof(ti);
            ti.uFlags = TTF_SUBCLASS | TTF_IDISHWND;
            ti.hwnd   = hDlg;
            ti.uId    = (UINT)GetDlgItem(hDlg, IDC_CHECK_ABS);
            ti.lpszText = "Absolute octave (oX)";
            SendMessage(g_hTooltip, TTM_ADDTOOL, 0, (LPARAM)&ti);

            // --- Format ---
            ZeroMemory(&ti, sizeof(ti));
            ti.cbSize = sizeof(ti);
            ti.uFlags = TTF_SUBCLASS | TTF_IDISHWND;
            ti.hwnd   = hDlg;
            ti.uId    = (UINT)GetDlgItem(hDlg, IDC_CHECK_FORMAT);
            ti.lpszText = "Format output";
            SendMessage(g_hTooltip, TTM_ADDTOOL, 0, (LPARAM)&ti);

            // --- D-ch（新規） ---
            ZeroMemory(&ti, sizeof(ti));
            ti.cbSize = sizeof(ti);
            ti.uFlags = TTF_SUBCLASS | TTF_IDISHWND;
            ti.hwnd   = hDlg;
            ti.uId    = (UINT)GetDlgItem(hDlg, IDC_CHECK_DSHIFT);
            ti.lpszText = "D-channel noise shift";
            SendMessage(g_hTooltip, TTM_ADDTOOL, 0, (LPARAM)&ti);

            // D&D Auto Save
            ZeroMemory(&ti, sizeof(ti));
            ti.cbSize = sizeof(ti);
            ti.uFlags = TTF_SUBCLASS | TTF_IDISHWND;
            ti.hwnd   = hDlg;
            ti.uId    = (UINT)GetDlgItem(hDlg, IDC_CHECK_AUTOSAVE);
            ti.lpszText = "Auto-save on drop";
            SendMessage(g_hTooltip, TTM_ADDTOOL, 0, (LPARAM)&ti);
        }

        // --- 初期状態 ---
        CheckDlgButton(hDlg, IDC_CHECK_REL,     BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_CHECK_ABS,     BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_CHECK_FORMAT,  BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_CHECK_DSHIFT,  BST_UNCHECKED);  // ← 新規

        // Auto-save ON（デフォルト）
        CheckDlgButton(hDlg, IDC_CHECK_AUTOSAVE, BST_CHECKED);
        g_autosave = 1;

        // Auto ON → Save Auto / Save As は無効
        EnableWindow(GetDlgItem(hDlg, IDC_BUTTON_SAVE_AUTO), FALSE);
        EnableWindow(GetDlgItem(hDlg, IDC_BUTTON_SAVE_AS),   FALSE);

        // スピンのバディ設定
        hEdit = GetDlgItem(hDlg, IDC_EDIT_SHIFT);
        hSpin = GetDlgItem(hDlg, IDC_SPIN_SHIFT);
        SendMessage(hSpin, UDM_SETBUDDY, (WPARAM)hEdit, 0);

        // スライダー初期化
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
            // --- 拡張子チェック ---
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

            // --- 正常ファイル ---
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
                // 手動保存モード
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
                MessageBox(hDlg, "No input file.", "Error", MB_OK | MB_ICONERROR);
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
                MessageBox(hDlg, "No input file.", "Error", MB_OK | MB_ICONERROR);
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

        // ★ Rel / Abs / Format / D-ch のチェック動作
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
            // FORMAT は独立動作なので特に何もしない
            break;

        case IDC_CHECK_DSHIFT:
            // D-ch は独立動作なので特に何もしない（mode に反映されるのは UpdateModeFromUI で）
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

/*
// --- ファイル選択ダイアログ ---
// （将来復活する可能性があるためコメントのまま残す）
BOOL OpenFileDialog(HWND hDlg)
{
    OPENFILENAME ofn;
    char path[MAX_PATH];

    ZeroMemory(&ofn, sizeof(ofn));
    ZeroMemory(path, sizeof(path));

    ofn.lStructSize  = OPENFILENAME_SIZE_VERSION_400;
    ofn.hwndOwner    = hDlg;
    ofn.lpstrFilter  = "MML Files (*.mml)\0*.mml\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile    = path;
    ofn.nMaxFile     = MAX_PATH;
    ofn.Flags        = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if (GetOpenFileName(&ofn)) {
        lstrcpy(input_path, path);
        return TRUE;
    }
    return FALSE;
}
*/

// --- 保存ダイアログ ---
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

// --- ファイル読み込み ---
BOOL LoadFile(const char* path, char** outbuf, DWORD* outsize)
{
    HANDLE hFile;
    DWORD size;
    char* buf;
    DWORD read;

    hFile = CreateFile(path, GENERIC_READ, FILE_SHARE_READ,
                       NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return FALSE;

    size = GetFileSize(hFile, NULL);
    if (size == INVALID_FILE_SIZE) {
        CloseHandle(hFile);
        return FALSE;
    }

    /* ★ 空ファイルはエラー扱い */
    if (size == 0) {
        CloseHandle(hFile);
        return FALSE;
    }

    buf = (char*)malloc(size + 1);
    if (!buf) {
        CloseHandle(hFile);
        return FALSE;
    }

    read = 0;
    if (!ReadFile(hFile, buf, size, &read, NULL) ||
        read != size || read == 0) {

        free(buf);
        CloseHandle(hFile);
        return FALSE;
    }

    buf[size] = '\0';
    CloseHandle(hFile);

    *outbuf  = buf;
    *outsize = size;
    return TRUE;
}

// --- ファイル保存 ---
BOOL SaveFile(const char* path, const char* data, DWORD size)
{
    FILE* fp;
    size_t written;

    fp = fopen(path, "w");
    if (!fp)
        return FALSE;

    written = fwrite(data, 1, size, fp);
    fclose(fp);

    return (written == size);
}

// --- AutoSave 用ファイル名生成（新モード定数対応） ---
void MakeAutoSaveName(const char* inpath, int shift, int mode,
                      char* out, int outsize)
{
    char drive[_MAX_DRIVE];
    char dir[_MAX_DIR];
    char fname[_MAX_FNAME];
    char ext[_MAX_EXT];
    char shiftbuf[16];
    char modebuf[32];
    int base_mode = mode & 7;  // bit0-2 のみ（bit8 は反映しない）
    int dch_flag = (mode & 8) ? 1 : 0;

    _splitpath(inpath, drive, dir, fname, ext);

    wsprintf(shiftbuf, shift == 0 ? "0" : "%c%d",
             shift > 0 ? '+' : '-',
             shift > 0 ? shift : -shift);

    switch (base_mode) {
        case MODE_PURE:       lstrcpy(modebuf, "pure");     break;  /* 000 */
        case MODE_PURE_ABS:   lstrcpy(modebuf, "pure_abs"); break;  /* 001 */
        case MODE_PURE_REL:   lstrcpy(modebuf, "pure_rel"); break;  /* 010 */
        case MODE_FMT:        lstrcpy(modebuf, "fmt");      break;  /* 100 */
        case MODE_FMT_ABS:    lstrcpy(modebuf, "fmt_abs");  break;  /* 101 */
        case MODE_FMT_REL:    lstrcpy(modebuf, "fmt_rel");  break;  /* 110 */
        default:              lstrcpy(modebuf, "pure");     break;
    }

    /* D-ch フラグが ON なら suffix に "_d" を追加 */
    if (dch_flag) {
        lstrcat(modebuf, "_d");
    }

    _snprintf(out, outsize, "%s%s%s_%s_%s.mml",
              drive, dir, fname, shiftbuf, modebuf);

    out[outsize - 1] = '\0';
}
