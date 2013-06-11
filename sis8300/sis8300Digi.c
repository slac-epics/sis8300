#include <inttypes.h>

#include <errno.h>
#include <stdio.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <string.h>

#include <sis8300_defs.h>
#include <sis8300_reg.h>

#include <sis8300Digi.h>

typedef int32_t Ampl_t;
typedef Ampl_t Ampl[4];

/* 4 chars to little-endian 32-bit int */
#define CHTO32(a,b,c,d)  (((a)<<0) | ((b)<<8) | ((c)<<16) | ((d)<<24))

#define SIS8300_QSPI_REG 0x400

#undef  MEASURE_POLLING

/* Register access primitives */
static void us_sleep(unsigned us)
{
struct timespec t, rem;
	if ( us >= 1000000UL ) {
		t.tv_sec  = us/1000000UL;
		t.tv_nsec = (us - t.tv_sec * 1000000UL)*1000UL;
	} else {
		t.tv_sec  = 0;
		t.tv_nsec = us * 1000UL;
	}
	while ( nanosleep( &t, &rem ) && EINTR == errno )
		t=rem;
}

static uint32_t
rrd(int fd, unsigned off)
{
sis8300_reg r;
	r.offset = off;
	if ( ioctl(fd, SIS8300_REG_READ, &r) ) {
		fprintf(stderr,"ERROR: ioctl(SIS8300_REG_READ) failed: %s\n", strerror(errno));
	}
	return r.data;
}

static void
rwr(int fd, unsigned off, uint32_t val)
{
sis8300_reg r;
	r.offset = off;
	r.data   = val;
	if ( ioctl(fd, SIS8300_REG_WRITE, &r) ) {
		fprintf(stderr,"ERROR: ioctl(SIS8300_REG_WRITE) failed: %s\n", strerror(errno));
	}
}

static void
rwr_p(int fd, unsigned off, uint32_t *val_p)
{
	rwr(fd, off, *val_p);
}

static void
rrd_p(int fd, unsigned off, uint32_t *val_p)
{
	*val_p = rrd(fd, off);
}

/* AD9268 ADC access primitives */

static void
adc_wr(int fd, unsigned inst, unsigned a, unsigned v)
{
uint32_t cmd;

	if ( inst > 4 )
		return;

	cmd  = inst << 24;

	cmd |= ((a&0xff)<<8) | (v&0xff);

	rwr(fd, SIS8300_ADC_SPI_REG, cmd);
	us_sleep(1);
}

/* AD9510 access primitives */
static void
ad9510_wr(int fd, unsigned inst, unsigned a, unsigned v)
{
uint32_t cmd = AD9510_GENERATE_SPI_RW_CMD;

	if ( inst )
		cmd |= AD9510_SPI_SELECT_NO2;

	cmd |= ((a&0xff)<<8) | (v&0xff);

	rwr(fd, SIS8300_AD9510_SPI_REG, cmd);
	us_sleep(1);
}

/* Si5326 access primitives */
static void
si5326_xact(void (*op)(int,unsigned,uint32_t *), int fd, unsigned off, uint32_t *v_p)
{
int retries;
unsigned o = SIS8300_CLOCK_MULTIPLIER_SPI_REG;
	/* busy-wait while SPI state machine is busy */
	for ( retries = 0; retries < 10; retries++ ) {
		if ( 0 == (rrd(fd, o) & 0x80000000) ) {
			/* now do the operation */
			op(fd, off, v_p);
			return;
		}
		us_sleep(10);
	}
	fprintf(stderr,"si5326_xact: Too many retries -- unable to program the Si5326!\n");
}

static uint32_t
si5326_rd(int fd, unsigned addr) __attribute__((unused));

