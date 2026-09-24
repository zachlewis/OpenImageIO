// Copyright Contributors to the OpenImageIO project.
// SPDX-License-Identifier: Apache-2.0
// https://github.com/AcademySoftwareFoundation/OpenImageIO

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

#include <OpenImageIO/Imath.h>
#include <OpenImageIO/argparse.h>
#include <OpenImageIO/benchmark.h>
#include <OpenImageIO/color.h>
#include <OpenImageIO/filesystem.h>
#include <OpenImageIO/hash.h>
#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/imagebufalgo.h>
#include <OpenImageIO/simd.h>
#include <OpenImageIO/strutil.h>
#include <OpenImageIO/tiffutils.h>
#include <OpenImageIO/timer.h>
#include <OpenImageIO/typedesc.h>
#include <OpenImageIO/unittest.h>

#include "imageio_pvt.h"


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
test_declared_color_space_info()
{
    ColorSpaceInfo invalid;
    OIIO_CHECK_FALSE(invalid.valid());
    OIIO_CHECK_ASSERT(invalid.chromaticities().empty());
    OIIO_CHECK_EQUAL(invalid.transfer_function_gamma(), 0.0f);
    if (!ColorConfig::supportsOpenColorIO())
        return;

    // Published IDs declared by name or alias, a linear encoding with no ID,
    // one contradicting its ID, and a data space.
    const std::string filename = Filesystem::temp_directory_path() + "/"
                                 + Filesystem::unique_path() + ".ocio";
    OIIO_CHECK_ASSERT(Filesystem::write_text_file(
        filename,
        "ocio_profile_version: 2.3\n"
        "roles: {default: ACEScg, scene_linear: ACEScg}\n"
        "file_rules:\n  - !<Rule> {name: Default, colorspace: default}\n"
        "colorspaces:\n"
        "  - !<ColorSpace>\n    name: ACEScg\n    encoding: scene-linear\n"
        "    aliases: [lin_ap1_scene]\n"
        "  - !<ColorSpace> {name: srgb_rec709_scene, encoding: sdr-video}\n"
        "  - !<ColorSpace> {name: Adobe, aliases: [g22_adobergb_scene]}\n"
        "  - !<ColorSpace> {name: Plain, encoding: scene-linear}\n"
        "  - !<ColorSpace>\n    name: Mislabeled\n    encoding: scene-linear\n"
        "    aliases: [g22_rec709_scene]\n"
        "  - !<ColorSpace> {name: Data, isdata: true}\n"));
    const float ap1[] = { .713f, .293f, .165f,   .83f,
                          .128f, .044f, .32168f, .33767f };
    ColorSpaceInfo saved;
    {
        ColorConfig config(filename);
        OIIO_CHECK_FALSE(config.has_error());
        saved = config.get_color_space_info("scene_linear");
        OIIO_CHECK_ASSERT(saved.valid());
        OIIO_CHECK_EQUAL(saved.transfer_function_gamma(), 1.0f);
        OIIO_CHECK_ASSERT(saved.chromaticities() == cspan<float>(ap1));
        auto srgb = config.get_color_space_info("srgb_rec709_scene");
        OIIO_CHECK_EQUAL(srgb.transfer_function_gamma(), 0.0f);
        OIIO_CHECK_EQUAL(srgb.chromaticities().size(), 8);
        OIIO_CHECK_EQUAL(
            config.derive_color_space_info("Adobe").transfer_function_gamma(),
            563.0f / 256.0f);
        for (auto name : { "Plain", "Mislabeled" }) {
            auto info = config.get_color_space_info(name);
            OIIO_CHECK_EQUAL(info.transfer_function_gamma(), 1.0f);
            OIIO_CHECK_ASSERT(info.chromaticities().empty());
        }
        auto data = config.get_color_space_info("Data");
        OIIO_CHECK_ASSERT(data.valid());
        OIIO_CHECK_ASSERT(data.chromaticities().empty());
        OIIO_CHECK_EQUAL(data.transfer_function_gamma(), 0.0f);
        OIIO_CHECK_FALSE(config.get_color_space_info("missing").valid());
    }
    // The properties outlive the config; a move leaves its source invalid.
    auto copy  = saved;
    auto moved = std::move(saved);
    OIIO_CHECK_FALSE(saved.valid());
    OIIO_CHECK_ASSERT(copy.chromaticities() == cspan<float>(ap1));
    OIIO_CHECK_EQUAL(moved.transfer_function_gamma(), 1.0f);
    Filesystem::remove(filename);

    // OpenColorIO 2.5 reads a declared interop_id, which outranks the name.
    if (ColorConfig::OpenColorIO_version_hex() >= 0x02050000) {
        OIIO_CHECK_ASSERT(Filesystem::write_text_file(
            filename,
            "ocio_profile_version: 2.5\n"
            "roles: {default: g18_rec709_scene}\n"
            "file_rules:\n  - !<Rule> {name: Default, colorspace: default}\n"
            "colorspaces:\n  - !<ColorSpace>\n    name: g18_rec709_scene\n"
            "    interop_id: g22_ap1_scene\n"));
        ColorConfig config(filename);
        OIIO_CHECK_FALSE(config.has_error());
        auto info = config.get_color_space_info("g18_rec709_scene");
        OIIO_CHECK_EQUAL(info.transfer_function_gamma(), 2.2f);
        OIIO_CHECK_ASSERT(info.chromaticities() == cspan<float>(ap1));
        Filesystem::remove(filename);
    }
}



static void
test_gamma_pair_conversion()
{
    // OCIO < 2.5 composes back-to-back exponents in the wrong direction under
    // its default optimization, giving v^(1.8/2.2) instead of v^(2.2/1.8).
    ColorConfig config("ocio://cg-config-v2.1.0_aces-v1.3_ocio-v2.3");
    auto proc = config.createColorProcessor("Gamma 2.2 Rec.709 - Texture",
                                            "Gamma 1.8 Rec.709 - Texture");
    OIIO_CHECK_ASSERT(proc);
    if (!proc)
        return;
    for (float v : { 0.001f, 0.18f, 0.5f }) {
        float rgb[3] = { v, v, v };
        proc->apply(rgb);
        OIIO_CHECK_EQUAL_THRESH(rgb[0], std::pow(v, 2.2f / 1.8f), 1.0e-4f);
    }
}



static void
test_interop_id_memo()
{
    if (!ColorConfig::supportsOpenColorIO())
        return;

    const char* test_config = OIIO_COLOR_TEST_CONFIG;

    // An older builtin with no native interop-ID declarations retains its
    // authored CIF alias answers without requiring other recognition work.
    {
        ColorConfig old_builtin("ocio://cg-config-v1.0.0_aces-v1.3_ocio-v2.1");
        OIIO_CHECK_EQUAL(old_builtin.get_color_interop_id("ACEScg"),
                         "lin_ap1_scene");
        OIIO_CHECK_EQUAL(old_builtin.get_color_interop_id("ACEScg"),
                         "lin_ap1_scene");
    }

    string_view saved;
    {
        ColorConfig first(test_config);
        OIIO_CHECK_FALSE(first.has_error());
        saved = first.get_color_interop_id("ACEScg");
        OIIO_CHECK_EQUAL(saved, "lin_ap1_scene");
    }
    OIIO_CHECK_EQUAL(saved, "lin_ap1_scene");

    constexpr int nthreads = 8;
    std::array<std::string, nthreads> results;
    std::array<std::unique_ptr<ColorConfig>, nthreads> configs;
    for (auto& concurrent : configs)
        concurrent.reset(new ColorConfig(test_config));
    for (const auto& concurrent : configs)
        OIIO_CHECK_FALSE(concurrent->has_error());
    OIIO_CHECK_ASSERT(configs[0]->getColorSpaceNames()
                      == configs[1]->getColorSpaceNames());
    OIIO_CHECK_EQUAL(configs[0]->resolve("scene_linear"),
                     "Linear Rec.709 (sRGB)");
    OIIO_CHECK_FALSE(configs[0]->isData("scene_linear"));

    // Race the first resolved-negative query, then repeat it warm. Gamma 2.6
    // Rec.709 is measurable but has no scene-referred reference counterpart,
    // so the sweep completes and the miss is what gets shared.
    const char* unmatched = "Gamma 2.6 Encoded Rec.709 (sRGB)";
    std::vector<std::thread> threads;
    for (int i = 0; i < nthreads; ++i) {
        threads.emplace_back([&, i] {
            results[i] = configs[i]->get_color_interop_id(unmatched);
        });
    }
    for (auto& thread : threads)
        thread.join();
    for (const auto& result : results)
        OIIO_CHECK_EQUAL(result, "");

    threads.clear();
    for (int i = 0; i < nthreads; ++i) {
        threads.emplace_back([&, i] {
            results[i] = configs[i]->get_color_interop_id(unmatched);
        });
    }
    for (auto& thread : threads)
        thread.join();
    for (const auto& result : results)
        OIIO_CHECK_EQUAL(result, "");

    // The same comparison identifies an undeclared log encoding the alias
    // table never listed, and the identity selects it back by name.
    OIIO_CHECK_EQUAL(configs[0]->get_color_interop_id("ACEScct"),
                     "ocio:acescct_ap1_scene");
    OIIO_CHECK_EQUAL(configs[0]->resolve("ocio:acescct_ap1_scene"), "ACEScct");
    OIIO_CHECK_ASSERT(configs[0]->equivalent("ACEScct", "acescct_ap1_scene"));

    OIIO_CHECK_EQUAL(configs[0]->get_color_interop_id("scene_linear"),
                     "lin_rec709_scene");

    // Growing the shared memo must not invalidate the first wrapper's view.
    ColorConfig config(test_config);
    OIIO_CHECK_FALSE(config.has_error());
    for (const auto& name : config.getColorSpaceNames())
        (void)config.get_color_interop_id(name);
    OIIO_CHECK_EQUAL(saved, "lin_ap1_scene");

    OIIO_CHECK_ASSERT(config.reset("ocio://default"));
    OIIO_CHECK_FALSE(config.has_error());
    OIIO_CHECK_EQUAL(config.get_color_interop_id("scene_linear"),
                     "lin_ap1_scene");
}



// The measured equality ID, as a derived snapshot reports it.
static std::string
equality_id(const ColorConfig& config, string_view colorspace,
            string_view context_key = "", string_view context_value = "")
{
    return std::string(ColorSpaceInfoAccess::equality_id(
        pvt::color_space_info(config, colorspace, true, context_key,
                              context_value)));
}



static void
test_recognized_id_equivalence()
{
    if (!ColorConfig::supportsOpenColorIO())
        return;
    const std::string filename = Filesystem::temp_directory_path() + "/"
                                 + Filesystem::unique_path() + ".ocio";
    const char* text           = R"(ocio_profile_version: 2.3
roles: {default: Reference, aces_interchange: Reference}
file_rules:
  - !<Rule> {name: Default, colorspace: Reference}
colorspaces:
  - !<ColorSpace> {name: Reference}
  - !<ColorSpace> {name: Twin}
  - !<ColorSpace>
    name: Near
    to_scene_reference: !<MatrixTransform> {matrix: [1.001, 0, 0, 0, 0, 1.001, 0, 0, 0, 0, 1.001, 0, 0, 0, 0, 1]}
)";
    OIIO_CHECK_ASSERT(Filesystem::write_text_file(filename, text));
    ColorConfig config(filename);
    OIIO_CHECK_FALSE(config.has_error());
    OIIO_CHECK_EQUAL(config.get_color_interop_id("Reference"), "lin_ap0_scene");
    OIIO_CHECK_EQUAL(config.get_color_interop_id("Near"), "lin_ap0_scene");
    OIIO_CHECK_FALSE(config.equivalent("Reference", "Near"));
    OIIO_CHECK_FALSE(config.equivalent("Near", "Reference"));
    OIIO_CHECK_ASSERT(config.equivalent("Reference", "Twin"));
    // Recognition is accepted within a response tolerance, so a gain too small
    // to move a probe is measured as the encoding it perturbs. Agreeing
    // measured identities nominate the pair and no more: the gain is real, the
    // conversion is not a no-op, and it is still performed.
    OIIO_CHECK_EQUAL(equality_id(config, "Reference"), "lin_ap0_scene");
    OIIO_CHECK_EQUAL(equality_id(config, "Near"), "lin_ap0_scene");
    OIIO_CHECK_FALSE(config.equivalent("Reference", "Near"));
    auto processor = config.createColorProcessor("Near", "Reference");
    OIIO_CHECK_ASSERT(processor);
    if (processor) {
        float pixel[3] = { 0.25f, 0.3f, 0.4f };
        processor->apply(pixel);
        OIIO_CHECK_EQUAL_THRESH(pixel[0], 0.25025f, 1.0e-7f);
        OIIO_CHECK_EQUAL_THRESH(pixel[1], 0.3003f, 1.0e-7f);
        OIIO_CHECK_EQUAL_THRESH(pixel[2], 0.4004f, 1.0e-7f);
    }
    Filesystem::remove(filename);
}



// A data space has no color to measure, so its own naming is the whole answer
// -- and that naming is read only after OpenColorIO has said the space is
// data, so a utility spelling on an ordinary color space asserts nothing.
// `bypass` and `data` can each be spelled once in a configuration, so the
// precedence rule is exercised by moving the one spelling that decides.
static void
test_measured_equality_id()
{
    if (!ColorConfig::supportsOpenColorIO())
        return;
    const std::string filename = Filesystem::temp_directory_path() + "/"
                                 + Filesystem::unique_path() + ".ocio";
    auto fixture = [&](string_view data_name, string_view data_aliases,
                       string_view utility_aliases) {
        std::string text
            = "ocio_profile_version: 2.3\n"
              "roles: {default: Reference, aces_interchange: Reference}\n"
              "file_rules:\n  - !<Rule> {name: Default, colorspace: Reference}\n"
              "colorspaces:\n"
              "  - !<ColorSpace>\n    name: Reference\n"
              "  - !<ColorSpace>\n    name: Utility\n";
        if (!utility_aliases.empty())
            text += "    aliases: [" + std::string(utility_aliases) + "]\n";
        text += "  - !<ColorSpace>\n    name: Unique\n"
                "    categories: [is-unique]\n"
                "  - !<ColorSpace>\n    name: "
                + std::string(data_name) + "\n    isdata: true\n";
        if (!data_aliases.empty())
            text += "    aliases: [" + std::string(data_aliases) + "]\n";
        OIIO_CHECK_ASSERT(Filesystem::write_text_file(filename, text));
    };

    fixture("Mask", "", "");
    {
        ColorConfig config(filename);
        OIIO_CHECK_FALSE(config.has_error());
        OIIO_CHECK_EQUAL(equality_id(config, "Mask"), "data");
        // A configuration that marks a space unique has said no shared
        // identity describes it, whatever its operations reproduce.
        OIIO_CHECK_EQUAL(equality_id(config, "Unique"), "");
        OIIO_CHECK_EQUAL(equality_id(config, "Reference"), "lin_ap0_scene");
        OIIO_CHECK_EQUAL(equality_id(config, "Utility"), "lin_ap0_scene");
        OIIO_CHECK_ASSERT(config.equivalent("Reference", "Utility"));
        // An identity that resolves to a local definition reports that
        // definition's measurement. One this configuration does not define
        // names no local definition, so no substituted reference is measured
        // for it.
        OIIO_CHECK_EQUAL(equality_id(config, "lin_ap0_scene"), "lin_ap0_scene");
        OIIO_CHECK_EQUAL(equality_id(config, "g22_rec709_scene"), "");
        OIIO_CHECK_EQUAL(equality_id(config, "not a color space"), "");
        OIIO_CHECK_EQUAL(equality_id(config, ""), "");
        // The two modes share one retained result map and cannot answer for
        // each other, warm or cold, in this wrapper or the next.
        const string_view interop = config.get_color_interop_id("Reference");
        OIIO_CHECK_EQUAL(equality_id(config, "Reference"), "lin_ap0_scene");
        OIIO_CHECK_EQUAL(config.get_color_interop_id("Reference"), interop);
        OIIO_CHECK_EQUAL(equality_id(config, "Mask"), "data");
        ColorConfig second(filename);
        OIIO_CHECK_FALSE(second.has_error());
        OIIO_CHECK_EQUAL(equality_id(second, "Utility"), "lin_ap0_scene");
        OIIO_CHECK_EQUAL(second.get_color_interop_id("Reference"), interop);
        OIIO_CHECK_EQUAL(equality_id(second, "Reference"), "lin_ap0_scene");
    }

    // Named `bypass` wins outright, without regard to case.
    fixture("bypass", "", "");
    {
        ColorConfig config(filename);
        OIIO_CHECK_FALSE(config.has_error());
        OIIO_CHECK_EQUAL(equality_id(config, "bypass"), "bypass");
        OIIO_CHECK_EQUAL(equality_id(config, "BYPASS"), "bypass");
    }
    // An alias of `bypass` wins only where no data identity is present.
    fixture("Mask", "bypass", "");
    {
        ColorConfig config(filename);
        OIIO_CHECK_FALSE(config.has_error());
        OIIO_CHECK_EQUAL(equality_id(config, "Mask"), "bypass");
        OIIO_CHECK_EQUAL(equality_id(config, "bypass"), "bypass");
    }
    fixture("data", "bypass", "");
    {
        ColorConfig config(filename);
        OIIO_CHECK_FALSE(config.has_error());
        OIIO_CHECK_EQUAL(equality_id(config, "data"), "data");
        OIIO_CHECK_EQUAL(equality_id(config, "bypass"), "data");
    }
    fixture("Mask", "bypass, data", "");
    {
        ColorConfig config(filename);
        OIIO_CHECK_FALSE(config.has_error());
        OIIO_CHECK_EQUAL(equality_id(config, "Mask"), "data");
    }
    // The same spelling on an ordinary color space claims no treatment: the
    // definition behind it is measured like any other.
    fixture("Mask", "", "bypass");
    {
        ColorConfig config(filename);
        OIIO_CHECK_FALSE(config.has_error());
        OIIO_CHECK_EQUAL(equality_id(config, "Utility"), "lin_ap0_scene");
        OIIO_CHECK_EQUAL(equality_id(config, "bypass"), "lin_ap0_scene");
        OIIO_CHECK_EQUAL(equality_id(config, "Mask"), "data");
    }
    Filesystem::remove(filename);
}



static void
test_declared_id_equivalence()
{
    if (!ColorConfig::supportsOpenColorIO()
        || ColorConfig::OpenColorIO_version_hex() < 0x02050000)
        return;

    const std::string filename = Filesystem::temp_directory_path() + "/"
                                 + Filesystem::unique_path() + ".ocio";
    const char* text           = R"(ocio_profile_version: 2.5
roles: {default: Actual, scene_linear: Actual, color_timing: Actual, compositing_log: Actual, aces_interchange: Actual}
file_rules:
  - !<Rule> {name: Default, colorspace: Actual}
displays:
  Test:
    - !<View> {name: Raw, colorspace: Actual}
colorspaces:
  - !<ColorSpace>
    name: Actual
    aliases: [lin_ap0_scene]
    interop_id: lin_ap0_scene
  - !<ColorSpace>
    name: Altered
    interop_id: lin_ap0_scene
    to_scene_reference: !<MatrixTransform> {matrix: [2, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]}
  - !<ColorSpace>
    name: Twin
  - !<ColorSpace>
    name: Renamed
    interop_id: my-studio:working
  - !<ColorSpace>
    name: FakeData
    interop_id: data
)";
    OIIO_CHECK_ASSERT(Filesystem::write_text_file(filename, text));
    ColorConfig config(filename);
    OIIO_CHECK_FALSE(config.has_error());
    OIIO_CHECK_EQUAL(config.get_color_interop_id("Actual"), "lin_ap0_scene");
    OIIO_CHECK_EQUAL(config.get_color_interop_id("Altered"), "lin_ap0_scene");
    OIIO_CHECK_EQUAL(config.get_color_interop_id("Twin"), "lin_ap0_scene");
    OIIO_CHECK_FALSE(config.equivalent("Actual", "Altered"));
    OIIO_CHECK_FALSE(config.equivalent("Altered", "Actual"));
    OIIO_CHECK_ASSERT(config.equivalent("Actual", "Twin"));

    // What the definitions measure as, with every declaration withheld. The
    // declaration a measurement contradicts is not withdrawn from the write
    // identity above: an author owns what is written to a file, and this owns
    // only what the arithmetic is.
    OIIO_CHECK_EQUAL(equality_id(config, "Actual"), "lin_ap0_scene");
    OIIO_CHECK_EQUAL(equality_id(config, "Twin"), "lin_ap0_scene");
    OIIO_CHECK_EQUAL(equality_id(config, "Altered"), "");
    OIIO_CHECK_EQUAL(config.get_color_interop_id("Altered"), "lin_ap0_scene");

    // An exact copy that declares a different identity. The two write
    // identities disagree and stay that way; the measured ones agree, so the
    // conversion OCIO itself reports as a no-op is no longer performed.
    OIIO_CHECK_EQUAL(config.get_color_interop_id("Renamed"),
                     "my-studio:working");
    OIIO_CHECK_EQUAL(config.resolve("my-studio:working"), "Renamed");
    // The cheap query selects by an authored ID, as resolve() does.
    auto authored = config.get_color_space_info("my-studio:working");
    OIIO_CHECK_ASSERT(authored.valid());
    OIIO_CHECK_EQUAL(ColorSpaceInfoAccess::color_interop_id(authored),
                     "my-studio:working");
    // CIF Rec 03 strips the query, never the declaration.
    OIIO_CHECK_EQUAL(config.resolve("working"), "working");
    OIIO_CHECK_FALSE(config.isData("my-studio:working"));
    OIIO_CHECK_EQUAL(equality_id(config, "Renamed"), "lin_ap0_scene");
    OIIO_CHECK_ASSERT(config.equivalent("Actual", "Renamed"));
    OIIO_CHECK_ASSERT(config.equivalent("Renamed", "Actual"));
    OIIO_CHECK_FALSE(config.equivalent("Altered", "Renamed"));
    // A utility identity declared on a space OpenColorIO does not call data
    // asserts no treatment. Data-ness is native, read before any naming, and
    // never inferred from an attribute.
    OIIO_CHECK_FALSE(config.isData("FakeData"));
    OIIO_CHECK_EQUAL(config.get_color_interop_id("FakeData"), "data");
    OIIO_CHECK_EQUAL(equality_id(config, "FakeData"), "lin_ap0_scene");

    // Measuring changes nothing about what is written.
    OIIO_CHECK_EQUAL(config.get_color_interop_id("Renamed"),
                     "my-studio:working");
    OIIO_CHECK_EQUAL(config.get_color_interop_id("Actual"), "lin_ap0_scene");

    auto processor = config.createColorProcessor("Actual", "Altered");
    OIIO_CHECK_ASSERT(processor);
    if (processor) {
        float pixel[3] = { 0.25f, 0.3f, 0.4f };
        processor->apply(pixel);
        OIIO_CHECK_EQUAL_THRESH(pixel[0], 0.125f, 1.0e-6f);
        OIIO_CHECK_EQUAL_THRESH(pixel[1], 0.3f, 1.0e-6f);
        OIIO_CHECK_EQUAL_THRESH(pixel[2], 0.4f, 1.0e-6f);
    }
    Filesystem::remove(filename);
}



