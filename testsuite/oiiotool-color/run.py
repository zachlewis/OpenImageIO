#!/usr/bin/env python

# Copyright Contributors to the OpenImageIO project.
# SPDX-License-Identifier: Apache-2.0
# https://github.com/AcademySoftwareFoundation/OpenImageIO


import os
import re

redirect = " >> out.txt 2>&1 "

# Same query runtest.py makes for the OpenColorIO version.
library_list = subprocess.check_output(
    [oiio_app("oiiotool").strip(), "--echo", "{getattribute(library_list)}"],
    text=True)
openexr_match = re.search(r"(?:^|;)openexr:OpenEXR (\d+)\.(\d+)\.(\d+)",
                          library_list)
assert openexr_match, "linked OpenEXR version absent from library_list"
openexr_version = tuple(map(int, openexr_match.groups()))


# print("ociover =", ociover)

# Make test pattern with increasing intensity left to right, decreasing
# alpha going down. Carefully done so that the first pixel is 0.0, last
# pixel is 1.0 (correcting for the half pixel offset).
command += oiiotool ("-pattern fill:topleft=0,0,0,1:topright=1,1,1,1:bottomleft=0,0,0,0:bottomright=1,1,1,0 256x256 4 "
                     + " -d uint8 -o greyalpha_lin_srgb.tif")
command += oiiotool ("-pattern fill:topleft=0,0,0:topright=1,1,1:bottomleft=0,0,0:bottomright=1,1,1 256x256 3 "
                     + " -d uint8 -o grey_lin_srgb.tif")


# test --colormap
command += oiiotool ("--autocc " + "../common/tahoe-tiny.tif" +
                     " --colormap inferno " +
                     "-d uint8 -o colormap-inferno.tif")
command += oiiotool ("--autocc " + "../common/tahoe-tiny.tif" +
                     " --colormap .25,.25,.25,0,.5,0,1,0,0 " +
                     "-d uint8 -o colormap-custom.tif")

colormaps = [ "magma", "inferno", "plasma", "viridis", "turbo", "blue-red", "spectrum", "heat" ]
for c in colormaps :
    command += oiiotool ("--pattern fill:left=0,0,0:right=1,1,1 64x64 3" +
                         " --colormap " + c +
                         " -d uint8 -o cmap-" + c + ".tif")

# test unpremult/premult
command += oiiotool ("--pattern constant:color=.1,.1,.1,1 100x100 4 " 
            + " --fill:color=.2,.2,.2,.5 30x30+50+50 "
            + " -d half -o premulttarget.exr")
command += oiiotool ("premulttarget.exr --unpremult -o unpremult.exr")
command += oiiotool ("unpremult.exr --premult -o premult.exr")

# test --no-autopremult on a TGA file thet needs it.
command += oiiotool ("--no-autopremult src/rgba.tga --ch R,G,B -o rgbfromtga.png")

# test --contrast
command += oiiotool ("--autocc " + "../common/tahoe-tiny.tif" +
                     " -contrast:black=0.1:white=0.75 -d uint8 -o contrast-stretch.tif")
command += oiiotool ("--autocc " + "../common/tahoe-tiny.tif" +
                     " -contrast:min=0.1:max=0.75 -d uint8 -o contrast-shrink.tif")
command += oiiotool ("--autocc " + "../common/tahoe-tiny.tif" +
                     " -contrast:black=1:white=0 -d uint8 -o contrast-inverse.tif")
command += oiiotool ("--autocc " + "../common/tahoe-tiny.tif" +
                     " -contrast:black=1,1,.25:white=1,1,0.25 -d uint8 -o contrast-threshold.tif")
command += oiiotool ("--autocc " + "../common/tahoe-tiny.tif" +
                     " -contrast:scontrast=5 -d uint8 -o contrast-sigmoid5.tif")

# test --saturate
command += oiiotool ("--autocc " + "../common/tahoe-tiny.tif" +
                     " --saturate 0 -d uint8 -o tahoe-sat0.tif")
command += oiiotool ("--autocc " + "../common/tahoe-tiny.tif" +
                     " --saturate 2 -d uint8 -o tahoe-sat2.tif")



#
# Test basic color transformation / OCIO functionality
#

# colorconvert without unpremult
if float(ociover) >= 2.2 :
    command += oiiotool ("greyalpha_lin_srgb.tif --colorconvert:unpremult=0 lin_srgb sRGB -o greyalpha_sRGB.tif")
    command += oiiotool ("greyalpha_lin_srgb.tif --colorconvert:unpremult=1 lin_srgb sRGB -o greyalpha_sRGB_un.tif")
    command += oiiotool ("grey_lin_srgb.tif --colorconvert:unpremult=0 lin_srgb sRGB -o grey_sRGB.tif")
    command += oiiotool ("grey_lin_srgb.tif --colorconvert:unpremult=1 lin_srgb sRGB -o grey_sRGB_un.tif")
else:
    command += oiiotool ("greyalpha_lin_srgb.tif --colorconvert:unpremult=0 linear sRGB -o greyalpha_sRGB.tif")
    command += oiiotool ("greyalpha_lin_srgb.tif --colorconvert:unpremult=0 linear Cineon -o greyalpha_Cineon.tif")
    command += oiiotool ("greyalpha_lin_srgb.tif --colorconvert:unpremult=1 linear sRGB -o greyalpha_sRGB_un.tif")
    command += oiiotool ("greyalpha_lin_srgb.tif --colorconvert:unpremult=1 linear Cineon -o greyalpha_Cineon_un.tif")
    command += oiiotool ("grey_lin_srgb.tif --colorconvert:unpremult=0 linear sRGB -o grey_sRGB.tif")
    command += oiiotool ("grey_lin_srgb.tif --colorconvert:unpremult=1 linear sRGB -o grey_sRGB_un.tif")
 
# test color convert by matrix
command += oiiotool ("--autocc " + "../common/tahoe-tiny.tif"+
                     " "
                     + "--ccmatrix 0.805,0.506,-0.311,0,-0.311,0.805,0.506,0,0.506,-0.311,0.805,0,0,0,0,1 "
                     + "-d uint8 -o tahoe-ccmatrix.tif")

# Apply a display
command += oiiotool ("greyalpha_lin_srgb.tif --iscolorspace lin_srgb --ociodisplay \"sRGB - Display\" Un-tone-mapped -o display-sRGB.tif")

# Applying a look
command += oiiotool ("--autocc ../common/tahoe-tiny.tif --ociolook \"ACES 1.3 Reference Gamut Compression\" -o look-default.tif")

# TODO: should test applying a file transform

# test various behaviors and misbehaviors related to OCIO configs.
command += oiiotool ("--nostderr --colorconfig missing.ocio -echo \"Nonexistent config\"", failureok=True)

# Test what happens with autocc and input files with color space names in
# their filenames, for both color-managed and non-color-managed spaces.
#
# Input: We transform an exr with each name to acescg, with autocc.
# - "nope" is an unknown color space, and should warn.
# - "raw" means known to be not color managed, and should warn.
# - "acescg" is known and should end up with output (as acescg) that is
#   unchanged, so the value should still be 0.5.
# - "srgb_tx" is known but should make a real transformation, giving a pixel
#   value that is not 0.5.
for c in ("nope", "raw", "acescg", "srgb_tx") :
    command += oiiotool (f"--pattern constant:color=.5 1x1 3 -d half -o in_{c}.exr")
    command += oiiotool (f"--autocc in_{c}.exr -o out_acescg.exr")
    command += oiiotool (f"out_acescg.exr -echo \"{c}: {{TOP.AVGCOLOR}} {{TOP.nativeformat}}\"")
# Output: We transform an acescg image to outputs with each name, with autocc.
# - "nope" is an unknown color space, and should warn. Or should it?
# - "raw" should retain the 0.5 value with no warning, since you're asking to
#   output a non-color-manaaged image.
# - "acescg" is known and should end up with output (as acescg) that is
#   unchanged, so the value should still be 0.5.
# - "srgb_tx" is known but should make a real transformation, giving a pixel
#   value that is not 0.5.
for c in ("nope", "raw", "acescg", "srgb_tx") :
    command += oiiotool (f"--pattern constant:color=.5 1x1 3 -d half -iscolorspace acescg "
                         + f"-autocc -o out_{c}.exr")
    command += oiiotool (f"out_{c}.exr -echo \"{c}: {{TOP.AVGCOLOR}} {{TOP.nativeformat}}\"")




# Input metadata resolution: facts the file declares outrank its name, PNG's
# raw sRGB chunk outranks gAMA/cHRM, and a declared encoding the config lacks
# (Adobe RGB, absent from the default config) still converts through OIIO's
# internal reference. The fixtures are synthesized so their chunks are exact:
# cHRM in W,R,G,B order and gAMA 45471 (563/256), in units of 1/100000. A
# saturated pixel makes the gamut, not just the exponent, visible.
import struct, zlib

def _png_chunk(kind, payload):
    body = kind + payload
    return (struct.pack(">I", len(payload)) + body
            + struct.pack(">I", zlib.crc32(body) & 0xffffffff))

