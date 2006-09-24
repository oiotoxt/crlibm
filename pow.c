
#include <stdio.h>
#include "crlibm.h"
#include "crlibm_private.h"
#include "triple-double.h"
#include "pow.h"


void log2_12(double* logxh, double* logxl, double x) {
  int E, index;
  db_number xdb;
  double ed, ri, logih, logil, y, yrih, yril, th, zh, zl;
  double t11h, t11m, t12h, t12m, t13h, t13m, t14h, t14m;
  double highPoly, logManth, logMantl, t15, t16, t17;
  
  E=0;
  xdb.d=x;
  
  /* Filter cases */
  if (xdb.i[HI] < 0x00100000){        /* x < 2^(-1022)    */
    /* Subnormal number */
    E = -52; 		
    xdb.d *= TWO52; 	  /* make x a normal number    */ 
  }
  
  /* Extract exponent and mantissa 
     Do range reduction,
     yielding to E holding the exponent and
     y the mantissa between sqrt(2)/2 and sqrt(2)
  */
  E += (xdb.i[HI]>>20)-1023;             /* extract the exponent */
  index = (xdb.i[HI] & 0x000fffff);
  xdb.i[HI] =  index | 0x3ff00000;	/* do exponent = 0 */
  index = (index + (1<<(12))) >> (13);
  
  /* reduce  such that sqrt(2)/2 < xdb.d < sqrt(2) */
  if (index >= 53){ /* corresponds to xdb>sqrt(2)*/
    xdb.i[HI] -= 0x00100000; 
    E++;
  }
  y = xdb.d;
  index = index & 0x7f;
  /* Cast integer E into double ed for addition later */
  ed = (double) E;
  
  /* 
     Read tables:
     Read one float for ri
     Read the first two doubles for -log(r_i) 
     
     Organization of the table:
     
     one struct entry per index, the struct entry containing 
     r, logih and logil in this order
  */
  
  
  ri = argredtable[index].ri;
  /* 
     Actually we don't need the logarithm entries now
     Move the following two lines to the eventual reconstruction
     As long as we don't have any if in the following code, we can overlap 
     memory access with calculations 
  */
  logih = argredtable[index].logih;
  logil = argredtable[index].logil;
  
  /* Do range reduction:
     
     zh + zl = y * ri - 1.0 correctly
  
     Correctness is assured by use of Mul12 and Add12
     even if we don't force ri to have its' LSBs set to zero
  
     Discard zl for higher monome degrees
  */
  
  Mul12(&yrih, &yril, y, ri);
  th = yrih - 1.0; 
  Add12Cond(zh, zl, th, yril); 
  
  /* Polynomial approximation */


#if defined(PROCESSOR_HAS_FMA) && !defined(AVOID_FMA)
  highPoly = zh * FMA(FMA(FMA(FMA(log2coeff8,zh,log2coeff7),zh,log2coeff6),zh,log2coeff5),zh,log2coeff4);
#else
  highPoly = zh * (log2coeff4 + zh * (log2coeff5 + zh * (log2coeff6 + zh * (log2coeff7 + zh * log2coeff8))));
#endif

  Add12(t11h,t11m,log2coeff3,highPoly);
  MulAdd22(&t12h,&t12m,log2coeff2h,log2coeff2l,zh,zl,t11h,t11m);
  MulAdd22(&t13h,&t13m,log2coeff1h,log2coeff1l,zh,zl,t12h,t12m);
  Mul22(&t14h,&t14m,t13h,t13m,zh,zl);
  
  /* Reconstruction 

     Compute log2(x) = E + p(z) + log(1/ri) + delta

  */

  Add22Cond(&logManth,&logMantl,t14h,t14m,logih,logil);
  Add12Cond(t15,t16,ed,logManth);
  t17 = t16 + logMantl;
  Add12(*logxh,*logxl,t15,t17);

}

