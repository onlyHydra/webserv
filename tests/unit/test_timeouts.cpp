// tests/unit/test_timeouts_backpressure.cpp
#include <catch2/catch_all.hpp>

#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <cerrno>
#include <vector>
#include <string>

// Test trick to see internals (ONLY in tests)
#define private public
#include "ClientConnection.h"
#undef private

// Small helpers
static inline void set_nonblocking(int fd) {
    int fl = fcntl(fd, F_GETFL, 0);
    REQUIRE(fl != -1);
    REQUIRE(fcntl(fd, F_SETFL, fl | O_NONBLOCK) != -1);
}

static inline unsigned long long now_ms() {
    timeval tv;
    gettimeofday(&tv, 0);
    return (unsigned long long)tv.tv_sec * 1000ULL + (unsigned long long)(tv.tv_usec / 1000);
}

TEST_CASE("Header timeout: idle connection gets closed  = 1;     by onTick()", "[timeouts]") {
    int sv[2];
    REQUIRE(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    set_nonblocking(sv[0]);
    set_nonblocking(sv[1]);

    // Create connection on one end
    ClientConnection conn(sv[0]);
    // We own sv[1] as the peer
    REQUIRE(conn.getFD() == sv[0]);
    REQUIRE(conn.state == READ_HEADERS);
    REQUIRE(!conn.isClosed());

    // Force the deadline into the past → onTick() must close
    conn.deadline_ms = 1;           // ms since epoch long ago
    conn.onTick();
    REQUIRE(conn.isClosed());

    // Clean up the peer
    close(sv[1]);
}

TEST_CASE("Header deadline extends after readable progress", "[timeouts]") {
    int sv[2];
    REQUIRE(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    set_nonblocking(sv[0]);
    set_nonblocking(sv[1]);

    ClientConnection conn(sv[0]);
    REQUIRE(conn.state == READ_HEADERS);
    REQUIRE(!conn.isClosed());

    // Make deadline "expired"
    conn.deadline_ms = now_ms() - 1;
    const unsigned long long old_deadline = conn.deadline_ms;

    // Write a single byte from the peer so onReadable() makes progress
    const char one = 'X';
    REQUIRE(write(sv[1], &one, 1) == 1);

    // Drive read path (this calls readFromSocket() internally)
    conn.onReadable();
    REQUIRE(!conn.isClosed());
    // Progress should extend header deadline
    REQUIRE(conn.deadline_ms > old_deadline);

    // Cleanup
    close(sv[1]);
}

TEST_CASE("Write timeout: expired write-phase gets closed by onTick()", "[timeouts]") {
    int sv[2];
    REQUIRE(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    set_nonblocking(sv[0]);
    set_nonblocking(sv[1]);

    ClientConnection conn(sv[0]);
    REQUIRE(!conn.isClosed());

    // Prepare some outgoing data and switch to WRITE
    conn.outBuffer.assign(64 * 1024, 'A');
    conn.outOffset = 0;
    conn.changeState(WRITE);

    // Force deadline in the past
    conn.deadline_ms = 1;
    conn.onTick();
    REQUIRE(conn.isClosed());

    close(sv[1]);
}

TEST_CASE("Write deadline extends on send progress", "[timeouts]") {
    int sv[2];
    REQUIRE(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    set_nonblocking(sv[0]);
    set_nonblocking(sv[1]);

    ClientConnection conn(sv[0]);
    REQUIRE(!conn.isClosed());

    // Prepare moderate payload, enter WRITE
    conn.outBuffer.assign(128 * 1024, 'B');
    conn.outOffset = 0;
    conn.changeState(WRITE);

    // Set a very near deadline
    conn.deadline_ms = now_ms() + 1;
    const unsigned long long before = conn.deadline_ms;

    // Let onWritable() send something (peer has room initially)
    conn.onWritable();
    REQUIRE(!conn.isClosed());
    // Should have made progress (either fully sent or partially)
    REQUIRE(conn.outOffset > 0);
    // Progress should have bumped the write deadline
    REQUIRE(conn.deadline_ms >= before);

    close(sv[1]);
}

TEST_CASE("Backpressure: resume reads when write buffer drains below LOW_WATER", "[backpressure]") {
    int sv[2];
    REQUIRE(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    set_nonblocking(sv[0]);
    set_nonblocking(sv[1]);

    ClientConnection conn(sv[0]);

    // Simulate earlier "pause": big response already queued, reads paused
    const std::size_t big = ClientConnection::HIGH_WATER + 64 * 1024;
    conn.outBuffer.assign(big, 'C');
    conn.outOffset  = 0;
    conn.readPaused = true;
    conn.changeState(WRITE);

    // Drain from peer to allow kernel to accept our sends
    // Repeatedly call onWritable() + read from peer until remaining <= LOW_WATER
    while (!conn.isClosed() && (conn.outBuffer.size() - conn.outOffset) > ClientConnection::LOW_WATER) {
        conn.onWritable(); // push data to socket
        // read whatever arrived on peer to keep space
        char tmp[32768];
        ssize_t n = read(sv[1], tmp, sizeof(tmp));
        // It's nonblocking; EAGAIN is fine
        if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            FAIL("peer read error");
        }
        // If nothing to write just break
        if (conn.outOffset >= conn.outBuffer.size()) break;
    }

    REQUIRE(!conn.isClosed());
    const std::size_t remaining = conn.outBuffer.size() - conn.outOffset;
    REQUIRE(remaining <= ClientConnection::LOW_WATER);
    // onWritable() should have auto-resumed reads once drained enough
    REQUIRE(conn.readPaused == false);

    // Finish sending to avoid leaks
    while (!conn.isClosed() && conn.outOffset < conn.outBuffer.size()) {
        conn.onWritable();
        char tmp[32768]; (void)read(sv[1], tmp, sizeof(tmp));
    }

    close(sv[1]);
}

TEST_CASE("Backpressure: wantsRead() is false while paused", "[backpressure]") {
    int sv[2];
    REQUIRE(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    set_nonblocking(sv[0]);
    set_nonblocking(sv[1]);

    ClientConnection conn(sv[0]);
    REQUIRE(conn.state == READ_HEADERS);
    REQUIRE(conn.wantsRead()); // initially true

    conn.readPaused = true;
    REQUIRE_FALSE(conn.wantsRead()); // paused ⇒ handler must drop POLLIN

    // unpause
    conn.readPaused = false;
    REQUIRE(conn.wantsRead());

    close(sv[1]);
}
