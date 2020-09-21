import hashlib
import os


UGIT_DIR = ".ugit"


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


def update_ref(ref, oid):
    ref_path = f"{UGIT_DIR}/{ref}"
    os.makedirs(os.path.dirname(ref_path), exist_ok=True)

    with open(ref_path, "w") as f:
        f.write(oid)


def get_ref(ref):
    ref_path = f"{UGIT_DIR}/{ref}"

    if os.path.isfile(ref_path):
        with open(ref_path) as f:
            return f.read().strip()


def iter_refs():
    refs = ["HEAD"]

    for root, _, filenames in os.walk(f"{UGIT_DIR}/refs"):
        root = os.path.relpath(root, UGIT_DIR)
        refs.extend(f"{root}/{name}" for name in filenames)

    for name in refs:
        yield name, get_ref(name)
