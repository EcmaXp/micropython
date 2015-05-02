import umicrothread
from umicrothread import MicroThread as _MicroThread, pause

# TODO: check this const is correct
_MP_VM_RETURN_NORMAL = 0
_MP_VM_RETURN_YIELD = 1
_MP_VM_RETURN_EXCEPTION = 2
_MP_VM_RETURN_PAUSE = 3
_MP_VM_RETURN_FORCE_PAUSE = 4

RETURN_NORMAL = "return"
RETURN_YIELD = "yield"
RETURN_EXCEPTION = "exception"
RETURN_PAUSE = "pause"
RETURN_FORCE_PAUSE = "force_pause"

_result_map = {
    _MP_VM_RETURN_NORMAL: RETURN_NORMAL,
    _MP_VM_RETURN_YIELD: RETURN_YIELD,
    _MP_VM_RETURN_EXCEPTION: RETURN_EXCEPTION,
    _MP_VM_RETURN_PAUSE: RETURN_PAUSE,
    _MP_VM_RETURN_FORCE_PAUSE: RETURN_FORCE_PAUSE,
}

assert umicrothread.init()

def auto(func):
    return MicroThread(func.__name__, func)

_current_thread = None

def current_thread():
    return _current_thread


INVAILD = object()


class MicroThread():
    def __init__(self, name, function):
        self._thread = _MicroThread(name, function)
        
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
    
    def __call__(self, value=INVAILD):
        return self.resume(value)
    
    def resume(self, value=INVAILD):
        global _current_thread
        thread = self._thread
        
        if value is INVAILD:
            thread.send_value = None
        else:
            thread.send_value = value

        try:
            _current_thread = thread
            thread.resume()
        finally:
            _current_thread = None
        
        kind = thread.last_kind
        result = thread.last_result
        
        kind_of_result = _result_map[kind]
        return kind_of_result, result
