import subprocess

from collections import defaultdict
from tempfile import NamedTemporaryFile

import data


def diff_trees(t_from, t_to):
    output = b""

    for path, o_from, o_to in _compare_trees(t_from, t_to):
        if o_from != o_to:
            output += diff_blobs(o_from, o_to, path)

    return output


def diff_blobs(o_from, o_to, path="blob"):
    with NamedTemporaryFile() as f_from, NamedTemporaryFile() as f_to:
        for oid, f in ((o_from, f_from), (o_to, f_to)):
            if oid:
                f.write(data.get_object(oid))
                f.flush()

        with subprocess.Popen([
            "diff", "--unified", "--show-c-function",
            "--label", f"a/{path}", f_from.name,
            "--label", f"b/{path}", f_to.name,
        ], stdout=subprocess.PIPE) as proc:
            output, _ = proc.communicate()

        return output


def iter_changed_files(t_from, t_to):
    for path, o_from, o_to in _compare_trees(t_from, t_to):
        if o_from != o_to:
            action = (
                "new file" if not o_from else
                "deleted" if not o_to else
                "modified"
            )

            yield path, action


def merge_trees(t_head, t_other):
    tree = {}

    for path, o_head, o_other in _compare_trees(t_head, t_other):
        tree[path] = merge_blobs(o_head, o_other)

    return tree


def merge_blobs(o_head, o_other):
    with NamedTemporaryFile() as f_head, NamedTemporaryFile() as f_other:
        for oid, f in ((o_head, f_head), (o_other, f_other)):
            if oid:
                f.write(data.get_object(oid))
                f.flush()

        with subprocess.Popen([
            "diff", "-DHEAD",
            f_head.name, f_other.name,
        ], stdout=subprocess.PIPE) as proc:
            output, _ = proc.communicate()

        return output


def _compare_trees(*trees):
    entries = defaultdict(lambda: [None] * len(trees))

    for i, tree in enumerate(trees):
        for path, oid in tree.items():
            entries[path][i] = oid

    for path, oids in entries.items():
        yield path, *oids
