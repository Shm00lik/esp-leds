#pragma once

#include <Arduino.h>

// Log output: everything printed goes to Serial, and each complete line is also queued
// (the last LOG_QUEUE_LINES, oldest dropped first) for the main loop to publish to leds/log.
class LogTee : public Print {
 public:
  size_t write(uint8_t c) override;
  using Print::write;
  // Copies the oldest queued line into `out`. Returns false if the queue is empty.
  bool pop(char *out, size_t size);
};

extern LogTee Log;
