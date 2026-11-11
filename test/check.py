#!/usr/bin/env python3
"""Checks jxl2pdf against the test images: python3 test/check.py path/to/jxl2pdf

Each image is converted, the pixels are read back out of the PDF, and their SHA-256 is
compared with the one of the PNG the .jxl file was made from (EXPECTED below). Only the
standard library is used, so this runs anywhere Python does.
"""
import hashlib
import os
import re
import subprocess
import sys
import tempfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))

# file: (options, colour space, [page: (width, height, colours, pixels, alpha)])
# pixels and alpha are SHA-256 of the samples; None for pixels means "not checked" (lossy),
# None for alpha means the page must have no soft mask.
EXPECTED = {
    'photo.jxl': ([], '/DeviceRGB', [
        (200, 150, 3, '3d418d5cc49f845a9ad1e0ef3a5c42cf6e40482f577bd5a6ef1152b6f5c15bac', None),
    ]),
    'alpha.jxl': ([], '/DeviceRGB', [
        (123, 77, 3, '392bf8be98bb92ab673ef25f5af121fe505507d142ab7d68ca93c6914a1ddfc3', '9ec1d76ff1a43adb9dca1977dc735ffc1bd41fb773e8e6805fa75cfa4ce564df'),
    ]),
    'grey.jxl': ([], '/DeviceGray', [
        (64, 64, 1, 'e1fcd8bdde4d48d374042c39fde4ad7cfa71a0ba00dda5a1b37614d9ebd4dfa9', 'abe17009c983b4c0f0040fd8d5ca6a00a0b37185dd740cc60f5406a58ce65581'),
    ]),
    'flat art.jxl': ([], '/DeviceRGB', [
        (300, 200, 3, 'ac3b0dc9dec81a099b0dc2bcb13a45b4a6f5f65f2fa663cdedceced86e368e1e', None),
    ]),
    'anim.jxl': (['-a'], '/DeviceRGB', [
        (96, 64, 3, '46642532f20f912c47172804a0201f4e60988a0645376dc7ab6e49a7fb4eec3f', None),
        (96, 64, 3, '3f581e9e0a3601b02dc8cb5f21189dbb543213e02ab3661e1e4a89b2238eb511', None),
        (96, 64, 3, '7c1d6c4d72566398dd76418c828d99e87db5f7d5103f988299281fd0497d4ce9', None),
    ]),
    'p3.jxl': ([], '/ICCBased', [
        (200, 150, 3, '3d418d5cc49f845a9ad1e0ef3a5c42cf6e40482f577bd5a6ef1152b6f5c15bac', None),
    ]),
    'lossy.jxl': (['-r', '144'], '/DeviceRGB', [
        (200, 150, 3, None, None),
    ]),
}


def unfilter(data, width, height, colors):
    """Undoes the PNG row filters of PDF's /Predictor 15."""
    n = width * colors
    out = bytearray()
    prev = bytes(n)
    for y in range(height):
        kind = data[y * (n + 1)]
        row = bytearray(data[y * (n + 1) + 1:(y + 1) * (n + 1)])
        for i in range(n):
            a = row[i - colors] if i >= colors else 0
            b = prev[i]
            c = prev[i - colors] if i >= colors else 0
            if kind == 0:
                p = 0
            elif kind == 1:
                p = a
            elif kind == 2:
                p = b
            elif kind == 3:
                p = (a + b) // 2
            else:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                p = a if pa <= pb and pa <= pc else b if pb <= pc else c
            row[i] = (row[i] + p) & 255
        out += row
        prev = row
    return bytes(out)


def images(pdf):
    """Yields (object number, dictionary, samples) for each image in a PDF from jxl2pdf."""
    pattern = rb"(\d+) 0 obj\n(<< /Type /XObject /Subtype /Image.*?/Length (\d+) >>)\nstream\n"
    for m in re.finditer(pattern, pdf, re.S):
        d = m.group(2).decode("latin-1")
        raw = zlib.decompress(pdf[m.end():m.end() + int(m.group(3))])
        width = int(re.search(r"/Width (\d+)", d).group(1))
        height = int(re.search(r"/Height (\d+)", d).group(1))
        colors = int(re.search(r"/Colors (\d+)", d).group(1))
        if len(raw) != height * (width * colors + 1):
            raise ValueError("image data has the wrong length")
        yield int(m.group(1)), d, (width, height, colors, unfilter(raw, width, height, colors))


def check(converter, name, options, space, pages, tmp):
    out = os.path.join(tmp, name + ".pdf")
    subprocess.run([converter, *options, os.path.join(HERE, name), out], check=True)
    with open(out, "rb") as f:
        pdf = f.read()
    if not pdf.startswith(b"%PDF-") or not pdf.rstrip().endswith(b"%%EOF"):
        return "not a PDF"
    if len(re.findall(rb"/Type /Page /", pdf)) != len(pages):
        return "wrong number of pages"
    found = {num: (d, img) for num, d, img in images(pdf)}
    masks = {int(m.group(1)) for d, _ in found.values() for m in [re.search(r"/SMask (\d+) 0 R", d)] if m}
    colour = [num for num in sorted(found) if num not in masks]
    if len(colour) != len(pages):
        return "wrong number of images"
    for num, (width, height, colors, pixels, alpha) in zip(colour, pages):
        d, (w, h, c, data) = found[num]
        if (w, h, c) != (width, height, colors):
            return "image is %dx%d with %d colours, expected %dx%d with %d" % (w, h, c, width, height, colors)
        if space not in d:
            return "colour space is not " + space
        if pixels and hashlib.sha256(data).hexdigest() != pixels:
            return "pixels differ"
        m = re.search(r"/SMask (\d+) 0 R", d)
        if bool(m) != bool(alpha):
            return "soft mask " + ("missing" if alpha else "not expected")
        if m:
            mw, mh, mc, mdata = found[int(m.group(1))][1]
            if (mw, mh, mc) != (width, height, 1) or hashlib.sha256(mdata).hexdigest() != alpha:
                return "alpha differs"
    return None


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    converter = os.path.abspath(sys.argv[1])
    failed = 0
    with tempfile.TemporaryDirectory() as tmp:
        for name, (options, space, pages) in EXPECTED.items():
            try:
                problem = check(converter, name, options, space, pages, tmp)
            except Exception as e:  # a failed run, or a PDF this script cannot read
                problem = "%s: %s" % (type(e).__name__, e)
            print("%-14s %s" % (name, problem or "ok"))
            failed += problem is not None
        # a file that cannot be decoded must fail and leave nothing behind
        bad = os.path.join(tmp, "bad.jxl")
        out = os.path.join(tmp, "bad.pdf")
        with open(os.path.join(HERE, "photo.jxl"), "rb") as f, open(bad, "wb") as g:
            g.write(f.read()[:3000])
        code = subprocess.run([converter, bad, out], stderr=subprocess.DEVNULL).returncode
        problem = None if code == 1 and not os.path.exists(out) else "exit code %d" % code
        print("%-14s %s" % ("(cut short)", problem or "ok"))
        failed += problem is not None
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
