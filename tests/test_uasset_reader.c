/*
 * test_uasset_reader.c — Unit tests for binary .uasset package reader.
 */
#include "foundation/compat.h"
#include "foundation/compat_fs.h"

int tf_pass_count = 0;
int tf_fail_count = 0;
int tf_skip_count = 0;
int tf_deselected_count = 0;
bool tf_suite_enter(const char *suite) {
    (void)suite;
    return true;
}
void tf_suite_leave(const char *suite) {
    (void)suite;
}
bool tf_test_selected(const char *test) {
    (void)test;
    return true;
}
void cbm_memev_flush_thread(void) {}
void cbm_log(int level, const char *event, ...) {
    (void)level;
    (void)event;
}

#include "test_framework.h"
#include "uasset_reader.h"
#include "arena.h"

TEST(uasset_truncated_buffer) {
    uint8_t buf[4] = {0xC1, 0x83, 0x2A, 0x9E};
    CBMArena arena;
    cbm_arena_init(&arena);
    uasset_package_t pkg;
    ASSERT_FALSE(uasset_parse_package(buf, sizeof(buf), &arena, &pkg));
    cbm_arena_destroy(&arena);
    PASS();
}

TEST(uasset_invalid_magic) {
    uint8_t buf[32] = {0};
    buf[0] = 0xAA;
    buf[1] = 0xBB;
    buf[2] = 0xCC;
    buf[3] = 0xDD;
    CBMArena arena;
    cbm_arena_init(&arena);
    uasset_package_t pkg;
    ASSERT_FALSE(uasset_parse_package(buf, sizeof(buf), &arena, &pkg));
    cbm_arena_destroy(&arena);
    PASS();
}

// Helper to write int32 little-endian
static void write_i32(uint8_t *p, int32_t val) {
    p[0] = (uint8_t)(val & 0xFF);
    p[1] = (uint8_t)((val >> 8) & 0xFF);
    p[2] = (uint8_t)((val >> 16) & 0xFF);
    p[3] = (uint8_t)((val >> 24) & 0xFF);
}

TEST(uasset_valid_blueprint_package) {
    uint8_t buf[1024] = {0};
    size_t off = 0;

    // Header: Magic + Legacy (-7 for UE4)
    write_i32(buf + off, 0x9E2A83C1);
    off += 4;
    write_i32(buf + off, -7);
    off += 4;

    // UE4 version: legacy3, ue4_ver, lic_ver, cv_count
    write_i32(buf + off, 0);
    off += 4;
    write_i32(buf + off, 522);
    off += 4;
    write_i32(buf + off, 0);
    off += 4;
    write_i32(buf + off, 0);
    off += 4; // 0 CustomVersions

    // TotalHeaderSize
    size_t total_hdr_pos = off;
    off += 4;

    // FolderName ("None\0")
    write_i32(buf + off, 5);
    off += 4;
    memcpy(buf + off, "None\0", 5);
    off += 5;

    // PackageFlags
    write_i32(buf + off, 0);
    off += 4;

    // NameCount, NameOffset
    size_t name_meta_pos = off;
    off += 8;

    // LocalizationId (len = 0)
    write_i32(buf + off, 0);
    off += 4;

    // GatherableText(0,0), ExportCount/Offset, ImportCount/Offset, DependsOffset
    write_i32(buf + off, 0);
    off += 4; // GatherableTextCount
    write_i32(buf + off, 0);
    off += 4; // GatherableTextOffset
    size_t exp_meta_pos = off;
    off += 8;
    size_t imp_meta_pos = off;
    off += 8;
    write_i32(buf + off, 0);
    off += 4; // DependsOffset

    write_i32(buf + total_hdr_pos, (int32_t)off);

    // Write NameMap: 4 names: None, BlueprintGeneratedClass, BP_Hero, Character
    size_t name_offset = off;
    write_i32(buf + name_meta_pos, 4);                        // NameCount
    write_i32(buf + name_meta_pos + 4, (int32_t)name_offset); // NameOffset

    const char *names[] = {"None", "BlueprintGeneratedClass", "BP_Hero", "Character"};
    for (int i = 0; i < 4; i++) {
        int len = (int)strlen(names[i]) + 1;
        write_i32(buf + off, len);
        off += 4;
        memcpy(buf + off, names[i], len);
        off += len;
        write_i32(buf + off, 0);
        off += 4; // 4-byte hash
    }

    // Write ImportMap (1 entry for "Character", size = 36 bytes)
    size_t import_offset = off;
    write_i32(buf + imp_meta_pos, 1); // ImportCount
    write_i32(buf + imp_meta_pos + 4, (int32_t)import_offset);

    // class_pkg (None=0), class_num(0), class_name (Character=3), cn_num(0), outer(0),
    // obj_name(Character=3), on_num(0)
    write_i32(buf + off, 0);
    off += 4;
    write_i32(buf + off, 0);
    off += 4;
    write_i32(buf + off, 3);
    off += 4; // ClassName = Character
    write_i32(buf + off, 0);
    off += 4;
    write_i32(buf + off, 0);
    off += 4; // OuterIndex = 0
    write_i32(buf + off, 3);
    off += 4; // ObjectName = Character
    write_i32(buf + off, 0);
    off += 4;
    write_i32(buf + off, 0);
    off += 4; // PackageName
    write_i32(buf + off, 0);
    off += 4;

    // Write ExportMap (1 entry for "BP_Hero", size = 48 bytes)
    size_t export_offset = off;
    write_i32(buf + exp_meta_pos, 1); // ExportCount
    write_i32(buf + exp_meta_pos + 4, (int32_t)export_offset);

    // class_idx (BlueprintGeneratedClass = name 1 -> import/export class index 1), super_idx (-1
    // for Character import), template(0), outer(0), obj_name (BP_Hero = 2)
    write_i32(buf + off, 1);
    off += 4; // class_index
    write_i32(buf + off, -1);
    off += 4; // super_index (points to Import[0] Character)
    write_i32(buf + off, 0);
    off += 4;
    write_i32(buf + off, 0);
    off += 4;
    write_i32(buf + off, 2);
    off += 4; // obj_name = BP_Hero
    write_i32(buf + off, 0);
    off += 4;
    // fill remainder of export entry to 48 bytes
    off += 24;

    CBMArena arena;
    cbm_arena_init(&arena);
    uasset_package_t pkg;
    ASSERT_TRUE(uasset_parse_package(buf, off, &arena, &pkg));
    ASSERT_EQ(pkg.name_count, 4);
    ASSERT_STR_EQ(pkg.names[2], "BP_Hero");
    ASSERT_EQ(pkg.import_count, 1);
    ASSERT_STR_EQ(pkg.imports[0].object_name, "Character");
    ASSERT_EQ(pkg.export_count, 1);
    ASSERT_STR_EQ(pkg.exports[0].object_name, "BP_Hero");
    ASSERT_EQ(pkg.exports[0].super_index, -1);

    ASSERT_TRUE(uasset_is_blueprint(&pkg));

    cbm_arena_destroy(&arena);
    PASS();
}

