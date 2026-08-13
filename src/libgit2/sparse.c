/*
 * Copyright (C) the libgit2 contributors. All rights reserved.
 *
 * This file is part of libgit2, distributed under the GNU GPL v2 with
 * a Linking Exception. For full terms see the included COPYING file.
 */

#include "git2/sparse.h"

#include "repository.h"
#include "config.h"
#include "filebuf.h"
#include "path.h"

static int validate_directory(git_repository *repo, const char *directory)
{
	const char *component, *slash;

	if (!directory || !*directory || directory[0] == '/' ||
	    directory[strlen(directory) - 1] == '/' ||
	    strchr(directory, '\\') || strstr(directory, "//")) {
		git_error_set(
		        GIT_ERROR_INVALID,
		        "sparse checkout directory must be a relative path");
		return GIT_EINVALID;
	}

	for (component = directory; component; component = slash + 1) {
		slash = strchr(component, '/');

		if ((slash && slash == component) ||
		    (!slash && component[0] == '\0') ||
		    ((slash ? (size_t)(slash - component) :
		              strlen(component)) == 1 &&
		     component[0] == '.') ||
		    ((slash ? (size_t)(slash - component) :
		              strlen(component)) == 2 &&
		     component[0] == '.' && component[1] == '.')) {
			git_error_set(
			        GIT_ERROR_INVALID,
			        "sparse checkout directory contains an invalid component");
			return GIT_EINVALID;
		}

		if (!slash)
			break;
	}

	if (!git_path_is_valid(
	            repo, directory, GIT_FILEMODE_TREE,
	            GIT_PATH_REJECT_WORKDIR_DEFAULTS)) {
		git_error_set(
		        GIT_ERROR_INVALID,
		        "invalid sparse checkout directory '%s'", directory);
		return GIT_EINVALID;
	}

	return 0;
}

static int
write_cone_patterns(git_filebuf *file, const git_strarray *directories)
{
	size_t i;
	int error;

	if ((error = git_filebuf_write(file, "/*\n!/*/\n", 8)) < 0)
		return error;

	for (i = 0; i < directories->count; i++) {
		const char *directory = directories->strings[i];
		const char *slash;
		size_t length;

		for (slash = directory;; slash++) {
			if (*slash != '/' && *slash != '\0')
				continue;

			length = (size_t)(slash - directory);

			if ((error = git_filebuf_printf(
			             file, "/%.*s/\n", (int)length,
			             directory)) < 0)
				return error;

			if (*slash == '\0')
				break;

			if ((error = git_filebuf_printf(
			             file, "!/%.*s/*/\n", (int)length,
			             directory)) < 0)
				return error;
		}
	}

	return 0;
}

int git_sparse_checkout_set(
        git_repository *repo,
        const git_strarray *directories)
{
	git_config *config;
	git_filebuf file = GIT_FILEBUF_INIT;
	git_str path = GIT_STR_INIT;
	size_t i;
	int error = 0;
	bool file_open = false;

	GIT_ASSERT_ARG(repo);
	GIT_ASSERT_ARG(directories);

	if (git_repository_is_bare(repo)) {
		git_error_set(
		        GIT_ERROR_REPOSITORY,
		        "cannot configure sparse checkout in a bare repository");
		return GIT_EBAREREPO;
	}

	if (directories->count && !directories->strings)
		return GIT_EINVALID;

	for (i = 0; i < directories->count; i++) {
		if ((error = validate_directory(
		             repo, directories->strings[i])) < 0)
			goto done;
	}

	if ((error = git_repository__item_path(
	             &path, repo, GIT_REPOSITORY_ITEM_INFO)) < 0 ||
	    (error = git_str_joinpath(&path, path.ptr, "sparse-checkout")) <
	            0 ||
	    (error = git_filebuf_open(
	             &file, path.ptr, GIT_FILEBUF_CREATE_LEADING_DIRS, 0666)) <
	            0)
		goto done;

	file_open = true;

	if ((error = write_cone_patterns(&file, directories)) < 0 ||
	    (error = git_filebuf_commit(&file)) < 0)
		goto done;

	file_open = false;

	if ((error = git_repository_config__weakptr(&config, repo)) < 0 ||
	    (error = git_config_set_bool(config, "core.sparsecheckout", true)) <
	            0 ||
	    (error = git_config_set_bool(
	             config, "core.sparsecheckoutcone", true)) < 0)
		goto done;

done:
	if (file_open)
		git_filebuf_cleanup(&file);

	git_str_dispose(&path);
	return error;
}
