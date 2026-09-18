#!/usr/bin/python3
"""Map only the integrated touchscreen to DSI-1 on XRandR changes.

Event-driven: no periodic subprocess polling. xinput computes the output's
rotation/reflection/offset matrix; do not duplicate that math or change libinput
calibration. Uses existing libX11/libXrandr and Python, no python-xlib dependency.
"""
import ctypes as C
import fcntl
import os
import selectors
import subprocess
import sys

DEVICE = 'Samsung S6D6FT0 Touchscreen'
OUTPUT = 'DSI-1'

def remap():
    result = subprocess.run(['/usr/bin/xinput', 'map-to-output', DEVICE, OUTPUT],
                            capture_output=True, text=True, timeout=5)
    if result.returncode:
        print('touch-map: ' + result.stderr.strip(), file=sys.stderr, flush=True)
    else:
        print('touch-map: mapped integrated touchscreen to DSI-1', flush=True)
    return result.returncode

def main():
    if '--once' in sys.argv:
        return remap()
    runtime = os.environ.get('XDG_RUNTIME_DIR', f'/run/user/{os.getuid()}')
    lock = os.open(runtime + '/lpi4a-touch-map.lock',
                   os.O_CREAT | os.O_RDWR | os.O_NOFOLLOW, 0o600)
    try:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError:
        os.close(lock)
        return 0
    x = C.CDLL('libX11.so.6')
    rr = C.CDLL('libXrandr.so.2')
    x.XOpenDisplay.argtypes, x.XOpenDisplay.restype = [C.c_char_p], C.c_void_p
    x.XDefaultRootWindow.argtypes, x.XDefaultRootWindow.restype = [C.c_void_p], C.c_ulong
    x.XConnectionNumber.argtypes, x.XConnectionNumber.restype = [C.c_void_p], C.c_int
    x.XPending.argtypes, x.XPending.restype = [C.c_void_p], C.c_int
    x.XNextEvent.argtypes, x.XNextEvent.restype = [C.c_void_p, C.c_void_p], C.c_int
    x.XFlush.argtypes, x.XFlush.restype = [C.c_void_p], C.c_int
    x.XCloseDisplay.argtypes, x.XCloseDisplay.restype = [C.c_void_p], C.c_int
    rr.XRRQueryExtension.argtypes = [C.c_void_p, C.POINTER(C.c_int), C.POINTER(C.c_int)]
    rr.XRRQueryExtension.restype = C.c_int
    rr.XRRSelectInput.argtypes = [C.c_void_p, C.c_ulong, C.c_int]
    rr.XRRSelectInput.restype = None
    rr.XRRUpdateConfiguration.argtypes, rr.XRRUpdateConfiguration.restype = [C.c_void_p], C.c_int
    display = x.XOpenDisplay(None)
    if not display:
        raise RuntimeError('No X display; this helper is for an X11 login session')
    try:
        event_base, error_base = C.c_int(), C.c_int()
        if not rr.XRRQueryExtension(display, C.byref(event_base), C.byref(error_base)):
            raise RuntimeError('XRandR extension unavailable')
        # Screen, CRTC (including 180-degree rotation), and output changes.
        rr.XRRSelectInput(display, x.XDefaultRootWindow(display), 1 | 2 | 4)
        x.XFlush(display)
        remap()
        selector = selectors.DefaultSelector()
        selector.register(x.XConnectionNumber(display), selectors.EVENT_READ)
        # XEvent's public ABI is a union padded to 24 native longs.
        event = (C.c_long * 24)()
        while True:
            if not x.XPending(display):
                selector.select()
            changed = False
            while x.XPending(display):
                x.XNextEvent(display, C.byref(event))
                kind = C.cast(C.byref(event), C.POINTER(C.c_int))[0]
                if kind in (event_base.value, event_base.value + 1):
                    rr.XRRUpdateConfiguration(C.byref(event))
                    changed = True
            if changed:
                remap()
    finally:
        x.XCloseDisplay(display)
        os.close(lock)

if __name__ == '__main__':
    sys.exit(main())
