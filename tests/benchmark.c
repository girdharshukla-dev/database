#define _POSIX_C_SOURCE 199309

#include "db.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#define INITIAL_KEYS 50000
#define TOTAL_OPS 200000

#define READ_PCT 70

#define KEY_SIZE 32

#define SMALL_VAL 128
#define MEDIUM_VAL 512
#define LARGE_VAL 4096

#define VERIFY_INTERVAL 200

struct expected_entry {
  uint8_t *data;
  uint32_t length;
  int exists;
};

struct expected_entry expected[INITIAL_KEYS];
double elapsed_ms(struct timespec s, struct timespec e) {
  return (e.tv_sec - s.tv_sec) * 1000.0 + (e.tv_nsec - s.tv_nsec) / 1000000.0;
}

uint64_t now_ns(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (uint64_t)t.tv_sec * 1000000000ULL + t.tv_nsec;
}

void random_string(char *buffer, size_t length) {
  static const char charset[] =
      "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  for (size_t i = 0; i < length - 1; i++) {
    buffer[i] = charset[rand() % (sizeof(charset) - 1)];
  }

  buffer[length - 1] = '\0';
}

int choose_value_size(void) {
  int r = rand() % 100;
  if (r < 70)
    return SMALL_VAL;
  if (r < 95)
    return MEDIUM_VAL;
  return LARGE_VAL;
}

int cmp_64(const void *a, const void *b) {
  uint64_t x = *(uint64_t *)a;
  uint64_t y = *(uint64_t *)b;
  return (x > y) - (x < y);
}

void print_latency(const char *name, uint64_t *samples, int count) {
  qsort(samples, count, sizeof(uint64_t), cmp_64);

  double p50 = samples[(int)(count * 0.50)] / 1000.0;
  double p95 = samples[(int)(count * 0.95)] / 1000.0;
  double p99 = samples[(int)(count * 0.99)] / 1000.0;
  double max = samples[count - 1] / 1000.0;

  printf("%s latency (us): p50=%.2f p95=%.2f p99=%.2f max=%.2f\n", name, p50, p95,
         p99, max);
}

void expected_put(int id, const uint8_t *data, uint32_t length) {
  free(expected[id].data);

  expected[id].data = malloc(length);
  if (expected[id].data == NULL) {
    fprintf(stderr, "malloc failed in expected_put\n");
    exit(1);
  }

  memcpy(expected[id].data, data, length);

  expected[id].length = length;
  expected[id].exists = 1;
}

int verify_expected(int id, const uint8_t *data, uint32_t length) {
  if (!expected[id].exists) {
    return -1;
  }

  if (expected[id].length != length) {
    return -1;
  }

  if (memcmp(expected[id].data, data, length))
    return -1;

  return 0;
}

