#!/bin/bash


# Compiles and runs the program with a custom config and free port
# This way it can run regardless of the server being up or not, as it makes its own


# Make custom config script
program_output_file="test_out_deleteMe.txt"
test_config_file="test_config_deleteMe.txt"
port=$(python3 -c 'import socket; s=socket.socket(); s.bind(("",0)); print(s.getsockname()[1]); s.close()')
echo "Testing on port: $port"
echo "server {
  listen 127.0.0.1:$port;
  server_name localhost;

  root  ./www;
  index index.html index.htm;

  location /200-1 { return 200 ; }
  location /200-2 { return 200 Testing_success ; }
  location /200-3 { return 200 Testing works even like this ; }
  location /301 { return 301 / ; }
  location /302 { return 302 / ; }
  location /307 { return 307 / ; }
  location /308 { return 308 / ; }
  location /204 { return 204 ; }
}"  > $test_config_file


# Launch webserv as a child process
make
./webserv $test_config_file 2>$program_output_file &
PID=$!

pass=0
fail=0

expected1="200 OK"
expected2=""
response=$(curl -s -i -X GET http://localhost:$port/200-1)
if echo "$response" | grep -q "$expected1" && echo "$response" | grep -q "$expected2"; then
	echo "✅ PASS — found '$expected1' and '$expected2'"
	((pass++))
else
	echo "❌ FAIL — expected '$expected1' and '$expected2'"
	echo "Got '$response'"
	((fail++))
fi

expected1="200 OK"
expected2="Testing_success"
response=$(curl -s -i -X GET http://localhost:$port/200-2)
if echo "$response" | grep -q "$expected1" && echo "$response" | grep -q "$expected2"; then
	echo "✅ PASS — found '$expected1' and '$expected2'"
	((pass++))
else
	echo "❌ FAIL — expected '$expected1' and '$expected2'"
	echo "Got '$response'"
	((fail++))
fi

expected1="200 OK"
expected2="Testing works even like this"
response=$(curl -s -i -X GET http://localhost:$port/200-3)
if echo "$response" | grep -q "$expected1" && echo "$response" | grep -q "$expected2"; then
	echo "✅ PASS — found '$expected1' and '$expected2'"
	((pass++))
else
	echo "❌ FAIL — expected '$expected1' and '$expected2'"
	echo "Got '$response'"
	((fail++))
fi

expected1="301 Moved Permanently"
expected2="Location: /"
response=$(curl -s -i -X GET http://localhost:$port/301)
if echo "$response" | grep -q "$expected1" && echo "$response" | grep -q "$expected2"; then
	echo "✅ PASS — found '$expected1' and '$expected2'"
	((pass++))
else
	echo "❌ FAIL — expected '$expected1' and '$expected2'"
	echo "Got '$response'"
	((fail++))
fi

expected1="302 Found"
expected2="Location: /"
response=$(curl -s -i -X GET http://localhost:$port/302)
if echo "$response" | grep -q "$expected1" && echo "$response" | grep -q "$expected2"; then
	echo "✅ PASS — found '$expected1' and '$expected2'"
	((pass++))
else
	echo "❌ FAIL — expected '$expected1' and '$expected2'"
	echo "Got '$response'"
	((fail++))
fi

expected1="307 Temporary Redirect"
expected2="Location: /"
response=$(curl -s -i -X GET http://localhost:$port/307)
if echo "$response" | grep -q "$expected1" && echo "$response" | grep -q "$expected2"; then
	echo "✅ PASS — found '$expected1' and '$expected2'"
	((pass++))
else
	echo "❌ FAIL — expected '$expected1' and '$expected2'"
	echo "Got '$response'"
	((fail++))
fi

expected1="308 Permanent Redirect"
expected2="Location: /"
response=$(curl -s -i -X GET http://localhost:$port/308)
if echo "$response" | grep -q "$expected1" && echo "$response" | grep -q "$expected2"; then
	echo "✅ PASS — found '$expected1' and '$expected2'"
	((pass++))
else
	echo "❌ FAIL — expected '$expected1' and '$expected2'"
	echo "Got '$response'"
	((fail++))
fi

expected1="HTTP/1.1 204 No Content
Connection: close
Content-Length: 0
Date: Wed, 24 Sep 2025 14:51:46 GMT
Server: webserv"
response=$(curl -s -i -X GET http://localhost:$port/204)
if echo "$response" | grep -q "$expected1" ; then
	echo "✅ PASS — found '204 No Content'"
	((pass++))
else
	echo "❌ FAIL — expected '$expected1'"
	echo "Got '$response'"
	((fail++))
fi

kill "$PID" 2>/dev/null || true
wait "$PID" 2>/dev/null || true
rm $test_config_file

echo "================================"
if [ "$fail" -eq "0" ]; then
	printf "\033[32m[Summary: $pass passed, $fail failed]\033[0m\n"
	rm $program_output_file
	exit 0
else
	printf "\033[31m[Summary: $pass passed, $fail failed]\033[0m\n"
	exit 1
fi
