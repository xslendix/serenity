/*
 * Copyright (c) 2021, Hunter Salyer <thefalsehonesty@gmail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "VP9Decoder.h"

namespace Video {

#define RESERVED_ZERO                  \
    if (m_bit_stream->read_bit() != 0) \
    return false

VP9Decoder::VP9Decoder()
{
    m_probability_tables = make<ProbabilityTables>();
}

bool VP9Decoder::parse_frame(const ByteBuffer& frame_data)
{
    m_bit_stream = make<BitStream>(frame_data.data(), frame_data.size());
    m_syntax_element_counter = make<SyntaxElementCounter>();

    if (!uncompressed_header())
        return false;
    dbgln("Finished reading uncompressed header");
    if (!trailing_bits())
        return false;
    if (m_header_size_in_bytes == 0) {
        // FIXME: Do we really need to read all of these bits?
        // while (m_bit_stream->get_position() < m_start_bit_pos + (8 * frame_data.size()))
        //     RESERVED_ZERO;
        dbgln("No header");
        return true;
    }
    m_probability_tables->load_probs(m_frame_context_idx);
    m_probability_tables->load_probs2(m_frame_context_idx);
    m_syntax_element_counter->clear_counts();

    if (!m_bit_stream->init_bool(m_header_size_in_bytes))
        return false;
    dbgln("Reading compressed header");
    if (!compressed_header())
        return false;
    dbgln("Finished reading compressed header");
    if (!m_bit_stream->exit_bool())
        return false;
    dbgln("Finished reading frame!");

    decode_tiles();
    return true;
}

bool VP9Decoder::uncompressed_header()
{
    auto frame_marker = m_bit_stream->read_f(2);
    if (frame_marker != 2)
        return false;
    auto profile_low_bit = m_bit_stream->read_bit();
    auto profile_high_bit = m_bit_stream->read_bit();
    m_profile = (profile_high_bit << 1u) + profile_low_bit;
    if (m_profile == 3)
        RESERVED_ZERO;
    auto show_existing_frame = m_bit_stream->read_bit();
    if (show_existing_frame) {
        m_frame_to_show_map_index = m_bit_stream->read_f(3);
        m_header_size_in_bytes = 0;
        m_refresh_frame_flags = 0;
        m_loop_filter_level = 0;
        return true;
    }

    m_last_frame_type = m_frame_type;
    m_frame_type = read_frame_type();
    m_show_frame = m_bit_stream->read_bit();
    m_error_resilient_mode = m_bit_stream->read_bit();

    if (m_frame_type == KEY_FRAME) {
        if (!frame_sync_code())
            return false;
        if (!color_config())
            return false;
        if (!frame_size())
            return false;
        if (!render_size())
            return false;
        m_refresh_frame_flags = 0xFF;
        m_frame_is_intra = true;
    } else {
        m_frame_is_intra = !m_show_frame && m_bit_stream->read_bit();

        if (!m_error_resilient_mode) {
            m_reset_frame_context = m_bit_stream->read_f(2);
        } else {
            m_reset_frame_context = 0;
        }

        if (m_frame_is_intra) {
            if (!frame_sync_code())
                return false;
            if (m_profile > 0) {
                if (!color_config())
                    return false;
            } else {
                m_color_space = CS_BT_601;
                m_subsampling_x = true;
                m_subsampling_y = true;
                m_bit_depth = 8;
            }

            m_refresh_frame_flags = m_bit_stream->read_f8();
            if (!frame_size())
                return false;
            if (!render_size())
                return false;
        } else {
            m_refresh_frame_flags = m_bit_stream->read_f8();
            for (auto i = 0; i < 3; i++) {
                m_ref_frame_idx[i] = m_bit_stream->read_f(3);
                m_ref_frame_sign_bias[LAST_FRAME + i] = m_bit_stream->read_bit();
            }
            frame_size_with_refs();
            m_allow_high_precision_mv = m_bit_stream->read_bit();
            read_interpolation_filter();
        }
    }

    if (!m_error_resilient_mode) {
        m_refresh_frame_context = m_bit_stream->read_bit();
        m_frame_parallel_decoding_mode = m_bit_stream->read_bit();
    } else {
        m_refresh_frame_context = false;
        m_frame_parallel_decoding_mode = true;
    }

    m_frame_context_idx = m_bit_stream->read_f(2);
    if (m_frame_is_intra || m_error_resilient_mode) {
        setup_past_independence();
        if (m_frame_type == KEY_FRAME || m_error_resilient_mode || m_reset_frame_context == 3) {
            for (auto i = 0; i < 4; i++) {
                m_probability_tables->save_probs(i);
            }
        } else if (m_reset_frame_context == 2) {
            m_probability_tables->save_probs(m_frame_context_idx);
        }
        m_frame_context_idx = 0;
    }

    loop_filter_params();
    quantization_params();
    segmentation_params();
    tile_info();

    m_header_size_in_bytes = m_bit_stream->read_f16();

    return true;
}

bool VP9Decoder::frame_sync_code()
{
    if (m_bit_stream->read_byte() != 0x49)
        return false;
    if (m_bit_stream->read_byte() != 0x83)
        return false;
    return m_bit_stream->read_byte() == 0x42;
}

bool VP9Decoder::color_config()
{
    if (m_profile >= 2) {
        m_bit_depth = m_bit_stream->read_bit() ? 12 : 10;
    } else {
        m_bit_depth = 8;
    }

    auto color_space = m_bit_stream->read_f(3);
    if (color_space > CS_RGB)
        return false;
    m_color_space = static_cast<ColorSpace>(color_space);

    if (color_space != CS_RGB) {
        m_color_range = read_color_range();
        if (m_profile == 1 || m_profile == 3) {
            m_subsampling_x = m_bit_stream->read_bit();
            m_subsampling_y = m_bit_stream->read_bit();
            RESERVED_ZERO;
        } else {
            m_subsampling_x = true;
            m_subsampling_y = true;
        }
    } else {
        m_color_range = FULL_SWING;
        if (m_profile == 1 || m_profile == 3) {
            m_subsampling_x = false;
            m_subsampling_y = false;
            RESERVED_ZERO;
        }
    }
    return true;
}

bool VP9Decoder::frame_size()
{
    m_frame_width = m_bit_stream->read_f16() + 1;
    m_frame_height = m_bit_stream->read_f16() + 1;
    compute_image_size();
    return true;
}

bool VP9Decoder::render_size()
{
    if (m_bit_stream->read_bit()) {
        m_render_width = m_bit_stream->read_f16() + 1;
        m_render_height = m_bit_stream->read_f16() + 1;
    } else {
        m_render_width = m_frame_width;
        m_render_height = m_frame_height;
    }
    return true;
}

bool VP9Decoder::frame_size_with_refs()
{
    bool found_ref;
    for (auto i = 0; i < 3; i++) {
        found_ref = m_bit_stream->read_bit();
        if (found_ref) {
            // TODO:
            //  - FrameWidth = RefFrameWidth[ref_frame_idx[ i] ];
            //  - FrameHeight = RefFrameHeight[ref_frame_idx[ i] ];
            break;
        }
    }

    if (!found_ref)
        frame_size();
    else
        compute_image_size();

    render_size();
    return true;
}

bool VP9Decoder::compute_image_size()
{
    m_mi_cols = (m_frame_width + 7u) >> 3u;
    m_mi_rows = (m_frame_height + 7u) >> 3u;
    m_sb64_cols = (m_mi_cols + 7u) >> 3u;
    m_sb64_rows = (m_mi_rows + 7u) >> 3u;
    return true;
}

bool VP9Decoder::read_interpolation_filter()
{
    if (m_bit_stream->read_bit()) {
        m_interpolation_filter = SWITCHABLE;
    } else {
        m_interpolation_filter = literal_to_type[m_bit_stream->read_f(2)];
    }
    return true;
}

bool VP9Decoder::loop_filter_params()
{
    m_loop_filter_level = m_bit_stream->read_f(6);
    m_loop_filter_sharpness = m_bit_stream->read_f(3);
    m_loop_filter_delta_enabled = m_bit_stream->read_bit();
    if (m_loop_filter_delta_enabled) {
        if (m_bit_stream->read_bit()) {
            for (auto i = 0; i < 4; i++) {
                if (m_bit_stream->read_bit()) {
                    // TODO: loop_filter_ref_deltas[i] = s(6);
                }
            }
            for (auto i = 0; i < 2; i++) {
                if (m_bit_stream->read_bit()) {
                    // TODO: loop_filter_mode_deltas[i] = s(6);
                }
            }
        }
    }
    return true;
}

bool VP9Decoder::quantization_params()
{
    auto base_q_idx = m_bit_stream->read_byte();
    auto delta_q_y_dc = read_delta_q();
    auto delta_q_uv_dc = read_delta_q();
    auto delta_q_uv_ac = read_delta_q();
    m_lossless = base_q_idx == 0 && delta_q_y_dc == 0 && delta_q_uv_dc == 0 && delta_q_uv_ac == 0;
    return true;
}

i8 VP9Decoder::read_delta_q()
{
    if (m_bit_stream->read_bit())
        return m_bit_stream->read_s(4);
    return 0;
}

static constexpr u8 segmentation_feature_bits[SEG_LVL_MAX] = { 8, 6, 2, 0 };
static constexpr bool segmentation_feature_signed[SEG_LVL_MAX] = { true, true, false, false };
static constexpr u8 inv_map_table[MAX_PROB] = {
    7, 20, 33, 46, 59, 72, 85, 98, 111, 124, 137, 150, 163, 176, 189, 202, 215, 228, 241, 254,
    1, 2, 3, 4, 5, 6, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 21, 22, 23, 24, 25, 26, 27,
    28, 29, 30, 31, 32, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 47, 48, 49, 50, 51, 52,
    53, 54, 55, 56, 57, 58, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 73, 74, 75, 76, 77,
    78, 79, 80, 81, 82, 83, 84, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 99, 100, 101, 102,
    103, 104, 105, 106, 107, 108, 109, 110, 112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122,
    123, 125, 126, 127, 128, 129, 130, 131, 132, 133, 134, 135, 136, 138, 139, 140, 141, 142, 143,
    144, 145, 146, 147, 148, 149, 151, 152, 153, 154, 155, 156, 157, 158, 159, 160, 161, 162, 164,
    165, 166, 167, 168, 169, 170, 171, 172, 173, 174, 175, 177, 178, 179, 180, 181, 182, 183, 184,
    185, 186, 187, 188, 190, 191, 192, 193, 194, 195, 196, 197, 198, 199, 200, 201, 203, 204, 205,
    206, 207, 208, 209, 210, 211, 212, 213, 214, 216, 217, 218, 219, 220, 221, 222, 223, 224, 225,
    226, 227, 229, 230, 231, 232, 233, 234, 235, 236, 237, 238, 239, 240, 242, 243, 244, 245, 246,
    247, 248, 249, 250, 251, 252, 253, 253
};

bool VP9Decoder::segmentation_params()
{
    auto segmentation_enabled = m_bit_stream->read_bit();
    if (!segmentation_enabled)
        return true;

    auto segmentation_update_map = m_bit_stream->read_bit();
    if (segmentation_update_map) {
        for (auto i = 0; i < 7; i++) {
            m_segmentation_tree_probs[i] = read_prob();
        }
        auto segmentation_temporal_update = m_bit_stream->read_bit();
        for (auto i = 0; i < 3; i++) {
            m_segmentation_pred_prob[i] = segmentation_temporal_update ? read_prob() : 255;
        }
    }

    if (!m_bit_stream->read_bit())
        return true;

    m_segmentation_abs_or_delta_update = m_bit_stream->read_bit();
    for (auto i = 0; i < MAX_SEGMENTS; i++) {
        for (auto j = 0; j < SEG_LVL_MAX; j++) {
            auto feature_value = 0;
            auto feature_enabled = m_bit_stream->read_bit();
            m_feature_enabled[i][j] = feature_enabled;
            if (feature_enabled) {
                auto bits_to_read = segmentation_feature_bits[j];
                feature_value = m_bit_stream->read_f(bits_to_read);
                if (segmentation_feature_signed[j]) {
                    if (m_bit_stream->read_bit())
                        feature_value = -feature_value;
                }
            }
            m_feature_data[i][j] = feature_value;
        }
    }
    return true;
}

u8 VP9Decoder::read_prob()
{
    if (m_bit_stream->read_bit())
        return m_bit_stream->read_byte();
    return 255;
}

bool VP9Decoder::tile_info()
{
    auto min_log2_tile_cols = calc_min_log2_tile_cols();
    auto max_log2_tile_cols = calc_max_log2_tile_cols();
    m_tile_cols_log2 = min_log2_tile_cols;
    while (m_tile_cols_log2 < max_log2_tile_cols) {
        if (m_bit_stream->read_bit())
            m_tile_cols_log2++;
        else
            break;
    }
    m_tile_rows_log2 = m_bit_stream->read_bit();
    if (m_tile_rows_log2) {
        m_tile_rows_log2 += m_bit_stream->read_bit();
    }
    return true;
}

u16 VP9Decoder::calc_min_log2_tile_cols()
{
    auto min_log_2 = 0u;
    while ((u8)(MAX_TILE_WIDTH_B64 << min_log_2) < m_sb64_cols)
        min_log_2++;
    return min_log_2;
}

u16 VP9Decoder::calc_max_log2_tile_cols()
{
    auto max_log_2 = 1;
    while ((m_sb64_cols >> max_log_2) >= MIN_TILE_WIDTH_B64)
        max_log_2++;
    return max_log_2 - 1;
}

bool VP9Decoder::setup_past_independence()
{
    for (auto i = 0; i < 8; i++) {
        for (auto j = 0; j < 4; j++) {
            m_feature_data[i][j] = 0;
            m_feature_enabled[i][j] = false;
        }
    }
    m_segmentation_abs_or_delta_update = false;
    for (auto row = 0u; row < m_mi_rows; row++) {
        for (auto col = 0u; col < m_mi_cols; col++) {
            // TODO: m_prev_segment_ids[row][col] = 0;
        }
    }
    m_loop_filter_delta_enabled = true;
    m_loop_filter_ref_deltas[INTRA_FRAME] = 1;
    m_loop_filter_ref_deltas[LAST_FRAME] = 0;
    m_loop_filter_ref_deltas[GOLDEN_FRAME] = -1;
    m_loop_filter_ref_deltas[ALTREF_FRAME] = -1;
    for (auto i = 0; i < 2; i++) {
        m_loop_filter_mode_deltas[i] = 0;
    }
    m_probability_tables->reset_probs();
    return true;
}

bool VP9Decoder::trailing_bits()
{
    while (m_bit_stream->get_position() & 7u)
        RESERVED_ZERO;
    return true;
}

bool VP9Decoder::compressed_header()
{
    read_tx_mode();
    if (m_tx_mode == TXModeSelect) {
        tx_mode_probs();
    }
    read_coef_probs();
    read_skip_prob();
    if (!m_frame_is_intra) {
        read_inter_mode_probs();
        if (m_interpolation_filter == SWITCHABLE) {
            read_interp_filter_probs();
        }
        read_is_inter_probs();
        frame_reference_mode();
        frame_reference_mode_probs();
        read_y_mode_probs();
        read_partition_probs();
        mv_probs();
    }
    return true;
}

bool VP9Decoder::read_tx_mode()
{
    if (m_lossless) {
        m_tx_mode = Only4x4;
    } else {
        auto tx_mode = m_bit_stream->read_literal(2);
        if (tx_mode == Allow32x32) {
            tx_mode += m_bit_stream->read_literal(1);
        }
        m_tx_mode = static_cast<TXMode>(tx_mode);
    }
    return true;
}

bool VP9Decoder::tx_mode_probs()
{
    auto& tx_probs = m_probability_tables->tx_probs();
    for (auto i = 0; i < TX_SIZE_CONTEXTS; i++) {
        for (auto j = 0; j < TX_SIZES - 3; j++) {
            tx_probs[TX8x8][i][j] = diff_update_prob(tx_probs[TX8x8][i][j]);
        }
    }
    for (auto i = 0; i < TX_SIZE_CONTEXTS; i++) {
        for (auto j = 0; j < TX_SIZES - 2; j++) {
            tx_probs[TX16x16][i][j] = diff_update_prob(tx_probs[TX16x16][i][j]);
        }
    }
    for (auto i = 0; i < TX_SIZE_CONTEXTS; i++) {
        for (auto j = 0; j < TX_SIZES - 1; j++) {
            tx_probs[TX32x32][i][j] = diff_update_prob(tx_probs[TX32x32][i][j]);
        }
    }
    return true;
}

u8 VP9Decoder::diff_update_prob(u8 prob)
{
    if (m_bit_stream->read_bool(252)) {
        auto delta_prob = decode_term_subexp();
        prob = inv_remap_prob(delta_prob, prob);
    }
    return prob;
}

u8 VP9Decoder::decode_term_subexp()
{
    if (m_bit_stream->read_literal(1) == 0)
        return m_bit_stream->read_literal(4);
    if (m_bit_stream->read_literal(1) == 0)
        return m_bit_stream->read_literal(4) + 16;
    if (m_bit_stream->read_literal(1) == 0)
        return m_bit_stream->read_literal(4) + 32;

    auto v = m_bit_stream->read_literal(7);
    if (v < 65)
        return v + 64;
    return (v << 1u) - 1 + m_bit_stream->read_literal(1);
}

u8 VP9Decoder::inv_remap_prob(u8 delta_prob, u8 prob)
{
    u8 m = prob - 1;
    auto v = inv_map_table[delta_prob];
    if ((m << 1u) <= 255) {
        return 1 + inv_recenter_nonneg(v, m);
    }
    return 255 - inv_recenter_nonneg(v, 254 - m);
}

u8 VP9Decoder::inv_recenter_nonneg(u8 v, u8 m)
{
    if (v > 2 * m)
        return v;
    if (v & 1u)
        return m - ((v + 1u) >> 1u);
    return m + (v >> 1u);
}

bool VP9Decoder::read_coef_probs()
{
    auto max_tx_size = tx_mode_to_biggest_tx_size[m_tx_mode];
    for (auto tx_size = TX4x4; tx_size <= max_tx_size; tx_size = static_cast<TXSize>(static_cast<int>(tx_size) + 1)) {
        auto update_probs = m_bit_stream->read_literal(1);
        if (update_probs == 1) {
            for (auto i = 0; i < 2; i++) {
                for (auto j = 0; j < 2; j++) {
                    for (auto k = 0; k < 6; k++) {
                        auto maxL = (k == 0) ? 3 : 6;
                        for (auto l = 0; l < maxL; l++) {
                            for (auto m = 0; m < 3; m++) {
                                auto& coef_probs = m_probability_tables->coef_probs()[tx_size];
                                coef_probs[i][j][k][l][m] = diff_update_prob(coef_probs[i][j][k][l][m]);
                            }
                        }
                    }
                }
            }
        }
    }
    return true;
}

bool VP9Decoder::read_skip_prob()
{
    for (auto i = 0; i < SKIP_CONTEXTS; i++) {
        m_probability_tables->skip_prob()[i] = diff_update_prob(m_probability_tables->skip_prob()[i]);
    }
    return true;
}

bool VP9Decoder::read_inter_mode_probs()
{
    for (auto i = 0; i < INTER_MODE_CONTEXTS; i++) {
        for (auto j = 0; j < INTER_MODES - 1; j++) {
            m_probability_tables->inter_mode_probs()[i][j] = diff_update_prob(m_probability_tables->inter_mode_probs()[i][j]);
        }
    }
    return true;
}

bool VP9Decoder::read_interp_filter_probs()
{
    for (auto i = 0; i < INTERP_FILTER_CONTEXTS; i++) {
        for (auto j = 0; j < SWITCHABLE_FILTERS - 1; j++) {
            m_probability_tables->interp_filter_probs()[i][j] = diff_update_prob(m_probability_tables->interp_filter_probs()[i][j]);
        }
    }
    return true;
}

bool VP9Decoder::read_is_inter_probs()
{
    for (auto i = 0; i < IS_INTER_CONTEXTS; i++) {
        m_probability_tables->is_inter_prob()[i] = diff_update_prob(m_probability_tables->is_inter_prob()[i]);
    }
    return true;
}

bool VP9Decoder::frame_reference_mode()
{
    auto compound_reference_allowed = false;
    for (auto i = 0; i < REFS_PER_FRAME; i++) {
        if (m_ref_frame_sign_bias[i + 1] != m_ref_frame_sign_bias[1])
            compound_reference_allowed = true;
    }
    if (compound_reference_allowed) {
        auto non_single_reference = m_bit_stream->read_literal(1);
        if (non_single_reference == 0) {
            m_reference_mode = SingleReference;
        } else {
            auto reference_select = m_bit_stream->read_literal(1);
            if (reference_select == 0)
                m_reference_mode = CompoundReference;
            else
                m_reference_mode = ReferenceModeSelect;
            setup_compound_reference_mode();
        }
    } else {
        m_reference_mode = SingleReference;
    }
    return true;
}

bool VP9Decoder::frame_reference_mode_probs()
{
    if (m_reference_mode == ReferenceModeSelect) {
        for (auto i = 0; i < COMP_MODE_CONTEXTS; i++) {
            auto& comp_mode_prob = m_probability_tables->comp_mode_prob();
            comp_mode_prob[i] = diff_update_prob(comp_mode_prob[i]);
        }
    }
    if (m_reference_mode != CompoundReference) {
        for (auto i = 0; i < REF_CONTEXTS; i++) {
            auto& single_ref_prob = m_probability_tables->single_ref_prob();
            single_ref_prob[i][0] = diff_update_prob(single_ref_prob[i][0]);
            single_ref_prob[i][1] = diff_update_prob(single_ref_prob[i][1]);
        }
    }
    if (m_reference_mode != SingleReference) {
        for (auto i = 0; i < REF_CONTEXTS; i++) {
            auto& comp_ref_prob = m_probability_tables->comp_ref_prob();
            comp_ref_prob[i] = diff_update_prob(comp_ref_prob[i]);
        }
    }
    return true;
}

bool VP9Decoder::read_y_mode_probs()
{
    for (auto i = 0; i < BLOCK_SIZE_GROUPS; i++) {
        for (auto j = 0; j < INTRA_MODES - 1; j++) {
            auto& y_mode_probs = m_probability_tables->y_mode_probs();
            y_mode_probs[i][j] = diff_update_prob(y_mode_probs[i][j]);
        }
    }
    return true;
}

bool VP9Decoder::read_partition_probs()
{
    for (auto i = 0; i < PARTITION_CONTEXTS; i++) {
        for (auto j = 0; j < PARTITION_TYPES - 1; j++) {
            auto& partition_probs = m_probability_tables->partition_probs();
            partition_probs[i][j] = diff_update_prob(partition_probs[i][j]);
        }
    }
    return true;
}

bool VP9Decoder::mv_probs()
{
    for (auto j = 0; j < MV_JOINTS - 1; j++) {
        auto& mv_joint_probs = m_probability_tables->mv_joint_probs();
        mv_joint_probs[j] = update_mv_prob(mv_joint_probs[j]);
    }

    for (auto i = 0; i < 2; i++) {
        auto& mv_sign_prob = m_probability_tables->mv_sign_prob();
        mv_sign_prob[i] = update_mv_prob(mv_sign_prob[i]);
        for (auto j = 0; j < MV_CLASSES - 1; j++) {
            auto& mv_class_probs = m_probability_tables->mv_class_probs();
            mv_class_probs[i][j] = update_mv_prob(mv_class_probs[i][j]);
        }
        auto& mv_class0_bit_prob = m_probability_tables->mv_class0_bit_prob();
        mv_class0_bit_prob[i] = update_mv_prob(mv_class0_bit_prob[i]);
        for (auto j = 0; j < MV_OFFSET_BITS; j++) {
            auto& mv_bits_prob = m_probability_tables->mv_bits_prob();
            mv_bits_prob[i][j] = update_mv_prob(mv_bits_prob[i][j]);
        }
    }

    for (auto i = 0; i < 2; i++) {
        for (auto j = 0; j < CLASS0_SIZE; j++) {
            for (auto k = 0; k < MV_FR_SIZE - 1; k++) {
                auto& mv_class0_fr_probs = m_probability_tables->mv_class0_fr_probs();
                mv_class0_fr_probs[i][j][k] = update_mv_prob(mv_class0_fr_probs[i][j][k]);
            }
        }
        for (auto k = 0; k < MV_FR_SIZE - 1; k++) {
            auto& mv_fr_probs = m_probability_tables->mv_fr_probs();
            mv_fr_probs[i][k] = update_mv_prob(mv_fr_probs[i][k]);
        }
    }

    if (m_allow_high_precision_mv) {
        for (auto i = 0; i < 2; i++) {
            auto& mv_class0_hp_prob = m_probability_tables->mv_class0_hp_prob();
            auto& mv_hp_prob = m_probability_tables->mv_hp_prob();
            mv_class0_hp_prob[i] = update_mv_prob(mv_class0_hp_prob[i]);
            mv_hp_prob[i] = update_mv_prob(mv_hp_prob[i]);
        }
    }

    return true;
}

u8 VP9Decoder::update_mv_prob(u8 prob)
{
    if (m_bit_stream->read_bool(252)) {
        return (m_bit_stream->read_literal(7) << 1u) | 1u;
    }
    return prob;
}

bool VP9Decoder::setup_compound_reference_mode()
{
    if (m_ref_frame_sign_bias[LAST_FRAME] == m_ref_frame_sign_bias[GOLDEN_FRAME]) {
        m_comp_fixed_ref = ALTREF_FRAME;
        m_comp_var_ref[0] = LAST_FRAME;
        m_comp_var_ref[1] = GOLDEN_FRAME;
    } else if (m_ref_frame_sign_bias[LAST_FRAME] == m_ref_frame_sign_bias[ALTREF_FRAME]) {
        m_comp_fixed_ref = GOLDEN_FRAME;
        m_comp_var_ref[0] = LAST_FRAME;
        m_comp_var_ref[1] = ALTREF_FRAME;
    } else {
        m_comp_fixed_ref = LAST_FRAME;
        m_comp_var_ref[0] = GOLDEN_FRAME;
        m_comp_var_ref[1] = ALTREF_FRAME;
    }
    return true;
}

bool VP9Decoder::decode_tiles()
{
    auto tile_cols = 1 << m_tile_cols_log2;
    auto tile_rows = 1 << m_tile_rows_log2;
    if (!clear_above_context())
        return false;
    for (auto tile_row = 0; tile_row < tile_rows; tile_row++) {
        for (auto tile_col = 0; tile_col < tile_cols; tile_col++) {
            auto last_tile = (tile_row == tile_rows - 1) && (tile_col == tile_cols - 1);
            // FIXME: Spec has `sz -= tile_size + 4`, but I think we don't need this because our bit stream manages how much data we have left?
            auto tile_size = last_tile ? m_bit_stream->bytes_remaining() : m_bit_stream->read_f(32);
            m_mi_row_start = get_tile_offset(tile_row, m_mi_rows, m_tile_rows_log2);
            m_mi_row_end = get_tile_offset(tile_row + 1, m_mi_rows, m_tile_rows_log2);
            m_mi_col_start = get_tile_offset(tile_col, m_mi_cols, m_tile_cols_log2);
            m_mi_col_end = get_tile_offset(tile_col + 1, m_mi_cols, m_tile_cols_log2);
            m_bit_stream->init_bool(tile_size);
            decode_tile();
            m_bit_stream->exit_bool();
        }
    }

    return true;
}

bool VP9Decoder::clear_above_context()
{
    // FIXME
    // When this function is invoked the arrays AboveNonzeroContext, AbovePartitionContext, AboveSegPredContext should be set equal to 0.
    // AboveNonzeroContext[0..2][0..MiCols*2-1] = 0
    // AboveSegPredContext[0..MiCols-1] = 0
    // AbovePartitionContext[0..Sb64Cols*8-1] = 0
    return true;
}

u32 VP9Decoder::get_tile_offset(u32 tile_num, u32 mis, u32 tile_size_log2)
{
    u32 super_blocks = (mis + 7) >> 3u;
    u32 offset = ((tile_num * super_blocks) >> tile_size_log2) << 3;
    return min(offset, mis);
}

bool VP9Decoder::decode_tile()
{
    for (auto row = m_mi_row_start; row < m_mi_row_end; row += 8) {
        if (!clear_left_context())
            return false;
        for (auto col = m_mi_col_start; col < m_mi_col_end; col += 8) {
            if (!decode_partition(row, col, Block_64x64))
                return false;
        }
    }
    return true;
}

bool VP9Decoder::clear_left_context()
{
    // FIXME
    // When this function is invoked the arrays LeftNonzeroContext, LeftPartitionContext, LeftSegPredContext should be set equal to 0.
    // LeftNonzeroContext[0..2][0..MiRows*2-1] = 0
    // LeftSegPredContext[0..MiRows-1] = 0
    // LeftPartitionContext[0..Sb64Rows*8-1] = 0
    return true;
}

bool VP9Decoder::decode_partition(u32 row, u32 col, u8 block_subsize)
{
    if (row >= m_mi_rows || col >= m_mi_cols)
        return false;
    auto num_8x8 = num_8x8_blocks_wide_lookup[block_subsize];
    auto half_block_8x8 = num_8x8 >> 1;
    auto has_rows = (row + half_block_8x8) < m_mi_rows;
    auto has_cols = (col + half_block_8x8) < m_mi_cols;

    // FIXME: Parse partition (type: T) as specified by spec in section 9.3
    (void)has_rows;
    (void)has_cols;

    return true;
}

void VP9Decoder::dump_info()
{
    dbgln("Frame dimensions: {}x{}", m_frame_width, m_frame_height);
    dbgln("Render dimensions: {}x{}", m_render_width, m_render_height);
    dbgln("Bit depth: {}", m_bit_depth);
    dbgln("Interpolation filter: {}", (u8)m_interpolation_filter);
}

}
