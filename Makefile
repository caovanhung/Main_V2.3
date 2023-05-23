CC=aarch64-linux-gnu-gcc
CFLAGS= -g -O0 -funwind-tables -rdynamic -Wall -Wextra -Wconversion -Werror=int-conversion -Werror-implicit-function-declaration -Wno-discarded-qualifiers -lcrypto -lssl -ldl -lpthread -pthread -lmosquitto  -I/opt/common/ssl/include -I/opt/common/mqtt/usr/local/include

all : HG_APPLICATION_SERVICES_AWS HG_CORE_SERVICES_COREDATA HG_DEVICE_SERVICES_BLE HG_DEVICE_SERVICES_WIFI HG_BOARD_CONFIG

HG_APPLICATION_SERVICES_AWS:    main_AppService_AWS.o core_mqtt_state.o openssl_posix.o sockets_posix.o backoff_algorithm.o clock_posix.o core_mqtt.o core_mqtt_serializer.o parson.o queue.o aws_process.o core_process_t.o mosquitto.o time_t.o helper.o messages.o cJSON.o
	$(CC) main_AppService_AWS.o core_mqtt_state.o openssl_posix.o sockets_posix.o backoff_algorithm.o clock_posix.o core_mqtt.o core_mqtt_serializer.o parson.o queue.o aws_process.o core_process_t.o mosquitto.o time_t.o helper.o messages.o cJSON.o -lm -o HG_APPLICATION_SERVICES_AWS $(CFLAGS)

HG_CORE_SERVICES_COREDATA: main_CoreService_Coredata.o ble_process.o uart.o database.o sqlite3.o time_t.o queue.o parson.o core_process_t.o mosquitto.o core_data_process.o helper.o messages.o cJSON.o core_api.o
	$(CC)   main_CoreService_Coredata.o ble_process.o uart.o database.o sqlite3.o time_t.o queue.o parson.o  core_process_t.o mosquitto.o core_data_process.o helper.o messages.o cJSON.o core_api.o -lm -o HG_CORE_SERVICES_COREDATA $(CFLAGS)

HG_DEVICE_SERVICES_BLE: main_DeviceService_BLE.o ble_process.o ble_security.o queue.o mosquitto.o aes.o time_t.o uart.o sqlite3.o parson.o core_process_t.o helper.o messages.o cJSON.o
	$(CC) main_DeviceService_BLE.o ble_process.o ble_security.o queue.o mosquitto.o aes.o time_t.o  uart.o sqlite3.o parson.o core_process_t.o helper.o messages.o cJSON.o -lm -o HG_DEVICE_SERVICES_BLE $(CFLAGS)

HG_DEVICE_SERVICES_WIFI: main_DeviceService_WIFI.o time_t.o queue.o parson.o core_process_t.o mosquitto.o wifi_process.o helper.o messages.o cJSON.o
	$(CC)   main_DeviceService_WIFI.o time_t.o queue.o parson.o  core_process_t.o mosquitto.o wifi_process.o helper.o messages.o cJSON.o -lm -o HG_DEVICE_SERVICES_WIFI $(CFLAGS)

HG_BOARD_CONFIG: main_BoardConfig.o time_t.o parson.o helper.o cJSON.o gpio.o
	$(CC)   main_BoardConfig.o time_t.o parson.o helper.o cJSON.o gpio.o -lm -o HG_BOARD_CONFIG $(CFLAGS)
