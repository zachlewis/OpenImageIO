# Color configuration facts and recognition

`ColorConfig` keeps authored names, aliases, roles, data flags, active state,
and declared color interop IDs separate from facts derived from transforms.
Construction and enumeration do not load the built-in interop-identities config
or analyze transforms.

## Property snapshots

`ColorSpaceInfo` is an immutable owning snapshot. Its public accessors are
`valid()`, `chromaticities()` and `transfer_function_gamma()`; `valid()` means
that the requested name resolved, not that every field is available. Copies
survive configuration reset or destruction. The cheap
`get_color_space_info()` query reads native and completed facts only.
`derive_color_space_info()` performs the bounded work needed to establish
supported missing properties.

The library reads the remaining facts through the private
`ColorSpaceInfoAccess` (in `imageio_pvt.h`): the transfer function kind and
family, the equality ID, the color interop ID and the image state. Each field
also records three independent lifecycle facts: whether
evaluation completed, whether a value is available, and whether the value came
from explicit derivation. A completed unsupported field is therefore distinct
from one that has not been evaluated. Before any derivation, the cheap query
counts the color interop ID and image state as evaluated only when the
configuration declares them.

The private `pvt::color_space_info()` accepts OpenColorIO context keys and
values. It acquires a configuration view using the caller's keys and values,
does not modify the configuration's own context, and results from one
effective context do not answer another. Failed acquisition returns an invalid
snapshot and retains no result.

## Declared and measured identity

A declared color interop ID is authoritative metadata supplied by the
configuration. `color_interop_id()` returns that declaration when present;
otherwise explicit derivation may return a recognized ID. Derivation does not
replace a declaration. The equality ID instead reports which supported
reference encoding the configured transform reproduces within the recognition
tolerance, independent of names, aliases, and declarations.

An empty equality ID means that no supported reference definition reproduced
the transform. It does not establish uniqueness or inequality. Likewise, two
spaces with the same measured ID are not automatically interchangeable:
`ColorConfig::equivalent()` also requires OpenColorIO to report their local
conversion as a no-op under the effective context.

Recognition compares admitted native transformations through the appropriate
scene or display interchange role. It may establish RGBW chromaticities, an
exact pure-power decoding exponent, a published transfer family, or whether
the native operations form one shared RGB transfer. Unsupported facts remain
unavailable rather than being guessed from a familiar name.

A declared `scene-linear` or `display-linear` encoding is an exception, and a
deliberate one: it is honored rather than measured. Such a space reports a
transfer function gamma of 1.0, and writers tag it gamma 1.0, even where its
transforms measure as a different pure power or as a curve no exponent
describes. The declaration is the config author's statement about the space;
measurement does not replace the declared 1.0.

Runtime recognition candidates come from the built-in interop-identities config
and the OpenColorIO version actually in use. This keeps recognition aligned with
the definitions the running build can construct. Recognition provides facts;
it does not add a color space to the active configuration or change pixels.

## Names and roles

`scene_linear` remains an actual OCIO role: it selects the facility working
space, not necessarily Rec.709, so a match to it is never evidence of Rec.709
primaries. Identifying a configured space measures its transform where it
can, and that measurement outranks the space's name. Selecting a space for a
portable ID or one of OIIO's long-standing generic names (such as `sRGB`,
`linear`, and `ACEScg`) is unchanged: a space carrying the legacy name is
selected first, even when its transform measures as another encoding, and a
space named for the identity remains the fallback when measurement finds
none.

Builds explicitly configured with `OIIO_SITE=spi` retain that site's naming
conventions: `cgln*` names identify ACEScg; `srgbf`, `srgbh`, `srgb16`, and
`srgb8` identify scene sRGB; `srgblnf`, `srgblnh`, `srgbln16`, and `srgbln8`
identify linear Rec.709; and `nc*` names are data. These opt-in hints do not
apply to ordinary builds and do not extend to `lnf` alone.

## Color-space property search

`pvt::find_color_spaces` is internal (declared in `src/include/imageio_pvt.h`,
not public C++ or Python API); users reach it through
`oiiotool --colorspacesearch`. It filters the native configuration catalog on
four axes: primaries, transfer function, encoding, and semantic image state. It
does not change identity, metadata, or conversion behavior, and returns
canonical local names in deterministic order. Data spaces and `is-unique`
spaces are never candidates.

Every axis uses the same three-valued term grammar. `-x` removes candidates
proven to match while retaining candidates whose property could not be
determined. `~x` selects only candidates proven to differ. `\` escapes a
leading operator. Local names, aliases, and roles take precedence over color
interop IDs and each axis's literal vocabulary.

Transfer searches evaluate encoding-direction transforms. They do not infer a
curve from a name, a declared color interop ID, or a `scene-linear` label. A
candidate that agrees with the hint on a published curve family matches on that
family; otherwise the two measurements are compared within the tolerance the
candidate's encoding sets, or the term's encoding where the candidate has none.
The `properties=1` display is not that axis: it prints the record
`ColorSpaceInfo` already holds, in which an authored linear encoding is
honored rather than measured. The display prints that as `declared-linear`,
with `derived=0`, and keeps `linear` for a derived property such as a curve
measured to be linear. A space declaring a linear encoding its definition does
not implement therefore prints `declared-linear` with a power of 1, even if the
definition measures as another pure power, and is still selected by a `~`
transfer term naming a linear space. The two answer different questions and
neither is wrong.
Image-state searches use the same identity/encoding evidence exposed by
`ColorSpaceInfo`; OCIO reference-space topology is not image-state evidence.
The state property may be derived when that axis is requested, while a query
without a state axis does no state work. An undetermined state survives a
negative exclusion and fails an inverse term.
Bounded searches inspect definitions supported by the existing property
machinery and reuse retained measurements. `exhaustive` allows OpenColorIO to
attempt definitions outside that structural bound, while an unavailable
property remains undetermined. Context-dependent candidates are opt-in, and a
single effective context is used for the complete query.
