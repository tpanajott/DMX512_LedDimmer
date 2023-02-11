#!/usr/bin/env python
import subprocess

current_version_hash_short = subprocess.check_output(['git', 'rev-parse', '--short', 'HEAD']).decode('ascii').strip()

print ("Writing version " + current_version_hash_short + " to file include/version.h");

with open("include/version.h", "w") as f:
    f.write("#define DMX512_SW_VERSION \"" + current_version_hash_short + "\"")