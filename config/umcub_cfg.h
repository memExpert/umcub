/*
 * The one header every umcub source includes to see the configuration:
 * constants -> board umcub_config.h -> family defaults -> generic defaults
 * -> sanity checks. Pure preprocessor (usable from linker scripts).
 */
#ifndef UMCUB_CFG_H
#define UMCUB_CFG_H

#include "umcub_config_types.h"
/* Optional overlays (build matrix, CI, per-product variants):
 *   UMCUB_CONFIG_PRE  - included before the board config (set #ifndef-guarded options)
 *   UMCUB_CONFIG_POST - included after it (#undef / #define anything) */
#ifdef UMCUB_CONFIG_PRE
#include UMCUB_CONFIG_PRE
#endif
#include "umcub_config.h"
#ifdef UMCUB_CONFIG_POST
#include UMCUB_CONFIG_POST
#endif
#include "umcub_family_defaults.h"
#include "umcub_config_defaults.h"
#include "umcub_config_check.h"

#endif /* UMCUB_CFG_H */
