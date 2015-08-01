import sys
import jnupy

func2name = {}
name2func = {}

def register_func(module_name, func_name, func):
    if callable(value):
        try:
            hash(value)
        except TypeError:
            return
        
        func2name[value] = k2 = "{}.{}".format(module_name, func_name).encode()
        name2func[k2] = value
        
for name, module in jnupy.builtin_modules.items():
    for key in dir(module):
        value = getattr(module, key)
        register_func(name, key, value)

for name, module in jnupy.get_loaded_modules().items():
    if name not in ("sys",):
        continue
    
    for key in dir(module):
        value = getattr(module, key)
        register_func(name, key, value)

def dumper(obj):
    try:
        hash(obj)
    except TypeError:
        return b"<FAIL>"
    
    if obj in func2name:
        return func2name[obj]
    
    raise RuntimeError("failed to find original")

def loader(parser, buf):
    if buf in name2func:
        return name2func[buf]
    
    return ParseError("failed to find original object")

try:
    import upersist
    persister = upersist.Persister(dumper)

    obj = dict(hello=32, world=31, test=30)
    
    result = upersist.test(persister, obj)
    print("object:", obj)
    
    assert result.startswith(b"MP\x80\x01"), result[:4]
    magic, header, content = result.split(b"\n", 2)
    assert magic == b"MP\x80\x01"
    assert header == b'micropython persist v0.1'
    print(content)
except Exception as e:
    sys.print_exception(e)
    sys.exit(1)

SEEK_SET = 2

class ReadIO():
    def __init__(self, content=""):
        self.content = content
        self.pos = 0
    
    def read(self, length=None):
        start = self.pos
        last = len(self.content)
        if length is None:
            end = last
        else:
            end = min(start + length, last)

        self.pos = end
        return self.content[start:end]

    def readline(self):
        pos = self.tell()
        preline = self.read(1024)
        self.seek(pos, SEEK_SET)

        last = preline.find(b"\n")
        if last != -1:
            self.read(last)
            assert self.read(1) == b"\n"
            return preline[:last + 1]
        else:
            assert False, "too large line"

    def seek(self, pos, mode=None):
        assert mode == SEEK_SET
        self.pos = pos

    def tell(self):
        return self.pos

class FakeFunction():
    def __init__(self, name):
        assert name.endswith("_obj")
        self.name = name[:-len("_obj")]
        
    def __repr__(self):
        return "<function {}>".format(self.name)

class ParseError(RuntimeError):
    pass

class Parser():
    def __init__(self, fp):
        self.fp = fp
        self.last = len(fp.content)
        self.data = {}
        self.main_obj = None
        
    def parse(self):
        fp = self.fp
        header = fp.read(4)
        if header != b"MP\x80\x01":
            return False
        
        fp.readline() # extra info 0 \n
        fp.readline() # extra info 1 \n
        
        while fp.tell() < self.last:
            self.load()
        
        return self.main_obj
        
    def load(self):
        fp = self.fp
        pos = fp.tell()
        tag = fp.read(1)
        if not tag:
            return False
        
        obj = getattr(self, "load_" + tag.decode())()
        
        if tag not in b'CX':
            self.data[pos] = obj
        
        buf = fp.content[pos:fp.tell()]
        if tag in b"oO":
            ref = self.decode_int(buf[1:])
            print('#{}: {} -> #{}'.format(pos, buf, ref))
        else:
            print('#{}: {} -> {!r}'.format(pos, buf, obj))

        return obj

    def decode_int(self, encoded_size):
        # TODO: support endian!
        # TODO: fast method (currently slow)
        encoded_size = bytes(list(encoded_size)[::-1])
        return int.from_bytes(encoded_size)

    def load_size(self):
        fp = self.fp
        encoded_size_length = fp.read(1)
        assert encoded_size_length in b'1248'

        size_length = int(encoded_size_length.decode())
        encoded_size = fp.read(size_length)
        
        size = self.decode_int(encoded_size)

        assert size < (self.last - fp.tell()), ("too large size", size, self.last - fp.tell())
        return size

    def load_b(self):
        "bytes"
        size = self.load_size()
        return self.fp.read(size)

    def load_s(self):
        "str"
        size = self.load_size()
        return self.fp.read(size).decode()

    def load_q(self):
        "qstr"
        return self.load_s()
    
    def load_t(self):
        "tuple"
        size = self.load_size()
        result = []
        for i in range(size):
            result.append(self.load())
        return tuple(result)

    def load_l(self):
        "list"
        size = self.load_size()
        result = []
        for i in range(size):
            result.append(self.load())
        return result

    def load_d(self):
        "dict"
        size = self.load_size()
        result = {}
        for i in range(size):
            key = self.load()
            value = self.load()
            result[key] = value
        return result
    
    def load_i(self):
        "int"
        # TODO: signed int?

        fp = self.fp
        encoded_size_length = fp.read(1)
        assert encoded_size_length in b'1248'

        num_length = int(encoded_size_length.decode())
        encoded_num = fp.read(num_length)
        
        num = self.decode_int(encoded_num)
        return num
    
    def load_S(self):
        "small int (with 4 bytes encoded)"
        encoded_num = self.fp.read(4)
        num = self.decode_int(encoded_num)
        return num
    
    def load_object(self, size):
        assert 1 <= size <= 4
        encoded_pos = self.fp.read(size)
        pos = self.decode_int(encoded_pos)
        if pos not in self.data:
            print(encoded_pos, pos)
        return self.data[pos] 
    
    def load_O(self):
        "#object (with 2 bytes encoded)"
        return self.load_object(2)

    def load_Q(self):
        "#object (with 4 bytes encoded)"
        return self.load_object(4)
        
    def load_E(self):
        "error (only debugging)"
        size = self.fp.read(1)
        assert size == b":"

        pos = self.fp.tell()
        encoded_message = self.fp.read(1024)
        encoded_message = encoded_message.partition(b'\0')[0]
        self.fp.seek(pos + len(encoded_message) + 1, SEEK_SET)

        message = encoded_message.decode()

        return ParseError(message)
    
    def load_X(self):
        raise NotImplementedError
    
    def load_C(self):
        subtag = self.fp.read(1)
        if subtag == b"N":
            return None
        elif subtag == b"T":
            return True
        elif subtag == b"F":
            return False
        else:
            assert False, "unexcepted subtag: %r" % (subtag,)
    
    def load_U(self):
        buf = self.load_b()
        return loader(self, buf)
    
    def load_M(self):
        "Main object"
        obj = self.load()
        self.main_obj = obj
        return obj

parser = Parser(ReadIO(result))

try:
    obj = parser.parse()
except Exception as e:
    sys.print_exception(e)
else:
    print("object (restored):", obj)