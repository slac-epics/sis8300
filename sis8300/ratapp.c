#include <math.h>

#ifdef TEST_RATAPP
#include <stdio.h>
#endif

#include <ratapp.h>


/* Hopefully the compiler optimizes this away... */

#define GR ((sqrt(5.0)+1.0)/2.0)


/* The "worst" continued fraction's convergents exhibit 'slowest'
 * growth of their numerators and denominators. I.e., for a denominator
 * of a give size more terms are required.
 * The worst case is [1;1,1,1,1,1,...] = (sqrt(5)+1)/2 (golden ratio).
 * since
 *    p(k) = a(k)*p(k-1)+p(k-2)
 * and with a(k)) integer and >0 it follows that the a(k)==1 leads to the
 * slowest possible growth of the p(k).
 * These are then Fibonacci numbers
 *
 * N (N-terms):      1   2   3   4   5   6   7   8
 * k (index)         0   1   2   3   4   5   6   7
 * ak                1   1   1   1   1   1   1   1
 * nk         0  1   1   2   3   5   8  13  21  34   == Fk+2
 * dk         1  0   1   1   2   3   5   8  13  21   == Fk+1
 *
 * Fj can be stated as { GR^j - (-GR)^(-j) } / sqrt(5)
 *
 * By solving for 'k' and assuming worst cases for signs we can
 * estimate the index of the next Fj > M where M is an integer.
 */                 

static int
fib_idx(int M)
{
double m = (double)M * sqrt(5.0);
	/* if j even - but we don't know which one it is */
double pl = log( (m + sqrt( m*m + 4.0))/2.0 ) / log( GR );
	/* if j odd  - but we don't know which one it is */
#ifdef TEST_RATAPP
double mi = round( log( (m + sqrt( m*m - 4.0))/2.0 ) / log( GR ) );
	/* If we're close to a Fj then pl, pm differ slightly from an integer (j)
	 * and we can round. If we are somewhere in the middle and the rounded
	 * values differ then take the ceiling.
	 * All of this is probably overkill but we just want to verify the theory ;-)
	 * (at least for the first few numbers by means of a brute-force test; see below).
	 *
	 * Just ceil(pl) should be enough - but ceil( log( sqrt(5) * M ) / log( GR ) )
	 * is not! A number with log(m)/log(GR) just below an integer could still
	 * be slightly bigger than an Fn. The ceiling would then yield Fn where
	 * in reality F(n+1) would be needed.
	 */
#endif
	return (int) (
#ifdef TEST_RATAPP
		(round(pl) == mi) ? mi :
#endif
		ceil( pl ) );
}

int
ratapp_estimate_terms(Rational *r_in, Rational *r_max)
{
RatNum m,l;
RatNum N;
int    inc = 0;

	/* max denominator is r_max->d or r_in->d if no limit is given by user */
	l = r_max->n;
	m = r_max->d;
	if ( 0 == m || RATNUM_MAX == m ) {
		if ( ! r_in || r_in->d == 0 )
			return -1;
		m = r_in->d;
	}

    /* however, if the numerator is limited then this implies a limitation on
	 * the denominator for
	 *
     *            !  nlim
     *    n/d = N =  ----
	 *               dlim
     *
     * i.e., the integer part of the limited fraction must equal the integer
	 * part of the unlimited fraction.
	 *
	 * Hence if nlim <= (nmax / N) * N. 
	 * 
	 * Therefore dlim = floor(nmax/N). However, in the case of N==0 (n<d)
	 * this breaks down.
	 * We note that this case is simply 0 + n/d = 1/(d/n) = [0; ao, a1, a2, a3...]
	 * I.e., we can proceed as above but now for d/n with nlim the new limiting denominator.
	 * Since we added one (zero) element (to the left of ao [which is also zero])
	 * we must increment the final result by 1.
	 */

	if ( 0 != l && RATNUM_MAX != l ) {
		/* if we have no r_in assume worst case of n/d == 0 */
		if ( r_in && (N = r_in->n/r_in->d) > 0 ) {
			l = l/N;
		} else {
			/* n < d; we hande this as 0 + 1 / ( d/n )
		     * i.e., we estimate the number of terms of d/n (with n limited to nlim)
			 * However, we have to add one to the final result since we added a
			 * zero term above.
			 */
			inc = 1;
		} 
		if ( l < m ) {
			m = l;
		} else {
			/* not limited by l - forget about the increment */
			inc = 0;
		}
	}
	
	/* Index of next bigger Fj; in the case of the denominator
	 * Fj = Fk+1 and since k+1 == N -> j == N
     */
	return fib_idx( m ) + inc;
}

