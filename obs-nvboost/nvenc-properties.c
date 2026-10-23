#include "nvenc-internal.h"

#include <limits.h>

#include <util/dstr.h> /* astrcmpi */

/* ---------------------------------------------------------------------------
 * 属性面板按 QSV boost 的模式分组（obs_properties_add_group + 子属性集）。
 * 每个分组一个子 obs_properties_t，最后统一挂到根属性集上。
 * --------------------------------------------------------------------------- */

#define add_group(props, id, text, sub) \
	obs_properties_add_group(props, id, obs_module_text(text), OBS_GROUP_NORMAL, sub)

void nvenc_properties_read(struct nvenc_properties *props, obs_data_t *settings)
{
	props->bitrate = obs_data_get_int(settings, "bitrate");
	props->max_bitrate = obs_data_get_int(settings, "max_bitrate");
	props->keyint_sec = obs_data_get_int(settings, "keyint_sec");
	props->cqp = obs_data_get_int(settings, "cqp");
	props->device = obs_data_get_int(settings, "device");
	props->bf = obs_data_get_int(settings, "bf");
	props->bframe_ref_mode = obs_data_get_int(settings, "bframe_ref_mode");
	props->split_encode = obs_data_get_int(settings, "split_encode");
	props->target_quality = obs_data_get_int(settings, "target_quality");

	props->rate_control = obs_data_get_string(settings, "rate_control");
	props->preset = obs_data_get_string(settings, "preset");
	props->profile = obs_data_get_string(settings, "profile");
	props->tune = obs_data_get_string(settings, "tune");
	props->multipass = obs_data_get_string(settings, "multipass");

	props->lookahead = obs_data_get_bool(settings, "lookahead");
	props->disable_scenecut = obs_data_get_bool(settings, "disable_scenecut");
	props->repeat_headers = obs_data_get_bool(settings, "repeat_headers");
	props->force_cuda_tex = obs_data_get_bool(settings, "force_cuda_tex");

	/* 码率控制 / 质量。0 = 自动 */
	props->qp_min = obs_data_get_int(settings, "qp_min");
	props->qp_max = obs_data_get_int(settings, "qp_max");
	props->qp_init = obs_data_get_int(settings, "qp_init");
	props->vbv_bufsize = obs_data_get_int(settings, "vbv_bufsize");
	props->temporal_aq = obs_data_get_bool(settings, "temporal_aq");
	props->adaptive_quantization = obs_data_get_bool(settings, "adaptive_quantization");
	props->aq_strength = obs_data_get_int(settings, "aq_strength");
	props->chroma_qp_offset = obs_data_get_int(settings, "chroma_qp_offset");

	/* 帧间结构 */
	props->strict_gop = obs_data_get_bool(settings, "strict_gop");
	props->multiref_l0 = obs_data_get_int(settings, "multiref_l0");
	props->multiref_l1 = obs_data_get_int(settings, "multiref_l1");
	props->refs_fwd = obs_data_get_int(settings, "refs_fwd");
	props->refs_bwd = obs_data_get_int(settings, "refs_bwd");

	/* 编码工具 */
	props->lookahead_depth = obs_data_get_int(settings, "lookahead_depth");
	props->lookahead_level = obs_data_get_int(settings, "lookahead_level");
	props->disable_badapt = obs_data_get_bool(settings, "disable_badapt");
	props->nonrefp = obs_data_get_bool(settings, "nonrefp");
	props->weightp = obs_data_get_bool(settings, "weightp");
	props->unidirectb = obs_data_get_bool(settings, "unidirectb");
	props->mv_precision = obs_data_get_int(settings, "mv_precision");
	props->tf_level = obs_data_get_int(settings, "tf_level");

	/* 编解码器特定 */
	props->slices = obs_data_get_int(settings, "slices");
	props->filler_data = obs_data_get_bool(settings, "filler_data");
	props->level = obs_data_get_int(settings, "level");
	props->tier = obs_data_get_int(settings, "tier");
	props->tile_cols = obs_data_get_int(settings, "tile_cols");
	props->tile_rows = obs_data_get_int(settings, "tile_rows");
	props->part_size_min = obs_data_get_int(settings, "part_size_min");
	props->part_size_max = obs_data_get_int(settings, "part_size_max");

	if (obs_data_has_user_value(settings, "opts")) {
		props->opts_str = obs_data_get_string(settings, "opts");
		props->opts = obs_parse_options(props->opts_str);
	}

	/* Retain settings object until destroyed since we use its strings. */
	obs_data_addref(settings);
	props->data = settings;
}

