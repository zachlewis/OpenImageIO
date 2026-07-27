// Copyright Contributors to the OpenImageIO project.
// SPDX-License-Identifier: Apache-2.0
// https://github.com/AcademySoftwareFoundation/OpenImageIO

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <vector>

#include <OpenImageIO/argparse.h>
#include <OpenImageIO/benchmark.h>
#include <OpenImageIO/color.h>
#include <OpenImageIO/filesystem.h>
#include <OpenImageIO/simd.h>
#include <OpenImageIO/strutil.h>
#include <OpenImageIO/timer.h>
#include <OpenImageIO/typedesc.h>
#include <OpenImageIO/unittest.h>

#include "color_pvt.h"


using namespace OIIO;
using namespace simd;


// Aid for things that are too short to benchmark accurately
#define REP10(x) x, x, x, x, x, x, x, x, x, x

static int iterations = 1000000;
static int ntrials    = 5;
static bool verbose   = false;



static void
getargs(int argc, char* argv[])
{
    ArgParse ap;
    // clang-format off
    ap.intro("color_test\n" OIIO_INTRO_STRING)
      .usage("color_test [options]");

    ap.arg("-v", &verbose)
      .help("Verbose mode");
    ap.arg("--iters %d", &iterations)
      .help(Strutil::fmt::format("Number of iterations (default: {})", iterations));
    ap.arg("--trials %d", &ntrials)
      .help("Number of trials");
    // clang-format on

    ap.parse(argc, (const char**)argv);
}



static void
test_sRGB_conversion()
{
    Benchmarker bench;

    OIIO_CHECK_EQUAL_THRESH(linear_to_sRGB(0.0f), 0.0f, 1.0e-6);
    OIIO_CHECK_EQUAL_THRESH(linear_to_sRGB(1.0f), 1.0f, 1.0e-6);
    OIIO_CHECK_EQUAL_THRESH(linear_to_sRGB(0.5f), 0.735356983052449f, 1.0e-6);

    OIIO_CHECK_EQUAL_THRESH(sRGB_to_linear(0.0f), 0.0f, 1.0e-6);
    OIIO_CHECK_EQUAL_THRESH(sRGB_to_linear(1.0f), 1.0f, 1.0e-6);
    OIIO_CHECK_EQUAL_THRESH(sRGB_to_linear(0.5f), 0.214041140482232f, 1.0e-6);

    // Check the SIMD versions, too
    OIIO_CHECK_SIMD_EQUAL_THRESH(linear_to_sRGB(vfloat4(0.0f)), vfloat4(0.0f),
                                 1.0e-5);
    OIIO_CHECK_SIMD_EQUAL_THRESH(linear_to_sRGB(vfloat4(1.0f)), vfloat4(1.0f),
                                 1.0e-5);
    OIIO_CHECK_SIMD_EQUAL_THRESH(linear_to_sRGB(vfloat4(0.5f)),
                                 vfloat4(0.735356983052449f), 1.0e-5);

    OIIO_CHECK_SIMD_EQUAL_THRESH(sRGB_to_linear(vfloat4(0.0f)), vfloat4(0.0f),
                                 1.0e-5);
    OIIO_CHECK_SIMD_EQUAL_THRESH(sRGB_to_linear(vfloat4(1.0f)), vfloat4(1.0f),
                                 1.0e-5);
    OIIO_CHECK_SIMD_EQUAL_THRESH(sRGB_to_linear(vfloat4(0.5f)),
                                 vfloat4(0.214041140482232f), 1.0e-5);

    float fval = 0.5f;
    clobber(fval);
    vfloat4 vfval(fval);
    clobber(vfval);
    bench("sRGB_to_linear",
          [&]() { return DoNotOptimize(sRGB_to_linear(fval)); });
    bench("linear_to_sRGB",
          [&]() { return DoNotOptimize(sRGB_to_linear(fval)); });
    bench.work(4);
    bench("sRGB_to_linear simd",
          [&]() { return DoNotOptimize(sRGB_to_linear(vfval)); });
    bench("linear_to_sRGB simd",
          [&]() { return DoNotOptimize(sRGB_to_linear(vfval)); });
}



