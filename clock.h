#ifndef BIDDLER_CLOCK
#define BIDDLER_CLOCK

#include <stdint.h>

typedef enum clock_flag {
	CLOCK_OK = 0,
	CLOCK_ALARM,
	CLOCK_OVERFLOW
};

struct clock {
	uint32_t time;
	uint32_t count;	
};

void clock_init(struct clock *c);
void clock_set_time(struct clock *c, uint32_t t);
uint32_t clock_time(struct clock *c);
void clock_process(struct clock *c, clock_flag *flag);
void clock_reset(struct clock *c);
uint32_t clock_count(struct clock *c);

#endif /* BIDDLER_CLOCK */
