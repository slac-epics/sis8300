#define _ISOC99_SOURCE
#include <math.h>
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

#include <ratapp.h>
#include <stdlib.h>

typedef struct Si53xxLim_ {
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
	unsigned bwmin;
	unsigned bwmax;
	int      bwselmin;
	int      bwselmax;
	unsigned (*fbw)(struct Si53xxLim_ *l, uint32_t f3, unsigned n2, int   bwsel);
	int      (*bws)(struct Si53xxLim_ *l, uint32_t f3, unsigned n2, unsigned bw);
} Si53xxLim;

static unsigned
fbw_wb(Si53xxLim *l, uint32_t f3, unsigned n2, int bwsel);
static unsigned
fbw_nb(Si53xxLim *l, uint32_t f3, unsigned n2, int bwsel);

static int
bws_wb(struct Si53xxLim_ *l, uint32_t f3, unsigned n2, unsigned bw);
static int
bws_nb(struct Si53xxLim_ *l, uint32_t f3, unsigned n2, unsigned bw);

static
Si53xxLim si5325_lim = {
		f3min:  10000000,
		f3max: 157500000,
		fomin:4850000000ULL,
		fomax:5670000000ULL,
		n1hmin:4,
		n1hmax:11,
		ncmin:1,    /* Nc must be even or 1 */
		ncmax:1<<20,
		n2hmin:1,
		n2hmax:1,
		n2lmin:32,	/* N2 must be even */
		n2lmax:512,
		n3min:1,
		n3max:1<<19,
		bwmin: 150000,
		bwmax:1300000,
		bwselmin:0, /* bwselmin/bwselmax: private communication from SiLabs */
		bwselmax:2,
		fbw : fbw_wb,
		bws : bws_wb,
};

static
Si53xxLim si5326_lim = {
		f3min:      2000,
		f3max:   2000000,
		fomin:4850000000ULL,
		fomax:5670000000ULL,
		n1hmin:4,
		n1hmax:11,
		ncmin:1,      /* Nc must be even or 1 */
		ncmax:1<<20,
		n2hmin:4,
		n2hmax:11,
		n2lmin:2,	  /* N2 must be even */
		n2lmax:1<<20, 
		n3min:1,
		n3max:1<<19,
		bwmin:     60,
		bwmax:   8400,
		bwselmin:1, /* bwselmin/bwselmax: private communication from SiLabs */
		bwselmax:10,
		fbw : fbw_nb,
		bws : bws_nb,
};

typedef int32_t Ampl_t;
typedef Ampl_t  Ampl[4];

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
adc_drain(int fd)
{
int rep=100;
	do { 
		us_sleep(10);
	} while ( (rrd(fd, SIS8300_ADC_SPI_REG) & 0x8000000) && --rep > 0 );
}

static void
adc_wr(int fd, unsigned inst, unsigned a, unsigned v)
{
uint32_t cmd;

	if ( inst > 4 )
		return;

	cmd  = inst << 24;

	cmd |= ((a&0xff)<<8) | (v&0xff);

	rwr(fd, SIS8300_ADC_SPI_REG, cmd);
	
	adc_drain(fd);
}

#define CMD_ADC_SPI_READ (1<<23)

