#!/usr/bin/env python

# Copyright Contributors to the OpenImageIO project.
# SPDX-License-Identifier: Apache-2.0
# https://github.com/AcademySoftwareFoundation/OpenImageIO

# The line counting and file comparison this test needs, in Python rather than
# `grep`, `tr` and `cmp` behind a shell. The patterns carry `<` and `|`, which
# the Windows shell reads as redirection and pipe inside the single quotes a
# POSIX shell would have honored, so the quoting -- not the test -- would have
# decided the answer there.
#
#   count <regex> <file>   the number of lines matching, as `grep -Ec` counts
#   same <a> <b> <label>   `label:yes` or `label:no`, as `cmp -s` decides
#   newlines <file>        the number of newlines ending the file
#   manual <file> <app>    run the manual's example commands as written

import re
import shlex
import subprocess
import sys

mode = sys.argv[1]
if mode == "count":
    pattern = re.compile(sys.argv[2])
    with open(sys.argv[3], "r", errors="replace") as f:
        print(sum(1 for line in f if pattern.search(line)))
elif mode == "same":
    with open(sys.argv[2], "rb") as a, open(sys.argv[3], "rb") as b:
        same = a.read() == b.read()
    print("{}:{}".format(sys.argv[4], "yes" if same else "no"))
elif mode == "newlines":
    with open(sys.argv[2], "rb") as f:
        text = f.read().replace(b"\r\n", b"\n")
    print("newlines:", len(text) - len(text.rstrip(b"\n")))
elif mode == "manual":
    # The commands the manual displays, run exactly as it displays them. No
    # shell sees them: one of them names an OpenColorIO context variable, and
    # whether `'$PLATE'` arrives as `$PLATE` would otherwise be the shell's
    # answer rather than this test's. What each command writes is
    # OpenColorIO's and moves with its version, so only the count is reported;
    # a command that fails prints what it said.
    app = shlex.split(sys.argv[3])
    with open(sys.argv[2], "r") as f:
        commands = [line.strip() for line in f]
    commands = [c for c in commands if c and not c.startswith("#")]
    ran = 0
    for command in commands:
        words = shlex.split(command)
        result = subprocess.run(app + words[1:], stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT)
        if result.returncode == 0 and result.stdout.strip():
            ran += 1
        else:
            print(command, "->", result.stdout.decode(errors="replace"))
    print("manual examples: {} of {}".format(ran, len(commands)))
else:
    sys.exit("unknown mode " + mode)
