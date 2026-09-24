// Copyright Contributors to the OpenImageIO project.
// SPDX-License-Identifier: Apache-2.0
// https://github.com/AcademySoftwareFoundation/OpenImageIO

// JPEG XL

// https://jpeg.org/jpegxl/index.html
// https://jpegxl.info
// https://jpegxl.info/test-page
// https://people.csail.mit.edu/ericchan/hdr/hdr-jxl.php
// https://saklistudio.com/jxltests
// https://thorium.rocks
// https://bugs.chromium.org/p/chromium/issues/detail?id=1451807

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>

#include <OpenImageIO/filesystem.h>
#include <OpenImageIO/fmath.h>
#include <OpenImageIO/imageio.h>
#include <OpenImageIO/tiffutils.h>

#include <jxl/color_encoding.h>
#include <jxl/decode.h>
#include <jxl/decode_cxx.h>
#include <jxl/resizable_parallel_runner_cxx.h>

#include "imageio_pvt.h"

OIIO_PLUGIN_NAMESPACE_BEGIN

#define DBG if (0)

class JxlInput final : public ImageInput {
public:
    JxlInput() { init(); }
    ~JxlInput() override { close(); }
    const char* format_name(void) const override { return "jpegxl"; }
    int supports(string_view feature) const override
    {
        return (feature == "exif" || feature == "ioproxy");
    }
    bool valid_file(Filesystem::IOProxy* ioproxy) const override;

    bool open(const std::string& name, ImageSpec& spec) override;
    bool open(const std::string& name, ImageSpec& spec,
              const ImageSpec& config) override;
    bool read_native_scanline(int subimage, int miplevel, int y, int z,
                              void* data) override;
    bool close() override;

    const std::string& filename() const { return m_filename; }

private:
    std::string m_filename;
    int m_next_scanline;  // Which scanline is the next to read?
    uint32_t m_channels;
    JxlDecoderPtr m_decoder;
    JxlResizableParallelRunnerPtr m_runner;
    std::unique_ptr<ImageSpec> m_config;  // Saved copy of configuration spec
    std::vector<uint8_t> m_icc_profile;
    std::unique_ptr<uint8_t[]> m_buffer;

    void init()
    {
        ioproxy_clear();
        m_config.reset();
        m_decoder = nullptr;
        m_runner  = nullptr;
        m_buffer  = nullptr;
    }

    void close_file() { init(); }
};



// Export version number and create function symbols
OIIO_PLUGIN_EXPORTS_BEGIN

OIIO_EXPORT int jpegxl_imageio_version = OIIO_PLUGIN_VERSION;



OIIO_EXPORT const char*
jpegxl_imageio_library_version()
{
    return "libjxl " OIIO_STRINGIZE(JPEGXL_MAJOR_VERSION) "." OIIO_STRINGIZE(
        JPEGXL_MINOR_VERSION) "." OIIO_STRINGIZE(JPEGXL_PATCH_VERSION);
}



OIIO_EXPORT ImageInput*
jpegxl_input_imageio_create()
{
    return new JxlInput;
}

OIIO_EXPORT const char* jpegxl_input_extensions[] = { "jxl", nullptr };

OIIO_PLUGIN_EXPORTS_END



// The CICP primaries code of a JPEG XL color encoding, or 0 when it has none.
// Every code names a white point as well as primaries, so an enumerated gamut
// paired with another white point -- which JPEG XL can express and CICP cannot
// -- has no code.
static int
cicp_primaries(const JxlColorEncoding& encoding)
{
    switch (encoding.primaries) {
    case JXL_PRIMARIES_SRGB:  // 1
    case JXL_PRIMARIES_2100:  // 9
        return encoding.white_point == JXL_WHITE_POINT_D65
                   ? int(encoding.primaries)
                   : 0;
    case JXL_PRIMARIES_P3:
        // The JxlPrimaries enum covers P3 as 11 only; CICP separates the two
        // white points, 11 for DCI and 12 for D65.
        if (encoding.white_point == JXL_WHITE_POINT_DCI)
            return 11;
        if (encoding.white_point == JXL_WHITE_POINT_D65)
            return 12;
        return 0;
    default: return 0;
    }
}



