#include <stdio.h>

#include "util/u_format.h"
#include "util/u_inlines.h"
#include "util/u_pack_color.h"
#include "util/u_transfer.h"
#include "util/u_clear.h"

#include "tegra_context.h"
#include "tegra_resource.h"
#include "tegra_screen.h"

#include <libdrm/tegra_drm.h>
#include <libdrm/tegra.h>


/*
 * XXX Required to access winsys_handle internals. Should go away in favour
 * of some abstraction to handle handles in a Tegra-specific winsys
 * implementation.
 */
#include "state_tracker/drm_driver.h"

static boolean
tegra_resource_get_handle(struct pipe_screen *pscreen,
			  struct pipe_resource *presource,
			  struct winsys_handle *handle)
{
	struct tegra_resource *resource = tegra_resource(presource);
	boolean ret = TRUE;
	int err;

	fprintf(stdout, "> %s(pscreen=%p, presource=%p, handle=%p)\n",
		__func__, pscreen, presource, handle);
	fprintf(stdout, "  handle:\n");
	fprintf(stdout, "    type: %u\n", handle->type);
	fprintf(stdout, "    handle: %u\n", handle->handle);
	fprintf(stdout, "    stride: %u\n", handle->stride);

	if (handle->type == DRM_API_HANDLE_TYPE_SHARED) {
		err = drm_tegra_bo_get_name(resource->bo, &handle->handle);
		if (err < 0) {
			fprintf(stderr, "drm_tegra_bo_get_name() failed: %d\n", err);
			ret = FALSE;
			goto out;
		}
	} else if (handle->type == DRM_API_HANDLE_TYPE_KMS) {
		err = drm_tegra_bo_get_handle(resource->bo, &handle->handle);
		if (err < 0) {
			fprintf(stderr, "drm_tegra_bo_get_handle() failed: %d\n", err);
			ret = FALSE;
			goto out;
		}
	} else {
		fprintf(stdout, "unsupported handle type: %d\n",
			handle->type);
		ret = FALSE;
	}

	if (ret) {
		handle->stride = resource->pitch;
		fprintf(stdout, "  handle: %u\n", handle->handle);
		fprintf(stdout, "  stride: %u\n", handle->stride);
	}

out:
	fprintf(stdout, "< %s() = %d\n", __func__, ret);
	return ret;
}

static void tegra_resource_destroy(struct pipe_screen *pscreen,
				   struct pipe_resource *presource)
{
	struct tegra_resource *resource = tegra_resource(presource);

	fprintf(stdout, "> %s(pscreen=%p, presource=%p)\n", __func__, pscreen,
		presource);

	free(resource);

	fprintf(stdout, "< %s()\n", __func__);
}

static void *tegra_resource_transfer_map(struct pipe_context *pcontext,
					 struct pipe_resource *presource,
					 unsigned level, unsigned usage,
					 const struct pipe_box *box,
					 struct pipe_transfer **transfer)
{
	struct tegra_context *context = tegra_context(pcontext);
	struct tegra_resource *resource = tegra_resource(presource);
	void *ret = NULL;
	struct pipe_transfer *ptrans;

	fprintf(stdout, "> %s(pcontext=%p, presource=%p, level=%u, usage=%u, box=%p, transfer=%p)\n",
		__func__, pcontext, presource, level, usage, box, transfer);

	if (usage & PIPE_TRANSFER_MAP_DIRECTLY)
		goto out;

	if (drm_tegra_bo_map(resource->bo, &ret))
		goto err;

	ptrans = util_slab_alloc(&context->transfer_pool);
	if (!ptrans)
		goto err;

	ptrans->resource = presource;
	ptrans->level = level;
	ptrans->usage = usage;
	ptrans->box = *box;
	ptrans->stride = resource->pitch;
	ptrans->layer_stride = ptrans->stride;
	*transfer = ptrans;

out:
	fprintf(stdout, "< %s() = %p\n", __func__, ret);
	return ret;

err:
	if (ret)
		drm_tegra_bo_unmap(resource->bo);
	fprintf(stdout, "< %s() = NULL\n", __func__);
	return NULL;
}

static void
tegra_resource_transfer_flush_region(struct pipe_context *pcontext,
				     struct pipe_transfer *transfer,
				     const struct pipe_box *box)
{
	fprintf(stdout, "> %s(pcontext=%p, transfer=%p, box=%p)\n", __func__,
		pcontext, transfer, box);
	fprintf(stdout, "< %s()\n", __func__);
}

