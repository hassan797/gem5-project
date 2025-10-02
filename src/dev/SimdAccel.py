import os
import sys
import importlib.util

# Path to the real SimObject implementation file (adjust relative path if needed)
_here = os.path.dirname(__file__)
_impl_path = os.path.normpath(os.path.join(_here, '..', 'python', 'm5', 'objects', 'SimdAccel.py'))

_spec = importlib.util.spec_from_file_location("m5.objects._SimdAccel_impl", _impl_path)
_module = importlib.util.module_from_spec(_spec)
sys.modules[_spec.name] = _module
_spec.loader.exec_module(_module)

# Expose the SimObject class and its generated params to SCons
SimdAccel = getattr(_module, "SimdAccel")
_params = getattr(SimdAccel, "_params")

__all__ = ["SimdAccel", "_params"]