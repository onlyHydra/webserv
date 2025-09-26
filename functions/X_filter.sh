#!/bin/sh

main_file="X_used_trimmed.txt"
filter1="filter_allowed_c.txt"
filter2="filter_forbidden_cpp.txt"
filter3="filter_forbidden_c.txt"
filter4="filter_allowed_cpp.txt"

# Use grep to exclude lines that match any pattern in the filter file
grep -Fvxf "$filter1" "$main_file" | grep -Fvxf "$filter2" | grep -Fvxf "$filter3" | grep -Fvxf "$filter4"