static uint32_t
si5326_rd(int fd, unsigned addr)
{
uint32_t v = addr;
unsigned o = SIS8300_CLOCK_MULTIPLIER_SPI_REG;
	/* write address */
	si5326_xact(rwr_p, fd, o, &v);
	v = 0x8000; /* read register command */
	si5326_xact(rwr_p, fd, o, &v);
	/* Seems we must have to repeat the read operation
	 * something in the struck firmware doesn't behave right...
	 * Maybe fixed in later firmware?
	 */
	si5326_xact(rwr_p, fd, o, &v);
	si5326_xact(rrd_p, fd, o, &v);
	return v;
}

static void
si5326_wr(int fd, unsigned addr, uint32_t val)
{
uint32_t v = addr;
unsigned o = SIS8300_CLOCK_MULTIPLIER_SPI_REG;
	/* write address */
	si5326_xact(rwr_p, fd, o, &v);
	/* write register command */
	v = 0x4000 | (val & 0xff);
	si5326_xact(rwr_p, fd, o, &v);
}

/* Setup of ADC */

static void
adc_setup(int fd, unsigned inst)
{
	/* output type LVDS; two-s complement*/
	adc_wr( fd, inst, 0x14, 0x41 );
	adc_wr( fd, inst, 0x16, 0x00 );
	adc_wr( fd, inst, 0x17, 0x00 );

	/* update cmd */
	adc_wr( fd, inst, 0xff, 0x01 );
}

/* Setup of AD9510; options are divider 'high' and 'low' clocks
 * Divider ratio is: ( high + 1 ) + ( low + 1 )
 */
static void
ad9510_setup(int fd, unsigned i, unsigned clkhl)
{
unsigned bypss;

	if ( clkhl > 0xff ) {
		bypss = 0x80;
		clkhl = 0x00;
	} else {
		bypss = 0x00;
	}

	/* soft reset; bidirectional SPI mode */
	ad9510_wr(fd, i, 0x00, 0xb0);
	/* clear reset;                       */
	ad9510_wr(fd, i, 0x00, 0x90);
	/*  shold be default anyways: asynchr. power down, no prescaler */
	ad9510_wr(fd, i, 0xa0, 0x01);
	/* power-down outputs 0..3            */
	ad9510_wr(fd, i, 0x3c, 0x0b);
	ad9510_wr(fd, i, 0x3d, 0x0b);
	ad9510_wr(fd, i, 0x3e, 0x0b);
	ad9510_wr(fd, i, 0x3f, 0x0b);
	/* lvds@3.5mA outputs 4..7            */
	ad9510_wr(fd, i, 0x40, 0x02);
	ad9510_wr(fd, i, 0x41, 0x02);
	ad9510_wr(fd, i, 0x42, 0x02);
	ad9510_wr(fd, i, 0x43, 0x02);
	/* power down refin, clock-pll-prescaler, clk2 */
	ad9510_wr(fd, i, 0x45, 0x1d);
	/* Clock divider for outputs 4..7 */
	/* I don't understand this (undocumented) but the demo 
	 * software does it like this...
	 * AD9510 #2, Out4 is Fpga CLK49
	 */
	if ( i ) {
		ad9510_wr(fd, i, 0x50, 0x00);
		ad9510_wr(fd, i, 0x51, 0xc0); 
	} else {
		ad9510_wr(fd, i, 0x50, clkhl);
		ad9510_wr(fd, i, 0x51, bypss); 
	}
	ad9510_wr(fd, i, 0x52, clkhl);
	ad9510_wr(fd, i, 0x53, bypss); 
	ad9510_wr(fd, i, 0x54, clkhl);
	ad9510_wr(fd, i, 0x55, bypss); 
	ad9510_wr(fd, i, 0x56, clkhl);
	ad9510_wr(fd, i, 0x57, bypss); 

	/* Function select: SYNCB */
	ad9510_wr(fd, i, 0x58, 0x22);

	/* UPDATE */
	ad9510_wr(fd, i, 0x5a, 0x01 ); 
}


