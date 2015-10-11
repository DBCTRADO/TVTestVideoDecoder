/*
 * slice.c
 * Copyright (C) 2015      DBCTRADO
 * Copyright (C) 2000-2003 Michel Lespinasse <walken@zoy.org>
 * Copyright (C) 2003      Peter Gubanov <peter@elecard.net.ru>
 * Copyright (C) 1999-2000 Aaron Holtzman <aholtzma@ess.engr.uvic.ca>
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

#include <inttypes.h>

#include "mpeg2.h"
#include "attributes.h"
#include "mpeg2_internal.h"

extern mpeg2_mc_t mpeg2_mc;
extern void (* mpeg2_idct_copy) (int16_t * block, uint8_t * dest, int stride);
extern void (* mpeg2_idct_add) (int last, int16_t * block,
				uint8_t * dest, int stride);
extern void (* mpeg2_cpu_state_save) (cpu_state_t * state);
extern void (* mpeg2_cpu_state_restore) (cpu_state_t * state);

#include "vlc.h"

#define SLICE_NEEDBITS(slice)	\
    bitstream_needbits(&((slice)->bitstream))
#define SLICE_DUMPBITS(slice, num)	\
    bitstream_dumpbits(&((slice)->bitstream), num)
#define SLICE_UBITS(slice, num)	\
    bitstream_ubits(&((slice)->bitstream), num)
#define SLICE_SBITS(slice, num)	\
    bitstream_sbits(&((slice)->bitstream), num)

static inline int get_macroblock_modes (mpeg2_decoder_t * const decoder,
					slice_decode_t * const slice)
{
    int macroblock_modes;
    const MBtab * tab;

    switch (decoder->coding_type) {
    case I_TYPE:

	tab = MB_I + SLICE_UBITS (slice, 1);
	SLICE_DUMPBITS (slice, tab->len);
	macroblock_modes = tab->modes;

	if ((! (decoder->frame_pred_frame_dct)) &&
	    (decoder->picture_structure == FRAME_PICTURE)) {
	    macroblock_modes |= SLICE_UBITS (slice, 1) * DCT_TYPE_INTERLACED;
	    SLICE_DUMPBITS (slice, 1);
	}

	return macroblock_modes;

    case P_TYPE:

	tab = MB_P + SLICE_UBITS (slice, 5);
	SLICE_DUMPBITS (slice, tab->len);
	macroblock_modes = tab->modes;

	if (decoder->picture_structure != FRAME_PICTURE) {
	    if (macroblock_modes & MACROBLOCK_MOTION_FORWARD) {
		macroblock_modes |=
		    SLICE_UBITS (slice, 2) << MOTION_TYPE_SHIFT;
		SLICE_DUMPBITS (slice, 2);
	    }
	    return macroblock_modes | MACROBLOCK_MOTION_FORWARD;
	} else if (decoder->frame_pred_frame_dct) {
	    if (macroblock_modes & MACROBLOCK_MOTION_FORWARD)
		macroblock_modes |= MC_FRAME << MOTION_TYPE_SHIFT;
	    return macroblock_modes | MACROBLOCK_MOTION_FORWARD;
	} else {
	    if (macroblock_modes & MACROBLOCK_MOTION_FORWARD) {
		macroblock_modes |=
		    SLICE_UBITS (slice, 2) << MOTION_TYPE_SHIFT;
		SLICE_DUMPBITS (slice, 2);
	    }
	    if (macroblock_modes & (MACROBLOCK_INTRA | MACROBLOCK_PATTERN)) {
		macroblock_modes |=
		    SLICE_UBITS (slice, 1) * DCT_TYPE_INTERLACED;
		SLICE_DUMPBITS (slice, 1);
	    }
	    return macroblock_modes | MACROBLOCK_MOTION_FORWARD;
	}

    case B_TYPE:

	tab = MB_B + SLICE_UBITS (slice, 6);
	SLICE_DUMPBITS (slice, tab->len);
	macroblock_modes = tab->modes;

	if (decoder->picture_structure != FRAME_PICTURE) {
	    if (! (macroblock_modes & MACROBLOCK_INTRA)) {
		macroblock_modes |=
		    SLICE_UBITS (slice, 2) << MOTION_TYPE_SHIFT;
		SLICE_DUMPBITS (slice, 2);
	    }
	    return macroblock_modes;
	} else if (decoder->frame_pred_frame_dct) {
	    /* if (! (macroblock_modes & MACROBLOCK_INTRA)) */
	    macroblock_modes |= MC_FRAME << MOTION_TYPE_SHIFT;
	    return macroblock_modes;
	} else {
	    if (macroblock_modes & MACROBLOCK_INTRA)
		goto intra;
	    macroblock_modes |= SLICE_UBITS (slice, 2) << MOTION_TYPE_SHIFT;
	    SLICE_DUMPBITS (slice, 2);
	    if (macroblock_modes & (MACROBLOCK_INTRA | MACROBLOCK_PATTERN)) {
	    intra:
		macroblock_modes |=
		    SLICE_UBITS (slice, 1) * DCT_TYPE_INTERLACED;
		SLICE_DUMPBITS (slice, 1);
	    }
	    return macroblock_modes;
	}

    case D_TYPE:

	SLICE_DUMPBITS (slice, 1);
	return MACROBLOCK_INTRA;

    default:
	return 0;
    }
}

static inline void get_quantizer_scale (mpeg2_decoder_t * const decoder,
					slice_decode_t * const slice)
{
    int quantizer_scale_code;

    quantizer_scale_code = SLICE_UBITS (slice, 5);
    SLICE_DUMPBITS (slice, 5);

    slice->quantizer_matrix[0] =
	decoder->quantizer_prescale[0][quantizer_scale_code];
    slice->quantizer_matrix[1] =
	decoder->quantizer_prescale[1][quantizer_scale_code];
    slice->quantizer_matrix[2] =
	decoder->chroma_quantizer[0][quantizer_scale_code];
    slice->quantizer_matrix[3] =
	decoder->chroma_quantizer[1][quantizer_scale_code];
}

static inline int get_motion_delta (mpeg2_decoder_t * const decoder,
				    slice_decode_t * const slice,
				    const int f_code)
{
    int delta;
    int sign;
    const MVtab * tab;

    if (slice->bitstream.buf & 0x80000000) {
	SLICE_DUMPBITS (slice, 1);
	return 0;
    } else if (slice->bitstream.buf >= 0x0c000000) {

	tab = MV_4 + SLICE_UBITS (slice, 4);
	delta = (tab->delta << f_code) + 1;
	slice->bitstream.bits += tab->len + f_code + 1;
	slice->bitstream.buf <<= tab->len;

	sign = SLICE_SBITS (slice, 1);
	slice->bitstream.buf <<= 1;

	if (f_code)
	    delta += SLICE_UBITS (slice, f_code);
	slice->bitstream.buf <<= f_code;

	return (delta ^ sign) - sign;

    } else {

	tab = MV_10 + SLICE_UBITS (slice, 10);
	delta = (tab->delta << f_code) + 1;
	slice->bitstream.bits += tab->len + 1;
	slice->bitstream.buf <<= tab->len;

	sign = SLICE_SBITS (slice, 1);
	slice->bitstream.buf <<= 1;

	if (f_code) {
	    SLICE_NEEDBITS (slice);
	    delta += SLICE_UBITS (slice, f_code);
	    SLICE_DUMPBITS (slice, f_code);
	}

	return (delta ^ sign) - sign;

    }
}

static inline int bound_motion_vector (const int vector, const int f_code)
{
    return ((int32_t)vector << (27 - f_code)) >> (27 - f_code);
}

static inline int get_dmv (slice_decode_t * const slice)
{
    const DMVtab * tab;

    tab = DMV_2 + SLICE_UBITS (slice, 2);
    SLICE_DUMPBITS (slice, tab->len);
    return tab->dmv;
}

static inline int get_coded_block_pattern (slice_decode_t * const slice)
{
    const CBPtab * tab;

    SLICE_NEEDBITS (slice);

    if (slice->bitstream.buf >= 0x20000000) {

	tab = CBP_7 + (SLICE_UBITS (slice, 7) - 16);
	SLICE_DUMPBITS (slice, tab->len);
	return tab->cbp;

    } else {

	tab = CBP_9 + SLICE_UBITS (slice, 9);
	SLICE_DUMPBITS (slice, tab->len);
	return tab->cbp;
    }
}

static inline int get_luma_dc_dct_diff (slice_decode_t * const slice)
{
    const DCtab * tab;
    int size;
    int dc_diff;

    if (slice->bitstream.buf < 0xf8000000) {
	tab = DC_lum_5 + SLICE_UBITS (slice, 5);
	size = tab->size;
	if (size) {
	    slice->bitstream.bits += tab->len + size;
	    slice->bitstream.buf <<= tab->len;
	    dc_diff =
		SLICE_UBITS (slice, size) -
		UBITS (SBITS (~slice->bitstream.buf, 1), size);
	    slice->bitstream.buf <<= size;
	    return dc_diff;
	} else {
	    SLICE_DUMPBITS (slice, 3);
	    return 0;
	}
    } else {
	tab = DC_long + (SLICE_UBITS (slice, 9) - 0x1e0);
	size = tab->size;
	SLICE_DUMPBITS (slice, tab->len);
	SLICE_NEEDBITS (slice);
	dc_diff = SLICE_UBITS (slice, size) - UBITS (SBITS (~slice->bitstream.buf, 1), size);
	SLICE_DUMPBITS (slice, size);
	return dc_diff;
    }
}

static inline int get_chroma_dc_dct_diff (slice_decode_t * const slice)
{
    const DCtab * tab;
    int size;
    int dc_diff;

    if (slice->bitstream.buf < 0xf8000000) {
	tab = DC_chrom_5 + SLICE_UBITS (slice, 5);
	size = tab->size;
	if (size) {
	    slice->bitstream.bits += tab->len + size;
	    slice->bitstream.buf <<= tab->len;
	    dc_diff =
		SLICE_UBITS (slice, size) -
		UBITS (SBITS (~slice->bitstream.buf, 1), size);
	    slice->bitstream.buf <<= size;
	    return dc_diff;
	} else {
	    SLICE_DUMPBITS (slice, 2);
	    return 0;
	}
    } else {
	tab = DC_long + (SLICE_UBITS (slice, 10) - 0x3e0);
	size = tab->size;
	SLICE_DUMPBITS (slice, tab->len + 1);
	SLICE_NEEDBITS (slice);
	dc_diff = SLICE_UBITS (slice, size) - UBITS (SBITS (~slice->bitstream.buf, 1), size);
	SLICE_DUMPBITS (slice, size);
	return dc_diff;
    }
}

#define SATURATE(val)					\
do {							\
    if (unlikely((uint32_t)(val + 2048) > 4095))	\
	val = SBITS (val, 1) ^ 2047;			\
} while (0)

