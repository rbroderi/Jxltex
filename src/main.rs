// jxl2pdf: decode a JPEG XL file and write it as a PDF, one page the size of the image, so
// that pdfLaTeX, LuaLaTeX and XeLaTeX can include it (see jxl.sty).
//
// The pixels are stored losslessly at 8 bits a sample: Flate-compressed with PNG row
// filters, with a soft mask when any pixel is transparent. sRGB images are written as
// DeviceRGB, or DeviceGray when every pixel is grey; any other colour space is kept and
// described by an ICC profile.
//
// Part of Jxltex, https://github.com/rbroderi/Jxltex
// Copyright (C) 2026 the Jxltex contributors
// SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE)

use std::fmt::Write as _;
use std::fs;
use std::process::ExitCode;

use jxl_oxide::color::{ColourEncoding, ColourSpace, TransferFunction, WhitePoint};
use jxl_oxide::{EnumColourEncoding, JxlImage, Render, RenderingIntent};

const USAGE: &str = "\
Usage: jxl2pdf [-a] [-r DPI] input.jxl output.pdf
  -a      write every frame of an animation, one page each (default: the first)
  -r DPI  pixels per inch, which sets the page size (default: 72, a point a pixel)
  -V      print the version";

struct Options {
    all_frames: bool,
    dpi: f64,
    input: String,
    output: String,
}

enum Parsed {
    Run(Options),
    Version,
    Usage,
}

fn parse_args(args: &[String]) -> Result<Parsed, String> {
    let mut all_frames = false;
    let mut dpi = 72.0;
    let mut files = Vec::new();
    let mut only_files = false;
    let mut it = args.iter();
    while let Some(arg) = it.next() {
        if only_files || !arg.starts_with('-') || arg == "-" {
            files.push(arg.clone());
            continue;
        }
        match arg.as_str() {
            "--" => only_files = true,
            "-a" => all_frames = true,
            "-r" => {
                let value = it.next().ok_or("-r needs a resolution")?;
                dpi = match value.parse::<f64>() {
                    Ok(v) if (1.0..=100_000.0).contains(&v) => v,
                    _ => return Err(format!("bad resolution {value}")),
                };
            }
            "-V" | "--version" => return Ok(Parsed::Version),
            "-h" | "--help" => return Ok(Parsed::Usage),
            _ => return Err(format!("unknown option {arg}")),
        }
    }
    match <[String; 2]>::try_from(files) {
        Ok([input, output]) => Ok(Parsed::Run(Options {
            all_frames,
            dpi,
            input,
            output,
        })),
        Err(_) => Err("expected an input file and an output file".into()),
    }
}

/// How the colour samples of every page are described in the PDF.
enum ColorSpace {
    /// sRGB or sRGB-like grey: DeviceRGB or DeviceGray, decided for each page.
    Device,
    /// Anything else: an ICC profile for `channels` colour channels.
    Icc { profile: Vec<u8>, channels: usize },
}

/// The image's own colour space when a PDF can hold it as it is; otherwise (CMYK, HDR)
/// asks the decoder for sRGB.
fn choose_color_space(image: &mut JxlImage) -> ColorSpace {
    let plain = match &image.image_header().metadata.colour_encoding {
        ColourEncoding::Enum(e) => {
            e.is_srgb()
                || (e.colour_space == ColourSpace::Grey
                    && e.white_point == WhitePoint::D65
                    && e.tf == TransferFunction::Srgb)
        }
        ColourEncoding::IccProfile(_) => false,
    };
    if plain {
        return ColorSpace::Device;
    }
    if image.pixel_format().has_black() || image.hdr_type().is_some() {
        image.request_color_encoding(EnumColourEncoding::srgb(RenderingIntent::Relative));
        return ColorSpace::Device;
    }
    let channels = if image.pixel_format().is_grayscale() {
        1
    } else {
        3
    };
    ColorSpace::Icc {
        profile: image.rendered_icc(),
        channels,
    }
}

fn paeth(a: u8, b: u8, c: u8) -> u8 {
    let (a16, b16, c16) = (i16::from(a), i16::from(b), i16::from(c));
    let p = a16 + b16 - c16;
    let (pa, pb, pc) = ((p - a16).abs(), (p - b16).abs(), (p - c16).abs());
    if pa <= pb && pa <= pc {
        a
    } else if pb <= pc {
        b
    } else {
        c
    }
}

