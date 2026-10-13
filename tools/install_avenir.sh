#!/bin/sh
# Use Apple's Avenir (what the original game's text is set in) for the local build.
# The fonts are licensed, so they stay out of git; without them the build uses Roboto from software/fonts/.
set -e
cd "$(dirname "$0")/.."
mkdir -p software/data
python3 tools/extract_font.py /System/Library/Fonts/Avenir.ttc 0 software/data/ui_font.ttf
python3 tools/extract_font.py "/System/Library/Fonts/Avenir Next.ttc" 7 software/data/word_font.ttf
