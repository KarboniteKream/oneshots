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