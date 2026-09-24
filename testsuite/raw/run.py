#!/usr/bin/env python

# Copyright Contributors to the OpenImageIO project.
# SPDX-License-Identifier: Apache-2.0
# https://github.com/AcademySoftwareFoundation/OpenImageIO


import os
import shutil

files = [ "RAW_CANON_EOS_7D.CR2",
          "RAW_NIKON_D3X.NEF",
          "RAW_FUJI_F700.RAF",
          "RAW_NIKON_D3X.NEF",
          "RAW_OLYMPUS_E3.ORF",
          "RAW_PANASONIC_G1.RW2",
          "RAW_PENTAX_K200D.PEF",
          "RAW_SONY_A300.ARW" ]
outputs = []

# Things vary a lot with libraw versions.
# FIXME -- return to this later
if (os.getenv('GITHUB_ACTIONS') == 'true'):
    failthresh = 0.024
    files.remove ("RAW_PANASONIC_G1.RW2")

# Fairly high hard fail, since libraw seems to diddle with its debayering
# from version to version, it's hard to make a single reference image.
hardfail = 0.017

# For each test image, read it and print all metadata, resize it (to make
# the ref images small) and compared to the reference.
for f in files:
    outputname = f+".tif"
    command += oiiotool ("-iconfig raw:ColorSpace lin_rec709_scene "
                         + "-i:info=2 " + OIIO_TESTSUITE_IMAGEDIR + "/" + f
                         + " -resample '5%' -d uint8 "
                         + "-o " + outputname)
    outputs += [ outputname ]

# Undebayered reads must present the sensor data in the orientation the
# spec advertises, for every one of LibRaw's 8 flip codes -- four of them
# used to leave the caller's buffer untouched, and a fifth was mirrored.
# Compare against the unflipped read put through the equivalent oiiotool
# transform, so the check doesn't depend on the LibRaw version.
flipsrc = OIIO_TESTSUITE_IMAGEDIR + "/RAW_SONY_A300.ARW"
fliptransform = [ (1, "--flop"), (2, "--flip"), (3, "--flip --flop"),
                  (4, "--transpose"), (5, "--transpose --flip"),
                  (6, "--transpose --flop"), (7, "--transpose --flip --flop") ]
undebayer = "-iconfig raw:Demosaic none -iconfig raw:user_flip "
corner = " --cut 64x64+0+0 -d uint16 -o "
cmd = undebayer + "0 -i " + flipsrc
for flip, transform in fliptransform :
    cmd += " --dup " + transform + corner + "flipref%d.tif --pop" % flip
command += oiiotool (cmd)
for flip, transform in fliptransform :
    command += oiiotool (undebayer + "%d -i %s%sflip%d.tif"
                         % (flip, flipsrc, corner, flip))
    command += oiiotool ("--diff flipref%d.tif flip%d.tif" % (flip, flip))

# Malformed and hostile headers must be rejected cleanly, and the valid
# control must still read. Read the pixels undebayered so the expected
# output doesn't move with LibRaw's demosaicing.
redirect = " >> out.txt 2>&1"
for f in [ "valid-32x32.dng", "bad-exif-type.dng", "truncated.dng",
           "bomb-32000x32000.dng" ] :
    command += oiiotool ("--info src/" + f, failureok = True)
    command += oiiotool ("-iconfig raw:Demosaic none --stats src/" + f,
                         failureok = True)

# Check the crop size and position in all 4 orientations.
for rotation in [0, 90, 180, 270]:
    command += oiiotool (
        "--iconfig raw:Demosaic none " +
        "-i src/" + "crop-36x32_" + str(rotation) + ".dng " +
        "--echo \"________________\" " +
        "--echo \"ROTATION " + str(rotation) + " info:\" " +
        "--eraseattrib \".*\" --printinfo " +
        "--echo \" Full stats:\" --printstats " +
        "--croptofull " +
        "--echo \"Crop stats:\" --printstats")

outputs += [ "out.txt" ]

