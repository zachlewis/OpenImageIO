#!/usr/bin/env python

# Copyright Contributors to the OpenImageIO project.
# SPDX-License-Identifier: Apache-2.0
# https://github.com/AcademySoftwareFoundation/OpenImageIO

redirect = " >> out.txt 2>&1 "

files = [ "dpx_nuke_10bits_rgb.dpx", "dpx_nuke_16bits_rgba.dpx" ]
for f in files:
    command += rw_command (OIIO_TESTSUITE_IMAGEDIR, f)


# Additionally, test for regressions for endian issues with 16 bit DPX output
# (related to issue #354)
command += oiio_app("oiiotool") + " src/input_rgb_mattes.tif -o output_rgb_mattes.dpx >> out.txt;"
command += oiio_app("idiff") + " src/input_rgb_mattes.tif output_rgb_mattes.dpx >> out.txt;"

# Test reading and writing of stereo DPX (multi-image)
#command += (oiio_app("oiiotool") + "--create 80x60 3 --text:x=10 Left "
#            + "--caption \"view angle: left\" -d uint10 -o L.dpx >> out.txt;")
#command += (oiio_app("oiiotool") + "--create 80x60 3 --text:x=10 Right "
#            + "--caption \"view angle: right\" -d uint10 -o R.dpx >> out.txt;")
command += (oiio_app("oiiotool") + "ref/L.dpx ref/R.dpx --siappend -o stereo.dpx >> out.txt;")
command += info_command("stereo.dpx", safematch=True, hash=False, extraargs="--stats")
command += oiio_app("idiff") + "-a stereo.dpx ref/stereo.dpx >> out.txt;"

# Test read/write of 1-channel DPX -- take a color image, make it grey,
# write it as 1-channel DPX, then read it again and compare to a reference.
# The reference is stored as TIFF rather than DPX just because it has
# fantastically better compression.
command += oiiotool(OIIO_TESTSUITE_IMAGEDIR+"/dpx_nuke_16bits_rgba.dpx"
                    " -chsum:weight=0.333,0.333,0.333 -chnames Y -ch Y -o grey.dpx")
command += info_command("grey.dpx", safematch=True)
command += diff_command("grey.dpx", "ref/grey.tif")

# DPX has no primaries field: a linear encoding writes a Linear transfer, and
# the file reads back unclaimed, whatever its primaries were
command += oiiotool("--create 4x4 3 -d uint16 --iscolorspace lin_ap0_scene -o lin_ap0.dpx")
command += oiiotool("lin_ap0.dpx --echo \"lin_ap0.dpx: {TOP.'dpx:Transfer'} {TOP.'colorInteropID'}\"")
# A PNG copy claims no more than the DPX did: no color chunk, so it reads back
# "unknown" with no label and no gamma, not as sRGB
command += oiiotool("lin_ap0.dpx -d uint8 -o lin_ap0.png")
command += oiiotool("lin_ap0.png --echo \"lin_ap0.png: <{TOP['oiio:ColorSpace']}> {TOP['colorInteropID']} <{TOP['oiio:Gamma']}> <{TOP['png:sRGB']}>\"")
# A built-in identity the config does not define is described by the built-in
# interop-identities config: a linear one writes Linear, gamma 2.2 does not
for cs in [ "oiio:lin_p3dci_display", "g22_adobergb_display" ] :
    f = cs.replace(":", "-") + ".dpx"
    command += oiiotool("--create 4x4 3 -d uint16 --iscolorspace " + cs + " -o " + f)
    command += oiiotool(f + " --echo \"" + f + ": {TOP.'dpx:Transfer'}\"")

# The reader identifies nothing, but that does not stop resolution: a color
# space named in the filename still resolves the image under --autocc, which
# converts it. With nothing to go on the answer is "unknown", whatever the
# configuration's own default assignment says.
command += oiiotool("lin_ap0.dpx -o plate_ACEScct_v01.dpx")
command += oiiotool("--autocc plate_ACEScct_v01.dpx --echo \"plate_ACEScct_v01.dpx: {TOP.'oiio:ColorSpace'} {TOP.'colorInteropID'}\"")
command += oiiotool("--autocc grey.dpx --echo \"grey.dpx: {TOP.'oiio:ColorSpace'} {TOP.'colorInteropID'}\"")

# Regression tests
command += oiiotool("src/crash-badusersize.dpx -o test.tif", failureok=True)

# Regression test: crafted DPX with a 1-channel, 10-bit, "Filled method A"
# packed subimage whose width (80) is not a multiple of 3. The 1-channel
# work-around in Read10bitFilled() swapped the first and third datum of
# each group of 3 packed samples, but did not check that a full group of 3
# remained for the last (partial) group in the scanline, writing/reading
# one uint16 past the end of the caller's scanline buffer.
command += info_command("src/crash-1chan-10bit-filled-methodA.dpx", safematch=True)

