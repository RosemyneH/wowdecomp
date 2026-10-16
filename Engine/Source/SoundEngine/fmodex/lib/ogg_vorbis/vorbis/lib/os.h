#ifndef _OS_H
#define _OS_H
/********************************************************************
 *                                                                  *
 * THIS FILE IS PART OF THE OggVorbis SOFTWARE CODEC SOURCE CODE.   *
 * USE, DISTRIBUTION AND REPRODUCTION OF THIS LIBRARY SOURCE IS     *
 * GOVERNED BY A BSD-STYLE SOURCE LICENSE INCLUDED WITH THIS SOURCE *
 * IN 'COPYING'. PLEASE READ THESE TERMS BEFORE DISTRIBUTING.       *
 *                                                                  *
 * THE OggVorbis SOURCE CODE IS (C) COPYRIGHT 1994-2009             *
 * by the Xiph.Org Foundation http://www.xiph.org/                  *
 *                                                                  *
 ********************************************************************

 function: #ifdef jail to whip a few platforms into the UNIX ideal.
 last mod: $Id: os.h 16227 2009-07-08 06:58:46Z xiphmont $

 ********************************************************************/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <math.h>
#include <ogg/os_types.h>

#include "misc.h"

#ifndef _V_IFDEFJAIL_H_
#  define _V_IFDEFJAIL_H_

#  ifdef __GNUC__
#    define STIN static __inline__
#  elif _WIN32
#    define STIN static __inline
#  else
#    define STIN static
#  endif

#ifdef DJGPP
#  define rint(x)   (FMOD_ogg_floor((x)+0.5f))
#endif

#ifndef M_PI
#  define M_PI (3.1415926536f)
#endif

#if (defined(_WIN32) && !defined(__SYMBIAN32__)) || defined(_XENON)
#  include <malloc.h>
#  define rint(x)   (FMOD_ogg_floor((x)+0.5f))
#  define NO_FLOAT_MATH_LIB
#  define FAST_HYPOT(a, b) FMOD_ogg_sqrt((a)*(a) + (b)*(b))
#endif

#if defined(__SYMBIAN32__) && defined(__WINS__)
void *_alloca(size_t size);
#  define alloca _alloca
#endif

#ifndef FAST_HYPOT
#  define FAST_HYPOT hypot
#endif

#endif

#ifdef HAVE_ALLOCA_H
#  include <alloca.h>
#endif

#ifdef USE_MEMORY_H
#  include <memory.h>
#endif

#ifndef min
#  define min(x,y)  ((x)>(y)?(y):(x))
#endif

#ifndef max
#  define max(x,y)  ((x)<(y)?(y):(x))
#endif


/* Special i386 GCC implementation */
#if defined(__i386__) && defined(__GNUC__) && !defined(__BEOS__)
#  define VORBIS_FPU_CONTROL
/* both GCC and MSVC are kinda stupid about rounding/casting to int.
   Because of encapsulation constraints (GCC can't see inside the asm
   block and so we end up doing stupid things like a store/load that
   is collectively a noop), we do it this way */

/* we must set up the fpu before this works!! */

typedef ogg_int16_t vorbis_fpu_control;

static inline void FMOD_vorbis_fpu_setround(vorbis_fpu_control *fpu){
  ogg_int16_t ret;
  ogg_int16_t temp;
  __asm__ __volatile__("fnstcw %0\n\t"
          "movw %0,%%dx\n\t"
          "andw $62463,%%dx\n\t"
          "movw %%dx,%1\n\t"
          "fldcw %1\n\t":"=m"(ret):"m"(temp): "dx");
  *fpu=ret;
}

static inline void FMOD_vorbis_fpu_restore(vorbis_fpu_control fpu){
  __asm__ __volatile__("fldcw %0":: "m"(fpu));
}

