"""`python -m umcub`, or `python <umcub>/tools/host/umcub` without installing the package
(e.g. an IDE post-build step: python <umcub>/tools/host/umcub image sign ...)."""
import os
import sys

if not __package__:
    # Run as a directory: put tools/host (not this package's directory, whose
    # modules would shadow libraries such as `smp`) first on the path.
    sys.path[0] = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    from umcub.cli import main
else:
    from .cli import main

sys.exit(main())