void exp2_12(int *E, double* exp2h, double* exp2l, double xh, double xl) {
  double scaledX, shiftedX, kd, rescaledK, z, tbl1h, tbl1l, tbl2h, tbl2l, q;
  double log2MultXl, qPlusLog2MultXl, qMultLog2MultXl, highPoly, polyh, polyl;
  double tablesh, tablesl;
  db_number shiftedXdb;
  int k, index1, index2;

  /* We compute 2^(xh + xl) as

     2^(xh + xl) = 2^xh * 2^xl = 2^xh * (1 + double(log(2)) * xl) * (1 + eps1) 
     
     where abs(eps1) < 2^(-86) because 

     abs(xh + xl) <= 2^(10) thus abs(xl) <= 2^(-42) 

     We compute 2^xh as 

     2^xh = 2^(E + i1/64 + i2/4096 + z) 
          = 2^E * 2^(i1/64) * 2^(i2/4096) * 2^z 
	  = 2^E * tbl1 * tbl2 * p(z) * (1 + eps2)

     where abs(eps2) <= 2^(-67)
          
     We have p(z) = 1 + q(z) 
     where q(z) = z * (c1 + z * (c2 + z * (c3 + z * c4)))

     Thus

     2^(xh + xl) = 2^E * tbl1 * tbl2 * (1 + q(z) + double(log(2)) * xl + q(z) * double(log(2)) * xl) * (1 + eps)
     

  */

  /* Overflow and underflow handling 

     Detailed analysis shows that 2^(xh + xl) surely overflows for xh >= 1024 and 
     that it does surely not overflow for xh + xl = 1024 - 1 * ulp(1023) + 1/2 * ulp(1023)
     Thus it suffices to check that xh <= 1024 - 1 * ulp(1023), i.e. xh < 1024

     Analysis shows further that 2^(xh + xl) rounds (in RN mode) to 0 for xh <= -1075 and
     that it does round to the least subnormal for xh + xl = -1075 + 1 * ulp(1075) - 1/2 * ulp(1075)
     Thus it suffices to check that xh >= -1075 + 1 * ulp(1075), i.e. xh > -1075
    
     On overflow, we set exp2h = 1, exp2l = 0 and E = 1025
     On underflow, we set exp2h = 0, exp2l = 0 and E = -1076

     Remark: we do not handle special cases like Inf and NaN in input.

  */

  if (xh > 1024.0) {
    *exp2h = 1.0;
    *exp2l = 0.0;
    *E = 1025;
    return;
  }

  if (xh < -1075.0) {
    *exp2h = 0.0;
    *exp2l = 0.0;
    *E = -1076;
    return;
  }

  /* Handling of small (subnormal) values on input 

     For abs(xh) < 2^(-54) 2^(xh + xl) is 1.0 (+/- 1ulp depending on the rounding mode)

     We check for this bound and set exp2h = 1.0, exp2l = 0.0 and E = 0 in this case

  */

  if (ABS(xh) < TWOM54) {
    *exp2h = 1.0;
    *exp2l = 0.0;
    *E = 0;
    return;
  }


  /* We start by computing a floating-point and integer representation of the 
     integer nearest to (2^(12) * xh) 
     Since the integer is representable on at most 11 bits, we can use the 
     shift method 
  */

  scaledX = SCALE * xh;
  shiftedX = scaledX + SHIFTCONSTANT;
  kd = shiftedX - SHIFTCONSTANT;
  rescaledK = RESCALE * kd;
  z = xh - rescaledK;
  shiftedXdb.d = shiftedX;
  k = shiftedXdb.i[LO];
  *E = k >> 12;
  index1 = (k & 0xfc0) >> 6;
  index2 = k & 0x3f;

  /* Table reads */
  
  tbl1h = twoPowerIndex1[index1].hi;
  tbl1l = twoPowerIndex1[index1].lo;
  tbl2h = twoPowerIndex2[index2].hi;
  tbl2l = twoPowerIndex2[index2].lo;

  /* Polynomial approximation */

#if defined(PROCESSOR_HAS_FMA) && !defined(AVOID_FMA)
  q = z * FMA(FMA(FMA(exp2coeff4,z,exp2coeff3),z,exp2coeff2),z,exp2coeff2);
#else
  q = z * (exp2coeff1 + z * (exp2coeff2 + z * (exp2coeff3 + z * exp2coeff4)));
#endif
  
  log2MultXl = LOG2 * xl;

  qPlusLog2MultXl = q + log2MultXl;
  qMultLog2MultXl = q * log2MultXl;
  highPoly = qPlusLog2MultXl + qMultLog2MultXl;

  Add12(polyh,polyl,1.0,highPoly);

  /* Reconstruction */

  Mul22(&tablesh,&tablesl,tbl1h,tbl1l,tbl2h,tbl2l);
  Mul22(exp2h,exp2l,tablesh,tablesl,polyh,polyl);

}

int isPowerSquare(double *j, int F, double m) {
  double logmh, logml, tFMlmh, tFMlml, uh, ul, jh, jl;
  db_number tempdb;
  int E, i;
  double shiftedJh, delta, jP, shiftedJ, corr;
  double s, th, tl;
  

  /* Preconditions:

     (i)  m an odd integer greater or eqal 3 and less than 2^(53)
     (ii) -5 <= F <= -1

  */

  /* We have to check whether there exists an odd integer j such that 
     
     j^(2^(-F)) = m      i.e.    j = m^(2^F);

     We perform this as follows:
     
     (i)   We compute an approximation jh + jl to m^(2^F). This approximation
           is exact to at least 53 bits. 
     (ii)  We round jh + jl to the nearest integer j. This rounding is 
           subject to the Table Maker's Dilemma iff m is such that m^(2^F) is not integer.
	   Thus if there exists an integer j' such that j'^(2^(-F)) = m, the computed j is
	   equal to the exact j'.
     (iii) We compute j^(2^(-F)) exactly by repeated squaring. 
           Since m is written on at most 53 bits, and j is an integer, 
	   if one of the square operations must be written on more than 
	   53 bits, there exists no integer j' such that j'^(2^(-F)) = m. 
     (iv)  If all squarings produce results that can be hold on 53 bits, the final result
           j^(2^(-F)) must be equal to m. Otherwise, there exists no integer j' such that 
	   j'^(2^(-F)) is equal to m.
  

     We start by computing the approximation jh + jl = m^(2^F) * (1 + eps) as

     jh + jl = 2^(2^F * log2(m)) * (1 + eps) 

     At the beginning, we compute log2(m).
   
  */

  log2_12(&logmh, &logml, m);

  /* Represent 2^F */
  tempdb.i[HI] = (F + 1023) << 20;
  tempdb.i[LO] = 0;

  /* Multiply log2(m) by 2^F */
  tFMlmh = tempdb.d * logmh;
  tFMlml = tempdb.d * logml;

  /* Compute 2^(2^F * log2(m)) as 2^E * (uh + ul) */
  exp2_12(&E, &uh, &ul, tFMlmh, tFMlml);

  /* Represent 2^E */
  tempdb.i[HI] = (E + 1023) << 20;
  tempdb.i[LO] = 0;

  /* Compute jh + jl = 2^E * (uh + ul) 

     Since -5 <= F <= -1 and 3 <= m <= 2^53, there is no 
     overflow nor underflow on this multiplication.

  */
  jh = tempdb.d * uh;
  jl = tempdb.d * ul;
  
  /* Round now jh + jl to the nearest integer 

     Since jh + jl = m^(2^F) is bounded by 2^(27), we can use the 
     shift method.
     
     We round first jh to jP. We compute then the rounding rest 
     delta = jh - jP. We add then jl to delta and this arithmetical sum 
     to shiftedJh. This should correct jP to the possibly nearer j in most cases.
     The rounding is not correct, anyway.

  */
  shiftedJh = jh + SHIFTCONSTANT;
  jP = shiftedJh - SHIFTCONSTANT;
  delta = jh - jP;
  corr = delta + jl;
  shiftedJ = shiftedJh + corr;
  *j = shiftedJ - SHIFTCONSTANT;

  /* Compute now j^(2^(-F)) by repeated squaring.
     As explained above, each intermediate must be able to be written on 
     at most 53 bits. 

     Since m is bounded by 2^(53), none of the Dekker sequences will overflow.
  */
  i = -F;
  s = *j;
  while (i > 0) {
    Mul12(&th,&tl,s,s);
    if (tl != 0.0) {
      /* An intermediate squaring cannot be written on 53 bits. 
	 Since j is equal to m^(2^F) if there exists an integer j' 
	 such that j'^(2^(-F)) = m, there does not exist any such integer j'.
      */
      return 0;
    }
    s = th;
    i--;
  }
  /* Check now whether s = j^(2^(-F)) is equal to m. */

  if (s != m) {
    /* s = j^(2^(-F)) is not equal to m. Since j is equal to m^(2^F) if
       there exists an integer j' such that j'^(2^(-F)) = m, there does
       not exist any such integer j'.
    */
    return 0;
  }

  return 1;
}


