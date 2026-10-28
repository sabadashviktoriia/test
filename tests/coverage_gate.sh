#!/bin/sh
# Перевіряє поріг покриття для firmware/App/bringup.c за звітом gcov.
# Використання: sh coverage_gate.sh <gcov_report.txt> <мін. % рядків> <мін. % гілок>
report="$1"; min_lines="$2"; min_branches="$3"
awk -v ml="$min_lines" -v mb="$min_branches" '
  /^File .*bringup\.c/ { f = 1; next }
  f && /^Lines executed/    { sub(/.*:/, ""); lines = $0 + 0 }
  f && /^Taken at least once/ { sub(/.*:/, ""); branches = $0 + 0; f = 0 }
  END {
    printf("Покриття bringup.c: рядки %.2f%% (поріг %s%%), гілки %.2f%% (поріг %s%%)\n", lines, ml, branches, mb)
    if (lines < ml || branches < mb) { print "ПОРІГ ПОКРИТТЯ НЕ ДОСЯГНУТО"; exit 1 }
    print "Поріг покриття досягнуто"
  }' "$report"
