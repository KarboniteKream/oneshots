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


def _get_remote_refs(remote_path, prefix=""):
    with data.change_git_dir(remote_path):
        return {name: ref.value for name, ref in data.iter_refs(prefix)}
