#include <stdio.h>
#include <stdlib.h>
#include <crlibm.h>
#include <crlibm_private.h>
#include "trigo_fast.h"
#include "coefpi2.h"


extern double scs_sin_rn(double);
extern double scs_sin_ru(double);
extern double scs_sin_rd(double);
extern double scs_sin_rz(double);
extern double scs_cos_rn(double);
extern double scs_cos_ru(double);
extern double scs_cos_rd(double);
extern double scs_cos_rz(double);
extern double scs_tan_rn(double); 
extern double scs_tan_rd(double);  
extern double scs_tan_ru(double);  
extern double scs_tan_rz(double);  



/* 

How these functions work:

The trig range reduction in crlibm computes an integer k and a reduced
argument y such that

x = k.Pi/256 + y

with the reduced argument y directly in -Pi/512, Pi/512.  
(Pi/512 < 4/512 = 2^-7)
y is computed as a double-double yh+yl

Then we read off a table 

  sah+sal ~ sin(kPi/256)
  cah+cal ~ cos(kPi/256)

and we use the reconstruction 

  sin(kPi/256 + y) = sin(kPi/256)cos(y) + cos(kPi/256)sin(y)
  cos(kPi/256 + y) = cos(kPi/256)cos(y) - sin(kPi/256)sin(y)

where cos(y) and sin(y) are computed as unevaluated 1+tc and (yh+yl)(1+ts)
respectively, where tc and ts are doubles resulting from a small
polynomial approximation.
This gives 14 extra bits of accuracy, so this first step is very accurate.


Why not use accurate tables as defined by Gal ?

In short, because Gal's fast approach only gives as many additiona bits 
as you've got to address the table (so we are limited to 7 if we limit 
the table size to 4KB), and we need more to have a good average performance. 
From a performance point of view we probably lose a few cycles: There
is 4 values to read in our scheme compared to 3 in Gal's method. The
reconstruction costs a few floating-point operations more (not that
many, if you look in details and want to ensure more than 7 extra
bits).
 
Now for the advantages:
1/ The whole thing is simpler
2/ We have much more accuracy in the table, which simplifies the proof.  
3/ We will be able to reuse the same table values to speed up the
second step (just tabulating a third double such that the three-double
approx of sin/cos(kPi/256) will be summed exactly into an SCS number)



Now a word on range reduction:

We have 4 possible range reductions: 

Cody and Waite with 2 constants (the fastest)
Cody and Waite with 3 constants (almost as fast)
Cody and Waite with 3 constants in double-double and k a long-long int
Payne and Hanek, implemented in SCS (the slowest).

Each of these range reductions except Payne and Hanek is valid for x
smaller than some bound. 

This range reduction may cancel up to 62 bits according to a program
by Kahan/Douglas available in Muller's book and implemented as
function WorstCaseForAdditiveRangeReduction in common-procedures.mpl
However this is not a concern unless x is close to a multiple of Pi/2
(that is k&127==0): in the general case the reconstruction will add a
tabulated non-zero value, so the error to consider in the range
reduction is the absolute error. Only in the cases when k&127==0 do we
need to have 62 extra bits to compute with. This is ensured by using a
slower, more accurate range reduction. This test for k&127==0 actually
speeds up even these cases, because in these cases there is no table
to read and no reconstruction to do : a simple approximation to the
function suffices.


Why not use Payne and Hanek only as in Markstein's book ?  Because
our scheme, in the absence of FMA, is much faster for small values
which are the most used.

Markstein takes as reduced argument the fractional part of x*256/Pi, 
(or maybe it's 512 in his case), so he's got the same tables as we have, 
but different polynomials (which compute sin(2Pi*y) and cos(2Pi*y).





 */




#define DEBUG 0
/* TODO: 


 - In some Cody and Waite there are Mul12 involving k, CH and CM. They
	 can be improved by pre-splitting CH, CM (tabulated values)
	 and k (as an int) Then you can improve the precision by
	 taking kmax into account

 - The first coefficient of the cosine polynomial is equal exactly
   to 1/2 and this should be modified in order to increase to accuracy
   of the approximation.

 - The second step should get the reduced argument from the first step
   (and use the same argument reduction). This should lead to 5x
   improvement of the worst case.

 - in the tangent there are three steps. This could be studied for the
   other functions
*/


