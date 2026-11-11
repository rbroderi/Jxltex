/* jxl2pdf: decode a JPEG XL file with J40 and write it as a PDF, one page the size of the
 * image, so that pdfLaTeX, LuaLaTeX and XeLaTeX can include it (see jxl.sty). The pixels
 * are stored losslessly: Flate-compressed with PNG row filters, as DeviceGray when the
 * image is grey and DeviceRGB otherwise, with a soft mask when any pixel is transparent.
 * It needs nothing but j40.h and the C library; the deflate encoder is in this file.
 * Part of Jxltex, https://github.com/rbroderi/Jxltex
 * Copyright (C) 2026 the Jxltex contributors
 * SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE)
 */
#define J40_CONFIRM_THAT_THIS_IS_EXPERIMENTAL_AND_POTENTIALLY_UNSAFE
#define J40_IMPLEMENTATION
#include "j40.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- growable output buffer with a bit writer ---- */

typedef struct {
	uint8_t *p;
	size_t n, cap;
	uint32_t bits;
	int nbits;
	int failed;
} obuf;

static void ob_byte(obuf *o, int b) {
	if (o->failed) return;
	if (o->n == o->cap) {
		size_t cap = o->cap ? o->cap * 2 : 4096;
		uint8_t *p = cap > o->cap ? (uint8_t *) realloc(o->p, cap) : NULL;
		if (!p) {
			o->failed = 1;
			return;
		}
		o->p = p;
		o->cap = cap;
	}
	o->p[o->n++] = (uint8_t) b;
}

/* deflate packs bits starting from the least significant bit of each byte */
static void ob_bits(obuf *o, uint32_t v, int n) {
	o->bits |= v << o->nbits;
	o->nbits += n;
	while (o->nbits >= 8) {
		ob_byte(o, (int) (o->bits & 255));
		o->bits >>= 8;
		o->nbits -= 8;
	}
}

static void ob_flush_bits(obuf *o) {
	if (o->nbits > 0) ob_byte(o, (int) (o->bits & 255));
	o->bits = 0;
	o->nbits = 0;
}

/* ---- Huffman codes ---- */

#define MAX_SYMS 286

/* Code lengths of at most maxbits for the given frequencies. At least two symbols get a
 * code, so that every code is complete, as inflaters require. */
static void huff_lengths(const uint32_t *freq0, int n, int maxbits, uint8_t *len) {
	uint32_t freq[MAX_SYMS], w[2 * MAX_SYMS];
	int parent[2 * MAX_SYMS], active[2 * MAX_SYMS];
	int i, used = 0;

	for (i = 0; i < n; ++i) {
		freq[i] = freq0[i];
		if (freq[i]) ++used;
	}
	for (i = 0; used < 2; ++i) {
		if (!freq[i]) {
			freq[i] = 1;
			++used;
		}
	}
	for (;;) {
		int m = n, left = used, maxlen = 0;
		for (i = 0; i < n; ++i) {
			w[i] = freq[i];
			active[i] = freq[i] != 0;
			parent[i] = -1;
		}
		while (left > 1) {
			int a = -1, b = -1; /* the two lightest active nodes, a the lighter */
			for (i = 0; i < m; ++i) {
				if (!active[i]) continue;
				if (a < 0 || w[i] < w[a]) {
					b = a;
					a = i;
				} else if (b < 0 || w[i] < w[b]) {
					b = i;
				}
			}
			w[m] = w[a] + w[b];
			active[a] = active[b] = 0;
			active[m] = 1;
			parent[a] = parent[b] = m;
			parent[m] = -1;
			++m;
			--left;
		}
		for (i = 0; i < n; ++i) {
			int d = 0, k;
			if (freq[i]) {
				for (k = i; parent[k] >= 0; k = parent[k]) ++d;
			}
			len[i] = (uint8_t) d;
			if (d > maxlen) maxlen = d;
		}
		if (maxlen <= maxbits) return;
		/* too deep: flatten the distribution and try again */
		for (i = 0; i < n; ++i) {
			if (freq[i]) freq[i] = (freq[i] + 1) / 2;
		}
	}
}

