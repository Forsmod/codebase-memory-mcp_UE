#ifndef CBM_EXTRACT_UASSET_H
#define CBM_EXTRACT_UASSET_H

#include "cbm.h"

// Extracts Blueprint definitions (Class, Method) and external calls from an Unreal Engine .uasset
// package
void cbm_uasset_extract_blueprint(CBMExtractCtx *ctx);

#endif /* CBM_EXTRACT_UASSET_H */