static void tegra_resource_transfer_unmap(struct pipe_context *pcontext,
					  struct pipe_transfer *transfer)
{
	struct tegra_context *context = tegra_context(pcontext);
	fprintf(stdout, "> %s(pcontext=%p, transfer=%p)\n", __func__, pcontext,
		transfer);
	drm_tegra_bo_unmap(tegra_resource(transfer->resource)->bo);
	util_slab_free(&context->transfer_pool, transfer);
	fprintf(stdout, "< %s()\n", __func__);
}

static const struct u_resource_vtbl tegra_resource_vtbl = {
	.resource_get_handle = tegra_resource_get_handle,
	.resource_destroy = tegra_resource_destroy,
	.transfer_map = tegra_resource_transfer_map,
	.transfer_flush_region = tegra_resource_transfer_flush_region,
	.transfer_unmap = tegra_resource_transfer_unmap,
	.transfer_inline_write = u_default_transfer_inline_write,
};

static boolean
tegra_screen_can_create_resource(struct pipe_screen *pscreen,
				 const struct pipe_resource *template)
{
	bool ret = TRUE;
	fprintf(stdout, "> %s(pscreen=%p, template=%p)\n", __func__, pscreen,
		template);
	fprintf(stdout, "< %s() = %d\n", __func__, ret);
	return ret;
}

static struct pipe_resource *
tegra_screen_resource_create(struct pipe_screen *pscreen,
			     const struct pipe_resource *template)
{
	struct tegra_screen *screen = tegra_screen(pscreen);
	struct tegra_resource *resource;
	uint32_t flags = 0, height, size;
	int err;

	fprintf(stdout, "> %s(pscreen=%p, template=%p)\n", __func__, pscreen,
		template);
	fprintf(stdout, "  template:\n");
	fprintf(stdout, "    target: %d\n", template->target);
	fprintf(stdout, "    format: %d\n", template->format);
	fprintf(stdout, "    width: %u\n", template->width0);
	fprintf(stdout, "    height: %u\n", template->height0);
	fprintf(stdout, "    depth: %u\n", template->depth0);
	fprintf(stdout, "    array_size: %u\n", template->array_size);
	fprintf(stdout, "    last_level: %u\n", template->last_level);
	fprintf(stdout, "    nr_samples: %u\n", template->nr_samples);
	fprintf(stdout, "    usage: %u\n", template->usage);
	fprintf(stdout, "    bind: %x\n", template->bind);
	fprintf(stdout, "    flags: %x\n", template->flags);

	resource = calloc(1, sizeof(*resource));
	if (!resource) {
		fprintf(stdout, "< %s() = NULL\n", __func__);
		return NULL;
	}

	resource->base.b = *template;

	pipe_reference_init(&resource->base.b.reference, 1);
	resource->base.vtbl = &tegra_resource_vtbl;
	resource->base.b.screen = pscreen;

	resource->pitch = template->width0 * util_format_get_blocksize(template->format);
	height = template->height0;

	resource->tiled = 0;
	if (template->bind & (PIPE_BIND_RENDER_TARGET | PIPE_BIND_SAMPLER_VIEW | PIPE_BIND_SCANOUT)) {
		resource->pitch = template->width0 * util_format_get_blocksize(template->format);
		resource->pitch = align(resource->pitch, 32);

		flags = DRM_TEGRA_GEM_CREATE_BOTTOM_UP;

		/* use linear layout for staging-textures, otherwise tiled */
		if (template->usage != PIPE_USAGE_STAGING && !(template->bind & PIPE_BIND_SHARED)) {
			flags |= DRM_TEGRA_GEM_CREATE_TILED;
			height = align(height, 16);
			resource->tiled = 1;
		}
	}

	size = resource->pitch * height;
	fprintf(stdout, "  pitch:%u size:%u flags:%x\n", resource->pitch, size, flags);

	err = drm_tegra_bo_create(screen->drm, flags, size, &resource->bo);
	if (err < 0) {
		fprintf(stderr, "drm_tegra_bo_create() failed: %d\n", err);
		return NULL;
	}

	fprintf(stdout, "< %s() = %p\n", __func__, &resource->base.b);
	return &resource->base.b;
}

