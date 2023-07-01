#ifndef APP_CONFIG
#define APP_CONFIG

/*
 * Thời gian timeout tối đa khi gửi các bản tin cấu hình (tạo nhóm, scene). Nếu 1 thiết bị
 * không phản hồi về thì sẽ đợi hết thời gian này mới gửi bản tin tiếp theo
 */
#define GW_SENDING_TIMEOUT_MAX         5000

/*
 * Thời gian tối thiểu khi gửi các bản tin cấu hình (tạo nhóm, scene). Nếu 1 thiết bị
 * đã phản hồi về thì sẽ đợi đến hết thời gian này mới gửi bản tin tiếp theo
 */
#define GW_SENDING_TIMEOUT_MIN         1000

#endif  // APP_CONFIG