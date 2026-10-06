"""预览构建的界面截图工具（开发自用）。

背景：正式产物 build\\KeyboardStats.exe 经常被正在运行的程序占用（链接失败），
而程序以管理员运行时普通权限又截不了图。因此这里用**独立的预览构建目录**
build-preview\\ 出图：它不带 RUNASADMIN 标记，普通权限即可运行并截图，
改代码看效果的全过程都不需要用户退出正在使用的程序。

用法：
    python tools/preview-shot.py                       # 编译预览 + 截「分开」模式
    python tools/preview-shot.py --filters 4,3 --tag pad2
    python tools/preview-shot.py --size 1680x980 --no-build

安全约定：本脚本只结束**镜像路径为 build-preview\\KeyboardStats.exe** 的进程，
绝不触碰用户正在使用的 build\\KeyboardStats.exe（两者路径不同，按路径过滤）。
"""
import argparse
import ctypes
import datetime
import os
import struct
import subprocess
import sys
import time
import zlib
from ctypes import wintypes

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CMAKE = r"D:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
PREVIEW_EXE = os.path.join(ROOT, "build-preview", "KeyboardStats.exe")
CFG = os.path.join(ROOT, ".preview-cfg")
OUT = os.path.join(ROOT, ".preview")

u = ctypes.WinDLL("user32")
g = ctypes.WinDLL("gdi32")
k = ctypes.WinDLL("kernel32")
psapi = ctypes.WinDLL("psapi")
ctypes.WinDLL("shcore").SetProcessDpiAwareness(2)


class BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = [("biSize", wintypes.DWORD), ("biWidth", wintypes.LONG),
                ("biHeight", wintypes.LONG), ("biPlanes", wintypes.WORD),
                ("biBitCount", wintypes.WORD), ("biCompression", wintypes.DWORD),
                ("biSizeImage", wintypes.DWORD), ("biXPelsPerMeter", wintypes.LONG),
                ("biYPelsPerMeter", wintypes.LONG), ("biClrUsed", wintypes.DWORD),
                ("biClrImportant", wintypes.DWORD)]


def preview_pids():
    """只返回预览构建的进程（按镜像全路径过滤）"""
    out = []
    n = wintypes.DWORD(4096)
    ids = (wintypes.DWORD * 4096)()
    psapi.EnumProcesses(ctypes.byref(ids), ctypes.sizeof(ids), ctypes.byref(n))
    target = PREVIEW_EXE.lower()
    for i in range(n.value // 4):
        pid = ids[i]
        if not pid:
            continue
        h = k.OpenProcess(0x1000 | 0x0001, False, pid)   # QUERY_LIMITED | TERMINATE
        if not h:
            continue
        buf = ctypes.create_unicode_buffer(1024)
        size = wintypes.DWORD(1024)
        if k.QueryFullProcessImageNameW(h, 0, buf, ctypes.byref(size)) and buf.value.lower() == target:
            out.append(pid)
        k.CloseHandle(h)
    return out


def kill_preview():
    for pid in preview_pids():
        h = k.OpenProcess(0x0001, False, pid)
        if h:
            k.TerminateProcess(h, 0)
            k.CloseHandle(h)


def find_window():
    found = []

    def proc_name(pid):
        h = k.OpenProcess(0x1000, False, pid)
        if not h:
            return ""
        b = ctypes.create_unicode_buffer(1024)
        n = wintypes.DWORD(1024)
        ok = k.QueryFullProcessImageNameW(h, 0, b, ctypes.byref(n))
        k.CloseHandle(h)
        return b.value if ok else ""

    callback = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)

    def cb(hwnd, _):
        if not u.IsWindowVisible(hwnd) or u.GetWindowTextLengthW(hwnd) == 0:
            return True
        pid = wintypes.DWORD()
        u.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        if proc_name(pid.value).lower() == PREVIEW_EXE.lower():
            found.append(hwnd)
            return False
        return True

    u.EnumWindows(callback(cb), 0)
    return found[0] if found else None