static int
adc_rd(int fd, unsigned inst, unsigned a)
{
uint32_t cmd;

	if ( inst > 4 )
		return -1;

	cmd = inst << 24;
	cmd |= ( (a&0xff)<<8 ) | CMD_ADC_SPI_READ;

	rwr(fd, SIS8300_ADC_SPI_REG, cmd);
	adc_drain(fd);
	return (int) (rrd(fd, SIS8300_ADC_SPI_REG) & 0xff);
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

static int is_8_channel_firmware(int fd)
{
	/* firmware 0x2402 and up are 8-channel @250msps */
	return (rrd( fd, SIS8300_IDENTIFIER_VERSION_REG) & 0xff00) >= 0x2400;
}

	/* If we have a 14-bit digitizer with firmware >= 2402 then we can adjust the 14 bits
     * so that the digitizer produces numbers on the same scale as its 16-bit counterpart.
	 */
static void shift_adc_bits(int fd) 
{
	if ( (rrd( fd, SIS8300_IDENTIFIER_VERSION_REG) & 0xffff) < 0x2402 )
		return;

	/* Get ADC chip ID 0x82 : AD9643; 0x32: AD9268 */
	if ( adc_rd( fd, 0, 0x01 ) != 0x82 )
		return;

	/* OK we have the right firmware and a device which deserves shifting... */
	uint32_t v = rrd(fd, SIS8300_USER_CONTROL_STATUS_REG);
	v &= ~0x30;
	v |=  0x10; /* MODE 1 - left-adjust 14-bits into 16-bit word */
	rwr(fd, SIS8300_USER_CONTROL_STATUS_REG, v);
}

/* Setup of ADC */

static void
adc_setup(int fd, unsigned inst)
{
	/* output type LVDS; two-s complement*/
	adc_wr( fd, inst, 0x14, 0x41 );
	adc_wr( fd, inst, 0x16, 0x00 );
	adc_wr( fd, inst, 0x17, 0x00 );

	/* VREF for 1.25Vpp input sensitivity */
	adc_wr( fd, inst, 0x18, 0x00 );

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

static Si53xxLim *
si53xx_getLims(int wb)
{
	return wb ? &si5325_lim : &si5326_lim;
}


/* Private communication from SiLabs; many thanks! */
static unsigned
fbw_nb(Si53xxLim *l, uint32_t f3, unsigned n2, int bwsel)
{
double sel,v;
	if ( bwsel < l->bwselmin || bwsel > l->bwselmax )
		return 0;
	sel = (double)(1<<bwsel);
	v = (double)f3/16.84/sel/sqrt((1.0-1.0/3.35/sel)*(1.0-4276.0/(double)n2/sel));
	return (unsigned)v;
}

static unsigned
fbw_wb(Si53xxLim *l, uint32_t f3, unsigned n2, int bwsel)
{
double sel;
double F;
double v;
	if ( bwsel < l->bwselmin || bwsel > l->bwselmax )
		return 0;
	sel = (double)(bwsel+1);
	F = 6.5E9/(double)f3/(double)n2;
	v = (double)f3*1.235/101.235/sel/sqrt(1.0-0.095/sel)*F*F;
	return (unsigned) v;
}

static int
bws_nb(struct Si53xxLim_ *l, uint32_t f3, unsigned n2, unsigned bw)
{
double d,A,B,C,p,s;
int    bwsel;
/*
	a=f3/16.84, B=1/3.35, C=4276/N2, p=2^(-BWSEL)

	bw=a*p/sqrt{(1-B*p)(1-C*p)}

	A = a/bw = f3/bw/16.84
    
    (A*p)^2 = 1-(B+C)p+B*C*p^2 

    p^2 (B*C-A^2) -(B+C) p + 1 == 0

    p = (B+C +/- sqrt((B+C)^2-4 (B*C-A^2) ) / 2 / (B*C-A^2)

	roots of A^2 p^2 - BC p^2 + (B+C)p - 1 = 0

    Root locus plot as a function of A^2


    'Open-loop': BC p^2 - (B+C)p + 1 = 0 
        p1/2 = { (B+C)+/-sqrt((B+C)^2-4BC) } /2/BC =>
		p1 = 1/B, b2=1/C	


     if C > B (4276/n2 > 1/3.35)
                |               
                |              
                |                  3.35                  
     -----------O<======X----------X========>
                       1/C        1/B

     If C < B
                |               
                |              
                |      3.35        
     -----------O<======X----------X========>
                       1/B        1/C

	 In any case: only ONE solution can be smaller than 1

 */
	if ( bw < l->bwmin )
		bw = l->bwmin;
	if ( bw > l->bwmax )
		bw = l->bwmax;
	A=(double)f3/(double)bw/16.84;
	B=1.0/3.35;
	C=4276.0/(double)n2;
	d=B*C-A*A;
	if ( 0.0 == d ) {
		p = 1.0/(B+C);
	} else {
		/* (B+C)^2-4*B*C + 4*A^2 = (B-C)^2 + 4 A^2 > 0 */
		s = B+C;
		s = sqrt(s*s - 4.0*d);
		if ( d < 0 ) {
			/* sqrt() > B+D; positive p can only be achieved
			 * for the negative sign.
			 */
			p = (B+C-s)/2./d;
		} else {
			/* we pick smaller solution */
			p = (B+C-s)/2./d;
		}
	}
	bwsel = -(int)round(log(p)/log(2.0));

	if ( bwsel < l->bwselmin )
		bwsel = l->bwselmin;
	if ( bwsel > l->bwselmax )
		bwsel = l->bwselmax;
	
	while ( l->fbw(l, f3, n2, bwsel) < l->bwmin ) {
		if ( --bwsel < l->bwselmin )
			return -1;
	}

	while ( l->fbw(l, f3, n2, bwsel) > l->bwmax ) {
		if ( ++bwsel > l->bwselmax )
			return -1;
	}

	return bwsel;
}

static int
bws_wb(struct Si53xxLim_ *l, uint32_t f3, unsigned n2, unsigned bw)
{
double A,B,p;
int    bwsel;
/*
	a = f3*1.235/101.235)*(6.5E9/f3/N2)^2
    A = a/bw
	B = 0.095
    p = bwsel + 1

    bw = a/p/sqrt(1-B/p) 

	(A/p)^2 = 1 - B/p 

	A^2 + Bp - p^2 = 0

    { -B +/- sqrt(B^2+4 A^2) } /2/(-1)

    1/2 {B-/+sqrt(B^2+4*A^2)}

    for p >=1 take + sign
 */
	if ( bw < l->bwmin )
		bw = l->bwmin;
	if ( bw > l->bwmax )
		bw = l->bwmax;

	A=6.5E9*(double)f3/(double)n2;
	A=(double)f3*1.235/101.235*A*A/(double)bw;
	B=0.095;
	p=0.5*(B+sqrt(B*B+4.0*A*A));
	bwsel = (int)round(p) - 1;

	if ( bwsel < l->bwselmin )
		bwsel = l->bwselmin;
	if ( bwsel > l->bwselmax )
		bwsel = l->bwselmax;
	
	while ( l->fbw(l, f3, n2, bwsel) < l->bwmin ) {
		if ( --bwsel < l->bwselmin )
			return -1;
	}

	while ( l->fbw(l, f3, n2, bwsel) > l->bwmax ) {
		if ( ++bwsel > l->bwselmax )
			return -1;
	}

	return bwsel;
}


static unsigned
fbw(Si53xxLim *l, Si5326Parms p)
{
	return l->fbw(l, p->fin/p->n3, p->n2h*p->n2l, p->bwsel);
}

static int
si5326_checkParms(const char *pre, Si5326Parms p, Si53xxLim *l)
{
uint64_t fo;
uint32_t f3;
unsigned bw;

#if 0 /* test code */
int i;
	for ( i=l->bwselmin; i<l->bwselmax; i++ ) {
		bw = l->fbw(l, p->fin/p->n3, p->n2h*p->n2l, i);
		printf("FBW(%i) = %u, BWS(%u) = %i\n", i, bw, bw, l->bws(l, p->fin/p->n3, p->n2h*p->n2l, bw));
	}
#endif

	if ( p->nc < l->ncmin || p->nc > l->ncmax ) {
		fprintf(stderr,"%s: NC divider out of range\n", pre);
		return -1;
	}
	if ( p->nc > 1 && (p->nc & 1) ) {
		fprintf(stderr,"%s: NC divider (%u) must be 1 or even\n", pre, p->nc);
		return -1;
	}
	if ( p->n1h < l->n1hmin || p->n1h > l->n1hmax ) {
		fprintf(stderr,"%s: N1H divider (%u) out of range\n", pre, p->n1h);
		return -1;
	}
	if ( p->n2l < l->n2lmin || p->n2l > l->n2lmax ) {
		fprintf(stderr,"%s: N2L divider (%u) out of range\n", pre, p->n2l);
		return -1;
	}
	if ( p->n2l & 1 ) {
		fprintf(stderr,"%s: N2L divider must be even\n", pre);
		return -1;
	}
	if ( p->n2h < l->n2hmin || p->n2h > l->n2hmax ) {
		fprintf(stderr,"%s: N2H divider (%u) out of range\n", pre, p->n2h);
		return -1;
	}
	if ( p->n3 < l->n3min || p->n3 > l->n3max ) {
		fprintf(stderr,"%s: N3 divider (%u) out of range\n", pre, p->n3);
		return -1;
	}

	f3 = p->fin/p->n3;
	if ( f3 < l->f3min || f3 > l->f3max ) {
		fprintf(stderr,"%s: F3 (%"PRId32") out of range\n", pre, f3);
		return -1;
	}
	fo = ((uint64_t)f3)*p->n2h*p->n2l;
	if ( fo < l->fomin || fo > l->fomax ) {
		fprintf(stderr,"%s: Fo (%"PRId64") out of range\n", pre, fo);
		return -1;
	}

	if ( p->bwsel < l->bwselmin || p->bwsel > l->bwselmax ) {
		fprintf(stderr,"%s: BWSEL (%i) out of range\n", pre, p->bwsel);
		return -1;
	}

	bw = fbw(l,p);

	if ( bw < l->bwmin || bw > l->bwmax ) {
		fprintf(stderr,"%s: PLL bandwidth (%d) out of range\n", pre, bw);
		return -1;
	}

	return 0;
}

/* Brute-force factorize a number picking the highest divisor
 * between div_min and div_max which divides n.
 *
 * RETURNS: divisor or 0 if none could be found.
 */
static unsigned
brutefac(unsigned n, unsigned div_min, unsigned div_max)
{
	while ( div_max >= div_min ) {
		if ( 0 == n % div_max )
			return div_max;
		div_max--;
	}
	return 0;
}

int
si53xx_calcParms(uint64_t fout, Si5326Parms p, int verbose)
{
Si53xxLim   *l;
unsigned    n1min, n1max, n1, n1h, n2h, n2l, nc, n3min, v2, v3;
Rational    r, ro, r_max, r_arg;
double      eps = 1.0/0.0, e;
Convergent *c = 0;
int         n_c, k;
RatNum      im_i;

	l = si53xx_getLims( p->wb );

	/* Find acceptable range of n1 */
	n1min = l->fomin / fout;
	if ( n1min * fout < l->fomin )
		n1min += 1;
	n1max = l->fomax / fout;

	if ( n1min < l->n1hmin * l->ncmin )
		n1min = l->n1hmin * l->ncmin;

	/* Probably not necessary */
	if ( n1max > l->n1hmax * l->ncmax )
		n1max = l->n1hmax * l->ncmax;

	r_max.d = p->fin/l->f3min;
	if ( r_max.d > l->n3max )
		r_max.d = l->n3max; 

	r_max.n = l->n2hmax*l->n2lmax/2;
	r_arg.d = p->fin;

	ro.d = ro.n = 0;
	p->nc = 0;

	if ( n1min <= l->n1hmax ) {
		fprintf(stderr,"si53xx_calcParms -- NOTE: case of odd N1 not implemented\n");
	}

	/* Enforce even-ness of n1 (needs to be even if nc > 1 anyways)
	 * This way we can easily enforce even-ness of N2. It is unlikely
	 * to have to handle odd n1 (could happen only for fo/fout <= 11).
	 */
	n1min = (n1min + 1) & ~1;

	if ( (n_c = ratapp_estimate_terms( 0, &r_max )) < 0 ) {
		fprintf(stderr,"si53xx_calcParms -- ratapp_estimate_terms failed\n");
		return -1;
	}

	if ( ! (c = malloc( sizeof(*c) * n_c )) ) {
		fprintf(stderr,"si53xx_calc_parms -- no memory\n");
		return -1;
	}

	/* N2 must be even; compute N2_ = N2/2; we know that n1 has to be even, too
	 * (at least as soon as n1 > n1hmax). Hence we perform all the computations
	 * for n1/2.
	 */
	for ( n1 = n1min/2; n1<=n1max/2; n1++ ) {

		/* Try to find a factorization */
		n1h = brutefac( n1, l->n1hmin, l->n1hmax );
		if ( 0 == n1h )
			continue;

		nc = n1/n1h;

		r_arg.n = n1 * fout;

		/* Continued fraction expansion of n1 * fout / fin */
		k = ratapp_find_convergents(c, n_c, &r_arg, &r_max);
		if ( k < 0 && k>= n_c ) {
			fprintf(stderr,"ratapp_find_convergents failed (return value %i, n_c %i)\n", k, n_c);
			free( c );
			return -1;
		}
		/* Find next best approximation */
		while ( --k >= 0 ) {
			/* Iterate over intermediates until finding an acceptable one */
			im_i = c[k+1].a;
			do {
				im_i--;
				im_i = ratapp_intermediate( &r, im_i, &c[k+1], &c[k], &r_arg );
				/* Check if this one's better... */
				e = fabs( (double)p->fin * (double)r.n / (double)r.d / (double)n1 - (double)fout );
				if ( verbose )
					printf("Checking n1h %u, nc %u, n1 %u, r.n %"PRIu64", r.d %"PRIu64", eps %g", n1h, nc, n1, r.n, r.d, e);
				if ( e <= eps ) {
					/* If as good pick the higher n1h but only if N2 can be factorized into legal values  */
					if (    (e < eps || n1h > p->n1h)
							&& (n2h = brutefac( r.n, l->n2hmin, l->n2hmax ))
							&& (n2l = r.n/n2h*2) <= l->n2lmax ) {
						if ( verbose )
							printf("  ==> Accepted");
						ro  = r;
						eps = e;
						p->n1h = n1h;
						p->nc  = 2*nc;
						p->n2h = n2h;

						/* done */
						k      = 0;
						im_i   = 0;
					}
				} else {
					/* end this effort */
					k    = 0;
					im_i = 0;
				}
				if ( verbose )
					printf("\n");
			} while ( im_i > 0 );
		}
	}

	free( c );

	if ( p->nc == 0 ) {
		/* No allowable N1 found */
		return -1;
	}
	p->n3  = ro.d;
	p->n2l = (ro.n/p->n2h)*2;
	if ( verbose )
		printf("Setting N3: %u, n2h %u, n2l %u\n", p->n3, p->n2h, p->n2l);

	/* If f3 is too high or n3 or n2 too small then multiply n3 and n2 by common factor */
	n3min = (p->fin + l->f3max - 1) / l->f3max;
	if ( l->n3min > n3min )
		n3min = l->n3min;
	/* n3min .. n3 .. r_max.d; n2min .. n2 .. r_max.n */	
	v3 = (n3min + p->n3 - 1)/p->n3;
	v2 = (l->n2lmin + p->n2l - 1) / p->n2l;
	if ( v2 > v3 )
		v3 = v2;
	if ( v3 > 1 ) {
		/* multiply n3, n2 by the next bigger even number (n2 must be even) */
		v3 = (v3+1)&~1;
		if ( verbose )
			printf("Readjusting by v3 %u\n", v3);
		p->n3  *= v3;
		p->n2l *= v3;
	}

	/* Compute bandwidth selector from user input */
	p->bwsel = l->bws( l, p->fin/p->n3, p->n2h*p->n2l, p->bw );

	if ( p->bwsel < 0 ) {
		fprintf(stderr,"Unable to find valid PLL bandwidth setting\n");
		return -1;
	}

	/* Adjust p->bw to reflect true bandwidth */
	p->bw = fbw( l, p );

	return si5326_checkParms("si53xx_calcParms", p, l);
}
    
int64_t
si5326_setup(int fd, Si5326Parms p)
{
uint64_t fo, fout;
uint32_t f3;
unsigned v;
Si53xxLim *l;
int      retries;

	l = si53xx_getLims( p->wb );

	if ( si5326_checkParms( "si5326_setup", p, l ) )
		return -1;

	/* Compute bandwidth and store for informational purposes */
	p->bw = fbw(l, p);

	f3 = p->fin/p->n3;
	fo = ((uint64_t)f3)*p->n2h*p->n2l;

	/* Reset */
	si5326_wr(fd, 136, 0x80);
	us_sleep( 20000 );

	fout = fo/(p->n1h*p->nc);

	si5326_wr(fd, 2, ((p->bwsel & 0xf)<<4) | 0x2);

	si5326_wr(fd, 4, 0x92); /* autosel */

	v = p->n1h - l->n1hmin;
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
		v = ((p->n2h-l->n2hmin) << 21) | (p->n2l-1);
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
	retries = 0;
	while ( (si5326_rd(fd, 130) & 1) ) {
		if ( 10 < retries ) {
			fprintf(stderr,"si5326_setup(): ERROR -- Si5326 won't lock\n");
			return -1;
		}
		retries++;
		fprintf(stderr,"... still waiting for Si5326 to lock\n");
		us_sleep( 500000 );
	}
	if ( retries ) {
		fprintf(stderr,"OK, it did lock.\n");
	}

	return fout;
}

static unsigned
sis8300_tap_delay(unsigned long adc_clk)
{
	/* SIS8300-M-2403-1-V101-250MSPS_addendum:
     * Tap delay 0  should work for clocks 40Mhz..160MHz.
	 * Tap delay 11 for 40MHz..250MHz. 
     * Maintain the switchover so that we dont inadvertently 
	 * mess something up that has been proven to work with the
	 * 125MSPS digi...
	 */
	return adc_clk > 130000000UL ? 11 : 0;
}

/* Mask selecting all ADC pairs */
#define SIS8300_TAP_DELAY_ALL_ADCS  0x1f00
#define SIS8300_TAP_DELAY_8_ADCS    0x0f00
#define SIS8300_TAP_DELAY_BUSY     (1<<31)

int
sis8300DigiSetup(int fd, Si5326Parms si5326_parms, unsigned clkhl, int exttrig)
{
int i;
uint32_t cmd;
long     fout;
unsigned long fclk, fmax;
int      rval = 0;
unsigned rat;
int      is_8_ch_fw = is_8_channel_firmware( fd );

	/* Assume single-channel buffer logic */
	if ( (rrd(fd, SIS8300_FIRMWARE_OPTIONS_REG) & SIS8300_DUAL_CHANNEL_SAMPLING) ) {
		fprintf(stderr,"ERROR: firmware does not support single-channel mode\n");
		return -1;
	}

	/* Init. to high divider ratio so that fclk doesn't become too high */
	ad9510_setup( fd, 0, 0xff );
	ad9510_setup( fd, 1, 0xff );
	/* Set to internal clock */
    rwr(fd, SIS8300_CLOCK_DISTRIBUTION_MUX_REG, 0x03f);

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

	fclk = fout/rat;

	fprintf(stderr,"AD9510 divider ratio:  %9u\n", rat);
	fprintf(stderr,"Digitizer clock:       %9ldHz\n", fclk);

	if ( 0 == (fmax = sis8300DigiGetFclkMax(fd)) ) {
		fprintf(stderr,"Unable to determine max. digitizer clock frequency!\n");
		return -1;
	} else {
		fprintf(stderr,"Max. digitizer clock:  %9luHz\n", fmax);
	}

	if ( fclk > fmax ) {
		fprintf(stderr,"Selected clock frequency too high!\n");
		return -1;
	}

	cmd = is_8_ch_fw ? SIS8300_TAP_DELAY_8_ADCS : SIS8300_TAP_DELAY_ALL_ADCS;
	cmd |= sis8300_tap_delay( fclk );

	/* Set infamous ADC tap delay */
	rwr(fd, SIS8300_ADC_INPUT_TAP_DELAY_REG, cmd);
	/* Busy-wait */
	for ( i=0; i<10000; i++ ) {
		if ( ! ( rrd(fd, SIS8300_ADC_INPUT_TAP_DELAY_REG) & SIS8300_TAP_DELAY_BUSY ) ) {
			break;
		}
	}

	for ( i=0; i < ( is_8_ch_fw ? 4 : 5 ); i++ ) {
		adc_setup(fd, i);
	}

	shift_adc_bits( fd );

	/* MUX A + B: 3 to select on-board quartz     */
    /* MUX C: 2 or 3 to pass A or B out to SI532x */
    /* MUX D/E: 0 - external quartz / 1 - SI532x  */

	/* Layout: 00 00 ee dd 00 cc bb aa            */
    rwr(fd, SIS8300_CLOCK_DISTRIBUTION_MUX_REG, 0x03f | (si5326_parms ? 0x500 : 0));

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

Sis8300ChannelSel
sis8300BuildChannelSel(unsigned start, unsigned end)
{
Sis8300ChannelSel rval = 0;

	start &= 0xf;
	end   &= 0xf;

	if ( start > 0 ) {
		while ( start <= end ) {
			rval = (rval << 4) | end;
			end--;
		}
	}
	return rval;
}

int
sis8300DigiValidateSel(Sis8300ChannelSel sel)
{
int               i,j,n,k;
Sis8300ChannelSel s,t;

	for ( i=0, s=sel; 0 != (n = ( s & 0xf ) ); ) {
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
	cmd |= 0x0ff;

	if ( is_8_channel_firmware(fd) ) {
		cmd |= 0x300;
	} else {
		cmd &= ~0x300;
	}
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

int
sis8300DigiReadReg(int fd, unsigned reg, uint32_t *val_p)
{
sis8300_reg r;
	r.offset = reg;
	if ( ioctl(fd, SIS8300_REG_READ, &r) )
		return -1;
	*val_p = r.data;	
	return 0;
}

int
sis8300DigiWriteReg(int fd, unsigned reg, uint32_t val)
{
sis8300_reg r;
	r.offset = reg;
	r.data   = val;
	return ioctl(fd, SIS8300_REG_WRITE, &r);
}

int
sis8300DigiGetADC_ID(int fd)
{
	return adc_rd(fd, 0, 0x01);
}

unsigned long
sis8300DigiGetFclkMax(int fd)
{
int chip_id = adc_rd(fd, 0, 0x01);
int grade   = adc_rd(fd, 0, 0x02);
/*
	fprintf(stderr, "ADC CHIP ID: 0x%02x, grade 0x%02x\n", chip_id, grade);
 */
	if ( chip_id < 0 || grade < 0 )
		return 0;
	grade = (grade>>4) & 3;
	switch ( chip_id ) {
		default: break;

		case 0x32: /* AD9268 */
			switch ( grade ) {
				default: break;
				case 1: return 125000000UL;
				case 2: return 105000000UL;
				case 3: return  80000000UL;
			}
		break;

		case 0x82: /* AD9643 */
			switch ( grade ) {
				default: break;
				case 0: return 250000000UL;
				case 1: return 210000000UL;
				case 3: return 170000000UL;
			}
		break;	
	}

	return 0;
}