static void get_intra_block_B14 (mpeg2_decoder_t * restrict const decoder,
				 slice_decode_t * restrict const slice,
				 const uint16_t * restrict const quant_matrix)
{
    int i;
    int j;
    int val;
    const uint8_t * restrict const scan = decoder->scan;
    int mismatch;
    const DCTtab * restrict tab;
    bitstream_t bitstream;
    int16_t * restrict const dest = slice->DCTblock;

    i = 0;
    mismatch = ~dest[0];

    bitstream = slice->bitstream;
    bitstream_needbits (&bitstream);

    while (1) {
	if (bitstream.buf >= 0x28000000) {

	    tab = DCT_B14AC_5 + (UBITS (bitstream.buf, 5) - 5);

	    i += tab->run;
	    if (i >= 64)
		break;	/* end of block */

	normal_code:
	    j = scan[i];
	    bitstream.buf <<= tab->len;
	    bitstream.bits += tab->len + 1;
	    val = (tab->level * quant_matrix[j]) >> 4;

	    /* if (bitstream_get (1)) val = -val; */
	    val = (val ^ SBITS (bitstream.buf, 1)) - SBITS (bitstream.buf, 1);

	    SATURATE (val);
	    dest[j] = val;
	    mismatch ^= val;

	    bitstream.buf <<= 1;
	    bitstream_needbits (&bitstream);

	    continue;

	} else if (bitstream.buf >= 0x04000000) {

	    tab = DCT_B14_8 + (UBITS (bitstream.buf, 8) - 4);

	    i += tab->run;
	    if (i < 64)
		goto normal_code;

	    /* escape code */

	    i += UBITS (bitstream.buf << 6, 6) - 64;
	    if (i >= 64)
		break;	/* illegal, check needed to avoid buffer overflow */

	    j = scan[i];

	    bitstream_dumpbits (&bitstream, 12);
	    bitstream_needbits (&bitstream);
	    val = (SBITS (bitstream.buf, 12) * quant_matrix[j]) / 16;

	    SATURATE (val);
	    dest[j] = val;
	    mismatch ^= val;

	    bitstream_dumpbits (&bitstream, 12);
	    bitstream_needbits (&bitstream);

	    continue;

	} else if (bitstream.buf >= 0x02000000) {
	    tab = DCT_B14_10 + (UBITS (bitstream.buf, 10) - 8);
	    i += tab->run;
	    if (i < 64)
		goto normal_code;
	} else if (bitstream.buf >= 0x00800000) {
	    tab = DCT_13 + (UBITS (bitstream.buf, 13) - 16);
	    i += tab->run;
	    if (i < 64)
		goto normal_code;
	} else if (bitstream.buf >= 0x00200000) {
	    tab = DCT_15 + (UBITS (bitstream.buf, 15) - 16);
	    i += tab->run;
	    if (i < 64)
		goto normal_code;
	} else {
	    tab = DCT_16 + UBITS (bitstream.buf, 16);
	    bitstream.buf <<= 16;
	    bitstream_getword(&bitstream, bitstream.bits + 16);
	    i += tab->run;
	    if (i < 64)
		goto normal_code;
	}
	break;	/* illegal, check needed to avoid buffer overflow */
    }
    dest[63] ^= mismatch & 1;
    bitstream_dumpbits (&bitstream, tab->len);	/* dump end of block code */
    slice->bitstream = bitstream;
}

static void get_intra_block_B15 (mpeg2_decoder_t * restrict const decoder,
				 slice_decode_t * restrict const slice,
				 const uint16_t * restrict const quant_matrix)
{
    int i;
    int j;
    int val;
    const uint8_t * restrict const scan = decoder->scan;
    int mismatch;
    const DCTtab * restrict tab;
    bitstream_t bitstream;
    int16_t * restrict const dest = slice->DCTblock;

    i = 0;
    mismatch = ~dest[0];

    bitstream = slice->bitstream;
    bitstream_needbits (&bitstream);

    while (1) {
	if (bitstream.buf >= 0x04000000) {

	    tab = DCT_B15_8 + (UBITS (bitstream.buf, 8) - 4);

	    i += tab->run;
	    if (i < 64) {

	    normal_code:
		j = scan[i];
		bitstream.buf <<= tab->len;
		bitstream.bits += tab->len + 1;
		val = (tab->level * quant_matrix[j]) >> 4;

		/* if (bitstream_get (1)) val = -val; */
		val = (val ^ SBITS (bitstream.buf, 1)) -
		       SBITS (bitstream.buf, 1);

		SATURATE (val);
		dest[j] = val;
		mismatch ^= val;

		bitstream.buf <<= 1;
		bitstream_needbits (&bitstream);

		continue;

	    } else {

		/* end of block. I commented out this code because if we */
		/* do not exit here we will still exit at the later test :) */

		/* if (i >= 128) break;	*/	/* end of block */

		/* escape code */

		i += UBITS (bitstream.buf << 6, 6) - 64;
		if (i >= 64)
		    break;	/* illegal, check against buffer overflow */

		j = scan[i];

		bitstream_dumpbits (&bitstream, 12);
		bitstream_needbits (&bitstream);
		val = (SBITS (bitstream.buf, 12) * quant_matrix[j]) / 16;

		SATURATE (val);
		dest[j] = val;
		mismatch ^= val;

		bitstream_dumpbits (&bitstream, 12);
		bitstream_needbits (&bitstream);

		continue;

	    }
	} else if (bitstream.buf >= 0x02000000) {
	    tab = DCT_B15_10 + (UBITS (bitstream.buf, 10) - 8);
	    i += tab->run;
	    if (i < 64)
		goto normal_code;
	} else if (bitstream.buf >= 0x00800000) {
	    tab = DCT_13 + (UBITS (bitstream.buf, 13) - 16);
	    i += tab->run;
	    if (i < 64)
		goto normal_code;
	} else if (bitstream.buf >= 0x00200000) {
	    tab = DCT_15 + (UBITS (bitstream.buf, 15) - 16);
	    i += tab->run;
	    if (i < 64)
		goto normal_code;
	} else {
	    tab = DCT_16 + UBITS (bitstream.buf, 16);
	    bitstream.buf <<= 16;
	    bitstream_getword (&bitstream, bitstream.bits + 16);
	    i += tab->run;
	    if (i < 64)
		goto normal_code;
	}
	break;	/* illegal, check needed to avoid buffer overflow */
    }
    dest[63] ^= mismatch & 1;
    bitstream_dumpbits (&bitstream, tab->len);	/* dump end of block code */
    slice->bitstream = bitstream;
}

static int get_non_intra_block (mpeg2_decoder_t * restrict const decoder,
				slice_decode_t * restrict const slice,
				const uint16_t * restrict const quant_matrix)
{
    int i;
    int j;
    int val;
    const uint8_t * restrict const scan = decoder->scan;
    int mismatch;
    const DCTtab * restrict tab;
    bitstream_t bitstream;
    int16_t * restrict const dest = slice->DCTblock;

    i = -1;
    mismatch = 1;

    bitstream = slice->bitstream;
    bitstream_needbits (&bitstream);
    if (bitstream.buf >= 0x28000000) {
	tab = DCT_B14DC_5 + (UBITS (bitstream.buf, 5) - 5);
	goto entry_1;
    } else
	goto entry_2;

    while (1) {
	if (bitstream.buf >= 0x28000000) {

	    tab = DCT_B14AC_5 + (UBITS (bitstream.buf, 5) - 5);

	entry_1:
	    i += tab->run;
	    if (i >= 64)
		break;	/* end of block */

	normal_code:
	    j = scan[i];
	    bitstream.buf <<= tab->len;
	    bitstream.bits += tab->len + 1;
	    val = ((2 * tab->level + 1) * quant_matrix[j]) >> 5;

	    /* if (bitstream_get (1)) val = -val; */
	    val = (val ^ SBITS (bitstream.buf, 1)) - SBITS (bitstream.buf, 1);

	    SATURATE (val);
	    dest[j] = val;
	    mismatch ^= val;

	    bitstream.buf <<= 1;
	    bitstream_needbits (&bitstream);

	    continue;

	}

    entry_2:
	if (bitstream.buf >= 0x04000000) {

	    tab = DCT_B14_8 + (UBITS (bitstream.buf, 8) - 4);

	    i += tab->run;
	    if (i < 64)
		goto normal_code;

	    /* escape code */

	    i += UBITS (bitstream.buf << 6, 6) - 64;
	    if (i >= 64)
		break;	/* illegal, check needed to avoid buffer overflow */

	    j = scan[i];

	    bitstream_dumpbits (&bitstream, 12);
	    bitstream_needbits (&bitstream);
	    val = 2 * (SBITS (bitstream.buf, 12) +
		       SBITS (bitstream.buf, 1)) + 1;
	    val = (val * quant_matrix[j]) / 32;

	    SATURATE (val);
	    dest[j] = val;
	    mismatch ^= val;

	    bitstream_dumpbits (&bitstream, 12);
	    bitstream_needbits (&bitstream);

	    continue;

	} else if (bitstream.buf >= 0x02000000) {
	    tab = DCT_B14_10 + (UBITS (bitstream.buf, 10) - 8);
	    i += tab->run;
	    if (i < 64)
		goto normal_code;
	} else if (bitstream.buf >= 0x00800000) {
	    tab = DCT_13 + (UBITS (bitstream.buf, 13) - 16);
	    i += tab->run;
	    if (i < 64)
		goto normal_code;
	} else if (bitstream.buf >= 0x00200000) {
	    tab = DCT_15 + (UBITS (bitstream.buf, 15) - 16);
	    i += tab->run;
	    if (i < 64)
		goto normal_code;
	} else {
	    tab = DCT_16 + UBITS (bitstream.buf, 16);
	    bitstream.buf <<= 16;
	    bitstream_getword (&bitstream, bitstream.bits + 16);
	    i += tab->run;
	    if (i < 64)
		goto normal_code;
	}
	break;	/* illegal, check needed to avoid buffer overflow */
    }
    dest[63] ^= mismatch & 1;
    bitstream_dumpbits (&bitstream, tab->len);	/* dump end of block code */
    slice->bitstream = bitstream;
    return i;
}

