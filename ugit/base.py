import itertools
import operator
import os
import string

from collections import deque, namedtuple

import data


Commit = namedtuple("Commit", ["tree", "parent", "message"])


def init():
    data.init()
    data.update_ref("HEAD", data.RefValue(symbolic=True, value="refs/heads/master"))


def write_tree(directory=os.getcwd()):
    entries = []

    with os.scandir(directory) as d:
        for entry in d:
            name = f"{directory}/{entry.name}"

            if _is_ignored(name):
                continue

            if entry.is_file(follow_symlinks=False):
                type = "blob"
                with open(name, "rb") as f:
                    oid = data.hash_object(f.read())
            elif entry.is_dir(follow_symlinks=False):
                type = "tree"
                oid = write_tree(name)

            entries.append((entry.name, oid, type))

    tree = "".join(
        f"{type} {oid} {name}\n"
        for name, oid, type
        in sorted(entries)
    )

    return data.hash_object(tree.encode(), "tree")


def read_tree(tree_oid):
    _empty_current_directory()

    for path, oid in _get_tree(tree_oid, os.getcwd()).items():
        os.makedirs(os.path.dirname(path), exist_ok=True)

        with open(path, "wb") as f:
            f.write(data.get_object(oid))


def commit(message):
    commit = f"tree {write_tree()}\n"

    head = data.get_ref("HEAD").value
    if head:
        commit += f"parent {head}\n"

    commit += "\n"
    commit += f"{message}\n"

    oid = data.hash_object(commit.encode(), "commit")
    data.update_ref("HEAD", data.RefValue(symbolic=False, value=oid))
    return oid


def get_commit(oid):
    parent = None

    commit = data.get_object(oid, "commit").decode()
    lines = iter(commit.splitlines())

    for line in itertools.takewhile(operator.truth, lines):
        key, value = line.split(" ", 1)

        if key == "tree":
            tree = value
        elif key == "parent":
            parent = value
        else:
            assert False, f"Unknown field {key}"

    message = "\n".join(lines)
    return Commit(tree=tree, parent=parent, message=message)


def get_oid(name):
    if name == "@":
        name = "HEAD"

    refs_to_try = [
        f"{name}",
        f"refs/{name}",
        f"refs/tags/{name}",
        f"refs/heads/{name}",
    ]

    for ref in refs_to_try:
        if data.get_ref(ref, deref=False).value:
            return data.get_ref(ref).value

    if len(name) == 40 and all(c in string.hexdigits for c in name):
        return name

    assert False, f"Unknown name {name}"


def iter_commits_and_parents(oids):
    oids = deque(oids)
    visited = set()

    while oids:
        oid = oids.popleft()

        if not oid or oid in visited:
            continue

        visited.add(oid)
        yield oid

        commit = get_commit(oid)
        oids.appendleft(commit.parent)


def _is_ignored(path):
    return ".ugit" in path.split("/")


def _get_tree(oid, base_path=""):
    result = {}

    for type, oid, name in _iter_tree_entries(oid):
        assert "/" not in name
        assert name not in [".", ".."]

        path = os.path.join(base_path, name)

        if type == "blob":
            result[path] = oid
        elif type == "tree":
            result.update(_get_tree(oid, f"{path}/"))
        else:
            assert False, f"Unknown tree entry {type}"

    return result


def _iter_tree_entries(oid):
    if not oid:
        return

    tree = data.get_object(oid, "tree")

    for entry in tree.decode().splitlines():
        yield entry.split(" ", 2)


def _empty_current_directory():
    cwd = os.getcwd()

    for root, _, filenames in os.walk(cwd):
        for filename in filenames:
            path = os.path.relpath(f"{root}/{filename}")

            if _is_ignored(path) or not os.path.isfile(path):
                continue

            os.remove(path)

        if _is_ignored(root) or os.path.samefile(root, cwd):
            continue

        os.rmdir(root)


def checkout(name):
    oid = get_oid(name)
    commit = get_commit(oid)
    read_tree(commit.tree)

    if _is_branch(name):
        head = data.RefValue(symbolic=True, value=f"refs/heads/{name}")
    else:
        head = data.RefValue(symbolic=False, value=oid)

    data.update_ref("HEAD", head, deref=False)


def create_tag(name, oid):
    data.update_ref(f"refs/tags/{name}", data.RefValue(symbolic=False, value=oid))


def create_branch(name, oid):
    data.update_ref(f"refs/heads/{name}", data.RefValue(symbolic=False, value=oid))


def get_branch_name():
    head = data.get_ref("HEAD", deref=False)
    if not head.symbolic:
        return None

    head = head.value
    assert head.startswith("refs/heads/")
    return os.path.relpath(head, "refs/heads")


def iter_branch_names():
    for name, _ in data.iter_refs("refs/heads"):
        yield os.path.relpath(name, "refs/heads")


def reset(oid):
    data.update_ref("HEAD", data.RefValue(symbolic=False, value=oid))


def _is_branch(name):
    return data.get_ref(f"refs/heads/{name}").value is not None
