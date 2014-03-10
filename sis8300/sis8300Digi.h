#ifndef SIS8300DIGI_H
#define SIS8300DIGI_H

#include <stdint.h>

#define SIS8300_KIND_OFF  (-1)
#define SIS8300_KIND_BEAM 0
#define SIS8300_KIND_CRED 1
#define SIS8300_KIND_CGRN 2

typedef uint64_t Sis8300ChannelSel;

typedef struct Si5326Parms_ {
	unsigned long fin;
	unsigned n3, n2h, n2l, n1h, nc;
	unsigned bw;
	int      bwsel;
	int      wb;
} Si5326ParmsRec, *Si5326Parms;

typedef enum Si5326Mode_ {
	/* Error communicating with device; errno may be set */
	Si5326_Error          = -1,
	Si5326_NoReference    = 0,
	Si5326_NarrowbandMode = 1,
	Si5326_WidebandMode   = 2
} Si5326Mode;

/* Try to compute divider configuration for output frequency 'fout'.
 * Note that 'fout' is the si53xx output frequency may be further
 * divided by the 9510.
 *
 *
 * NOTE:    The user must set the PLL input frequency (250000000),
 *          the desired PLL bandwidth as well as the mode the device is
 *          using before calling this routine.
 *          The 'fout' parameter is in Hz.
 *
 * RETURNS: 0 on success, nonzero on error (*p does not contain
 *          valid settings in this case).
 *
 *          On success the 'n3, n2h, n2l, n1h, nc, bwsel' and
 *          realizable 'bw' are filled into *p by the routine.
 */
int
si53xx_calcParms(uint64_t fout, Si5326Parms p, int verbose);

/*
 * Program the si5326 with the given parameters
 *
 * RETURNS: -1 on error; output frequency (which may
 *          differ from the requested frequency due to
 *          rational approximation).
 */
int64_t
si5326_setup(int fd, Si5326Parms p);

/*
 * Obtain basic status of the si5326
 */
#define SIS8300_SI5326_NO_REF	(1<<0) /* no reference present */
#define SIS8300_SI5326_NO_CLK1  (1<<1) /* no signal at CLK1    */
#define SIS8300_SI5326_NO_CLK2  (1<<2) /* no signal at CLK2    */
#define SIS8300_SI5326_NO_LOCK  (1<<8) /* PLL not locked       */
int
si5326_status(int fd);

/* Probe the 5326 chip to find out if it has a usable narrow-band
 * reference, if it is strapped for wide-band mode or if there is
 * no valid reference (original Sis8300 module had Si5326 strapped
 * for ext. reference but none loaded).
 *
 * NOTE: This routine results in a SOFT-RESET of the Si5326 chip!
 */
Si5326Mode
sis8300ClkDetect(int fd);

/* Change the 9510 divider - clkhl is *not* the divider ratio
 * but the pattern of hi/lo times (consult the ad9510 datasheet)
 */
void
sis8300DigiSet9510Divider(int fd, unsigned clkhl);

/* Bypass the 9510 divider (div. ratio 0)             */
#define SIS8300_BYPASS_9510_DIVIDER 0xffffffff

/* Hold 9510 output steady low (div. ration infinite) */
#define SIS8300_SILENT_9510_DIVIDER 0xfffffffe

/*
 * Convert ratio to clkhl pattern.
 * Valid divider ratios are even numbers up to and including 32
 * as well as 1 (in which case the routine returns
 * SIS8300_BYPASS_9510_DIVIDER).
 * Odd ratios (different from 1) are rounded down; the result
 * for ratios bigger than 32 are undefined.
 */
unsigned
sis8300DigiGet9510Clkhl(unsigned ratio);


/* clkhl value to bypass the 9510 divider; the divider
 * is automatically engaged (div by 2) if no si5326_parms are
 * given.
 */
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

/* Build a basic channel selector starting at channel 'start' up
 * to and including 'end', i.e., 
 *   (start<<0) | ((start+1)<<4) | ... | ((end)<<x)
 *
 * RETURNS selector or empty selector on error (start and end must be
 * 1..10, start <= end).
 */
Sis8300ChannelSel
sis8300BuildChannelSel(unsigned start, unsigned end);

int
sis8300DigiValidateSel(int fd, Sis8300ChannelSel sel);

int
sis8300DigiSetCount(int fd, Sis8300ChannelSel channel_selector, unsigned nsmpl);

int
sis8300DigiArm(int fd, int kind);

void
sis8300DigiSetSim(int fd, int32_t a, int32_t b, int32_t c, int32_t d, int quiet);

/* Optionally write qspi message to remote device and
 * optionally read response.
 * If 'data_out' non-negative then send message (data_out).
 * If data_in is not NULL then read response 
 * and store in *data_in.
 *
 * RETURNS: 0 on success
 *          nonzero (-1) if transfer timed out
 */
int 
sis8300DigiQspiWriteRead(const void *device, int data_out, uint16_t *data_in);

int
sis8300DigiReadReg(int fd, unsigned reg, uint32_t *val_p);

int
sis8300DigiWriteReg(int fd, unsigned reg, uint32_t val);

/* Retrieve chip ID of the first ADC chip
 *
 * RETURNS: chip ID or a negative number on error.
 */
int
sis8300DigiGetADC_ID(int fd);

/* Determine the max. clock frequency supported by the
 * first ADC chip on board.
 * 
 * RETURNS: max. clock frequency or zero on error,
 */
unsigned long
sis8300DigiGetFclkMax(int fd);

/* Set tap delay for fclk (Hz) -- it SUCKS that we have to to this */
void
sis8300DigiSetTapDelay(int fd, unsigned long fclk);

#endif