static void
test_color_space_info()
{
    ColorSpaceInfo invalid;
    OIIO_CHECK_FALSE(invalid.valid());
    OIIO_CHECK_ASSERT(invalid.chromaticities().empty());
    OIIO_CHECK_EQUAL(invalid.transfer_function_gamma(), 0.0f);
    for (auto field : { ColorSpaceInfoField::Chromaticities,
                        ColorSpaceInfoField::TransferFunction }) {
        OIIO_CHECK_FALSE(ColorSpaceInfoAccess::computed(invalid, field));
        OIIO_CHECK_FALSE(ColorSpaceInfoAccess::available(invalid, field));
        OIIO_CHECK_FALSE(ColorSpaceInfoAccess::derived(invalid, field));
    }
    const auto invalid_field = static_cast<ColorSpaceInfoField>(255);
    OIIO_CHECK_FALSE(ColorSpaceInfoAccess::computed(invalid, invalid_field));
    OIIO_CHECK_FALSE(ColorSpaceInfoAccess::available(invalid, invalid_field));
    OIIO_CHECK_FALSE(ColorSpaceInfoAccess::derived(invalid, invalid_field));
    if (!ColorConfig::supportsOpenColorIO())
        return;
    // Writers ask about a missing oiio:ColorSpace attribute, whose default
    // string_view has no storage. It names no color space.
    const ColorConfig& default_config = ColorConfig::default_colorconfig();
    OIIO_CHECK_FALSE(
        default_config.get_color_space_info(string_view()).valid());
    OIIO_CHECK_FALSE(
        default_config.derive_color_space_info(string_view()).valid());
    const std::string filename = Filesystem::temp_directory_path() + "/"
                                 + Filesystem::unique_path() + ".ocio";
    OIIO_CHECK_ASSERT(Filesystem::write_text_file(
        filename,
        "ocio_profile_version: 2.3\n"
        "roles: {default: Working, scene_linear: Working}\n"
        "file_rules:\n  - !<Rule> {name: Default, colorspace: default}\n"
        "colorspaces:\n  - !<ColorSpace>\n    name: Working\n"
        "    encoding: scene-linear\n    aliases: [working_alias, lin_rec709]\n"
        "  - !<ColorSpace>\n    name: Data\n    isdata: true\n"
        "    encoding: scene-linear\n    aliases: [data_alias]\n"
        "  - !<ColorSpace> {name: Plain}\n"
        "  - !<ColorSpace> {name: srgb_tx}\n"
        "display_colorspaces:\n"
        "  - !<ColorSpace> {name: Screen, encoding: display-linear}\n"));
    ColorSpaceInfo saved;
    {
        ColorConfig config(filename);
        OIIO_CHECK_FALSE(config.has_error());
        saved = config.get_color_space_info("scene_linear");
        OIIO_CHECK_ASSERT(saved.valid());
        OIIO_CHECK_EQUAL(saved.transfer_function_gamma(), 1.0f);
        OIIO_CHECK_ASSERT(saved.chromaticities().empty());
        OIIO_CHECK_ASSERT(ColorSpaceInfoAccess::computed(
            saved, ColorSpaceInfoField::TransferFunction));
        OIIO_CHECK_ASSERT(ColorSpaceInfoAccess::available(
            saved, ColorSpaceInfoField::TransferFunction));
        OIIO_CHECK_FALSE(ColorSpaceInfoAccess::derived(
            saved, ColorSpaceInfoField::TransferFunction));
        OIIO_CHECK_FALSE(ColorSpaceInfoAccess::computed(
            saved, ColorSpaceInfoField::Chromaticities));
        OIIO_CHECK_FALSE(ColorSpaceInfoAccess::computed(saved, invalid_field));
        OIIO_CHECK_FALSE(ColorSpaceInfoAccess::available(saved, invalid_field));
        OIIO_CHECK_FALSE(ColorSpaceInfoAccess::derived(saved, invalid_field));
        OIIO_CHECK_EQUAL(config.get_color_space_info("working_alias")
                             .transfer_function_gamma(),
                         1.0f);
        // It takes the steps of resolve() that need no measurement: a
        // stripped namespace and a legacy name, but not a space that only
        // measurement could select for OIIO's generic "sRGB".
        for (auto name : { "acme:working_alias", "linear" })
            OIIO_CHECK_EQUAL(
                config.get_color_space_info(name).transfer_function_gamma(),
                1.0f);
        OIIO_CHECK_FALSE(config.get_color_space_info("sRGB").valid());
        // A linear encoding names its image state without an ID.
        OIIO_CHECK_EQUAL(ColorSpaceInfoAccess::image_state(saved), "scene");
        OIIO_CHECK_EQUAL(ColorSpaceInfoAccess::image_state(
                             config.get_color_space_info("Screen")),
                         "display");
        // Before derivation, an undeclared ID and state are not computed.
        const auto cheap = config.get_color_space_info("Plain");
        const auto plain = config.derive_color_space_info("Plain");
        for (auto field : { ColorSpaceInfoField::ColorInteropID,
                            ColorSpaceInfoField::ImageState }) {
            OIIO_CHECK_FALSE(ColorSpaceInfoAccess::computed(cheap, field));
            OIIO_CHECK_ASSERT(ColorSpaceInfoAccess::computed(plain, field));
            OIIO_CHECK_FALSE(ColorSpaceInfoAccess::available(plain, field));
        }
        auto data = config.derive_color_space_info("data_alias");
        OIIO_CHECK_ASSERT(data.valid());
        OIIO_CHECK_EQUAL(data.transfer_function_gamma(), 0.0f);
        OIIO_CHECK_ASSERT(data.chromaticities().empty());
        for (auto field : { ColorSpaceInfoField::Chromaticities,
                            ColorSpaceInfoField::TransferFunction }) {
            OIIO_CHECK_ASSERT(ColorSpaceInfoAccess::computed(data, field));
            OIIO_CHECK_FALSE(ColorSpaceInfoAccess::available(data, field));
            OIIO_CHECK_FALSE(ColorSpaceInfoAccess::derived(data, field));
        }
        OIIO_CHECK_FALSE(config.get_color_space_info("missing").valid());
        OIIO_CHECK_ASSERT(config.reset("ocio://default"));
        OIIO_CHECK_EQUAL(saved.transfer_function_gamma(), 1.0f);
    }
    auto copy  = saved;
    auto moved = std::move(saved);
    OIIO_CHECK_FALSE(saved.valid());
    OIIO_CHECK_EQUAL(copy.transfer_function_gamma(), 1.0f);
    OIIO_CHECK_EQUAL(moved.transfer_function_gamma(), 1.0f);
    invalid = std::move(moved);
    OIIO_CHECK_FALSE(moved.valid());
    OIIO_CHECK_EQUAL(invalid.transfer_function_gamma(), 1.0f);
    Filesystem::remove(filename);

    // Writers report gamma 1.0 for any linear transfer function.
    for (const char* name :
         { "lin_rec709_scene", "scene_linear", "lin_ap1_scene",
           "lin_p3d65_scene", "lin_rec2020_scene" }) {
        ImageSpec spec;
        spec.attribute("oiio:ColorSpace", name);
        OIIO_CHECK_EQUAL(pvt::get_colorspace_rec709_gamma(spec), 1.0f);
    }
    ImageSpec srgb;
    srgb.attribute("oiio:ColorSpace", "srgb_rec709_scene");
    OIIO_CHECK_EQUAL(pvt::get_colorspace_rec709_gamma(srgb), 0.0f);

    // Explicit gamma 1 is linear even beside non-Rec.709 primaries. Writers
    // use this when no portable ID establishes the transfer function.
    const float adobe[8] = { 0.640f, 0.330f, 0.210f,  0.710f,
                             0.150f, 0.060f, 0.3127f, 0.3290f };
    ImageSpec gamma_one;
    gamma_one.attribute("oiio:Gamma", 1.0f);
    gamma_one.attribute("chromaticities", TypeDesc(TypeDesc::FLOAT, 8), adobe);
    OIIO_CHECK_EQUAL(pvt::get_colorspace_rec709_gamma(gamma_one), 1.0f);

    // A name or ID that spells its Rec.709 gamma reports that gamma without
    // any further measurement, and with no oiio:Gamma attribute to fall back
    // on. This is what keeps a writer's tag when nothing else supplies one.
    for (auto&& [name, gamma] : { std::pair { "g18_rec709_scene", 1.8f },
                                  std::pair { "g22_rec709_display", 2.2f } }) {
        ImageSpec spelled;
        spelled.attribute("oiio:ColorSpace", name);
        OIIO_CHECK_EQUAL(pvt::get_colorspace_rec709_gamma(spelled), gamma);
    }
}



static void
test_color_space_info_concurrent()
{
    if (!ColorConfig::supportsOpenColorIO())
        return;
    const std::string filename = Filesystem::temp_directory_path() + "/"
                                 + Filesystem::unique_path() + ".ocio";
    // A separate config keeps these first derivations cold, independently of
    // the other tests. An arbitrary studio name requires analytic recognition.
    // The unrelated missing LUT must not prevent sharing this file-free result.
    OIIO_CHECK_ASSERT(Filesystem::write_text_file(
        filename,
        "ocio_profile_version: 2.3\n"
        "roles: {default: Unknown, aces_interchange: Unknown, "
        "cie_xyz_d65_interchange: XYZ}\n"
        "file_rules:\n  - !<Rule> {name: Default, colorspace: default}\n"
        "colorspaces:\n  - !<ColorSpace>\n    name: Unknown\n"
        "display_colorspaces:\n  - !<ColorSpace>\n    name: XYZ\n"
        "    encoding: display-linear\n"
        "  - !<ColorSpace>\n    name: StudioAdobe\n"
        "    encoding: sdr-video\n"
        "    from_display_reference: !<GroupTransform>\n"
        "      children:\n"
        "        - !<MatrixTransform> {matrix: [2.04158790381075, "
        "-0.56500697427886, -0.34473135077833, 0, -0.96924363628088, "
        "1.87596750150772, 0.0415550574071756, 0, 0.0134442806320311, "
        "-0.118362392231018, 1.01517499439121, 0, 0, 0, 0, 1]}\n"
        "        - !<ExponentTransform> {value: 2.19921875, style: mirror, "
        "direction: inverse}\n"
        "named_transforms:\n  - !<NamedTransform>\n    name: Unrelated\n"
        "    transform: !<FileTransform> {src: absent.cube}\n"));

    constexpr int nthreads = 8;
    std::array<ColorSpaceInfo, nthreads> positives, misses;
    {
        std::array<std::unique_ptr<ColorConfig>, nthreads> configs;
        std::atomic<int> ready { 0 };
        std::atomic<int> loaded { 0 };
        std::vector<std::thread> threads;
        for (int i = 0; i < nthreads; ++i) {
            threads.emplace_back([&, i] {
                ++ready;
                while (ready.load() != nthreads)
                    std::this_thread::yield();
                // Also race construction of the configs.
                configs[i].reset(new ColorConfig(filename));
                ++loaded;
                while (loaded.load() != nthreads)
                    std::this_thread::yield();
                // Opposite orders exercise concurrent publication of both keys.
                if (i % 2)
                    misses[i] = configs[i]->derive_color_space_info("XYZ");
                positives[i] = configs[i]->derive_color_space_info(
                    "StudioAdobe");
                if (!(i % 2))
                    misses[i] = configs[i]->derive_color_space_info("XYZ");
            });
        }
        for (auto& thread : threads)
            thread.join();
        for (const auto& config : configs)
            OIIO_CHECK_FALSE(config->has_error());
    }

    // All originating wrappers are gone; both kinds of snapshot must survive.
    const float expected[] = { .64f, .33f, .21f,   .71f,
                               .15f, .06f, .3127f, .3290f };
    auto check_positive    = [&](const ColorSpaceInfo& info) {
        OIIO_CHECK_ASSERT(info.valid());
        OIIO_CHECK_EQUAL(info.transfer_function_gamma(), 2.19921875f);
        auto xy = info.chromaticities();
        OIIO_CHECK_EQUAL(xy.size(), 8);
        if (xy.size() == 8)
            for (int j = 0; j < 8; ++j)
                OIIO_CHECK_EQUAL_THRESH(xy[j], expected[j], 1e-6f);
    };
    auto check_miss = [&](const ColorSpaceInfo& info) {
        OIIO_CHECK_ASSERT(info.valid());
        // Display-reference XYZ has a linear transfer but no RGB primaries.
        OIIO_CHECK_EQUAL(info.transfer_function_gamma(), 1.0f);
        OIIO_CHECK_ASSERT(info.chromaticities().empty());
    };
    for (int i = 0; i < nthreads; ++i) {
        check_positive(positives[i]);
        check_miss(misses[i]);
    }
    // A new wrapper's cheap query sees the process-shared derived properties.
    ColorConfig another(filename);
    OIIO_CHECK_FALSE(another.has_error());
    check_positive(another.get_color_space_info("StudioAdobe"));
    check_miss(another.derive_color_space_info("XYZ"));
    Filesystem::remove(filename);
}



static void
test_numeric_recognition()
{
    if (!ColorConfig::supportsOpenColorIO())
        return;
    const std::string filename = Filesystem::temp_directory_path() + "/"
                                 + Filesystem::unique_path() + ".ocio";
    // Same authored Adobe RGB matrix used by the concurrent snapshot test.
    // Transfers vary independently; none of these studio names supplies an ID.
    const std::string matrix
        = "!<MatrixTransform> {matrix: [2.04158790381075, -0.56500697427886, "
          "-0.34473135077833, 0, -0.96924363628088, 1.87596750150772, "
          "0.0415550574071756, 0, 0.0134442806320311, -0.118362392231018, "
          "1.01517499439121, 0, 0, 0, 0, 1]}";
    std::string text
        = "ocio_profile_version: 2.3\n"
          "roles: {default: Scene, aces_interchange: Scene, "
          "cie_xyz_d65_interchange: XYZ}\n"
          "file_rules:\n  - !<Rule> {name: Default, colorspace: default}\n"
          "colorspaces:\n  - !<ColorSpace> {name: Scene}\n"
          "display_colorspaces:\n  - !<ColorSpace> {name: XYZ}\n";
    auto power = [](string_view value) {
        return std::string("!<ExponentTransform> {value: ") + std::string(value)
               + ", style: mirror, direction: inverse}";
    };
    auto add = [&](string_view name, const std::string& transfer,
                   const std::string& extra = "", bool nonreciprocal = false) {
        text += "  - !<ColorSpace>\n    name: " + std::string(name)
                + "\n    from_display_reference: !<GroupTransform>\n"
                  "      children:\n        - "
                + matrix + "\n";
        if (!extra.empty())
            text += "        - " + extra + "\n";
        if (!transfer.empty())
            text += "        - " + transfer + "\n";
        if (nonreciprocal)
            text += "    to_display_reference: !<MatrixTransform> {}\n";
    };
    add("CustomPower", power("[2.35, 2.35, 2.35, 1]"));
    add("NominalAdobe", power("[2.2, 2.2, 2.2, 1]"));
    add("ClampPower", "!<ExponentTransform> {value: 2.35, direction: inverse}");
    add("ExactAdobe", power("[2.19921875, 2.19921875, 2.19921875, 1]"));
    add("LinearAdobe", "");
    add("ToeAdobe",
        "!<ExponentWithLinearTransform> {gamma: [2.4, 2.4, 2.4, 1], "
        "offset: [0.055, 0.055, 0.055, 0], direction: inverse}");
    add("LogAdobe", "!<LogTransform> {base: 2}");
    add("LogAffineAdobe",
        "!<LogAffineTransform> {base: 2, log_side_slope: 0.5, "
        "log_side_offset: 0.1, lin_side_slope: 1, "
        "lin_side_offset: 0.01}");
    add("LogCameraAdobe",
        "!<LogCameraTransform> {base: 2, lin_side_break: 0.01}");
    add("UnequalLog", "!<LogAffineTransform> {base: 2, "
                      "log_side_slope: [0.5, 0.6, 0.5]}");
    // A linear RGB mixing matrix preserves unit white but changes the gamut.
    add("NovelGamut", power("[2.35, 2.35, 2.35, 1]"),
        "!<MatrixTransform> {matrix: [0.9, 0.1, 0, 0, 0, 1, 0, 0, "
        "0, 0, 1, 0, 0, 0, 0, 1]}");
    add("Gain", power("[2.35, 2.35, 2.35, 1]"),
        "!<MatrixTransform> {matrix: [2, 0, 0, 0, 0, 2, 0, 0, "
        "0, 0, 2, 0, 0, 0, 0, 1]}");
    add("ChangedWhite", power("[2.35, 2.35, 2.35, 1]"),
        "!<MatrixTransform> {matrix: [1.1, 0, 0, 0, 0, 1, 0, 0, "
        "0, 0, 0.9, 0, 0, 0, 0, 1]}");
    add("Offset", power("[2.35, 2.35, 2.35, 1]"),
        "!<MatrixTransform> {offset: [0.1, 0, 0, 0]}");
    add("UnequalPower", power("[2.35, 2.2, 2.35, 1]"));
    add("ChangedAlpha", power("[2.35, 2.35, 2.35, 2]"));
    // Forward, this is the reference's Adobe RGB exactly, so every comparison
    // that reads one direction reproduces it. Its independently authored
    // reverse says the space is the reference itself. The two cannot both be
    // true, so no encoding is named and neither the exponent nor the primaries
    // may be read off either direction.
    add("Nonreciprocal", power("[2.19921875, 2.19921875, 2.19921875, 1]"), "",
        true);
    // Curve alone, no gamut matrix: what the probe reads along the neutral
    // axis is the curve itself, so a published family either names it or none
    // does. The first is the sRGB curve exactly; the second is a toe nothing
    // publishes. A name is not evidence, so the third is spelled sRGB and
    // implements a 2.35 power. The fourth has no color to measure.
    auto plain = [&](string_view name, const std::string& transfer) {
        text += "  - !<ColorSpace>\n    name: " + std::string(name)
                + "\n    from_display_reference: " + transfer + "\n";
    };
    plain("PlainSRGB",
          "!<ExponentWithLinearTransform> {gamma: [2.4, 2.4, 2.4, 1], "
          "offset: [0.055, 0.055, 0.055, 0], direction: inverse}");
    plain("PlainOddToe", "!<ExponentWithLinearTransform> {gamma: [3, 3, 3, 1], "
                         "offset: [0.2, 0.2, 0.2, 0], direction: inverse}");
    add("MislabeledSRGB", power("[2.35, 2.35, 2.35, 1]"));
    text = Strutil::replace(text, "    name: MislabeledSRGB\n",
                            "    name: MislabeledSRGB\n"
                            "    aliases: [srgb_rec709_display]\n");
    text += "  - !<ColorSpace>\n    name: Bypass\n    isdata: true\n";
    OIIO_CHECK_ASSERT(Filesystem::write_text_file(filename, text));
    const float expected_xy[] = { .64f, .33f, .21f,   .71f,
                                  .15f, .06f, .3127f, .329f };
    auto check = [&](const ColorSpaceInfo& info, float gamma, bool has_xy) {
        OIIO_CHECK_ASSERT(info.valid());
        OIIO_CHECK_EQUAL(info.transfer_function_gamma(), gamma);
        auto xy = info.chromaticities();
        OIIO_CHECK_EQUAL(xy.size(), has_xy ? 8 : 0);
        if (has_xy && xy.size() == 8)
            for (int i = 0; i < 8; ++i)
                OIIO_CHECK_EQUAL_THRESH(xy[i], expected_xy[i], 1e-6f);
    };
    ColorSpaceInfo cold, saved, gamut_only;
    {
        ColorConfig config(filename);
        OIIO_CHECK_FALSE(config.has_error());
        cold = config.get_color_space_info("CustomPower");
        check(cold, 0.0f, false);
        OIIO_CHECK_FALSE(ColorSpaceInfoAccess::computed(
            cold, ColorSpaceInfoField::Chromaticities));
        OIIO_CHECK_FALSE(ColorSpaceInfoAccess::computed(
            cold, ColorSpaceInfoField::TransferFunction));
        saved = config.derive_color_space_info("CustomPower");
        check(saved, 2.35f, true);
        for (auto field : { ColorSpaceInfoField::Chromaticities,
                            ColorSpaceInfoField::TransferFunction }) {
            OIIO_CHECK_ASSERT(ColorSpaceInfoAccess::computed(saved, field));
            OIIO_CHECK_ASSERT(ColorSpaceInfoAccess::available(saved, field));
            OIIO_CHECK_ASSERT(ColorSpaceInfoAccess::derived(saved, field));
            OIIO_CHECK_FALSE(ColorSpaceInfoAccess::computed(cold, field));
        }
        check(config.get_color_space_info("CustomPower"), 2.35f, true);
        check(config.derive_color_space_info("NominalAdobe"), 2.2f, true);
        check(config.derive_color_space_info("ClampPower"), 2.35f, true);
        check(config.derive_color_space_info("ExactAdobe"), 2.19921875f, true);
        OIIO_CHECK_ASSERT(config.get_color_interop_id("NominalAdobe")
                          != "g22_adobergb_display");
        OIIO_CHECK_ASSERT(config.get_color_interop_id("NominalAdobe")
                          != "g22_adobergb_scene");
        check(config.derive_color_space_info("LinearAdobe"), 1.0f, true);
        gamut_only = config.derive_color_space_info("ToeAdobe");
        check(gamut_only, 0.0f, true);
        for (const char* name :
             { "LogAdobe", "LogAffineAdobe", "LogCameraAdobe" })
            check(config.derive_color_space_info(name), 0.0f, true);
        check(config.derive_color_space_info("NovelGamut"), 2.35f, false);
        const ColorSpaceInfo completed_negative
            = config.derive_color_space_info("NovelGamut");
        OIIO_CHECK_ASSERT(ColorSpaceInfoAccess::computed(
            completed_negative, ColorSpaceInfoField::Chromaticities));
        OIIO_CHECK_FALSE(ColorSpaceInfoAccess::available(
            completed_negative, ColorSpaceInfoField::Chromaticities));
        OIIO_CHECK_ASSERT(
            ColorSpaceInfoAccess::derived(completed_negative,
                                          ColorSpaceInfoField::Chromaticities));
        // A uniform gain is a statement about luminance, not about a gamut:
        // every chromaticity is a ratio and none of them moves, so the
        // primaries are still established. An unequal one moves the white and
        // a mixing matrix moves the primaries, and neither is reported.
        check(config.derive_color_space_info("Gain"), 2.35f, true);
        check(config.derive_color_space_info("ChangedWhite"), 2.35f, false);
        for (const char* name : { "Offset", "UnequalPower", "ChangedAlpha",
                                  "Nonreciprocal", "UnequalLog" })
            check(config.derive_color_space_info(name), 0.0f, false);
        OIIO_CHECK_ASSERT(config.get_color_interop_id("Nonreciprocal").empty());
        // Two authored directions that cannot both be true rule out every
        // reference encoding at once, so the measured query has nothing to
        // report either. Empty is "nothing reproduced this", not "unique".
        OIIO_CHECK_ASSERT(equality_id(config, "Nonreciprocal").empty());

        // What kind of curve was established, which is a separate question
        // from the exponent and is answered from measurement alone.
        using Kind = ColorTransferFunctionKind;
        static_assert(int(Kind::Transform) == int(Kind::Named) + 1);
        static_assert(int(Kind::Unrecognized) == int(Kind::Transform) + 1);
        auto kind = [](const ColorSpaceInfo& info) {
            return int(ColorSpaceInfoAccess::transfer_function_kind(info));
        };
        // The neutral-axis measurement is valid even though the three channel
        // exponents differ. Exact extraction is what then establishes that no
        // one shared RGB transfer function represents the definition.
        OIIO_CHECK_EQUAL(kind(config.derive_color_space_info("UnequalPower")),
                         int(Kind::Unrecognized));
        // The cheap query begins no measurement, so it has no family to
        // report until an explicit derivation has established one.
        const ColorSpaceInfo cold_curve = config.get_color_space_info(
            "PlainSRGB");
        OIIO_CHECK_EQUAL(kind(cold_curve), int(Kind::Undetermined));
        OIIO_CHECK_ASSERT(
            ColorSpaceInfoAccess::transfer_function_name(cold_curve).empty());
        // A piecewise sRGB curve is named by the family it measures as, and
        // no exponent is invented to describe it.
        const ColorSpaceInfo curve = config.derive_color_space_info(
            "PlainSRGB");
        OIIO_CHECK_EQUAL(kind(curve), int(Kind::Named));
        OIIO_CHECK_EQUAL(ColorSpaceInfoAccess::transfer_function_name(curve),
                         "srgb");
        OIIO_CHECK_EQUAL(curve.transfer_function_gamma(), 0.0f);
        // The snapshot handed out before is not strengthened behind the
        // caller's back; the cheap query afterwards reports what was retained.
        OIIO_CHECK_EQUAL(kind(cold_curve), int(Kind::Undetermined));
        OIIO_CHECK_EQUAL(ColorSpaceInfoAccess::transfer_function_name(
                             config.get_color_space_info("PlainSRGB")),
                         "srgb");
        // An established exponent answers first, and a name is never evidence
        // about a curve: this space is spelled sRGB and implements a power.
        const ColorSpaceInfo mislabeled = config.derive_color_space_info(
            "MislabeledSRGB");
        OIIO_CHECK_EQUAL(kind(mislabeled), int(Kind::Power));
        OIIO_CHECK_EQUAL(mislabeled.transfer_function_gamma(), 2.35f);
        OIIO_CHECK_ASSERT(
            ColorSpaceInfoAccess::transfer_function_name(mislabeled).empty());
        OIIO_CHECK_EQUAL(kind(config.derive_color_space_info("CustomPower")),
                         int(Kind::Power));
        OIIO_CHECK_EQUAL(kind(config.derive_color_space_info("LinearAdobe")),
                         int(Kind::Linear));
        // A measurement no published family describes is still an exact
        // native transfer when its operations separate into one RGB curve.
        const ColorSpaceInfo odd = config.derive_color_space_info(
            "PlainOddToe");
        OIIO_CHECK_EQUAL(kind(odd), int(Kind::Transform));
        OIIO_CHECK_ASSERT(
            ColorSpaceInfoAccess::transfer_function_name(odd).empty());
        // A log curve is never approximated by a power. Whether a published
        // family names one of these depends on the reference vocabulary; that
        // none of them is linear or a power does not.
        for (const char* name :
             { "LogAdobe", "LogAffineAdobe", "LogCameraAdobe" }) {
            const ColorSpaceInfo log = config.derive_color_space_info(name);
            OIIO_CHECK_ASSERT(kind(log) == int(Kind::Named)
                              || kind(log) == int(Kind::Transform));
            OIIO_CHECK_EQUAL(log.transfer_function_gamma(), 0.0f);
        }
        // Nothing to measure, and nothing to resolve.
        OIIO_CHECK_EQUAL(kind(config.derive_color_space_info("Bypass")),
                         int(Kind::Undetermined));
        OIIO_CHECK_EQUAL(kind(config.derive_color_space_info("NotASpace")),
                         int(Kind::Undetermined));
    }
    // Old empty and derived handles survive publication and wrapper lifetime.
    check(cold, 0.0f, false);
    check(saved, 2.35f, true);
    check(gamut_only, 0.0f, true);
    ColorConfig another(filename);
    OIIO_CHECK_FALSE(another.has_error());
    check(another.get_color_space_info("CustomPower"), 2.35f, true);
    check(another.get_color_space_info("ToeAdobe"), 0.0f, true);
    // A second wrapper's cheap query sees the family the first established.
    OIIO_CHECK_EQUAL(ColorSpaceInfoAccess::transfer_function_name(
                         another.get_color_space_info("PlainSRGB")),
                     "srgb");
    check(another.derive_color_space_info("NovelGamut"), 2.35f, false);
    Filesystem::remove(filename);
}



