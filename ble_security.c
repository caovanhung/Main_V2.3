#include "ble_security.h"

bool AES_get_code_encrypt(int fd,char *MAC_t, char *address_t)
{
	if(MAC_t == NULL || address_t == NULL)
	{
		return false;
	}

	int i = 0,check = 0;
	uint8_t  SET_CODE[]   = {0xe8,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xe0,0x11,0x02,0x00,0x00,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
	unsigned char key[]   = {0x48,0x47,0x59,0x62,0x6c,0x75,0x65,0x74,0x6f,0x6f,0x74,0x68,0x76,0x65,0x72,0x32};
	unsigned char param[] = {0x68,0x67,0x79,0x73,0x6d,0x61,0x72,0x74,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
	struct AES_ctx ctx;
	AES_init_ctx(&ctx, key);

	unsigned char  *hex_address_t = malloc(5);
	unsigned char  *hex_MAC_t = malloc(13);

	String2HexArr((char*)MAC_t,hex_MAC_t);
	param[8]  = *(hex_MAC_t+5);
	param[9]  = *(hex_MAC_t+4);
	param[10] = *(hex_MAC_t+3);
	param[11] = *(hex_MAC_t+2);
	param[12] = *(hex_MAC_t+1);
	param[13] = *(hex_MAC_t+0);

	String2HexArr((char*)address_t,hex_address_t);
	SET_CODE[8] = *hex_address_t;
    SET_CODE[9] = *(hex_address_t+1);

	param[14] = *hex_address_t;
	param[15] = *(hex_address_t+1);

	AES_ECB_encrypt(&ctx, param);
    printf("AES_ECB_encrypt: ");
    for(i = 0;i<16;i++)
    {
    	printf("0x%02X ",param[i]);
    }
    printf("\n\n");

    SET_CODE[17] = param[10];
    SET_CODE[18] = param[11];
    SET_CODE[19] = param[12];
    SET_CODE[20] = param[13];
    SET_CODE[21] = param[14];
    SET_CODE[22] = param[15];

    printf("SET_CODE: ");
    for(i = 0;i<23;i++)
    {
    	printf("0x%02X ",SET_CODE[i]);
    }
    printf("\n\n");
    check = UART0_Send(fd,SET_CODE,23);
	if(check != 23)
	{
		free(hex_address_t);
		free(hex_MAC_t);
		return false;
	}
	free(hex_address_t);
	free(hex_MAC_t);
	return true;
}


bool AES_get_code_encrypt_sensor(int fd,char *MAC_t, char *address_t)
{
	if(MAC_t == NULL || address_t == NULL)
	{
		return false;
	}

	int i = 0,check = 0;
	uint8_t  SET_CODE[]   = {0xe8,0xff,0x00,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0xe0,0x11,0x02,0xe1,0x00,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
	unsigned char key[]   = {0x48,0x4f,0x4d,0x45,0x47,0x59,0x25,0x53,0x6d,0x61,0x72,0x74,0x68,0x6f,0x6d,0x65};
	unsigned char param[] = {0x24,0x02,0x28,0x04,0x28,0x11,0x20,0x20,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
	struct AES_ctx ctx;
	AES_init_ctx(&ctx, key);

	printf("MAC_t = %s\n",MAC_t );
	printf("address_t = %s\n",address_t );
	unsigned char  *hex_address_t = malloc(5);
	unsigned char  *hex_MAC_t = malloc(13);

	String2HexArr((char*)MAC_t,hex_MAC_t);
	param[8]  = *(hex_MAC_t+5);
	param[9]  = *(hex_MAC_t+4);
	param[10] = *(hex_MAC_t+3);
	param[11] = *(hex_MAC_t+2);
	param[12] = *(hex_MAC_t+1);
	param[13] = *(hex_MAC_t+0);

	String2HexArr((char*)address_t,hex_address_t);
	SET_CODE[8] = *hex_address_t;
    SET_CODE[9] = *(hex_address_t+1);

	param[14] = *hex_address_t;
	param[15] = *(hex_address_t+1);

    printf("param: ");
    for(i = 0;i<16;i++)
    {
    	printf("0x%02X ",param[i]);
    }
    printf("\n\n");

	AES_ECB_encrypt(&ctx, param);
    printf("AES_ECB_encrypt: ");
    for(i = 0;i<16;i++)
    {
    	printf("0x%02X ",param[i]);
    }
    printf("\n\n");

    SET_CODE[17] = param[10];
    SET_CODE[18] = param[11];
    SET_CODE[19] = param[12];
    SET_CODE[20] = param[13];
    SET_CODE[21] = param[14];
    SET_CODE[22] = param[15];

    printf("SET_CODE: ");
    for(i = 0;i<23;i++)
    {
    	printf("0x%02X ",SET_CODE[i]);
    }
    printf("\n\n");
    check = UART0_Send(fd,SET_CODE,23);
	if(check != 23)
	{
		free(hex_address_t);
		free(hex_MAC_t);
		return false;
	}
	free(hex_address_t);
	free(hex_MAC_t);
	return true;
}