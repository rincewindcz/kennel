#!/bin/bash
# Builds the kennel-read fonts in assets/fonts/read from google/fonts at a pinned commit: every font
# is subset to Latin, Greek, Cyrillic and common punctuation, then variable fonts are instanced to
# static Regular, Bold, Italic and BoldItalic (other axes at their defaults). OpenType layout tables
# are dropped: kennel-read draws glyph by glyph without shaping and SDL_ttf kerning reads only the
# legacy kern table. Literata and Inter come from their upstream static releases and are subset in place.
# Needs curl and fonttools.
set -euo pipefail

repo=$(cd "$(dirname "$0")/.." && pwd)
out=$repo/assets/fonts/read
commit=45e3d5269e4a17d823f6d84470bc084dc5ae46f6
# Fixed head.modified, so a rebuild changes no file.
export SOURCE_DATE_EPOCH=0
base=https://raw.githubusercontent.com/google/fonts/$commit/ofl
unicodes="U+0020-007E,U+00A0-024F,U+0259,U+02B0-02FF,U+0300-036F,U+0370-03FF,U+0400-052F,U+1E00-1EFF,"
unicodes+="U+2000-206F,U+20A0-20CF,U+2100-214F,U+2190-21FF,U+2200-22FF,U+25CA,U+FB00-FB06,U+FFFD"

tmp=$(mktemp -d)
trap 'rm -rf -- "$tmp"' EXIT

fetch() {
    local file=$tmp/$(basename "$1")
    [[ -s $file ]] || curl -fsSL -o "$file" "$base/$(sed 's/\[/%5B/; s/\]/%5D/' <<<"$1")"
    printf '%s\n' "$file"
}

subset() {
    pyftsubset "$1" --unicodes="$unicodes" --layout-features='' --name-IDs='*' --notdef-outline --output-file="$2"
}

# family dir upright italic: variable fonts with a wght axis
variable() {
    local family=$1 dir=$2 upright italic
    upright=$tmp/upright.ttf
    italic=$tmp/italic.ttf
    subset "$(fetch "$dir/$3")" "$upright"
    subset "$(fetch "$dir/$4")" "$italic"
    for spec in Regular:"$upright":400 Bold:"$upright":700 Italic:"$italic":400 BoldItalic:"$italic":700; do
        IFS=: read -r style src weight <<<"$spec"
        fonttools varLib.instancer --static --update-name-table -q -o "$out/$family-$style.ttf" "$src" wght="$weight"
    done
    cp "$(fetch "$dir/OFL.txt")" "$out/$family-OFL.txt"
}

# family dir prefix: static fonts named <prefix>-<Style>.ttf
static() {
    local family=$1 dir=$2 prefix=$3
    for style in Regular Bold Italic BoldItalic; do
        subset "$(fetch "$dir/$prefix-$style.ttf")" "$out/$family-$style.ttf"
    done
    cp "$(fetch "$dir/OFL.txt")" "$out/$family-OFL.txt"
}

mkdir -p "$out"
for family in Literata Inter; do
    for style in Regular Bold Italic BoldItalic; do
        subset "$out/$family-$style.ttf" "$tmp/trimmed.ttf"
        mv "$tmp/trimmed.ttf" "$out/$family-$style.ttf"
    done
done
variable Alegreya alegreya "Alegreya[wght].ttf" "Alegreya-Italic[wght].ttf"
variable Atkinson atkinsonhyperlegiblenext "AtkinsonHyperlegibleNext[wght].ttf" "AtkinsonHyperlegibleNext-Italic[wght].ttf"
variable Bitter bitter "Bitter[wght].ttf" "Bitter-Italic[wght].ttf"
static CharisSIL charissil CharisSIL
variable CrimsonPro crimsonpro "CrimsonPro[wght].ttf" "CrimsonPro-Italic[wght].ttf"
variable EBGaramond ebgaramond "EBGaramond[wght].ttf" "EBGaramond-Italic[wght].ttf"
variable Lora lora "Lora[wght].ttf" "Lora-Italic[wght].ttf"
variable Merriweather merriweather "Merriweather[opsz,wdth,wght].ttf" "Merriweather-Italic[opsz,wdth,wght].ttf"
static PTSerif ptserif PT_Serif-Web
variable SourceSerif sourceserif4 "SourceSerif4[opsz,wght].ttf" "SourceSerif4-Italic[opsz,wght].ttf"
