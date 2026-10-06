"""摇杆内圈「实时跟随」验证（开发自用）。

--padseed 只能灌一组**固定**模拟量，测不出"数值变了界面跟不跟着动"。
这个脚本让界面在**运行期间**换值，再对比三张截图，看内圈/行程柱有没有动。

关键：走 `--padmirror=<文件>` 旁路，而不是共享内存。
共享内存的写入句柄是**独占**的——真有记录进程在跑时，注入器进程抢不到，
界面读到的永远是记录进程发布的全 0（没插真手柄就永远是 0），
"数值变了跟不跟着动"这件事就测不出来。旁路直接读文件，绕开独占。
tools/pad-inject.cpp 仍然保留：它用来单独验证"共享内存这条路的格式对不对"。

流程：
    1. 写镜像文件 A（摇杆推到右下）→ 启动预览构建（带 --padmirror）
    2. 截图 A
    3. 改写镜像 B（摇杆推到左上 + 扳机拉满）→ 等 1.5s → 截图 B
    4. 写全 0 → 等 1.0s → 截图 Z（回中基准）
    5. 三张两两做像素差分，输出变化像素数与包围盒

结论判据（差分与几何量都要看）：
    A/B/Z 两两都有差异，且内圈位移 ≈ 数值×可动半径 → 读取/量化/重绘全通，
        真手柄不动就是**写方（记录进程）**的问题
    A 与 B 完全一样（但 A/Z 有差异）→ 数值变了不重绘，问题在 requestUpdate 链路
    三张全一样 → 根本没读到数据（路径/版本/权限）

用法：
    python tools/pad-live-test.py             # 编译预览 + 跑全套
    python tools/pad-live-test.py --no-build  # 只跑（预览已是最新）
"""
import argparse
import ctypes
import os
import subprocess
import sys
import time
import zlib
from ctypes import wintypes

# 复用 preview-shot.py 的截图/找窗口实现——文件名带连字符，不能正常 import
import importlib.util
_spec = importlib.util.spec_from_file_location(
    "preview_shot", os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                 "preview-shot.py"))
_shot = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_shot)
ROOT, CMAKE, PREVIEW_EXE, CFG, OUT = _shot.ROOT, _shot.CMAKE, _shot.PREVIEW_EXE, _shot.CFG, _shot.OUT
u, g, k = _shot.u, _shot.g, _shot.k
capture, find_window, kill_preview = _shot.capture, _shot.find_window, _shot.kill_preview

CTRL = os.path.join(ROOT, ".preview", "pad-inject.txt")

# 三组数值。
# 注意摇杆 x 轴要小：**机身面板本身很宽，内圈的可视行程会被面板边界吃掉**。
# 左摇杆中心在设计坐标 (2.62, 2.70)、大圈半径 1.22，而机身盒从 y≈1.42 起、
# 顶栏到 y≈2.12——所以 y 轴只有约 0.58 个单位的余量，推满 0.85 会被裁掉。
# 用 0.40 保证无论怎么推都在可视区内，量的才是"数值→位置"而不是"裁剪结果"。
CASES = {
    "A": "0.40 -0.40 0.40 -0.40 0.00 0.00",
    "B": "-0.40 0.40 -0.40 0.40 1.00 1.00",
    "Z": "0 0 0 0 0 0",
}


def read_png(path):
    """解自己的 PNG（8bit RGB、filter 0、非隔行）→ (w, h, bytearray RGB)"""
    data = open(path, "rb").read()
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    pos, idat, w, h = 8, b"", 0, 0
    while pos < len(data):
        ln = int.from_bytes(data[pos:pos + 4], "big")
        tag = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + ln]
        if tag == b"IHDR":
            w, h = int.from_bytes(body[0:4], "big"), int.from_bytes(body[4:8], "big")
        elif tag == b"IDAT":
            idat += body
        pos += 12 + ln
    raw = zlib.decompress(idat)
    stride = w * 3 + 1
    px = bytearray()
    for y in range(h):
        row = raw[y * stride + 1:(y + 1) * stride]
        px += row
    return w, h, px


