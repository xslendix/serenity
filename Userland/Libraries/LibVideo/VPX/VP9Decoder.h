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

#pragma once

#include "ProbabilityTables.h"
#include "SyntaxElementCounter.h"
#include <AK/ByteBuffer.h>
#include <AK/Optional.h>
#include <AK/OwnPtr.h>
#include <AK/Types.h>

namespace Video {

class VP9Decoder {
public:
    VP9Decoder();
    bool parse_frame(const ByteBuffer&);
    void dump_info();

private:
    class BitStream {
    public:
        BitStream(const u8* data, size_t size)
            : m_data_ptr(data)
            , m_bytes_remaining(size)
        {
        }

        u8 read_byte()
        {
            ASSERT(m_bytes_remaining >= 1);
            m_bytes_remaining--;
            return *(m_data_ptr++);
        }

        bool read_bit()
        {
            if (!m_current_byte.has_value()) {
                m_current_byte = read_byte();
                m_current_bit_position = 7;
            }

            bool bit_value = m_current_byte.value() & (1u << m_current_bit_position);
            if (--m_current_bit_position < 0)
                m_current_byte.clear();
            return bit_value;
        }

        u8 read_f(size_t n)
        {
            u8 result = 0;
            for (size_t i = 0; i < n; i++) {
                result = (2 * result) + read_bit();
            }
            return result;
        }

        i8 read_s(size_t n)
        {
            auto value = read_f(n);
            auto sign = read_bit();
            return sign ? -value : value;
        }

        u8 read_f8()
        {
            if (!m_current_byte.has_value())
                return read_byte();

            auto high_bits = m_current_byte.value() & ((1u << m_current_bit_position) - 1);
            u8 remaining_bits = 7 - m_current_bit_position;
            m_current_byte = read_byte();
            m_current_bit_position = 7;
            auto low_bits = (m_current_byte.value() >> (8u - remaining_bits)) & ((1u << remaining_bits) - 1);
            m_current_bit_position -= remaining_bits;
            return (high_bits << remaining_bits) | low_bits;
        }

        u16 read_f16()
        {
            return (read_f8() << 8u) | read_f8();
        }

        u64 get_position() const
        {
            return (m_bytes_read * 8) + (7 - m_current_bit_position);
        }

        bool init_bool(size_t bytes)
        {
            if (bytes < 1)
                return false;
            m_bool_value = read_f8();
            m_bool_range = 255;
            m_bool_max_bits = (8 * bytes) - 8;
            return !read_bool(128);
        }

        bool read_bool(u8 probability)
        {
            auto split = 1u + (((m_bool_range - 1u) * probability) >> 8u);
            bool return_bool;

            if (m_bool_value < split) {
                m_bool_range = split;
                return_bool = false;
            } else {
                m_bool_value -= split;
                return_bool = true;
            }

            while (m_bool_range < 128) {
                bool new_bit;
                if (m_bool_max_bits) {
                    new_bit = read_bit();
                    m_bool_max_bits--;
                } else {
                    new_bit = false;
                }
                m_bool_range *= 2;
                m_bool_value = (m_bool_value << 1u) + new_bit;
            }

            return return_bool;
        }

        bool exit_bool()
        {
            // FIXME: I'm not sure if this call to min is spec compliant, or if there is an issue elsewhere earlier in the parser.
            auto padding_element = read_f(min(m_bool_max_bits, (u64)bits_remaining()));

            // FIXME: It is a requirement of bitstream conformance that enough padding bits are inserted to ensure that the final coded byte of a frame is not equal to a superframe marker.
            //  A byte b is equal to a superframe marker if and only if (b & 0xe0)is equal to 0xc0, i.e. if the most significant 3 bits are equal to 0b110.
            return padding_element == 0;
        }

        u8 read_literal(size_t n)
        {
            u8 return_value = 0;
            for (size_t i = 0; i < n; i++) {
                return_value = (2 * return_value) + read_bool(128);
            }
            return return_value;
        }

        size_t bytes_remaining() { return m_bytes_remaining; }

        size_t bits_remaining() { return (bytes_remaining() * 8) + m_current_bit_position + 1; }

    private:
        const u8* m_data_ptr { nullptr };
        size_t m_bytes_remaining { 0 };
        Optional<u8> m_current_byte;
        i8 m_current_bit_position { 0 };
        u64 m_bytes_read { 0 };

        u8 m_bool_value { 0 };
        u8 m_bool_range { 0 };
        u64 m_bool_max_bits { 0 };
    };

    enum FrameType {
        KEY_FRAME,
        NON_KEY_FRAME
    };

    enum ColorSpace : u8 {
        CS_UNKNOWN = 0,
        CS_BT_601 = 1,
        CS_BT_709 = 2,
        CS_SMPTE_170 = 3,
        CS_SMPTE_240 = 4,
        CS_BT_2020 = 5,
        CS_RESERVED = 6,
        CS_RGB = 7
    };

    enum ColorRange {
        STUDIO_SWING,
        FULL_SWING
    };

    enum InterpolationFilter {
        EIGHTTAP = 0,
        EIGHTTAP_SMOOTH = 1,
        EIGHTTAP_SHARP = 2,
        BILINEAR = 3,
        SWITCHABLE = 4
    };

