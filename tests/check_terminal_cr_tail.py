"""Diagnostic for the accepted opaque-feed resize-positioning limitation."""

import sys

sys.dont_write_bytecode = True
import check_terminal_native_output as native


def main():
    assert len(sys.argv) == 4 and sys.argv[1] == '--diagnostic', (
        'usage: check_terminal_cr_tail.py --diagnostic FIXTURE BUILD')
    fixture, build = sys.argv[2:]
    for source in ('a' * 27 + '中中\r' + 'b' * 28,
                   'a' * 29 + '中\r' + 'b' * 29,
                   'a' * 27 + '中中\r' + 'b' * 30):
        for prefilled in (False, True):
            native.handoff_resize_case(
                fixture, build, source, prefilled,
                dimensions=((30, 12), (40, 12), (30, 12), (40, 12)))
    print("CR tails: pre-wrap gap cursors continue at the direct VTE endpoint.")


if __name__ == '__main__':
    main()
