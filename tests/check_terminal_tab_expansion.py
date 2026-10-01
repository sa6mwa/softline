"""Kernel-expanded tabs must become native terminal tabs on an isolated PTY."""

import sys

sys.dont_write_bytecode = True
import check_terminal_native_output as native


def main():
    fixture, build = sys.argv[1:]
    for source in ("\tX", "a\tX", "a\t\tX"):
        expected = native.render(fixture, source, 0, False, False)
        for chunk in (1, len(source)):
            actual = native.render(fixture, source, chunk, False, False,
                                   expand_tabs=True)
            assert actual == expected, (source, chunk, expected, actual)
        for prefilled in (False, True):
            native.handoff_resize_case(
                fixture, build, source, prefilled,
                # Tab bytes and height changes remain supported. Rebuilding
                # tab spans on width resize was explicitly removed.
                dimensions=((40, 12), (40, 8)),
                expand_tabs=True)
    print("TAB3: native tab cells and resize continuation match direct VTE output.")


if __name__ == "__main__":
    main()