static struct pipe_resource *
tegra_screen_resource_from_handle(struct pipe_screen *pscreen,
				  const struct pipe_resource *template,
				  struct winsys_handle *handle)
{
	struct tegra_screen *screen = tegra_screen(pscreen);
	struct tegra_resource *resource;
	int err;

	fprintf(stdout, "> %s(pscreen=%p, template=%p, handle=%p)\n",
		__func__, pscreen, template, handle);
	fprintf(stdout, "  handle:\n");
	fprintf(stdout, "    type: %u\n", handle->type);
	fprintf(stdout, "    handle: %u\n", handle->handle);
	fprintf(stdout, "    stride: %u\n", handle->stride);

	resource = calloc(1, sizeof(*resource));
	if (!resource) {
		fprintf(stdout, "< %s() = NULL\n", __func__);
		return NULL;
	}

	resource->base.b = *template;

	pipe_reference_init(&resource->base.b.reference, 1);
	resource->base.vtbl = &tegra_resource_vtbl;
	resource->base.b.screen = pscreen;

	err = drm_tegra_bo_open(screen->drm, handle->handle, &resource->bo);
	if (err < 0) {
		fprintf(stderr, "drm_tegra_bo_open() failed: %d\n", err);
		free(resource);
		return NULL;
	}

	resource->pitch = align(template->width0 * util_format_get_blocksize(template->format), 32);

	fprintf(stdout, "< %s() = %p\n", __func__, &resource->base.b);
	return &resource->base.b;
}

void tegra_screen_resource_init(struct pipe_screen *pscreen)
{
	pscreen->can_create_resource = tegra_screen_can_create_resource;
	pscreen->resource_create = tegra_screen_resource_create;
	pscreen->resource_from_handle = tegra_screen_resource_from_handle;
	pscreen->resource_get_handle = u_resource_get_handle_vtbl;
	pscreen->resource_destroy = u_resource_destroy_vtbl;
}

static void tegra_resource_copy_region(struct pipe_context *pcontext,
				       struct pipe_resource *dst,
				       unsigned int dst_level,
				       unsigned int dstx, unsigned dsty,
				       unsigned int dstz,
				       struct pipe_resource *src,
				       unsigned int src_level,
				       const struct pipe_box *box)
{
	fprintf(stdout, "> %s(pcontext=%p, dst=%p, dst_level=%u, dstx=%u, dsty=%u, dstz=%u, src=%p, src_level=%u, box=%p)\n",
		__func__, pcontext, dst, dst_level, dstx, dsty, dstz, src,
		src_level, box);
	fprintf(stdout, "< %s()\n", __func__);
}

