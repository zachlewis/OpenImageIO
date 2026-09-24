#!/usr/bin/env python

# Copyright Contributors to the OpenImageIO project.
# SPDX-License-Identifier: Apache-2.0
# https://github.com/AcademySoftwareFoundation/OpenImageIO

# --colorspacesearch prints one name per line and needs no image operand.
# Every matrix below is copied verbatim from the bundled reference config, so
# a difference in a result is a difference in the search, not the fixture.

redirect = " >> out.txt 2>&1 "

REC709_TO_AP0 = (
    "!<MatrixTransform> {matrix: [0.439632981919491, 0.382988698151554, "
    "0.177378319928955, 0, 0.0897764429588424, 0.813439428748981, "
    "0.0967841282921771, 0, 0.0175411703831727, 0.111546553302387, "
    "0.87091227631444, 0, 0, 0, 0, 1]}"
)
AWG3_TO_AP0 = (
    "!<MatrixTransform> {matrix: [0.680205505106279, 0.236136601606481, "
    "0.0836578932872398, 0, 0.0854149797421404, 1.01747087860704, "
    "-0.102885858349182, 0, 0.00205652166929683, -0.0625625003847921, "
    "1.06050597871549, 0, 0, 0, 0, 1]}"
)

with open("search.ocio", "w") as f:
    f.write(f"""ocio_profile_version: 2.3
environment: {{SHOTSPACE: Pure22}}
roles: {{default: ACES, aces_interchange: ACES, scene_linear: ACES}}
file_rules:
  - !<Rule> {{name: Default, colorspace: default}}
inactive_colorspaces: [Hidden]
colorspaces:
  - !<ColorSpace>
    name: ACES
    encoding: scene-linear
  - !<ColorSpace>
    name: CameraLinear
    encoding: scene-linear
    to_scene_reference: {AWG3_TO_AP0}
  - !<ColorSpace>
    name: FalseLinearSRGB
    encoding: scene-linear
    to_scene_reference: !<GroupTransform>
      children:
        - !<ExponentWithLinearTransform> {{gamma: 2.4, offset: 0.055, direction: inverse}}
        - {REC709_TO_AP0}
  - !<ColorSpace>
    name: FalseLinearPower22
    encoding: scene-linear
    to_scene_reference: !<GroupTransform>
      children:
        - !<ExponentTransform> {{value: 2.2}}
        - {REC709_TO_AP0}
  - !<ColorSpace>
    name: DisplayMeaning
    encoding: display-linear
  - !<ColorSpace>
    name: Pure22
    aliases: ["Rec.709, gamma 2.2"]
    encoding: sdr-video
    to_scene_reference: !<GroupTransform>
      children:
        - !<ExponentTransform> {{value: 2.2}}
        - {REC709_TO_AP0}
  - !<ColorSpace>
    name: TrueSRGB
    encoding: sdr-video
    to_scene_reference: !<GroupTransform>
      children:
        - !<ExponentWithLinearTransform> {{gamma: 2.4, offset: 0.055}}
        - {REC709_TO_AP0}
  - !<ColorSpace>
    name: OddToeGain
    encoding: sdr-video
    to_scene_reference: !<GroupTransform>
      children:
        - !<ExponentWithLinearTransform> {{gamma: 3, offset: 0.2}}
        - !<MatrixTransform> {{matrix: [0.9, 0, 0, 0, 0, 0.9, 0, 0, 0, 0, 0.9, 0, 0, 0, 0, 1]}}
        - {REC709_TO_AP0}
  - !<ColorSpace>
    name: InsetChain
    encoding: sdr-video
    to_scene_reference: !<GroupTransform>
      children:
        - !<ExponentWithLinearTransform> {{gamma: 3, offset: 0.2}}
        - {AWG3_TO_AP0}
        - !<ExponentWithLinearTransform> {{gamma: 3, offset: 0.2}}
        - {REC709_TO_AP0}
  - !<ColorSpace>
    name: Hidden
    encoding: sdr-video
    to_scene_reference: !<GroupTransform>
      children:
        - !<ExponentTransform> {{value: 2.2}}
        - {REC709_TO_AP0}
  - !<ColorSpace>
    name: ShotSpace
    encoding: sdr-video
    to_scene_reference: !<ColorSpaceTransform> {{src: "$SHOTSPACE", dst: ACES}}
  - !<ColorSpace>
    name: Shot's Look
    encoding: sdr-video
    to_scene_reference: !<GroupTransform>
      children:
        - !<ExponentTransform> {{value: 2.2}}
        - {REC709_TO_AP0}
""")

config = "--nostderr --colorconfig search.ocio "

