/*
 * Copyright 2026, WoodenWillis
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

/*
 * No I2C controllers are exposed on this platform.
 *
 * On Tensor G4 the I2C blocks are USI-muxed: a single USI instance can speak
 * UART, SPI or I2C depending on a SW_CONF protocol selector that lives in a
 * separate syscon block, reached via `samsung,usi-phandle` + `samsung,usi-offset`.
 * Switching protocol requires the same firmware-mediated clock/reset path that
 * makes plat/clock.h a stub (see the note there), so there is no standalone
 * MMIO I2C driver to expose here.
 *
 * Nothing in the boot path needs I2C. If a peripheral is attached later,
 * extract that single USI block rather than the whole tree.
 */

enum i2c_id {
    NI2C
};