static void get_mpeg1_intra_block (mpeg2_decoder_t * restrict const decoder,
				   slice_decode_t * restrict const slice)
{
    int i;
    int j;
    int val;
    const uint8_t * restrict const scan = decoder->scan;
    const uint16_t * restrict const quant_matrix = slice->quantizer_matrix[0];
    const DCTtab * restrict tab;
    bitstream_t bitstream;
    int16_t * restrict const dest = slice->DCTblock;

    i = 0;

    bitstream = slice->bitstream;
    bitstream_needbits (&bitstream);

    while (1) {
	if (bitstream.buf >= 0x28000000) {

	    tab = DCT_B14AC_5 + (UBITS (bitstream.buf, 5) - 5);

	    i += tab->run;
	    if (i >= 64)
		break;	/* end of block */

	normal_code:
	    j = scan[i];
	    bitstream.buf <<= tab->len;
	    bitstream.bits += tab->len + 1;
	    val = (tab->level * quant_matrix[j]) >> 4;

	    /* oddification */
	    val = (val - 1) | 1;

	    /* if (bitstream_get (1)) val = -val; */
	    val = (val ^ SBITS (bitstream.buf, 1)) - SBITS (bitstream.buf, 1);

	    SATURATE (val);
	    dest[j] = val;

	    bitstream.buf <<= 1;
	    bitstream_needbits (&bitstream);

	    continue;

	} else if (bitstream.buf >= 0x04000000) {

	    tab = DCT_B14_8 + (UBITS (bitstream.buf, 8) - 4);

	    i += tab->run;
	    if (i < 64)
		goto normal_code;

	    /* escape code */

	    i += UBITS (bitstream.buf << 6, 6) - 64;
	    if (i >= 64)
		break;	/* illegal, check needed to avoid buffer overflow */

	    j = scan[i];

	    bitstream_dumpbits (&bitstream, 12);
	    bitstream_needbits (&bitstream);
	    val = SBITS (bitstream.buf, 8);
	    if (! (val & 0x7f)) {
		bitstream_dumpbits (&bitstream, 8);
		val = UBITS (bitstream.buf, 8) + 2 * val;
	    }
	    val = (val * quant_matrix[j]) / 16;

	    /* oddification */
	    val = (val + ~SBITS (val, 1)) | 1;

	    SATURATE (val);
	    dest[j] = val;

	    bitstream_dumpbits (&bitstream, 8);
	    bitstream_needbits (&bitstream);

	    continue;

	} else if (bitstream.buf >= 0x02000000) {
	    tab = DCT_B14_10 + (UBITS (bitstream.buf, 10) - 8);
	    i += tab->run;
	    if (i < 64)
		goto normal_code;
	} else if (bitstream.buf >= 0x00800000) {
	    tab = DCT_13 + (UBITS (bitstream.buf, 13) - 16);
	    i += tab->run;
	    if (i < 64)
		goto normal_code;
	} else if (bitstream.buf >= 0x00200000) {
	    tab = DCT_15 + (UBITS (bitstream.buf, 15) - 16);
	    i += tab->run;
	    if (i < 64)
		goto normal_code;
	} else {
	    tab = DCT_16 + UBITS (bitstream.buf, 16);
	    bitstream.buf <<= 16;
	    bitstream_getword (&bitstream, bitstream.bits + 16);
	    i += tab->run;
	    if (i < 64)
		goto normal_code;
	}
	break;	/* illegal, check needed to avoid buffer overflow */
    }
    bitstream_dumpbits (&bitstream, tab->len);	/* dump end of block code */
    slice->bitstream = bitstream;
}

static int get_mpeg1_non_intra_block (mpeg2_decoder_t * restrict const decoder,
				      slice_decode_t * restrict const slice)
{
    int i;
    int j;
    int val;
    const uint8_t * restrict const scan = decoder->scan;
    const uint16_t * restrict const quant_matrix = slice->quantizer_matrix[1];
    const DCTtab * restrict tab;
    bitstream_t bitstream;
    int16_t * restrict const dest = slice->DCTblock;

    i = -1;

    bitstream = slice->bitstream;
    bitstream_needbits (&bitstream);
    if (bitstream.buf >= 0x28000000) {
	tab = DCT_B14DC_5 + (UBITS (bitstream.buf, 5) - 5);
	goto entry_1;
    } else
	goto entry_2;

    while (1) {
	if (bitstream.buf >= 0x28000000) {

	    tab = DCT_B14AC_5 + (UBITS (bitstream.buf, 5) - 5);

	entry_1:
	    i += tab->run;
	    if (i >= 64)
		break;	/* end of block */

	normal_code:
	    j = scan[i];
	    bitstream.buf <<= tab->len;
	    bitstream.bits += tab->len + 1;
	    val = ((2 * tab->level + 1) * quant_matrix[j]) >> 5;

	    /* oddification */
	    val = (val - 1) | 1;

	    /* if (bitstream_get (1)) val = -val; */
	    val = (val ^ SBITS (bitstream.buf, 1)) - SBITS (bitstream.buf, 1);

	    SATURATE (val);
	    dest[j] = val;

	    bitstream.buf <<= 1;
	    bitstream_needbits(&bitstream);

	    continue;

	}

    entry_2:
	if (bitstream.buf >= 0x04000000) {

	    tab = DCT_B14_8 + (UBITS (bitstream.buf, 8) - 4);

	    i += tab->run;
	    if (i < 64)
		goto normal_code;

	    /* escape code */

	    i += UBITS (bitstream.buf << 6, 6) - 64;
	    if (i >= 64)
		break;	/* illegal, check needed to avoid buffer overflow */

	    j = scan[i];

	    bitstream_dumpbits(&bitstream, 12);
	    bitstream_needbits(&bitstream);
	    val = SBITS (bitstream.buf, 8);
	    if (! (val & 0x7f)) {
		bitstream_dumpbits(&bitstream, 8);
		val = UBITS (bitstream.buf, 8) + 2 * val;
	    }
	    val = 2 * (val + SBITS (val, 1)) + 1;
	    val = (val * quant_matrix[j]) / 32;

	    /* oddification */
	    val = (val + ~SBITS (val, 1)) | 1;

	    SATURATE (val);
	    dest[j] = val;

	    bitstream_dumpbits(&bitstream, 8);
	    bitstream_needbits(&bitstream);

	    continue;

	} else if (bitstream.buf >= 0x02000000) {
	    tab = DCT_B14_10 + (UBITS (bitstream.buf, 10) - 8);
	    i += tab->run;
	    if (i < 64)
		goto normal_code;
	} else if (bitstream.buf >= 0x00800000) {
	    tab = DCT_13 + (UBITS (bitstream.buf, 13) - 16);
	    i += tab->run;
	    if (i < 64)
		goto normal_code;
	} else if (bitstream.buf >= 0x00200000) {
	    tab = DCT_15 + (UBITS (bitstream.buf, 15) - 16);
	    i += tab->run;
	    if (i < 64)
		goto normal_code;
	} else {
	    tab = DCT_16 + UBITS (bitstream.buf, 16);
	    bitstream.buf <<= 16;
	    bitstream_getword (&bitstream, bitstream.bits + 16);
	    i += tab->run;
	    if (i < 64)
		goto normal_code;
	}
	break;	/* illegal, check needed to avoid buffer overflow */
    }
    bitstream_dumpbits(&bitstream, tab->len);	/* dump end of block code */
    slice->bitstream = bitstream;
    return i;
}

static inline void slice_intra_DCT (mpeg2_decoder_t * const decoder,
				    slice_decode_t * const slice,
				    const int cc,
				    uint8_t * const dest, const int stride)
{
    SLICE_NEEDBITS (slice);
    /* Get the intra DC coefficient and inverse quantize it */
    if (cc == 0)
	slice->dc_dct_pred[0] += get_luma_dc_dct_diff (slice);
    else
	slice->dc_dct_pred[cc] += get_chroma_dc_dct_diff (slice);

    slice->DCTblock[0] = slice->dc_dct_pred[cc] << (3 - decoder->intra_dc_precision);

    if (decoder->mpeg1) {
	if (decoder->coding_type != D_TYPE)
	    get_mpeg1_intra_block (decoder, slice);
    } else if (decoder->intra_vlc_format)
	get_intra_block_B15 (decoder, slice,
			     slice->quantizer_matrix[cc ? 2 : 0]);
    else
	get_intra_block_B14 (decoder, slice,
			     slice->quantizer_matrix[cc ? 2 : 0]);
    mpeg2_idct_copy (slice->DCTblock, dest, stride);
}

static inline void slice_non_intra_DCT (mpeg2_decoder_t * const decoder,
					slice_decode_t * const slice,
					const int cc,
					uint8_t * const dest, const int stride)
{
    int last;

    if (decoder->mpeg1)
	last = get_mpeg1_non_intra_block (decoder, slice);
    else
	last = get_non_intra_block (decoder, slice,
				    slice->quantizer_matrix[cc ? 3 : 1]);
    mpeg2_idct_add (last, slice->DCTblock, dest, stride);
}

