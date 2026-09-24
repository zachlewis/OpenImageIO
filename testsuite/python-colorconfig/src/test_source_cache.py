#!/usr/bin/env python
# Copyright Contributors to the OpenImageIO project.
# SPDX-License-Identifier: Apache-2.0
# https://github.com/AcademySoftwareFoundation/OpenImageIO

import os
from pathlib import Path
import subprocess
import sys
import tempfile
import zipfile

import numpy as np
import OpenImageIO as oiio


VARIABLE = "OIIO_TEST_SOURCE_SPACE"


def load(path, name="Reference"):
    config = oiio.ColorConfig(str(path))
    error = config.geterror()
    assert not error, error
    assert name in config.getColorSpaceNames()
    return config


def convert(path, expected, **context):
    pixels = np.array([[[.125, .25, .375]]], dtype=np.float32)
    result = oiio.ImageBufAlgo.colorconvert(
        oiio.ImageBuf(pixels), "Selected", "Reference", colorconfig=str(path),
        **context)
    assert not result.has_error, result.geterror()
    actual = result.get_pixels(oiio.FLOAT)
    assert actual is not None and not result.has_error, result.geterror()
    assert np.allclose(actual, pixels * expected), actual


def fixture(environment=True):
    return ("ocio_profile_version: 2.3\n"
            + (f"environment: {{{VARIABLE}: Reference}}\n" if environment else "")
            + "roles: {default: Reference}\n"
            "file_rules:\n  - !<Rule> {name: Default, colorspace: default}\n"
            "displays:\n"
            "  First:\n    - !<View> {name: One, colorspace: Reference}\n"
            "    - !<View> {name: Two, colorspace: Double}\n"
            "  Second:\n    - !<View> {name: One, colorspace: Reference}\n"
            "    - !<View> {name: Two, colorspace: Double}\n"
            "colorspaces:\n"
            "  - !<ColorSpace>\n    name: Reference\n"
            "    encoding: scene-linear\n"
            "  - !<ColorSpace>\n    name: Double\n"
            "    to_scene_reference: !<MatrixTransform> "
            "{matrix: [2,0,0,0,0,2,0,0,0,0,2,0,0,0,0,1]}\n"
            "  - !<ColorSpace>\n    name: Selected\n"
            "    to_scene_reference: !<ColorSpaceTransform> "
            f"{{src: '${VARIABLE}', dst: Reference}}\n")


