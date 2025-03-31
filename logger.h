// MultiStream.h
#ifndef MULTI_STREAM_H
#define MULTI_STREAM_H

#ifndef debug
#define debug Serial
#endif

#include <Print.h>

class MultiStream : public Print {
private:
  Print& debug;
  Print& telnet;

public:
  MultiStream(Print& debugStream, Print& telnetStream)
    : debug(debugStream), telnet(telnetStream) {}

  virtual size_t write(uint8_t c) override {
    size_t result1 = debug.write(c);
    size_t result2 = telnet.write(c);
    return result1;
  }

  virtual size_t write(const uint8_t* buffer, size_t size) override {
    size_t result1 = debug.write(buffer, size);
    size_t result2 = telnet.write(buffer, size);
    return result1;
  }
};

// Now create the logger
MultiStream logger(debug, TelnetStream);
#endif