static void
test_Rec709_conversion()
{
    Benchmarker bench;

    OIIO_CHECK_EQUAL_THRESH(linear_to_Rec709(0.0f), 0.0f, 1.0e-6);
    OIIO_CHECK_EQUAL_THRESH(linear_to_Rec709(1.0f), 1.0f, 1.0e-6);
    OIIO_CHECK_EQUAL_THRESH(linear_to_Rec709(0.5f), 0.705515089922121f, 1.0e-6);

    OIIO_CHECK_EQUAL_THRESH(Rec709_to_linear(0.0f), 0.0f, 1.0e-6);
    OIIO_CHECK_EQUAL_THRESH(Rec709_to_linear(1.0f), 1.0f, 1.0e-6);
    OIIO_CHECK_EQUAL_THRESH(Rec709_to_linear(0.5f), 0.259589400506286f, 1.0e-6);

    float fval = 0.5f;
    clobber(fval);
    bench("Rec709_to_linear",
          [&]() { return DoNotOptimize(Rec709_to_linear(fval)); });
    bench("linear_to_Rec709",
          [&]() { return DoNotOptimize(Rec709_to_linear(fval)); });
}



static void
test_interop_id_grammar()
{
    using OIIO::pvt::InteropIdForm;
    using OIIO::pvt::is_utility_interop_id;
    using OIIO::pvt::is_valid_interop_id;
    using OIIO::pvt::parse_interop_id;
    using OIIO::pvt::sanitize_id_token;
    using OIIO::pvt::strip_leftmost_namespace;

    // Validity + form, per the CIF Annex B grammar (4 legal forms; 3+
    // colons is always invalid).
    OIIO_CHECK_ASSERT(is_valid_interop_id("lin_ap0_scene"));
    OIIO_CHECK_EQUAL((int)parse_interop_id("lin_ap0_scene").form,
                     (int)InteropIdForm::BASE);

    // "local:srgb" is an ordinary INNER_BASE id at the grammar layer --
    // the grammar has zero knowledge of "local" as special; that's a
    // question one layer up (resolution code checking
    // form == OUTER_INNER_BASE && inner == "local").
    {
        auto parts = parse_interop_id("local:srgb");
        OIIO_CHECK_ASSERT(is_valid_interop_id("local:srgb"));
        OIIO_CHECK_EQUAL((int)parts.form, (int)InteropIdForm::INNER_BASE);
        OIIO_CHECK_EQUAL(parts.inner, "local");
        OIIO_CHECK_EQUAL(parts.base, "srgb");
    }

    {
        auto parts = parse_interop_id("show1-config:local:srgb");
        OIIO_CHECK_ASSERT(is_valid_interop_id("show1-config:local:srgb"));
        OIIO_CHECK_EQUAL((int)parts.form, (int)InteropIdForm::OUTER_INNER_BASE);
        OIIO_CHECK_EQUAL(parts.outer, "show1-config");
        OIIO_CHECK_EQUAL(parts.inner, "local");
        OIIO_CHECK_EQUAL(parts.base, "srgb");
    }

    {
        auto parts = parse_interop_id("my-studio::srgb");
        OIIO_CHECK_ASSERT(is_valid_interop_id("my-studio::srgb"));
        OIIO_CHECK_EQUAL((int)parts.form, (int)InteropIdForm::OUTER_BLANK_BASE);
        OIIO_CHECK_EQUAL(parts.outer, "my-studio");
        OIIO_CHECK_ASSERT(parts.inner.empty());
        OIIO_CHECK_EQUAL(parts.base, "srgb");
    }

    OIIO_CHECK_FALSE(is_valid_interop_id(""));
    OIIO_CHECK_FALSE(is_valid_interop_id(":base"));
    OIIO_CHECK_FALSE(is_valid_interop_id(":inner:base"));
    OIIO_CHECK_FALSE(is_valid_interop_id("a:b:c:d"));
    // Validation never folds case or sanitizes.
    OIIO_CHECK_FALSE(is_valid_interop_id("Lin_AP0_Scene"));
    OIIO_CHECK_FALSE(is_valid_interop_id("caf\xc3\xa9"));   // "café"
    OIIO_CHECK_FALSE(is_valid_interop_id("\xe4\xb8\xad"));  // "中"
    OIIO_CHECK_FALSE(is_valid_interop_id("outer::"));
    OIIO_CHECK_FALSE(is_valid_interop_id("outer:"));
    OIIO_CHECK_FALSE(is_valid_interop_id("lin_ap0_scene:"));

    // Sanitization (Annex C, 5-step precedence).
    OIIO_CHECK_EQUAL(sanitize_id_token("lin_ap0_scene"), "lin_ap0_scene");
    OIIO_CHECK_EQUAL(sanitize_id_token("ACEScg"), "acescg");
    OIIO_CHECK_EQUAL(sanitize_id_token("sRGB - Texture"), "srgb_-_texture");
    OIIO_CHECK_EQUAL(sanitize_id_token("a{b}c"), "a(b)c");
    OIIO_CHECK_EQUAL(sanitize_id_token("a<b>c"), "a(b)c");
    OIIO_CHECK_EQUAL(sanitize_id_token("a,b"), "a.b");
    OIIO_CHECK_EQUAL(sanitize_id_token("a;b"), "a|b");
    OIIO_CHECK_EQUAL(sanitize_id_token("a:b"), "a|b");
    OIIO_CHECK_EQUAL(sanitize_id_token("a'b\"c"), "a#b#c");
    OIIO_CHECK_EQUAL(sanitize_id_token("a\\b"), "a/b");
    OIIO_CHECK_EQUAL(sanitize_id_token("a!b=c@d"), "a*b*c*d");
    // Non-ASCII: one '^' per whole UTF-8 code point, never per byte.
    {
        std::string cafe = "caf\xc3\xa9";  // "café", 2-byte 'é'
        std::string got  = sanitize_id_token(cafe);
        OIIO_CHECK_EQUAL(got, "caf^");
        OIIO_CHECK_EQUAL(got.size(), size_t(4));
    }
    {
        std::string zhong = "\xe4\xb8\xad";  // "中", 3-byte code point
        std::string got   = sanitize_id_token(zhong);
        OIIO_CHECK_EQUAL(got, "^");
        OIIO_CHECK_EQUAL(got.size(), size_t(1));
    }
    OIIO_CHECK_EQUAL(sanitize_id_token("a\xe4\xb8\xad"
                                       "b"),
                     "a^b");

    // Namespace stripping: pure substring op, independent of validity,
    // never assumes the result is itself a valid id.
    OIIO_CHECK_EQUAL(strip_leftmost_namespace("a:b:c"), "b:c");
    OIIO_CHECK_EQUAL(strip_leftmost_namespace("a::c"), ":c");
    OIIO_CHECK_EQUAL(strip_leftmost_namespace("a"), "a");
    // Load-bearing: the blank-inner leading colon is retained, so the
    // result is NOT "srgb".
    OIIO_CHECK_EQUAL(strip_leftmost_namespace("my-studio::srgb"), ":srgb");
    OIIO_CHECK_NE(strip_leftmost_namespace("my-studio::srgb"),
                  std::string("srgb"));

    // Utility tokens: case-sensitive exact membership, no grammar
    // involvement.
    OIIO_CHECK_ASSERT(is_utility_interop_id("data"));
    OIIO_CHECK_ASSERT(is_utility_interop_id("unknown"));
    OIIO_CHECK_ASSERT(is_utility_interop_id("bypass"));
    OIIO_CHECK_FALSE(is_utility_interop_id("Data"));
}



