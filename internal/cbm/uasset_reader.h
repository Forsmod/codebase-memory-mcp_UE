#ifndef CBM_UASSET_READER_H
#define CBM_UASSET_READER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "arena.h"

#define UASSET_PACKAGE_MAGIC 0x9E2A83C1

typedef struct {
    const char *class_package;
    const char *class_name;
    int32_t outer_index;
    const char *object_name;
    const char *package_name;
} uasset_import_t;

typedef struct {
    int32_t class_index;
    int32_t super_index;
    int32_t template_index;
    int32_t outer_index;
    const char *object_name;
} uasset_export_t;

typedef struct {
    int32_t legacy_version;
    int32_t file_version_ue4;
    int32_t file_version_ue5;
    uint32_t package_flags;

    int32_t name_count;
    const char **names;

    int32_t import_count;
    uasset_import_t *imports;

    int32_t export_count;
    uasset_export_t *exports;
} uasset_package_t;

typedef struct {
    const uint8_t *data;
    size_t size;
    size_t offset;
    bool error;
} uasset_reader_t;

// Parse a .uasset package from memory buffer using the provided arena for string/table allocations
bool uasset_parse_package(const uint8_t *buffer, size_t size, CBMArena *arena,
                          uasset_package_t *out_pkg);

// Fast check whether the package exports any Blueprint generated classes
bool uasset_is_blueprint(const uasset_package_t *pkg);

// Resolve an object name by its index:
// If index > 0: exports[index - 1].object_name
// If index < 0: imports[-index - 1].object_name
// If index == 0: NULL
const char *uasset_resolve_name(const uasset_package_t *pkg, int32_t index);

#endif /* CBM_UASSET_READER_H */