TEST(uasset_non_blueprint_package) {
    uint8_t buf[512] = {0};
    size_t off = 0;
    write_i32(buf + off, 0x9E2A83C1);
    off += 4;
    write_i32(buf + off, -7);
    off += 4;
    off += 16; // version info
    size_t total_hdr_pos = off;
    off += 4;
    write_i32(buf + off, 0);
    off += 4; // folder name
    write_i32(buf + off, 0);
    off += 4; // flags
    size_t name_meta = off;
    off += 8;
    off += 4; // loc id
    off += 8; // gatherable
    size_t exp_meta = off;
    off += 8;
    off += 8; // import meta
    off += 4; // depends
    write_i32(buf + total_hdr_pos, (int32_t)off);

    // Names: None, StaticMesh, SM_Rock
    size_t name_off = off;
    write_i32(buf + name_meta, 3);
    write_i32(buf + name_meta + 4, (int32_t)name_off);
    const char *names[] = {"None", "StaticMesh", "SM_Rock"};
    for (int i = 0; i < 3; i++) {
        int len = (int)strlen(names[i]) + 1;
        write_i32(buf + off, len);
        off += 4;
        memcpy(buf + off, names[i], len);
        off += len;
        off += 4; // hash
    }

    // Export: SM_Rock (not a blueprint)
    size_t exp_off = off;
    write_i32(buf + exp_meta, 1);
    write_i32(buf + exp_meta + 4, (int32_t)exp_off);
    write_i32(buf + off, 1);
    off += 4; // class_idx = StaticMesh
    write_i32(buf + off, 0);
    off += 4;
    write_i32(buf + off, 0);
    off += 4;
    write_i32(buf + off, 0);
    off += 4;
    write_i32(buf + off, 2);
    off += 4; // obj_name = SM_Rock
    off += 28;

    CBMArena arena;
    cbm_arena_init(&arena);
    uasset_package_t pkg;
    ASSERT_TRUE(uasset_parse_package(buf, off, &arena, &pkg));
    ASSERT_FALSE(uasset_is_blueprint(&pkg));
    cbm_arena_destroy(&arena);
    PASS();
}

TEST(uasset_corrupted_offsets) {
    uint8_t buf[256] = {0};
    size_t off = 0;
    write_i32(buf + off, 0x9E2A83C1);
    off += 4;
    write_i32(buf + off, -7);
    off += 4;
    off += 16;
    write_i32(buf + off, 100);
    off += 4; // total header
    write_i32(buf + off, 0);
    off += 4; // folder name
    write_i32(buf + off, 0);
    off += 4; // flags
    write_i32(buf + off, 5);
    off += 4; // NameCount = 5
    write_i32(buf + off, 999999);
    off += 4; // NameOffset out of bounds!

    CBMArena arena;
    cbm_arena_init(&arena);
    uasset_package_t pkg;
    ASSERT_FALSE(uasset_parse_package(buf, sizeof(buf), &arena, &pkg));
    cbm_arena_destroy(&arena);
    PASS();
}

SUITE(uasset_reader) {
    RUN_TEST(uasset_truncated_buffer);
    RUN_TEST(uasset_invalid_magic);
    RUN_TEST(uasset_valid_blueprint_package);
    RUN_TEST(uasset_non_blueprint_package);
    RUN_TEST(uasset_corrupted_offsets);
}

int main(void) {
    RUN_SUITE(uasset_reader);
    printf("Tests passed: %d, failed: %d\n", tf_pass_count, tf_fail_count);
    return tf_fail_count == 0 ? 0 : 1;
}