# raw:ColorSpace names a color interop ID. The deprecated decoder names keep
# their decode and are tagged with the ID of that decode; Wide has none.
# "raw", a camera LibRaw has no color matrix for (valid-32x32.dng), a
# monochrome sensor, which has one and is decoded camera-native anyway
# (mono-32x32.dng), and an undemosaiced read are camera-native whatever is
# requested: they carry colorInteropID "unknown" instead of a tag. Wide is
# untagged too, but it is not camera-native: it is the only decode that
# describes itself, with chromaticities and a gamma. No untagged decode may
# keep the camera's Exif:ColorSpace. Only stdout is kept.
ids = [ "srgb_rec709_scene", "lin_rec709_scene", "g24_rec709_scene",
        "g22_rec709_scene", "g18_rec709_scene", "ocio:itu709_rec709_scene",
        "lin_adobergb_scene", "g22_adobergb_scene", "oiio:lin_prophoto_scene",
        "oiio:g18_prophoto_scene", "lin_ciexyzd65_scene", "lin_ap0_scene",
        "lin_p3d65_scene", "srgb_p3d65_scene", "lin_rec2020_scene" ]
deprecated = [ "sRGB", "sRGB-linear", "linear", "lin_srgb", "lin_rec709",
               "Adobe", "Wide", "ProPhoto", "ProPhoto-linear", "XYZ", "ACES",
               "DCI-P3", "Rec2020" ]
outputs += [ "colorspace.txt" ]
tests = [ (flipsrc, cs) for cs in ids + deprecated + [ "srgb_texture", "raw" ] ]
tests += [ ("src/valid-32x32.dng", "ACES"), ("src/mono-32x32.dng", "ACES") ]
echo = (" -> [{TOP['oiio:ColorSpace']}]"
        + " colorInteropID [{TOP['colorInteropID']}]"
        + " Exif:ColorSpace [{TOP['Exif:ColorSpace']}]"
        + " chromaticities [{TOP['chromaticities']}]"
        + " gamma [{TOP['oiio:Gamma']}]\" --pop")
cmd = ""
for f, cs in tests :
    cmd += (" --iconfig raw:ColorSpace " + cs + " " + f + " --echo \""
            + os.path.basename(f) + " " + cs + echo)
# An undemosaiced read is camera-native however it was asked for, including
# under the default request, which would otherwise keep Exif:ColorSpace, and
# under Wide, whose description it must not keep either.
cmd += (" --iconfig raw:Demosaic none --iconfig raw:ColorSpace"
        + " srgb_rec709_scene " + flipsrc + " --echo \""
        + os.path.basename(flipsrc) + " srgb_rec709_scene+undemosaiced" + echo)
cmd += (" --iconfig raw:Demosaic none --iconfig raw:ColorSpace Wide "
        + flipsrc + " --echo \"" + os.path.basename(flipsrc)
        + " Wide+undemosaiced" + echo)
command += oiiotool("-n" + cmd + " > colorspace.txt", silent=True)

# A PNG written from a camera-native decode claims no color, and one written
# from Wide carries its chromaticities and linear gamma.
command += run_app(pythonbin + ' src/test-raw-png.py "'
                   + oiio_app("oiiotool").strip() + '" ' + flipsrc, silent=True)

# Each deprecated name warns once per process. The warning is debug output,
# so turn that on for this command only, and keep only the warning lines.
outputs += [ "warnings.txt" ]
command += oiiotool("--oiioattrib debug 1 -n"
                    + " --iconfig raw:ColorSpace Adobe " + flipsrc
                    + " --iconfig raw:ColorSpace adobe " + flipsrc
                    + " --iconfig raw:ColorSpace Wide " + flipsrc
                    + " 2>&1 | grep raw:ColorSpace > warnings.txt", silent=True)