def _write_png(filename, rgb, chunks):
    rows = b"".join(b"\x00" + bytes(rgb) * 2 for _ in range(2))
    data = b"\x89PNG\r\n\x1a\n"
    data += _png_chunk(b"IHDR", struct.pack(">IIBBBBB", 2, 2, 8, 2, 0, 0, 0))
    for kind, payload in chunks:
        data += _png_chunk(kind, payload)
    data += _png_chunk(b"IDAT", zlib.compress(rows))
    data += _png_chunk(b"IEND", b"")
    with open(filename, "wb") as f:
        f.write(data)

_adobe = [(b"gAMA", struct.pack(">I", 45471)),
          (b"cHRM", struct.pack(">8I", 31270, 32900, 64000, 33000,
                                21000, 71000, 15000, 6000))]
_write_png("adobe_g22.png", (64, 191, 128), _adobe)
_write_png("adobe_g22_srgb_tx.png", (64, 191, 128), _adobe)
_write_png("adobe_g22_raw.png", (64, 191, 128), _adobe)
_write_png("gamma_only_srgb_tx.png", (64, 191, 128),
           [(b"gAMA", struct.pack(">I", 45455))])
_write_png("srgb_chunk_adobe.png", (64, 191, 128), [(b"sRGB", b"\x00")] + _adobe)
# A complete but unsupported RGB cICP claim suppresses the usable sRGB chunk.
_write_png("unsupported_cicp.png", (64, 191, 128),
           [(b"cICP", bytes((3, 3, 0, 1))), (b"sRGB", b"\x00")])
_write_png("dcdm_cicp.png", (64, 191, 128),
           [(b"cICP", bytes((12, 17, 0, 1)))])
# The same claim beside an interop ID no configuration owns, for the rule
# trace: a miss the resolver continues past, then the suppressed chunks.
_write_png("cicp_trace.png", (64, 191, 128),
           [(b"cICP", bytes((3, 3, 0, 1))), (b"sRGB", b"\x00"),
            (b"tEXt", b"colorInteropID\x00NoSuchInteropID")])
# SMPTE 240M primaries are numerically the same as SMPTE 170M, but selecting
# the Rec.601-named identity is reported as an approximate identity match.
_write_png("cicp_rec601.png", (64, 191, 128),
           [(b"cICP", bytes((6, 1, 0, 1)))])
_write_png("cicp_240m.png", (64, 191, 128),
           [(b"cICP", bytes((7, 1, 0, 1)))])

with open("gamma_rules.ocio", "w") as f:
    f.write("""ocio_profile_version: 2.3
roles: {default: Linear, scene_linear: Linear}
file_rules:
  - !<Rule> {name: gamma fixture, colorspace: Linear, pattern: "*_srgb_tx", extension: png}
  - !<Rule> {name: Default, colorspace: default}
colorspaces:
  - !<ColorSpace>
    name: Linear
    aliases: [lin_rec709_scene]
""")

# A real gAMA-only PNG retains its partial numeric fact without making a gamut
# claim. When no configured transfer matches, conversion uses a transfer-only
# process-local endpoint before either the reader label or fallback FileRule.
command += oiiotool ("gamma_only_srgb_tx.png --echo \"gamma-only gamma: "
                     "{TOP.'oiio:Gamma'}\"")
command += oiiotool ("gamma_only_srgb_tx.png --echo \"gamma-only chromaticities: "
                     "<{TOP['chromaticities']}>\"")
command += run_app(oiio_app("oiiotool")
                   + "--debug --colorconfig gamma_rules.ocio "
                     "--autocc:filerules=metadata gamma_only_srgb_tx.png "
                     "-d float -o gamma_reader.exr "
                     "> gamma-reader-trace.txt 2>&1", silent=True)
command += run_app("grep -o \"Resolver rule numeric metadata: matched\" "
                   "gamma-reader-trace.txt")
command += run_app(oiio_app("oiiotool")
                   + "--debug --colorconfig gamma_rules.ocio "
                     "--autocc:filerules=fallback gamma_only_srgb_tx.png "
                     "-d float -o gamma_filerule.exr "
                     "> gamma-filerule-trace.txt 2>&1", silent=True)
command += run_app("grep -o \"Resolver rule numeric metadata: matched\" "
                   "gamma-filerule-trace.txt")

# A real EXR carrying a complete, non-Annex-A linear gamut. Automatic input
# conversion must consume the same endpoint as the explicit selector;
# the non-neutral pixel makes the gamut matrix observable.
_exr_xy = "0.695,0.305,0.140,0.820,0.100,0.005,0.32168,0.33767"
def _inverse3(m):
    a, b, c, d, e, f, g, h, i = sum(m, ())
    det = a*(e*i-f*h) - b*(d*i-f*g) + c*(d*h-e*g)
    return (((e*i-f*h)/det, (c*h-b*i)/det, (b*f-c*e)/det),
            ((f*g-d*i)/det, (a*i-c*g)/det, (c*d-a*f)/det),
            ((d*h-e*g)/det, (b*g-a*h)/det, (a*e-b*d)/det))

def _mul3(a, b):
    return tuple(tuple(sum(a[r][k]*b[k][c] for k in range(3))
                       for c in range(3)) for r in range(3))

def _npm(xy):
    columns = tuple((xy[2*i]/xy[2*i+1], 1,
                     (1-xy[2*i]-xy[2*i+1])/xy[2*i+1]) for i in range(3))
    p = tuple(tuple(columns[c][r] for c in range(3)) for r in range(3))
    w = (xy[6]/xy[7], 1, (1-xy[6]-xy[7])/xy[7])
    inv = _inverse3(p)
    scale = tuple(sum(inv[r][c]*w[c] for c in range(3)) for r in range(3))
    return tuple(tuple(p[r][c]*scale[c] for c in range(3)) for r in range(3))

_xy_values = tuple(float(v) for v in _exr_xy.split(","))
_ap0 = (0.7347,0.2653,0,1,0.0001,-0.077,0.32168,0.33767)
_to_ap0 = _mul3(_inverse3(_npm(_ap0)), _npm(_xy_values))
_m = sum(_to_ap0, ())
_matrix = ",".join(str(v) for v in
                   (_m[0],_m[1],_m[2],0,_m[3],_m[4],_m[5],0,
                    _m[6],_m[7],_m[8],0,0,0,0,1))
with open("exr_numeric.ocio", "w") as f:
    f.write("""ocio_profile_version: 2.3
roles: {default: Reference, scene_linear: Reference, aces_interchange: Reference}
file_rules:
  - !<Rule> {name: Default, colorspace: default}
colorspaces:
  - !<ColorSpace>
    name: Reference
looks:
  - !<Look>
    name: identity
    process_space: Reference
    transform: !<MatrixTransform> {}
""")
command += oiiotool ("--pattern constant:color=.25,.30,.40 1x1 3 "
                     "\"--attrib:type=float[8]\" chromaticities " + _exr_xy
                     + " --attrib oiio:Gamma 1"
                     + " --eraseattrib oiio:ColorSpace"
                     " --eraseattrib colorInteropID -d float"
                     " -o arbitrary_linear.exr")
command += oiiotool ("--colorconfig exr_numeric.ocio --autocc "
                     "arbitrary_linear.exr -d float "
                     "-o exr_numeric_actual.exr")
command += oiiotool ("--colorconfig exr_numeric.ocio arbitrary_linear.exr "
                     "--ccmatrix:transpose=1 " + _matrix
                     + " --iscolorspace Reference -d float "
                       "-o exr_numeric_expected.exr")
command += oiiotool ("--fail 0.00001 --warn 0.00001 exr_numeric_actual.exr "
                     "exr_numeric_expected.exr --diff")
command += oiiotool ("--colorconfig exr_numeric.ocio arbitrary_linear.exr "
                     "--ociolook:to=Reference identity -d float "
                     "-o look_numeric.exr")
command += oiiotool ("--fail 0.00001 --warn 0.00001 look_numeric.exr "
                     "exr_numeric_expected.exr --diff")
command += oiiotool ("--autocc adobe_g22.png -d float -o autocc_adobe_g22.exr")
command += oiiotool ("adobe_g22.png --colorconvert g22_adobergb_display scene_linear"
                     " -d float -o expected_adobe_g22.exr")
command += oiiotool ("--fail 0.0001 --warn 0.0001 autocc_adobe_g22.exr expected_adobe_g22.exr --diff")
# Explicit retagging replaces stronger input facts before implicit resolution.
command += oiiotool ('srgb_chunk_adobe.png --iscolorspace g22_adobergb_display'
                     ' --colorconvert "" scene_linear -d float -o retagged_adobe.exr')
command += oiiotool ("--fail 0.0001 --warn 0.0001 retagged_adobe.exr expected_adobe_g22.exr --diff")
command += oiiotool ("-i:autocc=1:numericstate=scene adobe_g22.png -d float "
                     "-o autocc_adobe_g22_scene.exr")
command += oiiotool ("adobe_g22.png --colorconvert g22_adobergb_scene scene_linear"
                     " -d float -o expected_adobe_g22_scene.exr")
command += oiiotool ("--fail 0.0001 --warn 0.0001 autocc_adobe_g22_scene.exr "
                     "expected_adobe_g22_scene.exr --diff")
