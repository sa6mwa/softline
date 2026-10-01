"""Opaque feed bytes and terminal-owned static reflow agree with direct VTE."""

import sys

sys.dont_write_bytecode = True
import check_terminal_native_output as native


def main():
    fixture, build = sys.argv[1:]
    sources = ('a' * 29 + '🄀中X', 'a' * 29 + '中🏳X',
               'a' * 27 + '中中\r' + 'b' * 28,
               '\t\t\tX', 'é👩‍💻🇸🇪中')
    for source in sources:
        for grow in (False, True):
            expected = native.render(fixture, source, 0, False, grow)
            for chunk in (1, len(source.encode())):
                actual = native.render(fixture, source, chunk, False, grow)
                assert actual == expected, (source, chunk, grow, actual, expected)
    print('Opaque feed: producer bytes and static reflow match direct VTE.')


if __name__ == '__main__':
    main()
