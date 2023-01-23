// Copyright 2020 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#ifndef PACKAGER_MEDIA_FORMATS_MP2T_ES_PARSER_TELETEXT_H_
#define PACKAGER_MEDIA_FORMATS_MP2T_ES_PARSER_TELETEXT_H_

#include <unordered_map>
#include <vector>

#include "packager/base/callback.h"
#include "packager/media/base/byte_queue.h"
#include "packager/media/formats/mp2t/es_parser.h"

namespace shaka {
namespace media {
namespace mp2t {

class EsParserTeletext : public EsParser {
 public:
  EsParserTeletext(uint32_t pid,
              const NewStreamInfoCB& new_stream_info_cb,
              const EmitTextSampleCB& emit_sample_cb,
              const uint8_t* descriptor,
              size_t descriptor_length);
  ~EsParserTeletext() override;

  // EsParser implementation.
  bool Parse(const uint8_t* buf, int size, int64_t pts, int64_t dts) override;
  bool Flush() override;
  void Reset() override;

 private:
  EsParserTeletext(const EsParserTeletext&) = delete;
  EsParserTeletext& operator=(const EsParserTeletext&) = delete;

  bool ParseInternal(const uint8_t* data, size_t size, int64_t pts);

  // Callbacks:
  // - to signal a new audio configuration,
  // - to send ES buffers.
  NewStreamInfoCB new_stream_info_cb_;
  EmitTextSampleCB emit_sample_cb_;

  // A map of page_id to language.
  std::unordered_map<uint16_t, std::string> languages_;
  bool sent_info_ = false;
  uint8_t last_magazine_;
  uint8_t last_page_number_;
  std::vector<std::string> last_lines_;
  int64_t last_pts_;
};

}  // namespace mp2t
}  // namespace media
}  // namespace shaka

#endif  // PACKAGER_MEDIA_FORMATS_MP2T_ES_PARSER_TELETEXT_H_
