#!/usr/bin/env bash
set -euo pipefail

# Defaults
ENDPOINT="http://localhost:8080/files"
N=20              # number of uploads
P=10              # parallelism
NOTE="hi"
SIZE_BYTES=0      # 0 = tiny text file; >0 = generate ~SIZE_BYTES text payloads
KEEP=0            # 1 = keep working dir; 0 = delete
BOUNDARY="------------------------$(date +%s%N)"
CURL_EXPECT_OPT='-H Expect:'   # disables "Expect: 100-continue"
CURL_EXTRA_OPTS='--http1.1 --expect100-timeout 0'

usage() {
  cat <<EOF
Usage: $0 [options]

Options:
  --endpoint URL         Upload URL (default: $ENDPOINT)
  --n N                  Number of uploads (default: $N)
  --p P                  Parallel uploads (default: $P)
  --note TEXT            Form field "note" value (default: "$NOTE")
  --bytes N              Approx. bytes per file (default: $SIZE_BYTES; 0 = small)
  --boundary STR         Use a specific boundary (default: auto)
  --keep                 Keep temp payloads directory after run
  -h, --help             Show this help

Examples:
  $0 --endpoint http://localhost:8080/files --n 50 --p 20
  $0 --bytes 1000000     # ~1MB per file
EOF
}

# Parse args (fixed)
while [[ $# -gt 0 ]]; do
  case "$1" in
    --endpoint)   ENDPOINT="$2"; shift 2;;
    --n)          N="$2"; shift 2;;
    --p)          P="$2"; shift 2;;
    --note)       NOTE="$2"; shift 2;;
    --bytes)      SIZE_BYTES="$2"; shift 2;;
    --boundary)   BOUNDARY="$2"; shift 2;;
    --keep)       KEEP=1; shift;;
    -h|--help)    usage; exit 0;;
    *) echo "Unknown arg: $1"; usage; exit 1;;
  esac
done

# Working directory for payloads
WORKDIR="$(mktemp -d -t upload_bench_XXXXXX)"
trap '[[ "$KEEP" -eq 1 ]] || rm -rf "$WORKDIR"' EXIT

echo "[i] Workdir: $WORKDIR"
echo "[i] Endpoint: $ENDPOINT"
echo "[i] N=$N, P=$P, SIZE_BYTES=$SIZE_BYTES, boundary=$BOUNDARY"

# Generate one file of ~SIZE_BYTES (text)
gen_file() {
  local out="$1" bytes="$2"
  if [[ "$bytes" -le 0 ]]; then
    echo "Hello from upload bench at $(date -u +"%Y-%m-%dT%H:%M:%S.%NZ")" > "$out"
  else
    dd if=/dev/zero bs=1 count="$bytes" 2>/dev/null | tr '\0' 'A' > "$out"
  fi
}

# Build multipart body with CRLF and proper closing boundary
build_payload() {
  local payload="$1" upload_name="$2" src_file="$3"
  {
    printf -- "--%s\r\n" "$BOUNDARY"
    printf "Content-Disposition: form-data; name=\"note\"\r\n\r\n"
    printf "%s\r\n" "$NOTE"
    printf -- "--%s\r\n" "$BOUNDARY"
    printf "Content-Disposition: form-data; name=\"file\"; filename=\"%s\"\r\n" "$upload_name"
    printf "Content-Type: text/plain\r\n\r\n"
    cat "$src_file"
    printf "\r\n--%s--\r\n" "$BOUNDARY"
  } > "$payload"
}

echo "[i] Generating $N payload(s)..."
for (( i=1; i<=N; i++ )); do
  fname="file_${i}_$(date +%s%N)_$RANDOM.txt"
  src="$WORKDIR/$fname"
  gen_file "$src" "$SIZE_BYTES"
  payload="$WORKDIR/payload_$i.multipart"
  build_payload "$payload" "$fname" "$src"
done

echo "[i] Uploading with curl in parallel (P=$P)..."
start_ts=$(date +%s.%N)

# Run uploads in parallel and collect HTTP status codes
# Run uploads in parallel and collect HTTP status codes
ls "$WORKDIR"/payload_*.multipart | xargs -I{} -P"$P" sh -c '
  curl -s -o /dev/null -w "%{http_code}\n" \
    -H "Content-Type: multipart/form-data; boundary='"$BOUNDARY"'" \
    '"$CURL_EXPECT_OPT"' '"$CURL_EXTRA_OPTS"' \
    --data-binary @"{}" \
    '"$ENDPOINT"'
' > "$WORKDIR"/results.txt


end_ts=$(date +%s.%N)

echo
echo "=== Results ==="
echo -n "Elapsed: "
python3 - <<PY 2>/dev/null || awk 'BEGIN{print '"$end_ts"'-'"$start_ts"'}'
print(round( float("$end_ts") - float("$start_ts"), 4))
PY
echo "Status counts:"
sort "$WORKDIR"/results.txt | uniq -c

echo
if [[ "$KEEP" -eq 1 ]]; then
  echo "[i] Kept workdir: $WORKDIR"
else
  echo "[i] Cleaning temp files."
fi
