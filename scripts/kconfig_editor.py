#!/usr/bin/env python3
"""
scripts/kconfig_editor.py — Ark Kernel Configuration Editor

Reads/writes .kconfig and regenerates include/ark/kconfig.h via
gen_kconfig_h.py.  Runs make after saving if the user requests it.

Usage:
    python3 scripts/kconfig_editor.py [path/to/.kconfig]
"""

import os, sys, subprocess, tkinter as tk
from tkinter import ttk, messagebox, filedialog

# ── Schema ─────────────────────────────────────────────────────────────────
# Each entry: (key, type, default, label, tooltip)
# type: 'bool' | 'int' | 'str' | 'choice'
# For choice: default is a list, first element is the default value

SCHEMA = [
    # ── Architecture ────────────────────────────────────────────────────
    ("Architecture", None, None, None, None),
    ("ARCH",        "choice", ["x86_64","x86"],           "Architecture",       "Target CPU architecture"),
    ("OPT_LEVEL",   "choice", ["2","0","1","3","s"],       "Optimisation level", "-O level passed to GCC"),
    ("DEBUG",       "bool",   "1",                         "Debug build",        "Adds -g, disables some opts"),
    ("WERROR",      "bool",   "0",                         "Warnings as errors", "-Werror flag"),
    ("CODENAME",    "str",    "allyes",                    "Codename",           "Build codename string"),
    ("INIT_BIN",    "str",    "/init",                     "Init binary path",   "Path to /init inside initramfs"),

    # ── Serial / Printk ─────────────────────────────────────────────────
    ("Serial / Printk", None, None, None, None),
    ("PRINTK_ENABLE","bool",  "1",                         "Enable printk",      "Kernel log system"),
    ("SERIAL_ENABLE","bool",  "1",                         "Enable serial",      "COM1 serial output"),
    ("SERIAL_PORT",  "int",   "1016",                      "Serial port (hex)",  "COM1=0x3F8=1016"),
    ("LOGLEVEL",     "int",   "7",                         "Log level (0-7)",    "7=debug, 4=info, 0=silent"),

    # ── Memory ──────────────────────────────────────────────────────────
    ("Memory", None, None, None, None),
    ("PMM_ENABLE",   "bool",  "1",                         "Physical MM",        "Physical memory manager"),
    ("VMM_ENABLE",   "bool",  "1",                         "Virtual MM",         "Virtual memory manager"),
    ("HEAP_SIZE_KB", "int",   "4096",                      "Heap size (KB)",     "Kernel heap size"),
    ("STACK_SIZE_KB","int",   "64",                        "Stack size (KB)",    "Boot stack size"),

    # ── Framebuffer ─────────────────────────────────────────────────────
    ("Framebuffer", None, None, None, None),
    ("FB_ENABLE",   "bool",   "1",                         "Enable framebuffer", "Linear framebuffer console"),
    ("FB_WIDTH",    "int",    "1024",                      "Width (px)",         "Framebuffer width in pixels"),
    ("FB_HEIGHT",   "int",    "768",                       "Height (px)",        "Framebuffer height in pixels"),
    ("FB_BPP",      "choice", ["32","16","24"],             "Bits per pixel",     "Colour depth"),
    ("FB_DRIVER",   "choice", ["bga","vesa","vbe"],         "FB driver",          "Framebuffer driver backend"),

    # ── Interrupts ──────────────────────────────────────────────────────
    ("Interrupts / I/O", None, None, None, None),
    ("IDT_ENABLE",  "bool",   "1",                         "IDT",                "Interrupt descriptor table"),
    ("PIC_ENABLE",  "bool",   "1",                         "PIC",                "8259A PIC support"),
    ("IOAPIC_ENABLE","bool",  "1",                         "I/O APIC",           "I/O APIC support"),
    ("NMI_ENABLE",  "bool",   "1",                         "NMI handler",        "Non-maskable interrupt handler"),
    ("IRQ_STACK",   "bool",   "1",                         "IRQ stack",          "Separate IRQ stack"),
    ("PIO_ENABLE",  "bool",   "1",                         "Port I/O",           "x86 IN/OUT instructions"),
    ("MMIO_ENABLE", "bool",   "1",                         "MMIO",               "Memory-mapped I/O"),

    # ── Scheduler ───────────────────────────────────────────────────────
    ("Scheduler", None, None, None, None),
    ("SCHED_ENABLE",       "bool", "1",    "Preemptive scheduler",   "Enable task scheduler"),
    ("SCHED_PREEMPT",      "bool", "1",    "Preemption",             "Allow preemptive task switches"),
    ("SCHED_PIT_HZ",       "int",  "100",  "PIT tick rate (Hz)",     "IRQ0 frequency. 100=10ms tick, 250=4ms tick. Affects all timing."),
    ("SCHED_TIMESLICE_MS", "int",  "10",   "Timeslice (ms)",         "Time each task runs before preemption"),
    ("SCHED_MAX_TASKS",    "int",  "64",   "Max tasks",              "Maximum concurrent tasks"),
    ("SCHED_STACK_KB",     "int",  "16",   "Task stack (KB)",        "Per-task kernel stack size"),
    ("SCHED_JOB_CONTROL",  "bool", "1",    "Job control",            "Shell job control (fg/bg)"),

    # ── USB ─────────────────────────────────────────────────────────────
    ("USB", None, None, None, None),
    ("USB_ENABLE",  "bool",   "1",                         "USB support",        "USB host controller stack"),
    ("USB_XHCI",    "bool",   "1",                         "xHCI (USB3)",        "Extensible Host Controller"),
    ("USB_EHCI",    "bool",   "1",                         "EHCI (USB2)",        "Enhanced Host Controller"),
    ("USB_UHCI",    "bool",   "1",                         "UHCI (USB1.1)",      "Universal Host Controller"),
    ("USB_HID",     "bool",   "1",                         "USB HID",            "USB keyboard/mouse"),

    # ── Storage ─────────────────────────────────────────────────────────
    ("Storage", None, None, None, None),
    ("ATA_ENABLE",  "bool",   "1",                         "ATA/IDE",            "ATA disk support"),
    ("ATA_DMA",     "bool",   "1",                         "ATA DMA",            "DMA mode for ATA"),
    ("SATA_ENABLE", "bool",   "1",                         "SATA/AHCI",          "AHCI SATA support"),
    ("SD_ENABLE",   "bool",   "1",                         "SD card",            "SD/MMC support"),
    ("FAT32_ENABLE","bool",   "1",                         "FAT32",              "FAT32 filesystem"),
    ("RAMFS_ENABLE","bool",   "1",                         "RamFS",              "In-memory filesystem"),
    ("VFS_ENABLE",  "bool",   "1",                         "VFS",                "Virtual filesystem layer"),
    ("ZIP_ENABLE",  "bool",   "1",                         "ZIP/initrd",         "ZIP initramfs support"),

    # ── Networking ──────────────────────────────────────────────────────
    ("Networking", None, None, None, None),
    ("NET_ENABLE",  "bool",   "1",                         "Networking",         "Network stack"),
    ("E1000_ENABLE","bool",   "1",                         "Intel e1000",        "Intel e1000 NIC driver"),
    ("E100_ENABLE", "bool",   "1",                         "Intel e100",         "Intel e100 NIC driver"),
    ("IP_ENABLE",   "bool",   "1",                         "IPv4",               "IP layer"),
    ("UDP_ENABLE",  "bool",   "1",                         "UDP",                "UDP sockets"),
    ("TCP_ENABLE",  "bool",   "1",                         "TCP",                "TCP sockets"),

    # ── Audio ───────────────────────────────────────────────────────────
    ("Audio", None, None, None, None),
    ("AUDIO_ENABLE","bool",   "1",                         "Audio",              "Audio subsystem"),
    ("AC97_ENABLE", "bool",   "1",                         "AC97",               "AC97 codec driver"),
    ("HDA_ENABLE",  "bool",   "1",                         "Intel HDA",          "High Definition Audio"),

    # ── HID ─────────────────────────────────────────────────────────────
    ("HID / Input", None, None, None, None),
    ("KBD_ENABLE",  "bool",   "1",                         "Keyboard",           "PS/2 keyboard driver"),
    ("MOUSE_ENABLE","bool",   "1",                         "Mouse",              "PS/2 mouse driver"),
    ("TOUCH_ENABLE","bool",   "0",                         "Touch",              "Touchscreen support"),

    # ── GPU ─────────────────────────────────────────────────────────────
    ("GPU", None, None, None, None),
    ("GPU_ENABLE",  "bool",   "1",                         "GPU subsystem",      "GPU driver framework"),
    ("VESA_ENABLE", "bool",   "1",                         "VESA/VBE",           "VESA framebuffer driver"),

    # ── PCI ─────────────────────────────────────────────────────────────
    ("PCI", None, None, None, None),
    ("PCI_ENABLE",    "bool", "1",                         "PCI bus",            "PCI bus enumeration"),
    ("PCI_PROBE_ALL", "bool", "1",                         "Probe all buses",    "Scan all 256 PCI buses"),

    # ── Syscalls ────────────────────────────────────────────────────────
    ("Syscalls / ELF", None, None, None, None),
    ("SYSCALL_ENABLE","bool", "1",                         "Syscalls",           "System call interface"),
    ("ELF_LOADER",   "bool",  "1",                         "ELF loader",         "Execute ELF binaries"),

    # ── Debug ───────────────────────────────────────────────────────────
    ("Debugging", None, None, None, None),
    ("DEBUG_VERBOSE",   "bool","1",                        "Verbose debug",      "Extra debug output"),
    ("DEBUG_KASAN",     "bool","1",                        "KASAN",              "Kernel address sanitiser"),
    ("DEBUG_PANIC_DUMP","bool","1",                        "Panic dump",         "Register dump on panic"),
]