int rem_pio256_scs(scs_ptr result, const scs_ptr x){
  unsigned long long int r[SCS_NB_WORDS+3], tmp;
  unsigned int N;

  /* result r[0],...,r[10] could store till 300 bits of precision */

  /* that is really enough for computing the reduced argument */
  int sign, i, j, ind;
  int *digits_256_over_pi_pt;

  if ((X_EXP != 1)||(X_IND < -2)){
    scs_set(result, x);
    return 0;
  }
  
  /* Compute the product |x| * 256/Pi */
  if ((X_IND == -2)){
    r[0] =  0;    r[1] =  0;
    r[2] =  (unsigned long long int)(digits_256_over_pi[0]) * X_HW[0];
    r[3] = ((unsigned long long int)(digits_256_over_pi[0]) * X_HW[1]
	   +(unsigned long long int)(digits_256_over_pi[1]) * X_HW[0]);
    if(X_HW[2] == 0){
      for(i=4; i<(SCS_NB_WORDS+3); i++){   
	r[i] = ((unsigned long long int)(digits_256_over_pi[i-3]) * X_HW[1]
	       +(unsigned long long int)(digits_256_over_pi[i-2]) * X_HW[0]);
      }}else {
	for(i=4; i<(SCS_NB_WORDS+3); i++){   
	  r[i] = ((unsigned long long int)(digits_256_over_pi[i-4]) * X_HW[2]
		 +(unsigned long long int)(digits_256_over_pi[i-3]) * X_HW[1]
		 +(unsigned long long int)(digits_256_over_pi[i-2]) * X_HW[0]);
	}
      }
  }else {
    if (X_IND == -1){
      r[0] =  0;
      r[1] =  (unsigned long long int)(digits_256_over_pi[0]) * X_HW[0];
      r[2] = ((unsigned long long int)(digits_256_over_pi[0]) * X_HW[1]
	     +(unsigned long long int)(digits_256_over_pi[1]) * X_HW[0]);
      if(X_HW[2] == 0){
	for(i=3; i<(SCS_NB_WORDS+3); i++){   
	  r[i] = ((unsigned long long int)(digits_256_over_pi[i-2]) * X_HW[1]
		 +(unsigned long long int)(digits_256_over_pi[i-1]) * X_HW[0]);
	}}else {
	  for(i=3; i<(SCS_NB_WORDS+3); i++){   
	    r[i] = ((unsigned long long int)(digits_256_over_pi[i-3]) * X_HW[2]
		   +(unsigned long long int)(digits_256_over_pi[i-2]) * X_HW[1]
		   +(unsigned long long int)(digits_256_over_pi[i-1]) * X_HW[0]);
	  }}
    }else {
      if (X_IND == 0){
	r[0] =  (unsigned long long int)(digits_256_over_pi[0]) * X_HW[0];
	r[1] = ((unsigned long long int)(digits_256_over_pi[0]) * X_HW[1]
	       +(unsigned long long int)(digits_256_over_pi[1]) * X_HW[0]);
	if(X_HW[2] == 0){
	  for(i=2; i<(SCS_NB_WORDS+3); i++){   
	    r[i] = ((unsigned long long int)(digits_256_over_pi[i-1]) * X_HW[1]
		   +(unsigned long long int)(digits_256_over_pi[ i ]) * X_HW[0]);
	  }}else {
	    for(i=2; i<(SCS_NB_WORDS+3); i++){   
	      r[i] = ((unsigned long long int)(digits_256_over_pi[i-2]) * X_HW[2]
		     +(unsigned long long int)(digits_256_over_pi[i-1]) * X_HW[1]
		     +(unsigned long long int)(digits_256_over_pi[ i ]) * X_HW[0]);
	    }}
      }else {
	if (X_IND == 1){
  	  r[0] = ((unsigned long long int)(digits_256_over_pi[0]) * X_HW[1]
		 +(unsigned long long int)(digits_256_over_pi[1]) * X_HW[0]);
	  if(X_HW[2] == 0){
	    for(i=1; i<(SCS_NB_WORDS+3); i++){   
	      r[i] = ((unsigned long long int)(digits_256_over_pi[ i ]) * X_HW[1]
		     +(unsigned long long int)(digits_256_over_pi[i+1]) * X_HW[0]);
	    }}else {
	      for(i=1; i<(SCS_NB_WORDS+3); i++){   
		r[i] = ((unsigned long long int)(digits_256_over_pi[i-1]) * X_HW[2]
		       +(unsigned long long int)(digits_256_over_pi[ i ]) * X_HW[1]
		       +(unsigned long long int)(digits_256_over_pi[i+1]) * X_HW[0]);
	      }}
	}else {
	  ind = (X_IND - 2);
	  digits_256_over_pi_pt = (int*)&(digits_256_over_pi[ind]);
	  if(X_HW[2] == 0){
	    for(i=0; i<(SCS_NB_WORDS+3); i++){   
	      r[i] = ((unsigned long long int)(digits_256_over_pi_pt[i+1]) * X_HW[1]
		     +(unsigned long long int)(digits_256_over_pi_pt[i+2]) * X_HW[0]);
	    }}else {
	      for(i=0; i<(SCS_NB_WORDS+3); i++){   
		r[i] = ((unsigned long long int)(digits_256_over_pi_pt[ i ]) * X_HW[2]
		       +(unsigned long long int)(digits_256_over_pi_pt[i+1]) * X_HW[1]
		       +(unsigned long long int)(digits_256_over_pi_pt[i+2]) * X_HW[0]);
	      }
	    }
	}
      }
    }
  }
      
  /* Carry propagate */
  r[SCS_NB_WORDS+1] += r[SCS_NB_WORDS+2]>>30;
  for(i=(SCS_NB_WORDS+1); i>0; i--) {tmp=r[i]>>30;   r[i-1] += tmp;  r[i] -= (tmp<<30);}  
      
  /* The integer part is in r[0] */
  N = r[0];
#if 0
  printf("r[0] = %d\n", N);
#endif



  if (r[1] > (SCS_RADIX)/2){	/* test if the reduced part is bigger than Pi/4 */
    N += 1;
    sign = -1;
    for(i=1; i<(SCS_NB_WORDS+3); i++) { r[i]=((~(unsigned int)(r[i])) & 0x3fffffff);}
  } 
  else
    sign = 1; 


  /* Now we get the reduce argument and check for possible
   * cancellation By Kahan algorithm we will have at most 2 digits
   * of cancellations r[1] and r[2] in the worst case.
   */    
  if (r[1] == 0)
    if (r[2] == 0) i = 3;
    else           i = 2;
  else             i = 1;

  for(j=0; j<SCS_NB_WORDS; j++) { R_HW[j] = r[i+j];}


  R_EXP   = 1;
  R_IND   = -i;
  R_SGN   = sign*X_SGN; 
  
  /* Last step :
   *   Multiplication by pi/2
   */
  scs_mul(result, Pio256_ptr, result);
  return N*X_SGN;
}
 


#define LoadTableSinCos()                                 \
do  {                                                       \
    if(index<=(64<<2)) {                                    \
      sah=sincosTable[index+0].d; /* sin(a), high part */   \
      sal=sincosTable[index+1].d; /* sin(a), low part  */   \
      cah=sincosTable[index+2].d; /* cos(a), high part */   \
      cal=sincosTable[index+3].d; /* cos(a), low part  */   \
    }else { /* cah <= sah */                                \
      index=(128<<2) - index;                               \
      cah=sincosTable[index+0].d; /* cos(a), high part */   \
      cal=sincosTable[index+1].d; /* cos(a), low part  */   \
      sah=sincosTable[index+2].d; /* sin(a), high part */   \
      sal=sincosTable[index+3].d; /* sin(a), low part  */   \
    }                                                       \
  } while(0)


#define DoSinZero(psh,psl)                     \
do{                                                \
  yh2 = yh*yh ;                                    \
  ts = yh2 * (s3.d + yh2*(s5.d + yh2*s7.d));	   \
  /* (1+ts)*(yh+yl) is an approx to sin(yh+yl) */  \
  /* Now compute (1+ts)*(yh+yl) */                 \
  Add12(*psh,*psl,   yh, yl+ts*yh);	           \
} while(0)						   

