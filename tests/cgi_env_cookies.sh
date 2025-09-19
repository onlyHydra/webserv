#!/bin/bash

URL="http://localhost:8080/cgi/enviroment.cgi"

# Array of test cases: "cookie_header expected_substring"
tests=(
  "session=abc123; theme=dark HTTP_COOKIE=session=abc123; theme=dark"
)

pass=0
fail=0

# Run POST with Cookies, find Cookies in enviroment
expected="HTTP_COOKIE=session=abc123; theme=dark"
response=$(curl -s -i -X POST http://localhost:8080/cgi/enviroment.cgi \
    -H "Cookie: session=abc123; theme=dark" \
    -d "foo=bar")

if echo "$response" | grep -q "$expected"; then
	echo "✅ PASS — found '$expected'"
	((pass++))
else
	echo "❌ FAIL — expected '$expected'"
	echo "Got '$response'"
	((fail++))
fi

# Run POST with a bunch of Cookies, find Cookies in enviroment
expected="HTTP_COOKIE=age=34; lang=en-Us; location=canada; preference=private; session=abc123; theme=dark"
response=$(curl -s -i -X POST http://localhost:8080/cgi/enviroment.cgi \
    -H "Cookie: age=34; lang=en-Us; location=canada; preference=private; session=abc123; theme=dark" \
    -d "foo=bar")

if echo "$response" | grep -q "$expected"; then
	echo "✅ PASS — found '$expected'"
	((pass++))
else
	echo "❌ FAIL — expected '$expected'"
	echo "Got '$response'"
	((fail++))
fi

# Run POST without Cookies, no HTTP_COOKIE enviroment variable should exist
expected="HTTP_COOKIE"
response=$(curl -s -i -X POST http://localhost:8080/cgi/enviroment.cgi \
  -d "foo=bar&baz=qux")


if echo "$response" | grep -q "$expected"; then
	echo "❌ FAIL — found '$expected'"
	echo "Got '$response'"
	((fail++))
else
	echo "✅ PASS — Didnt find '$expected'"
	((pass++))
fi


echo "Summary: $pass passed, $fail failed"
exit $fail
