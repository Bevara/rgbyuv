/*
 *			GPAC - Multimedia Framework C SDK
 *
 *  This file is part of GPAC / RGB to YUV 4:2:0 converter filter.
 *
 *  Every still-image decoder in this build hands back packed RGB - libjpeg,
 *  openjpeg, libpng, libjxr and the rest all do. That is what writegen wants
 *  when the output is an image. It is not what an encoder wants: x264 takes
 *  planar YUV, so a chain that decodes frames and re-encodes them to MP4 -
 *  Motion JPEG and Motion JPEG 2000 are exactly that - has a hole in the
 *  middle where a colour conversion should be.
 *
 *  This filter fills it, and it is declared GF_CAPFLAG_RECONFIG so the session
 *  loads it by itself when the two ends of a link disagree, rather than having
 *  to be named in the graph.
 *
 *  The conversion is BT.601 at studio range - 16..235 for luma, 16..240 for
 *  chroma - which is what H.264 assumes when a stream says nothing else. JPEG
 *  carries full-range YCbCr internally, so a frame that came from a JPEG has
 *  been expanded to RGB by the decoder and is narrowed again here; that round
 *  trip costs a little dynamic range and is the conventional thing to do.
 */

#include <gpac/filters.h>
#include <gpac/constants.h>
#include <string.h>

typedef struct
{
	GF_FilterPid *ipid, *opid;
	u32 width, height, stride, in_fmt, bpp;
} GF_RGBYUVCtx;

static GF_Err rgbyuv_configure_pid(GF_Filter *filter, GF_FilterPid *pid, Bool is_remove)
{
	const GF_PropertyValue *p;
	GF_RGBYUVCtx *ctx = (GF_RGBYUVCtx *)gf_filter_get_udta(filter);

	if (is_remove)
	{
		if (ctx->opid)
		{
			gf_filter_pid_remove(ctx->opid);
			ctx->opid = NULL;
		}
		ctx->ipid = NULL;
		return GF_OK;
	}
	if (!gf_filter_pid_check_caps(pid))
		return GF_NOT_SUPPORTED;

	ctx->ipid = pid;

	p = gf_filter_pid_get_property(pid, GF_PROP_PID_PIXFMT);
	ctx->in_fmt = p ? p->value.uint : GF_PIXEL_RGB;
	switch (ctx->in_fmt)
	{
	case GF_PIXEL_RGB:
	case GF_PIXEL_BGR:
		ctx->bpp = 3;
		break;
	case GF_PIXEL_RGBA:
	case GF_PIXEL_RGBX:
	case GF_PIXEL_BGRA:
	case GF_PIXEL_BGRX:
		ctx->bpp = 4;
		break;
	default:
		return GF_NOT_SUPPORTED;
	}

	p = gf_filter_pid_get_property(pid, GF_PROP_PID_WIDTH);
	ctx->width = p ? p->value.uint : 0;
	p = gf_filter_pid_get_property(pid, GF_PROP_PID_HEIGHT);
	ctx->height = p ? p->value.uint : 0;
	p = gf_filter_pid_get_property(pid, GF_PROP_PID_STRIDE);
	ctx->stride = (p && p->value.uint) ? p->value.uint : ctx->width * ctx->bpp;

	if (!ctx->width || !ctx->height)
		return GF_NOT_SUPPORTED;
	/* 4:2:0 halves both dimensions, so both must be even. */
	if ((ctx->width % 2) || (ctx->height % 2))
	{
		GF_LOG(GF_LOG_ERROR, GF_LOG_MEDIA, ("[RGB2YUV] %ux%u: 4:2:0 needs both dimensions even\n", ctx->width, ctx->height));
		return GF_NOT_SUPPORTED;
	}

	if (!ctx->opid)
		ctx->opid = gf_filter_pid_new(filter);

	gf_filter_pid_copy_properties(ctx->opid, pid);
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_CODECID, &PROP_UINT(GF_CODECID_RAW));
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_PIXFMT, &PROP_UINT(GF_PIXEL_YUV));
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_STRIDE, &PROP_UINT(ctx->width));

	return GF_OK;
}

#define RGBYUV_CLIP(_v) ((u8)(((_v) < 0) ? 0 : (((_v) > 255) ? 255 : (_v))))

