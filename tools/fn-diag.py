"""Fn 键可捕获性诊断：低级键盘钩子 + RawInput 双通道监听。

用途：判断键盘的 Fn 键是否向系统发送任何数据。
  · 若两个通道都无事件 → 纯硬件 Fn，软件层面无法计数（物理限制）
  · 若 RawInput 通道有事件 → 可用原始输入实现 Fn 计数

运行：python fn-diag.py [监听秒数]   结果同时打印到控制台和 fn-diag.log
"""
import ctypes
import sys
import time
from ctypes import wintypes

user32 = ctypes.WinDLL("user32", use_last_error=True)
kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
user32.DefWindowProcW.restype = ctypes.c_ssize_t
user32.DefWindowProcW.argtypes = [wintypes.HWND, ctypes.c_uint, ctypes.c_size_t, ctypes.c_ssize_t]
user32.GetRawInputData.restype = wintypes.UINT
user32.GetRawInputData.argtypes = [wintypes.HANDLE, wintypes.UINT, ctypes.c_void_p,
                                   ctypes.POINTER(wintypes.UINT), wintypes.UINT]
user32.RegisterRawInputDevices.restype = wintypes.BOOL

DURATION = int(sys.argv[1]) if len(sys.argv) > 1 else 60
LOG = "fn-diag.log"
logf = open(LOG, "w", encoding="utf-8")


def log(msg):
    line = f"{time.strftime('%H:%M:%S')} {msg}"
    print(line, flush=True)
    logf.write(line + "\n")
    logf.flush()


# ── 通道 1：低级键盘钩子（标准键码）──
class KBDLLHOOKSTRUCT(ctypes.Structure):
    _fields_ = [("vkCode", wintypes.DWORD), ("scanCode", wintypes.DWORD),
                ("flags", wintypes.DWORD), ("time", wintypes.DWORD),
                ("dwExtraInfo", ctypes.c_void_p)]


HOOKPROC = ctypes.WINFUNCTYPE(ctypes.c_ssize_t, ctypes.c_int, wintypes.WPARAM, wintypes.LPARAM)


# 回调对象必须保留引用：HOOKPROC/WNDPROC 实例若被 GC，系统侧的回调会失效
# （表现为"钩子安装成功但一个事件都收不到"——连对照按键也没有）
_hook_refs = []


def hook_proc(nCode, wParam, lParam):
    if nCode == 0 and wParam in (0x100, 0x104):
        kb = ctypes.cast(lParam, ctypes.POINTER(KBDLLHOOKSTRUCT)).contents
        log(f"[键盘钩子] vk=0x{kb.vkCode:02X} scan=0x{kb.scanCode:02X} flags=0x{kb.flags:X}")
    return user32.CallNextHookEx(None, nCode, wParam, lParam)


# ── 通道 2：RawInput（可捕获 Consumer Control / 厂商自定义用法）──
WM_INPUT = 0x00FF
RID_INPUT = 0x10000003


class RAWINPUTDEVICE(ctypes.Structure):
    _fields_ = [("usUsagePage", wintypes.USHORT), ("usUsage", wintypes.USHORT),
                ("dwFlags", wintypes.DWORD), ("hwndTarget", wintypes.HWND)]


class RAWINPUTHEADER(ctypes.Structure):
    _fields_ = [("dwType", wintypes.DWORD), ("dwSize", wintypes.DWORD),
                ("hDevice", wintypes.HANDLE), ("wParam", wintypes.WPARAM)]


WNDPROC = ctypes.WINFUNCTYPE(ctypes.c_ssize_t, wintypes.HWND, ctypes.c_uint,
                             wintypes.WPARAM, wintypes.LPARAM)


