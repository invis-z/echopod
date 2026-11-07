import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from tide import bookmarks, sync

COMMANDS = {"sync": sync.main, "bookmarks": bookmarks.main}
USAGE = """usage: tide COMMAND [options]

  tide sync [CARD]       build the library and album covers for the card on this computer
  tide bookmarks CARD    list the audiobook and podcast bookmarks on the card

Without a command, tide runs sync. Add --help after a command for its options."""


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    if argv and argv[0] in ("-h", "--help"):
        print(USAGE)
        return 0
    if argv and argv[0] in COMMANDS:
        return COMMANDS[argv[0]](argv[1:])
    return sync.main(argv)


if __name__ == "__main__":
    sys.exit(main())
