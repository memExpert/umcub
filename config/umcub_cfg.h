/*
 * The one header every umcub source includes to see the configuration:
 * constants -> the generated umcub_config.h -> family facts -> what C derives
 * -> sanity checks. Pure preprocessor (usable from linker scripts).
 */
#ifndef UMCUB_CFG_H
#define UMCUB_CFG_H

#include "umcub_config_types.h"
#include "umcub_config.h"
/* Pins written without an AF, completed by the configure step
 * (tools/build/check_pins.py --header): #undef / #define of those options. */
#ifdef UMCUB_CONFIG_PINS
#include UMCUB_CONFIG_PINS
#endif
#include "umcub_family_facts.h"
#include "umcub_config_derived.h"
#include "umcub_config_check.h"

#endif /* UMCUB_CFG_H */
