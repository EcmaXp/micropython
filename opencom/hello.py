import upersist
persister = upersist.Persister()

obj = (b"world", b"hello", ())


result = upersist.test(persister, obj)
print("object:", obj)

assert result.startswith(b"MP\x80\x01"), result[:4]
magic, header, content = result.split(b"\n", 2)
assert magic == b"MP\x80\x01"
assert header == b'micropython persist v0.1'
print(content)