#define DoSinNotZero(psh,psl)                                      \
do {                                                                   \
  double thi, tlo, cahyh_h, cahyh_l  ;          		       \
  yh2 = yh*yh ;                                                        \
  Mul12(&cahyh_h,&cahyh_l, cah, yh);				       \
  Add12(thi, tlo, sah,cahyh_h);					       \
  ts = yh2 * (s3.d + yh2*(s5.d + yh2*s7.d));			       \
  /* (1+ts)*(yh+yl) is an approx to sin(yh+yl) */		       \
  tc = yh2 * (c2.d + yh2*(c4.d + yh2*c6.d ));			       \
  /* 1+ tc is an approx to cos(yh+yl) */			       \
  tlo = tc*sah+(ts*cahyh_h+(sal+(tlo+(cahyh_l+(cal*yh + cah*yl))))) ;  \
  Add12(*psh,*psl,  thi, tlo);	   			               \
} while(0)
 
#define DoCosZero(pch,pcl)                        \
do {                                              \
  yh2 = yh*yh ;                                   \
  tc = yh2 * (c2.d + yh2*(c4.d + yh2*c6.d ));	  \
  /* 1+ tc is an approx to cos(yh+yl) */	  \
  /* Now compute 1+tc */			  \
  Add12(*pch,*pcl, 1., tc);		          \
} while(0)					  

#define DoCosNotZero(pch,pcl)                                       \
do {                                                                \
  double thi, tlo, sahyh_h,sahyh_l;      			    \
  yh2 = yh*yh ;                                                     \
  Mul12(&sahyh_h,&sahyh_l, sah, yh);			            \
  ts = yh2 * (s3.d + yh2*(s5.d + yh2*s7.d));		            \
  /* (1+ts)*(yh+yl) is an approx to sin(yh+yl) */	            \
  tc = yh2 * (c2.d + yh2*(c4.d + yh2*(c6.d)));		            \
  /* 1+ tc is an approx to cos(yh+yl) */		            \
  Add12(thi, tlo,  cah, -sahyh_h);			            \
  tlo = tc*cah-(ts*sahyh_h-(cal+(tlo-(sahyh_l+(sal*yh+sah*yl))))) ; \
  Add12(*pch, *pcl,    thi, tlo);                                   \
} while(0)





/************************************************************************/
/*                                                                      */
/*                       Argument Reduction                             */
/*                                                                      */
/************************************************************************/


#define SIN 0
#define COS 1
#define TAN 2


#define RangeReductionSCS()                                   \
do { 								\
  scs_t X, Y,Yh,Yl;						\
      scs_set_d(X, rri->x); 			  		        \
      k= rem_pio256_scs(Y, X);					\
      index=(k&127)<<2;                                         \
      quadrant = (k>>7)&3;                                      \
      /* TODO an optimized procedure for the following */	\
      scs_get_d(&yh, Y);					\
      scs_set_d(Yh, yh);					\
      scs_sub(Yl, Y,Yh);					\
      scs_get_d(&yl, Yl);					\
}while(0)



/* A structure that holds all the information to be exchanged between
   ComputeTrigWithArgred2 and the 12 functions sin_rn etc

   It is purely for performance (almost 100 cycles out of 300 on a P4
   when compared to passing a list of arguments). Instead of saving a
   few memory accesses, it it allows other small optimizations like
   deferring the possible change of sign of the result to the the last
   moment using rri->changesign.

   All this is not very elegant, but it is safe.
*/

struct rrinfo_s {double rh; double rl; double x; int absxhi; int function;} ;
typedef struct rrinfo_s rrinfo;
#define changesign function