static void
test_interop_resolve()
{
    if (!ColorConfig::supportsOpenColorIO())
        return;

    // OCIO >= 2.5 is required for the `interop_id:` color space attribute
    // (native getInteropID()); tiers that depend on it are gated below.
    const bool has_interop_id_attr = ColorConfig::OpenColorIO_version_hex()
                                     >= 0x02050000;

    // ---- Base fixture: stripped-namespace, config-local, literal-unknown,
    // and total-miss passthrough. No interop_id attributes -- safe to parse
    // on any linked OCIO version. -----------------------------------------
    static const char* base_yaml = R"(ocio_profile_version: 2.1
name: resolvetest
search_path: ""
roles:
  default: ref
  scene_linear: ref
  aces_interchange: ref
displays:
  disp:
    - !<View> {name: main, colorspace: ref}
colorspaces:
  - !<ColorSpace>
    name: ref

  - !<ColorSpace>
    name: gamma24_space
    aliases: [g24_rec709_scene]
    from_scene_reference: !<ExponentTransform> {value: [2.4, 2.4, 2.4, 1]}

  - !<ColorSpace>
    name: local_target
    aliases: [my_local_alias, unknown]
    from_scene_reference: !<ExponentTransform> {value: [1.8, 1.8, 1.8, 1]}

  - !<ColorSpace>
    name: foo
    aliases: ["b:c:d"]
    from_scene_reference: !<ExponentTransform> {value: [2.2, 2.2, 2.2, 1]}
)";
    std::string base_path        = Filesystem::temp_directory_path()
                            + "/oiio_color_test_resolve_base.ocio";
    OIIO_CHECK_ASSERT(Filesystem::write_text_file(base_path, base_yaml));
    {
        ColorConfig cc(base_path);
        OIIO_CHECK_ASSERT(!cc.has_error());

        // Tier 1a': stripped-namespace retry -- the full string
        // "myapp:g24_rec709_scene" matches no name/alias/role, but stripping
        // the one leftmost namespace reaches the real alias.
        OIIO_CHECK_EQUAL(cc.resolve("myapp:g24_rec709_scene"), "gamma24_space");

        // Tier 1a'': config-local "<config>:local:<base>" form, matched
        // against names/aliases only. A hit through an alias...
        OIIO_CHECK_EQUAL(cc.resolve("resolvetest:local:my_local_alias"),
                         "local_target");
        // ...and a miss when the base names nothing in this config (proves
        // the tier doesn't fall back to a fuzzy match).
        OIIO_CHECK_EQUAL(cc.resolve("resolvetest:local:no_such_space"),
                         "resolvetest:local:no_such_space");

        // "unknown" is a literal name/alias lookup only -- never routed
        // through the ranked data-space search. Reachable here because
        // local_target happens to carry it as a literal alias (ordinary
        // tier 1a), not because of any utility-token machinery.
        OIIO_CHECK_EQUAL(cc.resolve("unknown"), "local_target");

        // Regression guard: a name that matches nothing in any tier is
        // still passed through unchanged (main's historical behavior).
        OIIO_CHECK_EQUAL(cc.resolve("totally_unrecognized_id"),
                         "totally_unrecognized_id");

        // The new tiers are gated on Annex-B validity: an input that is not
        // a well-formed interop id skips them entirely and keeps the legacy
        // passthrough. The gate is load-bearing in each check below -- an
        // ungated strip WOULD find a real space ("foo" or its literal
        // alias "b:c:d").
        OIIO_CHECK_EQUAL(cc.resolve("ns:foo"), "foo");  // valid: strip works
        OIIO_CHECK_EQUAL(cc.resolve(":foo"), ":foo");   // empty outer token
        OIIO_CHECK_EQUAL(cc.resolve("a:b:c:d"), "a:b:c:d");  // 3+ colons
        OIIO_CHECK_EQUAL(cc.resolve("foo:"), "foo:");        // empty base token
        // ...while tier 1a's direct OCIO lookup stays byte-identical to
        // legacy behavior, un-gated: the literal colon-bearing alias is
        // still reachable by its exact string.
        OIIO_CHECK_EQUAL(cc.resolve("b:c:d"), "foo");
    }
    Filesystem::remove(base_path);

    // ---- Reserved-local strip guard: the ONLY legal route for an
    // "outer:local:base" query is the config-local ownership tier. Its
    // stripped form "local:x" must never be fed to the ordinary
    // name/role/alias search, where a literal alias named "local:x" would
    // poach another config's private id. No interop_id attributes -- safe
    // on any OCIO version.
    {
        static const char* stripguard_yaml = R"(ocio_profile_version: 2.1
name: stripguard
search_path: ""
roles:
  default: ref
  scene_linear: ref
colorspaces:
  - !<ColorSpace>
    name: ref

  - !<ColorSpace>
    name: poach_via_alias
    aliases: ["local:x"]
    from_scene_reference: !<ExponentTransform> {value: [1.4, 1.4, 1.4, 1]}

  - !<ColorSpace>
    name: inner_target
    aliases: [xbase]
    from_scene_reference: !<ExponentTransform> {value: [1.3, 1.3, 1.3, 1]}
)";
        std::string stripguard_path
            = Filesystem::temp_directory_path()
              + "/oiio_color_test_resolve_stripguard.ocio";
        OIIO_CHECK_ASSERT(
            Filesystem::write_text_file(stripguard_path, stripguard_yaml));
        {
            ColorConfig cc(stripguard_path);
            OIIO_CHECK_ASSERT(!cc.has_error());
            // Another config's private id: strips to "local:x", which IS a
            // literal alias here -- but the reserved-local routing never
            // offers the stripped form to the alias search. Total miss.
            OIIO_CHECK_EQUAL(cc.resolve("othercfg:local:x"),
                             "othercfg:local:x");
            // Legacy tier 1a is untouched: the literal colon-bearing alias
            // is still reachable by its exact string.
            OIIO_CHECK_EQUAL(cc.resolve("local:x"), "poach_via_alias");
            // Genuine ownership matching still works for THIS config.
            OIIO_CHECK_EQUAL(cc.resolve("stripguard:local:xbase"),
                             "inner_target");
        }
        Filesystem::remove(stripguard_path);
    }

    // ---- Uppercase fixture: an OCIO name/alias lookup is case-insensitive,
    // so a literal (capitalized) "Unknown"/"Bypass" color space is reachable
    // via tier 1a's pre-existing OCIO lookup regardless of the CIF grammar's
    // lowercase-only validity rule (is_valid_interop_id, already covered by
    // test_interop_id_grammar) -- resolve() is not gated on id validity, by
    // design, so it doesn't re-derive that grammar-level invariant. What IS
    // decisive and worth guarding here: the new utility-ranking tier
    // (resolve_data_utility) never even runs for these, because tier 1a's
    // OCIO-native lookup already satisfied the query first.
    {
        static const char* upper_yaml = R"(ocio_profile_version: 2.1
search_path: ""
roles:
  default: ref
  scene_linear: ref
colorspaces:
  - !<ColorSpace>
    name: ref

  - !<ColorSpace>
    name: Uppercase_Utility
    aliases: [Unknown, Bypass]
    isdata: true
)";
        std::string upper_path        = Filesystem::temp_directory_path()
                                 + "/oiio_color_test_resolve_upper.ocio";
        OIIO_CHECK_ASSERT(Filesystem::write_text_file(upper_path, upper_yaml));
        ColorConfig cc(upper_path);
        OIIO_CHECK_ASSERT(!cc.has_error());
        OIIO_CHECK_EQUAL(cc.resolve("unknown"), "Uppercase_Utility");
        OIIO_CHECK_EQUAL(cc.resolve("bypass"), "Uppercase_Utility");
        Filesystem::remove(upper_path);
    }

    // ---- Real "Raw" data space: a config with a data space literally named
    // "Raw" alongside other spaces is NOT the synthetic one-space
    // OCIO::Config::CreateRaw() config, so the utility-token ranking must treat
    // its "Raw" as a valid target -- "bypass"/"data" resolve to it. (The
    // synthetic-raw skip is keyed on the config's single-colorspace shape,
    // not the name alone.) No interop_id attribute -- safe on any OCIO version.
    {
        static const char* raw_yaml = R"(ocio_profile_version: 2.1
search_path: ""
roles:
  default: ref
  scene_linear: ref
colorspaces:
  - !<ColorSpace>
    name: ref

  - !<ColorSpace>
    name: Raw
    isdata: true
)";
        std::string raw_path        = Filesystem::temp_directory_path()
                               + "/oiio_color_test_resolve_raw.ocio";
        OIIO_CHECK_ASSERT(Filesystem::write_text_file(raw_path, raw_yaml));
        ColorConfig cc(raw_path);
        OIIO_CHECK_ASSERT(!cc.has_error());
        OIIO_CHECK_EQUAL(cc.resolve("bypass"), "Raw");
        OIIO_CHECK_EQUAL(cc.resolve("data"), "Raw");
        Filesystem::remove(raw_path);
    }

    if (has_interop_id_attr) {
        // ---- Explicit interop_id attribute: safe directions -- exactly
        // one side stripped -- still match. ---------------------------
        static const char* safe_yaml = R"(ocio_profile_version: 2.1
search_path: ""
roles:
  default: ref
  scene_linear: ref
colorspaces:
  - !<ColorSpace>
    name: ref

  - !<ColorSpace>
    name: attr_bare_y
    interop_id: "y"
    from_scene_reference: !<ExponentTransform> {value: [1.5, 1.5, 1.5, 1]}

  - !<ColorSpace>
    name: attr_ns_z
    interop_id: "app2:z"
    from_scene_reference: !<ExponentTransform> {value: [1.6, 1.6, 1.6, 1]}
)";
        std::string safe_path        = Filesystem::temp_directory_path()
                                + "/oiio_color_test_resolve_safe.ocio";
        OIIO_CHECK_ASSERT(Filesystem::write_text_file(safe_path, safe_yaml));
        {
            ColorConfig cc(safe_path);
            OIIO_CHECK_ASSERT(!cc.has_error());
            // Query-side stripped: bare attribute "y" matches namespaced
            // query "app:y".
            OIIO_CHECK_EQUAL(cc.resolve("app:y"), "attr_bare_y");
            // Attribute-side stripped: namespaced attribute "app2:z"
            // matches bare query "z".
            OIIO_CHECK_EQUAL(cc.resolve("z"), "attr_ns_z");
        }
        Filesystem::remove(safe_path);

        // ---- Explicit interop_id attribute: the both-sides-stripped
        // cross-namespace false positive is rejected. -------------------
        static const char* reject_yaml = R"(ocio_profile_version: 2.1
