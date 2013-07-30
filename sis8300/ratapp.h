#ifndef RATAPP_H
#define RATAPP_H

#include <inttypes.h>

/* Approximation of rational numbers with others that have a smaller
 * denominator.
 */
#ifndef TEST_SMALL
typedef uint64_t RatNum;

#define RATNUM_MAX ((uint64_t)(-1LL))
#define RATu PRIu64
#define RATx PRIx64
#else
typedef uint32_t RatNum;

#define RATNUM_MAX ((uint32_t)(-1LL))
#define RATu PRIu32
#define RATx PRIx32
#endif

typedef struct Rational_ {
	RatNum n,d;
} Rational;

#define CONV_N1 1
#define CONV_N2 0

typedef struct Convergent_ {
	RatNum      a;
	Rational    conv;
} Convergent;

/* NOTES:
 *  - routines dont' handle overflow gracefully
 *  - must not supply denominator or zero.
 *  - r_max specifies max. acceptable numerator and denominator. 
 *    Values of zero mean 'infinite' - but not both may be set
 *    to zero or RATNUM_MAX.
 */

/* find (up to) the last 'n' convergents of n/d with denominator smaller or equal
 * to r_max.d and numerator smaller or equal to r_max.n.
 * 
 * The user must pass an array of 'n' Convergent structs. If the continued
 * fraction terminates then the 'a' member of the last convergent is zero.
 * RETURNS: The index of the last convergent. The last convergent is c[k].
 * NOTES:   Storage wraps around, i.e., if k >= n then the index of
 *          c[j] with j associated with k, k-1, k-(n-1) is computed as j = k MOD n;
 * 
 *          I.e., the last convergent is always at c[k % n];
 */
int
ratapp_find_convergents(Convergent *c, int n, Rational *r_in, Rational *r_max);

/* like ratapp_find_convergents() but store only the last two convergents
 * RETURNS: index/order of last convergent.
 * NOTES:   c[0] = c(n-1)
 *          c[1] = c(n-1)
 *
 *          c[1].a is set to zero (not stored).
 */
int
ratapp_find_last_convergents(Convergent c[2], Rational *r_in, Rational *r_max);

/* Return the number of convergents for r_in which have r_in->d <= r_max->d
 * and r_in->n <= r_max->n.
 */
int
ratapp_estimate_terms(Rational *r_in, Rational *r_max);

/* Find best rational approximation with denominator smaller or equal to d_max */
void
ratapp_find_rational(Rational *r, Rational *r_in, Rational *r_max);

/* Compute 'best' approximation considering 'l-th' intermediate fraction.
 * For convenience: if l is 0 or >= c1->a then l is set to c1-a - 1 (since
 * l == c1->a would yield the next convergent.
 *
 * RETURNS: l > 0 if intermediate fraction was used, 0 if convergent c1 was
 *          better. Best approximation stored in *r.
 *
 * NOTE:    This may be used to iterate through convergents to find a best 
 *          approximation where other criteria need to be met:
 *
 *          RatNum l = 0;
 *          k        = ratapp_find_convergents(c, n, &r_in, &r_max);
 *
 *          // c[k] is last convergent which meets r_max. Hence
 *          // c[k-1]+a[k]*c[k] == c[k+1] is not acceptable.
 *
 *          while ( --k >= 0 ) {
 *              // iterate through all intermediates
 *              l = c[k+1].a;
 *              do {
 *                  l--;
 *          		l = ratapp_intermediate( &r, l, &c[k+1], &c[k], &r_in );
 *                  if ( acceptable( &r ) ) {
 *          			// terminate loops
 *                      goto done;
 *                  }
 *              } while ( l > 0 );
 *          }
 */
RatNum
ratapp_intermediate(Rational *r, RatNum l, Convergent *c1, Convergent *c2, Rational *r_in);

#endif