searches = [
    # only= names canonical spellings, and the result is ordered.
    ("only", ":only=Pure22,TrueSRGB"),
    # Raw OpenColorIO transform text, read as a from-reference transform. The
    # commas and colons inside the flow mapping are part of one term.
    ("transfer text",
     ":only=Pure22,TrueSRGB:transfer='<ExponentTransform> "
     "{value: 2.2, direction: inverse}'"),
    # A curve named by a Color Interop ID, measured rather than matched by
    # name: the 2.2 power is not sRGB however it is spelled.
    ("transfer id", ":only=Pure22,TrueSRGB:transfer=srgb_rec709_scene"),
    # A quoted term keeps its comma, and loses its quotes, anywhere in a list.
    ("quoted term",
     ":only=Pure22,TrueSRGB:transfer=TrueSRGB,'Rec.709, gamma 2.2'"),
    # Including first, where the quote opens the value: the items after it are
    # part of the same list and not dropped.
    ("quoted first term", ":only='Pure22',TrueSRGB"),
    ("quoted first term with comma",
     ":only=Pure22,TrueSRGB:transfer='Rec.709, gamma 2.2',TrueSRGB"),
    # And a value that is entirely one quoted item: the item is unquoted as a
    # list item, so its comma stays inside it.
    ("whole quoted term",
     ":only=Pure22,TrueSRGB:transfer='Rec.709, gamma 2.2'"),
    ("quoted first exclusion",
     ":only=Pure22,TrueSRGB:exclude='Pure22',TrueSRGB"),
    # A quote that does not open a value or a term -- an apostrophe inside a
    # name -- is an ordinary character, so the modifiers and the list items
    # written after it are still read.
    ("apostrophe in a name", ":only=Shot's Look:properties=1"),
    ("apostrophe in a list", ":only=Shot's Look,Pure22"),
    # A colon-bearing identity survives modifier extraction when quoted.
    ("gamut colon id", ":gamut='ocio:lin_awg3_scene'"),
    ("quoted first colon id", ":gamut='ocio:lin_awg3_scene',ACES"),
    # In any position: a quote opens a term after a comma as well, so the
    # colon inside a later item is not read as oiiotool's own separator.
    ("quoted later colon id", ":gamut=ACES,'ocio:lin_awg3_scene'"),
    # An inactive space has to be asked for.
    ("inactive default", ":only=Hidden"),
    ("inactive requested", ":only=Hidden:inactive=1"),
    # So does a context-sensitive one, and it answers differently under an
    # override.
    ("context default",
     ":only=ShotSpace:contextsensitive=1:transfer=Pure22"),
    ("context override",
     ":only=ShotSpace:contextsensitive=1:transfer=Pure22"
     ":key=SHOTSPACE:value=TrueSRGB"),
    # key= and value= take a single value rather than a list, so a quoted one
    # loses its quotes as a whole and still overrides.
    ("quoted context override",
     ":only=ShotSpace:contextsensitive=1:transfer=Pure22"
     ":key='SHOTSPACE':value='TrueSRGB'"),
    # Image state follows known identity/encoding semantics, not OCIO's
    # reference-space topology. Unknown state survives exclusion but does not
    # satisfy inverse selection.
    ("semantic display state", ":only=DisplayMeaning:state=display"),
    ("unknown state exclusion", ":only=OddToeGain:state=-display"),
    ("unknown state inverse", ":only=OddToeGain:state=~display"),
    # Opt-in property display. This search measures nothing by itself, so
    # every curve reported here is derived for the display: the exponent for
    # the pure power, the published family for the piecewise curve, and no
    # invented exponent beside the family.
    ("properties", ":only=Pure22,TrueSRGB:properties=1"),
    ("representation properties",
     ":only=OddToeGain,InsetChain:properties=1"),
    # The property display honors an authored linear encoding rather than
    # measuring it, and says so: ACES, FalseLinearSRGB, and
    # FalseLinearPower22 -- whose definition is a 2.2 power -- print
    # declared-linear with a power of 1 and derived=0. transfer= measures
    # instead, so the false-linear spaces are not linear matches and *are*
    # proven differences from one. Both answers are intended; the manual says
    # so.
    ("declared linear properties",
     ":only=ACES,FalseLinearPower22,FalseLinearSRGB:properties=1"),
    ("declared linear measured",
     ":only=ACES,FalseLinearPower22,FalseLinearSRGB:transfer=ACES"),
    ("declared linear inverse",
     ":only=ACES,FalseLinearPower22,FalseLinearSRGB:transfer=~ACES"),
    # A curve measured to be linear still prints plain linear, with
    # derived=1: ShotSpace declares no linear encoding, and this context
    # points its definition at ACES.
    ("measured linear properties",
     ":only=ShotSpace:contextsensitive=1:key=SHOTSPACE:value=ACES"
     ":properties=1"),
]

for label, modifiers in searches:
    command += oiiotool(f'--echo "=== {label} ==="')
    # Double-quote the whole argument: the modifiers carry spaces, braces and
    # angle brackets that belong to OpenColorIO's grammar, not the shell's.
    # These commands run through cmd.exe on Windows, where single quotes are
    # ordinary characters and `<` stays a redirection operator unless it sits
    # inside double quotes -- so the OpenColorIO text inside is single-quoted
    # instead (oiiotool accepts either quote around a modifier value).
    # Disable oiiotool expressions while passing literal OCIO flow mappings.
    literal = "--evaloff " if label == "transfer text" else ""
    command += oiiotool(config + literal + '"--colorspacesearch' + modifiers
                        + '"')

# A malformed term and an unknown modifier are ordinary tool errors.
command += oiiotool('--echo "=== bad term ==="')
command += oiiotool(config + '"--colorspacesearch:transfer=-"', failureok=True)
command += oiiotool('--echo "=== bad modifier ==="')
command += oiiotool(config + '"--colorspacesearch:bogus=1"', failureok=True)

# The commands the user manual displays, run verbatim against the built-in
# default config: the manual includes them from this source, so a command a
# reader copies is one this test ran.
command += oiiotool('--echo "=== manual examples ==="')
command += run_app(pythonbin + ' src/manual-examples.py "'
                   + oiio_app("oiiotool").strip() + '"')

outputs = ["out.txt"]
