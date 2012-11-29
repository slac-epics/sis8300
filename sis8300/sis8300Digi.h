#ifndef SIS8300DIGI_H
#define SIS8300DIGI_H

#define SIS8300_KIND_OFF  (-1)
#define SIS8300_KIND_BEAM 0
#define SIS8300_KIND_CRED 1
#define SIS8300_KIND_CGRN 2

typedef uint64_t Sis8300ChannelSel;

typedef struct Si5326Parms_ {
	unsigned long fin;
	unsigned n3, n2h, n2l, n1h, nc;
	unsigned bw;
	int      wb;
} Si5326ParmsRec, *Si5326Parms;

/* clkhl value to bypass the 9510 divider; the divider
 * is automatically engaged (div by 2) if no si5326_parms are
 * given.
 */
#define SIS8300_BYPASS_9510_DIVIDER 0xffffffff
int
sis8300DigiSetup(int fd, Si5326Parms si5326_parms, unsigned clkhl, int exttrig_en);

/* channel_selector defines the order (and number) of channels in memory.
 * E.g., to have channels 4, 1, 8, 9 in this order in memory set 
 * 'channel_selector' = (9 << 12) | (8 << 8) | (1 << 4) | (4 << 0)
 * NOTE: channels are numbered 1..10; a 0 marks the end of the selected
 *       channels;
 *
 * 'nsmpl' defines samples per channel which MUST be a multiple of 16!
 */
int
sis8300DigiValidateSel(Sis8300ChannelSel sel);

int
sis8300DigiSetCount(int fd, Sis8300ChannelSel channel_selector, unsigned nsmpl);

int
sis8300DigiArm(int fd, int kind);

void
sis8300DigiSetSim(int fd, int32_t a, int32_t b, int32_t c, int32_t d, int quiet);

#endif