# Regression test: the "dpx:EndOfLinePadding" attribute is carried over from
# an input file's header, but the libdpx writer uses it as the row stride of
# the caller's pixel buffer, which is always tightly packed. A nonzero value
# therefore read past the end of the buffer. The writer must ignore it.
command += oiiotool("--create 80x60 3 -d uint10 "
                    "--attrib:type=int dpx:EndOfLinePadding 16384 "
                    "--attrib:type=int dpx:EndOfImagePadding 16384 "
                    "-o eolpad.dpx")
command += info_command("eolpad.dpx", safematch=True)

# Regression test: a 21 KB DPX whose header declares a ~12 GB image
# (46341x46341x3) -- a decompression bomb the compression-ratio guard must
# reject before any large allocation.
command += info_command("src/bomb-46341.dpx", safematch=True, failureok=True)

# Regression test: a 2100-byte DPX declaring 2000 bytes of user data. That
# size alone fits the file, so the old check passed it, but the block starts
# at offset 2048 and only 52 bytes remain. The short read was ignored and the
# uninitialized tail of the buffer went out as the dpx:UserData attribute.
# The bounds check now counts the offset and rejects the file up front.
command += info_command("src/truncated-userdata.dpx", safematch=True,
                        failureok=True)

# Color: the reader identifies no color space, so every reread states
# "unknown". The writer writes the codes a color space establishes, and with
# none (or "unknown") copies the codes it was given.
outputs += ["color.txt"]
def color_roundtrip(name, args, source="--pattern constant:color=0,1,0 1x1 3",
                    redirect=">>"):
    return run_app(
        oiio_app("oiiotool") + f" {source} {args} -d uint16 -o {name}.dpx"
        + f" --pop {name}.dpx"
        + f' --echo "{name}: {{TOP[\'dpx:Transfer\']}}, {{TOP[\'dpx:Colorimetric\']}} [{{TOP[\'colorInteropID\']}}]"'
        + f" {redirect} color.txt 2>&1", silent=True)
command += color_roundtrip("video", "--attrib oiio:ColorSpace g24_rec709_display",
                           redirect=">")
command += color_roundtrip("srgb", "--attrib oiio:ColorSpace srgb_rec709_scene")
# Nor does a Rec.709 gamma write User defined, or any transfer
command += color_roundtrip("gamma22", "--attrib oiio:ColorSpace g22_rec709_scene")
command += color_roundtrip("linear", "--attrib oiio:ColorSpace lin_rec709_scene")
# A linear identity the config does not define is still linear
command += color_roundtrip("linear_display",
                           "--attrib oiio:ColorSpace lin_rec709_display")
# The transfer comes from the same output-gamma lookup the other writers use,
# so a deprecated "Gamma 1.0" name is still linear
command += color_roundtrip("legacy_gamma", '--attrib oiio:ColorSpace "Gamma 1.0"')
command += color_roundtrip("log", "--attrib oiio:ColorSpace ACEScct")
command += color_roundtrip("density", '--attrib dpx:Transfer "Printing density"'
                           + ' --attrib dpx:Colorimetric "Printing density"')
# A claimed image whose CICP attribute carries Rec.709 primaries and transfer
# writes ITU-R 709-4, whatever color space it is labeled with.
command += color_roundtrip("cicp_709",
                           "--attrib oiio:ColorSpace g24_rec709_scene"
                           + " --cicp 1,1")
# An unclaimed image still copies its codes even when it carries a CICP
# attribute: nothing has established that the attribute describes these pixels.
command += color_roundtrip("unknown_cicp",
                           '--attrib oiio:ColorSpace unknown --cicp 1,1'
                           + ' --attrib dpx:Transfer "Printing density"'
                           + ' --attrib dpx:Colorimetric "Printing density"')
# A DPX copy keeps its codes; a conversion does not carry them.
command += color_roundtrip("copy", "", source="density.dpx")
command += color_roundtrip("converted", "--colorconvert lin_rec709_scene ACEScct",
                           source="density.dpx")
# --iscolorspace on a DPX that identifies nothing: a color space it assigns
# decides the codes written (a contradicting CICP attribute does not survive
# it), and re-asserting "unknown" leaves the DPX codes for the copy (the CICP
# attribute gives it color metadata to remove).
command += color_roundtrip("assigned", "--cicp 1,1 --iscolorspace ACEScct",
                           source="density.dpx")
command += color_roundtrip("reasserted", "--cicp 1,1 --iscolorspace unknown",
                           source="density.dpx")

# A DPX part states "unknown", which a strict multi-part OpenEXR write treats
# as a missing ID, so it is accepted after a known first part. Each part reads
# back as it was written.
command += oiiotool("--create 4x4 3 --iscolorspace lin_ap1_scene "
                    "-sattrib openexr:ColorInteropIDPolicy strict lin_ap0.dpx "
                    "--siappendall -d half -o dpx_parts.exr")
for part in (0, 1):
    command += oiiotool(f"dpx_parts.exr --subimage {part} --echo "
                        f"\"dpx_parts.exr part {part}: "
                        "{TOP['colorInteropID']} <{TOP['oiio:ColorSpace']}>\"")
