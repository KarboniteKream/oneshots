import os

import base
import data


LOCAL_REFS_BASE = "refs/remote/"
REMOTE_REFS_BASE = "refs/heads/"


def fetch(remote_path):
    refs = _get_remote_refs(remote_path, REMOTE_REFS_BASE)

    for oid in base.iter_objects_in_commits(refs.values()):
        data.fetch_object_if_missing(oid, remote_path)

    for remote_name, value in refs.items():
        ref = os.path.join(LOCAL_REFS_BASE, os.path.relpath(remote_name, REMOTE_REFS_BASE))
        data.update_ref(ref, data.RefValue(symbolic=False, value=value))


def push(remote_path, name):
    local_ref = data.get_ref(name).value
    assert local_ref

    remote_refs = _get_remote_refs(remote_path)
    remote_ref = remote_refs.get(name)
    assert not remote_ref or base.is_ancestor_of(local_ref, remote_ref)

    known_remote_refs = filter(data.object_exists, remote_refs.values())
    remote_objects = set(base.iter_objects_in_commits(known_remote_refs))
    local_objects = set(base.iter_objects_in_commits({local_ref}))
    objects_to_push = local_objects - remote_objects

    for oid in objects_to_push:
        data.push_object(oid, remote_path)

    with data.change_git_dir(remote_path):
        data.update_ref(name, data.RefValue(symbolic=False, value=local_ref))


def _get_remote_refs(remote_path, prefix=""):
    with data.change_git_dir(remote_path):
        return {name: ref.value for name, ref in data.iter_refs(prefix)}