int checkForExactCase(double x, double y, int H, double kh, double kl) {
  int32_t E, F, G;
  db_number xdb, ydb, tmpdb, khdb, tdb;
  double t1, t2, t3, t4;
  int d;
  int32_t tempInt;
  double m, n, r;
  double Ed, Gd, eMyh, eMyl;
  double khn, kln;
  double j, z, ph, pl, zh, zl, qh, ql;
  int isPowerSquareCase, tt;
  double rest, yEdh, yEdl;

#if defined(CRLIBM_TYPECPU_AMD64) || defined(CRLIBM_TYPECPU_X86) 
  db_number tmppdb;
#endif

  /* Preconditions: 

     (i)   x is positive and different from 0
     (ii)  y is different from 0
     (iii) y is different from 1
     (iv)  x^y does not overflow nor is flushed to 0
     (v)   kh + kl can be written on at most 54 bits
  
  */

  /* Copy x and y to db_numbers for bit manipulations */
  xdb.d = x;
  ydb.d = y;

  /* If y is negative, x^y can be exact only if x is
     a integer power of 2 and y is integer.
     We start by checking this.
  */

  if ((ydb.i[HI] & 0x80000000) == 0x80000000) {
    /* y is negative */
    
    /* Start by checking if x is an integer power of two 

       The check is simple if x is normal. For subnormal x
       we multiply by 2^1000 and check on the resulting normal.

    */
    if ((xdb.i[HI] & 0xfff00000) == 0) {
      /* x is subnormal */
      tmpdb.d = x * TWO1000;
    } else {
      tmpdb.d = xdb.d;
    }

    if (((tmpdb.i[HI] & 0x000fffff) | tmpdb.i[LO]) != 0) {
      /* x is not an integer power of 2 */
      /* x^y can thus not be an exact case */
      return 0;
    }

    /* If we are here, x is an integer power of 2.
       Compute E, the exponent of x.
       Check then if E * y is integer 

       Check first if E * y is less than 1.0 in magnitude.
       If yes, check if E * y is zero and return.
    
       Otherwise, we compute the integer
       nearest to E * y and compare the difference with 0.

       E * y must be written on a double-double. 
       If the high order word is less than 2^(52), we can work
       only on the high order word and verify the low order word
       is equal to 0. Otherwise, the high order word is already an
       integer and we can work on the low order word.

       The Dekker sequence is always exact because is y is always 
       less than 2^64 and E is always less than 2^10.

    */

    E = (xdb.i[HI] >> 20) - 1023;
    Ed = E;

    Mul12(&yEdh,&yEdl,y,Ed);

    if (yEdh < TWO52) {
      if (yEdl != 0.0) {
	/* yEdl is not equal to 0 and yEdh may still have a fractional part */
	return 0;
      }
      rest = yEdh;
    } else {
      /* yEdh is always integer, we work only on yEdl */
      rest = yEdl;
    }

    /* Compute the integer nearest to r by subnormal rounding */

    t1 = TWOM53 * rest;
    tmpdb.d = TWOM1021 * t1;
    
    /* If we are on x86, we must force the compile to go through memory in order to have 
       correct double subnormals 
    */
#if defined(CRLIBM_TYPECPU_AMD64) || defined(CRLIBM_TYPECPU_X86) 
    tmppdb.i[HI] = tmpdb.i[HI];
    tmppdb.i[LO] = tmpdb.i[LO];
    tmpdb.d = tmppdb.d;
#endif
    
    /* Remultiply */

    t2 = TWO1021 * tmpdb.d;
    t3 = TWO53 * t2;
    
    /* Compute the difference */

    t4 = t3 - rest;

    /* If the difference is 0.0, E * y is an integer and the case is exact 
       Otherwise the case is not exact.
       In the case we return "yes, exact case", we do not check 
       that actually x^y = 2^H * (kh + kl) but rely on the fact that 
       the value kh + kl = x^y * (1 + eps) is exact enough for correct rounding
       without occurence of the Table Maker's dilemma. 
    */
    return (t4 == 0.0);
  }

  /* Here y is always positive */

  /* We compute now E, F, G as well as m, n, r such that

     x = 2^E * m, E integer, m odd integer
     y = 2^F * n, F integer, n odd integer 
     2^H * (kh + kl) = 2^G * (2 * r + 1), G integer, r integer

     We represent E, F and G on integer variables and m, n and r on 
     floating point variables. This is possible also for r since
     kh + kl has maximally 54 bits set to one and we represent 1
     of this ones implicitely.

  */

  /* Start with x */

  /* Extract the exponent and remove it form x 
     Compute the exponent E so that the corresponding mantissa is integer 
  */
  E = (xdb.i[HI] >> 20) - 1023 - 52;
  xdb.i[HI] = xdb.i[HI] & 0x000fffff;

  /* Check how often x * 2^(-E) can be divided by 2 in integer */

  if (xdb.i[LO] == 0) {
    d = 32;
    tempInt = xdb.i[HI] | 0x00100000;
  } else {
    d = 0;
    tempInt = xdb.i[LO];
  }

  while ((tempInt & 0x1) == 0) {
    tempInt >>= 1;
    d++;
  }

  /* Correct now E and set the appropriate exponent in xdb which becomes m */
  
  xdb.i[HI] |= (52 + 1023 - d) << 20;
  m = xdb.d;
  E += d;

  /* Continue with y */

  /* Extract the exponent and remove it form y 
     Compute the exponent F so that the corresponding mantissa is integer 
  */
  F = (ydb.i[HI] >> 20) - 1023 - 52;
  ydb.i[HI] = ydb.i[HI] & 0x000fffff;

  /* Check how often y * 2^(-F) can be divided by 2 in integer */

  if (ydb.i[LO] == 0) {
    d = 32;
    tempInt = ydb.i[HI] | 0x00100000;
  } else {
    d = 0;
    tempInt = ydb.i[LO];
  }

  while ((tempInt & 0x1) == 0) {
    tempInt >>= 1;
    d++;
  }

  /* Correct now F and set the appropriate exponent in ydb which becomes n */
  
  ydb.i[HI] |= (52 + 1023 - d) << 20;
  n = ydb.d;
  F += d;

  /* Compute now r such that      
     
     2^H * (kh + kl) = 2^G * (2 * r + 1), G integer, r integer

     We have two main cases: 
     Main cases:
     (i)  kl = 0: 
          We compute first G and k an odd integer such that 
          2^H = 2^G * k
	  r is then (k - 1) / 2 
	  This last operation can be done exactly in FP because 
	  k has maximally 53 bits and is an odd integer.
     (ii) kl = +/- 1/2 * ulp(kh)
          Let be R such that 
	  2^R * (kh + kl) is an odd integer
	  Thus 2^R * kl = +/- 1 and 2^R * kh = 2 * r', i.e. r' = 2^(R-1) * kh
	  r is then equal to r' if kl is positive and r' - 1 otherwise
  */

  /* kh and kl might be not normalized, we normalize by a Add12Cond */

  Add12Cond(khn,kln,kh,kl);
  
  khdb.d = khn;

  if (kln == 0.0) {
    /* Extract the exponent and remove it from kh
       Compute the exponent G so that the corresponding mantissa is integer 
    */
    G = (khdb.i[HI] >> 20) - 1023 - 52;
    khdb.i[HI] = khdb.i[HI] & 0x000fffff;
    
    /* Check how often y * 2^(-G) can be divided by 2 in integer */
    
    if (khdb.i[LO] == 0) {
      d = 32;
      tempInt = khdb.i[HI] | 0x00100000;
    } else {
      d = 0;
      tempInt = khdb.i[LO];
    }
    
    while ((tempInt & 0x1) == 0) {
      tempInt >>= 1;
      d++;
    }
    
    /* Correct now G and set the appropriate exponent in khdb which yields to r */
    
    khdb.i[HI] |= (52 + 1023 - d) << 20;
    G += d;

    r = (khdb.d - 1.0) * 0.5;    
  } else {
    /* Extract the exponent and remove it from kh
       Compute the exponent G so that the corresponding mantissa is integer 
    */
    G = (khdb.i[HI] >> 20) - 1023 - 52 - 1;
    khdb.i[HI] = (khdb.i[HI] & 0x000fffff) | ((52 + 1023) << 20);
    if (kl < 0.0) r = khdb.d - 1.0; else r = khdb.d;
  }
  /* Take into account the scaling 2^H */
  G += H;
  
  /* In order to show that a case is exact (or midway) 
     we have to show 

     x^y = 2^H * (kh + kl) 

     Using the equalities

     x = 2^E * m
     y = 2^F * n
     2^H * (kh + kl) = 2^G * (2 * r + 1) 

     this yields to

     (2^E * m)^(2^F * n) = 2^G * (2 * r + 1)

     which is equal to

     2^(E * 2^F * n) * m^(2^F * n) = 2^G * (2 * r + 1)

     One can show that for 

     E,F,G integer and m,n,k odd integers 

     this implies that 

     (i)  E * 2^F * n = G
     (ii) m^(2^F * n) = 2 * r + 1

     In the following, we first check (i). If this 
     check fails, the case cannot be exact or midway.

     Since 2^F * n = y and E can be written on at most 11 bits,
     we can compute E * 2^F * n as E * y on a double-double 
     E * 2^F * n = eMyh + eMyl. Since G can be written on at most 11 bits,
     we can checking equality by checking that eMyl is equal to 0 and
     eMyh is equal to G.
     
     E and G are stored on integer variables. We have to convert them
     first to doubles Ed and Gd.
     The value y is bounded by 2^(70) since x^y does not overflow,
     so the unconditional Dekker sequence is always correct.

  */

  Ed = E;
  Gd = G;

  Mul12(&eMyh,&eMyl,Ed,y);
  
  if ((eMyl != 0.0) || (eMyh != G)) {
    /* eMyl is not equal to 0 or eMyh is different from G, thus eMyh + eMyl is different
       from G. Hence the case is not exact nor midway.
    */
    return 0;
  }

  /* If we are here, we know that 
     
     (i)  E * 2^F * n = G

     We have to check now that 

     (ii) m^(2^F * n) = 2 * r + 1

     We have first a special case to eliminate.

     If m = 1, r must be equal to 0. Otherwise the case is inexact.
     
  */
 
  if (m == 1.0) {
    /* Special case: m = 1 */
    if (r == 0.0) {
      /* We know now that 

         m = 1 and r = 0

	 Thus, for all F and n 

	 m^(2^F * n) = 2 * r + 1

	 Further, we have
       
	 E * 2^F * n = G

	 Hence

	 2^(E * 2^F * n) * m^(2^F * n) = 2^G * (2 * r + 1)

	 i.e. the case is exact
      */
      return 1;
    } else {
      /* We know now that 
	 m = 1 and r != 0

	 Thus, there exists no F and no n such that

	 1 = m^(2^F * n) = 2 * r + 1 != 1

	 Hence the case is inexact.
      */
      return 0;
    }
  }

  /* If we are here, m >= 3 since m != 0, m != 1 and m is odd 

     We still want to check 

     m^(2^F * n) = (2 * r + 1) 

     We have two main cases: 
     (i)  F is positive or zero 
          Since m is at least 3 and (2 * r + 1) is bounded by 2^54, the
          integer 2^F * n must be less or equal to 35.  Since y is
          different from 1 and y = 2^F * n, 2^F * n is at least 2.  We
          can therefore perform (2^F * n - 2) exact multiplications by
          m (starting with m in an accumulator) which must produce all
          results that are representable on at most 53
          bits. Effectively, there is no m such that an intermediate
          product t = m^l which contains more than 53 bits multiplied
          by m can be written on less or equal 53 bits.  A last
          multiplication by m produces then m^(2^F * n) in a double-double
	  which must be equal to 2 * r + 1.
     (ii) F is negative
          Let us first show that -F is bounded by 5.
	  Let be p_i, q_i prime numbers and a_i, b_i integers (valuations) such that 

	  m = product((p_i)^(a_i),i=1..) and (2 * r + 1) = product((q_i)^(b_i),i=1..) 

	  In order to have 
	  
	  m^(2^F * n) = (2 * r + 1)
	  
	  we must have

	  product((p_i)^(a_i * n),i=1..) = product((q_i)^(2^(-F) * b_i),i=1..) 

	  So, there exists a permutation s such that 

	  p_i = q_(s(i))

	  and

	  a_i * n = b_(s(i)) * 2^(-F) 

	  Since m is an odd integer and greater or equal to 3, its least 
	  prime factor must be 3, i.e. for all i, p_i >= 3. Since m is
	  less or equal to 2^(53), each valuation a_i must thus be bounded by
	  
	  a_i <= 53 * ln(2)/ln(3) < 37

	  The valuation a_i is an integer. Let be g the valuation of the 
	  prime factor 2 of a_i (i.e. let u be an odd integer and g an intger 
	  such that a_i = 2^g * u). Since a_i <= 36, g is bounded by 

	  g <= ln(36)/ln(2) < 5

	  Since n is odd, -F is bounded by g and therefore bounded by 5.

	  Since n is odd, in order to have 

	  m^(2^F * n) = (2 * r + 1) with F < 0

	  there must be an odd integer j such that 

	  j^(2^(-F)) = m 

	  and 

	  j^n = (2 * r + 1)

	  This can be proven as follows: 

	  Assume that there does not exist any integer j such that j^(2^(-F)) = m
	  but there exists an odd integer n such that m^(2^F * n) = k. 
	  The fact that there is no such j such that j^(2^(-F)) = m implies that there 
	  exists an i such that the valuation a_i of a prime factor p_i of m is not 
	  dividable by 2^(-F). Since n is odd, n * a_i is not dividable by 2^(-F) and yet 
	  since there is k such that (m^n)^(2^F) = k, all valuations of prime factors of 
	  m^n, hence also n * a_i, are dividable by 2^(-F). Thus contradiction.
	  
	  Testing j^n = (2 * r + 1) reduces to testing m^(2^F * n) = (2 * r + 1) 
	  with F positive or zero.

	  So we must test for F < 0 if there is a j such that j^(2^(-F)) = m 
	  
	  We factorize this code in the function isPowerSquare which checks if
	  there is such a j and returns it as the case may be.

	  We replace then m by j and F by 0 and check the case 

	  m^(2^F * n) = (2 * r + 1) 

	  for F >= 0 as explained above. 

	  Since m is equal or greater to 3, j^(2^(-F)) is equal or greater to 3
	  and j is therefore different from 1. Since m is odd, j is odd. 
	  Therefore j is lower bounded by 3. Trivially, j is upper bounded by 2^(53).
	  
	  So, after the replacement, we can do the check for F >= 0 as explained above.


  */

  if (F < 0) {
    isPowerSquareCase = isPowerSquare(&j,F,m);

    if (!isPowerSquareCase) {
      /* There exists no j integer such that 

         j^(2^(-F)) = m 
	 
	 Thus, as per the proof given above, we know that 
	 
	 m^(2^F * n) cannot be exact.

      */
      return 0;
    }

    /* Here, we have j such that j^(2^(-F)) = m 

       So, for checking 

       m^(2^F * n) = 2 * r + 1

       we replace

       m' = j 
       F' = 0

       and check 

       m'^(2^F' * n) = 2 * r + 1

    */

    m = j;
    F = 0;
    
  }

  /* If we are here, we have 
  
     F >= 0, 2^F * n <= 2^(53) 
     m odd, m >= 3

     We must check 

     m^(2^F * n) = 2 * r + 1

     We check first that 2^F * n <= 35. 
     
     Then we apply the algorithm given above. All the Dekker operations
     are exact because all checks before bound the results by 2^(53) << 2^(1024)

     We construct t = 2^F * n as a double precision variable.
     We check then and if the check does not fail, produce an integer
     variable tt = t. This operation is errorfree because 2^F * n is integer and 
     less or equal to 35. We count then on this integer variable.

     We can construct 2^F * n without overflow in any case.
     If F has not been reduced because it had been negative, 2^F * n is 
     equal to y which is representable. If F is result of this
     reduction, it is equal to 0. The integer n is never equal to 0.
  */

  tdb.d = n;
  tdb.i[HI] += F << 20;

  if (tdb.d > 35.0) {
    /* 2^F * n is greater than 35 
       There can therefore not be any exact case as per the explications above.
    */
    return 0;
  }

  /* If we are here, tdb.d = 2^F * n is less or equal to 35 and integer 
     by construction. We convert it to an integer variable.
  */

  tt = tdb.d;

  /* The loop will go to tt-2 */
  tt -= 2;

  /* Initialize z with m */
  z = m;

  /* Loop for multiplying */
  while (tt > 0) {
    /* Multiply the accumulator z exactly by m */
    Mul12(&ph,&pl,z,m);
   
    /* Check if the result can be represented on at most 53 bits. */
    if (pl != 0.0) {
      /* The result cannot be represented on at most 53 bits.
	 Therefore, as per the arguments given above, the case is 
	 not exact.
      */
      return 0;
    }

    /* Here, pl == 0. We make no rounding on setting z = ph + pl by setting z = ph. */

    z = ph;
    tt--;
  }
  
  /* If we are here, we have 

     z = m^(2^F * n - 1) 
  
     We multiply once again exactly by m in order to obtain

     zh + zl = m^(2^F * n)

  */

  Mul12(&zh,&zl,z,m);
  
  /* We have to check now:

     zh + zl = 2 * r + 1

     We produce the double-double 

     qh + ql = 2 * r + 1

     Since the representation of numbers as normalized double-doubles
     is unique, we can can lexicographically compare zh + zl and qh + ql.
     
     Since r is integer and less than 2^(53), the arithmetical
     multiplication 2.0 * r is exact. For r >= 1, 2 * r is greater than 1.
     So the Add12 can be used in this case. For r = 0, 2 * r is 0 and 
     the Add12 is exact, too.

  */

  Add12(qh,ql,2.0 * r,1.0);

  if (zh != qh) {
    /* The high order word of zh + zl and qh + ql is different.
       The value zh + zl is therefore different from qh + ql. 
       The case is thus inexact.
    */
    return 0;
  }

  /* If we are here, zh = qh, check now the lower words */

  if (ql != ql) {
    /* The low order word of zh + zl and qh + ql is different.
       The value zh + zl is therefore different from qh + ql. 
       The case is thus inexact.
    */
    return 0;
  }

  /* If we are here, zh + zl = qh + ql and therefore

     m^(2^F * n) = 2 * r + 1

     and 

     E * 2^F * n = G
     
     Hence
     
     2^(E * 2^F * n) * m^(2^F * n) = 2^G * (2 * r + 1)
     
     i.e. the case is exact

  */

  return 1;
}