static void nvenc_defaults_base(enum codec_type codec, obs_data_t *settings)
{
	struct encoder_caps *caps = get_encoder_caps(codec);

	obs_data_set_default_int(settings, "bitrate", 10000);
	obs_data_set_default_int(settings, "max_bitrate", 10000);
	obs_data_set_default_int(settings, "target_quality", 20);
	obs_data_set_default_int(settings, "keyint_sec", 0);
	obs_data_set_default_int(settings, "cqp", 20);
	obs_data_set_default_int(settings, "device", -1);
	obs_data_set_default_int(settings, "bf", caps->bframes > 0 ? 2 : 0);

	obs_data_set_default_string(settings, "rate_control", "cbr");
	obs_data_set_default_string(settings, "preset", "p5");
	obs_data_set_default_string(settings, "multipass", "qres");
	obs_data_set_default_string(settings, "tune", "hq");
	obs_data_set_default_string(settings, "profile", codec != CODEC_H264 ? "main" : "high");

	/* 空域 AQ：默认关闭。实测固定预算模式（VBR/CBR）低码率下会显著降低画质，
	 * 高强度呈类高斯模糊块效应——详情见选项说明，开启前请阅读 (2D不推荐) 警告。
	 * 强度默认 5（与 NVEncC 默认一致，比 OBS 历史硬编码的 8 温和）。 */
	obs_data_set_default_bool(settings, "adaptive_quantization", false);
	obs_data_set_default_int(settings, "aq_strength", 5);
	obs_data_set_default_bool(settings, "lookahead", caps->lookahead);

	/* 码率控制 / 质量，0 = 自动 */
	obs_data_set_default_int(settings, "qp_min", 0);
	obs_data_set_default_int(settings, "qp_max", 0);
	obs_data_set_default_int(settings, "qp_init", 0);
	obs_data_set_default_int(settings, "vbv_bufsize", 0);
	obs_data_set_default_bool(settings, "temporal_aq", true);
	obs_data_set_default_int(settings, "chroma_qp_offset", 0);

	/* 帧间结构 */
	obs_data_set_default_bool(settings, "strict_gop", false);
	obs_data_set_default_int(settings, "multiref_l0", 0);
	obs_data_set_default_int(settings, "multiref_l1", 0);
	obs_data_set_default_int(settings, "refs_fwd", 1);
	obs_data_set_default_int(settings, "refs_bwd", 1);

	/* 编码工具 */
	obs_data_set_default_int(settings, "lookahead_depth", 8);
	obs_data_set_default_int(settings, "lookahead_level", 0);
	obs_data_set_default_bool(settings, "disable_badapt", false);
	obs_data_set_default_bool(settings, "nonrefp", false);
	obs_data_set_default_bool(settings, "weightp", false);
	obs_data_set_default_bool(settings, "unidirectb", false);
	obs_data_set_default_int(settings, "mv_precision", 0);
	obs_data_set_default_int(settings, "tf_level", 0);

	/* 编解码器特定。level 0 = auto；tier -1 = auto；tile/part 0 = auto */
	obs_data_set_default_int(settings, "slices", 0);
	obs_data_set_default_bool(settings, "filler_data", true);
	obs_data_set_default_int(settings, "level", 0);
	obs_data_set_default_int(settings, "tier", -1);
	obs_data_set_default_int(settings, "tile_cols", 0);
	obs_data_set_default_int(settings, "tile_rows", 0);
	obs_data_set_default_int(settings, "part_size_min", 0);
	obs_data_set_default_int(settings, "part_size_max", 0);

	/* Hidden options */
	obs_data_set_default_bool(settings, "repeat_headers", false);
	obs_data_set_default_bool(settings, "force_cuda_tex", false);
	obs_data_set_default_bool(settings, "disable_scenecut", false);
}

void h264_nvenc_defaults(obs_data_t *settings)
{
	nvenc_defaults_base(CODEC_H264, settings);
}

#ifdef ENABLE_HEVC
void hevc_nvenc_defaults(obs_data_t *settings)
{
	nvenc_defaults_base(CODEC_HEVC, settings);
}
#endif

void av1_nvenc_defaults(obs_data_t *settings)
{
	nvenc_defaults_base(CODEC_AV1, settings);
}

/* ------------------------------------------------------------------------- */
/* 显隐联动                                                                   */

static bool is_lossless(obs_data_t *settings)
{
	return astrcmpi(obs_data_get_string(settings, "rate_control"), "lossless") == 0;
}

