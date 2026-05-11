
CC = arm-none-eabi-gcc
OBJCOPY = arm-none-eabi-objcopy

CFLAGS = -mcpu=cortex-m4 -mthumb -O2 -Wall -g -Iinc -Ilib/kuznyechik -Ilib/crypto
LDFLAGS = -specs=nosys.specs -T stm32f4.ld

SRC = src/mesh.c lib/kuznyechik/kuznyechik.c lib/crypto/mesh_crypto.c \
      lib/crypto/f25519.c lib/crypto/fprime.c lib/crypto/sha512.c \
      lib/crypto/c25519.c lib/crypto/ed25519.c lib/crypto/edsign.c \
      lib/crypto/x25519_wrapper.c src/main.c src/debug.c src/lora.c
OBJ = $(SRC:.c=.o)

all: mesh_firmware.elf mesh_firmware.bin

mesh_firmware.elf: $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) -o $@ $(LDFLAGS)

%.bin: %.elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -f $(OBJ) *.elf *.bin
