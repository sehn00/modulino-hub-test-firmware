# Modulino Mock Parts Module Firmware

This is an ESP32-C3 test-only UART handshake mock. It is not an implementation
of the Parts Module discovery, version, or production communication protocol.

Protocol UART configuration:

- UART1
- RX GPIO20
- TX GPIO21
- 115200 baud, 8 data bits, no parity, 1 stop bit, no flow control
- `MODULINO_PING` request returns `MODULINO_PONG`

Console logs use the ESP32-C3 USB Serial/JTAG console and are not written to the
GPIO20/GPIO21 protocol UART.
