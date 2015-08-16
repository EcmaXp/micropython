import sys

try:
    import jnupy
except ImportError:
    jnupy = None

if jnupy:
    func2name = {}
    name2func = {}
    
    x = {"a":32}
    exec("""
def hello():
    if callable(value):
        try:
            hash(value)
        except TypeError:
            return
        
        func2name[value] = k2 = "{}.{}".format(module_name, func_name).encode()
        name2func[k2] = value
    """, x, x)
    #func = x.pop('hello')
    
    obj = (x, "EOF")
    
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
else:
    def loader(parser, buf):
        return ParseError("failed to find original object (not jnupy)")

    result = b'MP\x80\x01\nmicropython persist v0.1\nq1\x05helloq1\x08<string>q1\x01ad1\x01O\x001S\x00\x00\x00 FO\x005\x03\x00\x00\x00\x00\x00\x00\x81\xc4\x04\xfe`\x00\x00\x00\x001\x81\x10\x84$\x824[,#ZG:\x00\x00\x00\x00\x00\xa2\x08\x00\x00\x00\x00\x00\x00\xa6\x08\x00\x00\x00\x00\x00\x00\xc6\x06\x00\x00\x00\x00\x00\x00\x0c\x01\xff\x1dp\x00\x1d\x81*\x00d\x017H\x80?\x0f\x00\x1d\x81\x05\x00\x1d\x81*\x00d\x012D5\x12\x800\x1dV\x00\xf77\t\x80222\x11[E5\x01\x80A\x16\x84+\x1f\x81M\xb0\xb1f\x02\x1f\x826f\x000\x1d\x84!\x00\x1d\x81*\x00\'\xc3\x1d\x81*\x00\x1d\x84"\x00\xb3\'\x11[Ebytecode\x000O\x00\x1eO\x00&1\x00\x00\x0c\x00\x011\x001\x001V\x1dp\x00\x1d\x81*\x00d\x017H\x80?\x0f\x00\x1d\x81\x05\x00\x1d\x81*\x00d\x012D5\x12\x800\x1dV\x00\xf77\t\x80222\x11[E5\x01\x80A\x16\x84+\x1f\x81M\xb0\xb1f\x02\x1f\x826f\x000\x1d\x84!\x00\x1d\x81*\x00\'\xc3\x1d\x81*\x00\x1d\x84"\x00\xb3\'\x11[q1\x03EOFt1\x02O\x00@O\x01JMO\x01P'

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

class _FakeFunction():
    def __init__(self, name):
        assert name.endswith("_obj")
        self.name = name[:-len("_obj")]
        
    def __repr__(self):
        return "<function {}>".format(self.name)

class _FakeRefernce():
    def __init__(self, ref):
        self.ref = ref
        
    def __repr__(self):
        return "<refernce #{}>".format(self.ref)

class FunBC():
    "fun_bc"
    """
    def __init__(self, n_pos_args, n_kwonly_args, n_def_args, flags, extra_args):
        self.n_pos_args =
        self.n_kwonly_args =
        self.n_def_args =
        self.flags =
        self.extra_args =
        self.bytecode = 
    """