# Every decode is what its table row says. Taken to lin_ap0_scene through its
# tag, it matches LibRaw's ACES decode of the same pixels taken to the tag,
# clipped as LibRaw clips, and back. Half-size decodes skip demosaicing but
# not the color conversion.
#
# The tolerances are what the identities' own matrices leave, so they do not
# depend on which LibRaw is installed. For a decode to X, the round trip
# multiplies by M_id(X->AP0) . M_LibRaw(AP0->X), which is the identity only as
# far as LibRaw's published primaries agree with the ID's. Over the rows with
# an ID the largest row sum of that residual is 3.7e-4 (rec709), so with
# pixels clipped to [0,1] and LibRaw's 16-bit output quantization (1.6e-5),
# 4e-4 bounds the error. Wide Gamut RGB has no ID, so its matrix is built
# from the chromaticities the reader attaches to that decode and AP0's,
# Bradford-adapted between their whites. That residual is 8.7e-5, so 2e-4
# bounds that row, and the tighter bound is what rejects describing the
# decode as unadapted Wide Gamut RGB with its D50 white, which is wrong by
# 3.8e-4 here.
# The residuals are the same for every LibRaw version upstream CI builds: its
# output matrices (src/tables/colorconst.cpp) and gamma_curve() are unchanged
# in 0.21.0, 0.22.0, 0.22.2 and master.
half = " --iconfig raw:half_size 1 --iconfig raw:ColorSpace "
crop = " --crop 256x256+992+384 --origin +0+0 -d float"
ap0_to_wide = ("1.38352255,-0.11731209,-0.26621046,0.00649934,1.03981713,"
               "-0.04631646,-0.00394319,-0.05898216,1.06292535")
# One command per row keeps each command line within cmd.exe's 8191
# characters.
aces = "-n" + half + "lin_ap0_scene " + flipsrc + crop + " --label aces"
for cs in ids + deprecated :
    cmd = aces + " --echo " + cs + half + cs + " " + flipsrc + crop + " aces"
    if cs == "Wide" :
        cmd += (" --ccmatrix:transpose=1 " + ap0_to_wide
                + " --clamp:min=0:max=1 --fail 0.0002")
    else :
        cmd += (" --tocolorspace \"{IMG[1].'oiio:ColorSpace'}\""
                + " --clamp:min=0:max=1 --tocolorspace lin_ap0_scene --swap"
                + " --tocolorspace lin_ap0_scene --fail 0.0004")
    command += oiiotool(cmd + " --diff", silent=True)

# The deprecated "Adobe" keeps its 2.2 exponent, and g22_adobergb_scene has
# 563/256. Both sides of this one are the same decode matrix, so only the
# curve differs and the tolerance can be tight.
cmd = "-n --fail 0.00005"
for cs, exponent in [ ("Adobe", "2.2"), ("g22_adobergb_scene", "2.19921875") ] :
    cmd += (" --echo " + cs + half + cs + " " + flipsrc + crop + " --powc "
            + exponent + half + "lin_adobergb_scene " + flipsrc + crop
            + " --diff --pop --pop")
command += oiiotool(cmd, silent=True)

# --autocc decodes RAW as lin_ap0_scene when raw:ColorSpace is not set. The
# copy's name avoids "raw", which --autocc would take as its color space, and
# its extension is uppercase, as camera files' usually are.
shutil.copyfile(flipsrc, "autocc.ARW")
command += oiiotool("--iconfig raw:half_size 1 --autocc autocc.ARW" + crop
                    + " -o autocc.exr", silent=True)
command += oiiotool(half + "lin_ap0_scene autocc.ARW"
                    + " --colorconvert lin_ap0_scene scene_linear" + crop
                    + " -o autocc-ref.exr", silent=True)
command += oiiotool("autocc.exr autocc-ref.exr --fail 0 --diff", silent=True)

# An explicit raw:ColorSpace wins over that default: --autocc converts from
# it, rather than overwriting it with lin_ap0_scene.
command += oiiotool(half + "g22_adobergb_scene --autocc autocc.ARW" + crop
                    + " -o autocc-explicit.exr", silent=True)
command += oiiotool(half + "g22_adobergb_scene autocc.ARW"
                    + " --colorconvert g22_adobergb_scene scene_linear" + crop
                    + " -o autocc-explicit-ref.exr", silent=True)
command += oiiotool("autocc-explicit.exr autocc-explicit-ref.exr --fail 0 --diff",
                    silent=True)