#ifdef MEASURE_POLLING
static unsigned long
measure_polling(int fd, unsigned msk)
{
int i;
struct timeval then, now;

	gettimeofday( &then, 0 );
	for ( i=0; i<1000; i++ ) {
		us_sleep( 1000 );
		if ( ! (si5326_rd(fd,129) & msk) ) {
			gettimeofday( &now, 0 );
			now.tv_sec -= then.tv_sec;
			if ( now.tv_usec < then.tv_usec ) {
				now.tv_sec--;
				now.tv_usec += 1000000UL;
			}
			now.tv_usec -= then.tv_usec;
			return now.tv_sec * 1000 + now.tv_usec/1000;
		}
	}
	return 0;
}
#endif

Si5326Mode
sis8300ClkDetect(int fd)
{
Si5326Mode rval;
uint32_t   old_0,v1,v2;
#ifdef MEASURE_POLLING
unsigned long dly;
#endif

	/* Reset */
	si5326_wr(fd, 136, 0x80);

#ifdef MEASURE_POLLING
	dly = measure_polling(fd, 1);
	if ( dly ) {
		printf("Have ref after %lums\n", dly);
	}
#else
	/* Test reveals that we need to wait at least 102ms!
	 * until ref-clock is detected.
	 * 
	 */
	us_sleep( 200000 );
#endif

	/* If there is no reference at all then the device is probably not strapped right */
	v1 = si5326_rd(fd, 129);
	if ( (v1 & 1) )
		return Si5326_NoReference;

	/* If we can switch to free-run mode and see a clock on CLKIN2 then we have
     * a proper reference
	 */
	old_0 = si5326_rd(fd, 0);
	si5326_wr(fd, 0, old_0 | 0x40);

#ifdef MEASURE_POLLING
	dly = measure_polling(fd, 4);
	if ( dly ) {
		printf("Have free-run-ref after %lums\n", dly);
	}
#else
	/* Test reveals that we need to wait at least 103ms
	 * until ref-clock is detected via free-run!
	 * 
	 */
	us_sleep( 200000 );
#endif

	v2 = si5326_rd(fd, 129);
	rval = (v2 & 0x4) ? Si5326_WidebandMode : Si5326_NarrowbandMode;

	si5326_wr(fd, 0, old_0);
	us_sleep( 200000 );

	return rval;
}

