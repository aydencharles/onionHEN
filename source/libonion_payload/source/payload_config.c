#include <onion/payload_config.h>

#include <cJSON.hpp>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static bool config_path(const char *path, char *out, size_t size) {
  if (!path || path[0] != '/')
    return false;
  const int length = snprintf(out, size, "%s.json", path);
  return length > 0 && (size_t)length < size;
}

static bool read_integer(const cJSON *root, const char *key, int max, int *out) {
  const cJSON *value = cJSON_GetObjectItemCaseSensitive(root, key);
  if (!value)
    return true;
  if (!cJSON_IsNumber(value) || !(value->valuedouble >= 0) ||
      !(value->valuedouble <= max) ||
      value->valuedouble != (double)value->valueint)
    return false;
  *out = value->valueint;
  return true;
}

bool onion_payload_config_load(const char *path, OnionPayloadConfig *out) {
  if (!out)
    return false;
  *out = (OnionPayloadConfig){0};
  char filename[ONION_PAYLOAD_PATH_SIZE];
  if (!config_path(path, filename, sizeof(filename)))
    return false;
  FILE *file = fopen(filename, "rb");
  if (!file)
    return errno == ENOENT;
  char buffer[4096];
  const size_t length = fread(buffer, 1, sizeof(buffer), file);
  const bool readable = !ferror(file) && length < sizeof(buffer);
  fclose(file);
  if (!readable || !length)
    return false;
  const char *end = NULL;
  cJSON *root = cJSON_ParseWithLengthOpts(buffer, length, &end, false);
  while (root && end < buffer + length && isspace((unsigned char)*end))
    ++end;
  OnionPayloadConfig config = {0};
  const cJSON *version = cJSON_GetObjectItemCaseSensitive(root, "version");
  const bool valid = cJSON_IsObject(root) && end == buffer + length &&
                     cJSON_IsNumber(version) && version->valuedouble == 1 &&
                     read_integer(root, "priority", ONION_PAYLOAD_PRIORITY_MAX,
                                  &config.priority) &&
                     read_integer(root, "delay_seconds", ONION_PAYLOAD_DELAY_MAX,
                                  &config.delay_seconds);
  cJSON_Delete(root);
  if (valid)
    *out = config;
  return valid;
}

bool onion_payload_config_save(const char *path, const OnionPayloadConfig *config) {
  if (!config || config->priority < 0 ||
      config->priority > ONION_PAYLOAD_PRIORITY_MAX || config->delay_seconds < 0 ||
      config->delay_seconds > ONION_PAYLOAD_DELAY_MAX)
    return false;
  char filename[ONION_PAYLOAD_PATH_SIZE];
  char temporary[ONION_PAYLOAD_PATH_SIZE + 16];
  if (!config_path(path, filename, sizeof(filename)))
    return false;
  snprintf(temporary, sizeof(temporary), "%s.tmp.XXXXXX", filename);

  cJSON *root = cJSON_CreateObject();
  if (!root)
    return false;
  const bool populated = cJSON_AddNumberToObject(root, "version", 1) &&
      cJSON_AddNumberToObject(root, "priority", config->priority) &&
      cJSON_AddNumberToObject(root, "delay_seconds", config->delay_seconds);
  char *json = populated ? cJSON_Print(root) : NULL;
  cJSON_Delete(root);
  if (!json)
    return false;

  const int fd = mkstemp(temporary);
  if (fd < 0) {
    cJSON_free(json);
    return false;
  }
  const size_t length = strlen(json);
  size_t written = 0;
  while (written < length) {
    const ssize_t result = write(fd, json + written, length - written);
    if (result < 0 && errno == EINTR)
      continue;
    if (result <= 0)
      break;
    written += (size_t)result;
  }
  bool ok = written == length && fsync(fd) == 0;
  if (close(fd) != 0)
    ok = false;
  if (ok)
    ok = rename(temporary, filename) == 0;
  if (!ok)
    unlink(temporary);
  cJSON_free(json);
  return ok;
}

int onion_payload_compare(int priority_a, const char *path_a,
                          int priority_b, const char *path_b) {
  if (priority_a != priority_b)
    return priority_a > priority_b ? -1 : 1;
  char canonical_a[ONION_PAYLOAD_PATH_SIZE];
  char canonical_b[ONION_PAYLOAD_PATH_SIZE];
  if (onion_payload_canonical_path(path_a, canonical_a, sizeof(canonical_a)))
    path_a = canonical_a;
  if (onion_payload_canonical_path(path_b, canonical_b, sizeof(canonical_b)))
    path_b = canonical_b;
  const char *name_a = strrchr(path_a, '/');
  const char *name_b = strrchr(path_b, '/');
  const int name_order = strcmp(name_a ? name_a + 1 : path_a,
                                name_b ? name_b + 1 : path_b);
  return name_order ? name_order : strcmp(path_a, path_b);
}