int main(void) {
  srand((unsigned)time(NULL));
  struct timespec total_start, total_end, t1, t2;
  clock_gettime(CLOCK_MONOTONIC, &total_start);

  clock_gettime(CLOCK_MONOTONIC, &t1);

  struct db_type *db = db_open("./benchmarkdb");
  if (db == NULL) {
    fprintf(stderr, "Error in db_open in main\n");
  }

  clock_gettime(CLOCK_MONOTONIC, &t2);
  printf("[TIMING] db_open: %.3f\n", elapsed_ms(t1, t2));

  char kbuf[KEY_SIZE];
  char *vbuf = malloc(LARGE_VAL + 1);
  char *outbuf = malloc(LARGE_VAL + 1);

  struct slice_type key;
  struct slice_type value;
  struct slice_type out;

  uint64_t *read_lat = malloc(sizeof(uint64_t) * TOTAL_OPS);
  uint64_t *write_lat = malloc(sizeof(uint64_t) * TOTAL_OPS);

  int read_count = 0;
  int write_count = 0;

  clock_gettime(CLOCK_MONOTONIC, &t1);
  for (int i = 0; i < INITIAL_KEYS; i++) {
    snprintf(kbuf, sizeof(kbuf), "user:%08d", i);
    int vsize = choose_value_size();
    random_string(vbuf, vsize);

    key.data = (uint8_t *)kbuf;
    key.length = strlen(kbuf);

    value.data = (uint8_t *)vbuf;
    value.length = vsize - 1;

    uint64_t start = now_ns();
    if (db_put(db, &key, &value) == -1) {
      fprintf(stderr, "Error in db_put in main\n");
      return -1;
    }

    uint64_t end = now_ns();

    write_lat[write_count++] = end - start;

    expected_put(i, (uint8_t *)vbuf, value.length);
  }
  clock_gettime(CLOCK_MONOTONIC, &t2);

  printf("[TIMING] Initial load (%d keys): %.3f ms\n", INITIAL_KEYS,
         elapsed_ms(t1, t2));

  clock_gettime(CLOCK_MONOTONIC, &t1);

  for (int op = 0; op < TOTAL_OPS; op++) {

    int r = rand() % 100;
    int hot = (rand() % 100 < 80) ? rand() % (INITIAL_KEYS / 5)
                                  : rand() % (INITIAL_KEYS);

    snprintf(kbuf, sizeof(kbuf), "user:%08d", hot);
    key.data = (uint8_t *)kbuf;
    key.length = strlen(kbuf);

    if (r < READ_PCT) {
      out.data = (uint8_t *)outbuf;
      out.length = LARGE_VAL;

      uint64_t start = now_ns();
      if (db_get(db, &key, &out) == -1) {
        fprintf(stderr, "Error in db_get in main\n");
        return -1;
      }

      uint64_t end = now_ns();
      read_lat[read_count++] = end - start;

    } else {
      int vsize = choose_value_size();
      random_string(vbuf, vsize);

      value.data = (uint8_t *)vbuf;
      value.length = vsize - 1;

      uint64_t start = now_ns();

      if (db_put(db, &key, &value) == -1) {
        fprintf(stderr, "Error in db_put in main\n");
        return -1;
      }

      uint64_t end = now_ns();
      write_lat[write_count++] = end - start;
      expected_put(hot, (uint8_t *)vbuf, value.length);
    }

    // random correctness check
    if ((op % VERIFY_INTERVAL) == 0) {
      int verify_id = rand() % INITIAL_KEYS;
      snprintf(kbuf, sizeof(kbuf), "user:%08d", verify_id);

      key.data = (uint8_t *)kbuf;
      key.length = strlen(kbuf);

      out.data = (uint8_t *)outbuf;
      out.length = LARGE_VAL;

      if (db_get(db, &key, &out) == -1) {
        fprintf(stderr, "Error in db_get in main\n");
        return -1;
      }

      if (verify_expected(verify_id, out.data, out.length) == -1) {
        fprintf(stderr, "CORRUPTION DETECTED, key = %d\n", verify_id);
        return -1;
      }
    }
  }

  clock_gettime(CLOCK_MONOTONIC, &t2);
  printf("[TIMING] Mixed workload (%d ops): %.3f ms\n", TOTAL_OPS,
         elapsed_ms(t1, t2));

  clock_gettime(CLOCK_MONOTONIC, &t1);
  db_close(db);
  clock_gettime(CLOCK_MONOTONIC, &t2);
  printf("[TIMING] db_close: %.3f ms\n", elapsed_ms(t1, t2));

  clock_gettime(CLOCK_MONOTONIC, &t1);
  db = db_open("./benchmarkdb");
  if (db == NULL) {
    fprintf(stderr, "Error in db_open in recovery in main\n");
    return -1;
  }
  clock_gettime(CLOCK_MONOTONIC, &t2);
  printf("[TIMING] recovery open: %.3f ms\n", elapsed_ms(t1, t2));

  clock_gettime(CLOCK_MONOTONIC, &t1);
  for (int i = 0; i < INITIAL_KEYS; i++) {
    if (!expected[i].exists) {
      continue;
    }

    snprintf(kbuf, sizeof(kbuf), "user:%08d", i);
    key.data = (uint8_t *)kbuf;
    key.length = strlen(kbuf);

    out.data = (uint8_t *)outbuf;
    out.length = LARGE_VAL;

    if (db_get(db, &key, &out) == -1) {
      fprintf(stderr, "Error in db_get in recovery in main\n");
      return -1;
    }

    if (verify_expected(i, out.data, out.length) == -1) {
      fprintf(stderr, "CORRUPTION found during recovery, with key = %d\n", i);
      return -1;
    }
  }

  clock_gettime(CLOCK_MONOTONIC, &t2);

  printf("[TIMING] recovery verify: %.3f ms\n", elapsed_ms(t1, t2));

  db_close(db);

  print_latency("READ", read_lat, read_count);
  print_latency("WRITE", write_lat, write_count);

  clock_gettime(CLOCK_MONOTONIC, &total_end);

  printf("[TIMING] TOTAL runtime: %.3f ms\n",
         elapsed_ms(total_start, total_end));

  for (int i = 0; i < INITIAL_KEYS; i++) {
    free(expected[i].data);
  }

  free(vbuf);
  free(outbuf);
  free(read_lat);
  free(write_lat);

  printf("All tests passed\n");

  return 0;
}
