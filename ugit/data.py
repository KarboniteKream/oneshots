import hashlib
import json
import os
import shutil

from collections import namedtuple
from contextlib import contextmanager


UGIT_DIR = None

RefValue = namedtuple("RefValue", ["symbolic", "value"])


@contextmanager
def change_git_dir(new_dir):
    global UGIT_DIR
    old_dir = UGIT_DIR
    UGIT_DIR = f"{new_dir}/.ugit"
    yield
    UGIT_DIR=old_dir


@contextmanager
def get_index():
    index_path = f"{UGIT_DIR}/index"
    index = {}

    if os.path.isfile(index_path):
        with open(index_path) as f:
            index = json.load(f)

    yield index

    with open(index_path, "w") as f:
        json.dump(index, f)


def init():
    os.makedirs(UGIT_DIR)
    os.makedirs(f"{UGIT_DIR}/objects")
    print(f"Initialized empty uGit repository in {os.getcwd()}/{UGIT_DIR}")


def hash_object(data, type="blob"):
    obj = type.encode() + b"\x00" + data
    oid = hashlib.sha1(obj).hexdigest()

    with open(f"{UGIT_DIR}/objects/{oid}", "wb") as f:
        f.write(obj)

    return oid


def get_object(oid, expected="blob"):
    with open(f"{UGIT_DIR}/objects/{oid}", "rb") as f:
        obj = f.read()

    type, _, content = obj.partition(b'\x00')
    type = type.decode()

    if expected is not None:
        assert type == expected, f"Expected {expected}, got {type}"

    return content


def object_exists(oid):
    return os.path.isfile(f"{UGIT_DIR}/objects/{oid}")


def fetch_object_if_missing(oid, remote_git_dir):
    if object_exists(oid):
        return

    remote_git_dir += "/.ugit"
    shutil.copy(f"{remote_git_dir}/objects/{oid}", f"{UGIT_DIR}/objects/{oid}")


def push_object(oid, remote_git_dir):
    remote_git_dir += "/.ugit"
    shutil.copy(f"{UGIT_DIR}/objects/{oid}", f"{remote_git_dir}/objects/{oid}")


def update_ref(ref, value, deref=True):
    ref = _get_ref(ref, deref=deref)[0]

    assert value.value
    if value.symbolic:
        value = f"ref: {value.value}"
    else:
        value = value.value

    ref_path = f"{UGIT_DIR}/{ref}"
    os.makedirs(os.path.dirname(ref_path), exist_ok=True)

    with open(ref_path, "w") as f:
        f.write(value)


def get_ref(ref, deref=True):
    return _get_ref(ref, deref=deref)[1]


def iter_refs(prefix="", deref=True):
    refs = ["HEAD", "MERGE_HEAD"]

    for root, _, filenames in os.walk(f"{UGIT_DIR}/refs"):
        root = os.path.relpath(root, UGIT_DIR)
        refs.extend(f"{root}/{name}" for name in filenames)

    for name in refs:
        if not name.startswith(prefix):
            continue

        ref = get_ref(name, deref=deref)

        if ref.value:
            yield name, ref


def delete_ref(ref, deref=True):
    ref = _get_ref(ref, deref=deref)[0]
    os.remove(f"{UGIT_DIR}/{ref}")


def _get_ref(ref, deref):
    ref_path = f"{UGIT_DIR}/{ref}"
    value = None

    if os.path.isfile(ref_path):
        with open(ref_path) as f:
            value = f.read().strip()

    symbolic = bool(value) and value.startswith("ref:")

    if symbolic:
        value = value.split(":", 1)[1].strip()
        if deref:
            return _get_ref(value, deref=True)

    return ref, RefValue(symbolic=symbolic, value=value)
