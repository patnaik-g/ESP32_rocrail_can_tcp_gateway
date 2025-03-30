The code manages communication between Rocrail (a model railroad control software) and the Märklin CAN bus
via a two-core ESP32, providing TCP and CAN translation. It consists of 2 main tasks and 1 optional debug task.

This code has been forked from Christophe Bobille’s ESP32_rocrail_can_tcp_gateway. Thanks, Christofe!
Please see the full presentation of the project at https://www.locoduino.org/spip.php?article361   

Changes/improvements include a WiFi manager with OTA capability and debugging via telnet.
The manager will set up an access point to input WiFi credentials on initial setup.
Performance improvements come from streamlining the code and reducing the number to tasks from 4 to 2.
(CAN and WiFi are both half duplex, so separate send/receive tasks are unnecessary).

ChatGPT was used extensively.

Please see Overview.docx for more details.