static void ComputeTrigWithArgred2(rrinfo *rri){ 
  double sah,sal,cah,cal, yh, yl, yh2, ts,tc, kd; 
  double kch_h,kch_l, kcm_h,kcm_l, th, tl,sh,sl,ch,cl;
  int k, quadrant, index;
  long long int kl;

  if  (rri->absxhi < XMAX_CODY_WAITE_3) {
    /* Compute k, deduce the table index and the quadrant */
    DOUBLE2INT(k, rri->x * INV_PIO256);
    kd = (double) k;
    quadrant = (k>>7)&3;      
    index=(k&127)<<2;
    if((index == 0)) { 
      /* Here a large cancellation on yh+yl would be a problem, so use double-double RR */
      /* all this is exact */
      Mul12(&kch_h, &kch_l,   kd, RR_DD_MCH);
      Mul12(&kcm_h, &kcm_l,   kd, RR_DD_MCM);
      Add12 (th,tl,  kch_l, kcm_h) ;
      /* only rounding error in the last multiplication and addition */ 
      Add22 (&yh, &yl,    (rri->x + kch_h) , (kcm_l - kd*RR_DD_CL),   th, tl) ;
      goto computeZero;
    } 
    else {      
      /* index <> 0, don't worry about cancellations on yh+yl */
      if (rri->absxhi < XMAX_CODY_WAITE_2) {
	/* CW 2: all this is exact but the rightmost multiplication */
	Add12 (yh,yl,  (rri->x - kd*RR_CW2_CH),  (kd*RR_CW2_MCL) ) ; 
      }
      else { 
	/* CW 3: all this is exact but the rightmost multiplication */
	Add12Cond(yh,yl,  (rri->x - kd*RR_CW3_CH) -  kd*RR_CW3_CM,   kd*RR_CW3_MCL);
      }
    }
    goto computeNotZero;
  }

  else if ( rri->absxhi < XMAX_DDRR ) {
    /* x sufficiently small for a Cody and Waite in double-double */
    DOUBLE2LONGINT(kl, rri->x*INV_PIO256);
    kd=(double)kl;
    quadrant = (kl>>7)&3;
    index=(kl&127)<<2;
    if(index == 0) { 
      /* Here again a large cancellation on yh+yl would be a problem, 
	 so we do the accurate range reduction */
      RangeReductionSCS();   /*recomputes k, index, quadrant, and yh and yl*/
      /* Now it may happen that the new k differs by 1 of kl, so check that */
      if(index==0)   /* no surprise */
	goto computeZero; 
      else 
	goto computeNotZero;
    }
    else {   /*  index<>0 */
      /* double-double argument reduction*/
      /* all this is exact */
      Mul12(&kch_h, &kch_l,   kd, RR_DD_MCH);
      Mul12(&kcm_h, &kcm_l,   kd, RR_DD_MCM);
      Add12 (th,tl,  kch_l, kcm_h) ;
      /* only rounding error in the last multiplication and addition */ 
      Add22 (&yh, &yl,    (rri->x + kch_h) , (kcm_l - kd*RR_DD_CL),   th, tl) ;
      goto computeNotZero;
    }
  } /* closes if ( absxhi < XMAX_DDRR ) */ 

  else {
    /* Worst case : x very large, sin(x) probably meaningless, we return
       correct rounding but do't mind taking time for it */
    RangeReductionSCS(); 
    quadrant = (k>>7)&3;                                       
    if(index == 0)
      goto computeZero;
    else 
      goto computeNotZero;
  }

 computeZero:
  switch(rri->function) {
 
  case SIN: 
    if (quadrant&1)
      DoCosZero(&rri->rh, &rri->rl);
    else 
      DoSinZero(&rri->rh, &rri->rl);
    rri->changesign=(quadrant==2)||(quadrant==3);
    return;
    
  case COS: 
    if (quadrant&1)
      DoSinZero(&rri->rh, &rri->rl);
    else 
      DoCosZero(&rri->rh, &rri->rl);
    rri->changesign= (quadrant==1)||(quadrant==2);
    return;

  case TAN: 
    if (quadrant&1) {
      DoSinZero(&ch, &cl);
      DoCosZero(&sh, &sl);
      sh=-sh; sl=-sl;
    } else {
      DoSinZero(&sh, &sl);
      DoCosZero(&ch, &cl);
    }
    Div22(&rri->rh, &rri->rl, sh, sl, ch, cl);
    return;
  }
  
 computeNotZero:
  LoadTableSinCos();
  switch(rri->function) {

  case SIN: 
    if (quadrant&1)   
      DoCosNotZero(&rri->rh, &rri->rl);
    else 
      DoSinNotZero(&rri->rh, &rri->rl);
    rri->changesign=(quadrant==2)||(quadrant==3);
    return;

  case COS: 
    if (quadrant&1)   
      DoSinNotZero(&rri->rh, &rri->rl);
    else 
      DoCosNotZero(&rri->rh, &rri->rl);
    rri->changesign=(quadrant==1)||(quadrant==2);
    return;

  case TAN: 
      /* Remark: the computation of ts and tc is done twice but the
	 compiler seems to remove the redundant one (as long as DoSin and
	 DoCos are #defines anyway) */
    if (quadrant&1) {
      DoSinNotZero(&ch, &cl);
      DoCosNotZero(&sh, &sl);
      sh=-sh; sl=-sl;
    } else {
      DoSinNotZero(&sh, &sl);
      DoCosNotZero(&ch, &cl);
    }
    Div22(&rri->rh, &rri->rl, sh, sl, ch, cl);
    return;
  }
}


/*************************************************************
 *************************************************************
 *              SIN ROUNDED  TO NEAREST			     *
 *************************************************************
 *************************************************************/ 

double sin_rn(double x){ 
  double ts,x2,rncst; 
  rrinfo rri;
  db_number x_split;
  
  x_split.d=x;
  rri.absxhi = x_split.i[HI_ENDIAN] & 0x7fffffff;
  
  /* SPECIAL CASES: x=(Nan, Inf) sin(x)=Nan */
  if (rri.absxhi>=0x7ff00000) return x-x;    
   
  else if (rri.absxhi < XMAX_SIN_CASE2){
    /* CASE 1 : x small enough sin(x)=x */
    if (rri.absxhi <XMAX_RETURN_X_FOR_SIN)
      return x;
    
    /* CASE 2 :XMAX_RETURN_X_FOR_SIN x < XMAX_SIN_CASE2
       Fast polynomial evaluation as in DoSinZero */
    x2 = x*x ;
    ts = x2 * (s3.d + x2*(s5.d + x2*s7.d));
    Add12(rri.rh,rri.rl,   x, ts*x);
    if(rri.rh == (rri.rh + (rri.rl * RN_CST_SIN_CASE2)))	
      return rri.rh;
    else
      return scs_sin_rn(x); 
  }
  
  /* CASE 3 : Need argument reduction */ 
  else {
    rri.x=x;
    rri.function=SIN;
    ComputeTrigWithArgred2(&rri);
    rncst= RN_CST_SIN_CASE3;
    if(rri.rh == (rri.rh + (rri.rl * rncst)))	
      if(rri.changesign) return -rri.rh; else return rri.rh;
    else
      return scs_sin_rn(x); 
  }
}






/*************************************************************
 *************************************************************
 *               SIN ROUNDED  TOWARD  +INFINITY              *
 *************************************************************
 *************************************************************/


double sin_ru(double x){
  double xx, ts, epsilon; 
  rrinfo rri;
  db_number x_split,  absyh, absyl, u, u53;
  
  x_split.d=x;
  rri.absxhi = x_split.i[HI_ENDIAN] & 0x7fffffff;
  
  /* SPECIAL CASES: x=(Nan, Inf) sin(x)=Nan */
  if (rri.absxhi>=0x7ff00000) return x-x;    
  
  if (rri.absxhi < XMAX_SIN_CASE2){

    /* CASE 1 : x small enough, return x suitably rounded */
    if (rri.absxhi <XMAX_RETURN_X_FOR_SIN) {
      if(x>=0.)
	return x;
      else {
	x_split.l --;
	return x_split.d;
      }
    }
    else {
      /* CASE 2 : x < Pi/512
	 Fast polynomial evaluation */
      xx = x*x;
      ts = x * xx * (s3.d + xx*(s5.d + xx*s7.d ));
      Add12(rri.rh,rri.rl, x, ts);
      epsilon=EPS_SIN_CASE2; 
    }
  }
  else {
    /* CASE 3 : Need argument reduction */ 
    rri.x=x;
    rri.function=SIN;
    ComputeTrigWithArgred2(&rri);
    epsilon=EPS_SIN_CASE3;
    if(rri.changesign) {
      rri.rh = -rri.rh;
      rri.rl = -rri.rl;
    } 
  }
  /* Rounding test to + infinity */
  absyh.d=rri.rh;
  absyl.d=rri.rl;
  absyh.l = absyh.l & 0x7fffffffffffffffLL;
  absyl.l = absyl.l & 0x7fffffffffffffffLL;
  u53.l     = (absyh.l & 0x7ff0000000000000LL) +  0x0010000000000000LL;
  u.l   = u53.l - 0x0350000000000000LL;
   
  if(absyl.d > epsilon * u53.d){
    if(rri.rl>0)  return rri.rh+u.d;
    else      return rri.rh;
  }
  else return scs_sin_ru(x);
}





