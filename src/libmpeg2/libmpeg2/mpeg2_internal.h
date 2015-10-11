/*
 * mpeg2_internal.h
 * Copyright (C) 2015      DBCTRADO
 * Copyright (C) 2000-2003 Michel Lespinasse <walken@zoy.org>
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

#ifndef LIBMPEG2_MPEG2_INTERNAL_H
#define LIBMPEG2_MPEG2_INTERNAL_H

#include "attributes.h"

#ifdef __cplusplus
extern "C" {
#endif

#define STATE_INTERNAL_NORETURN ((mpeg2_state_t)-1)

/* macroblock modes */
#define MACROBLOCK_INTRA 1
#define MACROBLOCK_PATTERN 2
#define MACROBLOCK_MOTION_BACKWARD 4
#define MACROBLOCK_MOTION_FORWARD 8
#define MACROBLOCK_QUANT 16
#define DCT_TYPE_INTERLACED 32
/* motion_type */
#define MOTION_TYPE_SHIFT 6
#define MC_FIELD 1
#define MC_FRAME 2
#define MC_16X8 2
#define MC_DMV 3

/* picture structure */
#define TOP_FIELD 1
#define BOTTOM_FIELD 2
#define FRAME_PICTURE 3

/* picture coding type */
#define I_TYPE 1
#define P_TYPE 2
#define B_TYPE 3
#define D_TYPE 4

typedef void mpeg2_mc_fct (uint8_t *, const uint8_t *, int, int);

typedef struct {
    uint8_t * ref[2][3];
    uint8_t ** ref2[2];
    int pmv[2][2];
    int f_code[2];
} motion_t;

typedef struct {
    uint32_t buf;		/* current 32 bit working set */
    int bits;			/* used bits in working set */
    const uint8_t * ptr;	/* buffer with stream data */
    const uint8_t * end;	/* end of buffer */
} bitstream_t;

#define GETWORD(bit_buf,shift,bit_ptr,end)			\
do {								\
    if (likely(bit_ptr < end)) {				\
	bit_buf |= ((bit_ptr[0] << 8) | bit_ptr[1]) << (shift);	\
	bit_ptr += 2;						\
    }								\
} while (0)

/* make sure that there are at least 16 valid bits in bit_buf */
#define NEEDBITS(bit_buf,bits,bit_ptr,end)	\
do {						\
    if (unlikely (bits > 0)) {			\
	GETWORD (bit_buf, bits, bit_ptr, end);	\
	bits -= 16;				\
    }						\
} while (0)

/* remove num valid bits from bit_buf */
#define DUMPBITS(bit_buf,bits,num)	\
do {					\
    bit_buf <<= (num);			\
    bits += (num);			\
} while (0)

/* take num bits from the high part of bit_buf and zero extend them */
#define UBITS(bit_buf,num) (((uint32_t)(bit_buf)) >> (32 - (num)))

/* take num bits from the high part of bit_buf and sign extend them */
#define SBITS(bit_buf,num) (((int32_t)(bit_buf)) >> (32 - (num)))

static inline void bitstream_init (bitstream_t * bitstream,
				   const uint8_t * start, int bytes)
{
    bitstream->buf =
	(start[0] << 24) | (start[1] << 16) | (start[2] << 8) | start[3];
    bitstream->bits = -16;
    bitstream->ptr = start + 4;
    bitstream->end = start + bytes;
}

#define bitstream_getword(bitstream, shift)	\
    GETWORD((bitstream)->buf, shift, (bitstream)->ptr, (bitstream)->end)

#define bitstream_needbits(bitstream)				\
do {								\
    if (unlikely ((bitstream)->bits > 0)) {			\
	bitstream_getword (bitstream, (bitstream)->bits);	\
	(bitstream)->bits -= 16;				\
    }								\
} while (0)

#define bitstream_dumpbits(bitstream, num)	\
    DUMPBITS((bitstream)->buf, (bitstream)->bits, num)
