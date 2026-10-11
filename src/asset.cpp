#include "asset.h"
#include <stdio.h>
#include <stdlib.h>

char* asset_load(const char* path, size_t* out_size) {
    FILE* f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "asset_load: cannot open %s\n", path); return NULL; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* buf = (char*)malloc((size_t)n + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    if (got != (size_t)n) { fprintf(stderr, "asset_load: short read on %s\n", path); free(buf); return NULL; }
    buf[n] = '\0';
    if (out_size) *out_size = (size_t)n;
    return buf;
}
