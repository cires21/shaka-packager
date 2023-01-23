// Copyright 2020 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include "packager/media/formats/mp2t/es_parser_teletext.h"

#include <math.h>

#include "packager/media/base/bit_reader.h"
#include "packager/media/base/text_stream_info.h"
#include "packager/media/base/timestamp.h"
#include "packager/media/formats/mp2t/mp2t_common.h"

namespace shaka {
namespace media {
namespace mp2t {

namespace {

const uint8_t EBU_TELETEXT_WITH_SUBTITLING = 0x03;
const uint8_t MAGAZINE = 8;
const uint8_t PAGE = 88;
const std::string LANG = "cat";

const uint8_t BITREVERSE_8[] = {
    0x00, 0x80, 0x40, 0xC0, 0x20, 0xA0, 0x60, 0xE0, 0x10, 0x90, 0x50, 0xD0,
    0x30, 0xB0, 0x70, 0xF0, 0x08, 0x88, 0x48, 0xC8, 0x28, 0xA8, 0x68, 0xE8,
    0x18, 0x98, 0x58, 0xD8, 0x38, 0xB8, 0x78, 0xF8, 0x04, 0x84, 0x44, 0xC4,
    0x24, 0xA4, 0x64, 0xE4, 0x14, 0x94, 0x54, 0xD4, 0x34, 0xB4, 0x74, 0xF4,
    0x0C, 0x8C, 0x4C, 0xCC, 0x2C, 0xAC, 0x6C, 0xEC, 0x1C, 0x9C, 0x5C, 0xDC,
    0x3C, 0xBC, 0x7C, 0xFC, 0x02, 0x82, 0x42, 0xC2, 0x22, 0xA2, 0x62, 0xE2,
    0x12, 0x92, 0x52, 0xD2, 0x32, 0xB2, 0x72, 0xF2, 0x0A, 0x8A, 0x4A, 0xCA,
    0x2A, 0xAA, 0x6A, 0xEA, 0x1A, 0x9A, 0x5A, 0xDA, 0x3A, 0xBA, 0x7A, 0xFA,
    0x06, 0x86, 0x46, 0xC6, 0x26, 0xA6, 0x66, 0xE6, 0x16, 0x96, 0x56, 0xD6,
    0x36, 0xB6, 0x76, 0xF6, 0x0E, 0x8E, 0x4E, 0xCE, 0x2E, 0xAE, 0x6E, 0xEE,
    0x1E, 0x9E, 0x5E, 0xDE, 0x3E, 0xBE, 0x7E, 0xFE, 0x01, 0x81, 0x41, 0xC1,
    0x21, 0xA1, 0x61, 0xE1, 0x11, 0x91, 0x51, 0xD1, 0x31, 0xB1, 0x71, 0xF1,
    0x09, 0x89, 0x49, 0xC9, 0x29, 0xA9, 0x69, 0xE9, 0x19, 0x99, 0x59, 0xD9,
    0x39, 0xB9, 0x79, 0xF9, 0x05, 0x85, 0x45, 0xC5, 0x25, 0xA5, 0x65, 0xE5,
    0x15, 0x95, 0x55, 0xD5, 0x35, 0xB5, 0x75, 0xF5, 0x0D, 0x8D, 0x4D, 0xCD,
    0x2D, 0xAD, 0x6D, 0xED, 0x1D, 0x9D, 0x5D, 0xDD, 0x3D, 0xBD, 0x7D, 0xFD,
    0x03, 0x83, 0x43, 0xC3, 0x23, 0xA3, 0x63, 0xE3, 0x13, 0x93, 0x53, 0xD3,
    0x33, 0xB3, 0x73, 0xF3, 0x0B, 0x8B, 0x4B, 0xCB, 0x2B, 0xAB, 0x6B, 0xEB,
    0x1B, 0x9B, 0x5B, 0xDB, 0x3B, 0xBB, 0x7B, 0xFB, 0x07, 0x87, 0x47, 0xC7,
    0x27, 0xA7, 0x67, 0xE7, 0x17, 0x97, 0x57, 0xD7, 0x37, 0xB7, 0x77, 0xF7,
    0x0F, 0x8F, 0x4F, 0xCF, 0x2F, 0xAF, 0x6F, 0xEF, 0x1F, 0x9F, 0x5F, 0xDF,
    0x3F, 0xBF, 0x7F, 0xFF};

struct DisplayText {
  int64_t pts_;
  int64_t time_offset_ms_;
  int64_t timeout_s;
  std::string lang_;
  std::string text_;
};

template <typename T>
constexpr T bit(T value, const size_t bit_pos) {
  return (value >> bit_pos) & 0x1;
}

uint8_t hamming_8_4(const uint8_t value) {
  uint8_t result = 0;

  for (uint8_t i = 0; i < 8; i += 2) {
    const uint8_t pos = 6 - i;
    const uint8_t b = bit(value, pos);
    result += static_cast<uint8_t>(pow(2, (i / 2))) * b;
  }

  return result;
}

uint8_t ReadHamming(BitReader& reader) {
  uint8_t bits;
  RCHECK(reader.ReadBits(8, &bits));
  return hamming_8_4(bits);
}

bool ParseDataBlock(const int64_t pts,
                    const uint8_t* data_block,
                    const uint16_t packet_nr,
                    uint8_t& current_page_number,
                    DisplayText& display_text) {
  BitReader reader(data_block, 40);

  size_t payload_len = 40;
  if (packet_nr == 0) {
    payload_len = 32;
    // ETS 300 706 9.3.1 Page header
    const uint8_t page_number_units = ReadHamming(reader);
    const uint8_t page_number_tens = ReadHamming(reader);
    const uint8_t page_number = 10 * page_number_tens + page_number_units;

    current_page_number = page_number;
    if (current_page_number != PAGE) {
      return false;
    }

    /*const uint8_t subcode_s1 = */ ReadHamming(reader);
    const uint8_t subcode_s2_c4 = ReadHamming(reader);
    /*const uint8_t subcode_s3 = */ ReadHamming(reader);
    const uint8_t subcode_s4_c5_c6 = ReadHamming(reader);
    const uint8_t subcode_c7_c10 = ReadHamming(reader);
    /*const uint8_t subcode_c11_c14 = */ ReadHamming(reader);

    /*const uint8_t c4_erase_page = */ bit(subcode_s2_c4, 0);
    /*const uint8_t c5_news_flash = */ bit(subcode_s4_c5_c6, 1);
    /*const uint8_t c6_subtitle = */ bit(subcode_s4_c5_c6, 0);
    /*const uint8_t c7_supress_header = */ bit(subcode_c7_c10, 4);
  } else if (packet_nr > 25) {
    return false;
  }

  if (current_page_number != PAGE) {
    return false;
  }

  const uint8_t* payload = reader.current_byte_ptr();
  RCHECK(reader.SkipBytes(payload_len));

  std::unique_ptr<char[]> chars(new char[payload_len + 1]);
  memset(chars.get(), 0, payload_len + 1);
  for (size_t i = 0; i < payload_len; ++i) {
    char nextChar = BITREVERSE_8[payload[i]] & 0x7f;
    if (nextChar < 32 || nextChar > 122) {
      chars[i] = 0x20;
    } else {
      chars[i] = nextChar;
    }
  }

  std::string text_utf8(chars.get());
  // text_utf8 = escape_chars(text_utf8);

  if (packet_nr == 0) {
    return false;
  }

  uint64_t offset_ms = 0;
  uint64_t timeout_s = 10;
  display_text = {pts, offset_ms, timeout_s, LANG, text_utf8};
  return true;
}

bool ParseSubtitlingDescriptor(
    const uint8_t* descriptor,
    size_t size,
    std::unordered_map<uint16_t, std::string>& result) {
  // See ETSI EN 300 468 Section 6.2.41.
  BitReader reader(descriptor, size);
  size_t data_size;
  RCHECK(reader.SkipBits(8));  // descriptor_tag
  RCHECK(reader.ReadBits(8, &data_size));
  RCHECK(data_size + 2 <= size);

  for (size_t i = 0; i < data_size; i += 8) {
    uint32_t lang_code;
    RCHECK(reader.ReadBits(24, &lang_code));
    uint8_t teletext_type;
    RCHECK(reader.ReadBits(5, &teletext_type));
    uint16_t magazine_number;
    RCHECK(reader.ReadBits(3, &magazine_number));
    if (magazine_number == 0) {
      magazine_number = 8;
    }

    uint16_t page_tenths;
    RCHECK(reader.ReadBits(4, &page_tenths));
    uint16_t page_digit;
    RCHECK(reader.ReadBits(4, &page_digit));
    const uint16_t page_number = page_tenths * 10 + page_digit;

    // The lang code is a ISO 639-2 code coded in Latin-1.
    std::string lang(3, '\0');
    lang[0] = (lang_code >> 16) & 0xff;
    lang[1] = (lang_code >> 8) & 0xff;
    lang[2] = (lang_code >> 0) & 0xff;

    const uint16_t index = magazine_number * 100 + page_number;
    LOG(INFO) << "teletext: descriptor lang " << lang << " magazine_number "
              << static_cast<uint32_t>(magazine_number) << " page_number "
              << static_cast<uint32_t>(page_number) << " teletext_type "
              << static_cast<uint32_t>(teletext_type);
    result.emplace(index, std::move(lang));
  }

  return true;
}

}  // namespace

EsParserTeletext::EsParserTeletext(uint32_t pid,
                                   const NewStreamInfoCB& new_stream_info_cb,
                                   const EmitTextSampleCB& emit_sample_cb,
                                   const uint8_t* descriptor,
                                   size_t descriptor_length)
    : EsParser(pid),
      new_stream_info_cb_(new_stream_info_cb),
      emit_sample_cb_(emit_sample_cb),
      current_page_number_(0) {
  if (!ParseSubtitlingDescriptor(descriptor, descriptor_length, languages_)) {
    LOG(ERROR) << "Unable to parse teletext_descriptor";
  }
}

EsParserTeletext::~EsParserTeletext() {}

bool EsParserTeletext::Parse(const uint8_t* buf,
                             int size,
                             int64_t pts,
                             int64_t dts) {
  if (!sent_info_) {
    sent_info_ = true;
    auto info = std::make_shared<TextStreamInfo>(pid(), kMpeg2Timescale,
                                                 kInfiniteDuration, kCodecText,
                                                 "", "", 0, 0, "");
    for (const auto& pair : languages_) {
      info->AddSubStream(pair.first, {pair.second});
    }

    new_stream_info_cb_.Run(info);
  }

  return ParseInternal(buf, size, pts);
}

bool EsParserTeletext::Flush() {
  return true;
}

void EsParserTeletext::Reset() {}

bool EsParserTeletext::ParseInternal(const uint8_t* data,
                                     size_t size,
                                     int64_t pts) {
  BitReader reader(data, size);

  uint8_t data_identifier;
  RCHECK(reader.ReadBits(8, &data_identifier));

  std::vector<DisplayText> text_pages;
  while (reader.bits_available()) {
    uint8_t data_unit_id;
    RCHECK(reader.ReadBits(8, &data_unit_id));

    uint8_t data_unit_length;
    RCHECK(reader.ReadBits(8, &data_unit_length));

    if (data_unit_length != 44) {
      LOG(INFO) << "Bad Teletext data length " << data_unit_length << " for "
                << data_unit_id;
      break;
    }

    if (data_unit_id != EBU_TELETEXT_WITH_SUBTITLING) {
      RCHECK(reader.SkipBytes(44));
      continue;
    }

    RCHECK(reader.SkipBits(2));
    uint8_t field_parity;
    RCHECK(reader.ReadBits(1, &field_parity));

    uint8_t line_offset;
    RCHECK(reader.ReadBits(5, &line_offset));

    uint8_t framing_code;
    RCHECK(reader.ReadBits(8, &framing_code));

    uint16_t address_bits;
    RCHECK(reader.ReadBits(16, &address_bits));

    uint16_t magazine = bit(address_bits, 14) + 2 * bit(address_bits, 12) +
                        4 * bit(address_bits, 10);

    if (magazine == 0) {
      magazine = 8;
    }

    const uint16_t packet_nr =
        (bit(address_bits, 8) + 2 * bit(address_bits, 6) +
         4 * bit(address_bits, 4) + 8 * bit(address_bits, 2) +
         16 * bit(address_bits, 0));
    const uint8_t* data_block = reader.current_byte_ptr();
    RCHECK(reader.SkipBytes(40));

    if (magazine != MAGAZINE) {
      continue;
    }

    DisplayText text_page;
    if (ParseDataBlock(pts, data_block, packet_nr, current_page_number_,
                       text_page)) {
      printf("[%ld] %s\n", pts, text_page.text_.c_str());
      text_pages.emplace_back(std::move(text_page));
    }
  }

  return true;
}

}  // namespace mp2t
}  // namespace media
}  // namespace shaka
