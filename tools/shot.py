#!/usr/bin/env python3
"""Minimal screen capture via GDI32 (no Pillow, no PowerShell Add-Type)."""
import ctypes
import struct
import sys
from ctypes import wintypes as w

user32 = ctypes.windll.user32
gdi32 = ctypes.windll.gdi32


class BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = [
        ('biSize', w.DWORD), ('biWidth', ctypes.c_long), ('biHeight', ctypes.c_long),
        ('biPlanes', w.WORD), ('biBitCount', w.WORD), ('biCompression', w.DWORD),
        ('biSizeImage', w.DWORD), ('biXPelsPerMeter', ctypes.c_long),
        ('biYPelsPerMeter', ctypes.c_long), ('biClrUsed', w.DWORD),
        ('biClrImportant', w.DWORD),
    ]


def grab(path, w_, h_, out_w=None, out_h=None):
    ow = out_w or w_
    oh = out_h or h_
    hdc = user32.GetDC(0)
    mem = gdi32.CreateCompatibleDC(hdc)
    bmp = gdi32.CreateCompatibleBitmap(hdc, ow, oh)
    old = gdi32.SelectObject(mem, bmp)
    if (ow, oh) == (w_, h_):
        gdi32.BitBlt(mem, 0, 0, w_, h_, hdc, 0, 0, 0x00CC0020)  # SRCCOPY
    else:
        gdi32.SetStretchBltMode(mem, 4)  # HALFTONE
        gdi32.StretchBlt(mem, 0, 0, ow, oh, hdc, 0, 0, w_, h_, 0x00CC0020)

    bi = BITMAPINFOHEADER()
    bi.biSize = ctypes.sizeof(bi)
    bi.biWidth = ow
    bi.biHeight = -oh          # top-down
    bi.biPlanes = 1
    bi.biBitCount = 32
    bi.biCompression = 0
    buf = ctypes.create_string_buffer(ow * oh * 4)
    gdi32.GetDIBits(mem, bmp, 0, oh, buf, ctypes.byref(bi), 0)

    gdi32.SelectObject(mem, old)
    gdi32.DeleteObject(bmp)
    gdi32.DeleteDC(mem)
    user32.ReleaseDC(0, hdc)

    with open(path, 'wb') as f:
        f.write(b'BM' + struct.pack('<IHHI', 54 + len(buf), 0, 0, 54))
        f.write(struct.pack('<IiiHHIIiiII', 40, ow, -oh, 1, 32, 0, len(buf), 0, 0, 0, 0))
        f.write(buf.raw)
    return len(buf) + 54


if __name__ == '__main__':
    out = sys.argv[1] if len(sys.argv) > 1 else 'shot.bmp'
    print('wrote', out, grab(out, 1920, 1080, 960, 540), 'bytes')
