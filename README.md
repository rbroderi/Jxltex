# Jxltex

`\includegraphics` for `.jxl` files in pdfLaTeX, LuaLaTeX and XeLaTeX.

None of the three engines reads JPEG XL. Jxltex adds it from the outside, the way the
`epstopdf` package adds EPS to pdfLaTeX:

- `jxl2pdf`, a small program with no dependencies, decodes a `.jxl` file and writes a
  PDF, one page the size of the image.
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

1. Get `jxl2pdf`. Download the archive for your system from the
   [latest release](https://github.com/rbroderi/Jxltex/releases/latest), unpack it, and
   put `jxl2pdf` (`jxl2pdf.exe` on Windows) in a directory on your `PATH`.

   | Archive | System |
   |---|---|
   | `jxl2pdf-…-x86_64-linux.tar.gz` | Linux, x86-64 |
   | `jxl2pdf-…-aarch64-linux.tar.gz` | Linux, ARM64 |
   | `jxl2pdf-…-macos.tar.gz` | macOS 11 or later, Apple Silicon and Intel |
   | `jxl2pdf-…-x86_64-windows.zip` | Windows, x86-64 |

   The binaries are statically linked: there is nothing else to install. On macOS,
   a downloaded binary may be quarantined; `xattr -d com.apple.quarantine jxl2pdf`
   clears that.

   Or build it yourself with [Rust](https://rustup.rs): `cargo build --release` leaves
   it in `target/release`.

2. Put `jxl.sty`, which is in the archive too, where LaTeX finds it: next to your
   document, or in `~/texmf/tex/latex/jxl/`. It needs `graphicx`, `pdftexcmds` and
   `kvoptions`, which every TeX distribution has.

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

The PDF holds the decoded pixels losslessly, at 8 bits a sample (Flate with PNG row
filters). Transparency becomes a PDF soft mask, so what is under the image shows
through. Expect the PDF to be about the size of a PNG of the same image, and several
times larger than a lossy `.jxl`.

Colour: an sRGB image is written as DeviceRGB, or DeviceGray when every pixel is grey.
An image in another colour space, such as Display P3, keeps its samples and gets an ICC
profile that describes them. CMYK and HDR (PQ, HLG) images are converted to sRGB.

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
jxl2pdf -V
```

## Limits

- Output is 8 bits a sample, whatever the file holds.
- Only the PDF-producing drivers are supported: pdfLaTeX and LuaLaTeX in PDF mode, and
  XeLaTeX. With `latex` + `dvips` the package warns and does nothing.
- A `.jxl` file has to be reachable by the name LaTeX builds from the current
  directory and `\graphicspath`. Files that only `TEXINPUTS` finds aren't converted.
- If you call `\DeclareGraphicsExtensions` after loading the package, include `.jxl`
  in the list, or give the extension in `\includegraphics`.
- Not tested, for want of files: CMYK images, and images whose colours are stored
  multiplied by alpha.

## The decoder

`jxl2pdf` is written in Rust and decodes with
[jxl-oxide](https://github.com/tirr-c/jxl-oxide), a JPEG XL decoder in pure Rust. With
no C code in it, static binaries for every platform come out of one `cargo build` each.

## Testing

```sh
cargo build --release
python3 test/check.py target/release/jxl2pdf   # the pixels jxl2pdf writes
sh test/run.sh                                 # \includegraphics in each engine
```

`test/check.py` converts the images in `test/`, reads the pixels back out of the PDFs
and compares them with the PNGs the `.jxl` files were made from. It covers lossless
RGB, RGBA and grey with alpha, an animation, a Display P3 image and a lossy image
(whose pixels aren't compared). `test/run.sh` typesets `test/test.tex` with each of the
three engines that is installed, first with shell escape and then without.

Three workflows run these on GitHub:

- **CI** on every push: builds and checks on Linux, macOS and Windows, and runs the
  three engines on Linux.
- **Release** builds the four archives, publishes them as release `v` + the version in
  `Cargo.toml`, and then runs the third workflow. Start it from the Actions tab, or
  push the tag.
- **Test release** downloads a published release and runs both tests with it on
  Linux (x86-64 and ARM64), macOS (Apple Silicon and Intel) and Windows.

## License

Jxltex is licensed under the GNU General Public License, version 3 or (at your option)
any later version. See [`LICENSE`](LICENSE).
