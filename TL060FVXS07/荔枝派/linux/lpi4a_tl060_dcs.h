/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LPI4A_TL060_DCS_H
#define LPI4A_TL060_DCS_H

/* DCS1.2 section6.17: D4=sleep-off,D3=normal,D2=display-on.
 * D7 is reserved; requiring the observed0x9c wrongly rejects valid0x1c.
 * Do not use reserved/vendor-specific bits as proof of electrical safety.
 */
static inline int lpi4a_tl060_power_ready(unsigned char mode)
{
    return (mode & 0x1c) == 0x1c;
}
#endif
