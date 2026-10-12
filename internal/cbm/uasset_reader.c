/*
 * uasset_reader.c — Binary parser for Unreal Engine .uasset packages.
 */
#include "uasset_reader.h"
#include <string.h>

static inline bool reader_can_read(const uasset_reader_t *r, size_t n) {
    return !r->error && (r->offset + n <= r->size) && (r->offset + n >= r->offset);
}

static int32_t reader_read_i32(uasset_reader_t *r) {
    if (!reader_can_read(r, 4)) {
        r->error = true;
        return 0;
    }
    const uint8_t *p = r->data + r->offset;
    r->offset += 4;
    return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
                     ((uint32_t)p[3] << 24));
}

static uint32_t reader_read_u32(uasset_reader_t *r) {
    return (uint32_t)reader_read_i32(r);
}

static bool reader_skip(uasset_reader_t *r, size_t n) {
    if (!reader_can_read(r, n)) {
        r->error = true;
        return false;
    }
    r->offset += n;
    return true;
}

static bool reader_seek(uasset_reader_t *r, size_t pos) {
    if (pos > r->size) {
        r->error = true;
        return false;
    }
    r->offset = pos;
    return true;
}

static const char *reader_read_fstring(uasset_reader_t *r, CBMArena *arena) {
    int32_t len = reader_read_i32(r);
    if (r->error || len == 0) {
        return "";
    }

    if (len > 0) {
        // ASCII / Latin-1 string
        size_t byte_len = (size_t)len;
        if (!reader_can_read(r, byte_len) || byte_len > 65536) {
            r->error = true;
            return "";
        }
        const uint8_t *src = r->data + r->offset;
        r->offset += byte_len;

        size_t str_len = byte_len;
        if (str_len > 0 && src[str_len - 1] == '\0') {
            str_len--;
        }

        char *dst = (char *)cbm_arena_alloc(arena, str_len + 1);
        if (!dst)
            return "";
        memcpy(dst, src, str_len);
        dst[str_len] = '\0';
        return dst;
    } else {
        // UTF-16LE string (len is negative)
        int32_t char_count = -len;
        if (char_count > 32768) {
            r->error = true;
            return "";
        }
        size_t byte_len = (size_t)char_count * 2;
        if (!reader_can_read(r, byte_len)) {
            r->error = true;
            return "";
        }
        const uint8_t *src = r->data + r->offset;
        r->offset += byte_len;

        // Convert UTF-16LE to UTF-8
        // Worst-case UTF-8 expansion is 3 bytes per UTF-16 code unit
        size_t max_u8 = (size_t)char_count * 3 + 1;
        char *dst = (char *)cbm_arena_alloc(arena, max_u8);
        if (!dst)
            return "";

        size_t out = 0;
        for (int32_t i = 0; i < char_count; i++) {
            uint16_t u16 = (uint16_t)src[i * 2] | ((uint16_t)src[i * 2 + 1] << 8);
            if (u16 == 0 && i == char_count - 1) {
                break; // trailing null
            }
            if (u16 < 0x80) {
                dst[out++] = (char)u16;
            } else if (u16 < 0x800) {
                dst[out++] = (char)(0xC0 | (u16 >> 6));
                dst[out++] = (char)(0x80 | (u16 & 0x3F));
            } else {
                dst[out++] = (char)(0xE0 | (u16 >> 12));
                dst[out++] = (char)(0x80 | ((u16 >> 6) & 0x3F));
                dst[out++] = (char)(0x80 | (u16 & 0x3F));
            }
        }
        dst[out] = '\0';
        return dst;
    }
}

