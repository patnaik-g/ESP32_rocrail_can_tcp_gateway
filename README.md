# ESP32 Rocrail CAN TCP Gateway

This project enables communication between **Rocrail**, a model railroad control software, and the **Märklin CAN bus** using a dual-core **ESP32**. It serves as a bridge, translating messages between **TCP and CAN**. The implementation consists of **two main tasks**, with an optional debug task.

## Project Origin
This code is a fork of **Christophe Bobille’s [ESP32_rocrail_can_tcp_gateway](https://www.locoduino.org/spip.php?article361)**. 

Special thanks to **Christophe Bobille** for the original implementation!

## Features & Improvements

- 🚀 **WiFi Manager with OTA Updates**  
  - Allows easy setup and over-the-air firmware updates.
  - Automatically sets up an **access point** for entering WiFi credentials on initial setup.

- 🔍 **Telnet Debugging**  
  - Enables remote debugging over the network.

- ⚡ **Performance Enhancements**  
  - Streamlined code to reduce the number of tasks from **4 to 2** for better efficiency.  
  - Since **CAN and WiFi are both half-duplex**, separate send/receive tasks are unnecessary.

## Development & Contributions
- AI tools, including **ChatGPT** and **Claude.ai**, were used extensively in development.
- Contributions and feedback are welcome! Feel free to submit a pull request or open an issue.

## Documentation
For more details, refer to **Overview.docx**.

## License
This project follows the same license as the original repository. Please check the original project for licensing terms.

---

🚂 **Happy Railroading!** 🚂
