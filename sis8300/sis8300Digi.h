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
 * NOTE:    The user must set the PLL input frequency (250000000)
 *          as well as the mode the device is using before calling
 *          this routine. The 'fout' parameter is in Hz.
 *
 * RETURNS: 0 on success, nonzero on error (*p does not contain
 *          valid settings in this case).
 *
 *          On success the 'n3, n2h, n2l, n1h, nc' and 'bw' are
 *          filled into *p by the routine.
 */
int
si53xx_calcParms(uint64_t fout, Si5326Parms p, int verbose);

/* Probe the 5326 chip to find out if it has a usable narrow-band
 * reference, if it is strapped for wide-band mode or if there is
 * no valid reference (original Sis8300 module had Si5326 strapped
 * for ext. reference but none loaded).
 *
 * NOTE: This routine results in a SOFT-RESET of the Si5326 chip!
 */
Si5326Mode
sis8300ClkDetect(int fd);

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

#endif
