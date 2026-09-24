// Copyright Contributors to the OpenImageIO project.
// SPDX-License-Identifier: Apache-2.0
// https://github.com/AcademySoftwareFoundation/OpenImageIO

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <jxl/decode.h>

#include <OpenImageIO/imageio.h>
#include <OpenImageIO/strutil.h>

namespace {

struct Expected {
    bool icc          = false;
    bool data_profile = false;
    int uses_original = -1;
    int color_space   = -1;
    int white_point   = -1;
    int primaries     = -1;
    int transfer      = -1;
    double gamma      = std::numeric_limits<double>::quiet_NaN();
    std::vector<double> xy;
    std::string decode_pfm;
    std::string data_icc;
};

int
fail(const std::string& filename, const std::string& message)
{
    OIIO::Strutil::print(stderr, "{}: {}\n", filename, message);
    return 1;
}

bool
close_enough(double actual, double expected, double tolerance)
{
    return std::fabs(actual - expected) <= tolerance;
}

// Write SOURCE to OUTPUT through one ImageOutput whose spec carries both the
// ICC profile in ICC_FILE and an explicit CICP. The writer sets the color
// encoding once, from the ICC profile, and must report no error.
int
write_icc_and_cicp(const std::string& output, const std::string& source,
                   const std::string& icc_file)
{
    using namespace OIIO;
    std::ifstream icc_input(icc_file, std::ios::binary);
    const std::vector<uint8_t> icc((std::istreambuf_iterator<char>(icc_input)),
                                   std::istreambuf_iterator<char>());
    auto in = ImageInput::open(source);
    if (!in)
        return fail(source, OIIO::geterror());
    ImageSpec spec(in->spec().width, in->spec().height, in->spec().nchannels,
                   TypeUInt16);
    std::vector<uint16_t> pixels(spec.image_pixels() * spec.nchannels);
    if (!in->read_image(0, 0, 0, spec.nchannels, TypeUInt16, pixels.data()))
        return fail(source, in->geterror());
    in.reset();
    spec.attribute("ICCProfile", TypeDesc(TypeDesc::UINT8, icc.size()),
                   icc.data());
    const int cicp[4] = { 9, 16, 9, 1 };
    spec.attribute("CICP", TypeDesc(TypeDesc::INT, 4), cicp);
    spec.attribute("compression", "jpegxl:100");

    auto out = ImageOutput::create(output);
    if (!out)
        return fail(output, OIIO::geterror());
    if (!out->open(output, spec))
        return fail(output, "open failed: " + out->geterror());
    if (!out->write_image(TypeUInt16, pixels.data()))
        return fail(output, "write_image failed: " + out->geterror());
    if (!out->close())
        return fail(output, "close failed: " + out->geterror());
    if (out->has_error())
        return fail(output, "unexpected error: " + out->geterror());
    out.reset();

    in = ImageInput::open(output);
    if (!in)
        return fail(output, OIIO::geterror());
    const ParamValue* read_icc = in->spec().find_attribute("ICCProfile");
    if (!read_icc || size_t(read_icc->datasize()) != icc.size()
        || !std::equal(icc.begin(), icc.end(),
                       static_cast<const uint8_t*>(read_icc->data())))
        return fail(output, "the ICC profile was not written");
    return 0;
}

}  // namespace