// Scene-referred gamut derivation and log recognition. Every matrix and every
// curve parameter below is copied verbatim from the built-in interop-identities
// config, so a difference in a result is a difference in recognition rather
// than in the fixture. That config reaches each of these gamuts by one
// chromatic adaptation; the differently adapted case, which needs primaries
// re-adapted numerically, lives in the Python properties test.
static void
test_gamut_recognition()
{
    if (!ColorConfig::supportsOpenColorIO())
        return;
    const std::string filename = Filesystem::temp_directory_path() + "/"
                                 + Filesystem::unique_path() + ".ocio";
    const std::string bmdwg5
        = "!<MatrixTransform> {matrix: [0.647091325580708, "
          "0.242595385134207, 0.110313289285085, 0, 0.0651915997328519, "
          "1.02504756760476, -0.0902391673376125, 0, -0.0275570729194699, "
          "-0.0805887097177784, 1.10814578263725, 0, 0, 0, 0, 1]}";
    const std::string rec2020
        = "!<MatrixTransform> {matrix: [0.679085634706912, "
          "0.157700914643159, 0.163213450649929, 0, 0.0460020030800595, "
          "0.859054673002908, 0.0949433239170327, 0, -0.000573943187616196, "
          "0.0284677684080264, 0.97210617477959, 0, 0, 0, 0, 1]}";
    const std::string awg3
        = "!<MatrixTransform> {matrix: [0.680205505106279, "
          "0.236136601606481, 0.0836578932872398, 0, 0.0854149797421404, "
          "1.01747087860704, -0.102885858349182, 0, 0.00205652166929683, "
          "-0.0625625003847921, 1.06050597871549, 0, 0, 0, 0, 1]}";
    const std::string bmdfilm5
        = "!<LogCameraTransform> {base: 2.71828182845905, log_side_slope: "
          "0.0869287606549122, log_side_offset: 0.530013339229194, "
          "lin_side_offset: 0.00549407243225781, lin_side_break: 0.005, "
          "direction: inverse}";
    const std::string davinci
        = "!<LogCameraTransform> {log_side_slope: 0.07329248, log_side_offset: "
          "0.51304736, lin_side_offset: 0.0075, lin_side_break: 0.00262409, "
          "linear_slope: 10.44426855, direction: inverse}";
    const std::string logc3
        = "!<LogCameraTransform> {base: 10, log_side_slope: "
          "0.247189638318671, log_side_offset: 0.385536998692443, "
          "lin_side_slope: 5.55555555555556, lin_side_offset: "
          "0.0522722750251688, lin_side_break: 0.0105909904954696, "
          "direction: inverse}";
    std::string text
        = "ocio_profile_version: 2.3\n"
          "roles: {default: ACES, aces_interchange: ACES}\n"
          "file_rules:\n  - !<Rule> {name: Default, colorspace: default}\n"
          "colorspaces:\n  - !<ColorSpace>\n    name: ACES\n"
          "    encoding: scene-linear\n";
    auto add = [&](string_view name, const std::string& curve,
                   const std::string& matrix) {
        text += "  - !<ColorSpace>\n    name: " + std::string(name)
                + "\n    encoding: " + (curve.empty() ? "scene-linear" : "log")
                + "\n";
        if (curve.empty()) {
            text += "    to_scene_reference: " + matrix + "\n";
            return;
        }
        // Decode order: the curve linearizes, then the matrix reaches AP0.
        text += "    to_scene_reference: !<GroupTransform>\n"
                "      children:\n        - "
                + curve + "\n        - " + matrix + "\n";
    };
    // The reference's own curve with its linear segment removed. Every shared
    // parameter still agrees; the two disagree only below the break.
    const std::string bmdfilm5_no_break
        = "!<LogAffineTransform> {base: 2.71828182845905, log_side_slope: "
          "0.0869287606549122, log_side_offset: 0.530013339229194, "
          "lin_side_offset: 0.00549407243225781, direction: inverse}";
    add("CameraLinear", "", bmdwg5);
    add("AP1Linear", "", "!<BuiltinTransform> {style: ACEScg_to_ACES2065-1}");
    add("CameraLog", bmdfilm5, bmdwg5);
    add("WrongGamutSameCurve", bmdfilm5, rec2020);
    add("WrongCurveSameGamut", davinci, bmdwg5);
    add("NoLinearSegment", bmdfilm5_no_break, bmdwg5);
    add("UnpublishedGamut", logc3, awg3);
    OIIO_CHECK_ASSERT(Filesystem::write_text_file(filename, text));

    const float bmdwg5_xy[]  = { .7177215f, .3171181f,  .228041f, .861569f,
                                 .1005841f, -.0820452f, .312717f, .3290312f };
    const float rec2020_xy[] = { .708f, .292f, .170f,  .797f,
                                 .131f, .046f, .3127f, .3290f };
    const float ap1_xy[]     = { .713f, .293f, .165f,   .830f,
                                 .128f, .044f, .32168f, .33767f };
    auto check_xy = [&](const ColorSpaceInfo& info, cspan<float> expected) {
        auto xy = info.chromaticities();
        OIIO_CHECK_EQUAL(xy.size(), expected.size());
        if (xy.size() == expected.size())
            for (size_t i = 0; i < expected.size(); ++i)
                OIIO_CHECK_EQUAL_THRESH(xy[i], expected[i], 1e-6f);
    };

    ColorConfig config(filename);
    OIIO_CHECK_FALSE(config.has_error());

    // A camera gamut with no reference transfer counterpart still reports its
    // published primaries, which the composed-matrix comparison alone cannot.
    auto camera_linear = config.derive_color_space_info("CameraLinear");
    OIIO_CHECK_ASSERT(camera_linear.valid());
    OIIO_CHECK_EQUAL(camera_linear.transfer_function_gamma(), 1.0f);
    check_xy(camera_linear, bmdwg5_xy);
    auto ap1 = config.derive_color_space_info("AP1Linear");
    OIIO_CHECK_EQUAL(ap1.transfer_function_gamma(), 1.0f);
    check_xy(ap1, ap1_xy);

    // Curve and gamut together identify the encoding, and the gamut remains a
    // usable partial fact whichever half fails.
    auto camera_log = config.derive_color_space_info("CameraLog");
    OIIO_CHECK_EQUAL(camera_log.transfer_function_gamma(), 0.0f);
    check_xy(camera_log, bmdwg5_xy);
    OIIO_CHECK_EQUAL(config.get_color_interop_id("CameraLog"),
                     "ocio:bmdfilm5_wg5_scene");

    // A matching curve on different primaries is not the encoding.
    auto wrong_gamut = config.derive_color_space_info("WrongGamutSameCurve");
    check_xy(wrong_gamut, rec2020_xy);
    OIIO_CHECK_EQUAL(config.get_color_interop_id("WrongGamutSameCurve"), "");

    // Matching primaries under a different curve are not the encoding either.
    auto wrong_curve = config.derive_color_space_info("WrongCurveSameGamut");
    check_xy(wrong_curve, bmdwg5_xy);
    OIIO_CHECK_EQUAL(config.get_color_interop_id("WrongCurveSameGamut"), "");

    // Agreement on every shared parameter is not agreement on the curve when
    // the behavior below the break differs. Numeric equivalence over the
    // nominal range does not establish equivalence outside it.
    auto no_break = config.derive_color_space_info("NoLinearSegment");
    check_xy(no_break, bmdwg5_xy);
    OIIO_CHECK_EQUAL(config.get_color_interop_id("NoLinearSegment"), "");

    // A gamut the published library does not hold is reconstructed from the
    // composed matrix rather than going unreported. The matrix reaches ACES
    // AP0, whose white and primaries are known, so un-adapting it recovers
    // the primaries the definition was built from -- here ARRI Wide Gamut 3,
    // to the digits ARRI publishes. The reconstruction is a reading of the
    // matrix, not of the name: nothing below consults either.
    const float awg3_xy[] = { .684f,  .313f,  .221f,  .848f,
                              .0861f, -.102f, .3127f, .3290f };
    auto unpublished      = config.derive_color_space_info("UnpublishedGamut");
    OIIO_CHECK_ASSERT(unpublished.valid());
    check_xy(unpublished, awg3_xy);
    Filesystem::remove(filename);
}



// What a color space is named and what it does are separate questions, and
// production configurations routinely disagree about them: a space called
// "sRGB" that is a pure 2.2 power, a space aliased for a display encoding that
// the configuration authors scene-referred, a state-less alias on a space the
// configuration places in its display section. Every fixture below is a
// reduction of one of those, and every one of them is a case where reading the
// name alone produces an identity the definition does not implement.
//
// The fixtures also differ from the built-in config in how their transfer
// functions treat negative input -- they clamp, where that config mirrors or
// passes through -- which is the ordinary case and which the operation-level
// comparison cannot see past. Recognition here comes from the measured
// comparison, whose probes stay inside the encoded gamut for that reason.
static void
test_naming_versus_measurement()
{
    if (!ColorConfig::supportsOpenColorIO())
        return;
    const std::string filename = Filesystem::temp_directory_path() + "/"
                                 + Filesystem::unique_path() + ".ocio";
    // Matrices copied verbatim from the built-in config, so a
    // difference in a result is a difference in recognition, not in a fixture.
    const std::string rec709_to_ap0
        = "!<MatrixTransform> {matrix: [0.439632981919491, "
          "0.382988698151554, 0.177378319928955, 0, 0.0897764429588424, "
          "0.813439428748981, 0.0967841282921771, 0, 0.0175411703831727, "
          "0.111546553302387, 0.87091227631444, 0, 0, 0, 0, 1]}";
    const std::string p3d65_to_ap0
        = "!<MatrixTransform> {matrix: [0.518933487597981, 0.28625658638669, "
          "0.194809926015329, 0, 0.0738593830470598, 0.819845163936986, "
          "0.106295453015954, 0, -0.000307011368446647, 0.0438070502536223, "
          "0.956499961114824, 0, 0, 0, 0, 1]}";
    const std::string xyz_to_rec709
        = "!<MatrixTransform> {matrix: [3.24096994190452, -1.53738317757009, "
          "-0.498610760293003, 0, -0.96924363628088, 1.87596750150772, "
          "0.0415550574071756, 0, 0.0556300796969936, -0.203976958888976, "
          "1.05697151424288, 0, 0, 0, 0, 1]}";
    const std::string xyz_to_p3d65
        = "!<MatrixTransform> {matrix: [2.49349691194143, -0.931383617919124, "
          "-0.402710784450717, 0, -0.829488969561575, 1.76266406031835, "
          "0.0236246858419436, 0, 0.0358458302437845, -0.0761723892680418, "
          "0.956884524007688, 0, 0, 0, 0, 1]}";
    const std::string xyz_to_rec2020
        = "!<MatrixTransform> {matrix: [1.71665118797127, -0.355670783776392, "
          "-0.25336628137366, 0, -0.666684351832489, 1.61648123663494, "
          "0.0157685458139111, 0, 0.0176398574453108, -0.0427706132578085, "
          "0.942103121235474, 0, 0, 0, 0, 1]}";
    const std::string xyz_to_p3dci
        = "!<MatrixTransform> {matrix: [2.690225911625597, "
          "-1.094001937366136, -0.4250823476747523, 0, -0.820082184273491, "
          "1.750480908292057, 0.02660195421220572, 0, 0.03624575465400463, "
          "-0.07858083680558862, 0.9587469936609856, 0, 0, 0, 0, 1]}";

    std::string text
        = "ocio_profile_version: 2.3\n"
          "roles: {default: ACES, aces_interchange: ACES, "
          "cie_xyz_d65_interchange: XYZ}\n"
          "file_rules:\n  - !<Rule> {name: Default, colorspace: default}\n"
          "colorspaces:\n"
          "  - !<ColorSpace>\n    name: ACES\n    encoding: scene-linear\n";
    // Decode order for a scene space: the curve linearizes, the matrix reaches
    // AP0. Encode order for a display space: the matrix leaves CIE XYZ, the
    // curve encodes.
    auto scene = [&](string_view name, string_view alias, string_view curve,
                     const std::string& matrix) {
        text += "  - !<ColorSpace>\n    name: " + std::string(name) + "\n";
        if (!alias.empty())
            text += "    aliases: [" + std::string(alias) + "]\n";
        text += "    encoding: sdr-video\n"
                "    to_scene_reference: !<GroupTransform>\n"
                "      children:\n        - "
                + std::string(curve) + "\n        - " + matrix + "\n";
    };
    scene("Pure22", "srgb_texture", "!<ExponentTransform> {value: 2.2}",
          rec709_to_ap0);
    scene("Rec1886Scene", "rec1886_rec709_display",
          "!<ExponentTransform> {value: 2.4}", rec709_to_ap0);
    scene("WideGamutPure22", "p3d65_display",
          "!<ExponentTransform> {value: 2.2}", p3d65_to_ap0);
    // The same 2.2 power, declaring itself linear. The declared transfer is a
    // label like any other, and the identity query refuses to publish an
    // encoding that contradicts it rather than publishing the contradiction.
    text += "  - !<ColorSpace>\n    name: FalseLinear\n"
            "    encoding: scene-linear\n"
            "    to_scene_reference: !<GroupTransform>\n      children:\n"
            "        - !<ExponentTransform> {value: 2.2}\n        - "
            + rec709_to_ap0 + "\n";
    // A declared linear encoding whose definition is a curve no exponent
    // describes. Nothing measured can replace the declaration here, so the
    // honored 1.0 is the only answer there is.
    text += "  - !<ColorSpace>\n    name: FalseLinearSRGB\n"
            "    encoding: scene-linear\n"
            "    to_scene_reference: !<GroupTransform>\n      children:\n"
            "        - !<ExponentWithLinearTransform> {gamma: 2.4, "
            "offset: 0.055, direction: inverse}\n        - "
            + rec709_to_ap0 + "\n";

    text += "display_colorspaces:\n"
            "  - !<ColorSpace>\n    name: XYZ\n"
            "    aliases: [cie_xyz_d65]\n    encoding: display-linear\n";
    auto display = [&](string_view name, string_view alias,
                       string_view encoding, const std::string& matrix,
                       string_view curve) {
        text += "  - !<ColorSpace>\n    name: " + std::string(name) + "\n";
        if (!alias.empty())
            text += "    aliases: [" + std::string(alias) + "]\n";
        text += "    encoding: " + std::string(encoding)
                + "\n    from_display_reference: !<GroupTransform>\n"
                  "      children:\n        - "
                + matrix + "\n        - " + std::string(curve) + "\n";
    };
    display("AppleDisplay", "srgb_p3d65", "sdr-video", xyz_to_p3d65,
            "!<ExponentWithLinearTransform> {gamma: 2.4, offset: 0.055, "
            "direction: inverse}");
    display("Gamma22Rec709", "", "sdr-video", xyz_to_rec709,
            "!<ExponentTransform> {value: 2.2, direction: inverse}");
    display("Rec1886Rec2020", "", "sdr-video", xyz_to_rec2020,
            "!<ExponentTransform> {value: 2.4, direction: inverse}");
    display("CinemaP3", "", "sdr-cinema", xyz_to_p3dci,
            "!<ExponentTransform> {value: 2.6, style: mirror, "
            "direction: inverse}");
    OIIO_CHECK_ASSERT(Filesystem::write_text_file(filename, text));

    ColorConfig config(filename);
    OIIO_CHECK_FALSE(config.has_error());

    // A pure 2.2 power aliased for the sRGB texture encoding is a 2.2 power.
    OIIO_CHECK_EQUAL(config.get_color_interop_id("Pure22"), "g22_rec709_scene");
    // A scene-referred definition aliased for a display encoding keeps the
    // image state its configuration authored. Its author's alias does not
    // move it, and nothing infers the state from the alias.
    OIIO_CHECK_EQUAL(config.get_color_interop_id("Rec1886Scene"),
                     "g24_rec709_scene");
    // A state-less alias on a display-referred definition reports the display
    // identity, and the same alias would report the scene one on a
    // scene-referred definition. The configuration decides, not the alias.
    OIIO_CHECK_EQUAL(config.get_color_interop_id("AppleDisplay"),
                     "srgb_p3d65_display");
    OIIO_CHECK_EQUAL(config.get_color_interop_id("XYZ"),
                     "ocio:lin_ciexyzd65_display");
    // The alias names a 2.6 cinema encoding and the definition is a 2.2
    // power. Nothing measured supports the alias, so it is withdrawn rather
    // than published, and what was measured survives the withdrawal.
    OIIO_CHECK_EQUAL(config.get_color_interop_id("WideGamutPure22"), "");

    // Measured with the naming and the declared transfer both withheld. The
    // aliases above change nothing, because measurement already answered over
    // them. The declared linearity does change the identity query: it makes it
    // decline the encoding it measured, which is why no linearity hint may
    // seed a measured query.
    OIIO_CHECK_EQUAL(equality_id(config, "Pure22"), "g22_rec709_scene");
    OIIO_CHECK_EQUAL(equality_id(config, "Rec1886Scene"), "g24_rec709_scene");
    OIIO_CHECK_EQUAL(equality_id(config, "AppleDisplay"), "srgb_p3d65_display");
    OIIO_CHECK_EQUAL(equality_id(config, "WideGamutPure22"), "");
    OIIO_CHECK_EQUAL(equality_id(config, "FalseLinear"), "g22_rec709_scene");
    OIIO_CHECK_ASSERT(config.get_color_interop_id("FalseLinear")
                      != "g22_rec709_scene");
    // Its declared linear encoding does not stop derivation from reporting
    // the 2.2 power the definition measures as.
    auto false_linear = config.derive_color_space_info("FalseLinear");
    OIIO_CHECK_ASSERT(ColorSpaceInfoAccess::transfer_function_kind(false_linear)
                      == ColorTransferFunctionKind::Power);
    OIIO_CHECK_EQUAL_THRESH(false_linear.transfer_function_gamma(), 2.2f,
                            1.0e-6f);
    // An authored linear encoding is honored rather than measured: only a
    // measured pure-power exponent replaces it, and this definition has none.
    auto false_linear_srgb = config.derive_color_space_info("FalseLinearSRGB");
    OIIO_CHECK_EQUAL(false_linear_srgb.transfer_function_gamma(), 1.0f);
    OIIO_CHECK_ASSERT(
        ColorSpaceInfoAccess::transfer_function_kind(false_linear_srgb)
        == ColorTransferFunctionKind::Linear);

    auto wide = config.derive_color_space_info("WideGamutPure22");
    OIIO_CHECK_ASSERT(wide.valid());
    OIIO_CHECK_EQUAL(wide.transfer_function_gamma(), 2.2f);

    // Display encodings whose only difference from the built-in config is
    // negative handling, which the encoded gamut never reaches.
    OIIO_CHECK_EQUAL(config.get_color_interop_id("Gamma22Rec709"),
                     "g22_rec709_display");
    OIIO_CHECK_EQUAL(config.get_color_interop_id("Rec1886Rec2020"),
                     "oiio:g24_rec2020_display");

    // A cinema primary set whose white is not the interchange white. The
    // published library names it by constructing each hypothesis forward, so
    // it reports the DCI white rather than the adapted coordinates that
    // reading the matrix back would give.
    OIIO_CHECK_EQUAL(config.get_color_interop_id("CinemaP3"),
                     "oiio:g26_p3dci_display");
    auto cinema = config.derive_color_space_info("CinemaP3");
    OIIO_CHECK_EQUAL(cinema.transfer_function_gamma(), 2.6f);
    const float p3dci_xy[]
        = { .68f, .32f, .265f, .69f, .15f, .06f, .314f, .351f };
    auto cinema_xy = cinema.chromaticities();
    OIIO_CHECK_EQUAL(cinema_xy.size(), 8);
    if (cinema_xy.size() == 8)
        for (int i = 0; i < 8; ++i)
            OIIO_CHECK_EQUAL_THRESH(cinema_xy[i], p3dci_xy[i], 1e-6f);
    Filesystem::remove(filename);
}



static void
test_spi_conventions()
{
    if (!ColorConfig::supportsOpenColorIO())
        return;
    const std::string filename = Filesystem::temp_directory_path() + "/"
                                 + Filesystem::unique_path() + ".ocio";
    const char* srgb_names[]   = { "srgbf", "srgbh", "srgb16", "srgb8" };
    const char* linear_names[] = { "srgblnf", "srgblnh", "srgbln16",
                                   "srgbln8" };
    auto fixture               = [&](bool authored) {
        std::string text
            = "ocio_profile_version: 2.3\n"
              "roles: {default: Reference, aces_interchange: Reference";
        if (authored)
            text += ", lin_rec709_scene: Reference";
        text += "}\nfile_rules:\n"
                "  - !<Rule> {name: Default, colorspace: default}\n"
                "colorspaces:\n"
                "  - !<ColorSpace>\n    name: Reference\n    isdata: false\n";
        if (authored)
            text += "    aliases: [lin_ap1_scene, srgb_texture, sRGB]\n";
        auto add = [&](const char* name, bool data = false) {
            text += Strutil::fmt::format(
                "  - !<ColorSpace>\n    name: {}\n    isdata: {}\n", name,
                data ? "true" : "false");
            if (!data)
                text
                    += "    to_scene_reference: !<ExponentTransform> {value: 2.2}\n";
            if (string_view(name) == "srgb8"
                && ColorConfig::OpenColorIO_version_hex() >= 0x02050000)
                text += "    interop_id: custom:spi\n";
        };
        add("cgln_a");
        add("cgln_b");
        add("cgln_data", true);
        add("nc_pixels");
        add("lnf");
        add("CGLN_case");
        for (auto name : srgb_names)
            add(name);
        for (auto name : linear_names)
            add(name);
        OIIO_CHECK_ASSERT(Filesystem::write_text_file(filename, text));
    };
    fixture(false);
    ColorConfig config(filename);
    OIIO_CHECK_FALSE(config.has_error());
    OIIO_CHECK_ASSERT(config.isData("cgln_data"));
    OIIO_CHECK_EQUAL(config.get_color_interop_id("cgln_data"), "data");
    OIIO_CHECK_EQUAL(config.get_color_interop_id("lnf"), "");
    OIIO_CHECK_EQUAL(config.get_color_interop_id("CGLN_case"), "");
#ifdef OIIO_SITE_spi
    OIIO_CHECK_ASSERT(config.isData("nc_pixels"));
    OIIO_CHECK_EQUAL(config.get_color_interop_id("nc_pixels"), "data");
    OIIO_CHECK_EQUAL(config.resolve("data"), "data");
    OIIO_CHECK_EQUAL(config.resolve("linear"), "srgblnf");
    OIIO_CHECK_EQUAL(config.resolve("sRGB"), "srgbf");
    OIIO_CHECK_EQUAL(config.get_color_interop_id("cgln_a"), "lin_ap1_scene");
    OIIO_CHECK_EQUAL(config.get_color_interop_id("cgln_b"), "lin_ap1_scene");
    OIIO_CHECK_ASSERT(config.equivalent("cgln_a", "cgln_b"));
    OIIO_CHECK_EQUAL(config.resolve("lin_ap1_scene"), "cgln_a");
    OIIO_CHECK_EQUAL(config.resolve("srgb_rec709_scene"), "srgbf");
    OIIO_CHECK_EQUAL(config.resolve("lin_rec709_scene"), "srgblnf");
    OIIO_CHECK_ASSERT(config.equivalent("srgbf", "srgbh"));
    for (auto name : srgb_names) {
        const char* expected = string_view(name) == "srgb8"
                                       && ColorConfig::OpenColorIO_version_hex()
                                              >= 0x02050000
                                   ? "custom:spi"
                                   : "srgb_rec709_scene";
        OIIO_CHECK_EQUAL(config.get_color_interop_id(name), expected);
    }
    for (auto name : linear_names)
        OIIO_CHECK_EQUAL(config.get_color_interop_id(name), "lin_rec709_scene");
#else
    OIIO_CHECK_FALSE(config.isData("nc_pixels"));
    OIIO_CHECK_FALSE(config.equivalent("cgln_a", "cgln_b"));
    OIIO_CHECK_EQUAL(config.get_color_interop_id("cgln_a"), "");
    OIIO_CHECK_EQUAL(config.get_color_interop_id("srgbf"), "");
    OIIO_CHECK_EQUAL(config.get_color_interop_id("srgblnf"), "");
#endif
    if (ColorConfig::OpenColorIO_version_hex() >= 0x02050000)
        OIIO_CHECK_EQUAL(config.get_color_interop_id("srgb8"), "custom:spi");
    fixture(true);
    ColorConfig authored(filename);
    OIIO_CHECK_FALSE(authored.has_error());
    OIIO_CHECK_EQUAL(authored.resolve("lin_ap1_scene"), "Reference");
    OIIO_CHECK_EQUAL(authored.resolve("lin_rec709_scene"), "Reference");
    OIIO_CHECK_EQUAL(authored.resolve("srgb_rec709_scene"), "Reference");
    OIIO_CHECK_EQUAL(authored.resolve("sRGB"), "Reference");
#ifdef OIIO_SITE_spi
    OIIO_CHECK_EQUAL(authored.get_color_interop_id("cgln_b"), "lin_ap1_scene");
    OIIO_CHECK_ASSERT(authored.equivalent("cgln_a", "cgln_b"));
#endif
    Filesystem::remove(filename);
}



// The resolved name alone, so the assertions below read as calls.
static std::string
resolve_colorspace(
    const ColorConfig& config, const ImageSpec& spec, string_view filename = "",
    string_view assignment = "", string_view failover = "",
    string_view context_key = "", string_view context_value = "",
    pvt::FileRulesPrecedence file_rules = pvt::FileRulesPrecedence::Fallback,
    pvt::MissingColorSpace missing      = pvt::MissingColorSpace::Preserve)
{
    return pvt::resolve_colorspace_source(config, spec, filename, assignment,
                                          failover, context_key, context_value,
                                          file_rules, missing)
        .name;
}



