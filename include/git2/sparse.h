/*
 * Copyright (C) the libgit2 contributors. All rights reserved.
 *
 * This file is part of libgit2, distributed under the GNU GPL v2 with
 * a Linking Exception. For full terms see the included COPYING file.
 */

#ifndef INCLUDE_git_sparse_h__
#define INCLUDE_git_sparse_h__

#include "common.h"
#include "repository.h"
#include "strarray.h"

/**
 * @file git2/sparse.h
 * @brief Sparse checkout configuration
 * @defgroup git_sparse Sparse checkout configuration
 * @ingroup Git
 * @{
 */
GIT_BEGIN_DECL

/**
 * Configure cone-mode sparse checkout directories.
 *
 * This writes Git-compatible cone-mode patterns to
 * `$GIT_DIR/info/sparse-checkout` and enables both
 * `core.sparseCheckout` and `core.sparseCheckoutCone`.
 *
 * Directory names must be non-empty, relative paths using forward
 * slashes. The root-level files of the working tree are always
 * included, as required by cone-mode sparse checkout.
 *
 * This function only persists sparse-checkout state. Applying that
 * state to the index and working directory is performed separately.
 *
 * @param repo repository to configure; must not be bare
 * @param directories directories to include in the cone
 * @return 0 on success, or an error code
 */
GIT_EXTERN(int) git_sparse_checkout_set(
	git_repository *repo,
	const git_strarray *directories);

/** @} */
GIT_END_DECL

#endif