BOOL_KEYS  = {e[0] for e in SCHEMA if e[1] == 'bool'}
INT_KEYS   = {e[0] for e in SCHEMA if e[1] == 'int'}
STR_KEYS   = {e[0] for e in SCHEMA if e[1] == 'str'}
CHOICE_KEYS= {e[0]: e[2] for e in SCHEMA if e[1] == 'choice'}

# ── File I/O ───────────────────────────────────────────────────────────────

def read_kconfig(path: str) -> dict:
    cfg = {}
    if not os.path.exists(path):
        return cfg
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            k, v = line.split("=", 1)
            cfg[k.strip()] = v.strip()
    return cfg


def write_kconfig(path: str, cfg: dict) -> None:
    # Group by schema sections for readable output
    groups: list[tuple[str, list]] = []
    cur_group = "General"
    cur_keys: list[str] = []
    for entry in SCHEMA:
        if entry[1] is None:
            if cur_keys:
                groups.append((cur_group, cur_keys))
            cur_group = entry[0]
            cur_keys = []
        else:
            cur_keys.append(entry[0])
    if cur_keys:
        groups.append((cur_group, cur_keys))

    all_schema_keys = {e[0] for e in SCHEMA if e[1] is not None}
    extra = {k: v for k, v in cfg.items() if k not in all_schema_keys}

    lines = ["# Ark kernel configuration", "# Edited by kconfig_editor.py", ""]
    for group, keys in groups:
        lines.append(f"# ── {group} " + "─" * max(1, 50 - len(group)))
        for k in keys:
            if k in cfg:
                lines.append(f"{k}={cfg[k]}")
        lines.append("")
    if extra:
        lines.append("# ── Extra keys (not in schema)")
        for k, v in extra.items():
            lines.append(f"{k}={v}")
        lines.append("")

    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))


