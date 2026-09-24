#!/usr/bin/env python

# Copyright Contributors to the OpenImageIO project.
# SPDX-License-Identifier: Apache-2.0
# https://github.com/AcademySoftwareFoundation/OpenImageIO


redirect = ' >> out.txt 2>&1 '

# Test handling of colorInteropID in multi-part files.

# Parts: "lin_ap1_scene", missing, "data", missing
command += oiiotool("--pattern constant:color=1,0,0 4x4 3 -d half "
                    "--attrib oiio:ColorSpace lin_ap1_scene --attrib oiio:subimagename beauty "
                    "--pattern constant:color=0,1,0 4x4 3 -d half "
                    "--eraseattrib oiio:ColorSpace --attrib oiio:subimagename diffuse "
                    "--pattern constant:color=0.25,0.25,0.25 4x4 3 -d half "
                    "--attrib oiio:ColorSpace data --attrib oiio:subimagename depth "
                    "--pattern constant:color=0,0,1 4x4 3 -d half "
                    "--eraseattrib oiio:ColorSpace --attrib oiio:subimagename specular "
                    "--siappendall -o copy_from_first.exr")
command += info_command("copy_from_first.exr",
                        extraargs="-oiioattrib openexr:core 0", safematch=True)
command += info_command("copy_from_first.exr",
                        extraargs="-oiioattrib openexr:core 1", safematch=True)

# Parts: missing, "data", missing
command += oiiotool("--pattern constant:color=1,0,0 4x4 3 -d half "
                    "--eraseattrib oiio:ColorSpace --attrib oiio:subimagename beauty "
                    "--pattern constant:color=0.25,0.25,0.25 4x4 3 -d half "
                    "--attrib oiio:ColorSpace data --attrib oiio:subimagename depth "
                    "--pattern constant:color=0,0,1 4x4 3 -d half "
                    "--eraseattrib oiio:ColorSpace --attrib oiio:subimagename specular "
                    "--siappendall -o missing_first.exr")
command += info_command("missing_first.exr",
                        extraargs="-oiioattrib openexr:core 0", safematch=True)
command += info_command("missing_first.exr",
                        extraargs="-oiioattrib openexr:core 1", safematch=True)

# Parts: "data", missing, "lin_ap1_scene", missing
# Not valid according to the CIF recommendation, but can be read anyway.
command += info_command("src/multipart_colorspace_data_first.exr",
                        extraargs="-oiioattrib openexr:core 0", safematch=True)
command += info_command("src/multipart_colorspace_data_first.exr",
                        extraargs="-oiioattrib openexr:core 1", safematch=True)

# Parts: "lin_ap1_scene", "lin_ap1_scene"
command += oiiotool("--pattern constant:color=1,0,0 4x4 3 -d half "
                    "--attrib oiio:ColorSpace lin_ap1_scene --attrib oiio:subimagename beauty "
                    "-sattrib openexr:ColorInteropIDPolicy strict "
                    "--pattern constant:color=0,1,0 4x4 3 -d half "
                    "--attrib oiio:ColorSpace lin_ap1_scene --attrib oiio:subimagename specular "
                    "--siappendall -o matched.exr")

# Parts: "lin_ap1_scene", "lin_rec709_scene"
# Not valid according to the CIF recommendation, error with strict policy.
command += oiiotool("--pattern constant:color=1,0,0 4x4 3 -d half "
                    "--attrib oiio:ColorSpace lin_ap1_scene --attrib oiio:subimagename beauty "
                    "-sattrib openexr:ColorInteropIDPolicy strict "
                    "--pattern constant:color=0,1,0 4x4 3 -d half "
                    "--attrib oiio:ColorSpace lin_rec709_scene --attrib oiio:subimagename specular "
                    "--siappendall -o mismatched.exr", failureok=True)

# Parts: "lin_ap1_scene", missing
command += oiiotool("--pattern constant:color=1,0,0 4x4 3 -d half "
                    "--attrib oiio:ColorSpace lin_ap1_scene --attrib oiio:subimagename beauty "
                    "-sattrib openexr:ColorInteropIDPolicy strict "
                    "--pattern constant:color=0,1,0 4x4 3 -d half "
                    "--attrib oiio:subimagename specular "
                    "--siappendall -o missing_second.exr")

