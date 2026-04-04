#!/bin/bash
# Convert a video file to MPEG-1 format optimized for FRANK OS Video Player
# Usage: ./convert_video.sh input.mp4 [output.mpg]

set -e

if [ -z "$1" ]; then
    echo "Usage: $0 <input_video> [output.mpg]"
    echo "Converts video to 180x138 MPEG-1 optimized for FRANK OS."
    echo "Output is written next to input file if not specified."
    exit 1
fi

INPUT="$1"

if [ ! -f "$INPUT" ]; then
    echo "Error: '$INPUT' not found"
    exit 1
fi

if [ -n "$2" ]; then
    OUTPUT="$2"
else
    DIR="$(dirname "$INPUT")"
    BASE="$(basename "$INPUT" | sed 's/\.[^.]*$//')"
    OUTPUT="${DIR}/${BASE}.mpg"
fi

ffmpeg -y -i "$INPUT" \
    -vf "scale=180:138" \
    -c:v mpeg1video -q:v 15 -g 1 -bf 0 \
    -c:a mp2 -b:a 64k -ar 32000 -ac 1 \
    -f mpeg "$OUTPUT"

echo "Output: $OUTPUT"
