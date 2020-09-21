import difflib

from collections import defaultdict

import data


def diff_trees(t_from, t_to):
    output = ""

    for path, o_from, o_to in _compare_trees(t_from, t_to):
        if o_from != o_to:
            output += diff_blobs(o_from, o_to, path)

    return output


def diff_blobs(o_from, o_to, path="blob"):
    c_from = data.get_object(o_from).decode()
    c_to = data.get_object(o_to).decode()

    return "".join(difflib.unified_diff(
        c_from.splitlines(keepends=True),
        c_to.splitlines(keepends=True),
        fromfile=f"a/{path}",
        tofile=f"b/{path}"
    ))


def iter_changed_files(t_from, t_to):
    for path, o_from, o_to in _compare_trees(t_from, t_to):
        if o_from != o_to:
            action = (
                "new file" if not o_from else
                "deleted" if not o_to else
                "modified"
            )

            yield path, action


def _compare_trees(*trees):
    entries = defaultdict(lambda: [None] * len(trees))

    for i, tree in enumerate(trees):
        for path, oid in tree.items():
            entries[path][i] = oid

    for path, oids in entries.items():
        yield path, *oids
