<!-- Copyright Contributors to the OpenImageIO project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Color metadata resolution

OpenImageIO resolves an image's declared color encoding before automatic color
conversion. The resolver runs once for an input and returns both the selected
source and ordered rule outcomes used by debug diagnostics.

The input rules are evaluated in this order:

1. An explicit caller assignment.
2. The ACES container flag.
3. The filename step when the policy is `first`.
4. `colorInteropID`.
5. cICP.
6. An ICC profile.
7. A PNG sRGB chunk.
8. Numeric metadata: PNG gamma, optionally with chromaticities, or OpenEXR
   chromaticities.
9. The filename step when the policy is `fallback`.
10. The reader's `oiio:ColorSpace` label.
11. A caller-provided failover.

The filename step is a non-default OCIO FileRule together with OpenImageIO's
own convention of a color space name embedded in the filename, which
`getColorSpaceFromFilepath` matches in one call. The `metadata` FileRules
policy omits steps 3 and 9, so it turns off both. A complete PNG cICP claim
suppresses weaker PNG chunks and the reader label, including when OpenImageIO
cannot identify the claim. Invalid evidence does not stop resolution; the
resolver records the failure and continues. Numeric evidence is invalid when
chromaticities are non-finite or gamma is negative or non-finite.
RGB matrix/TRC ICC profiles supported by OpenColorIO are decoded and compared
with known encodings. A known profile selects that encoding. An unmatched but
decodable profile receives a process-local selector for its exact conversion;
the selector is not a portable color interop ID. Unsupported profiles make no
claim and resolution continues. PNG cICP retains precedence over ICC.

Complete numeric facts that match no known encoding also receive a
process-local selector when the resolution is for a conversion; resolved for
anything else, they are evidence nothing here can use, as described below.
Virtual primaries remain valid when their
matrix is finite and invertible. Gamma without chromaticities remains a partial
fact: it searches configured display-referred spaces first and then
scene-referred spaces, without establishing a gamut. When neither search
matches, conversion receives a process-local selector for the stated transfer.
Resolution continues through the selected FileRules policy, the reader label,
and any caller-provided failover; a result from one of those rules retains that
rule's provenance.

The resolver evaluates names, roles, FileRules, and transforms under the
caller's effective OCIO context. Automatic conversion uses that same context,
so resolution and processor creation cannot select different context-dependent
spaces. Known portable encodings absent from the active configuration connect
to it through OpenImageIO's internal reference and an OCIO interchange role.

There are two ways for resolution to end without a color space, and they are
not the same.

**Something was stated and nothing here can use it.** A `colorInteropID` of
`unknown`, an ID or label this configuration does not define, numeric metadata
that identifies nothing it can use, an explicit assignment that names nothing
usable, or a full PNG cICP claim OpenImageIO cannot identify. Each is evidence,
so the rules after it still run, and a
caller's failover still wins. When they all come up empty the answer is
`unknown`, whatever the unresolved-source policy says: a default assignment
would overwrite a statement the file actually made. Such an image is tagged
`unknown` and left unconverted, and the evidence it could not act on goes
with the tag, because written out those facts would resolve the next read to
the encoding this one declined to assert.

A name the caller supplies — the assignment and the failover — is resolved
under the effective context before it is judged, so it may spell a context
variable. Only a caller's name that resolves to an *empty* string named
nothing; one that resolves to a non-empty name the configuration does not
define is the caller's mistake, and the catch-space below does not answer it.
A variable the effective context does not define is left unexpanded by
OpenColorIO, so it is such a name: an undefined variable is the caller's
mistake, not a name that resolved to nothing. The per-call override is a
key list and a value list of the same length, so an empty value list
overrides nothing: a spelling resolves to an empty string only where the
configuration's own `environment` maps it so.

**Nothing was stated at all.** This is the only case the caller's
unresolved-source policy answers. `none` leaves the image unconverted.
`config` follows the configuration: under OCIO strict parsing the answer is
`unknown`, and otherwise the FileRules default assignment and then the
`default` role.

Wherever the answer is `unknown`, a color space the configuration names or
aliases `unknown` answers first: that is the config author's catch-space, and
it is an ordinary source that conversion uses. It answers both cases above —
a stated `unknown`, an identifier nothing here can use, a suppressed cICP
claim, and an image that stated nothing under the `config` policy — so such
a configuration tags an image `unknown` only for a caller's own name. Only
when the configuration has no such space, or when a caller's own name
missed with something non-empty behind it, is the result the literal
`unknown`, which conversion refuses as a source.

`ImageBufAlgo::colorconvert`, `ImageBufAlgo::ociodisplay`, and their
`oiiotool` counterparts accept an empty or `current` source. They resolve that
source from the ImageBuf metadata under the supplied config and context, with
the `config` policy above; before 3.3 they silently used `scene_linear`
instead. They do not apply OCIO FileRules to the ImageBuf name. After a real
conversion, source encoding metadata is removed and the result is tagged with
the destination. Mastering display metadata is not removed.

`ImageBufAlgo::ociolook`, `ImageBufAlgo::FLIP_diff`, the output side of
`oiiotool --autocc` and the terminal image writer also resolve an unlabeled
image's own color space from its metadata, with the `none` policy: an image
that states nothing at all is still taken to be `scene_linear` there.

`oiiotool --autocc` keeps `none` as its default policy, so an input that
states nothing is left alone rather than converted from a configuration
default that the tool has never applied.

Readers take part in this by recording what a file says rather than deciding
what it means. In particular a file's own `colorInteropID` of `unknown` says
that whoever wrote it could not identify the pixels, which is not a statement
about the pixels: the OpenEXR readers leave `oiio:ColorSpace` unset and keep
the attribute as the evidence it is. `set_colorspace("unknown")`, by
contrast, is a statement, and it stops resolution.

`ImageSpec::set_colorspace(name)` (and `oiiotool --iscolorspace`) makes the name
authoritative: the other color metadata is kept where it agrees with the name,
rewritten where the name determines it, and removed otherwise. With no name it
sets an unset `oiio:ColorSpace` from this resolver, with no filename, and sets
it only to a configured color space or a Color Interop ID; `""` removes only
`oiio:ColorSpace`. A strict miss and an unsupported full PNG cICP claim use
`set_colorspace("unknown")`, so their evidence is removed with them.
Process-local ICC and numeric selectors are conversion endpoints only. They are
spelled with a `<synthetic>` prefix, which no color interop ID can contain, and
writing a converted result does not fabricate a `colorInteropID` for them.

With `oiiotool --debug`, each reached rule reports whether it matched, missed,
was skipped, or contained invalid evidence. The following summary describes
the same resolver result; debug mode does not run resolution again or alter the
conversion.