#define MOTION_420(table,ref,motion_x,motion_y,size,y)			      \
    pos_x = 2 * slice->offset + motion_x;				      \
    pos_y = 2 * slice->v_offset + motion_y + 2 * y;			      \
    if (unlikely (pos_x > decoder->limit_x)) {				      \
	pos_x = ((int)pos_x < 0) ? 0 : decoder->limit_x;		      \
	motion_x = pos_x - 2 * slice->offset;				      \
    }									      \
    if (unlikely (pos_y > decoder->limit_y_ ## size)) {			      \
	pos_y = ((int)pos_y < 0) ? 0 : decoder->limit_y_ ## size;	      \
	motion_y = pos_y - 2 * slice->v_offset - 2 * y;			      \
    }									      \
    xy_half = ((pos_y & 1) << 1) | (pos_x & 1);				      \
    table[xy_half] (slice->dest[0] + y * decoder->stride + slice->offset,     \
		    ref[0] + (pos_x >> 1) + (pos_y >> 1) * decoder->stride,   \
		    decoder->stride, size);				      \
    motion_x /= 2;	motion_y /= 2;					      \
    xy_half = ((motion_y & 1) << 1) | (motion_x & 1);			      \
    offset = (((slice->offset + motion_x) >> 1) +			      \
	      ((((slice->v_offset + motion_y) >> 1) + y/2) *		      \
	       decoder->uv_stride));					      \
    table[4+xy_half] (slice->dest[1] + y/2 * decoder->uv_stride +	      \
		      (slice->offset >> 1), ref[1] + offset,		      \
		      decoder->uv_stride, size/2);			      \
    table[4+xy_half] (slice->dest[2] + y/2 * decoder->uv_stride +	      \
		      (slice->offset >> 1), ref[2] + offset,		      \
		      decoder->uv_stride, size/2)

#define MOTION_FIELD_420(table,ref,motion_x,motion_y,dest_field,op,src_field) \
    pos_x = 2 * slice->offset + motion_x;				      \
    pos_y = slice->v_offset + motion_y;					      \
    if (unlikely (pos_x > decoder->limit_x)) {				      \
	pos_x = ((int)pos_x < 0) ? 0 : decoder->limit_x;		      \
	motion_x = pos_x - 2 * slice->offset;				      \
    }									      \
    if (unlikely (pos_y > decoder->limit_y)) {				      \
	pos_y = ((int)pos_y < 0) ? 0 : decoder->limit_y;		      \
	motion_y = pos_y - slice->v_offset;				      \
    }									      \
    xy_half = ((pos_y & 1) << 1) | (pos_x & 1);				      \
    table[xy_half] (slice->dest[0] + dest_field * decoder->stride +	      \
		    slice->offset,					      \
		    (ref[0] + (pos_x >> 1) +				      \
		     ((pos_y op) + src_field) * decoder->stride),	      \
		    2 * decoder->stride, 8);				      \
    motion_x /= 2;	motion_y /= 2;					      \
    xy_half = ((motion_y & 1) << 1) | (motion_x & 1);			      \
    offset = (((slice->offset + motion_x) >> 1) +			      \
	      (((slice->v_offset >> 1) + (motion_y op) + src_field) *	      \
	       decoder->uv_stride));					      \
    table[4+xy_half] (slice->dest[1] + dest_field * decoder->uv_stride +      \
		      (slice->offset >> 1), ref[1] + offset,		      \
		      2 * decoder->uv_stride, 4);			      \
    table[4+xy_half] (slice->dest[2] + dest_field * decoder->uv_stride +      \
		      (slice->offset >> 1), ref[2] + offset,		      \
		      2 * decoder->uv_stride, 4)

#define MOTION_DMV_420(table,ref,motion_x,motion_y)			      \
    pos_x = 2 * slice->offset + motion_x;				      \
    pos_y = slice->v_offset + motion_y;					      \
    if (unlikely (pos_x > decoder->limit_x)) {				      \
	pos_x = ((int)pos_x < 0) ? 0 : decoder->limit_x;		      \
	motion_x = pos_x - 2 * slice->offset;				      \
    }									      \
    if (unlikely (pos_y > decoder->limit_y)) {				      \
	pos_y = ((int)pos_y < 0) ? 0 : decoder->limit_y;		      \
	motion_y = pos_y - slice->v_offset;				      \
    }									      \
    xy_half = ((pos_y & 1) << 1) | (pos_x & 1);				      \
    offset = (pos_x >> 1) + (pos_y & ~1) * decoder->stride;		      \
    table[xy_half] (slice->dest[0] + slice->offset,			      \
		    ref[0] + offset, 2 * decoder->stride, 8);		      \
    table[xy_half] (slice->dest[0] + decoder->stride + slice->offset,	      \
		    ref[0] + decoder->stride + offset,			      \
		    2 * decoder->stride, 8);				      \
    motion_x /= 2;	motion_y /= 2;					      \
    xy_half = ((motion_y & 1) << 1) | (motion_x & 1);			      \
    offset = (((slice->offset + motion_x) >> 1) +			      \
	      (((slice->v_offset >> 1) + (motion_y & ~1)) *		      \
	       decoder->uv_stride));					      \
    table[4+xy_half] (slice->dest[1] + (slice->offset >> 1),		      \
		      ref[1] + offset, 2 * decoder->uv_stride, 4);	      \
    table[4+xy_half] (slice->dest[1] + decoder->uv_stride +		      \
		      (slice->offset >> 1),				      \
		      ref[1] + decoder->uv_stride + offset,		      \
		      2 * decoder->uv_stride, 4);			      \
    table[4+xy_half] (slice->dest[2] + (slice->offset >> 1),		      \
		      ref[2] + offset, 2 * decoder->uv_stride, 4);	      \
    table[4+xy_half] (slice->dest[2] + decoder->uv_stride +		      \
		      (slice->offset >> 1),				      \
		      ref[2] + decoder->uv_stride + offset,		      \
		      2 * decoder->uv_stride, 4)

#define MOTION_ZERO_420(table,ref)					      \
    table[0] (slice->dest[0] + slice->offset,				      \
	      (ref[0] + slice->offset +					      \
	       slice->v_offset * decoder->stride), decoder->stride, 16);      \
    offset = ((slice->offset >> 1) +					      \
	      (slice->v_offset >> 1) * decoder->uv_stride);		      \
    table[4] (slice->dest[1] + (slice->offset >> 1),			      \
	      ref[1] + offset, decoder->uv_stride, 8);			      \
    table[4] (slice->dest[2] + (slice->offset >> 1),			      \
	      ref[2] + offset, decoder->uv_stride, 8)

#define MOTION_422(table,ref,motion_x,motion_y,size,y)			      \
    pos_x = 2 * slice->offset + motion_x;				      \
    pos_y = 2 * slice->v_offset + motion_y + 2 * y;			      \
    if (unlikely (pos_x > decoder->limit_x)) {				      \
	pos_x = ((int)pos_x < 0) ? 0 : decoder->limit_x;		      \
	motion_x = pos_x - 2 * slice->offset;				      \
    }									      \
    if (unlikely (pos_y > decoder->limit_y_ ## size)) {			      \
	pos_y = ((int)pos_y < 0) ? 0 : decoder->limit_y_ ## size;	      \
	motion_y = pos_y - 2 * slice->v_offset - 2 * y;			      \
    }									      \
    xy_half = ((pos_y & 1) << 1) | (pos_x & 1);				      \
    offset = (pos_x >> 1) + (pos_y >> 1) * decoder->stride;		      \
    table[xy_half] (slice->dest[0] + y * decoder->stride + slice->offset,     \
		    ref[0] + offset, decoder->stride, size);		      \
    offset = (offset + (motion_x & (motion_x < 0))) >> 1;		      \
    motion_x /= 2;							      \
    xy_half = ((pos_y & 1) << 1) | (motion_x & 1);			      \
    table[4+xy_half] (slice->dest[1] + y * decoder->uv_stride +		      \
		      (slice->offset >> 1), ref[1] + offset,		      \
		      decoder->uv_stride, size);			      \
    table[4+xy_half] (slice->dest[2] + y * decoder->uv_stride +		      \
		      (slice->offset >> 1), ref[2] + offset,		      \
		      decoder->uv_stride, size)

#define MOTION_FIELD_422(table,ref,motion_x,motion_y,dest_field,op,src_field) \
    pos_x = 2 * slice->offset + motion_x;				      \
    pos_y = slice->v_offset + motion_y;					      \
    if (unlikely (pos_x > decoder->limit_x)) {				      \
	pos_x = ((int)pos_x < 0) ? 0 : decoder->limit_x;		      \
	motion_x = pos_x - 2 * slice->offset;				      \
    }									      \
    if (unlikely (pos_y > decoder->limit_y)) {				      \
	pos_y = ((int)pos_y < 0) ? 0 : decoder->limit_y;		      \
	motion_y = pos_y - slice->v_offset;				      \
    }									      \
    xy_half = ((pos_y & 1) << 1) | (pos_x & 1);				      \
    offset = (pos_x >> 1) + ((pos_y op) + src_field) * decoder->stride;	      \
    table[xy_half] (slice->dest[0] + dest_field * decoder->stride +	      \
		    slice->offset, ref[0] + offset,			      \
		    2 * decoder->stride, 8);				      \
    offset = (offset + (motion_x & (motion_x < 0))) >> 1;		      \
    motion_x /= 2;							      \
    xy_half = ((pos_y & 1) << 1) | (motion_x & 1);			      \
    table[4+xy_half] (slice->dest[1] + dest_field * decoder->uv_stride +      \
		      (slice->offset >> 1), ref[1] + offset,		      \
		      2 * decoder->uv_stride, 8);			      \
    table[4+xy_half] (slice->dest[2] + dest_field * decoder->uv_stride +      \
		      (slice->offset >> 1), ref[2] + offset,		      \
		      2 * decoder->uv_stride, 8)

#define MOTION_DMV_422(table,ref,motion_x,motion_y)			      \
    pos_x = 2 * slice->offset + motion_x;				      \
    pos_y = slice->v_offset + motion_y;					      \
    if (unlikely (pos_x > decoder->limit_x)) {				      \
	pos_x = ((int)pos_x < 0) ? 0 : decoder->limit_x;		      \
	motion_x = pos_x - 2 * slice->offset;				      \
    }									      \
    if (unlikely (pos_y > decoder->limit_y)) {				      \
	pos_y = ((int)pos_y < 0) ? 0 : decoder->limit_y;		      \
	motion_y = pos_y - slice->v_offset;				      \
    }									      \
    xy_half = ((pos_y & 1) << 1) | (pos_x & 1);				      \
    offset = (pos_x >> 1) + (pos_y & ~1) * decoder->stride;		      \
    table[xy_half] (slice->dest[0] + slice->offset,			      \
		    ref[0] + offset, 2 * decoder->stride, 8);		      \
    table[xy_half] (slice->dest[0] + decoder->stride + slice->offset,	      \
		    ref[0] + decoder->stride + offset,			      \
		    2 * decoder->stride, 8);				      \
    offset = (offset + (motion_x & (motion_x < 0))) >> 1;		      \
    motion_x /= 2;							      \
    xy_half = ((pos_y & 1) << 1) | (motion_x & 1);			      \
    table[4+xy_half] (slice->dest[1] + (slice->offset >> 1),		      \
		      ref[1] + offset, 2 * decoder->uv_stride, 8);	      \
    table[4+xy_half] (slice->dest[1] + decoder->uv_stride +		      \
		      (slice->offset >> 1),				      \
		      ref[1] + decoder->uv_stride + offset,		      \
		      2 * decoder->uv_stride, 8);			      \
    table[4+xy_half] (slice->dest[2] + (slice->offset >> 1),		      \
		      ref[2] + offset, 2 * decoder->uv_stride, 8);	      \
    table[4+xy_half] (slice->dest[2] + decoder->uv_stride +		      \
		      (slice->offset >> 1),				      \
		      ref[2] + decoder->uv_stride + offset,		      \
		      2 * decoder->uv_stride, 8)

#define MOTION_ZERO_422(table,ref)					      \
    offset = slice->offset + slice->v_offset * decoder->stride;		      \
    table[0] (slice->dest[0] + slice->offset,				      \
	      ref[0] + offset, decoder->stride, 16);			      \
    offset >>= 1;							      \
    table[4] (slice->dest[1] + (slice->offset >> 1),			      \
	      ref[1] + offset, decoder->uv_stride, 16);			      \
    table[4] (slice->dest[2] + (slice->offset >> 1),			      \
	      ref[2] + offset, decoder->uv_stride, 16)

#define MOTION_444(table,ref,motion_x,motion_y,size,y)			      \
    pos_x = 2 * slice->offset + motion_x;				      \
    pos_y = 2 * slice->v_offset + motion_y + 2 * y;			      \
    if (unlikely (pos_x > decoder->limit_x)) {				      \
	pos_x = ((int)pos_x < 0) ? 0 : decoder->limit_x;		      \
	motion_x = pos_x - 2 * slice->offset;				      \
    }									      \
    if (unlikely (pos_y > decoder->limit_y_ ## size)) {			      \
	pos_y = ((int)pos_y < 0) ? 0 : decoder->limit_y_ ## size;	      \
	motion_y = pos_y - 2 * slice->v_offset - 2 * y;			      \
    }									      \
    xy_half = ((pos_y & 1) << 1) | (pos_x & 1);				      \
    offset = (pos_x >> 1) + (pos_y >> 1) * decoder->stride;		      \
    table[xy_half] (slice->dest[0] + y * decoder->stride + slice->offset,     \
		    ref[0] + offset, decoder->stride, size);		      \
    table[xy_half] (slice->dest[1] + y * decoder->stride + slice->offset,     \
		    ref[1] + offset, decoder->stride, size);		      \
    table[xy_half] (slice->dest[2] + y * decoder->stride + slice->offset,     \
		    ref[2] + offset, decoder->stride, size)

#define MOTION_FIELD_444(table,ref,motion_x,motion_y,dest_field,op,src_field) \
    pos_x = 2 * slice->offset + motion_x;				      \
    pos_y = slice->v_offset + motion_y;					      \
    if (unlikely (pos_x > decoder->limit_x)) {				      \
	pos_x = ((int)pos_x < 0) ? 0 : decoder->limit_x;		      \
	motion_x = pos_x - 2 * slice->offset;				      \
    }									      \
    if (unlikely (pos_y > decoder->limit_y)) {				      \
	pos_y = ((int)pos_y < 0) ? 0 : decoder->limit_y;		      \
	motion_y = pos_y - slice->v_offset;				      \
    }									      \
    xy_half = ((pos_y & 1) << 1) | (pos_x & 1);				      \
    offset = (pos_x >> 1) + ((pos_y op) + src_field) * decoder->stride;	      \
    table[xy_half] (slice->dest[0] + dest_field * decoder->stride +	      \
		    slice->offset, ref[0] + offset,			      \
		    2 * decoder->stride, 8);				      \
    table[xy_half] (slice->dest[1] + dest_field * decoder->stride +	      \
		    slice->offset, ref[1] + offset,			      \
		    2 * decoder->stride, 8);				      \
    table[xy_half] (slice->dest[2] + dest_field * decoder->stride +	      \
		    slice->offset, ref[2] + offset,			      \
		    2 * decoder->stride, 8)

#define MOTION_DMV_444(table,ref,motion_x,motion_y)			      \
    pos_x = 2 * slice->offset + motion_x;				      \
    pos_y = slice->v_offset + motion_y;					      \
    if (unlikely (pos_x > decoder->limit_x)) {				      \
	pos_x = ((int)pos_x < 0) ? 0 : decoder->limit_x;		      \
	motion_x = pos_x - 2 * slice->offset;				      \
    }									      \
    if (unlikely (pos_y > decoder->limit_y)) {				      \
	pos_y = ((int)pos_y < 0) ? 0 : decoder->limit_y;		      \
	motion_y = pos_y - slice->v_offset;				      \
    }									      \
    xy_half = ((pos_y & 1) << 1) | (pos_x & 1);				      \
    offset = (pos_x >> 1) + (pos_y & ~1) * decoder->stride;		      \
    table[xy_half] (slice->dest[0] + slice->offset,			      \
		    ref[0] + offset, 2 * decoder->stride, 8);		      \
    table[xy_half] (slice->dest[0] + decoder->stride + slice->offset,	      \
		    ref[0] + decoder->stride + offset,			      \
		    2 * decoder->stride, 8);				      \
    table[xy_half] (slice->dest[1] + slice->offset,			      \
		    ref[1] + offset, 2 * decoder->stride, 8);		      \
    table[xy_half] (slice->dest[1] + decoder->stride + slice->offset,	      \
		    ref[1] + decoder->stride + offset,			      \
		    2 * decoder->stride, 8);				      \
    table[xy_half] (slice->dest[2] + slice->offset,			      \
		    ref[2] + offset, 2 * decoder->stride, 8);		      \
    table[xy_half] (slice->dest[2] + decoder->stride + slice->offset,	      \
		    ref[2] + decoder->stride + offset,			      \
		    2 * decoder->stride, 8)

#define MOTION_ZERO_444(table,ref)					      \
    offset = slice->offset + slice->v_offset * decoder->stride;		      \
    table[0] (slice->dest[0] + slice->offset,				      \
	      ref[0] + offset, decoder->stride, 16);			      \
    table[4] (slice->dest[1] + slice->offset,				      \
	      ref[1] + offset, decoder->stride, 16);			      \
    table[4] (slice->dest[2] + slice->offset,				      \
	      ref[2] + offset, decoder->stride, 16)

static void motion_mp1 (mpeg2_decoder_t * const decoder,
			slice_decode_t * const slice,
			motion_t * const motion,
			mpeg2_mc_fct * const * const table)
{
    int motion_x, motion_y;
    unsigned int pos_x, pos_y, xy_half, offset;

    SLICE_NEEDBITS (slice);
    motion_x = (motion->pmv[0][0] +
		(get_motion_delta (decoder, slice,
				   motion->f_code[0]) << motion->f_code[1]));
    motion_x = bound_motion_vector (motion_x,
				    motion->f_code[0] + motion->f_code[1]);
    motion->pmv[0][0] = motion_x;

    SLICE_NEEDBITS (slice);
    motion_y = (motion->pmv[0][1] +
		(get_motion_delta (decoder, slice,
				   motion->f_code[0]) << motion->f_code[1]));
    motion_y = bound_motion_vector (motion_y,
				    motion->f_code[0] + motion->f_code[1]);
    motion->pmv[0][1] = motion_y;

    MOTION_420 (table, motion->ref[0], motion_x, motion_y, 16, 0);
}

#define MOTION_FUNCTIONS(FORMAT,MOTION,MOTION_FIELD,MOTION_DMV,MOTION_ZERO)   \
									      \
static void motion_fr_frame_##FORMAT (mpeg2_decoder_t * const decoder,	      \
				      slice_decode_t * const slice,	      \
				      motion_t * const motion,		      \
				      mpeg2_mc_fct * const * const table)     \
{									      \
    int motion_x, motion_y;						      \
    unsigned int pos_x, pos_y, xy_half, offset;				      \
									      \
    SLICE_NEEDBITS (slice);						      \
    motion_x = motion->pmv[0][0] + get_motion_delta (decoder, slice,	      \
						     motion->f_code[0]);      \
    motion_x = bound_motion_vector (motion_x, motion->f_code[0]);	      \
    motion->pmv[1][0] = motion->pmv[0][0] = motion_x;			      \
									      \
    SLICE_NEEDBITS (slice);						      \
    motion_y = motion->pmv[0][1] + get_motion_delta (decoder, slice,	      \
						     motion->f_code[1]);      \
    motion_y = bound_motion_vector (motion_y, motion->f_code[1]);	      \
    motion->pmv[1][1] = motion->pmv[0][1] = motion_y;			      \
									      \
    MOTION (table, motion->ref[0], motion_x, motion_y, 16, 0);		      \
}									      \
									      \
static void motion_fr_field_##FORMAT (mpeg2_decoder_t * const decoder,	      \
				      slice_decode_t * const slice,	      \
				      motion_t * const motion,		      \
				      mpeg2_mc_fct * const * const table)     \
{									      \
    int motion_x, motion_y, field;					      \
    unsigned int pos_x, pos_y, xy_half, offset;				      \
									      \
    SLICE_NEEDBITS (slice);						      \
    field = SLICE_UBITS (slice, 1);					      \
    SLICE_DUMPBITS (slice, 1);						      \
									      \
    motion_x = motion->pmv[0][0] + get_motion_delta (decoder, slice,	      \
						     motion->f_code[0]);      \
    motion_x = bound_motion_vector (motion_x, motion->f_code[0]);	      \
    motion->pmv[0][0] = motion_x;					      \
									      \
    SLICE_NEEDBITS (slice);						      \
    motion_y = ((motion->pmv[0][1] >> 1) +				      \
		get_motion_delta (decoder, slice, motion->f_code[1]));	      \
    /* motion_y = bound_motion_vector (motion_y, motion->f_code[1]); */	      \
    motion->pmv[0][1] = motion_y << 1;					      \
									      \
    MOTION_FIELD (table, motion->ref[0], motion_x, motion_y, 0, & ~1, field); \
									      \
    SLICE_NEEDBITS (slice);						      \
    field = SLICE_UBITS (slice, 1);					      \
    SLICE_DUMPBITS (slice, 1);						      \
									      \
    motion_x = motion->pmv[1][0] + get_motion_delta (decoder, slice,	      \
						     motion->f_code[0]);      \
    motion_x = bound_motion_vector (motion_x, motion->f_code[0]);	      \
    motion->pmv[1][0] = motion_x;					      \
									      \
    SLICE_NEEDBITS (slice);						      \
    motion_y = ((motion->pmv[1][1] >> 1) +				      \
		get_motion_delta (decoder, slice, motion->f_code[1]));	      \
    /* motion_y = bound_motion_vector (motion_y, motion->f_code[1]); */	      \
    motion->pmv[1][1] = motion_y << 1;					      \
									      \
    MOTION_FIELD (table, motion->ref[0], motion_x, motion_y, 1, & ~1, field); \
}									      \
									      \
static void motion_fr_dmv_##FORMAT (mpeg2_decoder_t * const decoder,	      \
				    slice_decode_t * const slice,	      \
				    motion_t * const motion,		      \
				    mpeg2_mc_fct * const * const table)	      \
{									      \
    int motion_x, motion_y, dmv_x, dmv_y, m, other_x, other_y;		      \
    unsigned int pos_x, pos_y, xy_half, offset;				      \
									      \
    SLICE_NEEDBITS (slice);						      \
    motion_x = motion->pmv[0][0] + get_motion_delta (decoder, slice,	      \
						     motion->f_code[0]);      \
    motion_x = bound_motion_vector (motion_x, motion->f_code[0]);	      \
    motion->pmv[1][0] = motion->pmv[0][0] = motion_x;			      \
    SLICE_NEEDBITS (slice);						      \
    dmv_x = get_dmv (slice);						      \
									      \
    motion_y = ((motion->pmv[0][1] >> 1) +				      \
		get_motion_delta (decoder, slice, motion->f_code[1]));	      \
    /* motion_y = bound_motion_vector (motion_y, motion->f_code[1]); */	      \
    motion->pmv[1][1] = motion->pmv[0][1] = motion_y << 1;		      \
    dmv_y = get_dmv (slice);						      \
									      \
    m = decoder->top_field_first ? 1 : 3;				      \
    other_x = ((motion_x * m + (motion_x > 0)) >> 1) + dmv_x;		      \
    other_y = ((motion_y * m + (motion_y > 0)) >> 1) + dmv_y - 1;	      \
    MOTION_FIELD (mpeg2_mc.put, motion->ref[0], other_x, other_y, 0, | 1, 0); \
									      \
    m = decoder->top_field_first ? 3 : 1;				      \
    other_x = ((motion_x * m + (motion_x > 0)) >> 1) + dmv_x;		      \
    other_y = ((motion_y * m + (motion_y > 0)) >> 1) + dmv_y + 1;	      \
    MOTION_FIELD (mpeg2_mc.put, motion->ref[0], other_x, other_y, 1, & ~1, 0);\
									      \
    MOTION_DMV (mpeg2_mc.avg, motion->ref[0], motion_x, motion_y);	      \
}									      \
									      \
