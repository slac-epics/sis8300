#include <inttypes.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <getopt.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>

#include <sis8300Digi.h>

static void usage(const char *nm)
{
	fprintf(stderr,"Usage: %s [-d device] [-f freq] [-L loop_bandwidth] [-qh] [-S] [-b] [-B] [-N nblks] [-4] [-T W|N] [-C] <config>\n\n", nm);
	fprintf(stderr,"           -h         : print this message\n");
	fprintf(stderr,"           -q         : query Si5236 operating mode only\n");
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
	fprintf(stderr,"           -T W|N     : only compute divider settings w/o accessing the device.\n");
	fprintf(stderr,"                        Requires '-f'. The user must specify the device mode\n");
	fprintf(stderr,"                        ('W'ide- or 'N'arrow-band).\n");
	fprintf(stderr,"           -L bw      : Set PLL loop bandwidth\n");
	fprintf(stderr,"           -C         : read config parameters <n3> <n2h> <n2l> <n1h> <nc> <bwsel>\n");
	fprintf(stderr,"           -I         : ignore fixed, hard-configured configurations\n");
	fprintf(stderr,"           -v         : be verbose\n");
}

typedef struct {
	unsigned long    fout;
	Si5326ParmsRec   parms;
} Si5326ConfigRec, *Si5326Config;

static Si5326ConfigRec si5326Configs_none[] = {
	{
	fout: 0,
	parms: {}
	}
};