# ── GUI ────────────────────────────────────────────────────────────────────

DARK_BG   = "#1e1e2e"
PANEL_BG  = "#24273a"
ACCENT    = "#89b4fa"   # blue
GREEN     = "#a6e3a1"
RED       = "#f38ba8"
TEXT      = "#cdd6f4"
MUTED     = "#6c7086"
HEADER_BG = "#313244"
SECTION_FG= "#cba6f7"   # lavender
ENTRY_BG  = "#1e1e2e"
SEL_BG    = "#363653"


class Tooltip:
    def __init__(self, widget, text):
        self.widget = widget
        self.text   = text
        self.tip    = None
        widget.bind("<Enter>", self.show)
        widget.bind("<Leave>", self.hide)

    def show(self, _=None):
        if not self.text:
            return
        x = self.widget.winfo_rootx() + 20
        y = self.widget.winfo_rooty() + self.widget.winfo_height() + 4
        self.tip = tw = tk.Toplevel(self.widget)
        tw.wm_overrideredirect(True)
        tw.wm_geometry(f"+{x}+{y}")
        lbl = tk.Label(tw, text=self.text, bg="#2a2a3e", fg=TEXT,
                        relief="solid", bd=1, padx=6, pady=3,
                        font=("Consolas", 9), wraplength=320, justify="left")
        lbl.pack()

    def hide(self, _=None):
        if self.tip:
            self.tip.destroy()
            self.tip = None


