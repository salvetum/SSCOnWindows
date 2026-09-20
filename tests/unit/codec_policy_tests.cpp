/*
 * codec_policy_tests.cpp - unit tests for app/src/codec_policy.h
 *
 * Covers the three behaviors Faz 4 calls out:
 *   1. codec fallback logic (SSC > AAC > SBC),
 *   2. SSC bitrate snapping (mode-gated basic/UHQ sets),
 *   3. UHQ capability-bit fallback (0x02 absent -> 48 kHz).
 *
 * SPDX-License-Identifier: MIT
 */

#include "codec_policy.h"

#include "doctest/doctest.h"

using codec_policy::Caps;
using codec_policy::pick_bitrate_bps;
using codec_policy::resolve_codec;
using codec_policy::resolve_encode_sr;
using codec_policy::resolve_stream;
using codec_policy::snap_bitrate_bps;

static Caps caps_ssc_only();

TEST_CASE("codec policy: requested codec is honored when advertised") {
    Caps caps;                     /* nothing advertised */
    AudioCodec out = AudioCodec::SBC;

    caps.aac = true;
    REQUIRE(resolve_codec(AudioCodec::AAC, caps, &out));
    CHECK(out == AudioCodec::AAC);

    caps.ssc = true;
    REQUIRE(resolve_codec(AudioCodec::SSC, caps, &out));
    CHECK(out == AudioCodec::SSC);

    caps.sbc = true;
    REQUIRE(resolve_codec(AudioCodec::SBC, caps, &out));
    CHECK(out == AudioCodec::SBC);
}

TEST_CASE("codec policy: fallback priority is SSC > AAC > SBC") {
    AudioCodec out;

    /* Requested absent -> fallback to SSC whenever SSC is advertised. */
    CHECK(resolve_codec(AudioCodec::AAC, caps_ssc_only(), &out));
    CHECK(out == AudioCodec::SSC);
    CHECK(resolve_codec(AudioCodec::SBC, caps_ssc_only(), &out));
    CHECK(out == AudioCodec::SSC);

    /* SSC absent -> AAC beats SBC. */
    Caps caps_aac_sbc;
    caps_aac_sbc.aac = true;
    caps_aac_sbc.sbc = true;
    CHECK(resolve_codec(AudioCodec::SSC, caps_aac_sbc, &out));
    CHECK(out == AudioCodec::AAC);
    CHECK(resolve_codec(AudioCodec::SBC, caps_aac_sbc, &out));
    CHECK(out == AudioCodec::SBC);   /* requested SBC is advertised -> honored */
    CHECK(resolve_codec(AudioCodec::SSC, caps_aac_sbc, &out));
    CHECK(out == AudioCodec::AAC);

    /* Only SBC -> last resort. */
    Caps caps_sbc;
    caps_sbc.sbc = true;
    CHECK(resolve_codec(AudioCodec::SSC, caps_sbc, &out));
    CHECK(out == AudioCodec::SBC);
}

TEST_CASE("codec policy: no compatible codec returns false") {
    Caps none;
    AudioCodec out = AudioCodec::SSC;
    CHECK_FALSE(resolve_codec(AudioCodec::SSC, none, &out));
    CHECK_FALSE(resolve_codec(AudioCodec::AAC, none, &out));
    CHECK_FALSE(resolve_codec(AudioCodec::SBC, none, &out));
}

TEST_CASE("snap_bitrate_bps: 48 kHz basic set (88..328 kbps)") {
    CHECK(snap_bitrate_bps(100000, 48000) == 96000);
    CHECK(snap_bitrate_bps(128000, 48000) == 128000);
    CHECK(snap_bitrate_bps(229000, 48000) == 229000);
    CHECK(snap_bitrate_bps(240000, 48000) == 229000);   /* 240 -> 229 (11 vs 27) */
    CHECK(snap_bitrate_bps(447000, 48000) == 328000);   /* 447 snaps to max basic */
    CHECK(snap_bitrate_bps(999999, 48000) == 328000);
    CHECK(snap_bitrate_bps(1, 48000) == 88000);         /* below min -> min */
}

TEST_CASE("snap_bitrate_bps: 96 kHz UHQ set (152..886 kbps)") {
    CHECK(snap_bitrate_bps(100000, 96000) == 152000);
    CHECK(snap_bitrate_bps(229000, 96000) == 250000);   /* nearest UHQ entry */
    CHECK(snap_bitrate_bps(447000, 96000) == 442000);   /* NNNNNN documented gap */
    CHECK(snap_bitrate_bps(584000, 96000) == 584000);
    CHECK(snap_bitrate_bps(886000, 96000) == 886000);
    CHECK(snap_bitrate_bps(900000, 96000) == 886000);   /* above max -> max */
}

