#!/bin/sh
# Typesets test.tex with every LaTeX engine that is installed, in test/out/ENGINE, first
# with shell escape (which runs jxl2pdf) and then without (which reuses its output).
#   sh test/run.sh [directory that holds jxl2pdf]
# Without the argument, jxl2pdf comes from target/release or the PATH. If pdfimages
# (poppler) is installed, the script also checks that the PDF holds every image.
# Set ENGINES to try only some, e.g. ENGINES="pdflatex lualatex".
set -u
here=$(cd "$(dirname "$0")" && pwd)
if [ $# -gt 0 ]; then
	PATH="$(cd "$1" && pwd):$PATH"
elif [ -d "$here/../target/release" ]; then
	PATH="$here/../target/release:$PATH"
fi
export PATH
command -v jxl2pdf >/dev/null || { echo "jxl2pdf not found: run cargo build --release first" >&2; exit 1; }
jxl2pdf -V

status=0
ran=0
for engine in ${ENGINES:-pdflatex lualatex xelatex}; do
	command -v "$engine" >/dev/null || { echo "$engine: not installed, skipped"; continue; }
	ran=$((ran + 1))
	dir="$here/out/$engine"
	rm -rf "$dir"
	mkdir -p "$dir"
	cp "$here"/*.jxl "$here/test.tex" "$here/../jxl.sty" "$dir"
	if ! (cd "$dir" && "$engine" -shell-escape -interaction=nonstopmode -halt-on-error test.tex >run.log 2>&1); then
		echo "$engine: FAILED"
		tail -n 40 "$dir/run.log"
		status=1
		continue
	fi
	if ! (cd "$dir" && "$engine" -no-shell-escape -interaction=nonstopmode -halt-on-error test.tex >run2.log 2>&1); then
		echo "$engine: FAILED without shell escape"
		tail -n 40 "$dir/run2.log"
		status=1
		continue
	fi
	if command -v pdfimages >/dev/null; then
		# 10 colour images (photo twice) and 2 soft masks
		n=$(pdfimages -list "$dir/test.pdf" | awk 'NR > 2 && $3 == "image"' | wc -l)
		m=$(pdfimages -list "$dir/test.pdf" | awk 'NR > 2 && $3 == "smask"' | wc -l)
		if [ "$n" -ne 10 ] || [ "$m" -ne 2 ]; then
			echo "$engine: FAILED, $n images and $m soft masks in test.pdf, expected 10 and 2"
			status=1
			continue
		fi
	fi
	echo "$engine: ok"
done
[ "$ran" -gt 0 ] || { echo "no LaTeX engine found" >&2; exit 1; }
exit $status