static int64_t
si5326_setup(int fd, Si5326Parms p)
{
/* Wideband device constraints */
uint64_t fo, fout;
uint32_t f3;
unsigned v;

uint32_t f3min;
uint32_t f3max;
uint64_t fomin;
uint64_t fomax;
unsigned n1hmin;
unsigned n1hmax;
unsigned ncmin;
unsigned ncmax;
unsigned n2hmin;
unsigned n2hmax;
unsigned n2lmin;
unsigned n2lmax;
unsigned n3min;
unsigned n3max;

	if ( p->wb ) {
		f3min=  10000000;
		f3max= 157500000;
		fomin=4850000000ULL;
		fomax=5670000000ULL;
		n1hmin=4;
		n1hmax=11;
		ncmin=1;      /* Nc must be even or 1 */
		ncmax=1<<20;
		n2hmin=1;
		n2hmax=1;
		n2lmin=32;	/* N2 must be even */
		n2lmax=566;   /* in principle 1<<9 but fomax/f3min => 566 */
		n3min=1;
		n3max=1<<19;
	} else {
		f3min=      2000;
		f3max=   2000000;
		fomin=4850000000ULL;
		fomax=5670000000ULL;
		n1hmin=4;
		n1hmax=11;
		ncmin=1;      /* Nc must be even or 1 */
		ncmax=1<<20;
		n2hmin=4;
		n2hmax=11;
		n2lmin=2;	  /* N2 must be even */
		n2lmax=1<<20; 
		n3min=1;
		n3max=1<<19;
	}

	if ( p->nc < ncmin || p->nc > ncmax ) {
		fprintf(stderr,"si5326_setup: NC divider out of range\n");
		return -1;
	}
	if ( p->nc > 1 && (p->nc & 1) ) {
		fprintf(stderr,"si5326_setup: NC divider must be 1 or even\n");
		return -1;
	}
	if ( p->n1h < n1hmin || p->n1h > n1hmax ) {
		fprintf(stderr,"si5326_setup: N1H divider out of range\n");
		return -1;
	}
	if ( p->n2l < n2lmin || p->n2l > n2lmax ) {
		fprintf(stderr,"si5326_setup: N2L divider out of range\n");
		return -1;
	}
	if ( p->n2l & 1 ) {
		fprintf(stderr,"si5326_setup: N2L divider must be even\n");
		return -1;
	}
	if ( p->n2h < n2hmin || p->n2h > n2hmax ) {
		fprintf(stderr,"si5326_setup: N2H divider out of range\n");
		return -1;
	}
	if ( p->n3 < n3min || p->n3 > n3max ) {
		fprintf(stderr,"si5326_setup: N3 divider out of range\n");
		return -1;
	}

	f3 = p->fin/p->n3;
	if ( f3 < f3min || f3 > f3max ) {
		fprintf(stderr,"si5326_setup: F3 (%"PRId32") out of range\n", f3);
		return -1;
	}
	fo = ((uint64_t)f3)*p->n2h*p->n2l;
	if ( fo < fomin || fo > fomax ) {
		fprintf(stderr,"si5326_setup: Fo (%"PRId64") out of range\n", fo);
		return -1;
	}

	/* Reset */
	si5326_wr(fd, 136, 0x80);
	us_sleep( 20000 );

	fout = fo/(p->n1h*p->nc);

	si5326_wr(fd, 2, ((p->bw & 0xf)<<4) | 0x2);

	si5326_wr(fd, 4, 0x92); /* autosel */

	v = p->n1h - n1hmin;
	si5326_wr(fd, 25, v<<5 ); /* N1_HS */

	v = p->nc-1;
	si5326_wr(fd, 31, (v >> 16) & 0xf  );  /* NC1_LS */
	si5326_wr(fd, 32, (v >>  8) & 0xff );
	si5326_wr(fd, 33, (v >>  0) & 0xff );

	si5326_wr(fd, 34, (v >> 16) & 0xf  );  /* NC2_LS */
	si5326_wr(fd, 35, (v >>  8) & 0xff );
	si5326_wr(fd, 36, (v >>  0) & 0xff );

	if ( p->wb ) {
		/* wideband device needs N2 (even) */
		v = 0xc00000 | p->n2l; /* dspllsim put 0xc0 there */
	} else {
		/* narrowband mode needs N2-1 */
		v = ((p->n2h-n2hmin) << 21) | (p->n2l-1);
	}
	si5326_wr(fd, 40, (v >> 16) & 0xff );  /* N2 */
	si5326_wr(fd, 41, (v >>  8) & 0xff );
	si5326_wr(fd, 42, (v >>  0) & 0xff );

	v = p->n3-1;
	si5326_wr(fd, 43, (v >> 16) & 0xff );  /* N31 */
	si5326_wr(fd, 44, (v >>  8) & 0xff );
	si5326_wr(fd, 45, (v >>  0) & 0xff );

	si5326_wr(fd, 46, (v >> 16) & 0xff );  /* N32 */
	si5326_wr(fd, 47, (v >>  8) & 0xff );
	si5326_wr(fd, 48, (v >>  0) & 0xff );

	si5326_wr(fd, 136, 0x40); /* ICAL */

	us_sleep( 500000 );
	
	/* Loss of lock or missing reference ? */
	if ( si5326_rd(fd, 129) & 1 ) {
		fprintf(stderr,"si5326_setup(): ERROR -- missing reference\n");
		return -1;
	}
	if ( si5326_rd(fd, 130) & 1 ) {
		fprintf(stderr,"si5326_setup(): ERROR -- Si5326 won't lock\n");
		return -1;
	}

	return fout;
}

