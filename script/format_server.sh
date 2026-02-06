#!/usr/bin/env bash
# Format all C/C++ sources in the server folder using clang-format.

set -e
set -u
set -o pipefail

script_dir="$(cd "$(dirname "$0")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"
server_dir="$repo_root/server"

if [[ ! -d "$server_dir" ]]; then
  echo "Server directory not found: $server_dir" >&2
  exit 1
fi

formatter_cmd=()
if [[ -n "${CF_CMD:-}" ]]; then
  read -r -a formatter_cmd <<< "$CF_CMD"
else
  for candidate in clang-format clang-format-18 clang-format-17 clang-format-16 \
                   clang-format-15 clang-format-14 clang-format-13; do
    if command -v "$candidate" >/dev/null 2>&1; then
      formatter_cmd=("$candidate" "-i" "--style=file")
      break
    fi
  done
fi

if (( ${#formatter_cmd[@]} == 0 )); then
  echo "No formatter found. Install clang-format or set CF_CMD." >&2
  exit 1
fi

files=()
while IFS= read -r file; do
  files+=("$file")
done < <(
  find "$server_dir" \
    -name 'build-*' -prune -o \
    -path "$server_dir/.cache" -prune -o \
    -type f \( \
      -name '*.c' -o -name '*.cc' -o -name '*.cxx' -o -name '*.cpp' -o -name '*.c++' -o \
      -name '*.h' -o -name '*.hh' -o -name '*.hpp' -o -name '*.hxx' -o -name '*.h++' -o \
      -name '*.inl' \
    \) \
    -print
)

if (( ${#files[@]} == 0 )); then
  echo "No C/C++ source files found under $server_dir."
  exit 0
fi

echo "Formatting ${#files[@]} file(s) under $server_dir using ${formatter_cmd[*]}..."
for file in "${files[@]}"; do
  echo "Formatting $file"
  "${formatter_cmd[@]}" "$file"
done
