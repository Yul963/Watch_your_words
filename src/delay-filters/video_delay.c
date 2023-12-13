#include <obs-module.h>
#include <util/circlebuf.h>
#include <util/dstr.h>
#include <plugin-support.h>

#define DELAY_SEC 6

struct Frame {
	gs_texrender_t *render;
	uint64_t ts;
};

struct wyw_video_delay_info {
	obs_source_t *source;
	struct circlebuf frames;

	uint32_t cx;
	uint32_t cy;
	bool processed_frame;
	bool target_valid;
	bool enabled;
};

static const char *wyw_video_delay_get_name(void *type_data)
{
	UNUSED_PARAMETER(type_data);
	return obs_module_text("delay_video");
}

static void free_textures(struct wyw_video_delay_info *f)
{
	while (f->frames.size) {
		struct Frame frame;
		circlebuf_pop_front(&f->frames, &frame, sizeof(struct Frame));
		obs_enter_graphics();
		gs_texrender_destroy(frame.render);
		obs_leave_graphics();
	}
	circlebuf_free(&f->frames);
}

static void *wyw_video_delay_create(obs_data_t *settings, obs_source_t *source)
{
	struct wyw_video_delay_info *d = (struct wyw_video_delay_info *)bzalloc(sizeof(struct wyw_video_delay_info));
	d->source = source;
	return d;
}

static void wyw_video_delay_destroy(void *data)
{
	struct wyw_video_delay_info *c = (struct wyw_video_delay_info *)data;
	free_textures(c);
	bfree(c);
}

static void draw_frame(struct wyw_video_delay_info *d)
{
	struct Frame *frame = NULL;
	if (!d->frames.size)
		return;

	frame = (struct Frame *)circlebuf_data(&d->frames, (size_t)0);
	
	if (!frame) {
		return;
	}

	gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
	gs_texture_t *tex = gs_texrender_get_texture(frame->render);
	if (tex) {
		gs_eparam_t *image = gs_effect_get_param_by_name(effect, "image");
		gs_effect_set_texture(image, tex);

		while (gs_effect_loop(effect, "Draw"))
			gs_draw_sprite(tex, 0, d->cx, d->cy);
	}
}

static void wyw_video_delay_video_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	struct wyw_video_delay_info *d = (struct wyw_video_delay_info *)data;
	obs_source_t *target = obs_filter_get_target(d->source);
	obs_source_t *parent = obs_filter_get_parent(d->source);

	if (!d->target_valid || !target || !parent) {
		obs_source_skip_video_filter(d->source);
		return;
	}
	if (d->processed_frame) {
		draw_frame(d);
		return;
	}

	const uint64_t ts = obs_get_video_frame_time();
	struct Frame frame;
	frame.render = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	frame.ts = ts;

	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
	if (gs_texrender_begin(frame.render, d->cx, d->cy)) {
		uint32_t parent_flags = obs_source_get_output_flags(target);
		bool custom_draw = (parent_flags & OBS_SOURCE_CUSTOM_DRAW) != 0;
		bool async = (parent_flags & OBS_SOURCE_ASYNC) != 0;
		struct vec4 clear_color;
		vec4_zero(&clear_color);
		gs_clear(GS_CLEAR_COLOR, &clear_color, 0.0f, 0);
		gs_ortho(0.0f, (float)d->cx, 0.0f, (float)d->cy, -100.0f,
			 100.0f);
		if (target == parent && !custom_draw && !async)
			obs_source_default_render(target);
		else
			obs_source_video_render(target);
		gs_texrender_end(frame.render);
	}
	gs_blend_state_pop();

	circlebuf_push_back(&d->frames, &frame, sizeof(struct Frame));

	if (d->frames.size) {
		circlebuf_peek_front(&d->frames, &frame, sizeof(struct Frame));
		if (ts - frame.ts < (DELAY_SEC * (uint64_t)1000000000)) {
			draw_frame(d);
		} else {
			draw_frame(d);
			gs_texrender_destroy(frame.render);
			circlebuf_pop_front(&d->frames, NULL, sizeof(struct Frame));
		}
	}
	d->processed_frame = true;
}

static inline void check_size(struct wyw_video_delay_info *d)
{
	obs_source_t *target = obs_filter_get_target(d->source);

	d->target_valid = !!target;
	if (!d->target_valid)
		return;

	const uint32_t cx = obs_source_get_base_width(target);
	const uint32_t cy = obs_source_get_base_height(target);

	d->target_valid = !!cx && !!cy;
	if (!d->target_valid)
		return;

	if (cx != d->cx || cy != d->cy) {
		d->cx = cx;
		d->cy = cy;
		free_textures(d);
	}
}

static void wyw_video_delay_tick(void *data, float t)
{
	struct wyw_video_delay_info *d = (struct wyw_video_delay_info *)data;
	bool enabled = obs_source_enabled(d->source);
	if (enabled != d->enabled) {
		d->enabled = enabled;
		if (!enabled) {
			free_textures(d);
		}
	}
	if (!enabled)
		return;
	d->processed_frame = false;
	check_size(d);
}

struct obs_source_info video_delay = {
	.id = "WatchYourWord_delay_video",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_OUTPUT_VIDEO,
	.get_name = wyw_video_delay_get_name,
	.create = wyw_video_delay_create,
	.destroy = wyw_video_delay_destroy,
	.video_render = wyw_video_delay_video_render,
	.video_tick = wyw_video_delay_tick,
};