# Parts: "data", "lin_ap1_scene", "lin_ap1_scene"
# Not valid according to the CIF recommendation, error with strict policy.
command += oiiotool("--pattern constant:color=0.25,0.25,0.25 4x4 3 -d half "
                    "--attrib oiio:ColorSpace data --attrib oiio:subimagename depth "
                    "-sattrib openexr:ColorInteropIDPolicy strict "
                    "--pattern constant:color=1,0,0 4x4 3 -d half "
                    "--attrib oiio:ColorSpace lin_ap1_scene --attrib oiio:subimagename beauty "
                    "--pattern constant:color=0,1,0 4x4 3 -d half "
                    "--attrib oiio:ColorSpace lin_ap1_scene --attrib oiio:subimagename specular "
                    "--siappendall -o data_first.exr", failureok=True)

# How each later part's color space is established. The ID a part inherits
# from the first part is only a label, which the part's own chromaticities
# outrank. Explicit gamma 1 establishes their linear encoding. (OpenEXR
# requires every part to carry the same chromaticities, so the file is written
# with OpenColorIO disabled, which keeps them beside an ID with other
# primaries.) Parts: "lin_ap1_scene", missing; both with Rec.709
# chromaticities. Part 0 converts from AP1 by its own ID, part 1 from the
# Rec.709 its chromaticities state.
rec709 = "0.64,0.33,0.3,0.6,0.15,0.06,0.3127,0.329"
command += run_app(pythonbin + " src/without-ocio.py " + oiio_app("oiiotool")
                   + "--pattern constant:color=1,0,0 4x4 3 -d half "
                   "--attrib oiio:ColorSpace lin_ap1_scene --attrib oiio:subimagename beauty "
                   "--attrib oiio:Gamma 1 "
                   "--attrib:type=float[8] chromaticities " + rec709 + " "
                   "--pattern constant:color=1,0,0 4x4 3 -d half "
                   "--eraseattrib oiio:ColorSpace --attrib oiio:subimagename inherited "
                   "--attrib oiio:Gamma 1 "
                   "--attrib:type=float[8] chromaticities " + rec709 + " "
                   "--siappendall -o inherit_vs_numeric.exr")
# An inherited "unknown" is recorded as the part's own colorInteropID, since
# "unknown" cannot be a label: it is evidence, as a stated "unknown" is. With
# chromaticities the part converts from them; without, it ends as "unknown"
# and is refused, even under a configuration whose default would otherwise
# answer for a part that said nothing. Parts: "unknown", missing; with and
# without Rec.709 chromaticities; and "lin_ap1_scene", "unknown".
command += oiiotool("--pattern constant:color=1,0,0 4x4 3 -d half "
                    "--eraseattrib oiio:ColorSpace --attrib colorInteropID unknown "
                    "--attrib oiio:subimagename beauty "
                    "--attrib oiio:Gamma 1 "
                    "--attrib:type=float[8] chromaticities " + rec709 + " "
                    "--pattern constant:color=1,0,0 4x4 3 -d half "
                    "--eraseattrib oiio:ColorSpace --attrib oiio:subimagename inherited "
                    "--attrib oiio:Gamma 1 "
                    "--attrib:type=float[8] chromaticities " + rec709 + " "
                    "--siappendall -o unknown_first_chroma.exr")
command += oiiotool("--pattern constant:color=1,0,0 4x4 3 -d half "
                    "--eraseattrib oiio:ColorSpace --attrib colorInteropID unknown "
                    "--attrib oiio:subimagename beauty "
                    "--pattern constant:color=1,0,0 4x4 3 -d half "
                    "--eraseattrib oiio:ColorSpace --attrib oiio:subimagename inherited "
                    "--siappendall -o unknown_first.exr")
command += oiiotool("--pattern constant:color=1,0,0 4x4 3 -d half "
                    "--attrib oiio:ColorSpace lin_ap1_scene --attrib oiio:subimagename beauty "
                    "--pattern constant:color=1,0,0 4x4 3 -d half "
                    "--eraseattrib oiio:ColorSpace --attrib colorInteropID unknown "
                    "--attrib oiio:subimagename stated "
                    "--siappendall -o unknown_later.exr")
