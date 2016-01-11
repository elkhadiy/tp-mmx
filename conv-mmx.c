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

#define UNROLL 4

/* SIMD optimized integer version with 4 iterations unrolled */
void YCrCb_to_ARGB(uint8_t *YCrCb_MCU[3], uint32_t *RGB_MCU, uint32_t nb_MCU_H, uint32_t nb_MCU_V)
{
   uint8_t *MCU_Y, *MCU_Cr, *MCU_Cb;
   int8_t index, i, j;
   static int initialized = 0;

   if (!initialized) {
      __asm__("emms");
      initialized = 1;
   }

   MCU_Y = YCrCb_MCU[0];
   MCU_Cr = YCrCb_MCU[2];
   MCU_Cb = YCrCb_MCU[1];

   for (i = 0; i < 8 * nb_MCU_V; i++) {
      for (j = 0; j < 8 * nb_MCU_H; j += UNROLL) {
         index = i * (8 * nb_MCU_H)  + j;
         /* Set mm0 to full zero */
         __asm__("pxor      %mm0, %mm0");
         /* Load 4 bytes of MCU_Y memory into mm0 and at the same time
          * expand them to an intel "word", i.e. 16 bits. */
         __asm__("punpcklbw %0, %%mm0"::"m"(MCU_Y[0]));
#ifdef YCrCb_DEBUG
         /* Checking the intermediate results */
         uint16_t Y[4]; 
         __asm__("movq %%mm0, %0":"=m"(Y[0]));
#endif
         uint64_t cst_128 = 128;

         /* TODO: Load MCU_Cr into mm1 and expand it to 16 bits, and sub 128 */
         __asm__("punpcklbw %0, %%mm1"::"m"(MCU_Cr[0]));
         __asm__("psubw     %0, %%mm1"::"m"(cst_128));

         /* TODO: Load MCU_Cb into mm2 and expand it to 16 bits, and sub 128 */
         __asm__("punpcklbw %0, %%mm2"::"m"(MCU_Cb[0]));
         __asm__("psubw     %0, %%mm2"::"m"(cst_128));

#ifdef YCrCb_DEBUG
         /* Checking the intermediate results */
         int8_t  Cr[4], Cb[4];
         __asm__("movq %%mm1, %0":"=m"(Cr[0]));
         __asm__("movq %%mm2, %0":"=m"(Cb[0]));
#endif

         /* register containing 0 */
         __asm__("pxor      %mm6, %mm6");

         /* Here we have the Y_i in mm0, Cr_i in mm1 and Cb_i in mm2.
          * Since these values are used for R, G and B computations,
          * these registers should not be the target of any instruction */

         /* compute R in mm3 */

         uint64_t cst_359 = 359;
         __asm__("movq %mm1, %mm3");
         __asm__("pmullw %0, %%mm3"::"m"(cst_359));
         __asm__("paddw %mm0, %mm3");
         __asm__("psrlw $8, %mm3");
         __asm__("packuswb %mm6, %mm3");

#ifdef RGB_DEBUG
         uint8_t R[4];
         __asm__("movd %%mm3, %0":"=m"(R[0]));
#endif

         /* compute G in mm4 */

         uint64_t cst_183 = 183;
         /* use mm5 as intermediate result */
         __asm__("movq %mm1, %mm5");
         __asm__("pmullw %0, %%mm5"::"m"(cst_183));
         __asm__("movq %mm0, %mm4");
         __asm__("psubw %mm5, %mm4");

         uint64_t cst_88 = 88;
         __asm__("movq %mm2, %mm5");
         __asm__("pmullw %0, %%mm5"::"m"(cst_88));
         __asm__("psubw %mm5, %mm4");

         __asm__("psrlw $8, %mm4");
         __asm__("packuswb %mm6, %mm4");

#ifdef RGB_DEBUG
         uint8_t G[4];
         __asm__("movd %%mm4, %0":"=m"(G[0]));
#endif

         /* compute B in mm5 */

         uint64_t cst_455 = 455;
         __asm__("movq %mm2, %mm5");
         __asm__("pmullw %0, %%mm5"::"m"(cst_455));
         __asm__("paddw %mm0, %mm5");
         __asm__("psrlw $8, %mm5");
         __asm__("packuswb %mm6, %mm5");


#ifdef RGB_DEBUG
         uint8_t B[4];
         __asm__("movd      %%mm5, %0":"=m"(B[0]));
#endif
         /* TODO: Now the 4 lowest bytes of mm3, mm4 and mm5 contain the 
          * 4 R, G and B values  of the iteration.
          * We do some kind of matrix transpose to build the 2 times
          * 64 words containing the appropriate values:
          * First, produce a 0/R word, then a G/B word, and finally
          * mix them to produce two 0/R/G/B quads */

         __asm__("pxor      %mm7, %mm7");
         /* mm7: 0
          * mm3: R
          * mm4: G
          * mm5: B
          */

         __asm__("punpcklhw %mm7, %mm3"); // mm7 et mm3 -> mm3
         __asm__("punpcklhw %mm4, %mm5"); // mm4 et mm5 -> mm5

         // TODO: faire des copies
         __asm__("punpcklbw %mm3, %mm5"); // A
         __asm__("punpcklhw %mm3, %mm5"); // B

         // On a 3 dans les poids forts de A
         // On a 2 dans les poids faible de A
         // On a 1 dans les poids forts de B
         // On a 0 dans les poids faible de B
      }
   }
}
