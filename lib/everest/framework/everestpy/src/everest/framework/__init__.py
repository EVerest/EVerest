__version__ = '0.26.0'

try:
    from .everestpy import *
except ImportError:
    from everestpy import *
