#!/bin/bash
# Downloads and generates kennel-read test books into build/testbooks (existing files are kept).
#   Project Gutenberg: War and Peace (large EPUB and TXT), Pride and Prejudice (EPUB, TXT, single-file
#   HTML with images to drop), R.U.R. by Karel Capek (Czech, EPUB and TXT)
#   Generated from Pride and Prejudice by scripts/mkbooks.py: FB2, PalmDOC PDB (compressed and
#   uncompressed), MOBI (uncompressed UTF-8)
set -euo pipefail

repo=$(cd "$(dirname "$0")/.." && pwd)
out=$repo/build/testbooks
mkdir -p "$out"

fetch() {
    local url=$1 file=$out/$2
    [[ -s $file ]] && return
    curl -fsSL -o "$file.part" "$url"
    mv "$file.part" "$file"
}
fetch https://www.gutenberg.org/ebooks/2600.epub.noimages "War and Peace.epub"
fetch https://www.gutenberg.org/ebooks/2600.txt.utf-8 "War and Peace.txt"
fetch https://www.gutenberg.org/ebooks/1342.epub.noimages "Pride and Prejudice.epub"
fetch https://www.gutenberg.org/ebooks/1342.txt.utf-8 "Pride and Prejudice.txt"
fetch https://www.gutenberg.org/cache/epub/1342/pg1342-images.html "Pride and Prejudice.html"
fetch https://www.gutenberg.org/ebooks/13083.epub.noimages "R.U.R.epub"
fetch https://www.gutenberg.org/ebooks/13083.txt.utf-8 "R.U.R.txt"
[[ -s $out/R.U.R.epub ]] && mv "$out/R.U.R.epub" "$out/R.U.R. (Czech).epub"
[[ -s $out/R.U.R.txt ]] && mv "$out/R.U.R.txt" "$out/R.U.R. (Czech).txt"

src="$out/Pride and Prejudice.txt"
for kind in fb2:"Pride and Prejudice (FB2).fb2" pdb:"Pride and Prejudice (PalmDOC).pdb" \
            pdb0:"Pride and Prejudice (PalmDOC plain).pdb" mobi:"Pride and Prejudice (MOBI).mobi"; do
    file=$out/${kind#*:}
    [[ -s $file ]] || python3 -I "$repo/scripts/mkbooks.py" "${kind%%:*}" "$src" "$file"
done
ls -l "$out"