def capture(hwnd, path):
    rc = wintypes.RECT()
    u.GetWindowRect(hwnd, ctypes.byref(rc))
    w, h = rc.right - rc.left, rc.bottom - rc.top
    hdc = u.GetDC(0)
    memdc = g.CreateCompatibleDC(hdc)
    bmp = g.CreateCompatibleBitmap(hdc, w, h)
    g.SelectObject(memdc, bmp)
    u.PrintWindow(hwnd, memdc, 2)
    bi = BITMAPINFOHEADER()
    bi.biSize = ctypes.sizeof(BITMAPINFOHEADER)
    bi.biWidth = w
    bi.biHeight = -h
    bi.biPlanes = 1
    bi.biBitCount = 32
    buf = ctypes.create_string_buffer(w * h * 4)
    g.GetDIBits(memdc, bmp, 0, h, buf, ctypes.byref(bi), 0)
    data = buf.raw
    g.DeleteObject(bmp)
    g.DeleteDC(memdc)
    u.ReleaseDC(0, hdc)
    rows = [[(data[y * w * 4 + x * 4 + 2], data[y * w * 4 + x * 4 + 1], data[y * w * 4 + x * 4])
             for x in range(w)] for y in range(h)]
    raw = b"".join(b"\x00" + b"".join(bytes(p) for p in r) for r in rows)

    def chunk(tag, payload):
        return (struct.pack(">I", len(payload)) + tag + payload +
                struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 6)))
        f.write(chunk(b"IEND", b""))
    return w, h


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--filters", default="4", help="逗号分隔的筛选档位（0键盘 1鼠标 2手柄 3全部 4分开）")
    ap.add_argument("--size", default="1680x980", help="窗口尺寸 WxH；填 - 表示用默认尺寸")
    ap.add_argument("--tag", default="shot", help="输出文件名前缀")
    ap.add_argument("--no-build", action="store_true", help="跳过编译（只截图）")
    ap.add_argument("--extra", default="", help="附加启动参数（空格分隔）")
    ap.add_argument("--page", default="0", help="启动页（0热力图 1直方图 2设置 3主题）")
    ap.add_argument("--analog", default=None,
                    help="写入模拟行程种子 sl,sr,lt,rt（无手柄时核对列表/看板用）")
    ap.add_argument("--scroll", type=int, default=0,
                    help="截图前把滚轮在窗口中心滚 N 格（正数向下），用于核对列表后半部分")
    ap.add_argument("--hover", default=None,
                    help="截图前把鼠标移到窗口相对坐标 hx,hy（0..1），用于核对悬浮提示")
    args = ap.parse_args()

    if not args.no_build:
        r = subprocess.run([CMAKE, "--build", "build-preview", "--parallel", "8"],
                           cwd=ROOT, capture_output=True, timeout=1200)
        out = (r.stdout + r.stderr).decode("gbk", "replace")
        errs = [l for l in out.splitlines() if "error:" in l]
        if r.returncode != 0:
            print("编译失败：")
            print("\n".join(errs[:15]) or out[-1200:])
            return 1
        print("编译 OK")

    # 每次清空配置目录：主题/布局偏好不残留，截图状态每次一致（浅色主题 + 已引导）
    if os.path.isdir(CFG):
        for name in os.listdir(CFG):
            try:
                os.remove(os.path.join(CFG, name))
            except OSError:
                pass
    os.makedirs(CFG, exist_ok=True)
    os.makedirs(OUT, exist_ok=True)
    with open(os.path.join(CFG, "ui-general.txt"), "w") as f:
        f.write("onboarded=1\n")

    # 模拟行程种子：摇杆/扳机是模拟量，没接真手柄就永远读不到数据，
    # 列表与看板里那几项也就没机会显示。这里直接写一份 analog-*.jsonl 造出行程，
    # 好在没有手柄的机器上核对"左摇杆/右摇杆/左扳机/右扳机"四个条目。
    # 拆到多个小时写，顺便验证按小时聚合的累加。
    if args.analog:
        try:
            vals = [float(v) for v in args.analog.split(",")]
            assert len(vals) == 4
        except (ValueError, AssertionError):
            raise SystemExit("--analog 需要形如 24.6,9.7,6.1,2.4")
        today = datetime.date.today()
        ym = "%04d%02d" % (today.year, today.month)
        with open(os.path.join(CFG, "analog-%s.jsonl" % ym), "w", encoding="utf-8") as f:
            f.write('{"format":"keyboardstats-analog","version":1}\n')
            for i, h in enumerate((9, 10, 11, 13)):
                w = (0.4, 0.3, 0.2, 0.1)[i]     # 权重和为 1，总量正好等于传入值
                f.write('{"d":"%s","h":%d,"sl":%.3f,"sr":%.3f,"lt":%.3f,"rt":%.3f}\n'
                        % (today.isoformat(), h,
                           vals[0] * w, vals[1] * w, vals[2] * w, vals[3] * w))
        print("行程种子 sl=%.1f sr=%.1f lt=%.1f rt=%.1f" % tuple(vals))

    size = None
    if args.size != "-":
        try:
            size = tuple(int(v) for v in args.size.lower().split("x"))
        except ValueError:
            print("--size 需要形如 1680x980")
            return 1

    extra = [a for a in args.extra.split(" ") if a]
    env = dict(os.environ)
    env["KEYBOARDSTATS_DIR"] = CFG
    made = []
    try:
        for filt in [f.strip() for f in args.filters.split(",") if f.strip()]:
            kill_preview()
            time.sleep(0.6)
            proc = subprocess.Popen([PREVIEW_EXE, "--padseed", f"--page={args.page}",
                                     f"--filter={filt}", "--preview-instance"] + extra,
                                    env=env, creationflags=0x00000008, close_fds=True)
            hwnd = None
            for _ in range(30):
                time.sleep(0.4)
                hwnd = find_window()
                if hwnd:
                    break
            if not hwnd:
                print(f"[filter={filt}] 没找到窗口（进程退出码 {proc.poll()}）")
                continue
            time.sleep(2.2)
            if size:
                u.SetWindowPos(hwnd, None, 30, 20, size[0], size[1], 0x0004)
                time.sleep(1.6)
            if args.scroll:
                # 把光标移到按键列表所在区域再滚轮，避免命中别的可滚动控件。
                # 列表固定在窗口右侧（约 68% 宽、纵向中部），用窗口矩形按比例算点。
                rc = wintypes.RECT()
                u.GetWindowRect(hwnd, ctypes.byref(rc))
                px = rc.left + int((rc.right - rc.left) * 0.82)
                py = rc.top + int((rc.bottom - rc.top) * 0.62)
                u.SetCursorPos(px, py)
                time.sleep(0.4)
                # WM_MOUSEWHEEL：wParam 高位是 delta，一格 = 120
                for _ in range(abs(args.scroll)):
                    delta = -120 if args.scroll > 0 else 120
                    u.SendMessageW(hwnd, 0x020A, (delta & 0xFFFF) << 16, (py << 16) | (px & 0xFFFF))
                    time.sleep(0.12)
                time.sleep(1.0)
            if args.hover:
                try:
                    hx, hy = [float(v) for v in args.hover.split(",")]
                    rc = wintypes.RECT()
                    u.GetWindowRect(hwnd, ctypes.byref(rc))
                    px = rc.left + int((rc.right - rc.left) * hx)
                    py = rc.top + int((rc.bottom - rc.top) * hy)
                    u.SetCursorPos(px, py)
                    # 移动两下让框架收到 WM_MOUSEMOVE（一次可能被吞）
                    for dx in (0, 1, 0):
                        u.SetCursorPos(px + dx, py)
                        time.sleep(0.05)
                    time.sleep(1.2)   # 等 tooltip 的显示延时
                except Exception as e:
                    print("hover 失败:", e)
            path = os.path.join(OUT, f"{args.tag}-f{filt}.png")
            w, h = capture(hwnd, path)
            made.append(path)
            print(f"[filter={filt}] → {os.path.relpath(path, ROOT)}  {w}x{h}")
    finally:
        kill_preview()
    print("完成，共", len(made), "张")
    return 0


if __name__ == "__main__":
    sys.exit(main())