/*************************************************************
 *************************************************************
 *               SIN ROUNDED  TOWARD  -INFINITY              *
 *************************************************************
 *************************************************************/
double sin_rd(double x){ 
  double xx, ts, epsilon; 
  db_number x_split,  absyh, absyl, u, u53;
  rrinfo rri;
  
  x_split.d=x;
  rri.absxhi = x_split.i[HI_ENDIAN] & 0x7fffffff;
  
  /* SPECIAL CASES: x=(Nan, Inf) sin(x)=Nan */
  if (rri.absxhi>=0x7ff00000) return x-x;    
  
  if (rri.absxhi < XMAX_SIN_CASE2){

    /* CASE 1 : x small enough, return x suitably rounded */
    if (rri.absxhi <XMAX_RETURN_X_FOR_SIN) {
      if(x<=0.)
	return x;
      else {
	x_split.l --;
	return x_split.d;
      }
    }

    else{
      /* CASE 2 : x < Pi/512
	 Fast polynomial evaluation */
      xx = x*x;
      ts = x * xx * (s3.d + xx*(s5.d + xx*s7.d ));
      Add12(rri.rh,rri.rl, x, ts);
      epsilon=EPS_SIN_CASE2; 
    }
  }
  else {
    /* CASE 3 : Need argument reduction */ 
    rri.x=x;
    rri.function=SIN;
    ComputeTrigWithArgred2(&rri);
    epsilon=EPS_SIN_CASE3;
    if(rri.changesign) {
      rri.rh = -rri.rh;
      rri.rl = -rri.rl;
    } 
  }

  /* Rounding test to + infinity */
  absyh.d=rri.rh;
  absyl.d=rri.rl;
  absyh.l = absyh.l & 0x7fffffffffffffffLL;
  absyl.l = absyl.l & 0x7fffffffffffffffLL;
  u53.l     = (absyh.l & 0x7ff0000000000000LL) +  0x0010000000000000LL;
  u.l   = u53.l - 0x0350000000000000LL;
   
  if(absyl.d > epsilon * u53.d){ 
    if(rri.rl>0)  return rri.rh;
    else      return rri.rh-u.d;
  }
  else return scs_sin_rd(x);
}





/*************************************************************
 *************************************************************
 *               SIN ROUNDED  TOWARD  ZERO                   *
 *************************************************************
 *************************************************************/
double sin_rz(double x){ 
  double xx, ts, epsilon; 
  db_number x_split,  absyh, absyl, u, u53;
  rrinfo rri;  
  x_split.d=x;

  rri.absxhi = x_split.i[HI_ENDIAN] & 0x7fffffff;
  
  /* SPECIAL CASES: x=(Nan, Inf) sin(x)=Nan */
  if (rri.absxhi>=0x7ff00000) return x-x;    
  
  if (rri.absxhi < XMAX_SIN_CASE2){

    /* CASE 1 : x small enough, return x suitably rounded */
    if (rri.absxhi <XMAX_RETURN_X_FOR_SIN) {
      x_split.l --;
      return x_split.d;
    }
    else {
      /* CASE 2 : x < Pi/512
	 Fast polynomial evaluation */
      xx = x*x;
      ts = x * xx * (s3.d + xx*(s5.d + xx*s7.d ));
      Add12(rri.rh,rri.rl, x, ts);
      epsilon=EPS_SIN_CASE2; 
    }
  }
  else {
    /* CASE 3 : Need argument reduction */ 
    rri.x=x;
    rri.function=SIN;
    ComputeTrigWithArgred2(&rri);
    epsilon=EPS_SIN_CASE3;
    if(rri.changesign) {
      rri.rh = -rri.rh;
      rri.rl = -rri.rl;
    } 
  }

  /* Rounding test to + infinity */
  absyh.d=rri.rh;
  absyl.d=rri.rl;
  absyh.l = absyh.l & 0x7fffffffffffffffLL;
  absyl.l = absyl.l & 0x7fffffffffffffffLL;
  u53.l     = (absyh.l & 0x7ff0000000000000LL) +  0x0010000000000000LL;
  u.l   = u53.l - 0x0350000000000000LL;
   
  if(absyl.d > epsilon * u53.d){ 
    if(rri.rh>0) {
      if (rri.rl>0) return rri.rh;
      else      return rri.rh-u.d;
    }
    else {
      if (rri.rl>0) return rri.rh+u.d;
      else      return rri.rh;
    }	
  }
  else return scs_sin_rz(x);
}




/*************************************************************
 *************************************************************
 *              COS ROUNDED  TO NEAREST			     *
 *************************************************************
 *************************************************************/
double cos_rn(double x){ 
  double tc, x2;
  rrinfo rri;
  db_number x_split;

  x_split.d=x;
  rri.absxhi = x_split.i[HI_ENDIAN] & 0x7fffffff;

  /* SPECIAL CASES: x=(Nan, Inf) cos(x)=Nan */
  if (rri.absxhi>=0x7ff00000) return x-x;   

  if (rri.absxhi < XMAX_COS_CASE2){
    /* CASE 1 : x small enough cos(x)=1. */
    if (rri.absxhi <XMAX_RETURN_1_FOR_COS_RN)
      return 1.;
    else {
      /* CASE 2 : Fast polynomial evaluation */
      x2 = x*x;
      tc = x2 * (c2.d + x2*(c4.d + x2*c6.d ));
      Add12(rri.rh,rri.rl, 1.0, tc);
      if(rri.rh == (rri.rh + (rri.rl * RN_CST_COS_CASE2)))	
	return rri.rh;
      else
	return scs_cos_rn(x); 
    }
  }
  else {
  /* CASE 3 : Need argument reduction */ 
    rri.x=x;
    rri.function=COS;
    ComputeTrigWithArgred2(&rri);
    if(rri.rh == (rri.rh + (rri.rl * RN_CST_COS_CASE3)))	
      if(rri.changesign) return -rri.rh; else return rri.rh;
    else
      return scs_cos_rn(x); 
  }
}



