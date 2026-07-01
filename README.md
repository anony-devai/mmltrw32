# **MML Transposer Win32 GUI (MMLTRW32)**

A Win32 application edition of the MML Transposer tool.  
It transposes plain‑text MML source files used in NSF (NES Sound Format) music creation.  
This program does not process NSF data directly.

This edition uses the same 32‑bit engine `mmleng32.c` as the Win32 CUI version.

---

## Supported Environment

- Windows 95 and later Win32 GUI environments  
- Verified on Windows 95 (Virtual PC 2007), Windows 7, and Windows 10  
  (compatibility mode not required)

---

## Menu Structure (Alt Key)

Pressing **Alt** displays the application menu.  
The menu hides automatically when focus is lost.

### **File**
- **Open** — Open an MML file  
- **Exit** — Quit the application

### **Language**
- **English**  
- **Japanese** — Switches part of the UI and error messages to Japanese

---

## Basic Operation

MMLTRW32 operates in the following sequence:

### **1. Set the transpose amount (Shift) using the slider**
- Default is `0` (no transpose)  
- The current value is shown next to **Key:**  

### **2. Select options using checkboxes**
- **FMT** — formatted output (Pure mode when OFF)  
- **Rel** — relative octave mode  
- **Abs** — absolute octave mode  
- **D‑ch** — transpose D‑channel (noise channel included)

**Rel** and **Abs** are mutually exclusive.  
When both are OFF, octave notation (`oX` / `<>`) is automatically adjusted as needed.

### **3. Drag & Drop an MML file onto the window**
- If **Auto** is ON → automatic conversion and automatic save  
- If **Auto** is OFF → **Quick** and **Save** buttons become available

---

## Auto Save

### **Auto ON**
Drag & Drop triggers immediate conversion and automatic saving.  
The output filename is generated automatically:

```
<original>_<shift>_<mode>[_d].mml
```

Example:

```
input_+5_fmt_rel_d.mml
```

### **Auto OFF**
After Drag & Drop, **Quick** and **Save** become available.

---

## Quick Save

Saves the output immediately using the automatically generated filename.

---

## Save (Manual Save)

Opens a save dialog and allows choosing any filename.

---

## GUI Options (Checkboxes)

- **FMT** — formatted output (equivalent to CUI `-f`)  
  - OFF = Pure mode (no formatting)  
- **Rel** — relative octave mode (equivalent to CUI `-r`)  
- **Abs** — absolute octave mode (equivalent to CUI `-a`)  
  - Rel and Abs are mutually exclusive  
- **D‑ch** — transpose D‑channel (equivalent to CUI `-d`)

Pure/FMT, Rel/Abs, and D‑ch are independent settings.

---

## Example Workflow

1. Set Shift to **+5**  
2. Turn **FMT** and **Rel** ON  
3. Turn **D‑ch** ON  
4. Enable **Auto**  
5. Drag & Drop `input.mml`  
→ Output file `input_+5_fmt_rel_d.mml` is generated and saved automatically

---

## Notes

- This GUI edition uses the same engine and conversion logic as the Win32 CUI version.  
- UI layout is defined in `mmltrw32.rc`.

All source code in this project was created with assistance from Copilot.  
Unexpected issues may occur.