command += oiiotool ("--autocc adobe_g22_srgb_tx.png -d float -o autocc_misnamed.exr")
command += oiiotool ("--fail 0.0001 --warn 0.0001 autocc_misnamed.exr expected_adobe_g22.exr --diff")
command += oiiotool ("--autocc srgb_chunk_adobe.png -d float -o autocc_chunk.exr")
command += oiiotool ("srgb_chunk_adobe.png --colorconvert srgb_rec709_scene scene_linear"
                     " -d float -o expected_chunk.exr")
command += oiiotool ("--fail 0.0001 --warn 0.0001 autocc_chunk.exr expected_chunk.exr --diff")
command += oiiotool ("--autocc adobe_g22.png --echo \"adobe_g22: {TOP.'oiio:ColorSpace'} {TOP.'colorInteropID'}\"")
# OIIO's own gamma names spell Rec.709 primaries, so cHRM and gAMA that
# agree with one keep the label, and asserting the label keeps them.
_write_png("rec709_g15.png", (64, 191, 128),
           [(b"gAMA", struct.pack(">I", 66667)),
            (b"cHRM", struct.pack(">8I", 31270, 32900, 64000, 33000,
                                  30000, 60000, 15000, 6000))])
command += oiiotool ("rec709_g15.png --echo \"rec709_g15: {TOP.'oiio:ColorSpace'}\" "
                     "--iscolorspace g15_rec709_scene --echo \"asserted g15: "
                     "chroma <{TOP['chromaticities']}> gamma <{TOP['oiio:Gamma']}>\"")

# FileRules policy changes only the resolver position. Metadata-only preserves
# the file's Adobe RGB facts, First lets the filename rule win, and a per-input
# modifier overrides the persistent setting. Pixel comparisons prove which
# source transform was actually applied.
command += oiiotool ("--autocc:filerules=metadata adobe_g22_srgb_tx.png "
                     "-d float -o autocc_rules_metadata.exr")
command += oiiotool ("--fail 0.0001 --warn 0.0001 autocc_rules_metadata.exr "
                     "expected_adobe_g22.exr --diff")
command += oiiotool ("--autocc:filerules=first adobe_g22_srgb_tx.png "
                     "-d float -o autocc_rules_first.exr")
command += oiiotool ("--fail 0.0001 --warn 0.0001 autocc_rules_first.exr "
                     "expected_chunk.exr --diff")
command += oiiotool ("--autocc:filerules=first "
                     "-i:filerules=metadata adobe_g22_srgb_tx.png "
                     "-d float -o autocc_rules_input.exr")
command += oiiotool ("--fail 0.0001 --warn 0.0001 autocc_rules_input.exr "
                     "expected_adobe_g22.exr --diff")
command += oiiotool ("--autocc:filerules=first --autocc "
                     "adobe_g22_srgb_tx.png -d float "
                     "-o autocc_rules_reset.exr")
command += oiiotool ("--fail 0.0001 --warn 0.0001 autocc_rules_reset.exr "
                     "expected_adobe_g22.exr --diff")
command += oiiotool ("--autocc:filerules=first adobe_g22_raw.png "
                     "-d float -o autocc_rules_data.exr")
# (Silent: with OPENIMAGEIO_DEBUG set, the OpenEXR writer says that the
# Adobe RGB file is written as "unknown".)
command += oiiotool ("-i:autocc=0 adobe_g22_raw.png -d float "
                     "-o expected_rules_data.exr", silent=True)
command += oiiotool ("--fail 0.0001 --warn 0.0001 autocc_rules_data.exr "
                     "expected_rules_data.exr --diff")
command += oiiotool ("autocc_rules_data.exr --echo \"rules data: "
                     "{TOP.'oiio:ColorSpace'} {TOP.'colorInteropID'}\"")
command += oiiotool ("--nostderr -i:autocc=1:filerules=bogus adobe_g22.png",
                     failureok=True)

# A caller-selected failover is consulted after ordinary resolution is
# exhausted, including after PNG cICP supremacy suppresses the weaker chunk.
command += oiiotool ("--autocc:failover=g22_adobergb_scene unsupported_cicp.png "
                     "-d float -o autocc_failover.exr")
command += oiiotool ("-i:autocc=0 unsupported_cicp.png --colorconvert "
                     "g22_adobergb_scene scene_linear -d float "
                     "-o expected_failover.exr")
command += oiiotool ("--fail 0.0001 --warn 0.0001 autocc_failover.exr "
                     "expected_failover.exr --diff")
command += oiiotool ("autocc_failover.exr --echo \"failover: "
                     "{TOP.'oiio:ColorSpace'} {TOP.'colorInteropID'}\"")

# CICP 12/17 is the read-only DCDM P3-D65 endpoint. Automatic input decoding
# must match an explicit conversion through that endpoint, including its
# DCI-white headroom scaling.
command += oiiotool ("--autocc dcdm_cicp.png -d float -o dcdm_autocc.exr")
command += oiiotool ("-i:autocc=0 dcdm_cicp.png --colorconvert "
                     "dcdm_p3d65_display scene_linear -d float "
                     "-o dcdm_explicit.exr")
command += oiiotool ("--fail 0 --warn 0 dcdm_autocc.exr dcdm_explicit.exr "
                     "--diff")
# Without a failover, and with an invalid or explicitly empty per-input
# override, the same unresolved source retains the established no-conversion
# behavior. Presence of the per-input option wins over persistent state.
command += oiiotool ("-i:autocc=1 unsupported_cicp.png -d float "
                     "-o autocc_no_failover.exr")
command += oiiotool ("-i:autocc=0 unsupported_cicp.png -d float "
                     "-o expected_no_failover.exr")
command += oiiotool ("--fail 0.0001 --warn 0.0001 autocc_no_failover.exr "
                     "expected_no_failover.exr --diff")
command += oiiotool ("--autocc:failover=srgb_rec709_scene "
                     "-i:autocc=1:failover= unsupported_cicp.png -d float "
                     "-o autocc_empty_failover.exr")
command += oiiotool ("--fail 0.0001 --warn 0.0001 autocc_empty_failover.exr "
                     "expected_no_failover.exr --diff")
command += oiiotool ("-i:autocc=1:failover=NoSuchSpace unsupported_cicp.png "
                     "-d float -o autocc_invalid_failover.exr")
command += oiiotool ("--fail 0.0001 --warn 0.0001 autocc_invalid_failover.exr "
                     "expected_no_failover.exr --diff")
# A present, usable per-input value also wins over a different persistent one.
command += oiiotool ("--autocc:failover=g22_adobergb_scene "
                     "-i:autocc=1:failover=srgb_rec709_scene "
                     "unsupported_cicp.png -d float "
                     "-o autocc_input_failover.exr")
command += oiiotool ("-i:autocc=0 unsupported_cicp.png --colorconvert "
                     "srgb_rec709_scene scene_linear -d float "
                     "-o expected_input_failover.exr")
command += oiiotool ("--fail 0.0001 --warn 0.0001 autocc_input_failover.exr "
                     "expected_input_failover.exr --diff")

# Under --debug the one resolution explains itself rule by rule, in the order
# the rules were reached: the interop ID misses, the full RGB cICP claim
# misses and suppresses the container's legacy chunks and the reader's label,
# FileRules still run, and the failover matches. Only the rule and outcome are
# compared, so a candidate spelling this config chooses cannot churn the
# reference. Nothing after the winning rule is recorded, because nothing after
# it runs.
command += run_app(oiio_app("oiiotool")
                   + "--debug --autocc:failover=g22_adobergb_scene "
                     "cicp_trace.png -d float -o trace_debug.exr "
                     "> resolver-trace.txt 2>&1", silent=True)
command += run_app('grep -o "Resolver rule [a-zA-Z ]*: [a-z]*" '
                   "resolver-trace.txt")
# One suppression reason, on the failed claim and on each rule it hid.
command += run_app('grep -c "legacy color chunks are suppressed" '
                   "resolver-trace.txt")
# One resolution, so one summary line and one terminal rule.
command += run_app('grep -c "Metadata and FileRules" resolver-trace.txt')
command += run_app('grep -c "Resolver rule failover: matched" '
                   "resolver-trace.txt")
command += run_app('grep -c "Color metadata warning: the color interop ID '
                   'fails the CIF Annex B grammar" resolver-trace.txt')
# Tracing is a debug-only explanation: same pixels, same color metadata.
command += oiiotool ("--autocc:failover=g22_adobergb_scene cicp_trace.png "
                     "-d float -o trace_quiet.exr")
command += oiiotool ("--fail 0 --warn 0 trace_debug.exr trace_quiet.exr "
                     "--diff")
command += oiiotool ("trace_debug.exr --echo \"traced: "
                     "{TOP.'oiio:ColorSpace'} {TOP.'colorInteropID'}\"")
command += oiiotool ("trace_quiet.exr --echo \"untraced: "
                     "{TOP.'oiio:ColorSpace'} {TOP.'colorInteropID'}\"")
# Exact and identity-approximate CICP claims terminate at the same rule and
# convert identically; debug reports the evidence distinction.
command += run_app(oiio_app("oiiotool")
                   + "--debug --autocc cicp_rec601.png -d float "
                     "-o cicp_rec601.exr > cicp_rec601_trace.txt 2>&1",
                   silent=True)
command += run_app('grep -o "Resolver rule CICP: [a-z]*" '
                   "cicp_rec601_trace.txt")
command += run_app(oiio_app("oiiotool")
                   + "--debug --autocc cicp_240m.png -d float "
                     "-o cicp_240m.exr > cicp_240m_trace.txt 2>&1",
                   silent=True)
