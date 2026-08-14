libgit2 examples
================

These examples are a mixture of basic emulation of core Git command line
functions and simple snippets demonstrating libgit2 API usage (for use
with Docurium).  As a whole, they are not vetted carefully for bugs, error
handling, and cross-platform compatibility in the same manner as the rest
of the code in libgit2, so copy with caution.

That being said, you are welcome to copy code from these examples as
desired when using libgit2. They have been [released to the public domain][cc0],
so there are no restrictions on their use.

[cc0]: COPYING

For annotated HTML versions, see the "Examples" section of:

    https://libgit2.org/libgit2

such as:

    https://libgit2.org/libgit2/ex/HEAD/general.html

Partial clone and sparse checkout
---------------------------------

`lg2 partial-clone` reads `git-lg2.conf` from the current working directory.
It clones into `test-lg2-clone`, requests the `blob:none` filter and checks
out the configured repository directory using cone-mode sparse checkout.

To create only the configured directory tree, without checking out files or
downloading blobs, use:

    ../build/linux-x86_64-debug/examples/lg2 partial-clone --directories-only

The created empty directories are local placeholders. Git does not track empty
directories, so they are not part of a subsequent commit.

Build and run it from the repository root:

    cmake --build build/linux-x86_64-debug --target lg2
    cd examples
    ../build/linux-x86_64-debug/examples/lg2 partial-clone

For private GitHub repositories, set `username` and `password` in
`git-lg2.conf`; the password must be a personal access token. Protect this
file accordingly, for example with `chmod 600 git-lg2.conf`.