static void motion_reuse_##FORMAT (mpeg2_decoder_t * const decoder,	      \
				   slice_decode_t * const slice,	      \
				   motion_t * const motion,		      \
				   mpeg2_mc_fct * const * const table)	      \
{									      \
    int motion_x, motion_y;						      \
    unsigned int pos_x, pos_y, xy_half, offset;				      \
									      \
    motion_x = motion->pmv[0][0];					      \
    motion_y = motion->pmv[0][1];					      \
									      \
    MOTION (table, motion->ref[0], motion_x, motion_y, 16, 0);		      \
}									      \
									      \
static void motion_zero_##FORMAT (mpeg2_decoder_t * const decoder,	      \
				  slice_decode_t * const slice,		      \
				  motion_t * const motion,		      \
				  mpeg2_mc_fct * const * const table)	      \
{									      \
    unsigned int offset;						      \
									      \
    motion->pmv[0][0] = motion->pmv[0][1] = 0;				      \
    motion->pmv[1][0] = motion->pmv[1][1] = 0;				      \
									      \
    MOTION_ZERO (table, motion->ref[0]);				      \
}									      \
									      \
static void motion_fi_field_##FORMAT (mpeg2_decoder_t * const decoder,	      \
				      slice_decode_t * const slice,	      \
				      motion_t * const motion,		      \
				      mpeg2_mc_fct * const * const table)     \
{									      \
    int motion_x, motion_y;						      \
    uint8_t ** ref_field;						      \
    unsigned int pos_x, pos_y, xy_half, offset;				      \
									      \
    SLICE_NEEDBITS (slice);						      \
    ref_field = motion->ref2[SLICE_UBITS (slice, 1)];			      \
    SLICE_DUMPBITS (slice, 1);						      \
									      \
    motion_x = motion->pmv[0][0] + get_motion_delta (decoder, slice,	      \
						     motion->f_code[0]);      \
    motion_x = bound_motion_vector (motion_x, motion->f_code[0]);	      \
    motion->pmv[1][0] = motion->pmv[0][0] = motion_x;			      \
									      \
    SLICE_NEEDBITS (slice);						      \
    motion_y = motion->pmv[0][1] + get_motion_delta (decoder, slice,	      \
						     motion->f_code[1]);      \
    motion_y = bound_motion_vector (motion_y, motion->f_code[1]);	      \
    motion->pmv[1][1] = motion->pmv[0][1] = motion_y;			      \
									      \
    MOTION (table, ref_field, motion_x, motion_y, 16, 0);		      \
}									      \
									      \
