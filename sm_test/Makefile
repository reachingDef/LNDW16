CC=clang
CFLAGS=-g -Wall
all: simple esc_in_payload split_buffers twice

simple: simple.o uart_codec.o
	$(CC)  $(CFLAGS) simple.o uart_codec.o -o $@

esc_in_payload: esc_in_payload.o uart_codec.o
	$(CC) $(CFLAGS)  esc_in_payload.o uart_codec.o -o $@

split_buffers: split_buffers.o uart_codec.o
	$(CC) $(CFLAGS) split_buffers.o uart_codec.o -o $@
twice: twice.o uart_codec.o
	$(CC) $(CFLAGS) twice.o uart_codec.o -o $@

clean:
	rm -f *.o main simple esc_in_payload split_buffers twice