command += run_app('grep -o "Resolver rule CICP: [a-z]*" '
                   "cicp_240m_trace.txt")
command += oiiotool ("--fail 0 --warn 0 cicp_rec601.exr cicp_240m.exr "
                     "--diff")
# Nothing well formed is reported as malformed: the complete RGB cICP tuple
# above is one this build cannot identify, not a broken one, and neither it nor
# any chunk its claim suppressed is called invalid.
command += run_app('grep -c ": invalid" resolver-trace.txt', failureok=True)

# A non-finite coordinate is malformed. Classification still falls through to
# the same failover and leaves pixels and output metadata unchanged.
command += oiiotool ("--pattern constant:color=.25,.30,.40 1x1 3 "
                     "\"--attrib:type=float[8]\" chromaticities "
                     "0.64,0.33,0.30,nan,0.15,0.06,0.3127,0.3290"
                     " --attrib oiio:Gamma 1"
                     " --eraseattrib oiio:ColorSpace"
                     " --eraseattrib colorInteropID -d float"
                     " -o nonfinite_gamut.exr")
command += run_app(oiio_app("oiiotool")
                   + "--debug --autocc:failover=g22_adobergb_scene "
                     "nonfinite_gamut.exr -d float -o invalid_debug.exr "
                     "> resolver-invalid.txt 2>&1", silent=True)
command += run_app('grep -o "Resolver rule numeric metadata: [a-z]*" '
                   "resolver-invalid.txt")
command += run_app('grep -c "non-finite chromaticity or negative/non-finite '
                   'supplied gamma" '
                   "resolver-invalid.txt")
command += run_app('grep -c "Resolver rule failover: matched" '
                   "resolver-invalid.txt")
command += oiiotool ("--autocc:failover=g22_adobergb_scene nonfinite_gamut.exr "
                     "-d float -o invalid_quiet.exr")
command += oiiotool ("-i:autocc=0 nonfinite_gamut.exr --colorconvert "
                     "g22_adobergb_scene scene_linear -d float "
                     "-o expected_invalid.exr")
command += oiiotool ("--fail 0.0001 --warn 0.0001 invalid_quiet.exr "
                     "expected_invalid.exr --diff")
command += oiiotool ("--fail 0 --warn 0 invalid_debug.exr invalid_quiet.exr "
                     "--diff")
command += oiiotool ("invalid_debug.exr --echo \"malformed traced: "
                     "{TOP.'oiio:ColorSpace'} {TOP.'colorInteropID'}\"")
command += oiiotool ("invalid_quiet.exr --echo \"malformed untraced: "
                     "{TOP.'oiio:ColorSpace'} {TOP.'colorInteropID'}\"")

# Ordinary exhaustion is separate from PNG supremacy: write an unusable label
# and unknown identity in a simple EXR, with no numeric facts. The reader
# leaves the label unset, because a file's "unknown" is evidence rather than a
# statement about the pixels, and keeps the identity. The caller-selected
# failover still outranks the terminal answer.
command += oiiotool ("--pattern constant:color=.2,.4,.7 2x2 3 "
                     "--attrib oiio:ColorSpace NoSuchSpace "
                     "--attrib colorInteropID unknown -o unresolved.exr")
command += oiiotool ("unresolved.exr --echo \"ordinary source: "
                     "<{TOP['oiio:ColorSpace']}> {TOP.'colorInteropID'}\"")
# The same evidence in another case is the same evidence, to either reader.
command += oiiotool ("--pattern constant:color=.2,.4,.7 2x2 3 "
                     "--attrib oiio:ColorSpace NoSuchSpace "
                     "--attrib colorInteropID Unknown -o unresolved_case.exr")
for core in (1, 0) :
    command += oiiotool (f"-oiioattrib openexr:core {core} unresolved_case.exr "
                         f"--echo \"ordinary source cased core={core}: "
                         "<{TOP['oiio:ColorSpace']}> {TOP.'colorInteropID'}\"")
# The same image with nothing said about its color at all, which is the only
# case the missing-source policy answers.
command += oiiotool ("--pattern constant:color=.2,.4,.7 2x2 3 "
                     "--iscolorspace \"\" -o no_facts.exr")
command += oiiotool ("no_facts.exr --echo \"no facts: "
                     "<{TOP['oiio:ColorSpace']}> <{TOP['colorInteropID']}>\"")
command += oiiotool ("--autocc:failover=srgb_rec709_scene unresolved.exr "
                     "-d float -o ordinary_failover.exr")
command += oiiotool ("-i:autocc=0 unresolved.exr --colorconvert "
                     "srgb_rec709_scene scene_linear -d float "
                     "-o expected_ordinary_failover.exr")
command += oiiotool ("--fail 0.0001 --warn 0.0001 ordinary_failover.exr "
                     "expected_ordinary_failover.exr --diff")
command += oiiotool ("-i:autocc=1 unresolved.exr -d float "
                     "-o ordinary_absent.exr")
command += oiiotool ("-i:autocc=0 unresolved.exr -d float "
                     "-o expected_ordinary_absent.exr")
command += oiiotool ("--fail 0.0001 --warn 0.0001 ordinary_absent.exr "
                     "expected_ordinary_absent.exr --diff")
command += oiiotool ("ordinary_absent.exr --echo \"ordinary absent: "
                     "<{TOP['oiio:ColorSpace']}> {TOP.'colorInteropID'}\"")
command += oiiotool ("-i:autocc=0 no_facts.exr -d float "
                     "-o expected_no_facts_absent.exr")

# Opt-in missing-source policy follows the active config and input context.
# Its selected data space is tagged without changing pixels, and both a
# per-input override and a later bare --autocc restore the established policy.
with open("missing_policy.ocio", "w") as f:
    f.write("""ocio_profile_version: 2.3
strictparsing: false
roles: {default: FileDefault, scene_linear: Reference}
file_rules:
  - !<Rule> {name: Default, colorspace: FileDefault}
colorspaces:
  - !<ColorSpace>
    name: Reference
  - !<ColorSpace>
    name: FileDefault
    to_scene_reference: !<ExponentTransform> {value: 2.0}
  - !<ColorSpace>
    name: Data
    isdata: true
""")
command += oiiotool ("--colorconfig missing_policy.ocio "
                     "--autocc:missing=config no_facts.exr -d float "
                     "-o missing_default.exr")
command += oiiotool ("--colorconfig missing_policy.ocio -i:autocc=0 no_facts.exr "
                     "--colorconvert FileDefault Reference -d float "
                     "-o expected_missing_default.exr")
command += oiiotool ("--fail 0.0001 --warn 0.0001 missing_default.exr "
                     "expected_missing_default.exr --diff")
# The diagnostic consumes the same structured result that drives autocc.
command += run_app(oiio_app("oiiotool")
                   + "--debug --colorconfig missing_policy.ocio "
                     "--autocc:missing=config no_facts.exr "
                     "> provenance-debug.txt 2>&1", silent=True)
command += run_app('grep "Metadata and FileRules" provenance-debug.txt')
with open("missing_data_policy.ocio", "w") as f:
    f.write("""ocio_profile_version: 2.3
strictparsing: false
roles: {default: Data, scene_linear: Reference}
file_rules:
  - !<Rule> {name: Default, colorspace: Data}
colorspaces:
  - !<ColorSpace>
    name: Reference
  - !<ColorSpace>
    name: Data
    isdata: true
""")
command += oiiotool ("--colorconfig missing_data_policy.ocio "
                     "-i:autocc=1:missing=config no_facts.exr -d float "
                     "-o missing_data.exr")
command += oiiotool ("--fail 0.0001 --warn 0.0001 missing_data.exr "
                     "expected_no_facts_absent.exr --diff")
command += oiiotool ("missing_data.exr --echo \"missing data: "
                     "<{TOP['oiio:ColorSpace']}> {TOP.'colorInteropID'}\"")
command += oiiotool ("--colorconfig missing_policy.ocio --autocc:missing=config "
                     "-i:missing=none no_facts.exr -d float "
                     "-o missing_input_none.exr")
command += oiiotool ("--fail 0.0001 --warn 0.0001 missing_input_none.exr "
                     "expected_no_facts_absent.exr --diff")
command += oiiotool ("--colorconfig missing_policy.ocio --autocc:missing=config "
                     "--autocc no_facts.exr -d float -o missing_reset.exr")
command += oiiotool ("--fail 0.0001 --warn 0.0001 missing_reset.exr "
                     "expected_no_facts_absent.exr --diff")
command += oiiotool ("--nostderr -i:autocc=1:missing=bogus no_facts.exr",
                     failureok=True)
# A file that did state something ends as "unknown" whatever the policy says:
# the config's default assignment never overwrites a statement the file made.
command += oiiotool ("--colorconfig missing_policy.ocio "
                     "--autocc:missing=config unresolved.exr -d float "
                     "-o stated_unknown.exr")
command += oiiotool ("--fail 0.0001 --warn 0.0001 stated_unknown.exr "
                     "expected_ordinary_absent.exr --diff")
command += oiiotool ("stated_unknown.exr --echo \"stated unknown: "
                     "<{TOP['oiio:ColorSpace']}> {TOP.'colorInteropID'}\"")
