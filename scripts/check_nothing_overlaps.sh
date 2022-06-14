#!/bin/bash

set -euo pipefail

# usage instructions to stderr, with optional exit code as an argument
usage() {
  cat<<EOF 1>&2
Usage $(basename $0) [-f] [-j num_threads] [-h] input.step [BASE]
  -f  force overwriting of files
  -j  specify number of threads to use
  -h  show this help

Output files will be written to files prefixed by BASE.  For example,  if run
as:

  $(basename $0) source.step out/part-

then the discovered solids will be written into "out/part-geometry.csv".  Note
that if BASE is unspecified, output will be written to the current directory
and prefixed by the name of the input, e.g., source-geometry.csv.
EOF
  exit ${1-}
}

# command line processing
check_exists=1
oc_threads=-j

while getopts "hfj:" arg; do
  case "$arg" in
    j) oc_threads="-j$OPTARG" ;;
    f) check_exists= ;;
    h) usage ;;
    *) usage 1 ;;
  esac
done

shift "$(($OPTIND -1))"

# check input file exists
if [ -f "${1-}" ]; then
  source="$1"
else
  echo "Error: Input file \"${1-}\" does not exist" 1>&2
  exit 1
fi

# get output template name
if [ -n "${2-}" ]; then
  base="$2"
else
  base="${source##*/}"
  base="${base%.*}-"
fi

# assemble output file names
brep="${base}linear.brep"
geometry="${base}geometry.csv"
overlaps="${base}overlaps.csv"
common="${base}common.brep"
imprinted="${base}imprinted.brep"
merged="${base}merged.brep"

# check if output files already exist
if [ "$check_exists" ]; then
  for path in "$brep" "$geometry" "$overlaps" "$common" "$imprinted" "$merged"; do
    if [ -f "$path" ]; then
      echo "Error: output file \"$path\" already exists" 1>&2
      fail=1
    fi
  done
  if [ "${fail-}" ]; then
    exit 1
  fi
fi

echo "1/5: Linearising solids into $brep" 1>&2
step_to_brep "$source" "$brep" > "$geometry"

echo "2/5: Checking for intersecting solids" 1>&2
overlap_checker "$brep" "$oc_threads" > "$overlaps"

if grep -q overlap "$overlaps"; then
  echo "3/5: Writing overlapping solds into $common" 1>&2
  grep overlap "$overlaps" | overlap_collecter "$brep" "$common"
else
  echo "3/5: No overlaps found, $common will not be written"
  rm -f "$common"
fi

echo "4/5: Removing overlaps and writing to $imprinted" 1>&2
imprint_solids "$brep" "$imprinted" < "$overlaps"

echo "5/5: Merging faces, edges and verticies and writing to $merged" 1>&2
merge_solids "$imprinted" "$merged"
