import umicrothread
from umicrothread import MicroThread as _MicroThread
from umicrothread import *

assert umicrothread._init()

assert STATUS_NORMAL
assert STATUS_YIELD
assert STATUS_EXCEPTION
assert STATUS_LIMIT
assert STATUS_PAUSE
assert STATUS_FORCE_PAUSE
assert STATUS_STOP
assert LIMIT_SOFT
assert LIMIT_HARD


def auto(*args, **kwargs):
    def warp(func):
        return MicroThread(func.__name__, func, *args, **kwargs)
    return warp
    
_current_thread = None

def current_thread():
    return _current_thread

class MicroThread():
    def __init__(self, name, function, *args, **kwargs):
        self._thread = _MicroThread(name, function, *args, **kwargs)
        
    def __repr__(self):
        return "<%s name=%r, function=%r>" % (type(self).__name__, self.name, self.function)
    
    def __dir__(self):
        return dir(self._thread)
    
    # __setattr__ are not exists.

    @property
    def name(self):
        return self._thread.name
    
    @property
    def function(self):
        return self._thread.function
    
    @property
    def cpu_hard_limit(self):
        return self._thread.cpu_hard_limit
    
    @cpu_hard_limit.setter
    def cpu_hard_limit(self, value):
        self._thread.cpu_hard_limit = value
    
    @property
    def cpu_soft_limit(self):
        return self._thread.cpu_soft_limit
    
    @cpu_soft_limit.setter
    def cpu_soft_limit(self, value):
        self._thread.cpu_soft_limit = value
    
    @property
    def cpu_current_executed(self):
        return self._thread.cpu_current_executed
    
    @cpu_current_executed.setter
    def cpu_current_executed(self, value):
        self._thread.cpu_current_executed = value
    
    # del cpu_* value will set zero and unlimited.
    # assign with zero is not allowed?
    
    def __call__(self, value=None):
        return self.resume(value)
    
    def resume(self, value=None):
        global _current_thread
        thread = self._thread

        try:
            _current_thread = thread
            status, result = thread.resume(value)
        finally:
            _current_thread = None
        
        return status, result