search_path: ""
roles:
  default: ref
  scene_linear: ref
colorspaces:
  - !<ColorSpace>
    name: ref

  - !<ColorSpace>
    name: attr_oiio_x
    interop_id: "oiio:x"
    from_scene_reference: !<ExponentTransform> {value: [1.7, 1.7, 1.7, 1]}
)";
        std::string reject_path        = Filesystem::temp_directory_path()
                                  + "/oiio_color_test_resolve_reject.ocio";
        OIIO_CHECK_ASSERT(
            Filesystem::write_text_file(reject_path, reject_yaml));
        {
            ColorConfig cc(reject_path);
            OIIO_CHECK_ASSERT(!cc.has_error());
            // "oiio:x" and "ocio:x" both strip to "x", but neither raw side
            // matches -- a miss, not a false positive.
            OIIO_CHECK_EQUAL(cc.resolve("ocio:x"), "ocio:x");
        }
        Filesystem::remove(reject_path);

        // ---- Reserved `local` namespace: a declared interop_id attribute
        // whose leftmost segment is `local` is never matched by the
        // attribute tier -- otherwise a grammar-legal "local:x" declaration
        // would poach OTHER configs' private "<config>:local:x" IDs via the
        // stripped-attribute match. The genuine config-local tier and
        // ordinary declared attributes are unaffected.
        static const char* localns_yaml = R"(ocio_profile_version: 2.1