int
ratapp_find_convergents(Convergent *c, int n, Rational *r_in, Rational *r_max)
{
int k, kl, rval;

RatNum t,u,num,den,w,a;
RatNum n_max = r_max->n;
RatNum d_max = r_max->d;

	if ( n < 2 )
		return -1;

	if ( 0 == d_max )
		d_max = RATNUM_MAX;
	if ( 0 == n_max )
		n_max = RATNUM_MAX;

	c[0].conv.n = 1;
	c[0].conv.d = 0;
	c[0].a   = a = r_in->n / r_in->d; 
	den = r_in->n - a*r_in->d;
	num = r_in->d;

	u = a * 1 + 0;
	t = a * 0 + 1;

	k = rval = 0;

	while ( t <= d_max && u <= n_max ) {
		kl = k;
		if ( ++k >= n )
			k = 0;
		rval++;
		c[k].conv.n = u;
		c[k].conv.d = t;
		if ( 0 == den ) {
			c[k].a = 0;
			break;
		} else {
			c[k].a = a = num/den;
		}
		w   = den;
		den = num - a*den;
		num = w;

		u = a * u + c[kl].conv.n;
		t = a * t + c[kl].conv.d;
	}

	return rval;
}

/* find convergent of n/d with denominator smaller or equal to d_max */
int
ratapp_find_last_convergents(Convergent c[2], Rational *r_in, Rational *r_max)
{
RatNum t, u, a, d1, d2, n1, n2;
RatNum d = r_in->d;
RatNum n = r_in->n;
RatNum d_max = r_max->d;
RatNum n_max = r_max->n;
int    k,j;

	n1=1;
	d1=0;

	n2=0;
	d2=1;

	a = n/d;
	u = a*n1+n2;
    t = a*d1+d2;

	k = -1;

	if ( 0 == d_max )
		d_max = RATNUM_MAX;
	if ( 0 == n_max )
		n_max = RATNUM_MAX;

	if ( RATNUM_MAX == d_max && RATNUM_MAX == n_max )
		return -1;

	while ( t <= d_max && u <= n_max ) {
		d2 = t;
        n2 = u;
		k++;
        
		n  -= a*d;
		if ( 0 == n ) {
			/* Should really set a = infinity but that's not possible with integers */
			a = 0;
			break;
		}
	    a   = d/n;

		u = a*n2 + n1;
		t = a*d2 + d1;

		if ( t > d_max || u > n_max ) {
			break;
		}

		d1 = t;
		n1 = u;
		k++;

		d  -= a*n;
		if ( 0 == d ) {
			/* Should really set a = infinity but that's not possible with integers */
			a = 0;
			break;
		}
		a   = n/d;

		u   = a*n1+n2;
		t   = a*d1+d2;
	}
	j = k&1;
	c[j^1].conv.n   = n2;
	c[j^1].conv.d   = d2;
	c[j].conv.n   = n1;
	c[j].conv.d   = d1;
	c[CONV_N2].a  = 0; /* Undefined */
	c[CONV_N1].a  = a;
	return k;
}

