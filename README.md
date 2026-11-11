# Jxltex

Jxltex lets you use JPEG XL images in LaTeX documents: `\includegraphics{photo.jxl}`
works in pdfLaTeX, LuaLaTeX and XeLaTeX.

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

## What it is

The TeX engines read only a few image formats themselves: PDF, PNG and JPEG. Every
other format reaches LaTeX through a package that runs a converter while the document
is typeset. Jxltex is one of these:

| Package | Adds | By running |
|---|---|---|
| [`epstopdf`](https://ctan.org/pkg/epstopdf-pkg) | EPS in pdfLaTeX | the `epstopdf` script (Ghostscript) |
| [`svg`](https://ctan.org/pkg/svg) | SVG | Inkscape |
| [`minted`](https://ctan.org/pkg/minted) | highlighted code | Pygments |
| **Jxltex** (`jxl.sty`) | JPEG XL | `jxl2pdf`, which comes with it |

It is modelled on `epstopdf`, and if you have used that package you know how this one
behaves. `epstopdf` turns `figure.eps` into `figure-eps-converted-to.pdf` the first
time the figure is included, and includes that PDF from then on. `jxl.sty` does the
same for `photo.jxl` and `photo-jxl-converted-to.pdf`, with the same `outdir` and
`suffix` options.

Two things differ from `epstopdf`:

- **The converter comes with the package.** `epstopdf` needs Ghostscript; `jxl2pdf` is
  one statically linked program with nothing to install beside it.
- **You have to allow shell escape.** TeX Live lets `epstopdf` run in its default,
  restricted mode, because it is on TeX Live's list of trusted programs. `jxl2pdf` is
  not on that list, so run LaTeX with `-shell-escape` (`minted` and `svg` need the
  same).

## Installing

There are two files to install: the program `jxl2pdf` and the package `jxl.sty`. Both
are in the archive for your system on the
[latest release](https://github.com/rbroderi/Jxltex/releases/latest) page:

| Archive | System |
|---|---|
| `jxl2pdf-…-x86_64-linux.tar.gz` | Linux, x86-64 |
| `jxl2pdf-…-aarch64-linux.tar.gz` | Linux, ARM64 |
| `jxl2pdf-…-macos.tar.gz` | macOS 11 or later, Apple Silicon and Intel |
| `jxl2pdf-…-x86_64-windows.zip` | Windows, x86-64 |

### Linux and macOS

```sh
tar -xzf jxl2pdf-*.tar.gz
cd jxl2pdf-*/

# 1. the program, somewhere on your PATH
sudo install -m 755 jxl2pdf /usr/local/bin/

# 2. the package, in your personal TeX tree
dir="$(kpsewhich -var-value TEXMFHOME)/tex/latex/jxl"
mkdir -p "$dir" && cp jxl.sty "$dir"
```

Without `sudo`, any directory on your `PATH` does, such as `~/.local/bin`.

macOS marks downloaded programs as quarantined and refuses to run them. Clear the mark
once: `xattr -d com.apple.quarantine /usr/local/bin/jxl2pdf`.

### Windows

1. Unpack the `.zip` (right-click, *Extract All*).
2. Put `jxl2pdf.exe` in a folder that is on your `PATH`. To make one: create
   `C:\Tools`, move `jxl2pdf.exe` there, then search the Start menu for *Edit
   environment variables for your account*, edit `Path` and add `C:\Tools`. Open a new
   terminal afterwards.
3. Put `jxl.sty` in your personal TeX tree:
   - TeX Live: run `kpsewhich -var-value TEXMFHOME`, and copy `jxl.sty` to
     `tex\latex\jxl\` under the folder it prints (create the folders).
   - MiKTeX: copy it to `tex\latex\jxl\` under a folder of your own, add that folder
     in MiKTeX Console under *Settings > Directories*, and click *Refresh file name
     database*.

### Checking, and other ways to install

```sh
jxl2pdf -V            # prints: jxl2pdf 1.0.0
kpsewhich jxl.sty     # prints where LaTeX finds the package
```

- **One document only:** put `jxl.sty` next to the `.tex` file; it then needs no TeX
  tree. `jxl2pdf` can sit there too if you load the package with
  `\usepackage[converter=./jxl2pdf]{jxl}`.
- **From source:** with [Rust](https://rustup.rs) installed, `cargo install --git
  https://github.com/rbroderi/Jxltex` builds `jxl2pdf` and puts it in `~/.cargo/bin`.
  `jxl.sty` is in the repository.
- **Dependencies:** `jxl.sty` needs `graphicx`, `pdftexcmds` and `kvoptions`, which
  every TeX distribution has. `jxl2pdf` needs nothing.

### Running LaTeX

Add `-shell-escape` to the command your editor or build tool runs:

```sh
pdflatex -shell-escape paper.tex
latexmk -pdf -shell-escape paper.tex
```

In TeXstudio, TeXworks, VS Code's LaTeX Workshop and the like, it goes in the
settings where the `pdflatex`/`lualatex`/`xelatex` command is defined.

## How it works

`photo.jxl` becomes `photo-jxl-converted-to.pdf`, next to `photo.jxl`. The package
converts when that file is missing or older than the `.jxl`, so each image is
converted once, not on every run.

As with `epstopdf`, shell escape is only needed to run the converter. Without it
the package uses the converted files that are already there, and stops with an error
if one is missing. So you can typeset once with `-shell-escape`, and after that without, or
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
