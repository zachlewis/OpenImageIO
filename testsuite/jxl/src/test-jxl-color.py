#!/usr/bin/env python

# Copyright Contributors to the OpenImageIO project.
# SPDX-License-Identifier: Apache-2.0
# https://github.com/AcademySoftwareFoundation/OpenImageIO

import os
from pathlib import Path
import subprocess
import sys


oiiotool = sys.argv[1]
idiff = sys.argv[2]
profile_test = sys.argv[3]
color_config = Path(sys.argv[4]).resolve()
icc_profile = Path(sys.argv[5]).resolve()
source_dir = Path(sys.argv[6]).resolve()
version_environment = dict(os.environ)
version_environment.pop("OCIO", None)
version_result = subprocess.run(
    [oiiotool, "--echo", "{getattribute(opencolorio_version)}"],
    env=version_environment,
    stdout=subprocess.PIPE,
    stderr=subprocess.STDOUT,
    text=True,
)
assert version_result.returncode == 0, version_result.stdout
version_text = version_result.stdout.strip().split(".")
version = tuple(int(part) for part in version_text[:2])
if version < (2, 5):
    compatible_config = Path("jxl-test-config.ocio")
    compatible_config.write_text(
        "".join(
            line
            for line in color_config.read_text().splitlines(keepends=True)
            if not line.lstrip().startswith("interop_id:")
        )
    )
    color_config = compatible_config.resolve()
environment = dict(os.environ, OCIO=str(color_config))
pq_transform = "DISPLAY - CIE-XYZ-D65_to_REC.2100-PQ"
hlg_transform = "DISPLAY - CIE-XYZ-D65_to_REC.2100-HLG-1000nit"
config_text = color_config.read_text()
assert config_text.count(pq_transform) == 1
config_text = config_text.replace(
    "    name: pq_rec2020_display\n    interop_id: pq_rec2020_display\n",
    "    name: pq_rec2020_display\n",
)
misleading_config = Path("misleading-hdr.ocio")
misleading_config.write_text(config_text.replace(pq_transform, hlg_transform))
misleading_environment = dict(
    environment, OCIO=str(misleading_config.resolve())
)