def wnd_proc(hwnd, msg, wParam, lParam):
    if msg == WM_INPUT:
        size = wintypes.UINT(0)
        user32.GetRawInputData(wintypes.HANDLE(lParam), RID_INPUT, None, ctypes.byref(size),
                               ctypes.sizeof(RAWINPUTHEADER))
        if size.value:
            buf = (ctypes.c_ubyte * size.value)()
            ret = user32.GetRawInputData(wintypes.HANDLE(lParam), RID_INPUT,
                                         ctypes.cast(buf, ctypes.c_void_p),
                                         ctypes.byref(size), ctypes.sizeof(RAWINPUTHEADER))
            if ret != 0xFFFFFFFF:
                # 前 16 字节是 keyboard 数据：MakeCode/Flags/Reserved/VKey
                data = bytes(buf)
                hdr = RAWINPUTHEADER.from_buffer_copy(data)
                if hdr.dwType == 1:   # RIM_TYPEKEYBOARD
                    vk = data[14] | (data[15] << 8)
                    make = data[8] | (data[9] << 8)
                    flags = data[10] | (data[11] << 8)
                    log(f"[RawInput] 键盘报告 vk=0x{vk:04X} make=0x{make:04X} flags=0x{flags:04X}")
                else:
                    log(f"[RawInput] 非键盘报告 type={hdr.dwType} size={size.value} data={data[:24].hex()}")
    return user32.DefWindowProcW(hwnd, msg, int(wParam), int(lParam))


def main():
    hinst = kernel32.GetModuleHandleW(None)
    # 隐藏窗口（RawInput 需要窗口接收 WM_INPUT）
    wc = ctypes.WINFUNCTYPE(None)  # placeholder
    class WNDCLASSW(ctypes.Structure):
        _fields_ = [("style", ctypes.c_uint), ("lpfnWndProc", WNDPROC),
                    ("cbClsExtra", ctypes.c_int), ("cbWndExtra", ctypes.c_int),
                    ("hInstance", wintypes.HINSTANCE), ("hIcon", wintypes.HICON),
                    ("hCursor", wintypes.HANDLE), ("hbrBackground", wintypes.HBRUSH),
                    ("lpszMenuName", wintypes.LPCWSTR), ("lpszClassName", wintypes.LPCWSTR)]
    wndproc = WNDPROC(wnd_proc)
    wcw = WNDCLASSW()
    wcw.lpfnWndProc = wndproc
    wcw.hInstance = hinst
    wcw.lpszClassName = "FnDiagWnd"
    user32.RegisterClassW(ctypes.byref(wcw))
    hwnd = user32.CreateWindowExW(0, "FnDiagWnd", "FnDiag", 0, 0, 0, 0, 0, None, None, hinst, None)

    # 订阅：通用桌面-键盘 + Consumer Control + 常见厂商页
    # 只订阅合法组合——通配 usage（如 0x0C,0x00）会让整批注册失败 err=87
    devices = (RAWINPUTDEVICE * 2)()
    devices[0].usUsagePage, devices[0].usUsage = 0x01, 0x06   # 通用桌面-键盘
    devices[1].usUsagePage, devices[1].usUsage = 0x0C, 0x01   # Consumer Control（Fn/多媒体常走这里）
    for d in devices:
        d.dwFlags = 0x00000100   # RIDEV_INPUTSINK：窗口不聚焦也能收
        d.hwndTarget = hwnd
    ok = user32.RegisterRawInputDevices(devices, 2, ctypes.sizeof(RAWINPUTDEVICE))
    log(f"RawInput 订阅: {'成功' if ok else '失败 err=' + str(ctypes.get_last_error())}")

    hook_proc_cb = HOOKPROC(hook_proc)      # 保留引用（见 _hook_refs 注释）
    _hook_refs.append(hook_proc_cb)
    hook = user32.SetWindowsHookExW(13, hook_proc_cb, None, 0)
    log(f"键盘钩子: {'安装成功' if hook else '安装失败'}")

    log(f"开始监听 {DURATION} 秒 —— 请按：① 左 Win ② 右 Fn（5 次）③ 字母 A（作为对照）")
    msg = wintypes.MSG()
    t0 = time.time()
    last_hb = t0
    while time.time() - t0 < DURATION:
        if user32.PeekMessageW(ctypes.byref(msg), None, 0, 0, 1):
            user32.TranslateMessage(ctypes.byref(msg))
            user32.DispatchMessageW(ctypes.byref(msg))
        now = time.time()
        if now - last_hb >= 15:
            log(f"……仍在监听（剩余约 {int(DURATION - (now - t0))} 秒）")
            last_hb = now
        time.sleep(0.005)
    if hook:
        user32.UnhookWindowsHookEx(hook)
    log("监听结束。结论：若键盘钩子只有 0x5B/0x41 等而没有 Fn 相关事件，"
        "且 RawInput 也无对应报告，则该 Fn 为纯硬件键（无法计数）。")


if __name__ == "__main__":
    main()
