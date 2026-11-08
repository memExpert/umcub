"""Board names: a user's catalog (JSON) of the board type ids devices report, shown wherever a board type is."""
import json

from .errors import UmcubError

_names = {}


def load_board_names(*paths):
    """Replace the catalog with these JSON files merged (a later file wins), e.g. {"0x00010002": "Pump controller"};
    no paths empties it. Returns {board_type: name}. A bad file raises and keeps the catalog as it was."""
    names = {}
    for path in paths:
        try:
            text = open(path, encoding="utf-8").read()
        except OSError as e:
            raise UmcubError(code="file.read", path=str(path), reason=e.strerror) from e
        try:
            data = json.loads(text)
            if not isinstance(data, dict):
                raise ValueError("not a JSON object")
            for key, name in data.items():
                if not isinstance(name, str):
                    raise ValueError(f"{key}: the name is not a string")
                names[int(key, 0)] = name
        except ValueError as e:
            raise UmcubError(code="names.invalid", path=str(path), reason=str(e)) from e
    _names.clear()
    _names.update(names)
    return dict(names)


def board_label(board_type):
    """"0x00010002 (Pump controller)", or the hex alone for a board type not in the catalog."""
    name = _names.get(board_type)
    return f"0x{board_type:08x}" + (f" ({name})" if name else "")