with open("missing_no_default.ocio", "w") as f:
    f.write("""ocio_profile_version: 2.3
strictparsing: false
environment: {DEFAULT_SOURCE: Reference}
roles: {scene_linear: Reference}
file_rules:
  - !<Rule> {name: Default, colorspace: $DEFAULT_SOURCE}
colorspaces:
  - !<ColorSpace>
    name: Reference
""")
command += oiiotool ("--nostderr --colorconfig missing_no_default.ocio "
                     "-i:autocc=1:missing=config:key=DEFAULT_SOURCE:"
                     "value=Missing no_facts.exr", failureok=True)

# A configured non-data space literally named `unknown` is the config
# author's catch-space: every image this configuration cannot place goes
# through it, whether the file said "unknown" itself or the resolver ran out
# of evidence under strict parsing. These strict configs declare an empty
# environment, so a context variable they do not declare (the "$NOPE" below)
# cannot be taken from the process environment.
with open("strict_unknown.ocio", "w") as f:
    f.write("""ocio_profile_version: 2.3
strictparsing: true
environment: {}
roles: {default: Reference, scene_linear: Reference}
file_rules:
  - !<Rule> {name: Default, colorspace: default}
colorspaces:
  - !<ColorSpace>
    name: Reference
  - !<ColorSpace>
    name: unknown
    to_scene_reference: !<ExponentTransform> {value: 2.0}
""")
command += oiiotool ("--colorconfig strict_unknown.ocio "
                     "--autocc:missing=config unresolved.exr -d float "
                     "-o configured_result.exr")
command += oiiotool ("--colorconfig strict_unknown.ocio "
                     "-i:autocc=0 unresolved.exr --colorconvert unknown Reference "
                     "-d float -o expected_configured_result.exr")
command += oiiotool ("--fail 0.0001 --warn 0.0001 configured_result.exr "
                     "expected_configured_result.exr --diff")
# The same strict config without that catch-space gets the generated
# `unknown`, which carries a private terminal disposition: crop proves an
# ordinary image operation preserves it, both target-bearing outputs stay
# unconverted, and the writer must not serialize the marker.
with open("strict_plain.ocio", "w") as f:
    f.write("""ocio_profile_version: 2.3
strictparsing: true
environment: {}
roles: {default: Reference, scene_linear: Reference}
file_rules:
  - !<Rule> {name: Default, colorspace: default}
colorspaces:
  - !<ColorSpace>
    name: Reference
""")
command += oiiotool ("-i:autocc=0 unsupported_cicp.png --crop 1x2+0+0 "
                     "-d float -o expected_terminal_unknown.exr")
command += oiiotool ("--colorconfig strict_plain.ocio "
                     "--autocc:missing=config unsupported_cicp.png "
                     "--crop 1x2+0+0 -d float -o terminal_Reference.exr "
                     "-o terminal_again_Reference.exr")
for result in ("terminal_Reference.exr", "terminal_again_Reference.exr"):
    command += oiiotool ("--fail 0.0001 --warn 0.0001 " + result + " "
                         "expected_terminal_unknown.exr --diff")
command += oiiotool ("terminal_Reference.exr --echo \"terminal unknown: "
                     "<{TOP['oiio:ColorSpace']}> {TOP.'colorInteropID'} marker "
                     "<{TOP['oiio:autoccTerminalUnknown']}>\"")
# In the catch-space config the same image is placed there instead, so the
# pixels do convert, and an explicit assignment of that name does the same.
command += oiiotool ("--colorconfig strict_unknown.ocio "
                     "--autocc:missing=config unsupported_cicp.png "
                     "--crop 1x2+0+0 -d float -o caught_Reference.exr")
command += oiiotool ("--colorconfig strict_unknown.ocio "
                     "-i:autocc=0 expected_terminal_unknown.exr "
                     "--colorconvert unknown Reference -d float "
                     "-o expected_caught_Reference.exr")
command += oiiotool ("--fail 0.0001 --warn 0.0001 caught_Reference.exr "
                     "expected_caught_Reference.exr --diff")
# But a name the caller supplies that this configuration does not define is
# the caller's own mistake, and the catch-space does not answer it: the image
# ends as "unknown" and is left unconverted, where the same resolution
# without that name goes through the catch-space above.
command += oiiotool ("--colorconfig strict_unknown.ocio "
                     "--autocc:failover=NoSuchSpace unsupported_cicp.png "
                     "--crop 1x2+0+0 -d float -o nospace_failover.exr")
command += oiiotool ("--fail 0.0001 --warn 0.0001 nospace_failover.exr "
                     "expected_terminal_unknown.exr --diff")
command += oiiotool ("nospace_failover.exr --echo \"failover mistake: "
                     "<{TOP['oiio:ColorSpace']}> {TOP.'colorInteropID'} marker "
                     "<{TOP['oiio:autoccTerminalUnknown']}>\"")
# A context variable the effective context does not define comes back
# unexpanded, so it is such a non-empty name rather than one that resolved to
# nothing: the same caller's mistake, not the catch-space.
command += oiiotool ("--colorconfig strict_unknown.ocio "
                     "--autocc:failover='$NOPE' unsupported_cicp.png "
                     "--crop 1x2+0+0 -d float -o undefined_var_failover.exr")
command += oiiotool ("--fail 0.0001 --warn 0.0001 undefined_var_failover.exr "
                     "expected_terminal_unknown.exr --diff")
command += oiiotool ("undefined_var_failover.exr --echo \"failover undefined "
                     "variable: "
                     "<{TOP['oiio:ColorSpace']}> {TOP.'colorInteropID'} marker "
                     "<{TOP['oiio:autoccTerminalUnknown']}>\"")

# An embedded ICC profile is a color space fact, not only metadata. A matrix
# and tone curve profile is decoded by OpenColorIO and measured, so an sRGB
# profile drives the conversion even though these files carry no sRGB chunk
# and the reader's "no color information" default label would have said
# something else. A profile whose colorants nothing publishes still describes
# a complete conversion and becomes a process-local selector for this run. The
# selector is spelled from the SHA-1 of the profile's own bytes wherever the
# active config leaves that spelling free, which is the case here and is why it
# can be recomputed below rather than taken from OIIO. That is a property of this
# fixture, not a portable naming convention: a config that already used the
# spelling would keep it and the selector would be spelled differently. A
# profile OpenColorIO cannot decode makes no claim, mints nothing, and lets
# the reader's label apply.
import hashlib

def _s15f16(v):
    return struct.pack(">i", int(round(v * 65536.0)))

def _xyz(x, y, z):
    return b"XYZ " + b"\x00" * 4 + _s15f16(x) + _s15f16(y) + _s15f16(z)

def _curv_table(values):
    body = b"".join(struct.pack(">H", max(0, min(65535, int(round(v * 65535)))))
                    for v in values)
    return b"curv" + b"\x00" * 4 + struct.pack(">I", len(values)) + body

def _curv_gamma(g):
    return (b"curv" + b"\x00" * 4 + struct.pack(">I", 1)
            + struct.pack(">H", int(round(g * 256))))

def _icc_profile(tags):
    header = bytearray(128)
    header[8:12] = b"\x02\x40\x00\x00"   # profile version 2.4
    header[12:16] = b"mntr"              # display device class
    header[16:20] = b"RGB "              # device color space
    header[20:24] = b"XYZ "              # profile connection space
    header[36:40] = b"acsp"              # magic
    header[68:80] = _pcs_white[8:]       # connection space illuminant
    directory = struct.pack(">I", len(tags))
    body = b""
    base = 128 + 4 + 12 * len(tags)
    for signature, payload in tags:
        directory += signature + struct.pack(">II", base + len(body),
                                             len(payload))
        body += payload + b"\x00" * (-len(payload) % 4)
    blob = bytes(header) + directory + body
    return struct.pack(">I", len(blob)) + blob[4:]

def _iccp(profile):
    return (b"iCCP", b"Embedded Profile\x00\x00" + zlib.compress(profile))

def _trc(curve):
    return [(b"rTRC", curve), (b"gTRC", curve), (b"bTRC", curve)]

_pcs_white = _xyz(0.9642, 1.0, 0.8249)
# The published sRGB v2 colorants: the Rec.709 primaries stated adapted to the
# ICC D50 connection space, which OpenColorIO's own fixed Bradford step undoes
# when it decodes.
_srgb_colorants = [(b"rXYZ", _xyz(0.4360, 0.2225, 0.0139)),
                   (b"gXYZ", _xyz(0.3851, 0.7169, 0.0971)),
                   (b"bXYZ", _xyz(0.1431, 0.0606, 0.7139)),
                   (b"wtpt", _pcs_white)]
_srgb_curve = _curv_table([v / 12.92 if v <= 0.04045
                           else ((v + 0.055) / 1.055) ** 2.4
                           for v in (i / 1023.0 for i in range(1024))])
srgb_icc = _icc_profile(_srgb_colorants + _trc(_srgb_curve))
# A green colorant no standard states, with a pure 1.8 power.
_odd_colorants = [(b"rXYZ", _xyz(0.4360, 0.2225, 0.0139)),
                  (b"gXYZ", _xyz(0.2000, 0.7169, 0.1500)),
                  (b"bXYZ", _xyz(0.1431, 0.0606, 0.7139)),
                  (b"wtpt", _pcs_white)]
