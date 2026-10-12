/*
 * extract_uasset.c — Semantic Blueprint extractor for Unreal Engine .uasset packages.
 */
#include "extract_uasset.h"
#include "uasset_reader.h"
#include "helpers.h"
#include <string.h>
#include <stdio.h>

static void get_asset_base_name(const char *rel_path, char *out, size_t out_sz) {
    if (!rel_path || out_sz == 0) {
        if (out_sz > 0)
            out[0] = '\0';
        return;
    }
    const char *slash = strrchr(rel_path, '/');
#ifdef _WIN32
    const char *bslash = strrchr(rel_path, '\\');
    if (bslash && (!slash || bslash > slash)) {
        slash = bslash;
    }
#endif
    const char *fname = slash ? slash + 1 : rel_path;
    size_t len = strlen(fname);
    if (len > 7 && strcmp(fname + len - 7, ".uasset") == 0) {
        len -= 7;
    }
    if (len >= out_sz) {
        len = out_sz - 1;
    }
    memcpy(out, fname, len);
    out[len] = '\0';
}

void cbm_uasset_extract_blueprint(CBMExtractCtx *ctx) {
    if (!ctx || !ctx->source || ctx->source_len < 64 || !ctx->result || !ctx->arena ||
        !ctx->scratch) {
        return;
    }

    CBMArena *a = ctx->arena;
    CBMArena *scratch = ctx->scratch;

    uasset_package_t pkg;
    if (!uasset_parse_package((const uint8_t *)ctx->source, (size_t)ctx->source_len, scratch,
                              &pkg)) {
        return;
    }

    if (!uasset_is_blueprint(&pkg)) {
        return;
    }

    char asset_name[256];
    get_asset_base_name(ctx->rel_path, asset_name, sizeof(asset_name));

    char expected_c_name[260];
    snprintf(expected_c_name, sizeof(expected_c_name), "%s_C", asset_name);

    // 1. Locate primary Blueprint Class export
    const uasset_export_t *bp_export = NULL;
    for (int32_t i = 0; i < pkg.export_count; i++) {
        if (strcmp(pkg.exports[i].object_name, expected_c_name) == 0) {
            bp_export = &pkg.exports[i];
            break;
        }
    }

    if (!bp_export) {
        for (int32_t i = 0; i < pkg.export_count; i++) {
            if (strcmp(pkg.exports[i].object_name, asset_name) == 0) {
                bp_export = &pkg.exports[i];
                break;
            }
        }
    }

    if (!bp_export) {
        for (int32_t i = 0; i < pkg.export_count; i++) {
            const char *oname = pkg.exports[i].object_name;
            size_t nlen = strlen(oname);
            if (nlen > 2 && strcmp(oname + nlen - 2, "_C") == 0 &&
                strncmp(oname, "Default__", 9) != 0) {
                bp_export = &pkg.exports[i];
                break;
            }
        }
    }

    if (!bp_export && pkg.export_count > 0) {
        bp_export = &pkg.exports[0];
    }

    if (!bp_export) {
        return;
    }

    const char *class_name = bp_export->object_name;
    const char *parent_class = "UObject";

    if (bp_export->super_index < 0) {
        int32_t imp_idx = -bp_export->super_index - 1;
        if (imp_idx >= 0 && imp_idx < pkg.import_count) {
            parent_class = pkg.imports[imp_idx].object_name;
        }
    } else if (bp_export->super_index > 0) {
        int32_t exp_idx = bp_export->super_index - 1;
        if (exp_idx >= 0 && exp_idx < pkg.export_count) {
            parent_class = pkg.exports[exp_idx].object_name;
        }
    }

    const char *class_qn = cbm_arena_sprintf(a, "%s.%s", ctx->module_qn, class_name);

    // Push Class Definition
    CBMDefinition class_def = {0};
    class_def.name = cbm_arena_strdup(a, class_name);
    class_def.qualified_name = class_qn;
    class_def.label = cbm_arena_strdup(a, "Class");
    class_def.file_path = ctx->rel_path;
    class_def.start_line = 1;
    class_def.end_line = 1;

    if (parent_class && strcmp(parent_class, "None") != 0 && strcmp(parent_class, "?") != 0) {
        const char **bases = (const char **)cbm_arena_calloc(a, 2 * sizeof(const char *));
        if (bases) {
            bases[0] = cbm_arena_strdup(a, parent_class);
            bases[1] = NULL;
            class_def.base_classes = bases;
        }
    }
    cbm_defs_push(&ctx->result->defs, a, class_def);

    // 2. Extract Methods & Local Events from exports
    for (int32_t i = 0; i < pkg.export_count; i++) {
        if (&pkg.exports[i] == bp_export)
            continue;

        const char *func_class = uasset_resolve_name(&pkg, pkg.exports[i].class_index);
        if (!func_class && pkg.exports[i].class_index >= 0 &&
            pkg.exports[i].class_index < pkg.name_count) {
            func_class = pkg.names[pkg.exports[i].class_index];
        }

        if (func_class && strcmp(func_class, "Function") == 0) {
            const char *method_name = pkg.exports[i].object_name;
            if (method_name && strcmp(method_name, "None") != 0 && strcmp(method_name, "?") != 0) {
                CBMDefinition method_def = {0};
                method_def.name = cbm_arena_strdup(a, method_name);
                method_def.qualified_name = cbm_arena_sprintf(a, "%s.%s", class_qn, method_name);
                method_def.parent_class = class_qn;
                method_def.label = cbm_arena_strdup(a, "Method");
                method_def.file_path = ctx->rel_path;
                method_def.start_line = 1;
                method_def.end_line = 1;
                cbm_defs_push(&ctx->result->defs, a, method_def);
            }
        }
    }

    // 3. Extract External Function Calls from imports
    for (int32_t i = 0; i < pkg.import_count; i++) {
        if (pkg.imports[i].class_name && strcmp(pkg.imports[i].class_name, "Function") == 0) {
            const char *func_name = pkg.imports[i].object_name;
            if (!func_name || strcmp(func_name, "None") == 0 || strcmp(func_name, "?") == 0) {
                continue;
            }

            const char *callee = NULL;
            if (pkg.imports[i].outer_index < 0) {
                int32_t outer_imp = -pkg.imports[i].outer_index - 1;
                if (outer_imp >= 0 && outer_imp < pkg.import_count) {
                    const char *outer_name = pkg.imports[outer_imp].object_name;
                    if (outer_name && strcmp(outer_name, "None") != 0 &&
                        strcmp(outer_name, "?") != 0) {
                        callee = cbm_arena_sprintf(a, "%s.%s", outer_name, func_name);
                    }
                }
            }

            if (!callee) {
                callee = cbm_arena_strdup(a, func_name);
            }

            CBMCall call = {0};
            call.callee_name = callee;
            call.enclosing_func_qn = class_qn;
            call.start_line = 1;
            call.is_method = true;
            cbm_calls_push(&ctx->result->calls, a, call);
        }
    }
}