RatNum
ratapp_intermediate(Rational *r, RatNum l, Convergent *c1, Convergent *c2, Rational *r_in)
{
double     v;

	/* using c1->a would yield the next convergent
	 * NOTE: Special case of c1->a == 0 is handled
	 *       below; l is ignored and reset to 0 in
	 *       this case.
	 */
	if ( l > c1->a )
		l = c1->a;

	/* Check if semiconvergent for l is better than last convergent */
	if ( 0 == c1->a || l < (c1->a+1)/2 ) {
		r->d = c1->conv.d;
		r->n = c1->conv.n;
		l    = 0;
	} else {
		r->d = l*c1->conv.d+c2->conv.d;
		r->n = l*c1->conv.n+c2->conv.n;
		if ( l == c1->a/2 ) {
			v = (double)r_in->n/(double)r_in->d;
#ifdef TEST_RATAPP
			printf("testing special\n");
#endif
			if ( fabs(v - (double)r->n/(double)r->d) > fabs(v - (double)c1->conv.n/(double)c1->conv.d) ) {
#ifdef TEST_RATAPP
				printf("testing special negative\n");
#endif
				r->d = c1->conv.d;
				r->n = c1->conv.n;
				l    = 0;
			}
		}
	}
	return l;
}

/* Find best rational approximation with denominator smaller or equal to d_max */
void
ratapp_find_rational(Rational *r, Rational *r_in, Rational *r_max)
{
Convergent c[2];
RatNum     l,k;
int        n_conv;
	/* Find convergents first */
	n_conv = ratapp_find_last_convergents( c, r_in, r_max );

	if ( n_conv < 0 ) {
		/* error in input operands */
		return;
	}

	if ( c[CONV_N1].a == 0 ) {
		/* finite fraction; terminated with denominator or numerator less than d_max or n_max */
		r->d = c[CONV_N1].conv.d;
		r->n = c[CONV_N1].conv.n;
		return;
	}

	/* Estimate max allowable coefficient for semiconvergent */
	l = (r_max->d - c[CONV_N2].conv.d)/c[CONV_N1].conv.d;
	k = (r_max->n - c[CONV_N2].conv.n)/c[CONV_N1].conv.n;

	if ( k < l )
		l = k;

	ratapp_intermediate(r, l, &c[CONV_N1], &c[CONV_N2], r_in);
}

#ifdef TEST_RATAPP
static int
ratapp_find_convergent_1(Convergent c[2], RatNum n, RatNum d, RatNum n_max, RatNum d_max)
{
Rational r_arg, r_max;
	r_arg.n = n; r_arg.d = d;
	r_max.n = n_max; r_max.d = d_max;
#if 1
	return ratapp_find_last_convergents(c, &r_arg, &r_max);
#else
	{
	Convergent c_int[2];
	int k;
	k = ratapp_find_convergents(c_int, 2, &r_arg, &r_max);
	c[CONV_N1] = c_int[(k  )&1];
	c[CONV_N2] = c_int[(k+1)&1];
	return k;
	}
#endif
}

static void
ratapp_find_rational_1(Rational *r, RatNum n, RatNum d, RatNum n_max, RatNum d_max)
{
Rational r_arg, r_max;
	r_arg.n = n; r_arg.d = d;
	r_max.n = n_max; r_max.d = d_max;
	ratapp_find_rational(r, &r_arg, &r_max);
}


static void
pr_conv(Convergent c[2], FILE *f)
{
	fprintf(f,"Cn-1: %"RATu"/%"RATu", Cn-2: %"RATu"/%"RATu", An: %"RATu"\n",
		c[CONV_N1].conv.n,
		c[CONV_N1].conv.d,
		c[CONV_N2].conv.n,
		c[CONV_N2].conv.d,
		c[CONV_N1].a);
}

static void
pr_rat(Rational *r_p, FILE *f)
{
	fprintf(f,"R: %"RATu"/%"RATu"\n", r_p->n, r_p->d);
}

static int
cmp_conv(Convergent c1[2], Convergent c2[2])
{
int i;
	for ( i=0; i<2; i++ ) {
		if ( c1[i].conv.n != c2[i].conv.n || c2[i].conv.d != c2[i].conv.d )
			return -1;
	}
	return c1[CONV_N1].a != c2[CONV_N1].a;
}

static int
cmp_rat(Rational *r1_p, Rational *r2_p)
{
	return r1_p->n != r2_p->n || r1_p->d != r2_p->d;
}