int
sis8300DigiSetup(int fd, Si5326Parms si5326_parms, unsigned clkhl, int exttrig)
{
int i;
uint32_t cmd;
long     fout;
int      rval = 0;
unsigned rat;

	/* cannot bypass when we use 250MHz clock */
	if ( ! si5326_parms && clkhl > 0xffff )
		clkhl = 0; /* use divide-by-two */

	/* Assume single-channel buffer logic */
	if ( (rrd(fd, SIS8300_FIRMWARE_OPTIONS_REG) & 4) ) {
		fprintf(stderr,"ERROR: firmware does not support single-channel mode\n");
		return -1;
	}

	/* MUX A + B: 3 to select on-board quartz     */
    /* MUX C: 2 or 3 to pass A or B out to SI532x */
    /* MUX D/E: 0 - external quartz / 1 - SI532x  */

	/* Layout: 00 00 ee dd 00 cc bb aa            */
    rwr(fd, SIS8300_CLOCK_DISTRIBUTION_MUX_REG, 0x03f | (si5326_parms ? 0x500 : 0));

	if ( si5326_parms ) {
		fout = si5326_setup( fd, si5326_parms );
		if ( fout < 0 ) {
			fprintf(stderr,"Si5326_setup FAILED\n");
			return -1;
		} else {
			fprintf(stderr,"Si5326 clock in use:   %9ldHz\n", fout);
		}
	} else {
		fout = 250000000;
		fprintf(stderr,"On-board clock in use: %9ldHz\n", fout);
	}

	rat = clkhl > 0xff ? 1 : (clkhl & 0xf) + ((clkhl>>4) & 0xf) + 2;

	fprintf(stderr,"AD9510 divider ratio:  %9u\n", rat);
	fprintf(stderr,"Digitizer clock:       %9ldHz\n", fout/rat);

	for ( i=0; i<5; i++ ) {
		adc_setup(fd, i);
	}

	/* 9510 Setup */
	ad9510_setup(fd, 0, clkhl);
	ad9510_setup(fd, 1, clkhl);

	/* 9510 'sync' command as per demo software        */
	rwr(fd, SIS8300_AD9510_SPI_REG,
		AD9510_SPI_SET_FUNCTION_SYNCH_FPGA_CLK69);
	us_sleep(1);
	rwr(fd, SIS8300_AD9510_SPI_REG,
		  AD9510_GENERATE_FUNCTION_PULSE_CMD
		| AD9510_SPI_SET_FUNCTION_SYNCH_FPGA_CLK69);
	us_sleep(1);

	rwr(fd, SIS8300_PRETRIGGER_DELAY_REG, 0);
	/* Enable external trigger; disable all channels */
	cmd = 0x3ff;
	if ( exttrig ) {
		cmd |= 0x800;
		rwr(fd, SIS8300_HARLINK_IN_OUT_CONTROL_REG, 0x100);
	}
	rwr(fd, SIS8300_SAMPLE_CONTROL_REG, cmd);

	rwr(fd, SIS8300_ACQUISITION_CONTROL_STATUS_REG, 4);

	if (     CHTO32('S','t','r','i') == rrd(fd, 0x4fc) 
	     &&  CHTO32('p','B','P','M') == rrd(fd, 0x4fd) ) {
		printf("SLAC AFE Firmware found; enabling RTM Trigger\n");
		rwr(fd, 0x405, 0x10);
	}

	return rval;
}

int
sis8300DigiArm(int fd, int kind)
{
int cmd;
	switch ( kind ) {
		default:
			cmd = SIS8300_READ_MODE_DMACHAIN_OFF;
		break;
		case SIS8300_KIND_BEAM:
			cmd = SIS8300_READ_MODE_DMACHAIN_ARM;
		break;
		case SIS8300_KIND_CRED:
			cmd = SIS8300_READ_MODE_DMACHAIN_CAL_RED;
		break;
		case SIS8300_KIND_CGRN:
			cmd = SIS8300_READ_MODE_DMACHAIN_CAL_GRN;
		break;
	}
	return ioctl(fd, SIS8300_READ_MODE, &cmd);
}

