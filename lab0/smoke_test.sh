#!/bin/bash

# NAME: Joe Qian
# EMAIL: qzy@g.ucla.edu
# ID: 404816794

echo "Running smoke tests" >&2

# Test when --input and --output are provided
INPUT=$(mktemp lab0-in.txt.XXXXXX)
echo "Hello" > $INPUT
./lab0 --input "$INPUT" --output "${INPUT}-out" || exit 1
cmp "$INPUT" "${INPUT}-out" || exit 1
rm -f "$INPUT" "${INPUT}-out"
echo "1. Ok" >&2

# Test when only --input is provided
INPUT=$(mktemp lab0-in.txt.XXXXXX)
echo "Hello" > $INPUT
./lab0 --input "$INPUT" > "${INPUT}-out" || exit 1
cmp "$INPUT" "${INPUT}-out" || exit 1
rm -f "$INPUT" "${INPUT}-out"
echo "2. Ok" >&2

# Test when only --output is provided
INPUT=$(mktemp lab0-in.txt.XXXXXX)
echo "Hello" > $INPUT
./lab0 --output "${INPUT}-out" < "$INPUT" || exit 1
cmp "$INPUT" "${INPUT}-out" || exit 1
rm -f "$INPUT" "${INPUT}-out"
echo "3. Ok" >&2

# Test large files
INPUT=$(mktemp lab0-in.txt.XXXXXX)
dd if=/dev/urandom of="$INPUT" bs=1048576 count=2 2>/dev/null
./lab0 --input "$INPUT" --output "${INPUT}-out" || exit 1
cmp "$INPUT" "${INPUT}-out" || exit 1
rm -f "$INPUT" "${INPUT}-out"
echo "4. Ok" >&2

# Test empty files
OUTPUT=$(mktemp -u lab0-out.txt.XXXXXX)
./lab0 --input /dev/null --output "${OUTPUT}" || exit 1
[ -f "$OUTPUT" ] || exit 1
[ -s "$OUTPUT" ] && exit 1
rm -f "$OUTPUT"
echo "5. Ok" >&2

# Test segfaults
ERRMSG=$(mktemp lab0-stderr.txt.XXXXXX)
./lab0 --segfault --catch 2> $ERRMSG
[ $? -eq 4 ] || exit 1
grep -iq "segmentation fault" $ERRMSG || exit 1
rm -f $ERRMSG
echo "6. Ok" >&2