/* assumes the FPU is in round mode! */
static inline int FMOD_vorbis_ftoi(double f){  /* yes, double!  Otherwise,
                                             we get extra fst/fld to
                                             truncate precision */
  int i;
  __asm__("fistl %0": "=m"(i) : "t"(f));
  return(i);
}

    #ifdef __MACH__
      #define FMOD_ogg_ldexp (float)ldexp
    #else
      #define FMOD_ogg_ldexp ldexpf
    #endif

    #define FMOD_ogg_cos cosf
    #define FMOD_ogg_acos acosf
    #define FMOD_ogg_sin sinf
    #define FMOD_ogg_sqrt sqrtf
    #define FMOD_ogg_fabs fabsf
    #define FMOD_ogg_ceil ceilf
    #define FMOD_ogg_floor floorf
    #define FMOD_ogg_exp expf
    #define FMOD_ogg_log logf
    #define FMOD_ogg_frexp frexpf
    #define FMOD_ogg_atan atanf
    #define FMOD_ogg_pow powf
    #define FMOD_ogg_rint(_x) (FMOD_ogg_floor((_x)+0.5f)) /* rintf doesnt work.  causes horrible corruption.  bug in compiler i think */
#endif /* Special i386 GCC implementation */


#if defined(R5900)
    #  define VORBIS_FPU_CONTROL

    typedef int vorbis_fpu_control;

    static __inline int FMOD_vorbis_ftoi(float val)
    {   
        register int result;                                       
	    register float tmp = 0;
        __asm__ volatile ( 
            "cvt.w.s %2,%1\n"   	
		    "mfc1    %0,%2\n"
            : "=r" (result)   
            : "f" (val),"f" (tmp)
		    );  
	    return result;
    }
    static __inline int FMOD_vorbis_ftoipcm(float val)
    {   
        register int result;                                       
	    register float tmp = 0;
        __asm__ volatile ( 
        	"addi	 $4, $0, -32768\n"
        	"addi	 $5, $0,  32767\n"
            "cvt.w.s %2,%1\n"   	
		    "mfc1    %0,%2\n"
            "pmaxw   %0, %0, $4\n"
            "pminw   %0, %0, $5\n"
            : "=r" (result)   
            : "f" (val),"f" (tmp)
            : "$4", "$5"
		    );  
	    return result;
    }
    #  define FMOD_vorbis_fpu_setround(vorbis_fpu_control) {}
    #  define FMOD_vorbis_fpu_restore(vorbis_fpu_control) {}

    #define FMOD_ogg_cos cosf
    #define FMOD_ogg_acos acosf
    #define FMOD_ogg_sin sinf
    #define FMOD_ogg_sqrt sqrtf
    #define FMOD_ogg_fabs fabsf
    #define FMOD_ogg_ceil ceilf
    #define FMOD_ogg_floor floorf
    #define FMOD_ogg_exp expf
    #define FMOD_ogg_log logf
    #define FMOD_ogg_ldexp ldexpf
    #define FMOD_ogg_frexp frexpf
    #define FMOD_ogg_atan atanf
    #define FMOD_ogg_pow powf
    #define FMOD_ogg_rint(_x) (FMOD_ogg_floor((_x)+0.5f)) /* rintf doesnt work.  causes horrible corruption.  bug in compiler i think */
#endif

/* MSVC inline assembly. 32 bit only; inline ASM isn't implemented in the
 * 64 bit compiler */
#if defined(_MSC_VER) && !defined(_WIN64) && !defined(_WIN32_WCE) && !defined(_XENON)
#  define VORBIS_FPU_CONTROL

typedef ogg_int16_t vorbis_fpu_control;

static __inline int FMOD_vorbis_ftoi(double f){
        int i;
        __asm{
                fld f
                fistp i
        }
        return i;
}

static __inline void FMOD_vorbis_fpu_setround(vorbis_fpu_control *fpu){
}