/* 码率控制相关的条件显隐 */
static void update_rc_visibility(obs_properties_t *ppts, obs_data_t *settings)
{
	const char *rc = obs_data_get_string(settings, "rate_control");
	bool cqp = astrcmpi(rc, "CQP") == 0;
	bool vbr = astrcmpi(rc, "VBR") == 0;
	bool cqvbr = astrcmpi(rc, "CQVBR") == 0;
	bool lossless = is_lossless(settings);
	bool aq = obs_data_get_bool(settings, "adaptive_quantization");

	/* CBR / VBR 模式下生效的 QP 钳制与 VBV（CQP 忽略、CQVBR 清零 VBV） */
	const bool rc_bounded = !cqp && !lossless && !cqvbr;

	obs_property_set_visible(obs_properties_get(ppts, "bitrate"), !cqp && !lossless && !cqvbr);
	obs_property_set_visible(obs_properties_get(ppts, "max_bitrate"), vbr || cqvbr);
	obs_property_set_visible(obs_properties_get(ppts, "target_quality"), cqvbr);
	obs_property_set_visible(obs_properties_get(ppts, "cqp"), cqp);
	obs_property_set_visible(obs_properties_get(ppts, "qp_min"), rc_bounded);
	obs_property_set_visible(obs_properties_get(ppts, "qp_max"), rc_bounded);
	obs_property_set_visible(obs_properties_get(ppts, "qp_init"), rc_bounded);
	obs_property_set_visible(obs_properties_get(ppts, "vbv_bufsize"), rc_bounded);
	obs_property_set_visible(obs_properties_get(ppts, "filler_data"), astrcmpi(rc, "CBR") == 0);
	obs_property_set_visible(obs_properties_get(ppts, "chroma_qp_offset"), !lossless);
	obs_property_set_visible(obs_properties_get(ppts, "preset"), !lossless);
	obs_property_set_visible(obs_properties_get(ppts, "tune"), !lossless);
	obs_property_set_visible(obs_properties_get(ppts, "temporal_aq"), !lossless);
	obs_property_set_visible(obs_properties_get(ppts, "tf_level"), !lossless);
	/* 无损下驱动会静默忽略加权预测（PPS weighted_pred_flag=0），隐藏该选项避免误导 */
	obs_property_set_visible(obs_properties_get(ppts, "weightp"), !lossless);
	/* 空域 AQ 在所有量化模式下可选（无损除外），默认关闭并带 (2D不推荐) 警告 */
	obs_property_set_visible(obs_properties_get(ppts, "adaptive_quantization"), !lossless);
	obs_property_set_visible(obs_properties_get(ppts, "aq_strength"), !lossless && aq);
}

static bool rate_control_modified(obs_properties_t *ppts, obs_property_t *p, obs_data_t *settings)
{
	UNUSED_PARAMETER(p);
	update_rc_visibility(ppts, settings);
	return true;
}

static bool aq_modified(obs_properties_t *ppts, obs_property_t *p, obs_data_t *settings)
{
	UNUSED_PARAMETER(p);
	bool aq = obs_data_get_bool(settings, "adaptive_quantization");
	bool lossless = is_lossless(settings);

	obs_property_set_visible(obs_properties_get(ppts, "aq_strength"), !lossless && aq);
	return true;
}

static bool lookahead_modified(obs_properties_t *ppts, obs_property_t *p, obs_data_t *settings)
{
	UNUSED_PARAMETER(p);
	bool lookahead = obs_data_get_bool(settings, "lookahead");

	obs_property_set_visible(obs_properties_get(ppts, "lookahead_depth"), lookahead);
	obs_property_set_visible(obs_properties_get(ppts, "lookahead_level"), lookahead);
	return true;
}

/* ------------------------------------------------------------------------- */
/* 构建各分组                                                                 */