int
main(int argc, char* argv[])
{
    if (argc == 5 && std::string(argv[1]) == "--write-icc-and-cicp")
        return write_icc_and_cicp(argv[2], argv[3], argv[4]);
    if (argc < 2) {
        OIIO::Strutil::print(stderr,
                             "usage: jxl_profile_test FILE [EXPECTATIONS]\n"
                             "       jxl_profile_test --write-icc-and-cicp "
                             "OUTPUT SOURCE ICC\n");
        return 2;
    }

    const std::string filename = argv[1];
    Expected expected;
    try {
        for (int i = 2; i < argc; ++i) {
            const std::string option = argv[i];
            auto value               = [&]() -> const char* {
                if (++i >= argc)
                    throw std::runtime_error("missing value for " + option);
                return argv[i];
            };
            if (option == "--icc")
                expected.icc = true;
            else if (option == "--data-profile")
                expected.data_profile = true;
            else if (option == "--decode-pfm") {
                expected.decode_pfm   = value();
                expected.data_profile = true;
            } else if (option == "--data-icc") {
                expected.data_icc     = value();
                expected.data_profile = true;
            } else if (option == "--uses-original")
                expected.uses_original = std::stoi(value());
            else if (option == "--color-space")
                expected.color_space = std::stoi(value());
            else if (option == "--white-point")
                expected.white_point = std::stoi(value());
            else if (option == "--primaries")
                expected.primaries = std::stoi(value());
            else if (option == "--transfer")
                expected.transfer = std::stoi(value());
            else if (option == "--gamma")
                expected.gamma = std::stod(value());
            else if (option == "--xy") {
                for (int n = 0; n < 8; ++n)
                    expected.xy.push_back(std::stod(value()));
            } else {
                throw std::runtime_error("unknown option " + option);
            }
        }
    } catch (const std::exception& error) {
        return fail(filename, error.what());
    }

    std::ifstream input(filename, std::ios::binary);
    if (!input)
        return fail(filename, "could not open file");
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)),
                               std::istreambuf_iterator<char>());
    if (bytes.size() <= 128)
        return fail(filename, "test output is not larger than 128 bytes");
    const JxlSignature signature = JxlSignatureCheck(bytes.data(),
                                                     bytes.size());
    if (signature != JXL_SIG_CODESTREAM && signature != JXL_SIG_CONTAINER)
        return fail(filename, "invalid JPEG XL signature");

    std::unique_ptr<JxlDecoder, decltype(&JxlDecoderDestroy)> decoder(
        JxlDecoderCreate(nullptr), JxlDecoderDestroy);
    if (!decoder)
        return fail(filename, "could not create decoder");
    if (JxlDecoderSubscribeEvents(decoder.get(), JXL_DEC_BASIC_INFO
                                                     | JXL_DEC_COLOR_ENCODING
                                                     | JXL_DEC_FULL_IMAGE)
        != JXL_DEC_SUCCESS)
        return fail(filename, "could not subscribe to decoder events");
    if (JxlDecoderSetInput(decoder.get(), bytes.data(), bytes.size())
        != JXL_DEC_SUCCESS)
        return fail(filename, "could not set decoder input");
    JxlDecoderCloseInput(decoder.get());

    JxlBasicInfo basic_info {};
    bool have_basic_info = false;
    JxlColorEncoding data_profile {};
    bool have_data_profile = false;
    JxlPixelFormat pixel_format {};
    std::vector<float> pixels;
    for (;;) {
        const JxlDecoderStatus status = JxlDecoderProcessInput(decoder.get());
        if (status == JXL_DEC_BASIC_INFO) {
            if (JxlDecoderGetBasicInfo(decoder.get(), &basic_info)
                != JXL_DEC_SUCCESS)
                return fail(filename, "could not read basic info");
            have_basic_info = true;
            continue;
        }
        if (status == JXL_DEC_COLOR_ENCODING) {
            if (!have_basic_info)
                return fail(filename, "basic info event was not received");
            if (expected.uses_original >= 0
                && basic_info.uses_original_profile != expected.uses_original)
                return fail(filename, "unexpected uses_original_profile value");

            JxlColorEncoding profile {};
            const JxlDecoderStatus profile_status
                = JxlDecoderGetColorAsEncodedProfile(
                    decoder.get(), JXL_COLOR_PROFILE_TARGET_ORIGINAL, &profile);
            if (expected.icc) {
                if (profile_status != JXL_DEC_ERROR)
                    return fail(filename, "expected an original ICC profile");
                size_t icc_size = 0;
                if (JxlDecoderGetICCProfileSize(
                        decoder.get(), JXL_COLOR_PROFILE_TARGET_ORIGINAL,
                        &icc_size)
                        != JXL_DEC_SUCCESS
                    || icc_size < 128)
                    return fail(filename,
                                "original ICC profile is unavailable");
            } else {
                if (profile_status != JXL_DEC_SUCCESS)
                    return fail(filename,
                                "expected an original structured color profile");
                if (expected.color_space >= 0
                    && profile.color_space != expected.color_space)
                    return fail(filename, "unexpected color space");
                if (expected.white_point >= 0
                    && profile.white_point != expected.white_point)
                    return fail(filename, "unexpected white point");
                if (expected.primaries >= 0
                    && profile.primaries != expected.primaries)
                    return fail(filename, "unexpected primaries");
                if (expected.transfer >= 0
                    && profile.transfer_function != expected.transfer)
                    return fail(filename, "unexpected transfer function");
                if (std::isfinite(expected.gamma)
                    && !close_enough(profile.gamma, expected.gamma, 2.0e-7))
                    return fail(filename, "unexpected gamma");
                if (!expected.xy.empty()) {
                    const double actual[] = { profile.primaries_red_xy[0],
                                              profile.primaries_red_xy[1],
                                              profile.primaries_green_xy[0],
                                              profile.primaries_green_xy[1],
                                              profile.primaries_blue_xy[0],
                                              profile.primaries_blue_xy[1],
                                              profile.white_point_xy[0],
                                              profile.white_point_xy[1] };
                    for (int n = 0; n < 8; ++n)
                        if (!close_enough(actual[n], expected.xy[n], 2.0e-6))
                            return fail(filename,
                                        "unexpected chromaticity coordinate");
                }
            }
            if (expected.data_profile) {
                if (JxlDecoderGetColorAsEncodedProfile(
                        decoder.get(), JXL_COLOR_PROFILE_TARGET_DATA,
                        &data_profile)
                    != JXL_DEC_SUCCESS)
                    return fail(filename,
                                "structured DATA profile is unavailable");
                have_data_profile = true;
                if (!expected.data_icc.empty()) {
                    size_t icc_size = 0;
                    if (JxlDecoderGetICCProfileSize(
                            decoder.get(), JXL_COLOR_PROFILE_TARGET_DATA,
                            &icc_size)
                            != JXL_DEC_SUCCESS
                        || icc_size == 0)
                        return fail(filename,
                                    "DATA ICC profile is unavailable");
                    std::vector<uint8_t> icc(icc_size);
                    if (JxlDecoderGetColorAsICCProfile(
                            decoder.get(), JXL_COLOR_PROFILE_TARGET_DATA,
                            icc.data(), icc.size())
                        != JXL_DEC_SUCCESS)
                        return fail(filename,
                                    "could not read DATA ICC profile");
                    std::ofstream output(expected.data_icc, std::ios::binary);
                    output.write(reinterpret_cast<const char*>(icc.data()),
                                 icc.size());
                    if (!output)
                        return fail(filename,
                                    "could not write DATA ICC profile");
                }
            }
            if (expected.decode_pfm.empty())
                return 0;
            continue;
        }
        if (status == JXL_DEC_NEED_IMAGE_OUT_BUFFER) {
            pixel_format = { basic_info.num_color_channels, JXL_TYPE_FLOAT,
                             JXL_NATIVE_ENDIAN, 0 };
            size_t buffer_size = 0;
            if (JxlDecoderImageOutBufferSize(decoder.get(), &pixel_format,
                                             &buffer_size)
                    != JXL_DEC_SUCCESS
                || buffer_size % sizeof(float) != 0)
                return fail(filename, "could not size float DATA pixels");
            pixels.resize(buffer_size / sizeof(float));
            if (JxlDecoderSetImageOutBuffer(decoder.get(), &pixel_format,
                                            pixels.data(), buffer_size)
                != JXL_DEC_SUCCESS)
                return fail(filename, "could not set float DATA buffer");
            continue;
        }
        if (status == JXL_DEC_FULL_IMAGE)
            continue;
        if (status == JXL_DEC_SUCCESS) {
            if (expected.decode_pfm.empty() || !have_data_profile
                || pixels.empty())
                return fail(filename, "DATA decode did not complete");
            if (pixel_format.num_channels != 1
                && pixel_format.num_channels != 3)
                return fail(filename, "PFM requires one or three channels");
            std::ofstream output(expected.decode_pfm, std::ios::binary);
            if (!output)
                return fail(filename, "could not create DATA PFM");
            OIIO::Strutil::print(output, "{}{} {}\n",
                                 pixel_format.num_channels == 1 ? "Pf\n"
                                                                : "PF\n",
                                 basic_info.xsize, basic_info.ysize);
            const uint16_t endian_test = 1;
            OIIO::Strutil::print(output, "{}\n",
                                 *reinterpret_cast<const uint8_t*>(&endian_test)
                                     ? -1.0
                                     : 1.0);
            const size_t row_values = size_t(basic_info.xsize)
                                      * pixel_format.num_channels;
            for (size_t y = basic_info.ysize; y > 0; --y)
                output.write(reinterpret_cast<const char*>(
                                 pixels.data() + (y - 1) * row_values),
                             row_values * sizeof(float));
            if (!output)
                return fail(filename, "could not write DATA PFM");
            OIIO::Strutil::print("DATA {} {} {} {} {}\n",
                                 int(data_profile.color_space),
                                 int(data_profile.white_point),
                                 int(data_profile.primaries),
                                 int(data_profile.transfer_function),
                                 data_profile.gamma);
            return 0;
        }
        if (status == JXL_DEC_ERROR || status == JXL_DEC_NEED_MORE_INPUT)
            return fail(filename, "DATA decode failed");
    }
}
