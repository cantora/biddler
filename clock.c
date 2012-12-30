#include "clock.h"

void clock_init(struct clock *c) {
	c->time = 0;
	c->count = 0;
}

void clock_set_time(struct clock *c, uint32_t t) {
	c->time = t;
}

uint32_t clock_time(struct clock *c) {
	return c->time;
}

void clock_process(struct clock *c, clock_flag *flag) {
	c->count++;
#ifdef CHECK_OVERFLOW
	if(c->count == 0) {
		*flag = CLOCK_OVERFLOW;
		return;
	}
#endif
	if(c->count >= c->time)
		*flag = CLOCK_ALARM;
	else
		*flag = CLOCK_OK;
}

/* when process returns a CLOCK_ALARM, call this to reset counter */
void clock_reset(struct clock *c) {
	c->count = 0;
}

uint32_t clock_count(struct clock *c) {
	return c->count;
}
