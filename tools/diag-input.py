#!/usr/bin/env python3
"""Dump every top-level window of interest + the foreground window.

Run while CS2 and the overlay are both up. It shows, for the game window and
the overlay window: pid, class, title, rect and extended styles, so we can see
whether WS_EX_TRANSPARENT / TOPMOST / NOACTIVATE are actually set and who is
foreground.
"""
import ctypes
from ctypes import wintypes as wt

u = ctypes.windll.user32

EX = {
    0x8: 'TOPMOST', 0x20: 'TRANSPARENT', 0x80: 'TOOLWINDOW',
    0x80000: 'LAYERED', 0x8000000: 'NOACTIVATE', 0x80000000: 'APPWINDOW',
}


def main():
    hits = []

    def cb(hwnd, lparam):
        if not u.IsWindowVisible(hwnd):
            return True
        cls = ctypes.create_unicode_buffer(256)
        title = ctypes.create_unicode_buffer(256)
        u.GetClassNameW(hwnd, cls, 256)
        u.GetWindowTextW(hwnd, title, 256)
        c, t = cls.value, title.value
        if c == 'SDL_app' or c == 'Window' or c.startswith('C') or t in ('Counter-Strike 2', 'Window', ''):
            pid = wt.DWORD()
            u.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
            ex = u.GetWindowLongW(hwnd, -20)
            r = wt.RECT()
            u.GetWindowRect(hwnd, ctypes.byref(r))
            hits.append((hwnd, c, t, pid.value, ex, r))
        return True

    EnumWindowsProc = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    u.EnumWindows(EnumWindowsProc(cb), 0)

    for hwnd, cls, title, pid, ex, r in hits:
        flags = [n for v, n in EX.items() if ex & v]
        print(f"hwnd={hwnd:#x} pid={pid} class={cls!r} title={title!r}")
        print(f"   rect=({r.left},{r.top},{r.right},{r.bottom}) exstyle=0x{ex:x} [{' '.join(flags)}]")

    fg = u.GetForegroundWindow()
    fgc = ctypes.create_unicode_buffer(256)
    fgt = ctypes.create_unicode_buffer(256)
    if fg:
        u.GetClassNameW(fg, fgc, 256)
        u.GetWindowTextW(fg, fgt, 256)
    print(f"FOREGROUND hwnd={fg:#x} class={fgc.value!r} title={fgt.value!r}")


if __name__ == '__main__':
    main()
