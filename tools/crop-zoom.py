"""截图局部放大工具（开发自用）：把手柄面板区域裁出来放大，便于目视核对细节。

背景：preview-shot.py 出的是整窗截图（1680x980），手柄面板在热力图页只占
一小块区域，直接看图看不清按键是否重叠、圆角是否正常等细节。

用法：
    python tools/crop-zoom.py .preview/pad2-f4.png              # 默认裁手柄面板
    python tools/crop-zoom.py shot.png --box 540,256,710,374 --zoom 4
    python tools/crop-zoom.py shot.png --out .preview/big.png

说明：不依赖 Pillow（本机没装），自带 PNG 解码/编码（含全部 5 种行滤波）。
--box 用**屏幕像素**（预览截图的原始坐标），不是缩放后的显示坐标。
"""
import argparse
import os
import struct
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# 手柄面板在 1680x980 整窗截图里的位置（热力图页「分开」模式，实测）
DEFAULT_BOX = (540, 256, 710, 374)


def decode_png(path):
    d = open(path, "rb").read()
    pos, w, h, idat = 8, None, None, b""
    bit_depth, color_type = 8, 2
    while pos < len(d):
        ln = struct.unpack(">I", d[pos:pos + 4])[0]
        tag = d[pos + 4:pos + 8]
        pl = d[pos + 8:pos + 8 + ln]
        if tag == b"IHDR":
            w, h, bit_depth, color_type = struct.unpack(">IIBB", pl[:10])
        elif tag == b"IDAT":
            idat += pl
        pos += 12 + ln
    if bit_depth != 8:
        raise SystemExit("只支持 8bit PNG")
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}.get(color_type)
    if channels is None:
        raise SystemExit(f"不支持的 color_type {color_type}")

    raw = zlib.decompress(idat)
    stride = w * channels
    out = bytearray()
    prev = bytearray(stride)
    i = 0
    for _ in range(h):
        ft = raw[i]; i += 1
        line = bytearray(raw[i:i + stride]); i += stride
        if ft == 1:
            for x in range(channels, stride):
                line[x] = (line[x] + line[x - channels]) & 255
        elif ft == 2:
            for x in range(stride):
                line[x] = (line[x] + prev[x]) & 255
        elif ft == 3:
            for x in range(stride):
                a = line[x - channels] if x >= channels else 0
                line[x] = (line[x] + ((a + prev[x]) >> 1)) & 255
        elif ft == 4:
            for x in range(stride):
                a = line[x - channels] if x >= channels else 0
                b = prev[x]
                c = prev[x - channels] if x >= channels else 0
                pp = a + b - c
                pa, pb, pc = abs(pp - a), abs(pp - b), abs(pp - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[x] = (line[x] + pr) & 255
        out += line
        prev = line
    return w, h, channels, out


def write_png(path, w, h, rgb_rows):
    def chunk(tag, pl):
        return (struct.pack(">I", len(pl)) + tag + pl +
                struct.pack(">I", zlib.crc32(tag + pl) & 0xFFFFFFFF))
    body = b"".join(b"\x00" + r for r in rgb_rows)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(body, 6)))
        f.write(chunk(b"IEND", b""))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("src", help="输入 PNG 路径")
    ap.add_argument("--box", default=None, help="裁剪框 x0,y0,x1,y1（屏幕像素）")
    ap.add_argument("--zoom", type=int, default=4, help="放大倍数（最近邻，默认 4）")
    ap.add_argument("--out", default=None, help="输出路径（默认 <src>_zoom.png）")
    args = ap.parse_args()

    src = args.src if os.path.isabs(args.src) else os.path.join(ROOT, args.src)
    box = DEFAULT_BOX
    if args.box:
        try:
            box = tuple(int(v) for v in args.box.split(","))
            assert len(box) == 4
        except (ValueError, AssertionError):
            raise SystemExit("--box 需要形如 x0,y0,x1,y1")

    w, h, ch, px = decode_png(src)
    x0, y0, x1, y1 = box
    x0, x1 = max(0, min(x0, x1)), min(w, max(x0, x1))
    y0, y1 = max(0, min(y0, y1)), min(h, max(y0, y1))
    cw, chh = x1 - x0, y1 - y0
    z = max(1, args.zoom)

    rows = []
    for y in range(y0, y1):
        src_row = px[y * w * ch:(y + 1) * w * ch]
        row = bytearray()
        for x in range(x0, x1):
            o = x * ch
            if ch >= 3:
                rgb = bytes(src_row[o:o + 3])
            else:
                g = src_row[o]
                rgb = bytes((g, g, g))
            row += rgb * z
        rows.append(bytes(row))
    big = [r for r in rows for _ in range(z)]

    out = args.out or (os.path.splitext(src)[0] + "_zoom.png")
    if not os.path.isabs(out):
        out = os.path.join(ROOT, out)
    write_png(out, cw * z, chh * z, big)
    print(f"{os.path.relpath(out, ROOT)}  {cw * z}x{chh * z}  (源 {cw}x{chh} 放大 {z}x)")


if __name__ == "__main__":
    main()
