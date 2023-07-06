# Send to single device.
from pyfcm import FCMNotification

push_service = FCMNotification(api_key="AAAA8ZtqW1w:APA91bHkw59NZevimYv6KrNC5NP-ESf73h4O7TkmedYKWDtplkF7m5AntwmeHnbaYYUlRy4J3HcZNSIGHHnc0Ut2LqxS1c3ntSo69nVvp3fTOrttZ_FHztCV_ouUp2f_rc2ZXoByhtEJ")

registration_id = "AppBLE"
message_title = "Cháy nhà rồi cả làng ơi"
message_body = "Dậy đi cháy nhà rồi"
result = push_service.notify_topic_subscribers(topic_name="AppBLE", message_title=message_title, message_body=message_body)
print (result)