static __inline void FMOD_vorbis_fpu_restore(vorbis_fpu_control fpu){
}
    #define FMOD_ogg_cos (float)cos
    #define FMOD_ogg_acos (float)acos
    #define FMOD_ogg_sin (float)sin
    #define FMOD_ogg_sqrt (float)sqrt
    #define FMOD_ogg_fabs (float)fabs
    #define FMOD_ogg_ceil (float)ceil
    #define FMOD_ogg_floor (float)floor
    #define FMOD_ogg_exp (float)exp
    #define FMOD_ogg_log (float)log
    #define FMOD_ogg_ldexp (float)ldexp
    #define FMOD_ogg_frexp (float)frexp
    #define FMOD_ogg_atan (float)atan
    #define FMOD_ogg_pow (float)pow
    #define FMOD_ogg_rint (float)rint
#endif /* Special MSVC 32 bit implementation */


/* Optimized code path for x86_64 builds. Uses SSE2 intrinsics. This can be
   done safely because all x86_64 CPUs supports SSE2. */
#if (defined(_MSC_VER) && defined(_M_X64)) || (defined(__GNUC__) && defined(__x86_64__))
#  define VORBIS_FPU_CONTROL

typedef ogg_int16_t vorbis_fpu_control;

#include <emmintrin.h>
static __inline int FMOD_vorbis_ftoi(double f){
        return _mm_cvtsd_si32(_mm_load_sd(&f));
}

static __inline void FMOD_vorbis_fpu_setround(vorbis_fpu_control *fpu){
}

static __inline void FMOD_vorbis_fpu_restore(vorbis_fpu_control fpu){
}
    #define FMOD_ogg_cos (float)cos
    #define FMOD_ogg_acos (float)acos
    #define FMOD_ogg_sin (float)sin
    #define FMOD_ogg_sqrt (float)sqrt
    #define FMOD_ogg_fabs (float)fabs
    #define FMOD_ogg_ceil (float)ceil
    #define FMOD_ogg_floor (float)floor
    #define FMOD_ogg_exp (float)exp
    #define FMOD_ogg_log (float)log
    #define FMOD_ogg_ldexp (float)ldexp
    #define FMOD_ogg_frexp (float)frexp
    #define FMOD_ogg_atan (float)atan
    #define FMOD_ogg_pow (float)pow
    #define FMOD_ogg_rint (float)rint
    
#endif /* Special MSVC x64 implementation */


/* If no special implementation was found for the current compiler / platform,
   use the default implementation here: */
#ifndef VORBIS_FPU_CONTROL

typedef int vorbis_fpu_control;
    
    #define FMOD_ogg_cos (float)cos
    #define FMOD_ogg_acos (float)acos
    #define FMOD_ogg_sin (float)sin
    #define FMOD_ogg_sqrt (float)sqrt
    #define FMOD_ogg_fabs (float)fabs
    #define FMOD_ogg_ceil (float)ceil
    #define FMOD_ogg_floor (float)floor
    #define FMOD_ogg_exp (float)exp
    #define FMOD_ogg_log (float)log
    #define FMOD_ogg_ldexp (float)ldexp
    #define FMOD_ogg_frexp (float)frexp
    #define FMOD_ogg_atan (float)atan
    #define FMOD_ogg_pow (float)pow
    #define FMOD_ogg_rint (float)rint
    
static int FMOD_vorbis_ftoi(double f){
        /* Note: MSVC and GCC (at least on some systems) round towards zero, thus,
           the FMOD_ogg_floor() call is required to ensure correct roudning of
           negative numbers */
        return (int)FMOD_ogg_floor(f+.5f);
}

/* We don't have special code for this compiler/arch, so do it the slow way */
#  define FMOD_vorbis_fpu_setround(vorbis_fpu_control) {}
#  define FMOD_vorbis_fpu_restore(vorbis_fpu_control) {}

#endif /* default implementation */

#include <string.h>
#define ogg_strlen strlen
#define ogg_strcpy strcpy
#define ogg_strcat strcat

#endif /* _OS_H */
