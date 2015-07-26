import upersist
persister = upersist.Persister()
for tag, content in zip(("obj", "ptr"), upersist.test(persister, "hello")):
    print(tag)
    print(content)
    print()