    enum ReferenceFrame {
        INTRA_FRAME = 0,
        LAST_FRAME = 1,
        GOLDEN_FRAME = 2,
        ALTREF_FRAME = 3,
        MAX_REF_FRAMES = 4
    };

    enum TXMode {
        Only4x4 = 0,
        Allow8x8 = 1,
        Allow16x16 = 2,
        Allow32x32 = 3,
        TXModeSelect = 4,
    };

    enum TXSize {
        TX4x4 = 0,
        TX8x8 = 1,
        TX16x16 = 2,
        TX32x32 = 3,
    };

    enum ReferenceMode {
        SingleReference = 0,
        CompoundReference = 1,
        ReferenceModeSelect = 2,
    };

    static constexpr InterpolationFilter literal_to_type[4] = { EIGHTTAP_SMOOTH, EIGHTTAP, EIGHTTAP_SHARP, BILINEAR };
    static constexpr TXSize tx_mode_to_biggest_tx_size[TX_MODES] = { TX4x4, TX8x8, TX16x16, TX32x32, TX32x32 };

    FrameType read_frame_type()
    {
        if (m_bit_stream->read_bit())
            return NON_KEY_FRAME;
        return KEY_FRAME;
    }

    ColorRange read_color_range()
    {
        if (m_bit_stream->read_bit())
            return FULL_SWING;
        return STUDIO_SWING;
    }

    bool uncompressed_header();
    bool frame_sync_code();
    bool color_config();
    bool frame_size();
    bool render_size();
    bool frame_size_with_refs();
    bool compute_image_size();
    bool read_interpolation_filter();
    bool loop_filter_params();
    bool quantization_params();
    i8 read_delta_q();
    bool segmentation_params();
    u8 read_prob();
    bool tile_info();
    u16 calc_min_log2_tile_cols();
    u16 calc_max_log2_tile_cols();
    bool setup_past_independence();
    bool trailing_bits();

    bool compressed_header();
    bool read_tx_mode();
    bool tx_mode_probs();
    u8 diff_update_prob(u8 prob);
    u8 decode_term_subexp();
    u8 inv_remap_prob(u8 delta_prob, u8 prob);
    u8 inv_recenter_nonneg(u8 v, u8 m);
    bool read_coef_probs();
    bool read_skip_prob();
    bool read_inter_mode_probs();
    bool read_interp_filter_probs();
    bool read_is_inter_probs();
    bool frame_reference_mode();
    bool frame_reference_mode_probs();
    bool read_y_mode_probs();
    bool read_partition_probs();
    bool mv_probs();
    u8 update_mv_prob(u8 prob);
    bool setup_compound_reference_mode();

    u64 m_start_bit_pos { 0 };
    u8 m_profile { 0 };
    u8 m_frame_to_show_map_index { 0 };
    u16 m_header_size_in_bytes { 0 };
    u8 m_refresh_frame_flags { 0 };
    u8 m_loop_filter_level { 0 };
    u8 m_loop_filter_sharpness { 0 };
    bool m_loop_filter_delta_enabled { false };
    FrameType m_frame_type;
    FrameType m_last_frame_type;
    bool m_show_frame { false };
    bool m_error_resilient_mode { false };
    bool m_frame_is_intra { false };
    u8 m_reset_frame_context { 0 };
    bool m_allow_high_precision_mv { false };
    u8 m_ref_frame_idx[3];
    u8 m_ref_frame_sign_bias[LAST_FRAME + 3];
    bool m_refresh_frame_context { false };
    bool m_frame_parallel_decoding_mode { false };
    u8 m_frame_context_idx { 0 };
    u8 m_bit_depth { 0 };
    ColorSpace m_color_space;
    ColorRange m_color_range;
    bool m_subsampling_x { false };
    bool m_subsampling_y { false };
    u16 m_frame_width { 0 };
    u16 m_frame_height { 0 };
    u16 m_render_width { 0 };
    u16 m_render_height { 0 };
    bool m_render_and_frame_size_different { false };
    u16 m_mi_cols { 0 };
    u16 m_mi_rows { 0 };
    u16 m_sb64_cols { 0 };
    u16 m_sb64_rows { 0 };
    InterpolationFilter m_interpolation_filter;
    bool m_lossless { false };
    u8 m_segmentation_tree_probs[7];
    u8 m_segmentation_pred_prob[3];
    bool m_feature_enabled[8][4];
    u8 m_feature_data[8][4];
    bool m_segmentation_abs_or_delta_update { false };
    u16 m_tile_cols_log2 { 0 };
    u16 m_tile_rows_log2 { 0 };
    i8 m_loop_filter_ref_deltas[MAX_REF_FRAMES];
    i8 m_loop_filter_mode_deltas[2];

    TXMode m_tx_mode;
    ReferenceMode m_reference_mode;
    ReferenceFrame m_comp_fixed_ref;
    ReferenceFrame m_comp_var_ref[2];

    OwnPtr<BitStream> m_bit_stream;
    OwnPtr<ProbabilityTables> m_probability_tables;
    OwnPtr<SyntaxElementCounter> m_syntax_element_counter;
};

}