static void
test_color_space_info_context()
{
    if (!ColorConfig::supportsOpenColorIO())
        return;
    const std::string directory = Filesystem::temp_directory_path() + "/"
                                  + Filesystem::unique_path();
    OIIO_CHECK_ASSERT(Filesystem::create_directory(directory));
    auto matrix = [&](string_view shot, string_view values) {
        return Filesystem::write_text_file(
            Strutil::fmt::format("{}/gamut_{}.ctf", directory, shot),
            Strutil::fmt::format(
                "<ProcessList version=\"1.3\" id=\"gamut\">\n"
                "  <Matrix inBitDepth=\"32f\" outBitDepth=\"32f\">\n"
                "    <Array dim=\"3 3\">{}</Array>\n"
                "  </Matrix>\n</ProcessList>\n",
                values));
    };
    OIIO_CHECK_ASSERT(
        matrix("rec2020",
               "0.679085634706912 0.157700914643159 0.163213450649929 "
               "0.0460020030800595 0.859054673002908 0.0949433239170327 "
               "-0.000573943187616196 0.0284677684080264 0.97210617477959"));
    OIIO_CHECK_ASSERT(
        matrix("bmdwg5",
               "0.647091325580708 0.242595385134207 0.110313289285085 "
               "0.0651915997328519 1.02504756760476 -0.0902391673376125 "
               "-0.0275570729194699 -0.0805887097177784 1.10814578263725"));
    const std::string filename = directory + "/context.ocio";
    OIIO_CHECK_ASSERT(Filesystem::write_text_file(
        filename,
        "ocio_profile_version: 2.3\n"
        "environment: {SHOT: rec2020}\nsearch_path: .\n"
        "roles: {default: Reference, aces_interchange: Reference}\n"
        "file_rules:\n  - !<Rule> {name: Default, colorspace: default}\n"
        "colorspaces:\n  - !<ColorSpace> {name: Reference}\n"
        "  - !<ColorSpace>\n    name: Plate\n"
        "    to_scene_reference: !<FileTransform> {src: gamut_$SHOT.ctf}\n"));
    ColorConfig config(filename);
    OIIO_CHECK_FALSE(config.has_error());
    auto check = [](const ColorSpaceInfo& info, cspan<float> expected) {
        OIIO_CHECK_EQUAL(info.transfer_function_gamma(), 1.0f);
        const auto xy = info.chromaticities();
        OIIO_CHECK_EQUAL(xy.size(), expected.size());
        if (xy.size() == expected.size())
            for (size_t i = 0; i < expected.size(); ++i)
                OIIO_CHECK_EQUAL_THRESH(xy[i], expected[i], 1.0e-6f);
    };
    const float rec2020[] = { .708f, .292f, .170f,  .797f,
                              .131f, .046f, .3127f, .3290f };
    const float bmdwg5[]  = { .7177215f, .3171181f,  .228041f, .861569f,
                              .1005841f, -.0820452f, .312717f, .3290312f };
    OIIO_CHECK_ASSERT(
        pvt::color_space_info(config, "Plate", false, "SHOT", "rec2020")
            .chromaticities()
            .empty());
    check(pvt::color_space_info(config, "Plate", true, "SHOT", "rec2020"),
          rec2020);
    OIIO_CHECK_ASSERT(
        pvt::color_space_info(config, "Plate", false, "SHOT", "bmdwg5")
            .chromaticities()
            .empty());
    check(pvt::color_space_info(config, "Plate", true, "SHOT", "bmdwg5"),
          bmdwg5);
    check(pvt::color_space_info(config, "Plate", false, "SHOT", "rec2020"),
          rec2020);
    check(config.derive_color_space_info("Plate"), rec2020);
    // A variable the config never reads builds no view of its own, so the
    // cheap query answers what the default context derived, for any value.
    for (int i = 0; i < 100; ++i)
        check(pvt::color_space_info(config, "Plate", false, "UNUSED",
                                    Strutil::fmt::format("v{}", i)),
              rec2020);

    // String variables that name no file still select their own results.
    const std::string strings = directory + "/strings.ocio";
    OIIO_CHECK_ASSERT(Filesystem::write_text_file(
        strings,
        "ocio_profile_version: 2.3\n"
        "environment: {SHOT: Hidden}\n"
        "roles: {default: ACES2065-1, scene_linear: ACES2065-1,\n"
        "  aces_interchange: ACES2065-1, cie_xyz_d65_interchange: XYZ}\n"
        "file_rules:\n  - !<Rule> {name: Default, colorspace: default}\n"
        "colorspaces:\n"
        "  - !<ColorSpace> {name: ACES2065-1, encoding: scene-linear}\n"
        "  - !<ColorSpace>\n    name: XYZ\n    from_scene_reference: "
        "!<BuiltinTransform> {style: UTILITY - ACES-AP0_to_CIE-XYZ-D65_BFD}\n"
        "  - !<ColorSpace>\n    name: acescg\n    from_scene_reference: "
        "!<BuiltinTransform> {style: ACEScg_to_ACES2065-1, direction: "
        "inverse}\n"
        "  - !<ColorSpace>\n    name: Plate\n    to_scene_reference: "
        "!<ColorSpaceTransform> {src: $SHOT, dst: ACES2065-1}\n"
        "  - !<ColorSpace>\n    name: Hidden\n    to_scene_reference:\n"
        "      !<GroupTransform>\n      children:\n"
        "        - !<MatrixTransform> {matrix: [0.4123908, 0.3575843, "
        "0.1804808, 0, 0.2126390, 0.7151687, 0.0721923, 0, 0.0193308, "
        "0.1191948, 0.9505322, 0, 0, 0, 0, 1]}\n"
        "        - !<BuiltinTransform> {style: UTILITY - "
        "ACES-AP0_to_CIE-XYZ-D65_BFD, direction: inverse}\n"));
    ColorConfig vars(strings);
    OIIO_CHECK_FALSE(vars.has_error());
    OIIO_CHECK_EQUAL(ColorSpaceInfoAccess::color_interop_id(
                         vars.derive_color_space_info("Plate")),
                     "lin_rec709_scene");
    for (string_view name : { "Plate", "$SHOT" }) {
        OIIO_CHECK_EQUAL(ColorSpaceInfoAccess::color_interop_id(
                             pvt::color_space_info(vars, name, true, "SHOT",
                                                   "acescg")),
                         "lin_ap1_scene");
        OIIO_CHECK_EQUAL(equality_id(vars, name, "SHOT", "acescg"),
                         "lin_ap1_scene");
        OIIO_CHECK_EQUAL(equality_id(vars, name, "UNUSED,SHOT", "x,acescg"),
                         "lin_ap1_scene");
    }
    // A variable only the queried name reads still selects its own result.
    OIIO_CHECK_EQUAL(ColorSpaceInfoAccess::color_interop_id(
                         pvt::color_space_info(vars, "$SHOT2", true, "SHOT2",
                                               "acescg")),
                     "lin_ap1_scene");

    // A completed miss is reusable only under the effective context that
    // proved it. Under rec2020 these BMD Wide Gamut facts need a synthetic
    // endpoint; under bmdwg5 the context-dependent local Plate definition
    // wins.
    ImageSpec numeric;
    numeric.attribute("chromaticities", TypeDesc(TypeDesc::FLOAT, 8), bmdwg5);
    numeric.attribute("oiio:Gamma", 1.0f);
    const std::string custom = resolve_colorspace(config, numeric, "frame.jpg",
                                                  "", "", "SHOT", "rec2020");
    OIIO_CHECK_ASSERT(Strutil::starts_with(custom, "<synthetic>"));
    OIIO_CHECK_EQUAL(resolve_colorspace(config, numeric, "frame.jpg", "", "",
                                        "SHOT", "rec2020"),
                     custom);
    OIIO_CHECK_EQUAL(resolve_colorspace(config, numeric, "frame.jpg", "", "",
                                        "SHOT", "bmdwg5"),
                     "Plate");
    OIIO_CHECK_EQUAL(resolve_colorspace(config, numeric, "frame.jpg", "", "",
                                        "SHOT", "rec2020"),
                     custom);

    ImageSpec tagged;
    tagged.attribute("oiio:ColorSpace", "Plate");
    pvt::finalize_resolved_color_metadata(tagged, config, "openexr",
                                          "plate.exr", "SHOT", "rec2020");
    OIIO_CHECK_EQUAL(tagged.get_string_attribute("colorInteropID"),
                     "lin_rec2020_scene");
    tagged.erase_attribute("colorInteropID");
    pvt::finalize_resolved_color_metadata(tagged, config, "openexr",
                                          "plate.exr", "SHOT", "bmdwg5");
    OIIO_CHECK_EQUAL(tagged.get_string_attribute("colorInteropID"),
                     "ocio:lin_bmdwg5_scene");
    Filesystem::remove_all(directory);
}



// Between readings a measurement cannot separate, the one declaring the
// space's encoding answers: srgb_p3d65_display and srgbe_p3d65_display share
// their operations and differ only in encoding.
static void
test_encoding_tie_break()
{
    if (!ColorConfig::supportsOpenColorIO())
        return;
    const std::string filename = Filesystem::temp_directory_path() + "/"
                                 + Filesystem::unique_path() + ".ocio";
    for (string_view encoding : { "sdr-video", "hdr-video" }) {
        OIIO_CHECK_ASSERT(Filesystem::write_text_file(
            filename,
            Strutil::fmt::format(
                "ocio_profile_version: 2.3\n"
                "roles: {{default: XYZ, cie_xyz_d65_interchange: XYZ,\n"
                "  aces_interchange: ACES}}\n"
                "file_rules:\n  - !<Rule> {{name: Default, colorspace: default}}\n"
                "colorspaces:\n  - !<ColorSpace> {{name: ACES}}\n"
                "display_colorspaces:\n  - !<ColorSpace> {{name: XYZ}}\n"
                "  - !<ColorSpace>\n    name: Panel\n    encoding: {}\n"
                "    from_display_reference: !<GroupTransform>\n"
                "      children:\n"
                "        - !<MatrixTransform> {{matrix: [2.49349691194143, "
                "-0.931383617919124, -0.402710784450717, 0, -0.829488969561575, "
                "1.76266406031835, 0.0236246858419436, 0, 0.0358458302437845, "
                "-0.0761723892680418, 0.956884524007688, 0, 0, 0, 0, 1]}}\n"
                "        - !<ExponentWithLinearTransform> {{gamma: 2.4, "
                "offset: 0.055, style: mirror, direction: inverse}}\n",
                encoding)));
        ColorConfig config(filename);
        OIIO_CHECK_FALSE(config.has_error());
        OIIO_CHECK_EQUAL(ColorSpaceInfoAccess::color_interop_id(
                             config.derive_color_space_info("Panel")),
                         encoding == "hdr-video" ? "srgbe_p3d65_display"
                                                 : "srgb_p3d65_display");
    }
    Filesystem::remove(filename);
}



// The facts the library reads through the private access, for the built-in
// interop-identities config and for built-in identities a config does not
// define. Python sees only the public accessors.
static void
test_private_properties()
{
    if (!ColorConfig::supportsOpenColorIO())
        return;
    using Access      = ColorSpaceInfoAccess;
    using Field       = ColorSpaceInfoField;
    using Kind        = ColorTransferFunctionKind;
    const bool ocio25 = ColorConfig::OpenColorIO_version_hex() >= 0x02050000;
    // OpenColorIO before 2.5 cannot read interop_id.
    std::string text;
    OIIO_CHECK_ASSERT(
        Filesystem::read_text_file(OIIO_COLOR_TEST_IDENTITIES, text));
    if (!ocio25) {
        std::string kept;
        for (string_view line : Strutil::splitsv(text, "\n"))
            if (!Strutil::starts_with(line, "    interop_id:"))
                kept += std::string(line) + "\n";
        text = kept;
    }
    const std::string filename = Filesystem::temp_directory_path() + "/"
                                 + Filesystem::unique_path() + ".ocio";
    OIIO_CHECK_ASSERT(Filesystem::write_text_file(filename, text));
    {
        ColorConfig config(filename);
        OIIO_CHECK_FALSE(config.has_error());
        // Before derivation, the cheap query counts what the config declares
        // as computed: the image state its encoding names, and its ID.
        auto cheap = config.get_color_space_info("lin_rec2020_scene");
        OIIO_CHECK_ASSERT(Access::computed(cheap, Field::ImageState));
        OIIO_CHECK_EQUAL(Access::image_state(cheap), "scene");
        if (ocio25) {
            OIIO_CHECK_ASSERT(Access::computed(cheap, Field::ColorInteropID));
            OIIO_CHECK_FALSE(Access::derived(cheap, Field::ColorInteropID));
        }
        auto info = config.derive_color_space_info("lin_ap1_scene");
        OIIO_CHECK_ASSERT(Access::transfer_function_kind(info) == Kind::Linear);
        OIIO_CHECK_ASSERT(Access::transfer_function_name(info).empty());
        OIIO_CHECK_EQUAL(Access::equality_id(info), "lin_ap1_scene");
        OIIO_CHECK_EQUAL(Access::color_interop_id(info), "lin_ap1_scene");
        for (auto field : { Field::Chromaticities, Field::TransferFunction,
                            Field::EqualityID, Field::ColorInteropID,
                            Field::Encoding, Field::ImageState })
            OIIO_CHECK_ASSERT(Access::computed(info, field));
        OIIO_CHECK_ASSERT(Access::available(info, Field::ColorInteropID));
        OIIO_CHECK_EQUAL(Access::derived(info, Field::ColorInteropID), !ocio25);
        for (auto name :
             { "g18_rec709_scene", "g22_ap1_scene", "g24_rec709_scene" })
            OIIO_CHECK_ASSERT(Access::transfer_function_kind(
                                  config.derive_color_space_info(name))
                              == Kind::Power);
        // DCDM is a read-only input identity, reached by CICP 12/17 and never
        // won by measurement, so even its own definition measures as none.
        OIIO_CHECK_EQUAL(equality_id(config, "dcdm_p3d65_display"), "");
    }
    // Any valid ID names its image state by its suffix.
    for (auto [name, state] :
         { std::pair { "ocio:acescc_ap1_scene", "scene" },
           std::pair { "oiio:g22_p3d65_display", "display" } })
        OIIO_CHECK_EQUAL(Access::image_state(
                             ColorConfig(filename).derive_color_space_info(
                                 name)),
                         state);
    Filesystem::remove(filename);

    // A built-in identity the config does not define is described by the
    // built-in interop-identities config, named exactly (in any case).
    ColorConfig builtin("ocio://default");
    for (bool derive : { false, true })
        for (auto name :
             { "oiio:g24_rec2020_display", "OIIO:G24_REC2020_DISPLAY",
               "g22_adobergb_display", "oiio:lin_p3dci_display" })
            OIIO_CHECK_EQUAL(Access::color_interop_id(
                                 pvt::color_space_info(builtin, name, derive)),
                             Strutil::lower(name));
    OIIO_CHECK_ASSERT(
        Access::transfer_function_kind(
            builtin.derive_color_space_info("g22_adobergb_display"))
        == Kind::Power);

    // An is-unique space is never named by comparison with a reference
    // definition, but its separately measured curve and primaries may still
    // identify it, and a derived ID completes its image state.
    const std::string to_rec709
        = "!<MatrixTransform> {matrix: [2.52168618674388, -1.13413098823972, "
          "-0.387555198504164, 0, -0.276479914229922, 1.37271908766826, "
          "-0.096239173438334, 0, -0.0153780649660342, -0.152975335867399, "
          "1.16835340083343, 0, 0, 0, 0, 1]}";
    OIIO_CHECK_ASSERT(Filesystem::write_text_file(
        filename,
        "ocio_profile_version: 2.3\n"
        "roles: {aces_interchange: ACES2065-1, scene_linear: ACES2065-1, "
        "default: ACES2065-1}\n"
        "file_rules:\n  - !<Rule> {name: Default, colorspace: default}\n"
        "colorspaces:\n"
        "  - !<ColorSpace> {name: ACES2065-1, encoding: scene-linear}\n"
        "  - !<ColorSpace>\n    name: UniqueSRGB\n"
        "    categories: [is-unique]\n"
        "    from_scene_reference: !<GroupTransform> {children: ["
            + to_rec709
            + ", !<ExponentWithLinearTransform> {gamma: 2.4, offset: 0.055, "
              "direction: inverse}]}\n"
              "  - !<ColorSpace>\n    name: UniqueLinear\n"
              "    categories: [is-unique]\n"
              "    from_scene_reference: "
            + to_rec709 + "\n"));
    {
        ColorConfig unique(filename);
        OIIO_CHECK_FALSE(unique.has_error());
        auto srgb = unique.derive_color_space_info("UniqueSRGB");
        OIIO_CHECK_EQUAL(Access::equality_id(srgb), "");
        OIIO_CHECK_EQUAL(Access::color_interop_id(srgb), "srgb_rec709_scene");
        OIIO_CHECK_EQUAL(Access::image_state(srgb), "scene");
        auto linear = unique.derive_color_space_info("UniqueLinear");
        OIIO_CHECK_EQUAL(linear.chromaticities().size(), 8);
        OIIO_CHECK_EQUAL(Access::equality_id(linear), "");
        OIIO_CHECK_EQUAL(Access::color_interop_id(linear), "");
    }
    Filesystem::remove(filename);

    // A definition with a 3D LUT is not measured, even behind a FileTransform.
    const std::string directory = Filesystem::temp_directory_path() + "/"
                                  + Filesystem::unique_path();
    OIIO_CHECK_ASSERT(Filesystem::create_directory(directory));
    OIIO_CHECK_ASSERT(
        Filesystem::write_text_file(directory + "/identity3d.cube",
                                    "LUT_3D_SIZE 2\n0 0 0\n1 0 0\n0 1 0\n1 1 0\n"
                                    "0 0 1\n1 0 1\n0 1 1\n1 1 1\n"));
    OIIO_CHECK_ASSERT(Filesystem::write_text_file(
        directory + "/lut3d.ocio",
        "ocio_profile_version: 2.3\nsearch_path: .\n"
        "roles: {default: ACES2065-1, scene_linear: ACES2065-1, "
        "aces_interchange: ACES2065-1}\n"
        "file_rules:\n  - !<Rule> {name: Default, colorspace: default}\n"
        "colorspaces:\n"
        "  - !<ColorSpace> {name: ACES2065-1, encoding: scene-linear}\n"
        "  - !<ColorSpace>\n    name: Lut3DPlate\n"
        "    to_scene_reference: !<FileTransform> {src: identity3d.cube, "
        "interpolation: linear}\n"));
    {
        ColorConfig lut3d(directory + "/lut3d.ocio");
        OIIO_CHECK_FALSE(lut3d.has_error());
        OIIO_CHECK_EQUAL(equality_id(lut3d, "Lut3DPlate"), "");
    }

    // The ImageSpec helper answers as the default config does, with or
    // without an unused context.
    ImageSpec spec;
    spec.attribute("oiio:ColorSpace", "scene_linear");
    auto plain     = pvt::get_colorspace_info(spec, true);
    auto explicit_ = pvt::get_colorspace_info(spec, true, "SHOT", "rec2020");
    OIIO_CHECK_EQUAL(plain.valid(), explicit_.valid());
    OIIO_CHECK_EQUAL(plain.transfer_function_gamma(),
                     explicit_.transfer_function_gamma());
}



