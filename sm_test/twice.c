#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "uart_codec.h"

const byte test_buffer[] = {1, 2, 3, 1, 2, 3, 4, 5, 6, 7};


byte *test_target_buffer;
byte test_target_buffer_idx = 0;
byte read_buffer_idx = 0;
byte test_read(void) {
    printf("Reading: %x\n", test_target_buffer[read_buffer_idx]);
    return test_target_buffer[read_buffer_idx++];
}

byte test_flush(struct state *s) {
    printf("flush cb!\n");
    printf("Stop: I've read %d bytes of %d bytes.\n", s->already_read, s->next_packet_size);
    printf("Comparing src with dst: %d\n", memcmp(test_buffer, s->buffer, sizeof(test_buffer)));
    return s->already_read;
}

void test_send(byte buffer[], byte len) {
    int i=0;
    for (; i < len; i++) {
	test_target_buffer[i+test_target_buffer_idx] = buffer[i];
    }
    test_target_buffer_idx += len;
}

int main() {
    struct state *s = malloc(sizeof(struct state));
    init_state_machine(s);
    s->read_cb = test_read;
    s->flush_cb = test_flush;
    // sending test data
    printf("Simple test\n");
    test_target_buffer = malloc(2*sizeof(test_buffer));
    int sent = send_packet(test_buffer, sizeof(test_buffer), test_send);
    // reconstructing the data
    s->fifo_len = sent;
    s->read_cb = test_read;
    s->flush_cb = test_flush;
    while (s->fifo_len > 0) {
	s->next(s);
    }
 
    test_target_buffer_idx = 0;
    read_buffer_idx = 0;
    sent = send_packet(test_buffer, sizeof(test_buffer), test_send);
 
    s->fifo_len = sent;
    while (s->fifo_len > 0) {
	s->next(s);
    }  
    free(s);
    
}
