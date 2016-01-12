/*******************************************************************************
 * vim:set ts=3:
 * File   : conv.c, file for JPEG-JFIF sequential decoder    
 *
 * Copyright (C) 2007-2016 TIMA Laboratory
 * Author(s) :      Frédéric Pétrot <Frederic.Petrot@imag.fr>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 *************************************************************************************/

#include "conv.h"
#include "stdlib.h"
#include "stdio.h"
#include "utils.h"

#include <unistd.h>
#include <stdint.h>

#define EXPAND(x) ((x)<<48|(x)<<32|(x)<<16|(x))
//#define RGB_DEBUG
//#define RGB_DEBUG

/* SIMD optimized integer version with 4 iterations unrolled */
void YCrCb_to_ARGB(uint8_t *YCrCb_MCU[3], uint32_t *RGB_MCU, uint32_t nb_MCU_H, uint32_t nb_MCU_V)
{
   uint8_t *MCU_Y, *MCU_Cr, *MCU_Cb;
   int8_t index, i, j;
	uint32_t val;
   static int initialized = 0;

   if (!initialized) {
      __asm__("emms");
      initialized = 1;
   }

   MCU_Y = YCrCb_MCU[0];
   MCU_Cr = YCrCb_MCU[2];
   MCU_Cb = YCrCb_MCU[1];

   for (i = 0; i < 8 * nb_MCU_V; i++) {
      for (j = 0; j < 8 * nb_MCU_H; j += 4) {
         index = i * (8 * nb_MCU_H) + j;
         /* Set mm0 & mm7 to full zero */
         __asm__("pxor      %mm0, %mm0");
			__asm__("pxor      %mm7, %mm7");
         /* Load 4 bytes of MCU_Y memory into mm0 and at the same time
          * expand them to an intel "word", i.e. 16 bits. */
			val = MCU_Y[index+3]<<24|MCU_Y[index+2]<<16|MCU_Y[index+1]<<8|MCU_Y[index];
			__asm__("movd %0, %%mm1"::"m"(val)); // mm1 := 0 0 0 0 Y3 Y2 Y1 Y0
         __asm__("punpcklbw %mm1, %mm0"); // mm0 := Y3 0 Y2 0 Y1 0 Y0 0
#ifdef YCrCb_DEBUG
         /* Checking the intermediate results */
         uint16_t Y[4];
         __asm__("movq %%mm0, %0":"=m"(Y[0]));
			printf("MCU_Y = %04x %04x %04x %04x\n", MCU_Y[index+3], MCU_Y[index+2], MCU_Y[index+1], MCU_Y[index]);
			printf("Y     = %04x %04x %04x %04x\n", Y[3], Y[2], Y[1], Y[0]);
#endif

			uint64_t cst_128 = EXPAND((uint64_t)128);
			__asm__("movq %0, %%mm3"::"m"(cst_128));

         /* Load MCU_Cr into mm1 and expand it to 16 bits, and sub 128 */
			val = MCU_Cr[index+3]<<24|MCU_Cr[index+2]<<16|MCU_Cr[index+1]<<8|MCU_Cr[index];
			__asm__("movd %0, %%mm1"::"m"(val)); // mm1 := 0 0 0 0 Cr3 Cr2 Cr1 Cr0
			__asm__("punpcklbw %mm7, %mm1"); // mm1 := 0 Cr3 0 Cr2 0 Cr1 0 Cr0
         __asm__("psubw     %mm3, %mm1"); // mm1 := 0 Cr3-128 0 Cr2-128 0 Cr1-128 0 Cr0-128

         /* Load MCU_Cb into mm2 and expand it to 16 bits, and sub 128 */
			val = MCU_Cb[index+3]<<24|MCU_Cb[index+2]<<16|MCU_Cb[index+1]<<8|MCU_Cb[index];
			__asm__("movd %0, %%mm2"::"m"(val)); // mm1 := 0 0 0 0 Cb3 Cb2 Cb1 Cb0
			__asm__("punpcklbw %mm7, %mm2"); // mm1 := 0 Cb3 0 Cb2 0 Cb1 0 Cb0
         __asm__("psubw     %mm3, %mm2"); // mm1 := 0 Cb3-128 0 Cb2-128 0 Cb1-128 0 Cb0-128

#ifdef YCrCb_DEBUG
         /* Checking the intermediate results */
         int8_t  Cr[8], Cb[8];
         __asm__("movq %%mm1, %0":"=m"(Cr[0]));
         __asm__("movq %%mm2, %0":"=m"(Cb[0]));

			printf("MCU_Cr = %02i %02i %02i %02i\n", MCU_Cr[index+3], MCU_Cr[index+2], MCU_Cr[index+1], MCU_Cr[index]);
			printf("Cr-128 = %02i %02i %02i %02i %02i %02i %02i %02i\n", Cr[7], Cr[6], Cr[5], Cr[4], Cr[3], Cr[2], Cr[1], Cr[0]);

			printf("MCU_Cb = %02i %02i %02i %02i\n", MCU_Cb[index+3], MCU_Cb[index+2], MCU_Cb[index+1], MCU_Cb[index]);
			printf("Cb-128 = %02i %02i %02i %02i %02i %02i %02i %02i\n", Cb[7], Cb[6], Cb[5], Cb[4], Cb[3], Cb[2], Cb[1], Cb[0]);

#endif

         /* Here we have the Y_i in mm0, Cr_i in mm1 and Cb_i in mm2.
          * Since these values are used for R, G and B computations,
          * these registers should not be the target of any instruction */

         /* compute R in mm3 */
			//*
         uint64_t cst_359 = EXPAND((uint64_t)359);
         __asm__("movq %mm1, %mm3");                   // mm3 := (Cr_i-128)
         __asm__("pmullw %0, %%mm3"::"m"(cst_359));    // mm3 := (Cr_i-128)*359
         __asm__("paddw %mm0, %mm3");                  // mm3 := (Y_i<<8) + (Cb_i-128)*359
         __asm__("psrlw $8, %mm3");                    // mm3 := ((Y_i<<8) + (Cb_i-128)*359) >> 8
         __asm__("packuswb %mm7, %mm3");               // mm3 := 0 0 0 0 R3 R2 R1 R0
			//*/

#ifdef RGB_DEBUG
         uint8_t R[8];
         __asm__("movq %%mm3, %0":"=m"(R[0]));
			printf("R = %02i %02i %02i %02i %02i %02i %02i %02i\n", R[7], R[6], R[5], R[4], R[3], R[2], R[1], R[0]);
#endif

         /* compute G in mm4 */

         uint64_t cst_183 = EXPAND((uint64_t)183);
         /* use mm5 as intermediate result */
			//*
         __asm__("movq %mm1, %mm5");                  // mm5 := (Cr_i-128)
         __asm__("pmullw %0, %%mm5"::"m"(cst_183));   // mm5 := (Cr_i-128)*183
         __asm__("movq %mm0, %mm4");                  // mm4 := (Y_i<<8)
         __asm__("psubw %mm5, %mm4");                 // mm4 := (Y_i<<8) - (Cr_i-128)*183
			//*/

         uint64_t cst_88 = EXPAND((uint64_t)88);
			//*
         __asm__("movq %mm2, %mm5");                  // mm5 := (Cb_i-128)
         __asm__("pmullw %0, %%mm5"::"m"(cst_88));    // mm5 := (Cb_i-128)*88
         __asm__("psubw %mm5, %mm4");                 // mm4 := (Y_i<<8) - (Cr_i-128)*183 - (Cb_i-128)*88
         __asm__("psrlw $8, %mm4");                   // mm4 := ((Y_i<<8) - (Cr_i-128)*183 - (Cb_i-128)*88) >> 8
         __asm__("packuswb %mm6, %mm4");              // mm4 := 0 0 0 0 G3 G2 G1 G0
			//*/

#ifdef RGB_DEBUG
   		uint8_t G[8];
         __asm__("movq %%mm3, %0":"=m"(G[0]));
			printf("G = %02i %02i %02i %02i %02i %02i %02i %02i\n", G[7], G[6], G[5], G[4], G[3], G[2], G[1], G[0]);
#endif

         /* compute B in mm5 */

			//*
         uint64_t cst_455 = EXPAND((uint64_t)455);
         __asm__("movq %mm2, %mm5");                   // mm5 := (Cb_i-128)
         __asm__("pmullw %0, %%mm5"::"m"(cst_455));    // mm5 := (Cb_i-128)*359
         __asm__("paddw %mm0, %mm5");                  // mm5 := (Y_i<<8) + (Cb_i-128)*455
         __asm__("psrlw $8, %mm5");                    // mm5 := ((Y_i<<8) + (Cb_i-128)*455) >> 8
         __asm__("packuswb %mm7, %mm5");               // mm5 := 0 0 0 0 B3 B2 B1 B0
			//*/


#ifdef RGB_DEBUG
    		uint8_t B[8];
         __asm__("movq %%mm3, %0":"=m"(B[0]));
			printf("B = %02i %02i %02i %02i %02i %02i %02i %02i\n", B[7], B[6], B[5], B[4], B[3], B[2], B[1], B[0]);
#endif
         /* Now the 4 lowest bytes of mm3, mm4 and mm5 contain the 
          * 4 R, G and B values  of the iteration.
          * We do some kind of matrix transpose to build the 2 times
          * 64 words containing the appropriate values:
          * First, produce a 0/R word, then a G/B word, and finally
          * mix them to produce two 0/R/G/B quads */

         /* mm7 := 0 0 0 0 0 0 0 0
          * mm3 := 0 0 0 0 R3 R2 R1 R0
          * mm4 := 0 0 0 0 G3 G2 G1 G0
          * mm5 := 0 0 0 0 B3 B2 B1 B0
          */
			//*
         __asm__("punpcklbw %mm7, %mm3"); // mm3 := 0  R3 0  R2 0  R1 0  R0
         __asm__("punpcklbw %mm4, %mm5"); // mm5 := B3 G3 B2 G2 B1 G1 B0 G0

         
         __asm__("movq %mm5, %mm4");      // saving mm5 in mm4 for the second quad
         __asm__("punpckhwd %mm3, %mm5"); // mm5 := 0 R3 B3 G3 0 R2 B2 G2
         __asm__("punpcklwd %mm3, %mm4"); // mm4 := 0 R1 B1 G1 0 R0 B0 G0

         __asm__("movq %%mm4, %0":"=m"(RGB_MCU[index]));
         __asm__("movq %%mm5, %0":"=m"(RGB_MCU[index+2]));
			//*/
      }
   }
}