static void motion_fi_16x8_##FORMAT (mpeg2_decoder_t * const decoder,	      \
				     slice_decode_t * const slice,	      \
				     motion_t * const motion,		      \
				     mpeg2_mc_fct * const * const table)      \
{									      \
    int motion_x, motion_y;						      \
    uint8_t ** ref_field;						      \
    unsigned int pos_x, pos_y, xy_half, offset;				      \
									      \
    SLICE_NEEDBITS (slice);						      \
    ref_field = motion->ref2[SLICE_UBITS (slice, 1)];			      \
    SLICE_DUMPBITS (slice, 1);						      \
									      \
    motion_x = motion->pmv[0][0] + get_motion_delta (decoder, slice,	      \
						     motion->f_code[0]);      \
    motion_x = bound_motion_vector (motion_x, motion->f_code[0]);	      \
    motion->pmv[0][0] = motion_x;					      \
									      \
    SLICE_NEEDBITS (slice);						      \
    motion_y = motion->pmv[0][1] + get_motion_delta (decoder, slice,	      \
						     motion->f_code[1]);      \
    motion_y = bound_motion_vector (motion_y, motion->f_code[1]);	      \
    motion->pmv[0][1] = motion_y;					      \
									      \
    MOTION (table, ref_field, motion_x, motion_y, 8, 0);		      \
									      \
    SLICE_NEEDBITS (slice);						      \
    ref_field = motion->ref2[SLICE_UBITS (slice, 1)];			      \
    SLICE_DUMPBITS (slice, 1);						      \
									      \
    motion_x = motion->pmv[1][0] + get_motion_delta (decoder, slice,	      \
						     motion->f_code[0]);      \
    motion_x = bound_motion_vector (motion_x, motion->f_code[0]);	      \
    motion->pmv[1][0] = motion_x;					      \
									      \
    SLICE_NEEDBITS (slice);						      \
    motion_y = motion->pmv[1][1] + get_motion_delta (decoder, slice,	      \
						     motion->f_code[1]);      \
    motion_y = bound_motion_vector (motion_y, motion->f_code[1]);	      \
    motion->pmv[1][1] = motion_y;					      \
									      \
    MOTION (table, ref_field, motion_x, motion_y, 8, 8);		      \
}									      \
									      \
static void motion_fi_dmv_##FORMAT (mpeg2_decoder_t * const decoder,	      \
				    slice_decode_t * const slice,	      \
				    motion_t * const motion,		      \
				    mpeg2_mc_fct * const * const table)	      \
{									      \
    int motion_x, motion_y, other_x, other_y;				      \
    unsigned int pos_x, pos_y, xy_half, offset;				      \
									      \
    SLICE_NEEDBITS (slice);						      \
    motion_x = motion->pmv[0][0] + get_motion_delta (decoder, slice,	      \
						     motion->f_code[0]);      \
    motion_x = bound_motion_vector (motion_x, motion->f_code[0]);	      \
    motion->pmv[1][0] = motion->pmv[0][0] = motion_x;			      \
    SLICE_NEEDBITS (slice);						      \
    other_x = ((motion_x + (motion_x > 0)) >> 1) + get_dmv (slice);	      \
									      \
    motion_y = motion->pmv[0][1] + get_motion_delta (decoder, slice,	      \
						     motion->f_code[1]);      \
    motion_y = bound_motion_vector (motion_y, motion->f_code[1]);	      \
    motion->pmv[1][1] = motion->pmv[0][1] = motion_y;			      \
    other_y = (((motion_y + (motion_y > 0)) >> 1) + get_dmv (slice) +	      \
	       decoder->dmv_offset);					      \
									      \
    MOTION (mpeg2_mc.put, motion->ref[0], motion_x, motion_y, 16, 0);	      \
    MOTION (mpeg2_mc.avg, motion->ref[1], other_x, other_y, 16, 0);	      \
}									      \

MOTION_FUNCTIONS (420, MOTION_420, MOTION_FIELD_420, MOTION_DMV_420,
		  MOTION_ZERO_420)
MOTION_FUNCTIONS (422, MOTION_422, MOTION_FIELD_422, MOTION_DMV_422,
		  MOTION_ZERO_422)
MOTION_FUNCTIONS (444, MOTION_444, MOTION_FIELD_444, MOTION_DMV_444,
		  MOTION_ZERO_444)

/* like motion_frame, but parsing without actual motion compensation */
static void motion_fr_conceal (mpeg2_decoder_t * const decoder,
			       slice_decode_t * const slice)
{
    int tmp;

    SLICE_NEEDBITS (slice);
    tmp = (slice->f_motion.pmv[0][0] +
	   get_motion_delta (decoder, slice, slice->f_motion.f_code[0]));
    tmp = bound_motion_vector (tmp, slice->f_motion.f_code[0]);
    slice->f_motion.pmv[1][0] = slice->f_motion.pmv[0][0] = tmp;

    SLICE_NEEDBITS (slice);
    tmp = (slice->f_motion.pmv[0][1] +
	   get_motion_delta (decoder, slice, slice->f_motion.f_code[1]));
    tmp = bound_motion_vector (tmp, slice->f_motion.f_code[1]);
    slice->f_motion.pmv[1][1] = slice->f_motion.pmv[0][1] = tmp;

    SLICE_DUMPBITS (slice, 1); /* remove marker_bit */
}

static void motion_fi_conceal (mpeg2_decoder_t * const decoder,
			       slice_decode_t * const slice)
{
    int tmp;

    SLICE_NEEDBITS (slice);
    SLICE_DUMPBITS (slice, 1); /* remove field_select */

    tmp = (slice->f_motion.pmv[0][0] +
	   get_motion_delta (decoder, slice, slice->f_motion.f_code[0]));
    tmp = bound_motion_vector (tmp, slice->f_motion.f_code[0]);
    slice->f_motion.pmv[1][0] = slice->f_motion.pmv[0][0] = tmp;

    SLICE_NEEDBITS (slice);
    tmp = (slice->f_motion.pmv[0][1] +
	   get_motion_delta (decoder, slice, slice->f_motion.f_code[1]));
    tmp = bound_motion_vector (tmp, slice->f_motion.f_code[1]);
    slice->f_motion.pmv[1][1] = slice->f_motion.pmv[0][1] = tmp;

    SLICE_DUMPBITS (slice, 1); /* remove marker_bit */
}

#define MOTION_CALL(routine,direction)					\
do {									\
    if ((direction) & MACROBLOCK_MOTION_FORWARD)			\
	routine (decoder, slice, &(slice->f_motion), mpeg2_mc.put);	\
    if ((direction) & MACROBLOCK_MOTION_BACKWARD)			\
	routine (decoder, slice, &(slice->b_motion),			\
		 ((direction) & MACROBLOCK_MOTION_FORWARD ?		\
		  mpeg2_mc.avg : mpeg2_mc.put));			\
} while (0)

#define NEXT_MACROBLOCK							\
do {									\
    slice->offset += 16;						\
    if (slice->offset == decoder->width) {				\
	do { /* just so we can use the break statement */		\
	    if (decoder->convert) {					\
		decoder->convert (decoder->convert_id, slice->dest,	\
				  slice->v_offset);			\
		if (decoder->coding_type == B_TYPE)			\
		    break;						\
	    }								\
	    slice->dest[0] += decoder->slice_stride;			\
	    slice->dest[1] += decoder->slice_uv_stride;			\
	    slice->dest[2] += decoder->slice_uv_stride;			\
	} while (0);							\
	slice->v_offset += 16;						\
	if (slice->v_offset > decoder->limit_y) {			\
	    if (mpeg2_cpu_state_restore)				\
		mpeg2_cpu_state_restore (&cpu_state);			\
	    return;							\
	}								\
	slice->offset = 0;						\
    }									\
} while (0)

/**
 * Dummy motion decoding function, to avoid calling NULL in
 * case of malformed streams.
 */
