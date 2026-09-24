#!/usr/bin/env python

# Copyright Contributors to the OpenImageIO project.
# SPDX-License-Identifier: Apache-2.0
# https://github.com/AcademySoftwareFoundation/OpenImageIO

# --colortransfer writes one transform document to the console and needs no
# image operand. What OpenColorIO's own writers put inside a document is
# theirs and moves with the version, so what is compared here is what belongs
# to OpenImageIO: which operations survive the separation into a curve, which
# grammars can spell them, and what every refusal says. The unit test beside
# this one checks the pixels.

redirect = " >> out.txt 2>&1 "

REC709_TO_AP0 = (
    "!<MatrixTransform> {matrix: [0.439632981919491, 0.382988698151554, "
    "0.177378319928955, 0, 0.0897764429588424, 0.813439428748981, "
    "0.0967841282921771, 0, 0.0175411703831727, 0.111546553302387, "
    "0.87091227631444, 0, 0, 0, 0, 1]}"
)
P3D65_TO_AP0 = (
    "!<MatrixTransform> {matrix: [0.518933487597981, 0.28625658638669, "
    "0.194809926015329, 0, 0.0738593830470598, 0.819845163936986, "
    "0.106295453015954, 0, -0.000307011368446647, 0.0438070502536223, "
    "0.956499961114824, 0, 0, 0, 0, 1]}"
)
# A uniform headroom scale, authored as its own diagonal matrix. It is part of
# the curve, not of the primaries, and dropping it would export a bare power.
HEADROOM = ("!<MatrixTransform> {matrix: [0.9166, 0, 0, 0, 0, 0.9166, 0, 0, "
            "0, 0, 0.9166, 0, 0, 0, 0, 1]}")

with open("transfer.ocio", "w") as f:
    f.write(f"""ocio_profile_version: 2.3
environment: {{PLATE: Pure22}}
roles: {{default: ACES, aces_interchange: ACES, scene_linear: ACES}}
file_rules:
  - !<Rule> {{name: Default, colorspace: default}}
colorspaces:
  - !<ColorSpace>
    name: ACES
    encoding: scene-linear
  - !<ColorSpace>
    name: Rec709Linear
    to_scene_reference: {REC709_TO_AP0}
  - !<ColorSpace>
    name: Pure22
    to_scene_reference: !<GroupTransform>
      children:
        - !<ExponentTransform> {{value: 2.2}}
        - {REC709_TO_AP0}
  - !<ColorSpace>
    name: HeadroomPure26
    to_scene_reference: !<GroupTransform>
      children:
        - !<ExponentTransform> {{value: 2.6}}
        - {HEADROOM}
        - {REC709_TO_AP0}
  - !<ColorSpace>
    name: Pure26
    to_scene_reference: !<GroupTransform>
      children:
        - !<ExponentTransform> {{value: 2.6}}
        - {REC709_TO_AP0}
  - !<ColorSpace>
    name: ShotSpace
    to_scene_reference: !<ColorSpaceTransform> {{src: "$PLATE", dst: ACES}}
  - !<ColorSpace>
    name: InsetChain
    to_scene_reference: !<GroupTransform>
      children:
        - !<ExponentTransform> {{value: 2.2}}
        - {P3D65_TO_AP0}
        - !<ExponentTransform> {{value: 2.2}}
        - {REC709_TO_AP0}
  - !<ColorSpace>
    name: Plain
    isdata: true
""")

config = "--nostderr --colorconfig transfer.ocio "

# Which operations each document ends up carrying. The curve survives. The
# primaries matrix that carried the interchange into the space's own linear
# RGB does not, in any grammar. A uniform headroom scale is curve and survives
# beside the power. An identity curve still writes a native transform document.
# Every document ends with exactly one newline, whichever grammar wrote it.
# `oneline=1` carries the same operations of the `ocio` grammar in a flow
# sequence -- `children: [` is what a block sequence does not write -- so the
# whole of a two-operation curve arrives on the single line it promises.
cases = [
    ("power / ctf", "Pure22", "ctf", ["<(Gamma|Exponent) ", "<Matrix"]),
    ("power / clf", "Pure22", "clf", ["<(Gamma|Exponent) ", "<Matrix"]),
    ("power / ocio", "Pure22", "ocio",
     ["ExponentTransform", "MatrixTransform"]),
    ("headroom / ctf", "HeadroomPure26", "ctf",
     ["<(Gamma|Exponent) ", "<Matrix"]),
    ("headroom / ocio", "HeadroomPure26", "ocio",
     ["ExponentTransform", "MatrixTransform"]),
    ("headroom / oneline", "HeadroomPure26", "ocio:oneline=1",
     ["children: \\[", "ExponentTransform", "MatrixTransform"]),
    ("identity / ctf", "Rec709Linear", "ctf", ["<ProcessList", "<Matrix"]),
]

for label, space, fmt, patterns in cases:
    command += oiiotool(f'--echo "=== {label} ==="')
    name = label.replace(" / ", "-")
    command += run_app(oiio_app("oiiotool") + config
                       + f"--colortransfer:format={fmt} {space}"
                       + f" > {name}.txt 2>&1", silent=True)
    for pattern in patterns:
        command += run_app(f'{pythonbin} src/checks.py count "{pattern}" '
                           f"{name}.txt")
    command += run_app(f"{pythonbin} src/checks.py newlines {name}.txt")

# The key= and value= modifiers select the curve under that OCIO context, and
# without them the config's own environment applies -- to the one-line form as
# much as to the document. Compare native text written
# by the context-selected space with direct exports in the same linked OCIO
# writer, avoiding version-specific serialization text.
for name, context_args in (("shot-default", " ShotSpace"),
                           ("shot-override",
                            ":key=PLATE:value=Pure26 ShotSpace"),
                           ("pure22", " Pure22"), ("pure26", " Pure26"),
                           ("shot-override-oneline",
                            ":oneline=1:key=PLATE:value=Pure26 ShotSpace"),
                           ("pure26-oneline", ":oneline=1 Pure26")):
    command += run_app(oiio_app("oiiotool") + config
                       + f"--colortransfer:format=ocio{context_args} > {name}.txt",
                       silent=True)
command += run_app(f"{pythonbin} src/checks.py same shot-default.txt "
                   "pure22.txt context-default")
command += run_app(f"{pythonbin} src/checks.py same shot-override.txt "
                   "pure26.txt context-override")
command += run_app(f"{pythonbin} src/checks.py same shot-override-oneline.txt "
                   "pure26-oneline.txt context-oneline")

# The examples the manual shows for this command, run from the one file both
# it and this test read. src/examples.txt is what oiiotool.md displays, so an
# example that stops working stops the test rather than going stale in print.
command += oiiotool('--echo "=== manual examples ==="')
command += run_app(f"{pythonbin} src/checks.py manual src/examples.txt "
                   f'"{oiio_app("oiiotool").strip()}"')

# Every refusal is an ordinary tool error naming what it refused, and there is
# nothing behind any of them: no resampled, fitted or approximate stand-in
# follows a refusal.
command += oiiotool('--echo "=== refusals ==="')
for space, modifiers in (("InsetChain", ""),
                         ("Plain", ""),
                         ("NotASpace", ""),
                         ("Pure22", ":format=cube"),
                         ("Pure22", ":format=clf:oneline=1"),
                         ("Pure22", ":bogus=1")):
    command += oiiotool(config + f"--colortransfer{modifiers} {space}",
                        failureok=True)

outputs = ["out.txt"]
