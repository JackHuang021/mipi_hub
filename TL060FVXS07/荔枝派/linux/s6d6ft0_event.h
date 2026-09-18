/* SPDX-License-Identifier: GPL-2.0-only */
/* Wire layout from Samsung sec_ts g_6ft0.v00; explicit shifts avoid bitfields. */
#ifndef S6D6FT0_EVENT_H
#define S6D6FT0_EVENT_H
struct s6d6ft0_contact {
	unsigned int slot, action, type, x, y, pressure, major, minor;
};

static inline int s6d6ft0_decode(const unsigned char e[8],
			       struct s6d6ft0_contact *c)
{
	unsigned int tid = e[1] & 15;
	unsigned int action = e[0] & 7;

	if ((e[0] >> 6) != 1 || !tid || tid > 10 ||
	    action < 1 || action > 3)
		return 0;
	c->slot = tid - 1;
	c->action = action;
	c->type = (e[0] >> 3) & 7;
	c->x = ((unsigned int)e[2] << 4) | (e[4] >> 4);
	c->y = ((unsigned int)e[3] << 4) | (e[4] & 15);
	c->pressure = e[5];
	c->major = e[6];
	c->minor = e[7];
	return 1;
}
#endif