/// Applies PNG row filters (PDF's /Predictor 15) to rows of `row_len` bytes with `bpp` bytes
/// a pixel, choosing for each row the filter with the smallest sum of absolute residuals.
fn filter_rows(pixels: &[u8], row_len: usize, bpp: usize) -> Vec<u8> {
    let mut out = Vec::with_capacity(pixels.len() + pixels.len() / row_len.max(1));
    let zeros = vec![0u8; row_len];
    let mut trial = vec![0u8; row_len];
    let mut best = vec![0u8; row_len];
    let mut up: &[u8] = &zeros;
    for row in pixels.chunks_exact(row_len) {
        let mut best_type = 0u8;
        let mut best_sum = u64::MAX;
        for filter in 0..5u8 {
            for i in 0..row_len {
                let a = if i >= bpp { row[i - bpp] } else { 0 };
                let b = up[i];
                let c = if i >= bpp { up[i - bpp] } else { 0 };
                let predicted = match filter {
                    0 => 0,
                    1 => a,
                    2 => b,
                    3 => ((u16::from(a) + u16::from(b)) / 2) as u8,
                    _ => paeth(a, b, c),
                };
                trial[i] = row[i].wrapping_sub(predicted);
            }
            let sum: u64 = trial
                .iter()
                .map(|&v| u64::from(v.min(v.wrapping_neg())))
                .sum();
            if sum < best_sum {
                best_sum = sum;
                best_type = filter;
                std::mem::swap(&mut trial, &mut best);
            }
        }
        out.push(best_type);
        out.extend_from_slice(&best);
        up = row;
    }
    out
}

/// A PDF file being built in memory.
struct Pdf {
    data: Vec<u8>,
    /// Byte offset of each object; object numbers start at 1.
    offsets: Vec<usize>,
}

impl Pdf {
    fn new() -> Self {
        let mut pdf = Pdf {
            data: b"%PDF-1.4\n%\xe2\xe3\xcf\xd3\n".to_vec(),
            offsets: Vec::new(),
        };
        pdf.add("<< /Type /Catalog /Pages 2 0 R >>", None);
        pdf.offsets.push(0); // 2 is the page tree, written last
        pdf
    }

    /// Writes object `id`, whose number is already taken, with an optional stream.
    fn object(&mut self, id: usize, dict: &str, stream: Option<&[u8]>) {
        self.offsets[id - 1] = self.data.len();
        self.data
            .extend_from_slice(format!("{id} 0 obj\n{dict}\n").as_bytes());
        if let Some(stream) = stream {
            self.data.extend_from_slice(b"stream\n");
            self.data.extend_from_slice(stream);
            self.data.extend_from_slice(b"\nendstream\n");
        }
        self.data.extend_from_slice(b"endobj\n");
    }

    /// Writes a new object and returns its number.
    fn add(&mut self, dict: &str, stream: Option<&[u8]>) -> usize {
        self.offsets.push(0);
        let id = self.offsets.len();
        self.object(id, dict, stream);
        id
    }

    /// Writes an image of 8-bit samples, `colors` to a pixel. `extra` goes in its dictionary.
    fn image(
        &mut self,
        pixels: &[u8],
        width: usize,
        colors: usize,
        space: &str,
        extra: &str,
    ) -> usize {
        let filtered = filter_rows(pixels, width * colors, colors);
        let stream = miniz_oxide::deflate::compress_to_vec_zlib(&filtered, 8);
        let height = pixels.len() / (width * colors);
        let dict = format!(
            "<< /Type /XObject /Subtype /Image /Width {width} /Height {height} /BitsPerComponent 8\n\
             /ColorSpace {space}{extra} /Filter /FlateDecode\n\
             /DecodeParms << /Predictor 15 /Colors {colors} /BitsPerComponent 8 /Columns {width} >>\n\
             /Length {} >>",
            stream.len()
        );
        self.add(&dict, Some(&stream))
    }

    fn finish(mut self, pages: &[usize]) -> Vec<u8> {
        let kids: Vec<String> = pages.iter().map(|p| format!("{p} 0 R")).collect();
        let dict = format!(
            "<< /Type /Pages /Count {} /Kids [{}] >>",
            pages.len(),
            kids.join(" ")
        );
        self.object(2, &dict, None);
        let xref = self.data.len();
        let mut tail = format!("xref\n0 {}\n0000000000 65535 f \n", self.offsets.len() + 1);
        for offset in &self.offsets {
            let _ = writeln!(tail, "{offset:010} 00000 n ");
        }
        let _ = write!(
            tail,
            "trailer\n<< /Size {} /Root 1 0 R >>\nstartxref\n{xref}\n%%EOF\n",
            self.offsets.len() + 1
        );
        self.data.extend_from_slice(tail.as_bytes());
        self.data
    }
}

/// A length in PDF units, with up to four decimals.
fn pdf_number(v: f64) -> String {
    let s = format!("{v:.4}");
    s.trim_end_matches('0').trim_end_matches('.').to_string()
}