#define bitstream_ubits(bitstream, num) UBITS((bitstream)->buf, num)
#define bitstream_sbits(bitstream, num) SBITS((bitstream)->buf, num)
#define bitstream_isoverflow(bitstream) ((bitstream)->ptr >= (bitstream)->end)

typedef struct slice_decode_s {
    bitstream_t bitstream;

    uint8_t code;

    uint8_t * dest[3];

    uint16_t * quantizer_matrix[4];

    /* predictor for DC coefficients in intra blocks */
    int16_t dc_dct_pred[3];

    /* DCT coefficients */
    int16_t * DCTblock;

    int offset;
    unsigned int v_offset;

    motion_t f_motion;
    motion_t b_motion;

#ifdef MPEG2_MT
    mpeg2dec_t * mpeg2dec;
    uint8_t * chunk_buffer;

    struct slice_decode_s * next;
    struct slice_decode_s * next_free;
#endif
} slice_decode_t;

typedef void motion_parser_t (mpeg2_decoder_t * const decoder,
			      slice_decode_t * const slice,
			      motion_t * const motion,
			      mpeg2_mc_fct * const * const table);

struct mpeg2_decoder_s {
    /* first, state that carries information from one macroblock to the */
    /* next inside a slice, and is never used outside of mpeg2_slice() */

    int stride;
    int uv_stride;
    int slice_stride;
    int slice_uv_stride;
    int stride_frame;
    unsigned int limit_x;
    unsigned int limit_y_16;
    unsigned int limit_y_8;
    unsigned int limit_y;

    /* Motion vectors */
    /* The f_ and b_ correspond to the forward and backward motion */
    /* predictors */
    motion_t b_motion;
    motion_t f_motion;
    motion_parser_t * motion_parser[5];

#ifndef MPEG2_MT
    /* DCT coefficients */
    int16_t * DCTblock;
#endif

    uint8_t * picture_dest[3];
    void (* convert) (void * convert_id, uint8_t * const * src,
		      unsigned int v_offset);
    void * convert_id;

    int dmv_offset;

    /* now non-slice-specific information */

    /* sequence header stuff */
    uint16_t (* chroma_quantizer[2])[64];
    uint16_t quantizer_prescale[4][32][64];

    /* The width and height of the picture snapped to macroblock units */
    int width;
    int height;
    int vertical_position_extension;
    int chroma_format;

    /* picture header stuff */

    /* what type of picture this is (I, P, B, D) */
    int coding_type;

    /* picture coding extension stuff */

    /* quantization factor for intra dc coefficients */
    int intra_dc_precision;
    /* top/bottom/both fields */
    int picture_structure;
    /* bool to indicate all predictions are frame based */
    int frame_pred_frame_dct;
    /* bool to indicate whether intra blocks have motion vectors */
    /* (for concealment) */
    int concealment_motion_vectors;
    /* bool to use different vlc tables */
    int intra_vlc_format;
    /* used for DMV MC */
    int top_field_first;

    /* stuff derived from bitstream */

    /* pointer to the zigzag scan we're supposed to be using */
    const uint8_t * scan;

    int second_field;

    int mpeg1;

    /* XXX: stuff due to xine shit */
    int8_t q_scale_type;
};

typedef struct {
    mpeg2_fbuf_t fbuf;
} fbuf_alloc_t;

struct mpeg2dec_s {
    mpeg2_decoder_t decoder;

    mpeg2_info_t info;

    uint32_t shift;
    int is_display_initialized;
    mpeg2_state_t (* action) (struct mpeg2dec_s * mpeg2dec);
    mpeg2_state_t state;
    uint32_t ext_state;

    /* allocated in init - gcc has problems allocating such big structures */
    uint8_t * chunk_buffer;
    /* pointer to start of the current chunk */
    uint8_t * chunk_start;
    /* pointer to current position in chunk_buffer */
    uint8_t * chunk_ptr;
    /* last start code ? */
    uint8_t code;

