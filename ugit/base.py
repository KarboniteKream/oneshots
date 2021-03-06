import itertools
import operator
import os
import string

from collections import deque, namedtuple

import data
import diff


Commit = namedtuple("Commit", ["tree", "parents", "message"])


def init():
    data.init()
    data.update_ref("HEAD", data.RefValue(symbolic=True, value="refs/heads/master"))


def write_tree():
    index_as_tree = {}

    with data.get_index() as index:
        for path, oid in index.items():
            path = path.split("/")
            dirpath, filename = path[:-1], path[-1]

            current = index_as_tree
            for dirname in dirpath:
                current = current.setdefault(dirname, {})
            current[filename] = oid

    def write_tree_recursive(tree_dict):
        entries = []

        for name, value in tree_dict.items():
            if type(value) is dict:
                type_ = "tree"
                oid = write_tree_recursive(value)
            else:
                type_ = "blob"
                oid = value

            entries.append((name, oid, type_))

        tree = "".join(
            f"{type_} {oid} {name}\n"
            for name, oid, type_
            in sorted(entries)
        )

        return data.hash_object(tree.encode(), "tree")

    return write_tree_recursive(index_as_tree)


def read_tree(tree_oid, update_working=False):
    with data.get_index() as index:
        index.clear()
        index.update(get_tree(tree_oid))

        if update_working:
            _checkout_index(index)


def read_tree_merged(t_base, t_head, t_other, update_working=False):
    with data.get_index() as index:
        index.clear()
        index.update(diff.merge_trees(
            get_tree(t_base),
            get_tree(t_head),
            get_tree(t_other),
        ))

        if update_working:
            _checkout_index(index)


def commit(message):
    commit = f"tree {write_tree()}\n"

    head = data.get_ref("HEAD").value
    if head:
        commit += f"parent {head}\n"

    merge_head = data.get_ref("MERGE_HEAD").value
    if merge_head:
        commit += f"parent {merge_head}\n"
        data.delete_ref("MERGE_HEAD", deref=False)

    commit += "\n"
    commit += f"{message}\n"

    oid = data.hash_object(commit.encode(), "commit")
    data.update_ref("HEAD", data.RefValue(symbolic=False, value=oid))
    return oid


def get_commit(oid):
    parents = []

    commit = data.get_object(oid, "commit").decode()
    lines = iter(commit.splitlines())

    for line in itertools.takewhile(operator.truth, lines):
        key, value = line.split(" ", 1)

        if key == "tree":
            tree = value
        elif key == "parent":
            parents.append(value)
        else:
            assert False, f"Unknown field {key}"

    message = "\n".join(lines)
    return Commit(tree=tree, parents=parents, message=message)


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
        oids.extendleft(commit.parents[:1])
        oids.extend(commit.parents[1:])


def iter_objects_in_commits(oids):
    visited = set()

    def iter_objects_in_tree(oid):
        visited.add(oid)
        yield oid

        for type, oid, _ in _iter_tree_entries(oid):
            if oid not in visited:
                if type == "tree":
                    yield from iter_objects_in_tree(oid)
                else:
                    visited.add(oid)
                    yield oid

    for oid in iter_commits_and_parents(oids):
        yield oid
        commit = get_commit(oid)

        if commit.tree not in visited:
            yield from iter_objects_in_tree(commit.tree)


def get_tree(oid, base_path=""):
    result = {}

    for type, oid, name in _iter_tree_entries(oid):
        assert "/" not in name
        assert name not in [".", ".."]

        path = os.path.join(base_path, name)

        if type == "blob":
            result[path] = oid
        elif type == "tree":
            result.update(get_tree(oid, f"{path}/"))
        else:
            assert False, f"Unknown tree entry {type}"

    return result


def checkout(name):
    oid = get_oid(name)
    commit = get_commit(oid)
    read_tree(commit.tree, update_working=True)

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


def get_working_tree():
    result = {}

    for root, _, filenames in os.walk(os.getcwd()):
        for filename in filenames:
            path = os.path.relpath(f"{root}/{filename}")

            if _is_ignored(path) or not os.path.isfile(path):
                continue

            with open(path, "rb") as f:
                result[path] = data.hash_object(f.read())

    return result


def merge(other):
    head = data.get_ref("HEAD").value
    assert head

    base = get_merge_base(other, head)
    c_other = get_commit(other)
    ref = data.RefValue(symbolic=False, value=other)

    if base == head:
        read_tree(c_other.tree, update_working=True)
        data.update_ref("HEAD", ref)
        print("Fast-forward merge, no need to commit")
        return

    data.update_ref("MERGE_HEAD", ref)

    c_base = get_commit(base)
    c_head = get_commit(head)
    read_tree_merged(c_base.tree, c_head.tree, c_other.tree, update_working=True)
    print("Merged in working tree\nPlease commit")


def get_merge_base(oid1, oid2):
    parents = list(iter_commits_and_parents({oid1}))

    for oid in iter_commits_and_parents({oid2}):
        if oid in parents:
            return oid


def is_ancestor_of(commit, maybe_ancestor):
    return maybe_ancestor in iter_commits_and_parents({commit})


def add(filenames):
    def add_file(filename):
        filename = os.path.relpath(filename)
        with open(filename, "rb") as f:
            oid = data.hash_object(f.read())
        index[filename] = oid

    def add_directory(dirname):
        for root, _, filenames in os.walk(dirname):
            for filename in filenames:
                path = os.path.relpath(f"{root}/{filename}")
                if _is_ignored(path) or not os.path.isfile(path):
                    continue
                add_file(path)

    with data.get_index() as index:
        for name in filenames:
            if os.path.isfile(name):
                add_file(name)
            elif os.path.isdir(name):
                add_directory(name)


def get_index_tree():
    with data.get_index() as index:
        return index


def _is_ignored(path):
    return ".ugit" in path.split("/")


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

        try:
            os.rmdir(root)
        except (FileNotFoundError, OSError):
            pass


def _is_branch(name):
    return data.get_ref(f"refs/heads/{name}").value is not None


def _checkout_index(index):
    _empty_current_directory()

    for path, oid in index.items():
        os.makedirs(os.path.dirname(f"./{path}"), exist_ok=True)

        with open(path, "wb") as f:
            f.write(data.get_object(oid, "blob"))
