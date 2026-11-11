# Jxltex

`\includegraphics` for `.jxl` files in pdfLaTeX, LuaLaTeX and XeLaTeX.

None of the three engines reads JPEG XL. Jxltex adds it from the outside, the way the
`epstopdf` package adds EPS to pdfLaTeX:

- `jxl2pdf` decodes a `.jxl` file and writes a PDF, one page the size of the image.
- `jxl.sty` runs `jxl2pdf` on each `.jxl` file you include, then includes the PDF. All
  three engines include PDF themselves, so one package serves them all.

```latex
\documentclass{article}
\usepackage{jxl}
\begin{document}
\includegraphics[width=5cm]{photo.jxl}
\includegraphics{figure}   % finds figure.jxl if no figure.pdf, .png or .jpg exists
\end{document}
```

```sh
pdflatex -shell-escape paper.tex     # or lualatex, or xelatex
```

## Setting up

Build the converter and put it on your `PATH`:

```sh
make                 # builds ./jxl2pdf with cc; needs only a C99 compiler
```

With Open Watcom, `wmake -f watcom.mak nt` builds `jxl2pdfw.exe` (Win32) and
`wmake -f watcom.mak dos` builds `jxl2pdf.exe` (DOS/32A). Rename `jxl2pdfw.exe` to
`jxl2pdf.exe`, or pass `converter=jxl2pdfw` to the package.

Then put `jxl.sty` where LaTeX finds it: next to your document, or in
`~/texmf/tex/latex/jxl/`. It needs `graphicx`, `pdftexcmds` and `kvoptions`, which
every TeX distribution has.

## How the conversion works

`photo.jxl` becomes `photo-jxl-converted-to.pdf`, next to `photo.jxl`. The package
converts when that file is missing or older than the `.jxl`, so each image is
converted once, not on every run.

Running the converter needs shell escape (`-shell-escape`). Without it the package
uses the converted files that are already there, and stops with an error if one is
missing. So you can typeset once with `-shell-escape`, and after that without, or
send the converted PDFs to a service that doesn't allow shell escape. You can also
convert by hand:

```sh
jxl2pdf photo.jxl photo-jxl-converted-to.pdf
```

The PDF holds the decoded pixels losslessly (Flate with PNG row filters), as greyscale
when every pixel is grey and RGB otherwise. Transparency becomes a PDF soft mask, so
what is under the image shows through. Expect the PDF to be somewhat larger than a PNG
of the same image, and several times larger than a lossy `.jxl`.

## Options

Give these to `\usepackage[...]{jxl}`, or later to `\jxlsetup{...}`:

| Option | Default | Meaning |
|---|---|---|
| `converter=` | `jxl2pdf` | The command to run. |
| `outdir=` | | Write converted files to this directory, which has to exist, not next to the `.jxl`. |
| `suffix=` | `-jxl-converted-to` | What to add to the name of a converted file. |
| `dpi=` | 72 | Pixels per inch, which gives the image's natural size. At 72, a pixel is 1 bp. |
| `allframes` | off | Convert every frame of an animation, one page each. Pick one with `\includegraphics[page=2]{film.jxl}`. Otherwise only the first frame is converted. |

`dpi` and `allframes` only take effect when a file is converted. After changing them,
delete the converted PDFs.

`jxl2pdf` takes the same two as `-r DPI` and `-a`:

```
jxl2pdf [-a] [-r DPI] input.jxl output.pdf
```

## Limits

- The decoder is J40 as patched by jxlOpenWatcom, with the limits listed in
  [its README](https://github.com/rbroderi/jxlOpenWatcom#what-decodes): lossless images decode exactly, many lossy ones decode but without libjxl's
  restoration filters, and some files (small lossy images, cjxl effort 1, patches,
  splines, progressive) fail. A file that fails stops LaTeX with an error; nothing
  wrong is put on the page. Output is 8 bits a sample.
- Colours are written as DeviceRGB or DeviceGray. An ICC profile or a non-sRGB colour
  space in the `.jxl` is ignored.
- Only the PDF-producing drivers are supported: pdfLaTeX and LuaLaTeX in PDF mode, and
  XeLaTeX. With `latex` + `dvips` the package warns and does nothing.
- A `.jxl` file has to be reachable by the name LaTeX builds from the current
  directory and `\graphicspath`. Files that only `TEXINPUTS` finds aren't converted.
- If you call `\DeclareGraphicsExtensions` after loading the package, include `.jxl`
  in the list, or give the extension in `\includegraphics`.

## Testing

`make check` typesets `test/test.tex` with each of the three engines that is
installed, first with shell escape and then without, and checks with poppler's
`pdfimages` that every image is in the result.

Tested with TeX Live 2023 (pdfTeX 1.40.25, LuaHBTeX 1.17.0, XeTeX 0.999995) on Linux,
with `jxl2pdf` built by GCC. Images were written by cjxl 0.7: the pixels in the PDFs
match the source PNGs exactly for lossless RGB, RGBA, grey and grey-with-alpha files
at efforts 2, 4 and 7, and for a three-frame animation. The Open Watcom targets in
`watcom.mak` have not been built or run yet.

## The decoder

`j40.h` is copied unchanged from [jxlOpenWatcom](https://github.com/rbroderi/jxlOpenWatcom)
at commit `f12d841`. That is a fork of [J40](https://github.com/lifthrasiir/j40) by Kang
Seonghoon, with fixes for greyscale, animation and prefix codes. To update it, copy the
newer `j40.h` over this one.

## License

Jxltex is licensed under the GNU General Public License, version 3 or (at your option)
any later version. See [`LICENSE`](LICENSE). Upstream J40 is public domain, or MIT No
Attribution where that is not possible; see [`LICENSE-J40.txt`](LICENSE-J40.txt).