static void
test_metadata_resolution()
{
    if (!ColorConfig::supportsOpenColorIO())
        return;
    const std::string filename = Filesystem::temp_directory_path() + "/"
                                 + Filesystem::unique_path() + ".ocio";
    // "data" and "bypass" are aliases, as the CIF ID must be: OCIO rejects a
    // config whose alias is also a role name, and the built-in configs
    // already exercise the role spelling. A glob rule needs an explicit
    // extension; OCIO rejects an empty one. Expectations for the declared
    // interchange aliases below concern authored evidence; separate numeric
    // cases exercise measured recognition.
    OIIO_CHECK_ASSERT(Filesystem::write_text_file(
        filename,
        "ocio_profile_version: 2.3\n"
        "strictparsing: false\n"
        "roles: {default: Linear, scene_linear: Linear}\n"
        "file_rules:\n"
        "  - !<Rule> {name: plates, colorspace: sRGB, pattern: plate_*,"
        " extension: \"*\"}\n"
        "  - !<Rule> {name: Default, colorspace: Gamma22}\n"
        "colorspaces:\n"
        "  - !<ColorSpace>\n    name: Linear\n"
        "    aliases: [lin_rec709_scene]\n"
        "  - !<ColorSpace>\n    name: sRGB\n"
        "    aliases: [srgb_rec709_scene, srgb_rec709_display]\n"
        "    to_scene_reference: !<ExponentTransform> {value: 2.2}\n"
        "  - !<ColorSpace>\n    name: Gamma22\n"
        "    aliases: [g22_rec709_scene]\n"
        "    to_scene_reference: !<ExponentTransform> {value: 2.2}\n"
        "  - !<ColorSpace>\n    name: AP0\n"
        "    aliases: [lin_ap0_scene]\n"
        "  - !<ColorSpace>\n    name: PQ\n"
        "    aliases: [\"cicp:9-16-0-1\"]\n"
        "  - !<ColorSpace>\n    name: SD\n"
        "    aliases: [\"cicp:6-14-0-1\", \"cicp:6-1-5-0\"]\n"
        "  - !<ColorSpace>\n    name: AdobeRGB\n"
        "    aliases: [g22_adobergb_display]\n"
        "  - !<ColorSpace>\n    name: Data\n    isdata: true\n"
        "    aliases: [data, bypass]\n"
        "display_colorspaces:\n"
        "  - !<ColorSpace>\n    name: DisplayGamma22\n"
        "    aliases: [g22_rec709_display]\n"
        "    to_display_reference: !<ExponentTransform> {value: 2.2}\n"));
    ColorConfig config(filename);
    // A rejected fixture silently falls back to the built-in inventory and
    // every native expectation below would fail for the wrong reason.
    OIIO_CHECK_EQUAL(config.geterror(false), "");
    OIIO_CHECK_ASSERT(config.getColorSpaceIndex("Data") >= 0);
    if (config.has_error()) {
        Filesystem::remove(filename);
        return;
    }
    const float rec709[8] = { 0.640f, 0.330f, 0.300f,  0.600f,
                              0.150f, 0.060f, 0.3127f, 0.3290f };
    const float adobe[8]  = { 0.640f, 0.330f, 0.210f,  0.710f,
                              0.150f, 0.060f, 0.3127f, 0.3290f };
    const float custom[8] = { 0.70f, 0.29f, 0.17f,   0.80f,
                              0.13f, 0.05f, 0.3127f, 0.3290f };

    // Explicit assignment wins; a miss consults only the failover.
    ImageSpec spec;
    spec.attribute("colorInteropID", "srgb_rec709_scene");
    OIIO_CHECK_EQUAL(resolve_colorspace(config, spec, "plate_a.exr"), "sRGB");
    using Source    = pvt::ColorSpaceSource;
    using Status    = pvt::ColorSpaceStatus;
    auto provenance = pvt::resolve_colorspace_source(config, spec,
                                                     "plate_a.exr");
    OIIO_CHECK_EQUAL(provenance.name,
                     resolve_colorspace(config, spec, "plate_a.exr"));
    OIIO_CHECK_ASSERT(provenance.source == Source::InteropID);
    OIIO_CHECK_ASSERT(provenance.status == Status::Resolved);
    provenance = pvt::resolve_colorspace_source(config, spec, "plate_a.exr",
                                                "lin_rec709_scene");
    OIIO_CHECK_EQUAL(provenance.name, "Linear");
    OIIO_CHECK_ASSERT(provenance.source == Source::Assignment);
    OIIO_CHECK_EQUAL(resolve_colorspace(config, spec, "plate_a.exr",
                                        "lin_rec709_scene"),
                     "Linear");
    // An assignment this configuration cannot use is a statement, and once
    // the failover is exhausted the answer is "unknown", never a default.
    OIIO_CHECK_EQUAL(resolve_colorspace(config, spec, "plate_a.exr", "nope"),
                     "unknown");
    OIIO_CHECK_EQUAL(resolve_colorspace(config, spec, "plate_a.exr", "nope",
                                        "scene_linear"),
                     "Linear");
    OIIO_CHECK_EQUAL(resolve_colorspace(config, spec, "plate_a.exr",
                                        "lin_rec709_scene", "", "", "",
                                        pvt::FileRulesPrecedence::First),
                     "Linear");
    OIIO_CHECK_EQUAL(resolve_colorspace(config, spec, "", "bypass"), "Data");

    using Missing = pvt::MissingColorSpace;
    using Rules   = pvt::FileRulesPrecedence;
    ImageSpec missing;
    OIIO_CHECK_EQUAL(resolve_colorspace(config, missing, "frame.exr", "", "",
                                        "", "", Rules::Fallback,
                                        Missing::Preserve),
                     "");
    OIIO_CHECK_EQUAL(resolve_colorspace(config, missing, "frame.exr", "", "",
                                        "", "", Rules::Fallback,
                                        Missing::ConfigPolicy),
                     "Gamma22");
    provenance = pvt::resolve_colorspace_source(config, missing, "frame.exr",
                                                "", "", "", "", Rules::Fallback,
                                                Missing::ConfigPolicy);
    OIIO_CHECK_EQUAL(provenance.name, "Gamma22");
    OIIO_CHECK_ASSERT(provenance.source == Source::FileRulesDefault);
    OIIO_CHECK_ASSERT(provenance.status == Status::Resolved);
    // An invalid explicit assignment skips file facts and answers "unknown"
    // whatever the missing-source policy says, because something was stated.
    // A usable failover retains priority.
    OIIO_CHECK_EQUAL(resolve_colorspace(config, spec, "plate_a.exr", "nope", "",
                                        "", "", Rules::Fallback,
                                        Missing::ConfigPolicy),
                     "unknown");
    OIIO_CHECK_EQUAL(resolve_colorspace(config, spec, "plate_a.exr", "unknown",
                                        "", "", "", Rules::Fallback,
                                        Missing::ConfigPolicy),
                     "unknown");
    OIIO_CHECK_EQUAL(resolve_colorspace(config, spec, "plate_a.exr", "unknown",
                                        "data", "", "", Rules::Fallback,
                                        Missing::ConfigPolicy),
                     "Data");

    // ACES container over a conflicting ID; data/bypass are terminal;
    // unknown lets FileRules apply.
    spec.attribute("acesImageContainerFlag", 1);
    OIIO_CHECK_EQUAL(resolve_colorspace(config, spec), "AP0");
    OIIO_CHECK_EQUAL(resolve_colorspace(config, spec, "plate_a.exr", "", "", "",
                                        "", pvt::FileRulesPrecedence::First),
                     "AP0");
    spec.erase_attribute("acesImageContainerFlag");
    spec.attribute("colorInteropID", "data");
    OIIO_CHECK_EQUAL(resolve_colorspace(config, spec, "plate_a.exr", "", "", "",
                                        "",
                                        pvt::FileRulesPrecedence::MetadataOnly),
                     "Data");
    OIIO_CHECK_EQUAL(resolve_colorspace(config, spec, "plate_a.exr", "", "", "",
                                        "", pvt::FileRulesPrecedence::First),
                     "sRGB");
    OIIO_CHECK_EQUAL(resolve_colorspace(config, spec, "plate_a.exr", "", "", "",
                                        "", pvt::FileRulesPrecedence::Fallback),
                     "Data");
    OIIO_CHECK_EQUAL(resolve_colorspace(config, spec, "plate_a.exr"), "Data");
    spec.attribute("colorInteropID", "bypass");
    OIIO_CHECK_EQUAL(resolve_colorspace(config, spec), "Data");
    // A file's "unknown" is evidence that does not help, so resolution goes
    // on: a matching FileRule still answers. With everything exhausted the
    // answer is "unknown", on this config whose strictparsing is off.
    spec.attribute("colorInteropID", "unknown");
    OIIO_CHECK_EQUAL(resolve_colorspace(config, spec, "plate_a.exr"), "sRGB");
    OIIO_CHECK_EQUAL(resolve_colorspace(config, spec, "frame.exr"), "unknown");
    provenance = pvt::resolve_colorspace_source(config, spec, "frame.exr");
    OIIO_CHECK_ASSERT(provenance.terminal_unknown);
    OIIO_CHECK_ASSERT(provenance.status == Status::TerminalUnknown);
    // An ID this configuration cannot use behaves the same way, and the
    // rest of the file's evidence is still consulted first.
    spec.attribute("colorInteropID", "notaspace");
    OIIO_CHECK_EQUAL(resolve_colorspace(config, spec, "frame.exr"), "unknown");
    OIIO_CHECK_EQUAL(resolve_colorspace(config, spec, "plate_a.exr"), "sRGB");
    spec.attribute("colorInteropID", "unknown");
    spec.attribute("colorInteropID", "studio:custom_space");
    spec.attribute("oiio:ColorSpace", "scene_linear");
    OIIO_CHECK_EQUAL(resolve_colorspace(config, spec, "frame.exr"), "Linear");
    spec.erase_attribute("colorInteropID");
    spec.erase_attribute("oiio:ColorSpace");

    // CICP: exact alias, table identity, unspecified, limited range.
    const int pq[]          = { 9, 16, 0, 1 };
    const int srgb[]        = { 1, 13, 0, 1 };
    const int p3[]          = { 12, 13, 0, 1 };
    const int unspecified[] = { 2, 2, 0, 1 };
    const int limited[]     = { 1, 13, 0, 0 };
    const int unsupported[] = { 250, 250, 0, 1 };
    spec.attribute("CICP", TypeDesc(TypeDesc::INT, 4), pq);
    OIIO_CHECK_EQUAL(resolve_colorspace(config, spec, "frame.png"), "PQ");
    spec.attribute("CICP", TypeDesc(TypeDesc::INT, 4), srgb);
    OIIO_CHECK_EQUAL(resolve_colorspace(config, spec, "plate_a.png"), "sRGB");
    spec.attribute("CICP", TypeDesc(TypeDesc::INT, 4), p3);
    OIIO_CHECK_EQUAL(resolve_colorspace(config, spec, "frame.png"),
                     "srgb_p3d65_scene");
    spec.attribute("oiio:ColorSpace", "srgb_rec709_scene");
    spec.attribute("CICP", TypeDesc(TypeDesc::INT, 4), unspecified);
    OIIO_CHECK_EQUAL(resolve_colorspace(config, spec, "frame.png"), "sRGB");
    // A full PNG claim OIIO cannot identify hides the reader label but
    // reaches FileRules and the failover; other containers fall through.
    for (const int* claim : { limited, unsupported }) {
        spec.attribute("CICP", TypeDesc(TypeDesc::INT, 4), claim);
        OIIO_CHECK_EQUAL(resolve_colorspace(config, spec, "frame.png"),
                         "unknown");
        OIIO_CHECK_EQUAL(resolve_colorspace(config, spec, "frame.png", "", "",
                                            "", "", Rules::Fallback,
                                            Missing::ConfigPolicy),
                         "unknown");
        provenance = pvt::resolve_colorspace_source(config, spec, "frame.png",
                                                    "", "", "", "",
                                                    Rules::Fallback,
                                                    Missing::ConfigPolicy);
        OIIO_CHECK_EQUAL(provenance.name, "unknown");
        OIIO_CHECK_ASSERT(provenance.source == Source::CICP);
        OIIO_CHECK_ASSERT(provenance.status == Status::TerminalUnknown);
        OIIO_CHECK_EQUAL(resolve_colorspace(config, spec, "frame.png", "",
                                            "data"),
                         "Data");
        OIIO_CHECK_EQUAL(resolve_colorspace(config, spec, "plate_a.png", "",
                                            "data"),
                         "sRGB");
        OIIO_CHECK_EQUAL(resolve_colorspace(config, spec, "plate_a.png", "",
                                            "data", "", "",
                                            pvt::FileRulesPrecedence::First),
                         "sRGB");
        OIIO_CHECK_EQUAL(
            resolve_colorspace(config, spec, "plate_a.png", "", "data", "", "",
                               pvt::FileRulesPrecedence::MetadataOnly),
            "Data");
        OIIO_CHECK_EQUAL(resolve_colorspace(config, spec, "frame.exr", "",
                                            "data"),
                         "sRGB");
    }

    // Standard-definition tuples, on a spec of their own so the cases above
    // keep the state they established. This config defines neither SD color
    // space, so the identity itself is returned for a later conversion,
    // exactly as the P3 case above does.
    {
        ImageSpec sd;
        auto claim = [&](const int tuple[4], string_view file) {
            sd.attribute("CICP", TypeDesc(TypeDesc::INT, 4), tuple);
            return resolve_colorspace(config, sd, file);
        };
        const int sd525[]  = { 6, 1, 0, 1 };
        const int sd625[]  = { 5, 8, 0, 1 };
        const int sd240m[] = { 7, 1, 0, 1 };
        OIIO_CHECK_EQUAL(claim(sd525, "frame.png"), "oiio:g24_rec601_display");
        OIIO_CHECK_EQUAL(claim(sd625, "frame.png"),
                         "oiio:lin_rec601pal_display");
        // SMPTE 240M primaries are the 525-line primaries exactly.
        OIIO_CHECK_EQUAL(claim(sd240m, "frame.png"), "oiio:g24_rec601_display");
        // An exact authored alias claims its tuple as spelled, whether or not
        // anything else would have recognized it: the alternate transfer code
        // that the table would have answered for, and the narrow-range YCbCr
        // tuple that nothing else claims at all.
        const int aliased_transfer[] = { 6, 14, 0, 1 };
        const int aliased_carrier[]  = { 6, 1, 5, 0 };
        OIIO_CHECK_EQUAL(claim(aliased_transfer, "frame.png"), "SD");
        OIIO_CHECK_EQUAL(claim(aliased_carrier, "frame.png"), "SD");
        // Without an alias, a tuple describing a carrier this library does not
        // hand back -- narrow range, or YCbCr coefficients -- claims nothing.
        const int narrow[] = { 6, 1, 0, 0 };
        const int ycbcr[]  = { 5, 1, 5, 0 };
        OIIO_CHECK_EQUAL(claim(narrow, "frame.exr"), "");
        OIIO_CHECK_EQUAL(claim(ycbcr, "frame.exr"), "");
    }
    spec.erase_attribute("CICP");

    // Numeric metadata selects published or configured identities. Gamma
    // alone supplies no gamut, but can select a configured transfer match.
    // Complete unmatched facts give conversion a reusable process-local
    // selector; ordinary resolution ends as "unknown", never as the
    // configuration's missing-source default.
    ImageSpec numeric;
    numeric.attribute("chromaticities", TypeDesc(TypeDesc::FLOAT, 8), adobe);
    numeric.attribute("oiio:Gamma", 2.19921875f);
    provenance = pvt::resolve_colorspace_source(config, numeric, "frame.png");
    OIIO_CHECK_EQUAL(provenance.name, "AdobeRGB");
    OIIO_CHECK_ASSERT(provenance.source == Source::NumericMetadata);
    // The configured alias selects the local endpoint, but its intentionally
    // incomplete definition does not independently establish that identity.
    OIIO_CHECK_EQUAL(config.get_color_interop_id("AdobeRGB"), "");
    ImageSpec finalized_numeric = numeric;
    pvt::finalize_resolved_color_metadata(finalized_numeric, config, "png",
                                          "frame.png");
    OIIO_CHECK_EQUAL(finalized_numeric.get_string_attribute("colorInteropID"),
                     "g22_adobergb_display");
    // Explicit metadata remains authoritative even when other facts disagree.
    ImageSpec explicit_numeric = numeric;
    explicit_numeric.attribute("colorInteropID", "lin_ap1_scene");
    pvt::finalize_resolved_color_metadata(explicit_numeric, config, "png",
                                          "frame.png");
    OIIO_CHECK_EQUAL(explicit_numeric.get_string_attribute("colorInteropID"),
                     "lin_ap1_scene");
    ImageSpec retagged                 = explicit_numeric;
    const unsigned char profile_byte[] = { 0 };
    retagged.attribute("ICCProfile",
                       TypeDesc(TypeDesc::UINT8, int(std::size(profile_byte))),
                       profile_byte);
    // Retagging makes the name authoritative: a profile that identifies as
    // nothing goes, and the name's own identity and gamma replace the stale
    // ones.
    config.set_colorspace(retagged, "Linear");
    OIIO_CHECK_ASSERT(retagged.find_attribute("ICCProfile") == nullptr);
    OIIO_CHECK_EQUAL(config.get_color_interop_id("Linear"), "lin_rec709_scene");
    OIIO_CHECK_EQUAL(retagged.get_string_attribute("colorInteropID"),
                     "lin_rec709_scene");
    OIIO_CHECK_EQUAL(retagged.get_float_attribute("oiio:Gamma"), 1.0f);
    // A label is a derived guess. Beside chromaticities that agree with it,
    // it earns its ID and the chromaticities go as redundant; metadata that
    // contradicts it -- chromaticities or a gamma -- wins, and the missing
    // identity is recorded as unknown.
    ImageSpec label_agrees;
    label_agrees.attribute("oiio:ColorSpace", "Linear");
    label_agrees.attribute("chromaticities", TypeDesc(TypeDesc::FLOAT, 8),
                           rec709);
    pvt::finalize_resolved_color_metadata(label_agrees, config, "png",
                                          "frame.png");
    OIIO_CHECK_EQUAL(label_agrees.get_string_attribute("colorInteropID"),
                     "lin_rec709_scene");
    OIIO_CHECK_ASSERT(label_agrees.find_attribute("chromaticities") == nullptr);
    ImageSpec label_contradicted = label_agrees;
    label_contradicted.erase_attribute("colorInteropID");
    label_contradicted.attribute("chromaticities", TypeDesc(TypeDesc::FLOAT, 8),
                                 adobe);
    pvt::finalize_resolved_color_metadata(label_contradicted, config, "png",
                                          "frame.png");
    OIIO_CHECK_EQUAL(label_contradicted.get_string_attribute("colorInteropID"),
                     "unknown");
    OIIO_CHECK_ASSERT(label_contradicted.find_attribute("chromaticities"));
    ImageSpec gamma_labeled;
    gamma_labeled.attribute("oiio:ColorSpace", "Linear");
    gamma_labeled.attribute("oiio:Gamma", 1.8f);
    pvt::finalize_resolved_color_metadata(gamma_labeled, config, "png",
                                          "frame.png");
    OIIO_CHECK_EQUAL(gamma_labeled.get_string_attribute("colorInteropID"),
                     "unknown");
    gamma_labeled.erase_attribute("colorInteropID");
    gamma_labeled.attribute("oiio:Gamma", 1.0f);
    pvt::finalize_resolved_color_metadata(gamma_labeled, config, "png",
                                          "frame.png");
    OIIO_CHECK_EQUAL(gamma_labeled.get_string_attribute("colorInteropID"),
                     "lin_rec709_scene");
    // An ICC profile this configuration cannot use still describes the
    // encoding more completely than the sRGB chunk beside it. Ordinary
    // finalization publishes the chunk's identity; a caller that writes the
    // profile out with the pixels asks for a source that establishes an
    // identity, and gets none here.
    ImageSpec unreadable_icc;
    unreadable_icc.attribute("ICCProfile",
                             TypeDesc(TypeDesc::UINT8,
                                      int(std::size(profile_byte))),
                             profile_byte);
    unreadable_icc.attribute("png:sRGB", 0);
    provenance = pvt::resolve_colorspace_source(config, unreadable_icc,
                                                "frame.png");
    OIIO_CHECK_ASSERT(provenance.source == Source::PNGsRGB);
    ImageSpec png_srgb = unreadable_icc;
    pvt::finalize_resolved_color_metadata(png_srgb, config, "png", "frame.png");
    OIIO_CHECK_EQUAL(png_srgb.get_string_attribute("colorInteropID"),
                     "srgb_rec709_scene");
    ImageSpec icc_out = unreadable_icc;
    pvt::finalize_resolved_color_metadata(icc_out, config, "png", "frame.png",
                                          {}, {}, true);
    OIIO_CHECK_EQUAL(icc_out.get_string_attribute("colorInteropID"), "");
    numeric.attribute("chromaticities", TypeDesc(TypeDesc::FLOAT, 8), custom);
    numeric.attribute("oiio:Gamma", 1.8f);
    provenance = pvt::resolve_colorspace_source(config, numeric, "frame.png",
                                                "", "", "", "", Rules::Fallback,
                                                Missing::ConfigPolicy, nullptr,
                                                /*synthesize=*/false);
    OIIO_CHECK_EQUAL(provenance.name, "unknown");
    OIIO_CHECK_ASSERT(provenance.terminal_unknown);
    provenance = pvt::resolve_colorspace_source(config, numeric, "frame.png");
    OIIO_CHECK_ASSERT(Strutil::starts_with(provenance.name, "<synthetic>"));
    OIIO_CHECK_ASSERT(provenance.source == Source::NumericMetadata);
    OIIO_CHECK_ASSERT(provenance.status == Status::Resolved);
    OIIO_CHECK_EQUAL(resolve_colorspace(config, numeric, "frame.png"),
                     provenance.name);
    ImageSpec finalized_custom = numeric;
    pvt::finalize_resolved_color_metadata(finalized_custom, config, "png",
                                          "frame.png");
    OIIO_CHECK_EQUAL(finalized_custom.get_string_attribute("colorInteropID"),
                     "");
    // A selector is a conversion endpoint and nothing else, so only a
    // resolution for a conversion mints one. The same facts resolved to
    // choose a label end as "unknown" (above), and set_colorspace()
    // therefore leaves the label unset rather than storing something that
    // means nothing outside this process.
    ImageSpec unlabeled_custom = numeric;
    config.set_colorspace(unlabeled_custom);
    OIIO_CHECK_ASSERT(!unlabeled_custom.find_attribute("oiio:ColorSpace"));
    ImageSpec gamma_only;
    gamma_only.attribute("oiio:Gamma", 2.2f);
    provenance = pvt::resolve_colorspace_source(config, gamma_only,
                                                "frame.png");
    OIIO_CHECK_EQUAL(provenance.name, "DisplayGamma22");
    OIIO_CHECK_ASSERT(provenance.source == Source::NumericMetadata);
    gamma_only.attribute("oiio:Gamma", 1.8f);
    provenance = pvt::resolve_colorspace_source(config, gamma_only, "frame.png",
                                                "", "", "", "", Rules::Fallback,
                                                Missing::ConfigPolicy, nullptr,
                                                /*synthesize=*/false);
    OIIO_CHECK_EQUAL(provenance.name, "unknown");
    provenance = pvt::resolve_colorspace_source(config, gamma_only,
                                                "frame.png");
    OIIO_CHECK_ASSERT(
        Strutil::starts_with(provenance.name, "<synthetic>display:g"));
    gamma_only.attribute("oiio:Gamma", 2.2f);
    gamma_only.attribute("oiio:PNGNumericState", "scene");
    provenance = pvt::resolve_colorspace_source(config, gamma_only,
                                                "frame.png");
    OIIO_CHECK_ASSERT(provenance.name != "DisplayGamma22");
    OIIO_CHECK_FALSE(provenance.name.empty());
    ImageSpec exr_numeric;
    exr_numeric.attribute("chromaticities", TypeDesc(TypeDesc::FLOAT, 8),
                          rec709);
    provenance = pvt::resolve_colorspace_source(config, exr_numeric,
                                                "frame.exr");
    OIIO_CHECK_EQUAL(provenance.name, "Linear");
    OIIO_CHECK_ASSERT(provenance.source == Source::NumericMetadata);

    // One effective context expands an explicit selector without changing the
    // ColorConfig's own context.
    OIIO_CHECK_EQUAL(resolve_colorspace(config, missing, "frame.exr", "$CS", "",
                                        "CS", "lin_rec709_scene"),
                     "Linear");
    OIIO_CHECK_EQUAL(resolve_colorspace(config, missing, "frame.exr", "$CS"),
                     "unknown");

    ImageSpec contradictory;
    contradictory.attribute("colorInteropID", "data");
    contradictory.attribute("chromaticities", TypeDesc(TypeDesc::FLOAT, 8),
                            rec709);
    contradictory.attribute("oiio:Gamma", 2.2f);
    contradictory.attribute("CICP", TypeDesc(TypeDesc::INT, 4), srgb);
    // Metadata warnings are reported alongside a trace.
    std::vector<pvt::ResolverStep> trace;
    provenance = pvt::resolve_colorspace_source(
        config, contradictory, "frame.exr", "", "", "", "",
        pvt::FileRulesPrecedence::Fallback, pvt::MissingColorSpace::Preserve,
        &trace);
    OIIO_CHECK_ASSERT(
        std::find(provenance.metadata_warnings.begin(),
                  provenance.metadata_warnings.end(),
                  "the bundle is tagged 'data' but carries a gamma")
        != provenance.metadata_warnings.end());

    // The same resolver under a shipped test config.
    {
        ColorConfig unit(OIIO_COLOR_TEST_CONFIG);
        OIIO_CHECK_FALSE(unit.has_error());
        const std::string srgb_tx = "sRGB Encoded Rec.709 (sRGB)";
        ImageSpec interop;
        interop.attribute("colorInteropID", "srgb_rec709_scene");
        provenance = pvt::resolve_colorspace_source(unit, interop,
                                                    "in_srgb_tx.exr");
        OIIO_CHECK_EQUAL(provenance.name, srgb_tx);
        OIIO_CHECK_ASSERT(provenance.source == Source::InteropID);
        OIIO_CHECK_ASSERT(provenance.status == Status::Resolved);
        OIIO_CHECK_FALSE(provenance.approximate);
        OIIO_CHECK_EQUAL(resolve_colorspace(unit, interop, "in_srgb_tx.exr", "",
                                            "", "", "", Rules::First),
                         srgb_tx);
        OIIO_CHECK_EQUAL(resolve_colorspace(unit, interop, "in_srgb_tx.exr", "",
                                            "", "", "", Rules::MetadataOnly),
                         srgb_tx);

        provenance = pvt::resolve_colorspace_source(unit, ImageSpec(),
                                                    "in_srgb_tx.exr", "", "",
                                                    "", "", Rules::First);
        OIIO_CHECK_EQUAL(provenance.name, srgb_tx);
        OIIO_CHECK_ASSERT(provenance.source == Source::FileRulesFirst);

        // A label this config cannot use is evidence that does not help:
        // everything else is exhausted, so the answer is "unknown".
        ImageSpec unclaimed;
        unclaimed.attribute("oiio:ColorSpace", "not-in-this-config");
        provenance = pvt::resolve_colorspace_source(unit, unclaimed,
                                                    "plate.exr");
        OIIO_CHECK_EQUAL(provenance.name, "unknown");
        OIIO_CHECK_ASSERT(provenance.terminal_unknown);
        OIIO_CHECK_ASSERT(provenance.status == Status::TerminalUnknown);

        provenance = pvt::resolve_colorspace_source(unit, ImageSpec(), "", "",
                                                    "", "", "", Rules::Fallback,
                                                    Missing::ConfigPolicy);
        OIIO_CHECK_EQUAL(provenance.name, "unknown");
        OIIO_CHECK_ASSERT(provenance.status == Status::TerminalUnknown);
        OIIO_CHECK_ASSERT(provenance.source == Source::TerminalPolicy);

        const int exact_sd[]       = { 6, 1, 0, 1 };
        const int approximate_sd[] = { 7, 1, 0, 1 };
        ImageSpec exact_cicp, approximate_cicp;
        exact_cicp.attribute("CICP", TypeDesc(TypeDesc::INT, 4), exact_sd);
        approximate_cicp.attribute("CICP", TypeDesc(TypeDesc::INT, 4),
                                   approximate_sd);
        auto exact       = pvt::resolve_colorspace_source(unit, exact_cicp,
                                                          "plate.png");
        auto approximate = pvt::resolve_colorspace_source(unit,
                                                          approximate_cicp,
                                                          "plate.png");
        OIIO_CHECK_EQUAL(exact.name, approximate.name);
        OIIO_CHECK_ASSERT(exact.source == Source::CICP);
        OIIO_CHECK_ASSERT(approximate.source == Source::CICP);
        OIIO_CHECK_ASSERT(exact.status == Status::Resolved);
        OIIO_CHECK_ASSERT(approximate.status == Status::Resolved);
        OIIO_CHECK_FALSE(exact.approximate);
        OIIO_CHECK_ASSERT(approximate.approximate);

        // This config defines no Adobe RGB space, so the complete numeric
        // facts (563/256 exactly) select the identity itself.
        ImageSpec numeric;
        numeric.attribute("chromaticities", TypeDesc(TypeDesc::FLOAT, 8),
                          adobe);
        numeric.attribute("oiio:Gamma", 2.19921875f);
        provenance = pvt::resolve_colorspace_source(unit, numeric, "plate.png");
        OIIO_CHECK_EQUAL(provenance.name, "g22_adobergb_display");
        OIIO_CHECK_ASSERT(provenance.source == Source::NumericMetadata);

        // Malformed evidence makes no claim; the later reader label remains
        // usable.
        const int malformed_cicp[] = { 1, 13, 0 };
        ImageSpec malformed;
        malformed.attribute("CICP", TypeDesc(TypeDesc::INT, 3), malformed_cicp);
        malformed.attribute("oiio:ColorSpace", "lin_rec709_scene");
        provenance = pvt::resolve_colorspace_source(unit, malformed,
                                                    "plate.exr");
        OIIO_CHECK_EQUAL(provenance.name, "Linear Rec.709 (sRGB)");
        OIIO_CHECK_ASSERT(provenance.source == Source::ReaderLabel);
    }

    // The terminal policy when an image states nothing at all. Strict
    // parsing answers "unknown", or a color space the config names or
    // aliases that, which is the config author's catch-space. Without
    // strict parsing the config's own default assignment answers.
    {
        const std::string strict_name = Filesystem::temp_directory_path() + "/"
                                        + Filesystem::unique_path() + ".ocio";
        const char* body
            = "ocio_profile_version: 2.3\n"
              "strictparsing: true\n"
              "roles: {default: Linear, scene_linear: Linear}\n"
              "file_rules:\n"
              "  - !<Rule> {name: Default, colorspace: Gamma22}\n"
              "colorspaces:\n"
              "  - !<ColorSpace>\n    name: Linear\n"
              "    aliases: [lin_rec709_scene]\n"
              "  - !<ColorSpace>\n    name: Gamma22\n"
              "    aliases: [g22_rec709_scene]\n"
              "    to_scene_reference: !<ExponentTransform> {value: 2.2}\n";
        OIIO_CHECK_ASSERT(Filesystem::write_text_file(strict_name, body));
        ColorConfig strict(strict_name);
        OIIO_CHECK_EQUAL(strict.geterror(false), "");
        OIIO_CHECK_EQUAL(resolve_colorspace(strict, ImageSpec(), "frame.exr",
                                            "", "", "", "", Rules::Fallback,
                                            Missing::ConfigPolicy),
                         "unknown");
        Filesystem::remove(strict_name);

        // Same config, plus a catch-space the author named "unknown".
        const std::string caught_name = Filesystem::temp_directory_path() + "/"
                                        + Filesystem::unique_path() + ".ocio";
        OIIO_CHECK_ASSERT(Filesystem::write_text_file(
            caught_name,
            std::string(body)
                + "  - !<ColorSpace>\n    name: unknown\n    isdata: true\n"
                  "environment: {EMPTY: \"\"}\n"));
        ColorConfig caught(caught_name);
        OIIO_CHECK_EQUAL(caught.geterror(false), "");
        auto terminal = pvt::resolve_colorspace_source(caught, ImageSpec(),
                                                       "frame.exr", "", "", "",
                                                       "", Rules::Fallback,
                                                       Missing::ConfigPolicy);
        OIIO_CHECK_EQUAL(terminal.name, "unknown");
        OIIO_CHECK_FALSE(terminal.terminal_unknown);
        OIIO_CHECK_ASSERT(terminal.status == Status::Resolved);
        // And it catches a file that said "unknown" too.
        ImageSpec said;
        said.attribute("colorInteropID", "unknown");
        terminal = pvt::resolve_colorspace_source(caught, said, "frame.exr");
        OIIO_CHECK_EQUAL(terminal.name, "unknown");
        OIIO_CHECK_FALSE(terminal.terminal_unknown);
        // A caller's own name is resolved under the effective context before
        // it is judged. One that expands to a real space is used; one that
        // expands to nothing named nothing, so the catch-space answers; one
        // that expands to a name this configuration lacks is the caller's
        // mistake and the catch-space does not answer it. A variable the
        // context does not define comes back unexpanded, which is such a
        // name and not one that resolved to nothing.
        OIIO_CHECK_EQUAL(resolve_colorspace(caught, ImageSpec(), "frame.exr",
                                            "$CS", "", "CS", "Gamma22"),
                         "Gamma22");
        terminal = pvt::resolve_colorspace_source(caught, ImageSpec(),
                                                  "frame.exr", "$EMPTY");
        OIIO_CHECK_EQUAL(terminal.name, "unknown");
        OIIO_CHECK_FALSE(terminal.terminal_unknown);
        terminal = pvt::resolve_colorspace_source(caught, ImageSpec(),
                                                  "frame.exr", "$CS", "", "CS",
                                                  "NoSuchSpace");
        OIIO_CHECK_EQUAL(terminal.name, "unknown");
        OIIO_CHECK_ASSERT(terminal.terminal_unknown);
        terminal = pvt::resolve_colorspace_source(caught, ImageSpec(),
                                                  "frame.exr", "$NOPE");
        OIIO_CHECK_EQUAL(terminal.name, "unknown");
        OIIO_CHECK_ASSERT(terminal.terminal_unknown);
        // The same four, spelled as the caller's failover, and the first two
        // in the Windows "%VAR%" spelling OpenColorIO also expands. A file
        // that said "unknown" would be placed in the catch-space by name
        // before the failover is reached, so these start from an image that
        // says nothing and let the configuration's policy run out.
        OIIO_CHECK_EQUAL(resolve_colorspace(caught, ImageSpec(), "frame.exr",
                                            "", "%CS%", "CS", "Gamma22",
                                            Rules::Fallback,
                                            Missing::ConfigPolicy),
                         "Gamma22");
        terminal = pvt::resolve_colorspace_source(caught, ImageSpec(),
                                                  "frame.exr", "", "%EMPTY%",
                                                  "", "", Rules::Fallback,
                                                  Missing::ConfigPolicy);
        OIIO_CHECK_EQUAL(terminal.name, "unknown");
        OIIO_CHECK_FALSE(terminal.terminal_unknown);
        terminal = pvt::resolve_colorspace_source(caught, ImageSpec(),
                                                  "frame.exr", "", "$CS", "CS",
                                                  "NoSuchSpace",
                                                  Rules::Fallback,
                                                  Missing::ConfigPolicy);
        OIIO_CHECK_EQUAL(terminal.name, "unknown");
        OIIO_CHECK_ASSERT(terminal.terminal_unknown);
        terminal = pvt::resolve_colorspace_source(caught, ImageSpec(),
                                                  "frame.exr", "", "$NOPE", "",
                                                  "", Rules::Fallback,
                                                  Missing::ConfigPolicy);
        OIIO_CHECK_EQUAL(terminal.name, "unknown");
        OIIO_CHECK_ASSERT(terminal.terminal_unknown);
        Filesystem::remove(caught_name);
    }

    Filesystem::remove(filename);
}