name: localns
search_path: ""
roles:
  default: ref
  scene_linear: ref
colorspaces:
  - !<ColorSpace>
    name: ref

  - !<ColorSpace>
    name: poacher
    interop_id: "local:x"
    from_scene_reference: !<ExponentTransform> {value: [1.9, 1.9, 1.9, 1]}

  - !<ColorSpace>
    name: inner_target
    aliases: [xbase]
    from_scene_reference: !<ExponentTransform> {value: [2.0, 2.0, 2.0, 1]}

  - !<ColorSpace>
    name: normal_attr
    interop_id: "app9:q"
    from_scene_reference: !<ExponentTransform> {value: [2.1, 2.1, 2.1, 1]}
)";
        std::string localns_path        = Filesystem::temp_directory_path()
                                   + "/oiio_color_test_resolve_localns.ocio";
        OIIO_CHECK_ASSERT(
            Filesystem::write_text_file(localns_path, localns_yaml));
        {
            ColorConfig cc(localns_path);
            OIIO_CHECK_ASSERT(!cc.has_error());
            // Another config's config-local ID strips to "local:x", which
            // equals the declared attribute -- but the reserved-namespace
            // exclusion makes it a total miss, not a poach. (An attribute
            // declaring "othercfg:local:x" verbatim cannot even be tested
            // from YAML: OCIO's own loader rejects interop_id values with
            // more than one colon. resolve_explicit_interop_id still
            // excludes any attribute containing ":local:" as
            // defense-in-depth for programmatically built configs.)
            OIIO_CHECK_EQUAL(cc.resolve("othercfg:local:x"),
                             "othercfg:local:x");
            // The bare declared form itself is unreachable too.
            OIIO_CHECK_EQUAL(cc.resolve("local:x"), "local:x");
            // The genuine config-local tier still resolves for THIS config.
            OIIO_CHECK_EQUAL(cc.resolve("localns:local:xbase"), "inner_target");
            // Ordinary declared attributes are unaffected (attribute-side
            // strip still matches).
            OIIO_CHECK_EQUAL(cc.resolve("q"), "normal_attr");
        }
        Filesystem::remove(localns_path);

        // ---- Utility-token ranking: rank 0 (self-identity via interop_id)
        // short-circuits for both "bypass" and "data". --------------------
        static const char* rank_full_yaml = R"(ocio_profile_version: 2.1
