"""Build with a case-normalized environment for Windows MSBuild.

Some launchers supply both Path and PATH. MSBuild's CL task rejects that environment,
even though Windows treats those names as equivalent. Python's Windows environment
mapping normalizes names when passed explicitly to the child process.
"""

import os
import subprocess
import sys


if os.name != "nt":
    raise SystemExit("This build helper is for Windows only")

raise SystemExit(subprocess.call(["cmake", "--build", *sys.argv[1:]], env=dict(os.environ)))