odd_icc = _icc_profile(_odd_colorants + _trc(_curv_gamma(1.8)))
# A structurally valid LUT-based profile: one lut8Type A2B0 tag (identity
# matrix, identity curves, 2x2x2 identity grid) and no colorant or TRC tags.
# libpng accepts and exposes it, while the bounded matrix/TRC reader
# deliberately declines it.
_ramp = bytes(range(256))
_grid = bytes(255 * c for r in (0, 1) for g in (0, 1) for b in (0, 1)
              for c in (r, g, b))
_a2b0 = (b"mft1" + b"\x00" * 4 + bytes([3, 3, 2, 0])
         + b"".join(_s15f16(v) for v in (1, 0, 0, 0, 1, 0, 0, 0, 1))
         + _ramp * 3 + _grid + _ramp * 3)
_text = b"LUT-based test profile\x00"
_desc = (b"desc" + b"\x00" * 4 + struct.pack(">I", len(_text)) + _text
         + b"\x00" * 78)
unsupported_icc = _icc_profile([(b"desc", _desc), (b"wtpt", _pcs_white),
                                (b"A2B0", _a2b0)])

_write_png("srgb_icc.png", (64, 191, 128), [_iccp(srgb_icc)])
_write_png("odd_icc.png", (64, 191, 128), [_iccp(odd_icc)])
_write_png("unsupported_icc.png", (64, 191, 128), [_iccp(unsupported_icc)])
with open("unsupported.icc", "wb") as f:
    f.write(unsupported_icc)

command += oiiotool ("-i:autocc=1 srgb_icc.png -d float -o autocc_icc.exr")
command += oiiotool ("srgb_icc.png --colorconvert srgb_rec709_display scene_linear"
                     " -d float -o expected_icc.exr")
command += oiiotool ("--fail 0.0001 --warn 0.0001 autocc_icc.exr expected_icc.exr --diff")
# The unmatched profile's selector is usable by name, in the same process that
# read it, and performs the profile's own decode.
odd_selector = "<synthetic>icc_" + hashlib.sha1(odd_icc).hexdigest()
command += oiiotool ("-i:autocc=1 odd_icc.png -d float -o autocc_odd.exr"
                     " -i:autocc=0 odd_icc.png --colorconvert \"" + odd_selector +
                     "\" scene_linear -d float -o expected_odd.exr")
command += oiiotool ("--fail 0.0001 --warn 0.0001 autocc_odd.exr expected_odd.exr --diff")
# Pixels converted into a selector have no portable description.
command += oiiotool ("-i:autocc=0 odd_icc.png --colorconvert \"\" scene_linear "
                     "--colorconvert scene_linear \"" + odd_selector
                     + "\" --echo \"into a selector: {TOP.'oiio:ColorSpace'} "
                     "{TOP.'colorInteropID'}\"")
# Undecodable: the reader's label applies, exactly as before this was read.
command += oiiotool ("-i:autocc=1 unsupported_icc.png -d float "
                     "-o autocc_unsupported_icc.exr")
command += oiiotool ("unsupported_icc.png --colorconvert srgb_rec709_scene "
                     "scene_linear -d float -o expected_unsupported_icc.exr")
command += oiiotool ("--fail 0.0001 --warn 0.0001 autocc_unsupported_icc.exr "
                     "expected_unsupported_icc.exr --diff")

# An unconverted copy to OpenEXR finalizes portable source identity using the
# input container's rules. The pixels are untouched; resolving the EXR on a
# later read must therefore drive the same conversion as resolving the PNG.
command += oiiotool ("adobe_g22.png -d float -o copy_adobe_g22.exr")
command += oiiotool ("copy_adobe_g22.exr --echo \"copied numeric: "
                     "{TOP.'colorInteropID'}\"")
command += oiiotool ("adobe_g22.png --colorconvert \"\" scene_linear "
                     "-d float -o copy_adobe_expected.exr")
command += oiiotool ("copy_adobe_g22.exr --colorconvert \"\" scene_linear "
                     "-d float -o copy_adobe_actual.exr")
command += oiiotool ("--fail 0.0001 --warn 0.0001 copy_adobe_actual.exr "
                     "copy_adobe_expected.exr --diff")

# ICC evidence is container-independent. A recognized profile receives its
# portable identity, while an unmatched profile keeps its bytes but never
# publishes the process-local <synthetic>icc_ selector as a portable ID.
command += oiiotool ("srgb_icc.png -d float -o copy_srgb_icc.exr")
command += oiiotool ("copy_srgb_icc.exr --echo \"copied known ICC: "
                     "{TOP.'colorInteropID'}\"")
command += oiiotool ("odd_icc.png -d float -o copy_odd_icc.exr")
command += oiiotool ("copy_odd_icc.exr --echo \"copied unmatched ICC: "
                     "<{TOP['colorInteropID']}>\"")
command += oiiotool ("unsupported_icc.png -d float -o copy_unsupported_icc.exr")
command += oiiotool ("copy_unsupported_icc.exr --echo \"copied unsupported ICC: "
                     "<{TOP['colorInteropID']}>\"")
# OpenEXR 3.4+ carries arbitrary ICC bytes natively. The linked library was
# queried above through the CLI because this test is also registered when
# Python bindings are disabled. Both readers and the process-local conversion
# must succeed on supported builds. Portable-ID absence remains unconditional.
if openexr_version >= (3, 4, 0):
    for core in ("0", "1"):
        command += oiiotool ("--oiioattrib openexr:core " + core
                             + " copy_unsupported_icc.exr --iccwrite"
                             + " copy_unsupported_" + core + ".icc", silent=True)
        command += run_app (pythonbin + " -c \"raise SystemExit(not "
                            "__import__('filecmp').cmp('unsupported.icc', "
                            "'copy_unsupported_" + core + ".icc', False))\"")
    command += oiiotool ("odd_icc.png --colorconvert \"\" scene_linear "
                         "-d float -o copy_odd_expected.exr", silent=True)
    command += oiiotool ("copy_odd_icc.exr --colorconvert \"\" scene_linear "
                         "-d float -o copy_odd_actual.exr", silent=True)
    command += oiiotool ("--fail 0.0001 --warn 0.0001 copy_odd_actual.exr "
                         "copy_odd_expected.exr --diff", silent=True)

# Explicit identity remains authoritative, and an explicit retag clears the
# prior ICC/numeric evidence before output finalization.
command += oiiotool ("adobe_g22.png --attrib colorInteropID lin_ap1_scene "
                     "-o copy_explicit_id.exr")
command += oiiotool ("copy_explicit_id.exr --echo \"copied explicit: "
                     "{TOP.'colorInteropID'}\"")
command += oiiotool ("odd_icc.png --iscolorspace lin_ap1_scene "
                     "-o copy_retagged.exr")
command += oiiotool ("copy_retagged.exr --echo \"copied retag: "
                     "{TOP.'colorInteropID'}\"")

# A per-input OCIO context. "Plate" is whichever external curve $SHOT selects,
# so the same file read with the same command under two shots is a different
# color space and converts differently. The curves have two entries, so the
# interpolation between them is exact: each is a pure gain. Each conversion is
# checked against the same transform requested explicitly, which is the path
# that has always taken a context, and the averages are echoed so that the two
# shots are visibly not the same conversion. An input given no context is read
# in the config's own, which declares the third curve.
with open("shot_context.ocio", "w") as f:
    f.write("""ocio_profile_version: 2.3
environment: {SHOT: neutral}
search_path: .
roles: {default: Reference, scene_linear: Reference, aces_interchange: Reference}
file_rules:
  - !<Rule> {name: Default, colorspace: default}
colorspaces:
  - !<ColorSpace>
    name: Plate
    to_scene_reference: !<FileTransform> {src: shot_$SHOT.spi1d}
  - !<ColorSpace>
    name: Reference
""")
for shot, top in (("neutral", 0.25), ("match", 1.0), ("offset", 0.5)):
    with open(f"shot_{shot}.spi1d", "w") as f:
        f.write(f"Version 1\nFrom 0.0 1.0\nLength 2\nComponents 1\n"
                f"{{\n  0.0\n  {top}\n}}\n")