search_path: ""
roles:
  default: ref
  scene_linear: ref
colorspaces:
  - !<ColorSpace>
    name: ref

  - !<ColorSpace>
    name: bypass_named
    interop_id: bypass
    isdata: true

  - !<ColorSpace>
    name: data_named
    interop_id: data
    isdata: true

  - !<ColorSpace>
    name: plain_data_space
    isdata: true
)";
        std::string rank_full_path        = Filesystem::temp_directory_path()
                                     + "/oiio_color_test_resolve_rank_full.ocio";
        OIIO_CHECK_ASSERT(
            Filesystem::write_text_file(rank_full_path, rank_full_yaml));
        {
            ColorConfig cc(rank_full_path);
            OIIO_CHECK_ASSERT(!cc.has_error());
            OIIO_CHECK_EQUAL(cc.resolve("bypass"), "bypass_named");
            OIIO_CHECK_EQUAL(cc.resolve("data"), "data_named");
            // "unknown" is never ranked -- no literal "unknown" name/alias
            // exists here, so it's a total miss even though data spaces do.
            OIIO_CHECK_EQUAL(cc.resolve("unknown"), "unknown");
        }
        Filesystem::remove(rank_full_path);

        // ---- Utility-token ranking: without a self-identified space, a
        // plain data space (rank 1) beats one identified as the OTHER
        // token (rank 2). The "data" query's mirror case runs through the
        // identical ranking code path (data_space_identifies_as / rank
        // computation are symmetric in token/other), so one direction is
        // sufficient coverage here.
        static const char* rank_partial_yaml = R"(ocio_profile_version: 2.1