static int
prcmp(const char *pre, void *a, void *b, int (*cmp_f)(), void (*pr_f)())
{
	if ( cmp_f(a,b) ) {
		fprintf(stderr, "%-25s failed\n", pre);
		fprintf(stderr,"Computed:\n  ");
		pr_f(a, stderr);
		fprintf(stderr,"Expected:\n  ");
		pr_f(b, stderr);
		return -1;
	}
	fprintf(stderr, "%-25s passed\n", pre);
	return 0;	
}

static int
prcmp_conv(const char *pre, Convergent c1[2], Convergent c2[2])
{
	return prcmp(pre, c1, c2, cmp_conv, pr_conv);
}

static int
prcmp_rat(const char *pre, Rational *r1_p, Rational *r2_p)
{
	return prcmp(pre, r1_p, r2_p, cmp_rat, pr_rat);
}

static int
cf_test(void)
{
unsigned pi_n = 4272943;
unsigned pi_d = 1360120;
Convergent c[2], c_i[2];
Rational   r, r_i, r_max;
Rational   r_i_m, r_max_m;
int      i,j,k,l,t,md,got;

	/* Test a few cases; for even and odd number of iterations */
	ratapp_find_convergent_1( c, pi_n, pi_d, RATNUM_MAX, 112);
	c_i[CONV_N1].conv.n = 333;
	c_i[CONV_N1].conv.d = 106;
	c_i[CONV_N1].a      =   1;
	c_i[CONV_N2].conv.n =  22;
	c_i[CONV_N2].conv.d =   7;
	if ( prcmp_conv( "First PI test", c, c_i ) ) {
		return 1;
	}
	c_i[CONV_N1].conv.n = 355;
	c_i[CONV_N1].conv.d = 113;
	c_i[CONV_N1].a      = 292;
	c_i[CONV_N2].conv.n = 333;
	c_i[CONV_N2].conv.d = 106;
	ratapp_find_convergent_1( c, pi_n, pi_d, RATNUM_MAX, 113);
	if ( prcmp_conv( "Second PI test", c, c_i ) ) {
		return 1;
	}
	ratapp_find_convergent_1( c, pi_n, pi_d, RATNUM_MAX, 114);
	if ( prcmp_conv( "Third PI test", c, c_i ) ) {
		return 1;
	}

	/* Test even and odd cases of a terminating example */

	/* 12/29 = [0;2,2,2] with [0;2,2] = 2/5, [0;2,2,2] = 5/12 */
	ratapp_find_convergent_1( c, 2, 5, RATNUM_MAX, 100);
	c_i[CONV_N1].conv.n = 2;
	c_i[CONV_N1].conv.d = 5;
	c_i[CONV_N1].a      = 0;
	c_i[CONV_N2].conv.n = 1;
	c_i[CONV_N2].conv.d = 2;
	if ( prcmp_conv( "First 2/5 test", c, c_i) ) {
		return 1;
	}

	c_i[CONV_N1].a      =  2;
	ratapp_find_convergent_1( c, 5, 12, RATNUM_MAX, 8);
	if ( prcmp_conv( "Second 2/5 test", c, c_i) ) {
		return 1;
	}

	c_i[CONV_N1].conv.n =  5;
	c_i[CONV_N1].conv.d = 12;
	c_i[CONV_N1].a      =  0;
	c_i[CONV_N2].conv.n =  2;
	c_i[CONV_N2].conv.d =  5;
	ratapp_find_convergent_1( c, 5, 12, RATNUM_MAX, 100);
	if ( prcmp_conv( "First 5/12 test", c, c_i) ) {
		return 1;
	}
	c_i[CONV_N1].conv.n =  5;
	c_i[CONV_N1].conv.d = 12;
	c_i[CONV_N1].a      =  2;
	c_i[CONV_N2].conv.n =  2;
	c_i[CONV_N2].conv.d =  5;
	ratapp_find_convergent_1( c, 12, 29, RATNUM_MAX, 28);
	if ( prcmp_conv( "Second 5/12 test", c, c_i) ) {
		return 1;
	}

	c_i[CONV_N1].conv.n = 12;
	c_i[CONV_N1].conv.d = 29;
	c_i[CONV_N1].a      =  0;
	c_i[CONV_N2].conv.n =  5;
	c_i[CONV_N2].conv.d = 12;

	ratapp_find_convergent_1( c, 12, 29, RATNUM_MAX, 100);
	if ( prcmp_conv( "First 12/29 test", c, c_i) ) {
		return 1;
	}

	/* Test a few cases including semiconvergents */
	ratapp_find_rational_1( &r, pi_n, pi_d, RATNUM_MAX, 112 );
	r_i.n=333;
	r_i.d=106;
	if ( prcmp_rat( "First PI approx test", &r, &r_i) )
		return 1;

	ratapp_find_rational_1( &r, pi_n, pi_d, RATNUM_MAX, 113 );
	r_i.n=355;
	r_i.d=113;
	if ( prcmp_rat( "Second PI approx test", &r, &r_i) )
		return 1;

	ratapp_find_rational_1( &r, pi_n, pi_d, RATNUM_MAX, 16603 );
	if ( prcmp_rat( "Third PI approx test", &r, &r_i) )
		return 1;

	ratapp_find_rational_1( &r, pi_n, pi_d, RATNUM_MAX, 16604 );
	r_i.n=52163;
	r_i.d=16604;
	if ( prcmp_rat( "Fourth PI approx test", &r, &r_i) )
		return 1;

	/* A few borderline cases */
	ratapp_find_rational_1( &r, 0, 1, RATNUM_MAX, 100 );
	r_i.n=0;
	r_i.d=1;
	if ( prcmp_rat( "0/1 approx test", &r, &r_i) )
		return 1;

	/* A few borderline cases */
	ratapp_find_rational_1( &r, 1, 1, RATNUM_MAX, 100 );
	r_i.n=1;
	r_i.d=1;
	if ( prcmp_rat( "1/1 approx test", &r, &r_i) )
		return 1;

	/* Now test the normal and special cases of the approximation: */

	/* Let's look at [0;2,2,2] = 1/(2+1/(2+1/2)) = 5/12
     *
     * The range of  [0;2,2] = 2/5 is [0;2,1,1+1] = 3/8 ... [0;2,2+1] = 3/7
     *
	 * The convergents are [1/2, 2/5, 5/12]
     * 
     * The "special case" occurs if we approximate a value between 3/8..3/7
     * where l == a3/2; with a3 even. If we pick a3 == 2 then we have
	 * l = 1 and with d1 = 5, d2 = 2 -> m = 5*l+2 = 7. m up to 7+(5-1) yield
     * the same l == 1.
     *
     * Now we set out to find numbers which are closer and farther, respectively
     * to 2/5 and (l*n1+n2)/(l*d1+d2) = (2+1)/(2+5) = 3/7.
     * 
	 * The arithmetic mean is 1/2(2/5+3/7) = (14+15)/2/35 = 29/70 > 3/8.
     * 
     * We have 29/70 = [0;2,2,2,2,2]. We can slightly decrease this by 
     * decreasing a coefficient at even position (or increasing one at
     * an odd position). However, we want to leave a3=2 intact.
     * 
     * We find 3/8 < [0;2,2,2,1,2] = 19/46 < 29/70
     *
     * Hence 19/46 fulfills the conditions for the 'special test case'
	 * when we ask for approximation with denominator <= 7..11.
	 *
     * 1) last convergents with denominator <= 7..11
     *      [ 1/2, 2/5 ]
	 * 2) a3 even (2)
	 * 3) l = floor( (m - d2) / d1 ) = floor( (7 - 2)/5 ) = 1
	 * 4) semiconvergent: (1*n1+n2)/(1*d1+d2) = (1+2)/(2+5) = 3/7
     * 5) l = 1 = a3/2
     * 6) 19/46 - 2/5 < 3/7 - 19/46 => REJECT semiconvergent
	 *
	 * If we pick 3/7 > [0;2,2,2,3,2] = 39/94 > 29/70 then
	 * the above steps until 5 remain the same. However,
	 * we now have
	 * 6a) 39/94 - 2/5 > 3/7 - 39/94 => ACCEPT semiconvergent
	 */
	ratapp_find_rational_1( &r, 19, 46, RATNUM_MAX, 10 );
	r_i.n = 2;
	r_i.d = 5;
	if ( prcmp_rat( "19/46 (reject semiconvergent) test", &r, &r_i ) )
		return 1;

	ratapp_find_rational_1( &r, 39, 94, RATNUM_MAX, 10 );
	r_i.n = 3;
	r_i.d = 7;
	if ( prcmp_rat( "39/94 (accept semiconvergent) test", &r, &r_i ) )
		return 1;

	/* Brute-force test of ratap_estimate_terms() */
#define M1 100
#define M2 100
	fprintf(stderr,"Brute-force testing ratapp_estimate_terms() -- hang in there");
	md = 0;
	for ( i=0; i<M1; i++ ) {
		r_i.n = i;
		if ( ! (i & 63) )
			fprintf(stderr,"\n");
		fprintf(stderr,".");
		for ( j=1; j<M1; j++ ) {
			r_i.d = j;
			for ( k=1; k<M2; k++ ) {
				r_max.n = k;
				for ( l=0; l<M2; l++ ) {
					r_max.d=l;
					got = ratapp_find_last_convergents( c, &r_i, &r_max );
					if ( (t = ratapp_estimate_terms( &r_i, &r_max )) < got + 1 ) {
						fprintf(stderr,"MISMATCH FOUND %i %i %i %i -> %i < %i\n", i,j,k,l,t,got + 1);
						if ( got + 1 - t > md ) {
							md = got + 1 - t;			
							r_i_m = r_i;
							r_max_m = r_max;
						}
					}
				}
			}
			r_max.n = 0;
			for ( l=1; l<M2; l++ ) {
				r_max.d = l;
				got = ratapp_find_last_convergents( c, &r_i, &r_max );
				if ( (t = ratapp_estimate_terms( &r_i, &r_max )) < got  + 1 ) {
					fprintf(stderr,"MISMATCH FOUND %i %i %i %i -> %i < %i\n", i,j,k,l,t,got + 1);
					if ( got + 1 - t > md ) {
						md = got + 1 - t;			
						r_i_m = r_i;
						r_max_m = r_max;
					}
				}
			}
		}
	}
	fprintf(stderr,"\nDone: ");
	if ( md ) {
		fprintf(stderr,"MAX MISMATCH: %i\n", md);
		fprintf(stderr,"r = %"RATu"/%"RATu", r_max = %"RATu"/%"RATu"\n", r_i_m.n, r_i_m.d, r_max_m.n, r_max_m.d);
		return 1;
	} else {
		fprintf(stderr,"passed\n");
	}

	return 0;
}


int
main(int argc, char **argv)
{
int i,m;
unsigned a[4];
Rational r;

	m = sizeof(a)/sizeof(a[0]);
	for ( i=0; i<m; i++ )
		a[i] = 0;

	if ( 1 == argc ) {
		return cf_test();
	} else {
		for ( i=1; i<argc && i < m+1; i++ ) {
			if ( 1 != sscanf(argv[i],"%u",a+i-1) ) {
				break;
			}
		}
	}
	if ( 0 == a[1] || (0 == a[2] && 0 == a[3]) ) {
		fprintf(stderr,"Need at least 3 integer arguments: N D D_max [N_max]\n");
		fprintf(stderr,"D must not be zero and D_max, N_max must not both be zero (0 means infinite)\n");
		return 1;
	}

	ratapp_find_rational_1( &r, a[0], a[1], 0 == a[3] ? RATNUM_MAX : a[3], 0 == a[2] ? RATNUM_MAX : a[2]);

	printf("Best rational approximation (max. denominator %u, max. numerator %u) of %u/%u == %"RATu"/%"RATu"\n", a[2], a[3], a[0], a[1], r.n, r.d);
	return 0;
}
#endif
