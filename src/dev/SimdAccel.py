import os
import sys
import importlib.util

# compute folder robustly (some import contexts don't define __file__)
try:
    _here = os.path.dirname(__file__)
except NameError:
    _here = os.getcwd()

_impl_path = os.path.normpath(os.path.join(_here, '..', 'python', 'm5', 'objects', 'SimdAccel.py'))

_spec = importlib.util.spec_from_file_location("m5.objects._SimdAccel_impl", _impl_path)
_module = importlib.util.module_from_spec(_spec)
sys.modules[_spec.name] = _module
_spec.loader.exec_module(_module)

SimdAccel = getattr(_module, "SimdAccel")
_params = getattr(SimdAccel, "_params")

__all__ = ["SimdAccel", "_params"]