search_path: ""
roles:
  default: ref
  scene_linear: ref
colorspaces:
  - !<ColorSpace>
    name: ref

  - !<ColorSpace>
    name: data_named
    interop_id: data
    isdata: true

  - !<ColorSpace>
    name: plain_data_space
    isdata: true
)";
        std::string rank_partial_path
            = Filesystem::temp_directory_path()
              + "/oiio_color_test_resolve_rank_partial.ocio";
        OIIO_CHECK_ASSERT(
            Filesystem::write_text_file(rank_partial_path, rank_partial_yaml));
        {
            ColorConfig cc(rank_partial_path);
            OIIO_CHECK_ASSERT(!cc.has_error());
            // No space here self-identifies as "bypass"; data_named
            // identifies as the OTHER token (rank 2), so the plain data
            // space (rank 1) wins.
            OIIO_CHECK_EQUAL(cc.resolve("bypass"), "plain_data_space");
        }
        Filesystem::remove(rank_partial_path);
    }

    // ---- Interop IDs that name nothing in THIS config pass through
    // unchanged. A well-formed registry-style id ("lin_ap0_scene") that no
    // local name, alias, or attribute claims is a total miss today; the
    // lookup against OIIO's built-in interop identities that will resolve
    // it arrives in a following PR, slotting in after the syntactic tiers.
    // Utility tokens likewise stay misses on a config with no data spaces.
    {
        static const char* passthru_yaml = R"(ocio_profile_version: 2.1
search_path: ""
roles:
  default: my_ap0_ref
  scene_linear: my_ap0_ref
  aces_interchange: my_ap0_ref
colorspaces:
  - !<ColorSpace>
    name: my_ap0_ref
)";
        std::string passthru_path        = Filesystem::temp_directory_path()
                                    + "/oiio_color_test_resolve_passthru.ocio";
        OIIO_CHECK_ASSERT(
            Filesystem::write_text_file(passthru_path, passthru_yaml));
        ColorConfig cc(passthru_path);
        OIIO_CHECK_ASSERT(!cc.has_error());

        OIIO_CHECK_EQUAL(cc.resolve("lin_ap0_scene"), "lin_ap0_scene");
        OIIO_CHECK_EQUAL(cc.resolve("data"), "data");
        OIIO_CHECK_EQUAL(cc.resolve("bypass"), "bypass");
        OIIO_CHECK_EQUAL(cc.resolve("unknown"), "unknown");

        Filesystem::remove(passthru_path);
    }
}



int
main(int argc, char* argv[])
{
#if !defined(NDEBUG) || defined(OIIO_CI) || defined(OIIO_CODE_COVERAGE)
    // For the sake of test time, reduce the default iterations for DEBUG,
    // CI, and code coverage builds. Explicit use of --iters or --trials
    // will override this, since it comes before the getargs() call.
    iterations /= 10;
    ntrials = 1;
#endif

    getargs(argc, argv);

    test_sRGB_conversion();
    test_Rec709_conversion();
    test_interop_id_grammar();
    test_interop_resolve();

    return unit_test_failures != 0;
}