static void motion_dummy (mpeg2_decoder_t * const decoder,
			  slice_decode_t * const slice,
                          motion_t * const motion,
                          mpeg2_mc_fct * const * const table)
{
}

void mpeg2_init_fbuf (mpeg2_decoder_t * decoder, uint8_t * current_fbuf[3],
		      uint8_t * forward_fbuf[3], uint8_t * backward_fbuf[3])
{
    int offset, stride, height, bottom_field;

    stride = decoder->stride_frame;
    bottom_field = (decoder->picture_structure == BOTTOM_FIELD);
    offset = bottom_field ? stride : 0;
    height = decoder->height;

    decoder->picture_dest[0] = current_fbuf[0] + offset;
    decoder->picture_dest[1] = current_fbuf[1] + (offset >> 1);
    decoder->picture_dest[2] = current_fbuf[2] + (offset >> 1);

    decoder->f_motion.ref[0][0] = forward_fbuf[0] + offset;
    decoder->f_motion.ref[0][1] = forward_fbuf[1] + (offset >> 1);
    decoder->f_motion.ref[0][2] = forward_fbuf[2] + (offset >> 1);

    decoder->b_motion.ref[0][0] = backward_fbuf[0] + offset;
    decoder->b_motion.ref[0][1] = backward_fbuf[1] + (offset >> 1);
    decoder->b_motion.ref[0][2] = backward_fbuf[2] + (offset >> 1);

    if (decoder->picture_structure != FRAME_PICTURE) {
	decoder->dmv_offset = bottom_field ? 1 : -1;
	decoder->f_motion.ref2[0] = decoder->f_motion.ref[bottom_field];
	decoder->f_motion.ref2[1] = decoder->f_motion.ref[!bottom_field];
	decoder->b_motion.ref2[0] = decoder->b_motion.ref[bottom_field];
	decoder->b_motion.ref2[1] = decoder->b_motion.ref[!bottom_field];
	offset = stride - offset;

	if (decoder->second_field && (decoder->coding_type != B_TYPE))
	    forward_fbuf = current_fbuf;

	decoder->f_motion.ref[1][0] = forward_fbuf[0] + offset;
	decoder->f_motion.ref[1][1] = forward_fbuf[1] + (offset >> 1);
	decoder->f_motion.ref[1][2] = forward_fbuf[2] + (offset >> 1);

	decoder->b_motion.ref[1][0] = backward_fbuf[0] + offset;
	decoder->b_motion.ref[1][1] = backward_fbuf[1] + (offset >> 1);
	decoder->b_motion.ref[1][2] = backward_fbuf[2] + (offset >> 1);

	stride <<= 1;
	height >>= 1;
    }

    decoder->stride = stride;
    decoder->uv_stride = stride >> 1;
    decoder->slice_stride = 16 * stride;
    decoder->slice_uv_stride =
	decoder->slice_stride >> (2 - decoder->chroma_format);
    decoder->limit_x = 2 * decoder->width - 32;
    decoder->limit_y_16 = 2 * height - 32;
    decoder->limit_y_8 = 2 * height - 16;
    decoder->limit_y = height - 16;

    if (decoder->mpeg1) {
	decoder->motion_parser[0] = motion_zero_420;
        decoder->motion_parser[MC_FIELD] = motion_dummy;
 	decoder->motion_parser[MC_FRAME] = motion_mp1;
        decoder->motion_parser[MC_DMV] = motion_dummy;
	decoder->motion_parser[4] = motion_reuse_420;
    } else if (decoder->picture_structure == FRAME_PICTURE) {
	if (decoder->chroma_format == 0) {
	    decoder->motion_parser[0] = motion_zero_420;
	    decoder->motion_parser[MC_FIELD] = motion_fr_field_420;
	    decoder->motion_parser[MC_FRAME] = motion_fr_frame_420;
	    decoder->motion_parser[MC_DMV] = motion_fr_dmv_420;
	    decoder->motion_parser[4] = motion_reuse_420;
	} else if (decoder->chroma_format == 1) {
	    decoder->motion_parser[0] = motion_zero_422;
	    decoder->motion_parser[MC_FIELD] = motion_fr_field_422;
	    decoder->motion_parser[MC_FRAME] = motion_fr_frame_422;
	    decoder->motion_parser[MC_DMV] = motion_fr_dmv_422;
	    decoder->motion_parser[4] = motion_reuse_422;
	} else {
	    decoder->motion_parser[0] = motion_zero_444;
	    decoder->motion_parser[MC_FIELD] = motion_fr_field_444;
	    decoder->motion_parser[MC_FRAME] = motion_fr_frame_444;
	    decoder->motion_parser[MC_DMV] = motion_fr_dmv_444;
	    decoder->motion_parser[4] = motion_reuse_444;
	}
    } else {
	if (decoder->chroma_format == 0) {
	    decoder->motion_parser[0] = motion_zero_420;
	    decoder->motion_parser[MC_FIELD] = motion_fi_field_420;
	    decoder->motion_parser[MC_16X8] = motion_fi_16x8_420;
	    decoder->motion_parser[MC_DMV] = motion_fi_dmv_420;
	    decoder->motion_parser[4] = motion_reuse_420;
	} else if (decoder->chroma_format == 1) {
	    decoder->motion_parser[0] = motion_zero_422;
	    decoder->motion_parser[MC_FIELD] = motion_fi_field_422;
	    decoder->motion_parser[MC_16X8] = motion_fi_16x8_422;
	    decoder->motion_parser[MC_DMV] = motion_fi_dmv_422;
	    decoder->motion_parser[4] = motion_reuse_422;
	} else {
	    decoder->motion_parser[0] = motion_zero_444;
	    decoder->motion_parser[MC_FIELD] = motion_fi_field_444;
	    decoder->motion_parser[MC_16X8] = motion_fi_16x8_444;
	    decoder->motion_parser[MC_DMV] = motion_fi_dmv_444;
	    decoder->motion_parser[4] = motion_reuse_444;
	}
    }
}

static inline int slice_init (mpeg2_decoder_t * const decoder,
			      slice_decode_t * const slice)
{
    int code;
    int offset;
    const MBAtab * mba;

    slice->dc_dct_pred[0] = slice->dc_dct_pred[1] =
	slice->dc_dct_pred[2] = 128 << decoder->intra_dc_precision;

    slice->f_motion = decoder->f_motion;
    slice->b_motion = decoder->b_motion;

    slice->f_motion.pmv[0][0] = slice->f_motion.pmv[0][1] = 0;
    slice->f_motion.pmv[1][0] = slice->f_motion.pmv[1][1] = 0;
    slice->b_motion.pmv[0][0] = slice->b_motion.pmv[0][1] = 0;
    slice->b_motion.pmv[1][0] = slice->b_motion.pmv[1][1] = 0;

    code = slice->code;
    if (decoder->vertical_position_extension) {
	code += SLICE_UBITS (slice, 3) << 7;
	SLICE_DUMPBITS (slice, 3);
    }
    slice->v_offset = (code - 1) * 16;
    offset = 0;
    if (!(decoder->convert) || decoder->coding_type != B_TYPE)
	offset = (code - 1) * decoder->slice_stride;

    slice->dest[0] = decoder->picture_dest[0] + offset;
    offset >>= (2 - decoder->chroma_format);
    slice->dest[1] = decoder->picture_dest[1] + offset;
    slice->dest[2] = decoder->picture_dest[2] + offset;

    get_quantizer_scale (decoder, slice);

    /* ignore intra_slice and all the extra data */
    while (slice->bitstream.buf & 0x80000000) {
	SLICE_DUMPBITS (slice, 9);
	SLICE_NEEDBITS (slice);
    }

    /* decode initial macroblock address increment */
    offset = 0;
    while (1) {
	if (slice->bitstream.buf >= 0x08000000) {
	    mba = MBA_5 + (SLICE_UBITS (slice, 6) - 2);
	    break;
	} else if (slice->bitstream.buf >= 0x01800000) {
	    mba = MBA_11 + (SLICE_UBITS (slice, 12) - 24);
	    break;
	} else switch (SLICE_UBITS (slice, 12)) {
	case 8:		/* macroblock_escape */
	    offset += 33;
	    SLICE_DUMPBITS (slice, 11);
	    SLICE_NEEDBITS (slice);
	    continue;
	case 15:	/* macroblock_stuffing (MPEG1 only) */
	    slice->bitstream.buf &= 0xfffff;
	    SLICE_DUMPBITS (slice, 11);
	    SLICE_NEEDBITS (slice);
	    continue;
	default:	/* error */
	    return 1;
	}
    }
    SLICE_DUMPBITS (slice, mba->len + 1);
    slice->offset = (offset + mba->mba) << 4;

    while (slice->offset - decoder->width >= 0) {
	slice->offset -= decoder->width;
	if (!(decoder->convert) || decoder->coding_type != B_TYPE) {
	    slice->dest[0] += decoder->slice_stride;
	    slice->dest[1] += decoder->slice_uv_stride;
	    slice->dest[2] += decoder->slice_uv_stride;
	}
	slice->v_offset += 16;
    }
    if (slice->v_offset > decoder->limit_y)
	return 1;

    return 0;
}

