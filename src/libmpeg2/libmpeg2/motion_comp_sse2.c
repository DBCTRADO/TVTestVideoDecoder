/*
 * motion_comp_sse2.c
 * Copyright (C) 2015      DBCTRADO
 *
 * This file is part of mpeg2dec, a free MPEG-2 video stream decoder.
 * See http://libmpeg2.sourceforge.net/ for updates.
 *
 * mpeg2dec is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * mpeg2dec is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "config.h"

#if defined(ARCH_X86) || defined(ARCH_X86_64)

#include <inttypes.h>
#include <xmmintrin.h>
#include <emmintrin.h>
#include <immintrin.h>

#include "mpeg2.h"
#include "mpeg2_internal.h"
#include "attributes.h"


#define mm_loadl_unpackh_epi64(a, b) \
    (_mm_castpd_si128(_mm_loadl_pd(_mm_castsi128_pd(a), (const double*)(b))))
#define mm_loadh_unpackl_epi64(a, b) \
    (_mm_castpd_si128(_mm_loadh_pd(_mm_castsi128_pd(a), (const double*)(b))))
#define mm_loadl_epi64(a) \
    _mm_loadl_epi64((const __m128i*)(a))
    //(_mm_castpd_si128(_mm_loadl_pd(_mm_undefined_pd(), (const double*)(a))))
#define mm_loadh_epi64(a) \
    (_mm_castpd_si128(_mm_loadh_pd(_mm_undefined_pd(), (const double*)(a))))
#define mm_storel_epi64(a, b) \
    _mm_storel_epi64((__m128i*)(a), b)
    //(_mm_storel_pd((double*)(a), _mm_castsi128_pd(b)))
#define mm_storeh_epi64(a, b) \
    (_mm_storeh_pd((double*)(a), _mm_castsi128_pd(b)))
#define mm_movelh_epi64(a) \
    (_mm_castps_si128(_mm_movelh_ps(_mm_undefined_ps(), _mm_castsi128_ps(a))))


static ATTR_ALIGN(16) const int16_t const_1_16[8] = {1, 1, 1, 1, 1, 1, 1, 1};


static void MC_put_o_16_sse2 (uint8_t * dest, const uint8_t * ref, int stride,
			      int height)
{
    const uint8_t * edx = ref;
    uint8_t * ecx = dest;
    int esi = height;
    const int eax = stride;
    const int edi = eax + eax;
    const int ebx = edi + eax;

    for (; esi; edx += edi * 2, ecx += edi * 2, esi -= 4) {
	__m128i xmm0, xmm1, xmm2, xmm3;

	xmm0 = _mm_loadu_si128 ((const __m128i*)edx);
	xmm1 = _mm_loadu_si128 ((const __m128i*)(edx + eax));
	xmm2 = _mm_loadu_si128 ((const __m128i*)(edx + edi));
	xmm3 = _mm_loadu_si128 ((const __m128i*)(edx + ebx));
	_mm_store_si128 ((__m128i*)ecx, xmm0);
	_mm_store_si128 ((__m128i*)(ecx + eax), xmm1);
	_mm_store_si128 ((__m128i*)(ecx + edi), xmm2);
	_mm_store_si128 ((__m128i*)(ecx + ebx), xmm3);
    }
}

static void MC_put_o_8_sse2 (uint8_t * dest, const uint8_t * ref, int stride,
			     int height)
{
    const uint8_t * edx = ref;
    uint8_t * ecx = dest;
    int esi = height;
    const int eax = stride;
    const int edi = eax + eax;
    const int ebx = edi + eax;

    for (; esi; edx += edi * 2, ecx += edi * 2, esi -= 4) {
	__m128i xmm0, xmm1, xmm2, xmm3;

	xmm0 = mm_loadl_epi64 (edx);
	xmm1 = mm_loadl_epi64 (edx + eax);
	xmm2 = mm_loadl_epi64 (edx + edi);
	xmm3 = mm_loadl_epi64 (edx + ebx);
	mm_storel_epi64 (ecx, xmm0);
	mm_storel_epi64 (ecx + eax, xmm1);
	mm_storel_epi64 (ecx + edi, xmm2);
	mm_storel_epi64 (ecx + ebx, xmm3);
    }
}

static void MC_put_x_16_sse2 (uint8_t * dest, const uint8_t * ref, int stride,
			      int height)
{
    const uint8_t * edx = ref;
    uint8_t * ecx = dest;
    int esi = height;
    const int eax = stride;
    const int edi = eax + eax;
    const int ebx = edi + eax;

    for (; esi; edx += edi * 2, ecx += edi * 2, esi -= 4) {
	__m128i xmm0, xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7;

	xmm0 = _mm_loadu_si128 ((const __m128i*)edx);
	xmm1 = _mm_loadu_si128 ((const __m128i*)(edx + 1));
	xmm2 = _mm_loadu_si128 ((const __m128i*)(edx + eax));
	xmm3 = _mm_loadu_si128 ((const __m128i*)(edx + eax + 1));
	xmm4 = _mm_loadu_si128 ((const __m128i*)(edx + edi));
	xmm5 = _mm_loadu_si128 ((const __m128i*)(edx + edi + 1));
	xmm6 = _mm_loadu_si128 ((const __m128i*)(edx + ebx));
	xmm7 = _mm_loadu_si128 ((const __m128i*)(edx + ebx + 1));
	xmm0 = _mm_avg_epu8 (xmm0, xmm1);
	xmm2 = _mm_avg_epu8 (xmm2, xmm3);
	xmm4 = _mm_avg_epu8 (xmm4, xmm5);
	xmm6 = _mm_avg_epu8 (xmm6, xmm7);
	_mm_store_si128 ((__m128i*)ecx, xmm0);
	_mm_store_si128 ((__m128i*)(ecx + eax), xmm2);
	_mm_store_si128 ((__m128i*)(ecx + edi), xmm4);
	_mm_store_si128 ((__m128i*)(ecx + ebx), xmm6);
    }
}

static void MC_put_x_8_sse2 (uint8_t * dest, const uint8_t * ref, int stride,
			     int height)
{
    const uint8_t * edx = ref;
    uint8_t * ecx = dest;
    int esi = height;
    const int eax = stride;
    const int edi = eax + eax;
    const int ebx = edi + eax;

    for (; esi; edx += edi * 2, ecx += edi * 2, esi -= 4) {
	__m128i xmm0, xmm1, xmm2, xmm3;

	xmm0 = mm_loadl_epi64 (edx);
	xmm1 = mm_loadl_epi64 (edx + 1);
	xmm0 = mm_loadh_unpackl_epi64 (xmm0, edx + eax);
	xmm1 = mm_loadh_unpackl_epi64 (xmm1, edx + eax + 1);
	xmm2 = mm_loadl_epi64 (edx + edi);
	xmm3 = mm_loadl_epi64 (edx + edi + 1);
	xmm2 = mm_loadh_unpackl_epi64 (xmm2, edx + ebx);
	xmm3 = mm_loadh_unpackl_epi64 (xmm3, edx + ebx + 1);
	xmm0 = _mm_avg_epu8 (xmm0, xmm1);
	xmm2 = _mm_avg_epu8 (xmm2, xmm3);
	mm_storel_epi64 (ecx, xmm0);
	mm_storeh_epi64 (ecx + eax, xmm0);
	mm_storel_epi64 (ecx + edi, xmm2);
	mm_storeh_epi64 (ecx + ebx, xmm2);
    }
}

static void MC_put_y_16_sse2 (uint8_t * dest, const uint8_t * ref, int stride,
			      int height)
{
    const uint8_t * edx = ref;
    uint8_t * ecx = dest;
    int esi = height;
    const int eax = stride;
    const int edi = eax + eax;
    const int ebx = edi + eax;
    __m128i xmm0, xmm1, xmm2, xmm3, xmm4;

    xmm0 = _mm_loadu_si128 ((const __m128i*)edx);

    for (; esi; edx += edi * 2, ecx += edi * 2, esi -= 4) {
	xmm1 = _mm_loadu_si128 ((const __m128i*)(edx + eax));
	xmm2 = _mm_loadu_si128 ((const __m128i*)(edx + edi));
	xmm3 = _mm_loadu_si128 ((const __m128i*)(edx + ebx));
	xmm4 = _mm_loadu_si128 ((const __m128i*)(edx + edi * 2));
	xmm0 = _mm_avg_epu8 (xmm0, xmm1);
	xmm1 = _mm_avg_epu8 (xmm1, xmm2);
	xmm2 = _mm_avg_epu8 (xmm2, xmm3);
	xmm3 = _mm_avg_epu8 (xmm3, xmm4);
	_mm_store_si128 ((__m128i*)ecx, xmm0);
	_mm_store_si128 ((__m128i*)(ecx + eax), xmm1);
	_mm_store_si128 ((__m128i*)(ecx + edi), xmm2);
	_mm_store_si128 ((__m128i*)(ecx + ebx), xmm3);
	xmm0 = xmm4;
    }
}

static void MC_put_y_8_sse2 (uint8_t * dest, const uint8_t * ref, int stride,
			     int height)
{
    const uint8_t * edx = ref;
    uint8_t * ecx = dest;
    int esi = height;
    const int eax = stride;
    const int edi = eax + eax;
    const int ebx = edi + eax;
    __m128i xmm0, xmm1, xmm2, xmm3, xmm4;

    xmm0 = mm_loadl_epi64 (edx);

    for (; esi; edx += edi * 2, ecx += edi * 2, esi -= 4) {
	xmm1 = mm_loadl_epi64 (edx + eax);
	xmm2 = mm_loadl_epi64 (edx + edi);
	xmm3 = mm_loadl_epi64 (edx + ebx);
	xmm4 = mm_loadl_epi64 (edx + edi * 2);
	xmm0 = _mm_avg_epu8 (xmm0, xmm1);
	xmm1 = _mm_avg_epu8 (xmm1, xmm2);
	xmm2 = _mm_avg_epu8 (xmm2, xmm3);
	xmm3 = _mm_avg_epu8 (xmm3, xmm4);
	mm_storel_epi64 (ecx, xmm0);
	mm_storel_epi64 (ecx + eax, xmm1);
	mm_storel_epi64 (ecx + edi, xmm2);
	mm_storel_epi64 (ecx + ebx, xmm3);
	xmm0 = xmm4;
    }
}

static void MC_put_xy_16_sse2 (uint8_t * dest, const uint8_t * ref, int stride,
			       int height)
{
    const uint8_t * edx = ref;
    uint8_t  * ecx = dest;
    int esi = height;
    const int eax = stride;
    const int edi = eax + eax;
    __m128i xmm0, xmm1, xmm2, xmm3, xmm4, xmm5, xmm6;

    xmm6 = _mm_load_si128 ((const __m128i*)const_1_16);
    xmm0 = _mm_loadu_si128 ((const __m128i*)edx);
    xmm1 = _mm_loadu_si128 ((const __m128i*)(edx + 1));

    for (; esi; edx += edi, ecx += edi, esi -= 2) {
	xmm2 = _mm_loadu_si128 ((const __m128i*)(edx + eax));
	xmm3 = _mm_loadu_si128 ((const __m128i*)(edx + eax + 1));
	xmm4 = _mm_loadu_si128 ((const __m128i*)(edx + edi));
	xmm5 = _mm_loadu_si128 ((const __m128i*)(edx + edi + 1));
	xmm0 = _mm_avg_epu8 (xmm0, xmm1);
	xmm2 = _mm_avg_epu8 (xmm2, xmm3);
	xmm1 = xmm5;
	xmm5 = _mm_avg_epu8 (xmm5, xmm4);
	xmm2 = _mm_subs_epu8 (xmm2, xmm6);
	xmm0 = _mm_avg_epu8 (xmm0, xmm2);
	xmm2 = _mm_avg_epu8 (xmm2, xmm5);
	_mm_store_si128 ((__m128i*)ecx, xmm0);
	xmm0 = xmm4;
	_mm_store_si128 ((__m128i*)(ecx + eax), xmm2);
    }
}

static void MC_put_xy_8_sse2 (uint8_t * dest, const uint8_t * ref, int stride,
			      int height)
{
    const uint8_t * edx = ref;
    uint8_t * ecx = dest;
    int esi = height;
    const int eax = stride;
    const int edi = eax + eax;
    __m128i xmm0, xmm1, xmm2, xmm3, xmm4, xmm5, xmm6;

    xmm6 = _mm_load_si128 ((const __m128i*)const_1_16);
    xmm0 = mm_loadl_epi64 (edx);
    xmm1 = mm_loadl_epi64 (edx + 1);

    for (; esi; edx += edi, ecx += edi, esi -= 2) {
	xmm2 = mm_loadl_epi64 (edx + eax);
	xmm3 = mm_loadl_epi64 (edx + eax + 1);
	xmm4 = mm_loadl_epi64 (edx + edi);
	xmm5 = mm_loadl_epi64 (edx + edi + 1);
	xmm0 = _mm_avg_epu8 (xmm0, xmm1);
	xmm2 = _mm_avg_epu8 (xmm2, xmm3);
	xmm1 = xmm5;
	xmm5 = _mm_avg_epu8 (xmm5, xmm4);
	xmm2 = _mm_subs_epu8 (xmm2, xmm6);
	xmm0 = _mm_avg_epu8 (xmm0, xmm2);
	xmm2 = _mm_avg_epu8 (xmm2, xmm5);
	mm_storel_epi64 (ecx, xmm0);
	xmm0 = xmm4;
	mm_storel_epi64 (ecx + eax, xmm2);
    }
}

static void MC_avg_o_16_sse2 (uint8_t * dest, const uint8_t * ref, int stride,
			      int height)
{
    const uint8_t * edx= ref;
    uint8_t * ecx= dest;
    int esi = height;
    const int eax = stride;
    const int edi = eax + eax;
    const int ebx = edi + eax;

    for (; esi; edx += edi * 2, ecx += edi * 2, esi -= 4) {
	__m128i xmm0, xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7;

	xmm0 = _mm_loadu_si128 ((const __m128i*)edx);
	xmm1 = _mm_loadu_si128 ((const __m128i*)(edx + eax));
	xmm2 = _mm_loadu_si128 ((const __m128i*)(edx + edi));
	xmm3 = _mm_loadu_si128 ((const __m128i*)(edx + ebx));
	xmm4 = _mm_load_si128 ((const __m128i*)ecx);
	xmm5 = _mm_load_si128 ((const __m128i*)(ecx + eax));
	xmm6 = _mm_load_si128 ((const __m128i*)(ecx + edi));
	xmm7 = _mm_load_si128 ((const __m128i*)(ecx + ebx));
	xmm0 = _mm_avg_epu8 (xmm0, xmm4);
	xmm1 = _mm_avg_epu8 (xmm1, xmm5);
	xmm2 = _mm_avg_epu8 (xmm2, xmm6);
	xmm3 = _mm_avg_epu8 (xmm3, xmm7);
	_mm_store_si128 ((__m128i*)ecx, xmm0);
	_mm_store_si128 ((__m128i*)(ecx + eax), xmm1);
	_mm_store_si128 ((__m128i*)(ecx + edi), xmm2);
	_mm_store_si128 ((__m128i*)(ecx + ebx), xmm3);
    }
}

static void MC_avg_o_8_sse2 (uint8_t * dest, const uint8_t * ref, int stride,
			     int height)
{
    const uint8_t * edx = ref;
    uint8_t * ecx = dest;
    int esi = height;
    const int eax = stride;
    const int edi = eax + eax;
    const int ebx = edi + eax;

    for (; esi; edx += edi * 2, ecx += edi * 2, esi -= 4) {
	__m128i xmm0, xmm1, xmm2, xmm3;

	xmm0 = mm_loadl_epi64 (edx);
	xmm0 = mm_loadh_unpackl_epi64 (xmm0, edx + eax);
	xmm2 = mm_loadl_epi64 (edx + edi);
	xmm2 = mm_loadh_unpackl_epi64 (xmm2, edx + ebx);
	xmm1 = mm_loadl_epi64 (ecx);
	xmm1 = mm_loadh_unpackl_epi64 (xmm1, ecx + eax);
	xmm3 = mm_loadl_epi64 (ecx + edi);
	xmm3 = mm_loadh_unpackl_epi64 (xmm3, ecx + ebx);
	xmm0 = _mm_avg_epu8 (xmm0, xmm1);
	xmm2 = _mm_avg_epu8 (xmm2, xmm3);
	mm_storel_epi64 (ecx, xmm0);
	mm_storeh_epi64 (ecx + eax, xmm0);
	mm_storel_epi64 (ecx + edi, xmm2);
	mm_storeh_epi64 (ecx + ebx, xmm2);
    }
}

static void MC_avg_x_16_sse2 (uint8_t * dest, const uint8_t * ref, int stride,
			      int height)
{
    const uint8_t * edx = ref;
    uint8_t * ecx = dest;
    int esi = height;
    const int eax = stride;
    const int edi = eax + eax;
    const int ebx = edi + eax;

    for (; esi; edx += edi * 2, ecx += edi * 2, esi -= 4) {
	__m128i xmm0, xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7;

	xmm0 = _mm_loadu_si128 ((const __m128i*)edx);
	xmm1 = _mm_loadu_si128 ((const __m128i*)(edx + 1));
	xmm2 = _mm_loadu_si128 ((const __m128i*)(edx + eax));
	xmm3 = _mm_loadu_si128 ((const __m128i*)(edx + eax + 1));
	xmm4 = _mm_loadu_si128 ((const __m128i*)(edx + edi));
	xmm5 = _mm_loadu_si128 ((const __m128i*)(edx + edi + 1));
	xmm6 = _mm_loadu_si128 ((const __m128i*)(edx + ebx));
	xmm7 = _mm_loadu_si128 ((const __m128i*)(edx + ebx + 1));
	xmm0 = _mm_avg_epu8 (xmm0, xmm1);
	xmm2 = _mm_avg_epu8 (xmm2, xmm3);
	xmm4 = _mm_avg_epu8 (xmm4, xmm5);
	xmm6 = _mm_avg_epu8 (xmm6, xmm7);
	xmm1 = _mm_load_si128 ((const __m128i*)ecx);
	xmm3 = _mm_load_si128 ((const __m128i*)(ecx + eax));
	xmm5 = _mm_load_si128 ((const __m128i*)(ecx + edi));
	xmm7 = _mm_load_si128 ((const __m128i*)(ecx + ebx));
	xmm0 = _mm_avg_epu8 (xmm0, xmm1);
	xmm2 = _mm_avg_epu8 (xmm2, xmm3);
	xmm4 = _mm_avg_epu8 (xmm4, xmm5);
	xmm6 = _mm_avg_epu8 (xmm6, xmm7);
	_mm_store_si128 ((__m128i*)ecx, xmm0);
	_mm_store_si128 ((__m128i*)(ecx + eax), xmm2);
	_mm_store_si128 ((__m128i*)(ecx + edi), xmm4);
	_mm_store_si128 ((__m128i*)(ecx + ebx), xmm6);
    }
}

static void MC_avg_x_8_sse2 (uint8_t * dest, const uint8_t * ref, int stride,
			     int height)
{
    const uint8_t * edx = ref;
    uint8_t * ecx = dest;
    int esi = height;
    const int eax = stride;
    const int edi = eax + eax;
    const int ebx = edi + eax;

    for (; esi; edx += edi * 2, ecx += edi * 2, esi -= 4) {
	__m128i xmm0, xmm1, xmm2, xmm3, xmm4, xmm5;

	xmm0 = mm_loadl_epi64 (edx);
	xmm1 = mm_loadl_epi64 (edx + 1);
	xmm0 = mm_loadh_unpackl_epi64 (xmm0, edx + eax);
	xmm1 = mm_loadh_unpackl_epi64 (xmm1, edx + eax + 1);
	xmm2 = mm_loadl_epi64 (edx + edi);
	xmm3 = mm_loadl_epi64 (edx + edi + 1);
	xmm2 = mm_loadh_unpackl_epi64 (xmm2, edx + ebx);
	xmm3 = mm_loadh_unpackl_epi64 (xmm3, edx + ebx + 1);
	xmm4 = mm_loadl_epi64 (ecx);
	xmm4 = mm_loadh_unpackl_epi64 (xmm4, ecx + eax);
	xmm5 = mm_loadl_epi64 (ecx + edi);
	xmm5 = mm_loadh_unpackl_epi64 (xmm5, ecx + ebx);
	xmm0 = _mm_avg_epu8 (xmm0, xmm1);
	xmm2 = _mm_avg_epu8 (xmm2, xmm3);
	xmm0 = _mm_avg_epu8 (xmm0, xmm4);
	xmm2 = _mm_avg_epu8 (xmm2, xmm5);
	mm_storel_epi64 (ecx, xmm0);
	mm_storeh_epi64 (ecx + eax, xmm0);
	mm_storel_epi64 (ecx + edi, xmm2);
	mm_storeh_epi64 (ecx + ebx, xmm2);
    }
}

static void MC_avg_y_16_sse2 (uint8_t * dest, const uint8_t * ref, int stride,
			      int height)
{
    const uint8_t * edx = ref;
    uint8_t * ecx = dest;
    int esi = height;
    const int eax = stride;
    const int edi = eax + eax;
    const int ebx = edi + eax;
    __m128i xmm0, xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7;

    xmm0 = _mm_loadu_si128 ((const __m128i*)edx);

    for (; esi; edx += edi * 2, ecx += edi * 2, esi -= 4) {
	xmm1 = _mm_loadu_si128 ((const __m128i*)(edx + eax));
	xmm2 = _mm_loadu_si128 ((const __m128i*)(edx + edi));
	xmm3 = _mm_loadu_si128 ((const __m128i*)(edx + ebx));
	xmm4 = _mm_loadu_si128 ((const __m128i*)(edx + edi * 2));
	xmm0 = _mm_avg_epu8 (xmm0, xmm1);
	xmm1 = _mm_avg_epu8 (xmm1, xmm2);
	xmm2 = _mm_avg_epu8 (xmm2, xmm3);
	xmm3 = _mm_avg_epu8 (xmm3, xmm4);
	xmm5 = _mm_load_si128 ((const __m128i*)ecx);
	xmm6 = _mm_load_si128 ((const __m128i*)(ecx + eax));
	xmm7 = _mm_load_si128 ((const __m128i*)(ecx + edi));
	xmm0 = _mm_avg_epu8 (xmm0, xmm5);
	xmm5 = _mm_load_si128 ((const __m128i*)(ecx + ebx));
	xmm1 = _mm_avg_epu8 (xmm1, xmm6);
	xmm2 = _mm_avg_epu8 (xmm2, xmm7);
	xmm3 = _mm_avg_epu8 (xmm3, xmm5);
	_mm_store_si128 ((__m128i*)ecx, xmm0);
	_mm_store_si128 ((__m128i*)(ecx + eax), xmm1);
	_mm_store_si128 ((__m128i*)(ecx + edi), xmm2);
	_mm_store_si128 ((__m128i*)(ecx + ebx), xmm3);
	xmm0 = xmm4;
    }
}

static void MC_avg_y_8_sse2 (uint8_t * dest, const uint8_t * ref, int stride,
			     int height)
{
    const uint8_t * edx = ref;
    uint8_t * ecx = dest;
    int esi = height;
    const int eax = stride;
    const int edi = eax+eax;
    const int ebx = edi+eax;
    __m128i xmm0, xmm1, xmm2, xmm3, xmm4, xmm5;

    xmm0 = mm_loadh_epi64 (edx);
    xmm0 = mm_loadl_unpackh_epi64 (xmm0, edx + eax);

    for (; esi; edx += edi * 2, ecx += edi * 2, esi -= 4) {
	xmm1 = mm_movelh_epi64 (xmm0);
	xmm1 = mm_loadl_unpackh_epi64 (xmm1, edx + edi);
	xmm2 = mm_movelh_epi64 (xmm1);
	xmm2 = mm_loadl_unpackh_epi64 (xmm2, edx + ebx);
	xmm3 = mm_movelh_epi64 (xmm2);
	xmm3 = mm_loadl_unpackh_epi64 (xmm3, edx + edi * 2);
	xmm4 = mm_loadh_epi64 (ecx);
	xmm4 = mm_loadl_unpackh_epi64 (xmm4, ecx + eax);
	xmm5 = mm_loadh_epi64 (ecx + edi);
	xmm5 = mm_loadl_unpackh_epi64 (xmm5, ecx + ebx);
	xmm0 = _mm_avg_epu8 (xmm0, xmm1);
	xmm2 = _mm_avg_epu8 (xmm2, xmm3);
	xmm0 = _mm_avg_epu8 (xmm0, xmm4);
	xmm2 = _mm_avg_epu8 (xmm2, xmm5);
	mm_storeh_epi64 (ecx, xmm0);
	mm_storel_epi64 (ecx + eax, xmm0);
	mm_storeh_epi64 (ecx + edi, xmm2);
	mm_storel_epi64 (ecx + ebx, xmm2);
	xmm0 = xmm3;
    }
}

static void MC_avg_xy_16_sse2 (uint8_t * dest, const uint8_t * ref, int stride,
			       int height)
{
    const uint8_t * edx = ref;
    uint8_t * ecx = dest;
    int esi = height;
    const int eax = stride;
    const int edi = eax + eax;
    __m128i xmm0, xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7;

    xmm7 = _mm_load_si128((const __m128i*)const_1_16);
    xmm0 = _mm_loadu_si128 ((const __m128i*)edx);
    xmm1 = _mm_loadu_si128 ((const __m128i*)(edx + 1));

    for (; esi; edx += edi, ecx += edi, esi -= 2) {
	xmm2 = _mm_loadu_si128 ((const __m128i*)(edx + eax));
	xmm3 = _mm_loadu_si128 ((const __m128i*)(edx + eax + 1));
	xmm4 = _mm_loadu_si128 ((const __m128i*)(edx + edi));
	xmm5 = _mm_loadu_si128 ((const __m128i*)(edx + edi + 1));
	xmm0 = _mm_avg_epu8 (xmm0, xmm1);
	xmm2 = _mm_avg_epu8 (xmm2, xmm3);
	xmm3 = _mm_load_si128 ((const __m128i*)ecx);
	xmm6 = _mm_load_si128 ((const __m128i*)(ecx + eax));
	xmm1 = xmm5;
	xmm5 = _mm_avg_epu8 (xmm5, xmm4);
	xmm2 = _mm_subs_epu8 (xmm2, xmm7);
	xmm0 = _mm_avg_epu8 (xmm0, xmm2);
	xmm2 = _mm_avg_epu8 (xmm2, xmm5);
	xmm0 = _mm_avg_epu8 (xmm0, xmm3);
	xmm2 = _mm_avg_epu8 (xmm2, xmm6);
	_mm_store_si128 ((__m128i*)ecx, xmm0);
	xmm0 = xmm4;
	_mm_store_si128 ((__m128i*)(ecx + eax), xmm2);
    }
}

static void MC_avg_xy_8_sse2 (uint8_t * dest, const uint8_t * ref, int stride,
			      int height)
{
    const uint8_t * edx = ref;
    uint8_t * ecx = dest;
    int esi = height;
    const int eax = stride;
    const int edi = eax + eax;
    __m128i xmm0, xmm1, xmm2, xmm3, xmm4, xmm5;

    xmm5 = _mm_load_si128((const __m128i*)const_1_16);
    xmm0 = mm_loadh_epi64 (edx);
    xmm0 = mm_loadl_unpackh_epi64 (xmm0, edx + eax);
    xmm2 = mm_loadh_epi64 (edx + 1);
    xmm2 = mm_loadl_unpackh_epi64 (xmm2, edx + eax + 1);

    for (; esi; edx += edi, ecx += edi, esi -= 2) {
	xmm1 = mm_loadh_epi64 (edx + eax);
	xmm1 = mm_loadl_unpackh_epi64 (xmm1, edx + edi);
	xmm3 = mm_loadh_epi64 (edx + eax + 1);
	xmm3 = mm_loadl_unpackh_epi64 (xmm3, edx + edi + 1);
	xmm0 = _mm_avg_epu8 (xmm0, xmm1);
	xmm2 = _mm_avg_epu8 (xmm2, xmm3);
	xmm0 = _mm_subs_epu8 (xmm0, xmm5);
	xmm0 = _mm_avg_epu8 (xmm0, xmm2);
	xmm4 = mm_loadh_epi64 (ecx);
	xmm4 = mm_loadl_unpackh_epi64 (xmm4, ecx + eax);
	xmm0 = _mm_avg_epu8 (xmm0, xmm4);
	mm_storeh_epi64 (ecx, xmm0);
	mm_storel_epi64 (ecx + eax, xmm0);
	xmm0 = xmm1;
	xmm2 = xmm3;
    }
}


MPEG2_MC_EXTERN(sse2)

#endif