static GF_Err rgbyuv_process(GF_Filter *filter)
{
	GF_FilterPacket *pck, *dst_pck;
	const u8 *data;
	u8 *output, *dst_y, *dst_u, *dst_v;
	u32 size, x, y, w, h, cw;
	int r_off, b_off;
	GF_RGBYUVCtx *ctx = (GF_RGBYUVCtx *)gf_filter_get_udta(filter);

	pck = gf_filter_pid_get_packet(ctx->ipid);
	if (!pck)
	{
		if (gf_filter_pid_is_eos(ctx->ipid))
		{
			gf_filter_pid_set_eos(ctx->opid);
			return GF_EOS;
		}
		return GF_OK;
	}
	data = gf_filter_pck_get_data(pck, &size);
	if (!data || (size < (size_t)ctx->stride * ctx->height))
	{
		gf_filter_pid_drop_packet(ctx->ipid);
		GF_LOG(GF_LOG_ERROR, GF_LOG_MEDIA, ("[RGB2YUV] Frame of %u bytes, expected %u\n", size, ctx->stride * ctx->height));
		return GF_NON_COMPLIANT_BITSTREAM;
	}

	w = ctx->width;
	h = ctx->height;
	cw = w / 2;

	dst_pck = gf_filter_pck_new_alloc(ctx->opid, w * h * 3 / 2, &output);
	if (!dst_pck)
	{
		gf_filter_pid_drop_packet(ctx->ipid);
		return GF_OUT_OF_MEM;
	}
	dst_y = output;
	dst_u = output + (size_t)w * h;
	dst_v = dst_u + (size_t)cw * (h / 2);

	/* BGR orders put blue first; the arithmetic is otherwise the same. */
	r_off = ((ctx->in_fmt == GF_PIXEL_BGR) || (ctx->in_fmt == GF_PIXEL_BGRA) || (ctx->in_fmt == GF_PIXEL_BGRX)) ? 2 : 0;
	b_off = 2 - r_off;

	for (y = 0; y < h; y++)
	{
		const u8 *row = data + (size_t)y * ctx->stride;
		u8 *ly = dst_y + (size_t)y * w;

		for (x = 0; x < w; x++)
		{
			const u8 *px = row + (size_t)x * ctx->bpp;
			int R = px[r_off], G = px[1], B = px[b_off];

			/* BT.601 studio range, the fixed-point form used everywhere:
			 * Y  =  16 + ( 66R + 129G +  25B) / 256
			 * Cb = 128 + (-38R -  74G + 112B) / 256
			 * Cr = 128 + (112R -  94G -  18B) / 256 */
			ly[x] = RGBYUV_CLIP(((66 * R + 129 * G + 25 * B + 128) >> 8) + 16);

			/* Chroma is taken from the top-left pixel of each 2x2 block
			 * rather than averaged: the four values a JPEG gives back come
			 * from one chroma sample to begin with, so averaging would blur
			 * what was never sharp. */
			if (!(y & 1) && !(x & 1))
			{
				u32 ci = (y / 2) * cw + (x / 2);
				dst_u[ci] = RGBYUV_CLIP(((-38 * R - 74 * G + 112 * B + 128) >> 8) + 128);
				dst_v[ci] = RGBYUV_CLIP(((112 * R - 94 * G - 18 * B + 128) >> 8) + 128);
			}
		}
	}

	gf_filter_pck_merge_properties(pck, dst_pck);
	gf_filter_pck_send(dst_pck);
	gf_filter_pid_drop_packet(ctx->ipid);
	return GF_OK;
}

static void rgbyuv_finalize(GF_Filter *filter)
{
}

static const GF_FilterCapability RGBYUVCaps[] =
	{
		CAP_UINT(GF_CAPS_INPUT_OUTPUT, GF_PROP_PID_STREAM_TYPE, GF_STREAM_VISUAL),
		CAP_UINT(GF_CAPS_INPUT_OUTPUT, GF_PROP_PID_CODECID, GF_CODECID_RAW),
		/* RECONFIG is what lets the session load this filter by itself to
		 * bridge two ends that disagree on the pixel format. */
		{GF_CAPFLAG_IN_BUNDLE | GF_CAPFLAG_INPUT | GF_CAPFLAG_RECONFIG,
		 GF_PROP_PID_PIXFMT, {GF_PROP_UINT, {.uint = GF_PIXEL_RGB}}, NULL, 0},
		{GF_CAPFLAG_IN_BUNDLE | GF_CAPFLAG_INPUT | GF_CAPFLAG_RECONFIG,
		 GF_PROP_PID_PIXFMT, {GF_PROP_UINT, {.uint = GF_PIXEL_RGBA}}, NULL, 0},
		{GF_CAPFLAG_IN_BUNDLE | GF_CAPFLAG_INPUT | GF_CAPFLAG_RECONFIG,
		 GF_PROP_PID_PIXFMT, {GF_PROP_UINT, {.uint = GF_PIXEL_BGR}}, NULL, 0},
		{GF_CAPFLAG_IN_BUNDLE | GF_CAPFLAG_OUTPUT | GF_CAPFLAG_RECONFIG,
		 GF_PROP_PID_PIXFMT, {GF_PROP_UINT, {.uint = GF_PIXEL_YUV}}, NULL, 0},
};

GF_FilterRegister RGBYUVRegister = {
	.name = "rgb2yuv",
	GF_FS_SET_DESCRIPTION("RGB to YUV 4:2:0 converter")
		GF_FS_SET_HELP("This filter converts packed RGB frames to planar YUV 4:2:0, BT.601 studio range. It exists so that a decoder that produces RGB - every still-image decoder in this build does - can feed a video encoder.")
			.private_size = sizeof(GF_RGBYUVCtx),
	SETCAPS(RGBYUVCaps),
	.configure_pid = rgbyuv_configure_pid,
	.process = rgbyuv_process,
	.finalize = rgbyuv_finalize,
};

const GF_FilterRegister *EMSCRIPTEN_KEEPALIVE rgb2yuv_register(GF_FilterSession *session)
{
	return &RGBYUVRegister;
}

#include "filter_register.h"
__attribute__((constructor))
void register_rgb2yuv(void) {
    gf_filter_auto_register("rgb2yuv", rgb2yuv_register);
}