def worker(mode, directory):
    path = directory / "config.ocio"
    original = fixture(environment=(mode != "reuse_all"))
    path.write_text(original)
    if mode in ("reuse", "reuse_all"):
        for _ in range(4):
            config = load(path)
            info = config.get_color_space_info("Reference")
            del config
            assert info.valid() and info.transfer_function_gamma() == 1
    elif mode == "rewrite":
        first = load(path)
        saved = first.get_color_space_info("Reference")
        load(path)
        before = path.stat()
        changed = original.replace("Double", "Triple")
        assert len(changed) == len(original)
        path.write_text(changed)
        os.utime(path, ns=(before.st_atime_ns, before.st_mtime_ns))
        assert "Double" not in load(path, "Triple").getColorSpaceNames()
        assert "Double" in first.getColorSpaceNames()
        path.unlink()
        assert oiio.ColorConfig(str(path)).geterror()
        path.write_text("not valid OCIO: [\n")
        assert oiio.ColorConfig(str(path)).geterror()
        path.write_text(original)
        load(path, "Double")
        replacement = directory / "replacement.ocio"
        replacement.write_text(changed)
        replacement.replace(path)
        load(path, "Triple")
        del first
        assert saved.valid() and saved.transfer_function_gamma() == 1
    elif mode == "environment":
        convert(path, 1)
        os.environ[VARIABLE] = "Double"
        convert(path, 2)
        convert(path, 2)
        os.environ.pop(VARIABLE)
        convert(path, 1)
        convert(path, 2, context_key=VARIABLE, context_value="Double")
        convert(path, 1)
        # LOAD_ALL must also forget variables absent from a later capture.
        path.write_text(fixture(environment=False))
        os.environ[VARIABLE] = "Double"
        convert(path, 2)
        os.environ[VARIABLE] = "Reference"
        convert(path, 1)
        os.environ.pop(VARIABLE)
        result = oiio.ImageBufAlgo.colorconvert(
            oiio.ImageBuf(np.zeros((1, 1, 3), dtype=np.float32)),
            "Selected", "Reference", colorconfig=str(path))
        assert result.has_error
    elif mode == "selectors":
        first = load(path)
        assert first.getDefaultDisplayName() == "First"
        assert first.getDefaultViewName("First") == "One"
        # Change one selector at a time.
        os.environ["OCIO_ACTIVE_DISPLAYS"] = "Second"
        assert load(path).getDefaultDisplayName() == "Second"
        os.environ["OCIO_ACTIVE_VIEWS"] = "Two"
        second = load(path)
        assert second.getDefaultDisplayName() == "Second"
        assert second.getDefaultViewName("Second") == "Two"
        assert first.getDefaultDisplayName() == "First"
        os.environ.pop("OCIO_ACTIVE_DISPLAYS")
        third = load(path)
        assert third.getDefaultDisplayName() == "First"
        assert third.getDefaultViewName("First") == "Two"
        os.environ.pop("OCIO_ACTIVE_VIEWS")
        again = load(path)
        assert again.getDefaultDisplayName() == "First"
        assert again.getDefaultViewName("First") == "One"
        os.environ["OCIO_INACTIVE_COLORSPACES"] = "Double"
        load(path)
        os.environ.pop("OCIO_INACTIVE_COLORSPACES")
        load(path)
    elif mode == "paths":
        # Identical config bytes must retain the descriptor's relative LUT root.
        local = original.replace(
            f"!<ColorSpaceTransform> {{src: '${VARIABLE}', dst: Reference}}",
            "!<FileTransform> {src: scale.spimtx}")
        for name, scale in (("first", 2), ("second", 3)):
            folder = directory / name
            folder.mkdir()
            (folder / "config.ocio").write_text(local)
            (folder / "scale.spimtx").write_text(
                f"{scale} 0 0 0\n0 {scale} 0 0\n0 0 {scale} 0\n")
        previous = Path.cwd()
        try:
            for name, scale in (("first", 2), ("second", 3), ("first", 2)):
                os.chdir(directory / name)
                convert("config.ocio", scale)
            os.chdir(directory)
            if os.name != "nt":
                link = directory / "second" / "linked.ocio"
                link.symlink_to(directory / "first" / "config.ocio")
                convert(link, 3)
                convert(link, 3)
        finally:
            os.chdir(previous)
    elif mode == "archive":
        # Archives, URIs and other nonregular sources keep the native loader.
        archive = directory / "config.ocioz"
        with zipfile.ZipFile(archive, "w") as contents:
            contents.writestr("config.ocio", original)
        load(archive)
        load(archive)
        assert not oiio.ColorConfig("ocio://default").geterror()
        assert oiio.ColorConfig(str(directory)).geterror()


if "--worker" in sys.argv:
    worker(sys.argv[2], Path(sys.argv[3]))
else:
    for mode in ("reuse", "reuse_all", "rewrite", "environment", "selectors",
                 "paths", "archive"):
        with tempfile.TemporaryDirectory() as directory:
            env = dict(os.environ, OIIO_DEBUG_COLOR="1")
            for name in (VARIABLE, "OCIO_ACTIVE_DISPLAYS", "OCIO_ACTIVE_VIEWS",
                         "OCIO_INACTIVE_COLORSPACES"):
                env.pop(name, None)
            result = subprocess.run(
                [sys.executable, __file__, "--worker", mode, directory],
                env=env, capture_output=True, text=True)
            output = result.stdout + result.stderr
            assert result.returncode == 0, output
            if mode in ("reuse", "reuse_all"):
                assert output.count("OCIO source parse: ") == 1, output
                assert output.count("OCIO source cache hit: ") == 3, output
            if mode == "environment":
                # Source/context changes reparse; unchanged defaults and
                # per-query overrides reuse the same source capture.
                assert output.count("OCIO source parse: ") == 6, output
                assert output.count("OCIO source cache hit: ") == 3, output
            if mode == "archive":
                assert "OCIO source " not in output, output
            if mode == "selectors":
                # Every parse-time selector change invalidates acquisition,
                # including inactive space selection not exposed by enumeration.
                assert output.count("OCIO source parse: ") == 7, output
