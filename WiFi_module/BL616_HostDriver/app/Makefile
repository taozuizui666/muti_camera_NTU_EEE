
CC=cc -mfloat-abi=hard

CFLAGS := -DBUILD_USERSPACE -g

.PHONY: all clean
all: blutl bl_hci
clean:
	rm -rf blutl bl_hci

blutl:
	$(CC) -o $@ $(CFLAGS)  bl_util.c

bl_hci:
	$(CC) -o $@ $(CFLAGS) -lpthread bl_hci.c