static void tegra_blit(struct pipe_context *pcontext,
		       const struct pipe_blit_info *info)
{
	int err, value;
	struct tegra_context *context = tegra_context(pcontext);
	struct tegra_screen *screen = tegra_screen(context->base.screen);
	struct tegra_channel *gr2d = context->gr2d;
	struct host1x_pushbuf *pb;
	struct tegra_resource *dst, *src;

	fprintf(stdout, "> %s(pcontext=%p, info=%p)\n", __func__, pcontext,
		info);

	dst = tegra_resource(info->dst.resource);
	src = tegra_resource(info->src.resource);

	printf("blit-target: %p\n", dst);
	printf("blit-source: %p\n", tegra_resource(info->src.resource));

	printf("*** append job: %p\n", gr2d->job);
	err = host1x_job_append(gr2d->job, gr2d->cmdbuf, 0, &pb);
	if (err < 0) {
		fprintf(stderr, "host1x_job_append() failed: %d\n", err);
		goto out;
	}

	host1x_pushbuf_push(pb, HOST1X_OPCODE_SETCL(0, HOST1X_CLASS_GR2D, 0));

	host1x_pushbuf_push(pb, HOST1X_OPCODE_MASK(0x009, 0x9));
	host1x_pushbuf_push(pb, 0x0000003a);            /* 0x009 - trigger */
	host1x_pushbuf_push(pb, 0x00000000);            /* 0x00c - cmdsel */

	host1x_pushbuf_push(pb, HOST1X_OPCODE_MASK(0x01e, 0x7));
	host1x_pushbuf_push(pb, 0x00000000);            /* 0x01e - controlsecond */
	/*
	 * [20:20] source color depth (0: mono, 1: same)
	 * [17:16] destination color depth (0: 8 bpp, 1: 16 bpp, 2: 32 bpp)
	 */

	value = 1 << 20;
	switch (util_format_get_blocksize(dst->base.b.format)) {
	case 1:
		value |= 0 << 16;
		break;
	case 2:
		value |= 1 << 16;
		break;
	case 4:
		value |= 2 << 16;
		break;
	default:
		assert(0);
	}

	host1x_pushbuf_push(pb, value);                 /* 0x01f - controlmain */
	host1x_pushbuf_push(pb, 0x000000cc);            /* 0x020 - ropfade */

	host1x_pushbuf_push(pb, HOST1X_OPCODE_NONINCR(0x046, 1));

	/*
	 * [20:20] destination write tile mode (0: linear, 1: tiled)
	 * [ 0: 0] tile mode Y/RGB (0: linear, 1: tiled)
	 */
	value = (dst->tiled << 20) | src->tiled;
	host1x_pushbuf_push(pb, value);                 /* 0x046 - tilemode */

	host1x_pushbuf_push(pb, HOST1X_OPCODE_MASK(0x02b, 0xe149));
	host1x_pushbuf_relocate(pb, dst->bo, 0, 0);

	host1x_pushbuf_push(pb, 0xdeadbeef);            /* 0x02b - dstba */

	host1x_pushbuf_push(pb, dst->pitch);            /* 0x02e - dstst */

	host1x_pushbuf_relocate(pb, src->bo, 0, 0);
	host1x_pushbuf_push(pb, 0xdeadbeef);            /* 0x031 - srcba */

	host1x_pushbuf_push(pb, src->pitch);            /* 0x033 - srcst */

	value = info->dst.box.height << 16 | info->dst.box.width;
	host1x_pushbuf_push(pb, value);                 /* 0x038 - dstsize */

	value = info->src.box.y << 16 | info->src.box.x;
	host1x_pushbuf_push(pb, value );                /* 0x039 - srcps */

	value = info->dst.box.y << 16 | info->dst.box.x;
	host1x_pushbuf_push(pb, value);                 /* 0x03a - dstps */

	/* increase syncpt */
	host1x_pushbuf_push(pb, HOST1X_OPCODE_NONINCR(0x00, 1));
	host1x_pushbuf_sync(pb, HOST1X_SYNCPT_COND_OP_DONE);

	{
		struct host1x_fence *fence;
		printf("*** submit job: %p\n", gr2d->job);
		err = drm_tegra_submit(screen->drm, gr2d->job, &fence);
		if (err < 0) {
			fprintf(stderr, "drm_tegra_submit() failed: %d\n", err);
			goto out;
		}
		err = drm_tegra_wait(screen->drm, fence, -1);
		if (err < 0) {
			fprintf(stderr, "drm_tegra_wait() failed: %d\n", err);
			goto out;
		}
	}

out:
	fprintf(stdout, "< %s()\n", __func__);
}

static uint32_t pack_color(enum pipe_format format, const float *rgba)
{
	union util_color uc;
	util_pack_color(rgba, format, &uc);
	return uc.ui;
}