# OpenEXR requires a later part's chromaticities to equal the first part's,
# so a later part's that differ are not written. Its own colorInteropID
# states its encoding instead, and a part with none is written as "unknown"
# rather than inherit the first part's. Parts: "lin_rec709_scene", from a
# linear AP1 PNG, which gives it no ID; "lin_rec709_scene", "lin_ap1_scene"
# with AP1 chromaticities. (The first write is silent: with OPENIMAGEIO_DEBUG
# set, it says when a part is written as "unknown".)
ap1 = "0.713,0.293,0.165,0.830,0.128,0.044,0.32168,0.33767"
command += oiiotool("--pattern constant:color=1,0,0 4x4 3 -d uint16 "
                    "--iscolorspace ACEScg -o ap1.png")
command += oiiotool("--pattern constant:color=1,0,0 4x4 3 -d half "
                    "--attrib oiio:ColorSpace lin_rec709_scene --attrib oiio:subimagename beauty "
                    "ap1.png -d half --attrib oiio:subimagename png "
                    "--siappendall -o png_later.exr", silent=True)
command += oiiotool("--pattern constant:color=1,0,0 4x4 3 -d half "
                    "--attrib oiio:ColorSpace lin_rec709_scene --attrib oiio:subimagename beauty "
                    "--pattern constant:color=1,0,0 4x4 3 -d half "
                    "--attrib oiio:ColorSpace lin_ap1_scene --attrib oiio:subimagename ap1 "
                    "--attrib:type=float[8] chromaticities " + ap1 + " "
                    "--siappendall -o chroma_later.exr")
# Chromaticities beside an oiio:Gamma other than 1 are not written either,
# and a part with no ID that establishes its encoding is then written as
# "unknown", in a later part or a single one: an Adobe RGB PNG, and Adobe RGB
# chromaticities with oiio:Gamma 2.2. A part whose ID states its encoding
# keeps that ID. (Silent, as above.)
adobe = "0.64,0.33,0.21,0.71,0.15,0.06,0.3127,0.329"
command += oiiotool("--pattern constant:color=.5,.5,.5 4x4 3 -d uint8 "
                    "--iscolorspace g22_adobergb_display -o adobe.png")
command += oiiotool("--pattern constant:color=1,0,0 4x4 3 -d half "
                    "--attrib oiio:ColorSpace lin_rec709_scene --attrib oiio:subimagename beauty "
                    "adobe.png -d half --attrib oiio:subimagename png "
                    "--siappendall -o adobe_png_later.exr", silent=True)
command += oiiotool("--pattern constant:color=1,0,0 4x4 3 -d half "
                    "--attrib oiio:ColorSpace lin_rec709_scene --attrib oiio:subimagename beauty "
                    "--pattern constant:color=.5,.5,.5 4x4 3 -d half "
                    "--eraseattrib oiio:ColorSpace --attrib oiio:Gamma 2.2 "
                    "--attrib:type=float[8] chromaticities " + adobe + " "
                    "--attrib oiio:subimagename gamma "
                    "--siappendall -o gamma_later.exr", silent=True)
command += oiiotool("--pattern constant:color=1,0,0 4x4 3 -d half "
                    "--attrib oiio:ColorSpace lin_rec709_scene --attrib oiio:subimagename beauty "
                    "--pattern constant:color=.5,.5,.5 4x4 3 -d half "
                    "--attrib oiio:ColorSpace g22_rec709_scene --attrib oiio:Gamma 2.2 "
                    "--attrib:type=float[8] chromaticities " + rec709 + " "
                    "--attrib oiio:subimagename labeled "
                    "--siappendall -o labeled_later.exr")