// The set_colorspace contract: a name is authoritative over the other color
// metadata, "" clears only the label, and no name resolves an unset label.
static void
test_set_colorspace_contract()
{
    if (!ColorConfig::supportsOpenColorIO())
        return;
    const ColorConfig& config(ColorConfig::default_colorconfig());
    const TypeDesc xytype(TypeDesc::FLOAT, 8), cicptype(TypeDesc::INT, 4);
    const float ap0[8] = { .7347f, .2653f, 0.0f,    1.0f,
                           .0001f, -.077f, .32168f, .33767f };
    const float rec709[8]
        = { .64f, .33f, .30f, .60f, .15f, .06f, .3127f, .3290f };
    const float adobe[8]
        = { .64f, .33f, .21f, .71f, .15f, .06f, .3127f, .3290f };
    auto xy = [&](const ImageSpec& spec) {
        const ParamValue* p = spec.find_attribute("chromaticities", xytype);
        return p ? std::vector<float>(p->as_cspan<float>().begin(),
                                      p->as_cspan<float>().end())
                 : std::vector<float>();
    };
    auto cicp = [&](const ImageSpec& spec) {
        const ParamValue* p = spec.find_attribute("CICP", cicptype);
        return p ? std::vector<int>(p->as_cspan<int>().begin(),
                                    p->as_cspan<int>().end())
                 : std::vector<int>();
    };
    auto is_rec709 = [&](const ImageSpec& spec) {
        std::vector<float> v = xy(spec);
        for (size_t i = 0; v.size() == 8 && i < 8; ++i)
            if (std::abs(v[i] - rec709[i]) > 1.0e-4f)
                return false;
        return v.size() == 8;
    };

    // Contradicting primaries are rewritten, agreeing ones kept, and a stale
    // ID follows the name. Setting the current name again still reconciles.
    for (const char* label : { "", "lin_rec709_scene" }) {
        ImageSpec spec(2, 2, 3, TypeFloat);
        if (*label)
            spec.attribute("oiio:ColorSpace", label);
        spec.attribute("chromaticities", xytype, ap0);
        spec.attribute("colorInteropID", "lin_ap0_scene");
        spec.attribute("acesImageContainerFlag", 1);
        spec.set_colorspace("lin_rec709_scene");
        OIIO_CHECK_EQUAL(spec.get_string_attribute("oiio:ColorSpace"),
                         "lin_rec709_scene");
        OIIO_CHECK_ASSERT(is_rec709(spec));
        OIIO_CHECK_EQUAL(spec.get_string_attribute("colorInteropID"),
                         "lin_rec709_scene");
        OIIO_CHECK_ASSERT(!spec.find_attribute("acesImageContainerFlag"));
    }
    {
        ImageSpec spec(2, 2, 3, TypeFloat);
        spec.attribute("chromaticities", xytype, rec709);
        spec.attribute("oiio:Gamma", 1.0f);
        spec.set_colorspace("lin_rec709_scene");
        OIIO_CHECK_ASSERT(is_rec709(spec));
        OIIO_CHECK_EQUAL(spec.get_float_attribute("oiio:Gamma"), 1.0f);
    }

    // CICP: primaries and transfer follow the name when it has a code, and
    // matrix and range stay; a name without a code removes it.
    {
        const int pq2020[4] = { 9, 16, 0, 1 };
        ImageSpec spec(2, 2, 3, TypeFloat);
        spec.attribute("CICP", cicptype, pq2020);
        spec.set_colorspace("lin_rec709_scene");
        OIIO_CHECK_ASSERT(cicp(spec) == std::vector<int>({ 1, 8, 0, 1 }));
        spec.attribute("CICP", cicptype, pq2020);
        spec.set_colorspace("g22_rec709_display");
        OIIO_CHECK_ASSERT(cicp(spec).empty());
        const int srgb[4] = { 1, 13, 0, 1 };
        spec.attribute("CICP", cicptype, srgb);
        spec.set_colorspace("srgb_rec709_scene");
        OIIO_CHECK_ASSERT(cicp(spec) == std::vector<int>({ 1, 13, 0, 1 }));
    }

    // An ICC profile cannot be checked or regenerated here, so it goes with
    // its decoded fields. Mastering display metadata is not touched.
    {
        const unsigned char icc[4] = { 1, 2, 3, 4 };
        ImageSpec spec(2, 2, 3, TypeFloat);
        spec.attribute("ICCProfile", TypeDesc(TypeDesc::UINT8, 4), icc);
        spec.attribute("ICCProfile:profile_version", "4.3.0");
        spec.attribute("mdcv_max_luminance", 1000.0f);
        spec.attribute("png:sRGB", 0);
        spec.set_colorspace("srgb_rec709_scene");
        OIIO_CHECK_ASSERT(!spec.find_attribute("ICCProfile"));
        OIIO_CHECK_ASSERT(!spec.find_attribute("ICCProfile:profile_version"));
        OIIO_CHECK_EQUAL(spec.get_float_attribute("mdcv_max_luminance"),
                         1000.0f);
        OIIO_CHECK_ASSERT(spec.find_attribute("png:sRGB"));
        spec.set_colorspace("g22_rec709_scene");
        OIIO_CHECK_ASSERT(!spec.find_attribute("png:sRGB"));
        OIIO_CHECK_EQUAL(spec.get_float_attribute("mdcv_max_luminance"),
                         1000.0f);
    }

    // An identity the config does not define keeps its own properties.
    {
        const std::string filename = Filesystem::temp_directory_path() + "/"
                                     + Filesystem::unique_path() + ".ocio";
        OIIO_CHECK_ASSERT(Filesystem::write_text_file(
            filename, "ocio_profile_version: 2.3\n"
                      "roles: {default: Linear, scene_linear: Linear}\n"
                      "colorspaces:\n"
                      "  - !<ColorSpace> {name: Linear}\n"));
        ColorConfig small(filename);
        ImageSpec spec(2, 2, 3, TypeFloat);
        spec.attribute("chromaticities", xytype, rec709);
        spec.attribute("oiio:Gamma", 2.2f);
        small.set_colorspace(spec, "g22_rec709_display");
        OIIO_CHECK_ASSERT(is_rec709(spec));
        OIIO_CHECK_EQUAL(spec.get_float_attribute("oiio:Gamma"), 2.2f);
        Filesystem::remove(filename);
    }

    // A name's image state settles "oiio:PNGNumericState": a display name
    // over a scene state removes it, and a state the name agrees with stays.
    {
        ImageSpec spec(2, 2, 3, TypeFloat);
        spec.attribute("oiio:PNGNumericState", "scene");
        spec.set_colorspace("g22_rec709_display");
        OIIO_CHECK_ASSERT(!spec.find_attribute("oiio:PNGNumericState"));
        spec.attribute("oiio:PNGNumericState", "scene");
        spec.set_colorspace("g22_rec709_scene");
        OIIO_CHECK_EQUAL(spec.get_string_attribute("oiio:PNGNumericState"),
                         "scene");
    }

    // A space that declares an ID only by alias has the state that ID names,
    // though the cheap query reports the ID's primaries without it.
    {
        const std::string filename = Filesystem::temp_directory_path() + "/"
                                     + Filesystem::unique_path() + ".ocio";
        OIIO_CHECK_ASSERT(Filesystem::write_text_file(
            filename,
            "ocio_profile_version: 2.3\n"
            "roles: {default: Plain}\n"
            "colorspaces:\n"
            "  - !<ColorSpace> {name: Plain}\n"
            "  - !<ColorSpace> {name: P3, aliases: [lin_p3d65_scene]}\n"));
        ColorConfig small(filename);
        ImageSpec spec(2, 2, 3, TypeFloat);
        spec.attribute("oiio:PNGNumericState", "scene");
        small.set_colorspace(spec, "P3");
        OIIO_CHECK_EQUAL(spec.get_string_attribute("oiio:PNGNumericState"),
                         "scene");
        Filesystem::remove(filename);
    }

    // "unknown" states that nothing is known, and takes the evidence. A
    // colorInteropID is rewritten to "unknown" rather than removed: the file
    // keeps saying that nothing is known about it.
    {
        ImageSpec spec(2, 2, 3, TypeFloat);
        spec.attribute("chromaticities", xytype, rec709);
        spec.attribute("oiio:Gamma", 2.2f);
        spec.attribute("Exif:ColorSpace", 1);
        spec.attribute("colorInteropID", "lin_rec709_scene");
        spec.attribute("mdcv_max_luminance", 1000.0f);
        spec.set_colorspace("unknown");
        OIIO_CHECK_EQUAL(spec.get_string_attribute("oiio:ColorSpace"),
                         "unknown");
        OIIO_CHECK_ASSERT(xy(spec).empty());
        OIIO_CHECK_ASSERT(!spec.find_attribute("oiio:Gamma"));
        OIIO_CHECK_ASSERT(!spec.find_attribute("Exif:ColorSpace"));
        OIIO_CHECK_EQUAL(spec.get_string_attribute("colorInteropID"),
                         "unknown");
        OIIO_CHECK_ASSERT(spec.find_attribute("mdcv_max_luminance"));
    }

    // "" clears the label and nothing else.
    {
        const int srgb[4] = { 1, 13, 0, 1 };
        ImageSpec spec(2, 2, 3, TypeFloat);
        spec.attribute("oiio:ColorSpace", "g22_rec709_scene");
        spec.attribute("chromaticities", xytype, rec709);
        spec.attribute("CICP", cicptype, srgb);
        spec.attribute("oiio:Gamma", 2.2f);
        spec.attribute("Exif:ColorSpace", 1);
        spec.attribute("tiff:PhotometricInterpretation", 2);
        const size_t before = spec.extra_attribs.size();
        spec.set_colorspace("");
        OIIO_CHECK_ASSERT(!spec.find_attribute("oiio:ColorSpace"));
        OIIO_CHECK_EQUAL(spec.extra_attribs.size(), before - 1);
        OIIO_CHECK_EQUAL(spec.get_float_attribute("oiio:Gamma"), 2.2f);
        config.set_colorspace(spec, std::string());
        OIIO_CHECK_EQUAL(spec.extra_attribs.size(), before - 1);
    }

    // No name resolves an unset label from the metadata, to a portable
    // answer only, and touches nothing else. A set label is left alone.
    for (int form = 0; form < 3; ++form) {
        auto resolve = [&](ImageSpec& spec) {
            if (form == 0)
                spec.set_colorspace();
            else if (form == 1)
                set_colorspace(spec, nullptr);
            else
                config.set_colorspace(spec, ustring());
        };
        ImageSpec spec(2, 2, 3, TypeFloat);
        spec.attribute("chromaticities", xytype, rec709);
        spec.attribute("oiio:Gamma", 2.2f);
        resolve(spec);
        OIIO_CHECK_EQUAL(config.get_color_interop_id(
                             spec.get_string_attribute("oiio:ColorSpace")),
                         "g22_rec709_scene");
        OIIO_CHECK_EQUAL(spec.extra_attribs.size(), 3);
        spec.attribute("chromaticities", xytype, ap0);
        resolve(spec);
        OIIO_CHECK_ASSERT(xy(spec)[0] == ap0[0]);
        OIIO_CHECK_EQUAL(config.get_color_interop_id(
                             spec.get_string_attribute("oiio:ColorSpace")),
                         "g22_rec709_scene");

        ImageSpec none(2, 2, 3, TypeFloat);
        none.attribute("chromaticities", xytype, adobe);
        none.attribute("oiio:Gamma", 2.2f);
        resolve(none);
        OIIO_CHECK_ASSERT(!none.find_attribute("oiio:ColorSpace"));
        OIIO_CHECK_EQUAL(none.extra_attribs.size(), 2);
    }
}



// ---------------------------------------------------------------------------
// ICC fixtures
//
// Built here rather than committed, so what each profile contains is
// auditable in the same file that states what it should mean, and so the
// bytes are identical on every platform. The colorant tags are the
// load-bearing part: ICC states them adapted to the D50 profile connection
// space, and OpenColorIO's ICC reader composes in its own fixed Bradford
// D50-to-D65 matrix when it decodes, so they are built here as that step's
// inverse and a decoded fixture recovers the primaries it was written from.
// ---------------------------------------------------------------------------

struct IccTag {
    std::string signature;
    std::vector<uint8_t> data;
};


static void
icc_put32(std::vector<uint8_t>& out, uint32_t value)
{
    out.push_back(uint8_t(value >> 24));
    out.push_back(uint8_t(value >> 16));
    out.push_back(uint8_t(value >> 8));
    out.push_back(uint8_t(value));
}


static void
icc_put16(std::vector<uint8_t>& out, uint16_t value)
{
    out.push_back(uint8_t(value >> 8));
    out.push_back(uint8_t(value));
}


static void
icc_put_s15f16(std::vector<uint8_t>& out, double value)
{
    icc_put32(out, uint32_t(int32_t(std::lround(value * 65536.0))));
}


static void
icc_put_sig(std::vector<uint8_t>& out, string_view text)
{
    out.insert(out.end(), text.begin(), text.end());
}


// XYZ triples. A plain array rather than Imath::V3d, whose operator[] in
// Imath 3.1 is (&x)[i], undefined behavior for i > 0 that Intel icpx 2023
// compiles to NaN in the loops below.
using IccVector = std::array<double, 3>;


// The ICC nCIEXYZ illuminant: the header value, the media white, and the
// destination of the colorant adaptation below.
static const IccVector icc_pcs_white { 0.9642, 1.0, 0.8249 };


// A structurally valid profile from a tag list. `profile_id` writes the v4
// header's optional profile ID field, which nothing in OIIO reads.
static std::vector<uint8_t>
icc_profile(const std::vector<IccTag>& tags, uint8_t profile_id = 0)
{
    std::vector<uint8_t> profile(128, 0);
    profile[8] = 0x02;  // profile version 2.4.0
    profile[9] = 0x40;
    std::memcpy(&profile[12], "mntr", 4);  // display device class
    std::memcpy(&profile[16], "RGB ", 4);  // device color space
    std::memcpy(&profile[20], "XYZ ", 4);  // profile connection space
    std::memcpy(&profile[36], "acsp", 4);  // magic
    std::vector<uint8_t> illuminant;
    for (int i = 0; i < 3; ++i)
        icc_put_s15f16(illuminant, icc_pcs_white[i]);
    std::memcpy(&profile[68], illuminant.data(), illuminant.size());
    if (profile_id)
        std::memset(&profile[84], profile_id, 16);

    std::vector<uint8_t> directory, body;
    icc_put32(directory, uint32_t(tags.size()));
    const size_t base = 128 + 4 + 12 * tags.size();
    for (const IccTag& tag : tags) {
        icc_put_sig(directory, tag.signature);
        icc_put32(directory, uint32_t(base + body.size()));
        icc_put32(directory, uint32_t(tag.data.size()));
        body.insert(body.end(), tag.data.begin(), tag.data.end());
        while (body.size() % 4)
            body.push_back(0);
    }
    profile.insert(profile.end(), directory.begin(), directory.end());
    profile.insert(profile.end(), body.begin(), body.end());
    std::vector<uint8_t> size;
    icc_put32(size, uint32_t(profile.size()));
    std::copy(size.begin(), size.end(), profile.begin());
    return profile;
}


static IccTag
icc_xyz_tag(const char* signature, const IccVector& xyz)
{
    IccTag tag { signature, {} };
    icc_put_sig(tag.data, "XYZ ");
    icc_put32(tag.data, 0);
    for (int i = 0; i < 3; ++i)
        icc_put_s15f16(tag.data, xyz[i]);
    return tag;
}


// A tone curve as a table, the common spelling of a measured response.
static IccTag
icc_curv_table(const char* signature, const std::vector<double>& values)
{
    IccTag tag { signature, {} };
    icc_put_sig(tag.data, "curv");
    icc_put32(tag.data, 0);
    icc_put32(tag.data, uint32_t(values.size()));
    for (double v : values)
        icc_put16(tag.data,
                  uint16_t(std::min(std::max(std::lround(v * 65535.0), 0L),
                                    65535L)));
    return tag;
}


// A single-entry curve, which ICC reads as an 8.8 fixed-point exponent.
static IccTag
icc_curv_gamma(const char* signature, double gamma)
{
    IccTag tag { signature, {} };
    icc_put_sig(tag.data, "curv");
    icc_put32(tag.data, 0);
    icc_put32(tag.data, 1);
    icc_put16(tag.data, uint16_t(std::lround(gamma * 256.0)));
    return tag;
}


// A type 3 parametric curve: c*X below d, (a*X + b)^g at and above it.
static IccTag
icc_para_type3(const char* signature, const std::vector<double>& params)
{
    IccTag tag { signature, {} };
    icc_put_sig(tag.data, "para");
    icc_put32(tag.data, 0);
    icc_put16(tag.data, 3);
    icc_put16(tag.data, 0);
    for (double p : params)
        icc_put_s15f16(tag.data, p);
    return tag;
}


static IccVector
icc_xy_to_xyz(double x, double y)
{
    return { x / y, 1.0, (1.0 - x - y) / y };
}


static IccVector
icc_apply(const Imath::M33d& m, const IccVector& v)
{
    IccVector out {};
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col)
            out[row] += m[row][col] * v[col];
    return out;
}


static Imath::M33d
icc_multiply(const Imath::M33d& a, const Imath::M33d& b)
{
    Imath::M33d out;
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col) {
            double v = 0.0;
            for (int k = 0; k < 3; ++k)
                v += a[row][k] * b[k][col];
            out[row][col] = v;
        }
    return out;
}


// rXYZ/gXYZ/bXYZ/wtpt for the given RGBW chromaticities: the normalized
// primary matrix, Bradford-adapted from the profile's own white to the
// connection space illuminant.
static std::vector<IccTag>
icc_matrix_tags(const std::vector<double>& primaries)
{
    const IccVector red   = icc_xy_to_xyz(primaries[0], primaries[1]);
    const IccVector green = icc_xy_to_xyz(primaries[2], primaries[3]);
    const IccVector blue  = icc_xy_to_xyz(primaries[4], primaries[5]);
    const IccVector white = icc_xy_to_xyz(primaries[6], primaries[7]);
    Imath::M33d columns(red[0], green[0], blue[0], red[1], green[1], blue[1],
                        red[2], green[2], blue[2]);
    const IccVector scale = icc_apply(columns.inverse(), white);
    Imath::M33d npm;
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col)
            npm[row][col] = columns[row][col] * scale[col];
    const Imath::M33d bradford(0.8951, 0.2664, -0.1614, -0.7502, 1.7135, 0.0367,
                               0.0389, -0.0685, 1.0296);
    const IccVector from = icc_apply(bradford, white);
    const IccVector to   = icc_apply(bradford, icc_pcs_white);
    Imath::M33d ratio;
    for (int i = 0; i < 3; ++i)
        ratio[i][i] = to[i] / from[i];
    const Imath::M33d adapted = icc_multiply(
        icc_multiply(bradford.inverse(), icc_multiply(ratio, bradford)), npm);
    return { icc_xyz_tag("rXYZ", IccVector { adapted[0][0], adapted[1][0],
                                             adapted[2][0] }),
             icc_xyz_tag("gXYZ", IccVector { adapted[0][1], adapted[1][1],
                                             adapted[2][1] }),
             icc_xyz_tag("bXYZ", IccVector { adapted[0][2], adapted[1][2],
                                             adapted[2][2] }),
             icc_xyz_tag("wtpt", icc_pcs_white) };
}


static std::vector<uint8_t>
icc_matrix_trc_profile(const std::vector<double>& primaries, const IccTag& trc,
                       uint8_t profile_id = 0)
{
    std::vector<IccTag> tags = icc_matrix_tags(primaries);
    for (const char* channel : { "rTRC", "gTRC", "bTRC" }) {
        IccTag copy    = trc;
        copy.signature = channel;
        tags.push_back(copy);
    }
    return icc_profile(tags, profile_id);
}


// Decode one RGB triple through a resolved color space into the config's own
// CIE XYZ D65 interchange space, and report where it landed plus that point's
// chromaticity.
static bool
icc_decode(const ColorConfig& config, string_view from, const float rgb[3],
           IccVector& xyz, double& x, double& y)
{
    auto processor = config.createColorProcessor(from, "XYZ");
    if (!processor)
        return false;
    float pixel[3] = { rgb[0], rgb[1], rgb[2] };
    processor->apply(pixel);
    xyz              = { pixel[0], pixel[1], pixel[2] };
    const double sum = xyz[0] + xyz[1] + xyz[2];
    if (!(std::abs(sum) > 1e-9))
        return false;
    x = xyz[0] / sum;
    y = xyz[1] / sum;
    return true;
}


// A supported ICC profile is measured and resolves like a declared identity;
// a decodable profile nothing reproduces becomes a conversion handle; a
// profile OpenColorIO cannot decode makes no claim at all.
static void
test_icc_identification()
{
    if (!ColorConfig::supportsOpenColorIO())
        return;
    const std::string filename = Filesystem::temp_directory_path() + "/"
                                 + Filesystem::unique_path() + ".ocio";
    // Both interchange roles, so a session handle can bridge out of this
    // config, and one authored display counterpart for sRGB, so an
    // identified profile has a local space to select.
    OIIO_CHECK_ASSERT(Filesystem::write_text_file(
        filename,
        "ocio_profile_version: 2.3\n"
        "roles: {default: Reference, scene_linear: Reference,"
        " aces_interchange: Reference, cie_xyz_d65_interchange: XYZ}\n"
        "file_rules:\n  - !<Rule> {name: Default, colorspace: default}\n"
        "colorspaces:\n  - !<ColorSpace>\n    name: Reference\n"
        "display_colorspaces:\n"
        "  - !<ColorSpace>\n    name: XYZ\n    encoding: display-linear\n"
        "  - !<ColorSpace>\n    name: Display sRGB\n"
        "    aliases: [srgb_rec709_display]\n    encoding: sdr-video\n"
        "    from_display_reference: !<GroupTransform>\n"
        "      children:\n"
        "        - !<MatrixTransform> {matrix: [3.24096994190452,"
        " -1.53738317757009, -0.498610760293003, 0, -0.96924363628088,"
        " 1.87596750150772, 0.0415550574071756, 0, 0.0556300796969936,"
        " -0.203976958888976, 1.05697151424288, 0, 0, 0, 0, 1]}\n"
        "        - !<ExponentWithLinearTransform> {gamma: 2.4, offset: 0.055,"
        " style: mirror, direction: inverse}\n"));
    ColorConfig config(filename);
    OIIO_CHECK_EQUAL(config.geterror(false), "");
    if (config.has_error()) {
        Filesystem::remove(filename);
        return;
    }

    const std::vector<double> rec709 { 0.64, 0.33, 0.30,   0.60,
                                       0.15, 0.06, 0.3127, 0.3290 };
    // Deliberately not a published gamut, so nothing can reproduce it.
    const std::vector<double> wide { 0.7347, 0.2653, 0.1596, 0.8404,
                                     0.0366, 0.0001, 0.3127, 0.3290 };
    std::vector<double> srgb_curve(1024);
    for (size_t i = 0; i < srgb_curve.size(); ++i) {
        const double v = double(i) / double(srgb_curve.size() - 1);
        srgb_curve[i]  = v <= 0.04045 ? v / 12.92
                                      : std::pow((v + 0.055) / 1.055, 2.4);
    }
    // The same curve as a table, as a native parametric curve, and a pure
    // power: the three shapes OpenColorIO's ICC reader supports.
    const auto tabulated
        = icc_matrix_trc_profile(rec709, icc_curv_table("rTRC", srgb_curve));
    const auto parametric = icc_matrix_trc_profile(
        rec709, icc_para_type3("rTRC", { 2.4, 1.0 / 1.055, 0.055 / 1.055,
                                         1.0 / 12.92, 0.04045 }));
    const auto pure_power = icc_matrix_trc_profile(wide,
                                                   icc_curv_gamma("rTRC", 1.8));
    // No colorant and no curve tags: outside the matrix/TRC model entirely.
    const auto unsupported = icc_profile(
        { icc_xyz_tag("wtpt", icc_pcs_white) });

    auto resolve = [&](const std::vector<uint8_t>& profile) {
        ImageSpec spec;
        spec.attribute("ICCProfile",
                       TypeDesc(TypeDesc::UINT8, int(profile.size())),
                       profile.data());
        spec.attribute("oiio:ColorSpace", "Reference");
        return resolve_colorspace(config, spec, "frame.png");
    };

    // Identified: measured against the internal reference, in the display
    // state the decode lands in, and answered with this config's own space.
    OIIO_CHECK_EQUAL(resolve(tabulated), "Display sRGB");
    OIIO_CHECK_EQUAL(resolve(parametric), "Display sRGB");
    // set_colorspace() with no name stores only a portable answer, never a
    // process-local selector. set_colorspace(name) keeps a profile that
    // identifies as the name's encoding, image state aside, and removes one
    // that does not.
    {
        auto with_icc = [&](const std::vector<uint8_t>& profile) {
            ImageSpec spec;
            spec.attribute("ICCProfile",
                           TypeDesc(TypeDesc::UINT8, int(profile.size())),
                           profile.data());
            return spec;
        };
        ImageSpec unmatched = with_icc(pure_power);
        config.set_colorspace(unmatched);
        OIIO_CHECK_ASSERT(unmatched.find_attribute("oiio:ColorSpace")
                          == nullptr);
        ImageSpec srgb = with_icc(tabulated);
        config.set_colorspace(srgb);
        OIIO_CHECK_EQUAL(srgb.get_string_attribute("oiio:ColorSpace"),
                         "Display sRGB");
        config.set_colorspace(srgb, "srgb_rec709_scene");
        OIIO_CHECK_ASSERT(srgb.find_attribute("ICCProfile"));
        config.set_colorspace(srgb, "Reference");
        OIIO_CHECK_ASSERT(srgb.find_attribute("ICCProfile") == nullptr);
    }
    {
        ImageSpec spec;
        spec.attribute("ICCProfile",
                       TypeDesc(TypeDesc::UINT8, int(tabulated.size())),
                       tabulated.data());
        const auto result = pvt::resolve_colorspace_source(config, spec,
                                                           "frame.png");
        OIIO_CHECK_ASSERT(result.source == pvt::ColorSpaceSource::ICC);
    }
    // Not decodable: no claim, so the reader's label below still applies and
    // no handle was minted for it.
    OIIO_CHECK_EQUAL(resolve(unsupported), "Reference");
    {
        ImageSpec spec;
        spec.attribute("ICCProfile",
                       TypeDesc(TypeDesc::UINT8, int(unsupported.size())),
                       unsupported.data());
        OIIO_CHECK_EQUAL(resolve_colorspace(config, spec, "frame.png"), "");
    }

    // Decodable, reproduced by nothing: a conversion handle, the same one on
    // every call and for every byte-identical copy, and not an identity.
    const std::string handle = resolve(pure_power);
    OIIO_CHECK_ASSERT(Strutil::starts_with(handle, "<synthetic>icc_"));
    OIIO_CHECK_EQUAL(resolve(pure_power), handle);
    OIIO_CHECK_EQUAL(resolve(std::vector<uint8_t>(pure_power)), handle);
    OIIO_CHECK_EQUAL(config.get_color_interop_id(handle), "");
    OIIO_CHECK_FALSE(config.isData(handle));
    OIIO_CHECK_FALSE(config.equivalent(handle, "Reference"));

    // The handle performs the profile's own decode, in the direction that
    // decodes: device values into CIE XYZ at D65. Each primary comes back at
    // the chromaticity the profile was written from, which is a statement
    // about the colorimetry rather than about the identity string, and an
    // encoding-direction transform could not produce it.
    IccVector xyz;
    double x = 0.0, y = 0.0;
    const float red[3] = { 1.0f, 0.0f, 0.0f };
    OIIO_CHECK_ASSERT(icc_decode(config, handle, red, xyz, x, y));
    const double red_x = x, red_y = y;
    OIIO_CHECK_EQUAL_THRESH(float(x), float(wide[0]), 1.0e-3f);
    OIIO_CHECK_EQUAL_THRESH(float(y), float(wide[1]), 1.0e-3f);
    const float green[3] = { 0.0f, 1.0f, 0.0f };
    OIIO_CHECK_ASSERT(icc_decode(config, handle, green, xyz, x, y));
    OIIO_CHECK_EQUAL_THRESH(float(x), float(wide[2]), 1.0e-3f);
    OIIO_CHECK_EQUAL_THRESH(float(y), float(wide[3]), 1.0e-3f);
    const float grey[3] = { 0.5f, 0.5f, 0.5f };
    OIIO_CHECK_ASSERT(icc_decode(config, handle, grey, xyz, x, y));
    OIIO_CHECK_EQUAL_THRESH(float(x), float(wide[6]), 1.0e-3f);
    OIIO_CHECK_EQUAL_THRESH(float(y), float(wide[7]), 1.0e-3f);
    // The 1.8 exponent, applied as a decode. Encoding it would give 0.7009.
    OIIO_CHECK_EQUAL_THRESH(float(xyz[1]), std::pow(0.5f, 1.8f), 2.0e-3f);

    // A real bidirectional endpoint: the round trip through this config's
    // scene reference returns the values it started from.
    ImageBuf src(ImageSpec(1, 1, 3, TypeDesc::FLOAT));
    OIIO_CHECK_ASSERT(ImageBufAlgo::fill(src, { 0.25f, 0.5f, 0.75f }));
    ImageBuf linear = ImageBufAlgo::colorconvert(src, handle, "Reference",
                                                 false, "", "", &config);
    OIIO_CHECK_FALSE(linear.has_error());
    ImageBuf back = ImageBufAlgo::colorconvert(linear, "Reference", handle,
                                               false, "", "", &config);
    OIIO_CHECK_FALSE(back.has_error());
    float out[3] = { 0.0f, 0.0f, 0.0f };
    back.getpixel(0, 0, make_span(out));
    const float expected[3] = { 0.25f, 0.5f, 0.75f };
    for (int c = 0; c < 3; ++c)
        OIIO_CHECK_EQUAL_THRESH(out[c], expected[c], 2.0e-3f);
    // The handle is process-shared, not owned by the wrapper that minted it.
    ColorConfig second(filename);
    OIIO_CHECK_EQUAL(second.geterror(false), "");
    IccVector elsewhere;
    double x2 = 0.0, y2 = 0.0;
    OIIO_CHECK_ASSERT(icc_decode(second, handle, red, elsewhere, x2, y2));
    OIIO_CHECK_EQUAL_THRESH(float(x2), float(red_x), 1.0e-6f);
    OIIO_CHECK_EQUAL_THRESH(float(y2), float(red_y), 1.0e-6f);

    // Two valid profiles that declare the same v4 header profile ID are two
    // different profiles. Nothing here reads that field, so they are named
    // and converted by their bytes, in whichever order they arrive.
    const std::vector<double> other { 0.7000, 0.2900, 0.1700, 0.8000,
                                      0.0500, 0.0100, 0.3127, 0.3290 };
    const auto twin_a
        = icc_matrix_trc_profile(wide, icc_curv_gamma("rTRC", 1.8), 0x5A);
    const auto twin_b
        = icc_matrix_trc_profile(other, icc_curv_gamma("rTRC", 2.0), 0x5A);
    const std::string first_a = resolve(twin_a);
    const std::string first_b = resolve(twin_b);
    const std::string again_b = resolve(twin_b);
    const std::string again_a = resolve(twin_a);
    OIIO_CHECK_ASSERT(Strutil::starts_with(first_a, "<synthetic>icc_"));
    OIIO_CHECK_ASSERT(Strutil::starts_with(first_b, "<synthetic>icc_"));
    OIIO_CHECK_ASSERT(first_a != first_b);
    OIIO_CHECK_EQUAL(again_a, first_a);
    OIIO_CHECK_EQUAL(again_b, first_b);
    // And they convert differently, whichever one was seen first.
    double xa = 0.0, ya = 0.0, xb = 0.0, yb = 0.0;
    OIIO_CHECK_ASSERT(icc_decode(config, first_a, red, xyz, xa, ya));
    OIIO_CHECK_ASSERT(icc_decode(config, first_b, red, xyz, xb, yb));
    OIIO_CHECK_EQUAL_THRESH(float(xa), float(wide[0]), 1.0e-3f);
    OIIO_CHECK_EQUAL_THRESH(float(ya), float(wide[1]), 1.0e-3f);
    OIIO_CHECK_EQUAL_THRESH(float(xb), float(other[0]), 1.0e-3f);
    OIIO_CHECK_EQUAL_THRESH(float(yb), float(other[1]), 1.0e-3f);

    // An embedded cicpTag still names the signal before any decoding does.
    {
        std::vector<IccTag> tags = icc_matrix_tags(wide);
        for (const char* channel : { "rTRC", "gTRC", "bTRC" })
            tags.push_back(icc_curv_gamma(channel, 1.8));
        IccTag cicp { "cicp", {} };
        icc_put_sig(cicp.data, "cicp");
        icc_put32(cicp.data, 0);
        cicp.data.insert(cicp.data.end(), { 1, 13, 0, 1 });
        tags.push_back(cicp);
        OIIO_CHECK_EQUAL(resolve(icc_profile(tags)), "Display sRGB");
    }
    // The payload is four code bytes after the type header. A short cicpTag
    // is not read past its declared extent and makes no claim.
    {
        IccTag cicp { "cicp", {} };
        icc_put_sig(cicp.data, "cicp");
        icc_put32(cicp.data, 0);
        cicp.data.insert(cicp.data.end(), { 1, 13, 0 });
        // The padding byte after it would complete a full-range sRGB tuple.
        auto profile                             = icc_profile({ cicp });
        profile[128 + 4 + 12 + cicp.data.size()] = 1;
        OIIO_CHECK_EQUAL(resolve(profile), "Reference");
        // Like any other malformed tag, it stops decoding the profile.
        ImageSpec decoded;
        std::string error;
        OIIO_CHECK_FALSE(decode_icc_profile(profile, decoded, error));
    }

    // Nothing is written back to the caller's metadata.
    {
        ImageSpec spec;
        spec.attribute("ICCProfile",
                       TypeDesc(TypeDesc::UINT8, int(pure_power.size())),
                       pure_power.data());
        spec.attribute("colorInteropID", "lin_rec709_scene");
        OIIO_CHECK_EQUAL(resolve_colorspace(config, spec, "frame.png"),
                         "lin_rec709_scene");
        OIIO_CHECK_EQUAL(spec.get_string_attribute("colorInteropID"),
                         "lin_rec709_scene");
    }

    // Over the retention bound: refused before anything is decoded or held.
    {
        std::vector<uint8_t> huge = pure_power;
        huge.resize(size_t(16) * 1024 * 1024 + 4, 0);
        std::vector<uint8_t> size;
        icc_put32(size, uint32_t(huge.size()));
        std::copy(size.begin(), size.end(), huge.begin());
        OIIO_CHECK_EQUAL(resolve(huge), "Reference");
    }
    Filesystem::remove(filename);
}