/*************************************************************
 *************************************************************
 *              COS ROUNDED  TO +INFINITY      		     *
 *************************************************************
 *************************************************************/
double cos_ru(double x){ 
  double x2, tc, epsilon; 
  rrinfo rri;
  db_number x_split,  absyh, absyl, u, u53;

  x_split.d=x;
  rri.absxhi = x_split.i[HI_ENDIAN] & 0x7fffffff;

  /* SPECIAL CASES: x=(Nan, Inf) cos(x)=Nan */
  if (rri.absxhi>=0x7ff00000) return x-x;   
  if (rri.absxhi < XMAX_COS_CASE2){
    /* CASE 1 : x small enough cos(x)=1. */
    if (rri.absxhi <XMAX_RETURN_1_FOR_COS_RDIR)
      return 1.;
    else{
      /* CASE 2 : Fast polynomial evaluation */
      x2 = x*x;
      tc = x2 * (c2.d + x2*(c4.d + x2*c6.d ));
      Add12(rri.rh,rri.rl, 1, tc);
      epsilon=EPS_COS_CASE2; 
    }
  }

  else {
    /* CASE 3 : Need argument reduction */ 
    rri.x=x;
    rri.function=COS;
    ComputeTrigWithArgred2(&rri);
    epsilon=EPS_COS_CASE3;
    if(rri.changesign) {
      rri.rh = -rri.rh;
      rri.rl = -rri.rl;
    }
  }    
  
  /* Rounding test to + infinity */
  absyh.d=rri.rh;
  absyl.d=rri.rl;
  absyh.l = absyh.l & 0x7fffffffffffffffLL;
  absyl.l = absyl.l & 0x7fffffffffffffffLL;
  u53.l     = (absyh.l & 0x7ff0000000000000LL) +  0x0010000000000000LL;
  u.l   = u53.l - 0x0350000000000000LL;
  
  if(absyl.d > epsilon * u53.d){ 
    if(rri.rl>0)  return rri.rh+u.d;
    else      return rri.rh;
  }
  else return scs_cos_ru(x);
}


/*************************************************************
 *************************************************************
 *              COS ROUNDED  TO -INFINITY      		     *
 *************************************************************
 *************************************************************/
double cos_rd(double x){ 
  double x2, tc, epsilon; 
  rrinfo rri;
  db_number x_split,  absyh, absyl, u, u53;

  x_split.d=x;
  rri.absxhi = x_split.i[HI_ENDIAN] & 0x7fffffff;

  /* SPECIAL CASES: x=(Nan, Inf) cos(x)=Nan */
  if (rri.absxhi>=0x7ff00000) return x-x;   

  if (rri.absxhi < XMAX_COS_CASE2){
    if (x==0) return 1;
    /* CASE 1 : x small enough cos(x)=1. */
    if (rri.absxhi <XMAX_RETURN_1_FOR_COS_RDIR)
      return ONE_ROUNDED_DOWN; 
    else {   
      /* CASE 2 :  Fast polynomial evaluation */
      x2 = x*x;
      tc = x2 * (c2.d + x2*(c4.d + x2*c6.d ));
      Add12(rri.rh,rri.rl, 1, tc);
      epsilon=EPS_COS_CASE2; 
    }
  }
  else {
  /* CASE 3 : Need argument reduction */ 
    rri.x=x;
    rri.function=COS;
    ComputeTrigWithArgred2(&rri);
    epsilon=EPS_COS_CASE3;
    if(rri.changesign) {
      rri.rh = -rri.rh;
      rri.rl = -rri.rl;
    }     
  }
  /* Rounding test to - infinity */
  absyh.d=rri.rh;
  absyl.d=rri.rl;
  absyh.l = absyh.l & 0x7fffffffffffffffLL;
  absyl.l = absyl.l & 0x7fffffffffffffffLL;
  u53.l     = (absyh.l & 0x7ff0000000000000LL) +  0x0010000000000000LL;
  u.l   = u53.l - 0x0350000000000000LL;
  
  if(absyl.d > epsilon * u53.d){ 
    if(rri.rl>=0)  return rri.rh;
    else       return rri.rh-u.d;
  }
  else return scs_cos_rd(x);
}




/*************************************************************
 *************************************************************
 *              COS ROUNDED  TO ZERO      		     *
 *************************************************************
 *************************************************************/
double cos_rz(double x){ 
  double x2, tc, epsilon; 
  rrinfo rri;
  db_number x_split,  absyh, absyl, u, u53;

  x_split.d=x;
  rri.absxhi = x_split.i[HI_ENDIAN] & 0x7fffffff;

  /* SPECIAL CASES: x=(Nan, Inf) cos(x)=Nan */
  if (rri.absxhi>=0x7ff00000) return x-x;   

  if (rri.absxhi < XMAX_COS_CASE2){
    if (x==0) return 1;
    /* CASE 1 : x small enough cos(x)=1. */
    if (rri.absxhi <XMAX_RETURN_1_FOR_COS_RDIR)
      return ONE_ROUNDED_DOWN; 
    else {
      /* CASE 2 : Fast polynomial evaluation */
      x2 = x*x;
      tc = x2 * (c2.d + x2*(c4.d + x2*c6.d ));
      Add12(rri.rh,rri.rl, 1, tc);
      epsilon=EPS_COS_CASE2; 
    }
  }
  else {
    /* CASE 3 : Need argument reduction */ 
    rri.x=x;
    rri.function=COS;
    ComputeTrigWithArgred2(&rri);
    epsilon=EPS_COS_CASE3;
    if(rri.changesign) {
      rri.rh = -rri.rh;
      rri.rl = -rri.rl;
    } 
  }
  /* Rounding test to zero */
  absyh.d=rri.rh;
  absyl.d=rri.rl;
  absyh.l = absyh.l & 0x7fffffffffffffffLL;
  absyl.l = absyl.l & 0x7fffffffffffffffLL;
  u53.l     = (absyh.l & 0x7ff0000000000000LL) +  0x0010000000000000LL;
  u.l   = u53.l - 0x0350000000000000LL;
  
  if(absyl.d > epsilon * u53.d){ 
    if(rri.rh>0) {
      if (rri.rl>0) return rri.rh;
      else      return rri.rh-u.d;
    }
    else {
      if (rri.rl>0) return rri.rh+u.d;
      else      return rri.rh;
    }	
  }
  else return scs_cos_rz(x);
}





