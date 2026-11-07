#include "log.h"
#include "config.h"

LogTee Log;

static char lines[LOG_QUEUE_LINES][LOG_LINE_MAX];
static uint8_t head = 0, count = 0;  // head = oldest queued line
static char cur[LOG_LINE_MAX];
static size_t len = 0;

size_t LogTee::write(uint8_t c) {
  Serial.write(c);
  if (c == '\r') return 1;
  if (c != '\n') {
    if (len < LOG_LINE_MAX - 1) cur[len++] = c;  // longer lines are cut
    return 1;
  }
  if (len == 0) return 1;
  cur[len] = '\0';
  len = 0;
  uint8_t slot = (head + count) % LOG_QUEUE_LINES;
  if (count == LOG_QUEUE_LINES) {
    head = (head + 1) % LOG_QUEUE_LINES;  // full: overwrite the oldest
  } else {
    count++;
  }
  memcpy(lines[slot], cur, LOG_LINE_MAX);
  return 1;
}

bool LogTee::pop(char *out, size_t size) {
  if (!count) return false;
  strlcpy(out, lines[head], size);
  head = (head + 1) % LOG_QUEUE_LINES;
  count--;
  return true;
}