// Chromaticities and a gamma that name no published encoding still describe
// a conversion. The assertions here are colorimetric, against the declared
// coordinates rather than against anything the synthesis path produced.
static void
test_numeric_synthesis()
{
    if (!ColorConfig::supportsOpenColorIO())
        return;
    const std::string filename = Filesystem::temp_directory_path() + "/"
                                 + Filesystem::unique_path() + ".ocio";
    OIIO_CHECK_ASSERT(Filesystem::write_text_file(
        filename,
        "ocio_profile_version: 2.3\n"
        "roles: {default: Reference, scene_linear: Reference,"
        " aces_interchange: Reference, cie_xyz_d65_interchange: XYZ}\n"
        "file_rules:\n  - !<Rule> {name: Default, colorspace: default}\n"
        // A scene-referred endpoint reaching a display-referred one is
        // connected inside the config that holds the display space, so this
        // one needs its own bridge for the assertions below.
        "default_view_transform: bridge\n"
        "view_transforms:\n  - !<ViewTransform>\n    name: bridge\n"
        "    from_scene_reference: !<BuiltinTransform>"
        " {style: UTILITY - ACES-AP0_to_CIE-XYZ-D65_BFD}\n"
        "colorspaces:\n  - !<ColorSpace>\n    name: Reference\n"
        "display_colorspaces:\n"
        "  - !<ColorSpace>\n    name: XYZ\n    encoding: display-linear\n"));
    ColorConfig config(filename);
    OIIO_CHECK_EQUAL(config.geterror(false), "");
    if (config.has_error()) {
        Filesystem::remove(filename);
        return;
    }

    // Not a published gamut, and a gamma no reference encoding uses with it.
    const float xy[8] = { 0.695f, 0.305f, 0.140f,  0.820f,
                          0.100f, 0.005f, 0.3127f, 0.3290f };
    ImageSpec spec;
    spec.attribute("chromaticities", TypeDesc(TypeDesc::FLOAT, 8), xy);
    spec.attribute("oiio:Gamma", 1.8f);
    const std::string handle = resolve_colorspace(config, spec, "frame.png");
    OIIO_CHECK_ASSERT(Strutil::starts_with(handle, "<synthetic>"));
    OIIO_CHECK_EQUAL(resolve_colorspace(config, spec, "frame.png"), handle);
    // A conversion handle, not an identity.
    OIIO_CHECK_EQUAL(config.get_color_interop_id(handle), "");
    OIIO_CHECK_FALSE(config.isData(handle));

    // Each declared primary lands at the chromaticity it was declared at,
    // and the exponent is applied as a decode.
    IccVector xyz;
    double x = 0.0, y = 0.0;
    const float red[3] = { 1.0f, 0.0f, 0.0f };
    OIIO_CHECK_ASSERT(icc_decode(config, handle, red, xyz, x, y));
    OIIO_CHECK_EQUAL_THRESH(float(x), xy[0], 1.0e-3f);
    OIIO_CHECK_EQUAL_THRESH(float(y), xy[1], 1.0e-3f);
    const float blue[3] = { 0.0f, 0.0f, 1.0f };
    OIIO_CHECK_ASSERT(icc_decode(config, handle, blue, xyz, x, y));
    OIIO_CHECK_EQUAL_THRESH(float(x), xy[4], 1.0e-3f);
    OIIO_CHECK_EQUAL_THRESH(float(y), xy[5], 1.0e-3f);
    const float grey[3] = { 0.5f, 0.5f, 0.5f };
    OIIO_CHECK_ASSERT(icc_decode(config, handle, grey, xyz, x, y));
    OIIO_CHECK_EQUAL_THRESH(float(x), xy[6], 1.0e-3f);
    OIIO_CHECK_EQUAL_THRESH(float(y), xy[7], 1.0e-3f);
    OIIO_CHECK_EQUAL_THRESH(float(xyz[1]), std::pow(0.5f, 1.8f), 1.0e-4f);

    // Gamma alone supplies a transfer-only endpoint, while chromaticities
    // that do not span a plane describe no conversion and remain "unknown".
    ImageSpec bare;
    bare.attribute("oiio:Gamma", 1.8f);
    const std::string bare_handle = resolve_colorspace(config, bare,
                                                       "frame.png");
    OIIO_CHECK_ASSERT(
        Strutil::starts_with(bare_handle, "<synthetic>display:g"));
    OIIO_CHECK_ASSERT(icc_decode(config, bare_handle, grey, xyz, x, y));
    for (int c = 0; c < 3; ++c)
        OIIO_CHECK_EQUAL_THRESH(float(xyz[c]), std::pow(0.5f, 1.8f), 1.0e-4f);
    const float collinear[8] = { 0.3f, 0.3f, 0.4f,    0.4f,
                                 0.5f, 0.5f, 0.3127f, 0.3290f };
    ImageSpec degenerate;
    degenerate.attribute("chromaticities", TypeDesc(TypeDesc::FLOAT, 8),
                         collinear);
    degenerate.attribute("oiio:Gamma", 1.8f);
    OIIO_CHECK_EQUAL(resolve_colorspace(config, degenerate, "frame.png"),
                     "unknown");
    const float impossible[8] = { 0.64f, 0.0f,  0.30f,   0.60f,
                                  0.15f, 0.06f, 0.3127f, 0.3290f };
    degenerate.attribute("chromaticities", TypeDesc(TypeDesc::FLOAT, 8),
                         impossible);
    OIIO_CHECK_EQUAL(resolve_colorspace(config, degenerate, "frame.png"),
                     "unknown");
    Filesystem::remove(filename);
}


// A configuration for the session-handle tests: both interchange roles, a
// display-referred CIE XYZ, and a bridge so a scene-referred handle can reach
// it. `extra` continues the display_colorspaces sequence, or opens a new
// top-level block after it.
static std::string
session_test_config(const std::string& extra)
{
    const std::string path = Filesystem::temp_directory_path() + "/"
                             + Filesystem::unique_path() + ".ocio";
    OIIO_CHECK_ASSERT(Filesystem::write_text_file(
        path, "ocio_profile_version: 2.3\n"
              "roles: {default: Reference, scene_linear: Reference,"
              " aces_interchange: Reference, cie_xyz_d65_interchange: XYZ}\n"
              "file_rules:\n  - !<Rule> {name: Default, colorspace: default}\n"
              "default_view_transform: bridge\n"
              "view_transforms:\n  - !<ViewTransform>\n    name: bridge\n"
              "    from_scene_reference: !<BuiltinTransform>"
              " {style: UTILITY - ACES-AP0_to_CIE-XYZ-D65_BFD}\n"
              "colorspaces:\n  - !<ColorSpace>\n    name: Reference\n"
              "display_colorspaces:\n"
              "  - !<ColorSpace>\n    name: XYZ\n    encoding: display-linear\n"
                  + extra));
    return path;
}


// A configuration that already uses a session selector's spelling keeps it.
// The file that produced that spelling still converts, through a
// collision-free handle that performs its own decode, and the authored
// definition still answers for its own name. Checked once where the
// configuration owns the spelling through an alias, and once where it owns it
// as a named transform, because both are consulted before a session handle is.
static void
test_session_selector_collision()
{
    if (!ColorConfig::supportsOpenColorIO())
        return;
    const std::string plain = session_test_config("");
    ColorConfig config(plain);
    OIIO_CHECK_EQUAL(config.geterror(false), "");
    if (config.has_error()) {
        Filesystem::remove(plain);
        return;
    }

    // Published by nothing, so both facts below reach synthesis rather than
    // an identity, and distinct from the fixtures the other tests mint.
    const std::vector<double> unnamed { 0.7100, 0.2900, 0.1300, 0.8300,
                                        0.0500, 0.0050, 0.3127, 0.3290 };
    const auto profile = icc_matrix_trc_profile(unnamed,
                                                icc_curv_gamma("rTRC", 2.0));
    ImageSpec icc_spec;
    icc_spec.attribute("ICCProfile",
                       TypeDesc(TypeDesc::UINT8, int(profile.size())),
                       profile.data());
    const std::string icc_handle = resolve_colorspace(config, icc_spec,
                                                      "frame.png");
    OIIO_CHECK_ASSERT(Strutil::starts_with(icc_handle, "<synthetic>icc_"));

    // The same spelling, as an alias on an unrelated Rec.709 display space.
    const std::string collide = session_test_config(
        "  - !<ColorSpace>\n    name: Collide\n    aliases: [\"" + icc_handle
        + "\"]\n    encoding: display-linear\n"
          "    to_display_reference: !<MatrixTransform> {matrix:"
          " [0.4123907992659595, 0.3575843393838780, 0.1804807884018343, 0,"
          " 0.2126390058715104, 0.7151686787677559, 0.0721923153607337, 0,"
          " 0.0193308187155918, 0.1191947797946259, 0.9505321522496608, 0,"
          " 0, 0, 0, 1]}\n");
    ColorConfig owner(collide);
    OIIO_CHECK_EQUAL(owner.geterror(false), "");
    if (owner.has_error()) {
        Filesystem::remove(plain);
        Filesystem::remove(collide);
        return;
    }

    // Resolution steps past the spelling the configuration owns, and does so
    // the same way on every repeat.
    const std::string owned = resolve_colorspace(owner, icc_spec, "frame.png");
    OIIO_CHECK_ASSERT(Strutil::starts_with(owned, "<synthetic>icc_"));
    OIIO_CHECK_ASSERT(owned != icc_handle);
    OIIO_CHECK_EQUAL(resolve_colorspace(owner, icc_spec, "frame.png"), owned);

    // What comes back converts the profile: red lands where the profile put
    // it, not where the authored space would have put it.
    IccVector xyz;
    double x = 0.0, y = 0.0;
    const float red[3] = { 1.0f, 0.0f, 0.0f };
    OIIO_CHECK_ASSERT(icc_decode(owner, owned, red, xyz, x, y));
    OIIO_CHECK_EQUAL_THRESH(float(x), float(unnamed[0]), 1.0e-3f);
    OIIO_CHECK_EQUAL_THRESH(float(y), float(unnamed[1]), 1.0e-3f);
    // And the authored alias still means the space it was authored on.
    OIIO_CHECK_ASSERT(icc_decode(owner, icc_handle, red, xyz, x, y));
    OIIO_CHECK_EQUAL_THRESH(float(x), 0.64f, 1.0e-3f);
    OIIO_CHECK_EQUAL_THRESH(float(y), 0.33f, 1.0e-3f);
    // The configuration that never used the spelling still gets it, and it
    // still decodes the profile there.
    OIIO_CHECK_EQUAL(resolve_colorspace(config, icc_spec, "frame.png"),
                     icc_handle);
    OIIO_CHECK_ASSERT(icc_decode(config, icc_handle, red, xyz, x, y));
    OIIO_CHECK_EQUAL_THRESH(float(x), float(unnamed[0]), 1.0e-3f);
    OIIO_CHECK_EQUAL_THRESH(float(y), float(unnamed[1]), 1.0e-3f);

    // The numeric spelling is fully predictable, so the same collision is
    // easier to build there. A named transform is enough to own it.
    const float xy[8] = { 0.6800f, 0.3050f, 0.1450f, 0.8100f,
                          0.0900f, 0.0150f, 0.3127f, 0.3290f };
    ImageSpec numeric;
    numeric.attribute("chromaticities", TypeDesc(TypeDesc::FLOAT, 8), xy);
    numeric.attribute("oiio:Gamma", 1.9f);
    const std::string custom = resolve_colorspace(config, numeric, "frame.png");
    OIIO_CHECK_ASSERT(Strutil::starts_with(custom, "<synthetic>"));
    // The completed local-precedence proof is shared across wrappers of the
    // same effective configuration, and the selector remains stable.
    ColorConfig repeated(plain);
    OIIO_CHECK_EQUAL(repeated.geterror(false), "");
    if (!repeated.has_error()) {
        OIIO_CHECK_EQUAL(resolve_colorspace(repeated, numeric, "frame.png"),
                         custom);
        OIIO_CHECK_EQUAL(resolve_colorspace(repeated, numeric, "frame.png"),
                         custom);
    }
    const float xy2[8] = { 0.7000f, 0.2900f, 0.1700f, 0.8000f,
                           0.0500f, 0.0100f, 0.3127f, 0.3290f };
    ImageSpec numeric2;
    numeric2.attribute("chromaticities", TypeDesc(TypeDesc::FLOAT, 8), xy2);
    numeric2.attribute("oiio:Gamma", 2.0f);
    const std::string custom2 = resolve_colorspace(config, numeric2,
                                                   "frame.png");
    OIIO_CHECK_ASSERT(Strutil::starts_with(custom2, "<synthetic>"));
    OIIO_CHECK_ASSERT(custom2 != custom);

    // Both endpoints are session-owned. A supported context-variable file
    // inside a non-identity named look must sit between the two bridges, and
    // its inverse must undo the whole chain when the endpoints are exchanged.
    const std::string look_directory = Filesystem::temp_directory_path() + "/"
                                       + Filesystem::unique_path();
    OIIO_CHECK_ASSERT(Filesystem::create_directory(look_directory));
    auto gain_file = [&](string_view name, int gain) {
        return Filesystem::write_text_file(
            Strutil::fmt::format("{}/gain_{}.ctf", look_directory, name),
            Strutil::fmt::format(
                "<ProcessList version=\"1.3\" id=\"gain\">\n"
                "  <Matrix inBitDepth=\"32f\" outBitDepth=\"32f\">\n"
                "    <Array dim=\"3 3\">{} 0 0 0 {} 0 0 0 {}</Array>\n"
                "  </Matrix>\n</ProcessList>\n",
                gain, gain, gain));
    };
    OIIO_CHECK_ASSERT(gain_file("two", 2));
    OIIO_CHECK_ASSERT(gain_file("three", 3));
    const std::string look_config_file = look_directory + "/look.ocio";
    OIIO_CHECK_ASSERT(Filesystem::write_text_file(
        look_config_file,
        "ocio_profile_version: 2.3\n"
        "environment: {GAIN: two}\nsearch_path: .\n"
        "roles: {default: Reference, scene_linear: Reference,"
        " aces_interchange: Reference, cie_xyz_d65_interchange: XYZ}\n"
        "default_view_transform: bridge\n"
        "view_transforms:\n  - !<ViewTransform>\n    name: bridge\n"
        "    from_scene_reference: !<BuiltinTransform>"
        " {style: UTILITY - ACES-AP0_to_CIE-XYZ-D65_BFD}\n"
        "colorspaces:\n  - !<ColorSpace> {name: Reference}\n"
        "  - !<ColorSpace> {name: Raw, isdata: true}\n"
        "display_colorspaces:\n  - !<ColorSpace> {name: XYZ,"
        " encoding: display-linear}\n"
        "looks:\n  - !<Look>\n    name: gain\n"
        "    process_space: Reference\n"
        "    transform: !<FileTransform> {src: gain_$GAIN.ctf}\n"));
    ColorConfig look_config(look_config_file);
    OIIO_CHECK_EQUAL(look_config.geterror(false), "");
    ImageBuf look_src(ImageSpec(1, 1, 3, TypeDesc::FLOAT));
    OIIO_CHECK_ASSERT(ImageBufAlgo::fill(look_src, { 0.10f, 0.20f, 0.30f }));
    ImageBuf gain2 = ImageBufAlgo::ociolook(look_src, "gain", custom, custom2,
                                            false, false, "GAIN", "two",
                                            &look_config);
    ImageBuf gain3 = ImageBufAlgo::ociolook(look_src, "gain", custom, custom2,
                                            false, false, "GAIN", "three",
                                            &look_config);
    const bool gain2_ok = !gain2.has_error();
    const bool gain3_ok = !gain3.has_error();
    OIIO_CHECK_ASSERT(gain2_ok);
    OIIO_CHECK_ASSERT(gain3_ok);
    if (!gain2_ok) {
        const std::string error = gain2.geterror();
        OIIO_CHECK_EQUAL(error, "");
    }
    if (!gain3_ok) {
        const std::string error = gain3.geterror();
        OIIO_CHECK_EQUAL(error, "");
    }
    if (gain2_ok && gain3_ok) {
        float p2[3] = {}, p3[3] = {};
        gain2.getpixel(0, 0, make_span(p2));
        gain3.getpixel(0, 0, make_span(p3));
        OIIO_CHECK_ASSERT(std::abs(p2[0] - p3[0]) > 1.0e-3f);
        ImageBuf undone = ImageBufAlgo::ociolook(gain2, "gain", custom2, custom,
                                                 false, true, "GAIN", "two",
                                                 &look_config);
        const bool undone_ok = !undone.has_error();
        OIIO_CHECK_ASSERT(undone_ok);
        if (!undone_ok) {
            const std::string error = undone.geterror();
            OIIO_CHECK_EQUAL(error, "");
        } else {
            float restored[3] = {};
            undone.getpixel(0, 0, make_span(restored));
            const float look_input[3] = { 0.10f, 0.20f, 0.30f };
            for (int c = 0; c < 3; ++c)
                OIIO_CHECK_EQUAL_THRESH(restored[c], look_input[c], 2.0e-3f);
        }
        OIIO_CHECK_EQUAL(gain2.spec().get_string_attribute("colorInteropID"),
                         "unknown");
        OIIO_CHECK_ASSERT(gain2.spec().find_attribute("chromaticities")
                          == nullptr);
    }

    // Data input bypasses the look for pixels, while retaining the same
    // destination-tagging contract as an identity processor. A complete
    // output names the explicit destination; a partial output is mixed and
    // therefore unknown. In both cases stale source evidence is removed.
    ImageBuf data_src(ImageSpec(2, 1, 3, TypeDesc::FLOAT));
    OIIO_CHECK_ASSERT(ImageBufAlgo::fill(data_src, { 0.15f, 0.25f, 0.35f }));
    data_src.specmod().set_colorspace("Raw");
    data_src.specmod().attribute("chromaticities", TypeDesc(TypeDesc::FLOAT, 8),
                                 xy);
    data_src.specmod().attribute("oiio:Gamma", 1.9f);
    data_src.specmod().attribute("colorInteropID", "stale-source-id");
    ImageBuf data_full = ImageBufAlgo::ociolook(data_src, "gain", "Raw",
                                                "Reference", false, false, "",
                                                "", &look_config);
    const bool data_full_ok = !data_full.has_error();
    OIIO_CHECK_ASSERT(data_full_ok);
    float data_pixel[3] = {};
    if (!data_full_ok) {
        const std::string error = data_full.geterror();
        OIIO_CHECK_EQUAL(error, "");
    } else {
        data_full.getpixel(0, 0, make_span(data_pixel));
        for (int c = 0; c < 3; ++c)
            OIIO_CHECK_EQUAL_THRESH(data_pixel[c], 0.15f + 0.10f * c, 1.0e-7f);
        OIIO_CHECK_EQUAL(data_full.spec().get_string_attribute(
                             "oiio:ColorSpace"),
                         "Reference");
        OIIO_CHECK_ASSERT(
            data_full.spec().get_string_attribute("colorInteropID")
            != "stale-source-id");
        OIIO_CHECK_ASSERT(data_full.spec().find_attribute("chromaticities")
                          == nullptr);
        OIIO_CHECK_ASSERT(data_full.spec().find_attribute("oiio:Gamma")
                          == nullptr);
    }

    ROI one_pixel(0, 1, 0, 1, 0, 1, 0, 3);
    ImageBuf data_partial;
    OIIO_CHECK_ASSERT(ImageBufAlgo::copy(data_partial, data_src));
    const bool data_partial_ok
        = ImageBufAlgo::ociolook(data_partial, data_src, "gain", "Raw",
                                 "Reference", false, false, "", "",
                                 &look_config, one_pixel);
    OIIO_CHECK_ASSERT(data_partial_ok);
    if (!data_partial_ok) {
        const std::string error = data_partial.geterror();
        OIIO_CHECK_EQUAL(error, "");
    } else {
        data_partial.getpixel(0, 0, make_span(data_pixel));
        for (int c = 0; c < 3; ++c)
            OIIO_CHECK_EQUAL_THRESH(data_pixel[c], 0.15f + 0.10f * c, 1.0e-7f);
        OIIO_CHECK_EQUAL(data_partial.spec().get_string_attribute(
                             "oiio:ColorSpace"),
                         "unknown");
        OIIO_CHECK_EQUAL(data_partial.spec().get_string_attribute(
                             "colorInteropID"),
                         "unknown");
        OIIO_CHECK_ASSERT(data_partial.spec().find_attribute("chromaticities")
                          == nullptr);
        OIIO_CHECK_ASSERT(data_partial.spec().find_attribute("oiio:Gamma")
                          == nullptr);
    }
    // A data destination leaves the pixels alone too.
    ImageBuf into_data = ImageBufAlgo::ociolook(data_src, "gain", "Reference",
                                                "Raw", false, false, "", "",
                                                &look_config);
    OIIO_CHECK_ASSERT(!into_data.has_error());
    into_data.getpixel(0, 0, make_span(data_pixel));
    for (int c = 0; c < 3; ++c)
        OIIO_CHECK_EQUAL_THRESH(data_pixel[c], 0.15f + 0.10f * c, 1.0e-7f);

    Filesystem::remove_all(look_directory);

    const std::string named = session_test_config(
        "named_transforms:\n  - !<NamedTransform>\n    name: \"" + custom
        + "\"\n    transform: !<MatrixTransform> {matrix:"
          " [2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 1]}\n");
    ColorConfig transformed(named);
    OIIO_CHECK_EQUAL(transformed.geterror(false), "");
    if (!transformed.has_error()) {
        const std::string stepped = resolve_colorspace(transformed, numeric,
                                                       "frame.png");
        OIIO_CHECK_ASSERT(Strutil::starts_with(stepped, "<synthetic>"));
        OIIO_CHECK_ASSERT(stepped != custom);
        OIIO_CHECK_EQUAL(resolve_colorspace(transformed, numeric, "frame.png"),
                         stepped);
        // A proof from the plain configuration cannot bypass this
        // configuration's authored-name precedence.
        OIIO_CHECK_EQUAL(resolve_colorspace(config, numeric, "frame.png"),
                         custom);
        // The authored named transform keeps its name.
        const auto names = transformed.getNamedTransformNames();
        OIIO_CHECK_ASSERT(std::find(names.begin(), names.end(), custom)
                          != names.end());
        // And the handle converts the declared primaries, not the scale by 2
        // the named transform would have applied.
        OIIO_CHECK_ASSERT(icc_decode(transformed, stepped, red, xyz, x, y));
        OIIO_CHECK_EQUAL_THRESH(float(x), xy[0], 1.0e-3f);
        OIIO_CHECK_EQUAL_THRESH(float(y), xy[1], 1.0e-3f);
    }

    // Exhaust every selector spelling for a fresh ICC payload. Minting
    // declines rather than guesses, so no session selector comes back.
    const auto exhausted_profile
        = icc_matrix_trc_profile(unnamed, icc_curv_gamma("rTRC", 2.1));
    const std::string exhausted_base
        = "<synthetic>icc_"
          + Strutil::lower(
              SHA1::digest(exhausted_profile.data(), exhausted_profile.size()));
    std::string aliases;
    for (int n = 0; n <= 64; ++n) {
        if (n)
            aliases += ", ";
        aliases += "\"" + exhausted_base
                   + (n ? Strutil::fmt::format("@{}", n) : std::string())
                   + "\"";
    }
    const std::string exhausted = session_test_config(
        "  - !<ColorSpace>\n    name: Exhausted selectors\n    aliases: ["
        + aliases + "]\n    encoding: display-linear\n");
    ColorConfig exhausted_config(exhausted);
    OIIO_CHECK_EQUAL(exhausted_config.geterror(false), "");
    if (!exhausted_config.has_error()) {
        ImageSpec exhausted_spec;
        exhausted_spec.attribute("ICCProfile",
                                 TypeDesc(TypeDesc::UINT8,
                                          int(exhausted_profile.size())),
                                 exhausted_profile.data());
        OIIO_CHECK_FALSE(Strutil::starts_with(
            resolve_colorspace(exhausted_config, exhausted_spec, "frame.png"),
            "<synthetic>"));
    }

    // Concurrent duplicates publish one numeric endpoint and every caller
    // receives that stable selector.
    const float concurrent_xy[8] = { 0.6900f, 0.2950f, 0.1550f, 0.8150f,
                                     0.0750f, 0.0120f, 0.3127f, 0.3290f };
    ImageSpec concurrent_spec;
    concurrent_spec.attribute("chromaticities", TypeDesc(TypeDesc::FLOAT, 8),
                              concurrent_xy);
    concurrent_spec.attribute("oiio:Gamma", 1.7f);
    std::array<std::string, 8> concurrent_names;
    std::vector<std::thread> threads;
    for (size_t i = 0; i < concurrent_names.size(); ++i)
        threads.emplace_back([&, i] {
            concurrent_names[i] = resolve_colorspace(config, concurrent_spec,
                                                     "frame.png");
        });
    for (auto& thread : threads)
        thread.join();
    for (const auto& name : concurrent_names) {
        OIIO_CHECK_ASSERT(Strutil::starts_with(name, "<synthetic>"));
        OIIO_CHECK_EQUAL(name, concurrent_names[0]);
    }

    Filesystem::remove(exhausted);
    Filesystem::remove(named);
    Filesystem::remove(collide);
    Filesystem::remove(plain);
}