/*************************************************************
 *************************************************************
 *              TAN ROUNDED  TO NEAREST			     *
 *************************************************************
 *************************************************************/ 
double tan_rn(double x){  
  double sh, sl, ch, cl;
  double p7, t, x2;
  rrinfo rri;
  db_number x_split;


  x_split.d=x;
  rri.absxhi = x_split.i[HI_ENDIAN] & 0x7fffffff;


  /* SPECIAL CASES: x=(Nan, Inf) cos(x)=Nan */
  if (rri.absxhi>=0x7ff00000) return x-x;   

  if (rri.absxhi < XMAX_TAN_CASE2){ /* |x|<2^-3 TODO this tradeoff has not been studied */
    if (rri.absxhi < XMAX_RETURN_X_FOR_TAN) 
      return x;

    /* Fast Taylor series */
    x2 = x*x;
    p7 = t7.d + x2*(t9.d + x2*(t11.d + x2*(t13.d + x2*t15.d)));
    t  = x2*(t3l.d +x2*(t5.d + x2*p7));

    sh = x*(x2*t3h.d + t);
    Add12(rri.rh, rri.rl, x, sh);   
    /* Test if round to nearest achieved */ 
    if(rri.rh == (rri.rh + (rri.rl * RN_CST_TAN_CASE22)))
      return rri.rh;
    else {
      /* Still relatively fast, but more accurate */
      Mul12(&sh, &sl, x2, t3h.d);
      Add12(ch, cl, sh, (t+sl));
      Mul22(&sh, &sl, x, 0, ch, cl);
      Add22(&rri.rh, &rri.rl, x, 0, sh, sl);
      /* Test if round to nearest achieved */ 
      if(rri.rh == (rri.rh + (rri.rl * RN_CST_TAN_CASE21)))
	return rri.rh;
      else
	return scs_tan_rn(x); 
    }
  }
  else {
    /* Otherwise : Range reduction then standard evaluation */
    rri.x=x;
    rri.function=TAN;
    ComputeTrigWithArgred2(&rri);

    /* Test if round to nearest achieved */ 
    if(rri.rh == (rri.rh + (rri.rl * RN_CST_TAN_CASE3)))
      return rri.rh;
    else
      return scs_tan_rn(x); 
  }    
}



/*************************************************************
 *************************************************************
 *               ROUNDED  TOWARD  +INFINITY
 *************************************************************
 *************************************************************/
double tan_ru(double x){  
  double sh, sl, ch, cl, epsilon;
  double p7, t, x2;
  db_number x_split,  absyh, absyl, u, u53;
  rrinfo rri;

  x_split.d=x;
  rri.absxhi = x_split.i[HI_ENDIAN] & 0x7fffffff;
  
  /* SPECIAL CASES: x=(Nan, Inf) cos(x)=Nan */
  if (rri.absxhi>=0x7ff00000) return x-x;   
  
  if (rri.absxhi < XMAX_TAN_CASE2){
    if (rri.absxhi < XMAX_RETURN_X_FOR_TAN) {
      if(x<=0.)
	return x;
      else {
	x_split.l ++;
	return x_split.d;
      }
    }
    else {
      /* Fast Taylor series */
      x2 = x*x;
      p7 = t7.d + x2*(t9.d + x2*(t11.d + x2*(t13.d + x2*t15.d)));
      t  = x2*(t3l.d +x2*(t5.d + x2*p7));
    
      sh = x*(x2*t3h.d + t);
      Add12(rri.rh, rri.rl, x, sh);   
      
      /* Rounding test to + infinity */
      absyh.d = rri.rh;
      absyl.d = rri.rl;
      absyh.l = absyh.l & 0x7fffffffffffffffLL;
      absyl.l = absyl.l & 0x7fffffffffffffffLL;
      u53.l     = (absyh.l & 0x7ff0000000000000LL) +  0x0010000000000000LL;
      u.l   = u53.l - 0x0350000000000000LL;
      epsilon=EPS_TAN_CASE22; 
      if(absyl.d > epsilon * u53.d){ 
	if(rri.rl>0) return rri.rh+u.d;
	else     return rri.rh;
      }
      else {
	/* Still relatively fast, but more accurate */
	Mul12(&sh, &sl, x2, t3h.d);
	Add12(ch, cl, sh, (t+sl));
	Mul22(&sh, &sl, x, 0, ch, cl);
	Add22(&rri.rh, &rri.rl, x, 0, sh, sl);
	epsilon=EPS_TAN_CASE21; 
      }
    }
  }
  else { 
    /* Normal case: Range reduction then standard evaluation */
    rri.x=x;
    rri.function=TAN;
    ComputeTrigWithArgred2(&rri);
    epsilon=EPS_TAN_CASE3; 
   }
  
  /* Rounding test to + infinity */
  absyh.d = rri.rh;
  absyl.d = rri.rl;
  absyh.l = absyh.l & 0x7fffffffffffffffLL;
  absyl.l = absyl.l & 0x7fffffffffffffffLL;
  u53.l     = (absyh.l & 0x7ff0000000000000LL) +  0x0010000000000000LL;
  u.l   = u53.l - 0x0350000000000000LL;
  if(absyl.d > epsilon * u53.d){ 
    if(rri.rl>0) return rri.rh+u.d;
    else     return rri.rh;
  }
  else return scs_tan_ru(x); 
}


/*************************************************************
 *************************************************************
 *               ROUNDED  TOWARD  -INFINITY
 *************************************************************
 *************************************************************/
