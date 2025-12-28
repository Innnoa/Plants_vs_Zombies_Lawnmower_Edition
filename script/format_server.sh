#!/usr/bin/env zsh
# Format all C/C++ sources in the server folder using the cf alias from ~/.zshrc.

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

if [[ -f "$HOME/.zshrc" ]]; then
  # Ensure cf alias is available.
  source "$HOME/.zshrc"
fi

if ! alias cf >/dev/null 2>&1; then
  echo "cf alias is not available. Please ensure it is defined in ~/.zshrc." >&2
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

echo "Formatting ${#files[@]} file(s) under $server_dir using cf..."
for file in "${files[@]}"; do
  echo "Formatting $file"
  cf "$file"
done
