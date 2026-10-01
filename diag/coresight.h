/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

typedef void (*cs_puts_fn)(const char *s);

void cs_sample_all(cs_puts_fn puts);