bool uasset_parse_package(const uint8_t *buffer, size_t size, CBMArena *arena,
                          uasset_package_t *out_pkg) {
    if (!buffer || size < 64 || !out_pkg || !arena) {
        return false;
    }
    memset(out_pkg, 0, sizeof(*out_pkg));

    uasset_reader_t r = {.data = buffer, .size = size, .offset = 0, .error = false};

    uint32_t magic = reader_read_u32(&r);
    if (magic != UASSET_PACKAGE_MAGIC) {
        return false;
    }

    out_pkg->legacy_version = reader_read_i32(&r);
    bool is_ue54_plus = false;
    int32_t cv_count = 0;

    if (out_pkg->legacy_version <= -9) {
        // UE 5.4+
        reader_skip(&r, 4); // legacy3
        out_pkg->file_version_ue4 = reader_read_i32(&r);
        out_pkg->file_version_ue5 = reader_read_i32(&r);
        reader_skip(&r, 4);  // licensee_ver
        reader_skip(&r, 20); // SavedHash
        reader_read_i32(&r); // TotalHeaderSize
        cv_count = reader_read_i32(&r);
        if (cv_count < 0 || cv_count > 10000 || !reader_skip(&r, (size_t)cv_count * 20)) {
            return false;
        }
        is_ue54_plus = true;
    } else if (out_pkg->legacy_version <= -8) {
        // UE 5.0 - 5.3
        reader_skip(&r, 4); // legacy3
        out_pkg->file_version_ue4 = reader_read_i32(&r);
        out_pkg->file_version_ue5 = reader_read_i32(&r);
        reader_skip(&r, 4); // licensee_ver
        cv_count = reader_read_i32(&r);
        if (cv_count < 0 || cv_count > 10000 || !reader_skip(&r, (size_t)cv_count * 20)) {
            return false;
        }
    } else if (out_pkg->legacy_version <= -7) {
        // UE 4
        reader_skip(&r, 4); // legacy3
        out_pkg->file_version_ue4 = reader_read_i32(&r);
        reader_skip(&r, 4); // licensee_ver
        cv_count = reader_read_i32(&r);
        if (cv_count < 0 || cv_count > 10000 || !reader_skip(&r, (size_t)cv_count * 20)) {
            return false;
        }
    } else {
        return false;
    }

    if (!is_ue54_plus) {
        reader_read_i32(&r); // TotalHeaderSize
    }

    // Skip FolderName (FString)
    int32_t fn_len = reader_read_i32(&r);
    if (fn_len > 0) {
        if (!reader_skip(&r, (size_t)fn_len))
            return false;
    } else if (fn_len < 0) {
        if (!reader_skip(&r, (size_t)(-fn_len) * 2))
            return false;
    }

    out_pkg->package_flags = reader_read_u32(&r);

    int32_t name_count = reader_read_i32(&r);
    int32_t name_offset = reader_read_i32(&r);
    if (r.error || name_count < 0 || name_count > 1000000 || (size_t)name_offset > size) {
        return false;
    }

    if (is_ue54_plus) {
        reader_skip(&r, 8); // SoftObjectPathsCount, SoftObjectPathsOffset
    }

    // LocalizationId
    if (reader_can_read(&r, 4)) {
        int32_t loc_len = reader_read_i32(&r);
        if (loc_len > 0) {
            reader_skip(&r, (size_t)loc_len);
        } else if (loc_len < 0) {
            reader_skip(&r, (size_t)(-loc_len) * 2);
        }
    }

    // Table offsets
    reader_read_i32(&r); // GatherableTextCount
    reader_read_i32(&r); // GatherableTextOffset
    int32_t export_count = reader_read_i32(&r);
    int32_t export_offset = reader_read_i32(&r);
    int32_t import_count = reader_read_i32(&r);
    int32_t import_offset = reader_read_i32(&r);
    int32_t depends_offset = reader_read_i32(&r);
    int32_t soft_offset = 0;
    if (is_ue54_plus) {
        soft_offset = reader_read_i32(&r);
    }

    if (r.error || export_count < 0 || import_count < 0) {
        return false;
    }

    // 1. Read NameMap
    if (!reader_seek(&r, (size_t)name_offset)) {
        return false;
    }

    out_pkg->name_count = name_count;
    out_pkg->names =
        (const char **)cbm_arena_calloc(arena, (size_t)name_count * sizeof(const char *));
    if (!out_pkg->names && name_count > 0)
        return false;

    for (int32_t i = 0; i < name_count; i++) {
        out_pkg->names[i] = reader_read_fstring(&r, arena);
        reader_skip(&r, 4); // skip 4 bytes non-case preserving hash
        if (r.error)
            return false;
    }

// Helper macro to safely get name string by index
#define GET_NAME(idx) ((idx >= 0 && idx < name_count) ? out_pkg->names[idx] : "?")

    // 2. Read ImportMap
    if (import_count > 0 && export_offset > import_offset) {
        size_t entry_size = (size_t)(export_offset - import_offset) / (size_t)import_count;
        if (entry_size >= 28 && reader_seek(&r, (size_t)import_offset)) {
            out_pkg->import_count = import_count;
            out_pkg->imports = (uasset_import_t *)cbm_arena_calloc(
                arena, (size_t)import_count * sizeof(uasset_import_t));
            if (!out_pkg->imports)
                return false;

            for (int32_t i = 0; i < import_count; i++) {
                size_t entry_start = r.offset;
                int32_t cp_idx = reader_read_i32(&r);
                reader_skip(&r, 4); // class_pkg, number
                int32_t cn_idx = reader_read_i32(&r);
                reader_skip(&r, 4); // class_name, number
                int32_t outer = reader_read_i32(&r);
                int32_t on_idx = reader_read_i32(&r);
                reader_skip(&r, 4); // object_name, number

                out_pkg->imports[i].class_package = GET_NAME(cp_idx);
                out_pkg->imports[i].class_name = GET_NAME(cn_idx);
                out_pkg->imports[i].outer_index = outer;
                out_pkg->imports[i].object_name = GET_NAME(on_idx);

                if (entry_size >= 36 && reader_can_read(&r, 8)) {
                    int32_t pn_idx = reader_read_i32(&r);
                    reader_skip(&r, 4);
                    out_pkg->imports[i].package_name = GET_NAME(pn_idx);
                }

                if (!reader_seek(&r, entry_start + entry_size)) {
                    break;
                }
            }
        }
    }

    // 3. Read ExportMap
    int32_t export_end = (depends_offset > export_offset) ? depends_offset : soft_offset;
    if (export_end <= export_offset || (size_t)export_end > size) {
        export_end = (int32_t)size;
    }
    if (export_count > 0 && export_end > export_offset) {
        size_t entry_size = (size_t)(export_end - export_offset) / (size_t)export_count;
        if (entry_size >= 24 && reader_seek(&r, (size_t)export_offset)) {
            out_pkg->export_count = export_count;
            out_pkg->exports = (uasset_export_t *)cbm_arena_calloc(
                arena, (size_t)export_count * sizeof(uasset_export_t));
            if (!out_pkg->exports)
                return false;

            for (int32_t i = 0; i < export_count; i++) {
                size_t entry_start = r.offset;
                out_pkg->exports[i].class_index = reader_read_i32(&r);
                out_pkg->exports[i].super_index = reader_read_i32(&r);
                out_pkg->exports[i].template_index = reader_read_i32(&r);
                out_pkg->exports[i].outer_index = reader_read_i32(&r);
                int32_t on_idx = reader_read_i32(&r);
                reader_skip(&r, 4); // object_name, number
                out_pkg->exports[i].object_name = GET_NAME(on_idx);

                if (!reader_seek(&r, entry_start + entry_size)) {
                    break;
                }
            }
        }
    }

#undef GET_NAME
    return true;
}

