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



// The public cheap color-space property surface: ColorSpaceInfo +
// ColorConfig::get_color_space_info (scalar and batch). Field semantics
// (direct facts computed; derivation-requiring facts uncomputed; range
// never guessed), the error convention, batch order/duplicates, and the
// reserved options fields being accepted and ignored.
static void
test_color_space_info()
{
    using F = ColorSpaceInfoField;

    // A default-constructed info is the one honest "nothing" object -- no
    // OCIO needed for that check.
    {
        ColorSpaceInfo none;
        OIIO_CHECK_FALSE(none.valid());
        OIIO_CHECK_EQUAL(none.name(), "");
        OIIO_CHECK_EQUAL(none.encoding(), "");
        OIIO_CHECK_ASSERT(none.chromaticities().empty());
        OIIO_CHECK_EQUAL(int(none.transfer_function_kind()),
                         int(ColorTransferFunctionKind::Undetermined));
        OIIO_CHECK_FALSE(none.computed(F::ImageState));
        OIIO_CHECK_FALSE(none.available(F::ImageState));
        OIIO_CHECK_FALSE(none.derived(F::ImageState));
    }

    if (!ColorConfig::supportsOpenColorIO())
        return;

    // Fixture: a scene space named for a static-table interop id (the cheap
    // table tier identifies it with no interop_id attribute, so this is
    // OCIO-version-independent), a data space, a display space, and a space
    // with no authored facts at all.
    static const char* config_yaml = R"(ocio_profile_version: 2.1
name: infocfg
search_path: ""
roles:
  default: ref
  scene_linear: ref
displays:
  disp:
    - !<View> {name: main, colorspace: screen}
display_colorspaces:
  - !<ColorSpace>
    name: screen
    encoding: sdr-video
    from_display_reference: !<ExponentTransform> {value: [2.4, 2.4, 2.4, 1]}
colorspaces:
  - !<ColorSpace>
    name: ref

  - !<ColorSpace>
    name: srgb_rec709_scene
    aliases: [my_srgb]
    encoding: sdr-video
    from_scene_reference: !<ExponentTransform> {value: [2.2, 2.2, 2.2, 1]}

  - !<ColorSpace>
    name: rawdata
    isdata: true

  - !<ColorSpace>
    name: plain_space
    from_scene_reference: !<ExponentTransform> {value: [1.8, 1.8, 1.8, 1]}
)";
    std::string config_path        = Filesystem::temp_directory_path()
                              + "/oiio_color_test_info.ocio";
    OIIO_CHECK_ASSERT(Filesystem::write_text_file(config_path, config_yaml));
    ColorConfig cc(config_path);
    OIIO_CHECK_ASSERT(!cc.has_error());

    // Direct facts of a fully described scene space, queried by alias (the
    // record reports the canonical name).
    {
        ColorSpaceInfo info = cc.get_color_space_info("my_srgb");
        OIIO_CHECK_ASSERT(info.valid());
        OIIO_CHECK_ASSERT(!cc.has_error());
        OIIO_CHECK_EQUAL(info.name(), "srgb_rec709_scene");
        OIIO_CHECK_ASSERT(info.computed(F::ImageState)
                          && info.available(F::ImageState));
        OIIO_CHECK_EQUAL(info.image_state(), "scene");
        OIIO_CHECK_ASSERT(info.computed(F::ColorInteropID)
                          && info.available(F::ColorInteropID));
        OIIO_CHECK_EQUAL(info.color_interop_id(), "srgb_rec709_scene");
        OIIO_CHECK_ASSERT(info.computed(F::Encoding)
                          && info.available(F::Encoding));
        OIIO_CHECK_EQUAL(info.encoding(), "sdr-video");
        // Direct facts are direct, not behavioral derivations.
        OIIO_CHECK_FALSE(info.derived(F::ImageState));
        OIIO_CHECK_FALSE(info.derived(F::ColorInteropID));
        OIIO_CHECK_FALSE(info.derived(F::Encoding));
        // Range: attempted, but nothing registers one -- a stable negative,
        // never a guessed "full".
        OIIO_CHECK_ASSERT(info.computed(F::Range));
        OIIO_CHECK_FALSE(info.available(F::Range));
        OIIO_CHECK_EQUAL(info.range(), "");
        // Fields that would require behavioral derivation: not attempted
        // (this getter never silently derives).
        OIIO_CHECK_FALSE(info.computed(F::EqualityID));
        OIIO_CHECK_FALSE(info.computed(F::Chromaticities));
        OIIO_CHECK_FALSE(info.computed(F::TransferFunction));
        OIIO_CHECK_ASSERT(info.chromaticities().empty());
        OIIO_CHECK_EQUAL(int(info.transfer_function_kind()),
                         int(ColorTransferFunctionKind::Undetermined));
    }

    // The reserved options fields are accepted and do not change the
    // answer.
    {
        ColorSpaceInfoOptions opts;
        opts.profile            = "show";
        opts.policies           = "oiio:colorpolicy:example=1";
        ColorSpaceInfo reserved = cc.get_color_space_info("my_srgb", opts);
        OIIO_CHECK_ASSERT(reserved.valid());
        OIIO_CHECK_ASSERT(!cc.has_error());
        OIIO_CHECK_EQUAL(reserved.name(), "srgb_rec709_scene");
        OIIO_CHECK_EQUAL(reserved.encoding(), "sdr-video");
    }

    // A display space reports state "display"; a data space's state is
    // honestly undetermined and its cheap id is unavailable (the cheap
    // subset has nothing declared or matched for it); a space with no
    // authored facts has an unavailable id and encoding.
    {
        ColorSpaceInfo screen = cc.get_color_space_info("screen");
        OIIO_CHECK_ASSERT(screen.valid());
        OIIO_CHECK_EQUAL(screen.image_state(), "display");

        ColorSpaceInfo data = cc.get_color_space_info("rawdata");
        OIIO_CHECK_ASSERT(data.valid());
        OIIO_CHECK_ASSERT(data.computed(F::ImageState));
        OIIO_CHECK_FALSE(data.available(F::ImageState));
        OIIO_CHECK_ASSERT(data.computed(F::ColorInteropID));
        OIIO_CHECK_FALSE(data.available(F::ColorInteropID));
        OIIO_CHECK_EQUAL(data.color_interop_id(), "");

        ColorSpaceInfo plain = cc.get_color_space_info("plain_space");
        OIIO_CHECK_ASSERT(plain.valid());
        OIIO_CHECK_ASSERT(plain.computed(F::ColorInteropID));
        OIIO_CHECK_FALSE(plain.available(F::ColorInteropID));
        OIIO_CHECK_ASSERT(plain.computed(F::Encoding));
        OIIO_CHECK_FALSE(plain.available(F::Encoding));
    }

    // Error convention, scalar: unknown name -> invalid record + the
    // ColorConfig error state.
    {
        ColorSpaceInfo bad = cc.get_color_space_info("no_such_space_xyzzy");
        OIIO_CHECK_FALSE(bad.valid());
        OIIO_CHECK_ASSERT(cc.has_error());
        std::string err = cc.geterror();
        OIIO_CHECK_ASSERT(Strutil::contains(err, "unknown color space")
                          && Strutil::contains(err, "no_such_space_xyzzy"));
    }

    // Batch: input order and duplicates preserved, one record per input.
    {
        std::vector<std::string> names { "plain_space", "rawdata", "my_srgb",
                                         "plain_space" };
        std::vector<ColorSpaceInfo> infos = cc.get_color_space_infos(names);
        OIIO_CHECK_ASSERT(!cc.has_error());
        OIIO_CHECK_EQUAL(infos.size(), 4);
        if (infos.size() == 4) {
            OIIO_CHECK_EQUAL(infos[0].name(), "plain_space");
            OIIO_CHECK_EQUAL(infos[1].name(), "rawdata");
            OIIO_CHECK_EQUAL(infos[2].name(), "srgb_rec709_scene");
            OIIO_CHECK_EQUAL(infos[3].name(), "plain_space");
        }
        // An empty input span is an empty batch, not "all spaces".
        OIIO_CHECK_ASSERT(
            cc.get_color_space_infos(cspan<std::string>()).empty());
        OIIO_CHECK_ASSERT(!cc.has_error());
    }

    // Error convention, batch: any invalid input fails the whole batch with
    // one INDEXED error and an empty result.
    {
        std::vector<std::string> names { "ref", "bogus_name", "plain_space" };
        std::vector<ColorSpaceInfo> infos = cc.get_color_space_infos(names);
        OIIO_CHECK_ASSERT(infos.empty());
        OIIO_CHECK_ASSERT(cc.has_error());
        std::string err = cc.geterror();
        OIIO_CHECK_ASSERT(Strutil::contains(err, "get_color_space_infos[1]")
                          && Strutil::contains(err, "bogus_name"));
    }

    Filesystem::remove(config_path);
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
    test_color_space_info();

    return unit_test_failures != 0;
}