class _FakeBytecode():
    pass

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
        
        return self.load()

    def load(self):
        fp = self.fp
        pos = fp.tell()
        tag = fp.read(1).decode()
        if not tag:
            return False
        
        default_type = getattr(self, "default_" + tag, None)

        fakeref = obj = None
        if tag not in ('C', 'X'):
            if default_type is None:
                self.data[pos] = fakeref = _FakeRefernce(pos)
            else:
                self.data[pos] = obj = default_type()
        
        try:
            if default_type is None:
                obj = getattr(self, "load_" + tag)()
            else:
                obj = getattr(self, "load_" + tag)(obj)
        except Exception:
            buf = fp.content[pos:pos + 32]
            print('#ERR {}: {}...'.format(pos, buf))
            raise
        
        if tag not in ('C', 'X'):
            self.data[pos] = obj
            if fakeref is not None:
                fakeref.obj = obj
        
        # TODO: remove cycle reference. (and use own safe repr library?)
        try:
            repr(obj)
        except Exception:
            obj = _FakeRefernce("{}!".format(pos))
        
        buf = fp.content[pos:fp.tell()]
        if tag in ('o', 'O'):
            ref = self.decode_int(buf[1:])
            print('#{}: {} -> #{}'.format(pos, buf, ref))
        else:
            print('#{}: {} -> {!r}'.format(pos, buf, obj))

        return obj

    def decode_int(self, encoded_size):
        # TODO: support endian!
        # TODO: fast method (currently slow)
        encoded_size = bytes(list(encoded_size)[::-1])
        if jnupy:
            return int.from_bytes(encoded_size)
        else:
            return int.from_bytes(encoded_size, "little")

    def load_size(self):
        fp = self.fp
        size = self.load_i()

        # TODO: check by typed?
        typesize = 1
        assert size * typesize <= (self.last - fp.tell()), ("too large size", size, self.last - fp.tell())

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

    default_d = dict
    def load_d(self, result):
        "dict"
        size = self.load_size()
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
        assert encoded_size_length in b'01248'

        if encoded_size_length == b'0':
            return 0

        num_length = int(encoded_size_length.decode())
        encoded_num = fp.read(num_length)
        
        num = self.decode_int(encoded_num)
        return num
    
    def load_S(self):
        "small int (with 4 bytes encoded)"
        encoded_num = self.fp.read(4)
        num = self.decode_int(encoded_num)
        return num
    
    def load_F(self):
        "function (fun_bc)"
        load_int = lambda n: self.decode_int(self.fp.read(n // 8))
        
        global_dict = self.load()
        n_pos_args = load_int(8)
        n_kwonly_args = load_int(8)
        n_def_args = load_int(8)
        flags = load_int(8)
        extra_args = load_int(32)
        bytecode = self.load()
        
        print("== func ==")
        print("n_pos_args", n_pos_args)
        print("n_kwonly_args", n_kwonly_args)
        print("n_def_args", n_def_args)
        print("flags", flags)
        print("extra_args", extra_args)
        print("block_name", bytecode.block_name)
        print("source_file", bytecode.source_file)
        print("arg_names", bytecode.arg_names)
        print("n_state", bytecode.n_state)
        print("n_exc_stack", bytecode.n_exc_stack)
        print("local_nums", bytecode.local_nums)
        print("lineno_info", bytecode.lineno_info)
        print("body", bytecode.body)
        print("==")
        
        return "fake function with bytecode {}"
        
    def load_bytecode(self):
        "bytecode (will used for fun_bc)"
        load_int = lambda n: self.decode_int(self.fp.read(n // 8))
        skip = lambda n: self.read(n) and None
        version = self.fp.read(1)
        assert version == b'0'
        
        bc = _FakeBytecode()
        bc.block_name = self.load()
        bc.source_file = self.load()

        bc.arg_names = arg_names = []
        for i in range(self.load_size()):
            arg_names.append(self.load())
        
        bc.n_state = load_int(16)
        bc.n_exc_stack = load_int(16)

        bc.local_nums = local_nums = []
        for i in range(self.load_size()):
            local_nums.append(load_int(32))
        
        bc.lineno_info = self.load_b()
        bc.body = self.load_b()
        
        return bc
    
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
    
    def load_E(self):
        "extended object"
        pos = self.fp.tell()
        encoded_tag = self.fp.read(64)
        encoded_tag = encoded_tag.partition(b'\0')[0]
        self.fp.seek(pos + len(encoded_tag) + 1, SEEK_SET)
        
        tag = encoded_tag.decode()
        assert len(tag) > 1
        obj = getattr(self, "load_" + tag)()
        return obj

try:
    import time
except ImportError:
    import utime as time

parser = Parser(ReadIO(result))

try:
    obj = parser.parse()
except Exception as e:
    try:
        sys.print_exception(e)
    except AttributeError:
        raise e
else:
    print("persist object length:", len(result), 'bytes')
    print("object (restored):", obj)