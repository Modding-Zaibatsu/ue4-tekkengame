// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains data structures and utility functions used for serializing
// and parsing alternative service header values, common to HTTP/1.1 header
// fields and HTTP/2 and QUIC ALTSVC frames.  See specification at
// https://httpwg.github.io/http-extensions/alt-svc.html.

#ifndef NET_SPDY_SPDY_ALT_SVC_WIRE_FORMAT_H_
#define NET_SPDY_SPDY_ALT_SVC_WIRE_FORMAT_H_

#include <vector>

#include "base/basictypes.h"
#include "base/strings/string_piece.h"
#include "net/base/net_export.h"

using base::StringPiece;

namespace net {

namespace test {
class SpdyAltSvcWireFormatPeer;
}  // namespace test

class NET_EXPORT_PRIVATE SpdyAltSvcWireFormat {
 public:
  struct NET_EXPORT_PRIVATE AlternativeService {
    std::string protocol_id;
    std::string host;
    // Default is 0: invalid port.
    uint16 port = 0;
    // Default is 0: unspecified version.
    uint16 version = 0;
    // Default is one day.
    uint32 max_age = 86400;
    // Default is always use.
    double probability = 1.0;

    AlternativeService();
    AlternativeService(const std::string& protocol_id,
                       const std::string& host,
                       uint16 port,
                       uint16 version,
                       uint32 max_age,
                       double probability);

    bool operator==(const AlternativeService& other) const {
      return protocol_id == other.protocol_id && host == other.host &&
             port == other.port && version == other.version &&
             max_age == other.max_age && probability == other.probability;
    }
  };
  // An empty vector means alternative services should be cleared for given
  // origin.  Note that the wire format for this is the string "clear", not an
  // empty value (which is invalid).
  typedef std::vector<AlternativeService> AlternativeServiceVector;

  friend class test::SpdyAltSvcWireFormatPeer;
  static bool ParseHeaderFieldValue(StringPiece value,
                                    AlternativeServiceVector* altsvc_vector);
  static std::string SerializeHeaderFieldValue(
      const AlternativeServiceVector& altsvc_vector);

 private:
  static void SkipWhiteSpace(StringPiece::const_iterator* c,
                             StringPiece::const_iterator end);
  static bool PercentDecode(StringPiece::const_iterator c,
                            StringPiece::const_iterator end,
                            std::string* output);
  static bool ParseAltAuthority(StringPiece::const_iterator c,
                                StringPiece::const_iterator end,
                                std::string* host,
                                uint16* port);
  static bool ParsePositiveInteger16(StringPiece::const_iterator c,
                                     StringPiece::const_iterator end,
                                     uint16* value);
  static bool ParsePositiveInteger32(StringPiece::const_iterator c,
                                     StringPiece::const_iterator end,
                                     uint32* value);
  static bool ParseProbability(StringPiece::const_iterator c,
                               StringPiece::const_iterator end,
                               double* probability);
};

}  // namespace net

#endif  // NET_SPDY_SPDY_ALT_SVC_WIRE_FORMAT_H_
