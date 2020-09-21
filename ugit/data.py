import hashlib
import os

from collections import namedtuple


UGIT_DIR = ".ugit"

RefValue = namedtuple("RefValue", ["symbolic", "value"])


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

    first_null = obj.index(b"\x00")
    type = obj[:first_null].decode()
    content = obj[first_null + 1:]

    if expected is not None:
        assert type == expected, f"Expected {expected}, got {type}"

    return content


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
    refs = ["HEAD"]

    for root, _, filenames in os.walk(f"{UGIT_DIR}/refs"):
        root = os.path.relpath(root, UGIT_DIR)
        refs.extend(f"{root}/{name}" for name in filenames)

    for name in refs:
        if not name.startswith(prefix):
            continue

        yield name, get_ref(name, deref=deref)


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
