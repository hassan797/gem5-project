import os
import sys
import importlib.util

# compute folder robustly (some import contexts don't define __file__)
try:
    _here = os.path.dirname(__file__)
except NameError:
    _here = os.getcwd()

_here = os.path.abspath(_here)

# Search upward for the repository root and the real SimdAccel implementation.
_impl_path = None
search_dir = _here
while True:
    # Try common locations relative to a repo root
    candidates = [
        os.path.join(search_dir, "src", "python", "m5", "objects", "SimdAccel.py"),
        os.path.join(search_dir, "python", "m5", "objects", "SimdAccel.py"),
    ]
    for c in candidates:
        if os.path.exists(c):
            _impl_path = c
            break
    if _impl_path:
        break
    parent = os.path.dirname(search_dir)
    if parent == search_dir:
        break
    search_dir = parent

if not _impl_path:
    raise FileNotFoundError(
        "SimdAccel implementation not found; looked from %s upward" % _here
    )

_spec = importlib.util.spec_from_file_location("m5.objects._SimdAccel_impl", _impl_path)
_module = importlib.util.module_from_spec(_spec)
sys.modules[_spec.name] = _module
_spec.loader.exec_module(_module)

SimdAccel = getattr(_module, "SimdAccel")
_params = getattr(SimdAccel, "_params")

__all__ = ["SimdAccel", "_params"]