int
sis8300DigiValidateSel(Sis8300ChannelSel sel)
{
int               i,j,n,k;
Sis8300ChannelSel s,t;

	for ( i=0, s=sel; (n = s & 0xf); ) {
		if ( n > 10 ) {
			fprintf(stderr,"channel # %i in selector pos %i too big (1..10)\n", n, i);
			return -1;
		}
		i++;
		s >>= 4;
		for ( j=i, t=s; (k = t & 0xf); ) {
			if ( k == n ) {
				fprintf(stderr,"channel # %i duplicated in selector pos %i\n", n, j);
				return -1;
			}
			j++;
			t >>= 4;
		}
	}
	return 0;
}

/* channel_selector defines the order (and number) of channels in memory.
 * E.g., to have channels 4, 0, 8, 9 in this order in memory set 
 * 'channel_selector' = (9 << 12) | (8 << 8) | (0 << 4) | (4 << 0)
 * 'nsmpl' defines samples per channel!
 */
int
sis8300DigiSetCount(int fd, Sis8300ChannelSel channel_selector, unsigned nsmpl)
{
int      n,ch;
int      nblks;
uint32_t cmd;

	if ( nsmpl & 0xf ) {
		return -1;
	}

	if ( sis8300DigiValidateSel(channel_selector) ) {
		return -1;
	}

	nblks = (nsmpl >> 4);

	rwr(fd, SIS8300_SAMPLE_LENGTH_REG,    nblks - 1);

	cmd  = rrd(fd, SIS8300_SAMPLE_CONTROL_REG);
	cmd |= 0x3ff;
	/* Sample to contiguous memory area */
	for ( n=0; (ch = (channel_selector & 0xf)); n+=nblks, channel_selector >>= 4 ) {
		ch--;
		rwr(fd, SIS8300_SAMPLE_START_ADDRESS_CH1_REG + ch, n);
		cmd &= ~(1<<ch);
	}
	rwr(fd, SIS8300_SAMPLE_CONTROL_REG, cmd);
	return 0;
}

void
sis8300DigiSetSim(int fd, int32_t a, int32_t b, int32_t c, int32_t d, int quiet)
{
Ampl ampl;
	/* FIXME: should permute according to channelSel? */
printf("Setting SIM: %5"PRId32" %5"PRId32" %5"PRId32" %5"PRId32"\n", a,b,c,d);
	ampl[0] = a;
	ampl[1] = b;
	ampl[2] = c;
	ampl[3] = d;
	ioctl( fd, _IOW('s', 0x11, Ampl), &ampl );
	if ( ! quiet ) {
		/* could soft-trigger here but this would not be thread-safe since
		 * the assumption is that the drvPadUdpCommListener is the only
		 * thread dealing with 'fd' (certain ioctls() such as setting
         * the simulation amplitudes and reading/writing registers are
		 * safe; however, setting the read_mode and reading itself are
		 * not).
		 */
	}
}

int 
sis8300DigiQspiWriteRead(const void *device, int data_out, uint16_t *data_in)
{
sis8300_reg r;
struct timespec req, rem;
int    fd = (intptr_t)device;

	r.offset = SIS8300_QSPI_REG;

	if ( data_out >= 0 ) {
		r.data = data_out;
		if ( ioctl(fd, SIS8300_REG_WRITE, &r) ) {
			return -1;
		}

		/* must wait after write - there is no way for us to know if the transfer
		 * is complete. Transferring 32 bits @30MB/s = 1.07 us...
		 */
		req.tv_sec  = 0;
		req.tv_nsec = 2000;
		while (  nanosleep( &req, &rem ) && (EINTR == errno) ) {
			req = rem;
		}
	}

	if ( data_in ) {
		if ( ioctl(fd, SIS8300_REG_READ, &r) ) {
			return -1;
		}
		*data_in = r.data;
	}

	return 0;
}
