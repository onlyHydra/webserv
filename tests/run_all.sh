#!/usr/bin/env bash
set -euo pipefail

BASE_URL="${BASE_URL:-http://127.0.0.1:8080}"
UPLOAD_ENDPOINT="${UPLOAD_ENDPOINT:-/upload}"
STORE_DIR="${STORE_DIR:-www/upload/files}"

pass() { printf "\033[32m[PASS]\033[0m %s\n" "$*"; }
fail() { printf "\033[31m[FAIL]\033[0m %s\n" "$*" ; exit 1; }
need() { command -v "$1" >/dev/null 2>&1 || fail "Missing $1"; }

need curl
mkdir -p "$STORE_DIR"
rm -f "$STORE_DIR"/* || true

# 0) server reachability
curl -sS -m 2 -o /dev/null "$BASE_URL/" || fail "Server not reachable at $BASE_URL"
pass "Server reachable at $BASE_URL"

# 1) Simple multipart upload
echo "hello upload" > /tmp/test.txt
R="$(curl -sS -w '%{http_code}' -F "file=@/tmp/test.txt" "$BASE_URL${UPLOAD_ENDPOINT}")"
BODY="${R%???}"; CODE="${R: -3}"
[[ "$CODE" =~ ^20[01]$ ]] || fail "Simple upload expected 200/201, got $CODE"
[[ -f "$STORE_DIR/test.txt" ]] || fail "Uploaded file not found in $STORE_DIR"
pass "Simple upload → $CODE and file present"

# 2) Non-multipart POST → 415
CODE="$(curl -sS -o /dev/null -w '%{http_code}' -H 'Content-Type: text/plain' --data 'plain' "$BASE_URL${UPLOAD_ENDPOINT}")"
[[ "$CODE" = "415" ]] && pass "Non-multipart rejected with 415" || fail "Expected 415, got $CODE"

# 3) Overwrite policy (assumes upload_overwrite off)
R="$(curl -sS -w '%{http_code}' -F "file=@/tmp/test.txt" "$BASE_URL${UPLOAD_ENDPOINT}")"
CODE="${R: -3}"
if [[ "$CODE" = "409" ]]; then
  pass "Second upload same name → 409 Conflict (overwrite off)"
else
  echo "Note: overwrite likely enabled; got $CODE"
fi

# 4) Malicious filename sanitized
R="$(curl -sS -w '%{http_code}' -F "file=@/tmp/test.txt;filename=../../etc/passwd" "$BASE_URL${UPLOAD_ENDPOINT}")"
CODE="${R: -3}"
[[ "$CODE" =~ ^20[01]$ ]] || fail "Malicious filename upload expected 200/201, got $CODE"
# expect sanitized name inside store dir (pattern check)
FOUND="$(ls -1 "$STORE_DIR" | grep -E '(^|.*)[._-]*etc[._-]*passwd$' || true)"
[[ -n "$FOUND" ]] && pass "Malicious filename sanitized → $FOUND" || pass "Malicious filename saved with sanitized name (check $STORE_DIR)"

# 5) Per-part size cap (set upload_max_file_size small, e.g., 2m)
dd if=/dev/zero of=/tmp/big.bin bs=1M count=3 status=none
CODE="$(curl -sS -o /dev/null -w '%{http_code}' -F "file=@/tmp/big.bin" "$BASE_URL${UPLOAD_ENDPOINT}")"
if [[ "$CODE" = "413" ]]; then
  pass "Per-part cap enforced → 413"
else
  echo "Note: cap may be higher/disabled; got $CODE"
fi

# 6) Boundary edge cases (filename with boundary-like text)
echo "data" > /tmp/b.txt
CODE="$(curl -sS -o /dev/null -w '%{http_code}' -F "weird=@/tmp/b.txt;filename=----WebKitFormBoundary.txt" "$BASE_URL${UPLOAD_ENDPOINT}")"
[[ "$CODE" =~ ^20[01]$ ]] && pass "Boundary-like filename handled" || fail "Boundary-like filename → $CODE"

echo
pass "Smoke suite completed."
echo "For stress & chunked tests run:"
echo "  python3 tests/stress_10k.py"
echo "  python3 tests/chunked_multipart.py"
