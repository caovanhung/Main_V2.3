#include <stdio.h>
#include <string.h>
#include <stdint.h>


#include "define.h"
#include "uart.h"
#include "core_process_t.h"
#include "aes.h"

#define CBC 1
#define CTR 1
#define ECB 1

bool AES_get_code_encrypt(int fd,char *MAC_t, char *address_t);
bool AES_get_code_encrypt_sensor(int fd,char *MAC_t, char *address_t);