// Record the display-referred identity a JPEG XL color encoding describes.
// If none matches, retain the exact numeric evidence that OIIO can express.
static void
record_display_encoding(const JxlColorEncoding& encoding, ImageSpec& spec)
{
    float xy[8];
    bool have_xy = true;
    // Published RGB xy of the enumerated primaries.
    static const float rec709[6] = { 0.64f, 0.33f, 0.30f, 0.60f, 0.15f, 0.06f };
    static const float rec2020[6] = { 0.708f, 0.292f, 0.170f,
                                      0.797f, 0.131f, 0.046f };
    static const float p3[6]
        = { 0.680f, 0.320f, 0.265f, 0.690f, 0.150f, 0.060f };
    switch (encoding.primaries) {
    case JXL_PRIMARIES_SRGB: std::copy_n(rec709, 6, xy); break;
    case JXL_PRIMARIES_2100: std::copy_n(rec2020, 6, xy); break;
    case JXL_PRIMARIES_P3: std::copy_n(p3, 6, xy); break;
    case JXL_PRIMARIES_CUSTOM:
        for (int i = 0; i < 2; ++i) {
            xy[i]     = float(encoding.primaries_red_xy[i]);
            xy[2 + i] = float(encoding.primaries_green_xy[i]);
            xy[4 + i] = float(encoding.primaries_blue_xy[i]);
        }
        break;
    default: have_xy = false; break;
    }
    switch (encoding.white_point) {
    case JXL_WHITE_POINT_D65:
        xy[6] = 0.3127f;
        xy[7] = 0.3290f;
        break;
    case JXL_WHITE_POINT_DCI:
        xy[6] = 0.314f;
        xy[7] = 0.351f;
        break;
    case JXL_WHITE_POINT_E: xy[6] = xy[7] = 1.0f / 3.0f; break;
    case JXL_WHITE_POINT_CUSTOM:
        xy[6] = float(encoding.white_point_xy[0]);
        xy[7] = float(encoding.white_point_xy[1]);
        break;
    default: have_xy = false; break;
    }
    std::string curve;
    float gamma = 0.0f;
    switch (encoding.transfer_function) {
    case JXL_TRANSFER_FUNCTION_LINEAR:
        curve = "lin";
        gamma = 1.0f;
        break;
    case JXL_TRANSFER_FUNCTION_SRGB: curve = "srgb"; break;
    // Like CICP transfer 1, which the Color Interop Forum reads as BT.1886.
    case JXL_TRANSFER_FUNCTION_709: curve = "g24"; break;
    case JXL_TRANSFER_FUNCTION_PQ: curve = "pq"; break;
    // libjxl's DCI transfer is a pure gamma 2.6, with no white scaling.
    case JXL_TRANSFER_FUNCTION_DCI:
        curve = "g26";
        gamma = 2.6f;
        break;
    case JXL_TRANSFER_FUNCTION_GAMMA: {
        // libjxl stores the encoding exponent; name its decoding exponent.
        // A gamma of 1 never arrives here: libjxl stores it as LINEAR.
        const double g10 = 10.0 / encoding.gamma;
        gamma            = float(1.0 / encoding.gamma);
        if (std::isfinite(g10) && std::abs(g10 - std::round(g10)) <= 0.01)
            curve = Strutil::fmt::format("g{}", int(std::round(g10)));
        break;
    }
    default: break;
    }
    if (have_xy && !curve.empty()) {
        string_view interop_id = pvt::get_display_interop_id(xy, curve);
        if (!interop_id.empty()) {
            spec.attribute("oiio:ColorSpace", interop_id);
            return;
        }
    }
    if (have_xy)
        spec.attribute("chromaticities", TypeDesc(TypeDesc::FLOAT, 8), xy);
    if (std::isfinite(gamma) && gamma > 0.0f)
        spec.attribute("oiio:Gamma", gamma);
}



bool
JxlInput::valid_file(Filesystem::IOProxy* ioproxy) const
{
    DBG std::cout << "JxlInput::valid_file()\n";

    // Check magic number to assure this is a JPEG file
    if (!ioproxy || ioproxy->mode() != Filesystem::IOProxy::Read)
        return false;

    uint8_t magic[128] {};
    const size_t numRead = ioproxy->pread(magic, sizeof(magic), 0);
    if (numRead != sizeof(magic))
        return false;

    JxlSignature signature = JxlSignatureCheck(magic, sizeof(magic));
    switch (signature) {
    case JXL_SIG_CODESTREAM:
    case JXL_SIG_CONTAINER: break;
    default: return false;
    }

    DBG std::cout << "JxlInput::valid_file() return true\n";
    return true;
}