double tan_rd(double x){  
  double sh, sl, ch, cl, epsilon;
  double p7, t, x2;
  rrinfo rri;
  db_number x_split,  absyh, absyl, u, u53;

  
  x_split.d=x;
  rri.absxhi = x_split.i[HI_ENDIAN] & 0x7fffffff;
  
  /* SPECIAL CASES: x=(Nan, Inf) cos(x)=Nan */
  if (rri.absxhi>=0x7ff00000) return x-x;   
  
  if (rri.absxhi < XMAX_TAN_CASE2){
    if (rri.absxhi < XMAX_RETURN_X_FOR_TAN) {
      if(x>=0.)
	return x;
      else {
	x_split.l ++;
	return x_split.d;
      }
    }
    
    /* Fast Taylor series */
    x2 = x*x;
    p7 = t7.d + x2*(t9.d + x2*(t11.d + x2*(t13.d + x2*t15.d)));
    t  = x2*(t3l.d +x2*(t5.d + x2*p7));
    
    sh = x*(x2*t3h.d + t);
    Add12(rri.rh, rri.rl, x, sh);   

    /* Rounding test to - infinity */
    absyh.d=rri.rh;
    absyl.d=rri.rl;
    absyh.l = absyh.l & 0x7fffffffffffffffLL;
    absyl.l = absyl.l & 0x7fffffffffffffffLL;
    u53.l     = (absyh.l & 0x7ff0000000000000LL) +  0x0010000000000000LL;
    u.l   = u53.l - 0x0350000000000000LL;
    epsilon=EPS_TAN_CASE22; 
    if(absyl.d > epsilon * u53.d){ 
      if(rri.rl>0) return rri.rh;
      else     return rri.rh-u.d;
    }
    else { 
      /* Still relatively fast, but more accurate */
      Mul12(&sh, &sl, x2, t3h.d);
      Add12(ch, cl, sh, (t+sl));
      Mul22(&sh, &sl, x, 0, ch, cl);
      Add22(&rri.rh, &rri.rl, x, 0, sh, sl);
      epsilon=EPS_TAN_CASE21; 
    }
  }
  
  else { 
    /* normal case: Range reduction then standard evaluation */
    rri.x=x;
    rri.function=TAN;
    ComputeTrigWithArgred2(&rri);
    epsilon=EPS_TAN_CASE3; 
  }
  
  /* Rounding test to - infinity */
  absyh.d=rri.rh;
  absyl.d=rri.rl;
  absyh.l = absyh.l & 0x7fffffffffffffffLL;
  absyl.l = absyl.l & 0x7fffffffffffffffLL;
  u53.l     = (absyh.l & 0x7ff0000000000000LL) +  0x0010000000000000LL;
  u.l   = u53.l - 0x0350000000000000LL;
  if(absyl.d > epsilon * u53.d){ 
    if(rri.rl>0) return rri.rh;
    else     return rri.rh-u.d;
  }
  else return scs_tan_rd(x);
}
 	

/*************************************************************
 *************************************************************
 *               ROUNDED  TOWARD  ZERO
 *************************************************************
 *************************************************************/
double tan_rz(double x){  
  double sh, sl, ch, cl, epsilon;
  double p7, t, x2;
  rrinfo rri;
  db_number x_split,  absyh, absyl, u, u53;

  x_split.d=x;
  rri.absxhi = x_split.i[HI_ENDIAN] & 0x7fffffff;
  
  /* SPECIAL CASES: x=(Nan, Inf) cos(x)=Nan */
  if (rri.absxhi>=0x7ff00000) return x-x;   
  
  if (rri.absxhi < XMAX_TAN_CASE2){
    if (rri.absxhi < XMAX_RETURN_X_FOR_TAN) {
      return x;
    }
    
    /* Fast Taylor series */
    x2 = x*x;
    p7 = t7.d + x2*(t9.d + x2*(t11.d + x2*(t13.d + x2*t15.d)));
    t  = x2*(t3l.d +x2*(t5.d + x2*p7));
    
    sh = x*(x2*t3h.d + t);
    Add12(rri.rh, rri.rl, x, sh);   

    /* Rounding test to zero */
    absyh.d=rri.rh;
    absyl.d=rri.rl;
    absyh.l = absyh.l & 0x7fffffffffffffffLL;
    absyl.l = absyl.l & 0x7fffffffffffffffLL;
    u53.l     = (absyh.l & 0x7ff0000000000000LL) +  0x0010000000000000LL;
    u.l   = u53.l - 0x0350000000000000LL;
    epsilon=EPS_TAN_CASE22; 
    if(absyl.d > epsilon * u53.d){ 
      if(rri.rh>0) 
	if(rri.rl>0) return rri.rh;
	else     return rri.rh-u.d;
      else 
	if(rri.rl>0) return rri.rh+u.d;
	else    return rri.rh;
    }
    else {
      /* Still relatively fast, but more accurate */
      Mul12(&sh, &sl, x2, t3h.d);
      Add12(ch, cl, sh, (t+sl));
      Mul22(&sh, &sl, x, 0, ch, cl);
      Add22(&rri.rh, &rri.rl, x, 0, sh, sl);
      epsilon=EPS_TAN_CASE21; 
    }
  }
  else { 
    /* Normal case: Range reduction then standard evaluation */
    rri.x=x;
    rri.function=TAN;
    ComputeTrigWithArgred2(&rri);
    epsilon=EPS_TAN_CASE3; 
   }
  /* Rounding test to zero */
  absyh.d=rri.rh;
  absyl.d=rri.rl;
  absyh.l = absyh.l & 0x7fffffffffffffffLL;
  absyl.l = absyl.l & 0x7fffffffffffffffLL;
  u53.l     = (absyh.l & 0x7ff0000000000000LL) +  0x0010000000000000LL;
  u.l   = u53.l - 0x0350000000000000LL;
  if(absyl.d > epsilon * u53.d){ 
    if(rri.rh>0) 
      if(rri.rl>0) return rri.rh;
      else     return rri.rh-u.d;
    else 
      if(rri.rl>0) return rri.rh+u.d;
      else    return rri.rh;
  }
  else return scs_tan_rz(x); 
}