    /* picture tags */
    uint32_t tag_current, tag2_current, tag_previous, tag2_previous;
    int num_tags;
    int bytes_since_tag;

    int first;
    int alloc_index_user;
    int alloc_index;
    uint8_t first_decode_slice;
    uint8_t nb_decode_slices;

    unsigned int user_data_len;

    mpeg2_sequence_t new_sequence;
    mpeg2_sequence_t sequence;
    mpeg2_gop_t new_gop;
    mpeg2_gop_t gop;
    mpeg2_picture_t new_picture;
    mpeg2_picture_t pictures[4];
    mpeg2_picture_t * picture;
    /*const*/ mpeg2_fbuf_t * fbuf[3];	/* 0: current fbuf, 1-2: prediction fbufs */

    fbuf_alloc_t fbuf_alloc[3];
    int custom_fbuf;

    uint8_t * yuv_buf[3][3];
    int yuv_index;
    mpeg2_convert_t * convert;
    void * convert_arg;
    unsigned int convert_id_size;
    int convert_stride;
    void (* convert_start) (void * id, const mpeg2_fbuf_t * fbuf,
			    const mpeg2_picture_t * picture,
			    const mpeg2_gop_t * gop);

    uint8_t * buf_start;
    uint8_t * buf_end;

    int16_t display_offset_x, display_offset_y;

    int copy_matrix;
    int8_t scaled[4]; /* XXX: MOVED */
    //int8_t q_scale_type, scaled[4];
    uint8_t quantizer_matrix[4][64];
    uint8_t new_quantizer_matrix[4][64];

#ifdef MPEG2_MT
    struct thread_pool_s * thread_pool;
    slice_decode_t * slice_buf;
    slice_decode_t * free_slice_buf;
    struct mutex_s * slice_buf_mutex;
#endif
};

typedef struct {
#ifdef ARCH_PPC
    uint8_t regv[12*16];
#endif
    int dummy;
} cpu_state_t;

/* cpu_accel.c */
uint32_t mpeg2_detect_accel (uint32_t accel);

/* cpu_state.c */
void mpeg2_cpu_state_init (uint32_t accel);

/* decode.c */
mpeg2_state_t mpeg2_seek_header (mpeg2dec_t * mpeg2dec);
mpeg2_state_t mpeg2_parse_header (mpeg2dec_t * mpeg2dec);

/* header.c */
void mpeg2_header_state_init (mpeg2dec_t * mpeg2dec);
void mpeg2_reset_info (mpeg2_info_t * info);
int mpeg2_header_sequence (mpeg2dec_t * mpeg2dec);
int mpeg2_header_gop (mpeg2dec_t * mpeg2dec);
mpeg2_state_t mpeg2_header_picture_start (mpeg2dec_t * mpeg2dec);
int mpeg2_header_picture (mpeg2dec_t * mpeg2dec);
int mpeg2_header_extension (mpeg2dec_t * mpeg2dec);
int mpeg2_header_user_data (mpeg2dec_t * mpeg2dec);
void mpeg2_header_sequence_finalize (mpeg2dec_t * mpeg2dec);
void mpeg2_header_gop_finalize (mpeg2dec_t * mpeg2dec);
void mpeg2_header_picture_finalize (mpeg2dec_t * mpeg2dec, uint32_t accels);
mpeg2_state_t mpeg2_header_slice_start (mpeg2dec_t * mpeg2dec);
mpeg2_state_t mpeg2_header_end (mpeg2dec_t * mpeg2dec);
void mpeg2_set_fbuf (mpeg2dec_t * mpeg2dec, int b_type);

/* slice.c */
void mpeg2_slice (mpeg2_decoder_t * decoder, slice_decode_t * slice);

/* idct.c */
extern void mpeg2_idct_init (uint32_t accel);
extern ATTR_ALIGN(16) uint8_t mpeg2_scan_norm[64];
extern ATTR_ALIGN(16) uint8_t mpeg2_scan_alt[64];

