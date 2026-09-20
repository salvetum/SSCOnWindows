/*
 * negotiation_tests.cpp - A2DP/AVDTP negotiation with a mock transport.
 *
 * The real BtStackTransport parses AVDTP capability events inside btstack's
 * run loop and is not mockable without the full btstack stack + hardware, so
 * the negotiation *decision* (which codec + encode sample rate a stream uses)
 * is exercised here against a MockTransport that only supplies remote caps —
 * exactly the seam the CLI (main.cpp) and the service (a2dp_service.cpp) share
 * through codec_policy.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "codec_policy.h"
#include "audio_encoder.h"

#include "doctest/doctest.h"

namespace {

/* Minimal stand-in for BtStackTransport::RemoteCodecCaps plus the two policy
 * calls the negotiation path uses. */
class MockTransport {
public:
    codec_policy::Caps caps_;

    codec_policy::Caps get_remote_caps() const { return caps_; }

    /* What the CLI/service do after seeing the caps: negotiate codec + rate. */
    bool negotiate(AudioCodec requested, uint32_t capture_sr,
                   uint32_t requested_sr, AudioCodec *codec, uint32_t *sr,
                   bool *codec_fb, bool *uhq_fb) const {
        return codec_policy::resolve_stream(
            requested, capture_sr, requested_sr, get_remote_caps(),
            codec, sr, codec_fb, uhq_fb);
    }
};

}  // namespace

TEST_CASE("mock transport: SSC device negotiates SSC + UHQ when advertised") {
    MockTransport t;
    t.caps_.ssc = true;
    t.caps_.ssc_cap = 0x3E;
    t.caps_.ssc_uhq = true;

    AudioCodec codec;
    uint32_t sr;
    bool cf = true, uf = true;
    REQUIRE(t.negotiate(AudioCodec::SSC, 48000, 96000, &codec, &sr, &cf, &uf));
    CHECK(codec == AudioCodec::SSC);
    CHECK(sr == 96000);
    CHECK_FALSE(cf);
    CHECK_FALSE(uf);
}

TEST_CASE("mock transport: UHQ request on a basic-cap device falls back") {
    MockTransport t;
    t.caps_.ssc = true;
    t.caps_.ssc_cap = 0x3C;   /* Buds3 FE: no 0x02 UHQ bit */
    t.caps_.ssc_uhq = false;

    AudioCodec codec;
    uint32_t sr;
    bool cf = true, uf = false;
    REQUIRE(t.negotiate(AudioCodec::SSC, 48000, 96000, &codec, &sr, &cf, &uf));
    CHECK(codec == AudioCodec::SSC);
    CHECK(sr == 48000);
    CHECK_FALSE(cf);
    CHECK(uf);
}

TEST_CASE("mock transport: requested AAC absent -> falls back to SSC") {
    MockTransport t;
    t.caps_.ssc = true;
    t.caps_.sbc = true;
    /* no AAC */

    AudioCodec codec;
    uint32_t sr;
    bool cf = false, uf = true;
    REQUIRE(t.negotiate(AudioCodec::AAC, 48000, 48000, &codec, &sr, &cf, &uf));
    CHECK(codec == AudioCodec::SSC);
    CHECK(sr == 48000);
    CHECK(cf);
    CHECK_FALSE(uf);
}

TEST_CASE("mock transport: no compatible codec -> negotiation fails") {
    MockTransport t;   /* advertises nothing */
    AudioCodec codec;
    uint32_t sr;
    CHECK_FALSE(t.negotiate(AudioCodec::SSC, 48000, 48000, &codec, &sr, nullptr, nullptr));
}