/// Writes one frame as a page and returns the page's object number. `icc` is the object
/// that holds the ICC profile, when the colour space needs one.
fn write_page(
    pdf: &mut Pdf,
    render: &Render,
    dpi: f64,
    icc: Option<usize>,
    alpha_associated: bool,
) -> Result<usize, String> {
    let mut stream = render.stream();
    let (width, height) = (stream.width() as usize, stream.height() as usize);
    let channels = stream.channels() as usize;
    if width == 0 || height == 0 {
        return Err("the image is empty".into());
    }
    // colour channels, then alpha if the image has it
    let mut colors = match channels {
        1 | 2 => 1,
        3 | 4 => 3,
        n => return Err(format!("cannot write an image of {n} channels")),
    };
    let has_alpha = channels == colors + 1;
    let mut samples = vec![0u8; width * height * channels];
    stream.write_to_buffer(&mut samples);

    let mut color = Vec::with_capacity(width * height * colors);
    let mut alpha = Vec::new();
    if has_alpha {
        alpha.reserve(width * height);
        for px in samples.chunks_exact(channels) {
            color.extend_from_slice(&px[..colors]);
            alpha.push(px[colors]);
        }
    } else {
        color = samples;
    }
    if icc.is_none()
        && colors == 3
        && color
            .as_chunks::<3>()
            .0
            .iter()
            .all(|p| p[0] == p[1] && p[1] == p[2])
    {
        color = color.as_chunks::<3>().0.iter().map(|p| p[0]).collect();
        colors = 1;
    }

    let mut extra = String::new();
    if alpha.iter().any(|&a| a != 255) {
        // samples multiplied by alpha are stored as they are, and /Matte says so
        let matte = match (alpha_associated, colors) {
            (false, _) => "",
            (true, 1) => " /Matte [0]",
            (true, _) => " /Matte [0 0 0]",
        };
        let mask = pdf.image(&alpha, width, 1, "/DeviceGray", matte);
        extra = format!(" /SMask {mask} 0 R");
    }
    let space = match icc {
        Some(id) => format!("[/ICCBased {id} 0 R]"),
        None if colors == 1 => "/DeviceGray".to_string(),
        None => "/DeviceRGB".to_string(),
    };
    let image = pdf.image(&color, width, colors, &space, &extra);

    let w = pdf_number(width as f64 * 72.0 / dpi);
    let h = pdf_number(height as f64 * 72.0 / dpi);
    let content = format!("q {w} 0 0 {h} 0 0 cm /Im0 Do Q");
    let contents = pdf.add(
        &format!("<< /Length {} >>", content.len()),
        Some(content.as_bytes()),
    );
    Ok(pdf.add(
        &format!(
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 {w} {h}]\n\
             /Resources << /XObject << /Im0 {image} 0 R >> >> /Contents {contents} 0 R >>"
        ),
        None,
    ))
}

fn convert(options: &Options) -> Result<(), String> {
    let mut image = JxlImage::builder()
        .open(&options.input)
        .map_err(|e| e.to_string())?;
    let space = choose_color_space(&mut image);
    let alpha_associated = image
        .image_header()
        .metadata
        .ec_info
        .iter()
        .find_map(|ec| ec.alpha_associated())
        .unwrap_or(false);

    let frames = image.num_loaded_keyframes();
    if frames == 0 {
        return Err("no image in the file, or the file is cut short".into());
    }
    let mut pdf = Pdf::new();
    let icc = match &space {
        ColorSpace::Device => None,
        ColorSpace::Icc { profile, channels } => {
            let stream = miniz_oxide::deflate::compress_to_vec_zlib(profile, 8);
            let alternate = if *channels == 1 {
                "/DeviceGray"
            } else {
                "/DeviceRGB"
            };
            let dict = format!(
                "<< /N {channels} /Alternate {alternate} /Filter /FlateDecode /Length {} >>",
                stream.len()
            );
            Some(pdf.add(&dict, Some(&stream)))
        }
    };
    let mut pages = Vec::new();
    for index in 0..if options.all_frames { frames } else { 1 } {
        let render = image.render_frame(index).map_err(|e| e.to_string())?;
        pages.push(write_page(
            &mut pdf,
            &render,
            options.dpi,
            icc,
            alpha_associated,
        )?);
    }
    // written in one go, so a failure never leaves a broken PDF for LaTeX to find
    fs::write(&options.output, pdf.finish(&pages))
        .map_err(|e| format!("cannot write {}: {e}", options.output))
}

fn main() -> ExitCode {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let options = match parse_args(&args) {
        Ok(Parsed::Run(options)) => options,
        Ok(Parsed::Version) => {
            println!("jxl2pdf {}", env!("CARGO_PKG_VERSION"));
            return ExitCode::SUCCESS;
        }
        Ok(Parsed::Usage) => {
            println!("{USAGE}");
            return ExitCode::SUCCESS;
        }
        Err(message) => {
            eprintln!("jxl2pdf: {message}\n{USAGE}");
            return ExitCode::from(2);
        }
    };
    match convert(&options) {
        Ok(()) => ExitCode::SUCCESS,
        Err(message) => {
            eprintln!("jxl2pdf: {}: {message}", options.input);
            ExitCode::FAILURE
        }
    }
}