#if defined(ARCH_X86) || defined(ARCH_X86_64)
#ifndef NO_MMX
/* idct_mmx.c */
/*
void mpeg2_idct_copy_sse2 (int16_t * block, uint8_t * dest, int stride);
void mpeg2_idct_add_sse2 (int last, int16_t * block,
			  uint8_t * dest, int stride);
*/
void mpeg2_idct_copy_mmxext (int16_t * block, uint8_t * dest, int stride);
void mpeg2_idct_add_mmxext (int last, int16_t * block,
			    uint8_t * dest, int stride);
void mpeg2_idct_copy_mmx (int16_t * block, uint8_t * dest, int stride);
void mpeg2_idct_add_mmx (int last, int16_t * block,
			 uint8_t * dest, int stride);
void mpeg2_idct_mmx_init (void);
#endif

/* idct_sse2.c */
void mpeg2_idct_copy_sse2 (int16_t * block, uint8_t * dest, int stride);
void mpeg2_idct_add_sse2 (int last, int16_t * block,
			  uint8_t * dest, int stride);
void mpeg2_idct_sse2_init (void);
#endif

#ifdef ARCH_PPC
/* idct_altivec.c */
void mpeg2_idct_copy_altivec (int16_t * block, uint8_t * dest, int stride);
void mpeg2_idct_add_altivec (int last, int16_t * block,
			     uint8_t * dest, int stride);
void mpeg2_idct_altivec_init (void);
#endif

#ifdef ARCH_ALPHA
/* idct_alpha.c */
void mpeg2_idct_copy_mvi (int16_t * block, uint8_t * dest, int stride);
void mpeg2_idct_add_mvi (int last, int16_t * block,
			 uint8_t * dest, int stride);
void mpeg2_idct_copy_alpha (int16_t * block, uint8_t * dest, int stride);
void mpeg2_idct_add_alpha (int last, int16_t * block,
			   uint8_t * dest, int stride);
void mpeg2_idct_alpha_init (void);
#endif

/* motion_comp.c */
void mpeg2_mc_init (uint32_t accel);

typedef struct {
    mpeg2_mc_fct * put [8];
    mpeg2_mc_fct * avg [8];
} mpeg2_mc_t;

#define MPEG2_MC_EXTERN(x) mpeg2_mc_t mpeg2_mc_##x = {			  \
    {MC_put_o_16_##x, MC_put_x_16_##x, MC_put_y_16_##x, MC_put_xy_16_##x, \
     MC_put_o_8_##x,  MC_put_x_8_##x,  MC_put_y_8_##x,  MC_put_xy_8_##x}, \
    {MC_avg_o_16_##x, MC_avg_x_16_##x, MC_avg_y_16_##x, MC_avg_xy_16_##x, \
     MC_avg_o_8_##x,  MC_avg_x_8_##x,  MC_avg_y_8_##x,  MC_avg_xy_8_##x}  \
};

extern mpeg2_mc_t mpeg2_mc_c;
#if defined(ARCH_X86) || defined(ARCH_X86_64)
#ifndef NO_MMX
extern mpeg2_mc_t mpeg2_mc_mmx;
extern mpeg2_mc_t mpeg2_mc_mmxext;
extern mpeg2_mc_t mpeg2_mc_3dnow;
#endif
extern mpeg2_mc_t mpeg2_mc_sse2;
#endif
#ifdef ARCH_PPC
extern mpeg2_mc_t mpeg2_mc_altivec;
#endif
#ifdef ARCH_ALPHA
extern mpeg2_mc_t mpeg2_mc_alpha;
#endif
#ifdef ARCH_SPARC
extern mpeg2_mc_t mpeg2_mc_vis;
#endif
#ifdef ARCH_ARM
extern mpeg2_mc_t mpeg2_mc_arm;
#endif

#ifdef __cplusplus
}
#endif

#endif /* LIBMPEG2_MPEG2_INTERNAL_H */