# The color space comes from the filename, through the same file-rule step
# every other input uses.
command += oiiotool ("--pattern constant:color=0.5 1x1 3 -d float -o in_Plate.exr")
for shot in ("match", "offset") :
    command += oiiotool (f"--colorconfig shot_context.ocio "
                         f"-i:autocc=1:key=SHOT:value={shot} in_Plate.exr "
                         f"-d float -o autocc_shot_{shot}.exr")
    command += oiiotool (f"--colorconfig shot_context.ocio -i:autocc=0 in_Plate.exr "
                         f"--colorconvert:key=SHOT:value={shot} Plate Reference "
                         f"-d float -o expected_shot_{shot}.exr")
    command += oiiotool (f"--fail 0.0001 --warn 0.0001 autocc_shot_{shot}.exr "
                         f"expected_shot_{shot}.exr --diff")
    command += oiiotool (f"--colorconfig shot_context.ocio "
                         f"-i:autocc=1:filerules=first:key=SHOT:value={shot} "
                         f"in_Plate.exr -d float "
                         f"-o autocc_rules_shot_{shot}.exr")
    command += oiiotool (f"--fail 0.0001 --warn 0.0001 "
                         f"autocc_rules_shot_{shot}.exr "
                         f"expected_shot_{shot}.exr --diff")
    command += oiiotool (f"autocc_shot_{shot}.exr --echo \"shot {shot}: {{TOP.AVGCOLOR}}\"")
    # The failover spelling itself is expanded under SOURCE, and Plate's file
    # transform is evaluated under the same SHOT override as the conversion.
    command += oiiotool (f"--colorconfig shot_context.ocio "
                         f"-i:autocc=1:failover='$SOURCE':key=SOURCE,SHOT:"
                         f"value=Plate,{shot} unsupported_cicp.png "
                         f"--echo \"resolved failover {shot}: "
                         f"{{TOP.'oiio:ColorSpace'}} "
                         f"{{TOP.'colorInteropID'}}\" -d float "
                         f"-o autocc_failover_{shot}.exr")
    command += oiiotool (f"--colorconfig shot_context.ocio "
                         f"-i:autocc=0 unsupported_cicp.png "
                         f"--colorconvert:key=SHOT:value={shot} Plate Reference "
                         f"-d float -o expected_failover_{shot}.exr")
    command += oiiotool (f"--fail 0.0001 --warn 0.0001 "
                         f"autocc_failover_{shot}.exr "
                         f"expected_failover_{shot}.exr --diff")
    command += oiiotool (f"autocc_failover_{shot}.exr --echo \"failover {shot}: "
                         f"<{{TOP['oiio:ColorSpace']}}> "
                         f"{{TOP.'colorInteropID'}}\"")
command += oiiotool ("--colorconfig shot_context.ocio -i:autocc=1 in_Plate.exr "
                     "-d float -o autocc_shot_default.exr")
command += oiiotool ("autocc_shot_default.exr --echo \"shot default: {TOP.AVGCOLOR}\"")


# The display transform's source argument is the scene end of the forward
# transform, whichever way the transform runs. Inverse, the image is the
# display end, so an empty source is `scene_linear` and the image's own label
# is not an answer to a question about the other end. The result is tagged
# with the space the transform actually started from.
_display_src = ("--pattern constant:color=.2,.5,.8 2x2 3 "
                "--iscolorspace srgb_rec709_scene ")
command += oiiotool (_display_src + "--ociodisplay:inverse=1 "
                     "\"sRGB - Display\" Un-tone-mapped "
                     "--echo \"display inverse: {TOP.'oiio:ColorSpace'} "
                     "{TOP.'colorInteropID'}\" -d float "
                     "-o display_inverse.exr")
command += oiiotool (_display_src + "--ociodisplay:inverse=1:from=scene_linear "
                     "\"sRGB - Display\" Un-tone-mapped -d float "
                     "-o display_inverse_scene.exr")
command += oiiotool ("--fail 0 --warn 0 display_inverse.exr "
                     "display_inverse_scene.exr --diff")
# Data is not color, so the display transform is not applied to it and the
# tags that say it is data survive. The average is the constant it was made
# from, exactly.
command += oiiotool ("--pattern constant:color=.25,.5,.75 2x2 3 "
                     "--iscolorspace Raw --ociodisplay \"sRGB - Display\" "
                     "Un-tone-mapped --echo \"display data: "
                     "{TOP.'oiio:ColorSpace'} id <{TOP['colorInteropID']}> "
                     "{TOP.AVGCOLOR}\"")
# A conversion that lands where it started is a no-op, and a no-op leaves
# every fact about the image where it was. The resolved implicit source is
# the destination here, which `--colorconvert` cannot see to shortcut. It is
# spelled the way the default config names it, which changed in OCIO 2.4.
srgb_name = ("sRGB Encoded Rec.709 (sRGB)" if float(ociover) >= 2.4
             else "sRGB - Texture")
command += oiiotool ("srgb_chunk_adobe.png "
                     f"--colorconvert \"\" \"{srgb_name}\" "
                     "--echo \"same space: {TOP.'oiio:ColorSpace'} "
                     "chroma <{TOP['chromaticities']}> "
                     "sRGB <{TOP['png:sRGB']}>\"")

# A claim `--autocc:missing=config` could not identify takes the facts it
# suppressed with it. Left behind, they would be written out and resolve the
# next read to exactly the encoding this read refused to assert.
command += oiiotool ("--colorconfig strict_plain.ocio "
                     "--autocc:missing=config unsupported_cicp.png "
                     "--echo \"terminal facts: {TOP.'oiio:ColorSpace'} "
                     "{TOP.'colorInteropID'} chroma <{TOP['chromaticities']}> "
                     "sRGB <{TOP['png:sRGB']}>\" "
                     "-d float -o terminal_facts.exr")
command += oiiotool ("-i:autocc=0 unsupported_cicp.png -d float "
                     "-o expected_terminal_facts.exr")
command += oiiotool ("--autocc terminal_facts.exr -d float "
                     "-o terminal_reread.exr")
command += oiiotool ("--fail 0 --warn 0 terminal_reread.exr "
                     "expected_terminal_facts.exr --diff")

# Color Interop Forum Recommendation 04: an OpenEXR file states its color
# space once. The writer derives the ID from the color space, and the PNG
# chromaticities that came in with the pixels describe a non-linear encoding,
# which OpenEXR chromaticities cannot state, so they are not written beside it.
command += oiiotool ("-i:autocc=0 srgb_chunk_adobe.png -d float "
                     "-o interop_id_only.exr")
command += oiiotool ("interop_id_only.exr --echo \"exr color claim: "
                     "{TOP.'oiio:ColorSpace'} {TOP.'colorInteropID'} chroma "
                     "<{TOP['chromaticities']}>\"")

# Chromaticities that restate the ID's own linear encoding are not a second,
# conflicting claim, so a plain OpenEXR copy keeps them, as it always has.
# Primaries that contradict the ID are dropped. An ID of "unknown" establishes
# no linear encoding, so its chromaticities are dropped too. A D60 white beside
# Rec.709 primaries is a different gamut, while a D65 white stated to five
# decimals is the same one. A label is only a derived guess: contradictory
# numeric evidence wins, and the writer records that the identity is unknown.
REC709 = "0.64,0.33,0.30,0.60,0.15,0.06,0.3127,0.3290"
REC709_D60 = "0.64,0.33,0.30,0.60,0.15,0.06,0.32168,0.33767"
REC709_D65_5DP = "0.64,0.33,0.30,0.60,0.15,0.06,0.31271,0.32902"
AP0 = "0.7347,0.2653,0,1,0.0001,-0.077,0.32168,0.33767"
for name, tag, xy in [ ("chroma_rec709", "--iscolorspace lin_rec709_scene", REC709),
                       ("chroma_d60white", "--attrib colorInteropID lin_rec709_scene", REC709_D60),
                       ("chroma_d65white", "--attrib colorInteropID lin_rec709_scene", REC709_D65_5DP),
                       ("chroma_ap0", "--attrib colorInteropID lin_rec709_scene", AP0),
                       ("chroma_unknown", "--attrib colorInteropID unknown", REC709),
                       ("chroma_label_ap0", "--iscolorspace lin_rec709_scene", AP0) ] :
    command += oiiotool ("--pattern constant:color=.25,.3,.4 2x2 3 " + tag
                         + " \"--attrib:type=float[8]\" chromaticities " + xy
                         + " -d half -o " + name + ".exr")
command += oiiotool ("chroma_rec709.exr -o chroma_rec709_copy.exr")
command += run_app (oiio_app("iconvert") + "chroma_rec709.exr chroma_rec709_iconvert.exr")
command += oiiotool ("chroma_label_ap0.exr -o chroma_label_ap0_copy.exr")
# The same rule for a raw ImageInput-to-ImageOutput copy (iconvert) of a PNG
# whose reader label agrees with its gAMA and Rec.709 cHRM.
_write_png("g22_rec709_chrm.png", (64, 191, 128),
           [(b"gAMA", struct.pack(">I", 45455)),
            (b"cHRM", struct.pack(">8I", 31270, 32900, 64000, 33000,
                                  30000, 60000, 15000, 6000))])
command += run_app (oiio_app("iconvert") + "g22_rec709_chrm.png g22_rec709_chrm.exr")
command += oiiotool ("g22_rec709_chrm.exr --echo \"g22 rec709 raw copy: "
                     "<{TOP['colorInteropID']}> chroma <{TOP['chromaticities']}>\"")

# A reader's label does not override the image state that its own evidence
# states by convention. A PNG's gAMA and cHRM are display-referred, so a copy
# of this file is g22_rec709_display although the reader labeled it
# g22_rec709_scene. An application that means the scene-referred reading
# asserts it, and set_colorspace records that reading, so the assertion is
# what the writer sees.
command += oiiotool ("g22_rec709_chrm.png -d float -o png_state_copy.exr")
command += oiiotool ("png_state_copy.exr --echo \"PNG numerics copied: "
                     "<{TOP['colorInteropID']}>\"")
command += oiiotool ("g22_rec709_chrm.png --iscolorspace g22_rec709_scene "
                     "-d float -o png_state_asserted.exr")
command += oiiotool ("png_state_asserted.exr --echo \"PNG numerics asserted: "
                     "<{TOP['colorInteropID']}>\"")

