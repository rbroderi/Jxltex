#!/bin/sh
# Typesets test.tex with every engine that is installed, in test/out/ENGINE, and checks
# that the PDF holds every image. Run from the top directory: sh test/run.sh
# It needs jxl2pdf built there (make), and pdfimages (poppler) for the check.
set -u
here=$(cd "$(dirname "$0")" && pwd)
PATH="$here/..:$PATH"
TEXINPUTS="$here/..:${TEXINPUTS:-}"
export PATH TEXINPUTS
command -v jxl2pdf >/dev/null || { echo "jxl2pdf not found: run make first" >&2; exit 1; }

status=0
ran=0
for engine in pdflatex lualatex xelatex; do
	command -v "$engine" >/dev/null || { echo "$engine: not installed, skipped"; continue; }
	ran=$((ran + 1))
	dir="$here/out/$engine"
	rm -rf "$dir"
	mkdir -p "$dir"
	cp "$here"/*.jxl "$here/test.tex" "$dir"
	if ! (cd "$dir" && "$engine" -shell-escape -interaction=nonstopmode -halt-on-error test.tex >run.log 2>&1); then
		echo "$engine: FAILED, see $dir/run.log"
		status=1
		continue
	fi
	# without shell escape, the converted files are reused
	if ! (cd "$dir" && "$engine" -no-shell-escape -interaction=nonstopmode -halt-on-error test.tex >run2.log 2>&1); then
		echo "$engine: FAILED without shell escape, see $dir/run2.log"
		status=1
		continue
	fi
	if command -v pdfimages >/dev/null; then
		# 8 colour images (photo twice) and 2 soft masks
		n=$(pdfimages -list "$dir/test.pdf" | awk 'NR > 2 && $3 == "image"' | wc -l)
		m=$(pdfimages -list "$dir/test.pdf" | awk 'NR > 2 && $3 == "smask"' | wc -l)
		if [ "$n" -ne 8 ] || [ "$m" -ne 2 ]; then
			echo "$engine: FAILED, $n images and $m soft masks in test.pdf, expected 8 and 2"
			status=1
			continue
		fi
	fi
	echo "$engine: ok"
done
[ "$ran" -gt 0 ] || { echo "no LaTeX engine found" >&2; exit 1; }
exit $status