/* Canonical codes for the lengths, bit-reversed because Huffman codes go out first bit first. */
static void huff_codes(const uint8_t *len, int n, uint16_t *code) {
	int count[16], next[16], i, bits;
	unsigned c = 0;

	for (i = 0; i < 16; ++i) count[i] = 0;
	for (i = 0; i < n; ++i) ++count[len[i]];
	count[0] = 0;
	next[0] = 0;
	for (bits = 1; bits < 16; ++bits) {
		c = (c + (unsigned) count[bits - 1]) << 1;
		next[bits] = (int) c;
	}
	for (i = 0; i < n; ++i) {
		unsigned v = (unsigned) next[len[i]], r = 0;
		int k;
		if (!len[i]) {
			code[i] = 0;
			continue;
		}
		++next[len[i]];
		for (k = 0; k < len[i]; ++k) {
			r = (r << 1) | (v & 1);
			v >>= 1;
		}
		code[i] = (uint16_t) r;
	}
}

/* ---- deflate ---- */

static const uint16_t LEN_BASE[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59,
	67, 83, 99, 115, 131, 163, 195, 227, 258};
static const uint8_t LEN_EXTRA[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4,
	5, 5, 5, 5, 0};
static const uint16_t DIST_BASE[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513,
	769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const uint8_t DIST_EXTRA[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10,
	11, 11, 12, 12, 13, 13};
static const uint8_t CL_ORDER[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

static int len_sym(int len) {
	int s = 28;
	while (LEN_BASE[s] > len) --s;
	return s;
}

static int dist_sym(int dist) {
	int s = 29;
	while (DIST_BASE[s] > dist) --s;
	return s;
}

/* A token is a literal byte, or TOK_MATCH | (length - 3) << 16 | (distance - 1). */
#define TOK_MATCH 0x80000000u
#define BLOCK_TOKENS 16384

/* Run-length codes the code lengths (code-length symbols 16 to 18 carry extra bits, kept in
 * the high byte of each entry) and returns how many entries there are. */
static int rle_lengths(const uint8_t *len, int n, uint16_t *out) {
	int i = 0, m = 0;
	while (i < n) {
		int run = 1;
		while (i + run < n && len[i + run] == len[i]) ++run;
		if (len[i] == 0 && run >= 3) {
			if (run > 138) run = 138;
			out[m++] = run >= 11 ? (uint16_t) (18 | (run - 11) << 8) : (uint16_t) (17 | (run - 3) << 8);
		} else if (run >= 4) {
			/* the length itself, then "repeat the previous length" 3 to 6 times */
			if (run > 7) run = 7;
			out[m++] = len[i];
			out[m++] = (uint16_t) (16 | (run - 4) << 8);
		} else {
			run = 1;
			out[m++] = len[i];
		}
		i += run;
	}
	return m;
}

static void write_block(obuf *o, const uint32_t *tok, int ntok, int final) {
	uint32_t lfreq[MAX_SYMS], dfreq[30], cfreq[19];
	uint8_t llen[MAX_SYMS + 30], clen[19];
	uint16_t lcode[MAX_SYMS], dcode[30], ccode[19], rle[MAX_SYMS + 30];
	int i, nl, nd, nc, nrle;

	memset(lfreq, 0, sizeof lfreq);
	memset(dfreq, 0, sizeof dfreq);
	memset(cfreq, 0, sizeof cfreq);
	for (i = 0; i < ntok; ++i) {
		if (tok[i] & TOK_MATCH) {
			++lfreq[257 + len_sym((int) ((tok[i] >> 16) & 255) + 3)];
			++dfreq[dist_sym((int) (tok[i] & 0xffff) + 1)];
		} else {
			++lfreq[tok[i]];
		}
	}
	lfreq[256] = 1; /* end of block */
	huff_lengths(lfreq, MAX_SYMS, 15, llen);
	huff_lengths(dfreq, 30, 15, llen + MAX_SYMS);
	huff_codes(llen, MAX_SYMS, lcode);
	huff_codes(llen + MAX_SYMS, 30, dcode);

	/* the two sets of lengths are sent as one run-length coded sequence */
	for (nl = MAX_SYMS; nl > 257 && !llen[nl - 1]; --nl) {}
	for (nd = 30; nd > 1 && !llen[MAX_SYMS + nd - 1]; --nd) {}
	memmove(llen + nl, llen + MAX_SYMS, (size_t) nd);
	nrle = rle_lengths(llen, nl + nd, rle);
	for (i = 0; i < nrle; ++i) ++cfreq[rle[i] & 255];
	huff_lengths(cfreq, 19, 7, clen);
	huff_codes(clen, 19, ccode);
	for (nc = 19; nc > 4 && !clen[CL_ORDER[nc - 1]]; --nc) {}

	ob_bits(o, (uint32_t) final, 1);
	ob_bits(o, 2, 2); /* dynamic Huffman codes */
	ob_bits(o, (uint32_t) (nl - 257), 5);
	ob_bits(o, (uint32_t) (nd - 1), 5);
	ob_bits(o, (uint32_t) (nc - 4), 4);
	for (i = 0; i < nc; ++i) ob_bits(o, clen[CL_ORDER[i]], 3);
	for (i = 0; i < nrle; ++i) {
		int s = rle[i] & 255;
		ob_bits(o, ccode[s], clen[s]);
		if (s == 16) ob_bits(o, (uint32_t) (rle[i] >> 8), 2);
		if (s == 17) ob_bits(o, (uint32_t) (rle[i] >> 8), 3);
		if (s == 18) ob_bits(o, (uint32_t) (rle[i] >> 8), 7);
	}
	for (i = 0; i < ntok; ++i) {
		if (tok[i] & TOK_MATCH) {
			int len = (int) ((tok[i] >> 16) & 255) + 3, dist = (int) (tok[i] & 0xffff) + 1;
			int ls = len_sym(len), ds = dist_sym(dist);
			ob_bits(o, lcode[257 + ls], llen[257 + ls]);
			ob_bits(o, (uint32_t) (len - LEN_BASE[ls]), LEN_EXTRA[ls]);
			ob_bits(o, dcode[ds], llen[nl + ds]);
			ob_bits(o, (uint32_t) (dist - DIST_BASE[ds]), DIST_EXTRA[ds]);
		} else {
			ob_bits(o, lcode[tok[i]], llen[tok[i]]);
		}
	}
	ob_bits(o, lcode[256], llen[256]);
}

#define WINDOW 32768
#define HASH_BITS 15
#define MAX_CHAIN 48
#define HASH3(p) ((((uint32_t) (p)[0] << 16 | (uint32_t) (p)[1] << 8 | (p)[2]) * 0x9E3779B1u) >> (32 - HASH_BITS))

/* Appends src as a zlib stream (what PDF's FlateDecode expects). Returns 0 if out of memory. */
static int zlib_compress(obuf *o, const uint8_t *src, size_t n) {
	size_t *head, *prev, pos = 0, i;
	uint32_t *tok, s1 = 1, s2 = 0;
	int ntok = 0;

	head = (size_t *) calloc((size_t) 1 << HASH_BITS, sizeof(size_t)); /* position + 1, 0 if none */
	prev = (size_t *) calloc(WINDOW, sizeof(size_t));
	tok = (uint32_t *) malloc(BLOCK_TOKENS * sizeof(uint32_t));
	if (!head || !prev || !tok) {
		free(head);
		free(prev);
		free(tok);
		return 0;
	}
	ob_byte(o, 0x78);
	ob_byte(o, 0x9c);
	while (pos < n) {
		int best = 0, bestdist = 0, k;
		if (n - pos >= 3) {
			size_t cand = head[HASH3(src + pos)];
			int maxlen = n - pos < 258 ? (int) (n - pos) : 258, chain = MAX_CHAIN;
			while (cand && pos - (cand - 1) <= WINDOW && chain-- > 0) {
				const uint8_t *a = src + (cand - 1), *b = src + pos;
				if (a[best] == b[best]) {
					int l = 0;
					while (l < maxlen && a[l] == b[l]) ++l;
					if (l > best) {
						best = l;
						bestdist = (int) (pos - (cand - 1));
						if (l == maxlen) break;
					}
				}
				cand = prev[(cand - 1) & (WINDOW - 1)];
			}
		}
		if (best >= 3) {
			tok[ntok++] = TOK_MATCH | (uint32_t) (best - 3) << 16 | (uint32_t) (bestdist - 1);
		} else {
			best = 1;
			tok[ntok++] = src[pos];
		}
		for (k = 0; k < best; ++k, ++pos) {
			if (n - pos >= 3) {
				uint32_t h = HASH3(src + pos);
				prev[pos & (WINDOW - 1)] = head[h];
				head[h] = pos + 1;
			}
		}
		if (ntok == BLOCK_TOKENS) {
			write_block(o, tok, ntok, pos == n);
			ntok = 0;
		}
	}
	if (ntok > 0 || n == 0) write_block(o, tok, ntok, 1);
	ob_flush_bits(o);
	for (i = 0; i < n; ++i) { /* Adler-32 */
		s1 += src[i];
		if (s1 >= 65521) s1 -= 65521;
		s2 += s1;
		if (s2 >= 65521) s2 -= 65521;
	}
	ob_byte(o, (int) (s2 >> 8));
	ob_byte(o, (int) (s2 & 255));
	ob_byte(o, (int) (s1 >> 8));
	ob_byte(o, (int) (s1 & 255));
	free(head);
	free(prev);
	free(tok);
	return !o->failed;
}

/* ---- PNG row filters (PDF's /Predictor 15) ---- */

static int paeth(int a, int b, int c) {
	int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
	return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

static void filter_row(int type, const uint8_t *row, const uint8_t *up, int n, int bpp, uint8_t *out) {
	int i;
	for (i = 0; i < n; ++i) {
		int a = i >= bpp ? row[i - bpp] : 0, b = up ? up[i] : 0, c = up && i >= bpp ? up[i - bpp] : 0;
		int pred = type == 1 ? a : type == 2 ? b : type == 3 ? (a + b) / 2 : type == 4 ? paeth(a, b, c) : 0;
		out[i] = (uint8_t) (row[i] - pred);
	}
}

/* Filters h rows of n bytes each, choosing for each row the filter with the smallest sum of
 * absolute residuals. Returns h * (n + 1) bytes to free, or NULL if out of memory. */
static uint8_t *filter_image(const uint8_t *pix, int32_t h, int n, int bpp) {
	uint8_t *out = (uint8_t *) malloc((size_t) h * (size_t) (n + 1) + 1);
	uint8_t *trial = (uint8_t *) malloc((size_t) n + 1);
	int32_t y;

	if (!out || !trial) {
		free(out);
		free(trial);
		return NULL;
	}
	for (y = 0; y < h; ++y) {
		const uint8_t *row = pix + (size_t) y * (size_t) n, *up = y ? row - n : NULL;
		uint8_t *dst = out + (size_t) y * (size_t) (n + 1);
		unsigned long bestsum = 0;
		int type, i;
		for (type = 0; type < 5; ++type) {
			unsigned long sum = 0;
			filter_row(type, row, up, n, bpp, trial);
			for (i = 0; i < n; ++i) sum += trial[i] < 128 ? trial[i] : 256u - trial[i];
			if (type == 0 || sum < bestsum) {
				bestsum = sum;
				dst[0] = (uint8_t) type;
				memcpy(dst + 1, trial, (size_t) n);
			}
		}
	}
	free(trial);
	return out;
}

/* ---- PDF ---- */

typedef struct {
	FILE *f;
	long *offs; /* offs[i] is the file offset of object i + 1 */
	int nobjs, cap;
	int failed;
} pdf;

/* Starts the next object and returns its number. */
static int pdf_begin(pdf *d) {
	if (d->nobjs == d->cap) {
		int cap = d->cap ? d->cap * 2 : 64;
		long *p = (long *) realloc(d->offs, (size_t) cap * sizeof(long));
		if (!p) {
			d->failed = 1;
			return 0;
		}
		d->offs = p;
		d->cap = cap;
	}
	d->offs[d->nobjs] = ftell(d->f);
	fprintf(d->f, "%d 0 obj\n", d->nobjs + 1);
	return ++d->nobjs;
}

/* Writes an image XObject of 8-bit samples, `colors` to a pixel, and returns its object
 * number, or 0 on failure. extra is put in its dictionary. */
static int pdf_image(pdf *d, const uint8_t *pix, int32_t w, int32_t h, int colors, const char *extra) {
	obuf z;
	uint8_t *filtered;
	int id;

	memset(&z, 0, sizeof z);
	filtered = filter_image(pix, h, (int) w * colors, colors);
	if (!filtered || !zlib_compress(&z, filtered, (size_t) h * ((size_t) w * (size_t) colors + 1))) {
		free(filtered);
		free(z.p);
		d->failed = 1;
		return 0;
	}
	free(filtered);
	id = pdf_begin(d);
	fprintf(d->f, "<< /Type /XObject /Subtype /Image /Width %ld /Height %ld /BitsPerComponent 8\n"
		"/ColorSpace /Device%s%s /Filter /FlateDecode\n"
		"/DecodeParms << /Predictor 15 /Colors %d /BitsPerComponent 8 /Columns %ld >>\n"
		"/Length %lu >>\nstream\n",
		(long) w, (long) h, colors == 1 ? "Gray" : "RGB", extra, colors, (long) w, (unsigned long) z.n);
	fwrite(z.p, 1, z.n, d->f);
	fprintf(d->f, "\nendstream\nendobj\n");
	free(z.p);
	return id;
}

/* Prints a length in PDF units with up to four decimals. */
static void pdf_number(char *buf, double v) {
	char *p;
	sprintf(buf, "%.4f", v);
	p = buf + strlen(buf) - 1;
	while (*p == '0') *p-- = 0;
	if (*p == '.') *p = 0;
}

/* Writes one frame as a page; pagesobj is the object number the page tree will get. Returns
 * the page's object number, or 0 on failure. */
static int pdf_page(pdf *d, j40_frame *frame, double dpi, int pagesobj) {
	j40_pixels_u8x4 pixels = j40_frame_pixels_u8x4(frame, J40_RGBA);
	int32_t w = pixels.width, h = pixels.height, x, y;
	int grey = 1, opaque = 1, colors, image, mask = 0, contents, page;
	uint8_t *buf, *q;
	char extra[40], pw[32], ph[32], content[160];

	if (w <= 0 || h <= 0) return 0;
	for (y = 0; y < h; ++y) {
		const j40_u8x4 *row = j40_row_u8x4(pixels, y);
		for (x = 0; x < w; ++x) {
			if (row[x][0] != row[x][1] || row[x][0] != row[x][2]) grey = 0;
			if (row[x][3] != 255) opaque = 0;
		}
	}
	colors = grey ? 1 : 3;
	buf = (uint8_t *) malloc((size_t) w * (size_t) h * (size_t) colors);
	if (!buf) {
		d->failed = 1;
		return 0;
	}
	extra[0] = 0;
	if (!opaque) {
		q = buf;
		for (y = 0; y < h; ++y) {
			const j40_u8x4 *row = j40_row_u8x4(pixels, y);
			for (x = 0; x < w; ++x) *q++ = row[x][3];
		}
		mask = pdf_image(d, buf, w, h, 1, "");
		if (!mask) {
			free(buf);
			return 0;
		}
		sprintf(extra, " /SMask %d 0 R", mask);
	}
	q = buf;
	for (y = 0; y < h; ++y) {
		const j40_u8x4 *row = j40_row_u8x4(pixels, y);
		for (x = 0; x < w; ++x) {
			*q++ = row[x][0];
			if (!grey) {
				*q++ = row[x][1];
				*q++ = row[x][2];
			}
		}
	}
	image = pdf_image(d, buf, w, h, colors, extra);
	free(buf);
	if (!image) return 0;

	pdf_number(pw, (double) w * 72.0 / dpi);
	pdf_number(ph, (double) h * 72.0 / dpi);
	sprintf(content, "q %s 0 0 %s 0 0 cm /Im0 Do Q", pw, ph);
	contents = pdf_begin(d);
	fprintf(d->f, "<< /Length %d >>\nstream\n%s\nendstream\nendobj\n", (int) strlen(content), content);
	page = pdf_begin(d);
	fprintf(d->f, "<< /Type /Page /Parent %d 0 R /MediaBox [0 0 %s %s]\n"
		"/Resources << /XObject << /Im0 %d 0 R >> >> /Contents %d 0 R >>\nendobj\n",
		pagesobj, pw, ph, image, contents);
	return d->failed ? 0 : page;
}

static int usage(const char *prog) {
	fprintf(stderr,
		"Usage: %s [-a] [-r DPI] input.jxl output.pdf\n"
		"  -a      write every frame of an animation, one page each (default: the first)\n"
		"  -r DPI  pixels per inch, which sets the page size (default: 72, a point a pixel)\n",
		prog);
	return 2;
}

int main(int argc, char **argv) {
	j40_image image;
	pdf d;
	const char *prog = argv[0], *in, *out;
	double dpi = 72.0;
	int all = 0, npages = 0, pagescap = 0, *pages = NULL, i, ok = 1;
	long xref;

	for (++argv, --argc; argc > 0 && argv[0][0] == '-' && argv[0][1]; ++argv, --argc) {
		if (strcmp(argv[0], "-a") == 0) {
			all = 1;
		} else if (strcmp(argv[0], "-r") == 0 && argc > 1) {
			dpi = atof(argv[1]);
			if (!(dpi >= 1.0 && dpi <= 100000.0)) {
				fprintf(stderr, "%s: bad resolution %s\n", prog, argv[1]);
				return 2;
			}
			++argv;
			--argc;
		} else if (strcmp(argv[0], "--") == 0) {
			++argv;
			--argc;
			break;
		} else {
			return usage(prog);
		}
	}
	if (argc != 2) return usage(prog);
	in = argv[0];
	out = argv[1];

	memset(&d, 0, sizeof d);
	j40_from_file(&image, in);
	j40_output_format(&image, J40_RGBA, J40_U8X4);
	while (ok && j40_next_frame(&image)) {
		j40_frame frame = j40_current_frame(&image);
		int page;
		if (!d.f) {
			d.f = fopen(out, "wb");
			if (!d.f) {
				fprintf(stderr, "%s: cannot write %s\n", prog, out);
				j40_free(&image);
				return 1;
			}
			fprintf(d.f, "%%PDF-1.4\n%%\xe2\xe3\xcf\xd3\n");
			pdf_begin(&d); /* 1: the catalog; 2 is kept for the page tree, written last */
			fprintf(d.f, "<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");
			d.nobjs = 2;
		}
		if (npages == pagescap) {
			int cap = pagescap ? pagescap * 2 : 16;
			int *p = (int *) realloc(pages, (size_t) cap * sizeof(int));
			if (!p) {
				d.failed = 1;
				break;
			}
			pages = p;
			pagescap = cap;
		}
		page = pdf_page(&d, &frame, dpi, 2);
		if (!page) {
			ok = 0;
			break;
		}
		pages[npages++] = page;
		if (!all) break;
	}
	if (j40_error(&image) && (all || npages == 0)) {
		fprintf(stderr, "%s: %s: %s\n", prog, in, j40_error_string(&image));
		ok = 0;
	} else if (d.failed || !ok) {
		fprintf(stderr, "%s: %s: out of memory\n", prog, in);
		ok = 0;
	} else if (npages == 0) {
		fprintf(stderr, "%s: %s: no image\n", prog, in);
		ok = 0;
	}
	j40_free(&image);

	if (ok) {
		d.offs[1] = ftell(d.f); /* the page tree, object 2 */
		fprintf(d.f, "2 0 obj\n<< /Type /Pages /Count %d /Kids [", npages);
		for (i = 0; i < npages; ++i) fprintf(d.f, "%s%d 0 R", i ? " " : "", pages[i]);
		fprintf(d.f, "] >>\nendobj\n");
		xref = ftell(d.f);
		fprintf(d.f, "xref\n0 %d\n0000000000 65535 f \n", d.nobjs + 1);
		for (i = 0; i < d.nobjs; ++i) fprintf(d.f, "%010ld 00000 n \n", d.offs[i]);
		fprintf(d.f, "trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%ld\n%%%%EOF\n", d.nobjs + 1, xref);
		if (ferror(d.f)) {
			fprintf(stderr, "%s: error writing %s\n", prog, out);
			ok = 0;
		}
	}
	if (d.f) {
		if (fclose(d.f) != 0 && ok) {
			fprintf(stderr, "%s: error writing %s\n", prog, out);
			ok = 0;
		}
		if (!ok) remove(out); /* never leave a broken PDF for LaTeX to find */
	}
	free(d.offs);
	free(pages);
	return ok ? 0 : 1;
}
