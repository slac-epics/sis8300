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
	fprintf(stderr,"Usage: %s [-d device] [-S] [-b] [-N nblks] [-4]\n", nm);
	fprintf(stderr,"           -d device  : use 'device' (path to dev-node)\n");
	fprintf(stderr,"           -S         : set muxes to use si5326 clock\n");
	fprintf(stderr,"           -b         : do not bypass 9510 dividers (only if -S in wide-band mode)\n");
	fprintf(stderr,"           -e         : disable external trigger (enabled by default)\n");
	fprintf(stderr,"           -N nblks   : number of sample blocks (16samples) per channel\n");
	fprintf(stderr,"           -4         : use channels 2,4,6,8 only\n");
}

int
main(int argc, char **argv)
{
int      rval = 1;
int      fd = -1;
int      nblks = 2;
int      exttrig = 1;
unsigned div_clkhl = SIS8300_BYPASS_9510_DIVIDER;
int      opt;
const char *dev = getenv("RACC_DEV");
int      *i_p;
Si5326ParmsRec sip_wb = {
	/* Use 109MHz clock */
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
};
Si5326ParmsRec sip_nb = {
	/* Use 109MHz clock */
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
};
Si5326Parms si5326_clk = 0;
Sis8300ChannelSel  sel = 0xa987654321ULL;
Si5326Mode         mode;

	while ( (opt = getopt(argc, argv, "hSbed:N:4")) > 0 ) {
		i_p = 0;
		switch ( opt ) {
			default:
			case 'h': usage(argv[0]); return 0;

			case 'S': si5326_clk = &sip_wb; break;
			case 'b': div_clkhl  = 0; break;
	
			case 'd': dev        = optarg; break;

			case 'e': exttrig    = 0; break;

			case 'N': i_p        = &nblks; break;
	
			case '4': sel = 0x8642ULL; break;
		}

		if ( i_p ) {
			if ( 1 != sscanf(optarg,"%i",i_p) ) {
				fprintf(stderr,"Option '%c' needs integer argument!\n", opt);
				return 1;
			}
		}
	}

	if ( !si5326_clk )
		div_clkhl = 0;

	if ( ! dev ) {
		fprintf(stderr,"No device - use '-d <device>' or set RACC_DEV env_var\n");
		return 1;
	}

	if ( (fd = open(dev, O_RDWR)) < 0 ) {
		perror("opening device");
		return 1;
	}

	if ( si5326_clk ) {
		switch ( (mode = sis8300ClkDetect( fd )) ) {
			default:
				fprintf(stderr,"Sis8300ClkDetect - unknown result %i\n", mode);
			return 1;
			
			case Si5326_NoReference:
				fprintf(stdout,"Si5326 - no reference detected\n");
			return 1;

			case Si5326_NarrowbandMode:
				si5326_clk = &sip_nb;
				div_clkhl  = 0;
				fprintf(stdout,"Si5326 - operating in narrow-band mode\n");
			break;
			case Si5326_WidebandMode:
				si5326_clk = &sip_wb;
				fprintf(stdout,"Si5326 - operating in wide-band mode\n");
			break;
		}
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