/*************************************************************
 *************************************************************
 *               ROUNDED  TO NEAREST			     *
 *************************************************************
 *************************************************************/

double pow_rn(double x, double y) {
  double sign, absy, t1, t3, t4, r;
  double logxh, logxl, ylogxh, ylogxl, powh, powl;
  int E;
  db_number absydb, t2db, tempdb;
  double res, delta, miulp, resScaled;
  double tt1, tt2, tt3, tt4, tt5;
  double roundingBound, tmp1, correctedRes;
  int exactCase;

#if defined(CRLIBM_TYPECPU_AMD64) || defined(CRLIBM_TYPECPU_X86) 
  db_number t2pdb, temp2db;
#endif

  /* Handle known special cases */
  if (x == 1.0) return 1.0;
  if (y == 0.0) return 1.0;
  if (x == 0.0) return 0.0;
  if (y == 1.0) return x;
  if (y == 2.0) return x * x;
  if (y == -1.0) return 1 / x;

  /* WE DO NOT HANDLE INF, NAN, OVERFLOW AND UNDERFLOW BY NOW (OR NOT COMPLETELY) */

  /* Handle the sign of x */
  sign = 1.0;
  if (x < 0.0) {
    /* x is negative
       Return (-1)^abs(y) * (-x)^y if abs(y) is integer, NaN otherwise
       We can always strip off the sign of x 
    */
    x = -x;

    /* Test if abs(y) is integer */
    absy = ABS(y);
    
    /* If abs(y) is less than 1.0, it cannot be integer; 0.0 has been filtered out */
    if (absy < 1.0) {
      /* return NaN */
      return (x-x)/0.0;
    }
    
    /* If abs(y) is greater or equal to 2^52, abs(y) is always integer */

    if (absy >= TWO52) {
      /* Here we are always integer 
	 So we set sign to -1.0 if abs(y) is odd and to 1.0 if abs(y) is even (default)
      */
      
      /* If abs(y) is greater or equal to 2^53, abs(y) is always even */
      if (absy < TWO53) {
	/* In this case, the exponent of abs(y) is fixed to 52
	   abs(y) is even if its last mantissa bit is 0 
	*/
	absydb.d = absy;
	if ((absydb.i[LO] & 0x1) == 1) {
	  /* abs(y) is integer and odd */
	  sign = -1.0;
	}
      }
    } else {
      /* Here, we have to check whether abs(y) is integer 
	 We compute the integer nearest to abs(y) and 
	 subtract using Sterbenz' lemma for obtaining the rest 
      */

      /* Multiply abs(y) by 2^(-1074) in two steps
	 for obtaining a subnormal (nearest to 2^(-1074) * absy)
	 Multiply than by 2^(1074) in two steps for
	 obtaining the integer nearest to abs(y)
      */
      
      t1 = TWOM53 * absy;
      t2db.d = TWOM1021 * t1;
      
      /* If we are on x86, we must force the compile to go through memory in order to have 
	 correct double subnormals 
      */
#if defined(CRLIBM_TYPECPU_AMD64) || defined(CRLIBM_TYPECPU_X86) 
      t2pdb.i[HI] = t2db.i[HI];
      t2pdb.i[LO] = t2db.i[LO];
      t2db.d = t2pdb.d;
#endif

      t3 = TWO1021 * t2db.d;
      t4 = TWO53 * t3;

      /* Compute now the rest */
      r = absy - t4;

      if (r == 0.0) {
	/* abs(y) is integer 
	   It is odd if the last bit of the subnormal t2db.d is 1
	   If it is even, sign must be 1.0 which is the default
	*/
	if ((t2db.i[LO] & 0x1) == 1) {
	  /* abs(y) is integer and odd */
	  sign = -1.0;
	}
      } else {
	/* abs(y) is not integer, return NaN */
	return (x-x)/0.0;
      }
    }
  }

  /* x is now always positive */

  /* Compute log2(x) as a double-double */

  log2_12(&logxh, &logxl, x);

  /* Compute y * log2(x) as a double-double */

  /* ATTENTION: y might be a subnormal or so small that 
     the Mul122 operator is no longer correct 
     We have to check this in the future (TODO)
  */

  Mul122(&ylogxh,&ylogxl,y,logxh,logxl); 

  /* Compute 2^(y * log2(x)) as a double-double and an exponent */

  exp2_12(&E,&powh,&powl,ylogxh,ylogxl);


  /* Final overflow and underflow handling
     Rounding test
     Rounding to subnormals
     Filter for exact result values
  */

  if (E >= 1025) {
    /* Overflow, return Inf 
       The formula is for setting the inexact flag
    */
    return LARGEST * (1.0 + SMALLEST);
  }
  
  if (E <= -1076) {
    /* Surely flushed to zero 
       The formula is for setting the inexact flag
    */
    return SMALLEST * SMALLEST;
  }

  /* Tentative final rounding
     We round the intermediate result correctly to a double 
     We compute further the rounding rest and an half-ulp
     on the side of the rest. These values will allow then 
     to validate or invalidate the tentative result.

     If no subnormal rounding occurs, the procedure is 
     simpler. The sequence for subnormal rounding is
     capable of handling also the case where no subnormal 
     is produced. We can therefore branch on a fuzzy bound
     for performance reasons.
  */

  if (E < -1021) {
    /* Possible subnormal rounding 

       We multiply powh by 2^E using two real multiplications This
       result may produce a subnormal so a rounding may occur.  We
       remultiply then the value obtained by 2^(-E) using two real
       multiplications. We compute then the rounding error of the
       first multiplication series producing possibly a subnormal.
       The value delta will be the absolute value of the arithmetic
       sum of this error and powl.  We compute miulp by computing the
       ulp of the produced subnormal (or normal) in the direction 
       of powl. We rescale this ulp by 2^(-E-1) similar to the way
       of the rounding error in order to obtain miulp.

       On x86 architectures we must force each operation producing
       subnormals to really do so.
    */

    tt1 = powh * TWOM1000;
    
    tempdb.i[HI] = (E + 2023) << 20;
    tempdb.i[LO] = 0;
    tempdb.d *= tt1;
#if defined(CRLIBM_TYPECPU_AMD64) || defined(CRLIBM_TYPECPU_X86) 
    temp2db.i[HI] = tempdb.i[HI];
    temp2db.i[LO] = tempdb.i[LO];
    tempdb.d = temp2db.d;
#endif

    res = tempdb.d;
    if (powl < 0.0) tempdb.l--; else tempdb.l++;

    tt2 = tempdb.d - res;

    tempdb.i[HI] = (-E + 23) << 20;
    tempdb.i[LO] = 0;
    
    tt3 = tempdb.d * res;
    tt4 = tempdb.d * tt2;

    resScaled = tt3 * TWO1000;
    miulp = ABS(tt4 * TWO999);
    
    tt5 = powh - resScaled;
    delta = tt5 + powl;
    
  } else {
    /* Normal rounding, no overflow, no underflow 
     
       res = 2^E * powh

       It is possible that E = 1024 and the multiplicand is less than 1
       So we will do the multiplication by the power of 2 by bit
       manipulation.

       delta = abs(powl)
       miulp = 1/2*ulp+/-(powh)

    */
    tempdb.d = powh;
    tempdb.i[HI] += E << 20;
    res = tempdb.d;

    delta = powl;
    resScaled = powh;

    tempdb.d = powh;
    if (powl < 0.0) tempdb.l--; else tempdb.l++;
    miulp = ABS(0.5 * (tempdb.d - powh));
  }

  /* Rounding test and checking for possible exact cases 

     We have mainly two cases to check

     (i)  The rounding rest delta is nearer to 1/2*ulp(res) 
          than the static accuracy bound for the approximation.
	  In this case, we have two possibilities:
	  (a) The case is an exact half-way case and we can round
	  (b) The case is not exact and we must launch a more accurate phase
     (ii) The rounding rest delta is less than the static accuracy bound 
          for the approximation. We have the following possibilities
	  (a) The case is an exact case and we can round and not set the inexact flag
	  (b) The case is not an exact case and we can round and set the inexact flag

     We prepare first roundingBound, a value reflecting the static
     bound for the approximation error.  If we have b correct bits, we
     multiply miulp by 2^(-(b - 54)). This constant APPROXBOUNDFACTOR
     is produced by a Maple script.
     
     We check then case (i) by subtracting miulp from delta and comparing to roundingBound.
     We check then case (ii) by comparing delta directly to roundingBound.

     Before launching the exact case test, we have to round the result for x^y to 54 bits,
     i.e. bring delta to a standard form (0.11111... -> 1.00000...; 0.000101... -> 0.00000...).

  */

  roundingBound = APPROXBOUNDFACTOR * miulp;

  tmp1 = miulp - ABS(delta);
  if (tmp1 <= roundingBound) {
    /* Case (i) detected 

       Bring delta to a standard form, i.e. set it to sgn(delta) * miulp;
       Attention: the double-double resScaled + delta is not normalized w.r.t. even rounding

    */
        
    if (delta < 0.0) delta = -miulp; else delta = miulp;
    exactCase = checkForExactCase(x,y,E,resScaled,delta);

    if (exactCase) {
      /* If we are here, we know that x^y = 2^E * (resScaled + delta) exactly 

         Since the double-double resScaled + delta is not normalized
	 w.r.t. even rounding, we must reperform the final
	 rounding. We first add resScaled and delta arithmetically to
	 obtain correctedRes. This operation rounds correctly
	 resScaled to the nearest even if we have a final normal
	 result and is exact (TODO: paper proof) if the final result
	 is subnormal.  We multiply then by 2^E by two different
	 methods depending on E like above. This operation is exact if
	 the final result is a normal or rounds correctly if the
	 result is a subnormal.

      */

      correctedRes = resScaled + delta;

      if (E < -1021) {
	/* Possible subnormal rounding */

	tt1 = correctedRes * TWOM1000;
	
	tempdb.i[HI] = (E + 2023) << 20;
	tempdb.i[LO] = 0;
	tempdb.d *= tt1;
#if defined(CRLIBM_TYPECPU_AMD64) || defined(CRLIBM_TYPECPU_X86) 
	temp2db.i[HI] = tempdb.i[HI];
	temp2db.i[LO] = tempdb.i[LO];
	tempdb.d = temp2db.d;
#endif
	
	res = tempdb.d;

      } else {
	/* Normal rounding */
	tempdb.d = correctedRes;
	tempdb.i[HI] += E << 20;
	res = tempdb.d;
      }
      return sign * res;
    }
  } else {
    /* Case (i) not detected, check now for case (ii) */

    if (ABS(delta) <= roundingBound) {
      /* Case (ii) detected, bring delta to a standard form by ignoring it 
	 The double-double resScaled + 0.0 stays normalized.
      */

      exactCase = checkForExactCase(x,y,E,resScaled,0.0);

      if (exactCase) {
	/* Exact case
	   TODO: restore the inexact flag to the status it had when entering the function 
	*/
	return sign * res;
      } else {
	/* Inexact but roundable case 
	   TODO: correct handling of the inexact flag
	*/
	return sign * res;
      }
    } else {
      /* Neither case (i) nor case (ii) could be detected
	 We can decide the rounding and are inexact.

	 TODO: correct handling of the inexact flag

      */
      return sign * res;
    }
  }

  /* If we are here, we could not decide the rounding */

  /*  printf("We could not decide the rounding nor detect an exact case\n"); */

  return sign * res;
}