TEST_CASE("snap_bitrate_bps: parity with SscEncoder::snap_bitrate_kbps contract") {
    /* The public kbps wrapper returns 0 unchanged for the auto (0) sentinel;
     * the bps-level helper itself still snaps (0 -> min). */
    CHECK(snap_bitrate_bps(0, 48000) == 88000);
    CHECK((snap_bitrate_bps(447 * 1000, 48000) / 1000) == 328);
    CHECK((snap_bitrate_bps(447 * 1000, 96000) / 1000) == 442);
}

TEST_CASE("pick_bitrate_bps: quality x rate matrix") {
    CHECK(pick_bitrate_bps(EncoderQuality::High, 48000) == 229000);
    CHECK(pick_bitrate_bps(EncoderQuality::Standard, 48000) == 192000);
    CHECK(pick_bitrate_bps(EncoderQuality::Mobile, 48000) == 128000);
    CHECK(pick_bitrate_bps(EncoderQuality::High, 96000) == 584000);
    CHECK(pick_bitrate_bps(EncoderQuality::Standard, 96000) == 442000);
    CHECK(pick_bitrate_bps(EncoderQuality::Mobile, 96000) == 250000);
}

TEST_CASE("resolve_encode_sr: UHQ gate honors the 0x02 capability bit") {
    Caps caps;
    caps.ssc = true;
    caps.ssc_cap = 0x3C;          /* Buds3 FE: no UHQ2 (0x02) bit */
    caps.ssc_uhq = false;

    bool fell = true;
    CHECK(resolve_encode_sr(AudioCodec::SSC, 48000, 96000, caps, &fell) == 48000);
    CHECK(fell);                                   /* dropped to 48 kHz */

    caps.ssc_uhq = true;
    caps.ssc_cap = 0x3E;
    fell = false;
    CHECK(resolve_encode_sr(AudioCodec::SSC, 48000, 96000, caps, &fell) == 96000);
    CHECK_FALSE(fell);

    CHECK(resolve_encode_sr(AudioCodec::SSC, 48000, 88200, caps, &fell) == 88200);
}

TEST_CASE("resolve_encode_sr: non-SSC and non-UHQ requests pass through") {
    Caps caps;
    caps.ssc_uhq = false;         /* even without the UHQ bit... */
    bool fell = true;
    CHECK(resolve_encode_sr(AudioCodec::AAC, 48000, 96000, caps, &fell) == 48000);
    CHECK_FALSE(fell);
    CHECK(resolve_encode_sr(AudioCodec::SSC, 48000, 48000, caps, &fell) == 48000);
    CHECK_FALSE(fell);
    CHECK(resolve_encode_sr(AudioCodec::SBC, 44100, 96000, caps, &fell) == 44100);
    CHECK_FALSE(fell);
    CHECK(resolve_encode_sr(AudioCodec::SSC, 44100, 96000, caps, &fell) == 44100);
    CHECK_FALSE(fell);            /* UHQ only from a 48 kHz capture */
}

TEST_CASE("resolve_stream: end-to-end composition") {
    /* Full negotiation: device advertises only SBC (Buds-less case). */
    Caps caps;
    caps.sbc = true;
    AudioCodec codec;
    uint32_t sr;
    bool codec_fb = false, uhq_fb = false;
    REQUIRE(resolve_stream(AudioCodec::SSC, 48000, 96000, caps,
                           &codec, &sr, &codec_fb, &uhq_fb));
    CHECK(codec == AudioCodec::SBC);
    CHECK(codec_fb);
    CHECK_FALSE(uhq_fb);
    CHECK(sr == 48000);           /* SBC path ignores the UHQ request */

    /* SSC device without UHQ bit: codec kept, UHQ dropped. */
    caps.ssc = true;
    caps.ssc_cap = 0x3C;
    caps.ssc_uhq = false;
    codec_fb = uhq_fb = false;
    REQUIRE(resolve_stream(AudioCodec::SSC, 48000, 96000, caps,
                           &codec, &sr, &codec_fb, &uhq_fb));
    CHECK(codec == AudioCodec::SSC);
    CHECK_FALSE(codec_fb);
    CHECK(uhq_fb);
    CHECK(sr == 48000);

    /* No compatible codec at all -> failure. */
    Caps none;
    CHECK_FALSE(resolve_stream(AudioCodec::AAC, 48000, 48000, none,
                               &codec, &sr));
}

/* Small helpers used above. */
static Caps caps_ssc_only() {
    Caps c;
    c.ssc = true;
    c.ssc_cap = 0x3C;
    return c;
}