static obs_properties_t *build_rate_control_group(enum codec_type codec)
{
	obs_properties_t *g = obs_properties_create();
	obs_property_t *p;
	const int qp_max_val = codec == CODEC_AV1 ? 63 : 51;

	p = obs_properties_add_list(g, "rate_control", obs_module_text("RateControl"), OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(p, obs_module_text("CBR"), "CBR");
	obs_property_list_add_string(p, obs_module_text("CQP"), "CQP");
	obs_property_list_add_string(p, obs_module_text("VBR"), "VBR");
	obs_property_list_add_string(p, obs_module_text("CQVBR"), "CQVBR");
	if (get_encoder_caps(codec)->lossless) {
		obs_property_list_add_string(p, obs_module_text("Lossless"), "lossless");
	}
	obs_property_set_modified_callback(p, rate_control_modified);

	p = obs_properties_add_int(g, "bitrate", obs_module_text("Bitrate"), 50, UINT32_MAX / 1000, 50);
	obs_property_int_set_suffix(p, " Kbps");

	obs_properties_add_int(g, "target_quality", obs_module_text("TargetQuality"), 1,
			       codec == CODEC_AV1 ? 63 : 51, 1);

	p = obs_properties_add_int(g, "max_bitrate", obs_module_text("MaxBitrate"), 0, UINT32_MAX / 1000, 50);
	obs_property_int_set_suffix(p, " Kbps");

	/* AV1 uses 0-255 instead of 0-51 for QP, and most implementations just
	 * multiply the value by 4 to keep the range smaller. */
	obs_properties_add_int(g, "cqp", obs_module_text("CQP"), 1, qp_max_val, 1);

	/* QP 钳制/初始值：单值同时作用 I/P/B，需要 I:P:B 时用自定义 opts */
	obs_properties_add_int(g, "qp_min", obs_module_text("QPMin"), 0, qp_max_val, 1);
	obs_properties_add_int(g, "qp_max", obs_module_text("QPMax"), 0, qp_max_val, 1);
	obs_properties_add_int(g, "qp_init", obs_module_text("QPInit"), 0, qp_max_val, 1);

	p = obs_properties_add_int(g, "vbv_bufsize", obs_module_text("VBVBufSize"), 0, UINT32_MAX / 1000, 50);
	obs_property_int_set_suffix(p, " Kbps");
	obs_property_set_long_description(p, obs_module_text("VBVBufSize.ToolTip"));

	p = obs_properties_add_bool(g, "temporal_aq", obs_module_text("TemporalAQ"));
	obs_property_set_long_description(p, obs_module_text("TemporalAQ.ToolTip"));

	p = obs_properties_add_bool(g, "adaptive_quantization", obs_module_text("AdaptiveQuantization"));
	obs_property_set_long_description(p, obs_module_text("AdaptiveQuantization.ToolTip"));
	obs_property_set_modified_callback(p, aq_modified);

	p = obs_properties_add_int_slider(g, "aq_strength", obs_module_text("AQStrength"), 1, 15, 1);

#ifdef NVENC_11_1_OR_LATER
	p = obs_properties_add_int_slider(g, "chroma_qp_offset", obs_module_text("ChromaQPOffset"), -12, 12, 1);
	obs_property_set_long_description(p, obs_module_text("ChromaQPOffset.ToolTip"));
#endif

	return g;
}

static obs_properties_t *build_preset_profile_group(enum codec_type codec)
{
	obs_properties_t *g = obs_properties_create();
	obs_property_t *p;
	struct encoder_caps *caps = get_encoder_caps(codec);

	p = obs_properties_add_list(g, "preset", obs_module_text("Preset"), OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_STRING);

#ifdef NVENC_10_0_OR_LATER
#define add_preset(val) obs_property_list_add_string(p, obs_module_text("Preset." val), val)
	add_preset("p1");
	add_preset("p2");
	add_preset("p3");
	add_preset("p4");
	add_preset("p5");
	add_preset("p6");
	add_preset("p7");
#undef add_preset
#else
	/* SDK <10.0 只有旧式 preset GUID，P1-P7 就近映射后只剩 3 档，
	 * 按 3 档显示避免"选了没效果"的假选项 */
#define add_preset(val) obs_property_list_add_string(p, obs_module_text("Preset." val), val)
	add_preset("p1");
	add_preset("p5");
	add_preset("p7");
#undef add_preset
#endif

#ifdef NVENC_10_0_OR_LATER
	p = obs_properties_add_list(g, "tune", obs_module_text("Tuning"), OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_STRING);
#define add_tune(val) obs_property_list_add_string(p, obs_module_text("Tuning." val), val)
	/* The UHQ tune is only supported on Turing or later. */
	if (caps->uhq)
		add_tune("uhq");
	add_tune("hq");
	add_tune("ll");
	add_tune("ull");
#undef add_tune

	p = obs_properties_add_list(g, "multipass", obs_module_text("Multipass"), OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_STRING);
#define add_multipass(val) obs_property_list_add_string(p, obs_module_text("Multipass." val), val)
	add_multipass("disabled");
	add_multipass("qres");
	add_multipass("fullres");
#undef add_multipass
#endif

	p = obs_properties_add_list(g, "profile", obs_module_text("Profile"), OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_STRING);
#define add_profile(val) obs_property_list_add_string(p, val, val)
	if (codec == CODEC_HEVC) {
		if (caps->ten_bit)
			add_profile("main10");
		add_profile("main");
	} else if (codec == CODEC_AV1) {
		add_profile("main");
	} else {
#ifdef NVENC_13_0_OR_LATER
		if (caps->ten_bit)
			add_profile("high10");
#endif
		add_profile("high");
		add_profile("main");
		add_profile("baseline");
	}
#undef add_profile

	/* Level：值直接用 nvEncodeAPI.h 的 raw 常量，0 = auto */
	p = obs_properties_add_list(g, "level", obs_module_text("Level"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, obs_module_text("Level.Auto"), 0);
	if (codec == CODEC_H264) {
		obs_property_list_add_int(p, "1.0", 10);
		obs_property_list_add_int(p, "1b", 9);
		obs_property_list_add_int(p, "1.1", 11);
		obs_property_list_add_int(p, "1.2", 12);
		obs_property_list_add_int(p, "1.3", 13);
		obs_property_list_add_int(p, "2.0", 20);
		obs_property_list_add_int(p, "2.1", 21);
		obs_property_list_add_int(p, "2.2", 22);
		obs_property_list_add_int(p, "3.0", 30);
		obs_property_list_add_int(p, "3.1", 31);
		obs_property_list_add_int(p, "3.2", 32);
		obs_property_list_add_int(p, "4.0", 40);
		obs_property_list_add_int(p, "4.1", 41);
		obs_property_list_add_int(p, "4.2", 42);
		obs_property_list_add_int(p, "5.0", 50);
		obs_property_list_add_int(p, "5.1", 51);
		obs_property_list_add_int(p, "5.2", 52);
#ifdef NVENC_13_0_OR_LATER
		obs_property_list_add_int(p, "6.0", 60);
		obs_property_list_add_int(p, "6.1", 61);
		obs_property_list_add_int(p, "6.2", 62);
#endif
	} else if (codec == CODEC_HEVC) {
		obs_property_list_add_int(p, "1.0", 30);
		obs_property_list_add_int(p, "2.0", 60);
		obs_property_list_add_int(p, "2.1", 63);
		obs_property_list_add_int(p, "3.0", 90);
		obs_property_list_add_int(p, "3.1", 93);
		obs_property_list_add_int(p, "4.0", 120);
		obs_property_list_add_int(p, "4.1", 123);
		obs_property_list_add_int(p, "5.0", 150);
		obs_property_list_add_int(p, "5.1", 153);
		obs_property_list_add_int(p, "5.2", 156);
		obs_property_list_add_int(p, "6.0", 180);
		obs_property_list_add_int(p, "6.1", 183);
		obs_property_list_add_int(p, "6.2", 186);
	} else { /* AV1（仅 12.0+ 构建编译），AUTOSELECT(0) 与 2.0 同值 */
		obs_property_list_add_int(p, obs_module_text("Level.Auto"), 0);
		obs_property_list_add_int(p, "2.1", 1);
		obs_property_list_add_int(p, "3.0", 4);
		obs_property_list_add_int(p, "3.1", 5);
		obs_property_list_add_int(p, "4.0", 8);
		obs_property_list_add_int(p, "4.1", 9);
		obs_property_list_add_int(p, "5.0", 12);
		obs_property_list_add_int(p, "5.1", 13);
		obs_property_list_add_int(p, "5.2", 14);
		obs_property_list_add_int(p, "6.0", 16);
		obs_property_list_add_int(p, "6.1", 17);
	}

	/* Tier：-1 = auto（不改动 preset 值，保持向后兼容） */
	if (codec == CODEC_HEVC || codec == CODEC_AV1) {
		p = obs_properties_add_list(g, "tier", obs_module_text("Tier"), OBS_COMBO_TYPE_LIST,
					    OBS_COMBO_FORMAT_INT);
		obs_property_list_add_int(p, obs_module_text("Tier.Auto"), -1);
		if (codec == CODEC_HEVC) {
			obs_property_list_add_int(p, obs_module_text("Tier.Main"), 0);
			obs_property_list_add_int(p, obs_module_text("Tier.High"), 1);
		} else {
			obs_property_list_add_int(p, obs_module_text("Tier.AV1.0"), 0);
			obs_property_list_add_int(p, obs_module_text("Tier.AV1.1"), 1);
		}
	}

	return g;
}

static obs_properties_t *build_inter_frame_group(enum codec_type codec)
{
	obs_properties_t *g = obs_properties_create();
	obs_property_t *p;
	struct encoder_caps *caps = get_encoder_caps(codec);

	obs_properties_add_int(g, "keyint_sec", obs_module_text("KeyframeIntervalSec"), 0, INT_MAX, 1);

	p = obs_properties_add_bool(g, "strict_gop", obs_module_text("StrictGOP"));
	obs_property_set_long_description(p, obs_module_text("StrictGOP.ToolTip"));

	if (caps->bframes > 0) {
		obs_properties_add_int(g, "bf", obs_module_text("BFrames"), 0, caps->bframes, 1);
	}

	/* 原上游仅 HEVC/AV1 显示（担心 H.264 解码器兼容），NVEncC 对 H.264 同样
	 * 提供 bref-mode；已在提示中注明兼容性风险，交由用户自行取舍 */
	if (caps->bref_modes) {
		p = obs_properties_add_list(g, "bframe_ref_mode", obs_module_text("BFrameRefMode"),
					    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);

		obs_property_list_add_int(p, obs_module_text("BframeRefMode.Disabled"),
					  NV_ENC_BFRAME_REF_MODE_DISABLED);

		if (caps->bref_modes & 1) {
			obs_property_list_add_int(p, obs_module_text("BframeRefMode.Each"),
						  NV_ENC_BFRAME_REF_MODE_EACH);
		}
		if (caps->bref_modes & 2) {
			obs_property_list_add_int(p, obs_module_text("BframeRefMode.Middle"),
						  NV_ENC_BFRAME_REF_MODE_MIDDLE);
		}
#ifdef NVENC_13_1_OR_LATER
		/* hierarchical b-ref 始于 SDK 13.1，目前仅 AV1 */
		if (codec == CODEC_AV1 && (caps->bref_modes & NV_ENC_BFRAME_REF_MODE_HIERARCHICAL)) {
			obs_property_list_add_int(p, obs_module_text("BframeRefMode.Hierarchical"),
						  NV_ENC_BFRAME_REF_MODE_HIERARCHICAL);
		}
#endif
	}

	/* 多重参考帧始于 SDK 9.1；AV1 用 refs_fwd/refs_bwd */
#ifdef NVENC_9_1_OR_LATER
	if (codec != CODEC_AV1 && caps->multiref) {
		p = obs_properties_add_int(g, "multiref_l0", obs_module_text("MultirefL0"), 0, 7, 1);
		obs_property_set_long_description(p, obs_module_text("MultirefL0.ToolTip"));
		p = obs_properties_add_int(g, "multiref_l1", obs_module_text("MultirefL1"), 0, 4, 1);
		obs_property_set_long_description(p, obs_module_text("MultirefL1.ToolTip"));
	}
#endif

	if (codec == CODEC_AV1) {
		p = obs_properties_add_int(g, "refs_fwd", obs_module_text("RefsFwd"), 0, 4, 1);
		obs_property_set_long_description(p, obs_module_text("RefsFwd.ToolTip"));
		p = obs_properties_add_int(g, "refs_bwd", obs_module_text("RefsBwd"), 0, 3, 1);
		obs_property_set_long_description(p, obs_module_text("RefsBwd.ToolTip"));
	}

	p = obs_properties_add_bool(g, "repeat_headers", obs_module_text("RepeatHeaders"));
	obs_property_set_long_description(p, obs_module_text("RepeatHeaders.ToolTip"));

	return g;
}

static obs_properties_t *build_enc_tools_group(enum codec_type codec)
{
	obs_properties_t *g = obs_properties_create();
	obs_property_t *p;
	struct encoder_caps *caps = get_encoder_caps(codec);

	p = obs_properties_add_bool(g, "lookahead", obs_module_text("LookAhead"));
	obs_property_set_long_description(p, obs_module_text("LookAhead.ToolTip"));
	obs_property_set_modified_callback(p, lookahead_modified);

	p = obs_properties_add_int_slider(g, "lookahead_depth", obs_module_text("LookAheadDepth"), 0, 32, 1);
	obs_property_set_long_description(p, obs_module_text("LookAheadDepth.ToolTip"));

#ifdef NVENC_12_2_OR_LATER
	/* lookahead level 是 HEVC 专属功能（SDK 12.2 引入）；
	 * 且 <570.20 驱动的 cap 上报不可靠（见 encoder_caps 注释），双重限定 */
	if (codec == CODEC_HEVC && caps->lookahead_level) {
		p = obs_properties_add_list(g, "lookahead_level", obs_module_text("LookAheadLevel"),
					    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
		obs_property_list_add_int(p, obs_module_text("LookAheadLevel.0"), 0);
		obs_property_list_add_int(p, obs_module_text("LookAheadLevel.1"), 1);
		obs_property_list_add_int(p, obs_module_text("LookAheadLevel.2"), 2);
		obs_property_list_add_int(p, obs_module_text("LookAheadLevel.3"), 3);
		obs_property_set_long_description(p, obs_module_text("LookAheadLevel.ToolTip"));
	}

	if (caps->unidirectional_b) {
		p = obs_properties_add_bool(g, "unidirectb", obs_module_text("UniDirectB"));
		obs_property_set_long_description(p, obs_module_text("UniDirectB.ToolTip"));
	}

#ifdef NVENC_13_0_OR_LATER
	/* tfLevel 字段始于 SDK 13.0 */
	if (caps->temporal_filter) {
		p = obs_properties_add_list(g, "tf_level", obs_module_text("TFLevel"), OBS_COMBO_TYPE_LIST,
					    OBS_COMBO_FORMAT_INT);
		obs_property_list_add_int(p, obs_module_text("TFLevel.0"), 0);
		obs_property_list_add_int(p, obs_module_text("TFLevel.4"), 4);
		obs_property_set_long_description(p, obs_module_text("TFLevel.ToolTip"));
	}
#endif
#endif /* NVENC_12_2_OR_LATER */

	p = obs_properties_add_bool(g, "disable_scenecut", obs_module_text("DisableAdaptiveI"));
	obs_property_set_long_description(p, obs_module_text("DisableAdaptiveI.ToolTip"));

	p = obs_properties_add_bool(g, "disable_badapt", obs_module_text("DisableAdaptiveB"));
	obs_property_set_long_description(p, obs_module_text("DisableAdaptiveB.ToolTip"));

	p = obs_properties_add_bool(g, "nonrefp", obs_module_text("NonRefP"));
	obs_property_set_long_description(p, obs_module_text("NonRefP.ToolTip"));

	/* 加权预测不支持 B 帧（frameIntervalP > 1），且与 HEVC split encode 互斥 */
	if (codec != CODEC_AV1 && caps->weighted_pred) {
		p = obs_properties_add_bool(g, "weightp", obs_module_text("WeightP"));
		obs_property_set_long_description(p, obs_module_text("WeightP.ToolTip"));
	}

	if (caps->qpel_mv) {
		p = obs_properties_add_list(g, "mv_precision", obs_module_text("MvPrecision"), OBS_COMBO_TYPE_LIST,
					    OBS_COMBO_FORMAT_INT);
		obs_property_list_add_int(p, obs_module_text("MvPrecision.Default"), NV_ENC_MV_PRECISION_DEFAULT);
		obs_property_list_add_int(p, obs_module_text("MvPrecision.FullPel"), NV_ENC_MV_PRECISION_FULL_PEL);
		obs_property_list_add_int(p, obs_module_text("MvPrecision.HalfPel"), NV_ENC_MV_PRECISION_HALF_PEL);
		obs_property_list_add_int(p, obs_module_text("MvPrecision.QuarterPel"),
					  NV_ENC_MV_PRECISION_QUARTER_PEL);
	}

	return g;
}

static obs_properties_t *build_codec_specific_group(enum codec_type codec)
{
	obs_properties_t *g = obs_properties_create();
	obs_property_t *p;

	if (codec != CODEC_AV1) {
		p = obs_properties_add_int(g, "slices", obs_module_text("Slices"), 0, 128, 1);
		obs_property_set_long_description(p, obs_module_text("Slices.ToolTip"));
	}

	p = obs_properties_add_bool(g, "filler_data", obs_module_text("FillerData"));
	obs_property_set_long_description(p, obs_module_text("FillerData.ToolTip"));

	if (codec == CODEC_AV1) {
		p = obs_properties_add_int(g, "tile_cols", obs_module_text("TileColumns"), 0, 64, 1);
		obs_property_set_long_description(p, obs_module_text("TileColumns.ToolTip"));
		p = obs_properties_add_int(g, "tile_rows", obs_module_text("TileRows"), 0, 64, 1);
		obs_property_set_long_description(p, obs_module_text("TileRows.ToolTip"));

		/* NV_ENC_AV1_PART_SIZE 始于 SDK 12.0；AV1 编码器本身仅 12.0+ 构建注册 */
#ifdef NVENC_12_0_OR_LATER
		obs_property_t *part_min = obs_properties_add_list(g, "part_size_min", obs_module_text("PartSizeMin"),
								   OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
		obs_property_t *part_max = obs_properties_add_list(g, "part_size_max", obs_module_text("PartSizeMax"),
								   OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
		for (int mode = 0; mode <= NV_ENC_AV1_PART_SIZE_64x64; mode++) {
			const char *name;
			switch (mode) {
			case NV_ENC_AV1_PART_SIZE_AUTOSELECT:
				name = obs_module_text("PartSize.Auto");
				break;
			case NV_ENC_AV1_PART_SIZE_4x4:
				name = "4x4";
				break;
			case NV_ENC_AV1_PART_SIZE_8x8:
				name = "8x8";
				break;
			case NV_ENC_AV1_PART_SIZE_16x16:
				name = "16x16";
				break;
			case NV_ENC_AV1_PART_SIZE_32x32:
				name = "32x32";
				break;
			default:
				name = "64x64";
				break;
			}
			obs_property_list_add_int(part_min, name, mode);
			obs_property_list_add_int(part_max, name, mode);
		}
#endif
	}

	return g;
}

static obs_properties_t *build_misc_group(enum codec_type codec)
{
	obs_properties_t *g = obs_properties_create();
	obs_property_t *p;

	if (num_encoder_devices() > 1) {
		obs_properties_add_int(g, "device", obs_module_text("GPU"), -1, num_encoder_devices(), 1);
	}

#ifdef NVENC_12_1_OR_LATER
	/* Some older GPUs such as the 1080 Ti have 2 NVENC chips, but do not
	 * support split encoding. Therefore, we check for AV1 support here to
	 * make sure this option is only presented on 40-series and later. */
	if (is_codec_supported(CODEC_AV1) && get_encoder_caps(codec)->engines > 1 && !has_broken_split_encoding() &&
	    (codec == CODEC_HEVC || codec == CODEC_AV1)) {
		p = obs_properties_add_list(g, "split_encode", obs_module_text("SplitEncode"), OBS_COMBO_TYPE_LIST,
					    OBS_COMBO_FORMAT_INT);

		obs_property_list_add_int(p, obs_module_text("SplitEncode.Auto"), NV_ENC_SPLIT_AUTO_MODE);
		obs_property_list_add_int(p, obs_module_text("SplitEncode.AutoForced"), NV_ENC_SPLIT_AUTO_FORCED_MODE);
		obs_property_list_add_int(p, obs_module_text("SplitEncode.Disabled"), NV_ENC_SPLIT_DISABLE_MODE);
		obs_property_list_add_int(p, obs_module_text("SplitEncode.Enabled"), NV_ENC_SPLIT_TWO_FORCED_MODE);
		if (get_encoder_caps(codec)->engines > 2) {
			obs_property_list_add_int(p, obs_module_text("SplitEncode.ThreeWay"),
						  NV_ENC_SPLIT_THREE_FORCED_MODE);
		}
#ifdef NVENC_13_0_OR_LATER
		if (get_encoder_caps(codec)->engines > 3) {
			obs_property_list_add_int(p, obs_module_text("SplitEncode.FourWay"),
						  NV_ENC_SPLIT_FOUR_FORCED_MODE);
		}
#endif
	}
#endif

	p = obs_properties_add_text(g, "opts", obs_module_text("Opts"), OBS_TEXT_DEFAULT);
	obs_property_set_long_description(p, obs_module_text("Opts.TT"));

	return g;
}

obs_properties_t *nvenc_properties_internal(enum codec_type codec)
{
	obs_properties_t *props = obs_properties_create();

	add_group(props, "group_rate_control", "Group.RateControl", build_rate_control_group(codec));
	add_group(props, "group_preset_profile", "Group.PresetProfile", build_preset_profile_group(codec));
	add_group(props, "group_inter_frame", "Group.InterFrame", build_inter_frame_group(codec));
	add_group(props, "group_enc_tools", "Group.EncTools", build_enc_tools_group(codec));
	add_group(props, "group_codec_specific", "Group.CodecSpecific", build_codec_specific_group(codec));
	add_group(props, "group_misc", "Group.Misc", build_misc_group(codec));

	/* Invisible properties */
	obs_property_t *p = obs_properties_add_bool(props, "force_cuda_tex", "force_cuda_tex");
	obs_property_set_visible(p, false);

	return props;
}

obs_properties_t *h264_nvenc_properties(void *unused)
{
	UNUSED_PARAMETER(unused);
	return nvenc_properties_internal(CODEC_H264);
}

#ifdef ENABLE_HEVC
obs_properties_t *hevc_nvenc_properties(void *unused)
{
	UNUSED_PARAMETER(unused);
	return nvenc_properties_internal(CODEC_HEVC);
}
#endif

obs_properties_t *av1_nvenc_properties(void *unused)
{
	UNUSED_PARAMETER(unused);
	return nvenc_properties_internal(CODEC_AV1);
}