const char *uasset_resolve_name(const uasset_package_t *pkg, int32_t index) {
    if (!pkg)
        return NULL;
    if (index > 0 && (index - 1) < pkg->export_count) {
        return pkg->exports[index - 1].object_name;
    }
    if (index < 0) {
        int32_t imp_idx = -index - 1;
        if (imp_idx < pkg->import_count) {
            return pkg->imports[imp_idx].object_name;
        }
    }
    return NULL;
}

static bool str_ends_with(const char *str, const char *suffix) {
    if (!str || !suffix)
        return false;
    size_t s_len = strlen(str);
    size_t suf_len = strlen(suffix);
    if (s_len < suf_len)
        return false;
    return strcmp(str + s_len - suf_len, suffix) == 0;
}

bool uasset_is_blueprint(const uasset_package_t *pkg) {
    if (!pkg || pkg->name_count == 0 || pkg->export_count == 0)
        return false;

    for (int32_t i = 0; i < pkg->name_count; i++) {
        const char *name = pkg->names[i];
        if (name && (strstr(name, "BlueprintGeneratedClass") != NULL ||
                     strstr(name, "WidgetBlueprintGeneratedClass") != NULL ||
                     strstr(name, "AnimBlueprintGeneratedClass") != NULL)) {
            return true;
        }
    }
    return false;
}