bool
JxlInput::open(const std::string& name, ImageSpec& newspec,
               const ImageSpec& config)
{
    DBG std::cout << "JxlInput::open(name, newspec, config)\n";

    ioproxy_retrieve_from_config(config);
    m_config.reset(new ImageSpec(config));  // save config spec
    return open(name, newspec);
}



bool
JxlInput::open(const std::string& name, ImageSpec& newspec)
{
    DBG std::cout << "JxlInput::open(name, newspec)\n";

    m_filename = name;

    DBG std::cout << "m_filename = " << m_filename << "\n";

    if (!ioproxy_use_or_open(name)) {
        DBG std::cout << "ioproxy_use_or_open returned false\n";
        return false;
    }

    Filesystem::IOProxy* m_io = ioproxy();
    std::string proxytype     = m_io->proxytype();
    if (proxytype != "file" && proxytype != "memreader") {
        errorfmt("JPEG XL reader can't handle proxy type {}", proxytype);
        close();
        return false;
    }

    if (!valid_file(m_io)) {
        DBG std::cout << "JxlInput::valid_file() return false\n";
        errorfmt("Possible corrupt file, "
                 "JPEG XL signature verification failed\n");
        close();
        return false;
    }

    m_decoder = JxlDecoderMake(nullptr);
    if (m_decoder == nullptr) {
        DBG std::cout << "JxlDecoderMake failed\n";
        close();
        return false;
    }

    m_runner = JxlResizableParallelRunnerMake(nullptr);
    if (m_runner == nullptr) {
        DBG std::cout << "JxlThreadParallelRunnerMake failed\n";
        close();
        return false;
    }

    JxlDecoderStatus status = JxlDecoderSetParallelRunner(
        m_decoder.get(), JxlResizableParallelRunner, m_runner.get());
    if (status != JXL_DEC_SUCCESS) {
        DBG std::cout << "JxlDecoderSetParallelRunner failed\n";
        close();
        return false;
    }

    status
        = JxlDecoderSubscribeEvents(m_decoder.get(),
                                    JXL_DEC_BASIC_INFO | JXL_DEC_COLOR_ENCODING
                                        | JXL_DEC_FRAME | JXL_DEC_FULL_IMAGE);
    if (status != JXL_DEC_SUCCESS) {
        DBG std::cout << "JxlDecoderSubscribeEvents failed\n";
        close();
        return false;
    }

    std::unique_ptr<uint8_t[]> jxl;

    DBG std::cout << "proxytype = " << proxytype << "\n";
    if (proxytype == "file") {
        size_t size = m_io->size();
        DBG std::cout << "size = " << size << "\n";
        jxl.reset(new uint8_t[size]);
        size_t result = m_io->read(jxl.get(), size);
        DBG std::cout << "result = " << result << "\n";
        if (result != size) {
            errorfmt("Failed to read {} bytes from \"{}\"", size, m_filename);
            return false;
        }

        status = JxlDecoderSetInput(m_decoder.get(), jxl.get(), size);
        if (status != JXL_DEC_SUCCESS) {
            DBG std::cout << "JxlDecoderSetInput() returned " << status << "\n";
            close();
            return false;
        }
        JxlDecoderCloseInput(m_decoder.get());

    } else {
        auto buffer = reinterpret_cast<Filesystem::IOMemReader*>(m_io)->buffer();
        status = JxlDecoderSetInput(m_decoder.get(),
                                    const_cast<unsigned char*>(buffer.data()),
                                    buffer.size());
        if (status != JXL_DEC_SUCCESS) {
            close();
            return false;
        }
    }

    JxlBasicInfo info;
    JxlPixelFormat format;
    JxlDataType jxl_data_type;
    TypeDesc m_data_type;
    uint32_t bits = 0;
    JxlColorEncoding color_encoding {};
    bool have_color_encoding = false;
    bool got_basic_info      = false;

    for (;;) {
        JxlDecoderStatus status = JxlDecoderProcessInput(m_decoder.get());
        DBG std::cout << "JxlDecoderProcessInput() returned " << status << "\n";

        if (status == JXL_DEC_ERROR) {
            DBG std::cout << "JXL_DEC_ERROR\n";

            errorfmt("JPEG XL decoder error");
            close();
            return false;
        } else if (status == JXL_DEC_NEED_MORE_INPUT) {
            DBG std::cout << "JXL_DEC_NEED_MORE_INPUT\n";

            errorfmt("JPEG XL decoder error, already provided all input\n");
            close();
            return false;
        } else if (status == JXL_DEC_BASIC_INFO) {
            DBG std::cout << "JXL_DEC_BASIC_INFO\n";

            // Get the basic information about the image
            if (JXL_DEC_SUCCESS
                != JxlDecoderGetBasicInfo(m_decoder.get(), &info)) {
                errorfmt("JxlDecoderGetBasicInfo failed\n");
                close();
                return false;
            }

            // Need to check how we can support bfloat16 if jpegxl supports it
            bool is_float = info.exponent_bits_per_sample > 0;

            if (info.bits_per_sample <= 8) {
                jxl_data_type = JXL_TYPE_UINT8;
                m_data_type   = TypeDesc::UINT8;
                bits          = 8;
            } else if (info.bits_per_sample <= 16) {
                jxl_data_type = is_float ? JXL_TYPE_FLOAT16 : JXL_TYPE_UINT16;
                m_data_type   = is_float ? TypeDesc::HALF : TypeDesc::UINT16;
                bits          = 16;
            } else if (info.bits_per_sample <= 32) {
                jxl_data_type = JXL_TYPE_FLOAT;
                m_data_type   = TypeDesc::FLOAT;
                bits          = 32;
            } else {
                errorfmt("Unsupported bits per sample\n");
                close();
                return false;
            }

            format = { m_channels, jxl_data_type, JXL_NATIVE_ENDIAN, 0 };

            format.num_channels = info.num_color_channels
                                  + info.num_extra_channels;
            m_channels = info.num_color_channels + info.num_extra_channels;

            // Validate dimensions/channel count before the decoder proceeds
            // further and may attempt huge allocations on corrupt inputs.
            m_spec = ImageSpec(info.xsize, info.ysize, m_channels, m_data_type);
            if (!check_open(m_spec, { 0, (1 << 30) - 1, 0, (1 << 30) - 1, 0, 1,
                                      0, 4099 })) {
                close();
                return false;
            }
            got_basic_info = true;

            JxlResizableParallelRunnerSetThreads(
                m_runner.get(),
                JxlResizableParallelRunnerSuggestThreads(info.xsize,
                                                         info.ysize));
        } else if (status == JXL_DEC_COLOR_ENCODING) {
            DBG std::cout << "JXL_DEC_COLOR_ENCODING\n";

            // Get the ICC color profile of the pixel data
            size_t icc_size;

            if (JXL_DEC_SUCCESS
                != JxlDecoderGetICCProfileSize(m_decoder.get(),
                                               JXL_COLOR_PROFILE_TARGET_DATA,
                                               &icc_size)) {
                errorfmt("JxlDecoderGetICCProfileSize failed\n");
                close();
                return false;
            }
            m_icc_profile.resize(icc_size);
            if (JXL_DEC_SUCCESS
                != JxlDecoderGetColorAsICCProfile(m_decoder.get(),
                                                  JXL_COLOR_PROFILE_TARGET_DATA,
                                                  m_icc_profile.data(),
                                                  m_icc_profile.size())) {
                errorfmt("JxlDecoderGetColorAsICCProfile failed\n");
                close();
                return false;
            }

            // Get the color encoding of the pixel data
            // This will return JXL_DEC_ERR for a valid file without color
            // encoding information, so don't report an error.
            if (JXL_DEC_SUCCESS
                == JxlDecoderGetColorAsEncodedProfile(
                    m_decoder.get(), JXL_COLOR_PROFILE_TARGET_DATA,
                    &color_encoding)) {
                have_color_encoding = true;
            }
        } else if (status == JXL_DEC_NEED_IMAGE_OUT_BUFFER) {
            DBG std::cout << "JXL_DEC_NEED_IMAGE_OUT_BUFFER\n";

            size_t buffer_size;
            if (JXL_DEC_SUCCESS
                != JxlDecoderImageOutBufferSize(m_decoder.get(), &format,
                                                &buffer_size)) {
                errorfmt("JxlDecoderImageOutBufferSize failed\n");
                close();
                return false;
            }
            size_t expected_size = size_t(info.xsize) * info.ysize * m_channels
                                   * bits / 8;
            if (buffer_size != expected_size) {
                errorfmt("Invalid out buffer size {} {}\n", buffer_size,
                         expected_size);
                close();
                return false;
            }

            m_buffer.reset(new uint8_t[buffer_size]);

            if (JXL_DEC_SUCCESS
                != JxlDecoderSetImageOutBuffer(m_decoder.get(), &format,
                                               m_buffer.get(), buffer_size)) {
                errorfmt("JxlDecoderSetImageOutBuffer failed\n");
                close();
                return false;
            }
        } else if (status == JXL_DEC_FULL_IMAGE) {
            DBG std::cout << "JXL_DEC_FULL_IMAGE\n";

            // Nothing to do. Do not yet return. If the image is an animation, more
            // full frames may be decoded. This example only keeps the last one.
        } else if (status == JXL_DEC_FRAME) {
            DBG std::cout << "JXL_DEC_FRAME\n";

        } else if (status == JXL_DEC_SUCCESS) {
            DBG std::cout << "JXL_DEC_SUCCESS\n";

            // All decoding successfully finished.
            // It's not required to call JxlDecoderReleaseInput(m_decoder.get()) here since
            // the decoder will be destroyed.
            break;
        } else {
            errorfmt("Unknown decoder status\n");
            close();
            return false;
        }
    }

    if (!got_basic_info) {
        errorfmt("Possible corrupt file, no JPEG XL basic info found\n");
        close();
        return false;
    }

    // A valid image always triggers JXL_DEC_NEED_IMAGE_OUT_BUFFER and allocates
    // m_buffer during the decode loop above. If it didn't, the file provided
    // basic info but no decodable pixels; fail rather than let a later
    // read_native_scanline() dereference a null buffer.
    if (!m_buffer) {
        errorfmt("Possible corrupt file, no JPEG XL image data decoded\n");
        return false;
    }

    // Read ICC profile
    if (m_icc_profile.size() && m_icc_profile.data()) {
        m_spec.attribute("ICCProfile",
                         TypeDesc(TypeDesc::UINT8, m_icc_profile.size()),
                         m_icc_profile.data());
        std::string errormsg;

        bool ok = decode_icc_profile(cspan<uint8_t>(m_icc_profile.data(),
                                                    m_icc_profile.size()),
                                     m_spec, errormsg);

        if (!ok && OIIO::get_int_attribute("imageinput:strict")) {
            errorfmt("Possible corrupt file, could not decode ICC profile: {}\n",
                     errormsg);
            close();
            return false;
        }
    }

    if (have_color_encoding) {
        // Read CICP from color encoding. Arbitrary gamma has no CICP code,
        // and neither has libjxl's DCI transfer, a pure gamma 2.6 without
        // CICP 17's white scaling.
        const int color_primaries = cicp_primaries(color_encoding);
        bool named_encoding       = false;
        if (color_primaries
            && color_encoding.transfer_function != JXL_TRANSFER_FUNCTION_GAMMA
            && color_encoding.transfer_function != JXL_TRANSFER_FUNCTION_DCI) {
            const int cicp[4] = { color_primaries,
                                  color_encoding.transfer_function, 0 /* RGB */,
                                  1 /* Full range */ };
            m_spec.attribute("CICP", TypeDesc(TypeDesc::INT, 4), cicp);
            if (color_encoding.transfer_function == JXL_TRANSFER_FUNCTION_HLG) {
                string_view interop_id = pvt::get_color_interop_id(cicp);
                if (!interop_id.empty()) {
                    m_spec.attribute("oiio:ColorSpace", interop_id);
                    named_encoding = true;
                }
            }
        }
        // libjxl's color management treats every encoding but HLG as display
        // referred, so read one with known chromaticities and transfer
        // function as the matching display-referred identity, if any. Otherwise
        // retain only the exact evidence the encoding states.
        if (!named_encoding)
            record_display_encoding(color_encoding, m_spec);
    }

    newspec = m_spec;
    return true;
}



bool
JxlInput::read_native_scanline(int subimage, int miplevel, int y, int /*z*/,
                               void* data)
{
    DBG std::cout << "JxlInput::read_native_scanline(, , " << y << ")\n";
    size_t scanline_size = m_spec.width * m_channels * m_spec.channel_bytes();
    // size_t scanline_size = m_spec.width * m_channels * sizeof(uint8_t);

    lock_guard lock(*this);
    if (!seek_subimage(subimage, miplevel))
        return false;
    if (y < 0 || y >= m_spec.height)  // out of range scanline
        return false;

    memcpy(data, (void*)(m_buffer.get() + y * scanline_size), scanline_size);

    return true;
}



bool
JxlInput::close()
{
    DBG std::cout << "JxlInput::close()\n";

    if (ioproxy_opened()) {
        close_file();
    }

    m_buffer.reset();
    init();  // Reset to initial state
    return true;
}

OIIO_PLUGIN_NAMESPACE_END
