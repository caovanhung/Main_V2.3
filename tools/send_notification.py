# Send to single device.
from pyfcm import FCMNotification

# push_service = FCMNotification(api_key="AAAA8ZtqW1w:APA91bHkw59NZevimYv6KrNC5NP-ESf73h4O7TkmedYKWDtplkF7m5AntwmeHnbaYYUlRy4J3HcZNSIGHHnc0Ut2LqxS1c3ntSo69nVvp3fTOrttZ_FHztCV_ouUp2f_rc2ZXoByhtEJ")
push_service = FCMNotification(api_key="AAAA1sIBLfA:APA91bGFIczmjhG9GErtdDOCnPop9DF9t1TqQLLu92TjxkW7PluJyb_zybEYVBshrncyhqmy8gnHwnEJo7b1gUH2OCT4YxWHRU8CQvl_OjrKf1-INPu2dXHszX1GfqggP0OTTQGwh7mH")

registration_id = "HOME"
message_title = "Test"
message_body = "Phong Test"
result = push_service.notify_topic_subscribers(topic_name="HOMEGY_BLE_195256612", message_title=message_title, message_body=message_body)
print (result)