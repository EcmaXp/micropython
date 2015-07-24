__all__ = ["interact"]

import jnupy
import sys

def input(prompt=None):
    if prompt is not None:
        print(prompt, end="")

    result = jnupy.input()
    if result is None:
        raise EOFError
    
    return result

def mp_repl_continue_with_input(line):
    # check for blank input
    if not line:
        return False
    
    # check for escape char in terminal
    if "\x1b" in line:
        return False
    
    # check if input starts with a certain keyword
    starts_with_compound_keyword = False
    for keyword in "@", "if", "while", "for", "try", "with", "def", "class":
        starts_with_compound_keyword = starts_with_compound_keyword or line.startswith(keyword)
    
    # check for unmatched open bracket or triple quote
    # TODO don't look at triple quotes inside single quotes
    n_paren = n_brack = n_brace = 0
    in_triple_quote = 0
    passed = 0
    for charno, char in enumerate(line):
        if passed:
            passed -= 1
            continue
        elif char == '(': n_paren += 1
        elif char == ')': n_paren -= 1
        elif char == '[': n_brack += 1
        elif char == ']': n_brack -= 1
        elif char == '{': n_brace += 1
        elif char == '}': n_brace -= 1
        elif char == "'":
            if chr(in_triple_quote) != '"' and line[charno+1:charno+2] == line[charno+2:charno+3] == "'":
                passed += 2;
                in_triple_quote = ord("'") - in_triple_quote
        elif char == '"':
            if chr(in_triple_quote) != "'" and line[charno+1:charno+2] == line[charno+2:charno+3] == '"':
                passed += 2;
                in_triple_quote = ord('"') - in_triple_quote
    
    # continue if unmatched brackets or quotes
    if n_paren > 0 or n_brack > 0 or n_brace > 0 or in_triple_quote != 0:
        return True
    
    # continue if last character was backslash (for line continuation)
    if line.endswith('\\'):
        return True

    # continue if compound keyword and last line was not empty
    if starts_with_compound_keyword and not line.endswith('\n'):
        return True

    # otherwise, don't continue
    return False

def interact(banner=None, readfunc=None, local=None):
    if readfunc is None:
        readfunc = input

    if banner is None:
        banner = "Micro Python {} on {}; {} version".format(jnupy.get_version("MICROPY_GIT_TAG"), jnupy.get_version("MICROPY_BUILD_DATE"), sys.platform)

    if local is None:
        local = dict()

    print(banner)
    
    while True:
        try:
            code = readfunc(">>> ")
        except EOFError:
            print()
            continue

        while mp_repl_continue_with_input(code):
            try:
                code += "\n" + readfunc("... ") 
            except EOFError:
                print()
                continue
    
        try:
            fun = compile(code, "<stdin>", "single")
        except SyntaxError:
            sys.print_exception(sys.exc_info()[1])
            continue
        
        try:
            exec(fun, local, local)
        except SystemExit:
            raise
        except:
            sys.print_exception(sys.exc_info()[1])
