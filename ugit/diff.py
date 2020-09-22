import subprocess

from collections import defaultdict
from tempfile import NamedTemporaryFile as Temp

import data


def diff_trees(t_from, t_to):
    output = b""

    for path, o_from, o_to in _compare_trees(t_from, t_to):
        if o_from != o_to:
            output += diff_blobs(o_from, o_to, path)

    return output


def diff_blobs(o_from, o_to, path="blob"):
    with Temp() as f_from, Temp() as f_to:
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


def merge_trees(t_base, t_head, t_other):
    tree = {}

    for path, o_base, o_head, o_other in _compare_trees(t_base, t_head, t_other):
        tree[path] = data.hash_object(merge_blobs(o_base, o_head, o_other))

    return tree


def merge_blobs(o_base, o_head, o_other):
    with Temp() as f_base, Temp() as f_head, Temp() as f_other:
        for oid, f in ((o_base, f_base), (o_head, f_head), (o_other, f_other)):
            if oid:
                f.write(data.get_object(oid))
                f.flush()

        with subprocess.Popen([
            "diff3", "-m",
            "-L", "HEAD", f_head.name,
            "-L", "BASE", f_base.name,
            "-L", "MERGE_HEAD", f_other.name,
        ], stdout=subprocess.PIPE) as proc:
            output, _ = proc.communicate()
            assert proc.returncode in (0, 1)

        return output


def _compare_trees(*trees):
    entries = defaultdict(lambda: [None] * len(trees))

    for i, tree in enumerate(trees):
        for path, oid in tree.items():
            entries[path][i] = oid

    for path, oids in entries.items():
        yield path, *oids