// Virtual primaries are ordinary primaries. AP0's blue sits below the x axis
// and its green at x = 0, and camera native gamuts are wider still, so a
// coordinate outside the spectral triangle describes a conversion like any
// other. What is refused is a description with no conversion in it: a
// coordinate that names no direction in XYZ, and a value with no wire
// spelling at all.
static void
test_virtual_primary_gamut()
{
    if (!ColorConfig::supportsOpenColorIO())
        return;
    const std::string filename = session_test_config("");
    ColorConfig config(filename);
    OIIO_CHECK_EQUAL(config.geterror(false), "");
    if (config.has_error()) {
        Filesystem::remove(filename);
        return;
    }

    // Sony S-Gamut3.Venice: a shipping camera gamut whose blue primary has a
    // negative y, at its own D65 white so the bridge returns the declared
    // coordinates rather than adapted ones. The exponent belongs to no
    // published encoding with these primaries, so this reaches synthesis.
    const float venice[8] = { 0.74046426f, 0.27936437f, 0.08924115f,
                              0.89380953f, 0.11048824f, -0.05257933f,
                              0.3127f,     0.3290f };
    ImageSpec spec;
    spec.attribute("chromaticities", TypeDesc(TypeDesc::FLOAT, 8), venice);
    spec.attribute("oiio:Gamma", 1.8f);
    const std::string handle = resolve_colorspace(config, spec, "frame.png");
    OIIO_CHECK_ASSERT(Strutil::starts_with(handle, "<synthetic>"));

    IccVector xyz;
    double x = 0.0, y = 0.0;
    const float red[3] = { 1.0f, 0.0f, 0.0f };
    OIIO_CHECK_ASSERT(icc_decode(config, handle, red, xyz, x, y));
    OIIO_CHECK_EQUAL_THRESH(float(x), venice[0], 1.0e-3f);
    OIIO_CHECK_EQUAL_THRESH(float(y), venice[1], 1.0e-3f);
    const float green[3] = { 0.0f, 1.0f, 0.0f };
    OIIO_CHECK_ASSERT(icc_decode(config, handle, green, xyz, x, y));
    OIIO_CHECK_EQUAL_THRESH(float(x), venice[2], 1.0e-3f);
    OIIO_CHECK_EQUAL_THRESH(float(y), venice[3], 1.0e-3f);
    // The primary the spectral triangle would have refused.
    const float blue[3] = { 0.0f, 0.0f, 1.0f };
    OIIO_CHECK_ASSERT(icc_decode(config, handle, blue, xyz, x, y));
    OIIO_CHECK_EQUAL_THRESH(float(x), venice[4], 1.0e-3f);
    OIIO_CHECK_EQUAL_THRESH(float(y), venice[5], 1.0e-3f);
    const float grey[3] = { 0.5f, 0.5f, 0.5f };
    OIIO_CHECK_ASSERT(icc_decode(config, handle, grey, xyz, x, y));
    OIIO_CHECK_EQUAL_THRESH(float(x), venice[6], 1.0e-3f);
    OIIO_CHECK_EQUAL_THRESH(float(y), venice[7], 1.0e-3f);
    OIIO_CHECK_EQUAL_THRESH(float(xyz[1]), std::pow(0.5f, 1.8f), 1.0e-4f);

    // AP0 itself, whose green is at x = 0 and whose blue is at y = -0.077.
    // Its white is D60, so the assertion is on the white the adaptation
    // lands at rather than on the declared coordinates.
    const float ap0[8] = { 0.7347f, 0.2653f,  0.0f,     1.0f,
                           0.0001f, -0.0770f, 0.32168f, 0.33767f };
    ImageSpec aces;
    aces.attribute("chromaticities", TypeDesc(TypeDesc::FLOAT, 8), ap0);
    aces.attribute("oiio:Gamma", 1.8f);
    const std::string ap0_handle = resolve_colorspace(config, aces,
                                                      "frame.png");
    OIIO_CHECK_ASSERT(Strutil::starts_with(ap0_handle, "<synthetic>"));
    OIIO_CHECK_ASSERT(icc_decode(config, ap0_handle, grey, xyz, x, y));
    OIIO_CHECK_EQUAL_THRESH(float(x), 0.3127f, 1.0e-3f);
    OIIO_CHECK_EQUAL_THRESH(float(y), 0.3290f, 1.0e-3f);
    OIIO_CHECK_EQUAL_THRESH(float(xyz[1]), std::pow(0.5f, 1.8f), 1.0e-4f);

    // Finite, and still outside what the wire can hold: declined before
    // anything is rounded or narrowed, not converted out of range, and so
    // evidence that does not help.
    const float unrepresentable[8] = { 1.0e20f, 0.3f,  0.30f,   0.60f,
                                       0.15f,   0.06f, 0.3127f, 0.3290f };
    ImageSpec extreme;
    extreme.attribute("chromaticities", TypeDesc(TypeDesc::FLOAT, 8),
                      unrepresentable);
    extreme.attribute("oiio:Gamma", 1.8f);
    OIIO_CHECK_EQUAL(resolve_colorspace(config, extreme, "frame.png"),
                     "unknown");
    // A positive exponent so small its reciprocal has no wire spelling, and
    // one so large the wire rounds it away.
    ImageSpec exponent;
    exponent.attribute("chromaticities", TypeDesc(TypeDesc::FLOAT, 8), venice);
    exponent.attribute("oiio:Gamma", 1.0e-30f);
    OIIO_CHECK_EQUAL(resolve_colorspace(config, exponent, "frame.png"),
                     "unknown");
    exponent.attribute("oiio:Gamma", 1.0e9f);
    OIIO_CHECK_EQUAL(resolve_colorspace(config, exponent, "frame.png"),
                     "unknown");
    Filesystem::remove(filename);
}


// A deterministically unsupported profile mints no endpoint. Its bytes may
// remain in the bounded verdict memo, which can be cleared independently of
// admitted session endpoints. This checks the observable behavior: distinct
// unsupported profiles keep declining, and an earlier handle stays usable.
static void
test_icc_unsupported_retention()
{
    if (!ColorConfig::supportsOpenColorIO())
        return;
    const std::string filename = session_test_config("");
    ColorConfig config(filename);
    OIIO_CHECK_EQUAL(config.geterror(false), "");
    if (config.has_error()) {
        Filesystem::remove(filename);
        return;
    }

    const std::vector<double> unnamed { 0.7050, 0.2950, 0.1250, 0.8250,
                                        0.0450, 0.0100, 0.3127, 0.3290 };
    const auto usable = icc_matrix_trc_profile(unnamed,
                                               icc_curv_gamma("rTRC", 2.1));
    auto resolve      = [&](const std::vector<uint8_t>& profile) {
        ImageSpec spec;
        spec.attribute("ICCProfile",
                       TypeDesc(TypeDesc::UINT8, int(profile.size())),
                       profile.data());
        return resolve_colorspace(config, spec, "frame.png");
    };
    const std::string handle = resolve(usable);
    OIIO_CHECK_ASSERT(Strutil::starts_with(handle, "<synthetic>icc_"));
    IccVector xyz;
    double x = 0.0, y = 0.0;
    const float red[3] = { 1.0f, 0.0f, 0.0f };
    OIIO_CHECK_ASSERT(icc_decode(config, handle, red, xyz, x, y));
    const double before_x = x, before_y = y;

    // Structurally valid, no colorant and no curve tags, and each one a
    // distinct payload: outside the matrix/TRC model, so each is measured,
    // declines, and creates no endpoint.
    std::vector<uint8_t> last;
    for (int i = 0; i < 32; ++i) {
        last = icc_profile(
            { icc_xyz_tag("wtpt",
                          IccVector { icc_pcs_white[0] + 0.0001 * (i + 1), 1.0,
                                      icc_pcs_white[2] }) });
        OIIO_CHECK_EQUAL(resolve(last), "");
    }
    // The same bytes again, now answered from the memo if it survived and
    // re-measured if it did not. Either way the verdict is the same.
    OIIO_CHECK_EQUAL(resolve(last), "");

    // The usable handle is untouched by all of it: same selector, same decode.
    OIIO_CHECK_EQUAL(resolve(usable), handle);
    OIIO_CHECK_ASSERT(icc_decode(config, handle, red, xyz, x, y));
    OIIO_CHECK_EQUAL_THRESH(float(x), float(before_x), 1.0e-6f);
    OIIO_CHECK_EQUAL_THRESH(float(y), float(before_y), 1.0e-6f);
    Filesystem::remove(filename);
}


// A raw ImageOutput copy of an ImageInput spec has no source container. The
// OpenEXR writer keeps the label's ID for a PNG sRGB chunk or a bare gamma,
// which contradict nothing, and withholds it beside chromaticities. Beside an
// ID in the header, chromaticities survive exactly where nothing establishes
// them to disagree with it, in encoding or in gamut.
static void
test_exr_writer_copy_identity()
{
    if (!ColorConfig::supportsOpenColorIO())
        return;
    const std::string base = Filesystem::temp_directory_path() + "/"
                             + Filesystem::unique_path();
    auto write = [](const std::string& name, const ImageSpec& spec) {
        auto out = ImageOutput::create(name);
        std::vector<float> pixels(spec.image_pixels() * spec.nchannels, 0.5f);
        return out && out->open(name, spec)
               && out->write_image(TypeFloat, pixels.data()) && out->close();
    };
    bool copy_kept_chromaticities = false;
    auto exr_copy_id              = [&](const ImageSpec& spec) {
        const std::string exr = base + ".exr";
        OIIO_CHECK_ASSERT(write(exr, spec));
        auto in        = ImageInput::open(exr);
        std::string id = in ? in->spec().get_string_attribute("colorInteropID")
                            : "<unreadable>";
        copy_kept_chromaticities
            = in && in->spec().find_attribute("chromaticities") != nullptr;
        in.reset();
        Filesystem::remove(exr);
        return id;
    };
    auto png_copy_id = [&](string_view label) {
        const std::string png = base + ".png";
        ImageSpec spec(2, 2, 3, TypeUInt8);
        spec.set_colorspace(label);
        OIIO_CHECK_ASSERT(write(png, spec));
        auto in = ImageInput::open(png);
        OIIO_CHECK_ASSERT(in);
        ImageSpec read = in ? in->spec() : ImageSpec();
        in.reset();
        Filesystem::remove(png);
        OIIO_CHECK_ASSERT(read.find_attribute("oiio:Gamma"));
        return exr_copy_id(read);
    };
    // OIIO's PNG writer emits sRGB, gAMA and cHRM for sRGB, gAMA for gamma.
    OIIO_CHECK_EQUAL(png_copy_id("srgb_rec709_scene"), "srgb_rec709_scene");
    // The cHRM primaries describe a display encoding, which an OpenEXR
    // chromaticities attribute cannot state. Only the ID is written.
    OIIO_CHECK_FALSE(copy_kept_chromaticities);
    OIIO_CHECK_EQUAL(png_copy_id("g22_rec709_scene"), "g22_rec709_scene");
    const float custom[8] = { 0.705f, 0.295f, 0.125f,  0.825f,
                              0.045f, 0.010f, 0.3127f, 0.3290f };
    ImageSpec contradicted(2, 2, 3, TypeFloat);
    contradicted.set_colorspace("g22_rec709_scene");
    contradicted.attribute("oiio:Gamma", 2.2f);
    contradicted.attribute("chromaticities", TypeDesc(TypeDesc::FLOAT, 8),
                           custom);
    OIIO_CHECK_EQUAL(exr_copy_id(contradicted), "unknown");
    // With no ID, the gamma of 2.2 beside them says those chromaticities
    // state no linear encoding, so they are not written either, and the file
    // says "unknown" rather than nothing. The linear case below keeps them.
    OIIO_CHECK_FALSE(copy_kept_chromaticities);
    // A gamma that contradicts the label wins the same way.
    ImageSpec gamma_contradicted(2, 2, 3, TypeFloat);
    gamma_contradicted.set_colorspace("g22_rec709_scene");
    gamma_contradicted.attribute("oiio:Gamma", 1.8f);
    OIIO_CHECK_EQUAL(exr_copy_id(gamma_contradicted), "");
    // Chromaticities that agree with the label are redundant beside its ID
    // (Color Interop Forum Recommendation 04).
    const float rec709_label[8] = { 0.64f, 0.33f, 0.30f,   0.60f,
                                    0.15f, 0.06f, 0.3127f, 0.3290f };
    const float ap0_label[8]    = { 0.7347f, 0.2653f, 0.0f,     1.0f,
                                    0.0001f, -0.077f, 0.32168f, 0.33767f };
    ImageSpec agreed(2, 2, 3, TypeFloat);
    agreed.set_colorspace("lin_rec709_scene");
    agreed.attribute("chromaticities", TypeDesc(TypeDesc::FLOAT, 8),
                     rec709_label);
    OIIO_CHECK_EQUAL(exr_copy_id(agreed), "lin_rec709_scene");
    OIIO_CHECK_FALSE(copy_kept_chromaticities);
    // ImageBuf::write applies the same rule.
    auto ibuf_copy_id = [&](const ImageSpec& spec) {
        const std::string exr = base + "-ibuf.exr";
        OIIO_CHECK_ASSERT(ImageBuf(spec).write(exr));
        auto in        = ImageInput::open(exr);
        std::string id = in ? in->spec().get_string_attribute("colorInteropID")
                            : "<unreadable>";
        copy_kept_chromaticities
            = in && in->spec().find_attribute("chromaticities") != nullptr;
        in.reset();
        Filesystem::remove(exr);
        return id;
    };
    OIIO_CHECK_EQUAL(ibuf_copy_id(agreed), "lin_rec709_scene");
    OIIO_CHECK_FALSE(copy_kept_chromaticities);
    ImageSpec disagreed = agreed;
    disagreed.attribute("chromaticities", TypeDesc(TypeDesc::FLOAT, 8),
                        ap0_label);
    OIIO_CHECK_EQUAL(ibuf_copy_id(disagreed), "unknown");
    OIIO_CHECK_FALSE(copy_kept_chromaticities);
    OIIO_CHECK_EQUAL(exr_copy_id(disagreed), "unknown");
    OIIO_CHECK_FALSE(copy_kept_chromaticities);

    // An OpenEXR file that already states its primaries. The ID an ImageBuf
    // copy derives from them says what they say, so the copy states both and
    // the standard attribute every current reader understands survives a
    // plain passthrough.
    const float rec709[8] = { 0.64f, 0.33f, 0.30f,   0.60f,
                              0.15f, 0.06f, 0.3127f, 0.3290f };
    ImageSpec carried(2, 2, 3, TypeFloat);
    carried.attribute("chromaticities", TypeDesc(TypeDesc::FLOAT, 8), rec709);
    // An explicit gamma of 1 is the writer's documented linear carve-out.
    carried.attribute("oiio:Gamma", 1.0f);
    const std::string carried_name = base + "-carried.exr";
    const std::string copied_name  = base + "-copied.exr";
    OIIO_CHECK_ASSERT(write(carried_name, carried));
    ImageBuf copy(carried_name);
    OIIO_CHECK_ASSERT(copy.read(0, 0, true, TypeFloat));
    OIIO_CHECK_ASSERT(copy.write(copied_name));
    auto reread = ImageInput::open(copied_name);
    OIIO_CHECK_ASSERT(reread);
    if (reread) {
        const ImageSpec& got = reread->spec();
        OIIO_CHECK_EQUAL(got.get_string_attribute("colorInteropID"),
                         "lin_rec709_scene");
        const ParamValue* chroma = got.find_attribute("chromaticities");
        const bool eight = chroma
                           && chroma->type() == TypeDesc(TypeDesc::FLOAT, 8);
        OIIO_CHECK_ASSERT(eight);
        const float* xy = eight ? (const float*)chroma->data() : nullptr;
        for (int i = 0; xy && i < 8; ++i)
            OIIO_CHECK_EQUAL_THRESH(xy[i], rec709[i], 1.0e-6f);
    }
    reread.reset();
    Filesystem::remove(carried_name);
    Filesystem::remove(copied_name);

    // A linear ID is not restated by whatever primaries the spec happens to
    // hold. Beside primaries this build establishes to be a different gamut
    // the two claims contradict exactly as a non-linear ID does, so only the
    // ID is written.
    const float ap0[8] = { 0.7347f, 0.2653f, 0.0f,     1.0f,
                           0.0001f, -0.077f, 0.32168f, 0.33767f };
    ImageSpec mismatched(2, 2, 3, TypeFloat);
    mismatched.attribute("colorInteropID", "lin_rec709_scene");
    mismatched.attribute("chromaticities", TypeDesc(TypeDesc::FLOAT, 8), ap0);
    OIIO_CHECK_EQUAL(exr_copy_id(mismatched), "lin_rec709_scene");
    OIIO_CHECK_FALSE(copy_kept_chromaticities);

    // An ID whose encoding nothing here establishes does not prove the pixels
    // linear, so its chromaticities are omitted. A namespaced ID this
    // configuration does not define is that case.
    ImageSpec unestablished(2, 2, 3, TypeFloat);
    unestablished.attribute("colorInteropID", "acme:private_scene");
    unestablished.attribute("chromaticities", TypeDesc(TypeDesc::FLOAT, 8),
                            rec709);
    OIIO_CHECK_EQUAL(exr_copy_id(unestablished), "acme:private_scene");
    OIIO_CHECK_FALSE(copy_kept_chromaticities);

    // Beside an ICC profile the writer resolves the spec itself, taking only
    // a rule that establishes an identity. A profile it cannot interpret
    // outranks the PNG sRGB chunk beside it, so neither the chunk's ID nor
    // the label's is written.
    const unsigned char unreadable[] = { 0 };
    ImageSpec icc_srgb(2, 2, 3, TypeFloat);
    icc_srgb.set_colorspace("srgb_rec709_scene");
    icc_srgb.attribute("png:sRGB", 0);
    icc_srgb.attribute("ICCProfile", TypeDesc(TypeDesc::UINT8, 1), unreadable);
    OIIO_CHECK_EQUAL(exr_copy_id(icc_srgb), "");
    // But complete chromaticities and gamma beside that profile are read the
    // generic way and do supply an ID, where the same facts alone supply
    // none, and the file says "unknown".
    ImageSpec icc_numeric(2, 2, 3, TypeFloat);
    icc_numeric.attribute("chromaticities", TypeDesc(TypeDesc::FLOAT, 8),
                          rec709);
    icc_numeric.attribute("oiio:Gamma", 2.2f);
    OIIO_CHECK_EQUAL(exr_copy_id(icc_numeric), "unknown");
    icc_numeric.attribute("ICCProfile", TypeDesc(TypeDesc::UINT8, 1),
                          unreadable);
    OIIO_CHECK_EQUAL(exr_copy_id(icc_numeric), "g22_rec709_scene");

    // Where libpng can write cICP, the writer records the explicitly asserted
    // scene identity in the PNG, since a CICP code states no image state and
    // this one reads back as g22_rec709_scene. An ImageBuf copy then preserves
    // it with or without a repeated assertion. With an older libpng the writer
    // falls back to gAMA and cHRM, which are display-referred by convention,
    // so only a repeated assertion keeps the scene identity.
    const std::string png_state = base + "-state.png";
    ImageSpec png_spec(2, 2, 3, TypeUInt8);
    png_spec.set_colorspace("g22_rec709_scene");
    OIIO_CHECK_ASSERT(write(png_state, png_spec));
    bool png_has_cicp = false;
    if (auto in = ImageInput::open(png_state))
        png_has_cicp = in->spec().find_attribute("CICP") != nullptr;
    auto buf_copy_id = [&](bool assert_scene) {
        const std::string exr = base + "-state.exr";
        ImageBuf buf(png_state);
        OIIO_CHECK_ASSERT(buf.read(0, 0, true, TypeFloat));
        // Beside cICP, OIIO's own PNG writer emits no cHRM for a gamma space,
        // so supply the primaries a PNG that carries both chunks would be
        // read with.
        buf.specmod().attribute("chromaticities", TypeDesc(TypeDesc::FLOAT, 8),
                                rec709);
        if (assert_scene)
            buf.specmod().set_colorspace("g22_rec709_scene");
        OIIO_CHECK_ASSERT(buf.write(exr));
        auto in        = ImageInput::open(exr);
        std::string id = in ? in->spec().get_string_attribute("colorInteropID")
                            : "<unreadable>";
        in.reset();
        Filesystem::remove(exr);
        return id;
    };
    OIIO_CHECK_EQUAL(buf_copy_id(false),
                     png_has_cicp ? "g22_rec709_scene" : "g22_rec709_display");
    OIIO_CHECK_EQUAL(buf_copy_id(true), "g22_rec709_scene");
    Filesystem::remove(png_state);
}


static void
test_ociolook_default_config()
{
    if (!ColorConfig::supportsOpenColorIO())
        return;
    ImageBuf src(ImageSpec(1, 1, 3, TypeDesc::FLOAT)), dst;
    src.specmod().set_colorspace("scene_linear");
    OIIO_CHECK_ASSERT(ImageBufAlgo::fill(src, { 0.25f, 0.5f, 0.75f }));
    // Empty endpoints request the current encoding. Install the default null
    // configuration before resolving that implicit source.
    OIIO_CHECK_ASSERT(ImageBufAlgo::ociolook(dst, src, "", "", "", true, false,
                                             "", "", nullptr));
    OIIO_CHECK_FALSE(dst.has_error());
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
    test_declared_color_space_info();
    test_gamma_pair_conversion();
    test_interop_id_memo();
    test_recognized_id_equivalence();
    test_measured_equality_id();
    test_declared_id_equivalence();
    test_color_space_info();
    test_color_space_info_concurrent();
    test_numeric_recognition();
    test_gamut_recognition();
    test_naming_versus_measurement();
    test_spi_conventions();
    test_color_space_info_context();
    test_encoding_tie_break();
    test_private_properties();
    test_metadata_resolution();
    test_set_colorspace_contract();
    test_icc_identification();
    test_numeric_synthesis();
    test_session_selector_collision();
    test_virtual_primary_gamut();
    test_icc_unsupported_retention();
    test_exr_writer_copy_identity();
    test_ociolook_default_config();

    return unit_test_failures != 0;
}