command += oiiotool("adobe.png -d half -o adobe_png.exr", silent=True)
with open("default_linear.ocio", "w") as f:
    f.write("""ocio_profile_version: 2.3
strictparsing: false
roles: {default: Linear, scene_linear: Linear}
file_rules:
  - !<Rule> {name: Default, colorspace: default}
colorspaces:
  - !<ColorSpace>
    name: Linear
""")
for f, part in (("inherit_vs_numeric.exr", 0), ("inherit_vs_numeric.exr", 1),
                ("unknown_first_chroma.exr", 1), ("unknown_first.exr", 1),
                ("unknown_later.exr", 1), ("missing_first.exr", 2),
                ("png_later.exr", 1), ("chroma_later.exr", 1),
                ("adobe_png_later.exr", 1), ("gamma_later.exr", 1),
                ("labeled_later.exr", 1),
                ("adobe_png.exr", 0)):
    for core in (0, 1):
        command += oiiotool("--oiioattrib openexr:core {} {} --subimage {} "
                            "--echo \"{} part {} core {}: "
                            "<{{TOP['oiio:ColorSpace']}}> <{{TOP['colorInteropID']}}>\""
                            .format(core, f, part, f, part, core))
        command += oiiotool("--oiioattrib openexr:core {} -a {} "
                            "--tocolorspace lin_rec709_scene --subimage {} "
                            "--echo \"  to lin_rec709_scene: {{TOP.AVGCOLOR}}\""
                            .format(core, f, part), failureok=True)
# A configuration whose default would answer for a part that said nothing
# does not answer for one that inherited "unknown".
for f, part in (("unknown_first.exr", 1), ("missing_first.exr", 2)):
    command += oiiotool("--colorconfig default_linear.ocio -a {} "
                        "--tocolorspace Linear --subimage {} --echo \"{} part {} "
                        "under default_linear: converted\"".format(f, part, f, part),
                        failureok=True)

# Strict writes treat "unknown" as a missing ID wherever it appears, because
# it makes no claim.
# Parts: "unknown", "lin_ap1_scene" -- rejected, as with a missing first ID
command += oiiotool("--pattern constant:color=1,0,0 4x4 3 -d half "
                    "--eraseattrib oiio:ColorSpace --attrib colorInteropID unknown "
                    "--attrib oiio:subimagename beauty "
                    "-sattrib openexr:ColorInteropIDPolicy strict "
                    "--pattern constant:color=0,1,0 4x4 3 -d half "
                    "--attrib oiio:ColorSpace lin_ap1_scene --attrib oiio:subimagename specular "
                    "--siappendall -o strict_unknown_first.exr", failureok=True)
# Parts: "lin_ap1_scene", "unknown" -- accepted, as with a missing later ID
command += oiiotool("--pattern constant:color=1,0,0 4x4 3 -d half "
                    "--attrib oiio:ColorSpace lin_ap1_scene --attrib oiio:subimagename beauty "
                    "-sattrib openexr:ColorInteropIDPolicy strict "
                    "--pattern constant:color=0,1,0 4x4 3 -d half "
                    "--eraseattrib oiio:ColorSpace --attrib colorInteropID unknown "
                    "--attrib oiio:subimagename specular "
                    "--siappendall -o strict_unknown_later.exr")
# The same value in another case has the same missing-ID meaning.
command += oiiotool("--pattern constant:color=1,0,0 4x4 3 -d half "
                    "--attrib oiio:ColorSpace lin_ap1_scene --attrib oiio:subimagename beauty "
                    "-sattrib openexr:ColorInteropIDPolicy strict "
                    "--pattern constant:color=0,1,0 4x4 3 -d half "
                    "--eraseattrib oiio:ColorSpace --attrib colorInteropID UnKnOwN "
                    "--attrib oiio:subimagename specular "
                    "--siappendall -o strict_unknown_case_later.exr",
                    silent=True)
# Parts: "unknown", "unknown", "data" -- accepted
command += oiiotool("--pattern constant:color=1,0,0 4x4 3 -d half "
                    "--eraseattrib oiio:ColorSpace --attrib colorInteropID unknown "
                    "--attrib oiio:subimagename beauty "
                    "-sattrib openexr:ColorInteropIDPolicy strict "
                    "--pattern constant:color=0,1,0 4x4 3 -d half "
                    "--eraseattrib oiio:ColorSpace --attrib colorInteropID unknown "
                    "--attrib oiio:subimagename specular "
                    "--pattern constant:color=0,0,1 4x4 3 -d half "
                    "--attrib oiio:ColorSpace data --attrib oiio:subimagename depth "
                    "--siappendall -o strict_unknown_both.exr")
for f in ("strict_unknown_later.exr", "strict_unknown_both.exr"):
    command += oiiotool(f + " --echo \"" + f + " written\"")

# A later part's public output spec matches the header after OpenEXR rejects
# chromaticities that the first part lacks.
command += run_app(pythonbin + " src/check-output-spec.py", silent=True)
