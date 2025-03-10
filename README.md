The code manages communication between Rocrail (a model railroad control software) and the Märklin CAN bus
via a two-core ESP32, providing TCP and CAN translation. It consists of 4 main tasks and 1 optional debug task.

This code has been forked from Christophe Bobille’s ESP32_rocrail_can_tcp_gateway. Thanks, Christofe!
Please see the full presentation of the project at https://www.locoduino.org/spip.php?article361   

Changes/improvements include the WiFi manager with OTA capability and some performance improvements. 
ChatGPT was used extensively for the WiFi manager.

Please see Overview.docx for more details.
