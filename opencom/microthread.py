import umicrothread as _microthread
from umicrothread import pause, current_thread as _current_thread, \
    MicroThread as _MicroThread
from umicrothread import STATUS_NORMAL, STATUS_YIELD, STATUS_EXCEPTION, \
    STATUS_LIMIT, STATUS_PAUSE, STATUS_FORCE_PAUSE, STATUS_STOP, \
    STATUS_RUNNING, LIMIT_SOFT, LIMIT_HARD

def auto(*args, **kwargs):
    def warp(func):
        return MicroThread(func.__name__, func, *args, **kwargs)
    return warp

_active = None

def current_thread():
    raw_thread = _current_thread()
    thread = _active

    if raw_thread is None:
        assert _active is None
    else:
        assert thread._thread is raw_thread

    return thread

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
    def cpu_safe_limit(self):
        return self._thread.cpu_safe_limit
    
    @cpu_safe_limit.setter
    def cpu_safe_limit(self, value):
        self._thread.cpu_safe_limit = value
    
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
        global _active
        prev_thread = _active
        thread = self._thread

        try:
            _active = self
            status, result = thread.resume(value)
        finally:
            _active = prev_thread
        
        return status, result