static Si5326ConfigRec si5326Configs_wb[] = {
	{ /* 109MHz wideband */
	fout: 109000000UL,
	parms:
	{
	  fin  : 250000000UL,
	  n3   : 10,
	  /* f3 = 25Mhz */
	  n2h  : 1,
	  n2l  : 109*2,
	  /* fo = 50*109 MHz */
	  n1h  : 5,
	  nc   : 10,
	  bwsel: 1,
	  wb   : 1,
	}
	},

	{ /* Use 500MHz wideband */
	fout: 500000000UL,
	parms:
	{
	  fin  : 250000000UL,
	  n3   : 2,
	  /* f3 = 125Mhz */
	  n2h  : 1,
	  n2l  : 44,
	  /* fo = 44*125 MHz */
	  n1h  : 11,
	  nc   : 1,
	  bwsel: 2,
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
	  fin  : 250000000UL,
	  n3   : 125,
	  /* f3 = 2Mhz */
	  n2h  : 4,
	  n2l  : 654,
	  /* fo = 2*109 MHz */
	  n1h  : 6,
	  nc   : 4,
	  bwsel: 5,
	  wb   : 0,
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
int      query = 0;
int      do_config = 0;
int      ignore_fixed = 0;
int      i;
int      verbose   = 0;
unsigned div_clkhl = SIS8300_BYPASS_9510_DIVIDER;
int      opt;
const char *dev = getenv("RACC_DEV");
int      *i_p;
unsigned long *ul_p;
unsigned long freq = 0;
int      bw=0;
Si5326Parms        si5326_clk = 0;
Si5326Config       si5326_cfg = 0;
Sis8300ChannelSel  sel = 0xa987654321ULL;
Si5326Mode         mode = Si5326_Error;
Si5326ParmsRec     parms;
unsigned *pp[6];
uint64_t fout, rat;

	while ( (opt = getopt(argc, argv, "hqSbBed:N:4f:CT:IvL:")) > 0 ) {
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
			case 'C': do_config = 1; break;
			case 'q': query = 1; break;
			case 'v': verbose = 1; break;
			
			case 'T': switch ( toupper( (int)(unsigned char)optarg[0] ) ) {
			              default: fprintf(stderr,"Option -T needs 'W' or 'N' argument\n"); return 1;
			              case 'W': mode = Si5326_WidebandMode;   break;
			              case 'N': mode = Si5326_NarrowbandMode; break;
					  }
			          break;

			case 'L': i_p = &bw; break;

			case 'I': ignore_fixed = 1; break;
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

	parms.bw = bw;

	if ( do_config ) {
		unsigned bwsel;
		if ( Si5326_Error != mode ) {
			fprintf(stderr,"Cannot use both: -C and -T\n");
			return 1;
		}
		if ( freq > 0 ) {
			fprintf(stderr,"Cannot use both: -C and -f\n");
			return 1;
		}
		if ( optind + 5 > argc ) {
			fprintf(stderr,"Option -C needs 6 configuration parameters\n");
			return 1;
		}
		pp[0] = &parms.n3;
		pp[1] = &parms.n2h;
		pp[2] = &parms.n2l;
		pp[3] = &parms.n1h;
		pp[4] = &parms.nc;
		pp[5] = &bwsel;
		for ( i=0; i<6; i++ ) {
			if ( 1 != sscanf(argv[optind+i], "%u", pp[i]) ) {
				fprintf(stderr,"Option -C: unable to scan parameter %i\n", i+1);
				return 1;
			}
		}
		parms.bwsel = (int)bwsel;
		parms.fin   = 250000000UL;
		parms.wb    = 0;
	}

	if ( !freq )
		div_clkhl = 0;

	switch ( enf_byp ) {
		default:
		break;
		case -1: div_clkhl = 0; break;
		case +1: div_clkhl = SIS8300_BYPASS_9510_DIVIDER; break;
	}

	if ( Si5326_Error == mode ) {

		if ( ! dev ) {
			fprintf(stderr,"No device - use '-d <device>' or set RACC_DEV env_var\n");
			return 1;
		}

		if ( (fd = open(dev, O_RDWR)) < 0 ) {
			perror("opening device");
			return 1;
		}

	} else {
		if ( 0 == freq ) {
			fprintf(stderr, "if you use -T you must also use -f\n");
			return 1;
		}
		query = 0;
	}

	if ( freq > 0 || do_config || query ) {
		if ( Si5326_Error == mode ) {
			mode = sis8300ClkDetect( fd );
		}
		switch ( mode ) {
			default:
				fprintf(stderr,"Sis8300ClkDetect - unknown result %i\n", mode);
				return 1;

			case Si5326_NoReference:
				fprintf(stdout,"Si5326 - no reference detected\n");
				return 1;

			case Si5326_NarrowbandMode:
				if ( fd >= 0 ) {
					fprintf(stdout,"Si5326 - operating in narrow-band mode\n");
				}
				si5326_cfg = si5326Configs_nb;
				break;
			case Si5326_WidebandMode:
				if ( fd >= 0 ) {
					fprintf(stdout,"Si5326 - operating in wide-band mode\n");
				}
				si5326_cfg = si5326Configs_wb;
				parms.wb   = 1;
				break;
		}

		if ( query > 0 ) {
			/* query operating mode only */
			return 0;
		}

		if ( ignore_fixed )
			si5326_cfg = si5326Configs_none;

		if ( freq > 0 ) {
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
				parms.fin = 250000000UL;
				parms.wb  = ( Si5326_WidebandMode == mode );
				if ( si53xx_calcParms( freq, &parms, verbose ) ) {
					fprintf(stderr, "Sorry, no configuration for output frequency %luHz found\n", freq);
					return 1;
				}
				si5326_clk = &parms;
			} else {
				si5326_clk = &si5326_cfg->parms;
			}
		}

		if ( do_config ) {
			si5326_clk = &parms;
		}
	}

	if ( fd >= 0 ) {

		if ( sis8300DigiSetup( fd, si5326_clk, div_clkhl, exttrig ) ) {
			goto bail;
		}

		if ( sis8300DigiSetCount(fd, sel, nblks * 16) ) {
			fprintf(stderr,"Setting sample count failed\n");
		}

	}
	if ( verbose || fd < 0 ) {
		if ( si5326_clk ) {
			fout = (uint64_t)si5326_clk->fin * (uint64_t)si5326_clk->n2h * (uint64_t)si5326_clk->n2l;
			fout /= (uint64_t)si5326_clk->n3 * (uint64_t)si5326_clk->n1h * (uint64_t)si5326_clk->nc;
			printf("PLL Input  Frequency:  %9luHz\n\n", si5326_clk->fin); 
			printf("                fin  %-4u*%4u\n", si5326_clk->n2h, si5326_clk->n2l);
			printf("Divider: fout = ---  ---------\n");
			printf("                %3u  %-4u*%4u\n\n", si5326_clk->n3, si5326_clk->n1h, si5326_clk->nc);
			printf("PLL Bandwidth:         %9uHz\n", si5326_clk->bw);
			printf("PLL Output Frequency:  %9"PRIu64"Hz\n", fout);
		} else {
			fout = 250000000ULL;
			printf("PLL Bypassed; Output Frequency %9"PRIu64"Hz\n", fout);
		}
        rat = div_clkhl > 0xff ? 1 : (div_clkhl & 0xf) + ((div_clkhl>>4) & 0xf) + 2;
		printf("AD9510 divider ratio:  %9"PRIu64"\n", rat);
		printf("Digitizer clock:       %9"PRIu64"Hz\n", fout/rat);
	}

	rval = 0;
bail:
	close( fd );
	return rval;
}
