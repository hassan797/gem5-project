from m5.objects import SimdAccel

# This file is a small wrapper so the build system (SCons) can find
# the SimObject at src/dev/SimdAccel.py as expected by src/dev/SConscript.
# The real class implementation lives in src/python/m5/objects/SimdAccel.py

# Expose the generated params object at module level so the
# sim-object parameter generator can find it.
_params = SimdAccel._params

# optional export list
__all__ = ["SimdAccel", "_params"]