static int tegra_fill(struct tegra_channel *gr2d,
                      struct tegra_resource *dst,
                      uint32_t fill_value, int blocksize,
                      unsigned dstx, unsigned dsty,
                      unsigned width, unsigned height)
{
	struct host1x_pushbuf *pb;
	uint32_t value;
	int err;

	err = host1x_job_append(gr2d->job, gr2d->cmdbuf, 0, &pb);
	if (err < 0) {
		fprintf(stderr, "host1x_job_append() failed: %d\n", err);
		return -1;
	}

	host1x_pushbuf_push(pb, HOST1X_OPCODE_SETCL(0, HOST1X_CLASS_GR2D, 0));

	host1x_pushbuf_push(pb, HOST1X_OPCODE_MASK(0x09, 0x09));
	host1x_pushbuf_push(pb, 0x0000003a);           /* 0x009 - trigger */
	host1x_pushbuf_push(pb, 0x00000000);           /* 0x00c - cmdsel */

	host1x_pushbuf_push(pb, HOST1X_OPCODE_MASK(0x1e, 0x07));
	host1x_pushbuf_push(pb, 0x00000000);           /* 0x01e - controlsecond */

	value  = 1 << 6; /* fill mode */
	value |= 1 << 2; /* turbofill */
	switch (blocksize) {
	case 1:
		value |= 0 << 16;
		break;
	case 2:
		value |= 1 << 16;
		break;
	case 4:
		value |= 2 << 16;
		break;
	default:
		assert(0);
		return -1;
	}
	host1x_pushbuf_push(pb, value);                /* 0x01f - controlmain */

	host1x_pushbuf_push(pb, 0x000000cc);           /* 0x020 - ropfade */

	host1x_pushbuf_push(pb, HOST1X_OPCODE_MASK(0x2b, 0x09));

	host1x_pushbuf_relocate(pb, dst->bo, 0, 0);
	host1x_pushbuf_push(pb, 0xdeadbeef);           /* 0x02b - dstba */

	host1x_pushbuf_push(pb, dst->pitch);           /* 0x02e - dstst */

	host1x_pushbuf_push(pb, HOST1X_OPCODE_NONINCR(0x35, 1));

	host1x_pushbuf_push(pb, fill_value);           /* 0x035 - srcfgc */

	host1x_pushbuf_push(pb, HOST1X_OPCODE_NONINCR(0x46, 1));
	value = dst->tiled << 20;
	host1x_pushbuf_push(pb, value);                /* 0x046 - tilemode */

	host1x_pushbuf_push(pb, HOST1X_OPCODE_MASK(0x38, 0x05));
	host1x_pushbuf_push(pb, height << 16 | width); /* 0x038 - dstsize */
	host1x_pushbuf_push(pb, dsty << 16 | dstx);    /* 0x03a - dstps */

	/* increase syncpt */
	host1x_pushbuf_push(pb, HOST1X_OPCODE_NONINCR(0x00, 1));
	host1x_pushbuf_sync(pb, HOST1X_SYNCPT_COND_OP_DONE);

	return 0;
}

static void tegra_clear(struct pipe_context *pcontext, unsigned int buffers,
			const union pipe_color_union *color, double depth,
			unsigned int stencil)
{
	util_clear(pcontext, &tegra_context(pcontext)->framebuffer.base, buffers, color, depth, stencil);
}

static void tegra_clear_render_target(struct pipe_context *pipe,
                               struct pipe_surface *dst,
                               const union pipe_color_union *color,
                               unsigned dstx, unsigned dsty,
                               unsigned width, unsigned height)
{
	fprintf(stdout, "> %s(pipe=%p, dst=%p, color=%p, dstx=%d, dsty=%d, width=%d, height=%d)\n",
		__func__, pipe, dst, color, dstx, dsty, width, height);

	tegra_fill(tegra_context(pipe)->gr2d, tegra_resource(dst->texture),
	           pack_color(dst->format, color->f), util_format_get_blocksize(dst->format),
		   dstx, dsty, width, height);

	fprintf(stdout, "< %s()\n", __func__);
}

static void tegra_clear_depth_stencil(struct pipe_context *pipe,
                               struct pipe_surface *dst,
                               unsigned clear_flags,
                               double depth,
                               unsigned stencil,
                               unsigned dstx, unsigned dsty,
                               unsigned width, unsigned height)
{
	fprintf(stdout, "> %s(pipe=%p, dst=%p, clear_flags=%x, depth=%f, stencil=%d, dstx=%d, dsty=%d, width=%d, height=%d)\n",
		__func__, pipe, dst, clear_flags, depth, stencil, dstx, dsty, width, height);

	tegra_fill(tegra_context(pipe)->gr2d, tegra_resource(dst->texture),
	           util_pack_z_stencil(depth, stencil, dst->format),
	           util_format_get_blocksize(dst->format),
	           dstx, dsty, width, height);

	fprintf(stdout, "< %s()\n", __func__);
}

static void tegra_flush_resource(struct pipe_context *ctx, struct pipe_resource *resource)
{
}

void tegra_context_resource_init(struct pipe_context *pcontext)
{
	pcontext->transfer_map = u_transfer_map_vtbl;
	pcontext->transfer_flush_region = u_transfer_flush_region_vtbl;
	pcontext->transfer_unmap = u_transfer_unmap_vtbl;
	pcontext->transfer_inline_write = u_transfer_inline_write_vtbl;

	pcontext->resource_copy_region = tegra_resource_copy_region;
	pcontext->blit = tegra_blit;
	pcontext->clear = tegra_clear;
	pcontext->flush_resource = tegra_flush_resource;
	pcontext->clear_render_target = tegra_clear_render_target;
	pcontext->clear_depth_stencil = tegra_clear_depth_stencil;
}
