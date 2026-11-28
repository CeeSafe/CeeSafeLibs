#!/bin/sh
# Runs vectorcheck over RFC 8251's twelve test vectors, stereo and mono, and opus_compare on each.
#   run_vectors.sh <vectors dir> <opus_compare> [first [last]]
dir=$1; compare=$2; first=${3:-1}; last=${4:-12}
here=$(cd "$(dirname "$0")" && pwd)
check=$here/vectorcheck/out/vectorcheck
tmp=${TMPDIR:-/tmp}/opus-vectors.$$; mkdir -p "$tmp"
fail=0
for n in $(seq -f %02g "$first" "$last"); do
    for ch in 2 1; do
        ref=$dir/testvector$n.dec; [ $ch = 1 ] && ref=$dir/testvector${n}m.dec
        flag=""; [ $ch = 1 ] && flag="-r 48000"
        r=$("$check" "$dir/testvector$n.bit" "$tmp/$n-$ch.pcm" $ch | tail -1)
        if [ $ch = 2 ]; then v=$("$compare" -s "$ref" "$tmp/$n-$ch.pcm" 2>&1 | tail -2 | tr '\n' ' ');
        else v=$("$compare" "$ref" "$tmp/$n-$ch.pcm" 2>&1 | tail -2 | tr '\n' ' '); fi
        echo "$n ch$ch: $r; $v"
        echo "$v" | grep -q PASSES || fail=1
    done
done
rm -rf "$tmp"
exit $fail
