#!/bin/bash


#For adding a new test case:
#Add the name at the back of CASES
#Then find some string, that your test case only generates, if everything worked
#Add this string after a new line into EXPECTED



# Get absolute path of this test runner's folder
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# List of test scripts
CASES="cgi_env_cookies.sh
run_all.sh
stress_10k.py
chunked_multipart.py
chunked_debug_verbose.py
chunked_upload_test.py
probe_connect.py
return_directive.sh
cgi_rate_limits.py
proxy_e2e.py"

# Matching expected outputs (separate multiple expected strings with | )
EXPECTED="Summary: 3 passed, 0 failed
Smoke suite completed.
413
Status: 201
HTTP/1.1 201 Created
Status: 409
HTTP/1.1 200 OK
[Summary: 8 passed, 0 failed]
limiter enforced on CGI
All proxy tests passed"

SUCCESS=0
FAIL=0

#Stolen from run-all.sh >:3
pass() { printf "\033[32m[PASS]\033[0m %s\n" "$*"; }
fail() { printf "\033[31m[FAIL]\033[0m %s\n" "$*"; }





# Check if Server is running
if ! curl --silent --fail http://localhost:8080 >/dev/null 2>&1; then
    printf "\033[31mServer isnt running in background!\nPlease run the server in a different terminal\n(./webserv)\033[0m %s\n"
	exit 1
fi

idx=1
for SCRIPT in $CASES; do
	EXPECTED_LINE=$(printf "%s\n" "$EXPECTED" | sed -n "${idx}p")

	if [ ! -f "$SCRIPT_DIR/$SCRIPT" ]; then
		echo "MISSING: $SCRIPT"
		FAIL=$((FAIL + 1))
		idx=$((idx + 1))
		continue
	fi

	EXT="${SCRIPT##*.}"   # gets substring after last dot
	case "$EXT" in
		sh)
			CMD="bash \"$SCRIPT_DIR/$SCRIPT\""
			;;
		py)
			CMD="python3 \"$SCRIPT_DIR/$SCRIPT\""
			;;
		*)
			echo "Unknown script type: $SCRIPT"
			FAIL=$((FAIL + 1))
			idx=$((idx + 1))
			continue
			;;
	esac

# For Output on Terminal, remove the "2>&1"

	OUTPUT=$(eval $CMD 2>&1)

	ALL_FOUND=1
	OLDIFS="$IFS"
	IFS="|"
	for LINE in $EXPECTED_LINE; do
		echo "$OUTPUT" | grep -F -q "$LINE"
		if [ $? -ne 0 ]; then
			ALL_FOUND=0
			break
		fi
	done
	IFS="$OLDIFS"

	if [ $ALL_FOUND -eq 1 ]; then
		pass "PASS: $SCRIPT"
		SUCCESS=$((SUCCESS + 1))
	else
		fail "FAIL: $SCRIPT"
		FAIL=$((FAIL + 1))
		echo "Failure [$OUTPUT]"
	fi

	idx=$((idx + 1))
done



echo "================================"
if [ "$FAIL" -eq "0" ]; then
	printf "\033[32m[Summary: $SUCCESS passed, $FAIL failed]\033[0m\n"
else
	printf "\033[31m[Summary: $SUCCESS passed, $FAIL failed]\033[0m\n"
fi