def diff_boxes(p1, p2, thresh=24):
    """两张同尺寸图的差异包围盒（按 8x8 分块聚合，避免噪声点）"""
    w1, h1, a = p1
    w2, h2, b = p2
    if (w1, h1) != (w2, h2):
        return None, 0
    w, h = w1, h1
    blocks = {}
    total = 0
    for y in range(0, h, 8):
        for x in range(0, w, 8):
            n = 0
            for yy in range(y, min(y + 8, h)):
                base = yy * w * 3
                for xx in range(x, min(x + 8, w)):
                    i = base + xx * 3
                    if (abs(a[i] - b[i]) + abs(a[i + 1] - b[i + 1]) +
                            abs(a[i + 2] - b[i + 2])) > thresh:
                        n += 1
            if n:
                blocks[(x // 8, y // 8)] = n
                total += n
    if not blocks:
        return [], 0
    xs = [bx * 8 for bx, _ in blocks]
    ys = [by * 8 for _, by in blocks]
    return (min(xs), min(ys), max(xs) + 8, max(ys) + 8), total


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--no-build", action="store_true", help="跳过编译")
    ap.add_argument("--settle", type=float, default=2.0, help="启动后稳定时间（秒）")
    args = ap.parse_args()

    os.makedirs(OUT, exist_ok=True)
    os.makedirs(CFG, exist_ok=True)

    if not args.no_build:
        r = subprocess.run([CMAKE, "--build", "build-preview", "--parallel", "8"],
                           cwd=ROOT, capture_output=True, timeout=1200)
        out = (r.stdout + r.stderr).decode("gbk", "replace")
        if r.returncode != 0:
            print("编译失败：")
            print("\n".join(l for l in out.splitlines() if "error:" in l)[:15] or out[-1200:])
            return 1
        print("编译 OK")

    kill_preview()
    time.sleep(0.6)

    # 界面读的不是控制文件，而是这份"镜像"：注入器把当前值写进控制文件，
    # 界面直接读同一个文件。用文件而不是共享内存，是因为共享内存的写入句柄
    # 独占——真有记录进程在跑时注入器抢不到，界面读到的永远是它发布的全 0。
    mirror = os.path.join(OUT, "pad-mirror.txt")
    open(CTRL, "w").write(CASES["A"] + "\n")
    open(mirror, "w").write(CASES["A"] + "\n")
    env = dict(os.environ)
    env["KEYBOARDSTATS_DIR"] = CFG
    proc = subprocess.Popen([PREVIEW_EXE, "--page=0", "--filter=4",
                             "--padmirror=" + mirror], env=env,
                            creationflags=0x00000008, close_fds=True)
    try:
        hwnd = None
        for _ in range(30):
            time.sleep(0.4)
            hwnd = find_window()
            if hwnd:
                break
        if not hwnd:
            print("没找到预览窗口（进程退出码", proc.poll(), "）")
            return 1
        time.sleep(1.2)
        u.SetWindowPos(hwnd, None, 30, 20, 1680, 980, 0x0004)
        time.sleep(args.settle)

        shots = {}
        for name in ("A", "B", "Z"):
            # 只改镜像文件（界面直接读它）。模拟量注入器仅用于顺带核对共享内存路径。
            open(mirror, "w").write(CASES[name] + "\n")
            time.sleep(1.5 if name != "A" else 0.8)
            path = os.path.join(OUT, "padlive-%s.png" % name)
            capture(hwnd, path)
            shots[name] = read_png(path)
            print("截图 %s → %s" % (name, os.path.relpath(path, ROOT)))

        print("── 差分结果 ──")
        for x, y in (("A", "B"), ("A", "Z"), ("B", "Z")):
            box, n = diff_boxes(shots[x], shots[y])
            if box is None:
                print("   %s vs %s：尺寸不同，无法比较" % (x, y))
            elif not box:
                print("   %s vs %s：完全相同（%d 像素不同）—— 没动！" % (x, y, n))
                continue
            else:
                print("   %s vs %s：变化 %d 像素，包围盒 x[%d,%d] y[%d,%d]"
                      % (x, y, n, box[0], box[2], box[1], box[3]))
    finally:
        kill_preview()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