class KconfigEditor:
    def __init__(self, root: tk.Tk, kconfig_path: str):
        self.root = root
        self.path = kconfig_path
        self.root_dir = os.path.dirname(os.path.dirname(os.path.abspath(kconfig_path)))
        self.widgets: dict[str, tk.Variable] = {}
        self.dirty = False

        root.title(f"Ark Kernel Config — {os.path.basename(kconfig_path)}")
        root.configure(bg=DARK_BG)
        root.geometry("920x780")
        root.minsize(720, 500)

        self._build_ui()
        self._load()

    # ── UI construction ────────────────────────────────────────────────

    def _build_ui(self):
        root = self.root

        # ── Top bar ─────────────────────────────────────────────────────
        top = tk.Frame(root, bg=HEADER_BG, pady=6, padx=12)
        top.pack(fill="x", side="top")

        tk.Label(top, text="⚙  Ark Kernel Configuration", bg=HEADER_BG,
                 fg=ACCENT, font=("Consolas", 14, "bold")).pack(side="left")

        self.status_var = tk.StringVar(value="Ready")
        tk.Label(top, textvariable=self.status_var, bg=HEADER_BG,
                 fg=MUTED, font=("Consolas", 9)).pack(side="right", padx=8)

        # ── Search bar ──────────────────────────────────────────────────
        search_frame = tk.Frame(root, bg=PANEL_BG, pady=4, padx=12)
        search_frame.pack(fill="x")
        tk.Label(search_frame, text="🔍 Filter:", bg=PANEL_BG,
                 fg=MUTED, font=("Consolas", 9)).pack(side="left")
        self.search_var = tk.StringVar()
        self.search_var.trace_add("write", self._on_search)
        search_entry = tk.Entry(search_frame, textvariable=self.search_var,
                                bg=ENTRY_BG, fg=TEXT, insertbackground=TEXT,
                                relief="flat", font=("Consolas", 10), width=30)
        search_entry.pack(side="left", padx=6, ipady=3)
        tk.Button(search_frame, text="✕", bg=PANEL_BG, fg=MUTED,
                  relief="flat", cursor="hand2",
                  command=lambda: self.search_var.set("")).pack(side="left")

        # ── Scrollable content ───────────────────────────────────────────
        outer = tk.Frame(root, bg=DARK_BG)
        outer.pack(fill="both", expand=True, padx=0)

        canvas = tk.Canvas(outer, bg=DARK_BG, highlightthickness=0)
        vsb    = ttk.Scrollbar(outer, orient="vertical", command=canvas.yview)
        canvas.configure(yscrollcommand=vsb.set)
        vsb.pack(side="right", fill="y")
        canvas.pack(side="left", fill="both", expand=True)

        self.inner = tk.Frame(canvas, bg=DARK_BG)
        self.canvas_win = canvas.create_window((0, 0), window=self.inner,
                                               anchor="nw")

        def _resize(e):
            canvas.itemconfig(self.canvas_win, width=e.width)
        canvas.bind("<Configure>", _resize)
        self.inner.bind("<Configure>",
            lambda e: canvas.configure(scrollregion=canvas.bbox("all")))
        canvas.bind_all("<MouseWheel>",
            lambda e: canvas.yview_scroll(-1*(e.delta//120), "units"))
        canvas.bind_all("<Button-4>",
            lambda e: canvas.yview_scroll(-1, "units"))
        canvas.bind_all("<Button-5>",
            lambda e: canvas.yview_scroll(1, "units"))

        self.canvas = canvas
        self._build_rows()

        # ── Bottom button bar ────────────────────────────────────────────
        bot = tk.Frame(root, bg=HEADER_BG, pady=8, padx=12)
        bot.pack(fill="x", side="bottom")

        btn_cfg = dict(font=("Consolas", 10, "bold"), relief="flat",
                       padx=14, pady=5, cursor="hand2")

        tk.Button(bot, text="💾  Save", bg=GREEN, fg="#1e1e2e",
                  command=self._save, **btn_cfg).pack(side="left", padx=4)
        tk.Button(bot, text="🔨  Save & Build", bg=ACCENT, fg="#1e1e2e",
                  command=self._save_and_build, **btn_cfg).pack(side="left", padx=4)
        tk.Button(bot, text="↩  Reload", bg=PANEL_BG, fg=TEXT,
                  command=self._load, **btn_cfg).pack(side="left", padx=4)
        tk.Button(bot, text="📂  Open…", bg=PANEL_BG, fg=TEXT,
                  command=self._open_file, **btn_cfg).pack(side="left", padx=4)
        tk.Button(bot, text="↺  Defaults", bg=PANEL_BG, fg=MUTED,
                  command=self._apply_defaults, **btn_cfg).pack(side="left", padx=4)
        tk.Button(bot, text="✕  Quit", bg=RED, fg="#1e1e2e",
                  command=self._quit, **btn_cfg).pack(side="right", padx=4)

    def _build_rows(self):
        """Build one widget row per schema entry into self.inner."""
        self.row_frames: list[tuple] = []   # (frame, key, label_text)
        self.widgets.clear()

        for entry in SCHEMA:
            key, typ, default, label, tip = entry

            if typ is None:
                # Section header
                frm = tk.Frame(self.inner, bg=DARK_BG)
                frm.pack(fill="x", pady=(14, 2), padx=8)
                tk.Label(frm, text=f"  {key}", bg=HEADER_BG, fg=SECTION_FG,
                         font=("Consolas", 11, "bold"),
                         anchor="w", pady=4).pack(fill="x")
                self.row_frames.append((frm, None, key))
                continue

            # Data row
            frm = tk.Frame(self.inner, bg=PANEL_BG, pady=2)
            frm.pack(fill="x", padx=8, pady=1)

            # Left: label
            lbl_text = f"{label}  [{key}]"
            lbl = tk.Label(frm, text=lbl_text, bg=PANEL_BG, fg=TEXT,
                           font=("Consolas", 10), anchor="w", width=38)
            lbl.pack(side="left", padx=(10, 4))
            if tip:
                Tooltip(lbl, tip)

            # Right: control
            if typ == "bool":
                var = tk.IntVar(value=int(default or "0"))
                chk = tk.Checkbutton(frm, variable=var, bg=PANEL_BG,
                                     activebackground=PANEL_BG,
                                     selectcolor=DARK_BG, fg=GREEN,
                                     command=self._mark_dirty)
                chk.pack(side="left")
                if tip:
                    Tooltip(chk, tip)
                self.widgets[key] = var

            elif typ == "int":
                var = tk.StringVar(value=str(default or "0"))
                ent = tk.Entry(frm, textvariable=var, width=12,
                               bg=ENTRY_BG, fg=TEXT, insertbackground=TEXT,
                               relief="flat", font=("Consolas", 10))
                ent.pack(side="left", ipady=2)
                ent.bind("<FocusOut>", lambda e, k=key: self._validate_int(k))
                ent.bind("<Return>",   lambda e, k=key: self._validate_int(k))
                var.trace_add("write", lambda *a: self._mark_dirty())
                if tip:
                    Tooltip(ent, tip)
                self.widgets[key] = var

            elif typ == "str":
                var = tk.StringVar(value=str(default or ""))
                ent = tk.Entry(frm, textvariable=var, width=24,
                               bg=ENTRY_BG, fg=TEXT, insertbackground=TEXT,
                               relief="flat", font=("Consolas", 10))
                ent.pack(side="left", ipady=2)
                var.trace_add("write", lambda *a: self._mark_dirty())
                if tip:
                    Tooltip(ent, tip)
                self.widgets[key] = var

            elif typ == "choice":
                choices = default  # list
                var = tk.StringVar(value=choices[0])
                cb = ttk.Combobox(frm, textvariable=var, values=choices,
                                  width=14, state="readonly",
                                  font=("Consolas", 10))
                cb.pack(side="left")
                cb.bind("<<ComboboxSelected>>", lambda e: self._mark_dirty())
                if tip:
                    Tooltip(cb, tip)
                self.widgets[key] = var

            self.row_frames.append((frm, key, label))

    # ── Search / filter ────────────────────────────────────────────────

    def _on_search(self, *_):
        q = self.search_var.get().lower().strip()
        for frm, key, label in self.row_frames:
            if key is None:
                # Section header: show if any child matches
                frm.pack(fill="x", pady=(14, 2), padx=8)
                continue
            if not q or q in key.lower() or q in label.lower():
                frm.pack(fill="x", padx=8, pady=1)
            else:
                frm.pack_forget()

    # ── Validation ─────────────────────────────────────────────────────

    def _validate_int(self, key: str):
        var = self.widgets.get(key)
        if not var:
            return
        try:
            int(var.get(), 0)
        except ValueError:
            messagebox.showerror("Invalid value",
                                 f"{key} must be an integer.\n"
                                 f"Got: {var.get()!r}")

    # ── Load / save ────────────────────────────────────────────────────

    def _load(self):
        cfg = read_kconfig(self.path)
        for entry in SCHEMA:
            key, typ = entry[0], entry[1]
            if typ is None or key not in self.widgets:
                continue
            val = cfg.get(key, entry[2] if typ != "choice" else entry[2][0])
            var = self.widgets[key]
            if typ == "bool":
                try:
                    var.set(1 if int(val, 0) else 0)
                except Exception:
                    var.set(1 if str(val).lower() in ("y","yes","true","1") else 0)
            elif typ == "choice":
                choices = entry[2]
                var.set(val if val in choices else choices[0])
            else:
                var.set(str(val))
        self.dirty = False
        self._set_status(f"Loaded {self.path}")

    def _collect(self) -> dict:
        cfg = {}
        for entry in SCHEMA:
            key, typ = entry[0], entry[1]
            if typ is None or key not in self.widgets:
                continue
            var = self.widgets[key]
            if typ == "bool":
                cfg[key] = "1" if var.get() else "0"
            else:
                cfg[key] = str(var.get())
        return cfg

    def _save(self):
        cfg = self._collect()
        write_kconfig(self.path, cfg)
        self._regen_header(cfg)
        self.dirty = False
        self._set_status(f"Saved → {self.path}")

    def _regen_header(self, cfg: dict):
        gen_script = os.path.join(self.root_dir, "scripts", "gen_kconfig_h.py")
        out_h      = os.path.join(self.root_dir, "include", "ark", "kconfig.h")
        if os.path.exists(gen_script):
            try:
                subprocess.run(
                    [sys.executable, gen_script, self.path, out_h],
                    check=True, capture_output=True)
                self._set_status(f"Saved & regenerated kconfig.h")
            except subprocess.CalledProcessError as e:
                messagebox.showwarning("kconfig.h regen failed",
                                       e.stderr.decode(errors="replace"))

    def _save_and_build(self):
        self._save()
        arch = self.widgets.get("ARCH")
        arch_val = arch.get() if arch else "x86_64"
        self._set_status(f"Building ARCH={arch_val}…")
        self.root.update()

        result = subprocess.run(
            ["make", f"ARCH={arch_val}", "all", "-j4"],
            cwd=self.root_dir,
            capture_output=True, text=True
        )
        if result.returncode == 0:
            # Find kernel size
            image = os.path.join(self.root_dir, "compiled", arch_val,
                                 "compressed", "ArkImage")
            size = ""
            if os.path.exists(image):
                size = f"  ({os.path.getsize(image)//1024} KB)"
            self._set_status(f"Build OK{size}")
            messagebox.showinfo("Build succeeded",
                                f"ArkImage built successfully.{size}")
        else:
            lines = (result.stderr or result.stdout).splitlines()
            errors = [l for l in lines if "error:" in l.lower()][:10]
            self._set_status("Build FAILED — see error dialog")
            messagebox.showerror("Build failed",
                                 "\n".join(errors) if errors else
                                 "\n".join(lines[-20:]))

    def _open_file(self):
        path = filedialog.askopenfilename(
            title="Open .kconfig",
            initialdir=self.root_dir,
            filetypes=[(".kconfig files","*.kconfig"),("All files","*")])
        if path:
            self.path = path
            self._load()
            self.root.title(f"Ark Kernel Config — {os.path.basename(path)}")

    def _apply_defaults(self):
        if not messagebox.askyesno("Reset to defaults",
                                   "Reset all values to schema defaults?"):
            return
        for entry in SCHEMA:
            key, typ, default = entry[0], entry[1], entry[2]
            if typ is None or key not in self.widgets:
                continue
            var = self.widgets[key]
            if typ == "bool":
                try:
                    var.set(1 if int(default, 0) else 0)
                except Exception:
                    var.set(0)
            elif typ == "choice":
                var.set(default[0])
            else:
                var.set(str(default))
        self._mark_dirty()

    def _mark_dirty(self):
        self.dirty = True
        self._set_status("Unsaved changes")

    def _set_status(self, msg: str):
        self.status_var.set(msg)
        self.root.update_idletasks()

    def _quit(self):
        if self.dirty:
            if not messagebox.askyesno("Unsaved changes",
                                       "You have unsaved changes. Quit anyway?"):
                return
        self.root.destroy()


# ── Entry point ────────────────────────────────────────────────────────────

def main():
    # Find .kconfig relative to script location
    script_dir = os.path.dirname(os.path.abspath(__file__))
    root_dir   = os.path.dirname(script_dir)
    default_kconfig = os.path.join(root_dir, ".kconfig")

    kconfig_path = sys.argv[1] if len(sys.argv) > 1 else default_kconfig

    if not os.path.exists(kconfig_path):
        print(f"Warning: {kconfig_path} not found, will create on save.",
              file=sys.stderr)

    root = tk.Tk()

    # Apply dark theme to ttk comboboxes
    style = ttk.Style(root)
    style.theme_use("default")
    style.configure("TCombobox",
                    fieldbackground=ENTRY_BG, background=ENTRY_BG,
                    foreground=TEXT, selectbackground=SEL_BG,
                    selectforeground=TEXT)
    style.configure("TScrollbar", background=PANEL_BG,
                    troughcolor=DARK_BG, arrowcolor=MUTED)

    app = KconfigEditor(root, kconfig_path)
    root.protocol("WM_DELETE_WINDOW", app._quit)
    root.mainloop()


if __name__ == "__main__":
    main()