# A per-input OCIO context is in scope only while that input is read, so a
# portable identity established under it is recorded then. An input given no
# context key has nothing input-local to preserve and gains nothing on read.
command += oiiotool ("-i:autocc=0:key=SHOT:value=match srgb_icc.png "
                     "--echo \"input-context id: <{TOP['colorInteropID']}>\"")
command += oiiotool ("-i:autocc=0 srgb_icc.png "
                     "--echo \"no input-context id: <{TOP['colorInteropID']}>\"")

# A colorInteropID of "unknown" says the writer could not identify the pixels,
# which is evidence rather than a statement about them: the rest of the
# metadata is still tried. Here an unmatched but decodable ICC profile
# describes them, and the conversion synthesizes an endpoint from it, so the
# result matches the same conversion of the untagged file.
command += oiiotool ("odd_icc.png --attrib colorInteropID unknown "
                     "--colorconvert \"\" scene_linear -d float "
                     "-o unknown_icc_actual.exr")
command += oiiotool ("odd_icc.png --colorconvert \"\" scene_linear -d float "
                     "-o unknown_icc_expected.exr")
command += oiiotool ("--fail 0.0001 --warn 0.0001 unknown_icc_actual.exr "
                     "unknown_icc_expected.exr --diff")
for name in [ "chroma_rec709", "chroma_rec709_copy", "chroma_rec709_iconvert",
              "chroma_d60white", "chroma_d65white", "chroma_ap0",
              "chroma_unknown", "chroma_label_ap0", "chroma_label_ap0_copy" ] :
    command += oiiotool (name + ".exr --echo \"" + name + ": "
                         "<{TOP['colorInteropID']}> chroma <{TOP['chromaticities']}>\"")

# --iscolorspace calls set_colorspace, which makes the name authoritative:
# the image's other color metadata is kept where it agrees, rewritten where
# the name determines it and removed otherwise, even when the name is already
# the label. Mastering display metadata is not touched.
command += oiiotool ("--pattern constant:color=.25,.3,.4 2x2 3 "
                     "\"--attrib:type=float[8]\" chromaticities " + AP0 + " "
                     "--attrib colorInteropID lin_ap0_scene "
                     "--attrib acesImageContainerFlag 1 "
                     "--attrib mdcv_max_luminance 1000.0 "
                     "--iscolorspace lin_rec709_scene -d half -o assert_ap0.exr")
command += oiiotool ("assert_ap0.exr --echo \"assert over AP0: "
                     "{TOP.'colorInteropID'} chroma <{TOP['chromaticities']}> "
                     "aces <{TOP['acesImageContainerFlag']}> "
                     "mdcv <{TOP['mdcv_max_luminance']}>\"")
# Both OpenEXR readers label an ACES container flag lin_ap0_scene and keep the
# header's chromaticities, whether or not the two agree. The resolver ranks the
# flag above chromaticities, so a conversion uses AP0 either way. The writer
# refuses to write such a flag, so explicit gamma 1 preserves the linear
# chromaticities under a same-length placeholder that is renamed in the header.
for name, xy in [ ("aces_flag_ap0", AP0), ("aces_flag_rec709", REC709) ] :
    command += oiiotool ("--pattern constant:color=.25,.3,.4 2x2 3 "
                         "\"--attrib:type=float[8]\" chromaticities " + xy + " "
                         "--attrib oiio:Gamma 1 "
                         "--attrib acesImageContainerFlaX 1 -d half -o "
                         + name + ".exr")
    command += run_app (pythonbin + " src/rename-exr-attrib.py " + name
                        + ".exr acesImageContainerFlaX acesImageContainerFlag")
    for core in [ "0", "1" ] :
        command += oiiotool ("-oiioattrib openexr:core " + core + " " + name
                             + ".exr --echo \"" + name + " core " + core + ": "
                             "{TOP.'oiio:ColorSpace'} "
                             "aces <{TOP['acesImageContainerFlag']}> "
                             "chroma <{TOP['chromaticities']}>\"")
command += oiiotool ("--pattern constant:color=.25,.3,.4 2x2 3 "
                     "--iscolorspace lin_rec709_scene "
                     "\"--attrib:type=float[8]\" chromaticities " + AP0 + " "
                     "--iscolorspace lin_rec709_scene --echo \"assert twice: "
                     "{TOP.'oiio:ColorSpace'} chroma <{TOP['chromaticities']}>\"")
command += oiiotool ("--pattern constant:color=.25,.3,.4 2x2 3 "
                     "\"--attrib:type=int[4]\" CICP 9,16,0,1 "
                     "--attrib ICCProfile:profile_version 4.3.0 "
                     "--iscolorspace lin_rec709_scene --echo \"assert over CICP: "
                     "CICP <{TOP['CICP']}> icc <{TOP['ICCProfile:profile_version']}>\" "
                     "--iscolorspace g22_rec709_display --echo \"assert without "
                     "a code: CICP <{TOP['CICP']}>\"")
# A conversion removes the source's color metadata before tagging the
# destination, so the source CICP does not survive to be rewritten.
command += oiiotool ("--pattern constant:color=.25,.3,.4 2x2 3 "
                     "\"--attrib:type=int[4]\" CICP 9,16,0,1 "
                     "--attrib ICCProfile:profile_version 4.3.0 "
                     "--iscolorspace lin_rec709_scene "
                     "\"--attrib:type=int[4]\" CICP 9,16,0,1 "
                     "--attrib ICCProfile:profile_version 4.3.0 "
                     "--colorconvert lin_rec709_scene srgb_rec709_scene "
                     "--echo \"converted: CICP <{TOP['CICP']}> "
                     "icc <{TOP['ICCProfile:profile_version']}>\"")
# A PNG reader records its gAMA and cHRM as they are, and a cHRM stating
# primaries other than Rec.709 leaves the file unlabeled, so a conversion
# resolves from the two chunks together, which is what the --autocc rows
# above check.
_write_png("g22_adobe_chrm.png", (64, 191, 128),
           [(b"gAMA", struct.pack(">I", 45455)), _adobe[1]])
command += oiiotool ("g22_adobe_chrm.png --echo \"g22 adobe read: "
                     "<{TOP['oiio:ColorSpace']}> chroma <{TOP['chromaticities']}>\" "
                     "--iscolorspace g22_adobergb_scene -d half -o g22_adobe_chrm.exr")
command += oiiotool ("g22_adobe_chrm.exr --echo \"g22 adobe asserted: "
                     "{TOP.'colorInteropID'}\"")
command += oiiotool ("adobe_g22.png --echo \"adobe_g22 read: "
                     "<{TOP['oiio:ColorSpace']}>\"")

# A file's colorInteropID of "unknown", and an ID this config cannot use, are
# evidence that the writer could not identify the pixels, not linear-encoding
# claims. The writer therefore drops bare chromaticities beside either ID. The
# readers leave the label unset and keep the ID, so the image ends as "unknown"
# and automatic conversion leaves it alone.
for idname in [ "unknown", "notaspace" ] :
    command += oiiotool ("--pattern constant:color=.25,.3,.4 2x2 3 "
                         "\"--attrib:type=float[8]\" chromaticities " + REC709 + " "
                         "--attrib colorInteropID " + idname + " -d half -o "
                         "evidence_" + idname + ".exr")
    for core in [ "0", "1" ] :
        command += oiiotool ("-oiioattrib openexr:core " + core
                             + " evidence_" + idname + ".exr --echo \""
                             + idname + " core " + core + " read: "
                             "<{TOP['oiio:ColorSpace']}> id {TOP.'colorInteropID'}\"")
    command += oiiotool ("--autocc evidence_" + idname + ".exr -d float -o "
                         "autocc_" + idname + ".exr")
    command += oiiotool ("evidence_" + idname + ".exr -d float -o "
                         "expected_" + idname + ".exr")
    command += oiiotool ("--fail 0.0001 --warn 0.0001 autocc_" + idname
                         + ".exr expected_" + idname + ".exr --diff")
    command += oiiotool ("--pattern constant:color=.25,.3,.4 2x2 3 "
                         "--attrib colorInteropID " + idname + " -d half -o "
                         "bare_" + idname + ".exr")
    command += oiiotool ("--autocc bare_" + idname + ".exr --echo \""
                         + idname + " bare autocc: {TOP.'oiio:ColorSpace'} "
                         "id {TOP.'colorInteropID'}\"")

# To add more tests, just append more lines like the above and also add
# the new 'feature.tif' (or whatever you call it) to the outputs list,
# below.


# Outputs to check against references
outputs = [
            "colormap-inferno.tif", "colormap-custom.tif",
            "unpremult.exr", "premult.exr",
            "contrast-stretch.tif",
            "contrast-shrink.tif",
            "contrast-inverse.tif",
            "contrast-threshold.tif",
            "contrast-sigmoid5.tif",
            "display-sRGB.tif",
            "rgbfromtga.png",
            "greyalpha_sRGB.tif",
            "greyalpha_sRGB_un.tif",
            "grey_sRGB.tif",
            "grey_sRGB_un.tif",
            "tahoe-ccmatrix.tif",
            "tahoe-sat0.tif",
            "tahoe-sat2.tif"
    ]
for c in colormaps :
    outputs += [ "cmap-" + c + ".tif" ]
outputs += [ "look-default.tif" ]
outputs += [ "out.txt" ]
