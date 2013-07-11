#include <inttypes.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <getopt.h>
#include <stdlib.h>
#include <unistd.h>

#include <sis8300Digi.h>

static void usage(const char *nm)
{
	fprintf(stderr,"Usage: %s [-d device] [-S] [-b] [-B] [-N nblks] [-4]\n", nm);
	fprintf(stderr,"           -d device  : use 'device' (path to dev-node)\n");
	fprintf(stderr,"           -S         : set muxes to use si5326 clock\n");
	fprintf(stderr,"           -b         : do not bypass 9510 dividers (only if -S in wide-band mode)\n");
	fprintf(stderr,"           -B         : enforce bypass of 9510 dividers\n");
	fprintf(stderr,"           -e         : disable external trigger (enabled by default)\n");
	fprintf(stderr,"           -N nblks   : number of sample blocks (16samples) per channel\n");
	fprintf(stderr,"                        - defaults to 2.\n");
	fprintf(stderr,"           -4         : use channels 2,4,6,8 only\n");
	fprintf(stderr,"           -f freq    : program Si5326 for output frequency 'freq'\n");
	fprintf(stderr,"                        (implies -S)\n");
}

typedef struct {
	unsigned long    fout;
	Si5326ParmsRec   parms;
} Si5326ConfigRec, *Si5326Config;

static Si5326ConfigRec si5326Configs_wb[] = {
	{ /* 218MHz wideband */
	fout: 218000000UL,
	parms:
	{
	  fin : 250000000UL,
	  n3  : 10,
	  /* f3 = 25Mhz */
	  n2h : 1,
	  n2l : 109*2,
	  /* fo = 50*109 MHz */
	  n1h : 5,
	  nc  : 10,
	  bw  : 1, /* dspllsim gave us this; no other info available :-( */
	  wb  : 1,
	}
	},

	{ /* Use 500MHz wideband */
	fout: 500000000UL,
	parms:
	{
	  fin : 250000000UL,
	  n3  : 2,
	  /* f3 = 125Mhz */
	  n2h : 1,
	  n2l : 44,
	  /* fo = 44*125 MHz */
	  n1h : 11,
	  nc  : 1,
	  bw  : 2, /* dspllsim gave us this; no other info available :-( */
	  wb  : 1,
	}
	},

	{ /* END marker */
	fout: 0,
	parms:
	{
	}
	}
};

static Si5326ConfigRec si5326Configs_nb[] = {
	{ /* 109MHz narrow-band */
	fout: 109000000UL,
	parms:
	{
	  fin : 250000000UL,
	  n3  : 125,
	  /* f3 = 2Mhz */
	  n2h : 4,
	  n2l : 654,
	  /* fo = 2*109 MHz */
	  n1h : 6,
	  nc  : 4,
	  bw  : 5, /* dspllsim gave us this; no other info available :-( */
	  wb  : 0,
	}
	},

	{ /* END marker */
	fout: 0,
	parms:
	{
	}
	}
};

int
main(int argc, char **argv)
{
int      rval = 1;
int      fd = -1;
int      nblks = 2;
int      exttrig = 1;
int      enf_byp = 0;
unsigned div_clkhl = SIS8300_BYPASS_9510_DIVIDER;
int      opt;
const char *dev = getenv("RACC_DEV");
int      *i_p;
unsigned long *ul_p;
unsigned long freq = 0;
Si5326Parms        si5326_clk = 0;
Si5326Config       si5326_cfg = 0;
Sis8300ChannelSel  sel = 0xa987654321ULL;
Si5326Mode         mode;

	while ( (opt = getopt(argc, argv, "hSbBed:N:4f:")) > 0 ) {
		i_p  = 0;
		ul_p = 0;
		switch ( opt ) {
			default:
			case 'h': usage(argv[0]); return 0;

			case 'S': freq = 109000000UL; break;
			case 'b': enf_byp    = -1; break;
			case 'B': enf_byp    =  1; break;
	
			case 'd': dev        = optarg; break;

			case 'e': exttrig    = 0; break;

			case 'N': i_p        = &nblks; break;
	
			case '4': sel = 0x8642ULL; break;

			case 'f': ul_p = &freq; break;
		}

		if ( i_p ) {
			if ( 1 != sscanf(optarg,"%i",i_p) ) {
				fprintf(stderr,"Option '%c' needs integer argument!\n", opt);
				return 1;
			}
		}
		if ( ul_p ) {
			if ( 1 != sscanf(optarg,"%li",ul_p) ) {
				fprintf(stderr,"Option '%c' needs long integer argument!\n", opt);
				return 1;
			}
		}
	}

	if ( !freq )
		div_clkhl = 0;

	switch ( enf_byp ) {
		default:
		break;
		case -1: div_clkhl = 0; break;
		case +1: div_clkhl = SIS8300_BYPASS_9510_DIVIDER; break;
	}

	if ( ! dev ) {
		fprintf(stderr,"No device - use '-d <device>' or set RACC_DEV env_var\n");
		return 1;
	}

	if ( (fd = open(dev, O_RDWR)) < 0 ) {
		perror("opening device");
		return 1;
	}

	if ( freq > 0 ) {
		switch ( (mode = sis8300ClkDetect( fd )) ) {
			default:
				fprintf(stderr,"Sis8300ClkDetect - unknown result %i\n", mode);
			return 1;
			
			case Si5326_NoReference:
				fprintf(stdout,"Si5326 - no reference detected\n");
			return 1;

			case Si5326_NarrowbandMode:
				fprintf(stdout,"Si5326 - operating in narrow-band mode\n");
				si5326_cfg = si5326Configs_nb;
			break;
			case Si5326_WidebandMode:
				fprintf(stdout,"Si5326 - operating in wide-band mode\n");
				si5326_cfg = si5326Configs_wb;
			break;
		}

		for ( ; si5326_cfg->fout > 0; si5326_cfg++ ) {
			if ( freq == si5326_cfg->fout ) {
				div_clkhl = SIS8300_BYPASS_9510_DIVIDER;
				break;
			} else if ( 2*freq == si5326_cfg->fout ) {
				div_clkhl = 0;
				break;
			}
		}

		if ( 0 == si5326_cfg->fout ) {
			fprintf(stderr, "Sorry, no configuration for output frequency %luHz found\n", freq);
			return 1;
		}
	
		si5326_clk = &si5326_cfg->parms;
	}

	if ( sis8300DigiSetup( fd, si5326_clk, div_clkhl, exttrig ) ) {
		goto bail;
	}

	if ( sis8300DigiSetCount(fd, sel, nblks * 16) ) {
		fprintf(stderr,"Setting sample count failed\n");
	}

	rval = 0;
bail:
	close( fd );
	return rval;
}