void mpeg2_slice (mpeg2_decoder_t * decoder, slice_decode_t * slice)
{
    cpu_state_t cpu_state;

    if (slice_init (decoder, slice))
	return;

    if (mpeg2_cpu_state_save)
	mpeg2_cpu_state_save (&cpu_state);

    while (1) {
	int macroblock_modes;
	int mba_inc;
	const MBAtab * mba;

	SLICE_NEEDBITS (slice);

	macroblock_modes = get_macroblock_modes (decoder, slice);

	/* maybe integrate MACROBLOCK_QUANT test into get_macroblock_modes ? */
	if (macroblock_modes & MACROBLOCK_QUANT)
	    get_quantizer_scale (decoder, slice);

	if (macroblock_modes & MACROBLOCK_INTRA) {

	    int DCT_offset, DCT_stride;
	    int offset;
	    uint8_t * dest_y;

	    if (decoder->concealment_motion_vectors) {
		if (decoder->picture_structure == FRAME_PICTURE)
		    motion_fr_conceal (decoder, slice);
		else
		    motion_fi_conceal (decoder, slice);
	    } else {
		slice->f_motion.pmv[0][0] = slice->f_motion.pmv[0][1] = 0;
		slice->f_motion.pmv[1][0] = slice->f_motion.pmv[1][1] = 0;
		slice->b_motion.pmv[0][0] = slice->b_motion.pmv[0][1] = 0;
		slice->b_motion.pmv[1][0] = slice->b_motion.pmv[1][1] = 0;
	    }

	    if (macroblock_modes & DCT_TYPE_INTERLACED) {
		DCT_offset = decoder->stride;
		DCT_stride = decoder->stride * 2;
	    } else {
		DCT_offset = decoder->stride * 8;
		DCT_stride = decoder->stride;
	    }

	    offset = slice->offset;
	    dest_y = slice->dest[0] + offset;
	    slice_intra_DCT (decoder, slice, 0, dest_y, DCT_stride);
	    slice_intra_DCT (decoder, slice, 0, dest_y + 8, DCT_stride);
	    slice_intra_DCT (decoder, slice, 0, dest_y + DCT_offset,
			     DCT_stride);
	    slice_intra_DCT (decoder, slice, 0, dest_y + DCT_offset + 8,
			     DCT_stride);
	    if (likely (decoder->chroma_format == 0)) {
		slice_intra_DCT (decoder, slice, 1,
				 slice->dest[1] + (offset >> 1),
				 decoder->uv_stride);
		slice_intra_DCT (decoder, slice, 2,
				 slice->dest[2] + (offset >> 1),
				 decoder->uv_stride);
		if (decoder->coding_type == D_TYPE) {
		    SLICE_NEEDBITS (slice);
		    SLICE_DUMPBITS (slice, 1);
		}
	    } else if (likely (decoder->chroma_format == 1)) {
		uint8_t * dest_u = slice->dest[1] + (offset >> 1);
		uint8_t * dest_v = slice->dest[2] + (offset >> 1);
		DCT_stride >>= 1;
		DCT_offset >>= 1;
		slice_intra_DCT (decoder, slice, 1, dest_u, DCT_stride);
		slice_intra_DCT (decoder, slice, 2, dest_v, DCT_stride);
		slice_intra_DCT (decoder, slice, 1, dest_u + DCT_offset,
				 DCT_stride);
		slice_intra_DCT (decoder, slice, 2, dest_v + DCT_offset,
				 DCT_stride);
	    } else {
		uint8_t * dest_u = slice->dest[1] + offset;
		uint8_t * dest_v = slice->dest[2] + offset;
		slice_intra_DCT (decoder, slice, 1, dest_u, DCT_stride);
		slice_intra_DCT (decoder, slice, 2, dest_v, DCT_stride);
		slice_intra_DCT (decoder, slice, 1, dest_u + DCT_offset,
				 DCT_stride);
		slice_intra_DCT (decoder, slice, 2, dest_v + DCT_offset,
				 DCT_stride);
		slice_intra_DCT (decoder, slice, 1, dest_u + 8, DCT_stride);
		slice_intra_DCT (decoder, slice, 2, dest_v + 8, DCT_stride);
		slice_intra_DCT (decoder, slice, 1, dest_u + DCT_offset + 8,
				 DCT_stride);
		slice_intra_DCT (decoder, slice, 2, dest_v + DCT_offset + 8,
				 DCT_stride);
	    }
	} else {

	    motion_parser_t * parser;

	    if (   ((macroblock_modes >> MOTION_TYPE_SHIFT) < 0)
                || ((macroblock_modes >> MOTION_TYPE_SHIFT) >=
                    (int)(sizeof(decoder->motion_parser) 
                          / sizeof(decoder->motion_parser[0])))
	       ) {
		break; // Illegal !
	    }

	    parser =
		decoder->motion_parser[macroblock_modes >> MOTION_TYPE_SHIFT];
	    MOTION_CALL (parser, macroblock_modes);

	    if (macroblock_modes & MACROBLOCK_PATTERN) {
		int coded_block_pattern;
		int DCT_offset, DCT_stride;

		if (macroblock_modes & DCT_TYPE_INTERLACED) {
		    DCT_offset = decoder->stride;
		    DCT_stride = decoder->stride * 2;
		} else {
		    DCT_offset = decoder->stride * 8;
		    DCT_stride = decoder->stride;
		}

		coded_block_pattern = get_coded_block_pattern (slice);

		if (likely (decoder->chroma_format == 0)) {
		    int offset = slice->offset;
		    uint8_t * dest_y = slice->dest[0] + offset;
		    if (coded_block_pattern & 1)
			slice_non_intra_DCT (decoder, slice, 0, dest_y,
					     DCT_stride);
		    if (coded_block_pattern & 2)
			slice_non_intra_DCT (decoder, slice, 0, dest_y + 8,
					     DCT_stride);
		    if (coded_block_pattern & 4)
			slice_non_intra_DCT (decoder, slice, 0,
					     dest_y + DCT_offset,
					     DCT_stride);
		    if (coded_block_pattern & 8)
			slice_non_intra_DCT (decoder, slice, 0,
					     dest_y + DCT_offset + 8,
					     DCT_stride);
		    if (coded_block_pattern & 16)
			slice_non_intra_DCT (decoder, slice, 1,
					     slice->dest[1] + (offset >> 1),
					     decoder->uv_stride);
		    if (coded_block_pattern & 32)
			slice_non_intra_DCT (decoder, slice, 2,
					     slice->dest[2] + (offset >> 1),
					     decoder->uv_stride);
		} else if (likely (decoder->chroma_format == 1)) {
		    int offset;
		    uint8_t * dest_y;

		    coded_block_pattern |= slice->bitstream.buf & (3 << 30);
		    SLICE_DUMPBITS (slice, 2);

		    offset = slice->offset;
		    dest_y = slice->dest[0] + offset;
		    if (coded_block_pattern & 1)
			slice_non_intra_DCT (decoder, slice, 0, dest_y,
					     DCT_stride);
		    if (coded_block_pattern & 2)
			slice_non_intra_DCT (decoder, slice, 0, dest_y + 8,
					     DCT_stride);
		    if (coded_block_pattern & 4)
			slice_non_intra_DCT (decoder, slice, 0,
					     dest_y + DCT_offset,
					     DCT_stride);
		    if (coded_block_pattern & 8)
			slice_non_intra_DCT (decoder, slice, 0,
					     dest_y + DCT_offset + 8,
					     DCT_stride);

		    DCT_stride >>= 1;
		    DCT_offset = (DCT_offset + offset) >> 1;
		    if (coded_block_pattern & 16)
			slice_non_intra_DCT (decoder, slice, 1,
					     slice->dest[1] + (offset >> 1),
					     DCT_stride);
		    if (coded_block_pattern & 32)
			slice_non_intra_DCT (decoder, slice, 2,
					     slice->dest[2] + (offset >> 1),
					     DCT_stride);
		    if (coded_block_pattern & (2 << 30))
			slice_non_intra_DCT (decoder, slice, 1,
					     slice->dest[1] + DCT_offset,
					     DCT_stride);
		    if (coded_block_pattern & (1 << 30))
			slice_non_intra_DCT (decoder, slice, 2,
					     slice->dest[2] + DCT_offset,
					     DCT_stride);
		} else {
		    int offset;
		    uint8_t * dest_y, * dest_u, * dest_v;

		    coded_block_pattern |= slice->bitstream.buf & (63 << 26);
		    SLICE_DUMPBITS (slice, 6);

		    offset = slice->offset;
		    dest_y = slice->dest[0] + offset;
		    dest_u = slice->dest[1] + offset;
		    dest_v = slice->dest[2] + offset;

		    if (coded_block_pattern & 1)
			slice_non_intra_DCT (decoder, slice, 0, dest_y,
					     DCT_stride);
		    if (coded_block_pattern & 2)
			slice_non_intra_DCT (decoder, slice, 0, dest_y + 8,
					     DCT_stride);
		    if (coded_block_pattern & 4)
			slice_non_intra_DCT (decoder, slice, 0,
					     dest_y + DCT_offset,
					     DCT_stride);
		    if (coded_block_pattern & 8)
			slice_non_intra_DCT (decoder, slice, 0,
					     dest_y + DCT_offset + 8,
					     DCT_stride);

		    if (coded_block_pattern & 16)
			slice_non_intra_DCT (decoder, slice, 1,
					     dest_u, DCT_stride);
		    if (coded_block_pattern & 32)
			slice_non_intra_DCT (decoder, slice, 2,
					     dest_v, DCT_stride);
		    if (coded_block_pattern & (32 << 26))
			slice_non_intra_DCT (decoder, slice, 1,
					     dest_u + DCT_offset,
					     DCT_stride);
		    if (coded_block_pattern & (16 << 26))
			slice_non_intra_DCT (decoder, slice, 2,
					     dest_v + DCT_offset,
					     DCT_stride);
		    if (coded_block_pattern & (8 << 26))
			slice_non_intra_DCT (decoder, slice, 1, dest_u + 8,
					     DCT_stride);
		    if (coded_block_pattern & (4 << 26))
			slice_non_intra_DCT (decoder, slice, 2, dest_v + 8,
					     DCT_stride);
		    if (coded_block_pattern & (2 << 26))
			slice_non_intra_DCT (decoder, slice, 1,
					     dest_u + DCT_offset + 8,
					     DCT_stride);
		    if (coded_block_pattern & (1 << 26))
			slice_non_intra_DCT (decoder, slice, 2,
					     dest_v + DCT_offset + 8,
					     DCT_stride);
		}
	    }

	    slice->dc_dct_pred[0] = slice->dc_dct_pred[1] =
		slice->dc_dct_pred[2] = 128 << decoder->intra_dc_precision;
	}

	NEXT_MACROBLOCK;

	SLICE_NEEDBITS (slice);
	mba_inc = 0;
	while (1) {
	    if (slice->bitstream.buf >= 0x10000000) {
		mba = MBA_5 + (UBITS (slice->bitstream.buf, 5) - 2);
		break;
	    } else if (slice->bitstream.buf >= 0x03000000) {
		mba = MBA_11 + (UBITS (slice->bitstream.buf, 11) - 24);
		break;
	    } else switch (UBITS (slice->bitstream.buf, 11)) {
	    case 8:		/* macroblock_escape */
		mba_inc += 33;
		/* pass through */
	    case 15:	/* macroblock_stuffing (MPEG1 only) */
		SLICE_DUMPBITS (slice, 11);
		SLICE_NEEDBITS (slice);
		continue;
	    default:	/* end of slice, or error */
		if (mpeg2_cpu_state_restore)
		    mpeg2_cpu_state_restore (&cpu_state);
		return;
	    }
	}
	SLICE_DUMPBITS (slice, mba->len);
	mba_inc += mba->mba;

	if (mba_inc) {
	    slice->dc_dct_pred[0] = slice->dc_dct_pred[1] =
		slice->dc_dct_pred[2] = 128 << decoder->intra_dc_precision;

	    if (decoder->coding_type == P_TYPE) {
		do {
		    MOTION_CALL (decoder->motion_parser[0],
				 MACROBLOCK_MOTION_FORWARD);
		    NEXT_MACROBLOCK;
		} while (--mba_inc);
	    } else {
		do {
		    MOTION_CALL (decoder->motion_parser[4], macroblock_modes);
		    NEXT_MACROBLOCK;
		} while (--mba_inc);
	    }
	}
    }
}