def run(argv, run_environment=None):
    if run_environment is None:
        run_environment = environment
    result = subprocess.run(
        [str(arg) for arg in argv],
        env=run_environment,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    assert result.returncode == 0, result.stdout
    return result.stdout


def run_failure(argv, expected, run_environment=None):
    if run_environment is None:
        run_environment = environment
    result = subprocess.run(
        [str(arg) for arg in argv],
        env=run_environment,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    assert result.returncode != 0, result.stdout
    assert expected in result.stdout, result.stdout
    return result.stdout


def make_source(filename, channels, colors):
    run(
        [
            oiiotool,
            "--pattern",
            "checker:" + colors + ":width=8:height=8",
            "64x64",
            str(channels),
            "-d",
            "uint16",
            "-o",
            filename,
        ]
    )


def write(
    filename,
    colorspace,
    source="rgb-source.tif",
    extra=(),
    lossy=False,
    run_environment=None,
):
    argv = [oiiotool, source, "--attrib", "oiio:ColorSpace", colorspace]
    argv += list(extra)
    if lossy:
        argv += ["--attrib", "jpegxl:distance", "1.0"]
    else:
        argv += ["--compression", "jpegxl:100"]
    argv += ["-o", filename]
    run(argv, run_environment)
    return filename


def check(filename, *expectations):
    return run([profile_test, filename, *expectations])


def structured(filename, color_space, white_point, primaries, transfer,
               gamma=None, xy=None, uses_original=1, data_profile=False,
               decode_pfm=None, data_icc=None):
    expectations = [
        "--uses-original",
        uses_original,
        "--color-space",
        color_space,
        "--white-point",
        white_point,
        "--primaries",
        primaries,
        "--transfer",
        transfer,
    ]
    if gamma is not None:
        expectations += ["--gamma", gamma]
    if xy is not None:
        expectations += ["--xy", *xy]
    if data_profile:
        expectations += ["--data-profile"]
    if decode_pfm is not None:
        expectations += ["--decode-pfm", decode_pfm]
    if data_icc is not None:
        expectations += ["--data-icc", data_icc]
    return check(filename, *expectations)


def srgb(filename):
    structured(filename, 0, 1, 1, 13)


REC709_XY = (0.64, 0.33, 0.30, 0.60, 0.15, 0.06, 0.3127, 0.3290)
ADOBE_XY = (0.64, 0.33, 0.21, 0.71, 0.15, 0.06, 0.3127, 0.3290)
AP1_XY = (0.713, 0.293, 0.165, 0.830, 0.128, 0.044, 0.32168, 0.33767)

make_source(
    "rgb-source.tif", 3, "color1=.1,.2,.3:color2=.7,.5,.25"
)
make_source("gray-source.tif", 1, "color1=.1:color2=.8")
make_source(
    "rgba-source.tif", 4, "color1=.1,.2,.3,1:color2=.7,.5,.25,1"
)

def colorspace(filename, run_environment=None):
    for line in run([oiiotool, "--info", "-v", filename],
                    run_environment).splitlines():
        if "oiio:ColorSpace:" in line:
            return line.split('"')[1]
    return ""


def numeric_attribute(info, name):
    prefix = name + ":"
    for line in info.splitlines():
        field = line.strip()
        if field.startswith(prefix):
            return tuple(float(value) for value in field[len(prefix):].split(","))
    return ()


def assert_numeric_attribute(info, name, expected):
    actual = numeric_attribute(info, name)
    assert len(actual) == len(expected), (name, actual, expected, info)
    assert all(abs(a - e) <= 1.0e-6 for a, e in zip(actual, expected)), \
        (name, actual, expected)


lossless = []
# A pure power with no CICP code is written as a custom gamma.
for name, filename, xy, gamma in (
    ("g22_adobergb_display", "adobe.jxl", ADOBE_XY, 2.19921875),
    ("g22_rec709_display", "g22.jxl", REC709_XY, 2.2),
    ("g24_rec709_scene", "g24-scene.jxl", REC709_XY, 2.4),
):
    lossless.append(("rgb-source.tif", write(filename, name)))
    primaries = 2 if name == "g22_adobergb_display" else 1
    # Assert standard primaries by enum: libjxl canonicalizes their numeric xy.
    structured(filename, 0, 1, primaries, 65535, 1.0 / gamma,
               xy if primaries == 2 else None)

# The built-in interop-identities config, which describes an encoding for a
# color space the active config does not define, honors the environment's
# OpenColorIO switches. This config does not define g22_adobergb_display.
tiny_config = Path("tiny.ocio")
tiny_config.write_text(
    "ocio_profile_version: 2\n"
    "roles:\n  scene_linear: lnf\n  default: lnf\n"
    "displays:\n  sRGB:\n    - !<View> {name: Raw, colorspace: lnf}\n"
    "colorspaces:\n  - !<ColorSpace>\n    name: lnf\n    bitdepth: 32f\n"
)
tiny_environment = dict(environment, OCIO=str(tiny_config.resolve()))
write("adobe-tiny.jxl", "g22_adobergb_display",
      run_environment=tiny_environment)
structured("adobe-tiny.jxl", 0, 1, 2, 65535, 1.0 / 2.19921875, ADOBE_XY)
for variable in ("OIIO_DISABLE_OCIO", "OIIO_DISABLE_BUILTIN_OCIO_CONFIGS"):
    disabled = dict(tiny_environment, **{variable: "1"})
    # The writer describes no encoding, so the file is libjxl's sRGB default.
    write("adobe-disabled.jxl", "g22_adobergb_display",
          run_environment=disabled)
    structured("adobe-disabled.jxl", 0, 1, 1, 13)
    # The reader names no identity for the enabled file's custom encoding.
    assert not colorspace("adobe-tiny.jxl", run_environment=disabled), variable

# A color space with a CICP code is written as that code, not as a custom
# encoding.
lossless.append(("rgb-source.tif", write("g24.jxl", "g24_rec709_display")))
structured("g24.jxl", 0, 1, 1, 1)
rec1886 = dict(os.environ, OCIO="ocio://studio-config-v2.1.0_aces-v1.3_ocio-v2.3")
write("rec1886.jxl", "Rec.1886 Rec.709 - Display", run_environment=rec1886)
structured("rec1886.jxl", 0, 1, 1, 1)

lossless.append(
    ("rgb-source.tif", write("srgb.jxl", "srgb_rec709_display"))
)
srgb("srgb.jxl")
lossless.append(
    ("rgb-source.tif", write("linear.jxl", "lin_rec709_scene"))
)
structured("linear.jxl", 0, 1, 1, 8)
lossless.append(
    ("rgb-source.tif", write("pq.jxl", "pq_rec2020_display"))
)
structured("pq.jxl", 0, 1, 9, 16)
lossless.append(
    ("rgb-source.tif", write("hlg.jxl", "hlg_rec2020_display"))
)
structured("hlg.jxl", 0, 1, 9, 18)
lossless.append(
    ("rgb-source.tif", write("p3.jxl", "srgb_p3d65_display"))
)
structured("p3.jxl", 0, 1, 11, 13)

lossless.append(
    (
        "rgb-source.tif",
        write(
            "explicit-cicp.jxl",
            "g22_adobergb_display",
            extra=("--cicp", "9,16,0,1"),
        ),
    )
)
structured("explicit-cicp.jxl", 0, 1, 9, 16)

lossless.append(
    (
        "rgb-source.tif",
        write(
            "icc-and-cicp.jxl",
            "g22_adobergb_display",
            extra=("--iccread", icc_profile, "--cicp", "9,16,9,1"),
        ),
    )
)
check("icc-and-cicp.jxl", "--icc", "--uses-original", 1)

# Driven directly, the writer also uses the ICC profile, and the CICP it
# does not use leaves no error on the ImageOutput.
run([profile_test, "--write-icc-and-cicp", "direct-icc-and-cicp.jxl",
     "rgb-source.tif", icc_profile])
check("direct-icc-and-cicp.jxl", "--icc", "--uses-original", 1)
lossless.append(("rgb-source.tif", "direct-icc-and-cicp.jxl"))

lossless.append(
    (
        "rgb-source.tif",
        write(
            "icc-and-range.jxl",
            "g22_adobergb_display",
            extra=("--iccread", icc_profile, "--cicp", "9,16,0,0"),
        ),
    )
)
check("icc-and-range.jxl", "--icc", "--uses-original", 1)

# An explicit CICP JPEG XL has no code for is written as a custom encoding
# when one represents it, and otherwise as no color encoding, which libjxl
# reads as sRGB. The CICP matrix and range are not part of a JXL encoding.
for filename, cicp in (
    ("unsupported-cicp.jxl", "2,13,0,1"),
    ("cicp-matrix.jxl", "9,16,9,1"),
    ("cicp-range.jxl", "9,16,0,0"),
    ("cicp-gamma22.jxl", "1,4,0,1"),
    ("cicp-gamma28.jxl", "1,5,0,1"),
    ("cicp-601.jxl", "1,6,0,1"),
    ("cicp-2020-10bit.jxl", "9,14,0,1"),
    ("cicp-2020-12bit.jxl", "9,15,0,1"),
    ("cicp-custom-primaries.jxl", "5,8,0,1"),
):
    lossless.append(
        (
            "rgb-source.tif",
            write(filename, "g22_adobergb_display", extra=("--cicp", cicp)),
        )
    )
srgb("unsupported-cicp.jxl")
structured("cicp-matrix.jxl", 0, 1, 9, 16)
structured("cicp-range.jxl", 0, 1, 9, 16)
structured("cicp-gamma22.jxl", 0, 1, 1, 65535, 1.0 / 2.2)
structured("cicp-gamma28.jxl", 0, 1, 1, 65535, 1.0 / 2.8)
# H.273 transfers 6, 14 and 15 share the BT.709 curve.
for filename, primaries in (("cicp-601.jxl", 1), ("cicp-2020-10bit.jxl", 9),
                            ("cicp-2020-12bit.jxl", 9)):
    structured(filename, 0, 1, primaries, 1)
structured("cicp-custom-primaries.jxl", 0, 1, 2, 8)

# Every CICP primaries code names a white point as well as a gamut. JPEG XL
# can pair enumerated primaries with another white point, and CICP cannot, so
# such a file reports no CICP. These three differ only in that white point.
assert colorspace(source_dir / "whitepoint-rec709-d65.jxl") \
    == "srgb_rec709_display"
assert "CICP: 1, 13, 0, 1" in run(
    [oiiotool, "--info", "-v", source_dir / "whitepoint-rec709-d65.jxl"])
for filename in ("whitepoint-rec709-dci.jxl", "whitepoint-p3-e.jxl"):
    info = run([oiiotool, "--info", "-v", source_dir / filename])
    assert "CICP" not in info, info
    assert not colorspace(source_dir / filename), info

# Only an explicit CICP JPEG XL cannot represent warns; an inferred one
# (CIE XYZ primaries) silently writes no color encoding.
debug_environment = dict(environment, OPENIMAGEIO_DEBUG="1")
warning = "JPEG XL cannot represent CICP"
assert warning in run([oiiotool, "rgb-source.tif", "--cicp", "2,13,0,1",
                       "-o", "warn-explicit.jxl"], debug_environment)
assert warning not in run([oiiotool, "rgb-source.tif", "--attrib",
                           "oiio:ColorSpace", "lin_ciexyzd65_scene", "-o",
                           "warn-inferred.jxl"], debug_environment)

# CICP 17 scales white by 48/52.37 before its gamma 2.6, and libjxl's DCI
# transfer does not, so an explicit CICP 12,17 is one JPEG XL cannot represent.
assert warning in run([oiiotool, "rgb-source.tif", "--cicp", "12,17,0,1",
                       "-o", "warn-dcdm.jxl"], debug_environment)
srgb("warn-dcdm.jxl")
# And a pure gamma 2.6, which libjxl stores as its DCI transfer, reads back as
# itself with no CICP, so its PNG copy does not become the DCDM identity.
lossless.append(("rgb-source.tif", write("g26-p3d65.jxl", "g26_p3d65_display")))
structured("g26-p3d65.jxl", 0, 1, 11, 17)
assert "CICP" not in run([oiiotool, "--info", "-v", "g26-p3d65.jxl"])
assert colorspace("g26-p3d65.jxl") == "g26_p3d65_display", \
    colorspace("g26-p3d65.jxl")
run([oiiotool, "g26-p3d65.jxl", "-o", "g26-p3d65.png"])
assert colorspace("g26-p3d65.png") != "dcdm_p3d65_display", \
    colorspace("g26-p3d65.png")

# libjxl reduces coordinates and a pure gamma back to an enumerated primaries
# set, white point or curve when they match one. A custom encoding it reduces
# entirely is a named encoding by the time the reader sees it, so it does
# report a CICP code: P3 primaries at the DCI white point, linear.
lossless.append(
    ("rgb-source.tif", write("lin-p3dci.jxl", "oiio:lin_p3dci_display"))
)
structured("lin-p3dci.jxl", 0, 11, 11, 8)
assert "CICP: 11, 8, 0, 1" in run(
    [oiiotool, "--info", "-v", "lin-p3dci.jxl"])
assert colorspace("lin-p3dci.jxl") == "oiio:lin_p3dci_display", \
    colorspace("lin-p3dci.jxl")

# Custom primaries for an explicit CICP come from the built-in identities,
# whatever the active config. XYZ primaries have no usable JXL encoding.
for filename, cicp, transfer in (
    ("default-cicp-pal.jxl", "5,8,0,1", 8),
    ("default-cicp-ntsc.jxl", "6,1,0,1", 1),
    ("default-cicp-240m.jxl", "7,1,0,1", 1),
):
    write(filename, "unknown", extra=("--cicp", cicp),
          run_environment=version_environment)
    structured(filename, 0, 1, 2, transfer)
write("default-cicp-xyz.jxl", "unknown", extra=("--cicp", "10,8,0,1"),
      run_environment=version_environment)
srgb("default-cicp-xyz.jxl")
# Those primaries come from the built-in identities, so with either switch
# set such a CICP writes no encoding at all, not a custom one.
for variable in ("OIIO_DISABLE_OCIO", "OIIO_DISABLE_BUILTIN_OCIO_CONFIGS"):
    write("disabled-cicp-pal.jxl", "unknown", extra=("--cicp", "5,8,0,1"),
          run_environment=dict(version_environment, **{variable: "1"}))
    srgb("disabled-cicp-pal.jxl")

# A pure power is written as a custom gamma whatever the active config: an
# identity the config does not define, or one it declares, is described by
# its built-in definition. Each case wrote no encoding, read as sRGB, before.
# libjxl stores gamma 2.6 as its DCI transfer (17) and P3 primaries as 11.
power_cases = [
    (version_environment, "g22_adobergb_display", "unset-adobe.jxl", 1, 2,
     2.19921875, ADOBE_XY),
    (version_environment, "g24_rec709_scene", "unset-g24.jxl", 1, 1, 2.4),
    (version_environment, "oiio:g26_p3d60_display", "unset-p3d60.jxl", 2, 11,
     2.6),
]
if version >= (2, 4):
    cg = dict(os.environ, OCIO="ocio://cg-config-v2.2.0_aces-v1.3_ocio-v2.4")
    power_cases.append((cg, "g22_rec709_display", "cg-g22.jxl", 1, 1, 2.2))
if version >= (2, 5):
    studio = dict(os.environ,
                  OCIO="ocio://studio-config-v4.0.0_aces-v2.0_ocio-v2.5")
    power_cases += [
        (studio, "Gamma 1.8 Encoded Rec.709", "studio-g18.jxl", 1, 1, 1.8),
        (studio, "Gamma 2.4 Encoded Rec.709", "studio-g24.jxl", 1, 1, 2.4),
        (studio, "Gamma 2.2 Encoded AP1", "studio-g22-ap1.jxl", 2, 2, 2.2),
    ]
for power_environment, name, filename, white, primaries, gamma, *xy in (
        power_cases):
    lossless.append(("rgb-source.tif",
                     write(filename, name, run_environment=power_environment)))
    if gamma == 2.6:
        structured(filename, 0, white, primaries, 17)
    else:
        structured(filename, 0, white, primaries, 65535, 1.0 / gamma, *xy)
    assert "CICP" not in run([oiiotool, "--info", "-v", filename])

# Without interchange roles nothing is derived, and the writer still infers
# CICP from the color space name.
no_roles_config = Path("no-interchange-roles.ocio")
no_roles_config.write_text(
    "ocio_profile_version: 2\n"
    "roles: {default: raw, scene_linear: raw}\n"
    "colorspaces:\n"
    "  - !<ColorSpace> {name: raw, isdata: true}\n"
    "  - !<ColorSpace> {name: sRGB}\n"
)
no_roles_environment = dict(os.environ, OCIO=str(no_roles_config.resolve()))
run([oiiotool, "rgb-source.tif", "-d", "float", "--attrib", "oiio:ColorSpace",
     "sRGB", "-o", "no-roles-srgb.jxl"], no_roles_environment)
structured("no-roles-srgb.jxl", 0, 1, 1, 13)

# A built-in identity is selected by its own name only, like a transform
# endpoint: an alias or legacy spelling of one describes no encoding.
write("no-roles-acescg.jxl", "ACEScg", run_environment=no_roles_environment)
srgb("no-roles-acescg.jxl")

# And the built-in identity is consulted only for a name the config does not
# define: here the config names g22_rec709_display and makes it a 2.36 power.
shadowed_config = Path("shadowed-g22.ocio")
shadowed_config.write_text(
    color_config.read_text()
    .replace("    interop_id: g22_rec709_display\n", "", 1)
    .replace("value: 2.2, style: mirror", "value: 2.36, style: mirror", 1)
)
write("shadowed-g22.jxl", "g22_rec709_display",
      run_environment=dict(environment, OCIO=str(shadowed_config.resolve())))
structured("shadowed-g22.jxl", 0, 1, 1, 65535, 1.0 / 2.36)

lossless.append(
    (
        "rgb-source.tif",
        write(
            "misleading.jxl",
            "pq_rec2020_display",
            run_environment=misleading_environment,
        ),
    )
)
structured("misleading.jxl", 0, 1, 9, 18)

# Primaries libjxl will not store as a custom encoding, because a
# chromaticity is zero (ACES2065-1's green x, CIE XYZ-D65's blue), fall back
# to writing no color encoding rather than failing the whole file.
for filename, name, extra in (
    ("lin-ap0.jxl", "lin_ap0_scene", ()),
    ("lin-xyz.jxl", "lin_ciexyzd65_scene", ()),
    ("unknown.jxl", "unknown", ()),
    ("gamma-only.jxl", "unknown", ("--attrib", "oiio:Gamma", "2.2")),
):
    lossless.append(("rgb-source.tif", write(filename, name, extra=extra)))
    srgb(filename)

lossless.append(
    (
        "gray-source.tif",
        write("gray-adobe.jxl", "g22_adobergb_display", "gray-source.tif"),
    )
)
lossless.append(
    (
        "gray-source.tif",
        write("gray-cicp.jxl", "unknown", "gray-source.tif",
              extra=("--cicp", "1,13,0,1")),
    )
)
structured("gray-cicp.jxl", 1, 1, -1, 13)
structured("gray-adobe.jxl", 1, 1, -1, 65535, 1.0 / 2.19921875)
# libjxl ignores the primaries of a grey encoding but not its white point, so
# a one-channel image of a gamut whose primaries libjxl refuses still keeps
# its encoding. ACES2065-1 is linear, which libjxl stores as transfer 8.
lossless.append(
    (
        "gray-source.tif",
        write("gray-ap0.jxl", "lin_ap0_scene", "gray-source.tif"),
    )
)
structured("gray-ap0.jxl", 1, 2, -1, 8)
lossless.append(
    (
        "rgba-source.tif",
        write("rgba-adobe.jxl", "g22_adobergb_display", "rgba-source.tif"),
    )
)
structured(
    "rgba-adobe.jxl", 0, 1, 2, 65535, 1.0 / 2.19921875, ADOBE_XY
)

write("lossy-adobe.jxl", "g22_adobergb_display", lossy=True)
data_report = structured(
    "lossy-adobe.jxl",
    0,
    1,
    2,
    65535,
    1.0 / 2.19921875,
    ADOBE_XY,
    uses_original=0,
    data_profile=True,
    decode_pfm="native-lossy-data.pfm",
    data_icc="native-lossy-data.icc",
)
data_fields = data_report.split()
assert data_fields[:5] == ["DATA", "0", "1", "2", "65535"]
assert abs(float(data_fields[5]) - 1.0 / 2.19921875) <= 2.0e-7
run([oiiotool, "lossy-adobe.jxl", "--iccwrite", "oiio-lossy-data.icc"])
assert Path("native-lossy-data.icc").read_bytes() == Path(
    "oiio-lossy-data.icc"
).read_bytes()
run(
    [
        idiff,
        "-fail",
        "2e-5",
        "-warn",
        "2e-5",
        "native-lossy-data.pfm",
        "lossy-adobe.jxl",
    ]
)

# libjxl treats every encoding but HLG as display referred, so a file with
# known chromaticities and transfer function reads as the display-referred
# identity with those, and as none where there is no such identity. The
# reader does not consult the active config.
for filename, expected in (
    ("srgb.jxl", "srgb_rec709_display"),
    ("g22.jxl", "g22_rec709_display"),
    ("g24.jxl", "g24_rec709_display"),
    ("g24-scene.jxl", "g24_rec709_display"),
    ("rec1886.jxl", "g24_rec709_display"),
    ("adobe.jxl", "g22_adobergb_display"),
    ("linear.jxl", "lin_rec709_display"),
    ("p3.jxl", "srgb_p3d65_display"),
    ("pq.jxl", "pq_rec2020_display"),
    ("hlg.jxl", "hlg_rec2020_display"),
    ("default-cicp-ntsc.jxl", "oiio:g24_rec601_display"),
    ("cicp-601.jxl", "g24_rec709_display"),
    ("cicp-2020-10bit.jxl", "oiio:g24_rec2020_display"),
    ("lin-ap0.jxl", "srgb_rec709_display"),
    ("gray-cicp.jxl", ""),
):
    assert colorspace(filename) == expected, (filename, colorspace(filename))
for name, expected in (
    ("lin_ap1_scene", ""),
    ("oiio:lin_p3d60_display", "oiio:lin_p3d60_display"),
):
    filename = write(name.replace(":", "-") + ".jxl", name)
    assert colorspace(filename) == expected, (filename, colorspace(filename))
assert "CICP" not in run([oiiotool, "--info", "-v", "lin_ap1_scene.jxl"])
lin_ap1_info = run([oiiotool, "--info", "-v", "lin_ap1_scene.jxl"])
assert_numeric_attribute(lin_ap1_info, "chromaticities", AP1_XY)
assert_numeric_attribute(lin_ap1_info, "oiio:Gamma", (1.0,))

# A display identity the reader reports keeps its tag when copied to another
# format, even on a config (ocio://default) that does not define it.
run([oiiotool, "linear.jxl", "-o", "linear-copy.exr"], version_environment)
run([oiiotool, "linear.jxl", "-d", "uint16", "-o", "linear-copy.dpx"],
    version_environment)
assert 'colorInteropID: "lin_rec709_display"' in run(
    [oiiotool, "--info", "-v", "linear-copy.exr"], version_environment)
assert 'dpx:Transfer: "Linear"' in run(
    [oiiotool, "--info", "-v", "linear-copy.dpx"], version_environment)
structured("lin_ap1_scene.jxl", 0, 2, 2, 8)

# A custom gamma that is not a whole tenth names no identity: 2.36 is
# not g24.
g236_config = Path("g236.ocio")
g236_config.write_text(
    color_config.read_text()
    .replace("name: g22_rec709_display\n", "name: g236\n", 1)
    .replace("    interop_id: g22_rec709_display\n", "", 1)
    .replace("value: 2.2, style: mirror", "value: 2.36, style: mirror", 1)
)
write("g236.jxl", "g236",
      run_environment=dict(environment, OCIO=str(g236_config.resolve())))
structured("g236.jxl", 0, 1, 1, 65535, 1.0 / 2.36)
assert colorspace("g236.jxl") == "", colorspace("g236.jxl")
g236_info = run([oiiotool, "--info", "-v", "g236.jxl"])
assert_numeric_attribute(g236_info, "chromaticities", REC709_XY)
assert_numeric_attribute(g236_info, "oiio:Gamma", (2.36,))

print("JXL structured color profile checks passed")
for source, output in lossless:
    run([idiff, "-fail", "0", "-warn", "0", source, output])
print("JXL lossless pixel checks passed")
print("JXL lossy DATA-profile check passed")
