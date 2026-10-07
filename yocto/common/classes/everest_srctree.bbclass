# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
#
# Copy the EVerest source tree at EVEREST_SRCTREE_DIR into S during do_unpack.
#
# In a git checkout only files known to git are copied (tracked and untracked).
# do_unpack reruns whenever these files change.
# Ignored files, like build directories are not copied.
#
# Paths matching EVEREST_SRCTREE_EXCLUDE are not copied or checksummed,
# unless they also match EVEREST_SRCTREE_KEEP.
# Patterns are in fnmatch format, relative to EVEREST_SRCTREE_DIR,
# where '*' also matches '/'.

EVEREST_SRCTREE_DIR ?= "${@os.path.abspath(d.getVar('EVEREST_CORE_PATH'))}"

EVEREST_SRCTREE_EXCLUDE ?= " \
    .github/* \
    .ci/* \
    .devcontainer/* \
    yocto/* \
    docs/* \
    */docs/* \
    */doc/* \
    tests/* \
    */tests/* \
    */test/* \
    *.bazel \
    *.bzl \
    .bazel* \
    .clang-format \
    .clang-tidy \
    .eslintrc.json \
    .prettierrc.yaml \
    .prospector.yaml \
    AGENTS.md \
    CLAUDE.md \
    CODE_OF_CONDUCT.md \
    CONTRIBUTING.md \
    GOVERNANCE.md \
    README.md \
    RELEASE.md \
    SECURITY.md \
    THIRD_PARTY.md \
"

# installed to ${datadir}/everest
EVEREST_SRCTREE_KEEP ?= "docs/source/reference/EVerest_API/*"
# included unconditionally by the protocol headers
EVEREST_SRCTREE_KEEP += "applications/pionix_chargebridge/shared/protocol/test/*"

# re-parse on every build so that the source tree hash is always current
BB_DONT_CACHE = "1"

do_unpack[file-checksums] += "${@everest_srctree_checksums(d)}"
do_unpack[postfuncs] += "everest_srctree_copy"
do_unpack[vardeps] += "EVEREST_SRCTREE_EXCLUDE EVEREST_SRCTREE_KEEP"

def everest_srctree_git_dir(d):
    import subprocess
    try:
        return subprocess.check_output(['git', '-C', d.getVar('EVEREST_SRCTREE_DIR'), 'rev-parse', '--absolute-git-dir'],
                                       stderr=subprocess.DEVNULL).decode('utf-8').strip()
    except (OSError, subprocess.CalledProcessError):
        return None

def everest_srctree_selector(d):
    import fnmatch
    exclude = (d.getVar('EVEREST_SRCTREE_EXCLUDE') or '').split()
    keep = (d.getVar('EVEREST_SRCTREE_KEEP') or '').split()

    def matches(name, patterns):
        return any(fnmatch.fnmatchcase(name, pattern) for pattern in patterns)

    return lambda name: matches(name, keep) or not matches(name, exclude)

def everest_srctree_checksums(d):
    import hashlib
    import shutil
    import subprocess
    import tempfile

    srcdir = d.getVar('EVEREST_SRCTREE_DIR')
    git_dir = everest_srctree_git_dir(d)
    if git_dir is None:
        return srcdir + '/*:True'

    # a copy of the real index keeps tracked files that .gitignore would hide from 'git add'
    with tempfile.NamedTemporaryFile(prefix='everest-srctree-index') as tmp_index:
        shutil.copyfile(os.path.join(git_dir, 'index'), tmp_index.name)
        env = dict(os.environ, GIT_INDEX_FILE=tmp_index.name)
        subprocess.check_output(['git', 'add', '-A', '.'], cwd=srcdir, env=env)
        output = subprocess.check_output(['git', 'ls-files', '--stage', '-z'], cwd=srcdir, env=env)

    selected = everest_srctree_selector(d)
    entries = [entry for entry in output.decode('utf-8').split('\0') if entry and selected(entry.split('\t', 1)[1])]
    tree_hash = hashlib.sha1('\0'.join(entries).encode('utf-8')).hexdigest()

    hash_file = os.path.join(d.getVar('TMPDIR'), 'everest-srctree', d.getVar('PN') + '.sha1')
    bb.utils.mkdirhier(os.path.dirname(hash_file))
    with open(hash_file, 'w') as f:
        f.write(tree_hash)
    return hash_file + ':True'

def everest_srctree_cp(src, dest):
    import subprocess
    subprocess.check_output(['cp', '--force', '--preserve=timestamps', '--no-dereference', '--recursive', '-H',
                             src, dest], stderr=subprocess.STDOUT)

python everest_srctree_copy() {
    import shutil
    import subprocess

    srcdir = d.getVar('EVEREST_SRCTREE_DIR')
    destdir = d.getVar('S')
    if os.path.normpath(destdir) == os.path.normpath(d.getVar('WORKDIR')):
        bb.fatal('everest_srctree needs S to be a subdirectory of WORKDIR')
    # do_unpack only cleans S in releases before UNPACKDIR was introduced
    bb.utils.remove(destdir, recurse=True)
    bb.utils.mkdirhier(destdir)
    if everest_srctree_git_dir(d) is None:
        everest_srctree_cp(srcdir + '/.', destdir)
        return

    output = subprocess.check_output(['git', '-C', srcdir, 'ls-files', '-z', '--cached', '--others', '--exclude-standard'])
    selected = everest_srctree_selector(d)
    for name in output.decode('utf-8').split('\0'):
        src = os.path.join(srcdir, name)
        if not name or not selected(name) or not os.path.lexists(src):
            continue
        dest = os.path.join(destdir, name)
        bb.utils.mkdirhier(os.path.dirname(dest))
        shutil.copy2(src, dest, follow_symlinks=False)

    # needed by git describe for the version information
    everest_srctree_cp(os.path.join(srcdir, '.git'), destdir)
}
