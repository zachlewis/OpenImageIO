#!/usr/bin/env python

# Copyright Contributors to the OpenImageIO project.
# SPDX-License-Identifier: Apache-2.0
# https://github.com/AcademySoftwareFoundation/OpenImageIO

# oiiotool with color evidence other than a color space name: embedded ICC
# profiles and numeric metadata (gamma, chromaticities), both for conversion
# and for the identity that an OpenEXR copy records.

import hashlib
import re
import struct
import zlib

redirect = " >> out.txt 2>&1 "

# Same query runtest.py makes for the OpenColorIO version.
library_list = subprocess.check_output(
    [oiio_app("oiiotool").strip(), "--echo", "{getattribute(library_list)}"],
    text=True)
openexr_match = re.search(r"(?:^|;)openexr:OpenEXR (\d+)\.(\d+)\.(\d+)",
                          library_list)
assert openexr_match, "linked OpenEXR version absent from library_list"
openexr_version = tuple(map(int, openexr_match.groups()))

# Fixtures are synthesized so that their chunks are exact. A saturated
# pixel makes the gamut, not just the exponent, visible.
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
_write_png("gamma_only_srgb_tx.png", (64, 191, 128),
           [(b"gAMA", struct.pack(">I", 45455))])

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

outputs = [ "out.txt" ]
