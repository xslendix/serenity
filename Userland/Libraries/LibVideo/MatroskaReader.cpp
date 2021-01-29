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

#include "MatroskaReader.h"
#include <AK/Function.h>
#include <AK/MappedFile.h>
#include <AK/Optional.h>
#include <AK/Utf8View.h>

namespace Video {

#define CHECK_HAS_VALUE(x) \
    if (!x.has_value())    \
    return false

OwnPtr<MatroskaDocument> MatroskaReader::parse_matroska_from_file(const StringView& path)
{
    auto mapped_file_result = MappedFile::map(path);
    if (mapped_file_result.is_error())
        return {};

    auto mapped_file = mapped_file_result.release_value();
    return parse_matroska_from_data((u8*)mapped_file->data(), mapped_file->size());
}

OwnPtr<MatroskaDocument> MatroskaReader::parse_matroska_from_data(const u8* data, size_t size)
{
    MatroskaReader reader(data, size);
    return reader.parse();
}

OwnPtr<MatroskaDocument> MatroskaReader::parse()
{
    auto first_element_id = m_streamer.read_variable_size_integer(false);
    dbgln<MATROSKA_TRACE>("First element ID is %#010x\n", first_element_id.value());
    if (!first_element_id.has_value() || first_element_id.value() != 0x1A45DFA3)
        return {};

    auto header = parse_ebml_header();
    if (!header.has_value())
        return {};

    dbgln<MATROSKA_DEBUG>("Parsed EBML header");

    auto root_element_id = m_streamer.read_variable_size_integer(false);
    if (!root_element_id.has_value() || root_element_id.value() != 0x18538067)
        return {};

    auto matroska_document = make<MatroskaDocument>(header.value());

    auto segment_parse_success = parse_segment_elements(*matroska_document);
    if (!segment_parse_success)
        return {};

    return matroska_document;
}

bool MatroskaReader::parse_master_element(const StringView& element_name, Function<bool(u64)> element_consumer)
{
#ifndef MATROSKA_DEBUG
    (void)element_name;
#endif

    auto element_data_size = m_streamer.read_variable_size_integer();
    CHECK_HAS_VALUE(element_data_size);
    dbgln<>("{} has {} octets of data.", element_name, element_data_size.value());

    m_streamer.push_octets_read();
    while (m_streamer.octets_read() < element_data_size.value()) {
        dbgln<MATROSKA_TRACE>("====== Reading  element ======");
        auto optional_element_id = m_streamer.read_variable_size_integer(false);
        CHECK_HAS_VALUE(optional_element_id);

        auto element_id = optional_element_id.value();
        dbgln<MATROSKA_TRACE>("%s element ID is %#010x\n", element_name, element_id);

        if (!element_consumer(element_id)) {
            dbgln<MATROSKA_DEBUG>("%s consumer failed on ID %#010x\n", element_name.to_string().characters(), element_id);
            return false;
        }

        dbgln<MATROSKA_TRACE>("Read {} octets of the {} so far.", m_streamer.octets_read(), element_name);
    }
    m_streamer.pop_octets_read();

    return true;
}

Optional<EBMLHeader> MatroskaReader::parse_ebml_header()
{
    EBMLHeader header;
    auto success = parse_master_element("Header", [&](u64 element_id) {
        if (element_id == 0x4282) {
            auto doc_type = read_string_element();
            CHECK_HAS_VALUE(doc_type);
            header.doc_type = doc_type.value();
            dbgln<MATROSKA_DEBUG>("Read DocType attribute: {}", doc_type.value());
        } else if (element_id == 0x4287) {
            auto doc_type_version = read_u64_element();
            CHECK_HAS_VALUE(doc_type_version);
            header.doc_type_version = doc_type_version.value();
            dbgln<MATROSKA_DEBUG>("Read DocTypeVersion attribute: {}", doc_type_version.value());
        } else {
            return read_unknown_element();
        }

        return true;
    });

    if (!success)
        return {};
    return header;
}

bool MatroskaReader::parse_segment_elements(MatroskaDocument& matroska_document)
{
    dbgln<MATROSKA_DEBUG>("Parsing segment elements");
    auto success = parse_master_element("Segment", [&](u64 element_id) {
        if (element_id == 0x1549A966) {
            auto segment_information = parse_information();
            if (!segment_information)
                return false;
            matroska_document.set_segment_information(move(segment_information));
        } else if (element_id == 0x1654AE6B) {
            return parse_tracks(matroska_document);
        } else if (element_id == 0x1F43B675) {
            auto cluster = parse_cluster();
            if (!cluster)
                return false;
            matroska_document.clusters().append(cluster.release_nonnull());
        } else {
            return read_unknown_element();
        }

        return true;
    });

    dbgln("Success {}", success);
    return success;
}

OwnPtr<SegmentInformation> MatroskaReader::parse_information()
{
    auto segment_information = make<SegmentInformation>();
    auto success = parse_master_element("Segment Information", [&](u64 element_id) {
        if (element_id == 0x2AD7B1) {
            auto timestamp_scale = read_u64_element();
            CHECK_HAS_VALUE(timestamp_scale);
            segment_information->set_timestamp_scale(timestamp_scale.value());
            dbgln<MATROSKA_DEBUG>("Read TimestampScale attribute: {}", timestamp_scale.value());
        } else if (element_id == 0x4D80) {
            auto muxing_app = read_string_element();
            CHECK_HAS_VALUE(muxing_app);
            segment_information->set_muxing_app(muxing_app.value());
            dbgln<MATROSKA_DEBUG>("Read MuxingApp attribute: {}", muxing_app.value());
        } else if (element_id == 0x5741) {
            auto writing_app = read_string_element();
            CHECK_HAS_VALUE(writing_app);
            segment_information->set_writing_app(writing_app.value());
            dbgln<MATROSKA_DEBUG>("Read WritingApp attribute: {}", writing_app.value());
        } else {
            return read_unknown_element();
        }

        return true;
    });

    if (!success)
        return {};
    return segment_information;
}

bool MatroskaReader::parse_tracks(MatroskaDocument& matroska_document)
{
    auto success = parse_master_element("Tracks", [&](u64 element_id) {
        if (element_id == 0xAE) {
            dbgln<MATROSKA_DEBUG>("Parsing track");
            auto track_entry = parse_track_entry();
            if (!track_entry)
                return false;
            auto track_number = track_entry->track_number();
            matroska_document.add_track(track_number, track_entry.release_nonnull());
            dbgln<MATROSKA_DEBUG>("Track {} added to document", track_number);
        } else {
            return read_unknown_element();
        }

        return true;
    });

    return success;
}

OwnPtr<TrackEntry> MatroskaReader::parse_track_entry()
{
    auto track_entry = make<TrackEntry>();
    auto success = parse_master_element("Track", [&](u64 element_id) {
        if (element_id == 0xD7) {
            auto track_number = read_u64_element();
            CHECK_HAS_VALUE(track_number);
            track_entry->set_track_number(track_number.value());
            dbgln<MATROSKA_TRACE>("Read TrackNumber attribute: {}", track_number.value());
        } else if (element_id == 0x73C5) {
            auto track_uid = read_u64_element();
            CHECK_HAS_VALUE(track_uid);
            track_entry->set_track_uid(track_uid.value());
            dbgln<MATROSKA_TRACE>("Read TrackUID attribute: {}", track_uid.value());
        } else if (element_id == 0x83) {
            auto track_type = read_u64_element();
            CHECK_HAS_VALUE(track_type);
            track_entry->set_track_type(static_cast<TrackEntry::TrackType>(track_type.value()));
            dbgln<MATROSKA_TRACE>("Read TrackType attribute: {}", track_type.value());
        } else if (element_id == 0x22B59C) {
            auto language = read_string_element();
            CHECK_HAS_VALUE(language);
            track_entry->set_language(language.value());
            dbgln<MATROSKA_TRACE>("Read Track's Language attribute: {}", language.value());
        } else if (element_id == 0x86) {
            auto codec_id = read_string_element();
            CHECK_HAS_VALUE(codec_id);
            track_entry->set_codec_id(codec_id.value());
            dbgln<MATROSKA_TRACE>("Read Track's CodecID attribute: {}", codec_id.value());
        } else if (element_id == 0xE0) {
            auto video_track = parse_video_track_information();
            CHECK_HAS_VALUE(video_track);
            track_entry->set_video_track(video_track.value());
        } else if (element_id == 0xE1) {
            auto audio_track = parse_audio_track_information();
            CHECK_HAS_VALUE(audio_track);
            track_entry->set_audio_track(audio_track.value());
        } else {
            return read_unknown_element();
        }

        return true;
    });

    if (!success)
        return {};
    return track_entry;
}

Optional<TrackEntry::VideoTrack> MatroskaReader::parse_video_track_information()
{
    TrackEntry::VideoTrack video_track {};

    auto success = parse_master_element("VideoTrack", [&](u64 element_id) {
        if (element_id == 0xB0) {
            auto pixel_width = read_u64_element();
            CHECK_HAS_VALUE(pixel_width);
            video_track.pixel_width = pixel_width.value();
            dbgln<MATROSKA_TRACE>("Read VideoTrack's PixelWidth attribute: {}", pixel_width.value());
        } else if (element_id == 0xBA) {
            auto pixel_height = read_u64_element();
            CHECK_HAS_VALUE(pixel_height);
            video_track.pixel_height = pixel_height.value();
            dbgln<MATROSKA_TRACE>("Read VideoTrack's PixelHeight attribute: {}", pixel_height.value());
        } else {
            return read_unknown_element();
        }

        return true;
    });

    if (!success)
        return {};
    return video_track;
}

Optional<TrackEntry::AudioTrack> MatroskaReader::parse_audio_track_information()
{
    TrackEntry::AudioTrack audio_track {};

    auto success = parse_master_element("AudioTrack", [&](u64 element_id) {
        if (element_id == 0x9F) {
            auto channels = read_u64_element();
            CHECK_HAS_VALUE(channels);
            audio_track.channels = channels.value();
            dbgln<MATROSKA_TRACE>("Read AudioTrack's Channels attribute: {}", channels.value());
        } else if (element_id == 0x6264) {
            auto bit_depth = read_u64_element();
            CHECK_HAS_VALUE(bit_depth);
            audio_track.bit_depth = bit_depth.value();
            dbgln<MATROSKA_TRACE>("Read AudioTrack's BitDepth attribute: {}", bit_depth.value());
        } else {
            return read_unknown_element();
        }

        return true;
    });

    if (!success)
        return {};
    return audio_track;
}

OwnPtr<Cluster> MatroskaReader::parse_cluster()
{
    auto cluster = make<Cluster>();

    auto success = parse_master_element("Cluster", [&](u64 element_id) {
        if (element_id == 0xA3) {
            auto simple_block = parse_simple_block();
            if (!simple_block)
                return false;
            cluster->blocks().append(simple_block.release_nonnull());
        } else if (element_id == 0xE7) {
            auto timestamp = read_u64_element();
            if (!timestamp.has_value())
                return false;
            cluster->set_timestamp(timestamp.value());
        } else {
            auto success = read_unknown_element();
            if (!success)
                return false;
        }

        return true;
    });

    if (!success)
        return {};
    return cluster;
}

OwnPtr<Block> MatroskaReader::parse_simple_block()
{
    auto block = make<Block>();

    auto content_size = m_streamer.read_variable_size_integer();
    if (!content_size.has_value())
        return {};

    auto octets_read_before_track_number = m_streamer.octets_read();
    auto track_number = m_streamer.read_variable_size_integer();
    if (!track_number.has_value())
        return {};
    block->set_track_number(track_number.value());

    if (m_streamer.remaining() < 3)
        return {};
    block->set_timestamp(m_streamer.read_i16());

    auto flags = m_streamer.read_octet();
    block->set_only_keyframes(flags & (1u << 7u));
    block->set_invisible(flags & (1u << 3u));
    block->set_lacing(static_cast<Block::Lacing>((flags & 0b110u) >> 1u));
    block->set_discardable(flags & 1u);

    auto total_frame_content_size = content_size.value() - (m_streamer.octets_read() - octets_read_before_track_number);
    if (block->lacing() == Block::Lacing::EBML) {
        auto octets_read_before_frame_sizes = m_streamer.octets_read();
        auto frame_count = m_streamer.read_octet() + 1;
        Vector<u64> frame_sizes;
        frame_sizes.ensure_capacity(frame_count);

        u64 frame_size_sum = 0;
        u64 previous_frame_size;
        auto first_frame_size = m_streamer.read_variable_size_integer();
        if (!first_frame_size.has_value())
            return {};
        frame_sizes.append(first_frame_size.value());
        frame_size_sum += first_frame_size.value();
        previous_frame_size = first_frame_size.value();

        for (int i = 0; i < frame_count - 2; i++) {
            auto frame_size_difference = m_streamer.read_variable_sized_signed_integer();
            if (!frame_size_difference.has_value())
                return {};
            u64 frame_size;
            if (frame_size_difference.value() < 0)
                frame_size = previous_frame_size - (-frame_size_difference.value());
            else
                frame_size = previous_frame_size + frame_size_difference.value();
            frame_sizes.append(frame_size);
            frame_size_sum += frame_size;
            previous_frame_size = frame_size;
        }
        frame_sizes.append(total_frame_content_size - frame_size_sum - (m_streamer.octets_read() - octets_read_before_frame_sizes));

        for (int i = 0; i < frame_count; i++) {
            auto current_frame_size = frame_sizes.at(i);
            block->add_frame(ByteBuffer::copy(m_streamer.data(), current_frame_size));
            m_streamer.drop_octets(current_frame_size);
        }
    } else if (block->lacing() == Block::Lacing::FixedSize) {
        auto frame_count = m_streamer.read_octet() + 1;
        auto individual_frame_size = total_frame_content_size / frame_count;
        for (int i = 0; i < frame_count; i++) {
            block->add_frame(ByteBuffer::copy(m_streamer.data(), individual_frame_size));
            m_streamer.drop_octets(individual_frame_size);
        }
    } else {
        block->add_frame(ByteBuffer::copy(m_streamer.data(), total_frame_content_size));
        m_streamer.drop_octets(total_frame_content_size);
    }
    return block;
}

Optional<String> MatroskaReader::read_string_element()
{
    auto string_length = m_streamer.read_variable_size_integer();
    if (!string_length.has_value() || m_streamer.remaining() < string_length.value())
        return {};
    auto string_value = String(m_streamer.data_as_chars(), string_length.value());
    m_streamer.drop_octets(string_length.value());
    return string_value;
}

Optional<u64> MatroskaReader::read_u64_element()
{
    auto integer_length = m_streamer.read_variable_size_integer();
    if (!integer_length.has_value() || m_streamer.remaining() < integer_length.value())
        return {};
    u64 result = 0;
    for (size_t i = 0; i < integer_length.value(); i++) {
        if (!m_streamer.has_octet())
            return {};
        result = (result << 8u) + m_streamer.read_octet();
    }
    return result;
}

bool MatroskaReader::read_unknown_element()
{
    auto element_length = m_streamer.read_variable_size_integer();
    if (!element_length.has_value() || m_streamer.remaining() < element_length.value())
        return false;

    m_streamer.drop_octets(element_length.value());
    return true;
}

}
