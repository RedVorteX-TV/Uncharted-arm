/* Host test of bb_present_proto.h: a message and a descriptor through a SEQPACKET socketpair.
 * cc -std=c11 -Wall -Wextra -Werror test_proto.c -o /tmp/test_proto && /tmp/test_proto */
#include "bb_present_proto.h"
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>

int main(void) {
    int sv[2];
    assert(socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sv) == 0);
    int pipefd[2];
    assert(pipe(pipefd) == 0);
    bb_present_msg m = bb_present_make(BB_PRESENT_FRAME);
    m.generation = 7; m.index = 2; m.width = 1920; m.height = 1080; m.frame = 123456789012ull;
    assert(bb_present_send(sv[0], &m, pipefd[1]) == 0);
    bb_present_msg r; int fd = -1;
    assert(bb_present_recv(sv[1], &r, &fd, 0) == 0);
    assert(fd >= 0 && r.type == BB_PRESENT_FRAME && r.generation == 7 && r.frame == 123456789012ull);
    assert(write(fd, "x", 1) == 1);
    char c; assert(read(pipefd[0], &c, 1) == 1 && c == 'x');
    close(fd);
    /* No descriptor, empty socket, foreign message, closed peer. */
    assert(bb_present_send(sv[0], &m, -1) == 0);
    assert(bb_present_recv(sv[1], &r, &fd, 0) == 0 && fd == -1);
    assert(bb_present_recv(sv[1], &r, &fd, MSG_DONTWAIT) == -EAGAIN);
    assert(send(sv[0], "short", 5, 0) == 5);
    assert(bb_present_recv(sv[1], &r, &fd, 0) == -EPROTO);
    /* MODE (version 3) carries the flags in the field INDEX shares. */
    assert(BB_PRESENT_VERSION == 3u);
    bb_present_msg mode = bb_present_make(BB_PRESENT_MODE);
    mode.flags = BB_PRESENT_FLAG_FIFO;
    assert(bb_present_send(sv[0], &mode, -1) == 0);
    assert(bb_present_recv(sv[1], &r, &fd, 0) == 0 && fd == -1);
    assert(r.type == BB_PRESENT_MODE && r.version == 3 && (r.flags & BB_PRESENT_FLAGS_KNOWN) == BB_PRESENT_FLAG_FIFO);
    close(sv[0]);
    assert(bb_present_recv(sv[1], &r, &fd, 0) == -ECONNRESET);
    puts("bb_present_proto: ok");
    return 0;
}
